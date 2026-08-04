// The movement values Warcraft II actually uses.
//
// `SQM ` is one 16-bit word per tile. The format notes call it a bitfield, but
// the bits are not recoverable: no shipped map sets a combination outside the
// eight values below, so there is nothing to correlate a bit against. Across
// 556 maps and 6,975,488 tiles, exactly eight values occur, each attributable
// to a terrain situation:
//
//   0x0001  ground                100% ground tiles
//   0x0002  shore, mostly land    100% coast/water tiles, 1 water corner
//   0x0011  coast                 100% coast tiles
//   0x0040  open water            100% water tiles
//   0x0081  forest and rock       100% forest or mountain tiles
//   0x0082  shore, mostly water   100% coast/water tiles, 2 or 3 water corners
//   0x0089  orc wall              100% orc wall tiles
//   0x008d  human wall            100% human wall tiles
//
// The eight are Blizzard's habit rather than the format's rule: across ~1,200
// community maps, 0.24% of tiles hold something else. `movement_class_of`
// returns -1 for those and the editor shows raw hex, which is the escape hatch
// PUDDraft's two unexplainable checkboxes ("No Flyers", "No Others") were not.
//
// The value a tile ought to have is kGroupMovement in terrain_tables.hpp; this
// file is for the 236 tiles in 6,975,488 that deviate from it.

#include "../constants.hpp"
#include "../terrain_tables.hpp"

namespace pf {
namespace {

struct MovementClass {
  uint16_t value;
  const char* name;
};

/// In the order a palette should show them: passable first, then blocked.
const MovementClass kMovementClasses[] = {
    {0x0001, "Ground"},
    {0x0011, "Coast"},
    {0x0002, "Shore, mostly land"},
    {0x0082, "Shore, mostly water"},
    {0x0040, "Open water"},
    {0x0081, "Forest and rock"},
    {0x008d, "Human wall"},
    {0x0089, "Orc wall"},
};

}  // namespace

int movement_class_count() {
  return int(sizeof(kMovementClasses) / sizeof(kMovementClasses[0]));
}

int movement_class_value(int index) {
  if (index < 0 || index >= movement_class_count()) return -1;
  return kMovementClasses[index].value;
}

const char* movement_class_name(int index) {
  if (index < 0 || index >= movement_class_count()) return nullptr;
  return kMovementClasses[index].name;
}

int movement_class_of(int value) {
  for (int i = 0; i < movement_class_count(); i++) {
    if (kMovementClasses[i].value == value) return i;
  }
  return -1;
}

}  // namespace pf
