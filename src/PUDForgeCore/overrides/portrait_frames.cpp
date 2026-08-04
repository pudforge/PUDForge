// Which portrait belongs to which unit.
//
// Warcraft II keeps 196 command-button icons in `art/unit/portrait/*.grp`, one
// file per tileset, but the mapping from a unit to its frame lives in the game
// executable. This file is that mapping, written by hand, and each entry
// records where it came from: `kIconProven` for the four an upgrade's `UGRD`
// icon index pins, `kIconRead` for the rest, identified by eye.
//
// Reading them is reliable because the sheet is ordered — through frame 31 it
// alternates human on the even frame, orc on the odd — and the four proven
// entries fall exactly where that rule predicts. Past the land and sea units
// the alternation loosens and the entries rest on the artwork alone.
//
// A unit with no entry has no icon and callers fall back to its sprite. The
// gaps in the frame numbers are spells, upgrades and command buttons.

#include "../constants.hpp"

namespace pf {
namespace {

/// Where an entry's authority comes from. Kept in the data so a wrong icon can
/// be told apart from a wrong guess.
enum IconSource : uint8_t {
  kIconProven,  ///< read out of `UGRD`
  kIconRead,    ///< identified from the artwork
};

struct UnitIcon {
  uint8_t unit;    ///< index into kUnits
  uint8_t frame;   ///< frame in the portrait GRP
  IconSource source;
};

// Unit ids, named rather than written as numbers at the call site.
constexpr uint8_t kFootman = 0x00, kGrunt = 0x01, kPeasant = 0x02, kPeon = 0x03;
constexpr uint8_t kBallista = 0x04, kCatapult = 0x05, kKnight = 0x06, kOgre = 0x07;
constexpr uint8_t kArcher = 0x08, kAxethrower = 0x09, kMage = 0x0a, kDeathKnight = 0x0b;
constexpr uint8_t kPaladin = 0x0c, kOgreMage = 0x0d, kDwarvenDemolitionSquad = 0x0e;
constexpr uint8_t kGoblinSappers = 0x0f, kRanger = 0x12, kBerserker = 0x13;
constexpr uint8_t kAlleria = 0x14, kTeronGorefiend = 0x15, kKurdranAndSkyree = 0x16;
constexpr uint8_t kDentarg = 0x17, kKhadgar = 0x18, kGromHellscream = 0x19;
constexpr uint8_t kHumanOilTanker = 0x1a, kOrcOilTanker = 0x1b, kHumanTransport = 0x1c;
constexpr uint8_t kOrcTransport = 0x1d, kElvenDestroyer = 0x1e, kTrollDestroyer = 0x1f;
constexpr uint8_t kBattleship = 0x20, kJuggernaught = 0x21, kDeathwing = 0x23;
constexpr uint8_t kGnomishSubmarine = 0x26, kGiantTurtle = 0x27;
constexpr uint8_t kGnomishFlyingMachine = 0x28, kGoblinZeppelin = 0x29;
constexpr uint8_t kGryphonRider = 0x2a, kDragon = 0x2b, kTuralyon = 0x2c;
constexpr uint8_t kEyeOfKilrogg = 0x2d, kDanath = 0x2e, kKorgathBladefist = 0x2f;
constexpr uint8_t kChogall = 0x31, kLothar = 0x32, kGuldan = 0x33;
constexpr uint8_t kUtherLightbringer = 0x34, kZuljin = 0x35, kSkeleton = 0x37;
constexpr uint8_t kDaemon = 0x38, kCritter = 0x39, kFarm = 0x3a, kPigFarm = 0x3b;
constexpr uint8_t kHumanBarracks = 0x3c, kOrcBarracks = 0x3d, kChurch = 0x3e;
constexpr uint8_t kAltarOfStorms = 0x3f, kHumanScoutTower = 0x40, kOrcScoutTower = 0x41;
constexpr uint8_t kStables = 0x42, kOgreMound = 0x43, kGnomishInventor = 0x44;
constexpr uint8_t kGoblinAlchemist = 0x45, kGryphonAviary = 0x46, kDragonRoost = 0x47;
constexpr uint8_t kHumanShipyard = 0x48, kOrcShipyard = 0x49, kTownHall = 0x4a;
constexpr uint8_t kGreatHall = 0x4b, kElvenLumberMill = 0x4c, kTrollLumberMill = 0x4d;
constexpr uint8_t kHumanFoundry = 0x4e, kOrcFoundry = 0x4f, kMageTower = 0x50;
constexpr uint8_t kTempleOfTheDamned = 0x51, kHumanBlacksmith = 0x52;
constexpr uint8_t kOrcBlacksmith = 0x53, kHumanRefinery = 0x54, kOrcRefinery = 0x55;
constexpr uint8_t kHumanOilWell = 0x56, kOrcOilWell = 0x57, kKeep = 0x58;
constexpr uint8_t kStronghold = 0x59, kCastle = 0x5a, kFortress = 0x5b;
constexpr uint8_t kGoldMine = 0x5c, kOilPatch = 0x5d, kHumanGuardTower = 0x60;
constexpr uint8_t kOrcGuardTower = 0x61, kHumanCannonTower = 0x62;
constexpr uint8_t kOrcCannonTower = 0x63, kCircleOfPower = 0x64, kDarkPortal = 0x65;
constexpr uint8_t kRunestone = 0x66, kHumanWall = 0x67, kOrcWall = 0x68;

const UnitIcon kUnitIcons[] = {
    // Land units, in the order the game lists them: peasant, footman, archer,
    // ranger, knight, paladin, demolition, mage, siege.
    {kPeasant, 0, kIconRead},                 {kPeon, 1, kIconRead},
    {kFootman, 2, kIconRead},                 {kGrunt, 3, kIconRead},
    {kArcher, 4, kIconRead},                  {kAxethrower, 5, kIconRead},
    {kRanger, 6, kIconProven},                {kBerserker, 7, kIconProven},
    {kKnight, 8, kIconRead},                  {kOgre, 9, kIconRead},
    {kPaladin, 10, kIconProven},              {kOgreMage, 11, kIconProven},
    {kDwarvenDemolitionSquad, 12, kIconRead}, {kGoblinSappers, 13, kIconRead},
    {kMage, 14, kIconRead},                   {kDeathKnight, 15, kIconRead},
    {kBallista, 16, kIconRead},               {kCatapult, 17, kIconRead},

    // Sea and air, same pairing: tanker, transport, destroyer, capital ship,
    // submersible, flyer, dragon.
    {kHumanOilTanker, 18, kIconRead},       {kOrcOilTanker, 19, kIconRead},
    {kHumanTransport, 20, kIconRead},       {kOrcTransport, 21, kIconRead},
    {kElvenDestroyer, 22, kIconRead},       {kTrollDestroyer, 23, kIconRead},
    {kBattleship, 24, kIconRead},           {kJuggernaught, 25, kIconRead},
    {kGnomishSubmarine, 26, kIconRead},     {kGiantTurtle, 27, kIconRead},
    {kGnomishFlyingMachine, 28, kIconRead}, {kGoblinZeppelin, 29, kIconRead},
    {kGryphonRider, 30, kIconRead},         {kDragon, 31, kIconRead},

    // Heroes and the summoned daemon, where the alternation stops: Cho'gall sits
    // on an even frame despite being orc.
    {kLothar, 32, kIconRead},            {kGuldan, 33, kIconRead},
    {kUtherLightbringer, 34, kIconRead}, {kZuljin, 35, kIconRead},
    {kChogall, 36, kIconRead},           {kDaemon, 37, kIconRead},

    // Buildings. The pairing resumes but is no longer strictly by race parity,
    // and frames 70, 71, 83 to 91 carry artwork no unit uses.
    //
    // Frames 63 and 65 do not follow their human neighbours: the artwork has
    // the altar on 65 and the temple on 63, the opposite of how they pair in
    // the game.
    {kFarm, 38, kIconRead},              {kPigFarm, 39, kIconRead},
    {kTownHall, 40, kIconRead},          {kGreatHall, 41, kIconRead},
    {kHumanBarracks, 42, kIconRead},     {kOrcBarracks, 43, kIconRead},
    {kElvenLumberMill, 44, kIconRead},   {kTrollLumberMill, 45, kIconRead},
    {kHumanBlacksmith, 46, kIconRead},   {kOrcBlacksmith, 47, kIconRead},
    {kHumanShipyard, 48, kIconRead},     {kOrcShipyard, 49, kIconRead},
    {kHumanRefinery, 50, kIconRead},     {kOrcRefinery, 51, kIconRead},
    {kHumanFoundry, 52, kIconRead},      {kOrcFoundry, 53, kIconRead},
    {kHumanOilWell, 54, kIconRead},      {kOrcOilWell, 55, kIconRead},
    {kStables, 56, kIconRead},           {kOgreMound, 57, kIconRead},
    {kGnomishInventor, 58, kIconRead},   {kGoblinAlchemist, 59, kIconRead},
    {kHumanScoutTower, 60, kIconRead},   {kOrcScoutTower, 61, kIconRead},
    // Counterparts, like every other pair on this stretch (race_counterparts
    // .cpp). These two were crossed, which put the pentagram icon on the Temple
    // and the skull on the Altar — the same swap the sprite table had.
    {kChurch, 62, kIconRead},            {kAltarOfStorms, 63, kIconRead},
    {kMageTower, 64, kIconRead},         {kTempleOfTheDamned, 65, kIconRead},
    {kKeep, 66, kIconRead},              {kStronghold, 67, kIconRead},
    {kCastle, 68, kIconRead},            {kFortress, 69, kIconRead},
    {kGryphonAviary, 72, kIconRead},     {kDragonRoost, 73, kIconRead},
    {kGoldMine, 74, kIconRead},          {kHumanGuardTower, 75, kIconRead},
    {kHumanCannonTower, 76, kIconRead},  {kOrcGuardTower, 77, kIconRead},
    {kOrcCannonTower, 78, kIconRead},    {kOilPatch, 79, kIconRead},
    {kDarkPortal, 80, kIconRead},        {kCircleOfPower, 81, kIconRead},
    {kRunestone, 82, kIconRead},         {kHumanWall, 92, kIconRead},
    {kOrcWall, 93, kIconRead},

    // Summoned and neutral creatures.
    {kEyeOfKilrogg, 111, kIconRead}, {kSkeleton, 114, kIconRead},
    {kCritter, 115, kIconRead},

    // The expansion's heroes, at the very end of the sheet.
    {kKorgathBladefist, 186, kIconRead}, {kAlleria, 187, kIconRead},
    {kDanath, 188, kIconRead},           {kTeronGorefiend, 189, kIconRead},
    {kGromHellscream, 190, kIconRead},   {kKurdranAndSkyree, 191, kIconRead},
    {kDeathwing, 192, kIconRead},        {kKhadgar, 193, kIconRead},
    {kDentarg, 194, kIconRead},          {kTuralyon, 195, kIconRead},
};

}  // namespace

int unit_icon(int unit_id) {
  for (const UnitIcon& entry : kUnitIcons) {
    if (entry.unit == unit_id) return entry.frame;
  }
  return -1;
}

int unit_icon_count() { return int(sizeof(kUnitIcons) / sizeof(kUnitIcons[0])); }

}  // namespace pf
