// How many variations of a solid tile group are plain.
//
// The first few variations of a group are plain drawings and the rest are
// decorated, which is the boundary the "Plain / Mixed / Detailed" setting needs.
// Reading it off the artwork — plain is the unbroken run from 0, before a blank
// slot — holds for forest, winter and wasteland but not swamp, which populates
// the slot the others leave blank, so every swamp variation read as plain.
//
// The boundary is a property of the group, and usage across 12.7M solid tiles
// agrees with the forest tileset's blank-run structure: groups 1, 2 and 8
// spread evenly over variations 0-3, groups 3 to 7 over 0-2 with variation 3 in
// 0.0% of tiles. Swamp's histogram is indistinguishable from forest's.
//
// Groups outside 1-8 are boundary classes and keep the blank-run reading.

#include "../constants.hpp"

namespace pf {
namespace {

/// Indexed by the group nibble. 0 means "no measurement, read the artwork".
constexpr int kPlainVariations[16] = {
    0,  // 0x0 - not a solid group
    4,  // 0x1 water
    4,  // 0x2 dark water
    3,  // 0x3 coast
    3,  // 0x4 dark coast
    3,  // 0x5 ground
    3,  // 0x6 dark ground
    3,  // 0x7 forest
    4,  // 0x8 rock
    0, 0, 0, 0, 0, 0, 0,
};

}  // namespace

int plain_variation_count(int group) {
  if (group < 0 || group >= 16) return 0;
  return kPlainVariations[group];
}

}  // namespace pf
