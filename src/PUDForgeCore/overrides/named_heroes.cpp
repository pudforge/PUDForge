// The named characters the game's own hero flag misses.
//
// `UDTA` bit 23 is the obvious thing to classify heroes with and is wrong for
// most of them: only Lothar, Uther, Cho'gall, Gul'dan and Zuljin set it, so a
// palette grouped on the flag files ten named characters in with the footmen.
// The flag presumably means something else — very likely invincibility — and
// this records the editing judgement only.
//
// Additive: a unit the game marks as a hero stays one whatever this says.

#include "../constants.hpp"

namespace pf {
namespace {

// Unit ids, named rather than written as numbers at the call site.
constexpr uint8_t kAlleria = 0x14, kTeronGorefiend = 0x15, kKurdranAndSkyree = 0x16;
constexpr uint8_t kDentarg = 0x17, kKhadgar = 0x18, kGromHellscream = 0x19;
constexpr uint8_t kDeathwing = 0x23, kTuralyon = 0x2c, kDanath = 0x2e;
constexpr uint8_t kKorgathBladefist = 0x2f;

/// The named characters the flag leaves out. Human and orc, in id order.
const uint8_t kUnflaggedHeroes[] = {
    kAlleria,          // human
    kTeronGorefiend,   // orc
    kKurdranAndSkyree, // human
    kDentarg,          // orc
    kKhadgar,          // human
    kGromHellscream,   // orc
    kDeathwing,        // orc
    kTuralyon,         // human
    kDanath,           // human
    kKorgathBladefist, // orc
};

}  // namespace

bool unit_is_named_hero(int unit_id) {
  for (uint8_t id : kUnflaggedHeroes) {
    if (id == unit_id) return true;
  }
  return false;
}

int unit_named_hero_count() {
  return int(sizeof(kUnflaggedHeroes) / sizeof(kUnflaggedHeroes[0]));
}

}  // namespace pf
