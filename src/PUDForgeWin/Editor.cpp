#include "Editor.hpp"

#include <algorithm>

namespace pfwin {
namespace {

/// Why the core refused a placement, in the words the web client uses.
const char* PlacementReason(int code) {
  switch (code) {
    case PF_PLACE_OUT_OF_BOUNDS: return "outside the map";
    case PF_PLACE_NEEDS_LAND: return "needs solid ground";
    case PF_PLACE_NEEDS_WATER: return "needs water";
    case PF_PLACE_NEEDS_GROUND: return "needs ground to build on";
    case PF_PLACE_BLOCKED: return "blocked by forest, rock or wall";
    case PF_PLACE_NEEDS_SHORE:
      return "needs to sit on the shoreline, touching both water and coast";
    case PF_PLACE_TOO_NEAR_MINE:
      return "needs three tiles of clearance from a gold mine, or peasants "
             "cannot work it";
    case PF_PLACE_OFF_GRID:
      return "sits on a two-tile grid, so it cannot start on an odd tile";
    default: return "cannot stand there";
  }
}

/// Every tile of a square brush, for the paths that walk them one by one.
void SquareAround(int x, int y, int size, std::vector<int>& out) {
  const int r = (size - 1) / 2;
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      out.push_back(x + dx);
      out.push_back(y + dy);
    }
  }
}

}  // namespace

Editor::Editor(pf_map* map) { SetMap(map); }

Editor::~Editor() {
  if (clipboard_) pf_clipboard_free(clipboard_);
}

void Editor::SetMap(pf_map* map) {
  map_ = map;
  selected_.clear();
  // The mask is sized to the map, so it cannot survive a change of map.
  ClearTerrainSelection();
  last_refusal.clear();
  stroke_ = false;
  // The clipboard deliberately survives: a fragment is independent of the map it
  // came from, so copying out of one and pasting into the next is the point of
  // it being detached. Only the armed paste is cancelled.
  pasting_ = false;
  revision_ = clean_revision_ = 0;
  if (map_) {
    pf_map_set_variation_policy(map_, variation_policy_);
    ApplyPlacementOption();
  }
}

Mode Editor::ModeOfTool(Tool tool) {
  switch (tool) {
    case Tool::kPaint: case Tool::kRect: return Mode::kTerrain;
    case Tool::kWalkable: return Mode::kMovement;
    default: return Mode::kUnit;
  }
}

void Editor::SetMode(Mode mode) {
  mode_ = mode;
  if (ModeOfTool(tool_) != mode) {
    tool_ = mode == Mode::kTerrain    ? Tool::kPaint
            : mode == Mode::kMovement ? Tool::kWalkable
                                      : Tool::kSelect;
  }
  if (tool_ != Tool::kRect) ClearTerrainSelection();
  // Leaving unit mode drops a selection that can no longer be acted on.
  if (mode != Mode::kUnit) ClearSelection();
}

void Editor::SetTool(Tool tool) {
  mode_ = ModeOfTool(tool);
  tool_ = tool;
  // The terrain rectangle belongs to the terrain-select tool. Leaving it drawn
  // implies it still applies, and Fill would then act on something the user
  // cannot see themselves editing.
  if (tool != Tool::kRect) ClearTerrainSelection();
}

Tool Editor::ToolAfterCancel() const {
  // Terrain's own other tool, not Tool::kSelect: that one belongs to unit mode,
  // and backing out of the brush must not move the user to the other half of the
  // editor.
  if (mode_ == Mode::kTerrain && tool_ == Tool::kPaint) return Tool::kRect;
  return tool_;
}

int Editor::TerrainOfBrush() const {
  if (BrushIsCustom()) return PF_TERRAIN_GROUND_LIGHT;
  const int terrain = pf_brush_terrain(brush_index);
  // The palette holds one cell per terrain and a Light/Dark switch beside it, so
  // the dark drawing is reached from here rather than from a second cell.
  // Terrains with only one drawing answer with themselves.
  return DarkWanted() ? pf_terrain_other_shade(terrain) : terrain;
}

void Editor::SetBrush(int index) {
  if (index < 0 || index > pf_brush_count()) return;
  if (index == pf_brush_count()) { brush_index = index; return; }   // the custom cell
  const int terrain = pf_brush_terrain(index);
  const int twin = pf_terrain_other_shade(terrain);
  if (twin == terrain) { brush_index = index; return; }   // one drawing: no switch to set
  if (pf_brush_shade(index) < 0) {
    // A dark brush names the pair as much as the light one does; take the light
    // cell the palette shows and say so on the switch.
    for (int i = 0; i < pf_brush_count(); i++) {
      if (pf_brush_terrain(i) == twin) { brush_index = i; break; }
    }
    paint_dark = true;
    return;
  }
  brush_index = index;
  paint_dark = false;
}

std::string Editor::BrushName() const {
  if (BrushIsCustom()) {
    if (custom_tile < 0) return "No tile picked, so this brush paints nothing";
    // The number and nothing else. That this one brush does not fit its edges is
    // worth knowing once, which is what the tooltip on the cell is for.
    char line[32];
    snprintf(line, sizeof(line), "Tile 0x%04X", unsigned(custom_tile));
    return line;
  }
  const char* name = pf_terrain_name(TerrainOfBrush(),
                                     map_ ? pf_map_tileset(map_) : 0);
  return name ? name : "";
}

void Editor::SetVariationPolicy(int policy) {
  variation_policy_ = policy;
  if (map_) pf_map_set_variation_policy(map_, policy);
}

void Editor::ApplyPlacementOption() {
  if (!map_) return;
  // All three, because all three are the map's now — the core enforces them on
  // every way of putting a unit down, including the one that never comes through
  // this file at all.
  pf_map_set_allow_illegal_placement(map_, allow_illegal_placement_ ? 1 : 0);
  pf_map_set_allow_stacked_units(map_, allow_stacked_units_ ? 1 : 0);
  pf_map_set_allow_edge_placement(map_, allow_edge_placement_ ? 1 : 0);
}

const std::vector<Editor::Option>& Editor::SavedOptions() {
  // The order is the order they were added, and nothing depends on it.
  static const std::vector<Option> kOptions = {
      // How you paint. Settings about the brush, not about any one map.
      {"Grid", 0, [](const Editor& e) { return int(e.show_grid); },
       [](Editor& e, int v) { e.show_grid = v != 0; }},
      // Floored at one tile, so a run always opens on a brush that covers a
      // tile even if the last one ended on the corner rung. The corner brush is
      // a deliberate reach for something smaller than the grid, and inheriting
      // it silently means the first stroke of a new map is a quarter the size
      // the pointer looks like it is.
      {"BrushSize", 1, [](const Editor& e) { return e.brush_size; },
       [](Editor& e, int v) { e.brush_size = v < 1 ? 1 : v; }},
      {"BrushShape", PF_BRUSH_SQUARE,
       [](const Editor& e) { return e.brush_shape; },
       [](Editor& e, int v) { e.brush_shape = v; }},
      {"MixShades", 0, [](const Editor& e) { return int(e.mix_shades); },
       [](Editor& e, int v) { e.mix_shades = v != 0; }},
      {"PaintDark", 0, [](const Editor& e) { return int(e.paint_dark); },
       [](Editor& e, int v) { e.paint_dark = v != 0; }},
      {"Variation", PF_VARIATION_PLAIN,
       [](const Editor& e) { return e.variation_policy(); },
       [](Editor& e, int v) { e.SetVariationPolicy(v); }},

      // What the terrain tools do to what is already there.
      {"FitEdges", 1, [](const Editor& e) { return int(e.auto_fit_edges); },
       [](Editor& e, int v) { e.auto_fit_edges = v != 0; }},
      {"FitPasted", 1, [](const Editor& e) { return int(e.fit_pasted_edges); },
       [](Editor& e, int v) { e.fit_pasted_edges = v != 0; }},
      {"KeepStranded", 0,
       [](const Editor& e) { return int(e.keep_stranded_units); },
       [](Editor& e, int v) { e.keep_stranded_units = v != 0; }},

      // The three escape hatches from the placement rules, and the marker.
      // Through the setters, which push each one down to the core.
      {"AllowIllegal", 0,
       [](const Editor& e) { return int(e.allow_illegal_placement()); },
       [](Editor& e, int v) { e.SetAllowIllegalPlacement(v != 0); }},
      {"AllowStacked", 0,
       [](const Editor& e) { return int(e.allow_stacked_units()); },
       [](Editor& e, int v) { e.SetAllowStackedUnits(v != 0); }},
      {"AllowEdge", 0,
       [](const Editor& e) { return int(e.allow_edge_placement()); },
       [](Editor& e, int v) { e.SetAllowEdgePlacement(v != 0); }},
      {"MarkSpecial", 0,
       [](const Editor& e) { return int(e.mark_special_units); },
       [](Editor& e, int v) { e.mark_special_units = v != 0; }},

      // What the palette offers.
      {"ShowAllRaces", 0, [](const Editor& e) { return int(e.show_all_races); },
       [](Editor& e, int v) { e.show_all_races = v != 0; }},
      {"OfferUnusedUnits", 0,
       [](const Editor& e) { return int(e.offer_unused_units); },
       [](Editor& e, int v) { e.offer_unused_units = v != 0; }},
  };
  return kOptions;
}

int Editor::ToggleMirror(int flag) {
  mirrors = flag == PF_MIRROR_NONE ? PF_MIRROR_NONE : (mirrors ^ flag);
  return mirrors;
}

// --------------------------------------------------------------- undo/redo

void Editor::Checkpoint() {
  if (grouping_ || !map_) return;
  pf_map_checkpoint(map_);
}

bool Editor::Undo() {
  if (!map_ || pf_map_undo(map_) != PF_OK) return false;
  AfterHistoryStep();
  return true;
}

bool Editor::Redo() {
  if (!map_ || pf_map_redo(map_) != PF_OK) return false;
  AfterHistoryStep();
  return true;
}

void Editor::AfterHistoryStep() {
  // The map's content was replaced wholesale, so anything derived from it is
  // stale: drop a selection that may no longer name the same units.
  ClearSelection();
  Bump();
}

// ---------------------------------------------------------------- painting

void Editor::BeginStroke() {
  if (stroke_) return;
  // Any per-stroke value works; what matters is that one stroke keeps its
  // pattern instead of shimmering as the pointer passes back over itself.
  scatter_seed_ = scatter_seed_ * 1664525u + 1013904223u;
  if (!scatter_seed_) scatter_seed_ = 1;
  Checkpoint();
  // What is already stranded is grandfathered. Snapshot even when the option
  // currently allows keeping, so toggling it mid-stroke cannot widen the sweep.
  pre_stranded_ = StrandedNow();
  painted_ = {};
  painted_mask_.assign(size_t(pf_map_width(map_)) * size_t(pf_map_height(map_)), 0);
  stroke_ = true;
}

void Editor::MarkPainted(int x, int y, int size) {
  if (!map_) return;
  const int half = size / 2;
  const int x0 = x - half, y0 = y - half;
  const int x1 = x + half, y1 = y + half;
  if (painted_.empty()) {
    painted_ = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
  } else {
    const int nx = std::min(painted_.x, x0), ny = std::min(painted_.y, y0);
    const int mx = std::max(painted_.x + painted_.w - 1, x1);
    const int my = std::max(painted_.y + painted_.h - 1, y1);
    painted_ = {nx, ny, mx - nx + 1, my - ny + 1};
  }

  // The same union again, kept separately: `painted_` belongs to a stroke, while
  // the canvas asks about whatever has happened since it last repainted.
  if (touched_.empty()) {
    touched_ = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
  } else {
    const int tx = std::min(touched_.x, x0), ty = std::min(touched_.y, y0);
    const int mx = std::max(touched_.x + touched_.w - 1, x1);
    const int my = std::max(touched_.y + touched_.h - 1, y1);
    touched_ = {tx, ty, mx - tx + 1, my - ty + 1};
  }

  const int w = pf_map_width(map_), h = pf_map_height(map_);
  if (painted_mask_.size() != size_t(w) * size_t(h)) {
    painted_mask_.assign(size_t(w) * size_t(h), 0);
  }
  for (int ty = std::max(0, y0); ty <= std::min(h - 1, y1); ty++) {
    for (int tx = std::max(0, x0); tx <= std::min(w - 1, x1); tx++) {
      painted_mask_[size_t(ty) * size_t(w) + size_t(tx)] = 1;
    }
  }
}

int Editor::ShadedTerrain(int terrain, int x, int y) const {
  // Mix is a third answer to "which drawing does this stroke lay", rather than
  // something that happens after the stroke has landed. Asking per tile is what
  // makes it one: no second pass to reach tiles the stroke did not cover, and no
  // re-picking of tiles it had already settled.
  //
  // Shift means nothing here: a mixture has no other shade to borrow.
  if (!mix_shades) return terrain;
  return pf_shade_at(terrain, x, y, kShadeSeed);
}

int Editor::StepBrushSize(int dir) {
  const int rungs = pf_brush_size_count();
  if (rungs <= 0 || dir == 0) return -1;
  // Where the current size sits, or the nearest rung below it if the size came
  // from somewhere that did not use the ladder.
  int at = 0;
  for (int i = 0; i < rungs; i++) {
    if (pf_brush_size(i) <= brush_size) at = i;
  }
  // The bottom rung is a corner rather than a tile, and the movement layer
  // holds one value per tile — so in that mode the ladder starts a rung higher
  // instead of offering a size that would round back up to one.
  const int floor = (mode_ == Mode::kMovement &&
                     pf_brush_size(0) == PF_BRUSH_SIZE_CORNER)
                        ? 1
                        : 0;
  const int next =
      std::max(floor, std::min(at + (dir > 0 ? 1 : -1), rungs - 1));
  const int size = pf_brush_size(next);
  if (size == brush_size) return -1;
  brush_size = size;
  Bump();
  return size;
}

bool Editor::SprayAt(int x, int y, int held_ms) {
  const double density = pf_spray_density(held_ms, pf_scatter_density());
  return PaintBrushAt(x, y, density);
}

bool Editor::PaintAt(int x, int y) {
  return PaintBrushAt(x, y, pf_scatter_density());
}

bool Editor::PaintCornerAt(int cx, int cy) {
  if (!map_) return false;
  const int w = pf_map_width(map_), h = pf_map_height(map_);
  if (cx < 0 || cy < 0 || cx > w || cy > h) return false;

  const int terrain = TerrainOfBrush();
  // The two brushes that have no corner to lay fall back to the tile the corner
  // belongs to, which is the smallest either of them can be. Refusing the click
  // instead would make the bottom rung look broken for a wall or a custom tile.
  //
  // A wall is a per-tile overlay; the custom brush writes one tile value
  // verbatim and fits nothing, so there is no corner grid for it to move.
  if (BrushIsCustom() || pf_terrain_wall_kind(terrain)) {
    return PaintBrushAt(std::min(cx, w - 1), std::min(cy, h - 1),
                        pf_scatter_density());
  }
  // Raw painting writes tile values straight out, so it has no corner grid
  // either. One tile is as small as it goes.
  if (!auto_fit_edges) {
    return PaintBrushAt(std::min(cx, w - 1), std::min(cy, h - 1),
                        pf_scatter_density());
  }

  // Mirrored in corner space, not tile space — see pf_symmetry_corners.
  std::vector<int> spots(16);
  const int pairs = pf_symmetry_corners(map_, cx, cy, mirrors, spots.data(), 16);
  spots.resize(size_t(std::max(pairs, 0)) * 2);
  if (spots.empty()) { spots.push_back(cx); spots.push_back(cy); }

  bool ok = false;
  for (size_t i = 0; i + 1 < spots.size(); i += 2) {
    const int sx = spots[i], sy = spots[i + 1];
    if (sx < 0 || sy < 0 || sx > w || sy > h) continue;
    // The corner is shared by up to four tiles and every one of them may be
    // re-chosen, so all four are marked: the stroke's stranded-unit pass and
    // the canvas both ask this what moved. Two 1x1 marks rather than one 2x2,
    // because MarkPainted takes a centre and a size and 2 has no centre.
    MarkPainted(std::max(0, sx - 1), std::max(0, sy - 1), 1);
    MarkPainted(std::min(w - 1, sx), std::min(h - 1, sy), 1);
    if (pf_map_paint_corner(map_, sx, sy, terrain,
                            mix_shades ? kShadeSeed : 0u) == PF_OK) {
      ok = true;
    }
  }
  if (ok) Bump();
  return ok;
}

int Editor::EndStroke() {
  if (!stroke_) return 0;
  stroke_ = false;
  painted_ = {};
  painted_mask_.clear();
  const int stranded = RemoveStrandedUnits();
  pf_map_rebuild_regions(map_);
  Bump();
  return stranded;
}

bool Editor::FillAt(int x, int y) {
  if (!map_) return false;
  const TileRect& box = terrain_selection_;
  int filled = 0;
  const std::vector<int> spots = SymmetryPoints(x, y);
  for (size_t i = 0; i + 1 < spots.size(); i += 2) {
    const int sx = spots[i], sy = spots[i + 1];
    if (!InBounds(sx, sy)) continue;
    const int n = pf_map_fill_terrain(map_, sx, sy, TerrainOfBrush(),
                                      box.x, box.y, box.w, box.h);
    if (n > 0) filled += n;
  }
  if (filled) Bump();
  return filled > 0;
}

int Editor::FillTerrainEverywhere(int x, int y) {
  if (!map_ || !InBounds(x, y)) return 0;
  // What was clicked, not what the brush lays: the bucket asks "this terrain",
  // and the tile under the pointer is how it is named.
  const int from = pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map_, x, y)));
  // Through the same bulk edit the Replace sheet runs, which already decides
  // what it would do before doing any of it and settles the boundary in several
  // passes. Its scope is the terrain selection or the map, so a shift-click
  // inside a rectangle stays inside it.
  return ReplaceTerrain(from, TerrainOfBrush());
}

std::vector<int> Editor::BrushPoints(int x, int y, int shape, double density) {
  // The two cases the core has no points for, taken before brush_size is squared
  // into a buffer size.
  if (brush_size <= 1 || shape == PF_BRUSH_SQUARE) return {};

  // Sized for the whole square in one go rather than by a dry run for the count.
  // A dry run advances the scatter seed, so the real call would draw a different
  // set — and fewer points than the buffer was sized for leaves its tail reading
  // as tile (0,0), which is a corner of the map, not a gap.
  std::vector<int> points(size_t(brush_size) * size_t(brush_size) * 2);
  const int n = pf_brush_points(x, y, brush_size, shape, float(density),
                                &scatter_seed_, points.data(),
                                int(points.size()));
  points.resize(size_t(n) * 2);
  return points;
}

int Editor::MovementBrushSize() const {
  return BrushIsCorner() ? 1 : brush_size;
}

int Editor::MovementClassIndex() const {
  for (int i = 0; i < pf_movement_class_count(); i++) {
    if (pf_movement_class_value(i) == movement_value) return i;
  }
  return -1;
}

int Editor::MovementBrushValue() const {
  return movement_from_terrain ? -1 : movement_value;
}

int Editor::PaintMovementAt(int x, int y) {
  if (!map_ || !InBounds(x, y)) return 0;
  const int want = MovementBrushValue();

  // The same brush the terrain tools use, so size, shape and the mirrors mean
  // one thing across the editor. The bucket has nothing to flood here: what it
  // would follow is the terrain, and this layer is the one that disagrees with
  // the terrain on purpose.
  const int shape = brush_shape == kShapeFill ? PF_BRUSH_SQUARE : brush_shape;
  std::vector<int> points = BrushPoints(x, y, shape, pf_scatter_density());
  // The corner rung marks an intersection rather than a tile, and this layer is
  // one value per tile — so the smallest movement brush is a tile.
  if (points.empty()) SquareAround(x, y, MovementBrushSize(), points);

  int changed = 0;
  for (size_t i = 0; i + 1 < points.size(); i += 2) {
    const std::vector<int> spots = SymmetryPoints(points[i], points[i + 1]);
    for (size_t j = 0; j + 1 < spots.size(); j += 2) {
      const int tx = spots[j], ty = spots[j + 1];
      if (!InBounds(tx, ty)) continue;
      // -1 is the palette's "put it back": each tile takes what the tile drawn
      // there implies, which is what makes an override removable by hand.
      const int value =
          want >= 0 ? want : pf_tile_movement(pf_map_tile_at(map_, tx, ty));
      if (value < 0 || pf_map_movement_at(map_, tx, ty) == value) continue;
      if (pf_map_set_movement(map_, tx, ty, value) != PF_OK) continue;
      // Say which tiles moved, so the canvas recomposes those rather than the
      // whole visible region. Without it every dab of a 17-tile brush redrew
      // the view, overlay and all.
      MarkPainted(tx, ty, 1);
      changed++;
    }
  }
  if (changed) Bump();
  return changed;
}

bool Editor::PaintBrushAt(int x, int y, double density) {
  if (!map_ || !InBounds(x, y)) return false;

  // The bucket is its own operation: it decides what it covers from the map
  // rather than from a brush footprint.
  if (brush_shape == kShapeFill && !BrushIsCustom()) return FillAt(x, y);

  // The custom brush writes one tile value and fits nothing around it, because
  // the whole reason to reach for it is that the corner model would have chosen
  // something else.
  if (BrushIsCustom()) {
    if (custom_tile < 0) return false;
    const int shape = brush_shape == kShapeFill ? PF_BRUSH_SQUARE : brush_shape;
    std::vector<int> points = BrushPoints(x, y, shape, density);
    if (points.empty()) SquareAround(x, y, brush_size, points);
    bool wrote = false;
    for (size_t i = 0; i + 1 < points.size(); i += 2) {
      const std::vector<int> spots = SymmetryPoints(points[i], points[i + 1]);
      for (size_t j = 0; j + 1 < spots.size(); j += 2) {
        if (InBounds(spots[j], spots[j + 1]) &&
            pf_map_set_tile(map_, spots[j], spots[j + 1], custom_tile) == PF_OK) {
          wrote = true;
        }
      }
    }
    if (wrote) Bump();
    return wrote;
  }

  bool ok = false;
  const std::vector<int> points = BrushPoints(x, y, brush_shape, density);
  if (!points.empty()) {
    // Each point of the brush is mirrored on its own, so a scattered brush
    // scatters symmetrically rather than reflecting one blob.
    for (size_t i = 0; i + 1 < points.size(); i += 2) {
      const std::vector<int> spots = SymmetryPoints(points[i], points[i + 1]);
      for (size_t j = 0; j + 1 < spots.size(); j += 2) {
        ok = PaintOne(spots[j], spots[j + 1], 1) || ok;
      }
    }
  } else {
    // A square brush is the core's own footprint: one call covers it.
    const std::vector<int> spots = SymmetryPoints(x, y);
    for (size_t j = 0; j + 1 < spots.size(); j += 2) {
      ok = PaintOne(spots[j], spots[j + 1], brush_size) || ok;
    }
  }
  if (ok) Bump();
  return ok;
}

bool Editor::PaintOne(int x, int y, int size) {
  if (!InBounds(x, y)) return false;
  MarkPainted(x, y, size);
  const int terrain = TerrainOfBrush();
  // Which wall a terrain means is the core's answer, and it is already in the
  // numbering pf_map_paint_wall takes.
  if (const int kind = pf_terrain_wall_kind(terrain)) {
    return pf_map_paint_wall(map_, x, y, kind, size) == PF_OK;
  }
  // Mixing is the core's, per corner of the footprint: a brush is up to 32 tiles
  // a side, and one shade decided here would paint that whole square flat.
  //
  // Raw painting lays a tile value verbatim and fits nothing, so there is no
  // corner grid for a mixture to vary.
  if (auto_fit_edges) {
    return pf_map_paint_terrain_mixed(map_, x, y, terrain, size,
                                      mix_shades ? kShadeSeed : 0u) == PF_OK;
  }
  return (auto_fit_edges
              ? pf_map_paint_terrain(map_, x, y, terrain, size)
              : pf_map_paint_terrain_raw(map_, x, y, terrain, size)) == PF_OK;
}

std::set<int> Editor::StrandedNow() const {
  std::set<int> out;
  if (!map_) return out;
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) != PF_OK) continue;
    if (pf_map_placement_check(map_, u.x, u.y, u.type) != PF_PLACE_OK) {
      out.insert(i);
    }
  }
  return out;
}

int Editor::RemoveStrandedUnits() {
  if (keep_stranded_units || !map_) return 0;
  // Only units this stroke stranded. Real maps carry units our placement rule
  // would refuse, and a whole-map sweep here once deleted every base on a map
  // because a five-tile stroke was painted somewhere else. Comparing by index is
  // sound because nothing inside a stroke adds or removes units.
  std::vector<int> doomed;
  for (int i : StrandedNow()) {
    if (pre_stranded_.find(i) == pre_stranded_.end()) doomed.push_back(i);
  }
  // Back to front: removal renumbers everything after it.
  for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
    pf_map_remove_unit(map_, *it);
    selected_.erase(*it);
  }
  if (!doomed.empty()) Bump();
  return int(doomed.size());
}

bool Editor::PickBrush(int x, int y) {
  if (!map_ || !InBounds(x, y)) return false;
  const uint16_t tile = uint16_t(pf_map_tile_at(map_, x, y));
  const int terrain = pf_tile_dominant_terrain(tile);
  for (int i = 0; i < pf_brush_count(); i++) {
    // Through SetBrush, so picking up a dark tile comes back as its terrain with
    // the shade switch thrown rather than as a brush the palette hides.
    if (pf_brush_terrain(i) == terrain) { SetBrush(i); return true; }
  }
  // Nothing in the brush list lays this. The custom brush lays any tile at all,
  // so point it here rather than picking nothing and saying nothing.
  custom_tile = tile;
  brush_index = pf_brush_count();
  return true;
}

bool Editor::InBounds(int x, int y) const {
  return map_ && x >= 0 && y >= 0 &&
         x < pf_map_width(map_) && y < pf_map_height(map_);
}

std::vector<int> Editor::SymmetryPoints(int x, int y, int w, int h) const {
  std::vector<int> out(16);
  const int pairs = pf_symmetry_points(map_, x, y, w, h, mirrors, out.data(), 8);
  out.resize(size_t(std::max(pairs, 0)) * 2);
  if (out.empty()) { out.push_back(x); out.push_back(y); }
  return out;
}

// ---------------------------------------------------------------- selection

int Editor::SelectedUnit() const {
  return selected_.size() == 1 ? *selected_.begin() : -1;
}

int Editor::SelectAt(int x, int y, bool additive) {
  const int index = map_ ? pf_map_unit_at(map_, x, y) : -1;
  if (index < 0) {
    if (!additive) selected_.clear();
    return -1;
  }
  if (additive) {
    if (!selected_.erase(index)) selected_.insert(index);
  } else {
    selected_.clear();
    selected_.insert(index);
  }
  return index;
}

int Editor::SelectInRect(const TileRect& rect, bool additive) {
  if (!additive) selected_.clear();
  if (!map_ || rect.empty()) return int(selected_.size());
  const int x1 = rect.x + rect.w, y1 = rect.y + rect.h;
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) != PF_OK) continue;
    int fw = 1, fh = 1;
    pf_map_unit_footprint(map_, u.type, &fw, &fh);
    const bool apart = u.x + fw <= rect.x || x1 <= u.x ||
                       u.y + fh <= rect.y || y1 <= u.y;
    if (!apart) selected_.insert(i);
  }
  return int(selected_.size());
}

int Editor::SelectAll() {
  selected_.clear();
  const int count = map_ ? pf_map_unit_count(map_) : 0;
  for (int i = 0; i < count; i++) selected_.insert(i);
  return count;
}

int Editor::InvertSelection() {
  std::set<int> was;
  was.swap(selected_);
  const int count = map_ ? pf_map_unit_count(map_) : 0;
  for (int i = 0; i < count; i++) {
    if (!was.count(i)) selected_.insert(i);
  }
  return int(selected_.size());
}

int Editor::SelectGroup(int group, bool additive) {
  if (!additive) selected_.clear();
  const int count = map_ ? pf_map_unit_count(map_) : 0;
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK && pf_unit_in_group(u.type, group)) {
      selected_.insert(i);
    }
  }
  return int(selected_.size());
}

int Editor::SelectOwner(int owner, bool additive) {
  if (!additive) selected_.clear();
  const int count = map_ ? pf_map_unit_count(map_) : 0;
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK && u.owner == owner) selected_.insert(i);
  }
  return int(selected_.size());
}

int Editor::SelectSameType() {
  // The set of selected types rather than the first one, so selecting a farm and
  // a barracks and asking for more finds both kinds.
  std::set<int> types;
  for (int i : selected_) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK) types.insert(u.type);
  }
  if (types.empty()) return 0;
  selected_.clear();
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK && types.count(u.type)) selected_.insert(i);
  }
  return int(selected_.size());
}

int Editor::SelectSameOwner() {
  std::set<int> owners;
  for (int i : selected_) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK) owners.insert(u.owner);
  }
  if (owners.empty()) return 0;
  selected_.clear();
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK && owners.count(u.owner)) selected_.insert(i);
  }
  return int(selected_.size());
}

TileRect Editor::SelectionBounds() const {
  TileRect box;
  if (!map_ || selected_.empty()) return box;
  int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
  for (int i : selected_) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) != PF_OK) continue;
    int fw = 1, fh = 1;
    pf_map_unit_footprint(map_, u.type, &fw, &fh);
    x0 = std::min(x0, int(u.x));
    y0 = std::min(y0, int(u.y));
    x1 = std::max(x1, u.x + fw);
    y1 = std::max(y1, u.y + fh);
  }
  if (x1 <= x0) return box;
  box = {x0, y0, x1 - x0, y1 - y0};
  return box;
}

// ---------------------------------------------------------------- clipboard

/// Which rectangle "copy" means, given what the user has selected.
///
/// An explicit rectangle wins, then the terrain rectangle, then the bounds of
/// the unit selection — the same order the web client uses.
TileRect Editor::CopyBounds(const TileRect& rect) const {
  if (!rect.empty()) return rect;
  if (!terrain_selection_.empty()) return terrain_selection_;
  return SelectionBounds();
}

/// The fragment's holes, in the fragment's own coordinates, or empty when it has
/// none.
///
/// Only the terrain selection has a shape: an explicit rectangle is one by
/// definition, and a unit selection's bounding box is not a claim about the
/// tiles inside it.
std::vector<uint8_t> Editor::CopyMaskFor(const TileRect& box,
                                         const TileRect& explicit_rect) const {
  if (!explicit_rect.empty() || box.empty()) return {};
  if (terrain_selection_.empty() || TerrainSelectionIsRect()) return {};
  std::vector<uint8_t> mask(size_t(box.w) * size_t(box.h), 0);
  for (int y = 0; y < box.h; y++) {
    for (int x = 0; x < box.w; x++) {
      mask[size_t(y) * size_t(box.w) + size_t(x)] =
          TerrainSelected(box.x + x, box.y + y) ? 1 : 0;
    }
  }
  return mask;
}

int Editor::Copy(Grab what, const TileRect& rect) {
  if (!map_) return -1;
  const TileRect box = CopyBounds(rect);
  if (box.empty()) {
    last_refusal = "nothing is selected to copy";
    return -1;
  }
  // One or the other. By mode unless the caller was explicit, because which half
  // of the editor you are in is the same answer as which half of the map you
  // meant — and a fragment carrying both repaints ground the user did not want.
  if (what == Grab::kByMode) {
    what = mode_ == Mode::kTerrain ? Grab::kTerrain : Grab::kUnits;
  }
  const int want_terrain = what == Grab::kTerrain ? 1 : 0;
  // Shift and alt build selections that are not rectangles, and a copy that only
  // knew the bounding box came back with everything between two separately
  // picked squares. When the selection is a plain rectangle there is nothing to
  // say, and the fragment travels solid.
  const std::vector<uint8_t> holes = CopyMaskFor(box, rect);
  pf_clipboard* clip = pf_clipboard_copy_masked(
      map_, box.x, box.y, box.w, box.h, holes.empty() ? nullptr : holes.data(),
      want_terrain, want_terrain ? 0 : 1);
  if (!clip) {
    last_refusal = "that rectangle is not inside the map";
    return -1;
  }
  if (clipboard_) pf_clipboard_free(clipboard_);
  clipboard_ = clip;
  clipboard_revision_++;
  last_refusal.clear();
  return pf_clipboard_unit_count(clipboard_);
}

int Editor::Cut(const TileRect& rect) {
  if (!map_) return -1;
  const TileRect box = CopyBounds(rect);
  const int captured = Copy(Grab::kUnits, box);
  if (captured < 0) return -1;
  // Cut removes exactly what was captured, which means the units inside the
  // rectangle — not whatever happened to be selected before.
  SelectInRect(box, false);
  if (!selected_.empty()) DeleteSelected();
  return captured;
}

bool Editor::ClipboardHasTerrain() const {
  return clipboard_ && pf_clipboard_has_terrain(clipboard_) != 0;
}

TileRect Editor::ClipboardBounds() const {
  if (!clipboard_) return {};
  return {0, 0, pf_clipboard_width(clipboard_), pf_clipboard_height(clipboard_)};
}

int Editor::PasteAt(int x, int y) {
  if (!map_ || !clipboard_) {
    last_refusal = "there is nothing to paste";
    return -1;
  }
  Checkpoint();
  const int placed =
      pf_map_paste_ex(map_, clipboard_, x, y, fit_pasted_edges ? 1 : 0);
  if (placed < 0) {
    last_refusal = "the fragment does not fit there";
    return -1;
  }
  // A paste is a terrain edit like any other, so the regions it joined or split
  // have to be relabelled before anything reads them.
  pf_map_rebuild_regions(map_);
  ClearSelection();
  pasting_ = false;
  last_refusal.clear();
  Bump();
  return placed;
}

// Each bumps the revision: a preview that rasterised the fragment is now
// showing the shape it used to be.

bool Editor::FlipClipboard() {
  if (!clipboard_ || pf_clipboard_flip(clipboard_) != PF_OK) return false;
  clipboard_revision_++;
  return true;
}

bool Editor::MirrorClipboard() {
  if (!clipboard_ || pf_clipboard_mirror(clipboard_) != PF_OK) return false;
  clipboard_revision_++;
  return true;
}

bool Editor::RotateClipboard(int quarter_turns) {
  if (!clipboard_ || pf_clipboard_rotate(clipboard_, quarter_turns) != PF_OK) {
    return false;
  }
  clipboard_revision_++;
  return true;
}

// -------------------------------------------------------------------- units

std::string Editor::PlacementRefusal(int x, int y, int type,
                                     const std::vector<int>& ignore) const {
  if (!map_) return "there is no map open";
  // The whole question, asked once. Stacking and the edge rule used to be worked
  // out here, which meant paste — which happens inside the core — did not know
  // about them and dropped units on top of units.
  const int why = pf_map_placement_check_ex(
      map_, x, y, type, ignore.empty() ? nullptr : ignore.data(), int(ignore.size()));
  if (why == PF_PLACE_OK) return {};

  // The game's own sentence where it has one, which is how a localised install
  // gets a localised refusal. Used whole rather than joined to the unit's name:
  // these are complete sentences aimed at the person, not fragments that take a
  // subject. It only answers for three of the codes — for the rest it says "You
  // cannot build there." however the placement was wrong, and the lines below
  // say which.
  char said[160] = {};
  if (pf_placement_message(why, said, int(sizeof(said))) > 0 && said[0]) {
    return said;
  }

  // Two of the codes are about the map's other contents rather than about this
  // unit, so naming the unit would read oddly: it is not the footman that is on
  // the edge, it is where you pointed.
  if (why == PF_PLACE_OUT_OF_BOUNDS) return "that is outside the map";
  if (why == PF_PLACE_OCCUPIED) return "another unit is already there";
  if (why == PF_PLACE_ON_EDGE) return "that is on the map edge";
  const char* name = pf_unit_name(type);
  return std::string(name ? name : "that") + " " + PlacementReason(why);
}

bool Editor::PasteWouldPlace(int x, int y, int type) const {
  if (!map_) return false;
  return pf_map_placement_check_ex(map_, x, y, type, nullptr, 0) == PF_PLACE_OK;
}

void Editor::PlaceOrigin(int x, int y, int type, int& ox, int& oy) const {
  // A 1x1 unit is placed where you point. Anything bigger centres under the
  // cursor, snapped to whole tiles, so aiming a keep is not arithmetic.
  int fw = 1, fh = 1;
  pf_map_unit_footprint(map_, type, &fw, &fh);
  ox = x - ((fw - 1) >> 1);
  oy = y - ((fh - 1) >> 1);
  // Ships and flying units go on a two-tile grid, which is where the game's
  // editor puts every one of them. Snapped rather than refused: the tiles
  // between are not a placement to explain, they are half a tile of pointer
  // travel, and a ghost that jumps two at a time says the rule by moving.
  const int step = pf_unit_placement_step(type);
  if (step > 1) {
    ox -= ((ox % step) + step) % step;
    oy -= ((oy % step) + step) % step;
  }
}

int Editor::PlaceUnit(int x, int y) {
  int fw = 1, fh = 1;
  pf_map_unit_footprint(map_, placing_type, &fw, &fh);
  const std::vector<int> spots = SymmetryPoints(x, y, fw, fh);
  int first = -1;
  if (spots.size() <= 2) {
    first = PlaceOneUnit(x, y);
  } else {
    // A mirrored placement is one action, so one undo step. Restored rather than
    // cleared, because a placement drag has already switched grouping on for the
    // whole run and clearing it here would cost an undo step per tile.
    const bool was_grouping = grouping_;
    Checkpoint();
    grouping_ = true;
    first = PlaceOneUnit(x, y);
    for (size_t i = 2; i + 1 < spots.size(); i += 2) {
      PlaceOneUnit(spots[i], spots[i + 1]);
    }
    grouping_ = was_grouping;
  }
  if (placing_run_) {
    if (first >= 0) run_placed_++; else run_refused_++;
    // From the first unit that actually lands, the rest of the drag joins its
    // checkpoint — so a drag that places nothing costs no undo step, the same
    // rule the bulk edits follow.
    grouping_ = run_placed_ > 0;
  }
  return first;
}

void Editor::BeginPlacementRun() {
  if (placing_run_) return;
  placing_run_ = true;
  run_placed_ = 0;
  run_refused_ = 0;
}

int Editor::EndPlacementRun() {
  if (!placing_run_) return 0;
  placing_run_ = false;
  grouping_ = false;
  return run_placed_;
}

bool Editor::LeavePlacement() {
  if (mode_ != Mode::kUnit || tool_ != Tool::kPlace) return false;
  // Ended rather than abandoned: the units a drag already laid keep the undo
  // step they were grouped into.
  EndPlacementRun();
  SetTool(Tool::kSelect);
  return true;
}

int Editor::PlaceOneUnit(int x, int y) {
  const std::string refusal = PlacementRefusal(x, y, placing_type);
  if (!refusal.empty()) { last_refusal = refusal; return -1; }
  last_refusal.clear();
  Checkpoint();
  // A resource placed with nothing in it is useless, so it starts at whatever
  // the shipped maps most often carry. Which amount, and which of gold and oil
  // this even is, is the core's to say.
  const int value = pf_unit_default_value(placing_type);
  // Scenery belongs to nobody whoever is selected in the palette: a gold mine
  // owned by red is a mistake, and every shipped one is neutral.
  const int forced = pf_unit_default_owner(placing_type);
  const int owner = forced >= 0 ? forced : placing_owner;
  const int index = pf_map_add_unit(map_, x, y, placing_type, owner, value);
  // Placing is not selecting, and it drops whatever was selected: otherwise the
  // next drag moves units you had stopped thinking about.
  if (index >= 0) { ClearSelection(); Bump(); }
  return index;
}

bool Editor::MoveSelectionBy(int dx, int dy, bool checkpoint) {
  if (!map_ || selected_.empty()) return false;
  const std::vector<int> moving(selected_.begin(), selected_.end());
  // All or nothing: the whole selection keeps its shape or nothing moves.
  for (int i : moving) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) != PF_OK) return false;
    const std::string refusal = PlacementRefusal(u.x + dx, u.y + dy, u.type, moving);
    if (!refusal.empty()) { last_refusal = refusal; return false; }
  }
  last_refusal.clear();
  if (checkpoint) Checkpoint();
  for (int i : moving) {
    pf_unit u{};
    if (pf_map_unit(map_, i, &u) == PF_OK) {
      pf_map_move_unit(map_, i, u.x + dx, u.y + dy);
    }
  }
  Bump();
  return true;
}

bool Editor::DeleteSelected() {
  if (!map_ || selected_.empty()) return false;
  Checkpoint();
  // Removing a unit renumbers everything after it, so delete from the back.
  int removed = 0;
  for (auto it = selected_.rbegin(); it != selected_.rend(); ++it) {
    if (pf_map_remove_unit(map_, *it) == PF_OK) removed++;
  }
  selected_.clear();
  if (removed) Bump();
  return removed > 0;
}

bool Editor::EraseAt(int x, int y) {
  const int index = map_ ? pf_map_unit_at(map_, x, y) : -1;
  if (index < 0) return false;
  Checkpoint();
  if (pf_map_remove_unit(map_, index) != PF_OK) return false;
  selected_.erase(index);
  Bump();
  return true;
}

int Editor::MisplacementChecks() const {
  int checks = PF_MISPLACED_OFF_MAP;
  if (!allow_illegal_placement_) checks |= PF_MISPLACED_TERRAIN;
  if (!allow_stacked_units_) checks |= PF_MISPLACED_OVERLAP;
  return checks;
}

int Editor::MisplacedUnitCount() const {
  if (!map_) return 0;
  return pf_map_misplaced_units(map_, MisplacementChecks(), nullptr, 0);
}

int Editor::RemoveMisplacedUnits(int checks) {
  if (!map_) return 0;
  // Counted before the checkpoint rather than undone after an empty removal:
  // inside a group Checkpoint is a no-op, and the undo would then pop a step
  // belonging to whatever the group is doing.
  if (pf_map_misplaced_units(map_, checks, nullptr, 0) <= 0) return 0;
  Checkpoint();
  const int removed = pf_map_remove_misplaced_units(map_, checks);
  // Every index after a removed unit has moved, so no held selection survives.
  ClearSelection();
  Bump();
  return removed;
}

bool Editor::ListsUnit(int type, bool with_unused) {
  // Never, whatever the option says: walls are terrain here, and the core
  // marks the two wall-as-unit ids as ones no editor should offer.
  if (pf_unit_never_offered(type)) return false;
  if (with_unused) return true;
  // `pf_unit_is_unused` is a subset of the opt-in set — the corpus test asserts
  // it — but both are asked so a future unused id that nobody remembered to add
  // to the opt-in table is still kept out of the palette.
  return !pf_unit_is_unused(type) && !pf_unit_needs_opt_in(type);
}

bool Editor::OffersUnit(int type) const {
  if (show_all_races) return true;
  const char race = pf_unit_race(type);
  // Neutral belongs to nobody, and a hero belongs to whoever the story says.
  if (race != 'h' && race != 'o') return true;
  if (pf_unit_category(type) == PF_CATEGORY_HERO) return true;
  if (!map_) return true;
  const int want = pf_map_race(map_, placing_owner);
  if (want != PF_RACE_HUMAN && want != PF_RACE_ORC) return true;
  return race == (want == PF_RACE_HUMAN ? 'h' : 'o');
}

int Editor::CounterpartFor(int type, int owner) const {
  if (show_all_races || !map_) return type;
  const char race = pf_unit_race(type);
  if (race != 'h' && race != 'o') return type;
  const int want = pf_map_race(map_, owner);
  if (want != PF_RACE_HUMAN && want != PF_RACE_ORC) return type;
  if (race == (want == PF_RACE_HUMAN ? 'h' : 'o')) return type;
  const int other = pf_unit_counterpart(type);
  return other < 0 ? type : other;   // a hero, or something unpaired
}

int Editor::TypeForOwner(int type, int owner, int x, int y) const {
  const int other = CounterpartFor(type, owner);
  if (other == type) return type;
  // The terrain-only check, the same one SwitchPlayerRace uses: the unit being
  // replaced is still standing here, so the whole question would answer
  // "occupied" about the thing we are about to remove.
  if (!allow_illegal_placement_ &&
      pf_map_placement_check(map_, x, y, other) != PF_PLACE_OK) {
    return type;
  }
  return other;
}

bool Editor::RetargetPlacingType() {
  const int becomes = CounterpartFor(placing_type, placing_owner);
  if (becomes == placing_type) return false;
  placing_type = becomes;
  return true;
}

int Editor::SetUnitOwnerAndValue(int index, int owner, int value) {
  if (!map_ || index < 0) return -1;
  pf_unit unit{};
  if (pf_map_unit(map_, index, &unit) != PF_OK) return -1;
  if (unit.owner == owner && unit.value == value) return index;
  Checkpoint();
  const int becomes = TypeForOwner(unit.type, owner, unit.x, unit.y);
  if (becomes == unit.type) {
    if (unit.owner != owner) pf_map_set_unit_owner(map_, index, owner);
    if (unit.value != value) pf_map_set_unit_value(map_, index, value);
    MarkMapChanged();
    return index;
  }
  pf_map_remove_unit(map_, index);
  const int added = pf_map_add_unit(map_, unit.x, unit.y, becomes, owner, value);
  // The selection is by index and the indices have all moved, so it is found
  // again by the one thing that did not: where the unit stands.
  ClearSelection();
  if (added >= 0) SelectAt(unit.x, unit.y, false);
  MarkMapChanged();
  return added;
}

bool Editor::SetSelectedOwner(int owner) {
  if (!map_ || selected_.empty()) return false;
  Checkpoint();
  bool changed = false;
  bool converted = false;
  // Where each selected unit stands, read as it is visited and before it is
  // touched. A conversion is a remove and an add, so the selection cannot be
  // kept by index — but a unit's tile does not move.
  std::vector<std::pair<int, int>> tiles;
  tiles.reserve(selected_.size());
  // Descending, so the indices still to be visited are the ones a removal below
  // them cannot have shifted. Adding appends, which disturbs neither.
  for (auto it = selected_.rbegin(); it != selected_.rend(); ++it) {
    const int index = *it;
    pf_unit unit{};
    if (pf_map_unit(map_, index, &unit) != PF_OK) continue;
    tiles.push_back({unit.x, unit.y});
    const int becomes = TypeForOwner(unit.type, owner, unit.x, unit.y);
    if (becomes == unit.type) {
      if (pf_map_set_unit_owner(map_, index, owner) == PF_OK) changed = true;
      continue;
    }
    pf_map_remove_unit(map_, index);
    if (pf_map_add_unit(map_, unit.x, unit.y, becomes, owner, unit.value) >= 0) {
      changed = converted = true;
    }
  }
  if (converted) {
    std::set<int> found;
    for (const auto& [x, y] : tiles) {
      const int index = pf_map_unit_at(map_, x, y);
      if (index >= 0) found.insert(index);
    }
    selected_.swap(found);
  }
  if (changed) Bump();
  return changed;
}

// ------------------------------------------------------- terrain selection

void Editor::ClearTerrainSelection() {
  terrain_selection_ = {};
  terrain_mask_.clear();
  terrain_selected_count_ = 0;
}

bool Editor::TerrainSelected(int x, int y) const {
  if (terrain_mask_.empty() || !map_) return false;
  const int w = pf_map_width(map_), h = pf_map_height(map_);
  if (x < 0 || y < 0 || x >= w || y >= h) return false;
  return terrain_mask_[size_t(y) * size_t(w) + size_t(x)] != 0;
}

void Editor::RebuildTerrainBounds() {
  terrain_selection_ = {};
  terrain_selected_count_ = 0;
  if (!map_ || terrain_mask_.empty()) return;
  const int w = pf_map_width(map_), h = pf_map_height(map_);
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (!terrain_mask_[size_t(y) * size_t(w) + size_t(x)]) continue;
      terrain_selected_count_++;
      x0 = std::min(x0, x); y0 = std::min(y0, y);
      x1 = std::max(x1, x); y1 = std::max(y1, y);
    }
  }
  // Nothing left in it: drop the mask too, so "is anything selected" stays one
  // cheap test and a subtracted-away selection really is gone.
  if (x1 < x0) { terrain_mask_.clear(); return; }
  terrain_selection_ = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

void Editor::SelectTerrain(int x, int y, int w, int h, Pick how) {
  if (!map_) { ClearTerrainSelection(); return; }
  if (w <= 0 || h <= 0) {
    // A degenerate drag replaces with nothing and adds nothing; subtracting
    // nothing leaves what was there.
    if (how == Pick::kReplace) ClearTerrainSelection();
    return;
  }
  const int mw = pf_map_width(map_), mh = pf_map_height(map_);
  const int x0 = std::max(0, std::min(x, mw - 1));
  const int y0 = std::max(0, std::min(y, mh - 1));
  const int x1 = std::min(mw, x0 + w), y1 = std::min(mh, y0 + h);

  if (how == Pick::kReplace) terrain_mask_.clear();
  // Subtracting from nothing has nothing to take away, and must not conjure a
  // full mask to take it out of.
  if (terrain_mask_.empty() && how == Pick::kSubtract) return;
  if (terrain_mask_.size() != size_t(mw) * size_t(mh)) {
    terrain_mask_.assign(size_t(mw) * size_t(mh), 0);
  }
  const uint8_t value = how == Pick::kSubtract ? 0 : 1;
  for (int ty = y0; ty < y1; ty++) {
    for (int tx = x0; tx < x1; tx++) {
      terrain_mask_[size_t(ty) * size_t(mw) + size_t(tx)] = value;
    }
  }
  RebuildTerrainBounds();
}

void Editor::SelectAllTerrain() {
  if (map_) SelectTerrain(0, 0, pf_map_width(map_), pf_map_height(map_));
}

int Editor::FillTerrainSelection() {
  const TileRect rect = terrain_selection_;
  if (!map_ || rect.empty()) return 0;
  Checkpoint();
  grouping_ = true;
  painted_ = {};
  int painted = 0;
  // Painting tile by tile lets the corner model fit the edges, which is what
  // makes a filled region blend with what surrounds it. The box is only where to
  // look; the mask says which of those tiles were asked for.
  for (int y = rect.y; y < rect.y + rect.h; y++) {
    for (int x = rect.x; x < rect.x + rect.w; x++) {
      if (!TerrainSelected(x, y)) continue;
      if (PaintOne(x, y, 1)) painted++;
    }
  }
  grouping_ = false;
  painted_ = {};
  painted_mask_.clear();
  pf_map_rebuild_regions(map_);
  if (painted) Bump();
  return painted;
}

// ---------------------------------------------------------- acting in bulk

namespace {

/// A small xorshift, so a scatter does not depend on the host's rand().
struct Xorshift {
  uint32_t state;
  explicit Xorshift(uint32_t seed) : state(seed ? seed : 1u) {}
  double next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return double(state) / 4294967296.0;
  }
};

}  // namespace

TileRect Editor::BulkScope() const {
  if (!terrain_selection_.empty()) return terrain_selection_;
  if (!map_) return {};
  return {0, 0, pf_map_width(map_), pf_map_height(map_)};
}

bool Editor::InBulkScope(int x, int y) const {
  // No selection means the scope is the map, and every tile is in it.
  if (terrain_selection_.empty()) return true;
  return TerrainSelected(x, y);
}

int Editor::CountTerrain(int terrain) const {
  const TileRect box = BulkScope();
  if (!map_ || box.empty()) return 0;
  int n = 0;
  for (int y = box.y; y < box.y + box.h; y++) {
    for (int x = box.x; x < box.x + box.w; x++) {
      if (!InBulkScope(x, y)) continue;
      const int tile = pf_map_tile_at(map_, x, y);
      if (tile >= 0 && pf_tile_dominant_terrain(uint16_t(tile)) == terrain) n++;
    }
  }
  return n;
}

int Editor::ReplaceTerrain(int from, int to) {
  const TileRect box = BulkScope();
  if (!map_ || box.empty() || from == to) return 0;

  // Collect first: painting changes tiles underneath the scan, and a scan that
  // walks into its own results would chase the replacement across the map.
  std::vector<std::pair<int, int>> targets;
  for (int y = box.y; y < box.y + box.h; y++) {
    for (int x = box.x; x < box.x + box.w; x++) {
      if (!InBulkScope(x, y)) continue;
      const int tile = pf_map_tile_at(map_, x, y);
      if (tile >= 0 && pf_tile_dominant_terrain(uint16_t(tile)) == from) {
        targets.emplace_back(x, y);
      }
    }
  }
  if (targets.empty()) return 0;

  Checkpoint();
  grouping_ = true;
  painted_ = {};
  // Several passes: painting one tile refits its neighbours, which can put a
  // tile painted a moment ago back the way it was. It settles within a few, and
  // the cap stops a pair of terrains that cannot coexist from spinning.
  constexpr int kMaxPasses = 8;
  for (int pass = 0; pass < kMaxPasses; pass++) {
    int remaining = 0;
    for (const auto& at : targets) {
      // The shade this tile will end up, so the settled test is against what
      // will really be there. Comparing against `to` alone would never settle
      // for a tile the mixture sends to the other drawing.
      const int want = ShadedTerrain(to, at.first, at.second);
      const int tile = pf_map_tile_at(map_, at.first, at.second);
      if (tile >= 0 && pf_tile_dominant_terrain(uint16_t(tile)) != want) {
        MarkPainted(at.first, at.second, 1);
        pf_map_paint_terrain(map_, at.first, at.second, want, 1);
        remaining++;
      }
    }
    if (!remaining) break;
  }
  grouping_ = false;
  painted_ = {};
  painted_mask_.clear();
  pf_map_rebuild_regions(map_);
  Bump();
  return int(targets.size());
}

Editor::BulkResult Editor::DecorateTerrain(int terrain, double density,
                                           uint32_t seed) {
  BulkResult result;
  const TileRect box = BulkScope();
  if (!map_ || box.empty()) return result;
  const double clamped = density < 0.0 ? 0.0 : (density > 1.0 ? 1.0 : density);
  if (clamped == 0.0) return result;

  Xorshift rng(seed);
  std::vector<std::pair<int, int>> targets;
  for (int y = box.y; y < box.y + box.h; y++) {
    for (int x = box.x; x < box.x + box.w; x++) {
      // The die is rolled for every tile of the box, in scope or not, so that
      // one seed describes one scatter however the selection is shaped.
      const bool hit = rng.next() < clamped;
      if (hit && InBulkScope(x, y)) targets.emplace_back(x, y);
    }
  }
  if (targets.empty()) return result;

  Checkpoint();
  grouping_ = true;
  painted_ = {};
  // What the map already could not stand is grandfathered, exactly as a brush
  // stroke grandfathers it: real maps carry units our placement rule would
  // refuse, and a scatter in one corner must not take a base out of another.
  // Snapshot regardless of the option, so toggling it cannot widen the sweep.
  pre_stranded_ = StrandedNow();
  for (const auto& at : targets) {
    MarkPainted(at.first, at.second, 1);
    const int want = ShadedTerrain(terrain, at.first, at.second);
    if (pf_map_paint_terrain(map_, at.first, at.second, want, 1) == PF_OK) {
      result.changed++;
    }
  }
  grouping_ = false;
  painted_ = {};
  painted_mask_.clear();
  // After the whole scatter rather than per tile: legalisation reaches past the
  // tile painted, so a unit beside a seeded tree can be stranded by a tile that
  // is not the one under it, and a unit is only really stranded once the
  // terrain has stopped moving.
  result.removed = RemoveStrandedUnits();
  pf_map_rebuild_regions(map_);
  if (result.changed) Bump();
  return result;
}

int Editor::CountUnitsOfType(int type, bool selected_only) const {
  if (!map_) return 0;
  int n = 0;
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    if (selected_only && !selected_.count(i)) continue;
    pf_unit unit{};
    if (pf_map_unit(map_, i, &unit) == PF_OK && unit.type == type) n++;
  }
  return n;
}

Editor::BulkResult Editor::ReplaceUnitType(int from, int to, bool selected_only) {
  BulkResult result;
  if (!map_ || from == to) return result;

  // Decide before editing, so a replace that converts nothing costs no undo
  // step: pressing it on the wrong type should not eat the step the user was
  // about to undo back to.
  struct Target { int index; pf_unit unit; };
  std::vector<Target> legal;
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    if (selected_only && !selected_.count(i)) continue;
    pf_unit unit{};
    if (pf_map_unit(map_, i, &unit) != PF_OK || unit.type != from) continue;
    if (allow_illegal_placement_ ||
        pf_map_placement_check(map_, unit.x, unit.y, to) == 0) {
      legal.push_back({i, unit});
    } else {
      result.skipped++;
    }
  }
  if (legal.empty()) return result;

  Checkpoint();
  // Back to front: removing a unit renumbers the ones after it.
  for (auto it = legal.rbegin(); it != legal.rend(); ++it) {
    pf_map_remove_unit(map_, it->index);
    if (pf_map_add_unit(map_, it->unit.x, it->unit.y, to, it->unit.owner,
                        it->unit.value) >= 0) {
      result.changed++;
    } else {
      result.skipped++;
    }
  }
  ClearSelection();
  if (result.changed) Bump();
  return result;
}

Editor::BulkResult Editor::SwitchPlayerRace(int owner, int race) {
  BulkResult result;
  if (!map_ || owner < 0 || owner >= PF_PLAYER_COUNT) return result;
  const char want = race == 0 ? 'h' : 'o';

  struct Target { int index; pf_unit unit; int other; };
  std::vector<Target> legal;
  const int count = pf_map_unit_count(map_);
  for (int i = 0; i < count; i++) {
    pf_unit unit{};
    if (pf_map_unit(map_, i, &unit) != PF_OK || unit.owner != owner) continue;
    const char unit_race = pf_unit_race(unit.type);
    if (unit_race == 'n' || unit_race == want) continue;
    const int other = pf_unit_counterpart(unit.type);
    if (other < 0) { result.kept++; continue; }   // a hero, or something unpaired
    if (allow_illegal_placement_ ||
        pf_map_placement_check(map_, unit.x, unit.y, other) == 0) {
      legal.push_back({i, unit, other});
    } else {
      result.skipped++;
    }
  }

  const bool side_already = pf_map_race(map_, owner) == race;
  if (legal.empty() && side_already) return result;

  Checkpoint();
  for (auto it = legal.rbegin(); it != legal.rend(); ++it) {
    pf_map_remove_unit(map_, it->index);
    if (pf_map_add_unit(map_, it->unit.x, it->unit.y, it->other, it->unit.owner,
                        it->unit.value) >= 0) {
      result.changed++;
    } else {
      result.skipped++;
    }
  }
  // SIDE follows the units. The two disagreeing is what makes a swapped base
  // unbuildable rather than merely odd.
  pf_map_set_race(map_, owner, race);
  ClearSelection();
  Bump();
  return result;
}

int Editor::PickUnitType(int x, int y) {
  return map_ ? PickUnitTypeOf(pf_map_unit_at(map_, x, y)) : -1;
}

int Editor::PickUnitTypeOf(int index) {
  if (!map_ || index < 0) return -1;
  pf_unit unit{};
  if (pf_map_unit(map_, index, &unit) != PF_OK) return -1;
  placing_type = unit.type;
  // The owner too: picking a blue tower to lay more of almost never means laying
  // red ones. Scenery is the exception the core already enforces.
  //
  // Unless the map was hand-edited into one of the seven slots the game does not
  // support. Nothing offers those any more, so adopting one would arm an owner
  // the dropdown cannot show and no menu can get back to.
  if (pf_player_is_supported(unit.owner)) placing_owner = unit.owner;
  SetTool(Tool::kPlace);
  return index;
}

// ------------------------------------------------------------- movement

int Editor::MovementOverrides() const {
  const TileRect box = BulkScope();
  if (!map_ || box.empty()) return 0;
  int n = 0;
  for (int y = box.y; y < box.y + box.h; y++) {
    for (int x = box.x; x < box.x + box.w; x++) {
      if (!InBulkScope(x, y)) continue;
      const int tile = pf_map_tile_at(map_, x, y);
      if (tile < 0) continue;
      if (pf_map_movement_at(map_, x, y) != pf_tile_movement(tile)) n++;
    }
  }
  return n;
}

int Editor::ResetMovement() {
  const TileRect box = BulkScope();
  if (!map_ || box.empty()) return 0;
  // Decide first: a reset that changes nothing must not cost an undo step.
  if (MovementOverrides() == 0) return 0;
  Checkpoint();
  // The core takes a rectangle, and the selection may not be one. A row at a
  // time, one call per unbroken run of selected tiles: the runs are exactly the
  // rectangles the selection is made of, so nothing outside it is touched and
  // the whole thing is still one undo step.
  int changed = 0;
  for (int y = box.y; y < box.y + box.h; y++) {
    int run = -1;
    for (int x = box.x; x <= box.x + box.w; x++) {
      const bool inside = x < box.x + box.w && InBulkScope(x, y);
      if (inside && run < 0) run = x;
      if (inside || run < 0) continue;
      changed += pf_map_reset_movement(map_, run, y, x - run, 1);
      run = -1;
    }
  }
  if (changed) Bump();
  return changed;
}

}  // namespace pfwin
