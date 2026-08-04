// A scrollable grid of icon cells with headings between runs.
//
// Used twice — the terrain palette and the unit palette — which is the whole
// argument for it existing (reference/docs/windows-ui.md). A ListView in icon mode is the
// obvious reach and the wrong one: the headings and the exact N-per-row fit
// fight it the whole way.
//
// The control knows nothing about maps. The owner names the entries and draws
// the icons; the control owns layout, scroll, hover and the pick.

#pragma once

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <string>
#include <vector>

namespace pfwin {

/// The two automatic sizing modes, either side of a real column count.
///
/// The same question answered from opposite ends. **Fit** holds a column count
/// taken from the width and lets the cell size follow, so the row is always
/// spent exactly. **Scale** holds the cell at the artwork's own size — a whole
/// DPI multiple of one artwork pixel — and lets the count follow, at the price
/// of a margin where the division did not come out even.
///
/// Fit is the default: a texture wants to be big enough to recognise the pattern
/// in. Scale is for the mapper who would rather see more tiles, undistorted.
constexpr int kPaletteFitPanel = 0;
/// -2 rather than -1 because these travel to `AskDockMenu` as its `pinned`
/// argument, and -1 there already means a dock with no palette at all.
constexpr int kPaletteScaleTiles = -2;

class PaletteGrid {
 public:
  /// One row of the palette: either a heading or an icon cell.
  struct Entry {
    int id = -1;             ///< owner's identity for a cell; ignored for headings
    std::wstring heading;    ///< non-empty makes this a heading
    std::wstring label;      ///< the cell's tooltip
  };

  static const wchar_t* kClassName;
  static bool Register(HINSTANCE instance);

  HWND Create(HWND parent, HINSTANCE instance, int control_id);
  HWND hwnd() const { return hwnd_; }

  void SetEntries(std::vector<Entry> entries);
  void SetSelected(int id);
  int selected() const { return selected_; }

  /// How tall the entries would come out at this width, with no scrollbar.
  ///
  /// Asked rather than measured, because measuring is circular: the height
  /// decides whether it scrolls, the scrollbar decides the cells' width, and the
  /// width decides the height.
  int HeightFor(int width) const;

  /// Keep this many columns, and take the cell size from the width.
  ///
  /// A fixed cell size means a dock dragged wider gets more, smaller-looking
  /// columns and a ragged margin; sizing the cell to fill the box instead means
  /// the count changes with the *height*. The column count is the thing to hold
  /// still.
  ///
  /// The count moves within the range as the dock is dragged, unless the owner
  /// has pinned one with `SetColumnCount`. Pass 0 for `fewest` to go back to a
  /// fixed cell size.
  ///
  /// `scrolls` settles the scrollbar once instead of leaving it to be worked
  /// out: with cells sized from the width, a scrollbar narrows the grid, which
  /// takes fewer columns, which makes taller cells, which keep the scrollbar —
  /// an answer that is stable and wrong.
  void SetColumns(int fewest, int most, bool scrolls);

  /// Pin a count of cells per row, or one of the two automatic modes.
  void SetColumnCount(int columns);
  /// What is pinned, `kPaletteFitPanel` or `kPaletteScaleTiles`. The application
  /// stores this between runs, so the numbering may not change under a saved
  /// setting.
  int column_count() const { return pinned_columns_; }
  /// How many columns are actually being drawn right now.
  int columns_now() const { return columns_now_; }

  /// The right button came down on the grid, over entry `id` — or -1 on a
  /// heading or bare panel. The grid has no opinion about what a palette's
  /// entries mean, only about which one was pointed at.
  std::function<void(POINT screen, int id)> on_context;

  /// Draws one cell's interior. The grid has already filled the background, and
  /// draws the hover outline on top afterwards.
  ///
  /// `rect` is not always the whole cell: the selected one is inset, because the
  /// selection is a band of colour around the icon rather than a border over it.
  std::function<void(HDC dc, const RECT& rect, int id)> draw_icon;
  /// A cell was clicked.
  std::function<void(int id)> on_pick;

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void OnPaint();
  /// Create the tooltip control, once, on first need.
  void EnsureTooltip();
  /// Point the tooltip at whatever the pointer is over, or hide it.
  void UpdateTooltip(int hit);
  /// Columns and cell size for a width, shared by Relayout and HeightFor so the
  /// answer to "how tall" cannot drift from the layout it describes.
  void Arrange(int width, int& per_row, int& cell) const;
  /// Whether the cell size is the promise being kept, rather than the count. It
  /// decides where the width the cells did not use goes.
  bool ScalingTiles() const {
    return fewest_columns_ > 0 && pinned_columns_ == kPaletteScaleTiles;
  }
  void Relayout();
  void SetScroll();
  void OnVScroll(int code);
  int HitTest(int x, int y) const;

  /// Where each entry landed, filled by Relayout. Parallel to entries_.
  struct Slot { RECT rect; };

  HWND hwnd_ = nullptr;
  std::vector<Entry> entries_;
  std::vector<Slot> slots_;
  int content_height_ = 0;
  int scroll_ = 0;
  int selected_ = -1;
  int hover_ = -1;      ///< index into entries_, not an id
  bool tracking_ = false;
  /// Whether the vertical scrollbar is showing, and a guard against the one
  /// relayout that hiding it causes re-entering this.
  bool scrollbar_ = true;
  bool relaying_ = false;
  /// The column range, or 0 for "as many fixed-size cells as fit".
  int fewest_columns_ = 0, most_columns_ = 0;
  /// A column count the user chose, or one of the two automatic modes.
  int pinned_columns_ = kPaletteFitPanel;
  /// Whether this grid can scroll at all. A grid that cannot never takes the
  /// style, so its cells always get the whole width.
  bool scrolls_ = true;
  /// What the last layout came out at, for the owner's menu to tick.
  mutable int columns_now_ = 0;
  /// One tooltip for the whole grid, moved between cells rather than one per
  /// cell: a hundred and ten tool windows is a hundred and ten too many.
  HWND tip_ = nullptr;
};

}  // namespace pfwin
