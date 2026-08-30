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
// Three more come from War2XE, which offers nine and writes values Blizzard's
// maps never do. A map made for the purpose settles what each one is: every
// option laid on one row over the same tile, 0x0050, so nothing but the value
// differs.
//
//   0x0F00  space              its four high bits and nothing else
//   0x0000  bridge             no bits at all
//   0x0201  no flying units    ground with 0x0200 added
//
// Which also reads the bitfield the other way up: a bit says what is *stopped*,
// not what is let through. 0x0000 stops nothing, so a bridge carries walkers
// and ships alike; 0x0F00 stops everything, which is what space is; and 0x0200
// on its own is the one that stops flying — the bit War2XE adds to ground for
// its ninth option, and the bit inside space that grounds it too. That reading
// makes the eight above fall out as combinations rather than as eight
// unexplained numbers, but it is inference from ten tiles and is written here
// as such.
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
  /// Whether a palette offers it. A value a real map holds has to be named
  /// whether or not anybody should be painting more of it — 0x008d is on
  /// 12,128 tiles and has to read as something, but both walls are walls and
  /// one bit is the whole difference, so only one of them is on offer.
  bool offered;
};

/// What each bit of the word means, from the game's own header. This is the
/// one thing here that is not inference: the names are Blizzard's.
///
/// Every value in 4,638,720 tiles across 365 maps is a combination of these and
/// nothing else — `movement_values_are_made_of_named_bits` checks it — and the
/// eleven that occur come apart into sense:
///
///   0x0000  SQ_NONE                                   land and water at once
///   0x0011  SQ_LAND | SQ_UNBUILDABLE                  land nobody may build on
///   0x0081  SQ_LAND | SQ_UNPASSABLE                   forest and rock
///   0x008d  SQ_LAND | SQ_C_WALL | SQ_P_WALL | ...     one wall
///   0x0089  SQ_LAND | SQ_C_WALL | SQ_UNPASSABLE       the other
///   0x0201  SQ_LAND | SQ_MAN_AIR                      War2XE's "no flying"
///   0x0f00  the four restriction bits together        closed to everything
///
/// Which also settles the two walls: they are not a human bit and an orc bit
/// but one wall bit and a second that only one of them carries.
///
/// Five bits are never set in any of those tiles — SQ_VICTORY, SQ_RUNES,
/// TR_START and the top two, which have no name here at all. The first three
/// were painted on ground and taken into the game to see: nothing happened.
/// They are named so a map holding one can be read, and offered nowhere.
struct MovementBit {
  uint16_t value;
  const char* name;
};

constexpr uint16_t kNoFlying = 0x0200;
constexpr uint16_t kUnpassable = 0x0080;
constexpr uint16_t kWater = 0x0040;
constexpr uint16_t kUnbuildable = 0x0010;
constexpr uint16_t kBuilding = 0x0800;
constexpr uint16_t kLand = 0x0001;

const MovementBit kMovementBits[] = {
    {0x2000, "TR_START"},        {0x1000, "SQ_RUNES"},
    {0x0800, "SQ_BUILDING"},     {0x0400, "SQ_AI_BUILDING"},
    {0x0200, "SQ_MAN_AIR"},      {0x0100, "SQ_MAN"},
    {0x0080, "SQ_UNPASSABLE"},   {0x0040, "SQ_WATER"},
    {0x0020, "SQ_VICTORY"},      {0x0010, "SQ_UNBUILDABLE"},
    {0x0008, "SQ_C_WALL"},       {0x0004, "SQ_P_WALL"},
    {0x0002, "SQ_SHORE"},        {0x0001, "SQ_LAND"},
};

/// In the order a palette should show them: passable first, then blocked.
const MovementClass kMovementClasses[] = {
    {0x0001, "Ground", true},
    {0x0011, "Coast", true},
    {0x0002, "Shore, mostly land", true},
    {0x0082, "Shore, mostly water", true},
    {0x0040, "Open water", true},
    {0x0081, "Forest and rock", true},
    // One wall. SQ_C_WALL is the bit both carry; SQ_P_WALL is on the human one
    // and on nothing else, and is not worth a second cell for a difference
    // nobody paints on purpose. The human value is still named, because 12,128
    // tiles of real maps hold it and a map that does has to read as something.
    {0x0089, "Wall", true},
    {0x008d, "Wall, human", false},
    // 0x0000 declares neither land nor water and stops nothing, so a walker and
    // a ship may both be there — Land and water. War2XE calls it a bridge.
    //
    // Then the combinations, each a restriction added to a terrain rather than
    // painted bare. What stops a ground unit is SQ_UNPASSABLE, which is the bit
    // forest, rock and both walls carry and the reason none of them can be
    // walked through; SQ_MAN_AIR is what War2XE's "no flying units" sets. The
    // two together are the only barrier nothing crosses. War2XE writes its own
    // "no flying units" the same way, as 0x0201 and not as 0x0200 alone.
    //
    // SQ_VICTORY, SQ_RUNES and TR_START are deliberately not offered. Each was
    // painted on ground and taken into the game, and nothing happened — they
    // are scenario machinery the map alone cannot switch on, so a palette cell
    // for them would be a cell that does nothing. The bits keep their names in
    // the table below, because a map that holds one still has to be read.
    //
    // War2XE's "space", 0x0f00, went the same way. It is the four high bits
    // with no SQ_UNPASSABLE among them, and a footman walks straight over it —
    // tested. So did SQ_MAN on its own, which was offered here as "no walking"
    // on the strength of 0x0f00 being called space, and was wrong for the same
    // reason. Whatever that nibble is for, it is not stopping a man on foot.
    //
    // Which leaves SQ_BUILDING and SQ_AI_BUILDING below still untested. They
    // are offered because a bit named for building plausibly stops building,
    // and they will come out if that turns out to be another guess.
    {0x0000, "Land and water", true},
    {0x0281, "No walking or flying", true},
    {0x0201, "Ground, no flying", true},
    {0x0240, "Water, no flying", true},
    {0x0801, "Ground, no building", true},
    {0x0840, "Water, no building", true},
};

}  // namespace

uint16_t movement_no_flying_bit() { return kNoFlying; }

int movement_bit_count() {
  return int(sizeof(kMovementBits) / sizeof(kMovementBits[0]));
}

int movement_bit_value(int index) {
  if (index < 0 || index >= movement_bit_count()) return 0;
  return kMovementBits[index].value;
}

const char* movement_bit_name(int index) {
  if (index < 0 || index >= movement_bit_count()) return nullptr;
  return kMovementBits[index].name;
}

bool movement_class_offered(int index) {
  if (index < 0 || index >= int(sizeof(kMovementClasses) /
                                sizeof(kMovementClasses[0]))) {
    return false;
  }
  return kMovementClasses[index].offered;
}

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

// What a painted movement value lets stand on the tile.
//
// SQ_UNPASSABLE stops a walker and nothing else, SQ_MAN_AIR stops a flier, and
// the rest of the low byte says which of land and water the tile is - a tile
// declaring neither is both. That reproduces the terrain values: 0x0001
// walkable and not floatable, 0x0040 the reverse, 0x0000 both.
//
// UNPASSABLE reads as though it stopped everything, and does not. Every one of
// the 53 unit tiles in the corpus holding 0x0082, shore mostly water, carries a
// hull - so the bit that keeps a footman out of the trees is the same bit a
// ship ignores. Held to 84,365 unit tiles by
// units_in_real_maps_suit_their_own_movement, which is also what caught the
// stronger reading.
//
// Only asked about a tile somebody painted. Where the value is the one the
// terrain implies, the quadrant rules decide instead: they know things one word
// per tile cannot, such as which side of a shoreline a shipyard sits on.
bool movement_allows(uint16_t sq, UnitDomain domain) {
  switch (domain) {
    case kDomainLand: return !(sq & (kUnpassable | kWater));
    case kDomainWater: return !(sq & kLand);
    case kDomainAir: return !(sq & kNoFlying);
    default: return true;
  }
}

bool movement_allows_building(uint16_t sq) {
  return !(sq & (kUnpassable | kUnbuildable | kBuilding));
}

}  // namespace pf
