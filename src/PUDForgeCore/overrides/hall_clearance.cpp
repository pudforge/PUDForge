// How much room a town hall needs around a gold mine.
//
// A hall placed against a mine cannot be worked — the peasants need a lane to
// walk in and out of — and the number is written down nowhere, so it was
// measured. Gap in tiles between the two footprints, 1378 maps, 3013 pairs
// within eight tiles of each other:
//
//     gap   0    1    2     3    4    5    6
//     halls 8    0    0  1803  282  289  232
//
// One and two never occur, in any map from any author; the eight at 0 are
// overlapping placements from two joke maps. A hole that wide is a rule, not a
// habit. The rule is the hall's alone: every other building sits at gaps of 0,
// 1 and 2 against a mine freely (816, 519 and 933 times).

#include "../constants.hpp"

namespace pf {
namespace {

/// Town Hall and Great Hall and their two upgrade tiers, which occupy the same
/// slot on the map and are the same building to a peasant.
constexpr int kHalls[] = {74, 75, 88, 89, 90, 91};

}  // namespace

bool unit_needs_mine_clearance(int unit_id) {
  for (int hall : kHalls) if (hall == unit_id) return true;
  return false;
}

int mine_clearance_tiles() { return 3; }

}  // namespace pf
