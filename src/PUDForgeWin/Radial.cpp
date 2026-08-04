#include "Radial.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Blit.hpp"
#include "GameData.hpp"
#include "Strings.hpp"
#include "strings.h"

namespace pfwin {
namespace {

constexpr wchar_t kClassName[] = L"PUDForgeTerrainRing";

/// Inner hole and outer edge at 96 DPI. The hole is wide enough for the name of
/// the longest terrain — it is the part of the ring the eye rests on.
constexpr int kInnerBase = 76;
constexpr int kOuterBase = 190;
/// The ring's outline. The window is made round with a region, which cuts a
/// hard, aliased edge; a dark line along it turns that into a drawn one.
constexpr int kBorderBase = 2;
/// Below this from the centre the angle is noise and the highlight would flicker
/// between neighbours, so nothing is under the pointer at all.
constexpr int kDeadZoneBase = 14;
/// The terrain name in the hole, at 96 DPI. Twice the stock UI font: the ring
/// is aimed at rather than read, so the label has to land in peripheral vision.
constexpr int kLabelBase = 22;

/// A press shorter than this was a tap, not a hold: the ring stays up to be
/// clicked rather than resolving on the key coming back up.
constexpr DWORD kHoldMillis = 250;

int Scaled(HWND reference, int base) {
  const UINT dpi = reference ? GetDpiForWindow(reference) : 96;
  return MulDiv(base, int(dpi ? dpi : 96), 96);
}

double Angle(int index, int count) {
  // From twelve o'clock, clockwise, which is where a dial starts.
  return (double(index) / double(count)) * 6.283185307179586 - 1.5707963267948966;
}

/// One wedge as a region: out along one edge, round the outer arc, back down the
/// other, round the inner arc. Wedges rather than buttons on a circle because
/// they touch — every direction from the centre lands on something.
HRGN WedgeRegion(int cx, int cy, int inner, int outer, double a0, double a1) {
  constexpr int kSteps = 24;
  std::vector<POINT> points;
  points.reserve(size_t(kSteps) * 2 + 2);
  for (int i = 0; i <= kSteps; i++) {
    const double a = a0 + (a1 - a0) * double(i) / double(kSteps);
    points.push_back({cx + int(std::lround(std::cos(a) * outer)),
                      cy + int(std::lround(std::sin(a) * outer))});
  }
  for (int i = kSteps; i >= 0; i--) {
    const double a = a0 + (a1 - a0) * double(i) / double(kSteps);
    points.push_back({cx + int(std::lround(std::cos(a) * inner)),
                      cy + int(std::lround(std::sin(a) * inner))});
  }
  return CreatePolygonRgn(points.data(), int(points.size()), WINDING);
}

/// A brush to fill a wedge with: one tile of the terrain, tiled.
///
/// A pattern rather than one stretched tile, because 32 pixels of pixel art
/// blown up across a 190-pixel wedge is mush. Where the artwork has nothing to
/// offer, the terrain's flat colour.
HBRUSH TerrainBrush(const pf_tileset_art* art, int terrain, int tileset) {
  const int tile = pf_solid_tile(terrain, 0);
  const int mega = (art && tile >= 0)
                       ? pf_tileset_art_megatile_for(art, uint16_t(tile)) : -1;
  if (mega < 0 || pf_tileset_art_is_blank(art, mega)) {
    const uint32_t rgb = pf_terrain_flat_colour(terrain, tileset);
    // The core hands 0x00RRGGBB and a solid brush wants 0x00BBGGRR.
    return CreateSolidBrush(RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
  }
  // A packed DIB — header, the three channel masks, then the pixels — because
  // the core's packing is RGBA and a plain BI_RGB bitmap would read it as BGRA
  // and swap red with blue. Same reasoning as Blit.hpp.
  std::vector<uint8_t> packed(sizeof(BITMAPINFOHEADER) + 3 * sizeof(DWORD) +
                              32 * 32 * 4);
  auto* header = reinterpret_cast<BITMAPINFOHEADER*>(packed.data());
  header->biSize = sizeof(BITMAPINFOHEADER);
  header->biWidth = 32;
  header->biHeight = -32;            // top-down, the order the core draws in
  header->biPlanes = 1;
  header->biBitCount = 32;
  header->biCompression = BI_BITFIELDS;
  auto* masks = reinterpret_cast<DWORD*>(packed.data() + sizeof(BITMAPINFOHEADER));
  masks[0] = 0x000000FF;
  masks[1] = 0x0000FF00;
  masks[2] = 0x00FF0000;
  auto* pixels = reinterpret_cast<uint32_t*>(masks + 3);
  pf_tileset_art_draw(art, mega, pixels, 32);
  return CreateDIBPatternBrushPt(packed.data(), DIB_RGB_COLORS);
}

struct Ring {
  int count = 0;
  int index = 0;
  int chosen = -1;
  bool done = false;
  int inner = kInnerBase, outer = kOuterBase, dead = kDeadZoneBase;
  int tileset = 0;
  const pf_tileset_art* art = nullptr;
  std::vector<HBRUSH> fills;      ///< one per wedge, owned
  /// Which brush each wedge stands for.
  ///
  /// Not the brush index: the ring shows one wedge per terrain, so the dark
  /// member of each pair has none of its own. Ten wedges for seven terrains meant
  /// three pairs of near-identical textures side by side and a ring you had to
  /// read rather than aim at.
  std::vector<int> brushes;
  HWND hwnd = nullptr;

  ~Ring() { for (HBRUSH b : fills) if (b) DeleteObject(b); }

  int BrushAt(int wedge) const {
    return wedge >= 0 && wedge < int(brushes.size()) ? brushes[size_t(wedge)] : -1;
  }

  /// Which wedge a point in client coordinates lies in, or -1 for the hole.
  int HitTest(int x, int y) const {
    const double dx = x - outer, dy = y - outer;
    if (std::hypot(dx, dy) < dead) return -1;
    const double step = 6.283185307179586 / double(count);
    double angle = std::atan2(dy, dx) + 1.5707963267948966 + step / 2;
    while (angle < 0) angle += 6.283185307179586;
    return int(angle / step) % count;
  }
};

void PaintRing(Ring& ring, HDC dc) {
  const int size = ring.outer * 2;
  RECT all{0, 0, size, size};
  FillRect(dc, &all, GetSysColorBrush(COLOR_BTNFACE));

  const double step = 6.283185307179586 / double(ring.count);
  HBRUSH edge = GetSysColorBrush(COLOR_BTNSHADOW);
  HBRUSH accent = GetSysColorBrush(COLOR_HIGHLIGHT);
  for (int i = 0; i < ring.count; i++) {
    const double a = Angle(i, ring.count);
    HRGN wedge = WedgeRegion(ring.outer, ring.outer, ring.inner, ring.outer,
                             a - step / 2, a + step / 2);
    FillRgn(dc, wedge, ring.fills[size_t(i)]);
    // The chosen wedge is framed rather than tinted: a tint over terrain hides
    // the terrain, which is the one thing the wedge is there to show.
    FrameRgn(dc, wedge, i == ring.index ? accent : edge, i == ring.index ? 3 : 1,
             i == ring.index ? 3 : 1);
    DeleteObject(wedge);
  }

  // The name in the hole, where the eye already is. Nothing else: the size box
  // that used to sit under it went with the wheel that changed it.
  //
  // Larger than the client's usual DEFAULT_GUI_FONT, and derived from it so the
  // typeface still matches everything else. The ring is aimed at rather than
  // read, so the label is glanced at from wherever the pointer has flicked to —
  // and at the stock size it was a caption on a 300-pixel dial.
  SetBkMode(dc, TRANSPARENT);
  const char* name =
      pf_terrain_name(pf_brush_terrain(ring.BrushAt(ring.index)), ring.tileset);
  const std::wstring label = name ? FromUtf8(name) : L"";
  RECT hole{ring.outer - ring.inner, ring.outer - ring.inner,
            ring.outer + ring.inner, ring.outer + ring.inner};

  LOGFONTW lf{};
  GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
  // Negative for character height rather than cell height, which is what a
  // point size means to a reader.
  const int wanted = Scaled(ring.hwnd, kLabelBase);
  lf.lfHeight = -wanted;
  lf.lfWeight = FW_SEMIBOLD;

  // Shrink to fit rather than trusting the measurement: "Human Wall" is the
  // longest of the English names and leaves room to spare, but the names are a
  // translated table and a longer one must not be clipped mid-word.
  HFONT font = nullptr;
  const int room = ring.inner * 2 - Scaled(ring.hwnd, 16);
  for (int h = wanted; h >= Scaled(ring.hwnd, 11); h--) {
    lf.lfHeight = -h;
    HFONT candidate = CreateFontIndirectW(&lf);
    if (!candidate) break;
    HGDIOBJ probe = SelectObject(dc, candidate);
    SIZE extent{};
    GetTextExtentPoint32W(dc, label.c_str(), int(label.size()), &extent);
    SelectObject(dc, probe);
    if (extent.cx <= room || h == Scaled(ring.hwnd, 11)) { font = candidate; break; }
    DeleteObject(candidate);
  }

  HGDIOBJ old = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
  SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
  DrawTextW(dc, label.c_str(), -1, &hole,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old);
  if (font) DeleteObject(font);

  // The outline, last, over everything it encloses.
  const int border = Scaled(ring.hwnd, kBorderBase);
  if (HRGN round = CreateEllipticRgn(0, 0, size + 1, size + 1)) {
    HBRUSH ink = CreateSolidBrush(RGB(0, 0, 0));
    FrameRgn(dc, round, ink, border, border);
    DeleteObject(ink);
    DeleteObject(round);
  }
}

LRESULT CALLBACK RingProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  Ring* ring = reinterpret_cast<Ring*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    ring = static_cast<Ring*>(create->lpCreateParams);
    ring->hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ring));
  }
  if (!ring) return DefWindowProcW(hwnd, message, wparam, lparam);

  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      // Off screen and blitted once, the way the palette, the minimap and the
      // canvas already do it. Painted straight to the window, one move of the
      // pointer between wedges is a full-face fill followed by ten wedges, the
      // label and the outline arriving in turn — and the pointer crosses a
      // wedge boundary constantly, which is what made the ring shimmer.
      const int side = ring->outer * 2;
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP buffer = mem ? CreateCompatibleBitmap(dc, side, side) : nullptr;
      if (mem && buffer) {
        HGDIOBJ was = SelectObject(mem, buffer);
        PaintRing(*ring, mem);
        BitBlt(dc, 0, 0, side, side, mem, 0, 0, SRCCOPY);
        SelectObject(mem, was);
      } else {
        PaintRing(*ring, dc);
      }
      if (buffer) DeleteObject(buffer);
      if (mem) DeleteDC(mem);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;

    case WM_MOUSEMOVE: {
      const int hit = ring->HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (hit >= 0 && hit != ring->index) {
        ring->index = hit;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_LBUTTONUP:
      ring->chosen = ring->index;
      ring->done = true;
      return 0;

    case WM_RBUTTONUP:
      ring->done = true;
      return 0;

    // The wheel is swallowed rather than acted on: it used to size the brush
    // here, which meant one gesture with two meanings depending on whether a
    // ring happened to be up. Not passed on either, because the window behind is
    // the map and scrolling it under a ring nobody can see move is worse.
    case WM_MOUSEWHEEL:
      return 0;

    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

}  // namespace

bool RegisterTerrainRing(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &RingProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;      // WM_PAINT covers every pixel it owns
  wc.lpszClassName = kClassName;
  return RegisterClassExW(&wc) != 0;
}

int ShowTerrainRing(HWND owner, HINSTANCE instance, const pf_tileset_art* art,
                    int tileset, int current, POINT at) {
  Ring ring;
  // One wedge per terrain the palette shows, which is the light member of each
  // pair and everything with only one drawing. The Shade switch on the terrain
  // dock is what reaches the other member.
  for (int i = 0; i < pf_brush_count(); i++) {
    if (pf_brush_shade(i) >= 0) ring.brushes.push_back(i);
  }
  ring.count = int(ring.brushes.size());
  if (ring.count <= 0) return -1;
  // Open on the wedge the current brush is in, whichever brush index that is.
  ring.index = 0;
  for (int i = 0; i < ring.count; i++) {
    if (ring.brushes[size_t(i)] == current) ring.index = i;
  }
  ring.art = art;
  ring.tileset = tileset;
  ring.inner = Scaled(owner, kInnerBase);
  ring.outer = Scaled(owner, kOuterBase);
  ring.dead = Scaled(owner, kDeadZoneBase);
  ring.fills.reserve(size_t(ring.count));
  for (int brush : ring.brushes) {
    ring.fills.push_back(TerrainBrush(art, pf_brush_terrain(brush), tileset));
  }

  // Centred on the pointer and kept whole on its monitor: a ring half off the
  // screen is a ring with directions you cannot reach.
  const int span = ring.outer * 2;
  HMONITOR monitor = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(monitor, &info);
  const int x = std::max(int(info.rcWork.left),
                         std::min(int(at.x) - ring.outer, int(info.rcWork.right) - span));
  const int y = std::max(int(info.rcWork.top),
                         std::min(int(at.y) - ring.outer, int(info.rcWork.bottom) - span));

  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, nullptr,
                              WS_POPUP, x, y, span, span, owner, nullptr, instance,
                              &ring);
  if (!hwnd) return -1;
  // Round, so the corners of the square window do not sit over the map. The hole
  // stays part of the window: the pointer has to be able to cross it.
  if (HRGN round = CreateEllipticRgn(0, 0, span + 1, span + 1)) {
    SetWindowRgn(hwnd, round, FALSE);   // the window owns it now
  }
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);
  SetCapture(hwnd);

  // The pointer starts in the hole, so the ring opens on the current brush
  // rather than on whichever wedge happens to be at twelve o'clock.
  const DWORD opened = GetTickCount();
  bool space_was_down = (GetKeyState(VK_SPACE) & 0x8000) != 0;

  // Zero-initialised only to satisfy the compiler: GetMessageW fills it on every
  // path that reads it, including the WM_QUIT return that sets `quitting`, but
  // that link is past what the dataflow analysis can follow.
  MSG msg = {};
  bool quitting = false;
  while (!ring.done) {
    const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
    if (got <= 0) { quitting = got == 0; break; }
    if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
      switch (msg.wParam) {
        case VK_ESCAPE:
          ring.done = true;
          continue;
        case VK_RETURN:
          ring.chosen = ring.index;
          ring.done = true;
          continue;
        case VK_RIGHT: case VK_DOWN:
          ring.index = (ring.index + 1) % ring.count;
          InvalidateRect(hwnd, nullptr, FALSE);
          continue;
        case VK_LEFT: case VK_UP:
          ring.index = (ring.index + ring.count - 1) % ring.count;
          InvalidateRect(hwnd, nullptr, FALSE);
          continue;
        case VK_SPACE:
          // Auto-repeat while the key is held must not shut the ring the moment
          // it opened, so only a fresh press closes it. Bit 30 of lParam is
          // "was already down".
          if (!(msg.lParam & (1 << 30))) { ring.done = true; continue; }
          space_was_down = true;
          continue;
        default:
          break;
      }
    }
    if (msg.message == WM_KEYUP && msg.wParam == VK_SPACE) {
      // Held, space is spring-loaded: let go and you get whatever the pointer was
      // over. Tapped, it stays open. The ring tells which was meant by how long
      // the key was down.
      if (space_was_down && GetTickCount() - opened > kHoldMillis) {
        ring.chosen = ring.index;
        ring.done = true;
      }
      continue;
    }
    // Any click outside the ring dismisses it, and is swallowed rather than
    // reaching the map: it was aimed at something that happened to be in the way.
    if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN ||
        msg.message == WM_MBUTTONDOWN) {
      POINT pt = msg.pt;
      ScreenToClient(hwnd, &pt);
      if (ring.HitTest(pt.x, pt.y) < 0 &&
          (pt.x < 0 || pt.y < 0 || pt.x >= span || pt.y >= span)) {
        ring.done = true;
        continue;
      }
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  ReleaseCapture();
  DestroyWindow(hwnd);
  // The loop above is the only one running, so a WM_QUIT that arrives while
  // the ring is up would be swallowed here and the application would refuse to
  // close. Hand it back to the loop that knows what to do with it.
  if (quitting) PostQuitMessage(int(msg.wParam));
  // A brush index, not a wedge: the caller knows nothing about how the ring
  // chose to arrange itself.
  return ring.BrushAt(ring.chosen);
}

}  // namespace pfwin
