// Where the window is looking at the map. See view.cpp.

#pragma once

namespace pf {

/**
 * A viewport onto a map.
 *
 * Sizes are in device pixels; the scroll is in tiles and is deliberately
 * fractional, so panning is smooth rather than a tile at a time. `dpr` is the
 * device pixel ratio, which is 1 on a plain monitor and 1.5 or 2 on the ones
 * everybody actually has.
 */
struct View {
  int map_w = 0, map_h = 0;
  int viewport_w = 0, viewport_h = 0;
  double dpr = 1.0;
  int zoom = 100;              ///< percent, on the ladder; see zoom_snap
  double scroll_x = 0.0;       ///< top-left tile, fractional
  double scroll_y = 0.0;
  /**
   * Whether this is a fit rather than a zoom somebody chose.
   *
   * A fit is a statement about the window, so it has to be redone when the
   * window changes size or dragging the corner leaves the map floating in a
   * third of the space. A chosen zoom is left alone.
   */
  bool fitted = false;
};

/// Device pixels per tile, always a whole number. See view.cpp for why.
int view_tile_px(const View& v);

/// Pull the scroll back inside the map. Every route that moves it calls this.
void view_clamp(View& v);

/// Tile under a point in the viewport. False when it lands outside the map.
bool view_tile_at(const View& v, int px, int py, int& tx, int& ty);

/// Nearest grid *corner* to a point, for the corner brush. False when it lands
/// outside the map. Corners run 0..map_w and 0..map_h, one past the tiles.
bool view_corner_at(const View& v, int px, int py, int& cx, int& cy);

/// Zoom to `zoom`, keeping the tile under (px, py) where it is.
void view_zoom_about(View& v, int zoom, int px, int py);
/// One step in (`dir` > 0) or out, about a point.
void view_zoom_step(View& v, int dir, int px, int py);

/// Fit the whole map and centre it. False when the viewport is not real yet.
bool view_fit(View& v, bool only_to_shrink = false);

/// Fit a tile rectangle, with a tile of margin. False when there is nothing to fit.
bool view_fit_rect(View& v, int x, int y, int w, int h);

/// Centre the view on a tile, without changing the zoom.
void view_centre_on(View& v, int tx, int ty);

/// The tile region to compose: origin and size, clipped to the map.
void view_region(const View& v, int& x0, int& y0, int& cols, int& rows);

/// Where that region's top-left corner lands in the viewport, in whole pixels.
void view_origin(const View& v, int x0, int y0, int& ox, int& oy);

}  // namespace pf
