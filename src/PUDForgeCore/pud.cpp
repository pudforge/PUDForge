#include "pud.hpp"

#include "constants.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <cstring>

namespace pf {
namespace {

/// Canonical write order for new maps, taken from what the shipped files
/// actually do rather than the spec's section numbering: every retail map
/// writes `UDTA UGRD ALOW`, while the spec numbers ALOW (7) before UGRD (8).
const char* const kWriteOrder[] = {
    "TYPE", "VER ", "DESC", "OWNR", "ERA ", "ERAX", "DIM ", "UDTA", "UGRD",
    "ALOW", "SIDE", "SGLD", "SLBR", "SOIL", "AIPL", "MTXM", "SQM ", "OILM",
    "REGM", "UNIT", "SIGN",
};

/// Byte offset of the `unitSize` field inside a `UDTA` payload:
/// (the section's two valid lengths live in constants.hpp as kUdtaSize and
/// kUdtaSizeWithSwamp; `udta_layout_matches_the_parser` checks this offset
/// against the same table the field ABI derives its offsets from)
/// 2 (useDefault) + 110*2 (overlap) + 508*2 (obsolete) + 110*4 (sight)
/// + 110*2 (hp) + 110*5 (magic..oil).
constexpr size_t kUdtaUnitSizeOffset =
    2 + kUnitCount * 2 + 508 * 2 + kUnitCount * 4 + kUnitCount * 2 + kUnitCount * 5;

void copy_words(std::vector<uint16_t>& dst, const uint8_t* p, size_t bytes, size_t max) {
  size_t count = std::min(bytes / 2, max);
  dst.resize(count);
  for (size_t i = 0; i < count; i++) dst[i] = uint16_t(p[i * 2] | (p[i * 2 + 1] << 8));
}

template <typename T>
void fit(std::vector<T>& v, size_t n, T fill) {
  if (v.size() == n) return;
  if (v.size() > n) { v.resize(n); return; }
  v.resize(n, fill);
}

}  // namespace

// ---------------------------------------------------------------- binary

uint8_t Reader::u8() {
  if (pos_ + 1 > len_) { overrun_ = true; return 0; }
  return data_[pos_++];
}

uint16_t Reader::u16() {
  if (pos_ + 2 > len_) { overrun_ = true; pos_ = len_; return 0; }
  uint16_t v = uint16_t(data_[pos_] | (data_[pos_ + 1] << 8));
  pos_ += 2;
  return v;
}

uint32_t Reader::u32() {
  if (pos_ + 4 > len_) { overrun_ = true; pos_ = len_; return 0; }
  uint32_t v = uint32_t(data_[pos_]) | (uint32_t(data_[pos_ + 1]) << 8) |
               (uint32_t(data_[pos_ + 2]) << 16) | (uint32_t(data_[pos_ + 3]) << 24);
  pos_ += 4;
  return v;
}

const uint8_t* Reader::bytes(size_t n) {
  if (pos_ + n > len_) { overrun_ = true; pos_ = len_; return nullptr; }
  const uint8_t* p = data_ + pos_;
  pos_ += n;
  return p;
}

std::string Reader::fixed_string(size_t n) {
  const uint8_t* p = bytes(n);
  if (!p) return {};
  size_t end = 0;
  while (end < n && p[end]) end++;
  return std::string(reinterpret_cast<const char*>(p), end);
}

void Writer::u16(uint16_t v) {
  out_.push_back(uint8_t(v & 0xff));
  out_.push_back(uint8_t((v >> 8) & 0xff));
}

void Writer::u32(uint32_t v) {
  out_.push_back(uint8_t(v & 0xff));
  out_.push_back(uint8_t((v >> 8) & 0xff));
  out_.push_back(uint8_t((v >> 16) & 0xff));
  out_.push_back(uint8_t((v >> 24) & 0xff));
}

void Writer::raw(const uint8_t* p, size_t n) {
  if (p && n) out_.insert(out_.end(), p, p + n);
}

void Writer::tag(const char* name) {
  for (int i = 0; i < 4; i++) out_.push_back(name[i] ? uint8_t(name[i]) : uint8_t(' '));
}

// ------------------------------------------------------- description text

/// CP437's upper half as Unicode; the lower half is ASCII and needs no table.
///
/// Which code page this is was read off the game's own fonts rather than
/// assumed — see `desc_decode` in pud.hpp for the glyph census.
const uint16_t kCp437High[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

std::string desc_decode(const uint8_t* bytes, size_t len) {
  std::string out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    const uint32_t cp =
        bytes[i] < 0x80 ? bytes[i] : kCp437High[bytes[i] - 0x80];
    if (cp < 0x80) {
      out.push_back(char(cp));
    } else if (cp < 0x800) {
      out.push_back(char(0xc0 | (cp >> 6)));
      out.push_back(char(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(char(0xe0 | (cp >> 12)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(char(0x80 | (cp & 0x3f)));
    }
  }
  return out;
}

bool desc_encode(const std::string& utf8, std::string& out) {
  // The shortest form each width may encode: an overlong sequence would let two
  // byte strings mean one character, which a client counting characters to fill
  // a fixed field cannot afford.
  static const uint32_t kShortest[5] = {0, 0, 0x80, 0x800, 0x10000};

  std::string bytes;
  bytes.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size();) {
    const uint8_t lead = uint8_t(utf8[i]);
    uint32_t cp = 0;
    size_t width = 0;
    if (lead < 0x80) { cp = lead; width = 1; }
    else if ((lead & 0xe0) == 0xc0) { cp = lead & 0x1fu; width = 2; }
    else if ((lead & 0xf0) == 0xe0) { cp = lead & 0x0fu; width = 3; }
    else if ((lead & 0xf8) == 0xf0) { cp = lead & 0x07u; width = 4; }
    else return false;
    if (i + width > utf8.size()) return false;
    for (size_t k = 1; k < width; k++) {
      const uint8_t tail = uint8_t(utf8[i + k]);
      if ((tail & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (tail & 0x3fu);
    }
    if (cp < kShortest[width] || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    i += width;

    // A description is one line drawn into a list row, so a control byte has no
    // place in it and 0x00 would end the field early.
    if (cp < 0x20 || cp == 0x7f) return false;
    if (cp < 0x7f) { bytes.push_back(char(cp)); continue; }
    size_t at = 128;
    for (size_t k = 0; k < 128; k++) {
      if (kCp437High[k] == cp) { at = k; break; }
    }
    if (at == 128) return false;
    bytes.push_back(char(0x80 + at));
  }
  out.swap(bytes);
  return true;
}

// ------------------------------------------------------------------- map

int Map::tileset() const {
  int value = erax_ >= 0 ? erax_ : era_;
  return value > 3 ? 0 : value;  // 0x04-0xff render as forest
}

void Map::set_tileset(int value) {
  era_ = value;
  if (erax_ >= 0) erax_ = value;
}

bool Map::set_description(const std::string& text) {
  // First, so that writing back what was read is always a no-op: one shipped
  // map fills the field with no room for a terminator, and a client that
  // harvests its fields on OK must not corrupt it by having shown it.
  if (text == description_) return true;  // keeps any bytes hiding past the NUL

  std::string bytes;
  if (!desc_encode(text, bytes)) return false;
  if (bytes.size() > size_t(kDescBytes - 1)) return false;
  std::memset(desc_raw_, 0, sizeof(desc_raw_));
  std::memcpy(desc_raw_, bytes.data(), bytes.size());
  // Round-tripped rather than assigned, so `description()` reports what the
  // file now holds and not what the caller hoped it would.
  description_ = desc_decode(desc_raw_, bytes.size());
  return true;
}

void Map::set_start_resources(int player, uint16_t g, uint16_t l, uint16_t o) {
  if (!in_range(player)) return;
  gold_[player] = g;
  lumber_[player] = l;
  oil_[player] = o;
}

uint16_t Map::tile_at(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
  return tiles_[size_t(y) * size_t(width_) + size_t(x)];
}

void Map::unit_footprint(int type, int& w, int& h) const {
  w = h = 1;
  if (type < 0 || type >= kUnitCount) return;
  // A map may carry no UDTA, and the game falls back to its own table when it
  // does; so must we, or every building on a new map is 1x1.
  default_unit_footprint(type, w, h);
  if (udta_.size_x[type]) w = udta_.size_x[type];
  if (udta_.size_y[type]) h = udta_.size_y[type];
}

int Map::add_unit(int x, int y, int type, int owner, int value) {
  if (units_.size() >= size_t(kMaxUnits)) return -1;
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return -1;
  Unit u;
  u.x = uint16_t(x);
  u.y = uint16_t(y);
  u.type = uint8_t(type);
  u.owner = uint8_t(owner);
  u.value = uint16_t(value);
  // Where it first stood is what its facing is drawn from, from here on.
  u.seed_x = u.x;
  u.seed_y = u.y;
  units_.push_back(u);
  return int(units_.size()) - 1;
}

void Map::refresh_unit_sizes() {
  // Footprints live inside the UDTA payload, so they have to be read out again
  // whenever it changes. One decoder, so the parser and pf_map_add_unit_data
  // cannot disagree about the offset.
  for (int i = 0; i < kUnitCount; i++) { udta_.size_x[i] = 0; udta_.size_y[i] = 0; }
  if (udta_.raw.size() < kUdtaUnitSizeOffset + size_t(kUnitCount) * 4) return;
  const uint8_t* q = udta_.raw.data() + kUdtaUnitSizeOffset;
  for (int i = 0; i < kUnitCount; i++) {
    udta_.size_x[i] = uint16_t(q[i * 4] | (q[i * 4 + 1] << 8));
    udta_.size_y[i] = uint16_t(q[i * 4 + 2] | (q[i * 4 + 3] << 8));
  }
}

bool Map::remove_unit(int index) {
  if (index < 0 || size_t(index) >= units_.size()) return false;
  units_.erase(units_.begin() + index);
  return true;
}

int Map::unit_at(int x, int y) const {
  for (int i = int(units_.size()) - 1; i >= 0; i--) {
    const Unit& u = units_[size_t(i)];
    int w, h;
    unit_footprint(u.type, w, h);
    if (x >= u.x && x < u.x + w && y >= u.y && y < u.y + h) return i;
  }
  return -1;
}

void Map::read_type(const uint8_t* payload, size_t len) {
  Reader r(payload, len);
  magic_ = r.fixed_string(std::min<size_t>(10, len));
  if (r.remaining() >= 2) {
    const uint8_t* p = r.bytes(2);
    if (p) { type_unused_[0] = p[0]; type_unused_[1] = p[1]; }
  }
  if (r.remaining() >= 4) id_tag_ = r.u32();
}

void Map::normalize_layers() {
  size_t n = tile_count();
  if (tiles_.size() != n) {
    warnings_.push_back("'MTXM' had " + std::to_string(tiles_.size()) +
                        " tiles, expected " + std::to_string(n));
  }
  fit(tiles_, n, kDefaultTile);
  fit(movement_, n, uint16_t(0x0001));
  fit(regions_, n, uint16_t(0x4000));
  fit(oil_map_, n, uint8_t(0));
}

Map* Map::parse(const uint8_t* data, size_t len, Status& status) {
  status = Status::Ok;
  if (!data) { status = Status::NotAPud; return nullptr; }

  Map* map = new Map();
  Reader r(data, len);
  bool saw_type = false;
  bool saw_dim = false;

  while (r.remaining() >= 8) {
    std::string tag = r.fixed_string(4);
    while (tag.size() < 4) tag.push_back(' ');
    uint32_t length = r.u32();

    if (length > r.remaining()) {
      map->warnings_.push_back("section '" + tag + "' claims " + std::to_string(length) +
                               " bytes but only " + std::to_string(r.remaining()) +
                               " remain; truncated");
    }
    size_t take = std::min<size_t>(length, r.remaining());
    const uint8_t* payload = r.bytes(take);
    if (!payload && take) break;
    map->section_order_.push_back(tag);

    Reader p(payload, take);
    if (tag == "TYPE") {
      map->read_type(payload, take);
      saw_type = true;
    } else if (tag == "VER ") {
      map->version_ = p.u16();
    } else if (tag == "DESC") {
      std::memset(map->desc_raw_, 0, kDescBytes);
      std::memcpy(map->desc_raw_, payload, std::min<size_t>(take, kDescBytes));
      size_t end = 0;
      while (end < kDescBytes && map->desc_raw_[end]) end++;
      map->description_ = desc_decode(map->desc_raw_, end);
    } else if (tag == "OWNR") {
      for (int i = 0; i < kPlayerCount; i++) map->owners_[i] = i < int(take) ? payload[i] : 0;
    } else if (tag == "ERA ") {
      map->era_ = p.u16();
    } else if (tag == "ERAX") {
      map->erax_ = p.u16();
    } else if (tag == "DIM ") {
      map->width_ = p.u16();
      map->height_ = p.u16();
      saw_dim = true;
    } else if (tag == "UDTA") {
      map->udta_.present = true;
      map->udta_.raw.assign(payload, payload + take);
      map->udta_.use_default = take >= 2 && (payload[0] | (payload[1] << 8)) != 0;
      map->refresh_unit_sizes();
    } else if (tag == "UGRD") {
      map->ugrd_.assign(payload, payload + take);
    } else if (tag == "ALOW") {
      map->alow_.assign(payload, payload + take);
    } else if (tag == "SIDE") {
      for (int i = 0; i < kPlayerCount; i++) map->sides_[i] = i < int(take) ? payload[i] : 0;
    } else if (tag == "SGLD" || tag == "SLBR" || tag == "SOIL") {
      uint16_t* dst = tag == "SGLD" ? map->gold_ : (tag == "SLBR" ? map->lumber_ : map->oil_);
      for (int i = 0; i < kPlayerCount; i++) dst[i] = p.u16();
    } else if (tag == "AIPL") {
      for (int i = 0; i < kPlayerCount; i++) map->ai_[i] = i < int(take) ? payload[i] : 0;
    } else if (tag == "MTXM") {
      copy_words(map->tiles_, payload, take, SIZE_MAX);
    } else if (tag == "SQM ") {
      copy_words(map->movement_, payload, take, SIZE_MAX);
    } else if (tag == "OILM") {
      map->oil_map_.assign(payload, payload + take);
    } else if (tag == "REGM") {
      copy_words(map->regions_, payload, take, SIZE_MAX);
    } else if (tag == "UNIT") {
      map->units_.clear();
      while (p.remaining() >= 8) {
        Unit u;
        u.x = p.u16();
        u.y = p.u16();
        u.type = p.u8();
        u.owner = p.u8();
        u.value = p.u16();
        // The facing seed is not in the file, so it is re-derived from the
        // stored position — which is what makes a map read back exactly as it
        // was drawn before it was ever edited.
        u.seed_x = u.x;
        u.seed_y = u.y;
        map->units_.push_back(u);
      }
    } else if (tag == "SIGN") {
      map->sign_ = take >= 4 ? int64_t(p.u32()) : 0;
    } else {
      map->extra_.push_back({tag, std::vector<uint8_t>(payload, payload + take)});
      map->warnings_.push_back("unknown section '" + tag + "' (" + std::to_string(take) +
                               " bytes) preserved");
    }
  }

  if (!saw_type || map->magic_ != "WAR2 MAP") {
    delete map;
    status = Status::NotAPud;
    return nullptr;
  }
  if (!saw_dim) {
    delete map;
    status = Status::Malformed;
    return nullptr;
  }
  if (map->width_ < 1 || map->height_ < 1 ||
      map->width_ > kMaxMapDim || map->height_ > kMaxMapDim) {
    delete map;
    status = Status::UnsupportedSize;
    return nullptr;
  }

  map->normalize_layers();
  return map;
}

int Map::resize(int width, int height, int offset_x, int offset_y, Status& status) {
  status = Status::Ok;
  if (width < 1 || height < 1 || width > kMaxMapDim || height > kMaxMapDim) {
    status = Status::UnsupportedSize;
    return -1;
  }

  const size_t n = size_t(width) * size_t(height);
  // The same varied ground a new map is created on, so a map grown by 32 tiles
  // does not gain a flat strip that reads as a different surface.
  std::vector<uint16_t> tiles(n);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      tiles[size_t(y) * size_t(width) + size_t(x)] = blank_tile(x, y);
    }
  }
  std::vector<uint16_t> movement(n, 0x0001);
  std::vector<uint16_t> regions(n, 0x4000);
  std::vector<uint8_t> oil(n, 0);

  // Copy the overlapping window. Everything outside keeps the fill above.
  for (int y = 0; y < height; y++) {
    const int src_y = y - offset_y;
    if (src_y < 0 || src_y >= height_) continue;
    for (int x = 0; x < width; x++) {
      const int src_x = x - offset_x;
      if (src_x < 0 || src_x >= width_) continue;
      const size_t dst = size_t(y) * size_t(width) + size_t(x);
      const size_t src = size_t(src_y) * size_t(width_) + size_t(src_x);
      if (src < tiles_.size()) tiles[dst] = tiles_[src];
      if (src < movement_.size()) movement[dst] = movement_[src];
      if (src < regions_.size()) regions[dst] = regions_[src];
      if (src < oil_map_.size()) oil[dst] = oil_map_[src];
    }
  }

  // Move units with the content, dropping any that no longer fit whole.
  std::vector<Unit> kept;
  kept.reserve(units_.size());
  int dropped = 0;
  for (const Unit& u : units_) {
    const int nx = int(u.x) + offset_x;
    const int ny = int(u.y) + offset_y;
    int fw = 1, fh = 1;
    unit_footprint(u.type, fw, fh);
    if (nx < 0 || ny < 0 || nx + fw > width || ny + fh > height) {
      dropped++;
      continue;
    }
    Unit moved = u;
    moved.x = uint16_t(nx);
    moved.y = uint16_t(ny);
    kept.push_back(moved);
  }

  width_ = width;
  height_ = height;
  tiles_ = std::move(tiles);
  movement_ = std::move(movement);
  regions_ = std::move(regions);
  oil_map_ = std::move(oil);
  units_ = std::move(kept);
  return dropped;
}

Map* Map::create(int width, int height, int tileset, Status& status) {
  status = Status::Ok;
  if (width < 1 || height < 1 || width > kMaxMapDim || height > kMaxMapDim) {
    status = Status::UnsupportedSize;
    return nullptr;
  }
  Map* map = new Map();
  map->width_ = width;
  map->height_ = height;
  map->era_ = tileset;
  map->set_description("Untitled");

  size_t n = map->tile_count();
  // Varied ground rather than one tile repeated: see blank_tile.
  map->tiles_.resize(n);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      map->tiles_[size_t(y) * size_t(width) + size_t(x)] = blank_tile(x, y);
    }
  }
  map->movement_.assign(n, 0x0001);
  map->regions_.assign(n, 0x4000);
  map->oil_map_.assign(n, 0);

  for (int i = 0; i < kPlayerCount; i++) map->owners_[i] = 3;  // Nobody
  for (int i = 0; i < 2; i++) {
    map->owners_[i] = 5;  // Human
    map->sides_[i] = uint8_t(i == 0 ? 0 : 1);
    map->gold_[i] = 2000;
    map->lumber_[i] = 1000;
    map->oil_[i] = 1000;
  }
  map->sides_[15] = 2;  // Neutral
  return map;
}

std::vector<uint8_t> Map::serialize() const {
  // Build each modelled section's payload, then emit by tag so the recorded
  // file order can be replayed.
  std::vector<std::pair<std::string, std::vector<uint8_t>>> parts;
  auto put = [&](const char* tag, Writer&& w) {
    parts.emplace_back(std::string(tag), w.take());
  };

  { Writer w;
    uint8_t desc[10] = {};
    size_t n = std::min<size_t>(magic_.size(), 9);
    std::memcpy(desc, magic_.data(), n);
    w.raw(desc, 10);
    w.raw(type_unused_, 2);
    w.u32(id_tag_);
    put("TYPE", std::move(w)); }
  { Writer w; w.u16(uint16_t(version_)); put("VER ", std::move(w)); }
  { Writer w; w.raw(desc_raw_, kDescBytes); put("DESC", std::move(w)); }
  { Writer w; w.raw(owners_, kPlayerCount); put("OWNR", std::move(w)); }
  { Writer w; w.u16(uint16_t(era_)); put("ERA ", std::move(w)); }
  if (erax_ >= 0) { Writer w; w.u16(uint16_t(erax_)); put("ERAX", std::move(w)); }
  { Writer w; w.u16(uint16_t(width_)); w.u16(uint16_t(height_)); put("DIM ", std::move(w)); }
  // Every one of the 1378 corpus maps carries both, so a map that reached us
  // without one is our own doing and gets the retail table rather than a hole.
  if (udta_.present) parts.emplace_back("UDTA", udta_.raw);
  else parts.emplace_back("UDTA", std::vector<uint8_t>(
      kDefaultUdta, kDefaultUdta + kDefaultUdtaSize));
  if (!ugrd_.empty()) parts.emplace_back("UGRD", ugrd_);
  else parts.emplace_back("UGRD", std::vector<uint8_t>(
      kDefaultUgrd, kDefaultUgrd + kDefaultUgrdSize));
  if (!alow_.empty()) parts.emplace_back("ALOW", alow_);
  { Writer w; w.raw(sides_, kPlayerCount); put("SIDE", std::move(w)); }
  { Writer w; for (int i = 0; i < kPlayerCount; i++) w.u16(gold_[i]); put("SGLD", std::move(w)); }
  { Writer w; for (int i = 0; i < kPlayerCount; i++) w.u16(lumber_[i]); put("SLBR", std::move(w)); }
  { Writer w; for (int i = 0; i < kPlayerCount; i++) w.u16(oil_[i]); put("SOIL", std::move(w)); }
  { Writer w; w.raw(ai_, kPlayerCount); put("AIPL", std::move(w)); }

  size_t n = tile_count();
  { Writer w; for (size_t i = 0; i < n; i++) w.u16(i < tiles_.size() ? tiles_[i] : kDefaultTile);
    put("MTXM", std::move(w)); }
  { Writer w; for (size_t i = 0; i < n; i++) w.u16(i < movement_.size() ? movement_[i] : 0x0001);
    put("SQM ", std::move(w)); }
  { Writer w; for (size_t i = 0; i < n; i++) w.u8(i < oil_map_.size() ? oil_map_[i] : 0);
    put("OILM", std::move(w)); }
  { Writer w; for (size_t i = 0; i < n; i++) w.u16(i < regions_.size() ? regions_[i] : 0x4000);
    put("REGM", std::move(w)); }
  { Writer w;
    for (const Unit& u : units_) {
      w.u16(u.x); w.u16(u.y); w.u8(u.type); w.u8(u.owner); w.u16(u.value);
    }
    put("UNIT", std::move(w)); }
  if (sign_ >= 0) { Writer w; w.u32(uint32_t(sign_)); put("SIGN", std::move(w)); }

  // Unmodelled sections are emitted by tag too, so they land back in their
  // original positions rather than all bunching up at the end.
  std::vector<RawSection> spare = extra_;
  std::vector<bool> done(parts.size(), false);

  Writer out;
  auto emit = [&](const std::string& tag, const std::vector<uint8_t>& data) {
    out.tag(tag.c_str());
    out.u32(uint32_t(data.size()));
    out.raw(data);
  };
  auto write_tag = [&](const std::string& tag) {
    for (size_t i = 0; i < parts.size(); i++) {
      if (!done[i] && parts[i].first == tag) {
        emit(tag, parts[i].second);
        done[i] = true;
        return;
      }
    }
    for (auto it = spare.begin(); it != spare.end(); ++it) {
      if (it->tag == tag) {
        emit(tag, it->data);
        spare.erase(it);
        return;
      }
    }
  };

  // A section the parsed file lacked belongs at its canonical position rather
  // than appended: no corpus map writes UDTA after UNIT, so a map that gains
  // one must not either. Files whose section set is unchanged keep their exact
  // recorded order, which is what makes the round-trip byte-exact.
  std::vector<std::string> order = section_order_;

  // Repair what an earlier version of this writer produced: UDTA, UGRD or ALOW
  // parked after UNIT, which not one of the 1378 corpus maps does. Dropping
  // them lets the insertion below put them back where retail writes them.
  auto unit_at = std::find(order.begin(), order.end(), std::string("UNIT"));
  if (unit_at != order.end()) {
    order.erase(std::remove_if(unit_at + 1, order.end(), [](const std::string& t) {
                  return t == "UDTA" || t == "UGRD" || t == "ALOW";
                }),
                order.end());
  }

  auto placed = [&](const std::string& t) {
    return std::find(order.begin(), order.end(), t) != order.end();
  };
  const size_t canon = sizeof(kWriteOrder) / sizeof(kWriteOrder[0]);
  for (size_t k = 0; k < canon; k++) {
    std::string tag = kWriteOrder[k];
    if (placed(tag)) continue;
    bool have = false;
    for (const auto& p : parts) if (p.first == tag) { have = true; break; }
    for (const RawSection& s : spare) if (!have && s.tag == tag) have = true;
    if (!have) continue;
    size_t at = 0;  // after the nearest canonical predecessor already present
    for (size_t i = k; i-- > 0;) {
      auto it = std::find(order.begin(), order.end(), std::string(kWriteOrder[i]));
      if (it != order.end()) { at = size_t(it - order.begin()) + 1; break; }
    }
    order.insert(order.begin() + at, tag);
  }

  for (const std::string& tag : order) write_tag(tag);
  for (const char* tag : kWriteOrder) write_tag(tag);
  for (const RawSection& s : spare) emit(s.tag, s.data);
  return out.take();
}

}  // namespace pf
