// What a unit becomes when a base changes race.
//
// Every human unit and building has one orc opposite number, and the pairing is
// in the game's data nowhere, so it is written here.
//
// Heroes are deliberately absent: they are named characters with no opposite
// number, and the ids merely happen to alternate, so a rule derived from the
// numbering would turn Turalyon into the Eye of Kilrogg. The shipped maps agree
// — of 33,838 units under a human or orc player, the 71 sitting under the other
// race are every one of them a named hero.

#include "../constants.hpp"

namespace pf {
namespace {

// Unit ids, named rather than written as numbers at the call site.
constexpr uint8_t kFootman = 0x00, kGrunt = 0x01, kPeasant = 0x02, kPeon = 0x03;
constexpr uint8_t kBallista = 0x04, kCatapult = 0x05, kKnight = 0x06, kOgre = 0x07;
constexpr uint8_t kArcher = 0x08, kAxethrower = 0x09, kMage = 0x0a, kDeathKnight = 0x0b;
constexpr uint8_t kPaladin = 0x0c, kOgreMage = 0x0d, kDwarvenDemolitionSquad = 0x0e;
constexpr uint8_t kGoblinSappers = 0x0f, kAttackPeasant = 0x10, kAttackPeon = 0x11;
constexpr uint8_t kRanger = 0x12, kBerserker = 0x13, kHumanOilTanker = 0x1a;
constexpr uint8_t kOrcOilTanker = 0x1b, kHumanTransport = 0x1c, kOrcTransport = 0x1d;
constexpr uint8_t kElvenDestroyer = 0x1e, kTrollDestroyer = 0x1f, kBattleship = 0x20;
constexpr uint8_t kJuggernaught = 0x21, kGnomishSubmarine = 0x26, kGiantTurtle = 0x27;
constexpr uint8_t kGnomishFlyingMachine = 0x28, kGoblinZeppelin = 0x29;
constexpr uint8_t kGryphonRider = 0x2a, kDragon = 0x2b, kFarm = 0x3a, kPigFarm = 0x3b;
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
constexpr uint8_t kHumanStartLocation = 0x5e, kOrcStartLocation = 0x5f;
constexpr uint8_t kHumanGuardTower = 0x60, kOrcGuardTower = 0x61;
constexpr uint8_t kHumanCannonTower = 0x62, kOrcCannonTower = 0x63, kHumanWall = 0x67;
constexpr uint8_t kOrcWall = 0x68;

/// Human first, orc second. Read both ways round by `unit_counterpart`.
struct Counterpart {
  uint8_t human;
  uint8_t orc;
};

const Counterpart kCounterparts[] = {
    {kFootman, kGrunt},                        {kPeasant, kPeon},
    {kBallista, kCatapult},                    {kKnight, kOgre},
    {kArcher, kAxethrower},                    {kMage, kDeathKnight},
    {kPaladin, kOgreMage},                     {kDwarvenDemolitionSquad, kGoblinSappers},
    {kAttackPeasant, kAttackPeon},             {kRanger, kBerserker},
    {kHumanOilTanker, kOrcOilTanker},          {kHumanTransport, kOrcTransport},
    {kElvenDestroyer, kTrollDestroyer},        {kBattleship, kJuggernaught},
    {kGnomishSubmarine, kGiantTurtle},         {kGnomishFlyingMachine, kGoblinZeppelin},
    {kGryphonRider, kDragon},                  {kFarm, kPigFarm},
    {kHumanBarracks, kOrcBarracks},            {kChurch, kAltarOfStorms},
    {kHumanScoutTower, kOrcScoutTower},        {kStables, kOgreMound},
    {kGnomishInventor, kGoblinAlchemist},      {kGryphonAviary, kDragonRoost},
    {kHumanShipyard, kOrcShipyard},            {kTownHall, kGreatHall},
    {kElvenLumberMill, kTrollLumberMill},      {kHumanFoundry, kOrcFoundry},
    {kMageTower, kTempleOfTheDamned},          {kHumanBlacksmith, kOrcBlacksmith},
    {kHumanRefinery, kOrcRefinery},            {kHumanOilWell, kOrcOilWell},
    {kKeep, kStronghold},                      {kCastle, kFortress},
    {kHumanStartLocation, kOrcStartLocation},  {kHumanGuardTower, kOrcGuardTower},
    {kHumanCannonTower, kOrcCannonTower},      {kHumanWall, kOrcWall},
};

}  // namespace

int unit_counterpart(int unit_id) {
  for (const Counterpart& pair : kCounterparts) {
    if (pair.human == unit_id) return pair.orc;
    if (pair.orc == unit_id) return pair.human;
  }
  return -1;
}

int unit_counterpart_count() {
  return int(sizeof(kCounterparts) / sizeof(kCounterparts[0]));
}

}  // namespace pf
