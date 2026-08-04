// Which voice a unit answers in.
//
// Warcraft II keeps unit speech in `gamesfx/<folder>/` under names irregular
// enough that no rule derives them — the footman is `human/hready.wav`, the
// knight `knight/knready.wav`, the archer `elves/eready.wav` but
// `elves/ewhat1.wav` — so a generated path is wrong about a third of the time.
//
// Which unit takes which folder is in the game's data nowhere either: several
// units share a voice (a Paladin rides a Knight and sounds like one). That
// judgement is why this is in `overrides/`.
//
// Only two sounds are named, because only two are about something an editor
// does: a unit appearing, and a unit being pointed at. Anything the game left
// silent gets `sfx/button.wav`, so an edit is always audible and the editor
// never invents speech Blizzard did not record.

#include <string>

#include "../constants.hpp"

namespace pf {
namespace {

/// A voice: where it lives, and what the game called its files.
///
/// `what_count` is how many selection lines the folder holds, numbered from 1.
/// Zero means the folder has none, which is true of the Peon (one `pnready`
/// and nothing else) and of every building.
struct SoundSet {
  const char* folder;       ///< under `gamesfx/`
  const char* ready;        ///< file name without `.wav`, or null
  const char* what;         ///< the numbered selection lines' stem, or null
  int what_count;
};

/// The voices, in no particular order — `kUnitVoice` indexes this.
const SoundSet kSets[] = {
    {"human", "hready", "hwhat", 6},        //  0 human infantry
    {"orc", "oready", "owhat", 6},          //  1 orc infantry
    {"peasant", "psready", "pswhat", 4},    //  2
    {"peon", "pnready", nullptr, 0},        //  3 the game gave it one line
    {"knight", "knready", "knwhat", 4},     //  4
    {"ogre", "ogready", "ogwhat", 4},       //  5
    {"elves", "eready", "ewhat", 4},        //  6
    {"troll", "trready", "trwhat", 3},      //  7
    {"wizard", "wzready", "wzwhat", 3},     //  8
    {"deathknt", "dkready", "dkwhat", 2},   //  9
    {"paladin", "pkready", "pkwhat", 4},    // 10
    {"ogremage", "omready", "omwhat", 4},   // 11
    {"dwarf", "dwready", "dwhat", 2},       // 12 `dwhat`, not `dwwhat`
    {"goblin", "goready", "gowhat", 4},     // 13
    {"gnome", "gnready", nullptr, 0},       // 14
    {"zeppelin", "gbready", nullptr, 0},    // 15
    {"griffon", nullptr, "grwhat", 0},      // 16 one unnumbered line
    {"dragon", "drready", "drwhat", 0},     // 17 likewise
    {"ships", "hshpredy", "hshpwht", 3},    // 18 every human hull
    {"ships", "oshpredy", "oshpwht", 3},    // 19 and every orc one
    {"aleria", nullptr, "alwhat", 3},       // 20 the named heroes, who are
    {"danath", nullptr, "dnwhat", 3},       // 21 never built and so were
    {"kargath", nullptr, "kawhat", 3},      // 22 never given a ready line
    {"khadgar", nullptr, "khwhat", 3},      // 23
    {"kurdran", nullptr, "kuwhat", 3},      // 24
    {"teron", nullptr, "tewhat", 3},        // 25
    {"turalyon", nullptr, "tuwhat", 3},     // 26
    {"grom", nullptr, "grwhat", 3},         // 27
    {"dentarg", nullptr, "odwhat", 3},      // 28
    {"deathwng", nullptr, "dewhat", 3},     // 29
};

constexpr uint8_t kNone = 0xff;

/// Which voice each unit answers in, by unit id. kNone leaves it to the
/// fallback in `unit_sound_path`, which is where every building lands: a
/// barracks has no voice, it has a noise, and that noise is its own file in
/// `gamesfx/bldg/` rather than a line of speech.
const uint8_t kUnitVoice[kUnitCount] = {
    0,     // 0x00 Footman
    1,     // 0x01 Grunt
    2,     // 0x02 Peasant
    3,     // 0x03 Peon
    kNone, // 0x04 Ballista — a machine, and the game gave it no voice
    kNone, // 0x05 Catapult
    4,     // 0x06 Knight
    5,     // 0x07 Ogre
    6,     // 0x08 Archer
    7,     // 0x09 Axethrower
    8,     // 0x0a Mage
    9,     // 0x0b Death Knight
    10,    // 0x0c Paladin
    11,    // 0x0d Ogre-Mage
    12,    // 0x0e Dwarven Demolition Squad
    13,    // 0x0f Goblin Sappers
    2,     // 0x10 Attack Peasant — the same worker with a sword
    3,     // 0x11 Attack Peon
    6,     // 0x12 Ranger — an Elven Archer, and sounds like one
    7,     // 0x13 Berserker
    20,    // 0x14 Alleria
    25,    // 0x15 Teron Gorefiend
    24,    // 0x16 Kurdran and Sky'ree
    28,    // 0x17 Dentarg
    23,    // 0x18 Khadgar
    27,    // 0x19 Grom Hellscream
    18,    // 0x1a Human Oil Tanker
    19,    // 0x1b Orc Oil Tanker
    18,    // 0x1c Human Transport
    19,    // 0x1d Orc Transport
    18,    // 0x1e Elven Destroyer
    19,    // 0x1f Troll Destroyer
    18,    // 0x20 Battleship
    19,    // 0x21 Juggernaught
    kNone, // 0x22 unused
    9,     // 0x23 Deathwing's Skeleton — a summoned thing, the Death Knight's
    kNone, // 0x24 unused
    kNone, // 0x25 unused
    14,    // 0x26 Gnomish Submarine
    19,    // 0x27 Giant Turtle
    14,    // 0x28 Gnomish Flying Machine
    15,    // 0x29 Goblin Zeppelin
    16,    // 0x2a Gryphon Rider
    17,    // 0x2b Dragon
    kNone, // 0x2c Turalyon — mounted; see below
    kNone, // 0x2d Eye of Kilrogg
    21,    // 0x2e Danath
    22,    // 0x2f Kargath Bladefist
    kNone, // 0x30 unused
    29,    // 0x31 Cho'gall — Deathwing's folder is the only ogre-mage hero one
    kNone, // 0x32 Lothar
    kNone, // 0x33 Gul'dan
    kNone, // 0x34 Uther Lightbringer
    kNone, // 0x35 Zuljin
    kNone, // 0x36 unused
    29,    // 0x37 Deathwing
    kNone, // 0x38 unused
    kNone, // 0x39 Critters — sheep, seals, pigs; `misc` holds them, unshared
    kNone, // 0x3a Farm
    kNone, // 0x3b Pig Farm
    kNone, // 0x3c Human Barracks
    kNone, // 0x3d Orc Barracks
    kNone, // 0x3e Church
    kNone, // 0x3f Altar of Storms
    kNone, // 0x40 Human Scout Tower
    kNone, // 0x41 Orc Scout Tower
    kNone, // 0x42 Stables
    kNone, // 0x43 Ogre Mound
    kNone, // 0x44 Gnomish Inventor
    kNone, // 0x45 Goblin Alchemist
    kNone, // 0x46 Gryphon Aviary
    kNone, // 0x47 Dragon Roost
    kNone, // 0x48 Human Shipyard
    kNone, // 0x49 Orc Shipyard
    kNone, // 0x4a Town Hall
    kNone, // 0x4b Great Hall
    kNone, // 0x4c Elven Lumber Mill
    kNone, // 0x4d Troll Lumber Mill
    kNone, // 0x4e Human Foundry
    kNone, // 0x4f Orc Foundry
    kNone, // 0x50 Mage Tower
    kNone, // 0x51 Temple of the Damned
    kNone, // 0x52 Human Blacksmith
    kNone, // 0x53 Orc Blacksmith
    kNone, // 0x54 Human Refinery
    kNone, // 0x55 Orc Refinery
    kNone, // 0x56 Human Oil Well
    kNone, // 0x57 Orc Oil Well
    kNone, // 0x58 Keep
    kNone, // 0x59 Stronghold
    kNone, // 0x5a Castle
    kNone, // 0x5b Fortress
    kNone, // 0x5c Gold Mine
    kNone, // 0x5d Oil Patch
    kNone, // 0x5e Human Start Location
    kNone, // 0x5f Orc Start Location
    kNone, // 0x60 Human Guard Tower
    kNone, // 0x61 Orc Guard Tower
    kNone, // 0x62 Human Cannon Tower
    kNone, // 0x63 Orc Cannon Tower
    kNone, // 0x64 Circle of Power
    kNone, // 0x65 Dark Portal
    kNone, // 0x66 Runestone
    kNone, // 0x67 Human Wall
    kNone, // 0x68 Orc Wall
    kNone, // 0x69 unused
    kNone, // 0x6a unused
    kNone, // 0x6b unused
    kNone, // 0x6c unused
    kNone, // 0x6d unused
};

static_assert(sizeof(kUnitVoice) == kUnitCount, "one voice per unit id");

/// The noise a building makes while it is working, which is what the game plays
/// over one. Not speech, so it is not a voice and not in `kSets`.
///
/// Only the buildings the game gave a sound to are here; inventing one for a
/// barracks would be putting words in Blizzard's mouth.
struct BuildingNoise {
  uint8_t unit;
  const char* file;      ///< under `gamesfx/bldg/`, without `.wav`
};

const BuildingNoise kBuildingNoises[] = {
    {0x3a, "hfarm"},    {0x3b, "ofarm"},    {0x3e, "hchant"},
    {0x3f, "ochant"},   {0x42, "stables"},  {0x43, "ogrecamp"},
    {0x44, "inventor"}, {0x45, "alchemst"}, {0x46, "aviary"},
    {0x47, "dragon"},   {0x48, "shipbell"}, {0x49, "shipbell"},
    {0x4c, "lumbmill"}, {0x4d, "lumbmill"}, {0x4e, "foundry"},
    {0x4f, "foundry"},  {0x50, "wzrdtowr"}, {0x51, "dthtower"},
    {0x52, "smith"},    {0x53, "smith"},    {0x54, "oilrefin"},
    {0x55, "oilrefin"}, {0x56, "oilplat"},  {0x57, "oilplat"},
    {0x5c, "mine"},
};

const char* building_noise(int unit_id) {
  for (const BuildingNoise& one : kBuildingNoises) {
    if (one.unit == unit_id) return one.file;
  }
  return nullptr;
}

std::string in_gamesfx(const char* folder, const std::string& name) {
  return std::string("gamesfx/") + folder + "/" + name + ".wav";
}

}  // namespace

std::string unit_sound_path(int unit_id, int kind, int salt) {
  if (unit_id < 0 || unit_id >= kUnitCount) return {};

  const uint8_t voice = kUnitVoice[unit_id];
  if (voice != kNone) {
    const SoundSet& set = kSets[voice];
    if (kind == kSoundReady && set.ready) return in_gamesfx(set.folder, set.ready);
    if (kind == kSoundSelected && set.what) {
      // One of the numbered lines, chosen by the caller's salt, so a footman
      // clicked twice answers twice differently the way the game does. Deciding
      // it here would make this untestable for a policy that is not the core's.
      if (set.what_count <= 0) return in_gamesfx(set.folder, set.what);
      const int pick = (salt % set.what_count + set.what_count) % set.what_count;
      return in_gamesfx(set.folder, std::string(set.what) + char('1' + pick));
    }
  }

  // A building announces itself with the noise it makes while it works, which
  // is the closest thing it has to a voice; everything else gets the interface
  // click rather than speech the game never recorded.
  if (const char* noise = building_noise(unit_id)) {
    if (kind == kSoundReady) return std::string("gamesfx/bldg/") + noise + ".wav";
  }
  return "sfx/button.wav";
}

}  // namespace pf
