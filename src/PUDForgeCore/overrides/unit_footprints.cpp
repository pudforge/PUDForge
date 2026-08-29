// The footprints Warcraft II gives its ships and flying units, which `UDTA`
// does not.
//
// The `unitSize` field is the editor's source for how many tiles a unit covers,
// and for every mobile unit in the retail defaults it reads 1x1 — footman,
// peasant, battleship and dragon alike. Only buildings and the two resources
// carry anything else (farm 2x2, hall 4x4, gold mine 3x3). Taken literally, a
// destroyer fits in a one-tile pond and two gryphons stack on one tile.
//
// The game disagrees, and two things in the data say so.
//
// `boxSize`, the field beside it, is the same unit measured in pixels, and it
// is not 1x1-shaped at all:
//
//   31–42 px   every infantry, cavalry and hero on foot          (1 tile)
//   63–71 px   all ten ships, all six flying units               (2 tiles)
//
// Second, and harder to argue with: across the five maps in `test/fixtures`,
// all 45 ships and flying units sit at an even x *and* an even y. Land units do
// not (71 of 305 even) and neither do buildings (47 of 220). A 2x2 unit
// anchored at its top-left corner on an even grid is exactly that signature,
// and 45 units landing on it by chance is one in 10^27.
// `ships_and_fliers_sit_on_even_tiles` measures this against whatever corpus is
// present, so the claim is checked rather than asserted.
//
// Ballista and Catapult are the near miss: a 63 px box like the ships, but they
// appear at both parities in the fixtures, so they are land units with big
// artwork and stay 1x1.
//
// This table wins over a map's own `unitSize`, which is the one place in the
// core where hand-written judgement overrules the file. It has to: every map
// ever shipped carries Blizzard's 1x1 for these units, so a table that yielded
// to the file would never once apply. The field is preserved and re-emitted
// byte for byte either way — this decides what the editor lays out, not what a
// `.pud` may hold.

#include "../constants.hpp"

namespace pf {
namespace {

/// Every unit the game floats or flies, with the id spelled out. Each covers
/// 2x2 tiles; nothing here is bigger, and no land unit is in the list.
const struct { int id; const char* name; } kTwoByTwo[] = {
    {0x16, "Kurdran and Sky'ree"},   // the one flying hero
    {0x1a, "Human Oil Tanker"},
    {0x1b, "Orc Oil Tanker"},
    {0x1c, "Human Transport"},
    {0x1d, "Orc Transport"},
    {0x1e, "Elven Destroyer"},
    {0x1f, "Troll Destroyer"},
    {0x20, "Battleship"},
    {0x21, "Juggernaught"},
    {0x23, "Deathwing"},
    {0x26, "Gnomish Submarine"},
    {0x27, "Giant Turtle"},
    {0x28, "Gnomish Flying Machine"},
    {0x29, "Goblin Zeppelin"},
    {0x2a, "Gryphon Rider"},
    {0x2b, "Dragon"},
};

}  // namespace

bool unit_footprint_override(int unit_id, int& w, int& h) {
  for (const auto& entry : kTwoByTwo) {
    if (entry.id != unit_id) continue;
    w = h = 2;
    return true;
  }
  return false;
}

int oversize_unit_count() {
  return int(sizeof(kTwoByTwo) / sizeof(kTwoByTwo[0]));
}

int oversize_unit_id(int index) {
  if (index < 0 || index >= oversize_unit_count()) return -1;
  return kTwoByTwo[index].id;
}

}  // namespace pf
