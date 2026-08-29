// Which two units may stand on the same tiles.
//
// Every other overlap is a mistake worth reporting, so this list decides what
// the map check keeps quiet about. It is short on purpose: a pair listed here
// is one the game itself puts on top of each other.
//
// **Start locations** are markers rather than things. A town hall or a worker
// sits on one on nearly every map — 17 times across the five in
// `test/fixtures`, and on every shipped multiplayer map.
//
// **Oil patches** are harvested by moving onto them, so a tanker or an escort
// parked on one is what the map author meant. Seven times in the fixtures: two
// battleships, four giant turtles and a destroyer, one to a patch.
//
// **The Circle of Power** is where a unit is supposed to stand; that is the
// whole of what it is for. Read off the game rather than measured — the
// fixtures are multiplayer maps and carry none — so it is the one line here
// with no count behind it.
//
// **Gold mines are not on this list.** You cannot stand on one. A worker walks
// into it and is gone until it comes out again, and nothing else may be there
// at all. A town hall too close to a mine is a real fault and has a rule of its
// own, `hall_clearance.cpp`, which measures the gap rather than the contact —
// so keeping mines out of this list loses nothing and keeps the fault.

#include "../constants.hpp"

namespace pf {
namespace {

constexpr int kOilPatch = 0x5d;
constexpr int kHumanStart = 0x5e;
constexpr int kOrcStart = 0x5f;
constexpr int kCircleOfPower = 0x64;

/// True for the unit that the other one is allowed to be standing on.
bool is_shareable(int unit_id) {
  return unit_id == kHumanStart || unit_id == kOrcStart ||
         unit_id == kOilPatch || unit_id == kCircleOfPower;
}

}  // namespace

bool units_may_share_tiles(int a, int b) {
  return is_shareable(a) || is_shareable(b);
}

}  // namespace pf
