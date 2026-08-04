// Artwork and archives: tilesets, sprites, MPQ, embedded tables
//
// See harness.hpp for the assertions, fixtures and registration.

#include "harness.hpp"

#include "png.hpp"

TEST_GROUP("art")

namespace pft {

std::vector<std::string> split_lines(const char* text) {
  std::vector<std::string> out;
  std::string current;
  for (const char* p = text; *p; p++) {
    if (*p == '\n') { out.push_back(current); current.clear(); }
    else current += *p;
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

TEST(embedded_retail_tables_match_the_game_files) {
  // The game ships its own unit and upgrade tables as loose files, which is a
  // better provenance for what we embed than the copy taken out of a map.
  // They are the same payloads without the leading useDefaultData word, which
  // is why every one of them is exactly two bytes short of a section.
  // Two of them: `unitdato.dat` is the original game's table and
  // `unitdata.dat` the expansion's, which is the one the core embeds. They
  // differ in 225 fields across 10 units, so picking the wrong file would
  // quietly change what a unit costs.
  // Read them the way a user's machine would: out of the archive. They are
  // files inside `War2Dat.mpq`, not loose on disk — the unpacked tree beside
  // it is a convenience of this repository and nothing to test against.
  pf::DataSource source;
  if (source.add_directory(g_root + "/reference/app") == 0) { skip("no MPQ archives"); return; }

  std::vector<uint8_t> units, upgrades;
  if (!source.read("rez\\unitdata.dat", units) ||
      !source.read("rez\\upgrades.dat", upgrades)) {
    skip("no game data");
    return;
  }
  CHECK_EQ(int(units.size()), pf::kUdtaSizeWithSwamp - 2);
  CHECK_EQ(int(upgrades.size()), pf::kUgrdSize - 2);

  // Put the flag back and read it with our own field table: every field of
  // every unit has to come out as the retail value the core already embeds.
  std::vector<uint8_t> section(2, 0);
  section.insert(section.end(), units.begin(), units.end());
  CHECK_EQ(pf_component_kind(section.size()), PF_COMPONENT_UDTA);

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;
  CHECK_EQ(pf_map_import_component(map, section.data(), section.size()), PF_OK);

  int differences = 0;
  for (int field = 0; field < pf_udta_field_count(); field++) {
    if (!pf_udta_field_units(field)) continue;
    for (int unit = 0; unit < pf::kUnitCount; unit++) {
      for (int c = 0; c < pf_udta_field_components(field); c++) {
        if (pf_map_unit_field(map, field, unit, c) !=
            pf_udta_default_field(field, unit, c)) differences++;
      }
    }
  }
  std::printf("     rez/unitdata.dat vs the embedded table: %d differences\n", differences);
  CHECK_EQ(differences, 0);

  // And the same for the upgrades. This half used to check only that the file
  // was the length a section is, which says the payload is the right shape and
  // nothing at all about whether it holds the right numbers — a table off by
  // one entry is exactly the right length.
  std::vector<uint8_t> ugrd(2, 0);
  ugrd.insert(ugrd.end(), upgrades.begin(), upgrades.end());
  CHECK_EQ(pf_component_kind(ugrd.size()), PF_COMPONENT_UGRD);
  CHECK_EQ(pf_map_import_component(map, ugrd.data(), ugrd.size()), PF_OK);

  int upgrade_differences = 0, upgrade_checked = 0;
  for (int field = 0; field < pf_ugrd_field_count(); field++) {
    for (int entry = 0; entry < pf_ugrd_field_entries(field); entry++) {
      upgrade_checked++;
      if (pf_map_upgrade_field(map, field, entry) !=
          pf_ugrd_default_field(field, entry)) {
        upgrade_differences++;
      }
    }
  }
  std::printf("     rez/upgrades.dat vs the embedded table: %d of %d differ\n",
              upgrade_differences, upgrade_checked);
  // 364 of them across the field table. The floor is here so that a loop which
  // silently ran zero times cannot pass as agreement.
  CHECK(upgrade_checked > 300);
  CHECK_EQ(upgrade_differences, 0);
  pf_map_free(map);
}
TEST(unit_names_come_from_the_games_own_string_table) {
  // The names were hand-written and several were guesses. The game ships the
  // real ones in rez/stat_txt.tbl, which is also where a localised install
  // keeps its translations — so reading them is both a correctness fix and the
  // only way a German copy of the game gets German unit names.
  pf_data_source* source = pf_data_source_create();
  if (!source) { CHECK(false); return; }
  if (pf_data_source_add_directory(source, (g_root + "/reference/app").c_str()) == 0) {
    pf_data_source_free(source);
    skip("no MPQ archives");
    return;
  }

  pf_status st = PF_OK;
  pf_strings* strings = pf_strings_open_source(source, &st);
  CHECK(strings != nullptr);
  if (!strings) { pf_data_source_free(source); return; }
  // 479 in the shipped table: units, upgrades, button captions, messages.
  CHECK_EQ(pf_strings_count(strings), 479);
  // Entry 0 is empty and the units start at 1, which is the offset everything
  // here depends on. If the table ever stops starting that way, every name
  // moves by one and this is the check that says so.
  CHECK(std::string(pf_strings_at(strings, 0)).empty());
  CHECK(std::string(pf_strings_at(strings, 1)) == "Footman");

  // Nothing changes until a host asks for it, so that a client with no game
  // folder is not a client with no names.
  CHECK(std::string(pf_unit_name(0x08)) == "Archer");
  pf_use_strings(strings);
  // The eight the built-in table had wrong, now the game's own words.
  CHECK(std::string(pf_unit_name(0x08)) == "Elven Archer");
  CHECK(std::string(pf_unit_name(0x09)) == "Troll Axethrower");
  CHECK(std::string(pf_unit_name(0x21)) == "Ogre Juggernaught");
  CHECK(std::string(pf_unit_name(0x40)) == "Scout Tower");
  CHECK(std::string(pf_unit_name(0x41)) == "Watch Tower");
  CHECK(std::string(pf_unit_name(0x56)) == "Human Oil Platform");
  CHECK(std::string(pf_unit_name(0x57)) == "Orc Oil Platform");
  CHECK(std::string(pf_unit_name(0x67)) == "Wall");

  // Where the game names nothing the built-in label stands, because "" in a
  // unit palette is worse than a label nobody translated. The five dead slots
  // and the orc wall are the whole of that set.
  CHECK(std::string(pf_unit_name(0x22)) == "Unused #34");
  CHECK(std::string(pf_unit_name(0x68)) == "Orc Wall");
  // And every unit still has a name of some kind, which is the property the
  // palette and every list depend on.
  int named = 0;
  for (int unit = 0; unit < pf::kUnitCount; unit++) {
    const char* name = pf_unit_name(unit);
    CHECK(name != nullptr && *name != '\0');
    if (name && *name) named++;
  }
  CHECK_EQ(named, pf::kUnitCount);

  // Upgrades come from the table too, with the button's line breaks turned
  // into spaces — the raw entry keeps them, and the name must not.
  CHECK(std::string(pf_strings_at(strings, 106)).find('\n') != std::string::npos);
  CHECK(std::string(pf_upgrade_name(0)) == "Upgrade Sword Strength 1");
  CHECK(std::string(pf_upgrade_name(12)) == "Upgrade Human Ship Attack 1");
  CHECK(std::string(pf_upgrade_name(24)) == "Elven Ranger Training");
  CHECK(std::string(pf_upgrade_name(34)) == "Spell - Holy Vision");

  // Restriction bits are named by the unit or upgrade they stand for, so a page
  // that lists them reads in the game's words — and its language — rather than
  // in ours. Three of these four differ from the built-in label, which is the
  // point: bit 16 is "Farm" to us, and the pair to the game.
  CHECK(std::string(pf_alow_bit_name(0, 0)) == "Footman / Grunt");
  CHECK(std::string(pf_alow_bit_name(0, 16)) == "Farm / Pig Farm");
  CHECK(std::string(pf_alow_bit_name(1, 5)) == "Spell - Fireball");
  CHECK(std::string(pf_alow_bit_name(4, 2)) == "Upgrade Sword Strength 1");
  // A bit nothing names stays unnamed rather than borrowing a neighbour's.
  CHECK(pf_alow_bit_name(0, 13) == nullptr);
  CHECK(pf_alow_bit_name(1, 2) == nullptr);
  CHECK(std::string(pf_upgrade_name(51)) == "Spell - Death and Decay");
  // Every one of them named, on one line, for all 52. The block runs straight
  // through with no gaps, so a name that came back empty or wrapped would mean
  // the offset had drifted.
  for (int up = 0; up < pf::kUpgradeCount; up++) {
    const char* name = pf_upgrade_name(up);
    CHECK(name != nullptr && *name != '\0');
    CHECK(std::string(name).find('\n') == std::string::npos);
  }

  // The AI scripts, where the built-in list ran out of real names after 32 and
  // numbered the remaining 51. Every one of the 83 is named by the table.
  CHECK(std::string(pf_ai_name(0)) == "Land Attack");
  CHECK(std::string(pf_ai_name(25)) == "Sea Attack");
  // Without the leading underscore the table carries: that is Blizzard's mark
  // for the scripts their own editor hides, not part of the name, and this
  // client lists every script anyway.
  CHECK(std::string(pf_ai_name(32)) == "Orc Exp. 4");       // was "Expansion 1"
  CHECK(std::string(pf_ai_name(57)) == "Hum Exp. 6c (Orange)");
  CHECK(std::string(pf_ai_name(82)) == "Orc Exp.3");        // was "Expansion 51"
  for (int v = 0; v < pf_ai_name_count(); v++) {
    const char* one = pf_ai_name(v);
    CHECK(one && one[0] != '_');
  }
  int placeholders = 0;
  for (int v = 0; v < pf_ai_name_count(); v++) {
    const char* name = pf_ai_name(v);
    CHECK(name != nullptr && *name != '\0');
    if (name && std::string(name).rfind("Expansion ", 0) == 0) placeholders++;
  }
  CHECK_EQ(placeholders, 0);

  // The refusal messages, for the three codes the game words specifically
  // enough to be worth taking. They are whole sentences, so a caller uses them
  // as they are rather than joining them to a unit name.
  auto message = [](int code) {
    char buf[160] = {};
    pf_placement_message(code, buf, int(sizeof(buf)));
    return std::string(buf);
  };
  CHECK(message(PF_PLACE_OUT_OF_BOUNDS) == "You cannot build off the map.");
  CHECK(message(PF_PLACE_NEEDS_SHORE) ==
        "You must build this building on the coast.");
  CHECK(message(PF_PLACE_TOO_NEAR_MINE) ==
        "You cannot build a townhall too near a goldmine.");
  // And empty for the rest, deliberately: the game covers wrong ground, blocked
  // ground and the wrong element with one "You cannot build there.", where the
  // editor says which. An empty answer here is what keeps its own wording.
  CHECK(message(PF_PLACE_NEEDS_LAND).empty());
  CHECK(message(PF_PLACE_NEEDS_WATER).empty());
  CHECK(message(PF_PLACE_BLOCKED).empty());
  CHECK(message(PF_PLACE_OCCUPIED).empty());
  CHECK(message(PF_PLACE_OK).empty());

  // What the game calls a resource's remaining amount, which is per resource
  // where the built-in label was one "Amount" for either.
  auto label = [](int resource) {
    char buf[64] = {};
    pf_resource_label(resource, buf, int(sizeof(buf)));
    return std::string(buf);
  };
  CHECK(label(PF_RESOURCE_GOLD) == "Gold Left:");
  CHECK(label(PF_RESOURCE_OIL) == "Oil Left:");
  CHECK(label(PF_RESOURCE_NONE).empty());

  // Freeing uninstalls, so the built-ins come back rather than freed memory.
  pf_strings_free(strings);
  // And with no table there is no game wording to be had, which is the state a
  // client with no game folder runs in.
  CHECK(message(PF_PLACE_TOO_NEAR_MINE).empty());
  CHECK(label(PF_RESOURCE_GOLD).empty());
  CHECK(std::string(pf_unit_name(0x08)) == "Archer");
  CHECK(std::string(pf_ai_name(32)) == "Expansion 1");
  pf_data_source_free(source);
}

TEST(ai_scripts_decode_from_the_archive) {
  // The script table is read, not described: everything the editor says about a
  // script comes out of rez/ai.bin at runtime, so a modded archive is described
  // as it actually is. That is why the count is derived rather than assumed.
  if (!have_art()) { skip("no game artwork"); return; }
  pf_data_source* source = pf_data_source_create();
  pf_data_source_add_files(source, (g_corpus_dir + "/mpq/War2Dat").c_str());
  pf_ai_scripts* scripts = pf_ai_scripts_open_source(source, nullptr);
  if (!scripts) { pf_data_source_free(source); skip("no rez/ai.bin"); return; }

  // 83 is what the shipped file holds, and the parser has to find that without
  // being told: the offset table bounds itself, and every entry has to decode.
  CHECK_EQ(pf_ai_scripts_count(scripts), 83);

  // Every script produces a summary, and none of them fails to decode.
  int undecoded = 0;
  for (int i = 0; i < pf_ai_scripts_count(scripts); i++) {
    char text[2048];
    const int len = pf_ai_script_summary(scripts, i, text, sizeof(text));
    CHECK(len > 0);
    if (std::string(text).find("does not know") != std::string::npos) undecoded++;
  }
  CHECK_EQ(undecoded, 0);

  // The two scripts anyone recognises, as a check that the reading is right and
  // not merely self-consistent: Passive attacks with nothing, Sea attack goes by
  // sea and opens with a shipyard.
  char passive[4096], sea[4096];
  pf_ai_script_summary(scripts, 1, passive, sizeof(passive));
  pf_ai_script_summary(scripts, 25, sea, sizeof(sea));
  CHECK(std::string(passive).find("Attacks by: nothing") != std::string::npos);
  CHECK(std::string(sea).find("sea") != std::string::npos);
  CHECK(std::string(sea).find("battleships") != std::string::npos);

  // Lists, not prose: every line is a label and its values.
  for (const char* line : {passive, sea}) {
    CHECK(std::string(line).find(": ") != std::string::npos);
  }

  // Waves: a row per sleep, five tab-separated columns. A script that attacks
  // has some; Passive, which never does, has none worth showing.
  char waves[8192];
  CHECK(pf_ai_script_waves(scripts, 25, waves, sizeof(waves)) > 0);
  int rows = 0;
  for (const std::string& row : split_lines(waves)) {
    rows++;
    int tabs = 0;
    for (char c : row) tabs += c == '\t';
    CHECK_EQ(tabs, 4);
  }
  CHECK(rows > 1);
  CHECK(std::string(waves).find("sea") != std::string::npos);
  // The force grows across the waves rather than being stated once.
  CHECK(std::string(waves).find("destroyers") != std::string::npos);

  // The listing is a disassembly, so it holds instructions rather than prose.
  char listing[8192];
  CHECK(pf_ai_script_listing(scripts, 0, listing, sizeof(listing)) > 0);
  CHECK(std::string(listing).find("goto") != std::string::npos
        || std::string(listing).find("sleep") != std::string::npos);

  // Out of range says nothing rather than guessing.
  char none[64];
  CHECK_EQ(pf_ai_script_summary(scripts, 9999, none, sizeof(none)), 0);

  std::printf("     %d AI scripts decoded from rez/ai.bin\n",
              pf_ai_scripts_count(scripts));
  pf_ai_scripts_free(scripts);
  pf_data_source_free(source);
}
TEST(ai_scripts_are_named) {
  CHECK(std::string(pf_ai_name(0)) == "Land attack");
  CHECK(std::string(pf_ai_name(1)) == "Passive");
  CHECK(std::string(pf_ai_name(25)) == "Sea attack");
  CHECK(std::string(pf_ai_name(26)) == "Air attack");
  CHECK(pf_ai_name(-1) == nullptr);
  CHECK(pf_ai_name(pf_ai_name_count()) == nullptr);

  // Every value the shipped maps carry has to have a name, or a stock map
  // would show a bare number in its own player table.
  if (!have_corpus()) return;
  int checked = 0, unnamed = 0;
  for (const std::string& path : g_corpus) {
    pf_map* map = pf_map_open_file(path.c_str(), nullptr);
    if (!map) continue;
    for (int player = 0; player < 16; player++) {
      if (!pf_ai_name(pf_map_ai(map, player))) unnamed++;
      checked++;
    }
    pf_map_free(map);
  }
  std::printf("     %d player AI values, %d unnamed\n", checked, unnamed);
  CHECK_EQ(unnamed, 0);
}
TEST(tileset_art_decodes) {
  if (!have_art()) { skip("no game artwork"); return; }
  for (int era = 0; era < 4; era++) {
    pf::TilesetArt* art = pf::TilesetArt::open(g_bgs_dir, era);
    CHECK(art != nullptr);
    if (!art) continue;
    CHECK(art->megatile_count() > 300);

    // Solid terrain must have artwork, and look roughly the right colour.
    int water = art->megatile_for(0x0010);
    int grass = art->megatile_for(0x0050);
    CHECK(water >= 0);
    CHECK(grass >= 0);
    if (era == 0 && water >= 0 && grass >= 0) {
      uint32_t w = art->average(water), g = art->average(grass);
      CHECK(((w >> 16) & 0xff) > (w & 0xff));          // forest water is blue
      CHECK(((g >> 8) & 0xff) > (g & 0xff));           // forest grass is green
    }

    std::vector<uint32_t> block(32 * 32, 0);
    CHECK(art->draw_megatile(grass, block.data(), 32));
    bool any = false;
    for (uint32_t p : block) if (p) any = true;
    CHECK(any);
    delete art;
  }
}
TEST(changing_tileset_leaves_no_tile_without_artwork) {
  // The four tilesets are not one-to-one. Winter and wasteland populate water
  // variations 5 to 7 where forest and swamp leave them blank, and the swamp
  // coast stops at variation 9 where the others reach 11. So a map carried
  // from one tileset to another keeps a handful of tiles the new artwork
  // cannot draw, and they come out as flat colour - which is the "missing
  // tiles when switching tilesets" this fixes.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_tileset_art* art[4] = {};
  for (int era = 0; era < 4; era++) art[era] = pf_tileset_art_open(g_bgs_dir.c_str(), era, nullptr);
  for (int era = 0; era < 4; era++) CHECK(art[era] != nullptr);

  // Every tile the shipped maps actually use, gathered per source tileset.
  std::vector<std::vector<uint16_t>> used(4);
  for (const std::string& path : g_shipped) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(path, bytes)) continue;
    pf::Status s;
    pf::Map* m = pf::Map::parse(bytes.data(), bytes.size(), s);
    if (!m) continue;
    for (uint16_t tile : m->tiles()) used[size_t(m->tileset())].push_back(tile);
    delete m;
  }

  int repaired = 0;
  for (int from = 0; from < 4; from++) {
    if (used[size_t(from)].empty()) continue;
    for (int to = 0; to < 4; to++) {
      if (from == to || !art[to]) continue;

      // A map holding one of each tile the source tileset uses.
      const int side = 64;
      pf_status st = PF_OK;
      pf_map* map = pf_map_create(side, side, to, &st);
      CHECK(map != nullptr);
      if (!map) continue;
      pf_map_set_tileset_art(map, art[to]);
      const auto& tiles = used[size_t(from)];
      for (int i = 0; i < side * side; i++) {
        pf_map_set_tile(map, i % side, i / side, tiles[size_t(i) % tiles.size()]);
      }

      repaired += pf_map_refit_tiles(map);

      int undrawable = 0;
      for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
          const uint16_t tile = uint16_t(pf_map_tile_at(map, x, y));
          const int m = pf_tileset_art_megatile_for(art[to], tile);
          if (m < 0 || pf_tileset_art_is_blank(art[to], m)) undrawable++;
        }
      }
      CHECK_EQ(undrawable, 0);

      // The terrain each tile stands for is untouched: only the variation may
      // move, so the group nibble has to be the one it started with.
      for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
          const uint16_t want = tiles[size_t(y * side + x) % tiles.size()];
          CHECK_EQ(pf_map_tile_at(map, x, y) >> 4, want >> 4);
        }
      }
      pf_map_free(map);
    }
  }
  std::printf("     tileset changes repaired %d tiles\n", repaired);
  CHECK(repaired > 0);
  for (int era = 0; era < 4; era++) pf_tileset_art_free(art[era]);
}
TEST(compose_region_draws_the_map) {
  // The renderer moved out of the clients and into here. This is the cheap
  // check that it composes at all and respects its options; the pixel-for-pixel
  // agreement with the old JavaScript one was verified separately, across
  // terrain, artwork, grid, and the movement and region overlays.
  if (!have_art() || g_shipped.empty()) { skip("no game artwork"); return; }

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) { pf_tileset_art_free(art); return; }
  for (int y = 4; y < 28; y++) {
    for (int x = 4; x < 28; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_LIGHT, 1);
  }

  pf_render_options o = {};
  o.cols = 8;
  o.rows = 8;
  o.art = art;
  const int want = o.cols * o.rows * 32 * 32;

  // Asking with no buffer reports the size needed.
  CHECK_EQ(pf_map_compose_region(map, &o, nullptr, 0), want);

  std::vector<uint32_t> px(static_cast<size_t>(want), 0u);
  CHECK_EQ(pf_map_compose_region(map, &o, px.data(), px.size()), want);
  // A short buffer is refused rather than overrun.
  CHECK_EQ(pf_map_compose_region(map, &o, px.data(), size_t(want) - 1), -1);

  // Every pixel is opaque, and the water region is not the same colour as the
  // ground around it - i.e. something was actually drawn.
  int opaque = 0;
  for (uint32_t p : px) opaque += int((p >> 24) == 0xff);
  CHECK_EQ(opaque, want);
  CHECK(px[0] != px[size_t(4 * 32) * size_t(o.cols * 32) + size_t(5 * 32)]);

  // The grid darkens the tile boundaries, so turning it on has to change
  // pixels and turning it off has to put them back.
  std::vector<uint32_t> plain = px;
  o.grid = 1;
  CHECK_EQ(pf_map_compose_region(map, &o, px.data(), px.size()), want);
  CHECK(px != plain);
  o.grid = 0;
  CHECK_EQ(pf_map_compose_region(map, &o, px.data(), px.size()), want);
  CHECK(px == plain);

  // The minimap is one pixel per tile.
  std::vector<uint32_t> mini(32 * 32);
  CHECK_EQ(pf_map_compose_minimap(map, art, mini.data(), mini.size()), 32 * 32);
  CHECK_EQ(pf_map_compose_minimap(map, art, nullptr, 0), 32 * 32);

  // A building never turns: its second frame is scaffolding, not a facing.
  CHECK_EQ(pf_unit_facing_count(0x3a), 1);      // Farm
  CHECK(pf_unit_facing_count(0) > 1);           // Footman

  pf_map_free(map);
  pf_tileset_art_free(art);
}
TEST(painted_tiles_all_have_artwork) {
  // A tile group defines up to 16 variations but a tileset populates only
  // some. An unfiltered TileIndex hands back the unpopulated ones, and the
  // editor then paints tiles the renderer can only draw as a flat colour —
  // which is what "the stitching looks wrong" turned out to be. Painting
  // against the artwork must never produce a tile without a megatile.
  if (!have_art()) { skip("no game artwork"); return; }

  for (int era = 0; era < 4; era++) {
    pf::TilesetArt* art = pf::TilesetArt::open(g_bgs_dir, era);
    CHECK(art != nullptr);
    if (!art) continue;

    pf::Status s;
    pf::Map* map = pf::Map::create(32, 32, era, s);
    CHECK(map != nullptr);
    if (!map) { delete art; continue; }

    pf::CornerGrid grid = pf::CornerGrid::from_map(*map);
    pf::TileIndex index(
        [](uint16_t tile, void* ctx) {
          // One slot each, which is what a plain "can the tileset draw it"
          // filter means now that the index takes a weight.
          return static_cast<const pf::TilesetArt*>(ctx)->megatile_for(tile) >= 0
                     ? 1
                     : 0;
        },
        art);

    // Every terrain a brush offers, painted into and across each other.
    const uint8_t brushes[] = {pf::kWaterDark, pf::kWaterLight, pf::kCoastLight,
                               pf::kGroundLight, pf::kGroundDark, pf::kForest,
                               pf::kMountain};
    int at = 3;
    for (uint8_t terrain : brushes) {
      pf::paint_auto(*map, grid, index, at, at, terrain, 5);
      pf::paint_auto(*map, grid, index, at + 4, at + 2, terrain, 3);
      at += 3;
    }

    long missing = 0;
    for (uint16_t tile : map->tiles()) {
      if (art->megatile_for(tile) < 0) missing++;
    }
    if (missing) std::printf("     tileset %d: %ld painted tiles without artwork\n", era, missing);
    CHECK_EQ(missing, 0L);

    delete map;
    delete art;
  }
}
TEST(mpq_extracts_every_file_byte_for_byte) {
  // The whole point of the reader is that somebody can point at the game they
  // already own. So the test is not "does it decompress something" but "does
  // it produce exactly what is in the archive" — and there is ground truth for
  // that, because both archives sit beside an unpacked copy of themselves.
  struct Pair { const char* archive; const char* tree; };
  const Pair pairs[] = {
      {"/reference/app/War2Dat.mpq", "/reference/war2_ref/mpq/War2Dat"},
      {"/reference/app/War2Patch.mpq", "/reference/war2_ref/mpq/War2Patch"},
  };

  int checked = 0, mismatched = 0, unreadable = 0, archives = 0;
  for (const Pair& pair : pairs) {
    const std::string archive_path = g_root + pair.archive;
    pf::Status status = pf::Status::Ok;
    pf::MpqArchive* archive = pf::MpqArchive::open(archive_path, status);
    if (!archive) continue;
    archives++;

    for (const std::string& name : archive->files()) {
      std::string relative = name;
      for (char& c : relative) {
        if (c == '\\') c = '/';
      }
      std::vector<uint8_t> truth;
      if (!pf::read_file(g_root + pair.tree + "/" + relative, truth)) continue;

      std::vector<uint8_t> got;
      checked++;
      if (!archive->read(name, got)) { unreadable++; continue; }
      if (got != truth) mismatched++;
    }
    delete archive;
  }

  if (!archives) { skip("no MPQ archives"); return; }
  std::printf("     %d files from %d archives, %d unreadable, %d mismatched\n",
              checked, archives, unreadable, mismatched);
  CHECK(checked > 1000);
  CHECK_EQ(unreadable, 0);
  CHECK_EQ(mismatched, 0);
}
TEST(data_source_serves_artwork_from_the_install) {
  // The point of the whole milestone: point at a Warcraft II folder and the
  // artwork comes out, with nothing unpacked and no paths known to the caller.
  pf_data_source* source = pf_data_source_create();
  CHECK(source != nullptr);
  if (!source) return;

  if (pf_data_source_add_directory(source, (g_root + "/reference/app").c_str()) == 0) {
    pf_data_source_free(source);
    skip("no MPQ archives");
    return;
  }

  for (int tileset = 0; tileset < 4; tileset++) {
    pf_status st = PF_OK;
    pf_tileset_art* art = pf_tileset_art_open_source(source, tileset, &st);
    CHECK(art != nullptr);
    if (!art) continue;
    CHECK(pf_tileset_art_megatile_count(art) > 100);
    // A tile that every tileset draws, so this is artwork and not an empty
    // object that happened to construct.
    CHECK(pf_tileset_art_megatile_for(art, 0x0050) >= 0);
    pf_tileset_art_free(art);
  }

  pf_status st = PF_OK;
  pf_sprite* footman = pf_sprite_open_source(source, 0, 0, &st);
  CHECK(footman != nullptr);
  if (footman) {
    CHECK(pf_sprite_width(footman) > 0);
    CHECK(pf_sprite_frame_count(footman) >= 5);
    pf_sprite_free(footman);
  }

  size_t len = 0;
  uint8_t* bytes = pf_data_source_read(source, "art/bgs/forest/forest.ppl", &len);
  CHECK(bytes != nullptr);
  CHECK_EQ(int(len), 768);
  if (bytes) pf_buffer_free(bytes);

  pf_data_source_free(source);
}
TEST(mpq_data_source_prefers_the_patch) {
  pf::DataSource source;
  if (source.add_directory(g_root + "/reference/app") == 0) { skip("no MPQ archives"); return; }
  CHECK(source.archive_count() >= 1);

  // Something only the base archive has.
  std::vector<uint8_t> bytes;
  CHECK(source.read("art\\bgs\\forest\\forest.ppl", bytes));
  CHECK_EQ(int(bytes.size()), 768);

  // Forward slashes work too: every caller in this codebase writes them.
  std::vector<uint8_t> again;
  CHECK(source.read("art/bgs/forest/forest.ppl", again));
  CHECK(again == bytes);

  CHECK(!source.read("no/such/file.dat", bytes));
}
TEST(water_animates_by_cycling_its_palette) {
  // Warcraft II animates water by rotating palette entries, not by holding
  // extra frames. Indices 38 to 47 are used by water and by nothing else in
  // all four tilesets, and in each they run one rise and fall of a wave.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  const int water = pf_tileset_art_megatile_for(art, 0x0020);   // solid deep water
  const int grass = pf_tileset_art_megatile_for(art, 0x0050);   // solid ground
  CHECK(water >= 0);
  CHECK(grass >= 0);

  auto snapshot = [&](int megatile) {
    std::vector<uint32_t> px(32 * 32, 0);
    pf_tileset_art_draw(art, megatile, px.data(), 32);
    return px;
  };

  const std::vector<uint32_t> water0 = snapshot(water);
  const std::vector<uint32_t> grass0 = snapshot(grass);

  CHECK_EQ(pf_tileset_art_water_cycle(), 10);

  int water_moved = 0;
  for (int phase = 1; phase < pf_tileset_art_water_cycle(); phase++) {
    pf_tileset_art_set_water_phase(art, phase);
    if (snapshot(water) != water0) water_moved++;
    // Land must not so much as shimmer: the whole point of the band being
    // water-only is that everything else is untouched.
    CHECK(snapshot(grass) == grass0);
  }
  CHECK(water_moved >= 8);

  // A full turn of the cycle comes back to where it started, so counting up
  // forever is safe.
  pf_tileset_art_set_water_phase(art, pf_tileset_art_water_cycle());
  CHECK(snapshot(water) == water0);
  pf_tileset_art_set_water_phase(art, 0);
  CHECK(snapshot(water) == water0);

  pf_tileset_art_free(art);
}
TEST(mirrored_megatiles_are_exact_mirrors) {
  // The rock-to-coast edge exists in both handednesses, and the tileset does
  // not draw the second one: it lists the same minitiles in reversed column
  // order with a flip bit set on each. So the two must come out pixel-exact
  // mirrors of one another, and they do — all 1,024 of them.
  //
  // Reading that bit as a vertical flip instead turned every affected tile
  // into jumbled eight-pixel blocks. Only 62 of forest's 372 megatiles carry
  // a flipped minitile, which is why the rest of the artwork looked fine.
  if (!have_art()) { skip("no game artwork"); return; }

  pf_tileset_art* art = pf_tileset_art_open(g_bgs_dir.c_str(), 0, nullptr);
  CHECK(art != nullptr);
  if (!art) return;

  const int left = pf_tileset_art_megatile_for(art, 0x0490);
  const int right = pf_tileset_art_megatile_for(art, 0x0441);
  CHECK(left >= 0);
  CHECK(right >= 0);

  std::vector<uint32_t> a(32 * 32, 0), b(32 * 32, 0);
  CHECK(pf_tileset_art_draw(art, left, a.data(), 32) == PF_OK);
  CHECK(pf_tileset_art_draw(art, right, b.data(), 32) == PF_OK);

  int differ = 0;
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      if (a[size_t(y) * 32 + size_t(x)] != b[size_t(y) * 32 + size_t(31 - x)]) differ++;
    }
  }
  CHECK_EQ(differ, 0);

  pf_tileset_art_free(art);
}
TEST(sprites_decode) {
  if (!have_art() || g_unit_dir.empty()) { skip("no unit artwork"); return; }
  pf::TilesetArt* art = pf::TilesetArt::open(g_bgs_dir, 0);
  CHECK(art != nullptr);
  if (!art) return;

  // A building's canvas is exactly its tile footprint, which cross-checks the
  // sprite mapping against the unit data.
  const struct { int id; int tiles; } kBuildings[] = {
      {0x3a, 2}, {0x3c, 3}, {0x4a, 4}, {0x5a, 4}, {0x5c, 3}, {0x65, 4},
  };
  for (const auto& b : kBuildings) {
    pf::Sprite* s = pf::Sprite::open(g_unit_dir, b.id, 0);
    CHECK(s != nullptr);
    if (!s) continue;
    CHECK_EQ(s->width(), b.tiles * 32);
    CHECK_EQ(s->height(), b.tiles * 32);

    std::vector<uint32_t> px(size_t(s->width()) * size_t(s->height()), 0);
    uint32_t palette[256];
    pf::apply_player_color(art->palette(), 0xc81414u, palette);
    CHECK(s->draw_frame(0, palette, px.data()));
    long drawn = 0;
    for (uint32_t p : px) if (p) drawn++;
    CHECK(drawn > 500);
    delete s;
  }

  // Every mapped sprite must exist and decode.
  int loaded = 0, failed = 0;
  for (int id = 0; id < pf::kUnitCount; id++) {
    if (pf::sprite_path_for(id, 0).empty()) continue;
    pf::Sprite* s = pf::Sprite::open(g_unit_dir, id, 0);
    if (!s) { failed++; std::printf("     missing sprite for %s\n", pf::kUnits[id].name); continue; }
    std::vector<uint32_t> px(size_t(s->width()) * size_t(s->height()), 0);
    s->draw_frame(0, art->palette(), px.data());
    loaded++;
    delete s;
  }
  std::printf("     %d sprites loaded, %d missing\n", loaded, failed);
  CHECK_EQ(failed, 0);
  delete art;
}
}  // namespace pft

// ----------------------------------------------------------------- PNG
//
// The encoder writes DEFLATE by hand, so the only test worth having is one
// that reads it back with something that is not the encoder. This is a
// fixed-Huffman inflate, which is all `pf::zlib_compress` emits, kept here in
// the tests rather than the core because nothing shipping needs to decompress.

namespace {

struct Inflater {
  const uint8_t* data;
  size_t length;
  size_t pos = 0;
  int bit = 0;
  bool bad = false;

  int next_bit() {
    if (pos >= length) { bad = true; return 0; }
    const int value = (data[pos] >> bit) & 1;
    if (++bit == 8) { bit = 0; pos++; }
    return value;
  }
  uint32_t bits(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= uint32_t(next_bit()) << i;
    return v;
  }
  /// Huffman codes travel most-significant bit first, unlike everything else.
  uint32_t code(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | uint32_t(next_bit());
    return v;
  }

  /// One literal/length symbol under the fixed table of RFC 1951 section 3.2.6.
  int symbol() {
    uint32_t v = code(7);
    if (v <= 0x17) return int(v) + 256;
    v = (v << 1) | uint32_t(next_bit());
    if (v >= 0x30 && v <= 0xbf) return int(v) - 0x30;
    if (v >= 0xc0 && v <= 0xc7) return int(v) - 0xc0 + 280;
    v = (v << 1) | uint32_t(next_bit());
    if (v >= 0x190 && v <= 0x1ff) return int(v) - 0x190 + 144;
    bad = true;
    return -1;
  }
};

/// Inflate a zlib stream produced by the core. Empty on malformed input.
std::vector<uint8_t> inflate_fixed(const std::vector<uint8_t>& zlib) {
  std::vector<uint8_t> out;
  if (zlib.size() < 6) return out;
  Inflater in{zlib.data() + 2, zlib.size() - 6};   // skip 2-byte header, 4-byte adler

  static const int kLenBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19,
                                 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                                 131, 163, 195, 227, 258};
  static const int kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                  3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const int kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65,
                                  97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                  2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const int kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  for (;;) {
    const int final_block = int(in.bits(1));
    const int kind = int(in.bits(2));
    if (kind != 1 || in.bad) return {};   // this core only emits fixed Huffman
    for (;;) {
      const int sym = in.symbol();
      if (in.bad) return {};
      if (sym == 256) break;
      if (sym < 256) { out.push_back(uint8_t(sym)); continue; }
      const int li = sym - 257;
      if (li < 0 || li > 28) return {};
      const int len = kLenBase[li] + int(in.bits(kLenExtra[li]));
      const int di = int(in.code(5));
      if (di > 29) return {};
      const int dist = kDistBase[di] + int(in.bits(kDistExtra[di]));
      if (dist <= 0 || size_t(dist) > out.size()) return {};
      const size_t from = out.size() - size_t(dist);
      for (int i = 0; i < len; i++) out.push_back(out[from + size_t(i)]);
    }
    if (final_block) break;
  }
  return out;
}

uint32_t be32_at(const std::vector<uint8_t>& v, size_t at) {
  return (uint32_t(v[at]) << 24) | (uint32_t(v[at + 1]) << 16) |
         (uint32_t(v[at + 2]) << 8) | uint32_t(v[at + 3]);
}

}  // namespace

/**
 * A PNG the core writes decodes back to the pixels that went in.
 *
 * Read with a decoder that shares nothing with the encoder — an independent
 * fixed-Huffman inflate above, plus the container walked chunk by chunk with
 * every CRC recomputed. A hand-rolled DEFLATE that only its own author can
 * read is not an encoder, it is a private format.
 */
TEST(a_png_decodes_back_to_its_pixels) {
  // Flat runs, a gradient and a hard checker edge: the three things filtering
  // and matching each get wrong in different ways.
  const int w = 137, h = 91;
  std::vector<uint32_t> pixels(size_t(w) * size_t(h));
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const uint32_t r = uint32_t(x * 255 / w);
      const uint32_t g = uint32_t(y * 255 / h);
      const uint32_t b = ((x / 8) + (y / 8)) % 2 ? 240u : 16u;
      const uint32_t a = x < w / 2 ? 255u : 128u;
      pixels[size_t(y) * size_t(w) + size_t(x)] = r | (g << 8) | (b << 16) | (a << 24);
    }
  }

  size_t len = 0;
  uint8_t* encoded = pf_png_encode(pixels.data(), w, h, &len);
  CHECK(encoded != nullptr);
  if (!encoded) return;
  const std::vector<uint8_t> png(encoded, encoded + len);
  pf_buffer_free(encoded);

  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  CHECK(std::memcmp(png.data(), signature, 8) == 0);

  std::vector<uint8_t> idat;
  std::string order;
  size_t p = 8;
  while (p + 12 <= png.size()) {
    const uint32_t n = be32_at(png, p);
    const std::string type(png.begin() + long(p) + 4, png.begin() + long(p) + 8);
    order += type + " ";
    // Every chunk carries a CRC over its type and payload; a wrong one is how
    // a decoder decides the file is corrupt.
    CHECK_EQ(be32_at(png, p + 8 + n), pf::crc32(png.data() + p + 4, n + 4));
    if (type == "IHDR") {
      CHECK_EQ(int(be32_at(png, p + 8)), w);
      CHECK_EQ(int(be32_at(png, p + 12)), h);
      CHECK_EQ(int(png[p + 16]), 8);   // bit depth
      CHECK_EQ(int(png[p + 17]), 6);   // truecolour with alpha
    } else if (type == "IDAT") {
      idat.insert(idat.end(), png.begin() + long(p) + 8,
                  png.begin() + long(p) + 8 + long(n));
    }
    p += 12 + n;
  }
  CHECK(order == "IHDR IDAT IEND ");
  CHECK_EQ(p, png.size());

  const std::vector<uint8_t> raw = inflate_fixed(idat);
  const size_t stride = size_t(w) * 4;
  CHECK_EQ(raw.size(), size_t(h) * (stride + 1));
  if (raw.size() != size_t(h) * (stride + 1)) return;

  // The declared Adler-32 has to match what actually came out.
  CHECK_EQ(be32_at(idat, idat.size() - 4), pf::adler32(raw.data(), raw.size()));

  int wrong = 0;
  for (int y = 0; y < h; y++) {
    const uint8_t* row = raw.data() + size_t(y) * (stride + 1);
    CHECK_EQ(int(row[0]), 1);   // Sub, on every row
    std::vector<uint8_t> line(row + 1, row + 1 + stride);
    for (size_t i = 4; i < stride; i++) line[i] = uint8_t(line[i] + line[i - 4]);
    for (int x = 0; x < w; x++) {
      const uint32_t want = pixels[size_t(y) * size_t(w) + size_t(x)];
      for (int c = 0; c < 4; c++) {
        if (line[size_t(x) * 4 + size_t(c)] != uint8_t((want >> (8 * c)) & 0xff)) {
          wrong++;
        }
      }
    }
  }
  CHECK_EQ(wrong, 0);
  std::printf("     %d x %d in %zu bytes, %.1fx smaller than raw\n", w, h, len,
              double(pixels.size() * 4) / double(len));
}

TEST(png_rejects_nothing_to_encode) {
  size_t len = 123;
  CHECK(pf_png_encode(nullptr, 4, 4, &len) == nullptr);
  CHECK_EQ(len, size_t(0));
  const uint32_t one = 0;
  CHECK(pf_png_encode(&one, 0, 4, &len) == nullptr);
  CHECK(pf_png_encode(&one, 4, -1, &len) == nullptr);
  // One pixel is a legal image and must survive the same path.
  uint8_t* tiny = pf_png_encode(&one, 1, 1, &len);
  CHECK(tiny != nullptr);
  CHECK(len > 8);
  pf_buffer_free(tiny);
}
