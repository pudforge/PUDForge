// Terrain: tile decoding, auto-tiling, walls, regions, variations
//
// See harness.hpp for the assertions, fixtures and registration.

#include "harness.hpp"
#include "../PUDForgeCore/noise.hpp"
#include "../PUDForgeCore/view.hpp"

TEST_GROUP("terrain")

namespace pft {

TEST(tile_solid_decoding) {
  uint8_t q[4];
  pf::decode_tile(0x0050, q);
  for (int i = 0; i < 4; i++) CHECK_EQ(int(q[i]), int(pf::kGroundLight));
  pf::decode_tile(0x0012, q);
  for (int i = 0; i < 4; i++) CHECK_EQ(int(q[i]), int(pf::kWaterLight));
  pf::decode_tile(0x0070, q);
  for (int i = 0; i < 4; i++) CHECK_EQ(int(q[i]), int(pf::kForest));
}
TEST(tile_boundary_shapes_are_complements) {
  // 07.. is the forest/grass transition. Shape 0 fills the upper-left corner;
  // shape D is the same corner "clear", so it must be the exact inverse.
  uint8_t filled[4], clear[4];
  pf::decode_tile(0x0700, filled);
  pf::decode_tile(0x07d0, clear);
  CHECK_EQ(int(filled[0]), int(pf::kForest));
  CHECK_EQ(int(filled[1]), int(pf::kGroundLight));
  CHECK_EQ(int(clear[0]), int(pf::kGroundLight));
  CHECK_EQ(int(clear[1]), int(pf::kForest));

  const int pairs[][2] = {{0x0, 0xd}, {0x1, 0xc}, {0x2, 0xb}, {0x3, 0xa},
                          {0x4, 0x9}, {0x7, 0x6}, {0x8, 0x5}};
  for (const auto& p : pairs) {
    uint8_t a[4], b[4];
    pf::decode_tile(uint16_t(0x0700 | (p[0] << 4)), a);
    pf::decode_tile(uint16_t(0x0700 | (p[1] << 4)), b);
    for (int i = 0; i < 4; i++) CHECK(a[i] != b[i]);
  }
}
TEST(terrain_graph) {
  CHECK(pf::terrain_compatible(pf::kWaterLight, pf::kCoastLight));
  CHECK(pf::terrain_compatible(pf::kCoastLight, pf::kGroundLight));
  CHECK(pf::terrain_compatible(pf::kGroundLight, pf::kForest));
  CHECK(pf::terrain_compatible(pf::kCoastLight, pf::kMountain));
  // Pairs the format has no tile for.
  CHECK(!pf::terrain_compatible(pf::kWaterLight, pf::kGroundLight));
  CHECK(!pf::terrain_compatible(pf::kGroundLight, pf::kMountain));
  // Walls are not part of the terrain graph.
  CHECK(!pf::terrain_compatible(pf::kWallHuman, pf::kGroundLight));

  CHECK_EQ(int(pf::step_toward(pf::kForest, pf::kWaterLight)), int(pf::kGroundLight));
  CHECK_EQ(int(pf::step_toward(pf::kGroundLight, pf::kWaterLight)), int(pf::kCoastLight));
}
TEST(tile_index_round_trips) {
  pf::TileIndex index;
  CHECK(index.combination_count() > 100);

  // Every solid terrain must resolve.
  const uint8_t solids[] = {pf::kWaterLight, pf::kCoastLight, pf::kGroundLight,
                            pf::kForest, pf::kMountain};
  for (uint8_t t : solids) {
    uint8_t c[4] = {t, t, t, t};
    int tile = index.lookup(c, 1234);
    CHECK(tile >= 0);
    uint8_t q[4];
    pf::decode_tile(uint16_t(tile), q);
    for (int i = 0; i < 4; i++) CHECK_EQ(int(q[i]), int(t));
  }

  // An impossible combination has no tile.
  uint8_t bad[4] = {pf::kWaterDark, pf::kForest, pf::kMountain, pf::kWaterLight};
  CHECK_EQ(index.lookup(bad, 0), -1);

  // Lookup is deterministic for a given salt.
  uint8_t g[4] = {pf::kGroundLight, pf::kGroundLight, pf::kGroundLight, pf::kGroundLight};
  CHECK_EQ(index.lookup(g, 42), index.lookup(g, 42));
}
TEST(paint_auto_builds_a_shoreline) {
  pf::Status s;
  pf::Map* map = pf::Map::create(24, 24, 0, s);
  if (!map) { CHECK(false); return; }
  pf::CornerGrid grid = pf::CornerGrid::from_map(*map);
  pf::TileIndex index;

  pf::paint_auto(*map, grid, index, 12, 12, pf::kWaterLight, 5);

  bool water = false, coast = false, ground = false;
  for (uint16_t tile : map->tiles()) {
    uint8_t q[4];
    pf::decode_tile(tile, q);
    for (int i = 0; i < 4; i++) {
      if (q[i] == pf::kWaterLight) water = true;
      if (q[i] == pf::kCoastLight) coast = true;
      if (q[i] == pf::kGroundLight) ground = true;
    }
  }
  CHECK(water);
  CHECK(coast);   // inserted automatically: water may not touch grass
  CHECK(ground);

  // Every resulting tile must be expressible.
  for (uint16_t tile : map->tiles()) {
    uint8_t q[4];
    pf::decode_tile(tile, q);
    uint8_t a = q[0], b = q[0];
    for (int i = 1; i < 4; i++) if (q[i] != a) b = q[i];
    CHECK(pf::terrain_compatible(a, b));
  }
  delete map;
}
TEST(paint_auto_is_idempotent) {
  pf::Status s;
  pf::Map* map = pf::Map::create(16, 16, 0, s);
  if (!map) { CHECK(false); return; }
  pf::CornerGrid grid = pf::CornerGrid::from_map(*map);
  pf::TileIndex index;
  pf::paint_auto(*map, grid, index, 8, 8, pf::kWaterLight, 3);
  std::vector<uint16_t> once = map->tiles();
  pf::paint_auto(*map, grid, index, 8, 8, pf::kWaterLight, 3);
  CHECK(map->tiles() == once);
  delete map;
}
TEST(wall_tiles_follow_neighbours) {
  // Every value here is what the shipped maps use for that arrangement; see
  // the table in wall_tile.
  pf::CornerGrid grid(8, 8);

  // Standing alone and walled in on all four sides are different tiles. Using
  // one for both is what made a painted wall look like a row of posts.
  grid.set_wall(1, 1, 1);
  CHECK_EQ(int(pf::wall_tile(grid, 1, 1, 1)), 0x0090);
  for (int i = 0; i < 4; i++) {
    const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    grid.set_wall(1 + dx[i], 1 + dy[i], 1);
  }
  CHECK_EQ(int(pf::wall_tile(grid, 1, 1, 1)), 0x00b0);
  for (int y = 0; y <= 2; y++) for (int x = 0; x <= 2; x++) grid.set_wall(x, y, 0);

  // A horizontal run: opens east, runs through, opens west.
  for (int x = 1; x <= 3; x++) grid.set_wall(x, 3, 1);
  CHECK_EQ(int(pf::wall_tile(grid, 1, 3, 1)), 0x0810);
  CHECK_EQ(int(pf::wall_tile(grid, 2, 3, 1)), 0x0890);
  CHECK_EQ(int(pf::wall_tile(grid, 3, 3, 1)), 0x0870);

  // And a vertical one.
  for (int y = 1; y <= 3; y++) grid.set_wall(6, y, 1);
  CHECK_EQ(int(pf::wall_tile(grid, 6, 1, 1)), 0x0800);
  CHECK_EQ(int(pf::wall_tile(grid, 6, 2, 1)), 0x0840);
  CHECK_EQ(int(pf::wall_tile(grid, 6, 3, 1)), 0x0830);

  // Turning a corner changes the tile before it too: what was an end is now a
  // through piece. This is why painting works a margin wider than the brush.
  grid.set_wall(4, 3, 1);
  grid.set_wall(4, 4, 1);
  CHECK_EQ(int(pf::wall_tile(grid, 4, 3, 1)), 0x0880);   // south and west
  CHECK_EQ(int(pf::wall_tile(grid, 3, 3, 1)), 0x0890);   // no longer an end

  // Orc walls use the same shapes in their own class.
  grid.set_wall(1, 6, 2);
  CHECK_EQ(int(pf::wall_tile(grid, 1, 6, 2)), 0x00a0);
  grid.set_wall(2, 6, 2);
  CHECK_EQ(int(pf::wall_tile(grid, 1, 6, 2)), 0x0910);
  // A human wall is no neighbour to an orc one.
  grid.set_wall(3, 6, 1);
  CHECK_EQ(int(pf::wall_tile(grid, 2, 6, 2)), 0x0970);
}
TEST(regions_label_components) {
  pf::Status s;
  pf::Map* map = pf::Map::create(16, 16, 0, s);
  if (!map) { CHECK(false); return; }
  // A forest wall splits the map into two land regions.
  for (int y = 0; y < 16; y++) {
    map->tiles()[size_t(y) * 16 + 8] = uint16_t(pf::solid_tile(pf::kForest, 0));
  }
  pf::rebuild_regions(*map);
  CHECK_EQ(int(map->regions()[8]), 0xfffe);          // forest sentinel
  CHECK(map->regions()[0] != map->regions()[15]);    // the halves differ
  CHECK_EQ(int(map->regions()[0]), int(map->regions()[16]));  // same side shares
  delete map;
}
TEST(regions_use_eight_connectivity) {
  pf::Status s;
  pf::Map* map = pf::Map::create(8, 8, 0, s);
  if (!map) { CHECK(false); return; }
  for (auto& t : map->tiles()) t = uint16_t(pf::solid_tile(pf::kForest, 0));
  map->tiles()[1 * 8 + 1] = uint16_t(pf::solid_tile(pf::kGroundLight, 0));
  map->tiles()[2 * 8 + 2] = uint16_t(pf::solid_tile(pf::kGroundLight, 0));
  int count = pf::rebuild_regions(*map);
  CHECK_EQ(count, 1);  // diagonal contact joins them
  CHECK_EQ(int(map->regions()[1 * 8 + 1]), int(map->regions()[2 * 8 + 2]));
  delete map;
}
TEST(generated_maps_look_like_maps) {
  pf_noise_layer layers[3] = {
      {0.045f, 12345u, 1.0f},
      {0.11f, 999u, 0.45f},
      {0.30f, 4242u, 0.15f},
  };
  pf_generate_params params{};
  params.width = 96;
  params.height = 96;
  params.tileset = 0;
  // Water and coast are shares of the map; forest and rock are shares of the
  // land, so they hold however much land there is.
  params.water = 0.255f;
  params.coast = 0.109f;
  params.forest = 0.35f;
  params.rock = 0.14f;
  params.detail_seed = 77u;
  params.detail_scale = 0.10f;
  params.clearings = 4;
  params.clearing_radius = 8;

  pf_status st = PF_OK;
  pf_map* map = pf_map_generate(&params, layers, 3, &st);
  CHECK(map != nullptr);
  CHECK_EQ(int(st), int(PF_OK));
  if (!map) return;

  // Every tile has to be a real tile. A generator that emits corner terrains
  // the format cannot express would show up here and nowhere else.
  int unknown = 0;
  std::map<int, int> share;
  for (int y = 0; y < 96; y++) {
    for (int x = 0; x < 96; x++) {
      uint8_t q[4];
      pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), q);
      for (int i = 0; i < 4; i++) {
        if (q[i] == pf::kTerrainUnknown) unknown++;
        share[q[i]]++;
      }
    }
  }
  CHECK_EQ(unknown, 0);

  // And it has to have made all of it, not a flat field with one thing on it.
  const int total = 96 * 96 * 4;
  CHECK(share[pf::kWaterDark] + share[pf::kWaterLight] > total / 20);
  CHECK(share[pf::kForest] > total / 20);
  CHECK(share[pf::kGroundLight] + share[pf::kGroundDark] > total / 20);
  CHECK(share[pf::kCoastLight] > 0);
  // Rock and forest are the two the terrain graph erodes — a mountain must be
  // ringed by coast and a forest gives up its outermost corners to the ground
  // it meets — so both are compensated for, and both have to come out near
  // what was asked rather than at nothing.
  CHECK(share[pf::kMountain] * 100 / total >= 2);
  CHECK(share[pf::kForest] * 100 / total >= 15);

  // The point of the clearings is that bases fit. Asking the placer for four
  // start locations is the same question a player would ask, and it only
  // succeeds where there is open ground with room around it.
  for (int p = 0; p < 16; p++) pf_map_set_owner(map, p, PF_OWNER_NOBODY);
  for (int p = 0; p < 4; p++) pf_map_set_owner(map, p, PF_OWNER_COMPUTER);

  // Mines first, so the starts have something to be near.
  const int mines = pf_map_place_gold_mines(map, 8);
  CHECK(mines >= 4);
  CHECK_EQ(pf_map_place_start_locations(map), 4);

  // Every start has a mine within reach, or the base has nothing to work.
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit start;
    pf_map_unit(map, i, &start);
    if (start.type != 94 && start.type != 95) continue;
    long long nearest = 1LL << 40;
    for (int j = 0; j < pf_map_unit_count(map); j++) {
      pf_unit mine;
      pf_map_unit(map, j, &mine);
      if (mine.type != 0x5c) continue;
      const long long dx = int(start.x) - int(mine.x);
      const long long dy = int(start.y) - int(mine.y);
      nearest = std::min(nearest, dx * dx + dy * dy);
    }
    CHECK(nearest <= 20 * 20);
  }

  // And a base needs more than a tile: every start location wants a clear
  // block around it, which is what the clearing carved.
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit u;
    pf_map_unit(map, i, &u);
    int open = 0;
    for (int dy = -3; dy <= 3; dy++) {
      for (int dx = -3; dx <= 3; dx++) {
        const int tx = int(u.x) + dx, ty = int(u.y) + dy;
        if (tx < 0 || ty < 0 || tx >= 96 || ty >= 96) continue;
        uint8_t q[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, tx, ty)), q);
        bool clear = true;
        for (int k = 0; k < 4; k++) {
          if (q[k] != pf::kGroundLight && q[k] != pf::kGroundDark) clear = false;
        }
        if (clear) open++;
      }
    }
    CHECK(open >= 40);      // out of 49
  }
  std::printf("     generated: water %d%%, coast %d%%, ground %d%%, forest %d%%, rock %d%%\n",
              (share[pf::kWaterDark] + share[pf::kWaterLight]) * 100 / total,
              (share[pf::kCoastLight] + share[pf::kCoastDark]) * 100 / total,
              (share[pf::kGroundLight] + share[pf::kGroundDark]) * 100 / total,
              share[pf::kForest] * 100 / total, share[pf::kMountain] * 100 / total);

  // It round-trips like any other map, which is the invariant everything else
  // in this suite rests on.
  size_t len = 0;
  uint8_t* bytes = pf_map_save(map, &len, nullptr);
  CHECK(bytes != nullptr);
  if (bytes) {
    pf_map* reopened = pf_map_open(bytes, len, nullptr);
    CHECK(reopened != nullptr);
    if (reopened) {
      size_t again = 0;
      uint8_t* twice = pf_map_save(reopened, &again, nullptr);
      CHECK_EQ(int(again), int(len));
      if (twice) {
        CHECK(std::memcmp(bytes, twice, len) == 0);
        pf_buffer_free(twice);
      }
      pf_map_free(reopened);
    }
    pf_buffer_free(bytes);
  }

  // Same seeds, same map: a seed is how a generated map gets shared.
  pf_map* twin = pf_map_generate(&params, layers, 3, nullptr);
  if (twin) {
    int same = 1;
    for (int y = 0; y < 96 && same; y++) {
      for (int x = 0; x < 96; x++) {
        if (pf_map_tile_at(map, x, y) != pf_map_tile_at(twin, x, y)) { same = 0; break; }
      }
    }
    CHECK(same);
    pf_map_free(twin);
  }

  // A different seed gives a different map, or the seed is doing nothing.
  layers[0].seed = 54321u;
  pf_map* other = pf_map_generate(&params, layers, 3, nullptr);
  if (other) {
    int differences = 0;
    for (int y = 0; y < 96; y++) {
      for (int x = 0; x < 96; x++) {
        if (pf_map_tile_at(map, x, y) != pf_map_tile_at(other, x, y)) differences++;
      }
    }
    CHECK(differences > 96 * 96 / 4);
    pf_map_free(other);
  }
  pf_map_free(map);
}
TEST(a_generated_map_is_drawn_with_tiles_the_tileset_has) {
  // A tile group defines sixteen variations and a tileset populates only some
  // of them; the rest are valid ids whose every pixel is nothing. The tile
  // index filters them out — but only when artwork is attached to the map, and
  // pf_map_generate builds its own map, which has none. So generation chose
  // from all sixteen and most of the map came out as blank megatiles: flat
  // colour blocks with hard stair-stepped edges and none of the shore, cliff
  // or forest-edge drawings that make a map look like one.
  //
  // The fix is the caller's — attach the artwork, then pf_map_refit, which is
  // what that call is for. This holds both halves of it.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_noise_layer layers[3] = {
      {0.045f, 12345u, 1.0f}, {0.11f, 999u, 0.45f}, {0.30f, 4242u, 0.15f}};
  pf_generate_params params{};
  params.width = params.height = 96;
  params.tileset = 0;
  params.water = 0.255f;
  params.coast = 0.109f;
  params.forest = 0.35f;
  params.rock = 0.14f;
  params.detail_seed = 77u;
  params.detail_scale = 0.10f;
  params.clearings = 4;
  params.clearing_radius = 8;

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  pf_map* map = pf_map_generate(&params, layers, 3, nullptr);
  CHECK(map != nullptr);
  if (!map) { pf_tileset_art_free(art); return; }

  auto undrawable = [&](pf_map* m) {
    int bad = 0;
    for (int y = 0; y < 96; y++) {
      for (int x = 0; x < 96; x++) {
        const uint16_t tile = uint16_t(pf_map_tile_at(m, x, y));
        const int mega = pf_tileset_art_megatile_for(art, tile);
        if (mega < 0 || pf_tileset_art_is_blank(art, mega)) bad++;
      }
    }
    return bad;
  };

  const int before = undrawable(map);
  pf_map_set_tileset_art(map, art);
  const int rewritten = pf_map_refit(map);
  const int after = undrawable(map);
  std::printf("     generated: %d of %d tiles undrawable, %d after a refit "
              "(%d rewritten)\n", before, 96 * 96, after, rewritten);

  // Not one tile the artwork cannot draw. This is the whole promise.
  CHECK_EQ(after, 0);
  // And the refit really was needed, rather than the generator having been
  // right all along — if this ever fails, the generator learned to filter and
  // the client's refit is redundant rather than wrong.
  CHECK(before > 0);

  // The refit must not have flattened the map back into solid blocks: the
  // point of a corner model is the transition tiles, and a shipped forest map
  // is about 41% of them.
  int mixed = 0;
  for (int y = 0; y < 96; y++) {
    for (int x = 0; x < 96; x++) {
      uint8_t q[4];
      pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), q);
      if (q[0] != q[1] || q[1] != q[2] || q[2] != q[3]) mixed++;
    }
  }
  CHECK(mixed > 96 * 96 / 5);

  // And no seam, on the path the client actually takes. The test beside this
  // one holds the generator to it, but the generator has no artwork and so
  // indexes all sixteen variations of every group; a refit indexes only the
  // ones the tileset drew, and a corner arrangement whose whole group it left
  // blank would fall back to a solid tile here and nowhere else.
  int seams = 0;
  for (int y = 0; y < 96; y++) {
    for (int x = 0; x < 96; x++) {
      uint8_t here[4];
      pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), here);
      if (x + 1 < 96) {
        uint8_t east[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, x + 1, y)), east);
        if (here[1] != east[0] || here[3] != east[2]) seams++;
      }
      if (y + 1 < 96) {
        uint8_t south[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y + 1)), south);
        if (here[2] != south[0] || here[3] != south[1]) seams++;
      }
    }
  }
  CHECK_EQ(seams, 0);

  if (const char* dump = std::getenv("PF_DUMP_GENERATED")) {
    std::printf("     wrote %s: %d\n", dump, int(pf_map_save_file(map, dump)));
  }
  pf_map_free(map);
  pf_tileset_art_free(art);
}
TEST(walls_go_when_their_ground_does) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  for (int y = 8; y < 20; y++) pf_map_paint_terrain(map, 10, y, PF_TERRAIN_WALL_HUMAN, 1);
  uint8_t q[4];
  pf::decode_tile(uint16_t(pf_map_tile_at(map, 10, 12)), q);
  CHECK(q[0] == pf::kWallHuman || q[1] == pf::kWallHuman ||
        q[2] == pf::kWallHuman || q[3] == pf::kWallHuman);

  // A lake painted nearby turns the ground between into coast, and a wall
  // cannot stand on coast. Clearing only under the brush left it there.
  for (int pass = 0; pass < 4; pass++) {
    for (int y = 10; y < 16; y++) {
      for (int x = 13; x < 19; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }

  int walls_on_bad_ground = 0;
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      uint8_t t[4];
      pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), t);
      bool wall = false;
      for (int i = 0; i < 4; i++) {
        if (t[i] == pf::kWallHuman || t[i] == pf::kWallOrc) wall = true;
      }
      if (!wall) continue;
      // A wall tile draws ground in its clear quadrants, so a wall that
      // survived beside water would show water there instead.
      for (int i = 0; i < 4; i++) {
        const bool ok = t[i] == pf::kWallHuman || t[i] == pf::kWallOrc ||
                        t[i] == pf::kGroundLight || t[i] == pf::kGroundDark;
        if (!ok) walls_on_bad_ground++;
      }
    }
  }
  CHECK_EQ(walls_on_bad_ground, 0);
  pf_map_free(map);
}
TEST(movement_follows_the_tiles_it_is_painted_from) {
  // SQM is very nearly a function of the tile. Across the shipped maps, 1268
  // tiles disagree with the table it is derived from — 236 out of 6,975,488. So the table has to agree
  // with the corpus almost everywhere, and painting has to keep it that way.
  CHECK_EQ(pf_movement_class_count(), 8);
  for (int i = 0; i < pf_movement_class_count(); i++) {
    const int value = pf_movement_class_value(i);
    CHECK(value > 0);
    CHECK(pf_movement_class_name(i) != nullptr);
    CHECK_EQ(pf_movement_class_of(value), i);
  }
  CHECK_EQ(pf_movement_class_of(0x1234), -1);   // outside the set, and says so

  pf_map* map = pf_map_create(32, 32, 0, nullptr);
  CHECK(map != nullptr);

  // Painting writes movement to match, without anyone asking it to.
  pf_map_paint_terrain(map, 8, 8, PF_TERRAIN_WATER_DARK, 5);
  pf_map_paint_terrain(map, 20, 20, PF_TERRAIN_FOREST, 3);
  int mismatched = 0;
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      if (pf_map_movement_at(map, x, y) != pf_tile_movement(pf_map_tile_at(map, x, y))) {
        mismatched++;
      }
    }
  }
  CHECK_EQ(mismatched, 0);
  CHECK_EQ(pf_map_movement_at(map, 8, 8), 0x0040);   // water
  CHECK_EQ(pf_map_movement_at(map, 20, 20), 0x0081); // forest

  // An override sticks, and resetting puts it back. Both report honestly:
  // resetting a map with nothing overridden must change nothing, so a caller
  // can skip the undo step.
  CHECK_EQ(pf_map_reset_movement(map, 0, 0, 0, 0), 0);
  CHECK_EQ(pf_map_set_movement(map, 4, 4, 0x0081), PF_OK);
  CHECK_EQ(pf_map_movement_at(map, 4, 4), 0x0081);
  CHECK_EQ(pf_map_reset_movement(map, 0, 0, 4, 4), 0);   // outside the rectangle
  CHECK_EQ(pf_map_movement_at(map, 4, 4), 0x0081);
  CHECK_EQ(pf_map_reset_movement(map, 0, 0, 0, 0), 1);
  CHECK_EQ(pf_map_movement_at(map, 4, 4), pf_tile_movement(pf_map_tile_at(map, 4, 4)));

  // Repainting a tile keeps the override, which is the point of overriding:
  // the layer is where a mapper says a bridge is walkable or a shallow is
  // closed, and a touch-up to the artwork must not quietly undo that. It comes
  // off through Reset Movement, or the movement palette's own "put it back".
  //
  // Nothing records which tiles were meant. Nothing needs to: a value that
  // disagrees with the tile under it is the answer, which is also why it
  // survives a save and a reload.
  CHECK_EQ(pf_map_set_movement(map, 4, 4, 0x0040), PF_OK);
  pf_map_paint_terrain(map, 4, 4, PF_TERRAIN_GROUND_LIGHT, 1);
  CHECK_EQ(pf_map_movement_at(map, 4, 4), 0x0040);
  pf_map_paint_terrain_raw(map, 4, 4, PF_TERRAIN_FOREST, 1);
  CHECK_EQ(pf_map_movement_at(map, 4, 4), 0x0040);   // and the raw painter too

  // A tile nobody overrode still follows its terrain, which is what keeps the
  // layer right without anyone editing it.
  pf_map_paint_terrain(map, 6, 6, PF_TERRAIN_FOREST, 1);
  CHECK_EQ(pf_map_movement_at(map, 6, 6), pf_tile_movement(pf_map_tile_at(map, 6, 6)));

  // Put back, and the tile follows its terrain again.
  CHECK_EQ(pf_map_reset_movement(map, 0, 0, 0, 0), 1);
  CHECK_EQ(pf_map_movement_at(map, 4, 4), pf_tile_movement(pf_map_tile_at(map, 4, 4)));

  CHECK_EQ(pf_map_set_movement(map, -1, 0, 1), PF_ERR_OUT_OF_RANGE);
  CHECK_EQ(pf_map_movement_at(map, 99, 0), -1);
  pf_map_free(map);

  // Every movement value in every *shipped* map is one of the eight, and every
  // tile that is not an override agrees with the table. Community maps are a
  // different matter and are measured below rather than asserted about.
  if (!have_corpus()) { skip("no shipped maps"); return; }
  long long total = 0, deviating = 0, unknown = 0;
  for (const std::string& path : g_shipped) {
    pf_map* shipped = pf_map_open_file(path.c_str(), nullptr);
    if (!shipped) continue;
    const int w = pf_map_width(shipped), h = pf_map_height(shipped);
    const uint16_t* mv = pf_map_movement(shipped);
    const uint16_t* tiles = pf_map_tiles(shipped);
    for (int i = 0; i < w * h; i++) {
      total++;
      if (pf_movement_class_of(mv[i]) < 0) unknown++;
      if (mv[i] != pf_tile_movement(tiles[i])) deviating++;
    }
    pf_map_free(shipped);
  }
  std::printf("     %lld tiles, %lld outside the eight classes, %lld deviating (%.4f%%)\n",
              total, unknown, deviating, 100.0 * double(deviating) / double(total));
  CHECK_EQ(unknown, 0);
  // Measured at 236 of 6,975,488. Allow a little slack for a differing corpus,
  // but a table that has drifted will blow straight past this.
  CHECK(deviating * 10000 < total);

  // What the community maps do, reported and not asserted. They use values
  // outside the eight and deviate from the table far more often, which is why
  // pf_movement_class_of returns -1 rather than guessing and the editor shows an
  // unrecognised value as raw hex. Worth printing: it is the evidence that the
  // eight are Blizzard's habit and not the format's rule.
  long long other = 0, other_unknown = 0, other_deviating = 0;
  for (const std::string& path : pft::g_corpus) {
    if (is_shipped(path)) continue;
    pf_map* custom = pf_map_open_file(path.c_str(), nullptr);
    if (!custom) continue;
    const int w = pf_map_width(custom), h = pf_map_height(custom);
    const uint16_t* mv = pf_map_movement(custom);
    const uint16_t* tiles = pf_map_tiles(custom);
    for (int i = 0; i < w * h; i++) {
      other++;
      if (pf_movement_class_of(mv[i]) < 0) other_unknown++;
      if (mv[i] != pf_tile_movement(tiles[i])) other_deviating++;
    }
    pf_map_free(custom);
  }
  if (other) {
    std::printf("     community: %lld tiles, %lld outside the eight (%.2f%%),"
                " %lld deviating (%.2f%%)\n",
                other, other_unknown, 100.0 * double(other_unknown) / double(other),
                other_deviating, 100.0 * double(other_deviating) / double(other));
  }
}
TEST(randomizing_shades_keeps_the_map_legal) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(48, 48, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  for (int y = 6; y < 20; y++) {
    for (int x = 6; x < 20; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
  }

  const int changed = pf_map_randomize_shades(map, 0, 0, 0, 0, 4242u);
  CHECK(changed > 0);

  // Every tile still decodes, which is the thing randomising corners could
  // break: a shade the tile set cannot express would come back as unknown.
  int unknown = 0, dark_ground = 0;
  for (int y = 0; y < 48; y++) {
    for (int x = 0; x < 48; x++) {
      uint8_t q[4];
      pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), q);
      for (int i = 0; i < 4; i++) {
        if (q[i] == pf::kTerrainUnknown) unknown++;
        if (q[i] == pf::kGroundDark) dark_ground++;
      }
    }
  }
  CHECK_EQ(unknown, 0);
  CHECK(dark_ground > 0);      // it actually mixed the two shades in

  // And the same seed gives the same map, so it can be undone and repeated.
  pf_map* twin = pf_map_create(48, 48, 0, &st);
  if (twin) {
    for (int y = 6; y < 20; y++) {
      for (int x = 6; x < 20; x++) pf_map_paint_terrain(twin, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
    CHECK_EQ(pf_map_randomize_shades(twin, 0, 0, 0, 0, 4242u), changed);
    int same = 1;
    for (int y = 0; y < 48 && same; y++) {
      for (int x = 0; x < 48; x++) {
        if (pf_map_tile_at(map, x, y) != pf_map_tile_at(twin, x, y)) { same = 0; break; }
      }
    }
    CHECK(same);
    pf_map_free(twin);
  }
  pf_map_free(map);
}
TEST(brush_shapes_and_symmetry_are_shared) {
  // A square brush paints through the core's own size parameter, so it has no
  // point list at all.
  CHECK_EQ(pf_brush_points(5, 5, 5, PF_BRUSH_SQUARE, 1.0f, nullptr, nullptr, 0), 0);
  CHECK_EQ(pf_brush_points(5, 5, 1, PF_BRUSH_CIRCLE, 1.0f, nullptr, nullptr, 0), 0);

  std::vector<int> pts(2 * 21 * 21);
  const int circle = pf_brush_points(10, 10, 5, PF_BRUSH_CIRCLE, 1.0f, nullptr,
                                     pts.data(), int(pts.size()));
  CHECK(circle > 0);
  CHECK(circle < 25);                       // a disc is smaller than its square
  for (int i = 0; i < circle; i++) {
    const int dx = pts[i * 2] - 10, dy = pts[i * 2 + 1] - 10;
    CHECK(dx * dx + dy * dy <= 2 * 2 + 2);  // and every point is inside it
  }

  // At its smallest a disc is a cross. Any threshold wide enough to keep the
  // four orthogonal neighbours of a radius-1 brush also keeps the corners, so
  // "round" at size 3 used to paint the same 3x3 square the square brush does.
  const int cross = pf_brush_points(10, 10, 3, PF_BRUSH_CIRCLE, 1.0f, nullptr,
                                    pts.data(), int(pts.size()));
  CHECK_EQ(cross, 5);
  for (int i = 0; i < cross; i++) {
    const int dx = pts[i * 2] - 10, dy = pts[i * 2 + 1] - 10;
    CHECK_EQ(dx * dx + dy * dy <= 1, true);
  }

  // The scatter's sequence comes back so a stroke keeps its pattern; the same
  // seed must therefore give the same points.
  uint32_t a = 12345, b = 12345;
  std::vector<int> one(2 * 21 * 21), two(2 * 21 * 21);
  const int n1 = pf_brush_points(4, 4, 7, PF_BRUSH_SCATTER, 0.5f, &a, one.data(), int(one.size()));
  const int n2 = pf_brush_points(4, 4, 7, PF_BRUSH_SCATTER, 0.5f, &b, two.data(), int(two.size()));
  CHECK_EQ(n1, n2);
  CHECK_EQ(int(a), int(b));
  CHECK(a != 12345u);                       // and it advanced
  for (int i = 0; i < n1 * 2; i++) CHECK_EQ(one[i], two[i]);

  // And the can is round: a spray whose puff is square is a grid of random
  // tiles rather than a puff. Full density, so only the shape can exclude one.
  uint32_t c = 999;
  std::vector<int> puff(2 * 21 * 21);
  const int sprayed = pf_brush_points(10, 10, 7, PF_BRUSH_SCATTER, 1.0f, &c,
                                      puff.data(), int(puff.size()));
  CHECK(sprayed > 0);
  CHECK(sprayed < 49);                      // the corners of the square are gone
  for (int i = 0; i < sprayed; i++) {
    const int dx = puff[i * 2] - 10, dy = puff[i * 2 + 1] - 10;
    CHECK(dx * dx + dy * dy <= 3 * 3 + 3);
  }

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  int out[8];
  CHECK_EQ(pf_symmetry_points(map, 4, 6, 1, 1, PF_MIRROR_NONE, out, 8), 1);
  CHECK_EQ(pf_symmetry_points(map, 4, 6, 1, 1, PF_MIRROR_LEFT_RIGHT, out, 8), 2);
  CHECK_EQ(out[2], 27);                     // 32 - 4 - 1
  CHECK_EQ(out[3], 6);
  CHECK_EQ(pf_symmetry_points(map, 4, 6, 1, 1, (PF_MIRROR_LEFT_RIGHT | PF_MIRROR_TOP_BOTTOM), out, 8), 4);

  // A 4x4 building anchored at 2 spans 2..5, so its mirror is anchored at 26.
  CHECK_EQ(pf_symmetry_points(map, 2, 8, 4, 4, PF_MIRROR_LEFT_RIGHT, out, 8), 2);
  CHECK_EQ(out[2], 26);

  // On the axis a point is its own reflection and is not repeated, or a unit
  // placed there would be refused as a stack of two.
  CHECK_EQ(pf_symmetry_points(map, 16, 16, 1, 1, PF_MIRROR_DIAG_NW_SE, out, 8), 1);
  // Mirrors combine into a group rather than firing once each: both diagonals
  // with both axes is the full eight-fold symmetry of a square.
  CHECK_EQ(pf_symmetry_points(map, 3, 7, 1, 1,
                              PF_MIRROR_LEFT_RIGHT | PF_MIRROR_TOP_BOTTOM |
                              PF_MIRROR_DIAG_NW_SE | PF_MIRROR_DIAG_SW_NE,
                              out, 8), 8);
  // Left-right and top-bottom together must also give the opposite corner,
  // which applying each once to the original would miss.
  {
    int four[8];
    CHECK_EQ(pf_symmetry_points(map, 4, 6, 1, 1,
                                PF_MIRROR_LEFT_RIGHT | PF_MIRROR_TOP_BOTTOM,
                                four, 8), 4);
    bool opposite = false;
    for (int i = 0; i < 4; i++) {
      if (four[i * 2] == 27 && four[i * 2 + 1] == 25) opposite = true;
    }
    CHECK(opposite);
  }

  CHECK_EQ(pf_symmetry_points(map, 3, 7, 1, 1, PF_MIRROR_DIAG_NW_SE, out, 8), 2);
  pf_map_free(map);

  // Diagonal means nothing on a map that is not square, and does nothing.
  pf_map* oblong = pf_map_create(32, 16, 0, &st);
  if (oblong) {
    CHECK_EQ(pf_symmetry_points(oblong, 3, 7, 1, 1, PF_MIRROR_DIAG_NW_SE, out, 8), 1);
    pf_map_free(oblong);
  }

  // Facing is stable per unit and inside range.
  CHECK_EQ(pf_unit_facing(5, 7, 0, 1), 0);
  CHECK_EQ(pf_unit_facing(5, 7, 0, 5), pf_unit_facing(5, 7, 0, 5));
  int distinct[5] = {};
  for (int x = 0; x < 12; x++) distinct[pf_unit_facing(x, 3, 0, 5)]++;
  int kinds = 0;
  for (int i = 0; i < 5; i++) if (distinct[i]) kinds++;
  CHECK(kinds >= 3);        // a rank of footmen must not come out as a parade
}
TEST(a_blank_map_starts_on_varied_plain_ground) {
  // One tile repeated 4,096 times reads as graph paper: the eye finds the
  // period the moment the zoom shows more than a few tiles. A new map starts
  // on the ground group's plain drawings, all of them, mixed by position.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(64, 64, 0, &st);
  CHECK_EQ(int(st), int(PF_OK));
  CHECK(map != nullptr);
  if (!map) return;

  std::map<int, int> used;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      const int tile = pf_map_tile_at(map, x, y);
      // Still light ground everywhere, and still solid: varying the drawing
      // must not vary what the terrain *is*.
      CHECK_EQ((tile >> 4) & 0xf, 0x5);
      used[tile & 0xf]++;
    }
  }

  const int plain = pf::plain_variation_count(0x5);
  CHECK_EQ(int(used.size()), plain);
  for (const auto& [variation, count] : used) {
    CHECK(variation < plain);   // never a decorated drawing
    CHECK(count > 0);
  }
  std::printf("     blank map ground: %d plain variations over %d tiles\n",
              int(used.size()), 64 * 64);

  // Deterministic: two maps of the same size start identical, so a mapper who
  // makes a new map twice gets the same ground both times.
  pf_map* twin = pf_map_create(64, 64, 0, nullptr);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      CHECK_EQ(pf_map_tile_at(map, x, y), pf_map_tile_at(twin, x, y));
    }
  }
  pf_map_free(twin);
  pf_map_free(map);
}
TEST(the_corner_brush_lays_less_than_a_single_tile) {
  // The smallest mark the model can hold is one corner of the grid — a quarter
  // of the four that a size-1 brush sets. Both spill into the tiles around them,
  // because a corner is shared by up to four tiles, so the honest measure is how
  // many tiles stopped being plain ground rather than how many corners moved.
  auto tiles_moved = [](bool corner) {
    pf_status st = PF_OK;
    pf_map* map = pf_map_create(32, 32, 0, &st);
    CHECK(map != nullptr);
    if (!map) return 0;
    const int n = 32 * 32;
    std::vector<uint16_t> before(pf_map_tiles(map), pf_map_tiles(map) + n);
    if (corner) {
      CHECK_EQ(pf_map_paint_corner(map, 16, 16, PF_TERRAIN_FOREST, 0), PF_OK);
    } else {
      CHECK_EQ(pf_map_paint_terrain(map, 16, 16, PF_TERRAIN_FOREST, 1), PF_OK);
    }
    const uint16_t* after = pf_map_tiles(map);
    int moved = 0;
    for (int i = 0; i < n; i++) {
      if (after[i] != before[i]) moved++;
    }
    pf_map_free(map);
    return moved;
  };

  const int corner = tiles_moved(true);
  const int whole = tiles_moved(false);
  std::printf("     forest — corner brush moves %d tiles, 1x1 moves %d\n",
              corner, whole);
  // It has to lay something, or the rung is a brush that paints nothing.
  CHECK(corner > 0);
  CHECK(corner < whole);

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;
  // Corners run one past the tiles in each axis, so the far edge is a corner and
  // not out of bounds — getting this wrong makes the last row unpaintable.
  CHECK_EQ(pf_map_paint_corner(map, 32, 32, PF_TERRAIN_FOREST, 0), PF_OK);
  CHECK_EQ(pf_map_paint_corner(map, 33, 0, PF_TERRAIN_FOREST, 0), PF_ERR_OUT_OF_RANGE);
  CHECK_EQ(pf_map_paint_corner(map, -1, 0, PF_TERRAIN_FOREST, 0), PF_ERR_OUT_OF_RANGE);
  // A wall is a per-tile overlay, so it has no corner to sit on. Refused rather
  // than quietly laying the ground recorded underneath it.
  CHECK_EQ(pf_map_paint_corner(map, 8, 8, PF_TERRAIN_WALL_HUMAN, 0),
           PF_ERR_OUT_OF_RANGE);
  pf_map_free(map);
}

TEST(the_detail_setting_decides_how_much_decoration_is_laid) {
  // A tile group holds several drawings of the same terrain: plain ones, and
  // ones with rocks or pebbles scattered on them. The Detail setting decides
  // how much of the second kind a stroke lays.
  //
  //   Plain     none of it
  //   Mixed     seven parts plain to three, by share rather than by count
  //   Detailed  all of it
  //
  // Plain and Detailed are each other's mirror and exclude the other kind
  // outright; Mixed is the only setting that weighs the two. A group the
  // tileset gave just one kind of drawing takes what it has whatever the
  // setting says, or the tile could not be drawn at all.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  // Where the plain drawings of the ground group stop; see
  // overrides/tile_variations.cpp.
  const int run = pf::plain_variation_count(0x5);
  CHECK(run > 0);

  // A wide sample, because these are shares rather than certainties: the tile
  // is picked from a coordinate hash, so a small patch can sit a few points
  // off its own average.
  auto paint_with = [&](int policy) {
    pf_status st = PF_OK;
    pf_map* map = pf_map_create(64, 64, 0, &st);
    pf_map_set_tileset_art(map, art);
    CHECK_EQ(pf_map_set_variation_policy(map, policy), PF_OK);
    for (int y = 2; y < 62; y++) {
      for (int x = 2; x < 62; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_GROUND_LIGHT, 1);
    }
    std::map<int, int> used;
    for (int y = 4; y < 60; y++) {
      for (int x = 4; x < 60; x++) used[pf_map_tile_at(map, x, y) & 0xf]++;
    }
    pf_map_free(map);
    return used;
  };
  auto plain_share = [&](const std::map<int, int>& used) {
    int plain = 0, all = 0;
    for (const auto& [variation, count] : used) {
      all += count;
      if (variation < run) plain += count;
    }
    return all ? double(plain) / double(all) : 0.0;
  };

  const auto plain = paint_with(PF_VARIATION_PLAIN);
  const auto detailed = paint_with(PF_VARIATION_DECORATED);
  const auto any = paint_with(PF_VARIATION_ANY);
  CHECK(!plain.empty());
  CHECK(!detailed.empty());

  std::printf("     grass — plain %.0f%% of %d drawings, detailed %.0f%% of %d, "
              "mixed %.0f%% of %d\n",
              plain_share(plain) * 100, int(plain.size()),
              plain_share(detailed) * 100, int(detailed.size()),
              plain_share(any) * 100, int(any.size()));

  // Plain is plain, and nothing else.
  CHECK_EQ(plain_share(plain), 1.0);
  for (const auto& [variation, count] : plain) CHECK(variation < run);

  // Detailed is decorated, and nothing else — the mirror of Plain. Grass has
  // decorated drawings, so nothing here falls back to plain ground.
  CHECK_EQ(plain_share(detailed), 0.0);
  for (const auto& [variation, count] : detailed) CHECK(variation >= run);

  // Mixed keeps decoration to a scattering: seven parts plain to three. Five
  // points of slack, because the share is exact in the index but the tile is
  // picked from a coordinate hash that only approximates uniform.
  CHECK(plain_share(any) > 0.65);
  CHECK(plain_share(any) < 0.75);
  int fancy_in_mixed = 0;
  for (const auto& [variation, count] : any) {
    if (variation >= run) fancy_in_mixed++;
  }
  CHECK(fancy_in_mixed > 0);

  // And Detailed really is more decoration than Mixed gives, which is the whole
  // reason the setting has three positions rather than two. Both shares are
  // pinned above, so this is now an ordering rather than a hedge.
  CHECK(plain_share(detailed) < plain_share(any));

  // Whatever each policy used, the unrestricted one may use too.
  for (const auto& [variation, count] : plain) CHECK(any.count(variation) > 0);
  for (const auto& [variation, count] : detailed) CHECK(any.count(variation) > 0);

  // The decorated drawings really are busier, which is what the setting is
  // reaching for. Read off Mixed: it is the only policy that lays both kinds,
  // so it is the only one where the comparison has two sides to it.
  int plain_detail = 0, fancy_detail = 0;
  for (const auto& [variation, count] : any) {
    const int busy = pf_tileset_art_detail(
        art, pf_tileset_art_megatile_for(art, uint16_t(0x50 | variation)));
    if (variation < run) plain_detail = std::max(plain_detail, busy);
    else fancy_detail = std::max(fancy_detail, busy);
  }
  CHECK(fancy_detail > plain_detail);

  pf_tileset_art_free(art);
}
TEST(mixing_shades_touches_only_the_terrain_painted_and_only_where) {
  // "Mix light and dark" belongs to the brush, not to the neighbourhood. A
  // stroke drawn diagonally has a bounding box several times its own area,
  // and shading the whole box is how mixing a dab of dirt came back having
  // re-shaded all the grass around it.
  //
  // A fringe along the stroke still moves, and must: light grass and dark
  // dirt cannot share a tile, so a shaded corner drags its neighbour one step
  // to meet it. What must not happen is the rest of the box.
  auto run = [](bool held) {
    pf_map* map = pf_map_create(32, 32, 0, nullptr);
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 32; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_GROUND_LIGHT, 1);
    }
    std::vector<std::pair<int, int>> stroke;
    for (int i = 4; i < 28; i++) stroke.emplace_back(i, i);
    for (const auto& [x, y] : stroke) pf_map_paint_terrain(map, x, y, PF_TERRAIN_COAST_LIGHT, 1);

    std::vector<uint16_t> before(32 * 32);
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 32; x++) before[size_t(y) * 32 + x] = uint16_t(pf_map_tile_at(map, x, y));
    }

    const int x0 = 4, y0 = 4, w = 24, h = 24;
    std::vector<uint8_t> mask(size_t(w) * size_t(h), 0);
    for (const auto& [x, y] : stroke) mask[size_t(y - y0) * size_t(w) + size_t(x - x0)] = 1;
    // held: the brush's own rule. Otherwise the whole box, every family —
    // which is what the menu's "Randomize light and dark" means.
    pf_map_shade_stroke(map, x0, y0, w, h, 4242u,
                        held ? PF_TERRAIN_COAST_LIGHT : -1,
                        held ? mask.data() : nullptr);

    int moved = 0, far = 0;
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 32; x++) {
        if (before[size_t(y) * 32 + x] == uint16_t(pf_map_tile_at(map, x, y))) continue;
        moved++;
        // How far off the stroke's own diagonal this tile sits.
        int nearest = 32;
        for (const auto& [sx, sy] : stroke) {
          nearest = std::min(nearest, std::max(std::abs(sx - x), std::abs(sy - y)));
        }
        if (nearest > 2) far++;
      }
    }
    pf_map_free(map);
    return std::make_pair(moved, far);
  };

  const auto [moved, far] = run(true);
  const auto [loose_moved, loose_far] = run(false);

  CHECK(moved > 0);            // it did shade the stroke
  CHECK_EQ(far, 0);            // and nothing away from it
  CHECK(loose_far > 50);       // where the unheld pass reaches the whole box
  CHECK(loose_moved > moved * 2);
  std::printf("     shade — brush %d tiles (%d far), whole box %d (%d far)\n",
              moved, far, loose_moved, loose_far);
}
TEST(a_mixed_stroke_leaves_no_seam_around_itself) {
  // Two neighbouring tiles share two corners, and each tile's drawing says
  // what its own four corners are. When they disagree, the eye sees a hard
  // edge with no transition drawn across it — an unblended tile.
  //
  // Shading flips corners, and a corner on the edge of the shaded rectangle
  // belongs to the tile just outside it as well. That tile used to be left
  // alone: the rectangle said where to shade *and* which tiles to draw again,
  // so a mixed stroke came away ringed by tiles that had never been told.
  auto seams = [](const pf_map* map) {
    const int w = pf_map_width(map), h = pf_map_height(map);
    int count = 0;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        uint8_t here[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), here);
        if (x + 1 < w) {
          uint8_t east[4];
          pf::decode_tile(uint16_t(pf_map_tile_at(map, x + 1, y)), east);
          if (here[1] != east[0] || here[3] != east[2]) count++;
        }
        if (y + 1 < h) {
          uint8_t south[4];
          pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y + 1)), south);
          if (here[2] != south[0] || here[3] != south[1]) count++;
        }
      }
    }
    return count;
  };

  pf_map* map = pf_map_create(48, 48, 0, nullptr);
  for (int y = 0; y < 48; y++) {
    for (int x = 0; x < 48; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_GROUND_LIGHT, 1);
  }
  // The baseline has to be clean, or the count after means nothing.
  CHECK_EQ(seams(map), 0);

  // A solid block of it, mask and all, so every corner of the rectangle's own
  // border is one the shading may move.
  const int x0 = 12, y0 = 12, w = 20, h = 20;
  const std::vector<uint8_t> mask(size_t(w) * size_t(h), 1);
  const int changed = pf_map_shade_stroke(map, x0, y0, w, h, 91u,
                                          PF_TERRAIN_GROUND_LIGHT, mask.data());
  CHECK(changed > 0);          // it really did shade something

  const int left = seams(map);
  std::printf("     mixed stroke: %d tiles shaded, %d seams left\n", changed, left);
  CHECK_EQ(left, 0);
  pf_map_free(map);
}
TEST(the_view_scrolls_zooms_and_fits) {
  // Where the window is looking. Arithmetic, no window - which is why it can
  // be tested at all, and why both clients get the same answers.
  pf::View v;
  v.map_w = 96;
  v.map_h = 96;
  v.viewport_w = 1200;
  v.viewport_h = 800;

  // A tile is a whole number of device pixels at every zoom. At 110% the exact
  // value is 35.2, and drawing there is what softens pixel art.
  v.zoom = 100;
  CHECK_EQ(pf::view_tile_px(v), 32);
  v.zoom = 110;
  CHECK_EQ(pf::view_tile_px(v), 35);
  v.dpr = 2.0;
  CHECK_EQ(pf::view_tile_px(v), 70);
  v.dpr = 1.0;
  v.zoom = 100;

  // Scrolling cannot leave the map.
  v.scroll_x = -50.0;
  v.scroll_y = 500.0;
  pf::view_clamp(v);
  CHECK(v.scroll_x == 0.0);
  CHECK(v.scroll_y <= 96.0);
  CHECK(v.scroll_y > 0.0);

  // A map narrower than the window sits at the origin rather than drifting.
  v.map_w = 8;
  v.scroll_x = 3.0;
  pf::view_clamp(v);
  CHECK(v.scroll_x == 0.0);
  v.map_w = 96;

  // Fitting shows the whole map, and it really does fit: the pixels have to
  // land inside the viewport, not merely nearly.
  CHECK(pf::view_fit(v));
  CHECK(v.fitted);
  CHECK(v.map_w * pf::view_tile_px(v) <= v.viewport_w);
  CHECK(v.map_h * pf::view_tile_px(v) <= v.viewport_h);

  // Opening a small map only ever zooms out.
  pf::View small = v;
  small.map_w = small.map_h = 16;
  CHECK(pf::view_fit(small, true));
  CHECK_EQ(small.zoom, 100);
  CHECK(!small.fitted);

  // Zooming about a point keeps the tile under it, and a zoom somebody chose
  // stops the view being re-fitted when the window changes size.
  v.zoom = 100;
  v.scroll_x = 10.0;
  v.scroll_y = 10.0;
  v.fitted = true;
  int before_x = 0, before_y = 0;
  CHECK(pf::view_tile_at(v, 400, 300, before_x, before_y));
  pf::view_zoom_step(v, 1, 400, 300);
  int after_x = 0, after_y = 0;
  CHECK(pf::view_tile_at(v, 400, 300, after_x, after_y));
  CHECK(!v.fitted);
  CHECK(std::abs(after_x - before_x) <= 1);
  CHECK(std::abs(after_y - before_y) <= 1);

  // A point outside the map says so rather than returning a tile that is not
  // there, which is what stops a click off the edge painting.
  int ox = 0, oy = 0;
  v.zoom = 100;
  v.scroll_x = 0.0;
  v.scroll_y = 0.0;
  CHECK(pf::view_tile_at(v, 10, 10, ox, oy));
  CHECK(!pf::view_tile_at(v, -40, 10, ox, oy));

  // The composed region covers the viewport and never runs off the map.
  int x0 = 0, y0 = 0, cols = 0, rows = 0;
  v.scroll_x = 90.5;
  v.scroll_y = 0.25;
  pf::view_clamp(v);
  pf::view_region(v, x0, y0, cols, rows);
  CHECK(x0 >= 0 && y0 >= 0);
  CHECK(x0 + cols <= v.map_w);
  CHECK(y0 + rows <= v.map_h);
  CHECK(cols * pf::view_tile_px(v) >= std::min(v.viewport_w, v.map_w * pf::view_tile_px(v)));

  // And it lands on whole pixels, or panning softens every tile edge.
  int px = 0, py = 0;
  pf::view_origin(v, x0, y0, px, py);
  CHECK(px <= 0 && py <= 0);

  // Fitting a rectangle centres it.
  CHECK(pf::view_fit_rect(v, 40, 40, 8, 8));
  int cx = 0, cy = 0;
  CHECK(pf::view_tile_at(v, v.viewport_w / 2, v.viewport_h / 2, cx, cy));
  CHECK(std::abs(cx - 44) <= 2);
  CHECK(std::abs(cy - 44) <= 2);
}
TEST(the_zoom_ladder_is_a_list_and_stepping_walks_it) {
  // The rungs, in order, and nothing between them. Written out here because
  // the whole point of a list is that it is not derivable: a test that
  // recomputed it from the table would agree with any table at all.
  const int expect[] = {25,  33,  40,  50,  66,  75,  100, 125, 150,
                        175, 200, 250, 300, 400, 500, 600, 800};
  const int count = int(sizeof(expect) / sizeof(expect[0]));
  CHECK_EQ(pf_zoom_level_count(), count);
  for (int i = 0; i < count; i++) CHECK_EQ(pf_zoom_level(i), expect[i]);
  CHECK_EQ(pf_zoom_min(), 25);
  CHECK_EQ(pf_zoom_max(), 800);

  // One press moves one rung, and in-then-out comes back where it started -
  // which the old arithmetic ladder got wrong at exactly 100%.
  for (int i = 0; i < count; i++) {
    const int here = expect[i];
    CHECK_EQ(pf_zoom_step(here, 1), i + 1 < count ? expect[i + 1] : 800);
    CHECK_EQ(pf_zoom_step(here, -1), i > 0 ? expect[i - 1] : 25);
    if (i + 1 < count) CHECK_EQ(pf_zoom_step(pf_zoom_step(here, 1), -1), here);
  }

  // Snapping takes the nearest rung, from either side and from outside.
  CHECK_EQ(pf_zoom_snap(97), 100);
  CHECK_EQ(pf_zoom_snap(120), 125);
  CHECK_EQ(pf_zoom_snap(140), 150);
  CHECK_EQ(pf_zoom_snap(5), 25);
  CHECK_EQ(pf_zoom_snap(5000), 800);

  // A fit is the one thing allowed off the ladder: 128x128 is 1024 px at the
  // lowest rung, taller than the canvas on a 1080p screen, and a Fit that
  // leaves a quarter of the map off the bottom is not a fit.
  pf::View v;
  v.map_w = v.map_h = 128;
  v.viewport_w = 1480;
  v.viewport_h = 900;
  CHECK(pf::view_fit(v, false));
  CHECK(v.zoom < pf_zoom_min());
  CHECK(v.map_h * pf::view_tile_px(v) <= v.viewport_h);
  // And stepping from there lands back on a rung rather than below it.
  CHECK_EQ(pf_zoom_step(v.zoom, 1), 25);
  CHECK_EQ(pf_zoom_step(v.zoom, -1), v.zoom);

  // A window with room for the whole map stays on the ladder.
  v.viewport_w = 1480;
  v.viewport_h = 1200;
  CHECK(pf::view_fit(v, false));
  CHECK_EQ(v.zoom, pf_zoom_snap(v.zoom));
}
TEST(shading_makes_patches_not_static) {
  // Randomising the shade corner by corner is white noise and looks like it.
  // The measure that catches it is churn: how often the shade differs between
  // two neighbouring corners of the same family. Blizzard's 28 multiplayer
  // maps average 10.6%, p10 4.5% and p90 17.7%. A coin per corner gives ~39%.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(96, 96, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;
  for (int y = 0; y < 96; y++) {
    for (int x = 0; x < 96; x++) pf_map_paint_terrain_raw(map, x, y, PF_TERRAIN_GROUND_LIGHT, 1);
  }
  CHECK(pf_map_randomize_shades(map, 0, 0, 0, 0, 20260730) > 0);

  // Read the shade back off the corners of the tiles that were written.
  auto dark_at = [&](int x, int y, int corner) {
    uint8_t q[4];
    pf_tile_quadrants(uint16_t(pf_map_tile_at(map, x, y)), q);
    return q[corner] == PF_TERRAIN_GROUND_DARK;
  };
  int pairs = 0, flips = 0;
  for (int y = 0; y < 96; y++) {
    for (int x = 0; x + 1 < 96; x++) {
      // The right-hand corners of one tile are the left-hand corners of the
      // next, so comparing across tiles compares neighbouring corners.
      pairs += 2;
      flips += int(dark_at(x, y, 1) != dark_at(x + 1, y, 1));
      flips += int(dark_at(x, y, 3) != dark_at(x + 1, y, 3));
    }
  }
  const double churn = double(flips) / double(pairs);
  std::printf("     shade churn %.1f%% (Blizzard 4.5-17.7%%)\n", churn * 100.0);
  CHECK(churn < 0.25);        // well clear of the ~39% a coin per corner gives
  CHECK(churn > 0.005);       // and not a single flat slab either
  pf_map_free(map);
}
TEST(plain_ground_is_the_same_three_variations_in_every_tileset) {
  // The swamp tileset populates the slot the other three leave blank, so
  // reading the boundary off the artwork made every swamp variation plain and
  // "Plain" painted decorated ground. The boundary belongs to the group, and
  // the corpus histogram says so for all four tilesets: 0-2 at a third each,
  // variation 3 in 0.0% of tiles, a thin tail from 4 up. See
  // overrides/tile_variations.cpp.
  if (!have_art()) { skip("no game artwork"); return; }

  for (int era = 0; era < 4; era++) {
    pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), era, nullptr);
    CHECK(art != nullptr);
    if (!art) continue;

    pf_status st = PF_OK;
    pf_map* map = pf_map_create(24, 24, era, &st);
    pf_map_set_tileset_art(map, art);
    CHECK_EQ(pf_map_set_variation_policy(map, PF_VARIATION_PLAIN), PF_OK);
    for (int y = 2; y < 22; y++) {
      for (int x = 2; x < 22; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_GROUND_LIGHT, 1);
    }

    int worst = 0;
    for (int y = 4; y < 20; y++) {
      for (int x = 4; x < 20; x++) worst = std::max(worst, pf_map_tile_at(map, x, y) & 0xf);
    }
    // Three plain drawings, whatever the artwork happens to populate.
    CHECK(worst <= 2);
    pf_map_free(map);
    pf_tileset_art_free(art);
  }
}
TEST(plain_variations_keep_every_edge_drawing) {
  // The Plain setting is about decoration lying on the ground. A boundary
  // class carries none — its variations are alternate drawings of the same
  // edge, which the shipped maps use in equal measure — so filtering them can
  // only leave a cliff face with a single tile repeated all the way down.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) { pf_tileset_art_free(art); return; }
  pf_map_set_tileset_art(map, art);
  CHECK_EQ(pf_map_set_variation_policy(map, PF_VARIATION_PLAIN), PF_OK);

  for (int y = 2; y < 26; y++) pf_map_paint_terrain(map, 6, y, PF_TERRAIN_MOUNTAIN, 5);

  // Both drawings of the rock-to-coast edge have to appear down the column,
  // and every tile painted anywhere must be one the artwork can actually draw.
  std::map<int, int> left, right;
  int undrawable = 0;
  for (int y = 4; y < 24; y++) {
    left[pf_map_tile_at(map, 4, y) & 0xf]++;
    right[pf_map_tile_at(map, 9, y) & 0xf]++;
  }
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      const int tile = pf_map_tile_at(map, x, y);
      if (tile < 0 || pf_tileset_art_megatile_for(art, uint16_t(tile)) < 0) undrawable++;
    }
  }
  CHECK_EQ(undrawable, 0);
  CHECK(left.size() >= 2);
  CHECK(right.size() >= 2);

  // And the fill keeps all four of solid rock's drawings, which the shipped
  // maps use a quarter of the time each.
  std::map<int, int> fill;
  for (int y = 6; y < 22; y++) {
    for (int x = 5; x <= 7; x++) fill[pf_map_tile_at(map, x, y) & 0xf]++;
  }
  CHECK(fill.size() >= 4);

  pf_map_free(map);
  pf_tileset_art_free(art);
}

TEST(filling_a_terrain_with_itself_re_rolls_its_detail) {
  // A bucket over ground that already is that terrain used to be refused as a
  // no-op. It is not one: which drawing a tile gets comes from the tile index,
  // and that index is rebuilt whenever the detail policy changes, so a second
  // fill over the same ground is how a plain patch becomes a decorated one.
  //
  // Needs the artwork, because with none loaded there is no policy to apply.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  pf_map* map = pf_map_create(64, 64, PF_TILESET_FOREST, nullptr);
  CHECK(map != nullptr);
  if (!map) { pf_tileset_art_free(art); return; }
  CHECK_EQ(int(pf_map_set_tileset_art(map, art)), int(PF_OK));
  const int already = pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 32, 32)));

  // Not every solid group carries decorated drawings - the forest does not,
  // and where a group has none the two policies build the same list and
  // nothing could re-roll. Find one that does: the plain variations are the
  // unbroken run from 0, and a decorated one is anything non-blank above it.
  int terrain = -1;
  for (int group = 1; group <= 8 && terrain < 0; group++) {
    int run = 0;
    while (run < 16) {
      const int m = pf_tileset_art_megatile_for(art, uint16_t((group << 4) | run));
      if (m < 0 || pf_tileset_art_is_blank(art, m)) break;
      run++;
    }
    for (int v = run; v < 16; v++) {
      const int m = pf_tileset_art_megatile_for(art, uint16_t((group << 4) | v));
      if (m < 0 || pf_tileset_art_is_blank(art, m)) continue;
      const int t = pf_tile_dominant_terrain(uint16_t(group << 4));
      if (t >= 0 && t != already) terrain = t;
      break;
    }
  }
  if (terrain < 0) {
    skip("this tileset decorates nothing");
    pf_map_free(map);
    pf_tileset_art_free(art);
    return;
  }

  pf_map_set_variation_policy(map, PF_VARIATION_PLAIN);
  CHECK(pf_map_fill_terrain(map, 32, 32, terrain, 0, 0, 0, 0) > 0);

  std::vector<uint16_t> plain;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      plain.push_back(uint16_t(pf_map_tile_at(map, x, y)));
    }
  }

  // The same ground again, asking for the decorated drawings this time.
  pf_map_set_variation_policy(map, PF_VARIATION_DECORATED);
  const int rerolled = pf_map_fill_terrain(map, 32, 32, terrain, 0, 0, 0, 0);
  CHECK(rerolled > 0);

  int moved = 0;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      if (plain[size_t(y) * 64 + size_t(x)] != uint16_t(pf_map_tile_at(map, x, y))) {
        moved++;
      }
    }
  }
  // What comes back is what changed, not what the flood reached.
  CHECK_EQ(moved, rerolled);

  // And with the policy left alone there is nothing to re-roll, so the fill
  // reports no change: a client that checkpoints on a non-zero count must not
  // be handed an undo step for a click that did nothing.
  CHECK_EQ(pf_map_fill_terrain(map, 32, 32, terrain, 0, 0, 0, 0), 0);

  pf_map_free(map);
  pf_tileset_art_free(art);
}

}  // namespace pft

TEST(an_edit_leaves_the_map_it_did_not_reach_alone) {
  // The corner grid is a lossy reading of a real map. Warcraft's tiles carry
  // combinations the model votes away, so a good fraction of any shipped map
  // disagrees with the corners derived from it, and re-deriving those tiles is
  // what pf_map_refit exists to do. A brush must not do it as a side effect.
  //
  // It very nearly did. Painting legalises eleven tiles past the brush — the
  // length of the longest chain through the terrain tree — and used to re-pick
  // every tile in that margin from the grid. Across the 1349 maps here one
  // size-one click rewrote a mean of 502 tiles; on Twin Isles it took the
  // forest off the whole island. Choosing again only where the edit actually
  // moved something brings that to a mean of 14, worst 267.
  //
  // The bound below is loose on purpose: what it has to separate is "the
  // legalisation ripple reached this far" from "the whole margin was rewritten",
  // and those differ by more than an order of magnitude.
  int examined = 0;
  for (const std::string& path : pft::g_corpus) {
    pf_status status = PF_OK;
    pf_map* map = pf_map_open_file(path.c_str(), &status);
    if (!map) continue;
    const int w = pf_map_width(map), h = pf_map_height(map);
    std::vector<uint16_t> before(size_t(w) * size_t(h));
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        before[size_t(y) * size_t(w) + size_t(x)] = uint16_t(pf_map_tile_at(map, x, y));
      }
    }

    // One tile, dead centre, with the smallest brush there is.
    const int cx = w / 2, cy = h / 2;
    pf_map_paint_terrain(map, cx, cy, PF_TERRAIN_WATER_DARK, 1);

    int changed = 0, far = 0;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        if (before[size_t(y) * size_t(w) + size_t(x)] ==
            uint16_t(pf_map_tile_at(map, x, y))) {
          continue;
        }
        changed++;
        const int dx = x > cx ? x - cx : cx - x;
        const int dy = y > cy ? y - cy : cy - y;
        // Nothing may move outside the margin the legaliser was given.
        if (dx > 12 || dy > 12) far++;
      }
    }
    CHECK_EQ(far, 0);
    CHECK(changed < 400);
    examined++;
    pf_map_free(map);
  }
  // A silently empty corpus would let this pass by doing nothing.
  CHECK(examined > 0);
}

TEST(neighbouring_tiles_agree_about_the_corners_they_share) {
  // The blocky squares in a generated map are apply_corners falling back to a
  // solid tile for corners legalisation left illegal, and from outside the core
  // that is invisible: the fallback writes a real tile and re-reading the map
  // cannot say whether the generator meant it.
  //
  // What it cannot hide is its neighbours. Every tile of a generated map is
  // drawn from one shared corner grid, so a tile's right-hand corners are the
  // same two the tile beside it draws as its left-hand ones — unless one of them
  // was chosen by majority vote instead, which decides each tile alone. Solid
  // forest beside solid water is not merely ugly, it is a pair no corner grid
  // can produce.
  //
  // Generated maps only. A hand-made map is under no such obligation — its
  // author placed each tile — and they do disagree: prison-life.pud holds 4,343
  // seams across its 128x128 and is not wrong. Measured on the three maps the
  // bug was reported with, this separates them exactly: 0 seams on the one that
  // blends correctly, 23 and 29 on the two that came out blocky.
  //
  // Sizes and seeds both vary because the artefact was map-dependent: two maps
  // generated a minute apart, one clean and one with 19 fallbacks.
  int examined = 0, seams = 0, bad = 0;
  for (uint32_t seed = 1; seed <= 24; seed++) {
    pf_noise_layer layers[3] = {
        {0.045f, seed, 1.0f},
        {0.11f, seed * 7u + 1u, 0.45f},
        {0.30f, seed * 13u + 3u, 0.15f},
    };
    pf_generate_params params{};
    params.width = seed % 3 == 0 ? 128 : (seed % 3 == 1 ? 64 : 96);
    params.height = params.width;
    params.tileset = int(seed % 4);
    params.water = 0.26f;
    params.coast = 0.11f;
    params.forest = 0.35f;
    params.rock = 0.14f;
    params.detail_seed = seed * 31u + 5u;
    params.detail_scale = 0.10f;
    params.clearings = 4;
    params.clearing_radius = 8;

    pf_status st = PF_OK;
    pf_map* map = pf_map_generate(&params, layers, 3, &st);
    CHECK(map != nullptr);
    if (!map) continue;
    examined++;

    const int w = params.width, h = params.height;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        uint8_t here[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), here);
        // [topLeft, topRight, bottomLeft, bottomRight], so the right-hand edge
        // is 1 and 3 against the neighbour's 0 and 2, and the bottom edge is
        // 2 and 3 against its 0 and 1.
        if (x + 1 < w) {
          uint8_t east[4];
          pf::decode_tile(uint16_t(pf_map_tile_at(map, x + 1, y)), east);
          seams++;
          if (here[1] != east[0] || here[3] != east[2]) {
            if (bad < 4) {
              std::printf("     seam at (%d,%d) seed %u: %d,%d vs %d,%d\n",
                          x, y, seed, here[1], here[3], east[0], east[2]);
            }
            bad++;
          }
        }
        if (y + 1 < h) {
          uint8_t south[4];
          pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y + 1)), south);
          seams++;
          if (here[2] != south[0] || here[3] != south[1]) {
            if (bad < 4) {
              std::printf("     seam at (%d,%d) seed %u: %d,%d vs %d,%d\n",
                          x, y, seed, here[2], here[3], south[0], south[1]);
            }
            bad++;
          }
        }
      }
    }
    pf_map_free(map);
  }
  std::printf("     %d maps, %d seams, %d disagreeing\n", examined, seams, bad);
  CHECK_EQ(bad, 0);
  CHECK_EQ(examined, 24);
}
TEST(legalize_settles_terrains_that_pull_a_corner_both_ways) {
  // The seam test above proves the property on whole generated maps. This one
  // holds legalize alone to it, so a failure says which of the two is at fault.
  //
  // Coherent noise rather than a coin per corner, because that is what fails:
  // salt-and-pepper terrain legalises clean every time — with every terrain
  // beside every other, some majority always wins. It takes broad blobs meeting
  // along a ragged edge to produce the configuration that cannot settle,
  //
  //     forest forest . . .          the upper-left tile votes forest
  //     forest water  water          the lower-right ones vote water
  //     .      water  water
  //
  // where the corner in the middle belongs to tiles holding opposite
  // majorities, and a rule that keeps the commonest terrain moves it one way
  // and then the other for ever, whatever the pass budget.
  pf::TileIndex index;
  int grids = 0, unsettled = 0;
  for (uint32_t seed = 1; seed <= 40; seed++) {
    const pf::NoiseLayer layers[] = {
        {0.045f, seed, 1.0f}, {0.11f, seed * 7u + 1u, 0.45f}};
    const pf::LayeredNoise field(layers, 2);
    const pf::NoiseLayer growth_layer{0.10f, seed * 31u + 5u, 1.0f};
    const pf::LayeredNoise growth(&growth_layer, 1);

    // Water, coast and forest straight off two fields, with no legalisation-
    // friendly ordering: the point is to hand legalize the worst it will meet.
    const int n = 48;
    pf::CornerGrid grid(n, n);
    for (int y = 0; y <= n; y++) {
      for (int x = 0; x <= n; x++) {
        const float e = field.at(float(x), float(y));
        uint8_t t;
        if (e < 0.30f) t = pf::kWaterDark;
        else if (e < 0.40f) t = pf::kWaterLight;
        else if (e < 0.45f) t = pf::kCoastLight;
        else if (growth.at(float(x), float(y)) > 0.55f) t = pf::kForest;
        else t = pf::kGroundLight;
        grid.set(x, y, t);
      }
    }

    const pf::Rect rect{0, 0, n - 1, n - 1};
    pf::legalize(grid, rect);
    grids++;
    for (int y = 0; y < n; y++) {
      for (int x = 0; x < n; x++) {
        uint8_t c[4];
        grid.corners_of(x, y, c);
        if (index.lookup(c, 0) >= 0) continue;
        if (unsettled < 4) {
          std::printf("     seed %u unsettled at (%d,%d): %d %d / %d %d\n",
                      seed, x, y, c[0], c[1], c[2], c[3]);
        }
        unsettled++;
      }
    }
  }
  std::printf("     %d grids, %d tiles no tileset can draw\n", grids, unsettled);
  CHECK_EQ(unsettled, 0);
  CHECK_EQ(grids, 40);
}
TEST(pasting_terrain_blends_into_what_surrounds_it) {
  // A paste drops a block of corner terrains into the middle of others, so it
  // makes exactly the join a brush stroke makes and needs the same treatment:
  // the ring around it has to be legalised and redrawn, or forest lands against
  // water with no shore between and the tiles outside the fragment keep
  // whatever they were drawn as before.
  //
  // The generator is the fixture because it produces a map that is already
  // consistent everywhere — every tile drawn from one corner grid — so any
  // neighbouring pair that disagrees about a shared corner afterwards was made
  // by the paste and nothing else.
  pf_noise_layer layers[3] = {
      {0.045f, 12345u, 1.0f}, {0.11f, 999u, 0.45f}, {0.30f, 4242u, 0.15f}};
  pf_generate_params params{};
  params.width = params.height = 96;
  params.tileset = 0;
  params.water = 0.255f;
  params.coast = 0.109f;
  params.forest = 0.35f;
  params.rock = 0.14f;
  params.detail_seed = 77u;
  params.detail_scale = 0.10f;
  params.clearings = 4;
  params.clearing_radius = 8;

  pf_map* map = pf_map_generate(&params, layers, 3, nullptr);
  CHECK(map != nullptr);
  if (!map) return;
  const int n = 96;

  // The most solidly forest 8x8 to lift, and the most solidly water 8x8 to
  // drop it on: the pair furthest apart in the terrain graph, which is where a
  // missing transition shows up worst.
  const int side = 8;
  auto score = [&](int x, int y, bool want_forest) {
    int hits = 0;
    for (int ty = y; ty < y + side; ty++) {
      for (int tx = x; tx < x + side; tx++) {
        uint8_t q[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, tx, ty)), q);
        for (int i = 0; i < 4; i++) {
          const bool forest = q[i] == pf::kForest;
          const bool water = q[i] == pf::kWaterDark || q[i] == pf::kWaterLight;
          if (want_forest ? forest : water) hits++;
        }
      }
    }
    return hits;
  };
  int fx = 0, fy = 0, wx = 0, wy = 0, fbest = -1, wbest = -1;
  for (int y = 0; y + side <= n; y++) {
    for (int x = 0; x + side <= n; x++) {
      const int f = score(x, y, true);
      if (f > fbest) { fbest = f; fx = x; fy = y; }
      const int w = score(x, y, false);
      if (w > wbest) { wbest = w; wx = x; wy = y; }
    }
  }
  std::printf("     forest %dx%d at (%d,%d) score %d, water at (%d,%d) score %d\n",
              side, side, fx, fy, fbest, wx, wy, wbest);
  CHECK(fbest > side * side * 4 * 2 / 3);
  CHECK(wbest > side * side * 4 * 2 / 3);

  std::vector<uint16_t> before(size_t(n) * size_t(n));
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      before[size_t(y) * size_t(n) + size_t(x)] = uint16_t(pf_map_tile_at(map, x, y));
    }
  }

  pf_clipboard* clip = pf_clipboard_copy(map, fx, fy, side, side, 1, 0);
  CHECK(clip != nullptr);
  if (!clip) { pf_map_free(map); return; }
  CHECK(pf_map_paste_ex(map, clip, wx, wy, 1) >= 0);

  // No neighbouring pair may disagree about the corners it shares. That is the
  // signature of a tile chosen without regard to the one beside it.
  int seams = 0;
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      uint8_t here[4];
      pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y)), here);
      if (x + 1 < n) {
        uint8_t e[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, x + 1, y)), e);
        if (here[1] != e[0] || here[3] != e[2]) {
          if (seams < 4) std::printf("     seam east of (%d,%d)\n", x, y);
          seams++;
        }
      }
      if (y + 1 < n) {
        uint8_t s[4];
        pf::decode_tile(uint16_t(pf_map_tile_at(map, x, y + 1)), s);
        if (here[2] != s[0] || here[3] != s[1]) {
          if (seams < 4) std::printf("     seam south of (%d,%d)\n", x, y);
          seams++;
        }
      }
    }
  }

  // And the surroundings really did adapt. Forest is three hops from water, so
  // a legal join needs a band of ground and coast outside the fragment; a paste
  // that only rewrites its own footprint changes nothing out here.
  int outside = 0, reach = 0;
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      if (x >= wx && y >= wy && x < wx + side && y < wy + side) continue;
      if (before[size_t(y) * size_t(n) + size_t(x)] ==
          uint16_t(pf_map_tile_at(map, x, y))) {
        continue;
      }
      outside++;
      // How far out of the footprint the join reached, in tiles.
      const int dx = x < wx ? wx - x : (x >= wx + side ? x - (wx + side) + 1 : 0);
      const int dy = y < wy ? wy - y : (y >= wy + side ? y - (wy + side) + 1 : 0);
      reach = std::max(reach, std::max(dx, dy));
    }
  }
  std::printf("     %d disagreeing seams, %d tiles adapted outside the paste, "
              "reaching %d out\n", seams, outside, reach);
  CHECK_EQ(seams, 0);
  CHECK(outside > 0);
  // Beyond the one tile of slack the paste used to allow. Forest to water is
  // three hops, so the shore it needs cannot be drawn in a single ring however
  // well that ring is chosen — the fragment either keeps a hard edge or gives
  // up its own outermost tiles to make room.
  CHECK(reach >= 2);

  pf_clipboard_free(clip);
  pf_map_free(map);
}
