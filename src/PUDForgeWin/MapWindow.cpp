#include "MapWindow.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "Blit.hpp"
#include "GameData.hpp"   // FromUtf8
#include "Icons.hpp"
#include "Strings.hpp"
#include "strings.h"

namespace pfwin {
namespace {

/// Spray puffs while the button is held. Rate comes from the core.
constexpr UINT_PTR kSprayTimer = 1;
/// Palette cycling for the sea. 140 ms is about the game's own pace.
constexpr UINT_PTR kWaterTimer = 2;
constexpr UINT kWaterTickMs = 140;
/// Held middle button: scroll towards the pointer, on its own clock.
constexpr UINT_PTR kPanTimer = 3;
constexpr UINT kPanTickMs = 16;

/// How far the pointer must leave the press point before the view moves, in
/// DIPs. A hand resting on a mouse is never quite still.
constexpr double kPanDeadZone = 12.0;
/// Screen pixels per second of scroll, per DIP past the dead zone. At 100 DIPs
/// out that is a viewport width every couple of seconds — fast enough to cross
/// a 128x128 map, slow enough to stop on a tile.
constexpr double kPanGain = 7.0;
/// The ceiling, so a pointer flung to the far corner does not teleport.
constexpr double kPanMaxPxPerSec = 2600.0;

/// The artwork's own tile size: what pf_map_compose_region composes at,
/// whatever the zoom is, so it is also what a sprite's pixels are measured in.
constexpr int kArtTilePx = 32;

/// The two colours the placement mark has always used - a green that reads as
/// "this will land" and the game's own refusal red.
constexpr COLORREF kAllowed = RGB(64, 208, 64);
constexpr COLORREF kRefused = RGB(224, 64, 64);
/// How solid the ghost is: enough to recognise the unit, little enough that the
/// terrain it is about to cover is still readable underneath.
constexpr BYTE kGhostAlpha = 165;
/// A refused ghost is pulled most of the way to the refusal red rather than
/// only outlined in it: the outline is two pixels at the edge of a footprint
/// that may be four tiles across, and it is the sprite the eye is on.
constexpr uint32_t kRefusedTintRgb = 0xE04040;
constexpr int kRefusedTintPct = 70;
/// How solid a pending paste's terrain is. Lighter than a unit ghost, because
/// it covers a whole rectangle and the thing being judged is how its coastline
/// meets the one underneath.
constexpr BYTE kFragmentAlpha = 150;

/// The device pixel ratio for a window, from its own monitor's DPI.
double DprOf(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return dpi ? double(dpi) / 96.0 : 1.0;
}

}  // namespace

const wchar_t* MapWindow::kClassName = L"PUDForgeMap";

bool MapWindow::Register(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  // Redraw on either resize: the composed region is sized to the window.
  // CS_DBLCLKS, or there is no such thing as a double click on this window.
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = &MapWindow::Proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // No background brush. Every pixel is painted from the composed buffer, and
  // letting Windows erase first only produces a flash of grey.
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kClassName;
  return RegisterClassExW(&wc) != 0;
}

MapWindow::~MapWindow() {
  if (map_) pf_map_free(map_);
}

LRESULT CALLBACK MapWindow::Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  MapWindow* self = reinterpret_cast<MapWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<MapWindow*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT MapWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      OnPaint();
      return 0;

    // Nothing to erase: WM_PAINT covers every pixel from the composed buffer,
    // and erasing first is a visible flash while dragging.
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      OnSize(LOWORD(lparam), HIWORD(lparam));
      return 0;

    case WM_DESTROY:
      ReleaseBackBuffer();
      return 0;

    case WM_DPICHANGED_AFTERPARENT:
      view_.dpr = DprOf(hwnd_);
      if (view_.fitted) Fit();
      Invalidate();
      return 0;

    case WM_LBUTTONDBLCLK: {
      // Open what was double-clicked. The first click of the pair already ran
      // as a plain click and selected it, which makes this additive rather than
      // a mode of its own.
      SetFocus(hwnd_);
      int tx = 0, ty = 0;
      if (map_ && editor_ && host_ &&
          pf::view_tile_at(view_, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), tx, ty)) {
        const int index = editor_->SelectAt(tx, ty, false);
        if (index >= 0) {
          host_->OnEditorChanged();
          host_->OnInspectUnit(index);
        }
      }
      return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
      SetFocus(hwnd_);
      OnMouseDown(message == WM_LBUTTONDOWN ? 0 : message == WM_MBUTTONDOWN ? 1 : 2,
                  GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam);
      return 0;

    case WM_MOUSEMOVE:
      OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), wparam);
      return 0;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_CAPTURECHANGED:
      OnMouseUp();
      return 0;

    case WM_RBUTTONUP: {
      // The menu opens here rather than on button-down, so a right press that
      // meant something else — cancelling an armed paste, putting the brush
      // down — has already had its say. Which of those it was cannot be
      // re-derived here, since the press has undone the state that would say,
      // so button-down leaves the answer behind.
      const bool consumed = right_press_consumed_;
      right_press_consumed_ = false;
      OnMouseUp();
      if (consumed) return 0;
      return DefWindowProcW(hwnd_, message, wparam, lparam);
    }

    case WM_CONTEXTMENU: {
      if (!host_ || !map_) return 0;
      // lParam is -1 for the Menu key and Shift+F10, and screen coordinates
      // for the right button. Both open the same menu; only where differs.
      int tx = hover_tx_, ty = hover_ty_;
      POINT screen{};
      if (lparam == -1) {
        // At the hovered tile, or the top-left when the pointer is elsewhere: a
        // menu that opens off screen is a menu nobody asked for.
        if (tx < 0) tx = 0;
        if (ty < 0) ty = 0;
        const RECT tile = TileRectToScreen(tx, ty, 1, 1);
        screen = {tile.left, tile.bottom};
        ClientToScreen(hwnd_, &screen);
      } else {
        screen = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        POINT client = screen;
        ScreenToClient(hwnd_, &client);
        if (!pf::view_tile_at(view_, client.x, client.y, tx, ty)) return 0;
      }
      host_->OnContextMenu(tx, ty, screen);
      return 0;
    }

    case WM_SETCURSOR: {
      // The pointer says what the next click does. An arrow over a map that
      // paints, selects, places and picks up is the one shape that tells you
      // nothing.
      if (LOWORD(lparam) != HTCLIENT) break;
      SetCursor(LoadCursorW(nullptr, CursorForTool()));
      return TRUE;
    }

    case WM_MOUSEWHEEL: {
      // Wheel coordinates are in screen space, unlike every other mouse message.
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd_, &pt);
      OnWheel(GET_WHEEL_DELTA_WPARAM(wparam), pt.x, pt.y, wparam);
      return 0;
    }

    case WM_KEYDOWN:
      OnKey(wparam);
      return 0;

    // Alt is the one key whose *release* means something: it borrows the other
    // shade while it is down, and letting go has to put the switch back.
    //
    // Alt reaches a window as a *system* key, and a lone Alt press drops the
    // menu bar when it comes back up. Both are claimed here while it is doing
    // this job, so holding it over the canvas borrows a shade instead of
    // arming the menu — which is also why Alt+wheel over a unit used to leave
    // the ghost stranded: the menu took the pointer.
    case WM_SYSKEYDOWN:
      if (wparam == VK_MENU && ApplyShiftShade(true)) return 0;
      break;
    case WM_SYSKEYUP:
      if (wparam == VK_MENU) {
        const bool was = editor_ && editor_->shade_flipped;
        ApplyShiftShade(false);
        if (was) return 0;
      }
      break;

    case WM_TIMER:
      OnTimer(wparam);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void MapWindow::SetMap(pf_map* map) {
  if (map_) pf_map_free(map_);
  map_ = map;
  dirty_ = true;
  if (map_) {
    view_.map_w = pf_map_width(map_);
    view_.map_h = pf_map_height(map_);
    pf_map_set_tileset_art(map_, art_);
  }
  Fit(true);
  Invalidate();
}

void MapWindow::MapSizeChanged() {
  if (!map_) return;
  view_.map_w = pf_map_width(map_);
  view_.map_h = pf_map_height(map_);
  dirty_ = true;
  Fit();
  Invalidate();
}

void MapWindow::SetArtwork(pf_tileset_art* art, pf_sprite_set* sprites) {
  art_ = art;
  sprites_ = sprites;
  if (map_) pf_map_set_tileset_art(map_, art_);
  dirty_ = true;
  Invalidate();
}

void MapWindow::Fit(bool only_to_shrink) {
  pf::view_fit(view_, only_to_shrink);
  dirty_ = true;
  Invalidate();
  if (host_) host_->OnViewChanged();
}

void MapWindow::ZoomStep(int direction) {
  // About the centre when driven from the keyboard or the menu.
  pf::view_zoom_step(view_, direction, view_.viewport_w / 2, view_.viewport_h / 2);
  Invalidate();
  if (host_) host_->OnViewChanged();
}

void MapWindow::ZoomToTiles(int x, int y, int w, int h) {
  if (!map_ || w <= 0 || h <= 0) return;
  pf::view_fit_rect(view_, x, y, w, h);
  dirty_ = true;
  Invalidate();
  if (host_) host_->OnViewChanged();
}

void MapWindow::SetVaryFacing(bool on) {
  vary_facing_ = on;
  dirty_ = true;
  Invalidate();
}

void MapWindow::ZoomTo(int zoom) {
  pf::view_zoom_about(view_, zoom, view_.viewport_w / 2, view_.viewport_h / 2);
  view_.fitted = false;
  Invalidate();
  if (host_) host_->OnViewChanged();
}

void MapWindow::CentreOn(int tx, int ty) {
  pf::view_centre_on(view_, tx, ty);
  Invalidate();
  if (host_) host_->OnViewChanged();
}

void MapWindow::OnSize(int width, int height) {
  view_.viewport_w = width;
  view_.viewport_h = height;
  view_.dpr = DprOf(hwnd_);
  // A fitted view is a statement about the window, so it is redone; a zoom
  // somebody chose is left alone.
  if (view_.fitted) pf::view_fit(view_);
  else pf::view_clamp(view_);
  dirty_ = true;
  if (host_) host_->OnViewChanged();
}

void MapWindow::MarkTilesChanged(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) { MarkMapChanged(); return; }
  // Padded, because an edit is felt beyond the tiles it names: the corner model
  // legalises a margin around a stroke, and a unit's sprite reaches well past
  // its own tile.
  constexpr int kBleed = 12;
  const int nx0 = x - kBleed, ny0 = y - kBleed;
  const int nx1 = x + w - 1 + kBleed, ny1 = y + h - 1 + kBleed;
  if (patch_x1_ < patch_x0_) {
    patch_x0_ = nx0; patch_y0_ = ny0; patch_x1_ = nx1; patch_y1_ = ny1;
  } else {
    patch_x0_ = std::min(patch_x0_, nx0);
    patch_y0_ = std::min(patch_y0_, ny0);
    patch_x1_ = std::max(patch_x1_, nx1);
    patch_y1_ = std::max(patch_y1_, ny1);
  }
  dirty_ = true;
  Invalidate();
}

bool MapWindow::ComposePatch() {
  if (patch_x1_ < patch_x0_ || pixels_.empty()) return false;
  // Only when the buffer on hand still describes the same region: a scroll or
  // a zoom moves every pixel, and there is nothing to patch into.
  int x0 = 0, y0 = 0, cols = 0, rows = 0;
  pf::view_region(view_, x0, y0, cols, rows);
  // A patch redraws part of the last composition, so it cannot serve a change
  // of layer: the tiles it does not touch would keep the old one.
  if (editor_ && editor_->VisibleOverlay() != composed_overlay_) return false;
  if (x0 != composed_x0_ || y0 != composed_y0_ || cols != composed_cols_ ||
      rows != composed_rows_ || view_.zoom != composed_zoom_) {
    return false;
  }

  // What has to come out right, clipped to what is on screen.
  const int wx0 = std::max(x0, patch_x0_);
  const int wy0 = std::max(y0, patch_y0_);
  const int wx1 = std::min(x0 + cols - 1, patch_x1_);
  const int wy1 = std::min(y0 + rows - 1, patch_y1_);
  if (wx1 < wx0 || wy1 < wy0) {
    patch_x1_ = patch_x0_ - 1;      // entirely off screen: nothing to draw
    dirty_ = false;
    composed_revision_ = editor_ ? editor_->revision() : 0;
    return true;
  }

  // Composed with a margin around that, and the margin thrown away. A region
  // clips what it draws to its own edge, so the outermost tiles of a composed
  // region are not what the same tiles look like in a bigger one — copying only
  // the middle is what keeps a seam off the edge of every patch.
  constexpr int kGuard = 8;
  const int rx0 = std::max(x0, wx0 - kGuard);
  const int ry0 = std::max(y0, wy0 - kGuard);
  const int rx1 = std::min(x0 + cols - 1, wx1 + kGuard);
  const int ry1 = std::min(y0 + rows - 1, wy1 + kGuard);
  const int rcols = rx1 - rx0 + 1, rrows = ry1 - ry0 + 1;

  // Past about half the region the patch stops paying for itself: two composes
  // of most of it cost more than one of all of it.
  if (int64_t(rcols) * rrows * 2 > int64_t(cols) * rows) return false;

  pf_render_options o = ComposeOptions(rx0, ry0, rcols, rrows);
  const int needed = rcols * rrows * 1024;
  patch_pixels_.resize(size_t(needed));
  if (pf_map_compose_region(map_, &o, patch_pixels_.data(), patch_pixels_.size()) !=
      needed) {
    return false;
  }

  // Row by row into the buffer already on screen. Both are 32 px per tile, so
  // the arithmetic is in whole tiles times 32.
  const int stride = cols * 32;
  const int src_stride = rcols * 32;
  const int wide = (wx1 - wx0 + 1) * 32;
  for (int row = 0; row < (wy1 - wy0 + 1) * 32; row++) {
    const size_t src = size_t((wy0 - ry0) * 32 + row) * size_t(src_stride) +
                       size_t((wx0 - rx0) * 32);
    const size_t dst = size_t((wy0 - y0) * 32 + row) * size_t(stride) +
                       size_t((wx0 - x0) * 32);
    std::copy_n(patch_pixels_.data() + src, size_t(wide), pixels_.data() + dst);
  }
  patch_x1_ = patch_x0_ - 1;   // consumed
  dirty_ = false;
  composed_revision_ = editor_ ? editor_->revision() : 0;
  return true;
}

pf_render_options MapWindow::ComposeOptions(int x0, int y0, int cols, int rows) const {
  pf_render_options o = {};
  o.x0 = x0;
  o.y0 = y0;
  o.cols = cols;
  o.rows = rows;
  o.art = art_;
  o.sprites = sprites_;
  o.vary_facing = vary_facing_ ? 1 : 0;
  o.placeholders = 1;
  if (editor_) {
    o.overlay = editor_->VisibleOverlay();
    o.unit_filter = editor_->unit_filter;
    o.grid = editor_->show_grid ? 1 : 0;
    o.mark_special = editor_->mark_special_units ? 1 : 0;
  }
  return o;
}

void MapWindow::Compose() {
  if (!map_) return;
  int x0 = 0, y0 = 0, cols = 0, rows = 0;
  pf::view_region(view_, x0, y0, cols, rows);
  if (cols <= 0 || rows <= 0) return;

  const int revision = editor_ ? editor_->revision() : 0;
  const int overlay = editor_ ? editor_->VisibleOverlay() : PF_OVERLAY_NONE;
  if (!dirty_ && revision == composed_revision_ && x0 == composed_x0_ &&
      y0 == composed_y0_ && cols == composed_cols_ && rows == composed_rows_ &&
      view_.zoom == composed_zoom_ && overlay == composed_overlay_) {
    return;   // nothing moved and nothing changed
  }
  composed_overlay_ = overlay;

  if (ComposePatch()) return;
  patch_x1_ = patch_x0_ - 1;   // a full compose covers whatever it named

  pf_render_options o = ComposeOptions(x0, y0, cols, rows);
  const int needed = pf_map_compose_region(map_, &o, nullptr, 0);
  if (needed <= 0) return;
  pixels_.resize(size_t(needed));
  if (pf_map_compose_region(map_, &o, pixels_.data(), pixels_.size()) != needed) return;

  composed_x0_ = x0;
  composed_y0_ = y0;
  composed_cols_ = cols;
  composed_rows_ = rows;
  composed_zoom_ = view_.zoom;
  composed_revision_ = revision;
  dirty_ = false;
}

RECT MapWindow::TileRectToScreen(int tx, int ty, int w, int h) const {
  const int px = pf::view_tile_px(view_);
  int ox = 0, oy = 0;
  pf::view_origin(view_, tx, ty, ox, oy);
  return {ox, oy, ox + w * px, oy + h * px};
}

void MapWindow::OnPaint() {
  PAINTSTRUCT ps;
  HDC screen = BeginPaint(hwnd_, &ps);
  Compose();

  // Everything goes to a bitmap first and reaches the screen in one blit.
  // Drawing the layers straight onto the window shows as a flicker under the
  // pointer during a drag.
  RECT client;
  GetClientRect(hwnd_, &client);
  HDC dc = screen;
  if (!back_dc_ || back_w_ != client.right || back_h_ != client.bottom) {
    ReleaseBackBuffer();
    back_dc_ = CreateCompatibleDC(screen);
    back_bitmap_ = CreateCompatibleBitmap(screen, client.right, client.bottom);
    if (back_dc_ && back_bitmap_) {
      back_old_ = SelectObject(back_dc_, back_bitmap_);
      back_w_ = client.right;
      back_h_ = client.bottom;
    } else {
      ReleaseBackBuffer();
    }
  }
  if (back_dc_) dc = back_dc_;

  auto present = [&] {
    if (dc != screen) {
      BitBlt(screen, ps.rcPaint.left, ps.rcPaint.top,
             ps.rcPaint.right - ps.rcPaint.left,
             ps.rcPaint.bottom - ps.rcPaint.top, dc, ps.rcPaint.left,
             ps.rcPaint.top, SRCCOPY);
    }
    EndPaint(hwnd_, &ps);
  };

  if (pixels_.empty()) {
    FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_APPWORKSPACE));
    present();
    return;
  }

  const int src_w = composed_cols_ * 32;
  const int src_h = composed_rows_ * 32;
  const int px = pf::view_tile_px(view_);
  int ox = 0, oy = 0;
  pf::view_origin(view_, composed_x0_, composed_y0_, ox, oy);

  // The map may not cover the window; paint the four workspace strips around
  // it rather than the whole rect, so there is no flash under the map.
  const RECT dest = {ox, oy, ox + composed_cols_ * px, oy + composed_rows_ * px};
  HBRUSH workspace = GetSysColorBrush(COLOR_APPWORKSPACE);
  RECT strip;
  if (dest.top > 0) {
    strip = {0, 0, client.right, dest.top};
    FillRect(dc, &strip, workspace);
  }
  if (dest.bottom < client.bottom) {
    strip = {0, dest.bottom, client.right, client.bottom};
    FillRect(dc, &strip, workspace);
  }
  if (dest.left > 0) {
    strip = {0, dest.top, dest.left, dest.bottom};
    FillRect(dc, &strip, workspace);
  }
  if (dest.right < client.right) {
    strip = {dest.right, dest.top, client.right, dest.bottom};
    FillRect(dc, &strip, workspace);
  }

  BlitRgba(dc, ox, oy, composed_cols_ * px, composed_rows_ * px, src_w, src_h,
           pixels_.data());

  DrawOverlays(dc);
  present();
}

void MapWindow::ReleaseBackBuffer() {
  if (back_dc_) {
    if (back_old_) SelectObject(back_dc_, back_old_);
    DeleteDC(back_dc_);
  }
  if (back_bitmap_) DeleteObject(back_bitmap_);
  back_dc_ = nullptr;
  back_bitmap_ = nullptr;
  back_old_ = nullptr;
  back_w_ = back_h_ = 0;
}

void MapWindow::DrawOverlays(HDC dc) {
  if (!editor_ || !map_) return;

  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

  // Selection rectangles around every selected unit's footprint.
  HPEN white = CreatePen(PS_SOLID, kOutlinePx, RGB(255, 255, 255));
  HGDIOBJ old_pen = SelectObject(dc, white);
  for (int index : editor_->selected()) {
    pf_unit u{};
    if (pf_map_unit(map_, index, &u) != PF_OK) continue;
    int fw = 1, fh = 1;
    pf_map_unit_footprint(map_, u.type, &fw, &fh);
    RECT rect = TileRectToScreen(u.x, u.y, fw, fh);
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
  }

  HPEN dashed = CreateDottedPen(RGB(255, 255, 255));
  SelectObject(dc, dashed);
  SetBkMode(dc, TRANSPARENT);

  // The terrain selection, which persists between drags. Outlined round its
  // real shape rather than its box: shift and alt build regions that are not
  // rectangles, and a box round an L lies about what Fill will paint.
  const TileRect& terrain = editor_->terrain_selection();
  if (!terrain.empty()) {
    if (editor_->TerrainSelectionIsRect()) {
      RECT rect = TileRectToScreen(terrain.x, terrain.y, terrain.w, terrain.h);
      Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    } else if (HRGN shape = SelectionRegion(terrain)) {
      HBRUSH ink = CreateSolidBrush(RGB(255, 255, 255));
      FrameRgn(dc, shape, ink, kOutlinePx, kOutlinePx);
      DeleteObject(ink);
      DeleteObject(shape);
    }
  }

  // The rubber band, live during a band drag.
  if (drag_ == Drag::kBand && hover_tx_ >= 0) {
    const int x0 = std::min(anchor_tx_, hover_tx_);
    const int y0 = std::min(anchor_ty_, hover_ty_);
    const int x1 = std::max(anchor_tx_, hover_tx_);
    const int y1 = std::max(anchor_ty_, hover_ty_);
    RECT rect = TileRectToScreen(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
  }

  // An armed paste follows the pointer as a picture of the fragment. An outline
  // alone said how big it was and nothing about what was in it, which is the
  // question when the thing was copied several edits ago and rotated twice.
  if (editor_->pasting() && hover_tx_ >= 0 && drag_ != Drag::kPan) {
    const pf_clipboard* clip = editor_->clipboard();
    const TileRect frag = editor_->ClipboardBounds();
    const bool fits = hover_tx_ + frag.w <= pf_map_width(map_) &&
                      hover_ty_ + frag.h <= pf_map_height(map_);
    DrawClipboardTerrain(dc, clip, frag);
    // The outline answers "will this paste land whole", so it takes the worst
    // of what the ghosts found rather than only whether the box is on the map.
    // A single ship tinted red inside a green box reads as decoration.
    bool all_stand = fits;
    for (int i = 0, n = pf_clipboard_unit_count(clip); i < n; i++) {
      pf_unit u{};
      if (pf_clipboard_unit(clip, i, &u) != PF_OK) continue;
      int fw = 1, fh = 1;
      pf_map_unit_footprint(map_, u.type, &fw, &fh);
      // The sprite, the way the placement ghost draws one, in the fragment's
      // own colours. Red where paste will drop it: a rotated fragment puts
      // ships on grass, and the preview said nothing about it until the paste
      // landed.
      const bool ok = fits && editor_->PasteWouldPlace(hover_tx_ + u.x,
                                                       hover_ty_ + u.y, u.type);
      all_stand = all_stand && ok;
      DrawClipboardUnit(dc, u, fw, fh, ok);
    }
    HPEN edge = CreatePen(PS_SOLID, kOutlinePx, all_stand ? kAllowed : kRefused);
    SelectObject(dc, edge);
    RECT outline = TileRectToScreen(hover_tx_, hover_ty_, frag.w, frag.h);
    Rectangle(dc, outline.left, outline.top, outline.right, outline.bottom);
    SelectObject(dc, dashed);
    DeleteObject(edge);
  }

  // The pointer-following marks: brush outline when painting, footprint ghost
  // when placing - green where it may stand, red where it may not.
  if (!editor_->pasting() && hover_tx_ >= 0 && drag_ != Drag::kPan) {
    // The movement brush is the same brush, so it gets the same outline: it is
    // the one mark that says what the next click covers, and painting a layer
    // you cannot see needs it more than terrain does, not less.
    const bool painting = editor_->tool() == Tool::kPaint ||
                          editor_->tool() == Tool::kWalkable;
    if (painting) {
      const bool walkable = editor_->tool() == Tool::kWalkable;
      const int shape = editor_->brush_shape;
      const int size = shape == Editor::kShapeFill ? 1
                       : walkable                  ? editor_->MovementBrushSize()
                                                   : editor_->brush_size;
      // The corner brush marks an intersection, not a square, so it gets a small
      // box centred on the corner rather than an outline round a tile. Drawn
      // from the pointer's pixels for the same reason the paint is.
      // Never for the movement brush, which has no corner rung to mark.
      const bool corner = !walkable && size == PF_BRUSH_SIZE_CORNER &&
                          shape != Editor::kShapeFill;
      if (corner) {
        int cx = 0, cy = 0;
        if (pf::view_corner_at(view_, hover_px_, hover_py_, cx, cy)) {
          const RECT at = TileRectToScreen(cx, cy, 1, 1);
          const int arm = std::max(3, pf::view_tile_px(view_) / 4);
          // The dashed white pen already selected, which is what every other
          // brush outline is drawn with. A colour of its own would say the mark
          // means something different — green is the placement ghost's "may
          // stand here", and this is not a placement.
          //
          // Centred on the corner: TileRectToScreen gives the tile that starts
          // at it, so back off by an arm each way rather than boxing one tile.
          Rectangle(dc, at.left - arm, at.top - arm, at.left + arm, at.top + arm);
        }
      }
      // Every size above the bottom rung covers whole tiles, so it gets the
      // outline round them. The corner brush has drawn its own mark above; the
      // tile outline would say it covers a tile, which is the thing it is for
      // not doing. Size 0 would also make `r` and the disc below meaningless.
      const int r = corner ? 0 : (size - 1) / 2;
      // A round brush gets a round outline. The square was close enough while
      // the size was a number in a slider; with the wheel changing it under the
      // pointer, what is about to be painted should be what is drawn.
      //
      // The spray is round too — the core discs both shapes — so it takes the
      // same outline. Drawn at full density rather than the stroke's, because
      // the outline answers "what does this cover", and the puff itself is a
      // different scatter every tick; an outline that speckled with it would
      // report the last puff rather than the reach of the next one.
      HRGN covered = nullptr;
      if ((shape == PF_BRUSH_CIRCLE || shape == PF_BRUSH_SCATTER) && size > 1) {
        std::vector<int> points(size_t(size) * size_t(size) * 2);
        const int count = pf_brush_points(hover_tx_, hover_ty_, size, shape, 1.0f,
                                          nullptr, points.data(),
                                          int(points.size()));
        for (int i = 0; i < count; i++) {
          RECT one = TileRectToScreen(points[size_t(i) * 2],
                                      points[size_t(i) * 2 + 1], 1, 1);
          HRGN tile = CreateRectRgn(one.left, one.top, one.right, one.bottom);
          if (!covered) { covered = tile; continue; }
          CombineRgn(covered, covered, tile, RGN_OR);
          DeleteObject(tile);
        }
      }
      if (covered) {
        HBRUSH ink = CreateSolidBrush(RGB(255, 255, 255));
        FrameRgn(dc, covered, ink, kOutlinePx, kOutlinePx);
        DeleteObject(ink);
        DeleteObject(covered);
      } else if (!corner) {
        RECT rect = TileRectToScreen(hover_tx_ - r, hover_ty_ - r, size, size);
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
      }
    } else if (editor_->tool() == Tool::kPlace) {
      int ox = 0, oy = 0;
      editor_->PlaceOrigin(hover_tx_, hover_ty_, editor_->placing_type, ox, oy);
      int fw = 1, fh = 1;
      pf_map_unit_footprint(map_, editor_->placing_type, &fw, &fh);
      const bool ok =
          editor_->PlacementRefusal(ox, oy, editor_->placing_type).empty();
      // The sprite first, the outline over it: the outline says which tiles are
      // claimed, and a 96 px mine drawn on top would hide the corner the
      // footprint is being aimed by.
      DrawPlacementGhost(dc, editor_->placing_type, ox, oy, fw, fh, ok);
      HPEN ghost = CreatePen(PS_SOLID, kOutlinePx, ok ? kAllowed : kRefused);
      SelectObject(dc, ghost);
      RECT rect = TileRectToScreen(ox, oy, fw, fh);
      Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
      SelectObject(dc, dashed);
      DeleteObject(ghost);
    }
  }

  DrawRefusedMove(dc);
  DrawReach(dc);
  DrawSymmetryAxes(dc);

  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(dashed);
  DeleteObject(white);
}

/// The terrain selection's outline, as a region.
///
/// One rectangle per unbroken run of selected tiles in a row rather than one
/// per tile: a 128x128 selection is 16,384 tiles and 128 runs, and combining
/// sixteen thousand regions is a visible pause on every repaint.
///
/// The caller owns the region. Null when there is nothing to draw.
HRGN MapWindow::SelectionRegion(const TileRect& box) const {
  if (!editor_ || box.empty()) return nullptr;
  HRGN shape = nullptr;
  for (int y = box.y; y < box.y + box.h; y++) {
    int run = -1;
    for (int x = box.x; x <= box.x + box.w; x++) {
      const bool inside = x < box.x + box.w && editor_->TerrainSelected(x, y);
      if (inside && run < 0) run = x;
      if (inside || run < 0) continue;
      const RECT one = TileRectToScreen(run, y, x - run, 1);
      HRGN piece = CreateRectRgn(one.left, one.top, one.right, one.bottom);
      if (!shape) {
        shape = piece;
      } else {
        CombineRgn(shape, shape, piece, RGN_OR);
        DeleteObject(piece);
      }
      run = -1;
    }
  }
  return shape;
}

/// Rasterise the fragment's terrain into `frag_pixels_`, once per fragment.
///
/// The fragment holds corner terrains rather than tile values, so drawing it
/// means running the same corner-to-tile step the paste runs and then asking
/// the tileset for the artwork. Both are the core's.
///
/// Cached because it does not follow the pointer: the same picture wherever it
/// is about to land, so it is rasterised when the clipboard changes and blitted
/// on every mouse move.
///
/// Flat corner colours are the fallback, and what a fragment larger than
/// kFragmentTileCap gets — 32 pixels a tile is 4 KB per tile, so a whole-map
/// copy would be sixty megabytes to show something about to move anyway.
void MapWindow::BuildFragmentPixels(const pf_clipboard* clip, const TileRect& frag) {
  if (!editor_) return;
  const int revision = editor_->clipboard_revision();
  if (revision == frag_revision_ && frag_w_ == frag.w && frag_h_ == frag.h) {
    return;   // same fragment, same shape: the picture already made is right
  }
  frag_revision_ = revision;
  frag_w_ = frag.w;
  frag_h_ = frag.h;
  frag_pixels_.clear();
  frag_art_ = false;
  if (frag.empty()) return;

  const int tileset = pf_map_tileset(map_);
  /// How many tiles are worth rasterising at the artwork's own resolution.
  constexpr int kFragmentTileCap = 96 * 96;
  const bool detailed = art_ && int64_t(frag.w) * frag.h <= kFragmentTileCap;
  // One pixel per corner when there is no artwork to draw with, which is two
  // per tile each way: enough to see a coastline in, and the same flat colours
  // the renderer itself falls back to.
  const int scale = detailed ? 32 : 2;
  const int cw = frag.w * scale, ch = frag.h * scale;
  frag_pixels_.assign(size_t(cw) * size_t(ch), 0);
  frag_art_ = detailed;

  for (int ty = 0; ty < frag.h; ty++) {
    for (int tx = 0; tx < frag.w; tx++) {
      // A hole is left at zero, which is transparent to the blend, so the map
      // shows through exactly where the paste will leave it alone. A fragment
      // cut from two separate squares has to look like two squares.
      if (!pf_clipboard_tile_included(clip, tx, ty)) continue;
      // The four corners *around* the tile, in the order pf_tile_quadrants hands
      // them back: (tx, ty) is the tile's top-left, and the corner grid is one
      // wider and one taller than the tile grid.
      uint8_t corners[4] = {};
      bool known = true;
      for (int c = 0; c < 4; c++) {
        const int terrain =
            pf_clipboard_corner(clip, tx + (c & 1), ty + (c >> 1));
        if (terrain < 0) known = false;
        corners[c] = uint8_t(terrain >= 0 ? terrain : PF_TERRAIN_UNKNOWN);
      }

      if (detailed) {
        // Salted by position so a fragment does not shimmer between drawings as
        // it is dragged, and two tiles of the same terrain can still differ.
        const uint32_t salt = uint32_t(tx) * 73856093u ^ uint32_t(ty) * 19349663u;
        const int tile = known ? pf_map_tile_for_corners(map_, corners, salt) : -1;
        const int mega =
            tile >= 0 ? pf_tileset_art_megatile_for(art_, uint16_t(tile)) : -1;
        if (mega >= 0 && !pf_tileset_art_is_blank(art_, mega)) {
          pf_tileset_art_draw(art_, mega,
                              frag_pixels_.data() + size_t(ty) * 32 * size_t(cw) +
                                  size_t(tx) * 32,
                              cw);
          continue;
        }
      }

      // No artwork, no drawable tile, or a fragment too big to be worth it:
      // the corners' own flat colours, filling whatever cell they were given.
      for (int c = 0; c < 4; c++) {
        const uint32_t rgb = pf_terrain_flat_colour(corners[c], tileset);
        // The core hands 0x00RRGGBB; the blit wants its own RGBA packing.
        const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        const uint32_t packed = 0xFF000000u | (b << 16) | (g << 8) | r;
        const int half = scale / 2;
        const int x0 = tx * scale + (c & 1) * half;
        const int y0 = ty * scale + (c >> 1) * half;
        for (int y = 0; y < half; y++) {
          uint32_t* row = frag_pixels_.data() + size_t(y0 + y) * size_t(cw) + size_t(x0);
          for (int x = 0; x < half; x++) row[x] = packed;
        }
      }
    }
  }
}

/// The terrain a pending paste carries, drawn where it would land.
///
/// Blended rather than opaque: the point is to see it against the ground it is
/// about to cover, which is the thing being judged.
void MapWindow::DrawClipboardTerrain(HDC dc, const pf_clipboard* clip,
                                     const TileRect& frag) {
  if (!clip || !pf_clipboard_has_terrain(clip) || frag.empty()) return;
  const int px = pf::view_tile_px(view_);
  if (px <= 0) return;
  BuildFragmentPixels(clip, frag);
  if (frag_pixels_.empty()) return;

  const int scale = frag_art_ ? 32 : 2;
  const RECT where = TileRectToScreen(hover_tx_, hover_ty_, frag.w, frag.h);
  BlitRgbaBlended(dc, where.left, where.top, frag.w * px, frag.h * px,
                  frag.w * scale, frag.h * scale, frag_pixels_.data(),
                  kFragmentAlpha, 0, 0);
}

/// A unit drawn as a proposal rather than as part of the map.
///
/// The three things the canvas proposes — the unit about to be placed, the units
/// a paste carries, the units a drag is moving — are the same picture with
/// different reasons behind it. A footprint outline says how many tiles are
/// claimed and nothing about what goes in them, which is the question when the
/// palette holds 110 units and most are 1x1.
///
/// The same artwork pf_map_compose_region draws the map's units from, blended
/// so it stays a proposal, tinted red where it would be refused, and centred on
/// the footprint the way the composer centres it.
void MapWindow::DrawUnitGhost(HDC dc, int type, int owner, int ox, int oy,
                              int fw, int fh, bool ok) {
  if (!icons_) return;
  // Scenery lands on the neutral slot whoever owns it (see
  // Editor::PlaceOneUnit), so a ghost in the player's colour would be a
  // promise the drop will not keep.
  const int forced = pf_unit_default_owner(type);
  const Icon& sprite = icons_->Sprite(type, forced >= 0 ? forced : owner);
  if (sprite.empty()) return;   // no artwork: the outline is the whole mark

  const int px = pf::view_tile_px(view_);
  const RECT foot = TileRectToScreen(ox, oy, fw, fh);
  const int dw = std::max(1, sprite.w * px / kArtTilePx);
  const int dh = std::max(1, sprite.h * px / kArtTilePx);
  BlitRgbaBlended(dc, foot.left + (fw * px - dw) / 2,
                  foot.top + (fh * px - dh) / 2, dw, dh, sprite.w, sprite.h,
                  sprite.px.data(), kGhostAlpha, ok ? 0 : kRefusedTintRgb,
                  ok ? 0 : kRefusedTintPct);
}

/// One unit of a pending paste, drawn where it would land.
///
/// The fragment's own owner rather than the palette's, since that is who it
/// will belong to once it lands.
void MapWindow::DrawClipboardUnit(HDC dc, const pf_unit& unit, int fw, int fh,
                                  bool ok) {
  DrawUnitGhost(dc, unit.type, unit.owner, hover_tx_ + unit.x,
                hover_ty_ + unit.y, fw, fh, ok);
}

/// The unit about to be placed, drawn where it would land.
void MapWindow::DrawPlacementGhost(HDC dc, int type, int ox, int oy, int fw,
                                   int fh, bool ok) {
  if (!editor_) return;
  DrawUnitGhost(dc, type, editor_->placing_owner, ox, oy, fw, fh, ok);
}

/// Where a refused drag is trying to put the selection.
///
/// Editor::MoveSelectionBy is all-or-nothing, so a drag that reaches ground the
/// selection cannot stand on stops committing and the units sit at the last
/// tile that worked — which looked like the drag had been dropped. Drawing the
/// refusal says otherwise: the units follow the pointer in red, and the white
/// rectangles say where they will stay if the button comes up here.
void MapWindow::DrawRefusedMove(HDC dc) {
  if (!editor_ || !map_) return;
  if (drag_ != Drag::kMove) return;
  if (move_pending_dx_ == 0 && move_pending_dy_ == 0) return;

  HPEN red = CreatePen(PS_SOLID, kOutlinePx, kRefused);
  HGDIOBJ old_pen = SelectObject(dc, red);
  for (int index : editor_->selected()) {
    pf_unit u{};
    if (pf_map_unit(map_, index, &u) != PF_OK) continue;
    int fw = 1, fh = 1;
    pf_map_unit_footprint(map_, u.type, &fw, &fh);
    const int ox = u.x + move_pending_dx_, oy = u.y + move_pending_dy_;
    DrawUnitGhost(dc, u.type, u.owner, ox, oy, fw, fh, false);
    RECT rect = TileRectToScreen(ox, oy, fw, fh);
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
  }
  SelectObject(dc, old_pen);
  DeleteObject(red);
}

/// The index of a UDTA field by name, found once. The table describes the
/// format rather than a map, so this is the same answer forever.
static int UdtaField(const char* want) {
  for (int i = 0; i < pf_udta_field_count(); i++) {
    const char* name = pf_udta_field_name(i);
    if (name && std::string(name) == want) return i;
  }
  return -1;
}

/// How far the selected units see and shoot.
///
/// Radii in tiles from the unit's centre, drawn as circles because that is the
/// shape the game uses. Sight is the wide faint one and range the tight bright
/// one; a melee unit's range of 1 is its own footprint, so it gets no ring.
void MapWindow::DrawReach(HDC dc) {
  if (!show_reach_ || !editor_ || !map_ || editor_->selected().empty()) return;

  // The map's own table where it carries one, the game's where it does not —
  // a map that gives its towers more range should draw the range it gives.
  static const int kSight = UdtaField("sight");
  static const int kRange = UdtaField("attackRange");
  if (kSight < 0 || kRange < 0) return;
  const bool from_map = pf_map_has_unit_data(map_) != 0;
  auto field = [&](int which, int type) {
    return int(from_map ? pf_map_unit_field(map_, which, type, 0)
                        : pf_udta_default_field(which, type, 0));
  };

  const int px = pf::view_tile_px(view_);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  HPEN sight_pen = CreatePen(PS_DOT, 1, RGB(200, 200, 200));
  HPEN range_pen = CreatePen(PS_SOLID, std::max(1, px / 16), RGB(255, 255, 255));

  for (int index : editor_->selected()) {
    pf_unit u{};
    if (pf_map_unit(map_, index, &u) != PF_OK) continue;
    int fw = 1, fh = 1;
    pf_map_unit_footprint(map_, u.type, &fw, &fh);
    const RECT foot = TileRectToScreen(u.x, u.y, fw, fh);
    const int cx = (foot.left + foot.right) / 2;
    const int cy = (foot.top + foot.bottom) / 2;
    // The footprint counts: a 4x4 keep shoots four tiles further than its
    // centre suggests, which is exactly the mistake this exists to stop.
    const int edge = std::max(fw, fh) * px / 2;

    const int sight = field(kSight, u.type);
    if (sight > 0) {
      SelectObject(dc, sight_pen);
      const int r = sight * px + edge;
      Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    }
    const int range = field(kRange, u.type);
    if (range > 1) {
      SelectObject(dc, range_pen);
      const int r = range * px + edge;
      Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    }
  }
  SelectObject(dc, old_brush);
  DeleteObject(sight_pen);
  DeleteObject(range_pen);
}

/// The axes a mirrored stroke reflects across.
///
/// With symmetry on, an edit lands somewhere the pointer is not, and the axis is
/// the only thing that explains where. Between tiles rather than through one —
/// the reflection of tile x is `width - 1 - x`, so no tile is its own mirror.
void MapWindow::DrawSymmetryAxes(HDC dc) {
  if (!editor_ || !map_ || editor_->mirrors == PF_MIRROR_NONE) return;
  const int w = pf_map_width(map_), h = pf_map_height(map_);
  HPEN axis = CreatePen(PS_DOT, 1, RGB(74, 209, 255));
  HGDIOBJ old = SelectObject(dc, axis);

  auto line = [&](int x0, int y0, int x1, int y1) {
    const RECT a = TileRectToScreen(x0, y0, 1, 1);
    const RECT b = TileRectToScreen(x1, y1, 1, 1);
    MoveToEx(dc, a.left, a.top, nullptr);
    LineTo(dc, b.left, b.top);
  };
  const int mirrors = editor_->mirrors;
  if (mirrors & PF_MIRROR_LEFT_RIGHT) line(w / 2, 0, w / 2, h);
  if (mirrors & PF_MIRROR_TOP_BOTTOM) line(0, h / 2, w, h / 2);
  // The diagonals only mean anything on a square map, which every PUD is.
  if (mirrors & PF_MIRROR_DIAG_NW_SE) line(0, 0, w, h);
  if (mirrors & PF_MIRROR_DIAG_SW_NE) line(w, 0, 0, h);

  SelectObject(dc, old);
  DeleteObject(axis);
}

void MapWindow::OnMouseDown(int button, int x, int y, WPARAM keys) {
  if (!map_ || !editor_) return;
  // A press whose button-up never arrived — a capture stolen mid-gesture —
  // must not suppress the next menu.
  right_press_consumed_ = false;
  SetCapture(hwnd_);
  drag_from_ = {x, y};

  // Middle button pans by *direction*: the press point is an anchor and the
  // view scrolls towards wherever the pointer has been pushed, for as long as
  // the button is down. A grab-and-drag pan is bounded by how far the arm can
  // move before the mouse runs out of desk; this one is not, and holding still
  // is how you stop.
  //
  // Shift is deliberately not a pan: a person holding shift is selecting.
  if (button == 1) {
    drag_ = Drag::kPan;
    pan_pointer_ = {x, y};
    pan_tick_ = GetTickCount();
    SetTimer(hwnd_, kPanTimer, kPanTickMs, nullptr);
    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
    return;
  }

  int tx = 0, ty = 0;
  if (!pf::view_tile_at(view_, x, y, tx, ty)) return;

  // An armed paste owns the next click, whatever tool is current: the preview
  // under the pointer is what the user is aiming. Right button cancels.
  if (editor_->pasting()) {
    ReleaseCapture();
    drag_ = Drag::kNone;
    if (button == 2) {
      editor_->CancelPaste();
      right_press_consumed_ = true;
      if (host_) host_->OnStatus(Str(IDS_PASTE_CANCELLED), false);
    } else {
      const int carried = pf_clipboard_unit_count(editor_->clipboard());
      const int placed = editor_->PasteAt(tx, ty);
      if (placed < 0) {
        if (host_) host_->OnStatus(FromUtf8(editor_->last_refusal), true);
      } else if (host_) {
        // Terrain always lands whole; units only where they can stand. Saying
        // so matters — a silent paste of "3 units" that placed one is how a map
        // loses a base without anyone noticing until much later.
        if (placed < carried) {
          host_->OnStatus(
              Format(IDS_PASTED_SHORT, placed, carried, carried - placed), true);
        } else if (carried > 0) {
          host_->OnStatus(
              Format(Plural(placed, IDS_PASTED_ONE, IDS_PASTED_ALL), placed), false);
        } else {
          host_->OnStatus(Str(IDS_PASTED_TERRAIN_ONLY), false);
        }
        host_->OnMapEdited();
      }
    }
    Invalidate();
    return;
  }

  // The right button opens the context menu on button-up, so a press that turns
  // into something else still can: a right-click while painting means "stop
  // painting", and a menu as well would answer a question nobody asked.
  if (button == 2) {
    ReleaseCapture();
    drag_ = Drag::kNone;
    if (LeaveActiveTool()) right_press_consumed_ = true;
    return;
  }

  // Alt, held, borrows the other shade of the terrain. Read from the press as
  // well as from the keyboard because an Alt held down before the canvas had
  // focus generates no key message here at all. A mouse message carries no bit
  // for Alt the way it does for Shift and Ctrl, so this asks the keyboard.
  ApplyShiftShade((GetKeyState(VK_MENU) & 0x8000) != 0);

  // Ctrl and the left button: the eyedropper, the same chord the web client
  // uses. Over terrain it adopts the terrain, over units the type — the same
  // act, and it saves hunting a unit down in a palette of a hundred and ten.
  if (keys & MK_CONTROL) {
    if (editor_->mode() == Mode::kTerrain) {
      const bool picked = editor_->PickBrush(tx, ty);
      if (host_) {
        host_->OnEditorChanged();
        // Say what was adopted: an eyedropper that changes the palette in
        // silence looks the same as one that missed.
        if (picked) host_->OnStatus(FromUtf8(editor_->BrushName()), false);
      }
    } else if (editor_->mode() == Mode::kMovement) {
      // The same act on the layer: read what a tile already says, then paint
      // more of it. The panel names the value, so the status bar need not.
      if (editor_->PickMovement(tx, ty) && host_) host_->OnEditorChanged();
    } else {
      const int index = editor_->PickUnitType(tx, ty);
      if (host_) {
        host_->OnEditorChanged();
        const char* name = index >= 0 ? pf_unit_name(editor_->placing_type) : nullptr;
        if (name) host_->OnStatus(FromUtf8(name), false);
      }
    }
    ReleaseCapture();
    drag_ = Drag::kNone;
    Invalidate();
    return;
  }

  anchor_tx_ = last_tx_ = hover_tx_ = tx;
  anchor_ty_ = last_ty_ = hover_ty_ = ty;
  hover_px_ = x;
  hover_py_ = y;

  switch (editor_->tool()) {
    case Tool::kWalkable: {
      // The same shape as a terrain stroke: one undo step for the drag, and the
      // pointer paints as it moves.
      drag_ = Drag::kPaint;
      editor_->BeginStroke();
      const int changed = editor_->PaintMovementAt(tx, ty);
      if (host_ && changed) host_->OnMapStroke();
      Invalidate();
      break;
    }
    case Tool::kPaint:
      // Shift over the bucket means every tile of that terrain rather than the
      // region touching this one. Taken before BeginStroke: it is a bulk edit
      // with its own checkpoint, not a stroke somebody drags.
      if (editor_->brush_shape == Editor::kShapeFill &&
          (keys & MK_SHIFT) && !editor_->BrushIsCustom()) {
        const int changed = editor_->FillTerrainEverywhere(tx, ty);
        if (host_) {
          host_->OnStatus(changed > 0 ? Format(IDS_FILLED_EVERYWHERE, changed)
                                      : Str(IDS_NOTHING_CHANGED),
                          changed == 0);
          if (changed > 0) { MarkMapChanged(); host_->OnMapEdited(); }
        }
        Invalidate();
        return;
      }
      // One stroke is one undo step: the checkpoint is taken here, on
      // button-down, and never inside the move handler.
      editor_->BeginStroke();
      if (editor_->brush_shape == PF_BRUSH_SCATTER) {
        drag_ = Drag::kSpray;
        spray_since_ = GetTickCount();
        editor_->SprayAt(tx, ty, 0);
        SetTimer(hwnd_, kSprayTimer, UINT(pf_spray_tick_ms()), nullptr);
      } else {
        drag_ = Drag::kPaint;
        PaintPointer(tx, ty);
      }
      Invalidate();
      if (host_) host_->OnMapStroke();
      break;

    case Tool::kRect:
      drag_ = Drag::kRect;
      // Shift adds this drag to what is already selected, alt takes it away.
      // Decided on button-down and held for the drag, because reading the keys
      // again on every move would let a selection change meaning halfway
      // through being dragged.
      rect_pick_ = (keys & MK_SHIFT)  ? Editor::Pick::kAdd
                 : (GetKeyState(VK_MENU) & 0x8000) ? Editor::Pick::kSubtract
                                                   : Editor::Pick::kReplace;
      // Kept, so each move of the drag can start again from it rather than
      // piling every intermediate rectangle into the selection.
      rect_before_ = editor_->TerrainMask();
      editor_->SelectTerrain(tx, ty, 1, 1, rect_pick_);
      Invalidate();
      break;

    case Tool::kSelect: {
      const int under = pf_map_unit_at(map_, tx, ty);
      const bool additive = (keys & MK_SHIFT) != 0;
      // Whatever the click turns out to mean, the unit was pointed at, and that
      // is what the game answers.
      if (under >= 0 && host_) {
        pf_unit u{};
        if (pf_map_unit(map_, under, &u) == PF_OK) {
          host_->OnUnitSound(u.type, PF_SOUND_SELECTED);
        }
      }
      if (under >= 0 && !additive && editor_->selected().count(under)) {
        // Dragging an existing selection moves it. The checkpoint waits for
        // the first actual movement, so a click costs no undo step.
        drag_ = Drag::kMove;
        move_took_checkpoint_ = false;
        move_pending_dx_ = move_pending_dy_ = 0;
        move_refusal_said_.clear();
      } else if (under >= 0) {
        editor_->SelectAt(tx, ty, additive);
        drag_ = Drag::kMove;
        move_took_checkpoint_ = false;
        move_pending_dx_ = move_pending_dy_ = 0;
        move_refusal_said_.clear();
      } else {
        editor_->SelectAt(tx, ty, additive);   // clears unless additive
        drag_ = Drag::kBand;
      }
      Invalidate();
      if (host_) host_->OnEditorChanged();
      break;
    }

    case Tool::kPlace: {
      // Held, this draws with units the way the brush draws with terrain. The
      // run is one undo step and reports itself once, at button-up.
      drag_ = Drag::kPlace;
      editor_->BeginPlacementRun();
      int ox = 0, oy = 0;
      editor_->PlaceOrigin(tx, ty, editor_->placing_type, ox, oy);
      const int placed = editor_->PlaceUnit(ox, oy);
      if (host_) {
        // Only when one actually landed: a refusal is not a unit reporting for
        // duty, and the status bar has already said why.
        if (placed >= 0) host_->OnUnitSound(editor_->placing_type, PF_SOUND_READY);
        host_->OnMapEdited();
      }
      Invalidate();
      break;
    }

    case Tool::kErase:
      drag_ = Drag::kPaint;   // reuse the stroke-follows-pointer shape
      if (editor_->EraseAt(tx, ty) && host_) host_->OnMapEdited();
      Invalidate();
      break;
  }
}

bool MapWindow::PaintPointer(int tx, int ty) {
  if (!editor_) return false;
  if (!editor_->BrushIsCorner()) return editor_->PaintAt(tx, ty);
  // Rounded from the pointer's pixels rather than derived from the tile: which
  // of a tile's four corners was meant is the whole question, and the tile has
  // already thrown that away.
  int cx = 0, cy = 0;
  if (!pf::view_corner_at(view_, hover_px_, hover_py_, cx, cy)) return false;
  return editor_->PaintCornerAt(cx, cy);
}

void MapWindow::StrokeAt(int tx, int ty) {
  if (tx == last_tx_ && ty == last_ty_) return;
  last_tx_ = tx;
  last_ty_ = ty;

  switch (editor_->tool()) {
    case Tool::kWalkable:
      if (drag_ == Drag::kPaint) {
        if (editor_->PaintMovementAt(tx, ty) && host_) host_->OnMapStroke();
        Invalidate();
      }
      break;
    case Tool::kPaint:
      if (drag_ == Drag::kSpray) {
        // The timer lays the puffs; moving just re-aims it.
      } else {
        PaintPointer(tx, ty);
        if (host_) host_->OnMapStroke();
      }
      break;
    case Tool::kPlace: {
      // Every tile the pointer crosses is offered one; the core turns down what
      // will not fit, so a drag lays a row rather than a pile.
      //
      // Silently, unlike the click that began the run: each unit's sound cuts
      // off the one before it, so a drag of forty was forty fragments of the
      // same word.
      int ox = 0, oy = 0;
      editor_->PlaceOrigin(tx, ty, editor_->placing_type, ox, oy);
      if (editor_->PlaceUnit(ox, oy) >= 0 && host_) host_->OnMapEdited();
      break;
    }
    case Tool::kErase:
      if (editor_->EraseAt(tx, ty) && host_) host_->OnMapEdited();
      break;
    default:
      break;
  }
  Invalidate();
}

void MapWindow::OnMouseMove(int x, int y, WPARAM keys) {
  if (!map_ || !editor_) return;

  if (drag_ == Drag::kPan) {
    // Moving only re-aims. The timer does the scrolling, so the view keeps going
    // while the pointer is held still away from the anchor.
    pan_pointer_ = {x, y};
    return;
  }

  int tx = 0, ty = 0;
  const bool on_map = pf::view_tile_at(view_, x, y, tx, ty);
  const bool hover_moved =
      (on_map ? tx : -1) != hover_tx_ || (on_map ? ty : -1) != hover_ty_;
  hover_tx_ = on_map ? tx : -1;
  hover_ty_ = on_map ? ty : -1;
  hover_px_ = x;
  hover_py_ = y;
  if (hover_moved && host_) host_->OnHoverTile(hover_tx_, hover_ty_);

  switch (drag_) {
    case Drag::kPaint:
    case Drag::kSpray:
    case Drag::kPlace:
      if (on_map) {
        // Shift straightens a pen stroke, the way it does in every drawing
        // program: the tile is pulled onto the anchor's row or its column,
        // whichever the pointer has travelled further along.
        //
        // Re-decided on every move rather than locked at the first one, so a
        // stroke started in the wrong direction is corrected by carrying on
        // rather than by letting go and starting again.
        if (drag_ == Drag::kPaint && (keys & MK_SHIFT) && editor_ &&
            (editor_->brush_shape == PF_BRUSH_SQUARE ||
             editor_->brush_shape == PF_BRUSH_CIRCLE)) {
          if (std::abs(tx - anchor_tx_) >= std::abs(ty - anchor_ty_)) {
            ty = anchor_ty_;
          } else {
            tx = anchor_tx_;
          }
        }
        StrokeAt(tx, ty);
      }
      return;

    case Drag::kRect: {
      if (!on_map) return;
      const int x0 = std::min(anchor_tx_, tx);
      const int y0 = std::min(anchor_ty_, ty);
      // Back to what was selected when the drag began, then this rectangle once.
      // Otherwise every tile the pointer passed through would still be selected
      // after the pointer came back off it.
      editor_->SetTerrainMask(rect_before_);
      editor_->SelectTerrain(x0, y0, std::abs(tx - anchor_tx_) + 1,
                             std::abs(ty - anchor_ty_) + 1, rect_pick_);
      Invalidate();
      return;
    }

    case Drag::kBand:
      if (hover_moved) Invalidate();
      return;

    case Drag::kMove: {
      if (!on_map || (tx == last_tx_ && ty == last_ty_)) return;
      const int dx = tx - last_tx_, dy = ty - last_ty_;
      if (editor_->MoveSelectionBy(dx, dy, !move_took_checkpoint_)) {
        move_took_checkpoint_ = true;
        last_tx_ = tx;
        last_ty_ = ty;
        move_pending_dx_ = move_pending_dy_ = 0;
        move_refusal_said_.clear();
        if (host_) host_->OnMapEdited();
      } else {
        // The units stay where they are and the drag keeps its anchor, so this
        // is how far the pointer has run on past them — and it grows as the
        // pointer keeps going, which lets the red ghost follow it.
        move_pending_dx_ = dx;
        move_pending_dy_ = dy;
        if (host_ && !editor_->last_refusal.empty() &&
            editor_->last_refusal != move_refusal_said_) {
          move_refusal_said_ = editor_->last_refusal;
          host_->OnStatus(FromUtf8(editor_->last_refusal), true);
        }
      }
      Invalidate();
      return;
    }

    case Drag::kNone:
      // The hover marks - brush outline, placement ghost - follow the pointer.
      if (hover_moved) Invalidate();
      return;

    default:
      return;
  }
  // Do not paint from here. Set the state, invalidate, and let WM_PAINT do the
  // work, or a fast drag queues duplicated composes and tears.
}

void MapWindow::EndEditDrag() {
  if (drag_ == Drag::kPlace) {
    const int placed = editor_->EndPlacementRun();
    const int refused = editor_->run_refused();
    if (host_) {
      // A click that was refused says why, which is the one case where the
      // reason is short enough to be useful. A drag says how it went instead:
      // naming the reason forty times would be noise, and saying nothing would
      // hide that half of them did not land.
      if (placed == 0 && refused > 0) {
        if (refused == 1 && !editor_->last_refusal.empty()) {
          host_->OnStatus(FromUtf8(editor_->last_refusal), true);
        } else {
          host_->OnStatus(Format(IDS_PLACED_NONE, refused), true);
        }
      } else if (placed > 0 && refused > 0) {
        host_->OnStatus(Format(IDS_PLACED_SOME, placed, refused), true);
      } else if (placed > 1) {
        host_->OnStatus(Format(IDS_PLACED_ALL, placed), false);
      }
      host_->OnMapEdited();
    }
  } else if (drag_ == Drag::kPaint || drag_ == Drag::kSpray) {
    // The movement brush ends a stroke the same way. Painting a tile shut is a
    // reason for a unit to be stranded exactly as painting rock over it is, and
    // the check that finds them reads the movement layer now.
    if (editor_->tool() == Tool::kPaint || editor_->tool() == Tool::kWalkable) {
      const int stranded = editor_->EndStroke();
      if (host_) {
        if (stranded > 0) {
          host_->OnStatus(Format(Plural(stranded, IDS_UNITS_REMOVED_ONE,
                                        IDS_UNITS_REMOVED_MANY),
                                 stranded), true);
        }
        host_->OnMapEdited();
      }
    }
  } else if (drag_ == Drag::kBand) {
    const int x0 = std::min(anchor_tx_, std::max(hover_tx_, 0));
    const int y0 = std::min(anchor_ty_, std::max(hover_ty_, 0));
    const int x1 = std::max(anchor_tx_, hover_tx_);
    const int y1 = std::max(anchor_ty_, hover_ty_);
    if (x1 > x0 || y1 > y0) {
      TileRect band{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
      editor_->SelectInRect(band, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    }
    if (host_) host_->OnEditorChanged();
  } else if (drag_ == Drag::kRect) {
    // The rectangle is set live during the drag, but nothing has been told it
    // exists — and two things react: the panel's Fill and Clear appear, and the
    // status bar says how big it is.
    if (host_) host_->OnEditorChanged();
  } else if (drag_ == Drag::kMove) {
    if (host_) host_->OnEditorChanged();
  }
}

void MapWindow::OnMouseUp() {
  if (drag_ == Drag::kSpray) KillTimer(hwnd_, kSprayTimer);
  if (drag_ == Drag::kPan) KillTimer(hwnd_, kPanTimer);
  if (editor_ && drag_ != Drag::kNone && drag_ != Drag::kPan) EndEditDrag();
  drag_ = Drag::kNone;
  // The button is up and the units are staying where they are, so the red
  // ghost has nothing left to propose.
  move_pending_dx_ = move_pending_dy_ = 0;
  if (GetCapture() == hwnd_) ReleaseCapture();
  Invalidate();
}

bool MapWindow::LeaveActiveTool() {
  // With no map open nothing is in anyone's hand, so saying a tool was put down
  // would be a line about something that was not happening.
  if (!editor_ || !map_) return false;

  // Placement first, because leaving it is not only a question: a drag that is
  // still open has an undo group to close.
  if (editor_->LeavePlacement()) {
    if (host_) {
      host_->OnEditorChanged();
      host_->OnStatus(Str(IDS_PLACING_LEFT), false);
    }
    // The placement ghost was drawn under the pointer and is no longer what
    // the next click does.
    Invalidate();
    return true;
  }

  const Tool back = editor_->ToolAfterCancel();
  if (back == editor_->tool()) return false;
  editor_->SetTool(back);
  if (host_) {
    host_->OnEditorChanged();
    host_->OnStatus(Str(IDS_BRUSH_PUT_DOWN), false);
  }
  // The brush outline under the pointer went with the brush.
  Invalidate();
  return true;
}

const wchar_t* MapWindow::CursorForTool() const {
  if (drag_ == Drag::kPan) return IDC_SIZEALL;
  if (!editor_) return IDC_ARROW;
  // Ctrl is the eyedropper wherever it is held, so it wins over the tool.
  if (GetKeyState(VK_CONTROL) & 0x8000) return IDC_UPARROW;
  if (editor_->pasting()) return IDC_CROSS;
  if (editor_->mode() == Mode::kTerrain || editor_->mode() == Mode::kMovement) {
    // Painting, dragging a rectangle and laying a movement value are all
    // aiming at a tile.
    return IDC_CROSS;
  }
  if (editor_->tool() == Tool::kPlace) return IDC_CROSS;
  return IDC_ARROW;      // selecting units: the ordinary pointer
}

void MapWindow::OnWheel(int delta, int x, int y, WPARAM keys) {
  if (!map_) return;
  // Alt is the one modifier the wheel had left, and sizing the brush is what a
  // painter reaches for most after zoom. Wherever there is a brush, which is
  // both the modes that paint: a unit has no size to change, and silently doing
  // nothing is worse than not claiming the gesture.
  if ((GetKeyState(VK_MENU) & 0x8000) && editor_ &&
      (editor_->mode() == Mode::kTerrain ||
       editor_->mode() == Mode::kMovement)) {
    const int size = editor_->StepBrushSize(delta > 0 ? 1 : -1);
    if (size >= 0 && host_) {
      host_->OnEditorChanged();
      host_->OnStatus(size == PF_BRUSH_SIZE_CORNER
                          ? Str(IDS_BRUSH_SIZED_CORNER)
                          : Format(IDS_BRUSH_SIZED, size, size),
                      false);
    }
    return;
  }
  if (keys & MK_CONTROL) {
    // The same step the buttons and the keys take; the ladder is the core's.
    pf::view_zoom_step(view_, delta > 0 ? 1 : -1, x, y);
  } else {
    const double px = double(pf::view_tile_px(view_));
    const double lines = double(delta) / double(WHEEL_DELTA) * 3.0 * 32.0;
    // Shift scrolls sideways. A wheel mouse only reports one axis, so without
    // this there is no way to pan horizontally with one at all.
    if (keys & MK_SHIFT) view_.scroll_x -= lines / px;
    else view_.scroll_y -= lines / px;
    pf::view_clamp(view_);
  }
  // The wheel moves the map out from under a pointer that has not itself
  // moved, so the tile beneath it is a different one — and nothing else will
  // say so, because the hovered tile is otherwise only ever derived from a
  // WM_MOUSEMOVE. Without this the placement ghost stays pinned to the tile the
  // pointer used to be over and reads as having come unstuck from the cursor.
  RefreshHoverFromCursor();
  Invalidate();
  if (host_) host_->OnViewChanged();
}

/// Re-derive the hovered tile from where the cursor actually is.
///
/// For the paths that move the map rather than the mouse. Reports the change
/// like a move would, so the status bar's coordinates follow too.
void MapWindow::RefreshHoverFromCursor() {
  POINT at{};
  if (!GetCursorPos(&at) || !ScreenToClient(hwnd_, &at)) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  if (!PtInRect(&client, at)) return;

  int tx = 0, ty = 0;
  const bool on_map = pf::view_tile_at(view_, at.x, at.y, tx, ty);
  const int now_x = on_map ? tx : -1, now_y = on_map ? ty : -1;
  if (now_x == hover_tx_ && now_y == hover_ty_) return;
  hover_tx_ = now_x;
  hover_ty_ = now_y;
  if (host_) host_->OnHoverTile(hover_tx_, hover_ty_);
}

bool MapWindow::ApplyShiftShade(bool down) {
  // Terrain mode and the brush only. Shift used to do this, and moved off it so
  // that Shift could mean "constrain" the way it does in every other drawing
  // program; Alt was the modifier left over. Narrower than the old binding on
  // purpose: Alt+wheel sizes the brush, and a shade that flipped while somebody
  // resized would be a switch moving for no reason they could name.
  if (!editor_ || editor_->mode() != Mode::kTerrain) return false;
  if (editor_->tool() != Tool::kPaint) return false;
  if (!editor_ || editor_->mode() != Mode::kTerrain) return false;
  if (editor_->shade_flipped == down) return false;
  editor_->shade_flipped = down;
  // The dock's Light/Dark switch shows what the next stroke would lay, so it
  // has to be told; the brush outline is unchanged, so the canvas is not.
  if (host_) host_->OnEditorChanged();
  return true;
}

void MapWindow::OnKey(WPARAM key) {
  // Shift borrows the other shade for as long as it is held. Read here as well
  // as at paint time so the dock's switch shows it: a temporary state nobody
  // can see is one you cannot tell you are in.
  if (key == VK_MENU && ApplyShiftShade(true)) return;

  // While a paste is armed the fragment can be turned under the pointer. The
  // menu has these on Ctrl+H, Ctrl+J and Ctrl+R; these are the bare letters,
  // which only mean anything while there is a fragment to turn.
  if (editor_ && editor_->pasting()) {
    bool turned = false;
    if (key == 'R') turned = editor_->RotateClipboard(1);
    else if (key == 'F') turned = editor_->FlipClipboard();
    else if (key == 'M') turned = editor_->MirrorClipboard();
    if (turned) {
      if (host_) {
        const TileRect frag = editor_->ClipboardBounds();
        host_->OnStatus(Format(IDS_FRAGMENT_SIZE, frag.w, frag.h), false);
      }
      Invalidate();
      return;
    }
  }

  // Arrows pan by a tile; everything else arrives through the accelerators.
  double dx = 0, dy = 0;
  switch (key) {
    case VK_LEFT: dx = -1; break;
    case VK_RIGHT: dx = 1; break;
    case VK_UP: dy = -1; break;
    case VK_DOWN: dy = 1; break;
    default: return;
  }
  view_.scroll_x += dx;
  view_.scroll_y += dy;
  pf::view_clamp(view_);
  Invalidate();
  if (host_) host_->OnViewChanged();
}

void MapWindow::SetWaterAnimated(bool on) {
  water_animated_ = on;
  if (on) {
    SetTimer(hwnd_, kWaterTimer, kWaterTickMs, nullptr);
    return;
  }
  KillTimer(hwnd_, kWaterTimer);
  // Leave the palette where the artwork had it, or every later capture and
  // every screenshot is a different sea.
  water_phase_ = 0;
  if (art_) pf_tileset_art_set_water_phase(art_, 0);
  dirty_ = true;
  Invalidate();
}

void MapWindow::OnTimer(UINT_PTR id) {
  if (id == kPanTimer) {
    if (drag_ != Drag::kPan) { KillTimer(hwnd_, kPanTimer); return; }
    const DWORD now = GetTickCount();
    // Elapsed time rather than a fixed step per tick: a busy repaint would turn
    // "one notch a tick" into a pan that slows down exactly when the map is
    // heaviest to draw.
    const double dt = double(now - pan_tick_) / 1000.0;
    pan_tick_ = now;
    if (dt <= 0.0) return;

    const double dpr = DprOf(hwnd_);
    const double dx = double(pan_pointer_.x - drag_from_.x);
    const double dy = double(pan_pointer_.y - drag_from_.y);
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double dead = kPanDeadZone * dpr;
    if (dist <= dead) return;

    // Taking the direction from the normalised vector rather than from dx and
    // dy separately keeps a diagonal push the same speed as a straight one.
    // The gain is per DIP and the result is screen pixels, which cancels to
    // the same arithmetic at any DPI — only the dead zone and the ceiling
    // have to be scaled, because those are distances a hand feels.
    const double speed =
        std::min((dist - dead) * kPanGain, kPanMaxPxPerSec * dpr);
    const double px = double(pf::view_tile_px(view_));
    const double step = speed * dt / px;          // tiles this tick
    view_.scroll_x += dx / dist * step;
    view_.scroll_y += dy / dist * step;
    pf::view_clamp(view_);
    Invalidate();
    if (host_) host_->OnViewChanged();
    return;
  }
  if (id == kWaterTimer) {
    if (!art_) return;
    const int cycle = pf_tileset_art_water_cycle();
    water_phase_ = cycle > 0 ? (water_phase_ + 1) % cycle : 0;
    pf_tileset_art_set_water_phase(art_, water_phase_);
    // The map did not change, but its picture did.
    dirty_ = true;
    Invalidate();
    return;
  }
  if (id != kSprayTimer || drag_ != Drag::kSpray || !editor_) return;
  // The density ramps with how long the button has been held; the ramp is the
  // core's, so both clients build up at the same rate.
  if (hover_tx_ >= 0) {
    const DWORD held = GetTickCount() - spray_since_;
    // A single corner has nothing to scatter, so a held spray on the bottom
    // rung keeps laying the corner under the pointer rather than a puff.
    if (editor_->BrushIsCorner()) {
      PaintPointer(hover_tx_, hover_ty_);
    } else {
      editor_->SprayAt(hover_tx_, hover_ty_, int(held));
    }
    Invalidate();
    if (host_) host_->OnMapStroke();
  }
}

}  // namespace pfwin
