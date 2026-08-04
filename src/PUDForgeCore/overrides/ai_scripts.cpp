// Labels for the `AIPL` scripts.
//
// What a script *does* is read out of `rez/ai.bin` at runtime by ai_script.cpp,
// so a modded archive is described as it actually is. This holds only the three
// things the file itself cannot say.
//
// **Kind.** Four scripts are general purpose and cover essentially every melee
// map — across the 556 shipped maps, Land attack appears in 555, Sea attack in
// 135, Air attack in 117, Passive in 65. The other 79 are campaign scripts, and
// in a list of 83 that difference is worth flagging.
//
// **Aliases.** Four pairs are byte-identical in `ai.bin`, so they behave
// identically; "Orc 3" is Passive. Read out of the file by
// tools/dump-ai-scripts.mjs, not inferred.
//
// **Missions.** Every campaign script was written for one named mission, and
// "Expansion 17" does not say which. From the AI editor in WarDraft.

#include "../constants.hpp"

namespace pf {
namespace {

/// AIPL values, named rather than written as numbers at the call site.
constexpr int kLandAttack = 0, kPassive = 1, kSeaAttack = 25, kAirAttack = 26;
/// The first "Expansion N" entry; everything from here up is Dark Portal.
constexpr int kFirstExpansion = 32;

/**
 * Scripts whose bytes in `rez/ai.bin` are identical to another script's.
 *
 * Each entry is {duplicate, original}, read out of the game's data by
 * tools/dump-ai-scripts.mjs — the same bytes, therefore the same script.
 */
struct AiAlias {
  uint8_t duplicate;
  uint8_t original;
};

const AiAlias kAiAliases[] = {
    {2, 1},    // Orc 3        is Passive
    {76, 37},  // Expansion 45 is Expansion 6
    {72, 52},  // Expansion 41 is Expansion 21
    {75, 73},  // Expansion 44 is Expansion 42
};

/**
 * The mission each campaign script was written for, indexed from AIPL value 2.
 *
 * Empty where the name is already the mission. From the AI editor in WarDraft,
 * Alexander Cech's Warcraft II tool, which lists the mission behind every value.
 */
const char* const kMissionOf[] = {
    /* 2  */ "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    /* 18 */ "", "", "", "", "", "", "",
    /* 25 */ "", "",
    /* 27 */ "", "", "", "", "",
    /* 32 */ "Orc mission 4", "Orc mission 5", "Orc mission 7a",
    "Orc mission 9", "Orc mission 10", "Orc mission 12", "Orc mission 6a",
    "Orc mission 6b", "Orc mission 11a", "Orc mission 11b",
    "Human mission 2a (Red)", "Human mission 2b (Black)",
    "Human mission 2c (Yellow)", "Human mission 3a (Orange)",
    "Human mission 3b (Red)", "Human mission 3c (Violet)",
    "Human mission 4a (Black)", "Human mission 4b (Red)",
    "Human mission 4c (White)", "Human mission 5a (Green)",
    "Human mission 5b (Orange)", "Human mission 5c (Violet)",
    "Human mission 5d (Yellow)", "Human mission 6a (Green)",
    "Human mission 6b (Black)", "Human mission 6c (Orange)",
    "Human mission 6d (Red)", "Human mission 8a (White)",
    "Human mission 8b (Yellow)", "Human mission 8c (Violet)",
    "Human mission 9a (Black)", "Human mission 9b (Red)",
    "Human mission 9c (Green)", "Human mission 9d (White)",
    "Human mission 10a (Violet)", "Human mission 10b (Green)",
    "Human mission 10c (Black)", "Human mission 11a", "Human mission 11b",
    "Human mission 12a", "Orc mission 5b", "Human mission 7a",
    "Human mission 7b", "Human mission 7c", "Orc mission 12a",
    "Orc mission 12b", "Orc mission 12c", "Orc mission 12d",
    "Orc mission 2", "Orc mission 7b", "Orc mission 3",
};
constexpr int kFirstMissionValue = 2;

}  // namespace

const char* ai_mission(int value) {
  const int at = value - kFirstMissionValue;
  const int count = int(sizeof(kMissionOf) / sizeof(kMissionOf[0]));
  if (at < 0 || at >= count) return nullptr;
  const char* mission = kMissionOf[at];
  return mission[0] ? mission : nullptr;
}

int ai_same_as(int value) {
  for (const AiAlias& alias : kAiAliases) {
    if (alias.duplicate == value) return alias.original;
  }
  return -1;
}

int ai_kind(int value) {
  if (value == kLandAttack || value == kPassive
      || value == kSeaAttack || value == kAirAttack) {
    return kAiGeneral;
  }
  if (value < 0 || value >= kAiNameCount) return kAiGeneral;
  return value >= kFirstExpansion ? kAiExpansion : kAiCampaign;
}

}  // namespace pf
