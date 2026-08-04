// Units an editor should not offer until asked.
//
// The unit table has entries that round-trip perfectly but that no ordinary map
// has a reason to place: five dead slots the game has no unit for (placing one
// crashes it), runtime leftovers such as corpses, rubble and the wall-as-unit
// ids, and the two campaign workers.
//
// The membership is PUDDraft's "Unused/Special Units" submenu — see
// reference/dfm/TMAPEDFORM.dfm — so an editor built on this core hides the same
// set the tool these maps were made with did. The two campaign workers are the
// one departure: a palette offering two peasants that differ only in a name is
// a trap.
//
// Opt-in rather than forbidden, because people really do build maps with these.
// This governs what a palette offers, never what a file may hold.

#include "../constants.hpp"

namespace pf {
namespace {

// Unit ids, named rather than written as numbers at the call site.
constexpr uint8_t kAttackPeasant = 0x10, kAttackPeon = 0x11;
constexpr uint8_t kUnused34 = 0x22, kUnused36 = 0x24, kUnused37 = 0x25;
constexpr uint8_t kUnused48 = 0x30, kUnused54 = 0x36;
constexpr uint8_t kHumanWallUnit = 0x67, kOrcWallUnit = 0x68, kCorpse = 0x69;
constexpr uint8_t kRubble1x1 = 0x6a, kRubble2x2 = 0x6b, kRubble3x3 = 0x6c;
constexpr uint8_t kRubble4x4 = 0x6d;

/**
 * Units an editor should never offer, whatever the opt-in says.
 *
 * The wall-as-unit ids only: walls are terrain here, so a second way to place
 * one that does not auto-tile produces walls the wall tool cannot fix. A map
 * that already holds one still loads, edits and saves unchanged.
 */
constexpr uint8_t kNeverOffered[] = {0x67, 0x68};

/// Why a unit has to be asked for. Recorded so a future entry must say why.
enum OptInReason : uint8_t {
  kSlotIsDead,       ///< the game has no unit here; placing one crashes it
  kRuntimeLeftover,  ///< only exists once the game has been running
  kCampaignVariant,  ///< a scripted stand-in for an ordinary unit
};

struct OptInUnit {
  uint8_t unit;
  OptInReason reason;
};

const OptInUnit kOptInUnits[] = {
    {kAttackPeasant, kCampaignVariant}, {kAttackPeon, kCampaignVariant},
    {kUnused34, kSlotIsDead},           {kUnused36, kSlotIsDead},
    {kUnused37, kSlotIsDead},           {kUnused48, kSlotIsDead},
    {kUnused54, kSlotIsDead},
    {kHumanWallUnit, kRuntimeLeftover}, {kOrcWallUnit, kRuntimeLeftover},
    {kCorpse, kRuntimeLeftover},        {kRubble1x1, kRuntimeLeftover},
    {kRubble2x2, kRuntimeLeftover},     {kRubble3x3, kRuntimeLeftover},
    {kRubble4x4, kRuntimeLeftover},
};

}  // namespace

bool unit_never_offered(int unit_id) {
  for (uint8_t id : kNeverOffered) {
    if (id == unit_id) return true;
  }
  return false;
}

bool unit_needs_opt_in(int unit_id) {
  for (const OptInUnit& entry : kOptInUnits) {
    if (entry.unit == unit_id) return true;
  }
  return false;
}

int unit_opt_in_count() { return int(sizeof(kOptInUnits) / sizeof(kOptInUnits[0])); }

}  // namespace pf
