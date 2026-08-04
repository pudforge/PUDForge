// PUD file model — parse, edit, serialize.
//
// A PUD is a flat chain of [4-char tag][uint32 length][payload] sections with
// no index and no global header. Everything is little-endian.
//
// The parser records the section order a file used and re-emits it, and keeps
// sections it does not model verbatim, so a load/save round-trip is byte-exact
// on all 524 shipped maps.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace pf {

// ---------------------------------------------------------------- binary

/// Little-endian reader over a borrowed buffer. Bounds-checked: every accessor
/// returns 0 past the end and sets `overrun`, so a truncated file degrades
/// rather than reading out of bounds.
class Reader {
 public:
  Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

  size_t pos() const { return pos_; }
  size_t remaining() const { return pos_ < len_ ? len_ - pos_ : 0; }
  bool overrun() const { return overrun_; }
  void seek(size_t p) { pos_ = p; }
  void skip(size_t n) { pos_ += n; }

  uint8_t u8();
  uint16_t u16();
  uint32_t u32();
  /// Borrowed view of the next `n` bytes; empty and flags overrun if short.
  const uint8_t* bytes(size_t n);
  /// `n` bytes as text, stopping at the first NUL.
  std::string fixed_string(size_t n);

 private:
  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
  bool overrun_ = false;
};

/// Little-endian writer onto a growable buffer.
class Writer {
 public:
  void u8(uint8_t v) { out_.push_back(v); }
  void u16(uint16_t v);
  void u32(uint32_t v);
  void raw(const uint8_t* p, size_t n);
  void raw(const std::vector<uint8_t>& v) { raw(v.data(), v.size()); }
  /// 4-character section tag, space-padded.
  void tag(const char* name);

  const std::vector<uint8_t>& buffer() const { return out_; }
  std::vector<uint8_t> take() { return std::move(out_); }
  size_t size() const { return out_.size(); }

 private:
  std::vector<uint8_t> out_;
};

// ------------------------------------------------------------- constants

constexpr int kUnitCount = 110;
constexpr int kUpgradeCount = 52;
constexpr int kPlayerCount = 16;
constexpr int kMaxUnits = 600;
constexpr int kMaxMapDim = 128;
constexpr int kDescBytes = 32;
constexpr uint16_t kDefaultTile = 0x0050;  // solid light ground

// -------------------------------------------------------- description text

/// Decode `DESC` bytes as the game's own character set, into UTF-8.
///
/// The set is code page 437, read off the game's own bitmap font rather than
/// assumed: the glyphs `font12x.fnt` carries above 0x7f are exactly CP437's
/// accented block. Every byte decodes, including ones no font draws, because
/// reading a map must never fail or change it.
std::string desc_decode(const uint8_t* bytes, size_t len);

/// Encode UTF-8 as `DESC` bytes. False when a character has no byte in the
/// game's set, or when the text is not valid UTF-8; `out` is then untouched.
///
/// Refusing rather than dropping keeps one character equal to one byte, so what
/// a client counts is what the field stores.
bool desc_encode(const std::string& utf8, std::string& out);

/// A placed unit — the 8-byte `UNIT` record.
struct Unit {
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t type = 0;
  uint8_t owner = 0;
  uint16_t value = 0;

  /// Where this unit was when it first appeared, and the only thing its drawn
  /// facing is taken from.
  ///
  /// The format has no facing field, so facing is hashed from a position; using
  /// the *current* position made a unit spin on the spot as it was dragged.
  /// Set once and never touched again, and not written to the file — a reopened
  /// map re-derives the same values from the positions it stored.
  uint16_t seed_x = 0;
  uint16_t seed_y = 0;
};

/// A section we do not model, preserved verbatim.
struct RawSection {
  std::string tag;
  std::vector<uint8_t> data;
};

/// Unit data (`UDTA`). Only the fields the editor needs are broken out; the
/// rest is kept as raw bytes so the section round-trips exactly.
struct UnitData {
  bool present = false;
  bool use_default = false;
  std::vector<uint8_t> raw;  // the whole payload, re-emitted unchanged
  /// Tile footprint per unit, decoded from the `unitSize` field.
  uint16_t size_x[kUnitCount] = {};
  uint16_t size_y[kUnitCount] = {};
};

enum class Status {
  Ok,
  NotAPud,
  Malformed,
  UnsupportedSize,
  OutOfRange,
};

class Map {
 public:
  Map() = default;

  static Map* parse(const uint8_t* data, size_t len, Status& status);
  static Map* create(int width, int height, int tileset, Status& status);

  std::vector<uint8_t> serialize() const;

  int width() const { return width_; }
  int height() const { return height_; }
  size_t tile_count() const { return size_t(width_) * size_t(height_); }

  /// Resolved terrain id — `ERAX` wins over `ERA ` when present, and anything
  /// above 3 renders as forest.
  int tileset() const;
  void set_tileset(int value);

  /// UTF-8, decoded from the raw bytes. See `desc_decode`.
  const std::string& description() const { return description_; }
  /// UTF-8 in. False, and the map unchanged, when the text needs a character
  /// `DESC` cannot store or more than `kDescBytes - 1` of them.
  bool set_description(const std::string& text);
  /// The raw 32 bytes, for tests that compare against a file.
  const uint8_t* description_bytes() const { return desc_raw_; }

  int version() const { return version_; }

  const std::vector<std::string>& warnings() const { return warnings_; }

  uint8_t owner(int player) const { return in_range(player) ? owners_[player] : 0; }
  void set_owner(int player, uint8_t v) { if (in_range(player)) owners_[player] = v; }
  uint8_t race(int player) const { return in_range(player) ? sides_[player] : 0; }
  void set_race(int player, uint8_t v) { if (in_range(player)) sides_[player] = v; }
  uint16_t start_gold(int player) const { return in_range(player) ? gold_[player] : 0; }
  uint16_t start_lumber(int player) const { return in_range(player) ? lumber_[player] : 0; }
  uint16_t start_oil(int player) const { return in_range(player) ? oil_[player] : 0; }
  void set_start_resources(int player, uint16_t g, uint16_t l, uint16_t o);
  uint8_t ai(int player) const { return in_range(player) ? ai_[player] : 0; }
  void set_ai(int player, uint8_t v) { if (in_range(player)) ai_[player] = v; }

  std::vector<uint16_t>& tiles() { return tiles_; }
  const std::vector<uint16_t>& tiles() const { return tiles_; }
  std::vector<uint16_t>& movement() { return movement_; }
  const std::vector<uint16_t>& movement() const { return movement_; }
  std::vector<uint16_t>& regions() { return regions_; }
  const std::vector<uint16_t>& regions() const { return regions_; }
  const std::vector<uint8_t>& oil_map() const { return oil_map_; }

  uint16_t tile_at(int x, int y) const;

  std::vector<Unit>& units() { return units_; }
  const std::vector<Unit>& units() const { return units_; }
  int add_unit(int x, int y, int type, int owner, int value);
  bool remove_unit(int index);
  /// Topmost unit covering a tile, or -1.
  int unit_at(int x, int y) const;
  void unit_footprint(int type, int& w, int& h) const;

  /// Resize the map, keeping content at (offset_x, offset_y) in the new grid.
  ///
  /// Returns the number of units dropped so a caller can report them rather
  /// than losing them silently. `REGM` is left stale: it is a whole-map
  /// labelling, so rebuild it afterwards.
  int resize(int width, int height, int offset_x, int offset_y, Status& status);

  const UnitData& unit_data() const { return udta_; }
  UnitData& unit_data_mut() { return udta_; }
  /// Re-read unit footprints from the UDTA payload. Call after replacing it.
  void refresh_unit_sizes();
  const std::vector<uint8_t>& upgrade_data() const { return ugrd_; }
  std::vector<uint8_t>& upgrade_data_mut() { return ugrd_; }
  const std::vector<uint8_t>& restrictions() const { return alow_; }
  std::vector<uint8_t>& restrictions_mut() { return alow_; }

 private:
  bool in_range(int player) const { return player >= 0 && player < kPlayerCount; }
  void read_type(const uint8_t* payload, size_t len);
  void normalize_layers();

  std::string magic_ = "WAR2 MAP";
  uint8_t type_unused_[2] = {0x0a, 0xff};
  uint32_t id_tag_ = 0;
  int version_ = 0x13;

  /// Raw `DESC` bytes. Some shipped maps keep text *after* the NUL terminator,
  /// so the field is only rewritten when the description actually changes.
  uint8_t desc_raw_[kDescBytes] = {};
  std::string description_;

  uint8_t owners_[kPlayerCount] = {};
  uint8_t sides_[kPlayerCount] = {};
  uint16_t gold_[kPlayerCount] = {};
  uint16_t lumber_[kPlayerCount] = {};
  uint16_t oil_[kPlayerCount] = {};
  uint8_t ai_[kPlayerCount] = {};

  int era_ = 0;
  int erax_ = -1;  // -1 when absent
  int width_ = 32;
  int height_ = 32;

  std::vector<uint16_t> tiles_;
  std::vector<uint16_t> movement_;
  std::vector<uint8_t> oil_map_;
  std::vector<uint16_t> regions_;
  std::vector<Unit> units_;

  UnitData udta_;
  std::vector<uint8_t> ugrd_;
  std::vector<uint8_t> alow_;
  /// Battle.net map signature, written after `UNIT`. -1 when absent.
  int64_t sign_ = -1;

  /// Tags in the order the parsed file used them, so a round-trip reproduces
  /// the original byte for byte even when a writer disagreed with our default.
  std::vector<std::string> section_order_;
  std::vector<RawSection> extra_;
  std::vector<std::string> warnings_;
};

}  // namespace pf
