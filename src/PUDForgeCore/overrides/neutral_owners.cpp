// Units that belong to nobody.
//
// Scenery is not anyone's property, and giving it to a player is a mistake an
// editor should not let you make by accident. Measured across the shipped maps:
//
//   Gold Mine         5,670 of 5,676 neutral
//   Oil Patch         1,907 of 1,907 neutral
//   Circle of Power      29 of 29 neutral
//   Dark Portal          23 of 23 neutral
//
// A default applied when placing rather than a rule enforced on the file, so a
// map that already owns a mine keeps it. The runestone is deliberately absent:
// its 55 are spread across seven players, so no owner is the obvious one.

#include "../constants.hpp"

namespace pf {
namespace {

// Unit ids, named rather than written as numbers at the call site.
constexpr uint8_t kGoldMine = 0x5c, kOilPatch = 0x5d;
constexpr uint8_t kCircleOfPower = 0x64, kDarkPortal = 0x65;


const uint8_t kAlwaysNeutral[] = {kGoldMine, kOilPatch, kCircleOfPower, kDarkPortal};

}  // namespace

int unit_default_owner(int unit_id) {
  for (uint8_t id : kAlwaysNeutral) {
    if (id == unit_id) return kNeutralPlayer;
  }
  return -1;
}

}  // namespace pf
