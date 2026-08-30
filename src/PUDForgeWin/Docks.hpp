// The tool panels beside the map: terrain on the left, units on the right.
//
// Everything here is a stock control or a PaletteGrid. The segmented rows the
// web client hand-built - Detail, Shape, Mirror - are BS_PUSHLIKE buttons:
// auto-radio for the exclusive ones, auto-checkbox for the mirrors, which
// combine. Stock, themed, keyboard-navigable (reference/docs/windows-ui.md).

#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

#include "Editor.hpp"
#include "Host.hpp"
#include "Icons.hpp"
#include "PaletteGrid.hpp"
#include "UiIcons.hpp"
#include "pudforge/pudforge.h"

namespace pfwin {

class GameData;

/// What AskDockMenu returns when it did not return a sizing mode.
///
/// A dock's right-click menu answers two unrelated questions — how the palette
/// is sized, and which side of the canvas this lives on — because there is only
/// one right button. A sizing answer comes back as the palette's own mode, so
/// the caller has nothing to translate; these three are the rest.
constexpr int kDockMenuDismissed = -1;
constexpr int kDockMenuLeft = -3;
constexpr int kDockMenuRight = -4;
/// The caller's own item, when it passed one, was picked.
constexpr int kDockMenuExtra = -5;

/// `pinned` for a dock with no palette at all. The minimap passes it and gets
/// the side items and nothing else.
constexpr int kDockMenuNoColumns = -1;

/// The right-click menu the two palettes and the minimap share.
///
/// `pinned` is the palette's current sizing mode, or `kDockMenuNoColumns`.
///
/// `extra` is one item about whatever the pointer is over, put above the panel's
/// own settings and returned as `kDockMenuExtra`: the right button over a grid
/// of units should first offer to do something with the unit it is over. Null
/// for the panels that have nothing to name.
int AskDockMenu(HWND owner, POINT at, int pinned, bool on_right,
                const wchar_t* extra = nullptr);

/// Every unit a palette offers, in the order the units dock lays them out.
///
/// One race's land, air, water, buildings and heroes, then the other's, then
/// the neutral units, then the start-location markers. `lead` is 'o' to put the
/// orc sections first.
///
/// Shared with the quick pick: the two showing the same set in two different
/// orders would make a search feel like a different program from the grid.
/// `with_unused` is Editor::offer_unused_units. Passed rather than defaulted,
/// so a caller has to decide: a search that quietly listed a different set from
/// the grid is the disagreement this function exists to prevent.
std::vector<int> UnitsInPaletteOrder(char lead, bool with_unused);

/// The heading a unit sits under. Derived from the unit rather than looped over,
/// so the sections and the running order above cannot disagree about where one
/// of them ends.
std::wstring UnitGroupHeading(int id);

/// The terrain panel: brush palette, then Detail / Shape / Size / Mirror.
class TerrainPanel {
 public:
  static bool Register(HINSTANCE instance);
  HWND Create(HWND parent, HINSTANCE instance, Editor* editor, Host* host);
  HWND hwnd() const { return hwnd_; }

  /// Artwork for the brush icons; null falls back to flat terrain colours.
  void SetArtwork(const pf_tileset_art* art, int tileset);

  /// Put the sheet's drawings on the Detail, Shape, Mirror and Shade rows.
  ///
  /// Those rows have always been glyphs — L"■", L"↔" — which is what makes them
  /// readable once you know them and unreadable the first time. A drawn cell
  /// replaces the glyph; an undrawn one leaves it, so the sheet can be filled in
  /// a row at a time. Borrowed.
  void SetUiIcons(const UiIcons* icons);

  /// Push the editor's state into the controls, e.g. after PickBrush.
  void Refresh();

  /// How many tiles per row, or one of the two automatic modes. Kept between
  /// runs by the application, which owns the settings — so the numbers are
  /// stored ones, and 0 has to go on meaning what it meant.
  int columns() const { return palette_.column_count(); }
  void SetColumns(int columns) { palette_.SetColumnCount(columns); }

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
  void Build();
  void Layout();
  void OnCommand(int id);
  /// Put the brush back in hand after a row of this panel is used.
  void ArmTheBrush();
  void DrawBrushIcon(HDC dc, const RECT& rect, int brush);
  void RebuildPalette();
  /// The movement half of it: the eight classes plus the cell that puts a tile
  /// back to what its terrain implies.
  void RebuildMovementPalette();
  /// Which movement cell to show as chosen, or -1 when the bits have been taken
  /// somewhere no class has a name for.
  int SelectedMovementCell() const;
  /// Open the tile picker for the custom cell. False when nothing was chosen.
  bool PickCustomTile();
  /// Give a control a tooltip. One tooltip window for the whole panel.
  void Explain(HWND control, UINT text);
  /// The same, for a tip that has to be built rather than looked up: the bit
  /// toggles name the bit they stand for and show its value.
  void ExplainWith(HWND control, const std::wstring& text);
  /// The palette's right-click menu: how the cells are sized, and which side.
  void PickColumns(POINT screen);

  HWND hwnd_ = nullptr;
  HINSTANCE instance_ = nullptr;
  Editor* editor_ = nullptr;
  /// Which list the palette currently holds. A mode change swaps it for the
  /// other one, and nothing else does.
  Mode palette_mode_ = Mode::kTerrain;
  Host* host_ = nullptr;
  PaletteGrid palette_;
  /// No tool buttons. Painting is what clicking a palette cell means,
  /// selecting a rectangle is on the strip along the top, and the eyedropper
  /// is Ctrl and the left button — which the status bar says out loud whenever
  /// the brush is in hand, so it needs no button to keep in step with it.
  HWND detail_[3] = {};
  HWND shape_[4] = {};
  HWND size_slider_ = nullptr;
  HWND size_value_ = nullptr;
  HWND mirror_[5] = {};
  /// Light, Dark and Mix: one question — which drawing of the terrain does a
  /// stroke lay — with three exclusive answers. The palette carries one cell
  /// per terrain rather than one per drawing, which is why the answer is here.
  HWND shade_[3] = {};

  /// Fill and Clear, shown only while a terrain rectangle exists — a button
  /// that acts on something the user cannot see is worse than a missing one.
  /// Replace and Decorate are always here, because their scope is the map when
  /// there is no rectangle and so they mean something either way.
  HWND bulk_[2] = {};

  HWND labels_[5] = {};   ///< Detail, Shape, Size, Mirror, Shade
  HWND brush_name_ = nullptr;     ///< what is selected, under the palette
  /// One tooltip for the panel, moved between controls as the pointer goes.
  ///
  /// The shape, mirror and detail rows are glyphs — which is what makes them
  /// readable at a glance once you know them, and unreadable the first time.
  HWND tip_ = nullptr;
  /// The tooltip texts, kept alive because the control stores a pointer and
  /// reads it back when the pointer settles. A deque rather than a vector: its
  /// elements do not move when it grows.
  std::deque<std::wstring> tip_texts_;
  const pf_tileset_art* art_ = nullptr;
  int tileset_ = 0;
  /// One 32x32 tile per brush, drawn from the artwork once per tileset.
  ///
  /// Indexed by brush, including the dark members the palette no longer shows
  /// a cell for: the cell's icon follows the shade switch, so both drawings
  /// have to be to hand.
  std::vector<Icon> icons_;
};

/// The units panel: player picker over the unit palette.
class UnitsPanel {
 public:
  static bool Register(HINSTANCE instance);
  HWND Create(HWND parent, HINSTANCE instance, Editor* editor, Host* host);
  HWND hwnd() const { return hwnd_; }

  /// Sprites for the unit icons come from the game data; null draws names.
  /// The cache is shared with the property sheets and the quick pick, so a
  /// frame is rasterised once no matter how many of them ask for it.
  void SetArtwork(IconCache* icons, const pf_tileset_art* art, int tileset);
  /// The one drawing this panel wants: the button beside the player dropdown
  /// that opens the rest of that player. Borrowed.
  void SetUiIcons(const UiIcons* icons);
  void Refresh();

  /// How many tiles per row, or one of the two automatic modes.
  int columns() const { return palette_.column_count(); }
  void SetColumns(int columns) { palette_.SetColumnCount(columns); }

  /// Build the cells again, names and all.
  ///
  /// Public because the names are the game's, not ours: changing the game
  /// folder can change the language they are in, and the palette holds the
  /// labels it was built with rather than asking for them on every paint.
  void RebuildPalette();

 private:
  /// Which race's sections come first: the chosen player's, or 0 for the
  /// fixed order used when every race is on show.
  char LeadingRace() const;
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
  void Build();
  void Layout();
  /// Refill the player dropdown, naming each slot's race as well as its
  /// colour. The race is the map's, so it is re-read whenever the map may have
  /// changed rather than filled once at build time.
  void RefreshOwnerCombo();
  void DrawUnitIcon(HDC dc, const RECT& rect, int unit);
  /// Which player's colours this cell is drawn in — the chosen one, except
  /// for the scenery the core places neutral whatever is chosen.
  int PaletteOwner(int unit) const;

  HWND hwnd_ = nullptr;
  HINSTANCE instance_ = nullptr;
  Editor* editor_ = nullptr;
  Host* host_ = nullptr;
  /// The player the cells were last painted in, so Refresh repaints on a
  /// change of player and not on every edit.
  int shown_owner_ = -1;
  /// The races the dropdown was last filled for, so it is only refilled when
  /// one has actually moved — refilling a combo closes its list.
  int shown_races_[PF_PLAYER_COUNT] = {};
  bool owner_combo_filled_ = false;
  /// What the palette was last *built* for, as opposed to painted for: the
  /// chosen player's race and the show-every-race option decide which cells
  /// exist, and neither is something an edit changes often.
  int palette_race_ = -1;
  bool palette_all_races_ = false;
  bool palette_unused_ = false;
  /// The palette's right-click menu: what to do with the unit under the
  /// pointer, then how the cells are sized and which side the dock is on.
  /// `unit` is -1 when the click did not land on a cell.
  void PickColumns(POINT screen, int unit = -1);

  HWND owner_combo_ = nullptr;
  /// Beside the dropdown: the player sheet, where everything else about the
  /// slot lives. Choosing who the next unit is for and setting up who that
  /// player *is* are one train of thought.
  HWND owner_props_ = nullptr;
  PaletteGrid palette_;
  IconCache* icons_ = nullptr;   ///< borrowed
  const pf_tileset_art* art_ = nullptr;
  int tileset_ = 0;
};

}  // namespace pfwin
