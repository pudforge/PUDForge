// What each bit of ALOW's two upgrade blocks restricts.
//
// Read out of the game's own data rather than transcribed, by a method worth
// recording because it is checkable. `UGRD`'s four-byte "flags" field holds
// exactly one bit per upgrade, and the human and orc versions of a research
// share it — Sword 1 and Axe 1 are both bit 2 — which is the shape of a slot in
// the player's researched mask, exactly what an ALOW block indexes.
//
// The check: the eighteen spell upgrades use the same field the same way, and
// their bit positions reproduce `kAlowSpellBits` — transcribed from the format
// notes long before anyone looked at UGRD — for all eighteen, including both of
// its gaps. That is what turns this from a guess into a reading.
//
// Both upgrade blocks index the same bits; they differ in what they say about
// them. One quirk is the game's: Arrow 2 stores bit 0, the same bit as Arrow 1,
// where Throwing Axe 2 correctly stores bit 1. The pair is named on bit 1 below,
// since that is the slot the research occupies.

#include "../constants.hpp"

namespace pf {
namespace {

/// Human and orc research share a slot, so most entries name both. Bits with
/// no upgrade are null: fifteen of the thirty-two are unused by the game.
const char* const kAlowUpgradeBits[32] = {
    "Arrow 1 / Throwing Axe 1",                 // 0
    "Arrow 2 / Throwing Axe 2",                 // 1
    "Sword 1 / Axe 1",                          // 2
    "Sword 2 / Axe 2",                          // 3
    "Human Shield 1 / Orc Shield 1",            // 4
    "Human Shield 2 / Orc Shield 2",            // 5
    "Ship Cannon 1",                            // 6
    "Ship Cannon 2",                            // 7
    "Ship Armor 1",                             // 8
    "Ship Armor 2",                             // 9
    nullptr,                                    // 10
    nullptr,                                    // 11
    "Catapult 1 / Ballista 1",                  // 12
    "Catapult 2 / Ballista 2",                  // 13
    nullptr,                                    // 14
    nullptr,                                    // 15
    "Train Rangers / Berserkers",               // 16
    "Longbow / Lighter Axes",                   // 17
    "Ranger / Berserker Scouting",              // 18
    "Marksmanship / Regeneration",              // 19
    "Train Paladins / Ogre-Mages",              // 20
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

/// Which unit each of block 0's bits restricts, as a unit id.
///
/// A table rather than a name lookup, because the bit names read "Footman /
/// Grunt" and the unit table reads "Footman", so matching them is string
/// surgery that breaks the first time either side is reworded — and both sides
/// are reworded wholesale when a host installs the game's own strings. The
/// human half of each pair is the one recorded.
///
/// The first thirteen are exactly `bit * 2`, but the arithmetic stops holding
/// at bit 14 and a table that is half formula is worse than one that is none.
const int kAlowUnitBitUnits[32] = {
    0x00,  // 0  Footman / Grunt
    0x02,  // 1  Peasant / Peon
    0x04,  // 2  Ballista / Catapult
    0x06,  // 3  Knight / Ogre
    0x08,  // 4  Archer / Axethrower
    0x0a,  // 5  Mage / Death Knight
    0x1a,  // 6  Oil Tanker
    0x1e,  // 7  Destroyer
    0x1c,  // 8  Transport
    0x20,  // 9  Battleship / Juggernaught
    0x26,  // 10 Submarine / Turtle
    0x28,  // 11 Flying Machine / Zeppelin
    0x2a,  // 12 Gryphon / Dragon
    -1,    // 13 unused
    0x0e,  // 14 Demolition Squad / Sappers
    0x46,  // 15 Aviary / Roost
    0x3a,  // 16 Farm
    0x3c,  // 17 Barracks
    0x4c,  // 18 Lumber Mill
    0x42,  // 19 Stables / Ogre Mound
    0x50,  // 20 Mage Tower / Temple
    0x4e,  // 21 Foundry
    0x54,  // 22 Refinery
    0x44,  // 23 Inventor / Alchemist
    0x3e,  // 24 Church / Altar of Storms
    0x40,  // 25 Tower
    0x4a,  // 26 Town Hall / Great Hall
    0x58,  // 27 Keep / Stronghold
    0x5a,  // 28 Castle / Fortress
    0x52,  // 29 Blacksmith
    0x48,  // 30 Shipyard
    -1,    // 31 unused
};

/// Upgrade ids 34 to 51 are the spells; 0 to 33 are the researches. The two
/// halves index the same 32 bits independently — bit 0 is Arrow 1 in an upgrade
/// block and Holy Vision in a spell block — so a search has to be told which.
constexpr int kFirstSpellUpgrade = 34;

int ugrd_flags_field() {
  for (int i = 0; i < kUgrdSegmentCount; i++) {
    const char* n = kUgrdSegments[i].name;
    if (n[0] == 'f' && n[1] == 'l' && n[2] == 'a' && n[3] == 'g' &&
        n[4] == 's' && n[5] == '\0') {
      return i;
    }
  }
  return -1;
}

/// The default table's `flags` for one upgrade, or 0.
uint32_t default_upgrade_flags(int upgrade) {
  const int field = ugrd_flags_field();
  if (field < 0) return 0;
  const UdtaSegment& seg = kUgrdSegments[field];
  if (upgrade < 0 || upgrade >= seg.elements) return 0;
  const int at =
      segment_offset(kUgrdSegments, kUgrdSegmentCount, field) + upgrade * seg.width;
  if (at < 0 || at + seg.width > kDefaultUgrdSize) return 0;
  uint32_t v = 0;
  for (int i = 0; i < seg.width; i++) v |= uint32_t(kDefaultUgrd[at + i]) << (8 * i);
  return v;
}

}  // namespace

const char* alow_upgrade_bit_name(int bit) {
  if (bit < 0 || bit >= 32) return nullptr;
  return kAlowUpgradeBits[bit];
}

int alow_bit_unit(int block, int bit) {
  if (block != 0 || bit < 0 || bit >= 32) return -1;
  return kAlowUnitBitUnits[bit];
}

int alow_bit_upgrade(int block, int bit) {
  if (block < 1 || block > 5 || bit < 0 || bit >= 32) return -1;
  // Read back out of the same field the bit names came from rather than
  // transcribed a second time, so the two cannot drift apart.
  const int flags_field = ugrd_flags_field();
  if (flags_field < 0) return -1;
  const int count = kUgrdSegments[flags_field].elements;
  const bool spell = block <= 3;
  const int first = spell ? kFirstSpellUpgrade : 0;
  const int last = spell ? count : kFirstSpellUpgrade;
  const uint32_t want = uint32_t(1) << bit;
  for (int i = first; i < last; i++) {
    if (default_upgrade_flags(i) == want) return i;
  }
  return -1;
}

}  // namespace pf
