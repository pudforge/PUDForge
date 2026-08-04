// Getting the core's pixels on screen without copying them.
//
// The core packs 0xAABBGGRR - RGBA byte order, what a canvas ImageData wants.
// A plain 32bpp DIB is BGRA byte order, so blitting the buffer as BI_RGB
// swaps red and blue (an earlier version of this client did exactly that).
// BI_BITFIELDS names the channel masks explicitly, so GDI reads the buffer
// as it is and nobody swizzles - which was the point of the shared format.

#pragma once

#include <windows.h>

#include <cstdint>

namespace pfwin {

/// How wide every mark drawn over a map is — canvas and minimap alike.
///
/// One number rather than each overlay's own: a selection outlined at one pixel
/// beside a footprint outlined at two reads as two kinds of thing, and they are
/// all the editor saying which tiles it means. Two survives being drawn over
/// 32 px artwork.
constexpr int kOutlinePx = 2;

/// A dotted outline at kOutlinePx.
///
/// CreatePen drops the style as soon as the width is over one, so a wide dotted
/// pen has to be geometric, and a geometric pen needs a brush. The dash lengths
/// follow the width, which keeps a two pixel band looking dotted.
inline HPEN CreateDottedPen(COLORREF colour) {
  LOGBRUSH brush = {BS_SOLID, colour, 0};
  return ExtCreatePen(PS_GEOMETRIC | PS_DOT | PS_ENDCAP_FLAT, kOutlinePx,
                      &brush, 0, nullptr);
}

/// BITMAPINFO with room for the three BI_BITFIELDS channel masks.
struct RgbaBitmapInfo {
  BITMAPINFOHEADER header;
  DWORD masks[3];
};

inline RgbaBitmapInfo RgbaInfo(int width, int height) {
  RgbaBitmapInfo bi = {};
  bi.header.biSize = sizeof(bi.header);
  bi.header.biWidth = width;
  // Negative height: top-down, the order the core composes in.
  bi.header.biHeight = -height;
  bi.header.biPlanes = 1;
  bi.header.biBitCount = 32;
  bi.header.biCompression = BI_BITFIELDS;
  bi.masks[0] = 0x000000FF;   // red
  bi.masks[1] = 0x0000FF00;   // green
  bi.masks[2] = 0x00FF0000;   // blue
  return bi;
}

/// Nearest-neighbour stretch of an RGBA buffer - it is pixel art, and
/// anything smoother turns Warcraft II into mush.
inline void BlitRgba(HDC dc, int dx, int dy, int dw, int dh, int sw, int sh,
                     const uint32_t* pixels) {
  const RgbaBitmapInfo bi = RgbaInfo(sw, sh);
  SetStretchBltMode(dc, COLORONCOLOR);
  StretchDIBits(dc, dx, dy, dw, dh, 0, 0, sw, sh, pixels,
                reinterpret_cast<const BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                SRCCOPY);
}

/// The same buffer, blended over what is already on the DC at `alpha`/255 and
/// optionally pulled `tint_pct` of the way towards `tint_rgb` (0x00RRGGBB).
///
/// A separate function rather than a flag on BlitRgba, because AlphaBlend cannot
/// read a caller's pointer the way StretchDIBits can: it needs its source
/// selected into a DC, from a 32bpp DIB *section*, with the colour channels
/// premultiplied. So the pixels are copied into one per call — sprite-sized,
/// which is what the mask and the tint have to be walked for anyway.
///
/// AC_SRC_ALPHA has no BI_BITFIELDS equivalent, so this is the one place in the
/// client that swizzles the core's RGBA into the BGRA a plain DIB means.
inline void BlitRgbaBlended(HDC dc, int dx, int dy, int dw, int dh, int sw,
                            int sh, const uint32_t* pixels, BYTE alpha,
                            uint32_t tint_rgb = 0, int tint_pct = 0) {
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !pixels) return;
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = sw;
  bi.bmiHeader.biHeight = -sh;   // top-down, as in RgbaInfo
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib) return;
  HDC source = CreateCompatibleDC(dc);
  if (!source) { DeleteObject(dib); return; }

  const int tr = int((tint_rgb >> 16) & 0xFF);
  const int tg = int((tint_rgb >> 8) & 0xFF);
  const int tb = int(tint_rgb & 0xFF);
  auto* out = static_cast<uint32_t*>(bits);
  const size_t count = size_t(sw) * size_t(sh);
  for (size_t i = 0; i < count; i++) {
    const uint32_t p = pixels[i];
    const int a = int((p >> 24) & 0xFF);
    if (a == 0) { out[i] = 0; continue; }
    int r = int(p & 0xFF), g = int((p >> 8) & 0xFF), b = int((p >> 16) & 0xFF);
    if (tint_pct > 0) {
      r += (tr - r) * tint_pct / 100;
      g += (tg - g) * tint_pct / 100;
      b += (tb - b) * tint_pct / 100;
    }
    out[i] = (uint32_t(a) << 24) | (uint32_t(r * a / 255) << 16) |
             (uint32_t(g * a / 255) << 8) | uint32_t(b * a / 255);
  }

  HGDIOBJ old = SelectObject(source, dib);
  // The per-pixel alpha is the sprite's own transparency mask; the constant
  // alpha is how much of a ghost it is, and GDI multiplies the two.
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
  AlphaBlend(dc, dx, dy, dw, dh, source, 0, 0, sw, sh, blend);
  SelectObject(source, old);
  DeleteDC(source);
  DeleteObject(dib);
}

}  // namespace pfwin
