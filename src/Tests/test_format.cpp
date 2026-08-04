// PUD file model: sections, layouts, components, clipboard, resize
//
// See harness.hpp for the assertions, fixtures and registration.

#include "harness.hpp"

TEST_GROUP("format")

namespace pft {

TEST(binary_little_endian) {
  pf::Writer w;
  w.u8(0x12);
  w.u16(0x3456);
  w.u32(0x789abcde);
  const auto& b = w.buffer();
  CHECK_EQ(int(b.size()), 7);
  CHECK_EQ(int(b[1]), 0x56);  // low byte first
  CHECK_EQ(int(b[2]), 0x34);
  CHECK_EQ(int(b[3]), 0xde);
  CHECK_EQ(int(b[6]), 0x78);

  pf::Reader r(b.data(), b.size());
  CHECK_EQ(int(r.u8()), 0x12);
  CHECK_EQ(int(r.u16()), 0x3456);
  CHECK_EQ(r.u32(), 0x789abcdeu);
  CHECK(!r.overrun());
}
TEST(binary_bounds_checked) {
  uint8_t data[3] = {1, 2, 3};
  pf::Reader r(data, sizeof(data));
  r.u16();
  CHECK_EQ(r.u32(), 0u);  // reads past the end return 0
  CHECK(r.overrun());
}
TEST(a_units_facing_travels_with_it_rather_than_with_where_it_stands) {
  // The format stores no facing, so which way a unit is drawn is invented by
  // hashing a position — and hashing its *current* position meant a unit spun
  // on the spot as it was dragged, a frame per tile the pointer crossed.
  pf::Status s;
  pf::Map* map = pf::Map::create(32, 32, 0, s);
  CHECK(map != nullptr);
  if (!map) return;

  const int index = map->add_unit(10, 12, 0, 0, 0);   // a footman
  CHECK(index >= 0);
  CHECK_EQ(int(map->units()[0].seed_x), 10);
  CHECK_EQ(int(map->units()[0].seed_y), 12);

  // Dragged across the map: the seed does not follow, so the drawn frame does
  // not change.
  const int was = pf_unit_facing(map->units()[0].seed_x, map->units()[0].seed_y, 0, 8);
  map->units()[0].x = 25;
  map->units()[0].y = 4;
  CHECK_EQ(int(map->units()[0].seed_x), 10);
  CHECK_EQ(int(map->units()[0].seed_y), 12);
  CHECK_EQ(pf_unit_facing(map->units()[0].seed_x, map->units()[0].seed_y, 0, 8), was);

  // Saved and reopened, the seed comes back from the stored position: there is
  // nowhere in the format to keep it, and that is what makes an untouched map
  // draw exactly as it always did.
  std::vector<uint8_t> bytes = map->serialize();
  pf::Map* parsed = pf::Map::parse(bytes.data(), bytes.size(), s);
  CHECK(parsed != nullptr);
  if (parsed) {
    CHECK_EQ(int(parsed->units()[0].seed_x), 25);
    CHECK_EQ(int(parsed->units()[0].seed_y), 4);
    delete parsed;
  }
  delete map;
}
TEST(map_create_and_round_trip) {
  pf::Status s;
  pf::Map* map = pf::Map::create(64, 48, 2, s);
  CHECK(map != nullptr);
  if (!map) return;
  CHECK_EQ(map->width(), 64);
  CHECK_EQ(map->height(), 48);
  CHECK_EQ(map->tileset(), 2);
  CHECK_EQ(int(map->tiles().size()), 64 * 48);

  map->add_unit(5, 6, 0x5e, 0, 0);
  map->add_unit(10, 10, 0x5c, 15, 20);
  map->set_description("Round Trip");

  std::vector<uint8_t> bytes = map->serialize();
  pf::Map* parsed = pf::Map::parse(bytes.data(), bytes.size(), s);
  CHECK(parsed != nullptr);
  if (parsed) {
    CHECK_EQ(parsed->width(), 64);
    CHECK_EQ(parsed->height(), 48);
    CHECK_EQ(parsed->tileset(), 2);
    CHECK(parsed->description() == "Round Trip");
    CHECK_EQ(int(parsed->units().size()), 2);
    CHECK_EQ(int(parsed->units()[1].value), 20);
    // Serializing the parsed copy must reproduce the same bytes.
    std::vector<uint8_t> again = parsed->serialize();
    CHECK(again == bytes);
    delete parsed;
  }
  delete map;
}
TEST(map_rejects_junk) {
  pf::Status s;
  const char* junk = "not a pud file at all!!!!";
  CHECK(pf::Map::parse(reinterpret_cast<const uint8_t*>(junk), std::strlen(junk), s) == nullptr);
  CHECK(s == pf::Status::NotAPud);

  CHECK(pf::Map::parse(nullptr, 0, s) == nullptr);
  CHECK(pf::Map::create(0, 10, 0, s) == nullptr);
  CHECK(s == pf::Status::UnsupportedSize);
  CHECK(pf::Map::create(200, 200, 0, s) == nullptr);
}
TEST(description_keeps_hidden_bytes) {
  pf::Status s;
  pf::Map* map = pf::Map::create(32, 32, 0, s);
  if (!map) { CHECK(false); return; }
  map->set_description("Visible");
  std::vector<uint8_t> bytes = map->serialize();

  // Splice text in after the NUL, the way seven shipped maps do.
  // DESC payload starts 8 (TYPE hdr) + 16 + 8 + 2 + 8 = 42 bytes in.
  size_t desc = 8 + 16 + 8 + 2 + 8;
  const char* hidden = "[Expert Edition]";
  std::memcpy(&bytes[desc + 10], hidden, std::strlen(hidden));

  pf::Map* parsed = pf::Map::parse(bytes.data(), bytes.size(), s);
  CHECK(parsed != nullptr);
  if (!parsed) { delete map; return; }
  CHECK(parsed->description() == "Visible");  // reading stops at the NUL
  CHECK(parsed->serialize() == bytes);        // untouched description is exact

  CHECK(parsed->set_description("Renamed"));
  CHECK(parsed->description() == "Renamed");
  CHECK(parsed->serialize() != bytes);
  delete parsed;
  delete map;
}
TEST(description_is_code_page_437) {
  // The game draws DESC with a bitmap font indexed by byte value, and that
  // font's glyphs above 0x7f are CP437's. So one character is one byte, and
  // which byte is not negotiable.
  std::string bytes;
  CHECK(pf::desc_encode("Gr\xc3\xb6\xc3\x9f""e", bytes));   // "Größe"
  CHECK_EQ(int(bytes.size()), 5);
  CHECK_EQ(int(uint8_t(bytes[2])), 0x94);   // ö
  CHECK_EQ(int(uint8_t(bytes[3])), 0xe1);   // ß
  CHECK(pf::desc_decode(reinterpret_cast<const uint8_t*>(bytes.data()),
                        bytes.size()) == "Gr\xc3\xb6\xc3\x9f""e");

  // Every byte decodes, so no file can be unreadable; 0xa9 is the byte three
  // corpus maps hold where their authors typed © in a Windows editor.
  const uint8_t high[3] = {0xa9, 0xae, 0xde};
  const std::string shown = pf::desc_decode(high, 3);
  CHECK(shown == "\xe2\x8c\x90\xc2\xab\xe2\x96\x90");       // ⌐ « ▐
  std::string again;
  CHECK(pf::desc_encode(shown, again));
  CHECK(again == std::string(reinterpret_cast<const char*>(high), 3));

  // Nothing outside the set, and nothing that is not text.
  CHECK(!pf::desc_encode("\xe6\x97\xa5\xe6\x9c\xac", bytes));   // 日本
  CHECK(!pf::desc_encode("\xe2\x82\xac", bytes));               // €, CP1252 only
  CHECK(!pf::desc_encode("two\nlines", bytes));
  CHECK(!pf::desc_encode("\xff\xfe truncated", bytes));         // not UTF-8
  CHECK(!pf::desc_encode("\xc0\x80", bytes));                   // overlong NUL
}
TEST(description_round_trips_exactly) {
  pf::Status s;
  pf::Map* map = pf::Map::create(32, 32, 0, s);
  if (!map) { CHECK(false); return; }

  // 31 accented characters: 31 bytes in the file, and the byte count the
  // client shows is the character count the person typed. Before DESC had a
  // character set this was 62 UTF-8 bytes, half of which were dropped on save.
  const std::string accented =
      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
      "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
  CHECK(map->set_description(accented));
  CHECK(map->description() == accented);
  CHECK_EQ(int(map->description_bytes()[30]), 0x82);   // é
  CHECK_EQ(int(map->description_bytes()[31]), 0);      // the terminator survives

  std::vector<uint8_t> written = map->serialize();
  pf::Map* parsed = pf::Map::parse(written.data(), written.size(), s);
  CHECK(parsed != nullptr);
  if (parsed) {
    CHECK(parsed->description() == accented);
    CHECK(parsed->serialize() == written);
    delete parsed;
  }

  // One character more is refused, not trimmed: the map keeps what it had.
  CHECK(!map->set_description(accented + "\xc3\xa9"));
  CHECK(map->description() == accented);
  CHECK(!map->set_description("\xe6\x97\xa5 is not storable"));
  CHECK(map->description() == accented);
  delete map;
}
TEST(udta_field_kinds) {
  auto field = [](const char* name) {
    for (int i = 0; i < pf::kUdtaSegmentCount; i++) {
      if (std::string(pf::kUdtaSegments[i].name) == name) return i;
    }
    return -1;
  };
  const int flags = field("flags");
  const int missile = field("missileWeapon");
  const int movement = field("unitType");
  const int mouse = field("secondMouseButton");

  CHECK(pf::udta_field_kind(field("hitPoints")) == pf::kUdtaNumber);
  CHECK(pf::udta_field_kind(field("hasMagic")) == pf::kUdtaBool);
  CHECK(pf::udta_field_kind(missile) == pf::kUdtaEnum);
  CHECK(pf::udta_field_kind(flags) == pf::kUdtaFlags);
  CHECK(pf::udta_field_options(field("hitPoints"), nullptr) == nullptr);

  // The bit labels must agree with the bits the core itself acts on, or the
  // sheet would describe a different unit than the placement rules see.
  int count = 0;
  const pf::UdtaOption* bits = pf::udta_field_options(flags, &count);
  auto labelled = [&](uint32_t mask) {
    for (int i = 0; i < count; i++) if (bits[i].value == mask) return std::string(bits[i].label);
    return std::string();
  };
  CHECK(labelled(1u << 5) == "Building");
  CHECK(labelled(1u << 16) == "Shore building");
  CHECK(labelled(1u << 11) == "Gives oil");
  CHECK(labelled(1u << 13).empty());   // no known meaning; deliberately absent

  // Every value the retail table actually uses needs a label, otherwise a
  // stock unit would show "Unknown" in its own dropdown.
  for (const int f : {missile, movement, mouse}) {
    const int units = f == mouse ? 58 : 110;
    int n = 0;
    const pf::UdtaOption* options = pf::udta_field_options(f, &n);
    for (int unit = 0; unit < units; unit++) {
      if (pf::kUnits[unit].unused) continue;
      const int64_t value = pf_udta_default_field(f, unit, 0);
      bool known = false;
      for (int i = 0; i < n; i++) known |= int64_t(options[i].value) == value;
      CHECK(known);
    }
  }
}
TEST(alow_upgrade_bits_are_read_out_of_ugrd_flags) {
  // The claim: UGRD's flags field stores the single ALOW bit an upgrade
  // occupies. The check is the spell half, whose ALOW names were transcribed
  // from the format notes long before anyone looked at UGRD — if the flags
  // field means what this says, it must reproduce them exactly.
  int flags = -1;
  for (int i = 0; i < pf_ugrd_field_count(); i++) {
    if (std::string(pf_ugrd_field_name(i)) == "flags") flags = i;
  }
  CHECK(flags >= 0);

  auto single_bit = [](int64_t v) {
    if (v <= 0 || (v & (v - 1)) != 0) return -1;
    int bit = 0;
    while ((v >> bit) != 1) bit++;
    return bit;
  };

  // Every upgrade sets exactly one bit. That is what makes it a slot number.
  for (int u = 0; u < pf::kUpgradeCount; u++) {
    CHECK(single_bit(pf_ugrd_default_field(flags, u)) >= 0);
  }

  // Spells, ids 34 to 51, against the independently sourced table.
  int agreed = 0;
  for (int u = 34; u < pf::kUpgradeCount; u++) {
    const int bit = single_bit(pf_ugrd_default_field(flags, u));
    const char* named = pf_alow_bit_name(1, bit);
    if (named && std::string(named) == pf_upgrade_name(u)) agreed++;
  }
  CHECK_EQ(agreed, 18);

  // Human and orc research share a slot, which is why the names are pairs.
  CHECK_EQ(single_bit(pf_ugrd_default_field(flags, 0)),    // Sword 1
           single_bit(pf_ugrd_default_field(flags, 2)));   // Axe 1
  CHECK_EQ(single_bit(pf_ugrd_default_field(flags, 24)),   // Train Rangers
           single_bit(pf_ugrd_default_field(flags, 28)));  // Train Berserkers

  // The upgrade blocks are now named, and both name the same bits.
  CHECK(pf_alow_bit_name(4, 2) != nullptr);
  CHECK(std::string(pf_alow_bit_name(4, 2)) == "Sword 1 / Axe 1");
  CHECK(std::string(pf_alow_bit_name(5, 2)) == pf_alow_bit_name(4, 2));
  CHECK(pf_alow_bit_name(4, 10) == nullptr);   // unused by the game
  CHECK(pf_alow_bit_name(4, 32) == nullptr);

  // The game's own quirk: Arrow 2 stores Arrow 1's bit, where the orc Throwing Axe 2
  // correctly stores its own. Asserted so it stays a known oddity.
  CHECK_EQ(single_bit(pf_ugrd_default_field(flags, 5)), 0);   // Arrow 2
  CHECK_EQ(single_bit(pf_ugrd_default_field(flags, 7)), 1);   // Throwing Axe 2
}
TEST(upgrade_defaults_come_from_the_games_own_table) {
  // A map with no UGRD used to show -1 in every box under a note claiming to
  // show the game's defaults. The defaults were in the binary all along; they
  // just had no way out.
  int icon = -1, gold = -1, time = -1;
  for (int i = 0; i < pf_ugrd_field_count(); i++) {
    const std::string name = pf_ugrd_field_name(i);
    if (name == "icon") icon = i;
    if (name == "gold cost") gold = i;
    if (name == "upgrade time") time = i;
  }
  CHECK(icon >= 0);

  // Sword 1: the first upgrade, and one whose cost is well known.
  CHECK(pf_ugrd_default_field(icon, 0) >= 0);
  if (gold >= 0) CHECK_EQ(pf_ugrd_default_field(gold, 0), 800);
  if (time >= 0) CHECK_EQ(pf_ugrd_default_field(time, 0), 200);

  // Every upgrade has an icon, and every icon is inside the artwork.
  for (int u = 0; u < pf::kUpgradeCount; u++) {
    const int64_t frame = pf_ugrd_default_field(icon, u);
    CHECK(frame >= 0 && frame < 196);
  }
  CHECK_EQ(pf_ugrd_default_field(-1, 0), -1);
  CHECK_EQ(pf_ugrd_default_field(icon, 999), -1);
}
TEST(alow_bits_are_named_consistently) {
  // Only three corpus maps carry an ALOW section, so the evidence for these
  // meanings is thin. What can be checked is that they are consistent with the
  // rest of the format, and that editing one bit leaves the others alone.

  // The spell bits are exactly the eighteen spell upgrades of UGRD, in UGRD
  // order. If that ever stops holding, one of the two tables has drifted.
  std::vector<std::string> from_bits;
  for (int bit = 0; bit < 32; bit++) {
    const char* name = pf_alow_bit_name(1, bit);
    if (name) from_bits.push_back(name);
  }
  std::vector<std::string> spells_in_ugrd;
  for (int i = 34; i < pf::kUpgradeCount; i++) spells_in_ugrd.push_back(pf::kUpgrades[i]);
  CHECK_EQ(int(from_bits.size()), 18);
  CHECK(from_bits == spells_in_ugrd);

  // Blocks 1 to 3 are all spells and must agree with each other.
  for (int block = 2; block <= 3; block++) {
    for (int bit = 0; bit < 32; bit++) {
      const char* a = pf_alow_bit_name(1, bit);
      const char* b = pf_alow_bit_name(block, bit);
      CHECK((a == nullptr) == (b == nullptr));
      if (a && b) CHECK(std::string(a) == b);
    }
  }

  // Every unit bit names units that exist. The paired ones name two.
  int named_units = 0;
  for (int bit = 0; bit < 32; bit++) {
    const char* name = pf_alow_bit_name(0, bit);
    if (!name) continue;
    named_units++;
    std::string text = name;
    const size_t slash = text.find(" / ");
    std::vector<std::string> parts;
    if (slash == std::string::npos) parts.push_back(text);
    else { parts.push_back(text.substr(0, slash)); parts.push_back(text.substr(slash + 3)); }
    for (const std::string& part : parts) {
      bool found = false;
      for (int u = 0; u < pf::kUnitCount; u++) {
        if (std::string(pf::kUnits[u].name).find(part) != std::string::npos) found = true;
      }
      if (!found) std::printf("     unit bit %d names nothing: %s\n", bit, part.c_str());
      CHECK(found);
    }
  }
  CHECK_EQ(named_units, 30);        // 32 bits, two with no recovered meaning
  CHECK(pf_alow_bit_name(0, 13) == nullptr);
  CHECK(pf_alow_bit_name(0, 31) == nullptr);

  // The upgrade blocks are named now, from UGRD's flags field. Both index the
  // same bits, so they must agree with each other exactly.
  int named_upgrades = 0;
  for (int bit = 0; bit < 32; bit++) {
    const char* a = pf_alow_bit_name(4, bit);
    const char* b = pf_alow_bit_name(5, bit);
    CHECK((a == nullptr) == (b == nullptr));
    if (a && b) CHECK(std::string(a) == b);
    if (a) named_upgrades++;
  }
  CHECK_EQ(named_upgrades, 17);     // 32 bits, fifteen the game never uses
}
TEST(alow_bits_resolve_to_the_thing_they_name) {
  // A name is enough to read a bit; drawing one needs the unit or the upgrade
  // it stands for. The two answers come from different places — a table for
  // the units, a search of UGRD's flags for the rest — so the check is that
  // both agree with the names, which came from somewhere else again.

  // Units. The bit names a pair and the table records the human half, so the
  // first part of the name has to appear in the unit it resolved to.
  for (int bit = 0; bit < 32; bit++) {
    const char* name = pf_alow_bit_name(0, bit);
    const int unit = pf_alow_bit_unit(0, bit);
    CHECK((name == nullptr) == (unit < 0));
    if (!name) continue;
    std::string text = name;
    const size_t slash = text.find(" / ");
    const std::string human = slash == std::string::npos ? text : text.substr(0, slash);
    CHECK(unit >= 0 && unit < pf::kUnitCount);
    const std::string unit_name = pf::kUnits[unit].name;
    if (unit_name.find(human) == std::string::npos) {
      std::printf("     unit bit %d says %s, resolved to %s\n", bit, human.c_str(),
                  unit_name.c_str());
    }
    CHECK(unit_name.find(human) != std::string::npos);
    CHECK(pf::kUnits[unit].race == 'h' || pf::kUnits[unit].race == 'n');
    CHECK(!pf::kUnits[unit].unused);
  }
  // Only block 0 answers this question.
  for (int block = 1; block < 6; block++) CHECK(pf_alow_bit_unit(block, 0) < 0);

  // Spells resolve exactly: the eighteen names *are* the eighteen upgrade
  // names, so anything but an exact match means the search found the wrong
  // half of the table.
  for (int block = 1; block <= 3; block++) {
    for (int bit = 0; bit < 32; bit++) {
      const char* name = pf_alow_bit_name(block, bit);
      const int upgrade = pf_alow_bit_upgrade(block, bit);
      CHECK((name == nullptr) == (upgrade < 0));
      if (!name) continue;
      CHECK(upgrade >= 34);          // the spell half of UGRD
      CHECK(std::string(pf_upgrade_name(upgrade)) == name);
    }
  }

  // Researches resolve into the other half, and both blocks give the same
  // answer because they index the same bits.
  for (int bit = 0; bit < 32; bit++) {
    const char* name = pf_alow_bit_name(4, bit);
    const int upgrade = pf_alow_bit_upgrade(4, bit);
    CHECK((name == nullptr) == (upgrade < 0));
    CHECK_EQ(upgrade, pf_alow_bit_upgrade(5, bit));
    if (!name) continue;
    CHECK(upgrade >= 0 && upgrade < 34);
    // The name is a pair and the search takes whichever half UGRD lists
    // first, so either one is a correct answer — unlike the unit table, there
    // is no race to prefer, since the two researches share the slot.
    std::string text = name;
    const size_t slash = text.find(" / ");
    std::vector<std::string> parts;
    if (slash == std::string::npos) parts.push_back(text);
    else { parts.push_back(text.substr(0, slash)); parts.push_back(text.substr(slash + 3)); }
    const std::string upgrade_name = pf_upgrade_name(upgrade);
    bool matched = false;
    for (const std::string& part : parts) {
      if (upgrade_name.find(part) != std::string::npos) matched = true;
    }
    if (!matched) {
      std::printf("     upgrade bit %d says %s, resolved to %s\n", bit, name,
                  upgrade_name.c_str());
    }
    CHECK(matched);
  }

  // The game's own quirk, pinned so it stays visible: Arrow 2 stores bit 0,
  // the same bit as Arrow 1, where Throwing Axe 2 correctly stores bit 1. So bit 1
  // resolves to the orc half — there is no human research on it to find.
  CHECK(std::string(pf_upgrade_name(pf_alow_bit_upgrade(4, 1))) == "Throwing Axe 2");
  CHECK(pf_alow_bit_upgrade(0, 0) < 0);
  CHECK(pf_alow_bit_upgrade(4, 32) < 0);
}
TEST(udta_layout_sums_to_the_section_size) {
  // Every offset is derived by summing the widths before it, so if the table
  // is wrong the total gives it away immediately. 5696 without swampFrames,
  // 5950 with — exactly the two lengths the format has.
  int total = 0, without_swamp = 0;
  for (int i = 0; i < pf::kUdtaSegmentCount; i++) {
    const pf::UdtaSegment& seg = pf::kUdtaSegments[i];
    CHECK(seg.width == 1 || seg.width == 2 || seg.width == 4);
    CHECK(seg.components == 1 || seg.components == 2);
    CHECK(seg.elements % seg.components == 0);
    CHECK_EQ(pf::udta_offset(i), total);
    const int bytes = int(seg.width) * int(seg.elements);
    if (std::string(seg.name) != "swampFrames") without_swamp += bytes;
    total += bytes;
  }
  CHECK_EQ(without_swamp, pf::kUdtaSize);
  CHECK_EQ(total, pf::kUdtaSizeWithSwamp);

  // Per-unit fields must cover every unit id.
  for (int i = 0; i < pf::kUdtaSegmentCount; i++) {
    const pf::UdtaSegment& seg = pf::kUdtaSegments[i];
    if (!seg.per_unit) continue;
    const int units = seg.elements / seg.components;
    CHECK(units == pf::kUnitCount || units == 58);
  }
}
TEST(udta_fields_read_the_values_the_footprints_came_from) {
  // unitSize is decoded independently into the footprint cache, so the two
  // must agree — a cross-check that the offsets land where they should.
  if (!have_corpus()) { skip("no map corpus"); return; }
  std::vector<uint8_t> bytes;
  if (!pf::read_file(g_corpus[0], bytes)) { CHECK(false); return; }
  pf::Status s;
  pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
  if (!map) { CHECK(false); return; }

  int size_field = -1;
  for (int i = 0; i < pf::kUdtaSegmentCount; i++) {
    if (std::string(pf::kUdtaSegments[i].name) == "unitSize") size_field = i;
  }
  CHECK(size_field >= 0);

  if (map->unit_data().present) {
    const int offset = pf::udta_offset(size_field);
    const auto& raw = map->unit_data().raw;
    for (int unit = 0; unit < pf::kUnitCount; unit++) {
      const size_t at = size_t(offset) + size_t(unit) * 4;
      if (at + 4 > raw.size()) break;
      const uint16_t x = uint16_t(raw[at] | (raw[at + 1] << 8));
      CHECK_EQ(int(x), int(map->unit_data().size_x[unit]));
    }
  }
  delete map;
}
TEST(ugrd_and_alow_layouts_sum_to_their_section_sizes) {
  // 782 is the only valid UGRD length, so the sum is a complete check.
  int total = 0;
  for (int i = 0; i < pf::kUgrdSegmentCount; i++) {
    const pf::UdtaSegment& seg = pf::kUgrdSegments[i];
    CHECK(seg.width == 1 || seg.width == 2 || seg.width == 4);
    CHECK_EQ(pf::segment_offset(pf::kUgrdSegments, pf::kUgrdSegmentCount, i), total);
    if (seg.per_unit) CHECK_EQ(int(seg.elements), pf::kUpgradeCount);
    total += int(seg.width) * int(seg.elements);
  }
  CHECK_EQ(total, pf::kUgrdSize);

  // ALOW is six blocks of one uint32 per player.
  CHECK_EQ(pf::kAlowSize, 384);
  CHECK_EQ(pf::kAlowBlocks * pf::kPlayerCount * 4, 384);
  for (int i = 0; i < pf::kAlowBlocks; i++) CHECK(pf::kAlowBlockNames[i] != nullptr);
}
TEST(udta_layout_matches_the_parser) {
  // pud.cpp computes the unitSize offset by hand to decode footprints, and
  // constants.cpp derives every field offset from a table. Two sources of
  // truth for the same number is exactly how the JavaScript core drifted, so
  // they are pinned to each other here.
  int size_field = -1;
  for (int i = 0; i < pf::kUdtaSegmentCount; i++) {
    if (std::string(pf::kUdtaSegments[i].name) == "unitSize") size_field = i;
  }
  CHECK(size_field >= 0);

  const int by_hand = 2 + pf::kUnitCount * 2 + 508 * 2 + pf::kUnitCount * 4 +
                      pf::kUnitCount * 2 + pf::kUnitCount * 5;
  CHECK_EQ(pf::udta_offset(size_field), by_hand);
}
TEST(resize_grows_crops_and_reports_lost_units) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  // Mark a tile so the content can be found again after moving.
  CHECK_EQ(pf_map_paint_terrain(map, 4, 4, PF_TERRAIN_WATER_DARK, 1), PF_OK);
  const int marker = pf_map_tile_at(map, 4, 4);
  CHECK(pf_map_add_unit(map, 4, 4, 0, 0, 0) >= 0);
  CHECK(pf_map_add_unit(map, 30, 30, 0, 0, 0) >= 0);

  // Grow with a positive offset: everything shifts, nothing is lost.
  CHECK_EQ(pf_map_resize(map, 64, 48, 8, 4, &st), 0);
  CHECK_EQ(int(st), int(PF_OK));
  CHECK_EQ(pf_map_width(map), 64);
  CHECK_EQ(pf_map_height(map), 48);
  CHECK_EQ(pf_map_tile_at(map, 12, 8), marker);
  CHECK_EQ(pf_map_unit_count(map), 2);

  pf_unit u{};
  CHECK_EQ(pf_map_unit(map, 0, &u), PF_OK);
  CHECK_EQ(int(u.x), 12);
  CHECK_EQ(int(u.y), 8);

  // New area is filled, not left as garbage.
  CHECK(pf_map_tile_at(map, 60, 44) >= 0);

  // Crop hard enough to lose the far unit, and it must be reported.
  const int dropped = pf_map_resize(map, 20, 20, 0, 0, &st);
  CHECK_EQ(int(st), int(PF_OK));
  CHECK_EQ(dropped, 1);
  CHECK_EQ(pf_map_unit_count(map), 1);
  CHECK_EQ(pf_map_tile_at(map, 12, 8), marker);

  // Refuse impossible sizes rather than corrupting the map.
  CHECK_EQ(pf_map_resize(map, 0, 20, 0, 0, &st), -1);
  CHECK_EQ(int(st), int(PF_ERR_UNSUPPORTED_SIZE));
  CHECK_EQ(pf_map_resize(map, 129, 20, 0, 0, &st), -1);
  CHECK_EQ(pf_map_width(map), 20);   // unchanged after a refusal

  pf_map_free(map);
}
TEST(resized_maps_stay_loadable) {
  // A resized map must still be a valid PUD: serialize it and parse it back.
  if (!have_corpus()) { skip("no map corpus"); return; }

  int checked = 0;
  for (size_t i = 0; i < g_corpus.size(); i += 40) {
    std::vector<uint8_t> bytes;
    if (!pf::read_file(g_corpus[i], bytes)) continue;
    pf_status st = PF_OK;
    pf_map* map = pf_map_open(bytes.data(), bytes.size(), &st);
    if (!map) continue;

    const int w = pf_map_width(map), h = pf_map_height(map);
    if (w + 8 <= 128 && h + 8 <= 128) {
      CHECK(pf_map_resize(map, w + 8, h + 8, 4, 4, &st) >= 0);
      size_t len = 0;
      uint8_t* out = pf_map_save(map, &len, &st);
      CHECK(out != nullptr);
      if (out) {
        pf_map* again = pf_map_open(out, len, &st);
        CHECK(again != nullptr);
        if (again) {
          CHECK_EQ(pf_map_width(again), w + 8);
          CHECK_EQ(pf_map_height(again), h + 8);
          CHECK_EQ(pf_map_warning_count(again), 0);
          pf_map_free(again);
        }
        pf_buffer_free(out);
      }
      checked++;
    }
    pf_map_free(map);
  }
  std::printf("     %d maps resized and re-parsed\n", checked);
  CHECK(checked > 0);
}
TEST(clipboard_transforms_are_involutions) {
  // Four quarter turns, or two flips, must return the original exactly.
  // These identities catch off-by-one errors in the corner arithmetic that
  // eyeballing a rotated map would not.
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(24, 16, 0, &st);
  if (!map) { CHECK(false); return; }

  // Something asymmetric, so a wrong transform cannot look right by accident.
  for (int y = 2; y < 6; y++) {
    for (int x = 2; x < 10; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
  }
  for (int x = 3; x < 6; x++) pf_map_paint_terrain(map, x, 9, PF_TERRAIN_FOREST, 1);
  CHECK(pf_map_add_unit(map, 14, 12, 0, 0, 0) >= 0);   // dry ground

  pf_clipboard* clip = pf_clipboard_copy(map, 0, 0, 24, 16, 1, 1);
  CHECK(clip != nullptr);
  if (!clip) { pf_map_free(map); return; }

  pf_clipboard* reference = pf_clipboard_copy(map, 0, 0, 24, 16, 1, 1);
  CHECK(reference != nullptr);

  const int w0 = pf_clipboard_width(clip), h0 = pf_clipboard_height(clip);
  CHECK_EQ(w0, 24);
  CHECK_EQ(h0, 16);
  CHECK_EQ(pf_clipboard_unit_count(clip), 1);

  // One turn swaps the dimensions.
  CHECK_EQ(pf_clipboard_rotate(clip, 1), PF_OK);
  CHECK_EQ(pf_clipboard_width(clip), 16);
  CHECK_EQ(pf_clipboard_height(clip), 24);

  // Four turns in total restore everything.
  CHECK_EQ(pf_clipboard_rotate(clip, 3), PF_OK);
  CHECK_EQ(pf_clipboard_width(clip), w0);
  CHECK_EQ(pf_clipboard_height(clip), h0);

  // Paste both into fresh maps and compare the results tile for tile.
  auto paste_into = [](pf_clipboard* c) {
    pf_status s2 = PF_OK;
    pf_map* dst = pf_map_create(24, 16, 0, &s2);
    pf_map_paste(dst, c, 0, 0);
    return dst;
  };
  pf_map* a = paste_into(clip);
  pf_map* b = paste_into(reference);
  CHECK(a != nullptr && b != nullptr);
  if (a && b) {
    int same = 0, total = 0;
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 24; x++) {
        total++;
        // Same position in both maps, so the variation salt matches and tile
        // ids are directly comparable here.
        if (pf_map_tile_at(a, x, y) == pf_map_tile_at(b, x, y)) same++;
      }
    }
    CHECK_EQ(same, total);
    CHECK_EQ(pf_map_unit_count(a), pf_map_unit_count(b));
  }
  pf_map_free(a);
  pf_map_free(b);

  // Flip twice is the identity too.
  CHECK_EQ(pf_clipboard_flip(clip), PF_OK);
  CHECK_EQ(pf_clipboard_flip(clip), PF_OK);
  CHECK_EQ(pf_clipboard_mirror(clip), PF_OK);
  CHECK_EQ(pf_clipboard_mirror(clip), PF_OK);
  pf_map* c = paste_into(clip);
  pf_map* d = paste_into(reference);
  if (c && d) {
    int same = 0;
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 24; x++) {
        if (pf_map_tile_at(c, x, y) == pf_map_tile_at(d, x, y)) same++;
      }
    }
    CHECK_EQ(same, 24 * 16);
  }
  pf_map_free(c);
  pf_map_free(d);

  pf_clipboard_free(clip);
  pf_clipboard_free(reference);
  pf_map_free(map);
}
TEST(clipboard_copy_paste_moves_terrain_and_units) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  if (!map) { CHECK(false); return; }

  for (int y = 4; y < 8; y++) {
    for (int x = 4; x < 8; x++) pf_map_paint_terrain(map, x, y, PF_TERRAIN_WATER_DARK, 1);
  }
  CHECK(pf_map_add_unit(map, 3, 3, 0, 0, 0) >= 0);   // dry corner of the rect

  pf_clipboard* clip = pf_clipboard_copy(map, 3, 3, 6, 6, 1, 1);
  CHECK(clip != nullptr);
  if (!clip) { pf_map_free(map); return; }
  CHECK_EQ(pf_clipboard_unit_count(clip), 1);
  CHECK_EQ(pf_clipboard_has_terrain(clip), 1);

  const int before = pf_map_unit_count(map);
  CHECK_EQ(pf_map_paste(map, clip, 20, 20), 1);
  CHECK_EQ(pf_map_unit_count(map), before + 1);

  // The interior of the pasted block must reproduce the source exactly. The
  // border legitimately differs: paste refits the seam, and a 4x4 water pond
  // is mostly edge, so asserting a specific terrain in the middle would be
  // asserting the shape of the coastline rather than that paste works.
  // Compare *corner terrains*, not raw tile values: variation is chosen from a
  // position-based salt so neighbouring tiles differ visually, which means the
  // same terrain pasted elsewhere legitimately gets a different tile id. The
  // shape is what must survive the round trip, not the byte.
  uint8_t q[4], src[4];
  const int dx = 20 - 3, dy = 20 - 3;   // paste target minus copy origin
  for (int y = 4; y < 8; y++) {
    for (int x = 4; x < 8; x++) {
      pf_tile_quadrants(uint16_t(pf_map_tile_at(map, x, y)), src);
      pf_tile_quadrants(uint16_t(pf_map_tile_at(map, x + dx, y + dy)), q);
      for (int i = 0; i < 4; i++) CHECK_EQ(int(q[i]), int(src[i]));
    }
  }

  // And it is water, whichever shade the fitting chose.
  pf_tile_quadrants(uint16_t(pf_map_tile_at(map, 22, 22)), q);
  CHECK(q[0] == PF_TERRAIN_WATER_DARK || q[0] == PF_TERRAIN_WATER_LIGHT);

  bool coast = false;
  for (int y = 19; y < 28; y++) {
    for (int x = 19; x < 28; x++) {
      pf_tile_quadrants(uint16_t(pf_map_tile_at(map, x, y)), q);
      for (int i = 0; i < 4; i++) {
        if (q[i] == PF_TERRAIN_COAST_LIGHT || q[i] == PF_TERRAIN_COAST_DARK) coast = true;
      }
    }
  }
  CHECK(coast);

  // Pasting off the edge is refused rather than clipped silently.
  CHECK_EQ(pf_map_paste(map, clip, 30, 30), -1);

  // Every tile the paste produced must be expressible.
  for (int y = 19; y < 28; y++) {
    for (int x = 19; x < 28; x++) {
      pf_tile_quadrants(uint16_t(pf_map_tile_at(map, x, y)), q);
      for (int i = 0; i < 4; i++) CHECK(q[i] != PF_TERRAIN_UNKNOWN);
    }
  }

  pf_clipboard_free(clip);
  pf_map_free(map);
}
/**
 * A paste reproduces the fragment's corners exactly, seam-fitting or not.
 *
 * This is the promise the clipboard's whole design rests on: terrain travels
 * as corner terrains rather than tile values so it can be turned without the
 * artwork facing the wrong way, which only pays off if putting the corners
 * back yields the corners that were taken. Nothing tested `pf_clipboard_corner`
 * before, and a client that draws a paste preview from it — the Windows one
 * does — would show a fragment that is not what lands.
 *
 * Checked with edge fitting both off and on. On, the tiles *around* the
 * fragment may change; the fragment's own corners may not.
 */
TEST(a_paste_lands_the_corners_it_captured) {
  pf_map* map = nullptr;
  for (const std::string& path : pft::g_shipped) {
    map = pf_map_open_file(path.c_str(), nullptr);
    if (map && pf_map_width(map) >= 32 && pf_map_height(map) >= 32) break;
    if (map) { pf_map_free(map); map = nullptr; }
  }
  if (!map) { pft::skip("no map to copy from"); return; }

  const int cw = 9, ch = 7;
  pf_clipboard* clip = pf_clipboard_copy(map, 5, 5, cw, ch, 1, 1);
  CHECK(clip != nullptr);
  if (!clip) { pf_map_free(map); return; }

  int checked = 0;
  for (int fit = 0; fit <= 1; fit++) {
    pf_map* target = pf_map_create(64, 64, PF_TILESET_FOREST, nullptr);
    CHECK(pf_map_paste_ex(target, clip, 20, 20, fit) >= 0);
    for (int y = 0; y <= ch; y++) {
      for (int x = 0; x <= cw; x++) {
        const int want = pf_clipboard_corner(clip, x, y);
        if (want < 0) continue;
        // The corner grid is one larger than the tile grid in each direction,
        // so the far edges are read from the last tile's far quadrants.
        const int tx = 20 + (x == cw ? cw - 1 : x);
        const int ty = 20 + (y == ch ? ch - 1 : y);
        uint8_t q[4];
        pf_tile_quadrants(uint16_t(pf_map_tile_at(target, tx, ty)), q);
        CHECK_EQ(int(q[(y == ch ? 2 : 0) + (x == cw ? 1 : 0)]), want);
        checked++;
      }
    }
    pf_map_free(target);
  }
  std::printf("     %d corners checked, fitted and unfitted\n", checked);
  pf_clipboard_free(clip);
  pf_map_free(map);
}

TEST(component_files_round_trip_through_a_map) {
  // A component file has no header at all: a `.un` is literally the bytes of a
  // UDTA section, so length is the only thing identifying it. Which also means
  // a wrong file has to be rejected here rather than written into a map.
  CHECK_EQ(pf_component_kind(5696), PF_COMPONENT_UDTA);
  CHECK_EQ(pf_component_kind(5950), PF_COMPONENT_UDTA);   // with swampFrames
  CHECK_EQ(pf_component_kind(782), PF_COMPONENT_UGRD);
  CHECK_EQ(pf_component_kind(5696 + 782), PF_COMPONENT_BOTH);
  CHECK_EQ(pf_component_kind(384), PF_COMPONENT_ALOW);
  CHECK_EQ(pf_component_kind(1234), PF_COMPONENT_UNKNOWN);
  CHECK_EQ(pf_component_kind(0), PF_COMPONENT_UNKNOWN);

  if (!have_corpus()) { skip("no shipped maps"); return; }

  // Take a real map's unit data out and put it into a fresh one.
  pf_map* donor = nullptr;
  for (const std::string& path : g_corpus) {
    pf_map* candidate = pf_map_open_file(path.c_str(), nullptr);
    if (candidate && pf_map_has_unit_data(candidate)) { donor = candidate; break; }
    if (candidate) pf_map_free(candidate);
  }
  CHECK(donor != nullptr);
  if (!donor) return;

  size_t len = 0;
  uint8_t* bytes = pf_map_export_component(donor, PF_COMPONENT_UDTA, &len);
  CHECK(bytes != nullptr);
  CHECK_EQ(pf_component_kind(len), PF_COMPONENT_UDTA);
  if (!bytes) { pf_map_free(donor); return; }

  pf_status st = PF_OK;
  pf_map* fresh = pf_map_create(32, 32, 0, &st);
  CHECK(fresh != nullptr);
  if (fresh) {
    CHECK_EQ(pf_map_has_unit_data(fresh), 0);
    CHECK_EQ(pf_map_import_component(fresh, bytes, len), PF_OK);
    CHECK_EQ(pf_map_has_unit_data(fresh), 1);

    // Every field has to come back the same, or the transfer lost something.
    int differences = 0;
    for (int field = 0; field < pf_udta_field_count(); field++) {
      for (int unit = 0; unit < 110; unit++) {
        if (pf_map_unit_field(fresh, field, unit, 0) !=
            pf_map_unit_field(donor, field, unit, 0)) differences++;
      }
    }
    CHECK_EQ(differences, 0);

    // Nonsense is refused rather than written.
    const uint8_t junk[7] = {1, 2, 3, 4, 5, 6, 7};
    CHECK_EQ(pf_map_import_component(fresh, junk, sizeof(junk)), PF_ERR_MALFORMED);
    pf_map_free(fresh);
  }
  pf_buffer_free(bytes);
  pf_map_free(donor);
}
TEST(restrictions_can_be_added_without_changing_play) {
  pf_status st = PF_OK;
  pf_map* map = pf_map_create(32, 32, 0, &st);
  CHECK(map != nullptr);
  if (!map) return;

  CHECK_EQ(pf_map_has_restrictions(map), 0);
  CHECK_EQ(pf_map_add_restrictions(map), PF_OK);
  CHECK_EQ(pf_map_has_restrictions(map), 1);

  // Nothing restricted: a map with no ALOW already behaves this way, so
  // adding one must not change how it plays. Not all-ones — two blocks say
  // "researching", and one says which spells a player already knows.
  const uint32_t expect[6] = {0xFFFFFFFFu, 0x00004421u, 0xFFFFFFFFu,
                              0x00000000u, 0xFFFFFFFFu, 0x00000000u};
  for (int block = 0; block < 6; block++) {
    for (int player = 0; player < 16; player++) {
      CHECK_EQ(int(uint32_t(pf_map_allow(map, block, player))),
               int(expect[block]));
    }
  }

  // Asking twice leaves the first one alone.
  pf_map_set_allow(map, 0, 0, 0);
  CHECK_EQ(pf_map_add_restrictions(map), PF_OK);
  CHECK_EQ(int(pf_map_allow(map, 0, 0)), 0);

  // And it still serializes and reads back.
  size_t len = 0;
  uint8_t* bytes = pf_map_save(map, &len, nullptr);
  CHECK(bytes != nullptr);
  if (bytes) {
    pf_map* again = pf_map_open(bytes, len, nullptr);
    CHECK(again != nullptr);
    if (again) {
      CHECK_EQ(pf_map_has_restrictions(again), 1);
      CHECK_EQ(int(pf_map_allow(again, 0, 0)), 0);
      pf_map_free(again);
    }
    pf_buffer_free(bytes);
  }

  // Going back to the game's defaults drops the section rather than filling it
  // with the unrestricted table: that is the state 1338 of the 1378 maps are in.
  CHECK_EQ(pf_map_clear_restrictions(map), PF_OK);
  CHECK_EQ(pf_map_has_restrictions(map), 0);
  CHECK_EQ(int(pf_map_allow(map, 0, 0)), -1);
  CHECK_EQ(pf_map_clear_restrictions(map), PF_OK);   // twice is harmless

  size_t bare_len = 0;
  uint8_t* bare = pf_map_save(map, &bare_len, nullptr);
  CHECK(bare != nullptr);
  if (bare) {
    // The tag is gone from the file, not merely emptied.
    bool carries = false;
    for (size_t i = 0; i + 4 <= bare_len; i++) {
      if (std::memcmp(bare + i, "ALOW", 4) == 0) { carries = true; break; }
    }
    CHECK_EQ(carries, false);
    pf_map* again = pf_map_open(bare, bare_len, nullptr);
    CHECK(again != nullptr);
    if (again) {
      CHECK_EQ(pf_map_has_restrictions(again), 0);
      pf_map_free(again);
    }
    pf_buffer_free(bare);
  }
  pf_map_free(map);
}
TEST(oil_wells_hold_an_amount_not_a_flag) {
  // Found by auditing the shipped maps: 143 oil wells carry values of 2 to 8
  // in a field that is otherwise 0 or 1. They are buildings rather than
  // scenery, so they are not "resources" for selecting — but their value is an
  // amount all the same, and editing it as a checkbox would wipe the oil.
  CHECK(pf_unit_value_is_amount(0x5c));   // gold mine
  CHECK(pf_unit_value_is_amount(0x5d));   // oil patch
  CHECK(pf_unit_value_is_amount(86));     // human oil well
  CHECK(pf_unit_value_is_amount(87));     // orc oil well
  CHECK(!pf_unit_value_is_amount(0x00));  // footman
  CHECK(!pf_unit_value_is_amount(0x3a));  // farm

  // And they are still not in the resources group, which is about scenery.
  CHECK(!pf_unit_in_group(86, PF_GROUP_RESOURCES));
  CHECK(pf_unit_in_group(0x5c, PF_GROUP_RESOURCES));
}
/**
 * `OILM` is where War2mod keeps a map's trigger script, and the section has no
 * other use — nothing has read it as oil concentration since a 1995 beta. So an
 * edit must return it untouched, or every scripted map in existence loses its
 * script on a save.
 *
 * This runs without the corpus: a synthetic map is given a recognisable `OILM`
 * payload by patching the serialized bytes, since the ABI has no setter.
 * See reference/docs/w2tr-format.md.
 */
TEST(editing_preserves_oilm) {
  pf_status st = PF_OK;
  pf_map* blank = pf_map_create(32, 32, PF_TILESET_FOREST, &st);
  CHECK(blank != nullptr);
  if (!blank) return;

  size_t len = 0;
  uint8_t* raw = pf_map_save(blank, &len, &st);
  pf_map_free(blank);
  CHECK(raw != nullptr);
  if (!raw) return;
  std::vector<uint8_t> bytes(raw, raw + len);
  pf_buffer_free(raw);

  // Write a payload shaped like a real trigger program: opcodes, the editor's
  // FE FF marker, then opcode 0 to halt.
  size_t oilm_len = 0;
  const uint8_t* found = find_section(bytes, "OILM", &oilm_len);
  CHECK(found != nullptr);
  if (!found) return;
  CHECK_EQ(int(oilm_len), 32 * 32);
  const size_t oilm_at = size_t(found - bytes.data());

  const uint8_t program[] = {104, 106, 0, 1, 100, 0, 5, 7, 42, 24, 0xfe, 0xff, 0};
  std::memcpy(&bytes[oilm_at], program, sizeof(program));
  // Junk past the terminator, as the trigger editor leaves behind.
  for (size_t i = 200; i < 240; i++) bytes[oilm_at + i] = uint8_t(0xf0 | (i & 0xf));
  const std::vector<uint8_t> expected(&bytes[oilm_at], &bytes[oilm_at] + oilm_len);

  pf_map* map = pf_map_open(bytes.data(), bytes.size(), &st);
  CHECK(map != nullptr);
  if (!map) return;
  exercise_editing(map);

  size_t out_len = 0;
  uint8_t* out = pf_map_save(map, &out_len, &st);
  pf_map_free(map);
  CHECK(out != nullptr);
  if (!out) return;
  std::vector<uint8_t> after(out, out + out_len);
  pf_buffer_free(out);

  size_t after_len = 0;
  const uint8_t* got = find_section(after, "OILM", &after_len);
  CHECK(got != nullptr);
  if (!got) return;
  CHECK_EQ(int(after_len), int(oilm_len));

  size_t differing = 0;
  for (size_t i = 0; i < std::min(after_len, oilm_len); i++) {
    if (got[i] != expected[i]) differing++;
  }
  CHECK_EQ(int(differing), 0);
}
static std::vector<std::string> section_tags(const std::vector<uint8_t>& b) {
  std::vector<std::string> tags;
  size_t i = 0;
  while (i + 8 <= b.size()) {
    tags.push_back(std::string(reinterpret_cast<const char*>(&b[i]), 4));
    uint32_t n = uint32_t(b[i + 4]) | uint32_t(b[i + 5]) << 8 |
                 uint32_t(b[i + 6]) << 16 | uint32_t(b[i + 7]) << 24;
    i += 8 + n;
  }
  return tags;
}

static int tag_index(const std::vector<std::string>& tags, const char* tag) {
  for (size_t i = 0; i < tags.size(); i++) {
    if (tags[i] == tag) return int(i);
  }
  return -1;
}

TEST(a_map_that_gains_a_section_writes_it_where_retail_does) {
  // Sections missing from the parsed file used to be appended, so restricting
  // a spell wrote ALOW after UNIT — an order none of the 1378 corpus maps use,
  // and the game ignored the restriction.
  pf::Status s;
  pf::Map* fresh = pf::Map::create(32, 32, 0, s);
  CHECK(fresh != nullptr);
  if (!fresh) return;
  const std::vector<uint8_t> bytes = fresh->serialize();
  delete fresh;

  std::vector<std::string> tags = section_tags(bytes);
  // Every corpus map carries both, so a new one must too.
  CHECK(tag_index(tags, "UDTA") >= 0);
  CHECK(tag_index(tags, "UGRD") >= 0);
  CHECK(tag_index(tags, "UDTA") < tag_index(tags, "UNIT"));

  pf::Map* map = pf::Map::parse(bytes.data(), bytes.size(), s);
  CHECK(map != nullptr);
  if (!map) return;
  CHECK_EQ(int(map->restrictions().size()), 0);
  auto& alow = map->restrictions_mut();
  alow.assign(size_t(pf::kAlowSize), 0xFF);
  alow[4] = 0xFB;  // player 1 loses one spell
  const std::vector<uint8_t> again = map->serialize();
  delete map;

  tags = section_tags(again);
  const int at = tag_index(tags, "ALOW");
  CHECK(at > 0);
  if (at <= 0) return;
  CHECK(at < tag_index(tags, "UNIT"));
  CHECK(tags[size_t(at) - 1] == "UGRD");
  CHECK(tags[size_t(at) + 1] == "SIDE");
}

TEST(reopening_a_map_moves_a_stranded_section_back) {
  // Maps written before the ordering fix carry UDTA and ALOW after UNIT.
  // Re-saving one has to repair it, or the game keeps ignoring the overrides.
  pf::Status s;
  pf::Map* fresh = pf::Map::create(32, 32, 0, s);
  CHECK(fresh != nullptr);
  if (!fresh) return;
  const std::vector<uint8_t> good = fresh->serialize();
  delete fresh;

  // Rebuild the file with UDTA and UGRD moved to the end, as the old writer did.
  std::vector<uint8_t> stranded, tail;
  size_t i = 0;
  while (i + 8 <= good.size()) {
    const std::string tag(reinterpret_cast<const char*>(&good[i]), 4);
    uint32_t n = uint32_t(good[i + 4]) | uint32_t(good[i + 5]) << 8 |
                 uint32_t(good[i + 6]) << 16 | uint32_t(good[i + 7]) << 24;
    auto& dst = (tag == "UDTA" || tag == "UGRD") ? tail : stranded;
    dst.insert(dst.end(), good.begin() + long(i), good.begin() + long(i + 8 + n));
    i += 8 + n;
  }
  stranded.insert(stranded.end(), tail.begin(), tail.end());
  CHECK_EQ(int(stranded.size()), int(good.size()));

  std::vector<std::string> tags = section_tags(stranded);
  CHECK(tag_index(tags, "UDTA") > tag_index(tags, "UNIT"));  // the broken shape

  pf::Map* map = pf::Map::parse(stranded.data(), stranded.size(), s);
  CHECK(map != nullptr);
  if (!map) return;
  const std::vector<uint8_t> again = map->serialize();
  delete map;

  tags = section_tags(again);
  CHECK(tag_index(tags, "UDTA") < tag_index(tags, "UNIT"));
  CHECK(tag_index(tags, "UGRD") == tag_index(tags, "UDTA") + 1);
  CHECK(again == good);  // repaired all the way back
}
}  // namespace pft

TEST(field_names_become_labels_a_person_reads) {
  char buffer[64] = {};

  // The rule: split where a lower-case or digit meets an upper-case one, and
  // capitalise the first letter.
  CHECK(pf_field_label("buildTime", buffer, sizeof(buffer)) == 10);
  CHECK(std::string(buffer) == "Build Time");
  pf_field_label("hitPoints", buffer, sizeof(buffer));
  CHECK(std::string(buffer) == "Hit Points");
  pf_field_label("armor", buffer, sizeof(buffer));
  CHECK(std::string(buffer) == "Armor");
  pf_field_label("unitSize", buffer, sizeof(buffer));
  CHECK(std::string(buffer) == "Unit Size");

  // An override wins over the rule.
  pf_field_label("reactRangeComputer", buffer, sizeof(buffer));
  CHECK(std::string(buffer) == "React Range (Computer)");

  // Every field of both tables gets something readable: non-empty, starting
  // with a capital, and no camel humps left in it. This is the check that
  // matters — a field added to the core must not need anyone to remember to
  // write a label for it.
  for (int i = 0; i < pf_udta_field_count(); i++) {
    const char* name = pf_udta_field_name(i);
    CHECK(name != nullptr);
    const int length = pf_field_label(name, buffer, sizeof(buffer));
    CHECK(length > 0);
    CHECK(length < int(sizeof(buffer)));
    CHECK(buffer[0] >= 'A' && buffer[0] <= 'Z');
    for (int at = 1; buffer[at]; at++) {
      const bool hump = buffer[at] >= 'A' && buffer[at] <= 'Z' &&
                        buffer[at - 1] >= 'a' && buffer[at - 1] <= 'z';
      if (hump) std::printf("     %s -> %s still has a hump\n", name, buffer);
      CHECK(!hump);
    }
  }
  for (int i = 0; i < pf_ugrd_field_count(); i++) {
    const char* name = pf_ugrd_field_name(i);
    CHECK(name != nullptr);
    CHECK(pf_field_label(name, buffer, sizeof(buffer)) > 0);
    CHECK(buffer[0] >= 'A' && buffer[0] <= 'Z');
  }

  // The full length comes back even when the buffer is too small, so a caller
  // can size one and ask again, and what did fit is still terminated.
  char tiny[6] = {};
  CHECK_EQ(pf_field_label("buildTime", tiny, int(sizeof(tiny))), 10);
  CHECK_EQ(int(std::string(tiny).size()), 5);
  CHECK(std::string(tiny) == "Build");
  CHECK_EQ(pf_field_label(nullptr, buffer, sizeof(buffer)), 0);
}
