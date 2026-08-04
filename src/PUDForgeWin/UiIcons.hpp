// The client's own button artwork, cut out of one sheet.
//
// Not the game's artwork: these are the editor's own buttons — New, Undo, the
// brush shapes, the mirror axes — and Warcraft II has no drawing of any of them.
// The sheet is `src/ui-icons.png`, a grid of 16x16 cells read ten to
// a row, which `scripts/make-ui-icons.ps1` turns into the ui-icons.bmp that
// PUDForge.rc embeds — a BMP because rc.exe understands one and the core has a
// PNG encoder but no decoder.
//
// A cell that has not been drawn yet is not an error: a button whose cell is
// blank keeps the caption or glyph it has always had, so Has() is the question
// every caller asks first and adding a drawing is the whole of the work.
//
// Icons as well as an image list, because the toolbar wants an HIMAGELIST and
// the dock's push buttons want an HICON each, and building both from the same
// cells is the only way the two cannot drift. An icon also carries straight
// alpha, which is what the sheet has.
//
// Scaled by whole pixels where it can be: it is pixel art, and a bilinear 16-to
// -20 is mush.

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <vector>

namespace pfwin {

/// Where each icon sits in the sheet, reading left to right, ten to a row.
///
/// This enum *is* the sheet's layout: cell n is `UiIcon` n. The order is the
/// order the buttons appear in the window, because that is the order somebody
/// drawing the sheet works in.
enum UiIcon {
  // The strip along the top, in the order Toolbar.cpp lists it.
  kIconNew = 0,
  kIconOpen,
  kIconSave,
  kIconUndo,
  kIconRedo,
  kIconDelete,
  kIconCopy,
  kIconCut,
  kIconPaste,
  kIconSelectTerrain,
  kIconSelectUnits,
  // The terrain dock: Detail, Shape, Mirror, Shade, one row at a time.
  kIconDetailPlain,
  kIconDetailMixed,
  kIconDetailDetailed,
  kIconShapeSquare,
  kIconShapeCircle,
  kIconShapeSpray,
  kIconShapeFill,
  kIconMirrorNone,
  kIconMirrorLeftRight,
  kIconMirrorTopBottom,
  kIconMirrorSwNe,
  kIconMirrorNwSe,
  kIconShadeLight,
  kIconShadeDark,
  kIconShadeMix,
  /// The "open the properties for this" button beside a dropdown. One drawing
  /// used twice — the units dock's player picker and the AI script list —
  /// because it is one idea said in two places.
  kIconProperties,
  kUiIconCount
};

/// The sheet, rasterised once at one size.
///
/// Held by the application and handed to whoever draws a button. Loading twice
/// at two sizes is what a second monitor at a different scale would want;
/// nothing needs that yet.
class UiIcons {
 public:
  ~UiIcons() { Close(); }
  UiIcons() = default;
  UiIcons(const UiIcons&) = delete;
  UiIcons& operator=(const UiIcons&) = delete;

  /// Cut the sheet up at `size` pixels a side. False when the resource is
  /// missing or unreadable, which is not fatal: every caller falls back to the
  /// caption it had before there was a sheet.
  bool Load(HINSTANCE instance, int size);
  void Close();

  bool ready() const { return list_ != nullptr; }
  int size() const { return size_; }

  /// Whether that cell has been drawn. A blank cell means "keep the caption".
  bool Has(int which) const;

  /// The whole sheet as one image list, indexed by UiIcon. Borrowed: the list
  /// outlives any control it is given to, because this is owned by the
  /// application and they are its children.
  HIMAGELIST list() const { return list_; }

  /// One cell as an icon, or null when it has not been drawn. Borrowed on the
  /// same terms.
  HICON Icon(int which) const;

  /// Put an icon on a push button, or leave its caption alone.
  ///
  /// BS_ICON has to go on as well as the image, or the button keeps drawing its
  /// text and the icon is never asked for. Setting it only when there is an icon
  /// is what makes a half-drawn sheet look deliberate rather than broken.
  /// @return whether the button now shows an icon
  bool Decorate(HWND button, int which) const;

  /// Put an icon on a push button that keeps its caption, drawn to its left.
  ///
  /// Decorate() replaces the text, which is right for a button whose caption was
  /// only ever a stand-in for a picture. It is wrong for one carrying a sentence:
  /// "Show this AI script…" says something the icon does not.
  ///
  /// BCM_SETIMAGELIST rather than BM_SETIMAGE, because only the image-list form
  /// draws the image *beside* the text. It wants a list of its own per button,
  /// so one is built per cell on first use and kept.
  /// @return whether the button now shows an icon
  bool DecorateBeside(HWND button, int which) const;

 private:
  /// A one-image list for `which`, built on first ask. Null when that cell has
  /// nothing in it.
  HIMAGELIST Single(int which) const;

  int size_ = 0;
  HIMAGELIST list_ = nullptr;
  /// Indexed by UiIcon; null for a cell with nothing in it.
  std::vector<HICON> icons_;
  /// Indexed the same way. Mutable because this is a cache: a const UiIcons is
  /// still the same twenty-seven drawings.
  mutable std::vector<HIMAGELIST> singles_;
};

}  // namespace pfwin
