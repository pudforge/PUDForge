// What an ALOW section looks like when it restricts nothing.
//
// The section is optional — 1338 of the 1378 corpus maps have none — so adding
// one must reproduce how the game already behaves without it. Setting every bit
// looks like the safe reading of "nothing restricted" and is wrong: two of the
// six blocks do not mean "allowed" at all, and all-ones tells the game every
// player is mid-research on everything and starts with every spell known.
//
// Measured over the 40 maps that do carry the section, counting only maps whose
// 16 players share one value:
//
//     block                 all-ones   zero   0x00004020   maps agreeing
//     unitsAllowed              14       0        0            14 / 40
//     spellsStartWith            1       5       17            17 / 40
//     spellsAllowed             27       0        0            27 / 40
//     spellsResearching          1      39        0            39 / 40
//     upgradesAllowed           21       0        0            21 / 40
//     upgradesResearching        1      39        0            39 / 40
//
// The single all-ones outlier in the three telling blocks is one map, "-= Wall
// Knights =-", whose six blocks are all 0xFFFFFFFF — the shape an editor writes
// when it makes exactly the assumption this file exists to correct.
//
// spellsStartWith is the four spells the game never lets anyone research, and
// it takes both sources to name all four. Checked in the game: the two basic
// attack spells come with the human and orc spellcasters, and the human and
// orc vision spells arrive with the elite cavalry upgrade. None of the four
// has a research to buy, so a map cannot withhold them.
//
//     corpus mode          0x00004020   bits 5 and 14, the two attack spells
//     UGRD costing nothing 0x00000421   bits 0, 5 and 10, the two vision
//                                       spells and the human attack spell
//     union                0x00004421   all four
//
// Neither source alone is right. The corpus omits the two vision spells because
// their bits are don't-care — the game ignores what a map says about a spell it
// will hand over regardless — and UGRD's zero-cost test misses the orc attack
// spell, whose entry carries a research time of 100 it never charges.
// An editor showing a person what they can restrict must show all four as
// already known, so the union is what we write.

#include "../constants.hpp"

namespace pf {

/// One 32-bit value per block, repeated across all 16 players.
const uint32_t kDefaultAlowBlock[kAlowBlocks] = {
    0xFFFFFFFFu,  // unitsAllowed — every unit buildable
    0x00004421u,  // spellsStartWith — bits 0, 5, 10, 14: the four with no
                  // research to buy, so no map can withhold them
    0xFFFFFFFFu,  // spellsAllowed — every spell researchable
    0x00000000u,  // spellsResearching — nothing part-way researched
    0xFFFFFFFFu,  // upgradesAllowed — every upgrade researchable
    0x00000000u,  // upgradesResearching — nothing part-way researched
};

}  // namespace pf
