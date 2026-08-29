// C ABI implementation.
//
// Everything crossing the boundary is POD; no exceptions escape. The opaque
// handles wrap the C++ objects plus whatever per-map editing state the front
// end shouldn't have to know about (the corner grid and tile index).

#include "pudforge/pudforge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <numeric>
#include <string>

#include "art.hpp"
#include "ai_script.hpp"
#include "constants.hpp"
#include "mpq.hpp"
#include "noise.hpp"
#include "png.hpp"
#include "pud.hpp"
#include "tbl.hpp"
#include "terrain.hpp"
#include "view.hpp"
#include "terrain_tables.hpp"

namespace {

pf_status to_c(pf::Status s) {
  switch (s) {
    case pf::Status::Ok: return PF_OK;
    case pf::Status::NotAPud: return PF_ERR_NOT_A_PUD;
    case pf::Status::Malformed: return PF_ERR_MALFORMED;
    case pf::Status::UnsupportedSize: return PF_ERR_UNSUPPORTED_SIZE;
    case pf::Status::OutOfRange: return PF_ERR_OUT_OF_RANGE;
  }
  return PF_ERR_MALFORMED;
}

void set_status(pf_status* out, pf_status value) {
  if (out) *out = value;
}

}  // namespace

/// A map plus the editing state that belongs with it.
struct pf_map {
  std::unique_ptr<pf::Map> map;
  /// Corner terrains, kept alive across strokes so painting is incremental.
  std::unique_ptr<pf::CornerGrid> corners;
  /// Corner-quadruple to tile lookup. Built lazily.
  std::unique_ptr<pf::TileIndex> index;
  /// The three escape hatches from the placement rules, all off by default
  /// because the common case is a mistake rather than an intention.
  ///
  /// On the map rather than in a client because every way of putting a unit
  /// down has to obey them, and paste happens entirely inside the core — when
  /// these lived in the Windows client, a pasted fragment dropped units on top
  /// of units the editor would never have let you place.
  bool allow_illegal_placement = false;
  bool allow_stacked_units = false;
  bool allow_edge_placement = false;

  /// Which variations of a tile group painting may choose. PUDDraft offered
  /// the same choice as Plain / Random / Filler on its Map Brush menu.
  int variation_policy = PF_VARIATION_ANY;

  /// Artwork used to filter tile variations, borrowed and may be null.
  ///
  /// A tile group defines at most 16 variations but a tileset populates only
  /// some of them, so without this the editor paints tiles that cannot be drawn.
  const pf::TilesetArt* art = nullptr;

  pf::CornerGrid& grid() {
    if (!corners) corners = std::make_unique<pf::CornerGrid>(pf::CornerGrid::from_map(*map));
    return *corners;
  }
  pf::TileIndex& tiles() {
    if (!index) {
      index = art ? std::make_unique<pf::TileIndex>(&art_has_tile, this)
                  : std::make_unique<pf::TileIndex>();
    }
    return *index;
  }

  /// Undo history as whole-map snapshots.
  ///
  /// A PUD is tens of KB, so snapshotting the serialized file is far harder to
  /// get wrong than journalling individual edits and cannot miss a field.
  std::vector<std::vector<uint8_t>> undo_stack;
  std::vector<std::vector<uint8_t>> redo_stack;
  static constexpr size_t kMaxUndo = 64;

  /// Swap in a snapshot, dropping the caches derived from the old map.
  bool restore(const std::vector<uint8_t>& snapshot) {
    pf::Status s = pf::Status::Ok;
    pf::Map* parsed = pf::Map::parse(snapshot.data(), snapshot.size(), s);
    if (!parsed) return false;
    map.reset(parsed);
    corners.reset();
    index.reset();
    return true;
  }

  static int art_has_tile(uint16_t tile, void* ctx) {
    const auto* self = static_cast<const pf_map*>(ctx);
    const int policy = self->variation_policy;
    const pf::TilesetArt* a = self->art;
    const int m = a->megatile_for(tile);
    // A blank megatile is a valid index that draws as a black square, so it
    // is no more paintable than a missing one.
    if (m < 0 || a->is_blank(m)) return 0;

    // Which variations count as decorated is structural, not a matter of how
    // busy the drawing looks: judging it by pixel detail threw away one of the
    // two drawings of the rock-to-coast edge, leaving every tile down a cliff
    // face identical. Solid groups are tabulated in
    // overrides/tile_variations.cpp; boundary classes have no such layout, so
    // their plain variations are the unbroken run from 0 before a blank slot.
    const int base = tile & 0xfff0;
    int run = pf::plain_variation_count(tile >> 4);
    if (run == 0) {
      while (run < 16) {
        const int other = a->megatile_for(uint16_t(base | run));
        if (other < 0 || a->is_blank(other)) break;
        run++;
      }
    }
    const bool is_plain = (tile & 0xf) < run;
    if (policy == PF_VARIATION_PLAIN) return is_plain ? 1 : 0;

    // The two remaining settings each take a share of the decorated drawings:
    //
    //   Mixed     seven parts plain to three, so decoration reads as something
    //             scattered over ground rather than as the ground itself
    //   Detailed  decorated drawings and nothing else
    //
    int plain_count = 0, fancy_count = 0;
    for (int v = 0; v < 16; v++) {
      const int other = a->megatile_for(uint16_t(base | v));
      if (other < 0 || a->is_blank(other)) continue;
      if (v < run) plain_count++; else fancy_count++;
    }
    // Neither setting can mean "no tile at all", or the edge could not be drawn.
    // A group the tileset gave only one kind of drawing takes what it has: most
    // boundary classes have no decorated variation, and Detailed over a coast
    // has to lay coast.
    if (fancy_count == 0 || plain_count == 0) return 1;

    // Detailed excludes plain ground outright, the way Plain excludes decoration.
    if (policy == PF_VARIATION_DECORATED) return is_plain ? 0 : 1;

    // Mixed's share is by weight rather than by count, because a group has three
    // or four plain drawings and however many decorated ones the tileset gave
    // it, so picking evenly over the drawings would let the tileset set the
    // ratio. Cross-multiplied, so each side's slots total its share whatever the
    // two counts are: every plain drawing takes `7 * fancy_count` slots and every
    // decorated one `3 * plain_count`.
    const int plain_weight = 7 * fancy_count;
    const int fancy_weight = 3 * plain_count;
    // The index stores one slot per unit of weight, so keep the pair in lowest
    // terms — only the ratio means anything, and the common factor is vector
    // entries in every group of every tileset.
    const int common = std::gcd(plain_weight, fancy_weight);
    return (is_plain ? plain_weight : fancy_weight) / common;
  }
};

struct pf_tileset_art {
  std::unique_ptr<pf::TilesetArt> art;
};

struct pf_data_source {
  pf::DataSource source;
};

struct pf_strings {
  pf::Tbl tbl;
};

namespace pf {

/// The table a host has asked the core to describe things with, or null.
///
/// Process-wide because `pf_unit_name(id)` takes no context and hangs off
/// nothing — it is a fact about the game, not about a map. The core does not
/// own it: the host installs it once at startup and keeps it alive, the same
/// arrangement the artwork already has.
pf_strings*& strings_slot() {
  static pf_strings* installed = nullptr;
  return installed;
}
const pf_strings* installed_strings() { return strings_slot(); }

}  // namespace pf

struct pf_ai_scripts {
  std::unique_ptr<pf::AiScripts> scripts;
};

struct pf_sprite {
  std::unique_ptr<pf::Sprite> sprite;
};

// ------------------------------------------------------------- game data

// ------------------------------------------------------------- one archive

struct pf_mpq {
  std::unique_ptr<pf::MpqArchive> archive;
};

pf_mpq* pf_mpq_open_memory(const uint8_t* bytes, size_t length, pf_status* status) {
  if (!bytes || !length) {
    if (status) *status = PF_ERR_INVALID_ARG;
    return nullptr;
  }
  pf::Status s = pf::Status::Ok;
  pf::MpqArchive* archive =
      pf::MpqArchive::open_bytes(std::vector<uint8_t>(bytes, bytes + length), s);
  if (!archive) {
    if (status) *status = to_c(s);
    return nullptr;
  }
  if (status) *status = PF_OK;
  pf_mpq* out = new pf_mpq();
  out->archive.reset(archive);
  return out;
}

void pf_mpq_free(pf_mpq* mpq) { delete mpq; }

int pf_mpq_file_count(const pf_mpq* mpq) {
  return mpq ? int(mpq->archive->files().size()) : 0;
}

const char* pf_mpq_file_name(const pf_mpq* mpq, int index) {
  if (!mpq) return nullptr;
  const auto& files = mpq->archive->files();
  if (index < 0 || index >= int(files.size())) return nullptr;
  return files[size_t(index)].c_str();
}

uint8_t* pf_mpq_read(const pf_mpq* mpq, const char* name, size_t* length) {
  if (length) *length = 0;
  if (!mpq || !name) return nullptr;
  std::vector<uint8_t> bytes;
  if (!mpq->archive->read(name, bytes) || bytes.empty()) return nullptr;
  uint8_t* out = static_cast<uint8_t*>(std::malloc(bytes.size()));
  if (!out) return nullptr;
  std::memcpy(out, bytes.data(), bytes.size());
  if (length) *length = bytes.size();
  return out;
}

pf_data_source* pf_data_source_create(void) { return new pf_data_source(); }

void pf_data_source_free(pf_data_source* source) { delete source; }

int pf_data_source_add_directory(pf_data_source* source, const char* dir) {
  if (!source || !dir) return 0;
  return source->source.add_directory(dir);
}

int pf_data_source_add_archive(pf_data_source* source, const char* path) {
  if (!source || !path) return 0;
  return source->source.add_archive(path) ? 1 : 0;
}

void pf_data_source_add_files(pf_data_source* source, const char* dir) {
  if (source && dir) source->source.add_files(dir);
}

uint8_t* pf_data_source_read(const pf_data_source* source, const char* name,
                             size_t* out_len) {
  if (out_len) *out_len = 0;
  if (!source || !name) return nullptr;
  std::vector<uint8_t> bytes;
  if (!source->source.read(name, bytes) || bytes.empty()) return nullptr;
  auto* out = static_cast<uint8_t*>(std::malloc(bytes.size()));
  if (!out) return nullptr;
  std::memcpy(out, bytes.data(), bytes.size());
  if (out_len) *out_len = bytes.size();
  return out;
}

pf_tileset_art* pf_tileset_art_open_source(const pf_data_source* source, int tileset,
                                           pf_status* status) {
  if (!source || tileset < 0 || tileset > 3) {
    set_status(status, PF_ERR_INVALID_ARG);
    return nullptr;
  }
  const std::string name = pf::kTilesetDirs[tileset];
  const std::string base = "art\\bgs\\" + name + "\\" + name;
  std::vector<uint8_t> cv4, vx4, vr4, ppl;
  const bool ok = source->source.read(base + ".cv4", cv4) &&
                  source->source.read(base + ".vx4", vx4) &&
                  source->source.read(base + ".vr4", vr4) &&
                  source->source.read(base + ".ppl", ppl);
  if (!ok) { set_status(status, PF_ERR_IO); return nullptr; }

  pf::TilesetArt* art = pf::TilesetArt::open_bytes(cv4, vx4, vr4, ppl);
  if (!art) { set_status(status, PF_ERR_MALFORMED); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_tileset_art();
  handle->art.reset(art);
  return handle;
}

pf_sprite* pf_sprite_open_source(const pf_data_source* source, int unit_id, int tileset,
                                 pf_status* status) {
  if (!source) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  // The tileset-specific variant usually does not exist; fall back to the
  // forest original, which is the same order every other loader here uses.
  for (const int era : {tileset, 0}) {
    const std::string rel = pf::sprite_path_for(unit_id, era);
    if (rel.empty()) continue;
    std::vector<uint8_t> bytes;
    if (!source->source.read("art\\unit\\" + rel + ".grp", bytes)) continue;
    pf_sprite* sprite = pf_sprite_open_memory(bytes.data(), bytes.size(), status);
    if (sprite) return sprite;
  }
  set_status(status, PF_ERR_IO);
  return nullptr;
}

// ---------------------------------------------------------------- status

const char* pf_status_message(pf_status status) {
  switch (status) {
    case PF_OK: return "ok";
    case PF_ERR_INVALID_ARG: return "invalid argument";
    case PF_ERR_NOT_A_PUD: return "not a PUD file";
    case PF_ERR_MALFORMED: return "malformed PUD file";
    case PF_ERR_UNSUPPORTED_SIZE: return "unsupported map size";
    case PF_ERR_OUT_OF_RANGE: return "value out of range";
    case PF_ERR_IO: return "file could not be read or written";
    case PF_ERR_OUT_OF_MEMORY: return "out of memory";
  }
  return "unknown error";
}

const char* pf_version(void) { return "0.2.0"; }

// ------------------------------------------------------------- constants

const char* pf_unit_name(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return nullptr;
  // The game's own table when a host has installed one, so a localised or
  // modded install is described in its own words. Unit ids run one behind the
  // table, and a blank entry means the game does not name that unit.
  if (const pf_strings* s = pf::installed_strings()) {
    const std::string& named = s->tbl.at(pf::kFirstUnitString + unit_id);
    if (!named.empty()) return named.c_str();
  }
  return pf::kUnits[unit_id].name;
}

char pf_unit_race(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return 'n';
  return pf::kUnits[unit_id].race;
}

int pf_unit_is_unused(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return 1;
  return pf::kUnits[unit_id].unused ? 1 : 0;
}

const char* pf_upgrade_name(int upgrade_id) {
  if (upgrade_id < 0 || upgrade_id >= pf::kUpgradeCount) return nullptr;
  // Flattened, because the table stores these as command-button captions broken
  // over the three lines a button has. The wording is the game's and worth
  // keeping — it calls 12 to 15 "Ship Attack" where this repository guessed
  // "Ship Cannon" — but a list row is one line.
  if (const pf_strings* s = pf::installed_strings()) {
    const std::string& named = s->tbl.flat(pf::kFirstUpgradeString + upgrade_id);
    if (!named.empty()) return named.c_str();
  }
  return pf::kUpgrades[upgrade_id];
}

namespace {

bool has_flag(int unit_id, int bit) {
  return ((pf_unit_flags(unit_id) >> bit) & 1) != 0;
}

/// Hero for the purpose of grouping and selecting. `UDTA` bit 23 marks only
/// five of the fifteen named characters, so the rest come from a list — see
/// overrides/named_heroes.cpp. Additive: a unit the game flags is always a hero.
bool is_hero(int unit_id) {
  return has_flag(unit_id, 23) || pf::unit_is_named_hero(unit_id);
}

const char* const kGroupNames[PF_GROUP_COUNT] = {
    "Buildings", "Land units", "Flying units", "Naval units", "Critters",
    "Heroes", "Gold mines and oil patches", "Start locations", "Spellcasters",
    "Towers",
};

}  // namespace

int pf_unit_category(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return PF_CATEGORY_SPECIAL;
  if (is_hero(unit_id)) return PF_CATEGORY_HERO;
  // The ballista and the catapult set no movement flag at all, so the flags
  // alone leave them uncategorised. They walk, and belong with the land units.
  if (unit_id == 0x04 || unit_id == 0x05) return PF_CATEGORY_LAND;
  // Before buildings: a gold mine carries the building flag but is scenery.
  if (has_flag(unit_id, 21) || has_flag(unit_id, 22)) return PF_CATEGORY_SPECIAL;
  if (has_flag(unit_id, 5)) return PF_CATEGORY_BUILDING;
  if (has_flag(unit_id, 1)) return PF_CATEGORY_AIR;
  if (has_flag(unit_id, 3)) return PF_CATEGORY_WATER;
  if (has_flag(unit_id, 0)) return PF_CATEGORY_LAND;
  return PF_CATEGORY_SPECIAL;
}

int pf_unit_facing_count(int unit_id) {
  // Five is what the artwork holds: the .grp frames run south, south-east,
  // east, north-east, north, and the client mirrors for the western half.
  constexpr int kFacings = 5;
  const int category = pf_unit_category(unit_id);
  return (category == PF_CATEGORY_BUILDING || category == PF_CATEGORY_SPECIAL)
      ? 1 : kFacings;
}

int pf_unit_draw_class(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return 0;
  if (has_flag(unit_id, 5)) return 2;    // includes resources, which look built
  if (has_flag(unit_id, 1)) return 1;
  return 0;
}

int pf_unit_value_is_amount(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return 0;
  // Gives oil, is an oil patch, or is a gold mine.
  return has_flag(unit_id, 11) || has_flag(unit_id, 21) || has_flag(unit_id, 22) ? 1 : 0;
}

int pf_unit_resource(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return PF_RESOURCE_NONE;
  // Flag 22 is the gold mine; 11 gives oil and 21 is the oil patch. The oil
  // well carries 11 without being scenery, which is exactly the case a list of
  // ids kept getting wrong.
  if (has_flag(unit_id, 22)) return PF_RESOURCE_GOLD;
  if (has_flag(unit_id, 11) || has_flag(unit_id, 21)) return PF_RESOURCE_OIL;
  return PF_RESOURCE_NONE;
}

int64_t pf_resource_amount(int value) { return int64_t(value) * 2500; }

int pf_resource_value(int64_t amount) {
  if (amount <= 0) return 0;
  // Half a step up before dividing, so 41,000 lands on 40,000 and 41,300 on
  // 42,500 rather than everything truncating downwards.
  const int64_t steps = (amount + 1250) / 2500;
  return int(steps > 0xFFFF ? 0xFFFF : steps);
}

int pf_unit_default_value(int unit_id) {
  return pf::unit_default_value(unit_id, pf_unit_resource(unit_id));
}

int pf_unit_icon(int unit_id) { return pf::unit_icon(unit_id); }

int pf_unit_icon_count(void) { return pf::unit_icon_count(); }

int pf_unit_needs_opt_in(int unit_id) { return pf::unit_needs_opt_in(unit_id) ? 1 : 0; }

int pf_unit_never_offered(int unit_id) { return pf::unit_never_offered(unit_id) ? 1 : 0; }

const char* pf_terrain_name(int terrain, int tileset) {
  return pf::terrain_name(terrain, tileset);
}

int pf_unit_needs_opt_in_count(void) { return pf::unit_opt_in_count(); }

int pf_unit_counterpart(int unit_id) { return pf::unit_counterpart(unit_id); }

int pf_unit_counterpart_count(void) { return pf::unit_counterpart_count(); }

int pf_unit_named_hero_count(void) { return pf::unit_named_hero_count(); }

int pf_portrait_path(int tileset, char* out, int cap) {
  if (tileset < 0 || tileset > 3 || !out || cap <= 0) return 0;
  // Same per-tileset prefix scheme the unit sprites use.
  const std::string path = std::string("art\\unit\\portrait\\") +
                           pf::kTilesetSpritePrefix[tileset] + "port.grp";
  // The forest set is named "portrait.grp" rather than "port.grp".
  const std::string full = tileset == 0 ? "art\\unit\\portrait\\portrait.grp" : path;
  if (int(full.size()) + 1 > cap) return 0;
  std::memcpy(out, full.c_str(), full.size() + 1);
  return int(full.size());
}

int pf_unit_in_group(int unit_id, int group) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return 0;
  const bool resource = has_flag(unit_id, 21) || has_flag(unit_id, 22);
  switch (group) {
    case PF_GROUP_BUILDINGS: return has_flag(unit_id, 5) && !resource ? 1 : 0;
    // Land means the army: a critter is scenery that happens to walk.
    case PF_GROUP_LAND:
      return has_flag(unit_id, 0) && !has_flag(unit_id, 5) && !has_flag(unit_id, 4) ? 1 : 0;
    case PF_GROUP_AIR: return has_flag(unit_id, 1) && !has_flag(unit_id, 5) ? 1 : 0;
    case PF_GROUP_WATER: return has_flag(unit_id, 3) && !has_flag(unit_id, 5) ? 1 : 0;
    case PF_GROUP_CRITTERS: return has_flag(unit_id, 4) ? 1 : 0;
    case PF_GROUP_HEROES: return is_hero(unit_id) ? 1 : 0;
    case PF_GROUP_RESOURCES: return resource ? 1 : 0;
    case PF_GROUP_START_LOCATIONS: return unit_id == 94 || unit_id == 95 ? 1 : 0;
    case PF_GROUP_SPELLCASTERS: return has_flag(unit_id, 17) ? 1 : 0;
    case PF_GROUP_TOWERS: return has_flag(unit_id, 20) ? 1 : 0;
    default: return 0;
  }
}

const char* pf_unit_group_name(int group) {
  if (group < 0 || group >= PF_GROUP_COUNT) return nullptr;
  return kGroupNames[group];
}

int pf_unit_facing(int x, int y, int unit_id, int frame_count) {
  if (frame_count <= 1) return 0;
  const uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^
                     uint32_t(unit_id) * 83492791u;
  return int(h % uint32_t(frame_count));
}

int pf_terrain_is_wall(int terrain) {
  return pf_terrain_wall_kind(terrain) != 0 ? 1 : 0;
}

int pf_terrain_wall_kind(int terrain) {
  if (terrain == PF_TERRAIN_WALL_HUMAN) return 1;
  if (terrain == PF_TERRAIN_WALL_ORC) return 2;
  return 0;
}

int pf_brush_points(int x, int y, int size, int shape, float density,
                    uint32_t* seed, int* out, int capacity_ints) {
  if (size <= 1 || shape == PF_BRUSH_SQUARE) return 0;
  const int r = (size - 1) / 2;
  /* Squared distance a point may be from the centre and still be in the disc.
   *
   * At r == 1 the usual r*r + r admits all eight neighbours, so the round brush
   * at its smallest size painted the same 3x3 square the square brush does.
   * r*r drops the corners, which is a cross: the smallest footprint that is
   * visibly not a square. */
  const int reach = r == 1 ? r * r : r * r + r;
  int written = 0;
  uint32_t state = seed ? *seed : 1u;

  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (shape == PF_BRUSH_SCATTER) {
        // Advanced for every candidate, not only the kept ones, so the same
        // stroke over the same ground keeps the same pattern — and before the
        // disc test, so clipping does not reshuffle what was inside it.
        state = state * 1664525u + 1013904223u;
        if (float(state) / 4294967296.0f > density) continue;
      }
      // Both round shapes share the disc: a spray can that sprays a square
      // gives away that it is a grid of random tiles rather than a puff.
      if (dx * dx + dy * dy > reach) continue;
      if (out && written * 2 + 1 < capacity_ints) {
        out[written * 2] = x + dx;
        out[written * 2 + 1] = y + dy;
      }
      written++;
    }
  }
  if (seed) *seed = state;
  return written;
}

int pf_symmetry_points(const pf_map* map, int x, int y, int w, int h,
                       int mirrors, int* out, int capacity) {
  struct Placed { int x, y, w, h; };
  Placed found[8];
  int n = 0;
  auto add = [&](const Placed& p) {
    for (int i = 0; i < n; i++) {
      if (found[i].x == p.x && found[i].y == p.y) return false;
    }
    if (n >= 8) return false;
    found[n++] = p;
    return true;
  };

  const int fw = w > 0 ? w : 1;
  const int fh = h > 0 ? h : 1;
  add({x, y, fw, fh});

  if (map && mirrors != PF_MIRROR_NONE) {
    const int mw = map->map->width();
    const int mh = map->map->height();
    // A diagonal reflection swaps the axes, so it means nothing unless the map
    // is square. Dropping it beats producing something lopsided.
    const bool square = mw == mh;

    // Reflections generate a group, so keep applying them until nothing new
    // appears: left-right and top-bottom must also yield the opposite corner.
    for (int pass = 0; pass < 4; pass++) {
      const int had = n;
      for (int i = 0; i < had; i++) {
        const Placed p = found[i];
        if (mirrors & PF_MIRROR_LEFT_RIGHT) add({mw - p.x - p.w, p.y, p.w, p.h});
        if (mirrors & PF_MIRROR_TOP_BOTTOM) add({p.x, mh - p.y - p.h, p.w, p.h});
        // Transposing a footprint swaps it too, so the anchor is the corner of
        // the transposed rectangle rather than the transposed corner.
        if (square && (mirrors & PF_MIRROR_DIAG_NW_SE)) add({p.y, p.x, p.h, p.w});
        if (square && (mirrors & PF_MIRROR_DIAG_SW_NE)) {
          add({mh - p.y - p.h, mw - p.x - p.w, p.h, p.w});
        }
      }
      if (n == had) break;
    }
  }

  for (int i = 0; i < n && out && i * 2 + 1 < capacity; i++) {
    out[i * 2] = found[i].x;
    out[i * 2 + 1] = found[i].y;
  }
  return n;
}

int pf_symmetry_corners(const pf_map* map, int cx, int cy, int mirrors,
                        int* out, int capacity) {
  struct Point { int x, y; };
  Point found[8];
  int n = 0;
  auto add = [&](const Point& p) {
    for (int i = 0; i < n; i++) {
      if (found[i].x == p.x && found[i].y == p.y) return;
    }
    if (n < 8) found[n++] = p;
  };

  add({cx, cy});

  if (map && mirrors != PF_MIRROR_NONE) {
    // Corner counts, not tile counts: the grid is one larger in each axis, so
    // corners run 0..width inclusive and a reflection is `width - cx` rather
    // than the `width - x - 1` a tile takes. Reflecting a corner as though it
    // were a tile lands one corner short and puts the mirrored mark half a tile
    // off its own reflection.
    const int mw = map->map->width();
    const int mh = map->map->height();
    const bool square = mw == mh;

    // The same group closure pf_symmetry_points does: left-right and top-bottom
    // together must also give the diagonally opposite corner.
    for (int pass = 0; pass < 4; pass++) {
      const int had = n;
      for (int i = 0; i < had; i++) {
        const Point p = found[i];
        if (mirrors & PF_MIRROR_LEFT_RIGHT) add({mw - p.x, p.y});
        if (mirrors & PF_MIRROR_TOP_BOTTOM) add({p.x, mh - p.y});
        if (square && (mirrors & PF_MIRROR_DIAG_NW_SE)) add({p.y, p.x});
        if (square && (mirrors & PF_MIRROR_DIAG_SW_NE)) add({mh - p.y, mw - p.x});
      }
      if (n == had) break;
    }
  }

  for (int i = 0; i < n && out && i * 2 + 1 < capacity; i++) {
    out[i * 2] = found[i].x;
    out[i * 2 + 1] = found[i].y;
  }
  return n;
}

int pf_field_label(const char* name, char* out, int cap) {
  return pf::field_label(name, out, cap);
}

const char* pf_alow_bit_name(int block, int bit) {
  // The tables behind `alow_bit_name` are ours, and abbreviate: one bit covers
  // a race pair the table has room to name only half of. When a host has
  // installed the game's strings, the unit or upgrade the bit stands for can
  // name it instead — in full, and in the game's own language. A unit bit
  // restricts both halves, so both are named, the way the table already does.
  if (pf::installed_strings()) {
    static std::string composed;
    if (block == 0) {
      const int human = pf_alow_bit_unit(block, bit);
      if (human >= 0) {
        const char* a = pf_unit_name(human);
        const char* b = pf_unit_name(pf_unit_counterpart(human));
        if (a) {
          composed = b && std::string(b) != a ? std::string(a) + " / " + b : a;
          return composed.c_str();
        }
      }
    } else if (block >= 1) {
      const int upgrade = pf_alow_bit_upgrade(block, bit);
      if (upgrade >= 0) {
        if (const char* name = pf_upgrade_name(upgrade)) {
          composed = name;
          return composed.c_str();
        }
      }
    }
  }
  return pf::alow_bit_name(block, bit);
}

int pf_alow_bit_unit(int block, int bit) { return pf::alow_bit_unit(block, bit); }

int pf_alow_bit_upgrade(int block, int bit) { return pf::alow_bit_upgrade(block, bit); }

const char* pf_ai_name(int value) {
  if (value < 0 || value >= pf::kAiNameCount) return nullptr;
  // The game's own, which is the difference between naming a script and
  // numbering it: fifty-one of the built-ins are "Expansion 27" where the
  // table says "_Hum Exp. 6c (Orange)".
  if (const pf_strings* s = pf::installed_strings()) {
    const std::string& named = s->tbl.at(pf::kFirstAiString + value);
    if (named.empty()) return pf::ai_name(value);
    // Blizzard marks the scripts their own editor hides with a leading
    // underscore. It is a note to their tool rather than part of the name, and
    // this client offers every script anyway.
    //
    // Cleaned copies are kept per script rather than trimmed on the way out:
    // the returned pointer has to outlive the call and be distinct from every
    // other script's. Keyed on the source string, so pointing the client at a
    // different game folder rebuilds them rather than answering with the old.
    // static_cast rather than size_t(...): the functional-cast form is a most
    // vexing parse that MSVC reads as a function declaration.
    static std::vector<std::pair<std::string, std::string>> cleaned(
        static_cast<size_t>(pf::kAiNameCount));
    auto& slot = cleaned[size_t(value)];
    if (slot.first != named) {
      slot.first = named;
      const size_t at = named.find_first_not_of('_');
      slot.second = at == std::string::npos ? named : named.substr(at);
    }
    return slot.second.c_str();
  }
  return pf::ai_name(value);
}

int pf_ai_name_count(void) { return pf::kAiNameCount; }

const char* pf_player_name(int player) {
  if (player < 0 || player >= pf::kPlayerCount) return nullptr;
  return pf::kPlayerNames[player];
}

uint32_t pf_player_color(int player) {
  if (player < 0 || player >= pf::kPlayerCount) return 0x909090u;
  return pf::kPlayerColors[player];
}

int pf_player_is_supported(int player) {
  return pf::player_slot_is_supported(player) ? 1 : 0;
}

// ------------------------------------------------------------------- map

pf_map* pf_map_open(const uint8_t* data, size_t len, pf_status* status) {
  if (!data) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  pf::Status s = pf::Status::Ok;
  pf::Map* parsed = pf::Map::parse(data, len, s);
  if (!parsed) { set_status(status, to_c(s)); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_map();
  handle->map.reset(parsed);
  return handle;
}

pf_map* pf_map_open_file(const char* path, pf_status* status) {
  if (!path) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  std::vector<uint8_t> bytes;
  if (!pf::read_file(path, bytes)) { set_status(status, PF_ERR_IO); return nullptr; }
  return pf_map_open(bytes.data(), bytes.size(), status);
}

int pf_map_refit(pf_map* map) {
  if (!map) return -1;
  pf::Map& m = *map->map;
  const std::vector<uint16_t> before = m.tiles();
  const pf::Rect rect{0, 0, m.width() - 1, m.height() - 1};
  pf::apply_corners(m, map->grid(), rect, map->tiles());
  pf::rebuild_regions(m);

  int changed = 0;
  const std::vector<uint16_t>& after = m.tiles();
  for (size_t i = 0; i < before.size() && i < after.size(); i++) {
    if (before[i] != after[i]) changed++;
  }
  return changed;
}

pf_map* pf_map_generate(const pf_generate_params* params,
                        const pf_noise_layer* layers, int layer_count,
                        pf_status* status) {
  if (!params) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  const int w = params->width;
  const int h = params->height;

  pf_map* handle = pf_map_create(w, h, params->tileset, status);
  if (!handle) return nullptr;

  std::vector<pf::NoiseLayer> octaves;
  for (int i = 0; i < layer_count && layers; i++) {
    octaves.push_back({layers[i].scale, layers[i].seed, layers[i].weight});
  }
  if (octaves.empty()) octaves.push_back({0.06f, 1u, 1.0f});
  const pf::LayeredNoise height_field(octaves.data(), int(octaves.size()));

  // Mountains come from the broadest octave alone. The terrain graph only lets
  // rock touch coast, so every massif loses a ring to the shoreline it demands,
  // and peaks fragmented by the fine octaves are all ring and no interior —
  // which is why asking for 4.5% rock produced none at all.
  pf::NoiseLayer broadest = octaves[0];
  for (const pf::NoiseLayer& octave : octaves) {
    if (octave.scale < broadest.scale) broadest = octave;
  }
  broadest.weight = 1.0f;
  const pf::LayeredNoise massif_field(&broadest, 1);

  // A second field for what grows on the land: forest and rock are scattered
  // over ground rather than being the top of an altitude ramp, so a band of the
  // same field would ring them round the mountains.
  const pf::NoiseLayer detail_layer{
      params->detail_scale > 0.0f ? params->detail_scale : 0.11f,
      params->detail_seed ? params->detail_seed : 7u, 1.0f};
  const pf::LayeredNoise detail_field(&detail_layer, 1);

  // Corners, not tiles: the grid is one larger in each axis.
  const int cw = w + 1;
  const int ch = h + 1;
  std::vector<float> land(size_t(cw) * size_t(ch));
  std::vector<float> growth(land.size());
  std::vector<float> massif(land.size());
  for (int y = 0; y < ch; y++) {
    for (int x = 0; x < cw; x++) {
      const size_t i = size_t(y) * size_t(cw) + size_t(x);
      land[i] = height_field.at(float(x), float(y));
      growth[i] = detail_field.at(float(x), float(y));
      massif[i] = massif_field.at(float(x), float(y));
    }
  }

  // Cut at quantiles so the requested shares come out exact, whatever shape
  // the noise happens to have. Sorting a copy is the honest way to find them.
  auto quantile = [](std::vector<float> values, float fraction) {
    if (values.empty()) return 0.0f;
    fraction = std::min(1.0f, std::max(0.0f, fraction));
    size_t at = size_t(fraction * float(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + long(at), values.end());
    return values[at];
  };

  const float water_share = std::max(0.0f, params->water);
  const float coast_share = std::max(0.0f, params->coast);
  const float water_cut = quantile(land, water_share);
  const float coast_cut = quantile(land, water_share + coast_share);

  // Forest and rock are shares of the whole map but can only land on the part
  // that is not water, so their quantiles are taken over the land corners alone
  // and the share is scaled up by how much land there is. Taken over
  // everything, most of the growth field's peaks fall in the sea and the map
  // comes out nearly bare — 9% forest against the 25% asked for.
  //
  // Rock comes from the top of the elevation field rather than the growth one:
  // the terrain graph only lets mountain touch coast, so scattered peaks are
  // legalised away to nothing, where a range taken from elevation is
  // contiguous. Aimed high on top of that, because the ring the graph insists
  // on eats into whatever is asked for.
  std::vector<float> land_massif;
  land_massif.reserve(massif.size());
  for (size_t i = 0; i < massif.size(); i++) {
    if (land[i] >= coast_cut) land_massif.push_back(massif[i]);
  }
  const float rock_share = std::min(0.75f, std::max(0.0f, params->rock) * 2.2f);
  const float rock_cut = quantile(land_massif, 1.0f - rock_share);

  // Forest is a share of the whole map but can only land on the ground part of
  // it, so its quantile is taken over those corners alone. Taken over
  // everything, most of the growth field's peaks fall in the sea.
  std::vector<float> open_ground;
  open_ground.reserve(growth.size());
  for (size_t i = 0; i < growth.size(); i++) {
    if (land[i] >= coast_cut && massif[i] < rock_cut) open_ground.push_back(growth[i]);
  }
  const float ground_fraction = open_ground.empty()
      ? 1.0f : float(open_ground.size()) / float(growth.size());
  // Same compensation as the rock: a forest loses its outermost corners to the
  // ground it has to meet. Measured rather than guessed — the response is near
  // enough linear, and 1.75 puts the corpus's 25% forest at 25%. Clamped so an
  // island map still gets beaches.
  const float forest_share = std::min(0.9f,
      std::max(0.0f, params->forest) * 1.75f / ground_fraction);
  const float forest_cut = quantile(open_ground, 1.0f - forest_share);

  pf::CornerGrid& grid = handle->grid();
  for (int y = 0; y < ch; y++) {
    for (int x = 0; x < cw; x++) {
      const size_t i = size_t(y) * size_t(cw) + size_t(x);
      const float e = land[i];
      uint8_t terrain;
      // Deep water in the middle of the sea, shallows at its edge, which is
      // what the two water shades are for.
      if (e < water_cut * 0.75f) {
        terrain = pf::kWaterDark;
      } else if (e < water_cut) {
        terrain = pf::kWaterLight;
      } else if (e < coast_cut) {
        terrain = pf::kCoastLight;
      } else if (massif[i] >= rock_cut) {
        terrain = pf::kMountain;
      } else if (growth[i] >= forest_cut) {
        terrain = pf::kForest;
      } else {
        // The two ground shades split what is left, so the plains are mottled
        // rather than one flat colour.
        terrain = (growth[i] < forest_cut * 0.6f) ? pf::kGroundDark : pf::kGroundLight;
      }
      grid.set(x, y, terrain);
    }
  }

  // Clearings. Noise makes scenery; a map needs somewhere to build. Placed
  // farthest-first so several do not end up in the same valley.
  const int wanted_clearings = std::max(0, params->clearings);
  const int radius = params->clearing_radius > 0 ? params->clearing_radius : 8;
  if (wanted_clearings > 0) {
    auto is_land = [&](int cx, int cy) {
      if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) return false;
      const uint8_t t = grid.get(cx, cy);
      return t == pf::kGroundLight || t == pf::kGroundDark ||
             t == pf::kForest || t == pf::kMountain;
    };

    // How much land sits within a radius of each candidate, on a coarse grid.
    struct Spot { int x, y, score; };
    std::vector<Spot> spots;
    const int stride = std::max(2, radius / 2);
    for (int cy = radius; cy < ch - radius; cy += stride) {
      for (int cx = radius; cx < cw - radius; cx += stride) {
        int land_count = 0;
        for (int dy = -radius; dy <= radius; dy++) {
          for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius && is_land(cx + dx, cy + dy)) {
              land_count++;
            }
          }
        }
        spots.push_back({cx, cy, land_count});
      }
    }

    std::vector<std::pair<int, int>> chosen;
    for (int n = 0; n < wanted_clearings && !spots.empty(); n++) {
      size_t best = 0;
      long long best_score = -1;
      for (size_t i = 0; i < spots.size(); i++) {
        // Land first, then distance from the clearings already placed, so a
        // second base does not open onto the first.
        long long apart = 1LL << 30;
        for (const auto& c : chosen) {
          const long long dx = spots[i].x - c.first, dy = spots[i].y - c.second;
          apart = std::min(apart, dx * dx + dy * dy);
        }
        const long long score = static_cast<long long>(spots[i].score) * 1000 +
                                std::min(apart, 1LL << 20);
        if (score > best_score) { best_score = score; best = i; }
      }
      chosen.push_back({spots[best].x, spots[best].y});
      spots.erase(spots.begin() + long(best));
    }

    for (const auto& c : chosen) {
      for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
          if (dx * dx + dy * dy > radius * radius) continue;
          const int cx = c.first + dx, cy = c.second + dy;
          if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) continue;
          // Only what is already dry: a clearing must not fill in the sea.
          const uint8_t t = grid.get(cx, cy);
          if (t == pf::kForest || t == pf::kMountain || t == pf::kGroundDark) {
            grid.set(cx, cy, pf::kGroundLight);
          }
        }
      }
    }
  }

  const pf::Rect rect{0, 0, w - 1, h - 1};
  pf::legalize(grid, rect);
  pf::apply_corners(*handle->map, grid, rect, handle->tiles());
  pf::rebuild_regions(*handle->map);
  set_status(status, PF_OK);
  return handle;
}

pf_map* pf_map_create(int width, int height, int tileset, pf_status* status) {
  pf::Status s = pf::Status::Ok;
  pf::Map* created = pf::Map::create(width, height, tileset, s);
  if (!created) { set_status(status, to_c(s)); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_map();
  handle->map.reset(created);
  return handle;
}

void pf_map_free(pf_map* map) { delete map; }

uint8_t* pf_map_save(const pf_map* map, size_t* out_len, pf_status* status) {
  if (!map || !out_len) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  std::vector<uint8_t> bytes = map->map->serialize();
  auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size() ? bytes.size() : 1));
  if (!buffer) { set_status(status, PF_ERR_OUT_OF_MEMORY); return nullptr; }
  std::memcpy(buffer, bytes.data(), bytes.size());
  *out_len = bytes.size();
  set_status(status, PF_OK);
  return buffer;
}

pf_status pf_map_save_file(const pf_map* map, const char* path) {
  if (!map || !path) return PF_ERR_INVALID_ARG;
  std::vector<uint8_t> bytes = map->map->serialize();
  std::FILE* f = std::fopen(path, "wb");
  if (!f) return PF_ERR_IO;
  size_t wrote = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return wrote == bytes.size() ? PF_OK : PF_ERR_IO;
}

void pf_buffer_free(uint8_t* buffer) { std::free(buffer); }

int pf_map_width(const pf_map* map) { return map ? map->map->width() : 0; }
int pf_map_height(const pf_map* map) { return map ? map->map->height() : 0; }
int pf_map_tileset(const pf_map* map) { return map ? map->map->tileset() : 0; }
int pf_map_version(const pf_map* map) { return map ? map->map->version() : 0; }

pf_status pf_map_set_tileset(pf_map* map, int tileset) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (tileset < 0 || tileset > 3) return PF_ERR_OUT_OF_RANGE;
  map->map->set_tileset(tileset);
  return PF_OK;
}

namespace {

/// The start-location unit ids, human and orc.
constexpr int kStartHuman = 94, kStartOrc = 95;

/**
 * How far a start location can reasonably be from the gold it lives off.
 *
 * Measured over the 138 start locations on Blizzard's 28 multiplayer maps: a
 * median 5.4 tiles to the nearest mine, 21.2 at the 95th percentile, 32.2 at
 * the furthest. Forty leaves clear air above every one, so a warning here means
 * "nothing to mine" rather than "further than usual".
 *
 * Deliberately not the placer's 20, which answers where to *put* a new start;
 * reusing it flagged 4lake and clearing, which are perfectly playable.
 */
constexpr long long kStartReach = 40 * 40;

const pf::Unit* start_of(const pf::Map& m, int player) {
  for (const pf::Unit& u : m.units()) {
    if ((u.type == kStartHuman || u.type == kStartOrc) && u.owner == player) return &u;
  }
  return nullptr;
}

}  // namespace

int64_t pf_map_start_gold_in_reach(const pf_map* map, int player) {
  if (!map || player < 0 || player >= pf::kPlayerCount) return -1;
  const pf::Map& m = *map->map;
  const pf::Unit* start = start_of(m, player);
  if (!start) return -1;

  int64_t gold = 0;
  for (const pf::Unit& u : m.units()) {
    if (!((pf_unit_flags(u.type) >> 22) & 1)) continue;      // gold mines only
    const long long dx = int(u.x) - int(start->x);
    const long long dy = int(u.y) - int(start->y);
    if (dx * dx + dy * dy <= kStartReach) gold += int64_t(u.value) * 2500;
  }
  return gold;
}

int pf_map_start_landmasses(const pf_map* map) {
  if (!map) return 0;
  pf::Map& m = *map->map;
  // REGM is the game's own connectivity labelling and is what the start
  // locations are actually standing on, so it is asked rather than recomputed.
  pf::rebuild_regions(m);
  const std::vector<uint16_t>& regm = m.regions();
  std::vector<uint16_t> seen;
  for (const pf::Unit& u : m.units()) {
    if (u.type != kStartHuman && u.type != kStartOrc) continue;
    if (u.x >= m.width() || u.y >= m.height()) continue;
    const uint16_t region = regm[size_t(u.y) * size_t(m.width()) + size_t(u.x)];
    if (std::find(seen.begin(), seen.end(), region) == seen.end()) seen.push_back(region);
  }
  return int(seen.size());
}

int pf_map_oil_map_used(const pf_map* map) {
  if (!map) return 0;
  int used = 0;
  for (uint8_t b : map->map->oil_map()) if (b) used++;
  return used;
}

int pf_map_refit_tiles(pf_map* map) {
  if (!map || !map->art) return 0;
  const pf::TilesetArt* a = map->art;
  auto drawable = [&](uint16_t tile) {
    const int m = a->megatile_for(tile);
    // A blank megatile is a valid index that draws as a black square, so it is
    // no more usable than a missing one.
    return m >= 0 && !a->is_blank(m);
  };

  std::vector<uint16_t>& tiles = map->map->tiles();
  const int count = map->map->width() * map->map->height();
  int changed = 0;
  for (int i = 0; i < count; i++) {
    if (drawable(tiles[i])) continue;
    // The group says what the tile *is* and the variation is only which drawing
    // of it, so the replacement is another variation of the same group.
    const uint16_t base = uint16_t(tiles[i] & 0xfff0);
    for (int v = 0; v < 16; v++) {
      const uint16_t candidate = uint16_t(base | v);
      if (!drawable(candidate)) continue;
      tiles[i] = candidate;
      changed++;
      break;
    }
  }
  return changed;
}

int pf_map_description_max(void) { return pf::kDescBytes - 1; }

const char* pf_map_description(const pf_map* map) {
  return map ? map->map->description().c_str() : nullptr;
}

pf_status pf_map_set_description(pf_map* map, const char* text) {
  if (!map || !text) return PF_ERR_INVALID_ARG;
  return map->map->set_description(text) ? PF_OK : PF_ERR_OUT_OF_RANGE;
}

int pf_map_description_bytes(const char* text) {
  if (!text) return -1;
  std::string bytes;
  return pf::desc_encode(text, bytes) ? int(bytes.size()) : -1;
}

int pf_map_warning_count(const pf_map* map) {
  return map ? int(map->map->warnings().size()) : 0;
}

const char* pf_map_warning(const pf_map* map, int index) {
  if (!map || index < 0 || size_t(index) >= map->map->warnings().size()) return nullptr;
  return map->map->warnings()[size_t(index)].c_str();
}

int pf_map_owner(const pf_map* map, int player) { return map ? map->map->owner(player) : 0; }
int pf_map_race(const pf_map* map, int player) { return map ? map->map->race(player) : 0; }
int pf_map_start_gold(const pf_map* map, int player) { return map ? map->map->start_gold(player) : 0; }
int pf_map_start_lumber(const pf_map* map, int player) { return map ? map->map->start_lumber(player) : 0; }
int pf_map_start_oil(const pf_map* map, int player) { return map ? map->map->start_oil(player) : 0; }
int pf_map_ai(const pf_map* map, int player) { return map ? map->map->ai(player) : 0; }

pf_status pf_map_set_owner(pf_map* map, int player, int owner) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (player < 0 || player >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  map->map->set_owner(player, uint8_t(owner));
  return PF_OK;
}

pf_status pf_map_set_race(pf_map* map, int player, int race) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (player < 0 || player >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  map->map->set_race(player, uint8_t(race));
  return PF_OK;
}

pf_status pf_map_set_start_resources(pf_map* map, int player, int gold, int lumber, int oil) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (player < 0 || player >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  map->map->set_start_resources(player, uint16_t(gold), uint16_t(lumber), uint16_t(oil));
  return PF_OK;
}

pf_status pf_map_set_ai(pf_map* map, int player, int ai) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (player < 0 || player >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  map->map->set_ai(player, uint8_t(ai));
  return PF_OK;
}

const uint16_t* pf_map_tiles(const pf_map* map) {
  return map ? map->map->tiles().data() : nullptr;
}
const uint16_t* pf_map_movement(const pf_map* map) {
  return map ? map->map->movement().data() : nullptr;
}
const uint16_t* pf_map_regions(const pf_map* map) {
  return map ? map->map->regions().data() : nullptr;
}

int pf_map_tile_at(const pf_map* map, int x, int y) {
  if (!map) return -1;
  if (x < 0 || y < 0 || x >= map->map->width() || y >= map->map->height()) return -1;
  return map->map->tile_at(x, y);
}

int pf_tile_movement(int tile) {
  if (tile < 0 || tile > 0xffff) return -1;
  return pf::tile_movement(uint16_t(tile));
}

int pf_map_movement_at(const pf_map* map, int x, int y) {
  if (!map) return -1;
  if (x < 0 || y < 0 || x >= map->map->width() || y >= map->map->height()) return -1;
  return map->map->movement()[size_t(y) * size_t(map->map->width()) + size_t(x)];
}

pf_status pf_map_set_movement(pf_map* map, int x, int y, int value) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (x < 0 || y < 0 || x >= map->map->width() || y >= map->map->height()) {
    return PF_ERR_OUT_OF_RANGE;
  }
  if (value < 0 || value > 0xffff) return PF_ERR_INVALID_ARG;
  const size_t i = size_t(y) * size_t(map->map->width()) + size_t(x);
  map->map->movement()[i] = uint16_t(value);
  return PF_OK;
}

int pf_map_reset_movement(pf_map* map, int x, int y, int w, int h) {
  if (!map) return -1;
  pf::Map& m = *map->map;
  // A negative or zero size means the whole map, which is what "put it back"
  // means when nothing is selected.
  if (w <= 0 || h <= 0) { x = 0; y = 0; w = m.width(); h = m.height(); }
  const int x1 = std::min(x + w, m.width()), y1 = std::min(y + h, m.height());
  int changed = 0;
  for (int ty = std::max(0, y); ty < y1; ty++) {
    for (int tx = std::max(0, x); tx < x1; tx++) {
      const size_t i = size_t(ty) * size_t(m.width()) + size_t(tx);
      const uint16_t want = pf::tile_movement(m.tiles()[i]);
      if (m.movement()[i] == want) continue;
      m.movement()[i] = want;
      changed++;
    }
  }
  return changed;
}

int pf_unit_default_owner(int unit_id) { return pf::unit_default_owner(unit_id); }

int pf_ai_kind(int value) { return pf::ai_kind(value); }

int pf_ai_same_as(int value) { return pf::ai_same_as(value); }

const char* pf_ai_mission(int value) { return pf::ai_mission(value); }

// ------------------------------------------------------- AI script table

pf_strings* pf_strings_open_memory(const uint8_t* bytes, size_t length,
                                   pf_status* status) {
  if (!bytes || !length) { if (status) *status = PF_ERR_INVALID_ARG; return nullptr; }
  auto* out = new pf_strings();
  if (!out->tbl.parse(bytes, length)) {
    delete out;
    if (status) *status = PF_ERR_MALFORMED;
    return nullptr;
  }
  if (status) *status = PF_OK;
  return out;
}

pf_strings* pf_strings_open_source(const pf_data_source* source, pf_status* status) {
  if (!source) { if (status) *status = PF_ERR_INVALID_ARG; return nullptr; }
  std::vector<uint8_t> bytes;
  if (!source->source.read("rez\\stat_txt.tbl", bytes) || bytes.empty()) {
    if (status) *status = PF_ERR_IO;
    return nullptr;
  }
  return pf_strings_open_memory(bytes.data(), bytes.size(), status);
}

void pf_strings_free(pf_strings* strings) {
  // Uninstall first: leaving the core pointing at freed names is the one way
  // this arrangement can go wrong, and it costs a comparison to rule out.
  if (strings && pf::strings_slot() == strings) pf::strings_slot() = nullptr;
  delete strings;
}

int pf_strings_count(const pf_strings* strings) {
  return strings ? strings->tbl.count() : 0;
}

const char* pf_strings_at(const pf_strings* strings, int index) {
  if (!strings) return nullptr;
  return strings->tbl.at(index).c_str();
}

void pf_use_strings(pf_strings* strings) { pf::strings_slot() = strings; }

namespace {

/// Copy out in the convention `pf_sprite_path` set: writes up to `cap` bytes
/// with the terminator, returns the full length, takes NULL/0 as a query.
/// Shared with the AI script accessors further down, which had it first.
int copy_out(const std::string& text, char* out, int cap) {
  if (!out || cap <= 0) return int(text.size());
  const int n = std::min(int(text.size()), cap - 1);
  std::memcpy(out, text.data(), size_t(n));
  out[n] = '\0';
  return int(text.size());
}

}  // namespace

int pf_placement_message(int code, char* out, int cap) {
  int index = -1;
  switch (code) {
    case PF_PLACE_OUT_OF_BOUNDS: index = pf::kCannotBuildOffMapString; break;
    case PF_PLACE_NEEDS_SHORE:   index = pf::kMustBuildOnCoastString;  break;
    case PF_PLACE_TOO_NEAR_MINE: index = pf::kTooNearGoldmineString;   break;
    default: break;   // the rest the game covers with one vague sentence
  }
  const pf_strings* s = pf::installed_strings();
  if (index < 0 || !s) return copy_out(std::string(), out, cap);
  return copy_out(s->tbl.at(index), out, cap);
}

int pf_resource_label(int resource, char* out, int cap) {
  const pf_strings* s = pf::installed_strings();
  if (!s) return copy_out(std::string(), out, cap);
  if (resource == PF_RESOURCE_GOLD) {
    return copy_out(s->tbl.at(pf::kGoldLeftString), out, cap);
  }
  if (resource == PF_RESOURCE_OIL) {
    return copy_out(s->tbl.at(pf::kOilLeftString), out, cap);
  }
  return copy_out(std::string(), out, cap);
}

pf_ai_scripts* pf_ai_scripts_open_source(const pf_data_source* source,
                                         pf_status* status) {
  if (!source) { if (status) *status = PF_ERR_INVALID_ARG; return nullptr; }
  pf::Status s = pf::Status::Ok;
  pf::AiScripts* scripts = pf::AiScripts::open_source(source->source, s);
  if (!scripts) { if (status) *status = to_c(s); return nullptr; }
  if (status) *status = PF_OK;
  pf_ai_scripts* out = new pf_ai_scripts();
  out->scripts.reset(scripts);
  return out;
}

pf_ai_scripts* pf_ai_scripts_open_memory(const uint8_t* bytes, size_t length,
                                         pf_status* status) {
  if (!bytes || !length) { if (status) *status = PF_ERR_INVALID_ARG; return nullptr; }
  pf::Status s = pf::Status::Ok;
  pf::AiScripts* scripts =
      pf::AiScripts::open(std::vector<uint8_t>(bytes, bytes + length), s);
  if (!scripts) { if (status) *status = to_c(s); return nullptr; }
  if (status) *status = PF_OK;
  pf_ai_scripts* out = new pf_ai_scripts();
  out->scripts.reset(scripts);
  return out;
}

void pf_ai_scripts_free(pf_ai_scripts* scripts) { delete scripts; }

int pf_ai_scripts_count(const pf_ai_scripts* scripts) {
  return scripts ? scripts->scripts->count() : 0;
}

int pf_ai_script_summary(const pf_ai_scripts* scripts, int index, char* out, int cap) {
  if (!scripts) return 0;
  return copy_out(scripts->scripts->summary(index), out, cap);
}

int pf_ai_script_waves(const pf_ai_scripts* scripts, int index, char* out, int cap) {
  if (!scripts) return 0;
  return copy_out(scripts->scripts->waves(index), out, cap);
}

int pf_ai_script_listing(const pf_ai_scripts* scripts, int index, char* out, int cap) {
  if (!scripts) return 0;
  return copy_out(scripts->scripts->listing(index), out, cap);
}

int pf_movement_class_count(void) { return pf::movement_class_count(); }
int pf_movement_class_value(int index) { return pf::movement_class_value(index); }
const char* pf_movement_class_name(int index) { return pf::movement_class_name(index); }
int pf_movement_class_of(int value) { return pf::movement_class_of(value); }

void pf_tile_quadrants(uint16_t tile, uint8_t* out) {
  if (out) pf::decode_tile(tile, out);
}

int pf_tile_dominant_terrain(uint16_t tile) { return pf::dominant_terrain(tile); }

// ----------------------------------------------------------------- units

int pf_map_unit_count(const pf_map* map) {
  return map ? int(map->map->units().size()) : 0;
}

pf_status pf_map_unit(const pf_map* map, int index, pf_unit* out) {
  if (!map || !out) return PF_ERR_INVALID_ARG;
  const auto& units = map->map->units();
  if (index < 0 || size_t(index) >= units.size()) return PF_ERR_OUT_OF_RANGE;
  const pf::Unit& u = units[size_t(index)];
  out->x = u.x;
  out->y = u.y;
  out->type = u.type;
  out->owner = u.owner;
  out->value = u.value;
  return PF_OK;
}

int pf_map_add_unit(pf_map* map, int x, int y, int type, int owner, int value) {
  if (!map) return -1;
  if (type < 0 || type >= pf::kUnitCount) return -1;
  if (owner < 0 || owner >= pf::kPlayerCount) return -1;
  if (!map->allow_illegal_placement && pf_map_placement_check(map, x, y, type) != PF_PLACE_OK) {
    return -1;
  }

  // A player has exactly one start location. Placing another moves it rather
  // than adding a second: the game reads one per slot, and validation has
  // always reported the extras as an error — so the editor should not be able
  // to make them in the first place. Removed from the back, because indices
  // are positions in the array.
  if (pf_unit_in_group(type, PF_GROUP_START_LOCATIONS)) {
    auto& units = map->map->units();
    for (int i = int(units.size()) - 1; i >= 0; i--) {
      if (units[size_t(i)].owner != owner) continue;
      if (!pf_unit_in_group(units[size_t(i)].type, PF_GROUP_START_LOCATIONS)) continue;
      map->map->remove_unit(i);
    }
  }

  return map->map->add_unit(x, y, type, owner, value);
}

pf_status pf_map_remove_unit(pf_map* map, int index) {
  if (!map) return PF_ERR_INVALID_ARG;
  return map->map->remove_unit(index) ? PF_OK : PF_ERR_OUT_OF_RANGE;
}

pf_status pf_map_set_unit_value(pf_map* map, int index, int value) {
  if (!map || index < 0 || index >= int(map->map->units().size())) {
    return PF_ERR_INVALID_ARG;
  }
  if (value < 0 || value > 65535) return PF_ERR_OUT_OF_RANGE;
  map->map->units()[size_t(index)].value = uint16_t(value);
  return PF_OK;
}

pf_status pf_map_set_unit_owner(pf_map* map, int index, int owner) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (owner < 0 || owner >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  auto& units = map->map->units();
  if (index < 0 || size_t(index) >= units.size()) return PF_ERR_OUT_OF_RANGE;
  units[size_t(index)].owner = uint8_t(owner);
  return PF_OK;
}

pf_status pf_map_move_unit(pf_map* map, int index, int x, int y) {
  if (!map) return PF_ERR_INVALID_ARG;
  auto& units = map->map->units();
  if (index < 0 || size_t(index) >= units.size()) return PF_ERR_OUT_OF_RANGE;
  if (x < 0 || y < 0 || x >= map->map->width() || y >= map->map->height()) {
    return PF_ERR_OUT_OF_RANGE;
  }
  units[size_t(index)].x = uint16_t(x);
  units[size_t(index)].y = uint16_t(y);
  return PF_OK;
}

int pf_map_unit_at(const pf_map* map, int x, int y) {
  return map ? map->map->unit_at(x, y) : -1;
}

void pf_map_unit_footprint(const pf_map* map, int type, int* w, int* h) {
  int fw = 1, fh = 1;
  if (map) map->map->unit_footprint(type, fw, fh);
  if (w) *w = fw;
  if (h) *h = fh;
}

// -------------------------------------------------------------- unit data

namespace {

/// Locate one element of a UDTA field inside the section's raw bytes.
/// Returns nullptr when the field, unit or component is out of range, or the
/// section is absent or too short.
uint8_t* segment_slot(std::vector<uint8_t>& raw, const pf::UdtaSegment* table, int count,
                      int field, int unit, int component, uint8_t* width) {
  if (field < 0 || field >= count) return nullptr;
  const pf::UdtaSegment& seg = table[field];
  if (component < 0 || component >= seg.components) return nullptr;

  const int per_unit = seg.elements / seg.components;
  if (unit < 0 || unit >= per_unit) return nullptr;

  const int offset = pf::segment_offset(table, count, field) +
                     (unit * seg.components + component) * seg.width;
  if (offset < 0 || size_t(offset + seg.width) > raw.size()) return nullptr;
  *width = seg.width;
  return raw.data() + offset;
}

uint8_t* udta_slot(pf::UnitData& udta, int field, int unit, int component, uint8_t* width) {
  return segment_slot(udta.raw, pf::kUdtaSegments, pf::kUdtaSegmentCount,
                      field, unit, component, width);
}

int64_t read_le(const uint8_t* p, uint8_t width) {
  int64_t v = 0;
  for (int i = 0; i < width; i++) v |= int64_t(p[i]) << (8 * i);
  return v;
}

void write_le(uint8_t* p, uint8_t width, int64_t value) {
  for (int i = 0; i < width; i++) p[i] = uint8_t((value >> (8 * i)) & 0xff);
}

}  // namespace

int pf_udta_field_count(void) { return pf::kUdtaSegmentCount; }

int64_t pf_udta_default_field(int field, int unit, int component) {
  if (field < 0 || field >= pf::kUdtaSegmentCount) return -1;
  const pf::UdtaSegment& seg = pf::kUdtaSegments[field];
  if (component < 0 || component >= seg.components) return -1;
  const int per_unit = seg.elements / seg.components;
  if (unit < 0 || unit >= per_unit) return -1;
  const int at = pf::segment_offset(pf::kUdtaSegments, pf::kUdtaSegmentCount, field) +
                 (unit * seg.components + component) * seg.width;
  if (at < 0 || at + seg.width > pf::kDefaultUdtaSize) return -1;
  int64_t v = 0;
  for (int i = 0; i < seg.width; i++) v |= int64_t(pf::kDefaultUdta[at + i]) << (8 * i);
  return v;
}

const char* pf_udta_field_name(int field) {
  if (field < 0 || field >= pf::kUdtaSegmentCount) return nullptr;
  return pf::kUdtaSegments[field].name;
}

int pf_udta_field_components(int field) {
  if (field < 0 || field >= pf::kUdtaSegmentCount) return 0;
  return pf::kUdtaSegments[field].components;
}

int pf_udta_field_units(int field) {
  if (field < 0 || field >= pf::kUdtaSegmentCount) return 0;
  const pf::UdtaSegment& seg = pf::kUdtaSegments[field];
  return seg.per_unit ? seg.elements / seg.components : 0;
}

int pf_udta_field_width(int field) {
  if (field < 0 || field >= pf::kUdtaSegmentCount) return 0;
  return pf::kUdtaSegments[field].width;
}

int pf_udta_field_kind(int field) { return int(pf::udta_field_kind(field)); }

int pf_udta_field_option_count(int field) {
  int count = 0;
  pf::udta_field_options(field, &count);
  return count;
}

const char* pf_udta_field_option(int field, int index, int* value) {
  int count = 0;
  const pf::UdtaOption* options = pf::udta_field_options(field, &count);
  if (!options || index < 0 || index >= count) return nullptr;
  // A 32-bit mask with bit 31 set would be negative as an int; no flag that
  // high is named, so the cast is lossless for everything in the table.
  if (value) *value = int(options[index].value);
  return options[index].label;
}

int pf_map_shade_stroke(pf_map* map, int x, int y, int w, int h, uint32_t seed,
                        int terrain, const uint8_t* mask) {
  if (!map) return -1;
  pf::Map& m = *map->map;
  if (w <= 0) { x = 0; w = m.width(); }
  if (h <= 0) { y = 0; h = m.height(); }

  pf::CornerGrid& grid = map->grid();
  // A corner on the rectangle's own edge is shared with the tile just outside
  // it, and that tile has to be drawn again or it keeps a flat drawing while
  // one of its corners has gone dark — the ring of unblended tiles a mixed
  // stroke used to leave around itself.
  //
  // So legalisation and the redraw work the same margin a brush stroke uses.
  // Nothing in it is forced to change: the patch below is taken over the whole
  // margin, so only the neighbours the shading reached are re-picked.
  const int margin = int(pf::kTerrainCount);
  const pf::Rect shaded{x, y, x + w - 1, y + h - 1};
  const pf::Rect settled{shaded.x0 - margin, shaded.y0 - margin,
                         shaded.x1 + margin, shaded.y1 + margin};
  // Taken before any shade moves, so a corner the noise left alone keeps the
  // tile the author chose for it rather than being re-picked from the artwork.
  const pf::CornerPatch was(grid, settled);
  // The three pairs the game treats as one terrain each. Forest, rock and the
  // walls have no second shade, so they are left alone.
  const uint8_t pairs[3][2] = {
      {pf::kGroundLight, pf::kGroundDark},
      {pf::kWaterLight, pf::kWaterDark},
      {pf::kCoastLight, pf::kCoastDark},
  };
  // Which pair a corner belongs to, or -1 for one that has no second shade.
  // Forest, rock and the walls are the same drawing either way.
  const auto family_of = [&pairs](uint8_t here) {
    for (int i = 0; i < 3; i++) {
      if (here == pairs[i][0] || here == pairs[i][1]) return i;
    }
    return -1;
  };
  // The only family this call may move. A brush laying dirt has no business
  // re-shading the grass it happens to run alongside.
  const int only = terrain >= 0 ? family_of(uint8_t(terrain)) : -1;
  if (terrain >= 0 && only < 0) return 0;   // a terrain with one shade: nothing to mix

  // A corner belongs to the stroke when any of the four tiles meeting there
  // does. Corners are shared, so asking for the tiles' own corners is the
  // only way to shade a tile at all.
  const auto covered = [&](int cx, int cy) {
    if (!mask) return true;
    for (int ty = cy - 1; ty <= cy; ty++) {
      for (int tx = cx - 1; tx <= cx; tx++) {
        if (tx < x || ty < y || tx >= x + w || ty >= y + h) continue;
        if (mask[size_t(ty - y) * size_t(w) + size_t(tx - x)]) return true;
      }
    }
    return false;
  };

  // Coherent noise, not a coin per corner: flipping each independently changed
  // the shade between neighbours 39% of the time, where Blizzard's own 28
  // multiplayer maps change it a mean of 10.6% (p10 4.5%, p90 17.7%).
  const uint32_t s0 = seed ? seed : 1u;
  const pf::NoiseLayer layers[] = {
      {0.10f, s0, 1.0f},                       // the patches
      {0.25f, s0 ^ 0x9e3779b9u, 0.35f},        // and a broken edge on them
  };
  pf::LayeredNoise noise(layers, 2);

  // The three families are offset from each other in the field so a dark
  // stretch of water does not have to line up with a dark stretch of ground.
  const float offset[3] = {0.0f, 137.0f, 311.0f};

  // Where to cut light from dark, as a percentile of the field rather than a
  // fixed value, so the share does not drift with the shape the noise took.
  // Measured: a mean of 30% of shadeable corners are dark across those 28 maps.
  // What comes out is nearer 20%, because legalize afterwards pulls back a lone
  // dark corner in a light tile — inside the observed range, so left alone.
  constexpr float kDarkShare = 0.30f;
  std::vector<float> samples;
  samples.reserve(size_t(w + 1) * size_t(h + 1));
  for (int cy = y; cy <= y + h; cy++) {
    for (int cx = x; cx <= x + w; cx++) {
      const int i = family_of(grid.get(cx, cy));
      if (i < 0 || (only >= 0 && i != only)) continue;
      samples.push_back(noise.at(float(cx) + offset[i], float(cy) + offset[i]));
    }
  }
  float cut = 0.5f;
  if (!samples.empty()) {
    const size_t at = size_t(float(samples.size() - 1) * kDarkShare);
    std::nth_element(samples.begin(), samples.begin() + long(at), samples.end());
    cut = samples[at];
  }

  for (int cy = y; cy <= y + h; cy++) {
    for (int cx = x; cx <= x + w; cx++) {
      const int i = family_of(grid.get(cx, cy));
      if (i < 0 || (only >= 0 && i != only)) continue;
      if (!covered(cx, cy)) continue;
      const float v = noise.at(float(cx) + offset[i], float(cy) + offset[i]);
      // pairs[i][1] is the dark one; below the cut is the shaded side.
      grid.set(cx, cy, pairs[i][v < cut ? 1 : 0]);
    }
  }

  pf::legalize(grid, settled);
  pf::clear_unsupported_walls(grid, settled);

  const std::vector<uint16_t> before = m.tiles();
  pf::apply_corners(m, grid, settled, map->tiles(), &was);
  pf::rebuild_regions(m);

  int changed = 0;
  const std::vector<uint16_t>& after = m.tiles();
  for (size_t i = 0; i < before.size() && i < after.size(); i++) {
    if (before[i] != after[i]) changed++;
  }
  return changed;
}

int pf_map_randomize_shades(pf_map* map, int x, int y, int w, int h, uint32_t seed) {
  // Every pair, every tile: that is what the menu item says it does.
  return pf_map_shade_stroke(map, x, y, w, h, seed, -1, nullptr);
}

namespace {

/// Every tile a unit of `type` could stand on with `clear` tiles of open
/// ground around it, on a coarse stride so the search stays cheap.
std::vector<std::pair<int, int>> open_spots(pf_map* map, int type, int clear, int stride) {
  const pf::Map& m = *map->map;
  std::vector<std::pair<int, int>> spots;
  for (int ty = clear; ty < m.height() - clear; ty += stride) {
    for (int tx = clear; tx < m.width() - clear; tx += stride) {
      if (pf_map_placement_check(map, tx, ty, type) != PF_PLACE_OK) continue;
      bool room = true;
      for (int dy = -clear; dy <= clear && room; dy++) {
        for (int dx = -clear; dx <= clear && room; dx++) {
          uint8_t q[4];
          pf::decode_tile(m.tile_at(tx + dx, ty + dy), q);
          for (int i = 0; i < 4; i++) {
            if (q[i] != pf::kGroundLight && q[i] != pf::kGroundDark) room = false;
          }
        }
      }
      if (room) spots.push_back({tx, ty});
    }
  }
  return spots;
}

/// Index of the spot furthest from everything in `taken`.
size_t furthest_from(const std::vector<std::pair<int, int>>& spots,
                     const std::vector<std::pair<int, int>>& taken) {
  size_t best = 0;
  long long best_score = -1;
  for (size_t i = 0; i < spots.size(); i++) {
    long long score = 1LL << 40;
    for (const auto& t : taken) {
      const long long dx = spots[i].first - t.first;
      const long long dy = spots[i].second - t.second;
      score = std::min(score, dx * dx + dy * dy);
    }
    if (score > best_score) { best_score = score; best = i; }
  }
  return best;
}

}  // namespace

int pf_map_place_gold_mines(pf_map* map, int count) {
  if (!map || count <= 0) return 0;
  // A gold mine is 3x3 and wants one tile of room around it. Asking for more
  // meant no mines at all on a map of narrow islands, where start locations
  // still fitted — a stricter standard than the base the mine feeds.
  std::vector<std::pair<int, int>> spots = open_spots(map, 0x5c, 2, 2);
  if (spots.empty()) return 0;

  std::vector<std::pair<int, int>> taken;
  for (const pf::Unit& u : map->map->units()) taken.push_back({int(u.x), int(u.y)});

  int placed = 0;
  while (placed < count && !spots.empty()) {
    const size_t best = furthest_from(spots, taken);
    const auto spot = spots[best];
    spots.erase(spots.begin() + long(best));
    // 40,000 gold, stored as the amount over 2500 — the commonest in the
    // shipped maps. Owner 15 is neutral.
    if (pf_map_add_unit(map, spot.first, spot.second, 0x5c, 15, 16) < 0) continue;
    taken.push_back(spot);
    placed++;
  }
  return placed;
}

int pf_map_place_start_locations(pf_map* map) {
  if (!map) return -1;
  pf::Map& m = *map->map;

  // Which slots need one. A player nobody controls needs no start location,
  // and one that already has a start location is left where it was put.
  bool wanted[pf::kPlayerCount] = {};
  int need = 0;
  for (int p = 0; p < pf::kPlayerCount; p++) {
    if (pf_map_owner(map, p) == PF_OWNER_NOBODY) continue;
    wanted[p] = true;
    need++;
  }
  for (const pf::Unit& u : m.units()) {
    if ((u.type == 94 || u.type == 95) && u.owner < pf::kPlayerCount && wanted[u.owner]) {
      wanted[u.owner] = false;
      need--;
    }
  }
  if (need <= 0) return 0;

  // Every square of open ground that a start location could stand on, far
  // enough in that a base has room to grow around it.
  const int margin = 4;
  std::vector<std::pair<int, int>> spots;
  for (int ty = margin; ty < m.height() - margin; ty++) {
    for (int tx = margin; tx < m.width() - margin; tx++) {
      if (pf_map_placement_check(map, tx, ty, 94) != PF_PLACE_OK) continue;
      bool clear = true;
      for (int dy = -2; dy <= 2 && clear; dy++) {
        for (int dx = -2; dx <= 2 && clear; dx++) {
          uint8_t q[4];
          pf::decode_tile(m.tile_at(std::min(std::max(tx + dx, 0), m.width() - 1),
                                    std::min(std::max(ty + dy, 0), m.height() - 1)), q);
          for (int i = 0; i < 4; i++) {
            if (q[i] != pf::kGroundLight && q[i] != pf::kGroundDark) clear = false;
          }
        }
      }
      if (clear) spots.push_back({tx, ty});
    }
  }
  if (spots.empty()) return 0;

  // Taken positions seed the spread, so filling one gap on a laid-out map puts
  // it away from the starts that are already there.
  std::vector<std::pair<int, int>> taken;
  for (const pf::Unit& u : m.units()) {
    if (u.type == 94 || u.type == 95) taken.push_back({int(u.x), int(u.y)});
  }

  // A start location wants a gold mine within reach. When there are mines,
  // candidates too far from one are dropped; when that leaves nothing, the
  // whole map is used rather than placing none at all.
  std::vector<std::pair<int, int>> mines;
  for (const pf::Unit& u : m.units()) {
    if (u.type == 0x5c) mines.push_back({int(u.x), int(u.y)});
  }
  if (!mines.empty()) {
    // Within reach of a mine but not on top of one: a base needs room between
    // the town hall and the gold. The floor is measured — 535 shipped start
    // locations sit a median 9.2 tiles from their nearest mine, a quarter
    // inside 5.4 — so six is a little roomier than Blizzard's habit.
    constexpr long long kReach = 20 * 20;
    constexpr long long kElbowRoom = 6 * 6;
    std::vector<std::pair<int, int>> near;
    for (const auto& spot : spots) {
      bool in_reach = false, too_close = false;
      for (const auto& mine : mines) {
        const long long dx = spot.first - mine.first;
        const long long dy = spot.second - mine.second;
        const long long d2 = dx * dx + dy * dy;
        if (d2 < kElbowRoom) { too_close = true; break; }
        if (d2 <= kReach) in_reach = true;
      }
      if (in_reach && !too_close) near.push_back(spot);
    }
    // Nothing satisfying both is a small or crowded map, and a start location
    // somewhere beats none at all — so the band is a preference, not a rule.
    if (!near.empty()) spots = near;
  }

  int placed = 0;
  for (int p = 0; p < pf::kPlayerCount && placed < need; p++) {
    if (!wanted[p]) continue;

    // Farthest-first: whichever candidate is furthest from the start locations
    // already chosen. Crude, but it puts four starts in four corners, which is
    // what a person would have done.
    std::vector<std::pair<int, int>> others;
    for (const auto& t : taken) {
      // Distance from other *starts*, not from the mines they are meant to sit
      // beside — spreading away from the mines would defeat the point.
      bool is_mine = false;
      for (const auto& mine : mines) {
        if (mine == t) { is_mine = true; break; }
      }
      if (!is_mine) others.push_back(t);
    }
    const size_t best = furthest_from(spots, others);

    const int type = pf_map_race(map, p) == PF_RACE_ORC ? 95 : 94;
    if (pf_map_add_unit(map, spots[best].first, spots[best].second, type, p, 0) < 0) {
      continue;
    }
    taken.push_back(spots[best]);
    spots.erase(spots.begin() + long(best));
    placed++;
    if (spots.empty()) break;
  }
  return placed;
}

pf_status pf_map_add_unit_data(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  pf::UnitData& udta = map->map->unit_data_mut();
  if (udta.present) return PF_OK;
  // Seeded with the game's own table, so adding the section only makes the
  // values editable and changes nothing about how the map plays.
  udta.raw.assign(pf::kDefaultUdta, pf::kDefaultUdta + pf::kDefaultUdtaSize);
  udta.present = true;
  udta.use_default = false;
  // Footprints live inside the payload, so installing one means re-reading them.
  map->map->refresh_unit_sizes();
  return PF_OK;
}

pf_status pf_map_add_upgrade_data(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  std::vector<uint8_t>& ugrd = map->map->upgrade_data_mut();
  if (!ugrd.empty()) return PF_OK;
  ugrd.assign(pf::kDefaultUgrd, pf::kDefaultUgrd + pf::kDefaultUgrdSize);
  return PF_OK;
}

int pf_map_has_unit_data(const pf_map* map) {
  return map && map->map->unit_data().present ? 1 : 0;
}

int64_t pf_map_unit_field(const pf_map* map, int field, int unit, int component) {
  if (!map) return -1;
  auto& udta = const_cast<pf::Map*>(map->map.get())->unit_data_mut();
  uint8_t width = 0;
  uint8_t* slot = udta_slot(udta, field, unit, component, &width);
  return slot ? read_le(slot, width) : -1;
}

pf_status pf_map_set_unit_field(pf_map* map, int field, int unit, int component,
                                int64_t value) {
  if (!map) return PF_ERR_INVALID_ARG;
  pf::UnitData& udta = map->map->unit_data_mut();
  uint8_t width = 0;
  uint8_t* slot = udta_slot(udta, field, unit, component, &width);
  if (!slot) return PF_ERR_OUT_OF_RANGE;
  write_le(slot, width, value);

  // unitSize drives the footprint cache the editor lays units out with.
  if (pf::kUdtaSegments[field].name == std::string("unitSize")) {
    if (component == 0) udta.size_x[unit] = uint16_t(value);
    else udta.size_y[unit] = uint16_t(value);
  }
  return PF_OK;
}

// ----------------------------------------------------------- upgrade data

int pf_ugrd_field_count(void) { return pf::kUgrdSegmentCount; }

int64_t pf_ugrd_default_field(int field, int upgrade) {
  if (field < 0 || field >= pf::kUgrdSegmentCount) return -1;
  const pf::UdtaSegment& seg = pf::kUgrdSegments[field];
  if (upgrade < 0 || upgrade >= seg.elements) return -1;
  const int at = pf::segment_offset(pf::kUgrdSegments, pf::kUgrdSegmentCount, field) +
                 upgrade * seg.width;
  if (at < 0 || at + seg.width > pf::kDefaultUgrdSize) return -1;
  int64_t v = 0;
  for (int i = 0; i < seg.width; i++) v |= int64_t(pf::kDefaultUgrd[at + i]) << (8 * i);
  return v;
}

const char* pf_ugrd_field_name(int field) {
  if (field < 0 || field >= pf::kUgrdSegmentCount) return nullptr;
  return pf::kUgrdSegments[field].name;
}

int pf_ugrd_field_width(int field) {
  if (field < 0 || field >= pf::kUgrdSegmentCount) return 0;
  return pf::kUgrdSegments[field].width;
}

int pf_ugrd_field_entries(int field) {
  if (field < 0 || field >= pf::kUgrdSegmentCount) return 0;
  const pf::UdtaSegment& seg = pf::kUgrdSegments[field];
  return seg.per_unit ? seg.elements : 0;
}

int pf_map_has_upgrade_data(const pf_map* map) {
  return map && !map->map->upgrade_data().empty() ? 1 : 0;
}

int64_t pf_map_upgrade_field(const pf_map* map, int field, int upgrade) {
  if (!map) return -1;
  auto& raw = const_cast<pf::Map*>(map->map.get())->upgrade_data_mut();
  uint8_t width = 0;
  uint8_t* slot = segment_slot(raw, pf::kUgrdSegments, pf::kUgrdSegmentCount,
                               field, upgrade, 0, &width);
  return slot ? read_le(slot, width) : -1;
}

pf_status pf_map_set_upgrade_field(pf_map* map, int field, int upgrade, int64_t value) {
  if (!map) return PF_ERR_INVALID_ARG;
  uint8_t width = 0;
  uint8_t* slot = segment_slot(map->map->upgrade_data_mut(), pf::kUgrdSegments,
                               pf::kUgrdSegmentCount, field, upgrade, 0, &width);
  if (!slot) return PF_ERR_OUT_OF_RANGE;
  write_le(slot, width, value);
  return PF_OK;
}

// ----------------------------------------------------------- restrictions

pf_status pf_map_set_tile(pf_map* map, int x, int y, int tile) {
  if (!map || tile < 0 || tile > 0xffff) return PF_ERR_INVALID_ARG;
  pf::Map& m = *map->map;
  if (x < 0 || y < 0 || x >= m.width() || y >= m.height()) return PF_ERR_OUT_OF_RANGE;
  m.tiles()[size_t(y) * size_t(m.width()) + size_t(x)] = uint16_t(tile);
  // The corner grid is now stale where this tile is, so drop it: the next
  // paint rebuilds it from the map, this tile included.
  map->corners.reset();
  return PF_OK;
}

int pf_component_kind(size_t length) {
  if (length == size_t(pf::kUdtaSize) || length == size_t(pf::kUdtaSizeWithSwamp)) {
    return PF_COMPONENT_UDTA;
  }
  if (length == size_t(pf::kUgrdSize)) return PF_COMPONENT_UGRD;
  if (length == size_t(pf::kUdtaSize) + size_t(pf::kUgrdSize) ||
      length == size_t(pf::kUdtaSizeWithSwamp) + size_t(pf::kUgrdSize)) {
    return PF_COMPONENT_BOTH;
  }
  if (length == size_t(pf::kAlowSize)) return PF_COMPONENT_ALOW;
  return PF_COMPONENT_UNKNOWN;
}

pf_status pf_map_import_component(pf_map* map, const uint8_t* data, size_t len) {
  if (!map || !data) return PF_ERR_INVALID_ARG;
  const int kind = pf_component_kind(len);
  pf::Map& m = *map->map;
  switch (kind) {
    case PF_COMPONENT_UDTA:
      m.unit_data_mut().raw.assign(data, data + len);
      m.unit_data_mut().present = true;
      return PF_OK;
    case PF_COMPONENT_UGRD:
      m.upgrade_data_mut().assign(data, data + len);
      return PF_OK;
    case PF_COMPONENT_ALOW:
      m.restrictions_mut().assign(data, data + len);
      return PF_OK;
    case PF_COMPONENT_BOTH: {
      const size_t udta_len = len - size_t(pf::kUgrdSize);
      m.unit_data_mut().raw.assign(data, data + udta_len);
      m.unit_data_mut().present = true;
      m.upgrade_data_mut().assign(data + udta_len, data + len);
      return PF_OK;
    }
    default:
      return PF_ERR_MALFORMED;
  }
}

uint8_t* pf_map_export_component(const pf_map* map, int component, size_t* out_len) {
  if (out_len) *out_len = 0;
  if (!map) return nullptr;
  const pf::Map& m = *map->map;

  std::vector<uint8_t> bytes;
  if (component == PF_COMPONENT_UDTA && m.unit_data().present) {
    bytes = m.unit_data().raw;
  } else if (component == PF_COMPONENT_UGRD && !m.upgrade_data().empty()) {
    bytes = m.upgrade_data();
  } else if (component == PF_COMPONENT_ALOW && !m.restrictions().empty()) {
    bytes = m.restrictions();
  } else if (component == PF_COMPONENT_BOTH &&
             m.unit_data().present && !m.upgrade_data().empty()) {
    bytes = m.unit_data().raw;
    bytes.insert(bytes.end(), m.upgrade_data().begin(), m.upgrade_data().end());
  }
  if (bytes.empty()) return nullptr;

  auto* out = static_cast<uint8_t*>(std::malloc(bytes.size()));
  if (!out) return nullptr;
  std::memcpy(out, bytes.data(), bytes.size());
  if (out_len) *out_len = bytes.size();
  return out;
}

pf_status pf_map_add_restrictions(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  std::vector<uint8_t>& alow = map->map->restrictions_mut();
  if (!alow.empty()) return PF_OK;
  // Adding the section must not change how the map plays, and two of the six
  // blocks say "researching" rather than "allowed", so that is not all-ones.
  alow.assign(size_t(pf::kAlowSize), 0);
  size_t at = 0;
  for (int block = 0; block < pf::kAlowBlocks; block++) {
    const uint32_t v = pf::kDefaultAlowBlock[block];
    for (int player = 0; player < pf::kPlayerCount; player++) {
      alow[at++] = uint8_t(v & 0xff);
      alow[at++] = uint8_t((v >> 8) & 0xff);
      alow[at++] = uint8_t((v >> 16) & 0xff);
      alow[at++] = uint8_t((v >> 24) & 0xff);
    }
  }
  return PF_OK;
}

pf_status pf_map_clear_restrictions(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  // Dropped rather than filled with the unrestricted table: the section is
  // optional, and a map that carries none is what 1338 of the corpus's 1378 do.
  map->map->restrictions_mut().clear();
  return PF_OK;
}

int pf_map_has_restrictions(const pf_map* map) {
  return map && !map->map->restrictions().empty() ? 1 : 0;
}

const char* pf_alow_block_name(int block) {
  if (block < 0 || block >= pf::kAlowBlocks) return nullptr;
  return pf::kAlowBlockNames[block];
}

int64_t pf_alow_default(int block) {
  if (block < 0 || block >= pf::kAlowBlocks) return -1;
  return int64_t(pf::kDefaultAlowBlock[block]);
}

int64_t pf_map_allow(const pf_map* map, int block, int player) {
  if (!map || block < 0 || block >= pf::kAlowBlocks) return -1;
  if (player < 0 || player >= pf::kPlayerCount) return -1;
  const auto& raw = map->map->restrictions();
  const size_t at = (size_t(block) * pf::kPlayerCount + size_t(player)) * 4;
  if (at + 4 > raw.size()) return -1;
  return read_le(raw.data() + at, 4);
}

pf_status pf_map_set_allow(pf_map* map, int block, int player, int64_t bits) {
  if (!map || block < 0 || block >= pf::kAlowBlocks) return PF_ERR_OUT_OF_RANGE;
  if (player < 0 || player >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  auto& raw = map->map->restrictions_mut();
  const size_t at = (size_t(block) * pf::kPlayerCount + size_t(player)) * 4;
  // A map without ALOW must stay without it rather than gain an empty one.
  if (at + 4 > raw.size()) return PF_ERR_OUT_OF_RANGE;
  write_le(raw.data() + at, 4, bits);
  return PF_OK;
}

int pf_map_resize(pf_map* map, int width, int height, int offset_x, int offset_y,
                  pf_status* status) {
  if (!map) { set_status(status, PF_ERR_INVALID_ARG); return -1; }
  pf::Status s = pf::Status::Ok;
  const int dropped = map->map->resize(width, height, offset_x, offset_y, s);
  set_status(status, to_c(s));
  if (dropped < 0) return -1;
  // Everything derived from the old grid is now wrong.
  map->corners.reset();
  map->index.reset();
  pf::rebuild_regions(*map->map);
  return dropped;
}

// ------------------------------------------------------------- placement

int pf_unit_domain(int unit_id) { return int(pf::default_unit_domain(unit_id)); }

int pf_solid_tile(int terrain, int variation) {
  return pf::solid_tile(uint8_t(terrain), variation);
}

uint32_t pf_unit_flags(int unit_id) {
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return 0;
  static const int flags_field = pf::udta_field_index("flags");
  const int at = pf::segment_offset(pf::kUdtaSegments, pf::kUdtaSegmentCount, flags_field) +
                 unit_id * 4;
  if (at < 0 || at + 4 > pf::kDefaultUdtaSize) return 0;
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= uint32_t(pf::kDefaultUdta[at + i]) << (8 * i);
  return v;
}

int pf_map_placement_check(const pf_map* map, int x, int y, int type) {
  if (!map) return PF_PLACE_OUT_OF_BOUNDS;
  const pf::Map& m = *map->map;
  if (type < 0 || type >= pf::kUnitCount) return PF_PLACE_OUT_OF_BOUNDS;

  int fw = 1, fh = 1;
  m.unit_footprint(type, fw, fh);
  if (x < 0 || y < 0 || x + fw > m.width() || y + fh > m.height()) {
    return PF_PLACE_OUT_OF_BOUNDS;
  }

  const pf::UnitDomain domain = pf::default_unit_domain(type);
  const uint32_t flags = pf_unit_flags(type);
  // Only land buildings. Oil patches and platforms carry the building flag too
  // but belong on water, and their domain already says so.
  const bool building_flag = (flags >> 5) & 1;
  const bool building = building_flag && domain == pf::kDomainLand;
  const bool shore = (flags >> 16) & 1;
  const bool oil = (flags >> 11) & 1;

  // A building needs buildable ground under every tile it covers: across the
  // 529 shipped maps, not one of 17,838 non-shore buildings touches a coast
  // quadrant, forest or rock. A shore building is the exception and straddles
  // the shoreline instead — all 461 shipyards, foundries and refineries touch
  // both water and coast, and not one is inland.
  if (building_flag && shore) {
    bool any_water = false, any_coast = false, any_blocked = false;
    for (int ty = y; ty < y + fh; ty++) {
      for (int tx = x; tx < x + fw; tx++) {
        uint8_t q[4];
        pf::decode_tile(m.tile_at(tx, ty), q);
        for (int i = 0; i < 4; i++) {
          any_water |= q[i] == pf::kWaterDark || q[i] == pf::kWaterLight;
          any_coast |= q[i] == pf::kCoastDark || q[i] == pf::kCoastLight;
          any_blocked |= q[i] == pf::kForest || q[i] == pf::kMountain ||
                         q[i] == pf::kWallHuman || q[i] == pf::kWallOrc;
        }
      }
    }
    if (any_blocked) return PF_PLACE_BLOCKED;
    if (!any_water || !any_coast) return PF_PLACE_NEEDS_SHORE;
    return PF_PLACE_OK;
  }

  if (building && !shore && !oil) {
    int fw2 = 1, fh2 = 1;
    m.unit_footprint(type, fw2, fh2);
    for (int ty = y; ty < y + fh2; ty++) {
      for (int tx = x; tx < x + fw2; tx++) {
        uint8_t q[4];
        pf::decode_tile(m.tile_at(tx, ty), q);
        for (int i = 0; i < 4; i++) {
          // Ground and nothing else. A wall is a thing standing on the tile
          // rather than a kind of ground, and the game will not put a building
          // through one.
          const bool buildable =
              q[i] == pf::kGroundLight || q[i] == pf::kGroundDark;
          if (!buildable) return PF_PLACE_NEEDS_GROUND;
        }
      }
    }
    // A hall against a mine cannot be worked: the peasants need a lane to walk
    // in and out of. Three tiles of clearance, measured; see
    // overrides/hall_clearance.cpp.
    if (pf::unit_needs_mine_clearance(type)) {
      const int clear = pf::mine_clearance_tiles();
      for (const pf::Unit& u : m.units()) {
        if (!((pf_unit_flags(u.type) >> 22) & 1)) continue;   // gold mines only
        int mw = 1, mh = 1;
        m.unit_footprint(u.type, mw, mh);
        // Gap between the two boxes, in tiles. Negative when they overlap, so
        // the comparison covers overlapping placements too.
        const int dx = std::max(x - (u.x + mw), u.x - (x + fw2));
        const int dy = std::max(y - (u.y + mh), u.y - (y + fh2));
        if (std::max(dx, dy) < clear) return PF_PLACE_TOO_NEAR_MINE;
      }
    }
    return PF_PLACE_OK;
  }

  if (domain == pf::kDomainAir || domain == pf::kDomainAny) return PF_PLACE_OK;

  // Every tile of the footprint must suit the unit. A quadrant is water when
  // it is one of the two water classes; anything else is standable ground.
  for (int ty = y; ty < y + fh; ty++) {
    for (int tx = x; tx < x + fw; tx++) {
      uint8_t q[4];
      pf::decode_tile(m.tile_at(tx, ty), q);
      bool any_water = false, any_land = false, any_blocked = false;
      for (int i = 0; i < 4; i++) {
        const bool water = q[i] == pf::kWaterDark || q[i] == pf::kWaterLight;
        any_water |= water;
        any_land |= !water;
        // Terrain nothing walks through: not one of the 19,677 units in the
        // shipped maps stands on forest, rock or wall unless it flies.
        any_blocked |= q[i] == pf::kForest || q[i] == pf::kMountain ||
                       q[i] == pf::kWallHuman || q[i] == pf::kWallOrc;
      }
      if (domain == pf::kDomainLand && any_blocked) return PF_PLACE_BLOCKED;
      if (domain == pf::kDomainWater && any_land) return PF_PLACE_NEEDS_WATER;
      if (domain == pf::kDomainLand && any_water) return PF_PLACE_NEEDS_LAND;
    }
  }
  return PF_PLACE_OK;
}

int pf_map_placement_check_ex(const pf_map* map, int x, int y, int type,
                              const int* ignore, int ignore_count) {
  if (!map) return PF_PLACE_OUT_OF_BOUNDS;
  const pf::Map& m = *map->map;
  if (type < 0 || type >= pf::kUnitCount) return PF_PLACE_OUT_OF_BOUNDS;

  int fw = 1, fh = 1;
  m.unit_footprint(type, fw, fh);
  // Bounds first and never lifted: a unit off the map is not a placement the
  // format can even hold, whatever the options say.
  if (x < 0 || y < 0 || x + fw > m.width() || y + fh > m.height()) {
    return PF_PLACE_OUT_OF_BOUNDS;
  }

  // Then the two rules about the map's other contents. This order because
  // "something is already there" is the more useful thing to be told when both
  // are true.
  if (!map->allow_stacked_units) {
    const int count = int(m.units().size());
    for (int i = 0; i < count; i++) {
      bool skip = false;
      for (int k = 0; k < ignore_count && !skip; k++) skip = ignore[k] == i;
      if (skip) continue;   // a unit being moved must not block itself
      const pf::Unit& u = m.units()[size_t(i)];
      int ow = 1, oh = 1;
      m.unit_footprint(u.type, ow, oh);
      const bool apart = int(u.x) + ow <= x || x + fw <= int(u.x) ||
                         int(u.y) + oh <= y || y + fh <= int(u.y);
      if (!apart) return PF_PLACE_OCCUPIED;
    }
  }
  if (!map->allow_edge_placement &&
      (x <= 0 || y <= 0 || x + fw >= m.width() || y + fh >= m.height())) {
    return PF_PLACE_ON_EDGE;
  }

  // And last the terrain, which is the one the illegal-placement hatch lifts.
  if (map->allow_illegal_placement) return PF_PLACE_OK;
  return pf_map_placement_check(map, x, y, type);
}

pf_status pf_map_set_variation_policy(pf_map* map, int policy) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (policy < PF_VARIATION_ANY || policy > PF_VARIATION_DECORATED) {
    return PF_ERR_OUT_OF_RANGE;
  }
  if (map->variation_policy == policy) return PF_OK;
  map->variation_policy = policy;
  map->index.reset();   // rebuilt against the new policy
  return PF_OK;
}

int pf_map_variation_policy(const pf_map* map) {
  return map ? map->variation_policy : PF_VARIATION_ANY;
}

void pf_map_set_allow_stacked_units(pf_map* map, int allow) {
  if (map) map->allow_stacked_units = allow != 0;
}

int pf_map_allows_stacked_units(const pf_map* map) {
  return map && map->allow_stacked_units ? 1 : 0;
}

void pf_map_set_allow_edge_placement(pf_map* map, int allow) {
  if (map) map->allow_edge_placement = allow != 0;
}

int pf_map_allows_edge_placement(const pf_map* map) {
  return map && map->allow_edge_placement ? 1 : 0;
}

void pf_map_set_allow_illegal_placement(pf_map* map, int allow) {
  if (map) map->allow_illegal_placement = allow != 0;
}

int pf_map_allows_illegal_placement(const pf_map* map) {
  return map && map->allow_illegal_placement ? 1 : 0;
}

// ------------------------------------------------------------- clipboard

/// A detached fragment of a map.
///
/// Terrain is held as *corner terrains*, not tile values: a rotated tile is not
/// the same tile, so turning MTXM values would produce tiles whose artwork does
/// not match their shape. Corners rotate meaningfully.
struct pf_clipboard {
  int width = 0;   ///< tiles
  int height = 0;
  bool has_terrain = false;
  std::vector<uint8_t> corners;  ///< (width+1) * (height+1)
  std::vector<uint8_t> walls;    ///< width * height

  /// A unit plus the footprint it had when copied. The fragment is detached
  /// from any map, so it cannot look footprints up later — and rotation needs
  /// the extent, not just the anchor.
  struct ClipUnit {
    pf::Unit unit;
    uint8_t fw = 1;
    uint8_t fh = 1;
  };
  std::vector<ClipUnit> units;   ///< positions relative to the fragment

  /// Which of the rectangle's tiles the fragment actually carries, or empty for
  /// all of them: a selection made with shift and alt is not a rectangle, and a
  /// fragment that only knew its bounding box pasted the ground between two
  /// separately picked squares as well.
  ///
  /// Rectangular even so, because rotation needs an extent and a hole is
  /// cheaper to carry than a shape is to describe.
  std::vector<uint8_t> mask;   ///< width * height

  uint8_t corner(int x, int y) const {
    return corners[size_t(y) * size_t(width + 1) + size_t(x)];
  }
  void set_corner(int x, int y, uint8_t v) {
    corners[size_t(y) * size_t(width + 1) + size_t(x)] = v;
  }
  bool included(int x, int y) const {
    if (mask.empty()) return true;
    return mask[size_t(y) * size_t(width) + size_t(x)] != 0;
  }
};

namespace {

/// Footprint of a unit in the fragment's own terms.
void fragment_footprint(const pf::Map& map, const pf::Unit& u, int& w, int& h) {
  w = 1;
  h = 1;
  map.unit_footprint(u.type, w, h);
}

}  // namespace

pf_clipboard* pf_clipboard_copy(const pf_map* map, int x, int y, int w, int h,
                                int include_terrain, int include_units) {
  return pf_clipboard_copy_masked(map, x, y, w, h, nullptr, include_terrain,
                                  include_units);
}

pf_clipboard* pf_clipboard_copy_masked(const pf_map* map, int x, int y, int w,
                                       int h, const uint8_t* mask,
                                       int include_terrain, int include_units) {
  if (!map || w <= 0 || h <= 0) return nullptr;
  const pf::Map& m = *map->map;
  if (x < 0 || y < 0 || x + w > m.width() || y + h > m.height()) return nullptr;

  auto* clip = new pf_clipboard();
  clip->width = w;
  clip->height = h;

  if (mask) {
    // A mask that turns nothing down is no mask at all, and carrying it would
    // put every paste through the slower path for no difference in the result.
    const size_t tiles = size_t(w) * size_t(h);
    bool solid = true;
    for (size_t i = 0; i < tiles && solid; i++) solid = mask[i] != 0;
    if (!solid) clip->mask.assign(mask, mask + tiles);
  }

  if (include_terrain) {
    clip->has_terrain = true;
    const pf::CornerGrid grid = pf::CornerGrid::from_map(m);
    clip->corners.assign(size_t(w + 1) * size_t(h + 1), 0);
    for (int cy = 0; cy <= h; cy++) {
      for (int cx = 0; cx <= w; cx++) {
        clip->set_corner(cx, cy, grid.get(x + cx, y + cy));
      }
    }
    clip->walls.assign(size_t(w) * size_t(h), 0);
    for (int ty = 0; ty < h; ty++) {
      for (int tx = 0; tx < w; tx++) {
        clip->walls[size_t(ty) * size_t(w) + size_t(tx)] = grid.wall_at(x + tx, y + ty);
      }
    }
  }

  if (include_units) {
    for (const pf::Unit& u : m.units()) {
      int fw = 1, fh = 1;
      fragment_footprint(m, u, fw, fh);
      // Only units lying wholly inside the rectangle come along.
      if (int(u.x) < x || int(u.y) < y ||
          int(u.x) + fw > x + w || int(u.y) + fh > y + h) {
        continue;
      }
      // And wholly on tiles the mask kept. Half a keep is not a keep, and a
      // unit standing in a hole is one the user did not select.
      if (!clip->mask.empty()) {
        bool whole = true;
        for (int ty = 0; ty < fh && whole; ty++) {
          for (int tx = 0; tx < fw && whole; tx++) {
            whole = clip->included(int(u.x) - x + tx, int(u.y) - y + ty);
          }
        }
        if (!whole) continue;
      }
      pf_clipboard::ClipUnit entry;
      entry.unit = u;
      entry.unit.x = uint16_t(int(u.x) - x);
      entry.unit.y = uint16_t(int(u.y) - y);
      entry.fw = uint8_t(fw);
      entry.fh = uint8_t(fh);
      clip->units.push_back(entry);
    }
  }
  return clip;
}

void pf_clipboard_free(pf_clipboard* clip) { delete clip; }

int pf_clipboard_width(const pf_clipboard* clip) { return clip ? clip->width : 0; }
int pf_clipboard_height(const pf_clipboard* clip) { return clip ? clip->height : 0; }
int pf_clipboard_tile_included(const pf_clipboard* clip, int x, int y) {
  if (!clip) return 0;
  if (x < 0 || y < 0 || x >= clip->width || y >= clip->height) return 0;
  return clip->included(x, y) ? 1 : 0;
}

int pf_clipboard_corner(const pf_clipboard* clip, int x, int y) {
  if (!clip || !clip->has_terrain) return -1;
  if (x < 0 || y < 0 || x > clip->width || y > clip->height) return -1;
  return clip->corner(x, y);
}

int pf_map_tile_for_corners(pf_map* map, const uint8_t* corners, uint32_t salt) {
  if (!map || !corners) return -1;
  // The map's own index, so the answer is a tile the attached tileset can
  // actually draw rather than one the corner model merely permits.
  return map->tiles().lookup(corners, salt);
}

int pf_clipboard_unit_count(const pf_clipboard* clip) {
  return clip ? int(clip->units.size()) : 0;
}
pf_status pf_clipboard_unit(const pf_clipboard* clip, int index, pf_unit* out) {
  if (!clip || !out) return PF_ERR_INVALID_ARG;
  if (index < 0 || size_t(index) >= clip->units.size()) return PF_ERR_OUT_OF_RANGE;
  const auto& e = clip->units[size_t(index)];
  out->x = e.unit.x;
  out->y = e.unit.y;
  out->type = e.unit.type;
  out->owner = e.unit.owner;
  out->value = e.unit.value;
  return PF_OK;
}

int pf_clipboard_has_terrain(const pf_clipboard* clip) {
  return clip && clip->has_terrain ? 1 : 0;
}

pf_status pf_clipboard_flip(pf_clipboard* clip) {
  if (!clip) return PF_ERR_INVALID_ARG;
  const int w = clip->width, h = clip->height;
  if (clip->has_terrain) {
    std::vector<uint8_t> corners = clip->corners;
    for (int cy = 0; cy <= h; cy++) {
      for (int cx = 0; cx <= w; cx++) {
        clip->set_corner(cx, cy, corners[size_t(cy) * size_t(w + 1) + size_t(w - cx)]);
      }
    }
    std::vector<uint8_t> walls = clip->walls;
    for (int ty = 0; ty < h; ty++) {
      for (int tx = 0; tx < w; tx++) {
        clip->walls[size_t(ty) * size_t(w) + size_t(tx)] =
            walls[size_t(ty) * size_t(w) + size_t(w - 1 - tx)];
      }
    }
  }
  // The holes turn with the fragment, whether or not it carries terrain: a
  // units-only fragment has them too, and a mask left facing the old way would
  // put them over tiles the rotation had moved something else onto.
  if (!clip->mask.empty()) {
    std::vector<uint8_t> mask = clip->mask;
    for (int ty = 0; ty < h; ty++) {
      for (int tx = 0; tx < w; tx++) {
        clip->mask[size_t(ty) * size_t(w) + size_t(tx)] =
            mask[size_t(ty) * size_t(w) + size_t(w - 1 - tx)];
      }
    }
  }
  // Flip the unit's box, not just its anchor.
  for (auto& e : clip->units) e.unit.x = uint16_t(w - int(e.unit.x) - e.fw);
  return PF_OK;
}

pf_status pf_clipboard_mirror(pf_clipboard* clip) {
  if (!clip) return PF_ERR_INVALID_ARG;
  const int w = clip->width, h = clip->height;
  if (clip->has_terrain) {
    std::vector<uint8_t> corners = clip->corners;
    for (int cy = 0; cy <= h; cy++) {
      for (int cx = 0; cx <= w; cx++) {
        clip->set_corner(cx, cy, corners[size_t(h - cy) * size_t(w + 1) + size_t(cx)]);
      }
    }
    std::vector<uint8_t> walls = clip->walls;
    for (int ty = 0; ty < h; ty++) {
      for (int tx = 0; tx < w; tx++) {
        clip->walls[size_t(ty) * size_t(w) + size_t(tx)] =
            walls[size_t(h - 1 - ty) * size_t(w) + size_t(tx)];
      }
    }
  }
  if (!clip->mask.empty()) {
    std::vector<uint8_t> mask = clip->mask;
    for (int ty = 0; ty < h; ty++) {
      for (int tx = 0; tx < w; tx++) {
        clip->mask[size_t(ty) * size_t(w) + size_t(tx)] =
            mask[size_t(h - 1 - ty) * size_t(w) + size_t(tx)];
      }
    }
  }
  for (auto& e : clip->units) e.unit.y = uint16_t(h - int(e.unit.y) - e.fh);
  return PF_OK;
}

pf_status pf_clipboard_rotate(pf_clipboard* clip, int quarter_turns) {
  if (!clip) return PF_ERR_INVALID_ARG;
  int turns = quarter_turns % 4;
  if (turns < 0) turns += 4;

  for (int t = 0; t < turns; t++) {
    const int w = clip->width, h = clip->height;
    const int nw = h, nh = w;   // dimensions swap

    if (clip->has_terrain) {
      // Clockwise: the corner at (cx, cy) lands at (h - cy, cx).
      std::vector<uint8_t> corners(size_t(nw + 1) * size_t(nh + 1), 0);
      for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
          const int dx = h - cy, dy = cx;
          corners[size_t(dy) * size_t(nw + 1) + size_t(dx)] =
              clip->corners[size_t(cy) * size_t(w + 1) + size_t(cx)];
        }
      }
      std::vector<uint8_t> walls(size_t(nw) * size_t(nh), 0);
      for (int ty = 0; ty < h; ty++) {
        for (int tx = 0; tx < w; tx++) {
          const int dx = h - 1 - ty, dy = tx;
          walls[size_t(dy) * size_t(nw) + size_t(dx)] =
              clip->walls[size_t(ty) * size_t(w) + size_t(tx)];
        }
      }
      clip->corners = std::move(corners);
      clip->walls = std::move(walls);
    }

    // Outside the has_terrain block: a units-only fragment carries holes too,
    // and its mask is what decided which units came along.
    if (!clip->mask.empty()) {
      std::vector<uint8_t> mask(size_t(nw) * size_t(nh), 0);
      for (int ty = 0; ty < h; ty++) {
        for (int tx = 0; tx < w; tx++) {
          const int dx = h - 1 - ty, dy = tx;
          mask[size_t(dy) * size_t(nw) + size_t(dx)] =
              clip->mask[size_t(ty) * size_t(w) + size_t(tx)];
        }
      }
      clip->mask = std::move(mask);
    }

    // A unit occupies a box, so rotate the box and take its new top-left.
    // Every Warcraft II footprint happens to be square, but the extent is
    // carried along so a non-square one would rotate correctly too.
    for (auto& e : clip->units) {
      const int ux = int(e.unit.x), uy = int(e.unit.y);
      const int nx = h - (uy + int(e.fh));
      const int ny = ux;
      e.unit.x = uint16_t(nx < 0 ? 0 : nx);
      e.unit.y = uint16_t(ny);
      std::swap(e.fw, e.fh);
    }

    clip->width = nw;
    clip->height = nh;
  }
  return PF_OK;
}

int pf_map_paste(pf_map* map, const pf_clipboard* clip, int x, int y) {
  return pf_map_paste_ex(map, clip, x, y, 1);
}

int pf_map_paste_ex(pf_map* map, const pf_clipboard* clip, int x, int y, int fit_edges) {
  if (!map || !clip) return -1;
  pf::Map& m = *map->map;
  const int w = clip->width, h = clip->height;
  if (x < 0 || y < 0 || x + w > m.width() || y + h > m.height()) return -1;

  if (clip->has_terrain) {
    pf::CornerGrid& grid = map->grid();
    // A paste drops a block of corner terrains against others, which is the
    // same join a brush stroke makes, so it works the same margin. One tile of
    // slack is not enough for two reasons: forest is three hops from water and
    // the band of ground and coast the graph demands between them does not fit
    // in one tile, and legalisation that moves a corner on the rectangle's own
    // edge leaves the tile outside sharing that corner undrawn — a pair that
    // disagrees about a corner, which is the hard seam the slack was meant to
    // prevent.
    const int margin = fit_edges ? int(pf::kTerrainCount) : 0;
    const pf::Rect rect{x - margin, y - margin,
                        x + w - 1 + margin, y + h - 1 + margin};
    // Taken before the fragment lands, so a tile the join never reached keeps
    // the one its author drew. Nothing in the margin is forced to change:
    // re-deriving a band this wide from the corner grid is what apply_corners
    // warns against, the model being a lossy reading of the tiles.
    pf::CornerPatch was(grid, rect);

    if (clip->mask.empty()) {
      for (int cy = 0; cy <= h; cy++) {
        for (int cx = 0; cx <= w; cx++) {
          grid.set(x + cx, y + cy, clip->corner(cx, cy));
        }
      }
      for (int ty = 0; ty < h; ty++) {
        for (int tx = 0; tx < w; tx++) {
          grid.set_wall(x + tx, y + ty,
                        clip->walls[size_t(ty) * size_t(w) + size_t(tx)]);
        }
      }
      // The fragment repaints its own footprint whatever its corners say, for
      // the reason a brush does: the variation policy may want a different
      // drawing of the same ground.
      was.always(pf::Rect{x, y, x + w - 1, y + h - 1});
    } else {
      // With holes there is no corner rectangle to write, only the four corners
      // of each tile the fragment carries. A corner shared with a hole takes
      // the fragment's value — that shared edge is the seam legalisation exists
      // to settle. No `always` here, deliberately: what leaves the holes alone
      // is that their corners never moved, and forcing the bounding box would
      // fill them in.
      for (int ty = 0; ty < h; ty++) {
        for (int tx = 0; tx < w; tx++) {
          if (!clip->included(tx, ty)) continue;
          grid.set(x + tx, y + ty, clip->corner(tx, ty));
          grid.set(x + tx + 1, y + ty, clip->corner(tx + 1, ty));
          grid.set(x + tx, y + ty + 1, clip->corner(tx, ty + 1));
          grid.set(x + tx + 1, y + ty + 1, clip->corner(tx + 1, ty + 1));
          grid.set_wall(x + tx, y + ty,
                        clip->walls[size_t(ty) * size_t(w) + size_t(tx)]);
        }
      }
    }

    if (fit_edges) pf::legalize(grid, rect);
    // Paste a lake against a wall and the tiles between it and the wall become
    // coast, which is no longer ground for the wall to stand on.
    pf::clear_unsupported_walls(grid, rect);
    // With the snapshot, a tile whose corners never moved keeps the tile the
    // author drew there. That is what holds the margin steady where the join
    // did not reach it, and what leaves a masked fragment's holes alone:
    // recomposing them would re-roll which drawing of that ground they use.
    pf::apply_corners(m, grid, rect, map->tiles(), &was);
  }

  int pasted = 0;
  for (const auto& e : clip->units) {
    const pf::Unit& u = e.unit;
    const int tx = x + int(u.x), ty = y + int(u.y);
    // Paste obeys the same placement rule as placing by hand, not a similar one
    // — a rotated fragment can easily land a ship on grass. Every unit already
    // placed by this same paste counts as in the way, which stops a fragment
    // stacking on itself.
    if (pf_map_placement_check_ex(map, tx, ty, u.type, nullptr, 0) != PF_PLACE_OK) {
      continue;
    }
    if (m.add_unit(tx, ty, u.type, u.owner, u.value) >= 0) pasted++;
  }
  return pasted;
}

// ------------------------------------------------------------ validation

namespace {

/// Slots 0-7 are the real players. 8-14 are unused and 15 is the neutral slot,
/// which needs no start location — treating it as a player made all 529 shipped
/// maps "invalid".
constexpr int kRealPlayers = 8;
constexpr int kGoldMine = 92;
constexpr int kHumanStart = 94;
constexpr int kOrcStart = 95;

void add_issue(std::vector<pf_issue>& out, int severity, int code, int player,
               int x, int y, const char* message) {
  pf_issue issue{};
  issue.severity = severity;
  issue.code = code;
  issue.player = player;
  issue.x = x;
  issue.y = y;
  std::snprintf(issue.message, sizeof(issue.message), "%s", message);
  out.push_back(issue);
}

}  // namespace

int pf_map_validate(const pf_map* map, pf_issue* out, int capacity) {
  if (!map) return -1;
  const pf::Map& m = *map->map;
  std::vector<pf_issue> issues;

  // Two different questions, and conflating them produced false positives on
  // every shipped map: `in_play` is "the slot is used at all", `needs_start` is
  // "the slot starts a base". Passive and rescue players own pre-placed units
  // and legitimately have no start location.
  bool in_play[pf::kPlayerCount] = {};
  bool needs_start[pf::kPlayerCount] = {};
  int active_count = 0;
  for (int p = 0; p < pf::kPlayerCount; p++) {
    const int owner = m.owner(p);
    in_play[p] = p < kRealPlayers && owner != PF_OWNER_NOBODY;
    needs_start[p] = in_play[p] &&
                     (owner == PF_OWNER_HUMAN || owner == PF_OWNER_COMPUTER);
    if (needs_start[p]) active_count++;
  }
  if (active_count == 0) {
    add_issue(issues, PF_SEVERITY_ERROR, PF_ISSUE_NO_PLAYERS, -1, -1, -1,
              "No player slot is in play");
  }

  int starts[pf::kPlayerCount] = {};
  bool has_gold = false;
  char text[128];

  for (const pf::Unit& u : m.units()) {
    const int x = int(u.x), y = int(u.y);
    if (x < 0 || y < 0 || x >= m.width() || y >= m.height()) {
      std::snprintf(text, sizeof(text), "%s is outside the map",
                    pf_unit_name(u.type) ? pf_unit_name(u.type) : "Unit");
      add_issue(issues, PF_SEVERITY_ERROR, PF_ISSUE_UNIT_OUT_OF_BOUNDS,
                int(u.owner), x, y, text);
      continue;
    }

    int fw = 1, fh = 1;
    m.unit_footprint(u.type, fw, fh);
    if (x + fw > m.width() || y + fh > m.height()) {
      std::snprintf(text, sizeof(text), "%s runs off the edge of the map",
                    pf_unit_name(u.type) ? pf_unit_name(u.type) : "Unit");
      add_issue(issues, PF_SEVERITY_ERROR, PF_ISSUE_UNIT_OVERFLOWS,
                int(u.owner), x, y, text);
    }

    if (u.type == kGoldMine) has_gold = true;

    if (u.type == kHumanStart || u.type == kOrcStart) {
      const int p = int(u.owner);
      if (p >= 0 && p < pf::kPlayerCount) {
        starts[p]++;
        if (!in_play[p]) {
          std::snprintf(text, sizeof(text), "Start location for %s, which is not in play",
                        pf_player_name(p) ? pf_player_name(p) : "a player");
          add_issue(issues, PF_SEVERITY_WARNING, PF_ISSUE_ORPHAN_START, p, x, y, text);
        } else {
          // The start location's race should match the player's side.
          const int race = m.race(p);
          const bool wants_orc = race == PF_RACE_ORC;
          if ((u.type == kOrcStart) != wants_orc && race != PF_RACE_NEUTRAL) {
            std::snprintf(text, sizeof(text), "%s has a %s start location",
                          pf_player_name(p) ? pf_player_name(p) : "Player",
                          u.type == kOrcStart ? "orc" : "human");
            add_issue(issues, PF_SEVERITY_WARNING, PF_ISSUE_START_RACE_MISMATCH,
                      p, x, y, text);
          }
        }
      }
    }
  }

  for (int p = 0; p < pf::kPlayerCount; p++) {
    if (!needs_start[p]) continue;
    if (starts[p] == 0) {
      std::snprintf(text, sizeof(text), "%s is in play but has no start location",
                    pf_player_name(p) ? pf_player_name(p) : "Player");
      add_issue(issues, PF_SEVERITY_ERROR, PF_ISSUE_NO_START_LOCATION, p, -1, -1, text);
    } else if (starts[p] > 1) {
      std::snprintf(text, sizeof(text), "%s has %d start locations",
                    pf_player_name(p) ? pf_player_name(p) : "Player", starts[p]);
      add_issue(issues, PF_SEVERITY_WARNING, PF_ISSUE_EXTRA_START_LOCATION, p, -1, -1, text);
    }
  }

  // Units sharing a tile. Start locations are markers rather than physical
  // units and routinely sit under a town hall, so they are excluded.
  for (size_t i = 0; i < m.units().size(); i++) {
    const pf::Unit& a = m.units()[i];
    int aw = 1, ah = 1;
    m.unit_footprint(a.type, aw, ah);
    for (size_t j = i + 1; j < m.units().size(); j++) {
      const pf::Unit& b = m.units()[j];
      int bw = 1, bh = 1;
      m.unit_footprint(b.type, bw, bh);
      const bool apart = int(a.x) + aw <= int(b.x) || int(b.x) + bw <= int(a.x) ||
                         int(a.y) + ah <= int(b.y) || int(b.y) + bh <= int(a.y);
      if (apart) continue;
      // Intended arrangements: a marker under anything, and a well on its patch.
      const bool marker = a.type == 94 || a.type == 95 || b.type == 94 || b.type == 95;
      const bool well_on_patch =
          ((a.type == 86 || a.type == 87) && b.type == 93) ||
          ((b.type == 86 || b.type == 87) && a.type == 93);
      if (marker || well_on_patch) continue;
      std::snprintf(text, sizeof(text), "%s overlaps %s",
                    pf_unit_name(a.type) ? pf_unit_name(a.type) : "unit",
                    pf_unit_name(b.type) ? pf_unit_name(b.type) : "unit");
      add_issue(issues, PF_SEVERITY_WARNING, PF_ISSUE_UNITS_OVERLAP,
                int(a.owner), int(a.x), int(a.y), text);
    }
  }

  // Terrain the unit could never stand on.
  for (const pf::Unit& u : m.units()) {
    const int code = pf_map_placement_check(map, int(u.x), int(u.y), u.type);
    if (code == PF_PLACE_NEEDS_LAND || code == PF_PLACE_NEEDS_WATER) {
      std::snprintf(text, sizeof(text), "%s needs %s",
                    pf_unit_name(u.type) ? pf_unit_name(u.type) : "unit",
                    code == PF_PLACE_NEEDS_LAND ? "solid ground" : "water");
      add_issue(issues, PF_SEVERITY_ERROR, PF_ISSUE_ILLEGAL_TERRAIN,
                int(u.owner), int(u.x), int(u.y), text);
    }
    // An error rather than a warning: the game refuses the placement, so the
    // building is simply not there and the map plays wrong.
    if (code == PF_PLACE_TOO_NEAR_MINE) {
      std::snprintf(text, sizeof(text),
                    "%s needs %d tiles of clearance from the gold mine",
                    pf_unit_name(u.type) ? pf_unit_name(u.type) : "unit",
                    pf::mine_clearance_tiles());
      add_issue(issues, PF_SEVERITY_ERROR, PF_ISSUE_HALL_CROWDS_MINE,
                int(u.owner), int(u.x), int(u.y), text);
    }
  }

  if (!has_gold) {
    add_issue(issues, PF_SEVERITY_WARNING, PF_ISSUE_NO_RESOURCES, -1, -1, -1,
              "The map has no gold mine");
  } else {
    // A start with nothing to mine is a player who cannot open. Blizzard's own
    // 28 multiplayer maps trip this once, on 4lake, which is genuinely the odd
    // one out. How much gold is reported rather than judged — see
    // pf_map_start_gold_in_reach.
    for (int p = 0; p < pf::kPlayerCount; p++) {
      if (pf_map_owner(map, p) == PF_OWNER_NOBODY) continue;
      const pf::Unit* start = start_of(m, p);
      if (!start) continue;                       // its own issue, above
      if (pf_map_start_gold_in_reach(map, p) > 0) continue;
      std::snprintf(text, sizeof(text),
                    "%s has no gold mine within reach of their start location",
                    pf_player_name(p) ? pf_player_name(p) : "A player");
      add_issue(issues, PF_SEVERITY_WARNING, PF_ISSUE_START_NO_GOLD,
                p, int(start->x), int(start->y), text);
    }
  }

  // Errors first, so a truncated list still shows what matters.
  std::stable_sort(issues.begin(), issues.end(),
                   [](const pf_issue& a, const pf_issue& b) { return a.severity > b.severity; });

  if (out && capacity > 0) {
    const int n = int(issues.size()) < capacity ? int(issues.size()) : capacity;
    for (int i = 0; i < n; i++) out[i] = issues[i];
  }
  return int(issues.size());
}

namespace {

/// The unit indices `what` finds, ascending.
///
/// The rules are pf_map_validate's, down to the exclusions: they were tuned
/// against the shipped maps, and a second set of them here would eventually
/// disagree and offer to delete something Blizzard put there on purpose.
void collect_misplaced(const pf_map* map, int what, std::vector<int>& out) {
  const pf::Map& m = *map->map;
  const int count = int(m.units().size());
  std::vector<char> flagged(size_t(count > 0 ? count : 1), 0);

  if (what & PF_MISPLACED_OFF_MAP) {
    for (int i = 0; i < count; i++) {
      const pf::Unit& u = m.units()[size_t(i)];
      const int x = int(u.x), y = int(u.y);
      int fw = 1, fh = 1;
      m.unit_footprint(u.type, fw, fh);
      if (x < 0 || y < 0 || x >= m.width() || y >= m.height() ||
          x + fw > m.width() || y + fh > m.height()) {
        flagged[size_t(i)] = 1;
      }
    }
  }

  // Terrain, through the call validation uses, and only its two "this unit
  // cannot be here at all" answers. A building on ground the editor would
  // refuse is a judgement, and community maps are full of them.
  if (what & PF_MISPLACED_TERRAIN) {
    for (int i = 0; i < count; i++) {
      if (flagged[size_t(i)]) continue;
      const pf::Unit& u = m.units()[size_t(i)];
      if (int(u.x) >= m.width() || int(u.y) >= m.height()) continue;
      const int code = pf_map_placement_check(map, int(u.x), int(u.y), u.type);
      if (code == PF_PLACE_NEEDS_LAND || code == PF_PLACE_NEEDS_WATER) {
        flagged[size_t(i)] = 1;
      }
    }
  }

  // Overlap. The later unit of a pair is the one named, so removing the whole
  // list leaves the first of a pile standing rather than clearing the tile.
  if (what & PF_MISPLACED_OVERLAP) {
    for (int i = 0; i < count; i++) {
      if (flagged[size_t(i)]) continue;
      const pf::Unit& a = m.units()[size_t(i)];
      int aw = 1, ah = 1;
      m.unit_footprint(a.type, aw, ah);
      for (int j = i + 1; j < count; j++) {
        if (flagged[size_t(j)]) continue;
        const pf::Unit& b = m.units()[size_t(j)];
        int bw = 1, bh = 1;
        m.unit_footprint(b.type, bw, bh);
        const bool apart = int(a.x) + aw <= int(b.x) || int(b.x) + bw <= int(a.x) ||
                           int(a.y) + ah <= int(b.y) || int(b.y) + bh <= int(a.y);
        if (apart) continue;
        // Intended arrangements: a marker under anything, a well on its patch.
        const bool marker = a.type == kHumanStart || a.type == kOrcStart ||
                            b.type == kHumanStart || b.type == kOrcStart;
        const bool well_on_patch = ((a.type == 86 || a.type == 87) && b.type == 93) ||
                                   ((b.type == 86 || b.type == 87) && a.type == 93);
        if (marker || well_on_patch) continue;
        flagged[size_t(j)] = 1;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    if (flagged[size_t(i)]) out.push_back(i);
  }
}

}  // namespace

int pf_map_misplaced_units(const pf_map* map, int what, int* out, int capacity) {
  if (!map) return -1;
  if (what == 0) return 0;
  std::vector<int> found;
  collect_misplaced(map, what, found);
  if (out && capacity > 0) {
    const int n = int(found.size()) < capacity ? int(found.size()) : capacity;
    for (int i = 0; i < n; i++) out[i] = found[i];
  }
  return int(found.size());
}

int pf_map_remove_misplaced_units(pf_map* map, int what) {
  if (!map) return -1;
  if (what == 0) return 0;
  std::vector<int> found;
  collect_misplaced(map, what, found);
  // Back to front: every removal shifts the indices after it.
  for (size_t i = found.size(); i > 0; i--) map->map->remove_unit(found[i - 1]);
  return int(found.size());
}

// ------------------------------------------------------------------ undo

pf_status pf_map_checkpoint(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  map->undo_stack.push_back(map->map->serialize());
  if (map->undo_stack.size() > pf_map::kMaxUndo) {
    map->undo_stack.erase(map->undo_stack.begin());
  }
  map->redo_stack.clear();  // a new edit forks the history
  return PF_OK;
}

int pf_map_can_undo(const pf_map* map) {
  return map && !map->undo_stack.empty() ? 1 : 0;
}

int pf_map_can_redo(const pf_map* map) {
  return map && !map->redo_stack.empty() ? 1 : 0;
}

pf_status pf_map_undo(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (map->undo_stack.empty()) return PF_ERR_OUT_OF_RANGE;
  std::vector<uint8_t> current = map->map->serialize();
  std::vector<uint8_t> previous = std::move(map->undo_stack.back());
  map->undo_stack.pop_back();
  if (!map->restore(previous)) return PF_ERR_MALFORMED;
  map->redo_stack.push_back(std::move(current));
  return PF_OK;
}

pf_status pf_map_redo(pf_map* map) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (map->redo_stack.empty()) return PF_ERR_OUT_OF_RANGE;
  std::vector<uint8_t> current = map->map->serialize();
  std::vector<uint8_t> next = std::move(map->redo_stack.back());
  map->redo_stack.pop_back();
  if (!map->restore(next)) return PF_ERR_MALFORMED;
  map->undo_stack.push_back(std::move(current));
  return PF_OK;
}

void pf_map_clear_history(pf_map* map) {
  if (!map) return;
  map->undo_stack.clear();
  map->redo_stack.clear();
}

// -------------------------------------------------------------- painting

pf_status pf_map_set_tileset_art(pf_map* map, const pf_tileset_art* art) {
  if (!map) return PF_ERR_INVALID_ARG;
  const pf::TilesetArt* next = art ? art->art.get() : nullptr;
  if (map->art == next) return PF_OK;
  map->art = next;
  map->index.reset();  // rebuilt on next use against the new artwork
  return PF_OK;
}

pf_status pf_map_paint_terrain(pf_map* map, int x, int y, int terrain, int size) {
  return pf_map_paint_terrain_mixed(map, x, y, terrain, size, 0);
}

pf_status pf_map_paint_terrain_mixed(pf_map* map, int x, int y, int terrain,
                                     int size, uint32_t mix_seed) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (terrain < 0 || terrain >= pf::kTerrainCount) return PF_ERR_OUT_OF_RANGE;
  if (size < 1) size = 1;
  pf::paint_auto(*map->map, map->grid(), map->tiles(), x, y, uint8_t(terrain),
                 size, mix_seed);
  return PF_OK;
}

pf_status pf_map_paint_corner(pf_map* map, int cx, int cy, int terrain,
                              uint32_t mix_seed) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (terrain < 0 || terrain >= pf::kTerrainCount) return PF_ERR_OUT_OF_RANGE;
  const pf::Rect touched = pf::paint_corner(*map->map, map->grid(), map->tiles(),
                                            cx, cy, uint8_t(terrain), mix_seed);
  // An empty rectangle is how paint_corner says it refused: out of the corner
  // grid, or a wall, which has no corner to sit on.
  if (touched.x1 < touched.x0) return PF_ERR_OUT_OF_RANGE;
  return PF_OK;
}

int pf_map_fill_terrain(pf_map* map, int x, int y, int terrain,
                        int rx, int ry, int rw, int rh) {
  if (!map) return -1;
  if (terrain < 0 || terrain >= pf::kTerrainCount) return -1;
  pf::Rect within{rx, ry, rx + rw, ry + rh};
  const bool bounded = rw > 0 && rh > 0;
  return pf::fill_terrain(*map->map, map->grid(), map->tiles(), x, y,
                          uint8_t(terrain), bounded ? &within : nullptr);
}

pf_status pf_map_paint_terrain_raw(pf_map* map, int x, int y, int terrain, int size) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (terrain < 0 || terrain >= pf::kTerrainCount) return PF_ERR_OUT_OF_RANGE;
  if (size < 1) size = 1;
  const int half = size / 2;
  pf::Map& m = *map->map;
  pf::CornerGrid& grid = map->grid();
  for (int dy = -half; dy <= half; dy++) {
    for (int dx = -half; dx <= half; dx++) {
      const int tx = x + dx, ty = y + dy;
      if (tx < 0 || ty < 0 || tx >= m.width() || ty >= m.height()) continue;
      const int variation = ((tx * 7 + ty * 13) & 0x3);
      const int tile = pf::solid_tile(uint8_t(terrain), variation);
      if (tile < 0) continue;
      const size_t i = size_t(ty) * size_t(m.width()) + size_t(tx);
      m.tiles()[i] = uint16_t(tile);
      m.movement()[i] = pf::kGroupMovement[tile >> 4];
      // Keep the corner state in step so a later auto-tile stroke is coherent.
      grid.set(tx, ty, uint8_t(terrain));
      grid.set(tx + 1, ty, uint8_t(terrain));
      grid.set(tx, ty + 1, uint8_t(terrain));
      grid.set(tx + 1, ty + 1, uint8_t(terrain));
    }
  }
  return PF_OK;
}

pf_status pf_map_paint_wall(pf_map* map, int x, int y, int kind, int size) {
  if (!map) return PF_ERR_INVALID_ARG;
  if (kind < 0 || kind > 2) return PF_ERR_OUT_OF_RANGE;
  if (size < 1) size = 1;
  const int half = size / 2;
  pf::CornerGrid& grid = map->grid();
  for (int dy = -half; dy <= half; dy++) {
    for (int dx = -half; dx <= half; dx++) grid.set_wall(x + dx, y + dy, uint8_t(kind));
  }
  // Re-emit a margin so neighbouring segments re-pick their connection shape.
  pf::Rect rect{x - half - 1, y - half - 1, x + half + 1, y + half + 1};
  pf::apply_corners(*map->map, grid, rect, map->tiles());
  return PF_OK;
}

int pf_map_rebuild_regions(pf_map* map) {
  if (!map) return -1;
  return pf::rebuild_regions(*map->map);
}

int pf_terrain_compatible(int a, int b) {
  if (a < 0 || b < 0 || a >= pf::kTerrainCount || b >= pf::kTerrainCount) return 0;
  return pf::terrain_compatible(uint8_t(a), uint8_t(b)) ? 1 : 0;
}

// -------------------------------------------------------------- graphics

pf_tileset_art* pf_tileset_art_open(const char* dir, int tileset, pf_status* status) {
  if (!dir) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  pf::TilesetArt* art = pf::TilesetArt::open(dir, tileset);
  if (!art) { set_status(status, PF_ERR_IO); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_tileset_art();
  handle->art.reset(art);
  return handle;
}

pf_tileset_art* pf_tileset_art_open_memory(const uint8_t* cv4, size_t cv4_len,
                                           const uint8_t* vx4, size_t vx4_len,
                                           const uint8_t* vr4, size_t vr4_len,
                                           const uint8_t* ppl, size_t ppl_len,
                                           pf_status* status) {
  if (!cv4 || !vx4 || !vr4 || !ppl) {
    set_status(status, PF_ERR_INVALID_ARG);
    return nullptr;
  }
  pf::TilesetArt* art = pf::TilesetArt::open_bytes(
      std::vector<uint8_t>(cv4, cv4 + cv4_len), std::vector<uint8_t>(vx4, vx4 + vx4_len),
      std::vector<uint8_t>(vr4, vr4 + vr4_len), std::vector<uint8_t>(ppl, ppl + ppl_len));
  if (!art) { set_status(status, PF_ERR_MALFORMED); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_tileset_art();
  handle->art.reset(art);
  return handle;
}

void pf_tileset_art_free(pf_tileset_art* art) { delete art; }

int pf_tileset_art_detail(const pf_tileset_art* art, int megatile) {
  return art ? art->art->detail(megatile) : 0;
}

int pf_tileset_art_megatile_count(const pf_tileset_art* art) {
  return art ? art->art->megatile_count() : 0;
}

int pf_tileset_art_megatile_for(const pf_tileset_art* art, uint16_t tile) {
  return art ? art->art->megatile_for(tile) : -1;
}

int pf_tileset_art_is_blank(const pf_tileset_art* art, int megatile) {
  return art && art->art->is_blank(megatile) ? 1 : 0;
}

pf_status pf_tileset_art_draw(const pf_tileset_art* art, int megatile,
                              uint32_t* out, int stride) {
  if (!art || !out || stride < PF_TILE_PX) return PF_ERR_INVALID_ARG;
  return art->art->draw_megatile(megatile, out, stride) ? PF_OK : PF_ERR_OUT_OF_RANGE;
}

void pf_tileset_art_set_water_phase(pf_tileset_art* art, int phase) {
  if (art && art->art) art->art->set_water_phase(phase);
}

int pf_tileset_art_water_cycle(void) { return pf::TilesetArt::kWaterCycle; }

uint32_t pf_tileset_art_average(const pf_tileset_art* art, int megatile) {
  return art ? art->art->average(megatile) : 0xff000000u;
}

const uint32_t* pf_tileset_art_palette(const pf_tileset_art* art) {
  return art ? art->art->palette() : nullptr;
}

pf_sprite* pf_sprite_open(const char* dir, int unit_id, int tileset, pf_status* status) {
  if (!dir) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  pf::Sprite* sprite = pf::Sprite::open(dir, unit_id, tileset);
  if (!sprite) { set_status(status, PF_ERR_IO); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_sprite();
  handle->sprite.reset(sprite);
  return handle;
}

pf_sprite* pf_sprite_open_memory(const uint8_t* data, size_t len, pf_status* status) {
  if (!data) { set_status(status, PF_ERR_INVALID_ARG); return nullptr; }
  pf::Sprite* sprite = pf::Sprite::open_bytes(std::vector<uint8_t>(data, data + len));
  if (!sprite) { set_status(status, PF_ERR_MALFORMED); return nullptr; }
  set_status(status, PF_OK);
  auto* handle = new pf_sprite();
  handle->sprite.reset(sprite);
  return handle;
}

/** Relative path, without extension, of the .grp a unit uses on a tileset. */
int pf_sprite_path(int unit_id, int tileset, char* out, int cap) {
  const std::string path = pf::sprite_path_for(unit_id, tileset);
  if (!out || cap <= 0) return int(path.size());
  const int n = int(path.size()) < cap - 1 ? int(path.size()) : cap - 1;
  std::memcpy(out, path.data(), size_t(n));
  out[n] = '\0';
  return int(path.size());
}

int pf_unit_sound_path(int unit_id, int kind, int salt, char* out, int cap) {
  const std::string path = pf::unit_sound_path(unit_id, kind, salt);
  if (!out || cap <= 0) return int(path.size());
  const int n = int(path.size()) < cap - 1 ? int(path.size()) : cap - 1;
  std::memcpy(out, path.data(), size_t(n));
  out[n] = '\0';
  return int(path.size());
}

void pf_sprite_free(pf_sprite* sprite) { delete sprite; }

int pf_sprite_width(const pf_sprite* sprite) { return sprite ? sprite->sprite->width() : 0; }
int pf_sprite_height(const pf_sprite* sprite) { return sprite ? sprite->sprite->height() : 0; }
int pf_sprite_frame_count(const pf_sprite* sprite) {
  return sprite ? sprite->sprite->frame_count() : 0;
}

// ------------------------------------------------------------- rendering

struct pf_sprite_set {
  /// Keyed by unit_id << 8 | owner. A dozen types over a few players, so a
  /// flat map is the right shape and the lookup is not the hot path anyway.
  std::map<int, pf_sprite*> sprites;
  ~pf_sprite_set() { for (auto& kv : sprites) pf_sprite_free(kv.second); }
};

pf_sprite_set* pf_sprite_set_create(void) { return new pf_sprite_set(); }

void pf_sprite_set_free(pf_sprite_set* set) { delete set; }

pf_status pf_sprite_set_add(pf_sprite_set* set, int unit_id, int owner, pf_sprite* sprite) {
  if (!set || !sprite) return PF_ERR_INVALID_ARG;
  if (unit_id < 0 || unit_id >= pf::kUnitCount) return PF_ERR_OUT_OF_RANGE;
  if (owner < 0 || owner >= pf::kPlayerCount) return PF_ERR_OUT_OF_RANGE;
  const int key = (unit_id << 8) | owner;
  auto it = set->sprites.find(key);
  if (it != set->sprites.end()) pf_sprite_free(it->second);
  set->sprites[key] = sprite;
  return PF_OK;
}

int pf_sprite_set_has(const pf_sprite_set* set, int unit_id, int owner) {
  if (!set) return 0;
  return set->sprites.count((unit_id << 8) | owner) ? 1 : 0;
}

namespace {

/// 0xRRGGBB into the packed 0xAABBGGRR order everything downstream uses.
inline uint32_t pack_rgb(uint32_t rgb) {
  return 0xff000000u | ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 0xffu);
}

/// Mix two packed colours; `t` is how much of `b`.
inline uint32_t blend_px(uint32_t a, uint32_t b, double t) {
  const auto mix = [t](uint32_t x, uint32_t y) {
    return uint32_t(std::lround(double(x) + (double(y) - double(x)) * t));
  };
  return 0xff000000u
       | (mix((a >> 16) & 0xff, (b >> 16) & 0xff) << 16)
       | (mix((a >> 8) & 0xff, (b >> 8) & 0xff) << 8)
       | mix(a & 0xff, b & 0xff);
}

/// Golden-ratio hue stepping: adjacent labels land far apart in colour. Region
/// ids and tile ids are labels, not quantities, so a ramp would imply an order
/// that is not there.
uint32_t hsv_packed(double h, double s, double v) {
  const double c = v * s;
  const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
  const double m = v - c;
  const int i = int(std::floor(h / 60.0)) % 6;
  double r = 0, g = 0, b = 0;
  switch (i) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
  }
  const auto to8 = [](double f) { return uint32_t(std::lround(f * 255.0)); };
  return 0xff000000u | (to8(b + m) << 16) | (to8(g + m) << 8) | to8(r + m);
}

void fill_tile_px(uint32_t* out, int stride, int ox, int oy, uint32_t colour) {
  for (int y = 0; y < pf::kTilePx; y++) {
    uint32_t* row = out + size_t(oy + y) * size_t(stride) + size_t(ox);
    for (int x = 0; x < pf::kTilePx; x++) row[x] = colour;
  }
}

/// Hollow player-coloured box, so a unit with no artwork is still visible.
void outline_unit_px(uint32_t* out, int width, int height, const pf::Unit& u,
                     int fw, int fh, int x0, int y0, uint32_t override_colour) {
  const uint32_t colour = override_colour ? override_colour : pack_rgb(pf_player_color(u.owner));
  const int ox = (int(u.x) - x0) * pf::kTilePx;
  const int oy = (int(u.y) - y0) * pf::kTilePx;
  const int w = fw * pf::kTilePx, h = fh * pf::kTilePx;
  for (int y = 0; y < h; y++) {
    const int dy = oy + y;
    if (dy < 0 || dy >= height) continue;
    for (int x = 0; x < w; x++) {
      const int dx = ox + x;
      if (dx < 0 || dx >= width) continue;
      if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) {
        out[size_t(dy) * size_t(width) + size_t(dx)] = colour;
      }
    }
  }
}

/// Resources and start locations, the two a mapper hunts for.
bool draw_is_special(int type) {
  return pf_unit_in_group(type, PF_GROUP_RESOURCES) ||
         pf_unit_in_group(type, PF_GROUP_START_LOCATIONS);
}

/// Every eighth grid line is heavier, so the grid works as a ruler rather than
/// only saying where a tile ends.
constexpr int kGridEvery = 8;

}  // namespace

// ------------------------------------------------------------------ view

struct pf_view {
  pf::View v;
};

pf_view* pf_view_create(void) { return new pf_view(); }
void pf_view_free(pf_view* view) { delete view; }

void pf_view_set_map(pf_view* view, int width, int height) {
  if (!view) return;
  view->v.map_w = width;
  view->v.map_h = height;
}

void pf_view_set_viewport(pf_view* view, int width, int height, double dpr) {
  if (!view) return;
  view->v.viewport_w = width;
  view->v.viewport_h = height;
  view->v.dpr = dpr > 0.0 ? dpr : 1.0;
}

int pf_view_zoom(const pf_view* view) { return view ? view->v.zoom : 100; }

void pf_view_set_zoom(pf_view* view, int zoom) {
  if (!view) return;
  view->v.zoom = pf::zoom_snap(zoom);
  view->v.fitted = false;
}

double pf_view_scroll_x(const pf_view* view) { return view ? view->v.scroll_x : 0.0; }
double pf_view_scroll_y(const pf_view* view) { return view ? view->v.scroll_y : 0.0; }

void pf_view_set_scroll(pf_view* view, double x, double y) {
  if (!view) return;
  view->v.scroll_x = x;
  view->v.scroll_y = y;
}

int pf_view_fitted(const pf_view* view) { return view && view->v.fitted ? 1 : 0; }
int pf_view_tile_px(const pf_view* view) { return view ? pf::view_tile_px(view->v) : 32; }
void pf_view_clamp(pf_view* view) { if (view) pf::view_clamp(view->v); }

int pf_view_tile_at(const pf_view* view, int px, int py, int* tx, int* ty) {
  if (!view) return 0;
  int x = 0, y = 0;
  const bool inside = pf::view_tile_at(view->v, px, py, x, y);
  if (tx) *tx = x;
  if (ty) *ty = y;
  return inside ? 1 : 0;
}

void pf_view_zoom_about(pf_view* view, int zoom, int px, int py) {
  if (view) pf::view_zoom_about(view->v, zoom, px, py);
}

void pf_view_zoom_step(pf_view* view, int dir, int px, int py) {
  if (view) pf::view_zoom_step(view->v, dir, px, py);
}

int pf_view_fit(pf_view* view, int only_to_shrink) {
  return view && pf::view_fit(view->v, only_to_shrink != 0) ? 1 : 0;
}

int pf_view_fit_rect(pf_view* view, int x, int y, int w, int h) {
  return view && pf::view_fit_rect(view->v, x, y, w, h) ? 1 : 0;
}

void pf_view_centre_on(pf_view* view, int tx, int ty) {
  if (view) pf::view_centre_on(view->v, tx, ty);
}

void pf_view_region(const pf_view* view, int* x0, int* y0, int* cols, int* rows) {
  int a = 0, b = 0, c = 0, d = 0;
  if (view) pf::view_region(view->v, a, b, c, d);
  if (x0) *x0 = a;
  if (y0) *y0 = b;
  if (cols) *cols = c;
  if (rows) *rows = d;
}

void pf_view_origin(const pf_view* view, int x0, int y0, int* ox, int* oy) {
  int a = 0, b = 0;
  if (view) pf::view_origin(view->v, x0, y0, a, b);
  if (ox) *ox = a;
  if (oy) *oy = b;
}

// -------------------------------------------------------- editing policy

int pf_zoom_level_count(void) { return pf::zoom_level_count(); }
int pf_zoom_level(int index) { return pf::zoom_level(index); }
int pf_zoom_min(void) { return pf::zoom_min(); }
int pf_zoom_max(void) { return pf::zoom_max(); }
int pf_zoom_step(int zoom, int dir) { return pf::zoom_step(zoom, dir); }
int pf_zoom_snap(int zoom) { return pf::zoom_snap(zoom); }

int pf_brush_size_count(void) { return pf::brush_size_count(); }
int pf_brush_size(int index) { return pf::brush_size(index); }

int pf_spray_tick_ms(void) { return pf::spray_tick_ms(); }
int pf_spray_ramp_ms(void) { return pf::spray_ramp_ms(); }
double pf_spray_density(int held_ms, double full) {
  return pf::spray_density(held_ms, full);
}

int pf_brush_count(void) { return pf::brush_count(); }
int pf_brush_terrain(int index) { return pf::brush_terrain(index); }
int pf_brush_shade(int index) { return pf::brush_shade(index); }

int pf_terrain_other_shade(int terrain) {
  // The same three pairs the shade pass works in (pf_map_shade_stroke), which
  // is the whole of what the game treats as one terrain drawn two ways.
  switch (terrain) {
    case PF_TERRAIN_GROUND_LIGHT: return PF_TERRAIN_GROUND_DARK;
    case PF_TERRAIN_GROUND_DARK: return PF_TERRAIN_GROUND_LIGHT;
    case PF_TERRAIN_WATER_LIGHT: return PF_TERRAIN_WATER_DARK;
    case PF_TERRAIN_WATER_DARK: return PF_TERRAIN_WATER_LIGHT;
    case PF_TERRAIN_COAST_LIGHT: return PF_TERRAIN_COAST_DARK;
    case PF_TERRAIN_COAST_DARK: return PF_TERRAIN_COAST_LIGHT;
    default: return terrain;
  }
}

int pf_shade_at(int terrain, int x, int y, uint32_t seed) {
  if (terrain < 0 || terrain >= PF_TERRAIN_UNKNOWN) return terrain;
  return pf::shade_of(uint8_t(terrain), x, y, seed);
}

double pf_scatter_density(void) { return pf::scatter_density(); }

int pf_name_score(const char* query, const char* name) {
  return name ? pf::name_score(query, name) : -1;
}

int pf_unit_name_filter(const char* query, int* ids, int count) {
  if (!ids || count <= 0) return 0;
  // Nothing typed is not a ranking problem: the caller's own order — units
  // grouped by race and kind — is more use than sorting them by name length.
  bool typed = false;
  for (const char* c = query; c && *c; c++) {
    if (!std::isspace(static_cast<unsigned char>(*c))) { typed = true; break; }
  }
  if (!typed) return count;
  struct Ranked { int id; const char* name; int score; };
  std::vector<Ranked> kept;
  kept.reserve(size_t(count));
  for (int i = 0; i < count; i++) {
    const char* name = pf_unit_name(ids[i]);
    if (!name) continue;
    const int score = pf::name_score(query, name);
    if (score >= 0) kept.push_back({ids[i], name, score});
  }
  // Ties break on the shorter name and then alphabetically, so the order is
  // stable between keystrokes; stable_sort so equal names do not depend on the
  // sort's internals.
  std::stable_sort(kept.begin(), kept.end(), [](const Ranked& a, const Ranked& b) {
    if (a.score != b.score) return a.score > b.score;
    const size_t la = std::strlen(a.name), lb = std::strlen(b.name);
    if (la != lb) return la < lb;
    return std::strcmp(a.name, b.name) < 0;
  });
  for (size_t i = 0; i < kept.size(); i++) ids[i] = kept[i].id;
  return int(kept.size());
}

uint32_t pf_terrain_flat_colour(int terrain, int tileset) {
  return pf::terrain_flat_colour(terrain, tileset);
}

uint8_t* pf_png_encode(const uint32_t* rgba, int width, int height,
                       size_t* out_len) {
  if (out_len) *out_len = 0;
  const std::vector<uint8_t> bytes = pf::encode_png(rgba, width, height);
  if (bytes.empty()) return nullptr;
  uint8_t* out = static_cast<uint8_t*>(std::malloc(bytes.size()));
  if (!out) return nullptr;
  std::memcpy(out, bytes.data(), bytes.size());
  if (out_len) *out_len = bytes.size();
  return out;
}

int pf_map_compose_region(const pf_map* map, const pf_render_options* o,
                          uint32_t* out, size_t capacity) {
  if (!map || !o || o->cols <= 0 || o->rows <= 0) return -1;
  const int width = o->cols * pf::kTilePx, height = o->rows * pf::kTilePx;
  const size_t pixels = size_t(width) * size_t(height);
  if (!out) return int(pixels);
  if (capacity < pixels) return -1;

  const pf::Map& m = *map->map;
  const std::vector<uint16_t>& tiles = m.tiles();
  const int mw = m.width(), mh = m.height();
  const auto in_map = [&](int x, int y) { return x >= 0 && y >= 0 && x < mw && y < mh; };

  // --- terrain
  for (int row = 0; row < o->rows; row++) {
    for (int col = 0; col < o->cols; col++) {
      const int tx = o->x0 + col, ty = o->y0 + row;
      const uint16_t tile = in_map(tx, ty) ? tiles[size_t(ty) * size_t(mw) + size_t(tx)] : 0;
      const int mt = o->art ? pf_tileset_art_megatile_for(o->art, tile) : -1;
      if (mt >= 0) {
        uint32_t* dst = out + size_t(row * pf::kTilePx) * size_t(width)
                            + size_t(col * pf::kTilePx);
        pf_tileset_art_draw(o->art, mt, dst, width);
      } else {
        // No artwork, or a tile this tileset cannot draw: the flat colour of
        // whatever terrain the tile mostly is.
        fill_tile_px(out, width, col * pf::kTilePx, row * pf::kTilePx,
                     pack_rgb(pf::terrain_flat_colour(pf_tile_dominant_terrain(tile),
                                                      m.tileset())));
      }
    }
  }

  // --- overlays, under the units so a tint never covers one
  if (o->overlay != PF_OVERLAY_NONE) {
    const std::vector<uint16_t>& layer =
        o->overlay == PF_OVERLAY_MOVEMENT ? m.movement()
      : o->overlay == PF_OVERLAY_REGIONS  ? m.regions()
                                          : m.tiles();
    for (int row = 0; row < o->rows; row++) {
      for (int col = 0; col < o->cols; col++) {
        const int tx = o->x0 + col, ty = o->y0 + row;
        if (!in_map(tx, ty)) continue;
        const size_t i = size_t(ty) * size_t(mw) + size_t(tx);
        const uint16_t value = i < layer.size() ? layer[i] : 0;
        const uint32_t tint = o->overlay == PF_OVERLAY_MOVEMENT
            ? pack_rgb(pf::movement_colour(value))
            : hsv_packed(std::fmod(double(value) * 137.508, 360.0), 0.75, 1.0);

        const int ox = col * pf::kTilePx, oy = row * pf::kTilePx;
        for (int y = 0; y < pf::kTilePx; y++) {
          uint32_t* dst = out + size_t(oy + y) * size_t(width) + size_t(ox);
          for (int x = 0; x < pf::kTilePx; x++) dst[x] = blend_px(dst[x], tint, 0.55);
        }
        // An override is the only reason to look at the movement layer, so it
        // is marked rather than merely tinted: a corner no ordinary tile has.
        if (o->overlay == PF_OVERLAY_MOVEMENT &&
            int(value) != pf_tile_movement(tiles[i])) {
          for (int y = 0; y < 8; y++) {
            uint32_t* dst = out + size_t(oy + y) * size_t(width) + size_t(ox);
            for (int x = 0; x < 8 - y; x++) dst[x] = 0xff00ffffu;   // yellow
          }
        }
      }
    }
  }

  // --- units, back to front
  if (o->unit_filter != PF_UNITS_NONE && (o->sprites || o->placeholders)) {
    std::vector<const pf::Unit*> units;
    units.reserve(m.units().size());
    for (const pf::Unit& u : m.units()) units.push_back(&u);
    std::stable_sort(units.begin(), units.end(),
                     [](const pf::Unit* a, const pf::Unit* b) {
                       return a->y != b->y ? a->y < b->y : a->x < b->x;
                     });

    std::vector<uint32_t> frame;
    for (const pf::Unit* u : units) {
      if (o->unit_filter != PF_UNITS_ALL &&
          pf_unit_draw_class(u->type) != o->unit_filter - PF_UNITS_GROUND) continue;

      int fw = 1, fh = 1;
      m.unit_footprint(u->type, fw, fh);
      if (o->mark_special && draw_is_special(u->type)) {
        outline_unit_px(out, width, height, *u, fw, fh, o->x0, o->y0, 0xff00ffffu);
      }

      pf_sprite* sprite = nullptr;
      if (o->sprites) {
        auto it = o->sprites->sprites.find((int(u->type) << 8) | int(u->owner));
        if (it != o->sprites->sprites.end()) sprite = it->second;
      }
      if (!sprite || !o->art) {
        if (o->placeholders) {
          outline_unit_px(out, width, height, *u, fw, fh, o->x0, o->y0, 0);
        }
        continue;
      }

      const int sw = pf_sprite_width(sprite), sh = pf_sprite_height(sprite);
      if (sw <= 0 || sh <= 0) continue;
      // The smaller of what the artwork holds and what this unit may turn to.
      const int count = std::max(1, std::min(pf_sprite_frame_count(sprite),
                                             pf_unit_facing_count(u->type)));
      // The seed, not the position: a unit dragged across the map keeps the way
      // it was facing when it was put there rather than spinning a frame per
      // tile the pointer crosses.
      const int index =
          o->vary_facing ? pf_unit_facing(u->seed_x, u->seed_y, u->type, count) : 0;

      frame.assign(size_t(sw) * size_t(sh), 0);
      if (pf_sprite_draw(sprite, index, u->owner, o->art, frame.data()) != PF_OK) continue;

      // Centred on the footprint: a 3x3 mine's artwork is not 96 px square.
      const int ox = (int(u->x) - o->x0) * pf::kTilePx + ((fw * pf::kTilePx - sw) >> 1);
      const int oy = (int(u->y) - o->y0) * pf::kTilePx + ((fh * pf::kTilePx - sh) >> 1);
      for (int y = 0; y < sh; y++) {
        const int dy = oy + y;
        if (dy < 0 || dy >= height) continue;
        for (int x = 0; x < sw; x++) {
          const int dx = ox + x;
          if (dx < 0 || dx >= width) continue;
          const uint32_t p = frame[size_t(y) * size_t(sw) + size_t(x)];
          if (p != 0) out[size_t(dy) * size_t(width) + size_t(dx)] = p;
        }
      }
    }
  }

  // --- grid
  if (o->grid) {
    constexpr uint32_t kLine = 0x60ffffffu;
    for (int col = 0; col <= o->cols; col++) {
      const int x = std::min(col * pf::kTilePx, width - 1);
      const double a = ((o->x0 + col) % kGridEvery == 0) ? 0.6 : 0.28;
      for (int y = 0; y < height; y++) {
        uint32_t& p = out[size_t(y) * size_t(width) + size_t(x)];
        p = blend_px(p, kLine, a);
      }
    }
    for (int row = 0; row <= o->rows; row++) {
      const int y = std::min(row * pf::kTilePx, height - 1);
      const double a = ((o->y0 + row) % kGridEvery == 0) ? 0.6 : 0.28;
      for (int x = 0; x < width; x++) {
        uint32_t& p = out[size_t(y) * size_t(width) + size_t(x)];
        p = blend_px(p, kLine, a);
      }
    }
  }

  return int(pixels);
}

int pf_map_compose_minimap(const pf_map* map, const pf_tileset_art* art,
                           uint32_t* out, size_t capacity) {
  if (!map) return -1;
  const pf::Map& m = *map->map;
  const int w = m.width(), h = m.height();
  const size_t pixels = size_t(w) * size_t(h);
  if (!out) return int(pixels);
  if (capacity < pixels) return -1;

  const std::vector<uint16_t>& tiles = m.tiles();
  for (size_t i = 0; i < pixels && i < tiles.size(); i++) {
    const int mt = art ? pf_tileset_art_megatile_for(art, tiles[i]) : -1;
    out[i] = mt >= 0 ? pf_tileset_art_average(art, mt)
                     : pack_rgb(pf::terrain_flat_colour(pf_tile_dominant_terrain(tiles[i]),
                                                        m.tileset()));
  }

  // Units at their real footprint: as single pixels a base and a lone worker
  // looked the same, on the one view whose job is telling them apart.
  //
  // Resources take their own colour rather than their owner's, because every
  // one is neutral and "how many mines, and where" is this view's question.
  // Yellow as the game's own minimap does it, and black for the oil patch,
  // which sits on water where yellow would shout.
  for (const pf::Unit& u : m.units()) {
    int fw = 1, fh = 1;
    m.unit_footprint(u.type, fw, fh);
    const uint32_t rgb = u.type == 0x5d ? 0x000000u
                       : u.owner == 15  ? 0xffff00u
                                        : pf_player_color(u.owner);
    const uint32_t colour = pack_rgb(rgb);
    for (int y = int(u.y); y < std::min(int(u.y) + fh, h); y++) {
      for (int x = int(u.x); x < std::min(int(u.x) + fw, w); x++) {
        if (x >= 0 && y >= 0) out[size_t(y) * size_t(w) + size_t(x)] = colour;
      }
    }
  }
  return int(pixels);
}

pf_status pf_sprite_draw(const pf_sprite* sprite, int frame, int owner,
                         const pf_tileset_art* art, uint32_t* out) {
  if (!sprite || !art || !out) return PF_ERR_INVALID_ARG;
  uint32_t palette[256];
  pf::apply_player_color(art->art->palette(), pf_player_color(owner), palette);
  return sprite->sprite->draw_frame(frame, palette, out) ? PF_OK : PF_ERR_OUT_OF_RANGE;
}
