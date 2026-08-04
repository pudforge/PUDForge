// The Warcraft II AI instruction set.
//
// Hand-written because it is in no file the game ships: `rez/ai.bin` holds the
// scripts but nothing saying what an opcode or a variable number means. The
// mnemonics, variable numbers, item codes, wait codes and rate numbers come
// from the AI compiler in WarDraft, Alexander Cech's 1997 Warcraft II tool.
//
// The opcode *numbers* are not in that specification and were recovered from
// `ai.bin` by keeping the arrangements under which all 83 shipped scripts
// decode with nothing left over, which pins four of them — see
// reference/docs/game-data.md. `wait`, `rate` and `item` occur in no shipped script and
// so have no number; a script using one stops the decoder rather than being
// guessed at, since a wrong width desynchronises everything after it.

#include "../ai_script.hpp"

namespace pf {

const AiOpcode kAiOpcodes[] = {
    {0x00, AiOp::kVar, 3},
    {0x01, AiOp::kGoto, 3},
    {0x02, AiOp::kSleep, 5},
    {0x03, AiOp::kDo, 2},
};
const int kAiOpcodeCount = int(sizeof(kAiOpcodes) / sizeof(kAiOpcodes[0]));

/// What each variable holds. Most are "how many of this do I want", which is how
/// a script states the force it is trying to field.
const AiVariable kAiVariables[] = {
    {0x09, "attack by land", AiVarKind::kFlag},
    {0x0a, "attack by sea", AiVarKind::kFlag},
    {0x0b, "attack by air", AiVarKind::kFlag},
    {0x0c, "use strategy", AiVarKind::kFlag},
    {0x0d, "land party size", AiVarKind::kParty},
    {0x0e, "land parties", AiVarKind::kParty},
    {0x0f, "sea party size", AiVarKind::kParty},
    {0x10, "sea parties", AiVarKind::kParty},
    {0x11, "air party size", AiVarKind::kParty},
    {0x12, "air parties", AiVarKind::kParty},
    {0x13, "workers", AiVarKind::kForce},
    {0x14, "footmen/grunts", AiVarKind::kForce},
    {0x15, "archers/axethrowers", AiVarKind::kForce},
    {0x16, "ballistas/catapults", AiVarKind::kForce},
    {0x17, "knights/ogres", AiVarKind::kForce},
    {0x18, "oil tankers", AiVarKind::kForce},
    {0x19, "destroyers", AiVarKind::kForce},
    {0x1a, "transports", AiVarKind::kForce},
    {0x1b, "battleships/juggernaughts", AiVarKind::kForce},
    {0x1c, "submarines/turtles", AiVarKind::kForce},
    {0x1d, "mages/death knights", AiVarKind::kForce},
    {0x1e, "flying machines/zeppelins", AiVarKind::kForce},
    {0x1f, "demolition squads/sappers", AiVarKind::kForce},
    {0x20, "gryphons/dragons", AiVarKind::kForce},
    {0x21, "aggression", AiVarKind::kAggression},
    {0x22, "reset items", AiVarKind::kOther},
};
const int kAiVariableCount = int(sizeof(kAiVariables) / sizeof(kAiVariables[0]));

/**
 * Item codes from 0x80 up: research and tier-ups.
 *
 * A numbering of their own, unrelated to `UGRD` order. Below 0x80 an item code
 * is a unit id, so those are named from the game's own unit table instead and
 * are not repeated here.
 */
const AiItem kAiResearch[] = {
    {0x80, "arrow/axe 1"},          {0x81, "arrow/axe 2"},
    {0x82, "rangers/berserkers"},   {0x83, "ranger upgrade A"},
    {0x84, "ranger upgrade B"},        {0x85, "ranger upgrade C"},
    {0x86, "melee attack 1"},          {0x87, "melee attack 2"},
    {0x88, "melee armour 1"},          {0x89, "melee armour 2"},
    {0x8a, "siege 1"},                 {0x8b, "siege 2"},
    {0x8c, "ship cannon 1"},           {0x8d, "ship cannon 2"},
    {0x8e, "ship armour 1"},           {0x8f, "ship armour 2"},
    {0x90, "paladins/ogre-mages"},  {0x91, "paladin spell A"},
    {0x92, "paladin spell B"},         {0x93, "mage spell A"},
    {0x94, "mage spell B"},            {0x95, "mage spell C"},
    {0x96, "mage spell D"},            {0x97, "mage spell E"},
    {0x98, "keep/stronghold"},      {0x99, "castle/fortress"},
};
const int kAiResearchCount = int(sizeof(kAiResearch) / sizeof(kAiResearch[0]));

/**
 * Buildings whose two races have different names, so the summary can say both.
 *
 * Everything else below 0x80 takes the unit table's own name, which already
 * reads correctly for a single-race entry like the farm or the blacksmith.
 */
const AiItem kAiPairedNames[] = {
    {0x3e, "church/altar of storms"},
    {0x42, "stables/ogre mound"},
    {0x44, "inventor/alchemist"},
    {0x46, "aviary/roost"},
    {0x4a, "town hall/great hall"},
    {0x50, "mage tower/temple"},
};
const int kAiPairedNameCount = int(sizeof(kAiPairedNames) / sizeof(kAiPairedNames[0]));

}  // namespace pf
