// The canvas: the child window the map is drawn in.
//
// It owns paint, mouse, keyboard and scroll, and nothing else. Everything it
// shows comes from one call to pf_map_compose_region, which hands back a packed
// BGRA buffer with the terrain, the overlays, the units and the grid already in
// it - so this file has no drawing in it beyond getting that buffer on screen
// and putting the pointer-following marks on top: the brush outline, the
// placement ghost, the rubber band, the selection rectangles.
//
// Where the window is looking lives in pf::View, which is arithmetic and is
// tested. What a click *means* lives in pfwin::Editor, which is also tested.
// What is left here is Win32.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Editor.hpp"
#include "Host.hpp"
#include "pudforge/pudforge.h"
#include "view.hpp"

namespace pfwin {

class IconCache;

/// What a drag is doing. Set on button-down, cleared on button-up.
enum class Drag {
  kNone,
  kPaint,     ///< one stroke, one undo step
  kSpray,     ///< a held scatter brush, on its own timer
  kPlace,     ///< laying units along a drag, one undo step
  kRect,      ///< dragging out the terrain rectangle
  kBand,      ///< rubber-band unit selection
  kMove,      ///< dragging the selection
  kPan,       ///< middle button
};

class MapWindow {
 public:
  static const wchar_t* kClassName;
  /// Registers the window class. Call once, before creating any.
  static bool Register(HINSTANCE instance);

  MapWindow() = default;
  ~MapWindow();

  HWND hwnd() const { return hwnd_; }
  pf_map* map() const { return map_; }

  /// The editor the mouse drives, and who to tell when it edits.
  void SetEditor(Editor* editor, Host* host) { editor_ = editor; host_ = host; }

  /// Where the placement ghost gets its artwork. Borrowed, and the cache the
  /// palette and the property sheets already share: it knows the tileset and
  /// the artwork, and it throws its rasterisations away when either changes,
  /// which is exactly what a ghost needs and none of it belongs here.
  void SetIcons(IconCache* icons) { icons_ = icons; }

  /// Takes ownership of `map`.
  void SetMap(pf_map* map);
  void SetArtwork(pf_tileset_art* art, pf_sprite_set* sprites);

  /// Something changed that is not bounded by a rectangle — undo, a dialog, a
  /// menu action. Any pending patch is dropped with it: keeping one would let a
  /// partial compose stand in for one that has to be whole, which is how an
  /// undo would repaint only the tiles the stroke it undid had touched.
  void MarkMapChanged() {
    patch_x1_ = patch_x0_ - 1;
    dirty_ = true;
    Invalidate();
  }

  /// A change bounded by a rectangle of tiles — a brush stroke, mostly.
  ///
  /// Composing the whole visible region costs 13 ms on a 128x128 map zoomed out
  /// to fit, because it is composed at the artwork's 32 px per tile whatever the
  /// zoom. The tiles that changed are known, and the rest of the buffer is still
  /// correct.
  void MarkTilesChanged(int x, int y, int w, int h);

  /// The same map, a different shape. Not the same as MarkMapChanged: the
  /// view's idea of how big the map is has to change with it, and where it was
  /// looking is a rectangle of a grid that no longer exists.
  void MapSizeChanged();

  /// Cycle the water palette, at roughly the game's own pace.
  ///
  /// Each step rotates ten palette entries the artwork shares, so the cost is
  /// a redraw of what is on screen rather than of the map — which is why this
  /// belongs to the canvas and not to the map.
  void SetWaterAnimated(bool on);
  bool water_animated() const { return water_animated_; }

  /// Fit the whole map, or only shrink it - see pf::view_fit.
  void Fit(bool only_to_shrink = false);
  void ZoomStep(int direction);
  void ZoomTo(int zoom);
  /// Frame a tile rectangle: as close in as it fits, centred on it. What
  /// "zoom to the selection" means, and the reason view_fit_rect exists.
  void ZoomToTiles(int x, int y, int w, int h);
  void CentreOn(int tx, int ty);

  /// Whether a unit's facing varies with where it stands. A property of the
  /// drawing rather than of the map, so it does not go through the editor —
  /// and one worth turning off, because two captures of one map can only be
  /// compared when every unit faces the same way.
  void SetVaryFacing(bool on);

  /// Draw how far the selected units see and shoot. Off by default: it is a
  /// question you ask about a tower, not something to have on all the time.
  void SetShowReach(bool on) { show_reach_ = on; Invalidate(); }
  bool show_reach() const { return show_reach_; }
  void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }

  /// Put down whichever tool is armed: the terrain brush, or the unit about
  /// to be placed. That is what Escape and the right button both mean, and
  /// which one is in hand is not something the caller should have to ask.
  ///
  /// Escape reaches this through the menu command Esc is bound to; the right
  /// button reaches it from this window. Says so on the status bar, since a
  /// mode that changes in silence is one the next click discovers.
  ///
  /// @return true when the tool changed, in which case the right button has
  /// been spoken for and must not also raise the context menu
  bool LeaveActiveTool();

  const pf::View& view() const { return view_; }

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void OnPaint();
  void DrawOverlays(HDC dc);
  /// One unit drawn as a proposal: blended, and tinted red when whatever is
  /// proposing it would be refused. The placement ghost, the paste preview and
  /// the refused-move ghost are all this picture.
  void DrawUnitGhost(HDC dc, int type, int owner, int ox, int oy, int fw,
                     int fh, bool ok);
  void DrawPlacementGhost(HDC dc, int type, int ox, int oy, int fw, int fh,
                          bool ok);
  /// Where a drag is trying to put the selection, when the move is refused.
  void DrawRefusedMove(HDC dc);
  /// What an armed paste is about to drop, drawn under the pointer: the
  /// fragment's terrain, and each unit in it. An outline alone said how big
  /// the fragment was and nothing about what was in it, which is the question
  /// when it was copied several edits ago and rotated twice since.
  void DrawClipboardTerrain(HDC dc, const pf_clipboard* clip, const TileRect& frag);
  void DrawClipboardUnit(HDC dc, const pf_unit& unit, int fw, int fh, bool ok);
  /// Rasterise the fragment once per fragment rather than once per mouse move:
  /// it is the same picture wherever it is about to land.
  void BuildFragmentPixels(const pf_clipboard* clip, const TileRect& frag);
  /// The terrain selection's outline, as a region the caller owns. Only used
  /// when the selection is not a plain rectangle, which is the case shift and
  /// alt dragging can produce.
  HRGN SelectionRegion(const TileRect& box) const;
  void DrawReach(HDC dc);
  void DrawSymmetryAxes(HDC dc);
  void OnSize(int width, int height);
  void OnMouseDown(int button, int x, int y, WPARAM keys);
  void OnMouseMove(int x, int y, WPARAM keys);
  void OnMouseUp();
  void OnWheel(int delta, int x, int y, WPARAM keys);
  /// Which system cursor says what a click here would do.
  const wchar_t* CursorForTool() const;
  /// Drop the off-screen bitmap the canvas draws into. Rebuilt on next paint.
  void ReleaseBackBuffer();
  void OnKey(WPARAM key);
  /// Alt, held, borrows the other shade of the terrain being painted.
  /// @return whether anything moved, so the caller can stop looking
  bool ApplyShiftShade(bool down);
  /// Re-derive the hovered tile after the map moved under a still pointer.
  void RefreshHoverFromCursor();
  void OnTimer(UINT_PTR id);

  /// A tile rectangle in viewport pixels.
  RECT TileRectToScreen(int tx, int ty, int w, int h) const;
  /// One painted step of the active stroke, at the hover tile.
  void StrokeAt(int tx, int ty);
  /// Lay the brush where the pointer is, whichever kind of brush it is.
  ///
  /// The corner brush is aimed in corner coordinates taken from the pointer's
  /// pixels; every other size is aimed at the tile. Routing both through one
  /// place is what keeps click, drag and spray from each needing the test.
  bool PaintPointer(int tx, int ty);
  void EndEditDrag();

  /// Recompose only when something changed. Scrolling changes the region, so
  /// it counts; a repaint from another window being dragged over does not.
  void Compose();

  HWND hwnd_ = nullptr;
  pf_map* map_ = nullptr;
  pf_tileset_art* art_ = nullptr;      ///< borrowed
  pf_sprite_set* sprites_ = nullptr;   ///< borrowed
  Editor* editor_ = nullptr;           ///< borrowed
  Host* host_ = nullptr;               ///< borrowed
  IconCache* icons_ = nullptr;         ///< borrowed

  pf::View view_;
  Drag drag_ = Drag::kNone;
  POINT drag_from_{};
  /// Where the pointer is during a held-middle-button pan, and when the pan
  /// timer last ran. The anchor it is measured against is drag_from_.
  POINT pan_pointer_{};
  DWORD pan_tick_ = 0;
  DWORD spray_since_ = 0;
  /// The last tile a stroke painted, so a slow drag inside one tile does not
  /// repaint it, and the anchor tile of a band or rectangle drag.
  int last_tx_ = -1, last_ty_ = -1;
  int anchor_tx_ = 0, anchor_ty_ = 0;
  /// What a terrain-rectangle drag is doing to the selection, and what was
  /// selected before it started. Both are decided on button-down: reading the
  /// modifier keys again on every mouse move would let a drag change meaning
  /// halfway through, and re-applying to the live selection rather than to the
  /// one the drag began with would leave every tile the pointer ever crossed
  /// in it.
  Editor::Pick rect_pick_ = Editor::Pick::kReplace;
  std::vector<uint8_t> rect_before_;
  /// Where the pointer is, in tiles, for the brush outline and the ghost.
  int hover_tx_ = -1, hover_ty_ = -1;
  /// And in viewport pixels. The corner brush aims at the nearest grid corner,
  /// which the tile alone cannot say: both corners of a tile round to it.
  int hover_px_ = 0, hover_py_ = 0;
  /// Whether a kMove drag has taken its checkpoint yet: the first tile of
  /// movement takes it, so a click that never moves costs no undo step.
  bool move_took_checkpoint_ = false;
  /// How far past the last tile it could reach a kMove drag has been asked to
  /// go. Zero whenever the selection is where the pointer is; non-zero while
  /// the move is being refused, which is what DrawRefusedMove draws.
  int move_pending_dx_ = 0, move_pending_dy_ = 0;
  /// The refusal this drag has already said, so dragging along a coastline
  /// says "that is on the map edge" once rather than once per tile. The log
  /// keeps every line the status bar shows, and forty copies of one sentence
  /// is how a log stops being worth opening.
  std::string move_refusal_said_;
  /// Whether the right press already meant something — cancelling a paste,
  /// putting the brush down — so button-up must not also raise the menu.
  /// Set on button-down, where those decisions are made and where the state
  /// they read is still the state the user pressed against.
  bool right_press_consumed_ = false;

  /// The composed region, and what it was composed for.
  /// The render options for a region, filled from the editor's view state.
  pf_render_options ComposeOptions(int x0, int y0, int cols, int rows) const;
  /// Recompose just the tiles in `patch_`, into the buffer already composed.
  /// False when anything else about the view moved, and on the first paint.
  bool ComposePatch();

  /// The off-screen bitmap every paint goes through, sized to the client.
  HDC back_dc_ = nullptr;
  HBITMAP back_bitmap_ = nullptr;
  HGDIOBJ back_old_ = nullptr;
  int back_w_ = 0, back_h_ = 0;

  std::vector<uint32_t> pixels_;
  std::vector<uint32_t> patch_pixels_;   ///< scratch for one partial compose
  /// The pending paste's terrain, rasterised. Kept until the fragment is
  /// replaced or turned — which is what the revision catches, and what a bare
  /// pointer comparison would not: a freed fragment and its replacement can
  /// share an address.
  std::vector<uint32_t> frag_pixels_;
  int frag_w_ = 0, frag_h_ = 0;
  int frag_revision_ = -1;
  /// Whether that buffer is the artwork at 32 px a tile or the flat corner
  /// colours at 2. It decides what the blit's source size is.
  bool frag_art_ = false;
  /// Tiles known to have changed since the last compose, or empty for "all of
  /// it". Unioned rather than queued: two dabs a tile apart are one patch.
  int patch_x0_ = 0, patch_y0_ = 0, patch_x1_ = -1, patch_y1_ = -1;
  int composed_x0_ = -1, composed_y0_ = -1, composed_cols_ = 0, composed_rows_ = 0;
  int composed_zoom_ = -1;
  /// Which layer the last composition was drawn with.
  ///
  /// Part of the cache key rather than something a caller marks, because the
  /// visible layer follows the mode and the mode is set from half a dozen
  /// places. Issue #2 was those places not knowing they had to say so.
  int composed_overlay_ = -1;
  /// Set by anything that edits the map, so the next paint recomposes. The
  /// editor's revision counter is compared too; this catches edits that
  /// bypass it (see MarkMapChanged).
  bool dirty_ = true;
  int composed_revision_ = -1;

  bool water_animated_ = false;
  int water_phase_ = 0;
  bool vary_facing_ = true;
  bool show_reach_ = false;
};

}  // namespace pfwin
