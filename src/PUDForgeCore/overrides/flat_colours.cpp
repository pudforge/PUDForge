// Flat terrain colours, for drawing a map with no artwork loaded.
//
// Each entry is the mean colour of the first four solid tiles of that terrain
// class in that tileset, averaged over several variations so one decorated tile
// cannot set the colour. One palette for all four tilesets was a lie about
// three of them: winter ground is grey-blue and wasteland ground is rust.
//
// Forest and rock are then pulled away from the ground deliberately — measured
// honestly, swamp forest sits 4 units from swamp ground — because this palette
// exists to be read rather than to match. Everything else is the measurement.
//
// Hardcoded because the fallback has to work with no Warcraft II install, which
// is why it exists. Regenerate with tools/derive-flat-colours.mjs. 0xRRGGBB.

#include "../constants.hpp"
#include "../terrain.hpp"

namespace pf {
namespace {

constexpr uint32_t kFlat[4][kTerrainCount] = {
    // forest
    {0x043571, 0x043975, 0x543408, 0x5c3a09, 0x274811, 0x20400d, 0x03210b,
     0x484848, 0x3a3b3a, 0x33362a, 0xff00ff},
    // winter
    {0x043572, 0x043975, 0x174372, 0x1b4870, 0x74747f, 0x6e6e7c, 0x23443e,
     0x5c4847, 0x3b414e, 0x3e4144, 0xff00ff},
    // wasteland
    {0x0f212d, 0x0c212d, 0x3b220d, 0x3f240f, 0x62320b, 0x5c2c0b, 0x272b08,
     0x4a3c3a, 0x332b27, 0x3a2a20, 0xff00ff},
    // swamp
    {0x151d08, 0x192108, 0x5b2e01, 0x613504, 0x432c1d, 0x382317, 0x03210b,
     0x564c4d, 0x332c2a, 0x342b25, 0xff00ff},
};

/// Anything the corner model cannot name. Magenta, on purpose.
constexpr uint32_t kUnknown = 0xff00ff;

}  // namespace

uint32_t terrain_flat_colour(int terrain, int tileset) {
  if (terrain < 0 || terrain >= kTerrainCount) return kUnknown;
  const int set = (tileset >= 0 && tileset < 4) ? tileset : 0;
  return kFlat[set][terrain];
}

/**
 * Colours for the eight movement values, keyed by the value itself.
 *
 * Movement is not an arbitrary label like a region id: there are eight values
 * and each one means something, so the colours say what rather than merely
 * being far apart. Passable ground is green, water blue, blocked red, walls
 * their own grey. Anything outside the eight is magenta, and should never
 * occur in a shipped map.
 */
uint32_t movement_colour(int value) {
  switch (value) {
    case 0x0001: return 0x4fc76a;   // ground
    case 0x0011: return 0x8fd48a;   // coast
    case 0x0002: return 0x6fc2c8;   // shore, mostly land
    case 0x0082: return 0x3f8fc0;   // shore, mostly water
    case 0x0040: return 0x2f5fd0;   // open water
    case 0x0081: return 0xd05a3a;   // forest and rock
    case 0x008d: return 0xb0b4bc;   // human wall
    case 0x0089: return 0x8a7f6a;   // orc wall
    case 0x0000: return 0xe8c34a;   // land and water: stops nothing
    // The combinations. A restriction on ground keeps ground's green and goes
    // darker with it, so the family is visible before the name is read; the
    // three named after a bit rather than an effect take colours of their own.
    case 0x0201: return 0x3f8f52;   // ground, no flying
    case 0x0801: return 0x71904a;   // ground, no building
    case 0x0240: return 0x27489c;   // water, no flying
    case 0x0840: return 0x3a63b8;   // water, no building
    case 0x0281: return 0x5c1f14;   // no walking or flying: the only real barrier
    default: return kUnknown;
  }
}

}  // namespace pf
