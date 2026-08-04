// Terrain decoding, auto-tiling and region labelling.
//
// See reference/docs/terrain-editing.md for the reasoning. In short: MTXM is a corner
// (Wang) model, so the editable state is a grid of (w+1) x (h+1) corner
// terrains and a tile value is a lookup on its four corners.

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "pud.hpp"

namespace pf {

enum Terrain : uint8_t {
  kWaterDark = 0,
  kWaterLight = 1,
  kCoastDark = 2,
  kCoastLight = 3,
  kGroundLight = 4,
  kGroundDark = 5,
  kForest = 6,
  kMountain = 7,
  kWallHuman = 8,
  kWallOrc = 9,
  kTerrainUnknown = 10,
  kTerrainCount = 11,
};

/// Decode a tile into [topLeft, topRight, bottomLeft, bottomRight] terrains.
void decode_tile(uint16_t tile, uint8_t out[4]);
/// The terrain covering most of a tile.
uint8_t dominant_terrain(uint16_t tile);
/// Solid tile value for a terrain, or -1 if it has no solid form.
int solid_tile(uint8_t terrain, int variation = 0);

/**
 * The tile a blank map starts (x, y) at: light ground, in one of its plain
 * variations rather than variation 0 everywhere.
 *
 * One tile repeated across 4,096 of them reads as graph paper: the eye finds
 * the period at any zoom showing more than a few tiles. Plain variations only —
 * scattering stones over the whole map is what the Detail setting is for.
 * Deterministic in the coordinate, so a resize does not reshuffle the ground.
 */
uint16_t blank_tile(int x, int y);
/// Non-zero when the two terrains share a boundary class and may meet.
bool terrain_compatible(uint8_t a, uint8_t b);
/// One step from `from` towards `to` along the terrain tree.
uint8_t step_toward(uint8_t from, uint8_t to);

/// Reverse map: four corner terrains -> the tile values producing them.
class TileIndex {
 public:
  /**
   * @param weight_of optional filter: how many of a bucket's slots this tile
   *        takes, 0 for one the tileset cannot draw or the policy excludes.
   *
   * A weight rather than a yes-or-no, because `lookup` picks a slot uniformly
   * and a share is the only way to balance two sets of different sizes. See
   * `art_has_tile` in capi.cpp for the policies that say more than 1.
   */
  explicit TileIndex(int (*weight_of)(uint16_t, void*) = nullptr, void* ctx = nullptr);

  /// Tile for a corner quadruple, or -1. `salt` picks among art variations
  /// deterministically so repainting a spot doesn't shimmer.
  int lookup(const uint8_t corners[4], uint32_t salt) const;

  size_t combination_count() const { return by_corners_.size(); }

 private:
  std::unordered_map<uint32_t, std::vector<uint16_t>> by_corners_;
};

/// Corner terrains plus a per-tile wall overlay.
///
/// A wall is one tile wide, so a wall tile and the ground tile beside it
/// disagree about their shared corners; treating walls as terrain makes them
/// evaporate under corner voting.
class CornerGrid {
 public:
  CornerGrid(int tile_width, int tile_height);

  static CornerGrid from_map(const Map& map);

  int tile_width() const { return tile_w_; }
  int tile_height() const { return tile_h_; }

  uint8_t get(int x, int y) const;
  void set(int x, int y, uint8_t terrain);
  void corners_of(int x, int y, uint8_t out[4]) const;

  uint8_t wall_at(int x, int y) const;
  void set_wall(int x, int y, uint8_t kind);

 private:
  int tile_w_, tile_h_;
  int w_, h_;  // corner grid is one larger in each axis
  std::vector<uint8_t> cells_;
  std::vector<uint8_t> walls_;
};

struct Rect {
  int x0, y0, x1, y1;
};

/// Nudge corners until every tile in `rect` is expressible. Returns passes used.
int legalize(CornerGrid& grid, const Rect& rect, int max_passes = 12);

/**
 * The corners and wall flags inside a rectangle, as they were.
 *
 * The corner model is a lossy reading of Warcraft's tiles — roughly one tile in
 * eight of a shipped map disagrees with the grid voted from it — so the eleven
 * tiles of legalisation margin around an edit must not be rewritten wholesale.
 * Rewriting them all is what `pf_map_refit` is for.
 */
class CornerPatch {
 public:
  CornerPatch(const CornerGrid& grid, const Rect& rect);
  /// Whether this tile needs its tile value chosen again: true inside `always`,
  /// wherever the corners or the wall flag moved, and outside the patch, where
  /// nothing is known.
  bool repick(const CornerGrid& grid, int x, int y) const;

  /// Choose again everywhere in `rect`, whatever its corners say.
  ///
  /// A brush repaints its own footprint even where the corners come out the
  /// same: the variation policy may want a different drawing, and painting over
  /// a movement override is how you take it off.
  void always(const Rect& rect) { always_ = rect; }

 private:
  int x0_, y0_, x1_, y1_;  ///< clamped tile bounds
  int w_;                  ///< corner rows are w_ + 1 wide
  Rect always_{0, 0, -1, -1};
  std::vector<uint8_t> corners_;
  std::vector<uint8_t> walls_;
};

/// Re-choose every tile in `rect` from the corners it now has, returning the
/// number that had to fall back to a solid tile.
///
/// With a `was`, only tiles whose corners moved are touched; everything else
/// keeps the tile the author put there. Pass null to mean "re-choose the lot",
/// which is what refitting a map to newly-attached artwork wants.
int apply_corners(Map& map, const CornerGrid& grid, const Rect& rect,
                  const TileIndex& index, const CornerPatch* was = nullptr);

/**
 * Which of a terrain's two drawings the mixture wants at a corner.
 *
 * Coherent noise rather than a coin per corner: flipping corners independently
 * changed the shade between neighbours 39% of the time, where Blizzard's 28
 * multiplayer maps change it a mean of 10.6%. Answers `terrain` for one with no
 * second drawing, so no caller needs a test for which those are.
 */
uint8_t shade_of(uint8_t terrain, int x, int y, uint32_t seed);

/// Paint terrain with edge fitting. Returns the affected rectangle.
///
/// `mix_seed` non-zero lays the mixture instead of one flat shade, per corner
/// rather than per stroke: a brush covers up to 32 tiles a side, and one shade
/// over all of them is the flat square mixing exists to avoid.
Rect paint_auto(Map& map, CornerGrid& grid, const TileIndex& index,
                int x, int y, uint8_t terrain, int size, uint32_t mix_seed = 0);

/**
 * Paint one corner of the grid — the smallest mark the terrain model can hold.
 *
 * `cx` and `cy` are corner coordinates, not tile ones: the grid is one larger
 * in each axis, so they run 0..width and 0..height inclusive. The corner is
 * shared by up to four tiles and every one of them is re-chosen.
 *
 * A quarter of what `paint_auto` lays at size 1, which sets all four corners of
 * a tile. What comes out is not always one corner: legalisation may widen it
 * where the tileset has no drawing for the pair, exactly as it does for any
 * other brush.
 *
 * Walls are not corner-shaped — they are a per-tile overlay — so a wall terrain
 * is refused here rather than silently laying something else.
 */
Rect paint_corner(Map& map, CornerGrid& grid, const TileIndex& index,
                  int cx, int cy, uint8_t terrain, uint32_t mix_seed = 0);

/// Remove walls whose ground has been painted away, over a whole rectangle.
void clear_unsupported_walls(CornerGrid& grid, const Rect& rect);

/// Choose a wall tile from which neighbours are also walls.
uint16_t wall_tile(const CornerGrid& grid, int x, int y, uint8_t kind);

/**
 * Flood a terrain into the region under a tile. Returns tiles filled.
 *
 * `within` bounds the fill when it is a real rectangle, so a selection can hold
 * it to one lake rather than letting it run into the sea beyond.
 */
int fill_terrain(Map& map, CornerGrid& grid, const TileIndex& index,
                 int x, int y, uint8_t terrain, const Rect* within);

/// Recompute REGM as an 8-connected component labelling, returning the count.
int rebuild_regions(Map& map);

/**
 * The `SQM ` value a tile implies.
 *
 * Movement is very nearly a function of the tile: across the shipped maps only
 * 236 tiles in 6,975,488 hold anything other than this. Painting writes it, and
 * the movement editor compares against it to show what has been overridden.
 */
uint16_t tile_movement(uint16_t tile);

}  // namespace pf
