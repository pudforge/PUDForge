// Unit, upgrade and player tables.
//
// Originally generated from the JavaScript prototype's tables; ordinary
// hand-edited source now. `constants_match_the_format` in the test suite checks
// it against the format's own invariants.

#include "constants.hpp"

namespace pf {

const UnitInfo kUnits[kUnitCount] = {
    {"Footman", 'h', false},  // 0x00
    {"Grunt", 'o', false},  // 0x01
    {"Peasant", 'h', false},  // 0x02
    {"Peon", 'o', false},  // 0x03
    {"Ballista", 'h', false},  // 0x04
    {"Catapult", 'o', false},  // 0x05
    {"Knight", 'h', false},  // 0x06
    {"Ogre", 'o', false},  // 0x07
    {"Archer", 'h', false},  // 0x08
    {"Axethrower", 'o', false},  // 0x09
    {"Mage", 'h', false},  // 0x0a
    {"Death Knight", 'o', false},  // 0x0b
    {"Paladin", 'h', false},  // 0x0c
    {"Ogre-Mage", 'o', false},  // 0x0d
    {"Dwarven Demolition Squad", 'h', false},  // 0x0e
    {"Goblin Sappers", 'o', false},  // 0x0f
    {"Attack Peasant", 'h', false},  // 0x10
    {"Attack Peon", 'o', false},  // 0x11
    {"Ranger", 'h', false},  // 0x12
    {"Berserker", 'o', false},  // 0x13
    {"Alleria", 'h', false},  // 0x14
    {"Teron Gorefiend", 'o', false},  // 0x15
    {"Kurdran and Sky'ree", 'h', false},  // 0x16
    {"Dentarg", 'o', false},  // 0x17
    {"Khadgar", 'h', false},  // 0x18
    {"Grom Hellscream", 'o', false},  // 0x19
    {"Human Oil Tanker", 'h', false},  // 0x1a
    {"Orc Oil Tanker", 'o', false},  // 0x1b
    {"Human Transport", 'h', false},  // 0x1c
    {"Orc Transport", 'o', false},  // 0x1d
    {"Elven Destroyer", 'h', false},  // 0x1e
    {"Troll Destroyer", 'o', false},  // 0x1f
    {"Battleship", 'h', false},  // 0x20
    {"Juggernaught", 'o', false},  // 0x21
    {"Unused #34", 'n', true},  // 0x22
    {"Deathwing", 'o', false},  // 0x23
    {"Unused #36", 'n', true},  // 0x24
    {"Unused #37", 'n', true},  // 0x25
    {"Gnomish Submarine", 'h', false},  // 0x26
    {"Giant Turtle", 'o', false},  // 0x27
    {"Gnomish Flying Machine", 'h', false},  // 0x28
    {"Goblin Zeppelin", 'o', false},  // 0x29
    {"Gryphon Rider", 'h', false},  // 0x2a
    {"Dragon", 'o', false},  // 0x2b
    {"Turalyon", 'h', false},  // 0x2c
    {"Eye of Kilrogg", 'o', false},  // 0x2d
    {"Danath", 'h', false},  // 0x2e
    {"Korgath Bladefist", 'o', false},  // 0x2f
    {"Unused #48", 'n', true},  // 0x30
    {"Cho'gall", 'o', false},  // 0x31
    {"Lothar", 'h', false},  // 0x32
    {"Gul'dan", 'o', false},  // 0x33
    {"Uther Lightbringer", 'h', false},  // 0x34
    {"Zuljin", 'o', false},  // 0x35
    {"Unused #54", 'n', true},  // 0x36
    {"Skeleton", 'n', false},  // 0x37
    {"Daemon", 'n', false},  // 0x38
    {"Critter", 'n', false},  // 0x39
    {"Farm", 'h', false},  // 0x3a
    {"Pig Farm", 'o', false},  // 0x3b
    {"Human Barracks", 'h', false},  // 0x3c
    {"Orc Barracks", 'o', false},  // 0x3d
    {"Church", 'h', false},  // 0x3e
    {"Altar of Storms", 'o', false},  // 0x3f
    {"Human Scout Tower", 'h', false},  // 0x40
    {"Orc Scout Tower", 'o', false},  // 0x41
    {"Stables", 'h', false},  // 0x42
    {"Ogre Mound", 'o', false},  // 0x43
    {"Gnomish Inventor", 'h', false},  // 0x44
    {"Goblin Alchemist", 'o', false},  // 0x45
    {"Gryphon Aviary", 'h', false},  // 0x46
    {"Dragon Roost", 'o', false},  // 0x47
    {"Human Shipyard", 'h', false},  // 0x48
    {"Orc Shipyard", 'o', false},  // 0x49
    {"Town Hall", 'h', false},  // 0x4a
    {"Great Hall", 'o', false},  // 0x4b
    {"Elven Lumber Mill", 'h', false},  // 0x4c
    {"Troll Lumber Mill", 'o', false},  // 0x4d
    {"Human Foundry", 'h', false},  // 0x4e
    {"Orc Foundry", 'o', false},  // 0x4f
    {"Mage Tower", 'h', false},  // 0x50
    {"Temple of the Damned", 'o', false},  // 0x51
    {"Human Blacksmith", 'h', false},  // 0x52
    {"Orc Blacksmith", 'o', false},  // 0x53
    {"Human Refinery", 'h', false},  // 0x54
    {"Orc Refinery", 'o', false},  // 0x55
    {"Human Oil Well", 'h', false},  // 0x56
    {"Orc Oil Well", 'o', false},  // 0x57
    {"Keep", 'h', false},  // 0x58
    {"Stronghold", 'o', false},  // 0x59
    {"Castle", 'h', false},  // 0x5a
    {"Fortress", 'o', false},  // 0x5b
    {"Gold Mine", 'n', false},  // 0x5c
    {"Oil Patch", 'n', false},  // 0x5d
    {"Human Start Location", 'h', false},  // 0x5e
    {"Orc Start Location", 'o', false},  // 0x5f
    {"Human Guard Tower", 'h', false},  // 0x60
    {"Orc Guard Tower", 'o', false},  // 0x61
    {"Human Cannon Tower", 'h', false},  // 0x62
    {"Orc Cannon Tower", 'o', false},  // 0x63
    {"Circle of Power", 'n', false},  // 0x64
    {"Dark Portal", 'n', false},  // 0x65
    {"Runestone", 'n', false},  // 0x66
    {"Human Wall", 'h', false},  // 0x67
    {"Orc Wall", 'o', false},  // 0x68
    {"Corpse", 'n', false},  // 0x69
    {"1x1 Rubble", 'n', false},  // 0x6a
    {"2x2 Rubble", 'n', false},  // 0x6b
    {"3x3 Rubble", 'n', false},  // 0x6c
    {"4x4 Rubble", 'n', false},  // 0x6d
};

const char* const kUpgrades[kUpgradeCount] = {
    "Sword 1",
    "Sword 2",
    "Axe 1",
    "Axe 2",
    "Arrow 1",
    "Arrow 2",
    "Throwing Axe 1",
    "Throwing Axe 2",
    "Human Shield 1",
    "Human Shield 2",
    "Orc Shield 1",
    "Orc Shield 2",
    "Human Ship Cannon 1",
    "Human Ship Cannon 2",
    "Orc Ship Cannon 1",
    "Orc Ship Cannon 2",
    "Human Ship Armor 1",
    "Human Ship Armor 2",
    "Orc Ship Armor 1",
    "Orc Ship Armor 2",
    "Catapult 1",
    "Catapult 2",
    "Ballista 1",
    "Ballista 2",
    "Train Rangers",
    "Longbow",
    "Ranger Scouting",
    "Ranger Marksmanship",
    "Train Berserkers",
    "Lighter Axes",
    "Berserker Scouting",
    "Berserker Regeneration",
    "Train Ogre-Mages",
    "Train Paladins",
    "Holy Vision",
    "Healing",
    "Exorcism",
    "Flame Shield",
    "Fireball",
    "Slow",
    "Invisibility",
    "Polymorph",
    "Blizzard",
    "Eye of Kilrogg",
    "Bloodlust",
    "Raise Dead",
    "Death Coil",
    "Whirlwind",
    "Haste",
    "Unholy Armor",
    "Runes",
    "Death and Decay",
};

const char* const kPlayerNames[kPlayerCount] = {
    "Player 1 (Red)",
    "Player 2 (Blue)",
    "Player 3 (Green)",
    "Player 4 (Violet)",
    "Player 5 (Orange)",
    "Player 6 (Black)",
    "Player 7 (White)",
    "Player 8 (Yellow)",
    "Player 9",
    "Player 10",
    "Player 11",
    "Player 12",
    "Player 13",
    "Player 14",
    "Player 15",
    "Neutral",
};

const uint32_t kPlayerColors[kPlayerCount] = {
    0xc81414,
    0x2038c8,
    0x20a020,
    0x8020a0,
    0xe08000,
    0x202020,
    0xe8e8e8,
    0xd0c020,
    0x606060,
    0x606060,
    0x606060,
    0x606060,
    0x606060,
    0x606060,
    0x606060,
    0x909090,
};

// Sprite path per unit id, empty when the unit has no artwork.
const char* const kUnitSprites[kUnitCount] = {
    "human/grunt",  // 0x00 Footman
    "orc/grunt",  // 0x01 Grunt
    "human/peon",  // 0x02 Peasant
    "orc/peon",  // 0x03 Peon
    "human/catapult",  // 0x04 Ballista
    "orc/catapult",  // 0x05 Catapult
    "human/knight",  // 0x06 Knight
    "orc/knight",  // 0x07 Ogre
    "human/spear",  // 0x08 Archer
    "orc/spear",  // 0x09 Axethrower
    "human/wizard",  // 0x0a Mage
    "orc/dknight",  // 0x0b Death Knight
    // The paladin and Uther ride, so the knight artwork is what they look like;
    // `cleric.grp` is the mage on foot, and both were drawing as spellcasters.
    "human/knight",  // 0x0c Paladin
    "orc/knight",  // 0x0d Ogre-Mage
    "human/dwarves",  // 0x0e Dwarven Demolition Squad
    "orc/goblins",  // 0x0f Goblin Sappers
    "human/peon",  // 0x10 Attack Peasant
    "orc/peon",  // 0x11 Attack Peon
    "human/spear",  // 0x12 Ranger
    "orc/spear",  // 0x13 Berserker
    "human/spear",  // 0x14 Alleria
    "orc/dknight",  // 0x15 Teron Gorefiend
    "human/griffon",  // 0x16 Kurdran and Sky'ree
    "orc/knight",  // 0x17 Dentarg
    "human/wizard",  // 0x18 Khadgar
    "orc/grunt",  // 0x19 Grom Hellscream
    "human/tanker",  // 0x1a Human Oil Tanker
    "orc/tanker",  // 0x1b Orc Oil Tanker
    "human/transp",  // 0x1c Human Transport
    "orc/transp",  // 0x1d Orc Transport
    "human/destroy",  // 0x1e Elven Destroyer
    "orc/destroy",  // 0x1f Troll Destroyer
    "human/battlshp",  // 0x20 Battleship
    "orc/battlshp",  // 0x21 Juggernaught
    "",  // 0x22 Unused #34
    "orc/dragon",  // 0x23 Deathwing
    "",  // 0x24 Unused #36
    "",  // 0x25 Unused #37
    "human/sub",  // 0x26 Gnomish Submarine
    "orc/sub",  // 0x27 Giant Turtle
    "human/orn",  // 0x28 Gnomish Flying Machine
    "orc/zep",  // 0x29 Goblin Zeppelin
    "human/griffon",  // 0x2a Gryphon Rider
    "orc/dragon",  // 0x2b Dragon
    "human/knight",  // 0x2c Turalyon
    "orc/eyeofkil",  // 0x2d Eye of Kilrogg
    "human/grunt",  // 0x2e Danath
    "orc/grunt",  // 0x2f Korgath Bladefist
    "",  // 0x30 Unused #48
    "orc/knight",  // 0x31 Cho'gall
    "human/knight",  // 0x32 Lothar
    "orc/dknight",  // 0x33 Gul'dan
    "human/knight",  // 0x34 Uther Lightbringer
    "orc/spear",  // 0x35 Zuljin
    "",  // 0x36 Unused #54
    "orc/skeleton",  // 0x37 Skeleton
    "monster/demon",  // 0x38 Daemon
    "monster/sheep",  // 0x39 Critter
    "human/farm",  // 0x3a Farm
    "orc/farm",  // 0x3b Pig Farm
    "human/barr",  // 0x3c Human Barracks
    "orc/barr",  // 0x3d Orc Barracks
    "human/church",  // 0x3e Church
    // The Altar of Storms takes `temple.grp` and the Temple of the Damned takes
    // `dtower.grp`, which reads backwards until you see how the game names its
    // pairs: the sprites pair church/temple and wtower/dtower, and the building
    // sounds pair hchant/ochant and wzrdtowr/dthtower, both putting the Mage
    // Tower's opposite number in a file named for a tower. So `dtower` is the
    // *damned* tower, not a second altar — and the artwork agrees, temple.grp
    // being a runic pentagram and dtower.grp a skull.
    "orc/temple",  // 0x3f Altar of Storms
    "human/tower",  // 0x40 Human Scout Tower
    "orc/tower",  // 0x41 Orc Scout Tower
    "human/stable",  // 0x42 Stables
    "orc/ogrecamp",  // 0x43 Ogre Mound
    "human/invent",  // 0x44 Gnomish Inventor
    "orc/invent",  // 0x45 Goblin Alchemist
    "human/aviary",  // 0x46 Gryphon Aviary
    "orc/roost",  // 0x47 Dragon Roost
    // The four coastal buildings ship two GRPs each, and they are construction
    // stages rather than variants: `found1` is bare slabs where `found2` has
    // machinery. So these eight point at the second file, where every other
    // entry in this table has only one to point at.
    "human/ship2",  // 0x48 Human Shipyard
    "orc/ship2",  // 0x49 Orc Shipyard
    "human/thall",  // 0x4a Town Hall
    "orc/thall",  // 0x4b Great Hall
    "human/lmill",  // 0x4c Elven Lumber Mill
    "orc/lmill",  // 0x4d Troll Lumber Mill
    "human/found2",  // 0x4e Human Foundry
    "orc/found2",  // 0x4f Orc Foundry
    "human/wtower",  // 0x50 Mage Tower
    "orc/dtower",  // 0x51 Temple of the Damned
    "human/black",  // 0x52 Human Blacksmith
    "orc/black",  // 0x53 Orc Blacksmith
    "human/ref2",  // 0x54 Human Refinery
    "orc/ref2",  // 0x55 Orc Refinery
    "human/oplat2",  // 0x56 Human Oil Well
    "orc/oplat2",  // 0x57 Orc Oil Well
    "human/keep",  // 0x58 Keep
    "orc/keep",  // 0x59 Stronghold
    "human/castle",  // 0x5a Castle
    "orc/blakrock",  // 0x5b Fortress
    "other/mine",  // 0x5c Gold Mine
    "other/patch",  // 0x5d Oil Patch
    "human/startloc",  // 0x5e Human Start Location
    "orc/startloc",  // 0x5f Orc Start Location
    "human/towera",  // 0x60 Human Guard Tower
    "orc/towera",  // 0x61 Orc Guard Tower
    "human/towerc",  // 0x62 Human Cannon Tower
    "orc/towerc",  // 0x63 Orc Cannon Tower
    "other/vcircle",  // 0x64 Circle of Power
    "other/gate",  // 0x65 Dark Portal
    "other/rock",  // 0x66 Runestone
    "other/fwalunit",  // 0x67 Human Wall
    "other/fwalunit",  // 0x68 Orc Wall
    "",  // 0x69 Corpse
    "",  // 0x6a 1x1 Rubble
    "",  // 0x6b 2x2 Rubble
    "",  // 0x6c 3x3 Rubble
    "",  // 0x6d 4x4 Rubble
};

const char* const kTilesetSpritePrefix[4] = {"", "s_", "l_", "x_"};

const char* const kCritterSprites[4] = {"monster/sheep", "monster/seal", "monster/boar", "monster/hellhog"};

// UDTA layout, from reference/docs/pud-format.md. Order is the file's order and must
// not be rearranged: every offset is derived by summing the widths before it.
const UdtaSegment kUdtaSegments[] = {
    {"useDefaultData", 2, 1, 1, false},
    {"overlapFrames", 2, 110, 1, true},
    {"obsoleteFrames", 2, 508, 1, false},
    {"sight", 4, 110, 1, true},
    {"hitPoints", 2, 110, 1, true},
    {"hasMagic", 1, 110, 1, true},
    {"buildTime", 1, 110, 1, true},
    {"goldCost", 1, 110, 1, true},
    {"lumberCost", 1, 110, 1, true},
    {"oilCost", 1, 110, 1, true},
    {"unitSize", 2, 220, 2, true},
    {"boxSize", 2, 220, 2, true},
    {"attackRange", 1, 110, 1, true},
    {"reactRangeComputer", 1, 110, 1, true},
    {"reactRangeHuman", 1, 110, 1, true},
    {"armor", 1, 110, 1, true},
    {"selectableViaRectangle", 1, 110, 1, true},
    {"priority", 1, 110, 1, true},
    {"basicDamage", 1, 110, 1, true},
    {"piercingDamage", 1, 110, 1, true},
    {"weaponsUpgradable", 1, 110, 1, true},
    {"armorUpgradable", 1, 110, 1, true},
    {"missileWeapon", 1, 110, 1, true},
    {"unitType", 1, 110, 1, true},
    {"decayRate", 1, 110, 1, true},
    {"annoyComputerFactor", 1, 110, 1, true},
    {"secondMouseButton", 1, 58, 1, true},
    {"pointValue", 2, 110, 1, true},
    {"canTarget", 1, 110, 1, true},
    {"flags", 4, 110, 1, true},
    {"swampFrames", 2, 127, 1, false},
};

// `AIPL` scripts. The retail maps use 0-16, 23, 25, 26 and 28; the rest are
// the campaign and expansion scripts, which the editor still has to name so a
// map that carries one does not show a bare number.
const char* const kAiNames[] = {
    "Land attack", "Passive", "Orc 3",
    "Human 4", "Orc 4", "Human 5",
    "Orc 5", "Human 6", "Orc 6",
    "Human 7", "Orc 7", "Human 8",
    "Orc 8", "Human 9", "Orc 9",
    "Human 10", "Orc 10", "Human 11",
    "Orc 11", "Human 12", "Orc 12",
    "Human 13", "Orc 13", "Human 14 (Orange)",
    "Orc 14 (Blue)", "Sea attack", "Air attack",
    "Human 14 (Red)", "Human 14 (White)", "Human 14 (Black)",
    "Orc 14 (Green)", "Orc 14 (White)", "Expansion 1",
    "Expansion 2", "Expansion 3", "Expansion 4",
    "Expansion 5", "Expansion 6", "Expansion 7",
    "Expansion 8", "Expansion 9", "Expansion 10",
    "Expansion 11", "Expansion 12", "Expansion 13",
    "Expansion 14", "Expansion 15", "Expansion 16",
    "Expansion 17", "Expansion 18", "Expansion 19",
    "Expansion 20", "Expansion 21", "Expansion 22",
    "Expansion 23", "Expansion 24", "Expansion 25",
    "Expansion 26", "Expansion 27", "Expansion 28",
    "Expansion 29", "Expansion 30", "Expansion 31",
    "Expansion 32", "Expansion 33", "Expansion 34",
    "Expansion 35", "Expansion 36", "Expansion 37",
    "Expansion 38", "Expansion 39", "Expansion 40",
    "Expansion 41", "Expansion 42", "Expansion 43",
    "Expansion 44", "Expansion 45", "Expansion 46",
    "Expansion 47", "Expansion 48", "Expansion 49",
    "Expansion 50", "Expansion 51",
};

const int kAiNameCount = int(sizeof(kAiNames) / sizeof(kAiNames[0]));

const char* ai_name(int value) {
  if (value < 0 || value >= kAiNameCount) return nullptr;
  return kAiNames[value];
}

const int kUdtaSegmentCount = int(sizeof(kUdtaSegments) / sizeof(kUdtaSegments[0]));

namespace {

// Labels for the fields whose numbers mean nothing on their own, from
// reference/docs/pud-format.md. Values absent here still round-trip: an unknown missile
// id keeps its number, and an unnamed flag bit is never touched.

const UdtaOption kMissileOptions[] = {
    {0x00, "Lightning"},     {0x01, "Griffon Hammer"},  {0x02, "Dragon Breath"},
    {0x03, "Flame Shield"},  {0x07, "Big Cannon"},      {0x0a, "Touch of Death"},
    {0x0d, "Catapult Rock"}, {0x0e, "Ballista Bolt"},   {0x0f, "Arrow"},
    {0x10, "Axe"},           {0x11, "Submarine Missile"}, {0x12, "Turtle Missile"},
    {0x18, "Small Cannon"},  {0x1b, "Demon Fire"},      {0x1d, "None"},
};

const UdtaOption kUnitTypeOptions[] = {{0, "Land"}, {1, "Fly"}, {2, "Naval"}};

const UdtaOption kMouseOptions[] = {
    {0, "None"},     {1, "Attack"},   {2, "Move"},
    {3, "Harvest"},  {4, "Haul oil"}, {5, "Demolish"}, {6, "Sail"},
};

const UdtaOption kTargetOptions[] = {{1, "Land"}, {2, "Sea"}, {4, "Air"}};

// The `flags` long. Bit 13 and bits 28-31 have no known meaning and are
// deliberately absent rather than guessed at.
const UdtaOption kFlagOptions[] = {
    {1u << 0, "Land unit"},          {1u << 1, "Air unit"},
    {1u << 2, "Explodes when killed"}, {1u << 3, "Sea unit"},
    {1u << 4, "Critter"},            {1u << 5, "Building"},
    {1u << 6, "Submarine"},          {1u << 7, "Sees submarines"},
    {1u << 8, "Peon"},               {1u << 9, "Tanker"},
    {1u << 10, "Transport"},         {1u << 11, "Gives oil"},
    {1u << 12, "Stores gold"},       {1u << 14, "Can attack ground"},
    {1u << 15, "Undead"},            {1u << 16, "Shore building"},
    {1u << 17, "Can cast spells"},   {1u << 18, "Stores lumber"},
    {1u << 19, "Can attack"},        {1u << 20, "Tower"},
    {1u << 21, "Oil patch"},         {1u << 22, "Gold mine"},
    {1u << 23, "Hero"},              {1u << 24, "Stores oil"},
    {1u << 25, "Killed by invisibility"}, {1u << 26, "Flees when attacked"},
    {1u << 27, "Organic"},
};

struct FieldKindEntry {
  const char* name;
  UdtaFieldKind kind;
  const UdtaOption* options;
  int count;
};

#define PF_OPTS(a) (a), int(sizeof(a) / sizeof((a)[0]))

const FieldKindEntry kFieldKinds[] = {
    {"useDefaultData", kUdtaBool, nullptr, 0},
    {"hasMagic", kUdtaBool, nullptr, 0},
    {"selectableViaRectangle", kUdtaBool, nullptr, 0},
    {"weaponsUpgradable", kUdtaBool, nullptr, 0},
    {"armorUpgradable", kUdtaBool, nullptr, 0},
    {"missileWeapon", kUdtaEnum, PF_OPTS(kMissileOptions)},
    {"unitType", kUdtaEnum, PF_OPTS(kUnitTypeOptions)},
    {"secondMouseButton", kUdtaEnum, PF_OPTS(kMouseOptions)},
    {"canTarget", kUdtaFlags, PF_OPTS(kTargetOptions)},
    {"flags", kUdtaFlags, PF_OPTS(kFlagOptions)},
};

#undef PF_OPTS

const FieldKindEntry* kind_entry(int field) {
  if (field < 0 || field >= kUdtaSegmentCount) return nullptr;
  const std::string name = kUdtaSegments[field].name;
  for (const FieldKindEntry& e : kFieldKinds) {
    if (name == e.name) return &e;
  }
  return nullptr;
}

}  // namespace

UdtaFieldKind udta_field_kind(int field) {
  const FieldKindEntry* e = kind_entry(field);
  return e ? e->kind : kUdtaNumber;
}

const UdtaOption* udta_field_options(int field, int* count) {
  const FieldKindEntry* e = kind_entry(field);
  if (count) *count = e ? e->count : 0;
  return e ? e->options : nullptr;
}

// UGRD layout, from reference/docs/pud-format.md. Sums to 782, the only valid length.
const UdtaSegment kUgrdSegments[] = {
    {"useDefaultData", 2, 1, 1, false},
    {"upgradeTime", 1, 52, 1, true},
    {"goldCost", 2, 52, 1, true},
    {"lumberCost", 2, 52, 1, true},
    {"oilCost", 2, 52, 1, true},
    {"icon", 2, 52, 1, true},
    {"group", 2, 52, 1, true},
    {"flags", 4, 52, 1, true},
};

const int kUgrdSegmentCount = int(sizeof(kUgrdSegments) / sizeof(kUgrdSegments[0]));

// `ALOW` bit meanings, from the format notes.
//
// The evidence is thin but it exists: of the three corpus maps carrying an
// `ALOW` section, the first expansion human mission allows exactly one unit bit
// — bit 8, which this table calls Transport, in a mission where the player
// cannot build and crosses water. Two internal checks agree: the spell bits are
// exactly the eighteen spell upgrades of `UGRD` in its order, and every unit
// bit names a unit that exists. All of it is asserted in the test suite.
//
// Nulls are bits with no recovered meaning, and an editor must leave them alone
// rather than guess.
const char* const kAlowUnitBits[32] = {
    "Footman / Grunt", "Peasant / Peon", "Ballista / Catapult",
    "Knight / Ogre", "Archer / Axethrower", "Mage / Death Knight",
    "Oil Tanker", "Destroyer", "Transport", "Battleship / Juggernaught",
    "Submarine / Turtle", "Flying Machine / Zeppelin", "Gryphon / Dragon",
    nullptr, "Demolition Squad / Sappers", "Aviary / Roost",
    "Farm", "Barracks", "Lumber Mill", "Stables / Ogre Mound",
    "Mage Tower / Temple", "Foundry", "Refinery", "Inventor / Alchemist",
    "Church / Altar of Storms", "Tower", "Town Hall / Great Hall",
    "Keep / Stronghold", "Castle / Fortress", "Blacksmith", "Shipyard",
    nullptr,
};

const char* const kAlowSpellBits[32] = {
    "Holy Vision", "Healing", nullptr, "Exorcism", "Flame Shield", "Fireball",
    "Slow", "Invisibility", "Polymorph", "Blizzard", "Eye of Kilrogg",
    "Bloodlust", nullptr, "Raise Dead", "Death Coil", "Whirlwind", "Haste",
    "Unholy Armor", "Runes", "Death and Decay",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

// A field name as a label: `buildTime` becomes "Build Time".
//
// Mechanical, so a field added to the tables gets a label without anyone
// remembering to write one, and so the label cannot drift from the name it
// came from. The exceptions below are the handful the rule reads badly on —
// acronyms it would split, and two names whose expansion is not obvious from
// the identifier.
namespace {

struct LabelOverride { const char* name; const char* label; };
const LabelOverride kFieldLabels[] = {
    // "AI" is an acronym, and the rule would leave it as one lowercase word.
    {"reactRangeComputer", "React Range (Computer)"},
    {"reactRangeHuman", "React Range (Human)"},
    // Frames of the *overlap* animation — an editor showing "Overlap Frames"
    // says nothing; this at least says what the number counts.
    {"overlapFrames", "Overlap Frame"},
    // The game calls these by name in its own manual, so the manual wins.
    {"hasMagic", "Uses Magic"},
    {"selectableViaRectangle", "Band-Selectable"},
    {"weaponsUpgradable", "Weapons Upgradable"},
    {"armorUpgradable", "Armour Upgradable"},
    {"canAttack", "Can Attack"},
    {"annoy", "Annoyance"},
    {"pierceDamage", "Piercing Damage"},
    {"useDefaultData", "Use the Game's Own Table"},
};

}  // namespace

int field_label(const char* name, char* out, int cap) {
  if (!name) return 0;
  for (const LabelOverride& over : kFieldLabels) {
    const char* a = over.name;
    const char* b = name;
    while (*a && *a == *b) { a++; b++; }
    if (*a || *b) continue;
    int length = 0;
    while (over.label[length]) length++;
    for (int i = 0; i < length && i < cap - 1; i++) out[i] = over.label[i];
    if (cap > 0) out[length < cap - 1 ? length : cap - 1] = '\0';
    return length;
  }

  // Split where a lower-case or digit meets an upper-case one, capitalise the
  // first letter of every word, and leave the rest as it was — the names are
  // camelCase identifiers, so that is the whole of the rule.
  int length = 0;
  for (int i = 0; name[i]; i++) {
    const char c = name[i];
    const bool upper = c >= 'A' && c <= 'Z';
    const char previous = i > 0 ? name[i - 1] : '\0';
    const bool boundary =
        upper && ((previous >= 'a' && previous <= 'z') ||
                  (previous >= '0' && previous <= '9'));
    if (boundary) {
      if (length < cap - 1) out[length] = ' ';
      length++;
    }
    char letter = c;
    if (i == 0 && letter >= 'a' && letter <= 'z') letter = char(letter - 'a' + 'A');
    if (length < cap - 1) out[length] = letter;
    length++;
  }
  if (cap > 0) out[length < cap - 1 ? length : cap - 1] = '\0';
  return length;
}

const char* alow_bit_name(int block, int bit) {
  if (bit < 0 || bit >= 32) return nullptr;
  // Block 0 is units, 1-3 are spells, 4-5 are upgrades. The upgrade table was
  // read out of UGRD's flags field rather than transcribed — see
  // overrides/alow_upgrade_bits.cpp for the method and the check on it.
  if (block == 0) return kAlowUnitBits[bit];
  if (block >= 1 && block <= 3) return kAlowSpellBits[bit];
  if (block == 4 || block == 5) return alow_upgrade_bit_name(bit);
  return nullptr;
}

const char* const kAlowBlockNames[kAlowBlocks] = {
    "unitsAllowed",     "spellsStartWith",  "spellsAllowed",
    "spellsResearching", "upgradesAllowed", "upgradesResearching",
};

int segment_offset(const UdtaSegment* table, int count, int segment) {
  if (!table || segment < 0 || segment >= count) return -1;
  int offset = 0;
  for (int i = 0; i < segment; i++) {
    offset += int(table[i].width) * int(table[i].elements);
  }
  return offset;
}

int udta_field_index(const char* name) {
  for (int i = 0; i < kUdtaSegmentCount; i++) {
    const char* n = kUdtaSegments[i].name;
    int j = 0;
    while (n[j] && name[j] && n[j] == name[j]) j++;
    if (!n[j] && !name[j]) return i;
  }
  return -1;
}

namespace {

uint32_t read_default(int field, int unit, int component) {
  const int index = field;
  if (index < 0) return 0;
  const UdtaSegment& seg = kUdtaSegments[index];
  const int at = segment_offset(kUdtaSegments, kUdtaSegmentCount, index) +
                 (unit * seg.components + component) * seg.width;
  if (at < 0 || at + seg.width > kDefaultUdtaSize) return 0;
  uint32_t v = 0;
  for (int i = 0; i < seg.width; i++) v |= uint32_t(kDefaultUdta[at + i]) << (8 * i);
  return v;
}

}  // namespace

UnitDomain default_unit_domain(int unit_id) {
  if (unit_id < 0 || unit_id >= kUnitCount) return kDomainAny;
  static const int flags_field = udta_field_index("flags");
  const uint32_t flags = read_default(flags_field, unit_id, 0);

  // Flag bits, from reference/docs/pud-format.md: 0 land, 1 air, 3 sea, 5 building,
  // 16 shore building, 21 oil patch, 22 mine.
  const bool is_air = (flags >> 1) & 1;
  const bool is_sea = (flags >> 3) & 1;
  const bool is_land = flags & 1;
  const bool oil_patch = (flags >> 21) & 1;
  const bool mine = (flags >> 22) & 1;
  const bool oil_source = (flags >> 11) & 1;   // oil wells and platforms

  const bool is_building = (flags >> 5) & 1;
  const bool is_shore = (flags >> 16) & 1;

  // Resources and the things built on them have fixed homes: gold is mined on
  // land, oil is drilled at sea.
  if (mine) return kDomainLand;
  if (oil_patch || oil_source) return kDomainWater;
  if (is_air) return kDomainAir;
  // Ships and the oil platforms that sit on water.
  if (is_sea && !is_land) return kDomainWater;
  // Shipyards, foundries and refineries are built against the waterline, so
  // their footprint legitimately covers both. Shipped maps place 65 of them
  // that a land-only rule rejects, which is how this exception was found.
  if (is_shore) return kDomainAny;
  // Buildings do not set the "land unit" bit, so they need their own rule —
  // without it a farm could be placed in the ocean.
  if (is_land || is_building) return kDomainLand;
  return kDomainAny;
}

void default_unit_footprint(int unit_id, int& w, int& h) {
  w = h = 1;
  if (unit_id < 0 || unit_id >= kUnitCount) return;
  static const int size_field = udta_field_index("unitSize");
  const uint32_t sx = read_default(size_field, unit_id, 0);
  const uint32_t sy = read_default(size_field, unit_id, 1);
  if (sx) w = int(sx);
  if (sy) h = int(sy);
  unit_footprint_override(unit_id, w, h);
}

int udta_offset(int segment) {
  return segment_offset(kUdtaSegments, kUdtaSegmentCount, segment);
}

}  // namespace pf
