// Where a shipyard, foundry or refinery sits against the shoreline.
//
// Touching water and coast is not the whole rule. A 3x3 shore building can
// touch both with eight of its nine tiles inland, which the game will not
// have: the building is a dock, and a dock's middle is in the water.
//
// Measured over the 357 maps under `reference/war2_ref`, 85 shore buildings:
// the middle tile of the footprint is water on all 85. Not one is coast, and
// not one is inland. A hole that wide is a rule.
//
// Read as the tile's dominant terrain rather than as all four of its
// quadrants. The stricter reading holds 83 times out of 85, and the two it
// loses are real placements from real maps — a mixed tile under a dock is a
// shoreline the game accepts, so the rule that rejects them is the wrong one.
//
// **How many tiles may be inland is not a rule.** The same 85 run 0, 1, 2 or 3
// inland tiles 78 times and 4 or 5 the other 7 — no hole, so no rule, and a cap
// at 3 would refuse placements the game shipped.

#include "../constants.hpp"
#include "../terrain.hpp"

namespace pf {

bool shore_centre_terrain_ok(int terrain) {
  return terrain == kWaterDark || terrain == kWaterLight;
}

}  // namespace pf
