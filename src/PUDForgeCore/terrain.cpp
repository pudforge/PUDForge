#include "terrain.hpp"

#include <algorithm>
#include <cstring>
#include <queue>

#include "constants.hpp"
#include "noise.hpp"
#include "terrain_tables.hpp"

namespace pf {
namespace {

// --------------------------------------------------------------- decoding

/// Solid tile: terrain nibble `(tile >> 4) & 0xf` to terrain class.
uint8_t solid_terrain(int nibble) {
  switch (nibble) {
    case 0x1: return kWaterLight;
    case 0x2: return kWaterDark;
    case 0x3: return kCoastLight;
    case 0x4: return kCoastDark;
    case 0x5: return kGroundLight;
    case 0x6: return kGroundDark;
    case 0x7: return kForest;
    case 0x8: return kMountain;
    case 0x9: case 0xb: return kWallHuman;
    case 0xa: case 0xc: return kWallOrc;
    default: return kTerrainUnknown;
  }
}

/// Boundary class (the high byte) to its {filled, clear} terrain pair, in the
/// spec's filled-then-clear order.
bool boundary_pair(int high, uint8_t& filled, uint8_t& clear) {
  switch (high) {
    case 0x01: filled = kWaterDark;  clear = kWaterLight;  return true;
    case 0x02: filled = kWaterLight; clear = kCoastLight;  return true;
    case 0x03: filled = kCoastDark;  clear = kCoastLight;  return true;
    case 0x04: filled = kMountain;   clear = kCoastLight;  return true;
    case 0x05: filled = kCoastLight; clear = kGroundLight; return true;
    case 0x06: filled = kGroundDark; clear = kGroundLight; return true;
    case 0x07: filled = kForest;     clear = kGroundLight; return true;
    case 0x08: filled = kWallHuman;  clear = kGroundLight; return true;
    case 0x09: filled = kWallOrc;    clear = kGroundLight; return true;
    default: return false;
  }
}

constexpr uint8_t TL = 1, TR = 2, BL = 4, BR = 8;
constexpr uint8_t ALL = TL | TR | BL | BR;

/// Shape nibble to the quadrant mask holding the *filled* terrain; every
/// filled code has a clear code that is its exact complement (0<->D, 1<->C,
/// 2<->B, 3<->A, 4<->9, 7<->6, 8<->5).
uint8_t shape_mask(int nibble) {
  switch (nibble) {
    case 0x0: return TL;
    case 0x1: return TR;
    case 0x2: return TL | TR;
    case 0x3: return BL;
    case 0x4: return TL | BL;
    case 0x5: return uint8_t(ALL & ~(TL | BR));
    case 0x6: return uint8_t(ALL & ~BR);
    case 0x7: return BR;
    case 0x8: return TL | BR;
    case 0x9: return uint8_t(ALL & ~(TL | BL));
    case 0xa: return uint8_t(ALL & ~BL);
    case 0xb: return uint8_t(ALL & ~(TL | TR));
    case 0xc: return uint8_t(ALL & ~TR);
    case 0xd: return uint8_t(ALL & ~TL);
    default: return ALL;
  }
}

// ---------------------------------------------------------- terrain graph

/// Which terrains may share a tile, from the boundary classes. Walls are
/// deliberately absent — see the CornerGrid docs.
struct Graph {
  bool adjacent[kTerrainCount][kTerrainCount] = {};
  int8_t next_hop[kTerrainCount][kTerrainCount] = {};
  int8_t rank[kTerrainCount] = {};

  Graph() {
    const uint8_t pairs[][2] = {
        {kWaterDark, kWaterLight},  {kWaterLight, kCoastLight},
        {kCoastDark, kCoastLight},  {kMountain, kCoastLight},
        {kCoastLight, kGroundLight},{kGroundDark, kGroundLight},
        {kForest, kGroundLight},
    };
    for (const auto& p : pairs) {
      adjacent[p[0]][p[1]] = true;
      adjacent[p[1]][p[0]] = true;
    }

    for (int s = 0; s < kTerrainCount; s++) {
      for (int t = 0; t < kTerrainCount; t++) next_hop[s][t] = -1;
      next_hop[s][s] = int8_t(s);
      std::vector<int> queue{s};
      for (size_t qi = 0; qi < queue.size(); qi++) {
        int node = queue[qi];
        for (int nb = 0; nb < kTerrainCount; nb++) {
          if (!adjacent[node][nb] || next_hop[s][nb] >= 0) continue;
          next_hop[s][nb] = int8_t(node == s ? nb : next_hop[s][node]);
          queue.push_back(nb);
        }
      }
    }

    for (int t = 0; t < kTerrainCount; t++) {
      int steps = 0, cur = t;
      while (cur != kWaterDark && steps < kTerrainCount) {
        int8_t nxt = next_hop[cur][kWaterDark];
        if (nxt < 0) { steps = 99; break; }
        cur = nxt;
        steps++;
      }
      rank[t] = int8_t(steps);
    }
  }
};

const Graph& graph() {
  static const Graph g;
  return g;
}

/// Deterministic scramble so neighbouring tiles pick different art variations.
uint32_t mix(uint32_t x) {
  x ^= x >> 16;
  x *= 0x45d9f3bu;
  x ^= x >> 16;
  x *= 0x45d9f3bu;
  x ^= x >> 16;
  return x;
}

uint32_t corner_key(const uint8_t c[4]) {
  return uint32_t(c[0]) | (uint32_t(c[1]) << 4) | (uint32_t(c[2]) << 8) |
         (uint32_t(c[3]) << 12);
}

}  // namespace

uint16_t tile_movement(uint16_t tile) {
  // The corpus-derived table first: it knows the shoreline split that the
  // dominant terrain alone cannot see.
  const int group = tile >> 4;
  if (group < 158 && kGroupMovement[group]) return kGroupMovement[group];
  switch (dominant_terrain(tile)) {
    case kWaterDark: case kWaterLight: return 0x0040;
    case kForest: case kMountain: return 0x0081;
    case kWallHuman: return 0x008d;
    case kWallOrc: return 0x0089;
    case kCoastDark: case kCoastLight: return 0x0011;
    default: return 0x0001;
  }
}

// --------------------------------------------------------------- decoding

void decode_tile(uint16_t tile, uint8_t out[4]) {
  int high = (tile >> 8) & 0xff;
  if (high == 0) {
    uint8_t t = solid_terrain((tile >> 4) & 0xf);
    out[0] = out[1] = out[2] = out[3] = t;
    return;
  }
  uint8_t filled, clear;
  if (!boundary_pair(high, filled, clear)) {
    out[0] = out[1] = out[2] = out[3] = kTerrainUnknown;
    return;
  }
  uint8_t mask = shape_mask((tile >> 4) & 0xf);
  out[0] = (mask & TL) ? filled : clear;
  out[1] = (mask & TR) ? filled : clear;
  out[2] = (mask & BL) ? filled : clear;
  out[3] = (mask & BR) ? filled : clear;
}

uint8_t dominant_terrain(uint16_t tile) {
  uint8_t q[4];
  decode_tile(tile, q);
  int counts[kTerrainCount] = {};
  uint8_t best = q[0];
  int best_count = 0;
  for (int i = 0; i < 4; i++) {
    int c = ++counts[q[i]];
    if (c > best_count) { best_count = c; best = q[i]; }
  }
  return best;
}

int solid_tile(uint8_t terrain, int variation) {
  for (int nibble = 1; nibble <= 0xc; nibble++) {
    if (solid_terrain(nibble) == terrain) return (nibble << 4) | (variation & 0xf);
  }
  return -1;
}

uint16_t blank_tile(int x, int y) {
  const int flat = solid_tile(kGroundLight, 0);
  if (flat < 0) return kDefaultTile;
  // The measured count rather than the artwork's: a blank map is created before
  // any artwork is loaded, and this reading holds for the swamp tileset too.
  const int plain = plain_variation_count((flat >> 4) & 0xf);
  if (plain <= 1) return uint16_t(flat);
  const uint32_t salt = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
  return uint16_t((flat & 0xfff0) | (mix(salt) % uint32_t(plain)));
}

bool terrain_compatible(uint8_t a, uint8_t b) {
  if (a >= kTerrainCount || b >= kTerrainCount) return false;
  return a == b || graph().adjacent[a][b];
}

uint8_t step_toward(uint8_t from, uint8_t to) {
  if (from == to || from >= kTerrainCount || to >= kTerrainCount) return to;
  int8_t step = graph().next_hop[from][to];
  return step < 0 ? to : uint8_t(step);
}

// ------------------------------------------------------------- tile index

TileIndex::TileIndex(int (*weight_of)(uint16_t, void*), void* ctx) {
  // Groups 0x00-0x9F cover the solid tiles and all nine boundary classes.
  for (int group = 0; group <= 0x9f; group++) {
    std::vector<uint16_t> variations;
    for (int v = 0; v < 16; v++) {
      uint16_t tile = uint16_t((group << 4) | v);
      // Weight becomes repetition so that lookup's uniform pick lands on the
      // shares asked for rather than on the counts that happen to exist.
      const int weight = weight_of ? weight_of(tile, ctx) : 1;
      for (int i = 0; i < weight; i++) variations.push_back(tile);
    }
    if (variations.empty()) continue;

    uint8_t q[4];
    decode_tile(uint16_t(group << 4), q);
    if (q[0] == kTerrainUnknown) continue;
    // Never return a wall from a corner lookup, or painting grass beside a
    // wall would erase it.
    bool is_wall = false;
    for (int i = 0; i < 4; i++) {
      if (q[i] == kWallHuman || q[i] == kWallOrc) is_wall = true;
    }
    if (is_wall) continue;

    auto& slot = by_corners_[corner_key(q)];
    slot.insert(slot.end(), variations.begin(), variations.end());
  }
}

int TileIndex::lookup(const uint8_t corners[4], uint32_t salt) const {
  auto it = by_corners_.find(corner_key(corners));
  if (it == by_corners_.end() || it->second.empty()) return -1;
  return it->second[mix(salt) % it->second.size()];
}

// ------------------------------------------------------------ corner grid

CornerGrid::CornerGrid(int tile_width, int tile_height)
    : tile_w_(tile_width), tile_h_(tile_height),
      w_(tile_width + 1), h_(tile_height + 1),
      cells_(size_t(w_) * size_t(h_), kGroundLight),
      walls_(size_t(tile_width) * size_t(tile_height), 0) {}

uint8_t CornerGrid::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return kGroundLight;
  return cells_[size_t(y) * size_t(w_) + size_t(x)];
}

void CornerGrid::set(int x, int y, uint8_t terrain) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  cells_[size_t(y) * size_t(w_) + size_t(x)] = terrain;
}

void CornerGrid::corners_of(int x, int y, uint8_t out[4]) const {
  out[0] = get(x, y);
  out[1] = get(x + 1, y);
  out[2] = get(x, y + 1);
  out[3] = get(x + 1, y + 1);
}

uint8_t CornerGrid::wall_at(int x, int y) const {
  if (x < 0 || y < 0 || x >= tile_w_ || y >= tile_h_) return 0;
  return walls_[size_t(y) * size_t(tile_w_) + size_t(x)];
}

void CornerGrid::set_wall(int x, int y, uint8_t kind) {
  if (x < 0 || y < 0 || x >= tile_w_ || y >= tile_h_) return;
  walls_[size_t(y) * size_t(tile_w_) + size_t(x)] = kind;
}

CornerGrid CornerGrid::from_map(const Map& map) {
  CornerGrid grid(map.width(), map.height());
  const int w = map.width(), h = map.height();
  const int cw = w + 1;

  // Hand-edited maps disagree about a corner the four tiles around it share,
  // so majority wins.
  std::vector<uint8_t> votes(size_t(cw) * size_t(h + 1) * kTerrainCount, 0);
  uint8_t q[4];

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      decode_tile(map.tiles()[size_t(y) * size_t(w) + size_t(x)], q);
      if (q[0] == kTerrainUnknown) continue;
      for (int i = 0; i < 4; i++) {
        if (q[i] == kWallHuman || q[i] == kWallOrc) {
          grid.set_wall(x, y, q[i] == kWallHuman ? 1 : 2);
          q[i] = kGroundLight;  // record the ground beneath the wall
        }
      }
      const size_t idx[4] = {
          size_t(y) * size_t(cw) + size_t(x),
          size_t(y) * size_t(cw) + size_t(x) + 1,
          size_t(y + 1) * size_t(cw) + size_t(x),
          size_t(y + 1) * size_t(cw) + size_t(x) + 1,
      };
      for (int i = 0; i < 4; i++) votes[idx[i] * kTerrainCount + q[i]]++;
    }
  }

  for (int y = 0; y <= h; y++) {
    for (int x = 0; x <= w; x++) {
      size_t i = size_t(y) * size_t(cw) + size_t(x);
      uint8_t best = kGroundLight;
      int best_count = 0;
      for (int t = 0; t < kTerrainCount; t++) {
        int n = votes[i * kTerrainCount + t];
        if (n > best_count) { best_count = n; best = uint8_t(t); }
      }
      grid.set(x, y, best);
    }
  }
  return grid;
}

// ----------------------------------------------------------- legalization

int legalize(CornerGrid& grid, const Rect& rect, int max_passes) {
  const int x0 = std::max(0, rect.x0);
  const int y0 = std::max(0, rect.y0);
  const int x1 = std::min(grid.tile_width() - 1, rect.x1);
  const int y1 = std::min(grid.tile_height() - 1, rect.y1);
  const Graph& g = graph();

  // One tile's worth of the work. `descend` picks the terrain nearest water as
  // the one to keep rather than the commonest; see the settle phase below.
  auto fix = [&](int x, int y, bool descend) {
    uint8_t c[4];
    grid.corners_of(x, y, c);

    int counts[kTerrainCount] = {};
    int distinct = 0;
    for (int i = 0; i < 4; i++) {
      if (counts[c[i]]++ == 0) distinct++;
    }
    if (distinct == 1) return false;
    if (distinct == 2) {
      uint8_t a = kTerrainUnknown, b = kTerrainUnknown;
      for (int t = 0; t < kTerrainCount; t++) {
        if (!counts[t]) continue;
        (a == kTerrainUnknown ? a : b) = uint8_t(t);
      }
      if (terrain_compatible(a, b)) return false;
    }

    // Most common terrain wins; ties go to the one nearer water so that
    // coastlines grow outwards rather than eating into the water.
    //
    // Descending, it is the rank alone, with the terrain number only to break a
    // rank tie — the choice must be a function of the four corners and nothing
    // else, so that two tiles sharing a corner cannot want different things for
    // it.
    uint8_t target = c[0];
    for (int t = 0; t < kTerrainCount; t++) {
      if (!counts[t]) continue;
      const bool better = descend
          ? (g.rank[t] < g.rank[target] ||
             (g.rank[t] == g.rank[target] && t < int(target)))
          : (counts[t] > counts[target] ||
             (counts[t] == counts[target] && g.rank[t] < g.rank[target]));
      if (better) target = uint8_t(t);
    }

    // Keep at most one companion: the commonest terrain that may legally
    // sit beside the target.
    int companion = -1;
    for (int t = 0; t < kTerrainCount; t++) {
      if (!counts[t] || t == target || !terrain_compatible(uint8_t(t), target)) continue;
      if (companion < 0 || counts[t] > counts[companion]) companion = t;
    }

    const int cx[4] = {x, x + 1, x, x + 1};
    const int cy[4] = {y, y, y + 1, y + 1};
    bool moved = false;
    for (int i = 0; i < 4; i++) {
      if (c[i] == target || int(c[i]) == companion) continue;
      const uint8_t next = step_toward(c[i], target);
      if (next != c[i]) {
        grid.set(cx[i], cy[i], next);
        moved = true;
      }
    }
    return moved;
  };

  for (int pass = 0; pass < max_passes; pass++) {
    bool changed = false;
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) changed |= fix(x, y, false);
    }
    if (!changed) return pass + 1;
  }

  // The passes above can oscillate rather than settle, because a majority is a
  // property of one tile and a corner belongs to four. Forest against water gives
  //
  //     forest forest / forest water     majority forest, so the water corner
  //     forest water  / water  water     majority water, so the forest corner
  //
  // and the corner those two share is pulled both ways for ever. It is the
  // *target* the two tiles disagree about, so taking a bigger step toward it
  // settles nothing — an earlier attempt at this made the offending corners jump
  // straight to the target, and generated maps came out with more solid squares
  // than before, not fewer.
  //
  // What settles it is a target that cannot depend on which tile is asking:
  // whichever of the four corner terrains sits nearest deep water. Both tiles
  // above then agree on water, and the forest retreats through ground and coast
  // to reach it, which is the blend that belongs there anyway.
  //
  // It terminates, which the majority rule cannot promise. `rank` is the
  // distance to deep water through a graph that is a tree, so the terrain kept
  // is never an ancestor of one being moved, every step_toward is therefore a
  // step towards the root, and the sum of the ranks over the grid strictly falls
  // on every pass that changes anything. It is bounded below by zero, so the
  // loop ends — and `fix` moves something whenever a tile is illegal, so where
  // it ends every tile is legal and apply_corners has no reason to fall back.
  //
  // Majority still runs first because it is the better answer where it works: a
  // lone water corner in a field of grass wants filling in, and descending would
  // dig a pond instead. Shipped maps legalise clean and never reach this at all,
  // which the corpus test asserts by finding no fallbacks.
  //
  // The guard is the bound above — four is the deepest rank, so the sum can fall
  // at most that many times per corner — and it exists only so that a graph
  // edited into something that is not a tree cannot hang the editor. Measured
  // over 200 generated maps at 64, 96 and 128, the descent takes 4 passes at
  // worst and 3 almost always.
  const long guard = 4L * long(x1 - x0 + 2) * long(y1 - y0 + 2) + kTerrainCount;
  for (long settle = 0; settle < guard; settle++) {
    bool moved = false;
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) moved |= fix(x, y, true);
    }
    if (!moved) return max_passes + int(settle) + 1;
  }
  return max_passes + int(guard);
}

// ----------------------------------------------------------------- walls

uint16_t wall_tile(const CornerGrid& grid, int x, int y, uint8_t kind) {
  // Indexed by which neighbours are the same kind of wall: N | E<<1 | S<<2 |
  // W<<3. Measured off the shipped maps rather than derived like ordinary
  // boundary tiles, which produced walls that met at the wrong angles.
  static constexpr int kShape[16] = {
      -1,  0x3, 0x1, 0x5, 0x0, 0x4, 0x2, 0x6,
      0x7, 0xb, 0x9, 0xd, 0x8, 0xc, 0xa, -1,
  };
  const bool human = kind == 1;

  int mask = 0;
  if (grid.wall_at(x, y - 1) == kind) mask |= 1;
  if (grid.wall_at(x + 1, y) == kind) mask |= 2;
  if (grid.wall_at(x, y + 1) == kind) mask |= 4;
  if (grid.wall_at(x - 1, y) == kind) mask |= 8;

  // Standing alone and enclosed on all four sides are different tiles, and
  // using one for both made a painted wall look like a row of posts.
  if (mask == 0) return human ? 0x0090 : 0x00a0;
  if (mask == 0xf) return human ? 0x00b0 : 0x00c0;
  return uint16_t(((human ? 0x08 : 0x09) << 8) | (kShape[mask] << 4));
}

// -------------------------------------------------------------- applying

/**
 * Drop walls that no longer have ground under them.
 *
 * Legalization reaches past the brush footprint — paint a lake beside a wall
 * and the tiles between become coast — so the whole affected rectangle sweeps.
 */
void clear_unsupported_walls(CornerGrid& grid, const Rect& rect) {
  const int x0 = std::max(0, rect.x0);
  const int y0 = std::max(0, rect.y0);
  const int x1 = std::min(grid.tile_width() - 1, rect.x1);
  const int y1 = std::min(grid.tile_height() - 1, rect.y1);

  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      if (!grid.wall_at(x, y)) continue;
      uint8_t q[4];
      grid.corners_of(x, y, q);
      for (int i = 0; i < 4; i++) {
        if (q[i] != kGroundLight && q[i] != kGroundDark) {
          grid.set_wall(x, y, 0);
          break;
        }
      }
    }
  }
}

CornerPatch::CornerPatch(const CornerGrid& grid, const Rect& rect) {
  x0_ = std::max(0, rect.x0);
  y0_ = std::max(0, rect.y0);
  x1_ = std::min(grid.tile_width() - 1, rect.x1);
  y1_ = std::min(grid.tile_height() - 1, rect.y1);
  w_ = x1_ - x0_ + 1;
  if (w_ <= 0 || y1_ < y0_) { w_ = 0; return; }

  const int h = y1_ - y0_ + 1;
  // One more corner than tiles in each axis: the tiles on the far edge need
  // their outer corners too.
  corners_.resize(size_t(w_ + 1) * size_t(h + 1));
  walls_.resize(size_t(w_) * size_t(h));
  for (int cy = 0; cy <= h; cy++) {
    for (int cx = 0; cx <= w_; cx++) {
      corners_[size_t(cy) * size_t(w_ + 1) + size_t(cx)] =
          grid.get(x0_ + cx, y0_ + cy);
    }
  }
  for (int ty = 0; ty < h; ty++) {
    for (int tx = 0; tx < w_; tx++) {
      walls_[size_t(ty) * size_t(w_) + size_t(tx)] =
          grid.wall_at(x0_ + tx, y0_ + ty);
    }
  }
}

bool CornerPatch::repick(const CornerGrid& grid, int x, int y) const {
  if (w_ <= 0 || x < x0_ || y < y0_ || x > x1_ || y > y1_) return true;
  if (x >= always_.x0 && y >= always_.y0 && x <= always_.x1 && y <= always_.y1) {
    return true;
  }
  const int tx = x - x0_, ty = y - y0_;
  if (walls_[size_t(ty) * size_t(w_) + size_t(tx)] != grid.wall_at(x, y)) {
    return true;
  }
  const int stride = w_ + 1;
  const int cx[4] = {tx, tx + 1, tx, tx + 1};
  const int cy[4] = {ty, ty, ty + 1, ty + 1};
  for (int i = 0; i < 4; i++) {
    if (corners_[size_t(cy[i]) * size_t(stride) + size_t(cx[i])] !=
        grid.get(x0_ + cx[i], y0_ + cy[i])) {
      return true;
    }
  }
  return false;
}

int apply_corners(Map& map, const CornerGrid& grid, const Rect& rect,
                  const TileIndex& index, const CornerPatch* was) {
  const int w = map.width();
  const int x0 = std::max(0, rect.x0);
  const int y0 = std::max(0, rect.y0);
  const int x1 = std::min(w - 1, rect.x1);
  const int y1 = std::min(map.height() - 1, rect.y1);
  int fallbacks = 0;

  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      // The author's tile stands where nothing moved: on a shipped map the
      // corner grid is a lossy reading, and re-deriving the legalisation margin
      // from it took the forest off a whole island.
      if (was && !was->repick(grid, x, y)) continue;

      const size_t i = size_t(y) * size_t(w) + size_t(x);
      uint8_t kind = grid.wall_at(x, y);
      if (kind) {
        map.tiles()[i] = wall_tile(grid, x, y, kind);
        map.movement()[i] = kind == 1 ? 0x008d : 0x0089;
        continue;
      }

      uint8_t c[4];
      grid.corners_of(x, y, c);

      uint32_t salt = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u;
      int tile = index.lookup(c, salt);

      if (tile < 0) {
        // Unexpressible even after legalization: take the majority terrain.
        int counts[kTerrainCount] = {};
        uint8_t best = c[0];
        for (int k = 0; k < 4; k++) {
          if (++counts[c[k]] > counts[best]) best = c[k];
        }
        tile = solid_tile(best, 0);
        if (tile < 0) tile = kDefaultTile;
        fallbacks++;
      }

      map.tiles()[i] = uint16_t(tile);
      map.movement()[i] = tile_movement(uint16_t(tile));
    }
  }
  return fallbacks;
}

uint8_t shade_of(uint8_t terrain, int x, int y, uint32_t seed) {
  // The three pairs the game treats as one terrain drawn two ways. Anything
  // else has one drawing and is its own answer.
  int family = -1;
  uint8_t light = terrain, dark = terrain;
  switch (terrain) {
    case kGroundLight: case kGroundDark:
      family = 0; light = kGroundLight; dark = kGroundDark; break;
    case kWaterLight: case kWaterDark:
      family = 1; light = kWaterLight; dark = kWaterDark; break;
    case kCoastLight: case kCoastDark:
      family = 2; light = kCoastLight; dark = kCoastDark; break;
    default: return terrain;
  }

  // The families are offset from each other in the field, so a dark stretch of
  // water does not have to line up with a dark stretch of ground.
  static const float kOffset[3] = {0.0f, 137.0f, 311.0f};
  const uint32_t s0 = seed ? seed : 1u;
  const NoiseLayer layers[] = {
      {0.10f, s0, 1.0f},                 // the patches
      {0.25f, s0 ^ 0x9e3779b9u, 0.35f},  // and a broken edge on them
  };
  const LayeredNoise noise(layers, 2);

  // Where to cut light from dark, as a share of the field rather than a fixed
  // value, so the cut does not drift with the shape the noise happened to take.
  // 30% dark is what Blizzard's 28 multiplayer maps average. It is a property
  // of the field — per family and per seed — because a single corner offers no
  // population to take a percentile over.
  struct Cut { uint32_t seed; bool known[3]; float at[3]; };
  static Cut cut = {};
  if (cut.seed != s0) { cut = Cut{}; cut.seed = s0; }
  if (!cut.known[family]) {
    constexpr float kDarkShare = 0.30f;
    constexpr int kSpan = 96;   // 9,216 samples: steady to about a percent
    std::vector<float> samples;
    samples.reserve(size_t(kSpan) * size_t(kSpan));
    for (int sy = 0; sy < kSpan; sy++) {
      for (int sx = 0; sx < kSpan; sx++) {
        samples.push_back(noise.at(float(sx) + kOffset[family],
                                   float(sy) + kOffset[family]));
      }
    }
    const size_t at = size_t(float(samples.size() - 1) * kDarkShare);
    std::nth_element(samples.begin(), samples.begin() + long(at), samples.end());
    cut.at[family] = samples[at];
    cut.known[family] = true;
  }

  const float v = noise.at(float(x) + kOffset[family], float(y) + kOffset[family]);
  return v < cut.at[family] ? dark : light;
}

Rect paint_auto(Map& map, CornerGrid& grid, const TileIndex& index,
                int x, int y, uint8_t terrain, int size, uint32_t mix_seed) {
  Rect none{0, 0, -1, -1};
  if (x < 0 || y < 0 || x >= map.width() || y >= map.height()) return none;
  const int half = size / 2;

  // A wall is one tile wide, so it cannot be described by the corners it shares
  // with its neighbours; it is painted as an overlay with ground recorded
  // underneath, which is also how `from_map` reads one back.
  const bool wall = terrain == kWallHuman || terrain == kWallOrc;
  const uint8_t beneath = wall ? uint8_t(kGroundLight) : terrain;

  // Legalization ripples outwards, so work a margin wider than the brush.
  const int margin = kTerrainCount;
  const Rect rect{x - half - margin, y - half - margin, x + half + margin,
                  y + half + margin};
  // Taken before anything moves, so the tiles the stroke never reached can be
  // told from the ones it did.
  CornerPatch was(grid, rect);
  was.always(Rect{x - half, y - half, x + half, y + half});

  for (int cy = y - half; cy <= y + half + 1; cy++) {
    for (int cx = x - half; cx <= x + half + 1; cx++) {
      // Per corner, so a wide brush lays a mottle rather than one flat square.
      grid.set(cx, cy, mix_seed ? shade_of(beneath, cx, cy, mix_seed) : beneath);
    }
  }
  // Painting anything else over a wall takes the wall away with it.
  for (int ty = y - half; ty <= y + half; ty++) {
    for (int tx = x - half; tx <= x + half; tx++) {
      grid.set_wall(tx, ty, wall ? (terrain == kWallHuman ? 1 : 2) : 0);
    }
  }

  legalize(grid, rect);
  clear_unsupported_walls(grid, rect);
  apply_corners(map, grid, rect, index, &was);
  return rect;
}

Rect paint_corner(Map& map, CornerGrid& grid, const TileIndex& index,
                  int cx, int cy, uint8_t terrain, uint32_t mix_seed) {
  Rect none{0, 0, -1, -1};
  // Inclusive on both ends: the corner grid is one larger than the tile grid in
  // each axis, so the last corner of the last tile is at width, not width - 1.
  if (cx < 0 || cy < 0 || cx > map.width() || cy > map.height()) return none;
  // A wall is a per-tile overlay with ground recorded underneath, so it has no
  // corner to be laid at. Refused rather than quietly painting the ground.
  if (terrain == kWallHuman || terrain == kWallOrc) return none;

  // The four tiles sharing this corner are (cx-1, cy-1) through (cx, cy), and
  // legalisation ripples outwards from them the same way it does for a brush.
  const int margin = kTerrainCount;
  const Rect rect{cx - 1 - margin, cy - 1 - margin, cx + margin, cy + margin};
  CornerPatch was(grid, rect);
  was.always(Rect{cx - 1, cy - 1, cx, cy});

  grid.set(cx, cy, mix_seed ? shade_of(terrain, cx, cy, mix_seed) : terrain);

  legalize(grid, rect);
  clear_unsupported_walls(grid, rect);
  apply_corners(map, grid, rect, index, &was);
  return rect;
}

/**
 * Flood one terrain class into the region under a tile.
 *
 * Orthogonal only: two lakes touching at a corner are two lakes, and a fill
 * that leaked through the join would be a surprise nobody wants to undo. The
 * whole affected area is legalised in one pass, because doing it per tile is
 * thousands of passes for one result.
 */
int fill_terrain(Map& map, CornerGrid& grid, const TileIndex& index,
                 int x, int y, uint8_t terrain, const Rect* within) {
  const int w = map.width(), h = map.height();
  if (x < 0 || y < 0 || x >= w || y >= h) return 0;

  // Bounds: the selection when there is one, else the map. A fill inside a
  // rectangle is how you recolour one lake and not the sea it joins.
  int x0 = 0, y0 = 0, x1 = w, y1 = h;
  if (within && within->x1 > within->x0 && within->y1 > within->y0) {
    x0 = std::max(0, within->x0);
    y0 = std::max(0, within->y0);
    x1 = std::min(w, within->x1);
    y1 = std::min(h, within->y1);
    if (x < x0 || y < y0 || x >= x1 || y >= y1) return 0;
  }

  const uint8_t from = dominant_terrain(map.tile_at(x, y));
  if (from == kTerrainUnknown) return 0;
  const bool wall = terrain == kWallHuman || terrain == kWallOrc;
  const uint8_t beneath = wall ? uint8_t(kGroundLight) : terrain;
  // Filling a terrain with itself is not a no-op: the TileIndex is rebuilt
  // whenever the detail policy changes, so a second bucket re-rolls plain into
  // decorated.
  const bool repaint = from == terrain && !wall;
  // For a repaint, count the tiles that came out different rather than the
  // tiles the flood reached — an unchanged fill must not cost an undo step.
  std::vector<uint16_t> before;
  if (repaint) before = map.tiles();

  // The rectangle a bucket ends up covering is not known until the flood has
  // run, so the whole grid is the snapshot; it is one byte per corner.
  CornerPatch was(grid, Rect{0, 0, w - 1, h - 1});

  std::vector<uint8_t> seen(size_t(w) * size_t(h), 0);
  std::vector<std::pair<int, int>> stack{{x, y}};
  seen[size_t(y) * size_t(w) + size_t(x)] = 1;
  int filled = 0;
  Rect touched{w, h, -1, -1};

  while (!stack.empty()) {
    const auto [tx, ty] = stack.back();
    stack.pop_back();
    filled++;

    for (int cy = ty; cy <= ty + 1; cy++) {
      for (int cx = tx; cx <= tx + 1; cx++) grid.set(cx, cy, beneath);
    }
    grid.set_wall(tx, ty, wall ? (terrain == kWallHuman ? 1 : 2) : 0);
    touched.x0 = std::min(touched.x0, tx);
    touched.y0 = std::min(touched.y0, ty);
    touched.x1 = std::max(touched.x1, tx + 1);
    touched.y1 = std::max(touched.y1, ty + 1);

    const int step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : step) {
      const int nx = tx + d[0], ny = ty + d[1];
      if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1) continue;
      const size_t at = size_t(ny) * size_t(w) + size_t(nx);
      if (seen[at]) continue;
      if (dominant_terrain(map.tile_at(nx, ny)) != from) continue;
      seen[at] = 1;
      stack.push_back({nx, ny});
    }
  }
  if (!filled) return 0;

  // One legalisation over everything touched, with the same margin a brush
  // uses so the new boundary settles against what it meets.
  const int margin = kTerrainCount;
  Rect rect{touched.x0 - margin, touched.y0 - margin,
            touched.x1 + margin, touched.y1 + margin};
  // Every tile the flood reached is repainted whatever its corners say, for
  // the same reason a brush repaints its footprint.
  was.always(touched);
  legalize(grid, rect);
  clear_unsupported_walls(grid, rect);
  apply_corners(map, grid, rect, index, &was);

  if (repaint) {
    const std::vector<uint16_t>& after = map.tiles();
    int changed = 0;
    for (size_t i = 0; i < before.size() && i < after.size(); i++) {
      if (before[i] != after[i]) changed++;
    }
    return changed;
  }
  return filled;
}

// ---------------------------------------------------------------- regions

namespace {
enum RegionClass { kBlocked = 0, kLand = 1, kWater = 2 };

RegionClass classify(uint16_t tile, uint16_t& sentinel) {
  sentinel = 0;
  int group = tile >> 4;
  uint16_t observed = group < 158 ? kGroupRegion[group] : 0;
  if (observed) {
    if (observed >= 0xfff0) { sentinel = observed; return kBlocked; }
    return (observed & 0x4000) ? kLand : kWater;
  }
  switch (dominant_terrain(tile)) {
    case kForest: sentinel = 0xfffe; return kBlocked;
    case kMountain: sentinel = 0xfffd; return kBlocked;
    case kWallHuman: case kWallOrc: sentinel = 0xfffb; return kBlocked;
    case kWaterDark: case kWaterLight: return kWater;
    default: return kLand;
  }
}
}  // namespace

int rebuild_regions(Map& map) {
  const int w = map.width(), h = map.height();
  const size_t n = size_t(w) * size_t(h);
  auto& out = map.regions();
  out.assign(n, 0xffff);

  std::vector<uint8_t> cls(n);
  for (size_t i = 0; i < n; i++) {
    uint16_t sentinel = 0;
    cls[i] = uint8_t(classify(map.tiles()[i], sentinel));
    if (cls[i] == kBlocked) out[i] = sentinel;
  }

  int land = 0, water = 0;
  std::vector<int32_t> stack;
  stack.reserve(n);

  for (size_t start = 0; start < n; start++) {
    if (cls[start] == kBlocked || out[start] != 0xffff) continue;
    const bool is_land = cls[start] == kLand;
    const uint16_t label = uint16_t((is_land ? 0x4000 : 0x0000) | (is_land ? land : water));
    if (is_land) land++; else water++;

    stack.clear();
    stack.push_back(int32_t(start));
    out[start] = label;

    while (!stack.empty()) {
      int32_t i = stack.back();
      stack.pop_back();
      const int x = i % w, y = i / w;
      const bool left = x > 0, right = x < w - 1, up = y > 0, down = y < h - 1;
      // 8-connectivity: 4-connectivity over-segments the shipped maps (Gold
      // Rush comes out with 59 land regions against its actual 44), while
      // including diagonals reproduces their counts exactly.
      const int32_t nb[8] = {
          left ? i - 1 : -1,        right ? i + 1 : -1,
          up ? i - w : -1,          down ? i + w : -1,
          (left && up) ? i - w - 1 : -1,   (right && up) ? i - w + 1 : -1,
          (left && down) ? i + w - 1 : -1, (right && down) ? i + w + 1 : -1,
      };
      for (int k = 0; k < 8; k++) {
        int32_t j = nb[k];
        if (j < 0) continue;
        if (cls[size_t(j)] != cls[size_t(start)] || out[size_t(j)] != 0xffff) continue;
        out[size_t(j)] = label;
        stack.push_back(j);
      }
    }
  }
  return land + water;
}

}  // namespace pf
