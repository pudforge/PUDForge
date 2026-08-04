// Command-button icons, rasterised once and handed round.
//
// The artwork is one GRP per tileset holding 196 frames of 46x38, the same sheet
// the game draws its command buttons from. The unit palette, the property
// sheets' lists and the quick pick all want frames out of it, and they used to
// want them from three different places — which is how the sheets ended up with
// no icons at all: they have no GameData to ask.
//
// So the sheet is opened once per tileset and kept here, and everything that
// draws an icon draws it through BlitIcon.

#pragma once

#include <windows.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "pudforge/pudforge.h"

namespace pfwin {

class GameData;

/// A rasterised icon: RGBA pixels on its own canvas, scaled at draw time.
struct Icon {
  int w = 0, h = 0;
  /// Fill the cell edge to edge rather than fitting inside it with a margin.
  ///
  /// Set for portraits, which arrive 46x38 and are cropped to their middle
  /// square: a slightly cropped face reads better than a squashed one. Never set
  /// for a unit's own sprite, where cropping would cut off the wing or the tail
  /// that is often the only way to tell two units apart.
  bool fill = false;
  std::vector<uint32_t> px;
  bool empty() const { return px.empty(); }
};

/// Draw an icon into a cell, nearest-neighbour, because it is pixel art.
/// `inset` is pixels of margin on every side, which a filling icon ignores.
void BlitIcon(HDC dc, const RECT& rect, const Icon& icon, int inset = 2);

/// A solid flat-colour icon, for when there is no artwork to draw.
Icon FlatIcon(uint32_t rgb);

/// The owner an icon is drawn in when nobody says otherwise.
///
/// Player 15 is the neutral slot, which is right for the lists that ask what a
/// thing *is*. The unit palette is the exception: it is about what the next
/// click will place.
constexpr int kNeutralOwner = 15;

// ------------------------------------------------------- lists with icons
//
// A name is the slowest way to recognise a unit. Owner-drawing changes how a row
// is *painted* and nothing else: with LBS_/CBS_HASSTRINGS the control keeps its
// strings, so selection, keyboard navigation, multi-select and LB_GETTEXT all
// still work and a sheet gains two message handlers rather than a rewrite.
//
// Shared by the listboxes on the property sheets and the combo boxes on the
// bulk-edit sheets, which is why it is here and not in either.

/// Row height at 96 DPI for a list, and for a combo box's rows.
///
/// A list gets the taller one: 40 is a portrait's own 38 pixels with a hair of
/// margin, so the artwork is drawn at its own size instead of halved. Half of
/// that was enough when the icon was a hint beside a name; it is not enough when
/// the icon is how you find the row.
///
/// A combo keeps the short one, because its closed height comes from the dialog
/// template and a taller item would overlap what is underneath.
constexpr int kListRowBase = 40;
constexpr int kIconRowBase = 20;
int IconRowHeight(HWND reference);

/// What the property sheets draw a changed thing in.
///
/// A colour rather than the dot that used to sit in the form's mark column: what
/// a mapper wants off a page of thirty numbers is to see at a glance which are
/// not the game's. The same blue Windows uses for a hyperlink, which reads as
/// "not like the others" without reading as an error.
constexpr COLORREF kChangedInk = RGB(0, 90, 200);

/// Answer WM_MEASUREITEM for any owner-drawn list or combo on a sheet.
/// Sent before WM_INITDIALOG, so it must not lean on the sheet existing.
INT_PTR MeasureIconRow(HWND dialog, LPARAM lparam);

/// One row: a square icon cell, then whatever text the control was given. The
/// control still owns the selection; this only paints it, which is why the
/// colours come from the system.
///
/// `changed` marks a row this map has moved away from the game's own table, in
/// the same blue the property form uses, plus a star — the form could only say
/// it about the unit already open, which meant finding the changed ones was
/// opening all hundred and ten, and a colour alone is not a mark everyone sees.
INT_PTR DrawIconRow(LPARAM lparam, const Icon* icon, bool changed = false);

/// The portrait sheet for one tileset, and whatever has been asked of it.
///
/// Lazy: 196 frames times four tilesets is a lot of memory for a list that shows
/// twelve rows. Frames are kept once made, since a list scrolling back over
/// itself asks for the same ones repeatedly.
class IconCache {
 public:
  ~IconCache() { Close(); }

  /// Point at a tileset's artwork. Cheap to call with the same arguments —
  /// only a change of tileset or artwork throws the rasterised frames away.
  void Open(GameData* game, const pf_tileset_art* art, int tileset);
  void Close();

  bool ready() const { return sheet_ != nullptr; }
  int tileset() const { return tileset_; }

  /// Draw units as their own sprite rather than their command-button icon.
  ///
  /// The icons read far better at this size — they were drawn to be read at it —
  /// but they are the *game's* icons, and a mapper who thinks in terms of what
  /// will be on the map may want the top-down sprite. Changing it throws the
  /// rasterised units away and keeps the frames, which the upgrade lists share.
  void SetPreferSprites(bool on);
  bool prefer_sprites() const { return prefer_sprites_; }

  /// One frame of the portrait sheet, or an empty icon. Upgrades index the sheet
  /// directly — `UGRD` stores a frame number per upgrade — so this is the
  /// general way in.
  const Icon& Frame(int frame);

  /// One unit's button icon: its portrait frame where the mapping knows one,
  /// its own sprite for the fourteen that have none, empty for the rest.
  ///
  /// `owner` colours the player-colour ramp in the artwork. It defaults to
  /// neutral, and the cache is keyed by it, so a palette that follows the chosen
  /// player costs one rasterisation per player actually used — mappers switch
  /// back and forth while laying out two bases.
  const Icon& Unit(int unit_id, int owner = kNeutralOwner);

  /// One unit's own top-down sprite, whatever `prefer_sprites` says.
  /// Unit() answers what a *list* should show, which for most units is the
  /// command-button icon. The canvas asks a different question — what will be
  /// standing on the map — and only the sprite answers it, so the two are cached
  /// apart.
  const Icon& Sprite(int unit_id, int owner = kNeutralOwner);

 private:
  const Icon& Nothing() const { return nothing_; }

  GameData* game_ = nullptr;
  const pf_tileset_art* art_ = nullptr;
  int tileset_ = -1;
  pf_sprite* sheet_ = nullptr;
  std::unordered_map<int, Icon> frames_;
  std::unordered_map<int, Icon> units_;
  std::unordered_map<int, Icon> sprites_;
  bool prefer_sprites_ = false;
  Icon nothing_;
};

}  // namespace pfwin
