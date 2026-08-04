// The minimap: the whole map at one pixel per tile, and the viewport on it.
//
// pf_map_compose_minimap does the drawing - units at their real footprint,
// terrain from the artwork's average colours. This control blits it, draws
// the viewport rectangle, and turns a click or a drag into a scroll.

#pragma once

#include <windows.h>

#include <vector>

#include "Host.hpp"
#include "pudforge/pudforge.h"

namespace pfwin {

class Minimap {
 public:
  static const wchar_t* kClassName;
  static bool Register(HINSTANCE instance);

  HWND Create(HWND parent, HINSTANCE instance, int control_id, Host* host);
  HWND hwnd() const { return hwnd_; }

  /// Borrowed; null clears.
  void SetMap(const pf_map* map, const pf_tileset_art* art);
  /// The map's content changed, so the next paint recomposes.
  void MarkMapChanged() { dirty_ = true; Invalidate(); }
  /// The same map, a different shape. The cached dimensions are what the aspect
  /// fit, the hit test and the viewport box are computed from, so this is not
  /// the same as the content having changed.
  void MapSizeChanged() { SetMap(map_, art_); }
  /// The tile rectangle the canvas is showing, for the viewport box.
  void SetViewport(int x, int y, int w, int h);

  /// Repaint now rather than when the queue next runs dry.
  ///
  /// InvalidateRect alone posts a WM_PAINT, and WM_PAINT is the lowest-priority
  /// message there is: during a drag the mouse-move flood never lets the queue
  /// empty, so the minimap froze for the whole gesture and caught up when the
  /// button came back up.
  void Invalidate() {
    if (!hwnd_) return;
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
  }

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
  void OnPaint();
  void ScrollTo(int x, int y);
  /// Where the map lands in the client area, aspect-fit.
  RECT MapRect() const;

  HWND hwnd_ = nullptr;
  Host* host_ = nullptr;
  const pf_map* map_ = nullptr;
  const pf_tileset_art* art_ = nullptr;
  std::vector<uint32_t> pixels_;
  int map_w_ = 0, map_h_ = 0;
  int view_x_ = 0, view_y_ = 0, view_w_ = 0, view_h_ = 0;
  bool dirty_ = true;
  bool dragging_ = false;
};

}  // namespace pfwin
