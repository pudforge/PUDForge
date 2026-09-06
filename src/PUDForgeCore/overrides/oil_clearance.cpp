// How much room oil needs from the buildings that work it.
//
// A shipyard and a refinery are where a tanker docks; oil sits four tiles off
// them and never closer. Measured over the 357 maps under `reference/war2_ref`,
// gap in tiles between the two footprints, straight and diagonal alike:
//
//     gap        0  1  2  3  4  5  6  7  8
//     shipyard   0  0  0  0  8  2  3  3  1     32 pairs over 17 maps
//     refinery   0  0  0  0  2  2  2  4  1     14 pairs over  7 maps
//     foundry    0  1  1  2  1  1  0  0  2     12 pairs over  8 maps
//
// The hole is at 0 to 3 and the pile is on 4 itself, which is what a rule
// somebody is building against looks like — the same shape hall_clearance.cpp
// reads off the gold mine.
//
// **The foundry is not one of them**, and that is the finding rather than a
// gap in the data: it holds oil at 1, 2 and 3 tiles in maps the game's own
// editor wrote. It is the one shore building a tanker never visits.
//
// Thinner evidence than the hall's 3013 pairs, so it is written down where it
// can be re-measured. What it has instead is a second source that agrees: the
// rule was reported from play, naming the shipyard and the refinery and
// leaving the foundry out, before the corpus was asked.

#include "../constants.hpp"

namespace pf {
namespace {

/// Human and orc shipyards and refineries. Not the foundries, which sit at 1.
constexpr int kOilWorkers[] = {0x48, 0x49, 0x54, 0x55};

/// The patch a tanker harvests, and the two wells built on one. All three
/// carry oil and all three keep the same distance.
constexpr int kOil[] = {0x5d, 0x56, 0x57};

}  // namespace

bool unit_is_oil(int unit_id) {
  for (int oil : kOil) if (oil == unit_id) return true;
  return false;
}

bool unit_needs_oil_clearance(int unit_id) {
  for (int worker : kOilWorkers) if (worker == unit_id) return true;
  return false;
}

int oil_clearance_tiles() { return 4; }

}  // namespace pf
