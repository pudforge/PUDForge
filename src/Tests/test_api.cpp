// The C ABI, exercised the way a front-end uses it
//
// See harness.hpp for the assertions, fixtures and registration.

#include "harness.hpp"

TEST_GROUP("api")

namespace pft {

TEST(c_api_smoke) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  CHECK(map != nullptr);
  CHECK(st == PF_OK);
  if (!map) return;

  CHECK_EQ(pf_map_width(map), 32);
  CHECK_EQ(pf_map_tileset(map), 0);
  CHECK(pf_map_set_description(map, "From C") == PF_OK);
  CHECK(std::string(pf_map_description(map)) == "From C");

  int idx = pf_map_add_unit(map, 4, 4, 0x4a, 0, 0);
  CHECK(idx >= 0);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_unit u;
  CHECK(pf_map_unit(map, 0, &u) == PF_OK);
  CHECK_EQ(int(u.type), 0x4a);
  CHECK_EQ(pf_map_unit_at(map, 4, 4), 0);
  CHECK(pf_map_move_unit(map, 0, 6, 6) == PF_OK);
  CHECK_EQ(pf_map_unit_at(map, 6, 6), 0);

  CHECK(pf_map_paint_terrain(map, 10, 10, PF_TERRAIN_WATER_LIGHT, 3) == PF_OK);
  CHECK(pf_map_rebuild_regions(map) > 0);

  size_t len = 0;
  uint8_t* bytes = pf_map_save(map, &len, &st);
  CHECK(bytes != nullptr);
  CHECK(len > 0);
  if (bytes) {
    pf_map* reopened = pf_map_open(bytes, len, &st);
    CHECK(reopened != nullptr);
    if (reopened) {
      CHECK_EQ(pf_map_unit_count(reopened), 1);
      CHECK(std::string(pf_map_description(reopened)) == "From C");
      pf_map_free(reopened);
    }
    pf_buffer_free(bytes);
  }

  // Bad arguments must be rejected, not crash.
  CHECK(pf_map_unit(map, 99, &u) == PF_ERR_OUT_OF_RANGE);
  CHECK(pf_map_set_owner(map, 99, 5) == PF_ERR_OUT_OF_RANGE);
  CHECK_EQ(pf_map_add_unit(map, 0, 0, 999, 0, 0), -1);
  CHECK(pf_map_open(nullptr, 0, &st) == nullptr);
  CHECK(std::string(pf_unit_name(0)) == "Footman");
  CHECK(pf_unit_name(-1) == nullptr);

  pf_map_free(map);
  pf_map_free(nullptr);  // must be a no-op
}

/**
 * Sharding must lose nothing.
 *
 * CTest registers the corpus group as several independent `--shard I/N` runs, so
 * "all tests ran" is no longer something any single process can observe. If the
 * shards ever stopped tiling the list — an off-by-one, or a shard count in
 * CMakeLists.txt that no longer matches the divisor — tests would quietly go
 * unrun and the suite would still report success. This is the check that they
 * partition: every index exactly once, no gaps, no repeats.
 */
TEST(shards_tile_the_test_list) {
  for (int shards = 1; shards <= 5; shards++) {
    for (size_t count = 0; count <= 12; count++) {
      std::vector<int> seen(count, 0);
      size_t total = 0;
      for (int shard = 0; shard < shards; shard++) {
        for (size_t i : shard_indices(count, shard, shards)) {
          CHECK(i < count);
          if (i < count) seen[i]++;
          total++;
        }
      }
      CHECK_EQ(total, count);
      for (size_t i = 0; i < count; i++) CHECK_EQ(seen[i], 1);
      // Work is spread, not dumped on one runner: with more tests than shards
      // every shard gets some, and no two differ by more than one test.
      if (count >= size_t(shards)) {
        const size_t small = shard_indices(count, shards - 1, shards).size();
        const size_t large = shard_indices(count, 0, shards).size();
        CHECK(small > 0);
        CHECK(large - small <= 1);
      }
    }
  }

  // A nonsense shard spec selects nothing rather than reading out of bounds.
  CHECK(shard_indices(10, 0, 0).empty());
  CHECK(shard_indices(10, -1, 3).empty());
  CHECK(shard_indices(10, 3, 3).empty());
}
}  // namespace pft
