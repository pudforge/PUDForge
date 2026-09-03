// Units: tables, classification, placement rules, validation
//
// See harness.hpp for the assertions, fixtures and registration.

#include "harness.hpp"

#include <cstdlib>   // free, for the bytes pf_data_source_read hands back

#include "tbl.hpp"   // where the unit block of the game's string table ends

TEST_GROUP("units")

namespace pft {

TEST(start_gold_and_landmasses_match_the_shipped_melee_maps) {
  // These two are measurements, not rules, so the test pins the measurement:
  // Blizzard's own multiplayer maps must not trip the "no gold in reach"
  // warning, since that is what the 40-tile reach was calibrated against.
  if (g_shipped.empty()) { skip("no corpus"); return; }

  int checked = 0, tripped = 0, islands = 0;
  for (const std::string& path : g_shipped) {
    if (path.find("origmaps/multi") == std::string::npos) continue;
    pf_status st = PF_OK;
    pf_map* map = pf_map_open_file(path.c_str(), &st);
    if (!map) continue;
    checked++;

    if (pf_map_start_landmasses(map) > 1) islands++;
    const int count = pf_map_validate(map, nullptr, 0);
    std::vector<pf_issue> issues(size_t(count > 0 ? count : 1));
    pf_map_validate(map, issues.data(), count);
    for (int i = 0; i < count; i++) {
      if (issues[size_t(i)].code == PF_ISSUE_START_NO_GOLD) { tripped++; break; }
    }
    pf_map_free(map);
  }
  if (!checked) { skip("no multiplayer maps in the corpus"); return; }
  std::printf("     %d Blizzard melee maps, %d islands, %d trip the gold warning\n",
              checked, islands, tripped);
  // Zero false positives is the calibration. If this ever fires, the reach is
  // wrong rather than the map.
  CHECK_EQ(tripped, 0);
  // And the island maps are real: if this went to zero the landmass count has
  // stopped working rather than the maps having changed.
  CHECK(islands > 0);
}
TEST(a_hall_keeps_three_tiles_clear_of_a_gold_mine) {
  // A hall against a mine cannot be worked; the peasants need a lane. The
  // number comes from where the maps put them - a gap of one or two tiles
  // occurs in no map by any author, and three is the mode by a factor of six.
  // See overrides/hall_clearance.cpp.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  // Clear ground everywhere, then one mine at (10, 10). A mine is 3x3.
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) pf_map_paint_terrain_raw(map, x, y, PF_TERRAIN_GROUND_LIGHT, 1);
  }
  CHECK(pf_map_add_unit(map, 10, 10, 92, 15, 16) >= 0);

  int mw = 1, mh = 1;
  pf_map_unit_footprint(map, 92, &mw, &mh);
  int hw = 1, hh = 1;
  pf_map_unit_footprint(map, 74, &hw, &hh);

  // Walk the hall along the row through the mine and check where it is refused.
  for (int x = 0; x + hw <= 32; x++) {
    const int gap = std::max(x - (10 + mw), 10 - (x + hw));
    const int got = pf_map_placement_check(map, x, 10, 74);
    if (gap < 3) CHECK_EQ(got, PF_PLACE_TOO_NEAR_MINE);
    else CHECK_EQ(got, PF_PLACE_OK);
  }
  // Diagonally too: the clearance is in every direction, not along an axis.
  CHECK_EQ(pf_map_placement_check(map, 14, 14, 74), PF_PLACE_TOO_NEAR_MINE);
  CHECK_EQ(pf_map_placement_check(map, 16, 16, 74), PF_PLACE_OK);

  // Every other building is free to sit against a mine, which the corpus also
  // says: farms and barracks do it hundreds of times.
  CHECK_EQ(pf_map_placement_check(map, 13, 10, 0x3a), PF_PLACE_OK);   // Farm
  pf_map_free(map);
}
TEST(constants_match_the_format) {
  CHECK(std::string(pf::kUnits[0x00].name) == "Footman");
  CHECK(std::string(pf::kUnits[0x5c].name) == "Gold Mine");
  CHECK(std::string(pf::kUnits[0x4a].name) == "Town Hall");
  CHECK(pf::kUnits[0x22].unused);   // one of the five dead slots
  CHECK(!pf::kUnits[0x00].unused);
  CHECK(std::string(pf::kUpgrades[0]) == "Sword 1");
  CHECK(std::string(pf::kPlayerNames[0]) == "Player 1 (Red)");
}
TEST(unit_icons_agree_with_the_upgrade_table) {
  // Four entries are not opinions: UGRD carries an icon index per upgrade, and
  // the four that train a unit point at that unit's own icon. If the hand
  // written table ever drifts from them, it is the table that is wrong.
  if (!have_corpus()) { skip("no shipped maps"); return; }

  pf_map* map = nullptr;
  for (const std::string& path : g_corpus) {
    pf_map* candidate = pf_map_open_file(path.c_str(), nullptr);
    if (candidate && pf_map_has_upgrade_data(candidate)) { map = candidate; break; }
    if (candidate) pf_map_free(candidate);
  }
  if (!map) { skip("no map with UGRD"); return; }

  int icon_field = -1;
  for (int i = 0; i < pf_ugrd_field_count(); i++) {
    if (std::string(pf_ugrd_field_name(i)) == "icon") icon_field = i;
  }
  CHECK(icon_field >= 0);

  // upgrade id -> the unit it trains
  const struct { int upgrade; int unit; } trains[] = {
      {24, 0x12},   // Train Rangers      -> Ranger
      {28, 0x13},   // Train Berserkers   -> Berserker
      {33, 0x0c},   // Train Paladins     -> Paladin
      {32, 0x0d},   // Train Ogre-Mages   -> Ogre-Mage
  };
  for (const auto& t : trains) {
    const int from_ugrd = int(pf_map_upgrade_field(map, icon_field, t.upgrade));
    CHECK_EQ(pf_unit_icon(t.unit), from_ugrd);
  }
  pf_map_free(map);

  // Through frame 31 the sheet alternates strictly, human on the even frame
  // and orc on the odd one, which is the rule the land and sea block was read
  // with. An entry that breaks it is in the wrong row. Past 31 the game stops
  // alternating — Cho'gall is orc on an even frame — so the rule stops too,
  // rather than being weakened to fit.
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    const int frame = pf_unit_icon(unit);
    if (frame < 0 || frame >= 32) continue;
    const char race = pf_unit_race(unit);
    if (race == 'h') CHECK_EQ(frame % 2, 0);
    if (race == 'o') CHECK_EQ(frame % 2, 1);
  }

  // The five dead slots crash the game, so pointing artwork at one would only
  // make it easier to place.
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    if (pf_unit_is_unused(unit)) CHECK_EQ(pf_unit_icon(unit), -1);
  }
}
TEST(race_counterparts_pair_up_both_ways) {
  // Every pairing is human to orc, reads the same both ways round, and is its
  // own inverse. A table that fails any of those turns a race swap into data
  // loss the first time somebody runs it twice.
  int pairs = 0;
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    const int other = pf_unit_counterpart(unit);
    if (other < 0) continue;
    pairs++;
    CHECK(other >= 0 && other < pf::kUnitCount);
    CHECK_EQ(pf_unit_counterpart(other), unit);
    CHECK(unit != other);
    // One of each. Neutral units have no side to swap to.
    const char a = pf_unit_race(unit), b = pf_unit_race(other);
    CHECK((a == 'h' && b == 'o') || (a == 'o' && b == 'h'));
    // Counterparts stand in for each other, so they must occupy the same
    // ground: swapping a shipyard for a barracks would strand it on water.
    CHECK_EQ(pf_unit_category(unit), pf_unit_category(other));
  }
  CHECK_EQ(pairs, pf_unit_counterpart_count() * 2);

  // Heroes are named characters, not roles. The ids alternate human and orc,
  // so a rule derived from the numbering would pair Turalyon with the Eye of
  // Kilrogg; this table leaves every one of them alone.
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    if (pf_unit_category(unit) == PF_CATEGORY_HERO) {
      CHECK_EQ(pf_unit_counterpart(unit), -1);
    }
  }

  // The pairs everyone knows, spelled out so a regenerated table is still read
  // by a person before it lands.
  CHECK_EQ(pf_unit_counterpart(0x00), 0x01);  // Footman   -> Grunt
  CHECK_EQ(pf_unit_counterpart(0x03), 0x02);  // Peon      -> Peasant
  CHECK_EQ(pf_unit_counterpart(0x4a), 0x4b);  // Town Hall -> Great Hall
  CHECK_EQ(pf_unit_counterpart(0x5b), 0x5a);  // Fortress  -> Castle
  CHECK_EQ(pf_unit_counterpart(0x67), 0x68);  // Human Wall -> Orc Wall
  CHECK_EQ(pf_unit_counterpart(0x5c), -1);    // a gold mine belongs to nobody
}
TEST(opt_in_units_match_puddrafts_warning_submenu) {
  // PUDDraft put exactly twelve units behind "Unused/Special Units" and a
  // "*** WARNING: Click here first ***" item — five dead slots, then walls as
  // units, corpses and rubble. Transcribed from reference/dfm/TMAPEDFORM.dfm,
  // so this is a check against the tool these maps were made with rather than
  // against an opinion.
  // Plus the two campaign workers, which PUDDraft listed with the ordinary
  // units and this core does not: a palette offering two peasants that differ
  // only in a name is a trap. Deliberate divergence, recorded here so it
  // cannot drift into an accident.
  const int expected[] = {16, 17, 34, 36, 37, 48, 54, 103, 104, 105, 106, 107, 108, 109};
  CHECK_EQ(pf_unit_needs_opt_in_count(), int(sizeof(expected) / sizeof(expected[0])));

  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    bool wanted = false;
    for (int id : expected) wanted = wanted || id == unit;
    CHECK_EQ(pf_unit_needs_opt_in(unit) != 0, wanted);
  }

  // The five dead slots are a subset: anything unused must also be opt-in, or a
  // palette hiding one set would still offer the other.
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    if (pf_unit_is_unused(unit)) CHECK(pf_unit_needs_opt_in(unit));
  }

  // Opt-in governs the palette, never the format. Every one of them still has
  // a name and still round-trips, which is what a map already holding one
  // depends on.
  for (int id : expected) CHECK(pf_unit_name(id) != nullptr);

  // No two units share a frame, and every frame is inside the artwork.
  int seen[256] = {};
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    const int frame = pf_unit_icon(unit);
    if (frame < 0) continue;
    CHECK(frame >= 0 && frame < 196);
    CHECK_EQ(seen[frame], 0);
    seen[frame] = 1;
  }
  std::printf("     %d of %d units have an icon\n", pf_unit_icon_count(), pf::kUnitCount);

  // The artwork the frames index into really has them.
  if (have_art()) {
    char path[128];
    CHECK(pf_portrait_path(0, path, sizeof(path)) > 0);
    pf::DataSource source;
    if (source.add_directory(g_root + "/reference/app") > 0) {
      std::vector<uint8_t> bytes;
      CHECK(source.read(path, bytes));
      pf_status st = PF_OK;
      pf_sprite* icons = pf_sprite_open_memory(bytes.data(), bytes.size(), &st);
      CHECK(icons != nullptr);
      if (icons) {
        CHECK_EQ(pf_sprite_frame_count(icons), 196);
        CHECK_EQ(pf_sprite_width(icons), 46);
        CHECK_EQ(pf_sprite_height(icons), 38);
        pf_sprite_free(icons);
      }
    }
  }
}
TEST(a_player_has_only_one_start_location) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  CHECK(pf_map_add_unit(map, 4, 4, 94, 0, 0) >= 0);
  CHECK_EQ(pf_map_unit_count(map), 1);

  // Placing another for the same player moves it rather than adding a second.
  CHECK(pf_map_add_unit(map, 20, 20, 94, 0, 0) >= 0);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_unit u;
  pf_map_unit(map, 0, &u);
  CHECK_EQ(int(u.x), 20);
  CHECK_EQ(int(u.y), 20);

  // The orc marker counts as the same thing: a slot has one start, whichever
  // race it is for.
  CHECK(pf_map_add_unit(map, 8, 24, 95, 0, 0) >= 0);
  CHECK_EQ(pf_map_unit_count(map), 1);
  pf_map_unit(map, 0, &u);
  CHECK_EQ(int(u.type), 95);

  // Another player is untouched, and ordinary units are never displaced.
  CHECK(pf_map_add_unit(map, 12, 12, 94, 1, 0) >= 0);
  CHECK(pf_map_add_unit(map, 16, 16, 0, 0, 0) >= 0);
  CHECK(pf_map_add_unit(map, 18, 16, 0, 0, 0) >= 0);
  CHECK_EQ(pf_map_unit_count(map), 4);

  // And a map that already has extras keeps them until one is placed: this
  // rule is about what the editor creates, not about rewriting what it opens.
  pf_map_free(map);
}
TEST(start_locations_fill_the_active_slots) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(64, 64, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  for (int p = 0; p < 16; p++) pf_map_set_owner(map, p, PF_OWNER_NOBODY);
  pf_map_set_owner(map, 0, PF_OWNER_HUMAN);
  pf_map_set_owner(map, 1, PF_OWNER_COMPUTER);
  pf_map_set_owner(map, 2, PF_OWNER_PASSIVE_COMPUTER);
  pf_map_set_race(map, 1, PF_RACE_ORC);

  CHECK_EQ(pf_map_place_start_locations(map, PF_MIRROR_NONE), 3);
  CHECK_EQ(pf_map_unit_count(map), 3);

  // The unit follows the player's race, and each active slot got exactly one.
  int per_owner[16] = {};
  std::vector<std::pair<int, int>> at;
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit u;
    pf_map_unit(map, i, &u);
    CHECK(u.type == 94 || u.type == 95);
    if (u.owner == 1) CHECK_EQ(int(u.type), 95);
    else CHECK_EQ(int(u.type), 94);
    per_owner[u.owner]++;
    at.push_back({int(u.x), int(u.y)});
  }
  for (int p = 0; p < 3; p++) CHECK_EQ(per_owner[p], 1);

  // They are spread out rather than piled in one corner: a start location
  // beside another is worse than useless on a multiplayer map.
  for (size_t i = 0; i < at.size(); i++) {
    for (size_t j = i + 1; j < at.size(); j++) {
      const int dx = at[i].first - at[j].first;
      const int dy = at[i].second - at[j].second;
      CHECK(dx * dx + dy * dy > 400);        // more than 20 tiles apart
    }
  }

  // Running it again is a no-op: it fills gaps, it does not rearrange.
  CHECK_EQ(pf_map_place_start_locations(map, PF_MIRROR_NONE), 0);
  CHECK_EQ(pf_map_unit_count(map), 3);
  pf_map_free(map);
}
TEST(unit_classification_is_one_answer) {
  // These used to be four separate lists in the web client, and "a start
  // location is type 94 or 95" was written in three places. One answer now,
  // which is what stops the macOS client growing a fifth.
  CHECK_EQ(pf_unit_category(0x00), PF_CATEGORY_LAND);      // Footman
  CHECK_EQ(pf_unit_category(0x28), PF_CATEGORY_AIR);       // Flying Machine
  CHECK_EQ(pf_unit_category(0x1a), PF_CATEGORY_WATER);     // Oil Tanker
  CHECK_EQ(pf_unit_category(0x3a), PF_CATEGORY_BUILDING);  // Farm
  // Only five units carry the hero flag — Cho'gall, Lothar, Gul'dan, Uther and
  // Zuljin — so the flag alone files the other ten named characters in with
  // the footmen. overrides/named_heroes.cpp adds them back.
  CHECK_EQ(pf_unit_category(0x32), PF_CATEGORY_HERO);      // Lothar, flagged
  CHECK_EQ(pf_unit_category(0x14), PF_CATEGORY_HERO);      // Alleria, listed
  CHECK_EQ(pf_unit_named_hero_count(), 10);
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    const bool hero = pf_unit_category(unit) == PF_CATEGORY_HERO;
    CHECK_EQ(pf_unit_in_group(unit, PF_GROUP_HEROES) != 0, hero);
  }
  // A gold mine carries the building flag, and is scenery all the same.
  CHECK_EQ(pf_unit_category(0x5c), PF_CATEGORY_SPECIAL);
  CHECK_EQ(pf_unit_category(0x5d), PF_CATEGORY_SPECIAL);   // Oil Patch
  CHECK_EQ(pf_unit_category(-1), PF_CATEGORY_SPECIAL);

  // The drawing question has a different answer on purpose: on the map a mine
  // looks built, so a view filter showing buildings shows it.
  CHECK_EQ(pf_unit_draw_class(0x5c), 2);
  CHECK_EQ(pf_unit_draw_class(0x00), 0);
  CHECK_EQ(pf_unit_draw_class(0x28), 1);

  CHECK(pf_unit_in_group(0x3a, PF_GROUP_BUILDINGS));
  CHECK(!pf_unit_in_group(0x5c, PF_GROUP_BUILDINGS));   // a mine is not a build
  CHECK(pf_unit_in_group(0x5c, PF_GROUP_RESOURCES));
  CHECK(pf_unit_in_group(0x00, PF_GROUP_LAND));
  CHECK(!pf_unit_in_group(0x39, PF_GROUP_LAND));        // Critter is scenery
  CHECK(pf_unit_in_group(0x39, PF_GROUP_CRITTERS));
  CHECK(pf_unit_in_group(94, PF_GROUP_START_LOCATIONS));
  CHECK(pf_unit_in_group(95, PF_GROUP_START_LOCATIONS));
  CHECK(!pf_unit_in_group(0x00, PF_GROUP_START_LOCATIONS));
  CHECK(pf_unit_group_name(PF_GROUP_BUILDINGS) != nullptr);
  CHECK(pf_unit_group_name(PF_GROUP_COUNT) == nullptr);

  CHECK(pf_terrain_is_wall(PF_TERRAIN_WALL_HUMAN));
  CHECK(pf_terrain_is_wall(PF_TERRAIN_WALL_ORC));
  CHECK(!pf_terrain_is_wall(PF_TERRAIN_FOREST));
}
TEST(sprite_paths) {
  CHECK(pf::sprite_path_for(0x4a, 0) == "human/thall");
  CHECK(pf::sprite_path_for(0x4a, 1) == "human/s_thall");
  CHECK(pf::sprite_path_for(0x39, 0) == "monster/sheep");   // critter by tileset
  CHECK(pf::sprite_path_for(0x39, 3) == "monster/hellhog");
  CHECK(pf::sprite_path_for(0x22, 0).empty());              // unused slot

  // The one pair that reads backwards and is not. `dtower` is the *damned*
  // tower, so it is the Temple of the Damned and not a second altar — the
  // game's own naming pairs it with the Mage Tower's `wtower`, exactly as the
  // building sounds pair `dthtower` with `wzrdtowr`. The Altar of Storms is
  // the Church's opposite number and takes the worship-named file. These two
  // were crossed, and a swap here is invisible until somebody who knows the
  // game looks at the map.
  CHECK(pf::sprite_path_for(0x3f, 0) == "orc/temple");   // Altar of Storms
  CHECK(pf::sprite_path_for(0x51, 0) == "orc/dtower");   // Temple of the Damned
}

TEST(counterpart_buildings_stay_in_order_on_the_icon_sheet) {
  // The command-button sheet lays the buildings out in counterpart order, human
  // first: lumber mills at 44/45, blacksmiths at 46/47, and so on. Twice it
  // interleaves two pairs rather than one — the guard and cannon towers run
  // 75, 76, 77, 78 as human, human, orc, orc — so the orc frame is one or two
  // past its human, never before it and never further off.
  //
  // That is enough regularity to catch a crossed pair, which is what this is
  // for. The Church and the Altar of Storms sat at 62 and 65 with the Mage
  // Tower and the Temple of the Damned crossed between them: one pair three
  // frames apart, the other with the orc *before* the human. Both are shapes
  // no correct pair makes.
  int checked = 0;
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    if (pf_unit_category(unit) != PF_CATEGORY_BUILDING) continue;
    if (pf_unit_race(unit) != 'h') continue;
    const int other = pf_unit_counterpart(unit);
    if (other < 0) continue;
    const int human = pf_unit_icon(unit), orc = pf_unit_icon(other);
    if (human < 0 || orc < 0) continue;
    checked++;
    const bool ordered = orc > human && orc - human <= 2;
    if (!ordered) {
      std::printf("     %s at %d but %s at %d\n", pf_unit_name(unit), human,
                  pf_unit_name(other), orc);
    }
    CHECK(ordered);
  }
  std::printf("     %d paired buildings on the icon sheet\n", checked);
  CHECK(checked > 15);
}
TEST(a_resource_says_which_one_it_is_and_what_it_starts_with) {
  // Both clients used to work this out from the unit id — `type == 0x5c ? gold
  // : oil` — which is an opinion about the game held in a window, and wrong
  // for the oil well, which carries oil without being an oil patch.
  CHECK_EQ(pf_unit_resource(0x5c), int(PF_RESOURCE_GOLD));   // Gold Mine
  CHECK_EQ(pf_unit_resource(0x5d), int(PF_RESOURCE_OIL));    // Oil Patch
  CHECK_EQ(pf_unit_resource(0x56), int(PF_RESOURCE_OIL));    // Human Oil Well
  CHECK_EQ(pf_unit_resource(0x57), int(PF_RESOURCE_OIL));    // Orc Oil Well
  CHECK_EQ(pf_unit_resource(0x00), int(PF_RESOURCE_NONE));   // Footman
  CHECK_EQ(pf_unit_resource(0x3a), int(PF_RESOURCE_NONE));   // Farm

  // Every unit whose value is an amount is a resource of one kind or the
  // other, and nothing else is. The two answers are derived from the same
  // flags, so a unit that says one and not the other is a contradiction.
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    const bool amount = pf_unit_value_is_amount(unit) != 0;
    CHECK_EQ(amount, pf_unit_resource(unit) != PF_RESOURCE_NONE);
  }

  // The format keeps the amount in units of 2,500, which is what the game
  // shows: 16 is 40,000 gold.
  CHECK_EQ(pf_resource_amount(16), int64_t(40000));
  CHECK_EQ(pf_resource_amount(8), int64_t(20000));
  CHECK_EQ(pf_resource_amount(0), int64_t(0));

  // Back the other way, which is what an editor needs when somebody types an
  // amount into a field. A round trip is exact.
  CHECK_EQ(pf_resource_value(40000), 16);
  CHECK_EQ(pf_resource_value(20000), 8);
  CHECK_EQ(pf_resource_value(0), 0);
  for (int value = 0; value <= 0xFFFF; value += 977) {
    CHECK_EQ(pf_resource_value(pf_resource_amount(value)), value);
  }

  // Anything else rounds to the nearest step rather than truncating, because
  // the format cannot keep 41,000 and losing a thousand gold silently is worse
  // than gaining one and being told.
  CHECK_EQ(pf_resource_value(41000), 16);   // 40,000
  CHECK_EQ(pf_resource_value(41300), 17);   // 42,500
  CHECK_EQ(pf_resource_value(1250), 1);     // exactly half a step, up
  CHECK_EQ(pf_resource_value(1249), 0);

  // Neither end can leave the field: the value is sixteen bits, and a negative
  // amount is not a thing a mine can hold.
  CHECK_EQ(pf_resource_value(-1), 0);
  CHECK_EQ(pf_resource_value(int64_t(1) << 40), 0xFFFF);

  // And what the editor fills a freshly placed one with.
  CHECK_EQ(pf_unit_default_value(0x5c), 16);   // 40,000 gold
  CHECK_EQ(pf_unit_default_value(0x5d), 8);    // 20,000 oil
  // "0 passive 1 active" is the format specification's wording, so a footman
  // the editor places is active. It used to be placed at 0, which made every
  // unit PUDForge ever put down passive.
  CHECK_EQ(pf_unit_default_value(0x00), 1);
}

TEST(unit_sounds_name_files_the_game_actually_ships) {
  // The mapping from unit to voice is hand-written, and hand-written means it
  // can be wrong in a way that only shows up as silence. So every path the
  // table can produce is looked up in the archives: a typo in a folder or a
  // prefix fails here rather than the first time somebody places a Knight.
  auto path = [](int unit, int kind, int salt) {
    char buf[128] = {};
    pf_unit_sound_path(unit, kind, salt, buf, int(sizeof(buf)));
    return std::string(buf);
  };

  // The shapes the table turns on: a voice of its own, a voice borrowed from
  // another unit, and a building, which has a working noise rather than speech.
  CHECK(path(0x06, PF_SOUND_READY, 0) == "gamesfx/knight/knready.wav");
  CHECK(path(0x12, PF_SOUND_READY, 0) == "gamesfx/elves/eready.wav");  // ranger is an archer
  CHECK(path(0x3a, PF_SOUND_READY, 0) == "gamesfx/bldg/hfarm.wav");
  // Everything the game left silent takes the interface click rather than
  // nothing, so no edit is inaudible: a wall has no line to say, a barracks
  // was given no noise, and neither is ever asked to speak.
  CHECK(path(0x67, PF_SOUND_SELECTED, 0) == "sfx/button.wav");   // a wall
  CHECK(path(0x3c, PF_SOUND_READY, 0) == "sfx/button.wav");      // human barracks
  CHECK(path(0x3a, PF_SOUND_SELECTED, 0) == "sfx/button.wav");   // a farm, selected
  // The salt walks the selection lines rather than landing on one. Which salt
  // is the client's business — it draws a fresh one per click — but the
  // mapping from salt to line stays a pure function, which is what lets this
  // be a test at all.
  CHECK(path(0x00, PF_SOUND_SELECTED, 0) == "gamesfx/human/hwhat1.wav");
  CHECK(path(0x00, PF_SOUND_SELECTED, 5) == "gamesfx/human/hwhat6.wav");
  CHECK(path(0x00, PF_SOUND_SELECTED, 6) == "gamesfx/human/hwhat1.wav");   // wraps

  pf_data_source* source = pf_data_source_create();
  if (!source) { CHECK(false); return; }
  if (pf_data_source_add_directory(source, (g_root + "/reference/app").c_str()) == 0) {
    pf_data_source_free(source);
    skip("no MPQ archives");
    return;
  }

  int named = 0, missing = 0;
  for (int unit = 0; unit < PF_UNIT_COUNT; unit++) {
    if (pf_unit_is_unused(unit)) continue;
    for (int kind = 0; kind <= PF_SOUND_SELECTED; kind++) {
      // Every selection line, not just the one salt 0 picks: the count in the
      // table is the other half of the guess, and a folder with three lines
      // described as having four is silent one click in four.
      for (int salt = 0; salt < 8; salt++) {
        const std::string want = path(unit, kind, salt);
        if (want.empty()) continue;
        named++;
        size_t length = 0;
        uint8_t* bytes = pf_data_source_read(source, want.c_str(), &length);
        if (!bytes || length == 0) {
          if (missing < 8) {
            std::printf("     %s (unit %d, kind %d) is not in the archives\n",
                        want.c_str(), unit, kind);
          }
          missing++;
        }
        free(bytes);
      }
    }
  }
  std::printf("     %d unit sound paths named, %d missing\n", named, missing);
  CHECK(named > 500);
  CHECK_EQ(missing, 0);
  pf_data_source_free(source);
}

TEST(default_unit_data_gives_real_footprints) {
  // A map with no UDTA used to report every building as 1x1, because the
  // retail table lived only in the game. It travels with the core now.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  if (!map) { CHECK(false); return; }
  CHECK_EQ(pf_map_has_unit_data(map), 0);   // no UDTA of its own

  struct { int type; int w; int h; const char* what; } expected[] = {
      {0, 1, 1, "footman"}, {58, 2, 2, "farm"},
      {60, 3, 3, "human barracks"}, {92, 3, 3, "gold mine"},
  };
  for (const auto& e : expected) {
    int w = 0, h = 0;
    pf_map_unit_footprint(map, e.type, &w, &h);
    CHECK_EQ(w, e.w);
    CHECK_EQ(h, e.h);
  }
  pf_map_free(map);
}
TEST(units_cannot_be_placed_on_terrain_they_cannot_stand_on) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  if (!map) { CHECK(false); return; }

  // A lake in the middle; the rest stays grass.
  for (int y = 10; y < 20; y++) {
    for (int x = 10; x < 20; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
  }

  // Domains come from the retail flags.
  CHECK_EQ(pf_unit_domain(0), int(PF_DOMAIN_LAND));    // footman
  CHECK_EQ(pf_unit_domain(26), int(PF_DOMAIN_WATER));  // human tanker
  CHECK_EQ(pf_unit_domain(40), int(PF_DOMAIN_AIR));    // gryphon rider
  CHECK_EQ(pf_unit_domain(92), int(PF_DOMAIN_LAND));   // gold mine is mined on land
  CHECK_EQ(pf_unit_domain(93), int(PF_DOMAIN_WATER));  // oil patch is at sea
  CHECK_EQ(pf_unit_domain(86), int(PF_DOMAIN_WATER));  // human oil well
  CHECK_EQ(pf_unit_domain(87), int(PF_DOMAIN_WATER));  // orc oil well

  // Ground unit: fine on grass, refused in the lake.
  CHECK_EQ(pf_map_placement_check(map, 2, 2, 0), int(PF_PLACE_OK));
  CHECK_EQ(pf_map_placement_check(map, 15, 15, 0), int(PF_PLACE_NEEDS_LAND));
  CHECK(pf_map_add_unit(map, 2, 2, 0, 0, 0) >= 0);
  CHECK_EQ(pf_map_add_unit(map, 15, 15, 0, 0, 0), -1);

  // Ship: the mirror image. On an even tile, because a ship covers 2x2 and is
  // laid on a 2x2 grid — 15,15 is the tile between two of its blocks.
  CHECK_EQ(pf_map_placement_check(map, 14, 14, 26), int(PF_PLACE_OK));
  CHECK_EQ(pf_map_placement_check(map, 2, 2, 26), int(PF_PLACE_NEEDS_WATER));
  CHECK_EQ(pf_map_placement_check(map, 15, 15, 26), int(PF_PLACE_OFF_GRID));

  // Flying units go anywhere, on the same grid.
  CHECK_EQ(pf_map_placement_check(map, 14, 14, 40), int(PF_PLACE_OK));
  CHECK_EQ(pf_map_placement_check(map, 2, 2, 40), int(PF_PLACE_OK));
  CHECK_EQ(pf_map_placement_check(map, 2, 2, 92), int(PF_PLACE_OK));
  CHECK(pf_map_placement_check(map, 15, 15, 92) != PF_PLACE_OK);   // mine in a lake
  // An oil well goes on the odd tiles of the two-tile grid, so both of these
  // aim at odd ones: an even tile answers PF_PLACE_OFF_GRID first and says
  // nothing about the terrain, which is what this test is about.
  CHECK_EQ(pf_map_placement_check(map, 15, 15, 86), int(PF_PLACE_OK));
  CHECK_EQ(pf_map_placement_check(map, 3, 3, 86), int(PF_PLACE_NEEDS_WATER));
  CHECK_EQ(pf_map_placement_check(map, 2, 2, 86), int(PF_PLACE_OFF_GRID));

  // A building needs buildable ground under every tile, which is stricter
  // than "not water": no building in any of the 529 shipped maps sits on a
  // coast quadrant, so the shoreline is not somewhere you may build.
  CHECK_EQ(pf_unit_domain(60), int(PF_DOMAIN_LAND));
  CHECK_EQ(pf_map_placement_check(map, 9, 9, 60), int(PF_PLACE_NEEDS_GROUND));
  CHECK_EQ(pf_map_placement_check(map, 2, 2, 60), int(PF_PLACE_OK));

  // A footman may stand on the shore; a barracks may not be built there.
  pf_map_paint_terrain(map, 24, 24, PF_TERRAIN_COAST_LIGHT, 3);
  CHECK_EQ(pf_map_placement_check(map, 24, 24, 0), int(PF_PLACE_OK));
  CHECK_EQ(pf_map_placement_check(map, 24, 24, 60), int(PF_PLACE_NEEDS_GROUND));

  // A wall is not buildable. It is a thing standing on the tile, not a kind of
  // ground, and the game will not put a building through one — so neither will
  // this. One wall anywhere under the footprint is enough to refuse the lot.
  pf_map_paint_wall(map, 5, 5, 1, 1);
  CHECK(pf_terrain_is_wall(
      pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 5, 5)))));
  // Refused for needing ground, the same way forest and rock are: a building's
  // problem is always what it is standing on, never what is in its way. See
  // ground_units_stay_off_blocking_terrain, which pins that distinction.
  CHECK_EQ(pf_map_placement_check(map, 4, 4, 60), int(PF_PLACE_NEEDS_GROUND));
  CHECK_EQ(pf_map_add_unit(map, 4, 4, 60, 0, 0), -1);
  // The wall is still there: a refused placement changes nothing.
  CHECK(pf_terrain_is_wall(
      pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(map, 5, 5)))));
  // Clear of it, the same barracks is fine, so it really is the wall doing it.
  CHECK_EQ(pf_map_placement_check(map, 1, 1, 60), int(PF_PLACE_OK));

  // The escape hatch works, and is off by default.
  CHECK_EQ(pf_map_allows_illegal_placement(map), 0);
  pf_map_set_allow_illegal_placement(map, 1);
  CHECK(pf_map_add_unit(map, 15, 15, 0, 0, 0) >= 0);
  pf_map_set_allow_illegal_placement(map, 0);

  pf_map_free(map);
}
TEST(ground_units_stay_off_blocking_terrain) {
  // Nothing walks through a forest, a cliff or a wall. In the 529 shipped maps
  // not one of 19,677 units stands on any of the three unless it flies.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(24, 24, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  const int footman = 0x00, dragon = 0x2b, farm = 0x3a;
  CHECK_EQ(pf_map_placement_check(map, 5, 5, footman), PF_PLACE_OK);

  for (const auto& kind : {PF_TERRAIN_FOREST, PF_TERRAIN_MOUNTAIN}) {
    pf_map_paint_terrain(map, 5, 5, kind, 3);
    CHECK_EQ(pf_map_placement_check(map, 5, 5, footman), PF_PLACE_BLOCKED);
    // Flying over it is fine, which is the whole of the exception. On an even
    // tile: a dragon covers 2x2 and is laid on a 2x2 grid like every flier.
    CHECK_EQ(pf_map_placement_check(map, 4, 4, dragon), PF_PLACE_OK);
    // A building is refused too, but for needing ground rather than for this.
    CHECK_EQ(pf_map_placement_check(map, 5, 5, farm), PF_PLACE_NEEDS_GROUND);
    pf_map_paint_terrain(map, 5, 5, PF_TERRAIN_GROUND_LIGHT, 5);
  }

  pf_map_paint_terrain(map, 12, 12, PF_TERRAIN_WALL_HUMAN, 1);
  CHECK_EQ(pf_map_placement_check(map, 12, 12, footman), PF_PLACE_BLOCKED);
  CHECK_EQ(pf_map_placement_check(map, 12, 12, dragon), PF_PLACE_OK);

  // A footman may still stand on the shore; only the three blockers are new.
  pf_map_paint_terrain(map, 18, 18, PF_TERRAIN_COAST_LIGHT, 3);
  CHECK_EQ(pf_map_placement_check(map, 18, 18, footman), PF_PLACE_OK);

  pf_map_free(map);
}
TEST(validation_catches_a_broken_map) {
  // A map with players but no start locations must be reported.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  pf_map_set_owner(map, 0, PF_OWNER_HUMAN);
  pf_map_set_owner(map, 1, PF_OWNER_COMPUTER);

  std::vector<pf_issue> issues(16);
  int total = pf_map_validate(map, issues.data(), int(issues.size()));
  CHECK(total >= 2);

  int missing_starts = 0, no_gold = 0;
  for (int i = 0; i < total && i < int(issues.size()); i++) {
    if (issues[i].code == PF_ISSUE_NO_START_LOCATION) missing_starts++;
    if (issues[i].code == PF_ISSUE_NO_RESOURCES) no_gold++;
  }
  CHECK_EQ(missing_starts, 2);   // one per active player
  CHECK_EQ(no_gold, 1);
  CHECK_EQ(int(issues[0].severity), int(PF_SEVERITY_ERROR));  // errors sort first

  // Give player 1 a start location and the error for that slot goes away.
  CHECK(pf_map_add_unit(map, 4, 4, 94, 0, 0) >= 0);
  total = pf_map_validate(map, issues.data(), int(issues.size()));
  missing_starts = 0;
  for (int i = 0; i < total && i < int(issues.size()); i++) {
    if (issues[i].code == PF_ISSUE_NO_START_LOCATION) missing_starts++;
  }
  CHECK_EQ(missing_starts, 1);

  // A unit whose footprint runs off the edge is an error. A map created from
  // scratch carries no UDTA, so footprints fall back to 1x1 and a building at
  // the last tile fits — ask the map rather than assuming a town hall is 4x4.
  int fw = 1, fh = 1;
  pf_map_unit_footprint(map, 58, &fw, &fh);
  CHECK(pf_map_add_unit(map, 32 - fw, 32 - fh, 58, 0, 0) >= 0);
  total = pf_map_validate(map, issues.data(), int(issues.size()));
  int overflow = 0;
  for (int i = 0; i < total && i < int(issues.size()); i++) {
    if (issues[i].code == PF_ISSUE_UNIT_OVERFLOWS) overflow++;
  }
  CHECK_EQ(overflow, 0);   // exactly fits

  // One tile further is refused outright: placement is bounds-checked, so an
  // out-of-bounds unit cannot be created through the API at all. The
  // out-of-bounds and overflow rules therefore only fire on loaded maps, which
  // is what the corpus pass covers.
  CHECK_EQ(pf_map_add_unit(map, 32, 31, 58, 0, 0), -1);
  CHECK_EQ(pf_map_add_unit(map, 31, 32, 58, 0, 0), -1);

  // Counting without collecting must agree.
  CHECK_EQ(pf_map_validate(map, nullptr, 0), total);
  pf_map_free(map);
}

TEST(name_matching_ranks_the_one_that_was_meant_first) {
  // Ranking is the whole job. These are the same cases PUDForgeWeb's fuzzy.mjs
  // is held to in test/fuzzy.test.js, because two implementations of "which unit did
  // those letters mean" that disagree are worse than one that ranks poorly.
  CHECK(pf_name_score("gt", "Grunt") >= 0);
  CHECK(pf_name_score("ftm", "Footman") >= 0);
  CHECK_EQ(pf_name_score("zqx", "Footman"), -1);
  // Only one n in Peon to consume.
  CHECK_EQ(pf_name_score("nn", "Peon"), -1);

  CHECK_EQ(pf_name_score("KNIGHT", "Knight"), pf_name_score("knight", "Knight"));
  CHECK_EQ(pf_name_score("  ogre ", "Ogre"), pf_name_score("ogre", "Ogre"));
  // An empty query matches everything, so a caller needs no special case.
  CHECK_EQ(pf_name_score("", "Footman"), 0);

  // The names are real unit ids, because the competition between them is the
  // actual case: "dk" has to mean Death Knight and not the k-then-t of Knight.
  auto best = [](const char* query) {
    std::vector<int> ids;
    for (int i = 0; i < PF_UNIT_COUNT; i++) {
      if (pf_unit_name(i)) ids.push_back(i);
    }
    const int kept = pf_unit_name_filter(query, ids.data(), int(ids.size()));
    return kept > 0 ? std::string(pf_unit_name(ids[0])) : std::string();
  };
  CHECK(best("dk") == "Death Knight");
  CHECK(best("peon") == "Peon");
  CHECK(best("knight") == "Knight");
  CHECK(best("farm") == "Farm");

  // A query nothing matches leaves nothing, and an empty one leaves the order
  // it was given — the palette's grouping is more use than a name sort.
  std::vector<int> ids{5, 1, 9, 3};
  CHECK_EQ(pf_unit_name_filter("zzqqxx", ids.data(), 4), 0);
  ids = {5, 1, 9, 3};
  CHECK_EQ(pf_unit_name_filter("   ", ids.data(), 4), 4);
  CHECK_EQ(ids[0], 5);
  CHECK_EQ(ids[3], 3);
}

/**
 * Ships and flying units cover 2x2 tiles, whatever `unitSize` says.
 *
 * The field reads 1x1 for every mobile unit in the retail defaults, which would
 * fit a battleship in a one-tile pond. See overrides/unit_footprints.cpp for
 * what the game actually does and how that was established.
 */
TEST(ships_and_fliers_cover_two_by_two) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(64, 64, PF_TILESET_FOREST, &st);
  CHECK(map != nullptr);
  if (!map) return;

  auto boxed = [&](int type, int side) {
    int w = 0, h = 0;
    pf_map_unit_footprint(map, type, &w, &h);
    return w == side && h == side;
  };

  // Ten ships and eight fliers. The count is pinned so a unit cannot quietly
  // join or leave the table: the Daemon and the Eye of Kilrogg were missing
  // from it for as long as the rule was read off boxSize, which calls them
  // 31x31 while the maps put all 77 Daemons on the even grid.
  CHECK_EQ(pf::oversize_unit_count(), 18);
  for (int i = 0; i < pf::oversize_unit_count(); i++) {
    const int id = pf::oversize_unit_id(i);
    CHECK(boxed(id, 2));
    // The no-UDTA path has to agree, or a map without the section lays its
    // ships out differently from one with it.
    int dw = 1, dh = 1;
    pf::default_unit_footprint(id, dw, dh);
    CHECK_EQ(dw, 2);
    CHECK_EQ(dh, 2);
  }

  // Land units keep their tile, Ballista and Catapult included: they carry a
  // 63 px box like the ships, and are the reason the table is a list of ids
  // rather than a threshold on `boxSize`.
  for (int id : {0x00, 0x02, 0x06, 0x04, 0x05, 0x37, 0x39}) {
    CHECK(boxed(id, 1));
  }
  // And buildings still come from the file, which is right about those.
  CHECK(boxed(0x3a, 2));    // Farm
  CHECK(boxed(0x4a, 4));    // Town Hall
  CHECK(boxed(0x5c, 3));    // Gold Mine
  pf_map_free(map);

  // The override outranks a map's own UDTA, which is the whole point: every
  // map ever written carries Blizzard's 1x1 for these.
  if (g_corpus.empty()) return;
  for (const std::string& path : g_corpus) {
    pf_map* real = pf_map_open_file(path.c_str(), &st);
    if (!real) continue;
    if (pf_map_has_unit_data(real)) {
      int w = 0, h = 0;
      pf_map_unit_footprint(real, 0x1e, &w, &h);   // Elven Destroyer
      CHECK_EQ(w, 2);
      CHECK_EQ(h, 2);
    }
    pf_map_free(real);
  }
}

/**
 * Ships and flying units sit on a two-tile grid.
 *
 * The evidence the 2x2 table rests on, and the reason placement snaps. A 2x2
 * unit anchored at its top-left corner on an even grid lands on even
 * coordinates and nothing else.
 *
 * Split by who wrote the map, the way the region rule is: a `REGM` carrying
 * the 0xfffa shore sentinel came from the game's own editor, and that
 * population has to be perfect. Maps from other tools are measured and
 * reported, not asserted — one of them is where every exception lives.
 */
TEST(ships_and_fliers_sit_on_even_tiles) {
  if (!have_corpus()) { skip("no maps"); return; }
  long by_editor = 0, by_editor_even = 0;
  long elsewhere = 0, elsewhere_even = 0;
  long small = 0, small_even = 0;

  for (const std::string& path : g_corpus) {
    pf_status st = PF_OK;
    pf_map* map = pf_map_open_file(path.c_str(), &st);
    if (!map) continue;
    const int tiles = pf_map_width(map) * pf_map_height(map);
    const uint16_t* regions = pf_map_regions(map);
    bool game_editor = false;
    if (regions) {
      for (int i = 0; i < tiles; i++) {
        if (regions[i] == 0xfffa) { game_editor = true; break; }
      }
    }
    for (int i = 0, n = pf_map_unit_count(map); i < n; i++) {
      pf_unit u;
      if (pf_map_unit(map, i, &u) != PF_OK) continue;
      // On its own grid: the even tiles for a ship or a flier, the odd ones
      // for oil. The same two-tile grid, half a step apart.
      const int step = pf_unit_placement_step(u.type);
      const int phase = pf_unit_placement_phase(u.type);
      const bool on_grid =
          ((int(u.x) - phase) % step) == 0 && ((int(u.y) - phase) % step) == 0;
      if (step > 1) {
        if (game_editor) { by_editor++; by_editor_even += on_grid; }
        else { elsewhere++; elsewhere_even += on_grid; }
      } else {
        int w = 1, h = 1;
        pf_map_unit_footprint(map, u.type, &w, &h);
        // The control group, and it is asked the plain question: a 1x1 unit
        // has no grid, so what matters is that it is not on one by accident.
        const bool plain_even = (u.x % 2) == 0 && (u.y % 2) == 0;
        if (w == 1 && h == 1) { small++; small_even += plain_even; }
      }
    }
    pf_map_free(map);
  }

  std::printf("     %ld/%ld on their grid in maps by the game's editor, "
              "%ld/%ld elsewhere, %ld/%ld 1x1 units on even tiles\n",
              by_editor_even, by_editor, elsewhere_even, elsewhere,
              small_even, small);
  CHECK(by_editor + elsewhere > 0);
  // No share and no threshold for the population that decides the rule.
  CHECK_EQ(by_editor_even, by_editor);
  // And the control group is nowhere near it, or the parity means nothing.
  if (small > 100) CHECK(small_even * 2 < small);
}

/**
 * The units a map holds in places the game cannot put them, and taking them
 * away.
 *
 * A `.pud` may contain anything; the parser's job is to load it unchanged.
 * This is the separate question of what an editor should offer to do about it
 * once somebody has it open.
 */
TEST(misplaced_units_are_found_and_removed) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  if (!map) { CHECK(false); return; }
  for (int y = 10; y < 20; y++) {
    for (int x = 10; x < 20; x++) {
      pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }
  // Built the way a foreign editor would have written it, which is the only
  // way to get these on a map at all.
  pf_map_set_allow_illegal_placement(map, 1);

  CHECK(pf_map_add_unit(map, 1, 1, 0x00, 0, 0) >= 0);     // fine: a footman on grass
  CHECK(pf_map_add_unit(map, 12, 12, 0x00, 0, 0) >= 0);   // a footman in the lake
  CHECK(pf_map_add_unit(map, 2, 2, 0x1e, 0, 0) >= 0);     // a destroyer on dry land
  CHECK(pf_map_add_unit(map, 5, 5, 0x00, 0, 0) >= 0);     // and two in one place
  CHECK(pf_map_add_unit(map, 5, 5, 0x00, 0, 0) >= 0);
  CHECK(pf_map_add_unit(map, 30, 30, 0x4a, 0, 0) >= 0);   // a 4x4 hall two tiles from the edge
  // The arrangements the game intends, which must survive all of this: a start
  // location under a building, and a marker is not an overlap.
  CHECK(pf_map_add_unit(map, 22, 22, 0x5e, 0, 0) >= 0);
  CHECK(pf_map_add_unit(map, 22, 22, 0x3a, 0, 0) >= 0);
  const int total = pf_map_unit_count(map);
  CHECK_EQ(total, 8);

  pf_map_set_allow_illegal_placement(map, 0);

  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_OFF_MAP, nullptr, 0), 1);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_TERRAIN, nullptr, 0), 2);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_OVERLAP, nullptr, 0), 1);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_ALL, nullptr, 0), 4);
  CHECK_EQ(pf_map_misplaced_units(map, 0, nullptr, 0), 0);

  // Ascending, and the *later* of the overlapping pair: index 4, not 3.
  int found[8] = {};
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_ALL, found, 8), 4);
  CHECK_EQ(found[0], 1);
  CHECK_EQ(found[1], 2);
  CHECK_EQ(found[2], 4);
  CHECK_EQ(found[3], 5);

  CHECK_EQ(pf_map_remove_misplaced_units(map, PF_MISPLACED_ALL), 4);
  CHECK_EQ(pf_map_unit_count(map), 4);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_ALL, nullptr, 0), 0);

  // What is left is the clean footman, the first of the pile, and the pair the
  // game means to overlap.
  int kept[8] = {};
  for (int i = 0; i < pf_map_unit_count(map); i++) {
    pf_unit u;
    CHECK_EQ(pf_map_unit(map, i, &u), PF_OK);
    kept[i] = u.type;
  }
  CHECK_EQ(kept[0], 0x00);
  CHECK_EQ(kept[1], 0x00);
  CHECK_EQ(kept[2], 0x5e);
  CHECK_EQ(kept[3], 0x3a);
  pf_map_free(map);
}

/**
 * A ship needs two tiles of water in each direction, which is the practical
 * half of the 2x2 footprint: at 1x1 the editor would put a destroyer in a
 * puddle and the game would not have it.
 */
TEST(a_ship_needs_water_for_its_whole_footprint) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  if (!map) { CHECK(false); return; }
  // A single tile of water, then a pool big enough to float in. Painted wide,
  // because the corner model turns the rim of a lake into shore and only what
  // is inside that is open water.
  pf_map_paint_terrain(map, 4, 4, PF_TERRAIN_WATER_DARK, 1);
  for (int y = 18; y < 28; y++) {
    for (int x = 18; x < 28; x++) {
      pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }
  CHECK_EQ(pf_map_placement_check(map, 4, 4, 0x1e), PF_PLACE_NEEDS_WATER);
  CHECK_EQ(pf_map_placement_check(map, 20, 20, 0x1e), PF_PLACE_OK);
  // And the tile between two of its blocks is not a place it may start.
  CHECK_EQ(pf_map_placement_check(map, 21, 21, 0x1e), PF_PLACE_OFF_GRID);
  // And two ships a tile apart are on top of each other, which they were not
  // when both were a single tile.
  CHECK(pf_map_add_unit(map, 20, 20, 0x1e, 0, 0) >= 0);
  CHECK_EQ(pf_map_placement_check_ex(map, 21, 21, 0x1e, nullptr, 0),
           PF_PLACE_OCCUPIED);
  CHECK_EQ(pf_map_placement_check_ex(map, 22, 22, 0x1e, nullptr, 0),
           PF_PLACE_OK);
  // The next block along is free; the odd tile beside it is not a placement.
  CHECK_EQ(pf_map_placement_check_ex(map, 23, 22, 0x1e, nullptr, 0),
           PF_PLACE_OFF_GRID);
  pf_map_free(map);
}

/**
 * Opening a real map must not cry wolf.
 *
 * The client offers, once, to delete the units it thinks the game could not
 * place. An offer like that is only worth making if it is nearly always right,
 * and the way to get it wrong is a rule that is too strict rather than too
 * loose: giving ships their real 2x2 footprint turned every warship parked on
 * an oil patch into an "overlap" — eight of them on one fixture — until
 * resources were excluded the way start-location markers already were.
 *
 * A share rather than a count, and measured rather than asserted from memory.
 */
TEST(opening_a_map_does_not_offer_to_delete_much) {
  if (!have_corpus()) { skip("no maps"); return; }
  long units = 0, flagged = 0, shipped_units = 0, shipped_flagged = 0;
  std::string worst;
  long worst_n = 0;

  for (const std::string& path : g_corpus) {
    pf_status st = PF_OK;
    pf_map* map = pf_map_open_file(path.c_str(), &st);
    if (!map) continue;
    const long n = pf_map_misplaced_units(map, PF_MISPLACED_ALL, nullptr, 0);
    const long total = pf_map_unit_count(map);
    units += total;
    flagged += n;
    if (n > worst_n) { worst_n = n; worst = path; }
    if (is_shipped(path) || !g_corpus_is_shipped) {
      shipped_units += total;
      shipped_flagged += n;
    }
    pf_map_free(map);
  }

  std::printf("     %ld of %ld units would be offered up (%ld of %ld shipped)\n",
              flagged, units, shipped_flagged, shipped_units);
  if (worst_n) std::printf("     worst: %ld in %s\n", worst_n, worst.c_str());
  CHECK(units > 0);
  // Under 2%. Blizzard's own maps were made by the tool that defined the
  // format, so a rule that flags many of their units is the rule being wrong.
  CHECK(shipped_flagged * 50 <= shipped_units);
}

/**
 * Which units the map check lets stand on each other.
 *
 * The short list in overrides/shared_tiles.cpp. Everything else that shares a
 * tile is a fault worth reporting, so the list has to stay short — and a gold
 * mine has to stay off it, because nothing stands on a gold mine.
 */
TEST(only_a_few_units_may_share_tiles) {
  const int kFootman = 0x00, kTownHall = 0x4a, kGoldMine = 0x5c;
  const int kOilPatch = 0x5d, kHumanStart = 0x5e, kOrcStart = 0x5f;
  const int kCircleOfPower = 0x64;

  // A marker under anything, which is how nearly every map places a base.
  CHECK(pf::units_may_share_tiles(kHumanStart, kTownHall));
  CHECK(pf::units_may_share_tiles(kTownHall, kOrcStart));
  // A resource you harvest by moving onto it, and the spot a unit is meant to
  // stand on.
  CHECK(pf::units_may_share_tiles(kOilPatch, 0x1e));   // a destroyer on a patch
  CHECK(pf::units_may_share_tiles(kFootman, kCircleOfPower));

  // A gold mine is not one of them: a worker goes inside it rather than onto
  // it, and a hall too close to one is caught by its own clearance rule.
  CHECK(!pf::units_may_share_tiles(kFootman, kGoldMine));
  CHECK(!pf::units_may_share_tiles(kGoldMine, kTownHall));
  CHECK(!pf::units_may_share_tiles(kFootman, kFootman));
  CHECK(!pf::units_may_share_tiles(kTownHall, kTownHall));

  // And the map check agrees, which is the half that matters: a footman on a
  // mine is offered up, a footman on a circle is not.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  if (!map) { CHECK(false); return; }
  pf_map_set_allow_illegal_placement(map, 1);
  CHECK(pf_map_add_unit(map, 4, 4, kGoldMine, 15, 2400) >= 0);
  CHECK(pf_map_add_unit(map, 4, 4, kFootman, 0, 0) >= 0);
  pf_map_set_allow_illegal_placement(map, 0);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_OVERLAP, nullptr, 0), 1);

  CHECK(pf_map_add_unit(map, 20, 20, kCircleOfPower, 15, 0) >= 0);
  CHECK(pf_map_add_unit(map, 20, 20, kFootman, 0, 0) >= 0);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_OVERLAP, nullptr, 0), 1);
  pf_map_free(map);
}

/**
 * A flier standing over a ground unit is a map author's guard post, not a
 * fault — the offer to clean up misplaced units used to delete it anyway,
 * which is how a Dragon parked over a hall goes missing on the next open.
 *
 * The Daemon belongs here too, and not by courtesy: the retail table flags it
 * `Fly` the same as the Dragon and the Gryphon Rider, spell-summoned and
 * otherwise unlike them as it is.
 */
TEST(a_flier_may_share_a_tile_with_anything) {
  const int kFootman = 0x00, kGryphonRider = 0x2a, kDragon = 0x2b, kDaemon = 0x38;
  CHECK_EQ(pf::default_unit_domain(kDragon), pf::kDomainAir);
  CHECK_EQ(pf::default_unit_domain(kDaemon), pf::kDomainAir);

  CHECK(pf::units_may_share_tiles(kDragon, kFootman));
  CHECK(pf::units_may_share_tiles(kFootman, kGryphonRider));
  CHECK(pf::units_may_share_tiles(kDaemon, kFootman));
  CHECK(pf::units_may_share_tiles(kDragon, kDaemon));   // two fliers together

  // Two footmen on one tile are still a fault: the exemption is the flier's,
  // not a general amnesty for overlap.
  CHECK(!pf::units_may_share_tiles(kFootman, kFootman));

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  if (!map) { CHECK(false); return; }
  pf_map_set_allow_illegal_placement(map, 1);
  CHECK(pf_map_add_unit(map, 10, 10, kFootman, 0, 0) >= 0);
  CHECK(pf_map_add_unit(map, 10, 10, kDragon, 0, 0) >= 0);
  pf_map_set_allow_illegal_placement(map, 0);
  CHECK_EQ(pf_map_misplaced_units(map, PF_MISPLACED_OVERLAP, nullptr, 0), 0);
  pf_map_free(map);
}

/**
 * And the placement gate says the same, without the stacking option.
 *
 * The two used to disagree: the check kept quiet about a flier over a unit
 * and about a hall on a start location, while placement answered OCCUPIED for
 * both — so an arrangement the editor tolerated on open was one it would not
 * let you draw. A shared tile is only a stack when neither unit is entitled
 * to be there.
 */
TEST(placement_allows_the_tiles_the_check_shares) {
  const int kFootman = 0x00, kDragon = 0x2b, kTownHall = 0x4a;
  const int kHumanStart = 0x5e, kCircleOfPower = 0x64;
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  if (!map) { CHECK(false); return; }
  CHECK_EQ(pf_map_allows_stacked_units(map), 0);   // the option stays off

  CHECK(pf_map_add_unit(map, 10, 10, kFootman, 0, 0) >= 0);
  CHECK_EQ(pf_map_placement_check_ex(map, 10, 10, kDragon, nullptr, 0), PF_PLACE_OK);
  // Two footmen on one tile is still a stack, and still refused.
  CHECK_EQ(pf_map_placement_check_ex(map, 10, 10, kFootman, nullptr, 0),
           PF_PLACE_OCCUPIED);

  // A marker is not a thing to stand on, in either order.
  CHECK(pf_map_add_unit(map, 20, 20, kHumanStart, 0, 0) >= 0);
  CHECK_EQ(pf_map_placement_check_ex(map, 20, 20, kTownHall, nullptr, 0), PF_PLACE_OK);
  CHECK(pf_map_add_unit(map, 4, 4, kCircleOfPower, 15, 0) >= 0);
  CHECK_EQ(pf_map_placement_check_ex(map, 4, 4, kFootman, nullptr, 0), PF_PLACE_OK);
  pf_map_free(map);
}

/**
 * The unit block in the game's string table ends before the upgrades.
 *
 * 105 names for 110 units, so a lookup that does not stop at the boundary
 * gives the last five ids the first five upgrade captions. This runs without
 * the game, where the check against the real table cannot; it pins the two
 * offsets that the bound is made of.
 */
TEST(the_unit_name_block_stops_before_the_upgrades) {
  CHECK_EQ(pf::kFirstUpgradeString - pf::kFirstUnitString, 105);
  CHECK(pf::kFirstUnitString + pf::kUnitCount > pf::kFirstUpgradeString);

  // The ids the game does not name, which are the ones the bound protects.
  for (int unit = 105; unit < pf::kUnitCount; unit++) {
    const char* name = pf_unit_name(unit);
    CHECK(name != nullptr && *name != 0);
    CHECK(std::string(name).find("Upgrade") == std::string::npos);
  }
  CHECK(std::string(pf_unit_name(0x69)) == "Corpse");
}

/**
 * Oil goes on odd tiles, and only odd ones.
 *
 * The same two-tile grid the ships use, a tile off it. Measured rather than
 * assumed: every oil patch and every oil well the game's own editor placed sits
 * at an odd x and an odd y, and the gold mine — the same 3x3 size — does not,
 * which is what makes it the unit's rule and not the footprint's.
 */
TEST(oil_sits_on_the_odd_tiles_of_the_same_grid) {
  const int kOilPatch = 0x5d, kGoldMine = 0x5c, kDestroyer = 0x1e;

  CHECK_EQ(pf_unit_placement_step(kOilPatch), 2);
  CHECK_EQ(pf_unit_placement_phase(kOilPatch), 1);
  CHECK_EQ(pf_unit_placement_step(0x56), 2);    // human oil well
  CHECK_EQ(pf_unit_placement_phase(0x57), 1);   // orc oil well
  // A ship is on the same grid but the other half of it.
  CHECK_EQ(pf_unit_placement_step(kDestroyer), 2);
  CHECK_EQ(pf_unit_placement_phase(kDestroyer), 0);
  // The gold mine is the control: same size, no grid at all.
  CHECK_EQ(pf_unit_placement_step(kGoldMine), 1);
  CHECK_EQ(pf_unit_placement_phase(kGoldMine), 0);

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  if (!map) { CHECK(false); return; }
  for (int y = 4; y < 28; y++) {
    for (int x = 4; x < 28; x++) {
      pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
    }
  }
  // Odd is where it may go; even is between the lines.
  CHECK_EQ(pf_map_placement_check(map, 11, 11, kOilPatch), PF_PLACE_OK);
  CHECK_EQ(pf_map_placement_check(map, 12, 11, kOilPatch), PF_PLACE_OFF_GRID);
  CHECK_EQ(pf_map_placement_check(map, 11, 12, kOilPatch), PF_PLACE_OFF_GRID);
  CHECK_EQ(pf_map_placement_check(map, 12, 12, kOilPatch), PF_PLACE_OFF_GRID);
  // And a ship on the same water is the other way round.
  CHECK_EQ(pf_map_placement_check(map, 12, 12, kDestroyer), PF_PLACE_OK);
  CHECK_EQ(pf_map_placement_check(map, 11, 11, kDestroyer), PF_PLACE_OFF_GRID);
  pf_map_free(map);
}



/**
 * A map that says "use the default data" is read as using it.
 *
 * `useDefaultData` tells the game to read its own unit table and ignore the
 * section entirely, so whatever the section holds is not what the game reads.
 * 131 of the 357 maps on hand set the flag over a payload whose expansion
 * heroes are all zero, and the sheet showed those zeros - Alleria with no hit
 * points, which is not a number the game ever sees.
 */
TEST(the_use_default_data_flag_hides_whatever_the_section_holds) {
  if (pft::g_corpus.empty()) { skip("no corpus"); return; }
  int hp = -1;
  for (int i = 0; i < pf_udta_field_count(); i++) {
    const std::string n = pf_udta_field_name(i) ? pf_udta_field_name(i) : "";
    if (n == "hitPoints") hp = i;
  }
  CHECK(hp >= 0);
  // Alleria and Grom Hellscream: expansion heroes, and the two the report named.
  const int heroes[] = {0x14, 0x19};
  long long flagged = 0, flagged_zero = 0, unflagged_zero = 0, maps = 0;
  for (const std::string& path : pft::g_corpus) {
    pf_map* m = pf_map_open_file(path.c_str(), nullptr);
    if (!m) continue;
    if (pf_map_has_unit_data(m)) {
      maps++;
      const bool uses_default = pf_map_unit_field(m, 0, 0, 0) != 0;
      bool zero = false;
      for (int h : heroes) zero |= pf_map_unit_field(m, hp, h, 0) <= 0;
      if (uses_default) {
        flagged++;
        if (zero) flagged_zero++;
      } else if (zero) {
        unflagged_zero++;
      }
    }
    pf_map_free(m);
  }
  std::printf("     %lld maps carry UDTA, %lld say use-the-default;"
              " %lld of those hold a zeroed hero, %lld hold one without the flag\n",
              maps, flagged, flagged_zero, unflagged_zero);
  // The flag is common and the zeroed payload under it is common, which is why
  // the sheet may not read the section when the flag is set.
  CHECK(flagged > 0);
  CHECK(flagged_zero > 0);

  // Every hero the game ships has hit points, so a zero can only come from a
  // section nobody reads.
  for (int h : heroes) CHECK(pf_udta_default_field(hp, h, 0) > 0);
}

/**
 * Clearing the flag writes the table the page was showing.
 *
 * While the flag is set the sheet shows the game's own values, so turning it
 * off has to leave those values behind. Handing over the section as it lay
 * would give the map zeroed heroes that nothing had asked for.
 */
TEST(clearing_use_default_data_leaves_the_games_own_table) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(64, 64, PF_TILESET_FOREST, &st);
  CHECK(map != nullptr);
  if (!map) return;
  int hp = -1;
  for (int i = 0; i < pf_udta_field_count(); i++) {
    const std::string n = pf_udta_field_name(i) ? pf_udta_field_name(i) : "";
    if (n == "hitPoints") hp = i;
  }
  CHECK(hp >= 0);

  CHECK_EQ(pf_map_add_unit_data(map), PF_OK);
  // A map in the shape the corpus keeps finding: the flag on, Alleria zeroed.
  CHECK_EQ(pf_map_set_unit_field(map, 0, 0, 0, 1), PF_OK);
  CHECK_EQ(pf_map_set_unit_field(map, hp, 0x14, 0, 0), PF_OK);
  CHECK_EQ(pf_map_unit_field(map, hp, 0x14, 0), 0);

  CHECK_EQ(pf_map_reset_unit_data(map), PF_OK);
  CHECK_EQ(pf_map_unit_field(map, hp, 0x14, 0),
           pf_udta_default_field(hp, 0x14, 0));
  CHECK(pf_map_unit_field(map, hp, 0x14, 0) > 0);
  // The section is still there and the flag is down, so the table is now live.
  CHECK(pf_map_has_unit_data(map) != 0);
  CHECK_EQ(pf_map_unit_field(map, 0, 0, 0), 0);
  pf_map_free(map);
}


/**
 * A live unit table with a unit that has no hit points is reported.
 *
 * The pre-expansion shape: a `UDTA` written before Beyond the Dark Portal
 * leaves the ten expansion heroes at zero, which is harmless while
 * `useDefaultData` is set and is a broken map the moment it is not. 22 maps on
 * hand are already in the second state, and before this nothing said so.
 */
TEST(a_live_unit_table_with_no_hit_points_is_reported) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(64, 64, PF_TILESET_FOREST, &st);
  CHECK(map != nullptr);
  if (!map) return;
  int hp = -1;
  for (int i = 0; i < pf_udta_field_count(); i++) {
    const std::string n = pf_udta_field_name(i) ? pf_udta_field_name(i) : "";
    if (n == "hitPoints") hp = i;
  }
  CHECK(hp >= 0);
  CHECK_EQ(pf_map_add_unit_data(map), PF_OK);

  auto zero_stats_reported = [&] {
    std::vector<pf_issue> issues(64);
    const int n = pf_map_validate(map, issues.data(), int(issues.size()));
    for (int i = 0; i < n && i < int(issues.size()); i++) {
      if (issues[size_t(i)].code == PF_ISSUE_UNIT_STATS_ZERO) return true;
    }
    return false;
  };

  // A fresh section is the game's own table, so there is nothing to report.
  CHECK(!zero_stats_reported());

  // Alleria zeroed, the flag still up: the game reads its own table, so this is
  // 134 of the corpus and none of them is broken.
  CHECK_EQ(pf_map_set_unit_field(map, hp, 0x14, 0, 0), PF_OK);
  CHECK_EQ(pf_map_set_unit_field(map, 0, 0, 0, 1), PF_OK);
  CHECK(!zero_stats_reported());

  // Flag down and the same bytes are now what the game reads.
  CHECK_EQ(pf_map_set_unit_field(map, 0, 0, 0, 0), PF_OK);
  CHECK(zero_stats_reported());

  // And the way out that the message names.
  CHECK_EQ(pf_map_reset_unit_data(map), PF_OK);
  CHECK(!zero_stats_reported());
  pf_map_free(map);
}

/**
 * The zeroed units are the expansion ones and nothing else.
 *
 * Twelve ids across 156 maps: the ten Beyond the Dark Portal heroes, always,
 * and the Ranger and the Berserker in 50 of them. 0x12 to 0x19 is contiguous,
 * which is what a table that simply stops early looks like.
 *
 * Measured rather than read off the names. A thirteenth id joining the list
 * would mean this is not a pre-expansion table after all, and the validator
 * would be pointing at the wrong thing.
 */
TEST(the_zeroed_units_are_the_expansion_heroes) {
  if (pft::g_corpus.empty()) { skip("no corpus"); return; }
  int hp = -1;
  for (int i = 0; i < pf_udta_field_count(); i++) {
    const std::string n = pf_udta_field_name(i) ? pf_udta_field_name(i) : "";
    if (n == "hitPoints") hp = i;
  }
  CHECK(hp >= 0);
  // The ten heroes, which every such map zeroes.
  const int heroes[] = {0x14, 0x15, 0x16, 0x17, 0x18,
                        0x19, 0x23, 0x2c, 0x2e, 0x2f};
  // Those plus the two the expansion upgrades to, which only some of them do.
  const int expansion[] = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                           0x18, 0x19, 0x23, 0x2c, 0x2e, 0x2f};
  long long zeroed[PF_UNIT_COUNT] = {};
  long long maps_with_the_shape = 0;
  for (const std::string& path : pft::g_corpus) {
    pf_map* m = pf_map_open_file(path.c_str(), nullptr);
    if (!m) continue;
    if (pf_map_has_unit_data(m)) {
      int zeros = 0;
      for (int u = 0; u < PF_UNIT_COUNT; u++) {
        if (pf_udta_default_field(hp, u, 0) <= 0) continue;
        if (pf_map_unit_field(m, hp, u, 0) > 0) continue;
        zeroed[u]++;
        zeros++;
      }
      if (zeros) maps_with_the_shape++;
    }
    pf_map_free(m);
  }
  std::printf("     %lld maps hold a zeroed unit table\n", maps_with_the_shape);
  CHECK(maps_with_the_shape > 0);
  for (int u = 0; u < PF_UNIT_COUNT; u++) {
    if (!zeroed[u]) continue;
    bool known = false;
    for (int h : expansion) known |= h == u;
    if (!known) {
      std::printf("       unexpected: %s 0x%02x in %lld maps\n",
                  pf_unit_name(u), u, zeroed[u]);
    }
    CHECK(known);
  }
  // The heroes are the part that is always there, so they are the part the
  // reading rests on.
  for (int h : heroes) CHECK(zeroed[h] > 0);
}


/**
 * 1 is the value a unit gets, and 0 is the exception.
 *
 * The specification calls the field "0 passive 1 active" for everything that is
 * not a mine or an oil patch. The maps cannot confirm that wording - nothing in
 * them separates the two values by meaning:
 *
 *   critters, which never fight in any map ever made, are 1,706 to 35 for 1
 *   footmen, which always do, are 454 to 161 for 1 - the same way
 *   units under a passive computer are 1,722 to 35 for 1
 *   units under a human player are 5,672 to 366 for 1
 *   units under a Rescue (passive) player are 301 to 0 for 1, and under a
 *   Rescue (active) player 90 to 8 - the same way again
 *
 * No unit type and no owner prefers 0. What the maps do settle is which value
 * is the default: of 239 maps by the game's own editor, 215 hold nothing but 1
 * and not one holds nothing but 0.
 *
 * That is enough for unit_default_value without settling the polarity at all.
 * PUDForge placed units at 0, the value no map is made of; it places them at 1,
 * the value nearly every unit in every map holds. The label on the two buttons
 * rests on the specification, and this test is what a later answer has to argue
 * with if that label turns out to be backwards.
 */
TEST(units_carry_one_for_the_value_the_editor_writes) {
  if (pft::g_corpus.empty()) { skip("no corpus"); return; }
  long long editor_zero = 0, editor_one = 0;
  long long rescue_passive[2] = {}, rescue_active[2] = {}, critter[2] = {};
  long long editor_maps = 0, maps_all_one = 0, maps_all_zero = 0;
  for (const std::string& path : pft::g_corpus) {
    pf_map* m = pf_map_open_file(path.c_str(), nullptr);
    if (!m) continue;
    const int w = pf_map_width(m), h = pf_map_height(m);
    const uint16_t* rg = pf_map_regions(m);
    bool game_editor = false;
    for (int i = 0; rg && i < w * h; i++) {
      if (rg[i] == 0xfffa) { game_editor = true; break; }
    }
    bool map_holds[2] = {false, false};
    for (int i = 0; i < pf_map_unit_count(m); i++) {
      pf_unit u{};
      if (pf_map_unit(m, i, &u) != PF_OK) continue;
      // Only where the field is the state. On a mine or an oil patch it is an
      // amount and says nothing about this.
      if (pf_unit_value_is_amount(u.type)) continue;
      if (pf_unit_in_group(u.type, PF_GROUP_START_LOCATIONS)) continue;
      const int slot = u.value ? 1 : 0;
      if (game_editor) { if (slot) editor_one++; else editor_zero++; }
      const int owner = pf_map_owner(m, u.owner);
      if (owner == PF_OWNER_RESCUE_PASSIVE) rescue_passive[slot]++;
      if (owner == PF_OWNER_RESCUE_ACTIVE) rescue_active[slot]++;
      if (u.type == 0x39) critter[slot]++;   // never fights, in any map
      map_holds[slot] = true;
    }
    if (game_editor && (map_holds[0] || map_holds[1])) {
      editor_maps++;
      if (map_holds[1] && !map_holds[0]) maps_all_one++;
      if (map_holds[0] && !map_holds[1]) maps_all_zero++;
    }
    pf_map_free(m);
  }
  std::printf("     game editor: %lld hold 0, %lld hold 1 (%.0f%% are 1)\n",
              editor_zero, editor_one,
              100.0 * double(editor_one) / double(editor_zero + editor_one ? editor_zero + editor_one : 1));
  std::printf("     rescue passive owners: %lld at 0, %lld at 1;"
              " rescue active owners: %lld at 0, %lld at 1\n",
              rescue_passive[0], rescue_passive[1],
              rescue_active[0], rescue_active[1]);
  std::printf("     critters (never fight): %lld at 0, %lld at 1\n",
              critter[0], critter[1]);
  std::printf("     of %lld editor maps: %lld hold only 1, %lld hold only 0\n",
              editor_maps, maps_all_one, maps_all_zero);
  if (editor_zero + editor_one == 0) { skip("no maps by the game's editor"); return; }
  // One value dominates by a wide margin. If that ever stops being true the
  // reading of this field is a different question from the one described above.
  CHECK(editor_one > editor_zero * 4);
  // And it dominates under both rescue settings, which is the part that says
  // the field is not the owner's setting repeated per unit. Only where such a
  // player exists at all: the five fixtures have none, and an assertion over
  // nothing is an assertion that passes for the wrong reason.
  if (rescue_passive[0] + rescue_passive[1]) CHECK(rescue_passive[1] > rescue_passive[0]);
  if (rescue_active[0] + rescue_active[1]) CHECK(rescue_active[1] > rescue_active[0]);
  // The critter is the control: a unit that never fights holds the same value
  // as a footman, so the field is not telling them apart.
  if (critter[0] + critter[1]) CHECK(critter[1] > critter[0]);
  // And the part unit_default_value actually rests on: 1 is what a map is made
  // of, and no map is made of 0.
  if (editor_maps) {
    CHECK(maps_all_one > 0);
    CHECK_EQ(maps_all_zero, 0);
  }
}


}  // namespace pft
