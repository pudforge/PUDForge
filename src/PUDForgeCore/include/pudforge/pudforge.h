/*
 * PUDForge core — C ABI.
 *
 * The shared surface between the platform front-ends. Everything here is plain
 * C: opaque handles, POD structs, no exceptions crossing the boundary. Swift
 * imports this header directly; on Windows the same static library is callable
 * from C++/Win32 or from C# via P/Invoke.
 *
 * Conventions
 * -----------
 *  - Functions that can fail take a `pf_status*` out-parameter and return NULL
 *    or a negative value on failure. Passing NULL for `status` is allowed.
 *  - Returned pointers to arrays remain owned by the object they came from and
 *    stay valid until it is mutated or freed.
 *  - Strings are UTF-8 (in practice cp1252 from the file, transcoded on read).
 *  - All indices are 0-based. Tile coordinates are (x, y) from the top-left.
 */

#ifndef PUDFORGE_H
#define PUDFORGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(PUDFORGE_SHARED)
#  ifdef PUDFORGE_BUILDING
#    define PF_API __declspec(dllexport)
#  else
#    define PF_API __declspec(dllimport)
#  endif
#else
#  define PF_API
#endif

/* ---------------------------------------------------------------- status */

typedef enum pf_status {
  PF_OK = 0,
  PF_ERR_INVALID_ARG = 1,
  PF_ERR_NOT_A_PUD = 2,       /* missing or wrong TYPE section          */
  PF_ERR_MALFORMED = 3,       /* truncated or self-inconsistent         */
  PF_ERR_UNSUPPORTED_SIZE = 4,/* map dimensions outside 1..128          */
  PF_ERR_OUT_OF_RANGE = 5,
  PF_ERR_IO = 6,
  PF_ERR_OUT_OF_MEMORY = 7
} pf_status;

/** Human-readable text for a status code. Never NULL, never owned by caller. */
PF_API const char *pf_status_message(pf_status status);

/** Library version, for the front-ends to display. */
PF_API const char *pf_version(void);

/* ------------------------------------------------------------- constants */

/* Named so every language binding sees the same, predictable type. */
typedef enum pf_limit {
  PF_UNIT_COUNT = 110,
  PF_UPGRADE_COUNT = 52,
  PF_PLAYER_COUNT = 16,
  PF_MAX_UNITS = 600,
  PF_MAX_MAP_DIM = 128,
  PF_TILE_PX = 32
} pf_limit;

/** `ERA ` values. */
typedef enum pf_tileset {
  PF_TILESET_FOREST = 0,
  PF_TILESET_WINTER = 1,
  PF_TILESET_WASTELAND = 2,
  PF_TILESET_SWAMP = 3
} pf_tileset;

/** Terrain classes a tile quadrant can hold. */
typedef enum pf_terrain {
  PF_TERRAIN_WATER_DARK = 0,
  PF_TERRAIN_WATER_LIGHT = 1,
  PF_TERRAIN_COAST_DARK = 2,
  PF_TERRAIN_COAST_LIGHT = 3,
  PF_TERRAIN_GROUND_LIGHT = 4,
  PF_TERRAIN_GROUND_DARK = 5,
  PF_TERRAIN_FOREST = 6,
  PF_TERRAIN_MOUNTAIN = 7,
  PF_TERRAIN_WALL_HUMAN = 8,
  PF_TERRAIN_WALL_ORC = 9,
  PF_TERRAIN_UNKNOWN = 10
} pf_terrain;

/** `OWNR` values. */
typedef enum pf_owner {
  PF_OWNER_PASSIVE_COMPUTER = 2,
  PF_OWNER_NOBODY = 3,
  PF_OWNER_COMPUTER = 4,
  PF_OWNER_HUMAN = 5,
  PF_OWNER_RESCUE_PASSIVE = 6,
  PF_OWNER_RESCUE_ACTIVE = 7
} pf_owner;

/** `SIDE` values. */
typedef enum pf_race { PF_RACE_HUMAN = 0, PF_RACE_ORC = 1, PF_RACE_NEUTRAL = 2 } pf_race;

/** Name of a unit type, or NULL when `unit_id` is out of range. */
PF_API const char *pf_unit_name(int unit_id);
/** 'h', 'o' or 'n' for the unit's race. */
PF_API char pf_unit_race(int unit_id);
/** Non-zero when the slot is one of the five unused ones. */
PF_API int pf_unit_is_unused(int unit_id);
PF_API const char *pf_upgrade_name(int upgrade_id);
/**
 * Name of an `AIPL` script value, or NULL past the end.
 *
 * The value is an index into a fixed list of game scripts, not a quantity —
 * 0 is "Land attack" and 25 is "Sea attack" — so an editor should offer the
 * names rather than a number nobody can read.
 */
/**
 * What one bit of an `ALOW` block restricts, or NULL when nothing is known.
 *
 * Block 0 is units, blocks 1-3 are spells, blocks 4-5 are upgrades. Fifteen
 * upgrade bits and two unit bits are unused by the game and return NULL; treat
 * an unnamed bit as data to preserve, never as a bit to clear. Provenance is in
 * overrides/alow_upgrade_bits.cpp.
 */
PF_API const char *pf_alow_bit_name(int block, int bit);

/**
 * What a bit stands for, as something that has artwork.
 *
 * A name is enough to read a bit; an editor that wants to *show* one needs
 * the thing itself, because the icon lives on the unit or on the upgrade.
 * Block 0 answers with a unit id — the human half of the pair, since a bit
 * covers both races and one icon has to be picked; blocks 1 to 5 answer with
 * an upgrade id, found by reading the default `UGRD` table's flags field
 * rather than transcribing a second table beside the names.
 *
 * The wrong block for the question, or a bit the game does not use, is -1.
 */
PF_API int pf_alow_bit_unit(int block, int bit);
PF_API int pf_alow_bit_upgrade(int block, int bit);

PF_API const char *pf_ai_name(int value);
PF_API int pf_ai_name_count(void);

/** Which sort of AI script a value is: 0 general, 1 campaign, 2 expansion. */
PF_API int pf_ai_kind(int value);


/**
 * Another AI value whose script bytes are identical to this one's, or -1.
 *
 * Read out of `rez/ai.bin`, the game's own script table, so it says the two are
 * the same script — not that they look similar. "Orc 3" is Passive.
 */
PF_API int pf_ai_same_as(int value);

/**
 * The campaign mission a script was written for, or NULL.
 *
 * NULL where the name already says it ("Human 4") and where the script is one of
 * the four general ones. This is what makes "Expansion 17" legible.
 */
PF_API const char *pf_ai_mission(int value);


/**
 * What kind of thing a unit is, for grouping a palette and for selection.
 *
 * Resources come before buildings deliberately: a gold mine carries the
 * building flag but is scenery, and "select all buildings" to move a base
 * should not sweep it up.
 */
typedef enum pf_category {
  PF_CATEGORY_LAND = 0,
  PF_CATEGORY_AIR = 1,
  PF_CATEGORY_WATER = 2,
  PF_CATEGORY_BUILDING = 3,
  PF_CATEGORY_HERO = 4,
  PF_CATEGORY_SPECIAL = 5
} pf_category;

PF_API int pf_unit_category(int unit_id);

/**
 * Which of three things a unit is *drawn* as, for a view filter.
 *
 * Deliberately not the same answer as the category: a gold mine is drawn as a
 * building, because on the map that is what it looks like, but is selected as
 * a resource. Returns 0 ground, 1 air, 2 building.
 */
PF_API int pf_unit_draw_class(int unit_id);

/**
 * How many frames of a sprite are facings for this unit.
 *
 * Only things that walk, swim or fly turn round. A building's second frame is
 * its half-built state, not a second direction, so varying a farm's frame puts
 * scaffolding on a finished building - which is what this exists to prevent.
 * A renderer takes the smaller of this and the sprite's own frame count.
 */
PF_API int pf_unit_facing_count(int unit_id);

/** Named groups a selection can ask for. */
typedef enum pf_unit_group {
  PF_GROUP_BUILDINGS = 0,
  PF_GROUP_LAND,          /**< walks, not a building, not a critter */
  PF_GROUP_AIR,
  PF_GROUP_WATER,
  PF_GROUP_CRITTERS,
  PF_GROUP_HEROES,
  PF_GROUP_RESOURCES,     /**< gold mines and oil patches */
  PF_GROUP_START_LOCATIONS,
  PF_GROUP_SPELLCASTERS,
  PF_GROUP_TOWERS,
  PF_GROUP_COUNT
} pf_unit_group;

/** Non-zero when a unit belongs to a named group. */
PF_API int pf_unit_in_group(int unit_id, int group);

/**
 * Non-zero when a placed unit's `value` holds a resource amount rather than
 * the passive/active flag.
 *
 * Gold mines and oil patches are the obvious ones. Oil *wells* are the catch:
 * they are buildings, not scenery, so they are not resources for the purpose
 * of selecting them — but the shipped maps store an oil amount in their value
 * all the same, 143 of them carrying 2 to 8 where a flag would only ever be
 * 0 or 1. Editing that as a checkbox would quietly wipe the oil.
 */
PF_API int pf_unit_value_is_amount(int unit_id);

/** Which resource a unit holds, when it holds one. */
typedef enum pf_resource {
  PF_RESOURCE_NONE = 0,
  PF_RESOURCE_GOLD = 1,
  PF_RESOURCE_OIL = 2
} pf_resource;

/**
 * Whether a unit carries gold, oil or neither. Returns a `pf_resource`.
 *
 * Read off the unit flags rather than a list of ids, which is what the clients
 * were doing: `type == 0x5c ? gold : oil` appeared in two of them, and it is
 * wrong the moment anything other than a gold mine carries gold.
 */
PF_API int pf_unit_resource(int unit_id);

/**
 * How much of a resource a stored `value` means.
 *
 * The format keeps the amount in units of 2,500, which is also how the game
 * shows it — 16 is 40,000 gold. The multiplier is a fact about the format, so
 * it lives here rather than being written out wherever a total is summed.
 */
PF_API int64_t pf_resource_amount(int value);

/**
 * The stored `value` that holds an amount, which is the inverse of the above.
 *
 * Rounded to the nearest 2,500 and clamped to what the field can hold, because
 * an editor lets somebody type a number and the format has no way to keep
 * 41,000 gold. Here rather than in a client for the reason the multiplier is:
 * a client dividing by 2,500 itself is a second copy of the format's rule.
 */
PF_API int pf_resource_value(int64_t amount);

/**
 * What a newly placed unit's `value` should be: a sensible amount for a
 * resource, and the passive flag for everything else.
 *
 * A gold mine placed with nothing in it is scenery that does nothing, so the
 * editor fills it in. The amounts are the shipped maps' own habit rather than
 * a preference; see overrides/editing_policy.cpp.
 */
PF_API int pf_unit_default_value(int unit_id);

/**
 * Frame in the portrait artwork for a unit, or -1 when none is known.
 *
 * Warcraft II keeps its command-button icons in `art/unit/portrait`, one GRP
 * per tileset, but keeps the unit-to-frame mapping in the executable
 * rather than in its data. The table is therefore hand-written; see
 * overrides/portrait_frames.cpp, which records where each entry came from.
 *
 * Most units have no entry yet. Callers must fall back to the unit's sprite
 * rather than showing nothing.
 */
PF_API int pf_unit_icon(int unit_id);

/** How many units have a known icon. For tests and for reporting coverage. */
PF_API int pf_unit_icon_count(void);

/**
 * Whether a palette should keep this unit behind an opt-in.
 *
 * True for the twelve that no ordinary map places: the five dead slots, and
 * the runtime leftovers — corpses, rubble, and walls in unit form. PUDDraft
 * gathered the same twelve into one "Unused/Special Units" submenu behind a
 * warning; see overrides/hidden_units.cpp for the list and the reason for each.
 *
 * This governs what a palette offers, never what a file may contain: a map that
 * already holds one of these loads, edits and saves unchanged.
 */
PF_API int pf_unit_needs_opt_in(int unit_id);

/**
 * Whether a palette should never offer this unit, opt-in or not.
 *
 * The walls in unit form. Walls are terrain in this editor: there is a brush,
 * they auto-tile, and the corner model owns where they join. A second way to
 * place one that does none of that makes walls the wall tool cannot fix. As
 * ever, a map that already holds one round-trips unchanged.
 */
PF_API int pf_unit_never_offered(int unit_id);

/**
 * What a terrain class is called in a tileset, or NULL out of range.
 *
 * The ten classes mean the same thing everywhere; what they depict does not.
 * Winter's light ground is snow, the wasteland's and the swamp's is dirt, and
 * only the forest's is grass. Naming only — nothing here changes what a brush
 * paints. See overrides/terrain_names.cpp.
 */
PF_API const char *pf_terrain_name(int terrain, int tileset);

/** How many units are behind the opt-in. For tests. */
PF_API int pf_unit_needs_opt_in_count(void);

/**
 * The other race's equivalent of a unit, or -1 when it has none.
 *
 * A footman answers a grunt, a farm a pig farm, a keep a stronghold. Reads
 * both ways round. Heroes return -1: they are named characters rather than
 * roles, and have no opposite number — see overrides/race_counterparts.cpp.
 */
PF_API int pf_unit_counterpart(int unit_id);

/** How many human/orc pairs the table holds. For tests. */
PF_API int pf_unit_counterpart_count(void);

/** How many named heroes the game's own flag leaves out. For tests. */
PF_API int pf_unit_named_hero_count(void);

/** Path of the portrait artwork for a tileset, e.g. "art\\unit\\portrait\\s_port.grp". */
PF_API int pf_portrait_path(int tileset, char *out, int cap);
/** Display name of a group, or NULL past the end. */
PF_API const char *pf_unit_group_name(int group);

/**
 * Which way a unit faces, as a frame index below `frame_count`.
 *
 * The format stores no facing, so this hashes one, which keeps every client
 * drawing the same map the same way.
 *
 * `x` and `y` are where the unit *first stood*, not where it stands now — using
 * the current position spun a footman a frame per tile as it was dragged.
 */
PF_API int pf_unit_facing(int x, int y, int unit_id, int frame_count);

/** Non-zero for the two wall classes, which are an overlay, not a terrain. */
PF_API int pf_terrain_is_wall(int terrain);

/**
 * Which wall a terrain class means: 0 none, 1 human, 2 orc — the same numbering
 * `pf_map_paint_wall` takes, so a caller can hand one straight to the other.
 *
 * Exposed because the clients were translating it themselves, and a second
 * table mapping terrain to wall kind is a second place to get it wrong.
 */
PF_API int pf_terrain_wall_kind(int terrain);

/** How a brush covers its size. */
typedef enum pf_brush_shape {
  PF_BRUSH_SQUARE = 0,
  PF_BRUSH_CIRCLE = 1,
  PF_BRUSH_SCATTER = 2
} pf_brush_shape;

/**
 * The tiles a brush covers, as x,y pairs written into `out`.
 *
 * A square brush returns 0 and paints nothing here: the core's own painting
 * takes a size and covers a square already. `seed` is read and advanced, so a
 * scatter stroke keeps its pattern instead of shimmering as the pointer passes
 * back over the same ground.
 *
 * @param capacity_ints how many ints `out` holds — two per point. Named for its
 *        unit because passing the point count instead silently truncated the
 *        disc at half its points.
 * @return pairs written, or the number that would be written when `out` is
 *         NULL, or 0 for a square brush. A return larger than
 *         `capacity_ints / 2` means the buffer was too small.
 */
PF_API int pf_brush_points(int x, int y, int size, int shape, float density,
                           uint32_t *seed, int *out, int capacity_ints);

/** Display name of a player slot, e.g. "Player 1 (Red)". */
PF_API const char *pf_player_name(int player);
/** Packed 0x00RRGGBB display colour for a player slot. */
PF_API uint32_t pf_player_color(int player);
/**
 * Whether the game does anything with a slot: 1 for the eight playable ones and
 * the neutral slot 15, 0 for the seven between.
 *
 * Every per-player table in the format is sixteen wide, and no map in the
 * 1378-map corpus puts anything in slots 8..14. A host should leave those out
 * of what it offers; the tables stay sixteen wide regardless, so a map that
 * carries something there still round-trips it.
 */
PF_API int pf_player_is_supported(int player);

/* ----------------------------------------------------------- game data */

/**
 * Where artwork comes from: the Warcraft II install the user already has.
 *
 * Point this at the game folder and it opens the archives in the right order —
 * `War2Dat.mpq`, then `War2Patch.mpq` so the patch wins, then anything else it
 * finds. Loose directories are searched first, so an unpacked copy keeps
 * working and a mod folder can shadow the archives without repacking.
 *
 * This exists because Blizzard's files cannot be redistributed. An editor
 * somebody downloads has to read the game they own, and a game install is an
 * MPQ, not a folder of loose files.
 */
/**
 * One MPQ archive, opened from bytes.
 *
 * `pf_data_source` is for artwork, where several archives overlay each other and
 * the caller wants whichever wins. This is for looking inside one of them: the
 * campaign and the original multiplayer maps ship inside `War2Dat.mpq`, and
 * without this the only way to reach them is to unpack the archive first.
 *
 * `pf_mpq_read` returns memory the caller frees.
 */
typedef struct pf_mpq pf_mpq;

PF_API pf_mpq *pf_mpq_open_memory(const uint8_t *bytes, size_t length, pf_status *status);
PF_API void pf_mpq_free(pf_mpq *mpq);

/** Paths from the archive's `(listfile)`, empty when it carries none. */
PF_API int pf_mpq_file_count(const pf_mpq *mpq);
PF_API const char *pf_mpq_file_name(const pf_mpq *mpq, int index);

/** Extract one file. Returns caller-owned bytes, or NULL. */
PF_API uint8_t *pf_mpq_read(const pf_mpq *mpq, const char *name, size_t *length);

typedef struct pf_data_source pf_data_source;

PF_API pf_data_source *pf_data_source_create(void);
PF_API void pf_data_source_free(pf_data_source *source);

/**
 * The game's own string table — `rez/stat_txt.tbl`, where every name it shows
 * lives, and where a localised install keeps its translations.
 *
 * Strings come out as UTF-8; the file is cp1252, which is what the game's
 * fonts draw. Empty entries are real: the table names nothing for the five
 * dead unit slots.
 */
typedef struct pf_strings pf_strings;

PF_API pf_strings *pf_strings_open_memory(const uint8_t *bytes, size_t length,
                                          pf_status *status);
/** Reads `rez/stat_txt.tbl` out of the archives. NULL when it is not there. */
PF_API pf_strings *pf_strings_open_source(const pf_data_source *source,
                                          pf_status *status);
PF_API void pf_strings_free(pf_strings *strings);
PF_API int pf_strings_count(const pf_strings *strings);
/** One string, or "" out of range. Never NULL for a non-NULL table. */
PF_API const char *pf_strings_at(const pf_strings *strings, int index);

/**
 * Describe units and upgrades with this table from now on, rather than with the
 * names built into the core. NULL goes back to the built-ins.
 *
 * The game's wording is better than what this repository guessed at: unit 8 is
 * an "Elven Archer", and upgrade 12 is "Human Ship Attack 1" rather than "Human
 * Ship Cannon 1". Upgrade entries are command-button captions broken over three
 * lines, so their newlines come out as spaces; `pf_strings_at` has them intact.
 * Where the table names nothing the built-in label stands.
 *
 * The core borrows the table and never frees it: install once at startup and
 * keep it alive. Freeing it does uninstall it, so a host that gets the order
 * wrong gets the built-ins back rather than freed memory.
 */
PF_API void pf_use_strings(pf_strings *strings);

/**
 * The game's own sentence for a placement refusal, or "" when it has none.
 *
 * Same write-out convention as `pf_sprite_path`, and empty unless a table is
 * installed, so a client always needs wording of its own to fall back on.
 *
 * Only three of the `pf_placement` codes get one: the game says "You cannot
 * build there." for wrong ground, blocked ground and the wrong element alike,
 * where an editor can say which. Its messages are whole sentences aimed at a
 * player, so they do not compose with a unit name — use them as they are or not
 * at all.
 */
PF_API int pf_placement_message(int code, char *out, int cap);

/**
 * What the game calls a resource's remaining amount: "Gold Left:", "Oil Left:".
 * `resource` is a `pf_resource`. Empty for none, and when no table is
 * installed.
 */
PF_API int pf_resource_label(int resource, char *out, int cap);

/** Open every archive in a Warcraft II folder. Returns how many were opened. */
PF_API int pf_data_source_add_directory(pf_data_source *source, const char *dir);

/* --------------------------------------------------------- AI script table */

/**
 * The AI scripts, read out of `rez/ai.bin`.
 *
 * Everything about a script is derived from the file, so a modded archive with a
 * different or longer table is described as it actually is — including how many
 * scripts there are, which the file does not record and which is worked out.
 *
 * The instruction set is the one hand-written part; see overrides/ai_opcodes.cpp
 * for it and for where it came from.
 */
typedef struct pf_ai_scripts pf_ai_scripts;

PF_API pf_ai_scripts *pf_ai_scripts_open_source(const pf_data_source *source,
                                                pf_status *status);
PF_API pf_ai_scripts *pf_ai_scripts_open_memory(const uint8_t *bytes, size_t length,
                                                pf_status *status);
PF_API void pf_ai_scripts_free(pf_ai_scripts *scripts);

/** How many scripts the table holds. Not stored in the file; derived from it. */
PF_API int pf_ai_scripts_count(const pf_ai_scripts *scripts);

/**
 * A few lines saying what a script does: what it attacks with, the force it
 * tries to field, and what it opens by building.
 *
 * Returns the full length, so a caller can size a buffer and ask again. Writes
 * at most `cap - 1` bytes plus a terminator.
 */
PF_API int pf_ai_script_summary(const pf_ai_scripts *scripts, int index,
                                char *out, int cap);

/**
 * The attack waves, one per line, fields separated by tabs.
 *
 * A script states a target force, sleeps while the game builds it, then raises
 * the target and sleeps again, so each sleep ends a wave. Columns: number,
 * ticks slept after it, what it attacks by, the group shape, the force wanted.
 */
PF_API int pf_ai_script_waves(const pf_ai_scripts *scripts, int index,
                              char *out, int cap);

/** The disassembly, one instruction per line. Same return contract. */
PF_API int pf_ai_script_listing(const pf_ai_scripts *scripts, int index,
                                char *out, int cap);
/** One archive, at the highest priority so far. */
PF_API int pf_data_source_add_archive(pf_data_source *source, const char *path);
/** A directory of loose files, searched before any archive. */
PF_API void pf_data_source_add_files(pf_data_source *source, const char *dir);

/**
 * Read one file. Slashes may go either way. Free with `pf_buffer_free`.
 * Returns NULL when nothing in the source has it.
 */
PF_API uint8_t *pf_data_source_read(const pf_data_source *source, const char *name,
                                    size_t *out_len);

/* ------------------------------------------------------------------- map */

typedef struct pf_map pf_map;
/** Decoded tileset artwork; see the tileset section below. */
typedef struct pf_tileset_art pf_tileset_art;

/** A placed unit. Mirrors the 8-byte `UNIT` record. */
typedef struct pf_unit {
  uint16_t x;
  uint16_t y;
  uint8_t type;
  uint8_t owner;
  /** Gold mines and oil: amount / 2500. Otherwise 0 passive, 1 active. */
  uint16_t value;
} pf_unit;

/** Parse a PUD from memory. Returns NULL on failure. */
PF_API pf_map *pf_map_open(const uint8_t *data, size_t len, pf_status *status);
/** Read and parse a PUD from disk. */
PF_API pf_map *pf_map_open_file(const char *path, pf_status *status);
/** One octave of the noise a generated map is built from. */
typedef struct pf_noise_layer {
  float scale;    /**< features per tile; smaller is broader          */
  uint32_t seed;
  float weight;   /**< how much this octave contributes; 0 disables it */
} pf_noise_layer;

/**
 * Shares of the map each terrain should cover, as fractions summing to 1.
 *
 * Shares rather than thresholds: the noise is turned into terrain by cutting
 * it at the quantiles that produce these proportions, so asking for a fifth
 * water gives a fifth water whatever the noise happened to look like. Anything
 * left over after the five becomes ground.
 *
 * The shipped maps average water 25.5%, coast 10.9%, ground 33.7%, forest
 * 25.2% and rock 4.5%, which is a reasonable place to start.
 */
typedef struct pf_generate_params {
  int width;
  int height;
  int tileset;
  /** Share of the whole map. */
  float water;
  float coast;
  /**
   * Shares of the *land*, not of the map.
   *
   * Of the map they are unusable: raise the water and a fixed share of forest
   * eats the little land that is left, which at 72% water left 5% open ground
   * and 7% cliffs — an archipelago with nowhere to build. As shares of the
   * land, "a quarter of it is forest" holds however much land there is.
   */
  float forest;
  float rock;
  /** Second field deciding where forest and rock sit, rather than a band of
   *  the first — they are scattered on land, not an altitude. */
  uint32_t detail_seed;
  float detail_scale;
  /**
   * Clearings: flat open ground carved out for bases to be built in.
   *
   * Noise alone makes scenery, not a playable map. These are the spots a
   * player starts in, so they are cleared of forest and cliffs and spread as
   * far apart as the land allows. `clearing_radius` is in tiles; a Warcraft II
   * base wants about 8.
   */
  int clearings;
  int clearing_radius;
} pf_generate_params;

/**
 * A new map built from layered noise.
 *
 * The generator emits corner terrains through the same painting path a person
 * uses, so the result is exactly as editable as a hand-drawn map and the
 * terrain graph does the coastlines for free. It cannot emit tile values: a
 * field is not a tile id, and writing one directly would reintroduce every
 * problem the corner model exists to solve.
 *
 * Deterministic: the same layers and parameters always give the same map.
 */
PF_API pf_map *pf_map_generate(const pf_generate_params *params,
                               const pf_noise_layer *layers, int layer_count,
                               pf_status *status);

/**
 * Re-choose every tile from the corner terrains already on the map.
 *
 * Painting picks tiles the loaded artwork can draw, but a map generated before
 * any artwork was attached had nothing to filter against and will be full of
 * tiles with no megatile. Attaching the tileset and calling this fixes them.
 *
 * Deliberately not automatic on open: it rewrites tile values, and a shipped
 * map has to keep the exact bytes it came with.
 *
 * @return how many tiles changed.
 */
PF_API int pf_map_refit(pf_map *map);

/** Blank map filled with light ground. */
PF_API pf_map *pf_map_create(int width, int height, int tileset, pf_status *status);
PF_API void pf_map_free(pf_map *map);

/**
 * Serialize to a caller-owned buffer. Writes the length to `out_len`.
 * Free the result with `pf_buffer_free`. Returns NULL on failure.
 */
PF_API uint8_t *pf_map_save(const pf_map *map, size_t *out_len, pf_status *status);
PF_API pf_status pf_map_save_file(const pf_map *map, const char *path);
PF_API void pf_buffer_free(uint8_t *buffer);

PF_API int pf_map_width(const pf_map *map);
PF_API int pf_map_height(const pf_map *map);
/** Resolved tileset — `ERAX` wins over `ERA ` when both are present. */
PF_API int pf_map_tileset(const pf_map *map);
PF_API pf_status pf_map_set_tileset(pf_map *map, int tileset);

/**
 * Replace tiles the current artwork cannot draw, and say how many.
 *
 * The four tilesets are not one-to-one — winter and wasteland water hold
 * variations the forest and swamp groups leave blank — so changing a map's
 * tileset leaves a handful of tiles with no drawing, which render as flat
 * colour. Each is replaced by another variation of the same group, so only the
 * drawing changes. Does nothing until pf_map_set_tileset_art has been called.
 */
PF_API int pf_map_refit_tiles(pf_map *map);
/**
 * The description as UTF-8, decoded from the bytes the file holds.
 *
 * `DESC` is not UTF-8 and never was: it is raw bytes the game draws with a
 * bitmap font indexed by byte value, and that font is code page 437. So the
 * bytes are decoded on the way out and encoded on the way in, and a client
 * only ever sees UTF-8.
 */
PF_API const char *pf_map_description(const pf_map *map);

/**
 * Replace the description. `text` is UTF-8.
 *
 * PF_ERR_OUT_OF_RANGE, and the map untouched, when the text needs a character
 * code page 437 has no byte for or more than pf_map_description_max() of them.
 * Refusing is deliberate: storing part of it and dropping the rest at save time
 * is the failure this replaces.
 *
 * Writing back exactly what pf_map_description() returned always succeeds, even
 * for the one shipped map whose description leaves no terminator, because an
 * unchanged description is not rewritten.
 */
PF_API pf_status pf_map_set_description(pf_map *map, const char *text);

/**
 * How many `DESC` bytes UTF-8 `text` would need, or -1 if it cannot be stored.
 *
 * One storable character is one byte, so this is also the character count a
 * client should show against pf_map_description_max(). -1 means some character
 * has no place in the field, which is the moment to turn the keystroke down.
 */
PF_API int pf_map_description_bytes(const char *text);

/**
 * The longest description a PUD can hold, in bytes.
 *
 * `DESC` is a fixed 32-byte field. One shipped map fills all 32 and leaves no
 * terminator, and the game lists it, but a description this editor writes
 * keeps the last byte a NUL: 31 bytes of text. Reading that map does not
 * shorten it - only rewriting it would.
 */
PF_API int pf_map_description_max(void);
/** 0x11 classic, 0x13 expansion. */
PF_API int pf_map_version(const pf_map *map);

/** Number of non-fatal problems found while parsing. */
PF_API int pf_map_warning_count(const pf_map *map);
PF_API const char *pf_map_warning(const pf_map *map, int index);

/* Per-player data. `player` is 0..15. */
PF_API int pf_map_owner(const pf_map *map, int player);
PF_API pf_status pf_map_set_owner(pf_map *map, int player, int owner);
PF_API int pf_map_race(const pf_map *map, int player);
PF_API pf_status pf_map_set_race(pf_map *map, int player, int race);
PF_API int pf_map_start_gold(const pf_map *map, int player);
PF_API int pf_map_start_lumber(const pf_map *map, int player);
PF_API int pf_map_start_oil(const pf_map *map, int player);
PF_API pf_status pf_map_set_start_resources(pf_map *map, int player, int gold, int lumber, int oil);
PF_API int pf_map_ai(const pf_map *map, int player);
PF_API pf_status pf_map_set_ai(pf_map *map, int player, int ai);

/* Terrain layers. Pointers are valid until the map is edited or freed. */
PF_API const uint16_t *pf_map_tiles(const pf_map *map);
PF_API const uint16_t *pf_map_movement(const pf_map *map);
PF_API const uint16_t *pf_map_regions(const pf_map *map);
PF_API int pf_map_tile_at(const pf_map *map, int x, int y);

/**
 * The `SQM ` movement value a tile implies.
 *
 * Movement is very nearly a function of the tile: across 556 shipped maps only
 * 236 tiles in 6,975,488 hold anything else. Painting writes this, so the layer
 * stays correct without anyone editing it, and a tile whose stored value
 * differs from this is an override somebody meant.
 */
PF_API int pf_tile_movement(int tile);

/** The stored movement value at a tile, or -1 outside the map. */
PF_API int pf_map_movement_at(const pf_map *map, int x, int y);
/** Override the movement value at a tile. */
PF_API pf_status pf_map_set_movement(pf_map *map, int x, int y, int value);
/**
 * Put movement back to what the tiles imply, over a rectangle.
 *
 * A size of zero or less means the whole map. Returns how many tiles changed,
 * which is how a caller knows whether to take an undo step.
 */
PF_API int pf_map_reset_movement(pf_map *map, int x, int y, int w, int h);

/**
 * The movement values Warcraft II uses, as named classes.
 *
 * `SQM ` is nominally a bitfield, but no shipped map sets a combination
 * outside these eight, so there is nothing to correlate an individual bit
 * against. Each of the eight is attributable to a terrain situation instead —
 * see overrides/movement_classes.cpp for the measurement. `pf_movement_class_of`
 * returns -1 for a value outside the set, which an editor should show as raw
 * hex rather than pretend to understand.
 */
/**
 * The owner a newly placed unit should get, or -1 to use the chosen player.
 *
 * Gold mines, oil patches, circles of power and dark portals are scenery: every
 * one of them in the shipped maps belongs to player 15. A default applied when
 * placing, not a rule on the file — a map that already owns one keeps it. See
 * overrides/neutral_owners.cpp.
 */
PF_API int pf_unit_default_owner(int unit_id);

PF_API int pf_movement_class_count(void);
PF_API int pf_movement_class_value(int index);
PF_API const char *pf_movement_class_name(int index);
PF_API int pf_movement_class_of(int value);

/**
 * Decode a tile into its four quadrant terrains, in the order
 * top-left, top-right, bottom-left, bottom-right. `out` must hold 4 bytes.
 */
PF_API void pf_tile_quadrants(uint16_t tile, uint8_t *out);
/** The terrain covering most of a tile. */
PF_API int pf_tile_dominant_terrain(uint16_t tile);

/* Units. */
PF_API int pf_map_unit_count(const pf_map *map);
PF_API pf_status pf_map_unit(const pf_map *map, int index, pf_unit *out);
PF_API int pf_map_add_unit(pf_map *map, int x, int y, int type, int owner, int value);
PF_API pf_status pf_map_remove_unit(pf_map *map, int index);
PF_API pf_status pf_map_move_unit(pf_map *map, int index, int x, int y);
/**
 * Set a placed unit's `value`.
 *
 * Gold mines and oil patches store the resource amount divided by 2500;
 * everything else stores 0 for passive and 1 for active. The format gives the
 * field one name and two meanings, so the caller decides which it is writing.
 */
PF_API pf_status pf_map_set_unit_value(pf_map *map, int index, int value);

/** Reassign a placed unit to another player slot (0..15). */
PF_API pf_status pf_map_set_unit_owner(pf_map *map, int index, int owner);
/** Index of the topmost unit whose footprint covers a tile, or -1. */
PF_API int pf_map_unit_at(const pf_map *map, int x, int y);
/** Tile footprint of a unit type on this map, from its `UDTA`. */
PF_API void pf_map_unit_footprint(const pf_map *map, int type, int *w, int *h);

/* ------------------------------------------------------------ unit data */

/**
 * `UDTA` field access, table-driven.
 *
 * `UDTA` is a struct-of-arrays, so rather than sixty accessors fields are
 * addressed by index — enumerate them with `pf_udta_field_count` and
 * `pf_udta_field_name`. Reads and writes go straight into the section's raw
 * bytes, so fields nothing models are preserved and a round-trip stays exact.
 *
 * `component` is 0 except for `unitSize` and `boxSize`, which store x then y.
 * `pf_udta_field_units` returns 0 for the three arrays that are not per-unit;
 * they are listed so offsets stay derivable, not to be edited.
 */
PF_API int pf_udta_field_count(void);

/**
 * The same field from the retail table the core embeds.
 *
 * A map may carry no `UDTA`, in which case the game uses these — so this is
 * what to show rather than an error sentinel. It is also the reference for
 * "what did this map change", since the two can be compared directly.
 */
PF_API int64_t pf_udta_default_field(int field, int unit, int component);
PF_API const char *pf_udta_field_name(int field);
PF_API int pf_udta_field_components(int field);
PF_API int pf_udta_field_units(int field);
/** Bytes per element: 1, 2 or 4. */
PF_API int pf_udta_field_width(int field);

/** How to read a field's value: one of the `PF_UDTA_*` kinds below. */
PF_API int pf_udta_field_kind(int field);

/**
 * A field's name as a label a person reads: `buildTime` becomes "Build Time".
 *
 * Here rather than in each client because it is a fact about the format: three
 * front-ends spelling the same field three ways is the drift this repo's
 * layering rule exists to stop.
 *
 * Mechanical — split at a lower-to-upper boundary, capitalise each word — with
 * a short table of exceptions in constants.cpp. Writes at most `cap - 1`
 * characters plus a terminator and returns the full length.
 */
PF_API int pf_field_label(const char *name, char *out, int cap);

#define PF_UDTA_NUMBER 0
#define PF_UDTA_BOOL 1  /* 0 or 1 */
#define PF_UDTA_ENUM 2  /* one of the field's options */
#define PF_UDTA_FLAGS 3 /* any combination; each option is a bit mask */

/** How many labelled values an enum or flags field has; 0 for a number. */
PF_API int pf_udta_field_option_count(int field);

/**
 * The label of one option, and through `value` its number or bit mask.
 * Returns NULL when `index` is out of range.
 *
 * The list is not exhaustive: unnamed flag bits and unknown enum values occur
 * in real maps. A field editor must preserve them — toggle individual bits
 * rather than rebuilding a mask from the named ones, and keep an unrecognised
 * enum value rather than snapping it to the nearest label.
 */
PF_API const char *pf_udta_field_option(int field, int index, int *value);

/**
 * Mirror painting. A set of axes rather than one: they combine, and every
 * combination is a group of reflections whose orbit is what an edit repeats
 * across. Left-right and top-bottom together give four positions; adding both
 * diagonals gives eight.
 */
typedef enum pf_mirror {
  PF_MIRROR_NONE = 0,
  PF_MIRROR_LEFT_RIGHT = 1,  /**< across a vertical axis          */
  PF_MIRROR_TOP_BOTTOM = 2,  /**< across a horizontal axis        */
  PF_MIRROR_DIAG_NW_SE = 4,  /**< across the main diagonal, square maps  */
  PF_MIRROR_DIAG_SW_NE = 8   /**< across the anti-diagonal, square maps  */
} pf_mirror;

/**
 * Every position a point maps to under a set of mirrors, itself first, as x,y
 * pairs.
 *
 * The orbit of the point under the group the mirrors generate, not one
 * reflection each: left-right and top-bottom together must also give the
 * diagonally opposite corner.
 *
 * `w` and `h` are the footprint of what is being placed, because a mirrored
 * building is anchored at `width - x - w` — reflecting the anchor alone would
 * push a 4x4 keep three tiles off its own reflection. A diagonal reflection
 * swaps the footprint too, which is why it is only offered on a square map.
 *
 * Positions are deduplicated, since a point on an axis is its own reflection.
 *
 * @return pairs written, at most eight.
 */
/**
 * The same orbit for a *corner* of the grid, as cx,cy pairs.
 *
 * Corners run 0..width and 0..height inclusive, one more than the tiles, so a
 * reflection is `width - cx` where a tile's is `width - x - w`. Reflecting a
 * corner through `pf_symmetry_points` lands one corner short, which puts the
 * mirrored mark half a tile off its own reflection.
 *
 * Deduplicated, and diagonals only on a square map, both as above.
 *
 * @return pairs written, at most eight.
 */
PF_API int pf_symmetry_corners(const pf_map *map, int cx, int cy, int mirrors,
                               int *out, int capacity);

PF_API int pf_symmetry_points(const pf_map *map, int x, int y, int w, int h,
                              int mirrors, int *out, int capacity);

/**
 * Give every active player a start location.
 *
 * A player is active when its `OWNR` is anything but "nobody". Slots that
 * already have one are left alone, so this fills gaps rather than rearranging a
 * map somebody has laid out. Positions are chosen on open ground, away from the
 * edge, and as far from each other as a coarse search manages.
 *
 * @return how many were placed, or a negative value on failure.
 */
PF_API int pf_map_place_start_locations(pf_map *map);

/**
 * Scatter gold mines on open ground, spread as far apart as the land allows.
 *
 * Not always possible: a map that is nearly all water, forest or cliff has
 * nowhere to put them, and this places as many as fit rather than failing. The
 * caller is expected to say how many it got.
 *
 * Each starts with 40,000 gold, which is what the shipped maps use most often.
 *
 * @return how many were placed, which may be fewer than asked for.
 */
PF_API int pf_map_place_gold_mines(pf_map *map, int count);

/**
 * Randomise the light and dark shade of every terrain in a rectangle.
 *
 * Ground, water and coast each come in two shades that the game treats as the
 * same terrain, and a map painted in one flat shade looks machine-made. This
 * flips each corner between the two at random, then re-fits, so the result is
 * still expressible as real tiles.
 *
 * Pass `w` or `h` of 0 for the whole map.
 *
 * @return how many tiles changed.
 */
PF_API int pf_map_randomize_shades(pf_map *map, int x, int y, int w, int h,
                                   uint32_t seed);

/**
 * Shade what a stroke just laid, and nothing else.
 *
 * The version above rewrites every shadeable corner in its rectangle, which is
 * wrong for a brush: a stroke of dirt across a grass field has a bounding box
 * far larger than the stroke.
 *
 * So this is held two ways. `terrain` names the only pair it may touch, and
 * `mask`, when given, is `w * h` bytes in row-major order over the rectangle,
 * non-zero for a tile the stroke actually covered. Pass `terrain` of -1 for
 * every pair, or `mask` of null for the whole rectangle; with both, this is
 * `pf_map_randomize_shades`.
 *
 * @return how many tiles changed.
 */
PF_API int pf_map_shade_stroke(pf_map *map, int x, int y, int w, int h,
                               uint32_t seed, int terrain, const uint8_t *mask);

/**
 * Give a map an `ALOW` section it does not have, with nothing restricted.
 *
 * Seeded with the unrestricted table, which is how a map without the section
 * already behaves, so adding it changes nothing until something is turned off.
 * That table is not all-ones — see `pf_alow_default`. Opt-in because it rewrites
 * the file. Does nothing if the map already has one.
 *
 * @return PF_OK, or PF_ERR_INVALID_ARG.
 */
PF_API pf_status pf_map_add_restrictions(pf_map *map);

/**
 * Drop the `ALOW` section, so the map restricts nothing and carries no table.
 * The state most maps are in. Does nothing if the map has none already.
 *
 * @return PF_OK, or PF_ERR_INVALID_ARG.
 */
PF_API pf_status pf_map_clear_restrictions(pf_map *map);

/**
 * Give a map a `UDTA` or `UGRD` section it does not have.
 *
 * Seeded with the game's own table, so the map plays exactly as it did — the
 * values simply become editable. Both are ordinary optional sections of the
 * format, so this is supported rather than a trick. No-ops when the section is
 * already there, so an editor can offer it without checking first.
 */
PF_API pf_status pf_map_add_unit_data(pf_map *map);
PF_API pf_status pf_map_add_upgrade_data(pf_map *map);

/**
 * Write one tile value directly, with no fitting.
 *
 * The corner model cannot express every tile a tileset holds — particular
 * decorated variants, odd bridge pieces — so this is the escape hatch a tile
 * picker needs. It bypasses the terrain graph entirely, which is the point and
 * also the risk: neighbouring tiles are not adjusted, so the caller can make a
 * seam the model would never produce.
 */
PF_API pf_status pf_map_set_tile(pf_map *map, int x, int y, int tile);

/** What kind of payload a loose component file holds, from its length. */
typedef enum pf_component {
  PF_COMPONENT_UNKNOWN = 0,
  PF_COMPONENT_UDTA = 1,   /**< `.un`  — 5696 or 5950 bytes */
  PF_COMPONENT_UGRD = 2,   /**< `.up`  — 782              */
  PF_COMPONENT_BOTH = 3,   /**< `.unt` — both, back to back */
  PF_COMPONENT_ALOW = 4    /**< `.alo` — 384              */
} pf_component;

/**
 * Identify a component file by its length.
 *
 * These files have no header of any kind: a `.un` is literally the bytes of a
 * `UDTA` section. Length is the only thing that distinguishes them, which is
 * why the sizes are worth stating — and why a wrong file is rejected here
 * rather than being written into a map.
 */
PF_API int pf_component_kind(size_t length);

/**
 * Replace a map's `UDTA`, `UGRD` or `ALOW` from a component file's bytes.
 * The kind is taken from the length; pass what `pf_component_kind` returned.
 */
PF_API pf_status pf_map_import_component(pf_map *map, const uint8_t *data, size_t len);

/**
 * A component's bytes, straight out of the map. Returns NULL when the map has
 * no such section. Free with `pf_buffer_free`.
 */
PF_API uint8_t *pf_map_export_component(const pf_map *map, int component,
                                        size_t *out_len);

/** Non-zero when the map carries a `UDTA` section at all. */
PF_API int pf_map_has_unit_data(const pf_map *map);

/** Field value, or -1 when the field, unit or component is out of range. */
PF_API int64_t pf_map_unit_field(const pf_map *map, int field, int unit, int component);
PF_API pf_status pf_map_set_unit_field(pf_map *map, int field, int unit, int component,
                                       int64_t value);

/* --------------------------------------------------------- upgrade data */

/**
 * `UGRD` field access. Same shape as `UDTA`: a struct-of-arrays addressed by
 * field index, edited through offsets into the section's raw bytes so the
 * round-trip stays byte-exact. 52 upgrades.
 */
PF_API int pf_ugrd_field_count(void);

/**
 * An upgrade field from the game's own `UGRD`, for a map that carries none.
 *
 * The counterpart of pf_udta_default_field. Without it a property sheet on a
 * map with no `UGRD` shows -1 in every box while claiming to show defaults,
 * which is worse than showing nothing.
 */
PF_API int64_t pf_ugrd_default_field(int field, int upgrade);
PF_API const char *pf_ugrd_field_name(int field);
PF_API int pf_ugrd_field_width(int field);
/** Entries in the field, or 0 for `useDefaultData`, which is not per-upgrade. */
PF_API int pf_ugrd_field_entries(int field);
PF_API int pf_map_has_upgrade_data(const pf_map *map);
PF_API int64_t pf_map_upgrade_field(const pf_map *map, int field, int upgrade);
PF_API pf_status pf_map_set_upgrade_field(pf_map *map, int field, int upgrade,
                                          int64_t value);

/* --------------------------------------------------------- restrictions */

/**
 * `ALOW` — six blocks of 16 uint32 bitfields, one per player: units allowed,
 * spells started with, spells allowed, spells researching, upgrades allowed,
 * upgrades researching.
 *
 * The section is optional. When a map has none, reads return -1 and writes
 * fail rather than creating an empty one, so maps stay as they were found.
 */
PF_API int pf_map_has_restrictions(const pf_map *map);
PF_API const char *pf_alow_block_name(int block);
/**
 * What a block holds when nothing is restricted, for one player. -1 for a bad
 * block. Not all-ones: two blocks say "researching" and one says which spells a
 * player already knows, so a host showing a map that has no section must seed
 * itself from this or it will write a table no real map writes. The value
 * `pf_map_add_restrictions` uses; the reasoning is in overrides/alow_defaults.cpp.
 */
PF_API int64_t pf_alow_default(int block);
PF_API int64_t pf_map_allow(const pf_map *map, int block, int player);
PF_API pf_status pf_map_set_allow(pf_map *map, int block, int player, int64_t bits);

/**
 * Resize the map. Content keeps its position, shifted by (offset_x, offset_y)
 * in the new grid, so cropping and padding are the same operation.
 *
 * Growing fills with light ground. Shrinking discards tiles and drops any unit
 * that no longer fits whole; the return value is how many were dropped, so a
 * client can tell the user rather than losing them silently. `REGM` is rebuilt.
 *
 * Returns -1 on failure, with `status` set.
 */
PF_API int pf_map_resize(pf_map *map, int width, int height,
                         int offset_x, int offset_y, pf_status *status);

/**
 * How many bytes of `OILM` are not zero.
 *
 * The game does not read `OILM`, so a normal map's is all zeros. War2mod's
 * trigger editor stores its bytecode there instead, and that is not a per-tile
 * grid - resizing remaps it tile by tile like the terrain and scrambles it.
 * A client can ask this before offering to resize.
 */
PF_API int pf_map_oil_map_used(const pf_map *map);

/* ------------------------------------------------------------ variation */

/**
 * Which variations of a tile group painting may pick from.
 *
 * A group holds several drawings of the same terrain: some plain, some with
 * rocks, flowers or pebbles scattered on them. Painting everything with the
 * decorated ones looks noisy; painting everything plain looks flat. PUDDraft
 * offered the same choice as Plain / Random / Filler.
 *
 * "Decorated" is judged against the other variations of the same group, not
 * absolutely — forest and rock are busy textures in themselves.
 */
typedef enum pf_variation {
  PF_VARIATION_ANY = 0,       /**< any variation, uniformly     */
  PF_VARIATION_PLAIN = 1,     /**< only the undecorated ones    */
  /**
   * Half plain, half decorated.
   *
   * Not "only the decorated ones", which is what the name still says: every
   * tile carrying rocks reads as a rash rather than as texture, because the
   * decoration is drawn to be the exception on a field of plain ground.
   *
   * The halves are equal by share, not by count — a group has three or four
   * plain drawings and however many decorated ones its tileset gave it.
   */
  PF_VARIATION_DECORATED = 2
} pf_variation;

PF_API pf_status pf_map_set_variation_policy(pf_map *map, int policy);
PF_API int pf_map_variation_policy(const pf_map *map);

/* ------------------------------------------------------------ placement */

typedef enum pf_placement {
  PF_PLACE_OK = 0,
  PF_PLACE_OUT_OF_BOUNDS = 1,  /**< off the map, or footprint runs off it   */
  PF_PLACE_NEEDS_LAND = 2,     /**< a ground unit over water                */
  PF_PLACE_NEEDS_WATER = 3,    /**< a ship over land                        */
  PF_PLACE_NEEDS_GROUND = 4,   /**< a building on coast, forest or rock     */
  PF_PLACE_BLOCKED = 5,        /**< a ground unit on forest, rock or wall   */
  PF_PLACE_NEEDS_SHORE = 6,    /**< a shipyard, foundry or refinery inland   */
  PF_PLACE_TOO_NEAR_MINE = 7,  /**< a town hall crowding a gold mine         */
  PF_PLACE_OCCUPIED = 8,       /**< another unit is already standing there   */
  PF_PLACE_ON_EDGE = 9         /**< touching the map edge, where the game
                                    will not let a unit stand              */
} pf_placement;

typedef enum pf_domain {
  PF_DOMAIN_LAND = 0,
  PF_DOMAIN_WATER = 1,
  PF_DOMAIN_AIR = 2,
  PF_DOMAIN_ANY = 3   /**< gold mines, oil patches, markers */
} pf_domain;

/** Where a unit type can stand, from the retail unit table. Returns pf_domain. */
PF_API int pf_unit_domain(int unit_id);

/**
 * A tile value whose four quadrants are all `terrain`, or -1 when the terrain
 * has no solid tile (the two wall classes). Lets a client show what a brush
 * paints instead of naming it.
 */
PF_API int pf_solid_tile(int terrain, int variation);

/**
 * A unit's `UDTA` flags from the retail table, so clients can classify units
 * without each inventing its own list. Bits, from reference/docs/pud-format.md:
 * 0 land, 1 air, 3 sea, 4 critter, 5 building, 11 oil source, 16 shore
 * building, 20 tower, 21 oil patch, 22 mine, 23 hero.
 */
PF_API uint32_t pf_unit_flags(int unit_id);

/**
 * Whether the *terrain* at (x, y) suits a unit type — every tile of its
 * footprint has to. Returns a `pf_placement` code.
 *
 * This asks about the ground and nothing else, which is why it is not the one
 * an editor should call: it will happily approve a tile another unit is
 * already standing on. Use `pf_map_placement_check_ex` for the whole question.
 */
PF_API int pf_map_placement_check(const pf_map *map, int x, int y, int type);

/**
 * Whether a unit type may be placed at (x, y) at all: the terrain, what is
 * already there, and the map edge, subject to the three options below.
 *
 * The one answer every way of putting a unit down goes through. They used to
 * disagree: stacking and the edge rule were the Windows client's own, so a
 * paste could drop units on top of units the same client would have refused.
 *
 * `ignore` lists unit indices that do not count as being in the way, for the
 * caller that is moving them. Pass NULL/0 when nothing is being moved.
 *
 * @return a `pf_placement` code; `PF_PLACE_OK` when it may stand there
 */
PF_API int pf_map_placement_check_ex(const pf_map *map, int x, int y, int type,
                                     const int *ignore, int ignore_count);

/**
 * The three escape hatches from the rules above, all off by default. PUDDraft
 * had the same three, and off is right for all of them because the common case
 * is a slip rather than an intention: a footman in the ocean, two units on one
 * tile, a building the game will not let you finish because it is against the
 * edge.
 *
 * Illegal placement lifts only the terrain rule; bounds are never lifted,
 * because a unit off the map is not something the format can hold.
 */
PF_API void pf_map_set_allow_illegal_placement(pf_map *map, int allow);
PF_API int pf_map_allows_illegal_placement(const pf_map *map);
PF_API void pf_map_set_allow_stacked_units(pf_map *map, int allow);
PF_API int pf_map_allows_stacked_units(const pf_map *map);
PF_API void pf_map_set_allow_edge_placement(pf_map *map, int allow);
PF_API int pf_map_allows_edge_placement(const pf_map *map);

/* ------------------------------------------------------------ clipboard */

/**
 * A detached fragment of a map: terrain, walls and units.
 *
 * Terrain travels as *corner terrains* rather than tile values, because a
 * rotated tile is not the same tile — turning raw `MTXM` values would produce
 * tiles whose artwork does not match their shape. Paste puts the corners back
 * through the same corner model that painting uses, including one tile of
 * slack around the edge so the seam is refitted instead of left hard.
 *
 * The fragment is independent of the map it came from and may be pasted into
 * a different one.
 */
typedef struct pf_clipboard pf_clipboard;

/**
 * Copy a rectangle. Units are included only when they lie wholly inside it.
 * Returns NULL if the rectangle is not within the map.
 */
PF_API pf_clipboard *pf_clipboard_copy(const pf_map *map, int x, int y, int w, int h,
                                       int include_terrain, int include_units);

/**
 * Copy the tiles of a rectangle that `mask` marks, and nothing else.
 *
 * A selection built with shift and alt is not a rectangle, and copying its
 * bounding box takes ground the user never selected. `mask` is `w * h` bytes in
 * row-major order, non-zero where the tile is in the fragment; NULL means the
 * whole rectangle, which is exactly `pf_clipboard_copy`.
 *
 * A fragment with holes still travels as a rectangle, because a rotation needs
 * something to rotate. Units are taken only when every tile of their footprint
 * is masked in — half a keep is not a keep.
 */
PF_API pf_clipboard *pf_clipboard_copy_masked(const pf_map *map, int x, int y,
                                              int w, int h, const uint8_t *mask,
                                              int include_terrain,
                                              int include_units);
PF_API void pf_clipboard_free(pf_clipboard *clip);

PF_API int pf_clipboard_width(const pf_clipboard *clip);
PF_API int pf_clipboard_height(const pf_clipboard *clip);
PF_API int pf_clipboard_unit_count(const pf_clipboard *clip);

/**
 * Whether the fragment carries the tile at (x, y) or has a hole there.
 *
 * Always 1 for a fragment copied without a mask. Exposed so a paste preview
 * draws the shape that is going to land rather than its bounding box.
 */
PF_API int pf_clipboard_tile_included(const pf_clipboard *clip, int x, int y);

/**
 * One corner terrain of the fragment, or -1 when it has no terrain.
 *
 * The grid is (width + 1) by (height + 1), as the corner model always is: a
 * tile's appearance is decided by the four corners around it. Exposed so a
 * paste preview can show the terrain it is about to drop rather than an empty
 * rectangle.
 */
PF_API int pf_clipboard_corner(const pf_clipboard *clip, int x, int y);
PF_API int pf_clipboard_has_terrain(const pf_clipboard *clip);

/**
 * The tile the corner model would choose for four corner terrains.
 *
 * `corners` is top-left, top-right, bottom-left, bottom-right, the order
 * `pf_tile_quadrants` hands them back in. `salt` picks among the drawings a
 * tileset has for that combination, deterministically.
 *
 * Filtered by whatever artwork the map has attached, which is why it takes a
 * map. Exposed for the paste preview: a fragment travels as corner terrains, so
 * showing what it would drop means running the same corner-to-tile step.
 *
 * @return the tile value, or -1 when no drawable tile expresses that
 *         combination
 */
PF_API int pf_map_tile_for_corners(pf_map *map, const uint8_t *corners,
                                   uint32_t salt);

/**
 * One unit from the fragment, positioned relative to its top-left corner.
 * Lets a client show where each would land before committing to a paste.
 */
PF_API pf_status pf_clipboard_unit(const pf_clipboard *clip, int index, pf_unit *out);

/** Mirror left-to-right. */
PF_API pf_status pf_clipboard_flip(pf_clipboard *clip);
/** Mirror top-to-bottom. */
PF_API pf_status pf_clipboard_mirror(pf_clipboard *clip);
/** Rotate clockwise; width and height swap on odd counts. */
PF_API pf_status pf_clipboard_rotate(pf_clipboard *clip, int quarter_turns);

/**
 * Paste at (x, y). Returns the number of units placed, or -1 when the
 * fragment does not fit inside the map at that position.
 */
PF_API int pf_map_paste(pf_map *map, const pf_clipboard *clip, int x, int y);

/**
 * Paste, choosing whether the seam is refitted.
 *
 * With `fit_edges` the surrounding tiles are adjusted so the fragment blends
 * in; without it only the fragment's own tiles are written and the join stays
 * exactly as pasted. PUDDraft offered the same choice.
 */
PF_API int pf_map_paste_ex(pf_map *map, const pf_clipboard *clip, int x, int y,
                           int fit_edges);

/* ----------------------------------------------------------- validation */

typedef enum pf_severity {
  PF_SEVERITY_INFO = 0,
  PF_SEVERITY_WARNING = 1,
  PF_SEVERITY_ERROR = 2
} pf_severity;

typedef enum pf_issue_code {
  PF_ISSUE_NO_PLAYERS = 1,           /* no slot is in play                  */
  PF_ISSUE_NO_START_LOCATION = 2,    /* an active player has none           */
  PF_ISSUE_EXTRA_START_LOCATION = 3, /* a player has more than one          */
  PF_ISSUE_ORPHAN_START = 4,         /* start location for an inactive slot */
  PF_ISSUE_UNIT_OUT_OF_BOUNDS = 5,   /* unit outside the map                */
  PF_ISSUE_UNIT_OVERFLOWS = 6,       /* footprint runs off the edge         */
  PF_ISSUE_NO_RESOURCES = 7,         /* no gold mine anywhere               */
  PF_ISSUE_START_RACE_MISMATCH = 8,  /* orc start for a human player        */
  PF_ISSUE_OIL_WELL_NO_PATCH = 9,    /* oil well not standing on a patch     */
  PF_ISSUE_UNITS_OVERLAP = 10,       /* two footprints share a tile          */
  PF_ISSUE_ILLEGAL_TERRAIN = 11,     /* unit on terrain it cannot stand on   */
  PF_ISSUE_HALL_CROWDS_MINE = 12,    /* town hall too close to a gold mine   */
  PF_ISSUE_START_NO_GOLD = 13        /* start location with no mine in reach */
} pf_issue_code;

typedef struct pf_issue {
  int severity;   /**< pf_severity */
  int code;       /**< pf_issue_code */
  int player;     /**< slot the issue concerns, or -1 */
  int x, y;       /**< tile position, or -1 when not positional */
  char message[128];
} pf_issue;

/**
 * Check a map for problems that would make it unplayable or surprising.
 *
 * Fills up to `capacity` issues and returns the total number found, which may
 * exceed `capacity` — pass NULL/0 to count first. Findings are ordered by
 * severity, errors first, so a truncated list still shows what matters.
 *
 * Every client renders the same findings because the rules live here.
 */
PF_API int pf_map_validate(const pf_map *map, pf_issue *out, int capacity);

/**
 * Gold within working reach of a player's start location, or -1 for a player
 * with no start location.
 *
 * "Within reach" is 40 tiles, clear air above the furthest any of the 138 start
 * locations on Blizzard's 28 multiplayer maps sits from its nearest mine (32.2;
 * the median is 5.4).
 *
 * Reported rather than judged: across those 28 maps the spread between the
 * best-supplied start and the worst is 0% on seventeen and 100% on 4lake, so an
 * even split is a habit and not a rule.
 */
PF_API int64_t pf_map_start_gold_in_reach(const pf_map *map, int player);

/**
 * How many separate landmasses the map's start locations sit on.
 *
 * 1 means every player can walk to every other. Also not a rule: of Blizzard's
 * 28, sixteen are one landmass and twelve are islands, including one with all
 * eight players separated. It decides what kind of map this is, which is worth
 * showing and not worth warning about.
 */
PF_API int pf_map_start_landmasses(const pf_map *map);

/* ----------------------------------------------------------------- undo */

/**
 * Undo history, shared by every front-end so they cannot disagree about it.
 *
 * Snapshots are whole serialized maps. A PUD is tens of KB, so this is both
 * cheaper to reason about than journalling individual edits and incapable of
 * missing a field. Depth is capped at 64; the oldest is dropped.
 *
 * Call `pf_map_checkpoint` *before* a change, at the granularity a user would
 * expect to undo — once per brush stroke, not once per tile.
 */
PF_API pf_status pf_map_checkpoint(pf_map *map);
PF_API int pf_map_can_undo(const pf_map *map);
PF_API int pf_map_can_redo(const pf_map *map);
/** Step back one checkpoint. The current state becomes redoable. */
PF_API pf_status pf_map_undo(pf_map *map);
PF_API pf_status pf_map_redo(pf_map *map);
/** Forget all history, e.g. straight after opening or saving a file. */
PF_API void pf_map_clear_history(pf_map *map);

/* ------------------------------------------------------------- painting */

/**
 * Give the map the artwork painting should fit itself to.
 *
 * A tile group defines up to 16 variations, but a tileset only populates
 * some of them. Without artwork the painter assumes all 16 exist and happily
 * writes tiles that have no megatile, which the renderer can only draw as a
 * flat colour. Pass the tileset art here and it picks from what actually
 * exists.
 *
 * The map borrows `art` and does not take ownership; it must outlive the map
 * or be replaced. Pass NULL to go back to assuming every variation exists.
 */
PF_API pf_status pf_map_set_tileset_art(pf_map *map, const pf_tileset_art *art);

/**
 * Paint terrain with automatic edge fitting.
 *
 * `size` is the brush edge in tiles (1, 3, 5, ...). Neighbouring terrain is
 * adjusted so the result is expressible — painting water into grass grows a
 * coastline by itself. Movement data is updated in step.
 *
 * Region data is *not* recomputed here: `REGM` is a whole-map connected
 * component labelling, so call `pf_map_rebuild_regions` once when a stroke
 * ends rather than per tile.
 */
PF_API pf_status pf_map_paint_terrain(pf_map *map, int x, int y, int terrain, int size);

/** Paint terrain without edge fitting — writes solid tiles only. */
PF_API pf_status pf_map_paint_terrain_raw(pf_map *map, int x, int y, int terrain, int size);

/**
 * Paint a single corner of the grid — the smallest mark the model can hold.
 *
 * `cx` and `cy` are *corner* coordinates, not tile ones. The corner grid is one
 * larger than the tile grid in each axis, so they run 0..width and 0..height
 * inclusive, and the corner is shared by up to four tiles, all of which are
 * re-chosen. This is a quarter of what `pf_map_paint_terrain` lays at size 1,
 * which sets all four corners of one tile.
 *
 * What lands is not always a single corner: legalisation widens it where the
 * tileset has no drawing for the pair, the same way it does for any brush.
 *
 * `mix_seed` behaves as it does for `pf_map_paint_terrain_mixed`; 0 lays one
 * flat shade.
 *
 * Walls are a per-tile overlay and have no corner to sit on, so a wall terrain
 * returns PF_ERR_OUT_OF_RANGE rather than laying the ground underneath.
 *
 * Regions are not recomputed; call `pf_map_rebuild_regions` when a stroke ends.
 */
PF_API pf_status pf_map_paint_corner(pf_map *map, int cx, int cy, int terrain,
                                     uint32_t mix_seed);

/**
 * Flood a terrain into the region under a tile. Returns tiles filled, or -1.
 *
 * The region is what is reachable orthogonally with the same dominant terrain
 * as the tile clicked. A rectangle of non-zero size bounds it, which is how a
 * selection holds a fill to one lake instead of the sea it joins; pass a zero
 * size for the whole map.
 *
 * The boundary is refitted once at the end rather than per tile, so a lake of
 * ten thousand tiles costs one legalisation pass, not ten thousand.
 */
PF_API int pf_map_fill_terrain(pf_map *map, int x, int y, int terrain,
                               int rx, int ry, int rw, int rh);

/** Paint or clear a wall overlay. `kind` is 0 none, 1 human, 2 orc. */
PF_API pf_status pf_map_paint_wall(pf_map *map, int x, int y, int kind, int size);

/** Recompute `REGM`. Returns the number of regions found, or -1 on failure. */
PF_API int pf_map_rebuild_regions(pf_map *map);

/** Non-zero if the two terrains may share a tile. */
PF_API int pf_terrain_compatible(int a, int b);

/* ------------------------------------------------------------- graphics */

/**
 * Decoded tileset artwork. Loaded from the four files a tileset directory
 * holds: `<name>.cv4`, `.vx4`, `.vr4`, `.ppl`. The handle is declared next to
 * `pf_map` above, which needs it for `pf_map_set_tileset_art`.
 */

/**
 * @param dir directory holding the tileset folders, e.g. ".../art/bgs/"
 * @param tileset `ERA ` value 0-3; picks the folder name
 */
PF_API pf_tileset_art *pf_tileset_art_open(const char *dir, int tileset, pf_status *status);

/**
 * Decode a tileset from buffers instead of a directory.
 *
 * The web build has no filesystem to point at, so it reads the four files
 * itself — upload, File System Access, fetch — and passes the bytes. The
 * buffers are copied; the caller may free them on return.
 */
PF_API pf_tileset_art *pf_tileset_art_open_memory(const uint8_t *cv4, size_t cv4_len,
                                                  const uint8_t *vx4, size_t vx4_len,
                                                  const uint8_t *vr4, size_t vr4_len,
                                                  const uint8_t *ppl, size_t ppl_len,
                                                  pf_status *status);
PF_API void pf_tileset_art_free(pf_tileset_art *art);

/** Number of 32x32 megatiles. */
PF_API int pf_tileset_art_megatile_count(const pf_tileset_art *art);

/** How busy a megatile is, 0-100. Only meaningful within one tile group. */
PF_API int pf_tileset_art_detail(const pf_tileset_art *art, int megatile);
/** Megatile id for a tile value, or -1 when the tile has no artwork. */
PF_API int pf_tileset_art_megatile_for(const pf_tileset_art *art, uint16_t tile);

/**
 * Whether a megatile is blank.
 *
 * A tileset leaves the variations it does not use as a valid megatile id whose
 * every pixel is nothing, which draws as a black square. So "has a megatile"
 * is not the same as "can be drawn", and both have to be asked.
 */
PF_API int pf_tileset_art_is_blank(const pf_tileset_art *art, int megatile);

/**
 * Rasterise a megatile into 32x32 packed RGBA (0xAABBGGRR, little-endian).
 * `out` must hold at least 1024 uint32 values.
 */
PF_API pf_status pf_tileset_art_draw(const pf_tileset_art *art, int megatile,
                                     uint32_t *out, int stride);

/**
 * Advance the water animation.
 *
 * Warcraft II animates water by cycling palette entries rather than by holding
 * extra frames. Indices 38 to 47 are used by water and by nothing else in all
 * four tilesets, and in each they run one rise and fall of a wave, so rotating
 * them walks the highlight across the sea. Everything drawn afterwards uses
 * the rotated palette; the artwork is untouched.
 *
 * `phase` is taken modulo the cycle length, so a caller can simply count up.
 */
PF_API void pf_tileset_art_set_water_phase(pf_tileset_art *art, int phase);

/** How many steps the water animation has. */
PF_API int pf_tileset_art_water_cycle(void);

/** Mean colour of a megatile as packed RGBA — for minimaps and zoomed-out views. */
PF_API uint32_t pf_tileset_art_average(const pf_tileset_art *art, int megatile);

/** The tileset's 256-entry palette as packed RGBA. Owned by `art`. */
PF_API const uint32_t *pf_tileset_art_palette(const pf_tileset_art *art);

/** A decoded unit sprite (`.grp`). */
typedef struct pf_sprite pf_sprite;

/**
 * Load the sprite for a unit type.
 *
 * @param dir directory holding the unit folders, e.g. ".../art/unit/"
 * @param unit_id 0..109
 * @param tileset selects the tileset-specific variant when one exists
 */
PF_API pf_sprite *pf_sprite_open(const char *dir, int unit_id, int tileset, pf_status *status);
/**
 * Decode a `.grp` from bytes. The web client has no filesystem, so it resolves
 * the path with `pf_sprite_path` and fetches the file itself.
 */
PF_API pf_sprite *pf_sprite_open_memory(const uint8_t *data, size_t len, pf_status *status);

/**
 * Relative path (no extension) of the `.grp` a unit uses on a tileset, e.g.
 * "human/thall". Writes up to `cap` bytes including the terminator and returns
 * the full length; pass NULL/0 to query the length. Empty for unused slots.
 */
PF_API int pf_sprite_path(int unit_id, int tileset, char *out, int cap);

/** What a unit is heard doing. */
typedef enum pf_sound_kind {
  PF_SOUND_READY = 0,    /**< it has appeared */
  PF_SOUND_SELECTED = 1  /**< it is being pointed at */
} pf_sound_kind;

/**
 * Archive path of a unit's sound, e.g. "gamesfx/knight/knready.wav".
 *
 * Same convention as `pf_sprite_path`: writes up to `cap` bytes including the
 * terminator, returns the full length, takes NULL/0 as a length query.
 *
 * A unit the game gave no voice falls back to `sfx/button.wav`, so an edit is
 * always audible without the editor inventing speech Blizzard left unrecorded.
 * The core resolves the path and nothing more; reading and playing it is the
 * host's.
 *
 * `salt` picks among the several selection lines most units have. Which one is
 * the caller's policy, and this stays a pure function of its arguments so it
 * can be tested against the archives.
 */
PF_API int pf_unit_sound_path(int unit_id, int kind, int salt, char *out, int cap);

PF_API void pf_sprite_free(pf_sprite *sprite);

PF_API int pf_sprite_width(const pf_sprite *sprite);
PF_API int pf_sprite_height(const pf_sprite *sprite);
PF_API int pf_sprite_frame_count(const pf_sprite *sprite);

/**
 * Tileset artwork straight from a data source, so the caller never has to know
 * which four files a tileset is made of or where they live.
 */
PF_API pf_tileset_art *pf_tileset_art_open_source(const pf_data_source *source,
                                                  int tileset, pf_status *status);

/** A unit's sprite from a data source, falling back to the forest original. */
PF_API pf_sprite *pf_sprite_open_source(const pf_data_source *source, int unit_id,
                                        int tileset, pf_status *status);


/**
 * Rasterise a frame into packed RGBA on the sprite's full canvas, tinted for
 * `owner`. Pixels the sprite does not cover are left as zero (transparent),
 * so clear `out` first. `out` must hold `width * height` uint32 values.
 *
 * Frame 0 is the south-facing idle for mobile units and the completed state
 * for buildings, which is what an editor wants in both cases.
 */
PF_API pf_status pf_sprite_draw(const pf_sprite *sprite, int frame, int owner,
                                const pf_tileset_art *art, uint32_t *out);

/* ------------------------------------------------------------------ view */

/**
 * Where a window is looking at a map.
 *
 * Scroll, zoom, fit, and turning a point into a tile — arithmetic over a map
 * size, a viewport size and a device pixel ratio, and every client needs the
 * same answers or the same map scrolls differently in each.
 *
 * An opaque handle rather than a struct the caller reads, because the fields
 * mix ints and doubles and every binding would then depend on the layout.
 * Sizes are device pixels; the scroll is in tiles and deliberately fractional.
 */
typedef struct pf_view pf_view;

PF_API pf_view *pf_view_create(void);
PF_API void pf_view_free(pf_view *view);

PF_API void pf_view_set_map(pf_view *view, int width, int height);
/** `dpr` is 1 on a plain monitor and 1.5 or 2 on the ones people have. */
PF_API void pf_view_set_viewport(pf_view *view, int width, int height, double dpr);

PF_API int pf_view_zoom(const pf_view *view);
PF_API void pf_view_set_zoom(pf_view *view, int zoom);
PF_API double pf_view_scroll_x(const pf_view *view);
PF_API double pf_view_scroll_y(const pf_view *view);
PF_API void pf_view_set_scroll(pf_view *view, double x, double y);

/**
 * Whether this is a fit rather than a zoom somebody chose.
 *
 * A fit is a statement about the window, so it has to be redone when the window
 * changes size, or dragging the corner leaves the map floating in a third of
 * the space. A chosen zoom is left alone.
 */
PF_API int pf_view_fitted(const pf_view *view);

/**
 * Device pixels per tile, always a whole number.
 *
 * The artwork is pixel art. At 110% a tile is 35.2 device pixels, so every tile
 * would land on a fraction and the whole map would soften. The rendered size
 * rounds while the percentage stays on its ladder.
 */
PF_API int pf_view_tile_px(const pf_view *view);

/** Pull the scroll back inside the map. Every route that moves it calls this. */
PF_API void pf_view_clamp(pf_view *view);

/** Tile under a point in the viewport. 0 when it lands outside the map. */
PF_API int pf_view_tile_at(const pf_view *view, int px, int py, int *tx, int *ty);

/** Zoom to `zoom`, keeping the tile under (px, py) where it is. Clamps after. */
PF_API void pf_view_zoom_about(pf_view *view, int zoom, int px, int py);
/** One step in (`dir` > 0) or out, about a point. Uses the core's ladder. */
PF_API void pf_view_zoom_step(pf_view *view, int dir, int px, int py);

/**
 * Fit the whole map and centre it. 0 when the viewport is not real yet.
 *
 * `only_to_shrink` leaves a map small enough to see at its own size: 32x32
 * blown up to fill a monitor is not a favour.
 *
 * The fit is worked out in exact pixels, then the tile size rounds and the zoom
 * snaps to the ladder - either of which can come back a couple of percent
 * larger than the window - so it steps down until it really fits.
 */
PF_API int pf_view_fit(pf_view *view, int only_to_shrink);
/** Fit a tile rectangle, with a tile of margin round it. */
PF_API int pf_view_fit_rect(pf_view *view, int x, int y, int w, int h);
/** Centre on a tile without changing the zoom. */
PF_API void pf_view_centre_on(pf_view *view, int tx, int ty);

/** The tile region to compose: origin and size, clipped to the map. */
PF_API void pf_view_region(const pf_view *view, int *x0, int *y0, int *cols, int *rows);
/**
 * Where that region's top-left corner lands in the viewport, in whole pixels.
 *
 * Whole, because the scroll is in fractional tiles: without rounding, the map
 * sits a fraction of a pixel off its own grid and every tile edge softens as
 * you pan.
 */
PF_API void pf_view_origin(const pf_view *view, int x0, int y0, int *ox, int *oy);

/* -------------------------------------------------------- editing policy */

/**
 * Settings that decide how editing feels, rather than how it looks.
 *
 * Each of these was measured or tuned against the shipped maps and then lived
 * in one client, where a second client would have re-derived it and disagreed.
 * None of them needs a window to describe, so none of them belongs in a client.
 */

/**
 * The zoom ladder: the percentages one press of zoom-in or zoom-out lands on.
 *
 * `pf_zoom_level_count` entries, ascending, 25% to 800%. A list rather than a
 * step size, because what a step should be worth is not constant: 25% to 33%
 * is a third as much map again, while 200% to 210% is barely visible. Index
 * them rather than assuming the arithmetic — there is none.
 */
PF_API int pf_zoom_level_count(void);
PF_API int pf_zoom_level(int index);
PF_API int pf_zoom_min(void);
PF_API int pf_zoom_max(void);
/** The rung one step in (`dir` > 0) or out from `zoom`, clamped to the ladder. */
PF_API int pf_zoom_step(int zoom, int dir);
/**
 * Snap an arbitrary zoom onto the nearest rung, so any route lands on one.
 *
 * `pf_view_fit` is the exception and may sit between two: the ladder has a
 * floor and a fit does not, and the largest maps do not fit at 25%.
 */
PF_API int pf_zoom_snap(int zoom);

/**
 * The brush size meaning one corner of the grid rather than a whole tile.
 *
 * Zero because it is smaller than one tile, and it is the first rung of the
 * ladder below. A caller that treats brush sizes as a count of tiles should
 * test for it: `pf_map_paint_terrain` reads 0 as "no brush at all" and clamps
 * it to 1, so a corner brush goes through `pf_map_paint_corner` instead.
 */
#define PF_BRUSH_SIZE_CORNER 0

/**
 * Brush sizes, in tiles. All odd from 1 up, so a brush has a centre tile to aim
 * with; the first rung is PF_BRUSH_SIZE_CORNER, which is smaller than a tile.
 *
 * `pf_brush_size_count` entries, ascending; index them rather than assuming the
 * arithmetic, which is what lets a slider step through them evenly.
 */
PF_API int pf_brush_size_count(void);
PF_API int pf_brush_size(int index);

/**
 * The spray can: how a held scatter brush builds up.
 *
 * A scatter brush that lays its whole density the instant you click gives no
 * control but the size; a can of spray paint starts thin and darkens while you
 * hold it. The tick is about eleven puffs a second - slow enough that a tap is
 * one or two, fast enough to feel continuous - and the ramp is roughly how long
 * a hold lasts before it stops feeling like a click.
 */
PF_API int pf_spray_tick_ms(void);
PF_API int pf_spray_ramp_ms(void);
/** Density of one puff after `held_ms`, from the floor up to `full`. */
PF_API double pf_spray_density(int held_ms, double full);

/**
 * Score a name against a typed query, the Quicksilver way.
 *
 * "gt" finds Grunt, "ftm" finds Footman, "dk" finds Death Knight. With 110
 * units the ranking is the whole job — a match that is merely possible is not
 * interesting. Initials weigh far more than letters buried inside a word, a run
 * of adjacent letters beats the same letters scattered, and there is mild,
 * bounded pressure towards the front of the name.
 *
 * Case-insensitive, and surrounding space in the query is ignored. Here rather
 * than in a client because two of them want it and there is one right answer.
 *
 * @return higher is better; -1 when the query is not a subsequence of the name.
 *   An empty query matches everything at 0.
 */
PF_API int pf_name_score(const char *query, const char *name);

/**
 * Rank unit ids by how well their names match, dropping the ones that do not.
 *
 * Ties break on the shorter name and then alphabetically, so the order is
 * stable between keystrokes — a list that reshuffles under the cursor is worse
 * than one that ranks imperfectly. An empty query leaves `ids` in the order it
 * was given, because the caller's own grouping (by race, then by kind) is more
 * use than sorting a hundred units by name length.
 *
 * Filters in place. @return how many survived.
 */
PF_API int pf_unit_name_filter(const char *query, int *ids, int count);

/**
 * The terrain brushes a palette offers, in order.
 *
 * A brush is a terrain class; what it is *called* depends on the tileset, so
 * ask pf_terrain_name. `pf_brush_shade` is 1 for the light member of a
 * light/dark pair, -1 for the dark one, and 0 for a class with only one shade.
 */
PF_API int pf_brush_count(void);
PF_API int pf_brush_terrain(int index);
PF_API int pf_brush_shade(int index);

/**
 * The other drawing of a terrain that comes in two, or the terrain itself.
 *
 * Ground, water and coast each have a light and a dark member that the game
 * treats as one terrain; forest, rock and the two walls do not. A palette that
 * offers one cell per terrain with a Light/Dark switch beside it needs this to
 * turn the switch into something to paint with, and it is the same pairing the
 * shade pass mixes across.
 */
PF_API int pf_terrain_other_shade(int terrain);

/**
 * Which of a terrain's two drawings a mixed stroke lays at a tile.
 *
 * Answers `terrain` itself for one that has no second drawing, so a caller
 * needs no test for which those are.
 *
 * The same coherent noise `pf_map_shade_stroke` mixes with, asked one tile at a
 * time, which is what lets a brush lay a mixture *as it paints*. A pass after
 * the fact reaches every tile in the stroke's bounding box and re-picks tiles
 * the stroke had already settled; asking per tile has neither problem.
 *
 * Deterministic in the tile and the seed, so a stroke drawn over its own tracks
 * lays the same shades again.
 */
PF_API int pf_shade_at(int terrain, int x, int y, uint32_t seed);

/**
 * Paint, laying the mixture of a terrain's two drawings rather than one.
 *
 * `mix_seed` of 0 is exactly `pf_map_paint_terrain`. Non-zero, every corner of
 * the brush takes `pf_shade_at` — per corner, not per stroke, because a brush
 * is up to 32 tiles a side and one shade over all of them is the flat square
 * mixing exists to avoid.
 *
 * The alternative this replaced, painting flat and re-shading, needs a mask of
 * what the stroke covered, reaches tiles it did not, and re-picks tiles it had
 * already settled.
 */
PF_API pf_status pf_map_paint_terrain_mixed(pf_map *map, int x, int y,
                                            int terrain, int size,
                                            uint32_t mix_seed);

/** What fraction of its footprint a scatter brush paints, before the ramp. */
PF_API double pf_scatter_density(void);

/* ------------------------------------------------------------- rendering */

/**
 * Composing a map into pixels.
 *
 * This lived in each client and was 411 lines of JavaScript in the web one,
 * which meant every new client wrote it again. Nothing in it is presentation:
 * which tile goes where, which way a unit faces, what a movement value is
 * coloured, where the heavy grid lines fall. So it lives here, and a client's
 * job is to put the buffer on screen.
 *
 * Output is packed 0xAABBGGRR - what a canvas `ImageData` wants, what a Windows
 * DIB wants, and what `pf_tileset_art_draw` already produces. Nobody swizzles.
 */

/** Which units to draw. Buildings win over their domain. */
typedef enum pf_unit_filter {
  PF_UNITS_ALL = 0,
  PF_UNITS_NONE = 1,
  PF_UNITS_GROUND = 2,   /**< matches pf_unit_draw_class 0 */
  PF_UNITS_AIR = 3,      /**< 1 */
  PF_UNITS_BUILDINGS = 4 /**< 2 */
} pf_unit_filter;

/** A data layer tinted over the terrain, under the units. */
typedef enum pf_overlay {
  PF_OVERLAY_NONE = 0,
  PF_OVERLAY_MOVEMENT = 1,
  PF_OVERLAY_REGIONS = 2,
  PF_OVERLAY_TILES = 3
} pf_overlay;

/**
 * Sprites to draw units with, keyed by unit type and owner.
 *
 * Owner matters because a footman's armour is the player's colour in the
 * artwork itself, so the same unit for two players is two decodes. A client
 * fills this as it loads and the renderer only reads it.
 */
typedef struct pf_sprite_set pf_sprite_set;
PF_API pf_sprite_set *pf_sprite_set_create(void);
PF_API void pf_sprite_set_free(pf_sprite_set *set);
/** Takes ownership of `sprite`, replacing whatever was at (unit_id, owner). */
PF_API pf_status pf_sprite_set_add(pf_sprite_set *set, int unit_id, int owner,
                                   pf_sprite *sprite);
/** Whether a sprite is already loaded, so a client can avoid decoding twice. */
PF_API int pf_sprite_set_has(const pf_sprite_set *set, int unit_id, int owner);

typedef struct pf_render_options {
  int x0, y0;                    /**< top-left tile of the region            */
  int cols, rows;                /**< its size in tiles                      */
  const pf_tileset_art *art;     /**< null draws flat terrain colours        */
  const pf_sprite_set *sprites;  /**< null draws no units at all             */
  int overlay;                   /**< pf_overlay                             */
  int unit_filter;               /**< pf_unit_filter                         */
  int grid;                      /**< one-pixel tile grid, heavier every 8th */
  int mark_special;              /**< box resources and start locations      */
  int vary_facing;               /**< 0 makes every unit face the same way   */
  int placeholders;              /**< outline units with no sprite loaded    */
} pf_render_options;

/**
 * Compose a tile region into `out` at the artwork's native 32 px per tile.
 *
 * Returns the number of pixels written (`cols * rows * 1024`), or -1 when
 * `capacity` is short of that. Pass a null `out` with zero capacity to ask how
 * much is needed.
 */
PF_API int pf_map_compose_region(const pf_map *map, const pf_render_options *opts,
                                 uint32_t *out, size_t capacity);

/**
 * Encode RGBA pixels as a PNG. Free the result with `pf_buffer_free`.
 *
 * Every client renders into the same RGBA buffer and every client then wants a
 * file someone can look at — a headless capture for CI, a bug report, a
 * screenshot over SSH. Doing that per platform meant CoreGraphics on macOS,
 * Node's zlib on the web and nothing at all on Windows, so it lives here now.
 *
 * Returns NULL and sets `out_len` to 0 when the dimensions are not positive.
 */
PF_API uint8_t *pf_png_encode(const uint32_t *rgba, int width, int height,
                              size_t *out_len);

/**
 * Compose the whole map at one pixel per tile, with units at their footprint.
 *
 * Returns pixels written (`width * height`), or -1 when `capacity` is short.
 */
PF_API int pf_map_compose_minimap(const pf_map *map, const pf_tileset_art *art,
                                  uint32_t *out, size_t capacity);

/**
 * A terrain class as a flat 0xRRGGBB colour in a tileset.
 *
 * Measured from the artwork; see overrides/flat_colours.cpp. What the renderer
 * falls back to with no install present, and what a client should tint a
 * preview or a paste ghost with so the two agree.
 */
PF_API uint32_t pf_terrain_flat_colour(int terrain, int tileset);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PUDFORGE_H */
