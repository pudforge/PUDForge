#include "Minimap.hpp"

#include <windowsx.h>

#include <algorithm>

#include "Blit.hpp"
#include "Docks.hpp"   // AskDockMenu, the menu the three docks share

namespace pfwin {

const wchar_t* Minimap::kClassName = L"PUDForgeMinimap";

bool Minimap::Register(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &Minimap::Proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;   // every pixel painted
  wc.lpszClassName = kClassName;
  return RegisterClassExW(&wc) != 0;
}

HWND Minimap::Create(HWND parent, HINSTANCE instance, int control_id, Host* host) {
  host_ = host;
  return CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, nullptr,
                         WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
                         reinterpret_cast<HMENU>(INT_PTR(control_id)), instance,
                         this);
}

void Minimap::SetMap(const pf_map* map, const pf_tileset_art* art) {
  map_ = map;
  art_ = art;
  map_w_ = map ? pf_map_width(map) : 0;
  map_h_ = map ? pf_map_height(map) : 0;
  dirty_ = true;
  Invalidate();
}

void Minimap::SetViewport(int x, int y, int w, int h) {
  if (x == view_x_ && y == view_y_ && w == view_w_ && h == view_h_) return;
  view_x_ = x;
  view_y_ = y;
  view_w_ = w;
  view_h_ = h;
  Invalidate();
}

LRESULT CALLBACK Minimap::Proc(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam) {
  Minimap* self =
      reinterpret_cast<Minimap*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<Minimap*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

RECT Minimap::MapRect() const {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  if (map_w_ <= 0 || map_h_ <= 0 || rc.right <= 0 || rc.bottom <= 0) return rc;
  // Aspect-fit, integer scale where it helps: the map is square-ish and the
  // panel is whatever the dock width made it.
  int dw = rc.right, dh = rc.bottom;
  if (map_w_ * dh > map_h_ * dw) dh = std::max(1, map_h_ * dw / map_w_);
  else dw = std::max(1, map_w_ * dh / map_h_);
  const int x = (rc.right - dw) / 2;
  const int y = (rc.bottom - dh) / 2;
  return {x, y, x + dw, y + dh};
}

LRESULT Minimap::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      OnPaint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONDOWN:
      SetCapture(hwnd_);
      dragging_ = true;
      ScrollTo(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_MOUSEMOVE:
      if (dragging_) ScrollTo(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      dragging_ = false;
      if (GetCapture() == hwnd_) ReleaseCapture();
      return 0;
    case WM_CONTEXTMENU: {
      // Which side it sits on, and nothing else — a minimap has no columns.
      if (!host_) return 0;
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (at.x == -1 && at.y == -1) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        at = {rc.right / 2, rc.bottom / 2};
        ClientToScreen(hwnd_, &at);
      }
      const int picked = AskDockMenu(hwnd_, at, -1,
                                     host_->DockIsRight(Host::Dock::kMinimap));
      if (picked == kDockMenuLeft || picked == kDockMenuRight) {
        host_->OnDockSide(Host::Dock::kMinimap, picked == kDockMenuRight);
      }
      return 0;
    }
    default:
      return DefWindowProcW(hwnd_, message, wparam, lparam);
  }
}

void Minimap::ScrollTo(int x, int y) {
  if (!map_ || !host_) return;
  const RECT rect = MapRect();
  const int dw = rect.right - rect.left, dh = rect.bottom - rect.top;
  if (dw <= 0 || dh <= 0) return;
  const int tx = (x - rect.left) * map_w_ / dw;
  const int ty = (y - rect.top) * map_h_ / dh;
  host_->OnScrollTo(std::clamp(tx, 0, map_w_ - 1), std::clamp(ty, 0, map_h_ - 1));
}

void Minimap::OnPaint() {
  PAINTSTRUCT ps;
  HDC window_dc = BeginPaint(hwnd_, &ps);

  // Workspace, map and viewport box compose off screen and reach the window in
  // one blit, the way the palette and the canvas do. Straight to the window they
  // are three passes over the same pixels, which showed as the white box
  // arriving over a bare grey panel while a dock's seam was dragged.
  const int w = ps.rcPaint.right - ps.rcPaint.left;
  const int h = ps.rcPaint.bottom - ps.rcPaint.top;
  HDC dc = window_dc;
  HDC mem = nullptr;
  HBITMAP buffer = nullptr;
  HGDIOBJ old_bitmap = nullptr;
  if (w > 0 && h > 0) {
    mem = CreateCompatibleDC(window_dc);
    buffer = mem ? CreateCompatibleBitmap(window_dc, w, h) : nullptr;
    if (mem && buffer) {
      old_bitmap = SelectObject(mem, buffer);
      // The buffer's origin is the invalid rectangle's, so every coordinate
      // below stays in client space and none of the drawing knows about this.
      SetViewportOrgEx(mem, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);
      dc = mem;
    } else {
      if (buffer) DeleteObject(buffer);
      if (mem) DeleteDC(mem);
      mem = nullptr;
      buffer = nullptr;
    }
  }

  FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_APPWORKSPACE));

  if (map_ && map_w_ > 0) {
    if (dirty_) {
      pixels_.resize(size_t(map_w_) * size_t(map_h_));
      if (pf_map_compose_minimap(map_, art_, pixels_.data(), pixels_.size()) > 0) {
        dirty_ = false;
      }
    }
    if (!pixels_.empty()) {
      const RECT rect = MapRect();
      BlitRgba(dc, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, map_w_, map_h_, pixels_.data());

      // The viewport rectangle, in minimap pixels.
      if (view_w_ > 0 && view_h_ > 0) {
        const int dw = rect.right - rect.left, dh = rect.bottom - rect.top;
        RECT box = {rect.left + view_x_ * dw / map_w_,
                    rect.top + view_y_ * dh / map_h_,
                    rect.left + (view_x_ + view_w_) * dw / map_w_,
                    rect.top + (view_y_ + view_h_) * dh / map_h_};
        HPEN pen = CreatePen(PS_SOLID, kOutlinePx, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
      }
    }
  }

  if (mem) {
    // Back to device coordinates first: BitBlt reads its source rectangle
    // through the source DC's own transform.
    SetViewportOrgEx(mem, 0, 0, nullptr);
    BitBlt(window_dc, ps.rcPaint.left, ps.rcPaint.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(buffer);
    DeleteDC(mem);
  }
  EndPaint(hwnd_, &ps);
}

}  // namespace pfwin
