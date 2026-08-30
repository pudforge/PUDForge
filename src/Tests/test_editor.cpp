// The Windows client's editor state — tools, strokes, selection, refusals.
//
// pfwin::Editor is the one part of PUDForgeWin with no Win32 in it,
// which is what lets the client's behaviour be tested on a machine with no
// MSVC: these tests drive the same calls the window layer makes from mouse
// and keyboard events. It is a port of PUDForgeWeb's editor.mjs, so several of
// these assert the behaviours that file documents.

#include "harness.hpp"

#include "../PUDForgeWin/Editor.hpp"

TEST_GROUP("editor")

using pfwin::Editor;
using pfwin::Mode;
using pfwin::TileRect;
using pfwin::Tool;

namespace {

/// A blank 64x64 forest map, freed by the caller.
pf_map* blank() {
  pf_status status = PF_OK;
  pf_map* map = pf_map_create(64, 64, PF_TILESET_FOREST, &status);
  CHECK(map != nullptr);
  return map;
}

/// The brush index for a terrain class, or -1.
int brush_for(int terrain) {
  for (int i = 0; i < pf_brush_count(); i++) {
    if (pf_brush_terrain(i) == terrain) return i;
  }
  return -1;
}

}  // namespace

TEST(stroke_is_one_undo_step) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);

  ed.BeginStroke();
  for (int x = 10; x < 20; x++) CHECK(ed.PaintAt(x, 10));
  ed.EndStroke();

  CHECK(ed.CanUndo());
  CHECK(ed.Undo());
  // One undo puts the whole drag back, not one tile of it.
  CHECK(!ed.CanUndo());
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 15, 10))),
           int(PF_TERRAIN_GROUND_LIGHT));
  pf_map_free(map);
}

TEST(painting_bumps_revision_and_dirty) {
  pf_map* map = blank();
  Editor ed(map);
  CHECK(!ed.Dirty());
  ed.BeginStroke();
  ed.PaintAt(5, 5);
  ed.EndStroke();
  CHECK(ed.Dirty());
  ed.MarkClean();
  CHECK(!ed.Dirty());
  pf_map_free(map);
}

TEST(mirrored_stroke_paints_the_reflection) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);
  ed.mirrors = PF_MIRROR_LEFT_RIGHT;

  ed.BeginStroke();
  CHECK(ed.PaintAt(4, 8));
  ed.EndStroke();

  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 4, 8))),
           int(PF_TERRAIN_FOREST));
  // width - x - 1 for a 1x1 brush.
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 64 - 4 - 1, 8))),
           int(PF_TERRAIN_FOREST));
  pf_map_free(map);
}

TEST(toggle_mirror_combines_and_none_clears) {
  pf_map* map = blank();
  Editor ed(map);
  ed.ToggleMirror(PF_MIRROR_LEFT_RIGHT);
  ed.ToggleMirror(PF_MIRROR_TOP_BOTTOM);
  CHECK_EQ(ed.mirrors, PF_MIRROR_LEFT_RIGHT | PF_MIRROR_TOP_BOTTOM);
  ed.ToggleMirror(PF_MIRROR_LEFT_RIGHT);
  CHECK_EQ(ed.mirrors, int(PF_MIRROR_TOP_BOTTOM));
  ed.ToggleMirror(PF_MIRROR_NONE);
  CHECK_EQ(ed.mirrors, int(PF_MIRROR_NONE));
  pf_map_free(map);
}

TEST(wall_brush_paints_walls) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_WALL_HUMAN);
  CHECK(ed.brush_index >= 0);
  ed.BeginStroke();
  CHECK(ed.PaintAt(20, 20));
  ed.EndStroke();
  CHECK(pf_terrain_is_wall(
      pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 20, 20)))));
  pf_map_free(map);
}

TEST(place_select_move_delete) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;   // footman
  ed.placing_owner = 0;

  const int index = ed.PlaceUnit(10, 10);
  CHECK(index >= 0);
  CHECK(ed.last_refusal.empty());
  // Placing is not selecting.
  CHECK(!ed.HasSelection());

  CHECK_EQ(ed.SelectAt(10, 10, false), index);
  CHECK_EQ(ed.SelectedUnit(), index);

  CHECK(ed.MoveSelectionBy(2, 3, true));
  pf_unit u{};
  CHECK(pf_map_unit(map, index, &u) == PF_OK);
  CHECK_EQ(int(u.x), 12);
  CHECK_EQ(int(u.y), 13);

  CHECK(ed.DeleteSelected());
  CHECK_EQ(pf_map_unit_count(map), 0);
  CHECK(!ed.HasSelection());
  pf_map_free(map);
}

TEST(stacking_is_refused_with_a_reason) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK_EQ(ed.PlaceUnit(10, 10), -1);
  CHECK(ed.last_refusal == "another unit is already there");
  // Lifting the option lets it through: stacking is legal in the format.
  ed.SetAllowStackedUnits(true);
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  pf_map_free(map);
}

TEST(edge_placement_is_refused_by_default) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK_EQ(ed.PlaceUnit(0, 5), -1);
  CHECK(ed.last_refusal == "that is on the map edge");
  ed.SetAllowEdgePlacement(true);
  CHECK(ed.PlaceUnit(0, 5) >= 0);
  pf_map_free(map);
}

TEST(refusals_name_the_unit_and_the_reason) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.BeginStroke();
  ed.brush_size = 9;
  ed.PaintAt(30, 30);
  ed.EndStroke();

  ed.placing_type = 0;   // footman, a land unit
  CHECK_EQ(ed.PlaceUnit(30, 30), -1);
  CHECK(ed.last_refusal.find("Footman") != std::string::npos);
  CHECK(ed.last_refusal.find("ground") != std::string::npos);
  pf_map_free(map);
}

TEST(scenery_lands_neutral_whoever_is_chosen) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0x5c;   // gold mine
  ed.placing_owner = 0;     // player 1 picked in the palette
  const int index = ed.PlaceUnit(20, 20);
  CHECK(index >= 0);
  pf_unit u{};
  CHECK(pf_map_unit(map, index, &u) == PF_OK);
  CHECK_EQ(int(u.owner), 15);
  // And it starts with the shipped maps' usual 40,000 gold: 16 * 2500.
  CHECK_EQ(int(u.value), 16);
  pf_map_free(map);
}

TEST(mirrored_placement_is_one_undo_step) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  ed.mirrors = PF_MIRROR_LEFT_RIGHT;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK_EQ(pf_map_unit_count(map), 2);
  CHECK(ed.Undo());
  // Undoing half a symmetric edit would leave the map lopsided.
  CHECK_EQ(pf_map_unit_count(map), 0);
  pf_map_free(map);
}

TEST(a_placement_run_is_one_undo_step) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;   // footman
  // A drag that lays a row of units: the canvas offers every tile the pointer
  // crosses, and the whole gesture is one thing the hand did.
  ed.BeginPlacementRun();
  CHECK(ed.PlacementRunActive());
  for (int x = 10; x < 20; x++) CHECK(ed.PlaceUnit(x, 10) >= 0);
  CHECK_EQ(ed.EndPlacementRun(), 10);
  CHECK(!ed.PlacementRunActive());
  CHECK_EQ(pf_map_unit_count(map), 10);
  CHECK_EQ(ed.run_refused(), 0);

  // One press of undo takes the whole row, not the last footman of it.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 0);
  pf_map_free(map);
}

TEST(a_placement_run_that_places_nothing_costs_no_undo_step) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  // Something to undo back to, so "nothing was consumed" is checkable.
  ed.PlaceUnit(40, 40);
  CHECK_EQ(pf_map_unit_count(map), 1);

  // Now a drag that lands nothing: straight over the unit already there,
  // which the stacking rule turns down at every tile.
  ed.BeginPlacementRun();
  for (int i = 0; i < 8; i++) CHECK_EQ(ed.PlaceUnit(40, 40), -1);
  CHECK_EQ(ed.EndPlacementRun(), 0);
  CHECK_EQ(ed.run_refused(), 8);

  // The undo step the failed drag must not have eaten is the placement.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 0);
  pf_map_free(map);
}

TEST(a_placement_run_counts_what_would_not_fit) {
  pf_map* map = blank();
  Editor ed(map);
  // Water down one half of the map, so a run across it is refused there and
  // lands on the other side — which is what a drag across a coastline is.
  for (int y = 0; y < 64; y++) {
    for (int x = 32; x < 64; x++) {
      pf_map_paint_terrain_raw(map, x, y, PF_TERRAIN_WATER_LIGHT, 1);
    }
  }
  pf_map_rebuild_regions(map);
  ed.placing_type = 0;   // footman, a land unit
  ed.BeginPlacementRun();
  for (int x = 20; x < 44; x++) ed.PlaceUnit(x, 20);
  const int placed = ed.EndPlacementRun();
  CHECK_EQ(placed + ed.run_refused(), 24);
  CHECK(placed > 0);
  CHECK(ed.run_refused() > 0);
  CHECK_EQ(pf_map_unit_count(map), placed);
  pf_map_free(map);
}

TEST(leaving_placement_falls_back_to_selecting) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  ed.SetTool(Tool::kPlace);

  // Escape and the right button both arrive here, and both mean the same
  // thing: stop arming the next click.
  CHECK(ed.LeavePlacement());
  CHECK(ed.mode() == Mode::kUnit);
  CHECK(ed.tool() == Tool::kSelect);

  // Nothing left to leave, so the caller knows not to say anything — and the
  // right button is free to raise the context menu instead.
  CHECK(!ed.LeavePlacement());
  ed.SetTool(Tool::kErase);
  CHECK(!ed.LeavePlacement());
  ed.SetTool(Tool::kPaint);
  CHECK(!ed.LeavePlacement());
  CHECK(ed.tool() == Tool::kPaint);
  pf_map_free(map);
}

TEST(leaving_placement_mid_drag_closes_the_run) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  ed.SetTool(Tool::kPlace);

  // A right-click lands while the left button is still down, so the run is
  // open when the tool changes.
  ed.BeginPlacementRun();
  for (int x = 10; x < 14; x++) CHECK(ed.PlaceUnit(x, 10) >= 0);
  CHECK(ed.LeavePlacement());
  CHECK(!ed.PlacementRunActive());
  CHECK_EQ(pf_map_unit_count(map), 4);

  // The four the drag laid are still one undo step, and the next placement
  // does not join it — which is what a run left open would have done.
  ed.SetTool(Tool::kPlace);
  CHECK(ed.PlaceUnit(20, 20) >= 0);
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 4);
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 0);
  pf_map_free(map);
}

TEST(band_select_catches_footprints_not_anchors) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 90;     // castle, 4x4
  const int index = ed.PlaceUnit(12, 12);
  CHECK(index >= 0);
  pf_unit u{};
  CHECK(pf_map_unit(map, index, &u) == PF_OK);
  // A band over the far corner of the footprint still selects the unit,
  // even though its anchor tile lies outside the band.
  TileRect band{u.x + 3, u.y + 3, 2, 2};
  CHECK_EQ(ed.SelectInRect(band, false), 1);
  pf_map_free(map);
}

TEST(select_all_invert_and_groups) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;                       // footman
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  ed.placing_type = 90;                      // castle
  CHECK(ed.PlaceUnit(20, 20) >= 0);

  CHECK_EQ(ed.SelectAll(), 2);
  CHECK_EQ(ed.SelectGroup(PF_GROUP_BUILDINGS, false), 1);
  CHECK_EQ(ed.InvertSelection(), 1);
  CHECK_EQ(ed.SelectOwner(0, false), 2);
  ed.ClearSelection();
  CHECK(!ed.HasSelection());
  pf_map_free(map);
}

TEST(select_same_type_takes_the_set_of_types) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK(ed.PlaceUnit(14, 10) >= 0);
  ed.placing_type = 90;
  CHECK(ed.PlaceUnit(24, 24) >= 0);

  ed.SelectAt(10, 10, false);
  CHECK_EQ(ed.SelectSameType(), 2);
  pf_map_free(map);
}

TEST(painting_water_removes_stranded_units) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(30, 30) >= 0);

  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.brush_size = 9;
  ed.BeginStroke();
  ed.PaintAt(30, 30);
  const int stranded = ed.EndStroke();
  CHECK_EQ(stranded, 1);
  CHECK_EQ(pf_map_unit_count(map), 0);

  // Part of the stroke that caused it: one undo puts terrain and unit back.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_map_free(map);
}

TEST(keep_stranded_units_option_holds_them) {
  pf_map* map = blank();
  Editor ed(map);
  ed.keep_stranded_units = true;
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(30, 30) >= 0);
  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.brush_size = 9;
  ed.BeginStroke();
  ed.PaintAt(30, 30);
  CHECK_EQ(ed.EndStroke(), 0);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_map_free(map);
}

/**
 * A stroke may only remove what it stranded, never what it found stranded.
 *
 * Real maps carry units our placement rule refuses — twin-isles has six,
 * including both its gold mines. The first time the Windows client ran, one
 * five-tile stroke deleted every base on the map, both islands, because the
 * end-of-stroke sweep checked all units instead of the stroke's own damage.
 * The map author's pre-existing content is not the editor's to police.
 */
TEST(stroke_leaves_units_it_did_not_strand) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  const int survivor = ed.PlaceUnit(10, 10);
  CHECK(survivor >= 0);
  CHECK(ed.PlaceUnit(50, 50) >= 0);

  // Strand the first footman deliberately, with removal switched off, so the
  // map now holds a pre-existing illegal unit the way a loaded map might.
  ed.keep_stranded_units = true;
  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.brush_size = 9;
  ed.BeginStroke();
  ed.PaintAt(10, 10);
  ed.EndStroke();
  ed.keep_stranded_units = false;
  CHECK_EQ(pf_map_unit_count(map), 2);

  // A stroke far away must not touch it.
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);
  ed.brush_size = 3;
  ed.BeginStroke();
  ed.PaintAt(40, 20);
  CHECK_EQ(ed.EndStroke(), 0);
  CHECK_EQ(pf_map_unit_count(map), 2);

  // But a stroke that strands the second footman still removes it.
  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.brush_size = 9;
  ed.BeginStroke();
  ed.PaintAt(50, 50);
  CHECK_EQ(ed.EndStroke(), 1);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_map_free(map);
}

TEST(fill_respects_the_terrain_rectangle) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);
  ed.brush_shape = Editor::kShapeFill;
  ed.SelectTerrain(10, 10, 8, 8);

  ed.BeginStroke();
  CHECK(ed.PaintAt(12, 12));
  ed.EndStroke();

  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 12, 12))),
           int(PF_TERRAIN_FOREST));
  // Outside the rectangle the ground is untouched.
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 40, 40))),
           int(PF_TERRAIN_GROUND_LIGHT));
  pf_map_free(map);
}

TEST(fill_selection_is_one_undo_step) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);
  ed.SelectTerrain(5, 5, 6, 6);
  CHECK(ed.FillTerrainSelection() > 0);
  CHECK(ed.Undo());
  CHECK(!ed.CanUndo());
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 7, 7))),
           int(PF_TERRAIN_GROUND_LIGHT));
  pf_map_free(map);
}

TEST(mode_switch_adopts_the_default_tool) {
  pf_map* map = blank();
  Editor ed(map);
  CHECK(ed.mode() == Mode::kTerrain);
  CHECK(ed.tool() == Tool::kPaint);
  ed.SetMode(Mode::kUnit);
  CHECK(ed.tool() == Tool::kSelect);
  ed.SetTool(Tool::kPaint);
  // Setting a terrain tool switches the mode back with it.
  CHECK(ed.mode() == Mode::kTerrain);
  pf_map_free(map);
}

TEST(leaving_rect_tool_clears_the_rectangle) {
  pf_map* map = blank();
  Editor ed(map);
  ed.SetTool(Tool::kRect);
  ed.SelectTerrain(4, 4, 10, 10);
  CHECK(!ed.terrain_selection().empty());
  ed.SetTool(Tool::kPaint);
  CHECK(ed.terrain_selection().empty());
  pf_map_free(map);
}

TEST(cancelling_leaves_the_brush_and_stays_in_terrain_mode) {
  pf_map* map = blank();
  Editor ed(map);
  // What Escape and a right-click on the canvas ask for while painting.
  CHECK(ed.tool() == Tool::kPaint);
  CHECK(ed.ToolAfterCancel() == Tool::kRect);
  ed.SetTool(ed.ToolAfterCancel());
  CHECK(ed.tool() == Tool::kRect);
  // Within the mode: backing out of the brush is not a request to go and
  // edit units.
  CHECK(ed.mode() == Mode::kTerrain);

  // From the rectangle there is nothing to back out of, so the gesture is a
  // no-op and the right button is left to raise the menu.
  CHECK(ed.ToolAfterCancel() == Tool::kRect);
  // As it is from selecting units, which is not the brush either.
  ed.SetTool(Tool::kSelect);
  CHECK(ed.ToolAfterCancel() == Tool::kSelect);
  pf_map_free(map);
}

TEST(erase_and_owner_reassignment) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  ed.placing_owner = 0;
  const int index = ed.PlaceUnit(10, 10);
  CHECK(index >= 0);

  ed.SelectAt(10, 10, false);
  CHECK(ed.SetSelectedOwner(3));
  pf_unit u{};
  CHECK(pf_map_unit(map, index, &u) == PF_OK);
  CHECK_EQ(int(u.owner), 3);

  CHECK(ed.EraseAt(10, 10));
  CHECK_EQ(pf_map_unit_count(map), 0);
  pf_map_free(map);
}

TEST(the_palette_follows_the_chosen_players_race) {
  // With the race filter on — which is the default — the units palette shows
  // the chosen player's own race, plus what belongs to nobody in particular.
  pf_map* map = blank();
  Editor ed(map);
  CHECK(!ed.show_all_races);            // off unless asked for

  // Unit ids from overrides/race_counterparts.cpp: the pairs it names.
  constexpr int kFootman = 0x00, kGrunt = 0x01, kFarm = 0x3a, kPigFarm = 0x3b;
  constexpr int kHumanStart = 0x5e, kOrcStart = 0x5f;
  // A named hero of each side. Ids from overrides/named_heroes.cpp.
  constexpr int kAlleria = 0x14, kTeronGorefiend = 0x15;
  CHECK_EQ(int(pf_unit_category(kAlleria)), int(PF_CATEGORY_HERO));
  CHECK_EQ(int(pf_unit_category(kTeronGorefiend)), int(PF_CATEGORY_HERO));
  CHECK_EQ(int(pf_unit_race(kAlleria)), int('h'));
  CHECK_EQ(int(pf_unit_race(kTeronGorefiend)), int('o'));

  pf_map_set_race(map, 0, PF_RACE_HUMAN);
  pf_map_set_race(map, 1, PF_RACE_ORC);

  ed.placing_owner = 0;
  CHECK(ed.OffersUnit(kFootman));
  CHECK(!ed.OffersUnit(kGrunt));
  CHECK(ed.OffersUnit(kFarm));
  CHECK(!ed.OffersUnit(kPigFarm));
  // Markers go with the race, so a human player is offered one start location.
  CHECK(ed.OffersUnit(kHumanStart));
  CHECK(!ed.OffersUnit(kOrcStart));
  // Heroes of both sides stay: the shipped maps really do place them either
  // way round, and they have no opposite number to convert to.
  CHECK(ed.OffersUnit(kAlleria));
  CHECK(ed.OffersUnit(kTeronGorefiend));

  ed.placing_owner = 1;
  CHECK(!ed.OffersUnit(kFootman));
  CHECK(ed.OffersUnit(kGrunt));
  CHECK(!ed.OffersUnit(kHumanStart));
  CHECK(ed.OffersUnit(kOrcStart));
  CHECK(ed.OffersUnit(kAlleria));
  CHECK(ed.OffersUnit(kTeronGorefiend));

  // The option turns the whole thing off, and then nothing is filtered.
  ed.show_all_races = true;
  CHECK(ed.OffersUnit(kFootman));
  CHECK(ed.OffersUnit(kHumanStart));
  ed.show_all_races = false;

  // A slot with no race is not a filter anybody could act on.
  pf_map_set_race(map, 2, PF_RACE_NEUTRAL);
  ed.placing_owner = 2;
  CHECK(ed.OffersUnit(kFootman));
  CHECK(ed.OffersUnit(kGrunt));

  // And choosing a player of the other race re-arms the palette rather than
  // leaving a Footman armed under an orc.
  ed.placing_owner = 0;
  ed.placing_type = kFootman;
  ed.placing_owner = 1;
  CHECK(ed.RetargetPlacingType());
  CHECK_EQ(ed.placing_type, kGrunt);
  CHECK(!ed.RetargetPlacingType());     // already where it belongs
  pf_map_free(map);
}

TEST(handing_a_unit_to_the_other_race_converts_it) {
  // "Change this footman to player 2, who is an orc" means a grunt. Leaving it
  // a footman builds a base the game will not let that player use — and across
  // 556 shipped maps, not one ordinary unit sits under a player of the other
  // race.
  pf_map* map = blank();
  Editor ed(map);
  constexpr int kFootman = 0x00, kGrunt = 0x01, kAlleria = 0x14;
  pf_map_set_race(map, 0, PF_RACE_HUMAN);
  pf_map_set_race(map, 1, PF_RACE_ORC);

  ed.placing_type = kFootman;
  ed.placing_owner = 0;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  ed.SelectAt(10, 10, false);
  CHECK(ed.SetSelectedOwner(1));

  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_unit u{};
  CHECK_EQ(pf_map_unit_at(map, 10, 10) >= 0, true);
  CHECK(pf_map_unit(map, pf_map_unit_at(map, 10, 10), &u) == PF_OK);
  CHECK_EQ(int(u.type), kGrunt);
  CHECK_EQ(int(u.owner), 1);
  // The selection survives the remove-and-add: it is found again by tile.
  CHECK_EQ(ed.SelectedUnit(), pf_map_unit_at(map, 10, 10));

  // Back the other way, and it is a footman again.
  CHECK(ed.SetSelectedOwner(0));
  CHECK(pf_map_unit(map, pf_map_unit_at(map, 10, 10), &u) == PF_OK);
  CHECK_EQ(int(u.type), kFootman);

  // A hero has no opposite number, so it changes hands and stays itself.
  ed.placing_type = kAlleria;
  CHECK(ed.PlaceUnit(20, 20) >= 0);
  ed.SelectAt(20, 20, false);
  CHECK(ed.SetSelectedOwner(1));
  CHECK(pf_map_unit(map, pf_map_unit_at(map, 20, 20), &u) == PF_OK);
  CHECK_EQ(int(u.type), kAlleria);
  CHECK_EQ(int(u.owner), 1);

  // With the option on, nothing converts: the mapper has said they know.
  ed.show_all_races = true;
  ed.SelectAt(10, 10, false);
  CHECK(ed.SetSelectedOwner(1));
  CHECK(pf_map_unit(map, pf_map_unit_at(map, 10, 10), &u) == PF_OK);
  CHECK_EQ(int(u.type), kFootman);
  CHECK_EQ(int(u.owner), 1);
  pf_map_free(map);
}

TEST(place_origin_centres_the_footprint) {
  pf_map* map = blank();
  Editor ed(map);
  int ox = 0, oy = 0;
  ed.PlaceOrigin(20, 20, 0, ox, oy);           // 1x1: where you point
  CHECK_EQ(ox, 20);
  CHECK_EQ(oy, 20);
  ed.PlaceOrigin(20, 20, 90, ox, oy);          // castle, 4x4: just up and left
  CHECK_EQ(ox, 19);
  CHECK_EQ(oy, 19);
  pf_map_free(map);
}

TEST(pick_brush_reads_the_map) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);
  ed.BeginStroke();
  ed.PaintAt(30, 30);
  ed.EndStroke();

  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.PickBrush(30, 30);
  CHECK_EQ(pf_brush_terrain(ed.brush_index), int(PF_TERRAIN_FOREST));
  pf_map_free(map);
}

TEST(undo_drops_a_stale_selection) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  const int index = ed.PlaceUnit(10, 10);
  CHECK(index >= 0);
  ed.SelectAt(10, 10, false);
  CHECK(ed.HasSelection());
  CHECK(ed.Undo());
  // The unit the selection named no longer exists.
  CHECK(!ed.HasSelection());
  pf_map_free(map);
}

// ---------------------------------------------------------------- clipboard
//
// The fragment itself is the core's, and tested there. What these cover is the
// editor's half: which rectangle a copy means when the user has not said, what
// cut removes, that a fragment outlives the map it came from, and that a
// refused paste costs neither an undo step nor the clipboard.

TEST(copy_prefers_the_terrain_rectangle_then_the_selection) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(40, 40) >= 0);

  // Nothing selected at all: there is no rectangle to guess.
  CHECK_EQ(ed.Copy(), -1);
  CHECK(!ed.last_refusal.empty());
  CHECK(!ed.HasClipboard());

  // A unit selection stands in for one.
  ed.SelectAt(40, 40, false);
  CHECK_EQ(ed.Copy(Editor::Grab::kUnits), 1);
  CHECK(ed.HasClipboard());

  // A terrain rectangle outranks it.
  ed.SelectTerrain(0, 0, 6, 4);
  CHECK_EQ(ed.Copy(Editor::Grab::kTerrain), 0);
  const TileRect bounds = ed.ClipboardBounds();
  CHECK_EQ(bounds.w, 6);
  CHECK_EQ(bounds.h, 4);

  // An explicit rectangle outranks both.
  CHECK_EQ(ed.Copy(Editor::Grab::kUnits, {38, 38, 5, 5}), 1);
  pf_map_free(map);
}

TEST(a_copy_takes_terrain_or_units_and_never_both) {
  // A fragment carrying both drops both, so pasting a formation onto ground
  // you meant to keep repaints it and there is no way to ask for the other
  // half. Which half is the mode's answer unless the caller is explicit.
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(20, 20) >= 0);
  ed.SelectTerrain(18, 18, 6, 6);

  ed.SetMode(Mode::kTerrain);
  ed.SelectTerrain(18, 18, 6, 6);   // SetMode drops a rectangle it does not own
  CHECK_EQ(ed.Copy(), 0);           // terrain, so no units came along
  CHECK(ed.ClipboardHasTerrain());

  ed.SetTool(Tool::kSelect);        // unit mode
  CHECK_EQ(ed.Copy(Editor::Grab::kUnits, {18, 18, 6, 6}), 1);
  CHECK(!ed.ClipboardHasTerrain()); // and no terrain came along with it

  // Turning the fragment is what a preview has to notice, so each turn is a
  // new revision even though the fragment is the same allocation.
  const int was = ed.clipboard_revision();
  CHECK(ed.RotateClipboard(1));
  CHECK(ed.clipboard_revision() != was);
  pf_map_free(map);
}

TEST(cut_removes_the_units_it_captured) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK(ed.PlaceUnit(50, 50) >= 0);

  // A unit outside the rectangle is not the cut's business, even though it
  // happens to be the one selected.
  ed.SelectAt(50, 50, false);
  CHECK_EQ(ed.Cut({8, 8, 6, 6}), 1);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_unit left{};
  CHECK(pf_map_unit(map, 0, &left) == PF_OK);
  CHECK_EQ(int(left.x), 50);

  // One undo step, and the survivor is untouched by it.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 2);
  pf_map_free(map);
}

TEST(paste_puts_the_fragment_where_it_is_asked) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK_EQ(ed.Copy(Editor::Grab::kUnits, {10, 10, 4, 4}), 1);

  const int before = pf_map_unit_count(map);
  CHECK_EQ(ed.PasteAt(30, 30), 1);
  CHECK_EQ(pf_map_unit_count(map), before + 1);

  // The pasted unit landed at the fragment's own offset from its top-left.
  bool found = false;
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit u{};
    if (pf_map_unit(map, i, &u) == PF_OK && u.x == 30 && u.y == 30) found = true;
  }
  CHECK(found);

  // One undo step for the whole paste.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), before);
  pf_map_free(map);
}

TEST(copying_two_separate_squares_leaves_the_ground_between_them) {
  // Shift-adding a second square to a selection makes a shape, not a bigger
  // rectangle. A copy that only knew the bounding box brought the ground
  // between the two squares along and pasted it over whatever was there.
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);
  CHECK(ed.brush_index >= 0);

  // Forest in both squares, so the fragment carries something the destination
  // does not have and a paste is visible tile by tile.
  ed.BeginStroke();
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 3; x++) CHECK(ed.PaintAt(x + 2, y + 2));
    for (int x = 0; x < 3; x++) CHECK(ed.PaintAt(x + 12, y + 2));
  }
  ed.EndStroke();

  ed.SetMode(Mode::kTerrain);
  ed.SelectTerrain(2, 2, 3, 3);
  ed.SelectTerrain(12, 2, 3, 3, Editor::Pick::kAdd);
  CHECK_EQ(ed.terrain_selected_count(), 18);
  CHECK(!ed.TerrainSelectionIsRect());

  CHECK_EQ(ed.Copy(Editor::Grab::kTerrain), 0);
  const TileRect frag = ed.ClipboardBounds();
  CHECK_EQ(frag.w, 13);   // the bounding box is still what travels
  CHECK_EQ(frag.h, 3);

  // But it travels with holes: the eight columns between the squares are not
  // part of the fragment.
  const pf_clipboard* clip = ed.clipboard();
  CHECK_EQ(pf_clipboard_tile_included(clip, 0, 0), 1);
  CHECK_EQ(pf_clipboard_tile_included(clip, 2, 2), 1);
  CHECK_EQ(pf_clipboard_tile_included(clip, 5, 1), 0);
  CHECK_EQ(pf_clipboard_tile_included(clip, 9, 0), 0);
  CHECK_EQ(pf_clipboard_tile_included(clip, 10, 0), 1);
  CHECK_EQ(pf_clipboard_tile_included(clip, 12, 2), 1);

  // And paste writes nothing through the holes: what is under the gap after
  // the paste is what was there before it.
  //
  // Row 41 rather than row 40, which is the row the assertions below read.
  // The two agreed for as long as a blank map was one tile value repeated;
  // they stopped agreeing when it became varied ground, and the comparison
  // had been reading a neighbouring row's tile all along.
  std::vector<uint16_t> before;
  for (int x = 0; x < 13; x++) {
    before.push_back(uint16_t(pf_map_tile_at(map, 10 + x, 41)));
  }
  CHECK(ed.PasteAt(10, 40) >= 0);

  // Under the squares: forest arrived.
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 11, 41))),
           int(PF_TERRAIN_FOREST));
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 21, 41))),
           int(PF_TERRAIN_FOREST));
  // Under the gap: no forest, and the middle of it is the tile it always was.
  // Only the two columns either side of the gap may move, and those are the
  // seam the corner model has to settle.
  for (int x = 5; x <= 7; x++) {
    CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 10 + x, 41))),
             int(PF_TERRAIN_GROUND_LIGHT));
    CHECK_EQ(int(pf_map_tile_at(map, 10 + x, 41)), int(before[size_t(x)]));
  }
  pf_map_free(map);
}

TEST(paste_obeys_the_same_placement_rules_as_placing_by_hand) {
  // Stacking and the edge rule used to be worked out in this class, so paste —
  // which happens inside the core and never comes through here — could not see
  // them, and a fragment dropped over a base put units on top of the units
  // already there. One check now answers for both.
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;                    // footman
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK_EQ(ed.Copy(Editor::Grab::kUnits, {10, 10, 2, 2}), 1);

  // Straight back on top of itself: the same refusal a second click gets.
  CHECK_EQ(ed.PlaceUnit(10, 10), -1);
  CHECK_EQ(ed.PasteAt(10, 10), 0);        // the paste ran; nothing landed
  CHECK_EQ(pf_map_unit_count(map), 1);

  // Somewhere free it lands, which is what says the rule and not the paste is
  // what refused above.
  CHECK_EQ(ed.PasteAt(30, 30), 1);
  CHECK_EQ(pf_map_unit_count(map), 2);

  // The map edge, the other rule that used to be invisible to paste.
  CHECK_EQ(ed.PasteAt(0, 0), 0);
  CHECK_EQ(pf_map_unit_count(map), 2);

  // And lifting the option lifts it for paste too, which is the point of the
  // rules living in one place: stacking is legal in the format.
  ed.SetAllowStackedUnits(true);
  CHECK_EQ(ed.PasteAt(10, 10), 1);
  CHECK_EQ(pf_map_unit_count(map), 3);
  pf_map_free(map);
}

TEST(a_paste_that_does_not_fit_changes_nothing) {
  pf_map* map = blank();
  Editor ed(map);
  CHECK_EQ(ed.Copy(Editor::Grab::kTerrain, {0, 0, 8, 8}), 0);

  const int revision = ed.revision();
  // Hard against the far edge, so the fragment runs off the map.
  CHECK_EQ(ed.PasteAt(60, 60), -1);
  CHECK(!ed.last_refusal.empty());
  CHECK_EQ(ed.revision(), revision);
  // Refusing must not cost the fragment either — the next paste should work.
  CHECK(ed.HasClipboard());
  CHECK(ed.PasteAt(20, 20) >= 0);
  pf_map_free(map);
}

TEST(rotating_a_fragment_swaps_its_sides) {
  pf_map* map = blank();
  Editor ed(map);
  CHECK_EQ(ed.Copy(Editor::Grab::kTerrain, {4, 4, 8, 3}), 0);
  CHECK_EQ(ed.ClipboardBounds().w, 8);
  CHECK_EQ(ed.ClipboardBounds().h, 3);

  CHECK(ed.RotateClipboard(1));
  CHECK_EQ(ed.ClipboardBounds().w, 3);
  CHECK_EQ(ed.ClipboardBounds().h, 8);

  // Flip and mirror are shape-preserving, and both are free until pasted.
  CHECK(ed.FlipClipboard());
  CHECK(ed.MirrorClipboard());
  CHECK_EQ(ed.ClipboardBounds().w, 3);
  CHECK_EQ(ed.ClipboardBounds().h, 8);
  CHECK(ed.PasteAt(10, 10) >= 0);
  pf_map_free(map);
}

TEST(a_fragment_outlives_the_map_it_came_from) {
  pf_map* first = blank();
  Editor ed(first);
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(12, 12) >= 0);
  CHECK_EQ(ed.Copy(Editor::Grab::kUnits, {12, 12, 5, 5}), 1);

  // Opening another map must not cost the fragment: it holds corner terrains,
  // not tile values, so it is independent of where it came from.
  pf_status status = PF_OK;
  pf_map* second = pf_map_create(64, 64, PF_TILESET_SWAMP, &status);
  CHECK(second != nullptr);
  ed.SetMap(second);
  CHECK(ed.HasClipboard());
  CHECK_EQ(ed.PasteAt(20, 20), 1);
  CHECK_EQ(pf_map_unit_count(second), 1);

  pf_map_free(second);
  pf_map_free(first);
}

TEST(an_armed_paste_disarms_when_it_lands_or_is_cancelled) {
  pf_map* map = blank();
  Editor ed(map);
  // Nothing copied: arming is a no-op rather than a state that misleads.
  ed.BeginPaste();
  CHECK(!ed.pasting());

  CHECK_EQ(ed.Copy(Editor::Grab::kTerrain, {0, 0, 4, 4}), 0);
  ed.BeginPaste();
  CHECK(ed.pasting());
  ed.CancelPaste();
  CHECK(!ed.pasting());

  ed.BeginPaste();
  CHECK(ed.PasteAt(20, 20) >= 0);
  CHECK(!ed.pasting());
  pf_map_free(map);
}

TEST(a_terrain_selection_is_a_region_that_drags_add_to_and_take_from) {
  // One drag is a rectangle, which is what almost every selection is. Shift
  // adds another and alt takes one away, so an L round a base is two drags
  // rather than impossible.
  pf_map* map = blank();
  Editor ed(map);

  ed.SelectTerrain(10, 10, 6, 6);
  CHECK_EQ(ed.terrain_selected_count(), 36);
  CHECK(ed.TerrainSelectionIsRect());
  CHECK(ed.TerrainSelected(10, 10));
  CHECK(!ed.TerrainSelected(16, 10));

  // Added: the box grows to hold both, and the count is the union rather than
  // the box's area — the corner where the two do not meet is not selected.
  ed.SelectTerrain(16, 16, 4, 4, Editor::Pick::kAdd);
  CHECK_EQ(ed.terrain_selected_count(), 36 + 16);
  CHECK_EQ(ed.terrain_selection().w, 10);
  CHECK_EQ(ed.terrain_selection().h, 10);
  CHECK(!ed.TerrainSelectionIsRect());
  CHECK(ed.TerrainSelected(16, 16));
  CHECK(!ed.TerrainSelected(16, 10));

  // Subtracted: a bite out of the first rectangle, and the box shrinks only
  // when the bite reaches an edge of it.
  ed.SelectTerrain(10, 10, 2, 2, Editor::Pick::kSubtract);
  CHECK_EQ(ed.terrain_selected_count(), 36 + 16 - 4);
  CHECK(!ed.TerrainSelected(10, 10));
  CHECK(ed.TerrainSelected(12, 12));

  // A plain drag replaces the lot.
  ed.SelectTerrain(30, 30, 3, 3);
  CHECK_EQ(ed.terrain_selected_count(), 9);
  CHECK(ed.TerrainSelectionIsRect());
  CHECK(!ed.TerrainSelected(12, 12));

  // Taking away everything leaves nothing selected, not an empty box that
  // still counts as a selection: Fill and the bulk edits key off that.
  ed.SelectTerrain(28, 28, 8, 8, Editor::Pick::kSubtract);
  CHECK_EQ(ed.terrain_selected_count(), 0);
  CHECK(ed.terrain_selection().empty());

  // Subtracting when nothing is selected must not conjure a full selection to
  // take the bite out of.
  ed.SelectTerrain(5, 5, 4, 4, Editor::Pick::kSubtract);
  CHECK_EQ(ed.terrain_selected_count(), 0);
  pf_map_free(map);
}

TEST(a_fill_and_a_bulk_edit_follow_the_region_not_its_box) {
  // The whole point of being able to subtract: what gets painted is what is
  // selected, and a box drawn round an L would paint the corner too.
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_FOREST);

  ed.SelectTerrain(10, 10, 6, 6);
  ed.SelectTerrain(10, 10, 3, 3, Editor::Pick::kSubtract);
  CHECK_EQ(ed.terrain_selected_count(), 36 - 9);

  CHECK_EQ(ed.FillTerrainSelection(), 27);
  // Inside the box but not the selection: untouched.
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 11, 11))),
           int(PF_TERRAIN_GROUND_LIGHT));
  CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 14, 14))),
           int(PF_TERRAIN_FOREST));

  // And a bulk edit counts the same way: the forest just laid is all inside
  // the selection, so the count is the selection's, not the box's.
  CHECK_EQ(ed.CountTerrain(PF_TERRAIN_FOREST), 27);
  pf_map_free(map);
}

TEST(filling_a_rectangle_changes_no_decoration_around_it) {
  // Fill paints tile by tile so the corner model can fit the seam, and that
  // fitting legitimately reaches past the rectangle: dropping water into grass
  // forces a ring of coast, and the legaliser walks the terrain tree outwards
  // from there. What it must never do is *re-roll* a tile — change which
  // variation of the same four corners is on the map. Callers legalise a
  // margin eleven tiles wide, so a fill that re-picked every tile it legalised
  // would reshuffle the decoration of a whole neighbourhood, put all of it in
  // one undo step, and none of it was asked for.
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_WATER_DARK);

  std::vector<uint16_t> before(64 * 64);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      before[size_t(y) * 64 + size_t(x)] = uint16_t(pf_map_tile_at(map, x, y));
    }
  }

  ed.SelectTerrain(20, 20, 6, 4);
  CHECK_EQ(ed.FillTerrainSelection(), 24);

  int inside = 0, rerolled_outside = 0;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      const uint16_t was = before[size_t(y) * 64 + size_t(x)];
      const uint16_t now = uint16_t(pf_map_tile_at(map, x, y));
      if (was == now) continue;
      if (x >= 20 && x < 26 && y >= 20 && y < 24) { inside++; continue; }
      uint8_t a[4], b[4];
      pf_tile_quadrants(was, a);
      pf_tile_quadrants(now, b);
      // Same corners, different tile: decoration changed for no reason.
      if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]) {
        rerolled_outside++;
      }
    }
  }
  CHECK_EQ(inside, 24);
  CHECK_EQ(rerolled_outside, 0);
  pf_map_free(map);
}

TEST(painting_the_same_spot_twice_changes_nothing_the_second_time) {
  // The other half of the same property, and the one a person notices: paint,
  // paint again, and the map is untouched — no new undo step's worth of
  // difference, no shimmer under the brush.
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_WATER_DARK);

  ed.BeginStroke();
  CHECK(ed.PaintAt(30, 30));
  ed.EndStroke();

  std::vector<uint16_t> after_first(64 * 64);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      after_first[size_t(y) * 64 + size_t(x)] = uint16_t(pf_map_tile_at(map, x, y));
    }
  }

  ed.BeginStroke();
  CHECK(ed.PaintAt(30, 30));
  ed.EndStroke();

  int moved = 0;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      if (after_first[size_t(y) * 64 + size_t(x)] != uint16_t(pf_map_tile_at(map, x, y))) {
        moved++;
      }
    }
  }
  CHECK_EQ(moved, 0);
  pf_map_free(map);
}

TEST(the_eyedropper_reports_what_it_picked) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_WATER_DARK);
  // Big enough that the middle really is dark water. A one-tile dab of it in
  // open grass legalises away to coast, and then the eyedropper is right and
  // the test is wrong.
  ed.brush_size = 7;

  ed.BeginStroke();
  CHECK(ed.PaintAt(30, 30));
  ed.EndStroke();

  CHECK(ed.PickBrush(30, 30));
  CHECK_EQ(ed.TerrainOfBrush(), int(PF_TERRAIN_WATER_DARK));
  CHECK(ed.BrushName() == pf_terrain_name(PF_TERRAIN_WATER_DARK, 0));

  // Off the map there is nothing to adopt, and the brush must not change.
  CHECK(!ed.PickBrush(-1, 0));
  CHECK(!ed.PickBrush(64, 64));
  CHECK_EQ(ed.TerrainOfBrush(), int(PF_TERRAIN_WATER_DARK));

  // The custom brush names the tile it lays, because "which tile is this" is
  // the only thing that distinguishes one custom brush from another.
  ed.brush_index = pf_brush_count();
  ed.custom_tile = -1;
  CHECK(ed.BrushName().find("paints nothing") != std::string::npos);
  ed.custom_tile = 0x0930;
  CHECK(ed.BrushName().find("0x0930") != std::string::npos);
  pf_map_free(map);
}

TEST(mixing_shades_mottles_a_stroke_and_leaves_the_rest_flat) {
  // Painting on its own only ever lays the light shade of a pair, so a painted
  // map is flat where a shipped one is mottled: 28.4% of Blizzard's shaded
  // forest terrain is the dark member, against 0% for anything painted here.
  auto dark_share = [](pf_map* map, int x0, int y0, int side) {
    int light = 0, dark = 0;
    for (int y = y0; y < y0 + side; y++) {
      for (int x = x0; x < x0 + side; x++) {
        switch (pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, x, y)))) {
          case PF_TERRAIN_GROUND_LIGHT: light++; break;
          case PF_TERRAIN_GROUND_DARK: dark++; break;
          default: break;
        }
      }
    }
    return light + dark ? 100 * dark / (light + dark) : 0;
  };

  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_GROUND_LIGHT);
  ed.brush_size = 21;
  ed.mix_shades = true;

  ed.BeginStroke();
  for (int x = 12; x <= 20; x++) CHECK(ed.PaintAt(x, 16));
  ed.EndStroke();

  // Mottled where the stroke went...
  const int inside = dark_share(map, 8, 8, 17);
  CHECK(inside > 5);
  CHECK(inside < 60);
  // ...and untouched everywhere else. A stroke that reshaded the whole map
  // would be the same class of mistake as painting rewriting its whole margin.
  CHECK_EQ(dark_share(map, 44, 44, 18), 0);

  // One undo takes the shading off with the paint that caused it.
  CHECK(ed.Undo());
  CHECK_EQ(dark_share(map, 8, 8, 17), 0);
  pf_map_free(map);
}

TEST(mixing_shades_is_off_unless_asked_and_is_stable_when_on) {
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = brush_for(PF_TERRAIN_GROUND_LIGHT);
  ed.brush_size = 15;

  auto darks = [&] {
    int n = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        if (pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, x, y))) ==
            PF_TERRAIN_GROUND_DARK) {
          n++;
        }
      }
    }
    return n;
  };

  // Off by default: painting ground on ground changes nothing at all.
  ed.BeginStroke();
  CHECK(ed.PaintAt(20, 20));
  ed.EndStroke();
  CHECK_EQ(darks(), 0);

  ed.mix_shades = true;
  ed.BeginStroke();
  CHECK(ed.PaintAt(20, 20));
  ed.EndStroke();
  const int first = darks();
  CHECK(first > 0);

  // The noise is a property of position, not of when you painted, so going
  // over the same ground again settles rather than reshuffling.
  ed.BeginStroke();
  CHECK(ed.PaintAt(20, 20));
  ed.EndStroke();
  CHECK_EQ(darks(), first);
  pf_map_free(map);
}

TEST(the_brush_size_steps_by_rung_and_stops_at_the_ends) {
  pf_map* map = blank();
  Editor ed(map);
  const int rungs = pf_brush_size_count();
  CHECK(rungs > 1);

  ed.brush_size = pf_brush_size(0);
  // Up the whole ladder, one rung a call.
  for (int i = 1; i < rungs; i++) {
    CHECK_EQ(ed.StepBrushSize(1), pf_brush_size(i));
    CHECK_EQ(ed.brush_size, pf_brush_size(i));
  }
  // And no further: rolling off the top must not wrap round to one tile.
  // -1 rather than 0 for "did not move", because 0 is the bottom rung itself.
  CHECK_EQ(ed.StepBrushSize(1), -1);
  CHECK_EQ(ed.brush_size, pf_brush_size(rungs - 1));

  for (int i = rungs - 2; i >= 0; i--) CHECK_EQ(ed.StepBrushSize(-1), pf_brush_size(i));
  CHECK_EQ(ed.StepBrushSize(-1), -1);
  CHECK_EQ(ed.brush_size, pf_brush_size(0));
  // The bottom of the ladder is the corner brush, and stepping onto it reports
  // its size rather than the "stayed put" answer.
  CHECK_EQ(pf_brush_size(0), PF_BRUSH_SIZE_CORNER);
  ed.brush_size = pf_brush_size(1);
  CHECK_EQ(ed.StepBrushSize(-1), PF_BRUSH_SIZE_CORNER);
  CHECK(ed.BrushIsCorner());

  // A size that is not on the ladder steps to the rung above the one below it,
  // rather than jumping to an end.
  ed.brush_size = pf_brush_size(rungs - 1) + 1;
  CHECK_EQ(ed.StepBrushSize(-1), pf_brush_size(rungs - 2));
  pf_map_free(map);
}

TEST(mixing_shades_follows_the_stroke_rather_than_its_bounding_box) {
  // Mixing belongs to the brush. A stroke drawn diagonally has a bounding box
  // several times its own area, and shading that box is how a stroke of dirt
  // came back having re-shaded the ground all around it — everything the
  // pointer had passed *near*, not what it had painted.
  pf_map* map = blank();
  Editor ed(map);
  ed.mix_shades = true;
  ed.brush_index = brush_for(PF_TERRAIN_COAST_LIGHT);
  ed.brush_size = 1;

  std::vector<uint16_t> before(64 * 64);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) before[size_t(y) * 64 + x] = uint16_t(pf_map_tile_at(map, x, y));
  }

  // A diagonal from (16,16) to (47,47): 32 tiles inside a 32x32 box.
  ed.BeginStroke();
  for (int i = 16; i < 48; i++) CHECK(ed.PaintAt(i, i));
  ed.EndStroke();

  int moved = 0, far = 0;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      if (before[size_t(y) * 64 + x] == uint16_t(pf_map_tile_at(map, x, y))) continue;
      moved++;
      // How far off the diagonal the change sits. Two tiles is the most the
      // corner model can carry: a shaded corner drags its neighbour one step
      // to meet it, and legalisation carries that one further.
      int nearest = 64;
      for (int i = 16; i < 48; i++) {
        nearest = std::min(nearest, std::max(std::abs(i - x), std::abs(i - y)));
      }
      if (nearest > 2) far++;
    }
  }
  CHECK(moved > 0);
  CHECK_EQ(far, 0);
  // And well short of the box it was drawn in, which is the whole point.
  CHECK(moved < 32 * 32 / 2);
  pf_map_free(map);
}

TEST(replace_terrain_acts_on_the_scope_and_costs_nothing_when_it_matches_nothing) {
  pf_map* map = blank();
  Editor ed(map);
  const int light = brush_for(PF_TERRAIN_GROUND_LIGHT);
  const int water = brush_for(PF_TERRAIN_WATER_LIGHT);
  CHECK(light >= 0 && water >= 0);

  auto count = [&](int terrain) {
    int n = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        if (pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, x, y))) == terrain) n++;
      }
    }
    return n;
  };

  // A blank forest map is all light ground, so replacing something it has
  // none of must not even take a checkpoint — pressing Replace on the wrong
  // terrain should not eat the step you were about to undo back to.
  const int before_ground = count(PF_TERRAIN_GROUND_LIGHT);
  CHECK(before_ground > 0);
  CHECK_EQ(ed.CountTerrain(PF_TERRAIN_WATER_LIGHT), 0);
  CHECK_EQ(ed.ReplaceTerrain(PF_TERRAIN_WATER_LIGHT, PF_TERRAIN_GROUND_LIGHT), 0);
  CHECK(!ed.CanUndo());

  // Scoped to the rectangle: outside it nothing moves.
  ed.SelectTerrain(10, 10, 8, 8);
  CHECK_EQ(ed.CountTerrain(PF_TERRAIN_GROUND_LIGHT), 64);
  const int replaced = ed.ReplaceTerrain(PF_TERRAIN_GROUND_LIGHT, PF_TERRAIN_WATER_LIGHT);
  CHECK_EQ(replaced, 64);
  CHECK(count(PF_TERRAIN_WATER_LIGHT) > 0);
  // The corner model refits the seam, so the water spreads a little past the
  // rectangle; what matters is that most of the map is untouched.
  CHECK(count(PF_TERRAIN_GROUND_LIGHT) > before_ground - 400);

  // One undo step for the whole thing.
  CHECK(ed.Undo());
  CHECK_EQ(count(PF_TERRAIN_GROUND_LIGHT), before_ground);
  pf_map_free(map);
}

TEST(decorate_is_deterministic_and_bounded_by_the_scope) {
  pf_map* map = blank();
  Editor ed(map);
  ed.SelectTerrain(20, 20, 16, 16);

  auto forest = [&] {
    int n = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        if (pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, x, y))) ==
            PF_TERRAIN_FOREST) {
          n++;
        }
      }
    }
    return n;
  };

  const int painted = ed.DecorateTerrain(PF_TERRAIN_FOREST, 0.25, 12345).changed;
  CHECK(painted > 0);
  const int after = forest();
  CHECK(after > 0);

  // Same seed, same scatter: a decoration you cannot reproduce is one you
  // cannot review.
  CHECK(ed.Undo());
  CHECK_EQ(forest(), 0);
  CHECK_EQ(ed.DecorateTerrain(PF_TERRAIN_FOREST, 0.25, 12345).changed, painted);
  CHECK_EQ(forest(), after);

  // A different seed gives a different scatter.
  CHECK(ed.Undo());
  const int other = ed.DecorateTerrain(PF_TERRAIN_FOREST, 0.25, 999).changed;
  CHECK(other > 0);

  // Density zero does nothing at all, and takes no undo step.
  CHECK(ed.Undo());
  const bool could_undo = ed.CanUndo();
  CHECK_EQ(ed.DecorateTerrain(PF_TERRAIN_FOREST, 0.0, 1).changed, 0);
  CHECK_EQ(ed.CanUndo(), could_undo);
  pf_map_free(map);
}

/**
 * Decorate scatters trees and rock, which nothing walks on, so it strands units
 * exactly as a brush stroke does — and until it swept up after itself, it left
 * them standing inside the trees it had just dropped on them.
 *
 * The same rule as a stroke, for the same reason: only what this scatter
 * stranded, never what it found already stranded.
 */
TEST(decorating_removes_the_units_it_strands) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;                        // Footman

  // One in the patch about to be scattered over, one well outside it.
  const int inside = ed.PlaceUnit(20, 20);
  CHECK(inside >= 0);
  CHECK(ed.PlaceUnit(60, 60) >= 0);
  CHECK_EQ(pf_map_unit_count(map), 2);

  // Every tile of the patch, so the footman inside it is certainly buried.
  ed.SelectTerrain(16, 16, 10, 10);
  const Editor::BulkResult result = ed.DecorateTerrain(PF_TERRAIN_FOREST, 1.0, 12345);
  CHECK(result.changed > 0);
  CHECK_EQ(result.removed, 1);
  CHECK_EQ(pf_map_unit_count(map), 1);

  // The one it removed is the one it buried, not merely one of them.
  pf_unit left{};
  CHECK_EQ(pf_map_unit(map, 0, &left), PF_OK);
  CHECK_EQ(int(left.x), 60);
  CHECK_EQ(int(left.y), 60);

  // And undo puts it back, so the removal is part of the scatter's own step
  // rather than something that outlives it.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 2);
  pf_map_free(map);
}

TEST(decorating_leaves_units_it_did_not_strand) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_type = 0;

  // Strand one deliberately with removal off, so the map holds a pre-existing
  // illegal unit the way a loaded map does — twin-isles ships with six.
  ed.keep_stranded_units = true;
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  ed.brush_index = brush_for(PF_TERRAIN_WATER_LIGHT);
  ed.brush_size = 9;
  ed.BeginStroke();
  ed.PaintAt(10, 10);
  ed.EndStroke();
  ed.keep_stranded_units = false;
  CHECK_EQ(pf_map_unit_count(map), 1);

  // A scatter in the far corner must not police it.
  ed.SelectTerrain(40, 40, 12, 12);
  const Editor::BulkResult result = ed.DecorateTerrain(PF_TERRAIN_FOREST, 1.0, 999);
  CHECK(result.changed > 0);
  CHECK_EQ(result.removed, 0);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_map_free(map);
}

TEST(keeping_stranded_units_holds_them_through_a_scatter) {
  // The option is one switch over every terrain edit, not one per tool.
  pf_map* map = blank();
  Editor ed(map);
  ed.keep_stranded_units = true;
  ed.placing_type = 0;
  CHECK(ed.PlaceUnit(20, 20) >= 0);

  ed.SelectTerrain(16, 16, 10, 10);
  const Editor::BulkResult result = ed.DecorateTerrain(PF_TERRAIN_FOREST, 1.0, 12345);
  CHECK(result.changed > 0);
  CHECK_EQ(result.removed, 0);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_map_free(map);
}

/**
 * The option that offers what an editor normally keeps back.
 *
 * The set is PUDDraft's "Unused/Special Units" submenu: five slots the game has
 * no unit for, the runtime leftovers, and the two campaign workers. Off it is a
 * palette anybody can hand to a beginner; on it is the escape hatch for people
 * who really do build maps with these.
 */
TEST(unused_units_are_offered_only_when_asked_for) {
  // Ordinary units are listed either way — the option adds, it never removes.
  for (int unit : {0x00, 0x02, 0x06, 0x5c}) {   // footman, peasant, knight, mine
    CHECK(Editor::ListsUnit(unit, false));
    CHECK(Editor::ListsUnit(unit, true));
  }

  // One of each reason the override gives for holding a unit back.
  const int kept_back[] = {
      0x22,   // a slot the game has no unit for; placing one crashes it
      0x69,   // a corpse, which only exists once the game is running
      0x10,   // Attack Peasant, a campaign stand-in for an ordinary worker
  };
  for (int unit : kept_back) {
    CHECK(!Editor::ListsUnit(unit, false));
    CHECK(Editor::ListsUnit(unit, true));
  }

  // The wall-as-unit ids are the exception the option does not reach. Walls are
  // terrain in this editor, so a second way to place one that does not auto-tile
  // would make walls the wall tool cannot fix.
  for (int unit : {0x67, 0x68}) {
    CHECK(!Editor::ListsUnit(unit, false));
    CHECK(!Editor::ListsUnit(unit, true));
  }

  // Exactly the opt-in set appears, less the two walls that never do. Counted
  // rather than spot-checked, so a unit added to the override without a thought
  // for this option shows up here.
  int off = 0, on = 0;
  for (int unit = 0; unit < PF_UNIT_COUNT; unit++) {
    if (Editor::ListsUnit(unit, false)) off++;
    if (Editor::ListsUnit(unit, true)) on++;
  }
  CHECK(off > 0);
  CHECK_EQ(on - off, pf_unit_needs_opt_in_count() - 2);
}

TEST(converting_a_unit_type_keeps_position_owner_and_value) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_owner = 2;
  ed.placing_type = 0x00;                 // Footman
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  CHECK(ed.PlaceUnit(14, 14) >= 0);
  ed.placing_type = 0x02;                 // Peasant, which must be left alone
  CHECK(ed.PlaceUnit(18, 18) >= 0);

  const Editor::BulkResult result = ed.ReplaceUnitType(0x00, 0x01, false);   // to Grunt
  CHECK_EQ(result.changed, 2);
  CHECK_EQ(result.skipped, 0);

  int grunts = 0, peasants = 0, footmen = 0;
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit u{};
    CHECK_EQ(pf_map_unit(map, i, &u), PF_OK);
    if (u.type == 0x01) { grunts++; CHECK_EQ(u.owner, 2); }
    if (u.type == 0x02) peasants++;
    if (u.type == 0x00) footmen++;
  }
  CHECK_EQ(grunts, 2);
  CHECK_EQ(peasants, 1);
  CHECK_EQ(footmen, 0);

  // One undo step for all of it, and converting nothing costs no step.
  CHECK(ed.Undo());
  CHECK_EQ(ed.ReplaceUnitType(0x63, 0x00, false).changed, 0);   // a type the map has none of
  pf_map_free(map);
}

TEST(switching_a_race_swaps_units_and_the_side_together) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_owner = 0;
  pf_map_set_race(map, 0, 0);             // human
  ed.placing_type = 0x00;                 // Footman
  CHECK(ed.PlaceUnit(10, 10) >= 0);
  ed.placing_type = 0x02;                 // Peasant
  CHECK(ed.PlaceUnit(14, 14) >= 0);

  const Editor::BulkResult result = ed.SwitchPlayerRace(0, 1);   // to orc
  CHECK_EQ(result.changed, 2);
  // SIDE follows the units: the two disagreeing is what makes a swapped base
  // unbuildable rather than merely odd.
  CHECK_EQ(pf_map_race(map, 0), 1);

  int orcs = 0;
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit u{};
    CHECK_EQ(pf_map_unit(map, i, &u), PF_OK);
    CHECK_EQ(pf_unit_race(u.type), 'o');
    orcs++;
  }
  CHECK_EQ(orcs, 2);

  // Idempotent: asking for the race it already is changes nothing and takes
  // no undo step.
  const bool could_undo = ed.CanUndo();
  CHECK_EQ(ed.SwitchPlayerRace(0, 1).changed, 0);
  CHECK_EQ(ed.CanUndo(), could_undo);

  // And back again, as one step.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_race(map, 0), 0);
  pf_map_free(map);
}

TEST(movement_overrides_are_counted_and_put_back) {
  pf_map* map = blank();
  Editor ed(map);

  // A freshly created map agrees with itself everywhere: MOVE is written by
  // the same code that writes the tiles.
  CHECK_EQ(ed.MovementOverrides(), 0);
  CHECK_EQ(ed.ResetMovement(), 0);
  CHECK(!ed.CanUndo());          // and a reset that does nothing costs nothing

  // Open a tile up by hand, the way a mapper makes a bridge walkable.
  const int was = pf_map_movement_at(map, 5, 5);
  CHECK_EQ(pf_map_set_movement(map, 5, 5, was ^ 0x000f), PF_OK);
  CHECK_EQ(ed.MovementOverrides(), 1);

  // Scoped like every other bulk edit: a rectangle that misses it sees none.
  ed.SelectTerrain(20, 20, 4, 4);
  CHECK_EQ(ed.MovementOverrides(), 0);
  ed.ClearTerrainSelection();
  CHECK_EQ(ed.MovementOverrides(), 1);

  CHECK_EQ(ed.ResetMovement(), 1);
  CHECK_EQ(ed.MovementOverrides(), 0);
  CHECK_EQ(pf_map_movement_at(map, 5, 5), was);

  // One undo step, and it brings the override back.
  CHECK(ed.Undo());
  CHECK_EQ(ed.MovementOverrides(), 1);
  pf_map_free(map);
}

TEST(bulk_edited_maps_stay_loadable) {
  // Every bulk edit goes through the same core calls painting does, so this
  // should hold by construction — which is exactly why it is worth asserting.
  // A map that stops parsing after a whole-map repaint is the kind of thing
  // nobody finds until they try to play it.
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_owner = 0;
  // Well clear of the rectangle the replace flooods: a unit standing in water
  // cannot become anything, so the race swap would skip it and the test would
  // be asserting the wrong thing.
  ed.placing_type = 0x00;                     // Footman
  CHECK(ed.PlaceUnit(50, 50) >= 0);
  ed.placing_type = 0x02;                     // Peasant
  CHECK(ed.PlaceUnit(55, 50) >= 0);

  ed.SelectTerrain(8, 8, 24, 24);
  CHECK(ed.ReplaceTerrain(PF_TERRAIN_GROUND_LIGHT, PF_TERRAIN_WATER_LIGHT) > 0);
  CHECK(ed.DecorateTerrain(PF_TERRAIN_FOREST, 0.1, 4242).changed >= 0);
  ed.ClearTerrainSelection();
  ed.ReplaceUnitType(0x00, 0x01, false);      // Footman to Grunt
  ed.SwitchPlayerRace(0, 1);
  ed.ResetMovement();

  size_t length = 0;
  pf_status status = PF_OK;
  uint8_t* bytes = pf_map_save(map, &length, &status);
  CHECK(bytes != nullptr);
  CHECK_EQ(status, PF_OK);

  pf_map* again = pf_map_open(bytes, length, &status);
  CHECK(again != nullptr);
  CHECK_EQ(status, PF_OK);
  CHECK_EQ(pf_map_width(again), 64);
  CHECK_EQ(pf_map_race(again, 0), 1);
  // Both units survived and both are orc now.
  CHECK_EQ(pf_map_unit_count(again), 2);
  for (int i = 0; i < pf_map_unit_count(again); i++) {
    pf_unit u{};
    CHECK_EQ(pf_map_unit(again, i, &u), PF_OK);
    CHECK_EQ(pf_unit_race(u.type), 'o');
  }
  pf_buffer_free(bytes);
  pf_map_free(again);
  pf_map_free(map);
}

TEST(the_unit_eyedropper_adopts_type_and_owner) {
  pf_map* map = blank();
  Editor ed(map);
  ed.placing_owner = 3;
  ed.placing_type = 0x00;                     // Footman
  CHECK(ed.PlaceUnit(10, 10) >= 0);

  // Something else selected, and pointing at the footman brings it back —
  // which is the whole point: finding a unit again in a palette of a hundred
  // and ten is slower than pointing at one.
  ed.placing_owner = 0;
  ed.placing_type = 0x02;                     // Peasant
  ed.SetTool(Tool::kSelect);

  const int index = ed.PickUnitType(10, 10);
  CHECK(index >= 0);
  CHECK_EQ(ed.placing_type, 0x00);
  CHECK_EQ(ed.placing_owner, 3);
  // Choosing what to place means placing it.
  CHECK(ed.tool() == Tool::kPlace);

  // Nothing under the pointer changes nothing at all.
  ed.placing_type = 0x02;
  CHECK_EQ(ed.PickUnitType(40, 40), -1);
  CHECK_EQ(ed.placing_type, 0x02);

  // A unit standing in one of the seven slots the game does not support — a
  // hand-edited map, since no corpus map has one — gives up its type but not
  // its owner. Adopting it would arm a player nothing in the client can show or
  // get back to, so the pick keeps the one that was already armed.
  CHECK(!pf_player_is_supported(9));
  CHECK(pf_map_add_unit(map, 20, 20, 0x00, 9, 0) >= 0);
  ed.placing_owner = 3;
  CHECK(ed.PickUnitType(20, 20) >= 0);
  CHECK_EQ(ed.placing_type, 0x00);
  CHECK_EQ(ed.placing_owner, 3);
  pf_map_free(map);
}

TEST(a_round_brush_lays_the_whole_disc_and_nothing_at_the_origin) {
  // A size-9 circle painted its top half and dropped the rest on tile (0,0).
  // pf_brush_points counts its `capacity` in ints, every caller here handed it
  // a point count, and past the halfway mark the bounds check stopped writing:
  // the tail of the buffer kept its zeroed value, which is not "no point" but
  // the tile at the map's corner.
  const int kSize = 9;
  std::vector<int> disc(size_t(kSize) * kSize * 2);
  const int points = pf_brush_points(30, 30, kSize, PF_BRUSH_CIRCLE, 1.0f,
                                     nullptr, disc.data(), int(disc.size()));
  CHECK_EQ(points, 69);                       // dx*dx + dy*dy <= 4*4 + 4
  auto in_disc = [&](int x, int y) {
    for (int i = 0; i < points; i++) {
      if (disc[size_t(i) * 2] == x && disc[size_t(i) * 2 + 1] == y) return true;
    }
    return false;
  };

  // The custom brush writes one tile value with no corner fitting around it,
  // so what it touches is exactly the brush footprint and nothing else.
  {
    pf_map* map = blank();
    Editor ed(map);
    ed.brush_index = pf_brush_count();
    ed.custom_tile = 0x0930;
    ed.brush_shape = PF_BRUSH_CIRCLE;
    ed.brush_size = kSize;
    CHECK(ed.PaintAt(30, 30));

    int laid = 0, missing = 0, stray = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        const bool painted = pf_map_tile_at(map, x, y) == 0x0930;
        if (painted) laid++;
        if (in_disc(x, y) && !painted) missing++;
        if (painted && !in_disc(x, y)) stray++;
      }
    }
    CHECK_EQ(laid, points);
    CHECK_EQ(missing, 0);                     // the lower half was the half lost
    CHECK_EQ(stray, 0);                       // and (0,0) was where it went
    pf_map_free(map);
  }

  // A terrain brush fits corners, so it moves tiles just outside its footprint
  // too. What it must not do is move one thirty tiles away.
  {
    pf_map* map = blank();
    Editor ed(map);
    ed.brush_index = brush_for(PF_TERRAIN_WATER_DARK);
    ed.brush_shape = PF_BRUSH_CIRCLE;
    ed.brush_size = kSize;

    std::vector<uint16_t> before(64 * 64);
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        before[size_t(y) * 64 + x] = uint16_t(pf_map_tile_at(map, x, y));
      }
    }

    ed.BeginStroke();
    CHECK(ed.PaintAt(30, 30));
    ed.EndStroke();

    int far = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        if (before[size_t(y) * 64 + x] == uint16_t(pf_map_tile_at(map, x, y))) continue;
        // Chebyshev distance from the disc: fitting reaches a tile or two past
        // it, nothing reaches the corner of the map.
        if (std::max(std::abs(x - 30), std::abs(y - 30)) > kSize / 2 + 2) far++;
      }
    }
    CHECK_EQ(far, 0);
    // And the bottom of the disc is water, not the ground it started as: the
    // truncated buffer left everything below the centre row unpainted.
    CHECK_EQ(pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 30, 33))),
             int(PF_TERRAIN_WATER_DARK));
    pf_map_free(map);
  }
}

TEST(a_scatter_brush_stays_inside_its_own_footprint) {
  // Scatter had the same corner tile for a second reason: the buffer was sized
  // by a dry run, and that run advances the seed, so the call that filled it
  // drew a different sample. Whenever the second sample was the smaller of the
  // two, the pairs left over from the sizing read as (0,0).
  pf_map* map = blank();
  Editor ed(map);
  ed.brush_index = pf_brush_count();
  ed.custom_tile = 0x0930;
  ed.brush_shape = PF_BRUSH_SCATTER;
  ed.brush_size = 9;

  // Twenty applications, because a single sample is as likely as not to be the
  // larger one and leave nothing behind.
  const int ground = pf_map_tile_at(map, 0, 0);
  int painted = 0;
  for (int i = 0; i < 20; i++) {
    ed.PaintAt(30, 30);
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        if (pf_map_tile_at(map, x, y) != 0x0930) continue;
        painted++;
        CHECK(std::max(std::abs(x - 30), std::abs(y - 30)) <= 4);
        pf_map_set_tile(map, x, y, ground);   // cleared, so each pass is its own
      }
    }
  }
  CHECK(painted > 0);
  pf_map_free(map);
}

/**
 * Every editor preference survives a restart.
 *
 * Written after four of them did not: the three placement escape hatches and
 * "mark special units" were read out of the Options dialog and never written
 * anywhere, so every session opened with the rules back on. They were missing
 * from the window's settings code, which no test can drive — hence the table,
 * which one can.
 */
TEST(saved_options_round_trip) {
  // Spelled out, in order. An option added to SavedOptions and not to this
  // list is a deliberate addition and this line is where it is admitted to;
  // an option added to the editor and never listed there is the bug above,
  // and the only defence against it is that this list has to be looked at.
  const char* const expected[] = {
      "Grid", "BrushSize", "BrushShape", "MixShades", "PaintDark", "Variation",
      "FitEdges", "FitPasted", "KeepStranded",
      "AllowIllegal", "AllowStacked", "AllowEdge", "MarkSpecial",
      "ShowAllRaces", "OfferUnusedUnits",
  };
  const std::vector<Editor::Option>& options = Editor::SavedOptions();
  CHECK_EQ(int(options.size()), int(sizeof(expected) / sizeof(expected[0])));
  std::set<std::string> names;
  for (size_t i = 0; i < options.size() && i < sizeof(expected) / sizeof(expected[0]); i++) {
    CHECK(std::string(options[i].name) == expected[i]);
    // Two options under one key would silently overwrite each other.
    CHECK(names.insert(options[i].name).second);
  }

  pf_map* map = blank();
  Editor before(map);

  // The fallback in the table has to be what a fresh editor already is, or a
  // first run and a run after a reset disagree about the defaults.
  for (const Editor::Option& option : options) {
    CHECK_EQ(option.get(before), option.fallback);
  }

  // Move every one of them off its default. Which value does not matter, only
  // that `get` reports something other than the fallback afterwards — a bool
  // takes 0 or 1 and a policy takes the next value along.
  std::map<std::string, int> stored;
  for (const Editor::Option& option : options) {
    for (int candidate : {option.fallback + 1, option.fallback - 1}) {
      option.set(before, candidate);
      if (option.get(before) != option.fallback) break;
    }
    CHECK(option.get(before) != option.fallback);
    stored[option.name] = option.get(before);
  }

  // A second editor, loaded from the store the way a new session is.
  Editor after(map);
  for (const Editor::Option& option : options) {
    option.set(after, stored[option.name]);
  }
  for (const Editor::Option& option : options) {
    CHECK_EQ(option.get(after), stored[option.name]);
  }

  // The three placement hatches are the map's, not the editor's, so restoring
  // them has to have reached the core as well.
  CHECK_EQ(pf_map_allows_illegal_placement(map), 1);
  CHECK_EQ(pf_map_allows_stacked_units(map), 1);
  CHECK_EQ(pf_map_allows_edge_placement(map), 1);
  pf_map_free(map);
}

/**
 * What a freshly opened map is asked about follows the options.
 *
 * The client offers to delete the units the game could not place, which is
 * only sensible for the rules the person is actually keeping: somebody who has
 * turned stacking on is not to be asked whether to delete the units they
 * stacked on purpose. Off the map is asked about either way — a unit outside
 * its own map is broken however the options are set.
 */
TEST(misplacement_checks_follow_the_placement_options) {
  pf_map* map = blank();
  Editor ed(map);

  CHECK_EQ(ed.MisplacementChecks(),
           PF_MISPLACED_OFF_MAP | PF_MISPLACED_TERRAIN | PF_MISPLACED_OVERLAP);
  ed.SetAllowStackedUnits(true);
  CHECK_EQ(ed.MisplacementChecks(), PF_MISPLACED_OFF_MAP | PF_MISPLACED_TERRAIN);
  ed.SetAllowIllegalPlacement(true);
  CHECK_EQ(ed.MisplacementChecks(), PF_MISPLACED_OFF_MAP);
  ed.SetAllowStackedUnits(false);
  ed.SetAllowIllegalPlacement(false);

  // A map arriving with two units on one tile and a footman in the sea, the
  // way one written by another editor does.
  for (int y = 20; y < 30; y++) {
    for (int x = 20; x < 30; x++) {
      pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }
  pf_map_set_allow_illegal_placement(map, 1);
  pf_map_add_unit(map, 5, 5, 0x00, 0, 0);
  pf_map_add_unit(map, 5, 5, 0x00, 0, 0);
  pf_map_add_unit(map, 25, 25, 0x00, 0, 0);
  pf_map_set_allow_illegal_placement(map, 0);
  CHECK_EQ(pf_map_unit_count(map), 3);
  CHECK_EQ(ed.MisplacedUnitCount(), 2);

  // Selecting first, because the indices it holds do not survive a removal.
  CHECK_EQ(ed.SelectAt(5, 5, false), 1);
  CHECK(ed.HasSelection());

  CHECK_EQ(ed.RemoveMisplacedUnits(ed.MisplacementChecks()), 2);
  CHECK_EQ(pf_map_unit_count(map), 1);
  CHECK(!ed.HasSelection());
  CHECK(ed.Dirty());

  // One undo step, and it puts all of them back.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_unit_count(map), 3);

  // Nothing to do is not an undo step: with stacking and terrain allowed there
  // is nothing left to find on this map.
  ed.SetAllowStackedUnits(true);
  ed.SetAllowIllegalPlacement(true);
  CHECK_EQ(ed.MisplacedUnitCount(), 0);
  CHECK_EQ(ed.RemoveMisplacedUnits(ed.MisplacementChecks()), 0);
  CHECK_EQ(pf_map_unit_count(map), 3);
  pf_map_free(map);
}

/**
 * Converting only the selected units.
 *
 * The dialog's scope switch. Off is the whole map, which is what the tool is
 * for; on is the escape hatch for turning three of a dozen footmen into grunts
 * without picking them off one at a time.
 */
TEST(converting_can_be_narrowed_to_the_selection) {
  pf_map* map = blank();
  Editor ed(map);
  for (int i = 0; i < 4; i++) CHECK(pf_map_add_unit(map, 4 + i * 2, 4, 0x00, 0, 0) >= 0);
  CHECK_EQ(ed.CountUnitsOfType(0x00, false), 4);
  CHECK_EQ(ed.CountUnitsOfType(0x00, true), 0);   // nothing selected yet

  CHECK_EQ(ed.SelectAt(4, 4, false), 0);   // SelectAt answers with the index
  CHECK_EQ(ed.SelectAt(6, 4, true), 1);
  CHECK_EQ(int(ed.selected().size()), 2);
  CHECK_EQ(ed.CountUnitsOfType(0x00, true), 2);
  // The count the dialog shows and the conversion it then runs are the same
  // question asked twice, so they are asked of the same function.
  CHECK_EQ(ed.ReplaceUnitType(0x00, 0x01, true).changed, 2);
  CHECK_EQ(ed.CountUnitsOfType(0x00, false), 2);
  CHECK_EQ(ed.CountUnitsOfType(0x01, false), 2);

  // And with the switch off it takes what is left, selection or no selection.
  CHECK_EQ(ed.ReplaceUnitType(0x00, 0x01, false).changed, 2);
  CHECK_EQ(ed.CountUnitsOfType(0x00, false), 0);
  CHECK_EQ(ed.CountUnitsOfType(0x01, false), 4);

  // On with an empty selection converts nothing rather than everything, which
  // is why the dialog greys the box out instead of leaving it to mean "all".
  ed.ClearSelection();
  CHECK_EQ(ed.ReplaceUnitType(0x01, 0x00, true).changed, 0);
  CHECK_EQ(ed.CountUnitsOfType(0x01, false), 4);
  pf_map_free(map);
}

/**
 * Two ships cannot be put on top of each other.
 *
 * The whole path the canvas uses — PlaceOrigin to turn the pointer into an
 * origin, then PlaceUnit — rather than the core call underneath it, because
 * that is where a report of stacked ships comes from.
 *
 * Two rules meet here. A ship covers 2x2, and it goes on a 2x2 grid, so every
 * tile of a block points at the same origin and the blocks tile the map
 * without touching. Pointing anywhere inside one that is taken is refused;
 * there is no offset left over to overlap by.
 */
TEST(a_ship_cannot_be_placed_on_another_ship) {
  pf_map* map = blank();
  for (int y = 10; y < 40; y++) {
    for (int x = 10; x < 40; x++) {
      pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }
  Editor ed(map);
  ed.SetMode(pfwin::Mode::kUnit);
  ed.SetTool(Tool::kPlace);
  ed.placing_type = 0x1e;   // Elven Destroyer
  ed.placing_owner = 0;
  CHECK_EQ(pf_map_allows_stacked_units(map), 0);

  auto click = [&](int tx, int ty) {
    int ox = 0, oy = 0;
    ed.PlaceOrigin(tx, ty, ed.placing_type, ox, oy);
    return ed.PlaceUnit(ox, oy);
  };
  // A predicate, not a pair: a comma inside CHECK is two macro arguments.
  auto sits_at = [&](int index, int x, int y) {
    pf_unit u{};
    if (pf_map_unit(map, index, &u) != PF_OK) return false;
    return int(u.x) == x && int(u.y) == y;
  };

  // Pointing at an odd tile lands on the even corner of its block.
  CHECK(click(21, 21) >= 0);
  CHECK(sits_at(0, 20, 20));

  // Every tile of that block is the same block, and it is taken.
  for (int dy = 0; dy <= 1; dy++) {
    for (int dx = 0; dx <= 1; dx++) {
      CHECK_EQ(click(20 + dx, 20 + dy), -1);
      CHECK(ed.last_refusal.find("already there") != std::string::npos);
    }
  }
  CHECK_EQ(pf_map_unit_count(map), 1);

  // The neighbouring blocks are free, and land two tiles away.
  CHECK(click(23, 21) >= 0);
  CHECK(sits_at(1, 22, 20));
  CHECK(click(21, 23) >= 0);
  CHECK(sits_at(2, 20, 22));
  CHECK_EQ(pf_map_unit_count(map), 3);

  // A unit put on an odd tile by something other than the pointer — a paste, a
  // map from another editor — is still caught by the overlap rule.
  pf_map_set_allow_illegal_placement(map, 1);
  CHECK(pf_map_add_unit(map, 25, 25, 0x1e, 0, 0) >= 0);
  pf_map_set_allow_illegal_placement(map, 0);
  CHECK_EQ(click(26, 26), -1);
  CHECK(ed.last_refusal.find("already there") != std::string::npos);

  // And with the escape hatch on it is the person's business, as with every
  // other placement rule.
  ed.SetAllowStackedUnits(true);
  CHECK(click(26, 26) >= 0);
  pf_map_free(map);
}

/**
 * A ship cannot reach an odd tile by any route.
 *
 * Snapping the pointer is not enough on its own: a paste arrives at whatever
 * offset it is dropped at, and a dragged selection carries its own delta. Both
 * ask the core, so the rule lives there and all three paths obey it.
 */
TEST(nothing_can_move_a_ship_onto_an_odd_tile) {
  pf_map* map = blank();
  for (int y = 4; y < 50; y++) {
    for (int x = 4; x < 50; x++) {
      pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }
  Editor ed(map);
  ed.SetMode(pfwin::Mode::kUnit);
  ed.placing_type = 0x1e;
  CHECK(pf_map_add_unit(map, 20, 20, 0x1e, 0, 0) >= 0);

  // Moving. One tile is refused whichever way it goes; two is fine.
  CHECK_EQ(ed.SelectAt(20, 20, false), 0);
  for (int i = 0; i < 4; i++) {
    const int dx = (i == 0) - (i == 1), dy = (i == 2) - (i == 3);
    CHECK(!ed.MoveSelectionBy(dx, dy, true));
    CHECK(ed.last_refusal.find("odd tile") != std::string::npos);
  }
  CHECK(ed.MoveSelectionBy(2, 0, true));
  pf_unit u{};
  CHECK_EQ(pf_map_unit(map, 0, &u), PF_OK);
  CHECK_EQ(int(u.x), 22);
  CHECK_EQ(int(u.y), 20);

  // Pasting. A fragment holding a ship lands its units only where the grid
  // allows, so an odd drop leaves the ship behind rather than misplacing it.
  ed.ClearSelection();
  ed.SelectAt(22, 20, false);
  CHECK(ed.Copy() > 0);
  const int before = pf_map_unit_count(map);
  ed.PasteAt(31, 31);
  CHECK_EQ(pf_map_unit_count(map), before);   // odd offset: nothing landed
  ed.PasteAt(30, 30);
  CHECK_EQ(pf_map_unit_count(map), before + 1);
  CHECK_EQ(pf_map_unit(map, before, &u), PF_OK);
  CHECK_EQ(int(u.x) % 2, 0);
  CHECK_EQ(int(u.y) % 2, 0);
  pf_map_free(map);
}

/**
 * Painting where things may walk, over whatever is drawn there.
 *
 * The movement layer is very nearly a function of the tile, and painting
 * terrain keeps it that way. This is the tool for the handful of tiles that
 * are meant to disagree — a bridge made walkable, a shallow closed off — so it
 * writes the layer and leaves the artwork alone.
 */
TEST(the_movement_brush_paints_over_what_is_drawn) {
  pf_map* map = blank();
  Editor ed(map);
  ed.SetMode(pfwin::Mode::kMovement);
  CHECK(ed.tool() == Tool::kWalkable);
  CHECK(ed.mode() == pfwin::Mode::kMovement);
  ed.brush_size = 1;
  ed.brush_shape = PF_BRUSH_SQUARE;

  const int tile = pf_map_tile_at(map, 5, 5);
  const int implied = pf_tile_movement(tile);
  CHECK_EQ(pf_map_movement_at(map, 5, 5), implied);

  // Lay open water over ground. The tile is untouched; only the layer moves.
  ed.movement_value = 0x0040;
  CHECK_EQ(ed.MovementBrushValue(), 0x0040);
  // The palette lights the class whose value this is.
  CHECK(ed.MovementClassIndex() >= 0);
  CHECK_EQ(pf_movement_class_value(ed.MovementClassIndex()), 0x0040);

  ed.BeginStroke();
  CHECK_EQ(ed.PaintMovementAt(5, 5), 1);
  // A drag back over a tile it already painted is not another edit.
  CHECK_EQ(ed.PaintMovementAt(5, 5), 0);
  ed.EndStroke();
  CHECK_EQ(pf_map_movement_at(map, 5, 5), 0x0040);
  CHECK_EQ(pf_map_tile_at(map, 5, 5), tile);   // the artwork is as it was
  CHECK_EQ(ed.MovementOverrides(), 1);

  // One undo step puts the stroke back.
  CHECK(ed.Undo());
  CHECK_EQ(pf_map_movement_at(map, 5, 5), implied);
  CHECK(ed.Redo());
  CHECK_EQ(pf_map_movement_at(map, 5, 5), 0x0040);

  // The palette's "put it back" entry takes whatever the tile implies, which
  // is how one override comes off without resetting the whole map.
  ed.movement_from_terrain = true;
  CHECK_EQ(ed.MovementBrushValue(), -1);
  ed.BeginStroke();
  CHECK_EQ(ed.PaintMovementAt(5, 5), 1);
  ed.EndStroke();
  CHECK_EQ(pf_map_movement_at(map, 5, 5), implied);
  CHECK_EQ(ed.MovementOverrides(), 0);
  ed.movement_from_terrain = false;

  // Water no flier may cross: a combination the palette offers and no map in
  // existence holds.
  ed.movement_value = 0x0040 | int(pf_movement_no_flying_bit());
  CHECK_EQ(ed.MovementBrushValue(), 0x0240);
  CHECK(ed.MovementClassIndex() >= 0);
  ed.BeginStroke();
  CHECK_EQ(ed.PaintMovementAt(7, 7), 1);
  ed.EndStroke();
  CHECK_EQ(pf_map_movement_at(map, 7, 7), 0x0240);

  // And a value the palette does not offer is still a value: the brush paints
  // the word it is given, and the palette lights no cell for it.
  ed.movement_value = 0x0082 | int(pf_movement_no_flying_bit());
  CHECK_EQ(ed.MovementClassIndex(), -1);
  ed.BeginStroke();
  CHECK_EQ(ed.PaintMovementAt(13, 13), 1);
  ed.EndStroke();
  CHECK_EQ(pf_map_movement_at(map, 13, 13), 0x0282);

  // A bigger brush lays the value over its whole footprint.
  ed.movement_value = 0x0040;
  ed.brush_size = 3;
  ed.BeginStroke();
  CHECK_EQ(ed.PaintMovementAt(10, 10), 9);
  ed.EndStroke();
  // Nine from this stroke, plus the two odd values painted above; the tile that
  // was put back is not an override any more and is not counted.
  CHECK_EQ(ed.MovementOverrides(), 11);
  pf_map_free(map);
}

/**
 * A terrain edit does not take hand-painted movement away.
 *
 * Nothing in the format records which tiles somebody meant, and nothing needs
 * to: a value that disagrees with the tile under it is the answer. That also
 * makes it survive a save and a reload, which is the half that was asked for.
 */
TEST(painting_terrain_leaves_authored_movement_alone) {
  pf_map* map = blank();
  Editor ed(map);
  ed.SetMode(pfwin::Mode::kMovement);
  ed.brush_size = 1;
  // Two tiles: one authored, one left as the terrain made it.
  ed.movement_value = 0x0040;
  CHECK_EQ(ed.MovementBrushValue(), 0x0040);
  ed.BeginStroke();
  CHECK_EQ(ed.PaintMovementAt(8, 8), 1);
  ed.EndStroke();
  const int plain_before = pf_map_movement_at(map, 20, 20);

  // Paint forest across both of them.
  ed.SetMode(pfwin::Mode::kTerrain);
  ed.brush_size = 5;
  int forest = -1;
  for (int i = 0; i < pf_brush_count(); i++) {
    if (pf_brush_terrain(i) == PF_TERRAIN_FOREST) forest = i;
  }
  CHECK(forest >= 0);
  ed.brush_index = forest;
  ed.BeginStroke();
  CHECK(ed.PaintAt(8, 8));
  CHECK(ed.PaintAt(20, 20));
  ed.EndStroke();

  // The authored tile keeps what a person put there; its neighbour follows the
  // terrain, which is what makes the layer right without anyone editing it.
  CHECK_EQ(pf_map_movement_at(map, 8, 8), 0x0040);
  CHECK(pf_map_movement_at(map, 20, 20) != plain_before);
  CHECK_EQ(pf_map_movement_at(map, 20, 20),
           pf_tile_movement(pf_map_tile_at(map, 20, 20)));
  pf_map_free(map);
}

/**
 * The brush ladder works in movement mode, minus the rung it has no use for.
 *
 * Alt+wheel and the two size keys were terrain-only, which left the movement
 * brush at whatever size it happened to be. It is the same brush, so it takes
 * the same shortcuts — except the bottom rung, which marks a corner rather
 * than a tile where this layer holds one value per tile.
 */
TEST(the_brush_ladder_skips_the_corner_rung_in_movement_mode) {
  pf_map* map = blank();
  Editor ed(map);

  // Terrain mode reaches the corner rung, which is what it is for.
  ed.SetMode(pfwin::Mode::kTerrain);
  ed.brush_size = pf_brush_size(1);
  CHECK_EQ(ed.StepBrushSize(-1), PF_BRUSH_SIZE_CORNER);
  CHECK(ed.BrushIsCorner());

  // Movement mode stops a rung above it, and never paints half a tile.
  ed.SetMode(pfwin::Mode::kMovement);
  ed.brush_size = pf_brush_size(1);
  CHECK_EQ(ed.StepBrushSize(-1), -1);        // already as small as it goes
  CHECK_EQ(ed.brush_size, pf_brush_size(1));
  CHECK(!ed.BrushIsCorner());

  // And it climbs the same ladder as terrain does.
  const int up = ed.StepBrushSize(1);
  CHECK(up > pf_brush_size(1));
  CHECK_EQ(ed.brush_size, up);
  CHECK_EQ(ed.MovementBrushSize(), up);

  // A size left on the corner rung by terrain mode still paints a whole tile
  // rather than nothing, whichever way it was reached.
  ed.brush_size = PF_BRUSH_SIZE_CORNER;
  CHECK_EQ(ed.MovementBrushSize(), 1);
  pf_map_free(map);
}
