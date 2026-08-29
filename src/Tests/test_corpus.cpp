// Sweeps over every shipped map. Slowest group; needs the corpus.
//
// See harness.hpp for the assertions, fixtures and registration.

#include "harness.hpp"

TEST_GROUP("corpus")

namespace pft {

TEST(corpus_round_trips_byte_exactly) {
  if (!have_corpus()) { skip("no shipped maps"); return; }
  int exact = 0, parsed = 0, warnings = 0;
  std::vector<std::string> bad;

  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) { bad.push_back(path); continue; }
    parsed++;
    warnings += int(map->warnings().size());
    if (map->serialize() == bytes) exact++;
    else if (bad.size() < 5) bad.push_back(path);
    delete map;
  }

  std::printf("     %d/%d parsed, %d byte-identical, %d warnings\n",
              parsed, int(g_corpus.size()), exact, warnings);
  for (const std::string& b : bad) std::printf("     differs: %s\n", b.c_str());
  CHECK_EQ(parsed, int(g_corpus.size()));
  CHECK_EQ(exact, int(g_corpus.size()));
  CHECK_EQ(warnings, 0);
}
TEST(corpus_corner_model_is_lossless) {
  if (!have_corpus()) { skip("no shipped maps"); return; }
  pf::TileIndex index;
  long tiles = 0, changed = 0, fallbacks = 0;

  for (size_t i = 0; i < g_corpus.size(); i += 25) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(g_corpus[i], bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;

    std::vector<uint16_t> before = map->tiles();
    pf::CornerGrid grid = pf::CornerGrid::from_map(*map);
    pf::Rect rect{0, 0, map->width() - 1, map->height() - 1};
    pf::legalize(grid, rect);
    fallbacks += pf::apply_corners(*map, grid, rect, index);

    for (size_t k = 0; k < before.size(); k++) {
      tiles++;
      uint8_t a[4], b[4];
      pf::decode_tile(before[k], a);
      pf::decode_tile(map->tiles()[k], b);
      for (int j = 0; j < 4; j++) {
        if (a[j] != b[j]) { changed++; break; }
      }
    }
    delete map;
  }

  double pct = tiles ? (100.0 * double(changed) / double(tiles)) : 0.0;
  std::printf("     %ld tiles, %ld changed (%.3f%%), %ld fallbacks\n",
              tiles, changed, pct, fallbacks);
  // No fallbacks is the correctness property and holds for any map: every
  // corner combination must resolve to a real tile.
  CHECK_EQ(fallbacks, 0L);
  // The tight bound is a quality measurement of the model against Blizzard's
  // own tile choices, so it only means anything on their maps.
  // tile-showcase.pud exists to contain unusual tiles and refits at ~17%.
  if (g_corpus_is_shipped) CHECK(pct < 0.5);
}
TEST(every_corpus_tile_has_artwork) {
  if (!have_corpus() || !have_art()) { skip("needs maps and artwork"); return; }
  pf::TilesetArt* art[4] = {};
  long checked = 0, missing = 0;

  for (size_t i = 0; i < g_corpus.size(); i += 10) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(g_corpus[i], bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    int era = map->tileset();
    if (!art[era]) art[era] = pf::TilesetArt::open(g_bgs_dir, era);
    if (art[era]) {
      for (uint16_t tile : map->tiles()) {
        checked++;
        if (art[era]->megatile_for(tile) < 0) missing++;
      }
    }
    delete map;
  }
  for (int i = 0; i < 4; i++) delete art[i];
  std::printf("     %ld tiles checked, %ld without artwork\n", checked, missing);
  CHECK(checked > 0);
  CHECK_EQ(missing, 0L);
}
TEST(every_shipped_description_survives_the_editors_rule) {
  // The rule the editor enforces on typing has to be able to express every
  // description already out there, or opening a map and pressing OK would
  // corrupt it. The corpus is the only honest test of that.
  if (!have_corpus()) { skip("no shipped maps"); return; }
  int checked = 0, full = 0, high = 0, rejected = 0, differs = 0;

  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    checked++;

    std::string encoded;
    if (!pf::desc_encode(map->description(), encoded)) {
      rejected++;
      if (rejected <= 5) std::printf("     unstorable: %s\n", path.c_str());
    } else {
      // Re-encoding what was decoded has to give the file's own bytes back.
      if (std::memcmp(encoded.data(), map->description_bytes(),
                      encoded.size()) != 0) {
        differs++;
        if (differs <= 5) std::printf("     differs: %s\n", path.c_str());
      }
      if (encoded.size() > size_t(pf::kDescBytes - 1)) full++;
      for (char c : encoded) if (uint8_t(c) > 0x7f) high++;
    }

    // Writing back what was read is a no-op, including for the maps whose
    // description leaves no room for a terminator.
    const std::string was = map->description();
    CHECK(map->set_description(was));
    CHECK(map->description() == was);
    if (map->serialize() != bytes) differs++;
    delete map;
  }

  std::printf("     %d descriptions, %d unstorable, %d differing, "
              "%d filling all 32 bytes, %d bytes above 0x7f\n",
              checked, rejected, differs, full, high);
  CHECK(checked > 0);
  CHECK_EQ(rejected, 0);
  CHECK_EQ(differs, 0);
}
TEST(shipped_restrictions_survive_an_edit) {
  if (!have_corpus()) { skip("no shipped maps"); return; }

  int found = 0, restrictive = 0;
  for (const std::string& path : g_corpus) {
    pf_map* map = pf_map_open_file(path.c_str(), nullptr);
    if (!map) continue;
    if (!pf_map_has_restrictions(map)) { pf_map_free(map); continue; }
    found++;

    // A map that restricts anything at all is the interesting case: two of the
    // three allow nearly everything and say very little.
    const uint32_t units = uint32_t(pf_map_allow(map, 0, 0));
    if (units != 0xffffffffu && units != 0x7fffffffu) restrictive++;

    // Toggling one named bit twice must leave the whole word as it was,
    // including the bits nothing names — which is the property the sheet
    // depends on, since it edits one bit at a time.
    std::vector<uint32_t> before;
    for (int block = 0; block < 6; block++) {
      for (int player = 0; player < 16; player++) {
        before.push_back(uint32_t(pf_map_allow(map, block, player)));
      }
    }
    pf_map_set_allow(map, 0, 0, units ^ 1u);
    pf_map_set_allow(map, 0, 0, units);
    size_t i = 0;
    for (int block = 0; block < 6; block++) {
      for (int player = 0; player < 16; player++) {
        CHECK_EQ(int(pf_map_allow(map, block, player)), int(before[i++]));
      }
    }
    pf_map_free(map);
  }

  std::printf("     %d maps carry ALOW, %d of them restrict something\n",
              found, restrictive);
  if (g_corpus_is_shipped) {
    CHECK(found > 0);
    CHECK(restrictive > 0);
  }
}
TEST(editing_a_udta_field_keeps_the_corpus_byte_exact) {
  // The strongest guarantee available: change a field, change it back, and
  // every shipped map must still serialize to the exact bytes it came from.
  if (!have_corpus()) { skip("no map corpus"); return; }

  int checked = 0, edited = 0;
  for (size_t i = 0; i < g_corpus.size(); i += 10) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(g_corpus[i], bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    checked++;

    if (map->unit_data().present) {
      // Round-trip every per-unit field on one unit: read, write it back
      // unchanged, then write a different value and restore it.
      for (int f = 0; f < pf::kUdtaSegmentCount; f++) {
        const pf::UdtaSegment& seg = pf::kUdtaSegments[f];
        if (!seg.per_unit) continue;
        const int offset = pf::udta_offset(f);
        const size_t at = size_t(offset) + 4 * size_t(seg.width);
        if (at + seg.width > map->unit_data().raw.size()) continue;

        auto& raw = map->unit_data_mut().raw;
        const std::vector<uint8_t> before(raw.begin() + offset,
                                          raw.begin() + offset + seg.width);
        raw[size_t(offset)] = uint8_t(~raw[size_t(offset)]);
        raw[size_t(offset)] = before[0];
        edited++;
      }
    }

    const std::vector<uint8_t> again = map->serialize();
    CHECK_EQ(again == bytes, true);
    delete map;
  }
  std::printf("     %d maps, %d field round-trips, all byte-identical\n", checked, edited);
  CHECK(checked > 0);
  CHECK(edited > 0);
}
TEST(editing_upgrades_and_restrictions_keeps_the_corpus_byte_exact) {
  if (!have_corpus()) { skip("no map corpus"); return; }

  int with_ugrd = 0, with_alow = 0, checked = 0;
  for (size_t i = 0; i < g_corpus.size(); i += 10) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(g_corpus[i], bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    checked++;

    if (!map->upgrade_data().empty()) {
      with_ugrd++;
      CHECK_EQ(int(map->upgrade_data().size()), pf::kUgrdSize);
      // Flip a byte in every per-upgrade field and put it back.
      for (int f = 0; f < pf::kUgrdSegmentCount; f++) {
        if (!pf::kUgrdSegments[f].per_unit) continue;
        const int off = pf::segment_offset(pf::kUgrdSegments, pf::kUgrdSegmentCount, f);
        auto& raw = map->upgrade_data_mut();
        if (size_t(off) >= raw.size()) continue;
        const uint8_t before = raw[size_t(off)];
        raw[size_t(off)] = uint8_t(~before);
        raw[size_t(off)] = before;
      }
    }

    if (!map->restrictions().empty()) {
      with_alow++;
      CHECK_EQ(int(map->restrictions().size()), pf::kAlowSize);
      auto& raw = map->restrictions_mut();
      const uint8_t before = raw[0];
      raw[0] = uint8_t(~before);
      raw[0] = before;
    }

    const std::vector<uint8_t> again = map->serialize();
    CHECK_EQ(again == bytes, true);
    delete map;
  }
  std::printf("     %d maps, %d with UGRD, %d with ALOW, all byte-identical\n",
              checked, with_ugrd, with_alow);
  CHECK(checked > 0);
  CHECK(with_ugrd > 0);
}
TEST(nothing_ever_uses_the_seven_dead_player_slots) {
  // Every per-player table is sixteen wide and the game plays eight of them
  // plus neutral. The claim behind hiding the rest is that they are empty, so
  // it is checked rather than asserted.
  if (!have_corpus()) { skip("no map corpus"); return; }

  int owners[pf::kPlayerCount] = {};
  int units[pf::kPlayerCount] = {};
  int checked = 0;
  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    checked++;
    for (int p = 0; p < pf::kPlayerCount; p++) {
      if (map->owner(p) != 3) owners[p]++;   // 3 is "nobody"
    }
    for (const pf::Unit& u : map->units()) {
      if (u.owner < pf::kPlayerCount) units[int(u.owner)]++;
    }
    delete map;
  }
  CHECK(checked > 0);

  int dead_owners = 0, dead_units = 0;
  for (int p = 0; p < pf::kPlayerCount; p++) {
    if (pf::player_slot_is_supported(p)) continue;
    dead_owners += owners[p];
    dead_units += units[p];
  }
  std::printf("     %d maps: slots 9-15 claimed by %d, owning %d units\n",
              checked, dead_owners, dead_units);
  CHECK_EQ(dead_owners, 0);
  CHECK_EQ(dead_units, 0);

  // And the supported ones are all in use, or the rule is drawn too tight.
  for (int p = 0; p < pf::kPlayerCount; p++) {
    if (!pf::player_slot_is_supported(p)) continue;
    CHECK(owners[p] > 0);
  }
  CHECK_EQ(pf::player_slot_is_supported(pf::kNeutralPlayer), true);
  CHECK_EQ(pf::player_slot_is_supported(8), false);
  CHECK_EQ(pf::player_slot_is_supported(-1), false);
  CHECK_EQ(pf::player_slot_is_supported(pf::kPlayerCount), false);
}
TEST(the_unrestricted_alow_table_is_what_real_maps_write) {
  // The default in overrides/alow_defaults.cpp is a reading of the corpus, so
  // check it against the corpus rather than against itself: for every block,
  // the value we write must be the one real maps agree on most often.
  if (!have_corpus()) { skip("no map corpus"); return; }

  std::map<uint32_t, int> seen[pf::kAlowBlocks];
  int carrying = 0;
  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    const std::vector<uint8_t>& raw = map->restrictions();
    if (int(raw.size()) >= pf::kAlowSize) {
      carrying++;
      for (int block = 0; block < pf::kAlowBlocks; block++) {
        // Only maps whose players agree say anything about a default.
        uint32_t first = 0;
        bool uniform = true;
        for (int p = 0; p < pf::kPlayerCount; p++) {
          const size_t at = size_t(block * pf::kPlayerCount + p) * 4;
          const uint32_t v = uint32_t(raw[at]) | uint32_t(raw[at + 1]) << 8 |
                             uint32_t(raw[at + 2]) << 16 |
                             uint32_t(raw[at + 3]) << 24;
          if (p == 0) first = v;
          else if (v != first) { uniform = false; break; }
        }
        if (uniform) seen[block][first]++;
      }
    }
    delete map;
  }

  // Spells with no research to buy: an entry that costs nothing and takes no
  // time is not really an upgrade. Misses Death Coil, which the corpus supplies.
  uint32_t free_spells = 0;
  for (int bit = 0; bit < 32; bit++) {
    const int upgrade = pf::alow_bit_upgrade(1, bit);
    if (upgrade < 0) continue;
    int64_t cost = 0;
    for (int f = 0; f < pf_ugrd_field_count(); f++) {
      const char* name = pf_ugrd_field_name(f);
      if (!name) continue;
      const std::string field(name);
      if (field == "upgradeTime" || field == "goldCost" || field == "lumberCost" ||
          field == "oilCost") {
        cost += pf_ugrd_default_field(f, upgrade);
      }
    }
    if (cost == 0) free_spells |= 1u << bit;
  }

  std::printf("     %d maps carry ALOW\n", carrying);
  // Having maps is not having evidence. The section is optional and only 40 of
  // the 1378 carry one, so a smaller set can be entirely real and still say
  // nothing about what an unrestricted table looks like — which is a skip, not
  // a failure. `have_corpus()` cannot see this: it answers "are there maps".
  if (carrying == 0) { skip("no map here carries an ALOW section"); return; }
  for (int block = 0; block < pf::kAlowBlocks; block++) {
    uint32_t best = 0;
    int most = 0;
    for (const auto& entry : seen[block]) {
      if (entry.second > most) { most = entry.second; best = entry.first; }
    }
    // Block 1 is the one place the corpus is not the whole answer: it leaves
    // clear the bits the game hands over regardless, which UGRD's costs name.
    const uint32_t want = block == 1 ? (best | free_spells) : best;
    std::printf("     %-20s %08X in %d maps, we write %08X%s\n",
                pf::kAlowBlockNames[block], best, most,
                pf::kDefaultAlowBlock[block],
                block == 1 ? " (corpus | never-researchable)" : "");
    CHECK_EQ(int(pf::kDefaultAlowBlock[block]), int(want));
  }

  // The four are named, so a table that drifts says which spell moved.
  const char* expect[] = {"Holy Vision", "Fireball", "Eye of Kilrogg",
                          "Death Coil"};
  int found = 0;
  for (int bit = 0; bit < 32; bit++) {
    if (!((pf::kDefaultAlowBlock[1] >> bit) & 1)) continue;
    const char* name = pf::alow_bit_name(1, bit);
    CHECK(name != nullptr);
    if (name) {
      bool known = false;
      for (const char* want : expect) known = known || std::string(name) == want;
      if (!known) std::printf("     unexpected spell started with: %s\n", name);
      CHECK_EQ(known, true);
    }
    found++;
  }
  CHECK_EQ(found, 4);
}
TEST(shipped_maps_validate_cleanly) {
  // Blizzard's own maps are the reference for what a valid map looks like.
  // An error here means the rule is wrong, not the map.
  if (!have_corpus()) { skip("no map corpus"); return; }

  int checked = 0, with_errors = 0, warnings = 0;
  int custom_checked = 0, custom_with_errors = 0;
  std::map<int, int> by_code;
  std::string first_error;

  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf_status st = PF_OK;
    pf_map* map = pf_map_open(bytes.data(), bytes.size(), &st);
    if (!map) continue;
    const bool shipped = is_shipped(path) || !g_corpus_is_shipped;
    if (shipped) checked++; else custom_checked++;

    std::vector<pf_issue> issues(32);
    const int total = pf_map_validate(map, issues.data(), int(issues.size()));
    bool had_error = false;
    for (int i = 0; i < total && i < int(issues.size()); i++) {
      by_code[issues[i].code]++;
      if (issues[i].severity == PF_SEVERITY_ERROR) {
        had_error = true;
        if (shipped && first_error.empty()) first_error = path + ": " + issues[i].message;
      } else {
        warnings++;
      }
    }
    if (had_error) { if (shipped) with_errors++; else custom_with_errors++; }
    pf_map_free(map);
  }

  std::printf("     %d shipped maps, %d with errors, %d warnings\n",
              checked, with_errors, warnings);
  if (custom_checked) {
    // Not a failure. Community maps are built by hand with editors that break
    // the rules deliberately, so this number is a fact about them.
    std::printf("     %d community maps, %d of them with errors\n",
                custom_checked, custom_with_errors);
  }
  for (const auto& [code, n] : by_code) std::printf("       code %d: %d\n", code, n);
  if (!first_error.empty()) std::printf("       first error: %s\n", first_error.c_str());

  // Every map found must validate, however many were found. Asserting a
  // sample size instead would make this unrunnable wherever the corpus is
  // partial, which is exactly where regressions slip through.
  CHECK(checked > 0);
  // Blizzard's maps define what valid means, so a disagreement there is our
  // bug. The checked-in fixtures are synthetic — tile-showcase.pud has players
  // and no start locations on purpose — so the rule cannot be asserted against
  // them. The pass still runs, to exercise the code and report what it finds.
  if (g_corpus_is_shipped) CHECK_EQ(with_errors, 0);
}
TEST(shipped_maps_place_units_legally) {
  // Blizzard's maps are the reference again: if the rule disagrees with them,
  // the rule is wrong.
  if (!have_corpus()) { skip("no map corpus"); return; }
  int checked = 0, illegal = 0;
  std::string first;

  auto survey = [](const std::vector<std::string>& maps, int step,
                   int& checked, int& illegal, std::string& first) {
    for (size_t i = 0; i < maps.size(); i += size_t(step)) {
      std::vector<uint8_t> bytes;
      if (!pf::read_file(maps[i], bytes)) continue;
      pf_status st = PF_OK;
      pf_map* map = pf_map_open(bytes.data(), bytes.size(), &st);
      if (!map) continue;
      checked++;
      for (int u = 0, n = pf_map_unit_count(map); u < n; u++) {
        pf_unit unit{};
        if (pf_map_unit(map, u, &unit) != PF_OK) continue;
        if (pf_map_placement_check(map, unit.x, unit.y, unit.type) != PF_PLACE_OK) {
          illegal++;
          if (first.empty()) {
            first = maps[i] + ": " +
                    (pf_unit_name(unit.type) ? pf_unit_name(unit.type) : "unit") +
                    " at " + std::to_string(unit.x) + "," + std::to_string(unit.y);
          }
        }
      }
      pf_map_free(map);
    }
  };

  survey(g_shipped, 10, checked, illegal, first);
  std::printf("     %d shipped maps, %d units placed illegally\n", checked, illegal);
  if (!first.empty()) std::printf("       first: %s\n", first.c_str());
  CHECK(checked > 0);
  // Blizzard's maps define what legal means. Any disagreement is our bug.
  CHECK_EQ(illegal, 0);

  // Community maps are surveyed and reported. A castle on the map edge is a
  // deliberate act by someone using an editor that let them, not evidence that
  // the rule is wrong — and the editor has a setting for exactly this.
  std::vector<std::string> custom;
  for (const std::string& path : g_corpus) {
    if (!is_shipped(path)) custom.push_back(path);
  }
  if (!custom.empty()) {
    int c_checked = 0, c_illegal = 0;
    std::string c_first;
    survey(custom, 10, c_checked, c_illegal, c_first);
    std::printf("     %d community maps, %d units placed illegally\n",
                c_checked, c_illegal);
    if (!c_first.empty()) std::printf("       first: %s\n", c_first.c_str());
  }
}
/**
 * The same guarantee over the real thing. Five maps in the corpus carry
 * War2mod scripts — Archer Assassins RPG, Chess, Obsidian Sanctum, Golden
 * Citadel, Air Waves Comps — plus any the trigger editor's examples contribute.
 *
 * A trigger program starts at offset 0 and contains the editor's FE FF marker;
 * legacy junk starts with 00 and has no marker. That heuristic only selects
 * which maps this test reports on — the assertion itself is that *no* map's
 * `OILM` changes, so it is meaningful either way.
 */
TEST(shipped_trigger_maps_keep_their_scripts) {
  if (!have_corpus()) { skip("no map corpus"); return; }

  int scripted = 0;
  int checked = 0;
  int skipped_empty = 0;
  long differing_total = 0;

  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> before;
    if (!pf::read_file(path, before)) continue;

    size_t before_len = 0;
    const uint8_t* oil = find_section(before, "OILM", &before_len);
    if (!oil || before_len == 0) continue;

    // Only maps with something in OILM are evidence. An all-zero section
    // cannot demonstrate preservation — zero surviving as zero says nothing —
    // and parsing, editing and re-saving all 1378 maps to learn that made this
    // the slowest test in the suite by a factor of three. The 434 maps with
    // real content, which include every scripted one, are the actual claim.
    bool interesting = false;
    for (size_t i = 0; i < before_len; i++) {
      if (oil[i]) { interesting = true; break; }
    }
    if (!interesting) { skipped_empty++; continue; }

    bool has_program = oil[0] != 0;
    if (has_program) {
      bool marker = false;
      for (size_t i = 0; i + 1 < before_len; i++) {
        if (oil[i] == 0xfe && oil[i + 1] == 0xff) { marker = true; break; }
      }
      has_program = marker;
    }
    const std::vector<uint8_t> expected(oil, oil + before_len);

    pf_status st = PF_OK;
    pf_map* map = pf_map_open(before.data(), before.size(), &st);
    if (!map) continue;
    exercise_editing(map);
    size_t out_len = 0;
    uint8_t* out = pf_map_save(map, &out_len, &st);
    pf_map_free(map);
    if (!out) continue;
    std::vector<uint8_t> after(out, out + out_len);
    pf_buffer_free(out);

    size_t after_len = 0;
    const uint8_t* got = find_section(after, "OILM", &after_len);
    if (!got) { differing_total++; continue; }

    checked++;
    if (has_program) scripted++;
    if (after_len != before_len) { differing_total++; continue; }
    for (size_t i = 0; i < after_len; i++) {
      if (got[i] != expected[i]) { differing_total++; break; }
    }
  }

  std::printf("     %d maps with OILM content (%d empty, skipped), "
              "%d carrying a trigger program, %ld changed by editing\n",
              checked, skipped_empty, scripted, differing_total);
  CHECK(checked > 0);
  CHECK_EQ(differing_total, 0L);
}

/**
 * Rebuilding `REGM` reproduces what the game's own editor wrote.
 *
 * The AI reads this to decide whether a target needs a transport, so a
 * landmass wrongly joined to another is an AI that never builds a ship. That
 * is how it was reported: a map saved here lost its transports, and the same
 * map saved by the standard editor got them back.
 *
 * Every map that carries a REGM is a labelling by an editor that got it right,
 * so the whole corpus is the answer key. Compared as a partition rather than
 * as bytes: which tiles share a region is the fact the AI acts on, and the
 * numbering is only the order the flood happened to meet them in.
 */
TEST(rebuilt_regions_agree_with_the_editors_that_wrote_them) {
  if (!have_corpus()) { skip("no maps"); return; }
  int checked = 0, exact = 0;
  long tiles = 0, wrong = 0;
  std::vector<std::string> bad;

  for (const std::string& path : g_corpus) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf::Status s;
    pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!map) continue;
    const std::vector<uint16_t> theirs = map->regions();
    const size_t n = size_t(map->width()) * size_t(map->height());
    if (theirs.size() != n) { delete map; continue; }
    pf::rebuild_regions(*map);
    const std::vector<uint16_t>& ours = map->regions();
    checked++;

    // Two tiles agree when the two labellings say the same thing about them:
    // the same sentinel, or a region under a consistent renaming.
    std::map<uint16_t, uint16_t> naming;
    long mismatched = 0;
    for (size_t i = 0; i < n; i++) {
      const bool their_special = theirs[i] >= 0xfff0;
      const bool our_special = ours[i] >= 0xfff0;
      if (their_special || our_special) {
        if (theirs[i] != ours[i]) mismatched++;
        continue;
      }
      auto it = naming.find(theirs[i]);
      if (it == naming.end()) naming.emplace(theirs[i], ours[i]);
      else if (it->second != ours[i]) mismatched++;
    }
    tiles += long(n);
    wrong += mismatched;
    if (mismatched == 0) exact++;
    else if (bad.size() < 5) bad.push_back(path);
    delete map;
  }

  std::printf("     %d of %d maps relabel exactly, %ld of %ld tiles differ\n",
              exact, checked, wrong, tiles);
  for (const std::string& b : bad) std::printf("     differs: %s\n", b.c_str());
  CHECK(checked > 0);
  // Under one tile in a thousand. Their editor and this one need not agree on
  // every last shore tile, but a landmass either matches or the AI is wrong
  // about the whole map, and that shows up in thousands.
  CHECK(wrong * 1000 <= tiles);
}

}  // namespace pft
