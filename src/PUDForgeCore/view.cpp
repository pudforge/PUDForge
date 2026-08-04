// Where the window is looking at the map.
//
// Scroll, zoom, fit, and turning a point on screen into a tile — arithmetic
// over a map size, a viewport size and a device pixel ratio, and every client
// needs the same answers or the same map scrolls differently in each.
//
// Three subtleties, all found the hard way in the web client:
//
//   Tiles are drawn at a whole number of device pixels, because the artwork is
//   pixel art and at 110% a tile would be 35.2 px. The rendered size rounds
//   while the percentage stays on its ladder.
//
//   Fitting is worked out in exact pixels, then rounding and the ladder snap
//   can each come back a couple of percent larger than the window, so a fit
//   steps down until it really fits.
//
//   Zooming about a point has to clamp afterwards: anchoring near an edge walks
//   the view past it.

#include <algorithm>
#include <cmath>

#include "art.hpp"
#include "constants.hpp"
#include "view.hpp"

namespace pf {
namespace {

/// Space left around a fitted map, in device pixels.
constexpr int kFitMargin = 8;

/// Below this the viewport is not real yet - a window reports its default size
/// before the first resize, and fitting to that pins the zoom at its minimum.
constexpr int kViewportUsable = 200;

}  // namespace

int view_tile_px(const View& v) {
  const double px = double(kTilePx) * (double(v.zoom) / 100.0) * v.dpr;
  const int rounded = int(std::lround(px));
  return rounded < 1 ? 1 : rounded;
}

void view_clamp(View& v) {
  const double px = double(view_tile_px(v));
  const double visible_w = double(v.viewport_w) / px;
  const double visible_h = double(v.viewport_h) / px;

  v.scroll_x = std::max(0.0, std::min(v.scroll_x, double(v.map_w) - visible_w));
  v.scroll_y = std::max(0.0, std::min(v.scroll_y, double(v.map_h) - visible_h));
  // A map narrower than the window sits at the origin rather than drifting.
  if (double(v.map_w) <= visible_w) v.scroll_x = 0.0;
  if (double(v.map_h) <= visible_h) v.scroll_y = 0.0;
}

bool view_tile_at(const View& v, int px, int py, int& tx, int& ty) {
  const double scale = double(view_tile_px(v));
  tx = int(std::floor(double(px) / scale + v.scroll_x));
  ty = int(std::floor(double(py) / scale + v.scroll_y));
  return tx >= 0 && ty >= 0 && tx < v.map_w && ty < v.map_h;
}

bool view_corner_at(const View& v, int px, int py, int& cx, int& cy) {
  // Rounded where view_tile_at floors: a tile is the square you are inside, but
  // a corner is the nearest grid intersection, and the one you are aiming at is
  // the one you are closest to rather than the one up and to the left.
  const double scale = double(view_tile_px(v));
  cx = int(std::lround(double(px) / scale + v.scroll_x));
  cy = int(std::lround(double(py) / scale + v.scroll_y));
  // Inclusive: corners run one past the last tile in each axis.
  return cx >= 0 && cy >= 0 && cx <= v.map_w && cy <= v.map_h;
}

void view_zoom_about(View& v, int zoom, int px, int py) {
  const double before = double(view_tile_px(v));
  const double anchor_x = v.scroll_x + double(px) / before;
  const double anchor_y = v.scroll_y + double(py) / before;

  v.zoom = zoom_snap(zoom);
  v.fitted = false;          // a zoom somebody chose is not a fit

  const double after = double(view_tile_px(v));
  v.scroll_x = anchor_x - double(px) / after;
  v.scroll_y = anchor_y - double(py) / after;
  view_clamp(v);
}

void view_zoom_step(View& v, int dir, int px, int py) {
  view_zoom_about(v, zoom_step(v.zoom, dir), px, py);
}

bool view_fit(View& v, bool only_to_shrink) {
  if (v.viewport_w < kViewportUsable || v.viewport_h < kViewportUsable) return false;
  if (v.map_w <= 0 || v.map_h <= 0) return false;

  const double fit_x = double(v.viewport_w - kFitMargin) / double(v.map_w * kTilePx);
  const double fit_y = double(v.viewport_h - kFitMargin) / double(v.map_h * kTilePx);
  const double fit = std::min(fit_x, fit_y);

  // Opening a map only ever zooms out: a map small enough to see belongs at its
  // own size, and 32x32 blown up to fill a monitor is not a favour.
  if (only_to_shrink && fit >= 1.0) {
    v.zoom = zoom_snap(100);
    v.scroll_x = 0.0;
    v.scroll_y = 0.0;
    v.fitted = false;
    return true;
  }

  auto too_big = [&] {
    return v.map_w * view_tile_px(v) > v.viewport_w ||
           v.map_h * view_tile_px(v) > v.viewport_h;
  };

  v.zoom = zoom_snap(int(std::floor(fit * 100.0)));
  while (v.zoom > zoom_min() && too_big()) {
    const int stepped = zoom_step(v.zoom, -1);
    if (stepped >= v.zoom) break;         // at the floor; stop rather than spin
    v.zoom = stepped;
  }

  // The ladder has a floor and a fit does not: a 128x128 map wants 1024 px at
  // the lowest rung, and a Fit leaving a quarter of the map off the bottom is
  // not a fit. The one caller allowed off the ladder — stepping from here still
  // lands back on it.
  if (too_big()) {
    v.zoom = std::max(1, int(std::floor(fit * 100.0)));
  }

  v.fitted = true;
  const double px = double(view_tile_px(v));
  v.scroll_x = (double(v.map_w) - double(v.viewport_w) / px) / 2.0;
  v.scroll_y = (double(v.map_h) - double(v.viewport_h) / px) / 2.0;
  view_clamp(v);
  return true;
}

void view_centre_on(View& v, int tx, int ty) {
  const double px = double(view_tile_px(v));
  v.scroll_x = double(tx) - double(v.viewport_w) / px / 2.0;
  v.scroll_y = double(ty) - double(v.viewport_h) / px / 2.0;
  view_clamp(v);
}

bool view_fit_rect(View& v, int x, int y, int w, int h) {
  if (v.viewport_w < kViewportUsable || v.viewport_h < kViewportUsable) return false;
  if (w <= 0 || h <= 0) return false;

  // A tile of margin all round, so the thing is not flush against the edge.
  const double fit_x = double(v.viewport_w) / double((w + 2) * kTilePx);
  const double fit_y = double(v.viewport_h) / double((h + 2) * kTilePx);
  v.zoom = zoom_snap(int(std::floor(std::min(fit_x, fit_y) * 100.0)));
  v.fitted = false;
  view_centre_on(v, x + w / 2, y + h / 2);
  return true;
}

void view_region(const View& v, int& x0, int& y0, int& cols, int& rows) {
  const double px = double(view_tile_px(v));
  x0 = std::max(0, int(std::floor(v.scroll_x)));
  y0 = std::max(0, int(std::floor(v.scroll_y)));
  // One tile of slop, because the region starts at a whole tile and the scroll
  // does not: without it the right and bottom edges show a strip of nothing.
  cols = std::min(v.map_w - x0, int(std::ceil(double(v.viewport_w) / px)) + 1);
  rows = std::min(v.map_h - y0, int(std::ceil(double(v.viewport_h) / px)) + 1);
  if (cols < 0) cols = 0;
  if (rows < 0) rows = 0;
}

void view_origin(const View& v, int x0, int y0, int& ox, int& oy) {
  // Whole device pixels: tile_px is already integral, but the scroll is in
  // fractional tiles, so without this every tile edge softens as you pan.
  const double px = double(view_tile_px(v));
  ox = int(std::lround((double(x0) - v.scroll_x) * px));
  oy = int(std::lround((double(y0) - v.scroll_y) * px));
}

}  // namespace pf
