// Which of the sixteen player slots the game actually supports.
//
// The format gives every per-player table sixteen entries — OWNR, SIDE, SGLD,
// SLBR, SOIL, AIPL, and all six ALOW blocks — and Warcraft II plays eight of
// them. Slot 16 is the neutral owner: gold mines, oil patches, critters and
// circles of power belong to it. The seven in between are storage the game has
// no use for, and no map uses them either.
//
// Across the 1378-map corpus, counting a slot as used when OWNR says anything
// but "nobody", or when a placed unit names it as owner:
//
//     slot        1     2     3     4     5     6     7     8
//     OWNR     1327  1334  1136  1075   975   991   839   785
//     units    1327  1332  1136  1073   975   991   838   784
//
//     slot        9    10    11    12    13    14    15      16
//     OWNR        0     0     0     0     0     0     0    1378
//     units       0     0     0     0     0     0     0    1112
//
// Not one map out of 1378, from Blizzard or from any community author, puts
// anything in slots 9 to 15. Zero across seven slots and two independent
// measures is not a gap in the sample; it is the game's limit showing through.
//
// The tables stay sixteen wide — this says which entries are worth showing a
// person, not which ones exist. A map that somehow carries data in a dead slot
// still round-trips it untouched.

#include "../constants.hpp"

namespace pf {

bool player_slot_is_supported(int player) {
  if (player < 0 || player >= kPlayerCount) return false;
  return player < 8 || player == kNeutralPlayer;
}

}  // namespace pf
