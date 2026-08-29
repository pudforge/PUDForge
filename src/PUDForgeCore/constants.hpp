// Unit, upgrade and player tables. The data lives in constants.cpp, which is
// originally derived from the JavaScript prototype; now hand-maintained source.

#pragma once

#include <cstdint>
#include <string>

#include "pud.hpp"

namespace pf {

struct UnitInfo {
  const char* name;
  /// 'h' human, 'o' orc, 'n' neutral.
  char race;
  /// True for the five dead slots (34, 36, 37, 48, 54) that crash the game
  /// unless the map also ships matching exe/maindat hacks.
  bool unused;
};

extern const UnitInfo kUnits[kUnitCount];
extern const char* const kUpgrades[kUpgradeCount];
extern const char* const kPlayerNames[kPlayerCount];
/// Packed 0x00RRGGBB display colours, matching the original's Player menu.
extern const uint32_t kPlayerColors[kPlayerCount];
/// The slot every map's mines, oil and critters belong to.
constexpr int kNeutralPlayer = kPlayerCount - 1;
/// Whether the game does anything with a slot. Eight playable and the neutral
/// one; the seven between are storage nothing reads — see
/// overrides/player_slots.cpp for the corpus counts.
bool player_slot_is_supported(int player);

extern const char* const kUnitSprites[kUnitCount];
extern const char* const kTilesetSpritePrefix[4];
extern const char* const kCritterSprites[4];

/// One field of `UDTA`, which is a struct-of-arrays: each field is a full
/// array over unit ids, laid out back to back. Editing works on offsets into
/// the section's raw bytes, so a load/save round-trip stays byte-exact even
/// for fields nothing understands.
struct UdtaSegment {
  const char* name;
  uint8_t width;       ///< bytes per element: 1, 2 or 4
  uint16_t elements;   ///< total elements in the array
  uint8_t components;  ///< elements per unit: 2 for unitSize/boxSize, else 1
  bool per_unit;       ///< false for useDefaultData, obsoleteFrames, swampFrames
};

/// Retail UDTA/UGRD payloads (default_data.cpp). A map may omit either
/// section, in which case the game falls back to these — so the editor must
/// too, or footprints and unit flags are simply wrong.
extern const uint8_t kDefaultUdta[];
extern const int kDefaultUdtaSize;
extern const uint8_t kDefaultUgrd[];
extern const int kDefaultUgrdSize;

/// Frame in the portrait GRP for a unit, or -1 when none is known.
/// The table and its provenance are in overrides/portrait_frames.cpp.
int unit_icon(int unit_id);
int unit_icon_count();

/// Whether an editor should keep this unit behind an opt-in. The list and the
/// reason for each entry is in overrides/hidden_units.cpp.
bool unit_needs_opt_in(int unit_id);

/// Whether a palette should never offer this unit. See overrides/hidden_units.cpp.
bool unit_never_offered(int unit_id);

/// Editing policy: zoom ladder, brush sizes, spray ramp, the brush list.
/// See overrides/editing_policy.cpp.
int zoom_level_count();
int zoom_level(int index);
int zoom_min();
int zoom_max();
int zoom_snap(int zoom);
int zoom_step(int zoom, int dir);
int brush_size_count();
int brush_size(int index);
int spray_tick_ms();
int spray_ramp_ms();
double spray_density(int held_ms, double full);
double scatter_density();

/// Score a name against a typed query; -1 when the query is not a subsequence.
/// See pf_name_score in the public header for the reasoning.
int name_score(const char *query, const char *name);
int brush_count();
int brush_terrain(int index);
int brush_shade(int index);

/// A terrain class as a flat 0xRRGGBB colour. See overrides/flat_colours.cpp.
uint32_t terrain_flat_colour(int terrain, int tileset);
/// A movement value as a flat 0xRRGGBB colour. See overrides/flat_colours.cpp.
uint32_t movement_colour(int value);

/// Whether a unit must keep clear of gold mines. See overrides/hall_clearance.cpp.
bool unit_needs_mine_clearance(int unit_id);
/// How many tiles of clearance it needs. See overrides/hall_clearance.cpp.
int mine_clearance_tiles();

/// How many variations of a solid tile group are plain drawings of it, or 0
/// when the group has no measured layout. See overrides/tile_variations.cpp.
int plain_variation_count(int group);

/// What a terrain class is called in a tileset. See overrides/terrain_names.cpp.
const char* terrain_name(int terrain, int tileset);
int unit_opt_in_count();

/// Named characters the game's own hero flag leaves out. See overrides/named_heroes.cpp.
bool unit_is_named_hero(int unit_id);
int unit_named_hero_count();

/// The other race's equivalent of a unit, or -1 when it has none. See
/// overrides/race_counterparts.cpp.
int unit_counterpart(int unit_id);
int unit_counterpart_count();

/// The eight `SQM ` values the game uses. See overrides/movement_classes.cpp.
int movement_class_count();
int movement_class_value(int index);
const char* movement_class_name(int index);
int movement_class_of(int value);

/// What one bit of ALOW's upgrade blocks restricts, or null when unused.
/// See overrides/alow_upgrade_bits.cpp.
const char* alow_upgrade_bit_name(int bit);

/// What kind of AI script a value is. See overrides/ai_scripts.cpp.
enum AiKind { kAiGeneral = 0, kAiCampaign = 1, kAiExpansion = 2 };
int ai_kind(int value);
int ai_same_as(int value);
const char* ai_mission(int value);


/// The owner a newly placed unit should get, or -1 for the chosen player.
/// See overrides/neutral_owners.cpp.
int unit_default_owner(int unit_id);

/// Name of an `AIPL` script value, or null when out of range.
const char* ai_name(int value);
extern const int kAiNameCount;

extern const UdtaSegment kUdtaSegments[];
extern const int kUdtaSegmentCount;

/// How a `UDTA` field should be read. Most are plain numbers, but several are
/// enumerations or bit masks whose numeric value means nothing on its own.
enum UdtaFieldKind {
  kUdtaNumber = 0,
  kUdtaBool = 1,   ///< 0 or 1
  kUdtaEnum = 2,   ///< one of `udta_field_options`
  kUdtaFlags = 3,  ///< any combination of them, each option a bit mask
};

/// One labelled value of an enum field, or one labelled bit of a flags field.
struct UdtaOption {
  uint32_t value;     ///< the value, or the bit's mask
  const char* label;
};

UdtaFieldKind udta_field_kind(int field);

/// Labelled values for an enum or flags field; null and `count` 0 otherwise.
///
/// Not every possible value is listed, so editors must preserve what they do
/// not recognise: set and clear individual bits rather than writing a mask
/// built only from the named ones.
const UdtaOption* udta_field_options(int field, int* count);

/// `UGRD` has the same struct-of-arrays shape, so it reuses UdtaSegment.
extern const UdtaSegment kUgrdSegments[];
extern const int kUgrdSegmentCount;
constexpr int kUgrdSize = 782;

/// `ALOW`: six blocks of 16 uint32s, one per player. Regular enough to need
/// no table.
constexpr int kAlowBlocks = 6;
constexpr int kAlowSize = kAlowBlocks * kPlayerCount * 4;
extern const char* const kAlowBlockNames[kAlowBlocks];
/// What each block holds when nothing is restricted, per player. Two of the six
/// are not "allowed" flags, so this is not all-ones — see overrides/alow_defaults.cpp.
extern const uint32_t kDefaultAlowBlock[kAlowBlocks];
/// A field name as a label a person reads: `buildTime` -> "Build Time".
int field_label(const char* name, char* out, int cap);

/// Meaning of one bit of an `ALOW` block, or null when it has none.
const char* alow_bit_name(int block, int bit);
extern const char* const kAlowUnitBits[32];
extern const char* const kAlowSpellBits[32];
/// The unit a block 0 bit restricts, or -1. The human half of the pair.
int alow_bit_unit(int block, int bit);
/// The upgrade a spell or upgrade block's bit restricts, or -1.
int alow_bit_upgrade(int block, int bit);
/// Payload lengths the field list sums to, without and with `swampFrames`.
constexpr int kUdtaSize = 5696;
constexpr int kUdtaSizeWithSwamp = 5950;

/// Byte offset of a segment within the payload, or -1 if out of range.
int udta_offset(int segment);
/// Byte offset within a struct-of-arrays section built from `table`.
int segment_offset(const UdtaSegment* table, int count, int segment);

/// Index of a UDTA field by name, or -1. Cheap enough to call at startup.
int udta_field_index(const char* name);

/// Where a unit may stand, from its UDTA `flags`.
enum UnitDomain {
  kDomainLand = 0,   ///< walks; needs passable ground
  kDomainWater = 1,  ///< floats; needs water
  kDomainAir = 2,    ///< flies; anywhere
  kDomainAny = 3,    ///< resources and markers; anywhere
};

/// Domain of a unit from the retail defaults. Independent of any map, so it
/// works before one is open and for maps with no UDTA of their own.
UnitDomain default_unit_domain(int unit_id);

/// Footprint from the retail defaults, for maps carrying no UDTA.
void default_unit_footprint(int unit_id, int& w, int& h);

/// The footprint the game uses where `unitSize` does not say it: the ships and
/// flying units, which the field calls 1x1 and the game lays out 2x2. See
/// overrides/unit_footprints.cpp for the evidence and for why this wins over a
/// map's own UDTA. False, leaving w and h alone, for every other unit.
bool unit_footprint_override(int unit_id, int& w, int& h);

/// The units that table names, for the tests that check it.
int oversize_unit_count();
int oversize_unit_id(int index);

/// Whether two units standing on the same tiles is an arrangement the game
/// intends rather than a fault. See overrides/shared_tiles.cpp; a gold mine is
/// deliberately not one of them.
bool units_may_share_tiles(int a, int b);

/// What a newly placed unit's `value` should start at. See
/// overrides/editing_policy.cpp; `resource` is a pf_resource.
int unit_default_value(int unit_id, int resource);

/// The two moments an editor has a sound for. The rest of the game's audio —
/// spells, weapons, a unit dying — is about a game being played, not about
/// building the map it is played on.
enum SoundKind {
  kSoundReady = 0,     ///< the unit has appeared
  kSoundSelected = 1,  ///< the unit is being pointed at
};

/// Where in the game's archives to find a unit's sound. The table and its
/// reasoning are in overrides/unit_sounds.cpp.
///
/// `salt` picks among the several selection lines a unit has; which one is the
/// caller's policy, and this stays a pure function of its arguments so it can
/// be tested against the archives.
std::string unit_sound_path(int unit_id, int kind, int salt);

}  // namespace pf
