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
//   63–71 px   all ten ships, six of the eight flying units      (2 tiles)
//
// Six of the eight, because boxSize is evidence and not the rule. The Daemon
// and the Eye of Kilrogg are summoned rather than built, and both carry a
// 31x31 box like a footman — yet all 77 Daemons across the corpus sit at an
// even x and an even y, none of them odd, which is the 2x2 signature and not a
// footman's. Chance would leave about a quarter of them there. The Eye of
// Kilrogg appears in no map at all, so it is in the table on the Daemon's
// evidence and on being its opposite number: same box, same summoning, same
// flight. Deathwing is in it on the same footing and has always been.
//
// Second, and harder to argue with: across the five maps in `test/fixtures`,
// all 45 ships and flying units sit at an even x *and* an even y. Land units do
// not (71 of 305 even) and neither do buildings (47 of 220). A 2x2 unit
// anchored at its top-left corner on an even grid is exactly that signature,
// and 45 units landing on it by chance is one in 10^27.
// `ships_and_fliers_sit_on_even_tiles` measures this against whatever corpus is
// present, so the claim is checked rather than asserted.
//
// The same units are placed on a two-tile grid, and that is the other half of
// the rule: across 43 maps whose `REGM` carries the game editor's own shore
// sentinel, all 330 ships and flying units sit on an even x *and* an even y.
// Not one is odd. The 1x1 units on those maps are at both parities, near
// enough a quarter of them even, which is what chance looks like.
//
// So the editor lays them out in 2x2 blocks rather than anywhere they fit, and
// a ship on an odd tile is a ship the game was never given. `My_Map.pud`, made
// with some other tool, holds all 19 of the exceptions on this machine.
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
    // The two summoned fliers. Their boxSize is 31x31, so the pixel evidence
    // above says nothing about them; the maps do. See the note below.
    {0x2d, "Eye of Kilrogg"},
    {0x38, "Daemon"},
};

/// Oil, which goes on the same two-tile grid as the ships but one tile off it.
///
/// Across the maps the game's own editor wrote, all 1,286 oil patches sit at an
/// odd x *and* an odd y, and so do all 16 oil wells. Not one is anywhere else.
/// The gold mine is the control and settles that this is not simply what a 3x3
/// unit does: it is the same size, and its 2,605 placements fall 676 / 718 /
/// 625 / 586 across the four parities, which is what chance looks like.
///
/// Why the odd one is not known here. A patch is 3x3, so an odd corner puts its
/// middle on the even grid the ships use, which would make it the same rule
/// seen from the centre rather than the corner — but the gold mine is 3x3 too
/// and does not follow it, so that explanation is not enough and is not
/// claimed.
const uint8_t kOddGrid[] = {
    0x5d,   // Oil Patch
    0x56,   // Human Oil Well
    0x57,   // Orc Oil Well
};

bool on_the_odd_grid(int unit_id) {
  for (uint8_t id : kOddGrid) {
    if (id == unit_id) return true;
  }
  return false;
}

}  // namespace

bool unit_footprint_override(int unit_id, int& w, int& h) {
  for (const auto& entry : kTwoByTwo) {
    if (entry.id != unit_id) continue;
    w = h = 2;
    return true;
  }
  return false;
}

int unit_placement_step(int unit_id) {
  if (on_the_odd_grid(unit_id)) return 2;
  int w = 0, h = 0;
  return unit_footprint_override(unit_id, w, h) ? 2 : 1;
}

int unit_placement_phase(int unit_id) { return on_the_odd_grid(unit_id) ? 1 : 0; }


int oversize_unit_count() {
  return int(sizeof(kTwoByTwo) / sizeof(kTwoByTwo[0]));
}

int oversize_unit_id(int index) {
  if (index < 0 || index >= oversize_unit_count()) return -1;
  return kTwoByTwo[index].id;
}

}  // namespace pf
