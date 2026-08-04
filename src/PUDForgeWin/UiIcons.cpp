#include "UiIcons.hpp"

#include <algorithm>
#include <cstdint>

#include "resource.h"

namespace pfwin {
namespace {

/// The sheet's own cell size. The grid is square and ten cells wide.
constexpr int kCell = 16;
constexpr int kColumns = 10;

/// A pixel counts as drawn above this alpha. Not zero: an editor that
/// antialiases against transparency leaves a scatter of alpha-1 pixels behind,
/// and a cell of those would claim to be artwork.
constexpr int kOpaqueEnough = 8;

/// One cell, scaled to `size`, as straight-alpha BGRA.
///
/// Nearest neighbour, deliberately: these are 16 pixel drawings, and the only
/// honest way to show one at 32 pixels is as four pixels each. At the odd
/// scalings some source pixels land on two rows and some on one, which is what
/// pixel art has always looked like when it is not doubled.
std::vector<uint32_t> CutCell(const uint32_t* sheet, int sheet_w, int sheet_h,
                              int cell, int size) {
  std::vector<uint32_t> out(size_t(size) * size_t(size), 0);
  const int cx = (cell % kColumns) * kCell;
  const int cy = (cell / kColumns) * kCell;
  if (cx + kCell > sheet_w || cy + kCell > sheet_h) return out;
  for (int y = 0; y < size; y++) {
    const int sy = cy + std::min(kCell - 1, y * kCell / size);
    for (int x = 0; x < size; x++) {
      const int sx = cx + std::min(kCell - 1, x * kCell / size);
      out[size_t(y) * size_t(size) + size_t(x)] =
          sheet[size_t(sy) * size_t(sheet_w) + size_t(sx)];
    }
  }
  return out;
}

bool AnyOpaque(const std::vector<uint32_t>& pixels) {
  for (uint32_t p : pixels) {
    if (int((p >> 24) & 0xFF) >= kOpaqueEnough) return true;
  }
  return false;
}

/// An icon from straight-alpha BGRA pixels.
///
/// The AND mask is redundant with the alpha channel on every Windows this runs
/// on, but an icon is not an icon without one, and a solid-zero mask is what
/// "the alpha channel decides" means.
HICON MakeIcon(const std::vector<uint32_t>& pixels, int size) {
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;   // top-down, the order CutCell wrote
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP colour = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!colour) return nullptr;
  std::copy(pixels.begin(), pixels.end(), static_cast<uint32_t*>(bits));

  // A 1bpp mask of the right size, all zeros: "take every pixel from the colour
  // bitmap and let its alpha decide".
  //
  // The zeros have to be passed in. CreateBitmap with a null bits pointer leaves
  // the contents *uninitialised*, and a mask bit that comes up 1 means "XOR the
  // colour against the screen there" — which is how these icons first reached
  // the toolbar with their outlines missing.
  //
  // 1bpp rows pad to a 16-bit boundary, which is what CreateBitmap documents.
  const size_t mask_stride = size_t((size + 15) / 16) * 2;
  const std::vector<uint8_t> blank(mask_stride * size_t(size), 0);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, blank.data());
  if (!mask) { DeleteObject(colour); return nullptr; }

  ICONINFO info = {};
  info.fIcon = TRUE;
  info.hbmMask = mask;
  info.hbmColor = colour;
  // CreateIconIndirect copies both bitmaps, so they are ours to delete.
  HICON icon = CreateIconIndirect(&info);
  DeleteObject(mask);
  DeleteObject(colour);
  return icon;
}

}  // namespace

bool UiIcons::Load(HINSTANCE instance, int size) {
  Close();
  if (size < 1) return false;

  // LR_CREATEDIBSECTION, or the alpha channel is thrown away on the way in:
  // without it LoadImage converts to a device bitmap and the icons come out as
  // black squares with the artwork somewhere inside them.
  HBITMAP sheet = HBITMAP(LoadImageW(instance, MAKEINTRESOURCEW(IDB_UI_ICONS),
                                     IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
  if (!sheet) return false;

  DIBSECTION dib = {};
  if (GetObjectW(sheet, sizeof(dib), &dib) != sizeof(dib) ||
      dib.dsBm.bmBitsPixel != 32 || !dib.dsBm.bmBits) {
    DeleteObject(sheet);
    return false;
  }
  const int sheet_w = dib.dsBm.bmWidth;
  const int sheet_h = std::abs(dib.dsBm.bmHeight);
  const auto* raw = static_cast<const uint32_t*>(dib.dsBm.bmBits);

  // The resource is a bottom-up DIB, which is how a .bmp on disk is stored, so
  // the first row in memory is the last row of the picture. Flipped once here
  // rather than reasoned about at every cut.
  std::vector<uint32_t> rows(size_t(sheet_w) * size_t(sheet_h));
  const bool bottom_up = dib.dsBmih.biHeight > 0;
  for (int y = 0; y < sheet_h; y++) {
    const int from = bottom_up ? sheet_h - 1 - y : y;
    std::copy(raw + size_t(from) * size_t(sheet_w),
              raw + size_t(from + 1) * size_t(sheet_w),
              rows.begin() + size_t(y) * size_t(sheet_w));
  }
  DeleteObject(sheet);

  size_ = size;
  // ILC_MASK alongside ILC_COLOR32 because the images go in as icons, which
  // carry a mask whether or not their alpha needs one.
  list_ = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, kUiIconCount, 0);
  if (!list_) { size_ = 0; return false; }

  icons_.assign(kUiIconCount, nullptr);
  singles_.assign(kUiIconCount, nullptr);
  for (int i = 0; i < kUiIconCount; i++) {
    const std::vector<uint32_t> cell = CutCell(rows.data(), sheet_w, sheet_h, i, size);
    // A blank cell still takes a slot, so an index into the list is a UiIcon and
    // stays one however much of the sheet has been drawn.
    HICON icon = AnyOpaque(cell) ? MakeIcon(cell, size) : nullptr;
    if (icon) {
      icons_[size_t(i)] = icon;
      ImageList_ReplaceIcon(list_, -1, icon);
    } else {
      // An empty image, to keep the indices lined up with the enum.
      const std::vector<uint32_t> nothing(size_t(size) * size_t(size), 0);
      if (HICON blank = MakeIcon(nothing, size)) {
        ImageList_ReplaceIcon(list_, -1, blank);
        DestroyIcon(blank);
      }
    }
  }
  return true;
}

void UiIcons::Close() {
  for (HICON icon : icons_) {
    if (icon) DestroyIcon(icon);
  }
  icons_.clear();
  for (HIMAGELIST single : singles_) {
    if (single) ImageList_Destroy(single);
  }
  singles_.clear();
  if (list_) { ImageList_Destroy(list_); list_ = nullptr; }
  size_ = 0;
}

bool UiIcons::Has(int which) const {
  return which >= 0 && size_t(which) < icons_.size() && icons_[size_t(which)];
}

HICON UiIcons::Icon(int which) const {
  return Has(which) ? icons_[size_t(which)] : nullptr;
}

bool UiIcons::Decorate(HWND button, int which) const {
  HICON icon = Icon(which);
  if (!button || !icon) return false;
  SetWindowLongPtrW(button, GWL_STYLE,
                    GetWindowLongPtrW(button, GWL_STYLE) | BS_ICON);
  SendMessageW(button, BM_SETIMAGE, IMAGE_ICON, LPARAM(icon));
  return true;
}

HIMAGELIST UiIcons::Single(int which) const {
  if (!Has(which)) return nullptr;
  HIMAGELIST& held = singles_[size_t(which)];
  if (held) return held;
  held = ImageList_Create(size_, size_, ILC_COLOR32 | ILC_MASK, 1, 0);
  if (held) ImageList_ReplaceIcon(held, -1, icons_[size_t(which)]);
  return held;
}

bool UiIcons::DecorateBeside(HWND button, int which) const {
  HIMAGELIST single = button ? Single(which) : nullptr;
  if (!single) return false;
  BUTTON_IMAGELIST images = {};
  images.himl = single;
  // Left, with room round the drawing. BUTTON_IMAGELIST_ALIGN_LEFT puts the
  // image against the button's left edge and centres the caption in what is
  // left, which is what every shell button with both looks like.
  images.margin = RECT{size_ / 4, 0, size_ / 4, 0};
  images.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;
  return SendMessageW(button, BCM_SETIMAGELIST, 0, LPARAM(&images)) != 0;
}

}  // namespace pfwin
