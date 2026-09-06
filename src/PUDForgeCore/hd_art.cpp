#include "hd_art.hpp"

#include <algorithm>
#include <cstring>

#include "constants.hpp"
// Through our own ABI for the two rules it already owns: which sprite a unit
// draws, and how many facings that sprite has. Restating either here would be
// the second implementation the layering rule exists to prevent.
#include "pudforge/pudforge.h"

namespace pf {
namespace {

// ------------------------------------------------------- the sidecar reader
//
// A scanner over the shape TexturePacker writes rather than a JSON parser. It
// walks the `frames` object entry by entry, skipping each value's braces
// whole — the trap being that a value's own fields are named `x`, `y`, `w` and
// `h` too, so a search that does not respect nesting reads the wrong ones.

void skip_space(const char*& p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
}

/// The quoted string starting at `p`, which must be on the opening quote.
/// Escapes are passed through: no key TexturePacker writes carries one.
bool read_string(const char*& p, const char* end, std::string& out) {
  if (p >= end || *p != '"') return false;
  const char* start = ++p;
  while (p < end && *p != '"') {
    if (*p == '\\' && p + 1 < end) p++;
    p++;
  }
  if (p >= end) return false;
  out.assign(start, size_t(p - start));
  p++;
  return true;
}

/// Past a value of any kind, leaving `p` after it.
bool skip_value(const char*& p, const char* end) {
  skip_space(p, end);
  if (p >= end) return false;
  if (*p == '"') {
    std::string ignored;
    return read_string(p, end, ignored);
  }
  if (*p == '{' || *p == '[') {
    const char open = *p, close = open == '{' ? '}' : ']';
    int depth = 0;
    while (p < end) {
      if (*p == '"') {
        std::string ignored;
        if (!read_string(p, end, ignored)) return false;
        continue;
      }
      if (*p == open) depth++;
      else if (*p == close && --depth == 0) { p++; return true; }
      p++;
    }
    return false;
  }
  while (p < end && *p != ',' && *p != '}' && *p != ']') p++;
  return true;
}

/// An integer field of the object `p` is inside, without descending into any
/// nested object. Leaves `p` where it found it; -1 when the field is absent.
int field_int(const char* body, const char* end, const char* name, int fallback) {
  const char* p = body;
  skip_space(p, end);
  if (p < end && *p == '{') p++;
  for (;;) {
    skip_space(p, end);
    if (p >= end || *p == '}') return fallback;
    std::string key;
    if (!read_string(p, end, key)) return fallback;
    skip_space(p, end);
    if (p >= end || *p != ':') return fallback;
    p++;
    skip_space(p, end);
    if (key == name && p < end && (*p == '-' || (*p >= '0' && *p <= '9'))) {
      return int(std::strtol(p, nullptr, 10));
    }
    if (!skip_value(p, end)) return fallback;
    skip_space(p, end);
    if (p < end && *p == ',') p++;
  }
}

/// The nested object named `name`, as a range. Empty when it is not there.
bool field_object(const char* body, const char* end, const char* name,
                  const char*& out_begin, const char*& out_end) {
  const char* p = body;
  skip_space(p, end);
  if (p < end && *p == '{') p++;
  for (;;) {
    skip_space(p, end);
    if (p >= end || *p == '}') return false;
    std::string key;
    if (!read_string(p, end, key)) return false;
    skip_space(p, end);
    if (p >= end || *p != ':') return false;
    p++;
    skip_space(p, end);
    const char* value = p;
    if (!skip_value(p, end)) return false;
    if (key == name && value < p && *value == '{') {
      out_begin = value;
      out_end = p;
      return true;
    }
    skip_space(p, end);
    if (p < end && *p == ',') p++;
  }
}

bool field_true(const char* body, const char* end, const char* name) {
  const char* p = body;
  skip_space(p, end);
  if (p < end && *p == '{') p++;
  for (;;) {
    skip_space(p, end);
    if (p >= end || *p == '}') return false;
    std::string key;
    if (!read_string(p, end, key)) return false;
    skip_space(p, end);
    if (p >= end || *p != ':') return false;
    p++;
    skip_space(p, end);
    if (key == name) return p + 4 <= end && std::memcmp(p, "true", 4) == 0;
    if (!skip_value(p, end)) return false;
    skip_space(p, end);
    if (p < end && *p == ',') p++;
  }
}

// --------------------------------------------------------------- rescaling

/// A box filter: every source pixel inside the destination pixel's footprint,
/// averaged. Right for shrinking, which is the only direction this goes — the
/// artwork is 6.75x the size the editor draws at.
void scale_box(const std::vector<uint32_t>& src, int sw, int sh,
               std::vector<uint32_t>& dst, int dw, int dh) {
  dst.assign(size_t(dw) * size_t(dh), 0);
  for (int y = 0; y < dh; y++) {
    const int y0 = int(int64_t(y) * sh / dh);
    const int y1 = std::max(y0 + 1, int(int64_t(y + 1) * sh / dh));
    for (int x = 0; x < dw; x++) {
      const int x0 = int(int64_t(x) * sw / dw);
      const int x1 = std::max(x0 + 1, int(int64_t(x + 1) * sw / dw));
      // Premultiplied, so a transparent pixel contributes no colour rather
      // than contributing black.
      uint64_t r = 0, g = 0, b = 0, a = 0;
      uint64_t n = 0;
      for (int sy = y0; sy < y1 && sy < sh; sy++) {
        for (int sx = x0; sx < x1 && sx < sw; sx++) {
          const uint32_t p = src[size_t(sy) * size_t(sw) + size_t(sx)];
          const uint64_t pa = (p >> 24) & 0xff;
          r += ((p >> 0) & 0xff) * pa;
          g += ((p >> 8) & 0xff) * pa;
          b += ((p >> 16) & 0xff) * pa;
          a += pa;
          n++;
        }
      }
      if (!n) continue;
      const uint64_t alpha = a / n;
      uint32_t out = uint32_t(alpha) << 24;
      if (a) {
        // Back to straight alpha, rounding rather than truncating so a flat
        // colour survives the trip unchanged.
        out |= uint32_t(std::min<uint64_t>(255, (r + a / 2) / a)) << 0;
        out |= uint32_t(std::min<uint64_t>(255, (g + a / 2) / a)) << 8;
        out |= uint32_t(std::min<uint64_t>(255, (b + a / 2) / a)) << 16;
      }
      dst[size_t(y) * size_t(dw) + size_t(x)] = out;
    }
  }
}

}  // namespace

std::vector<const HdFrame*> HdAtlas::sprite(const std::string& stem) const {
  std::vector<const HdFrame*> out;
  for (const HdFrame& f : frames) {
    const size_t underscore = f.key.rfind('_');
    if (underscore == std::string::npos) continue;
    if (f.key.compare(0, underscore, stem) != 0) continue;
    if (underscore != stem.size()) continue;
    out.push_back(&f);
  }
  // By frame number, which is the order the game animates them in and the
  // order the .grp keeps — so index 0 is the south-facing idle either way.
  std::sort(out.begin(), out.end(), [](const HdFrame* a, const HdFrame* b) {
    return std::strtol(a->key.c_str() + a->key.rfind('_') + 1, nullptr, 10) <
           std::strtol(b->key.c_str() + b->key.rfind('_') + 1, nullptr, 10);
  });
  return out;
}

bool parse_hd_atlas(const char* json, size_t length, HdAtlas& out) {
  if (!json || length == 0) return false;
  const char* end = json + length;

  const char* meta_begin = nullptr;
  const char* meta_end = nullptr;
  if (field_object(json, end, "meta", meta_begin, meta_end)) {
    const char* size_begin = nullptr;
    const char* size_end = nullptr;
    if (field_object(meta_begin, meta_end, "size", size_begin, size_end)) {
      out.width = field_int(size_begin, size_end, "w", 0);
      out.height = field_int(size_begin, size_end, "h", 0);
    }
  }

  const char* frames_begin = nullptr;
  const char* frames_end = nullptr;
  if (!field_object(json, end, "frames", frames_begin, frames_end)) return false;

  const char* p = frames_begin;
  skip_space(p, frames_end);
  if (p < frames_end && *p == '{') p++;
  for (;;) {
    skip_space(p, frames_end);
    if (p >= frames_end || *p == '}') break;
    std::string key;
    if (!read_string(p, frames_end, key)) return false;
    skip_space(p, frames_end);
    if (p >= frames_end || *p != ':') return false;
    p++;
    skip_space(p, frames_end);
    const char* value = p;
    if (!skip_value(p, frames_end)) return false;

    HdFrame frame;
    frame.key = key;
    frame.rotated = field_true(value, p, "rotated");
    const char* r_begin = nullptr;
    const char* r_end = nullptr;
    if (field_object(value, p, "frame", r_begin, r_end)) {
      frame.x = field_int(r_begin, r_end, "x", 0);
      frame.y = field_int(r_begin, r_end, "y", 0);
      frame.w = field_int(r_begin, r_end, "w", 0);
      frame.h = field_int(r_begin, r_end, "h", 0);
    }
    if (field_object(value, p, "spriteSourceSize", r_begin, r_end)) {
      frame.offset_x = field_int(r_begin, r_end, "x", 0);
      frame.offset_y = field_int(r_begin, r_end, "y", 0);
    }
    if (field_object(value, p, "sourceSize", r_begin, r_end)) {
      frame.source_w = field_int(r_begin, r_end, "w", 0);
      frame.source_h = field_int(r_begin, r_end, "h", 0);
    }
    // A frame with no rectangle is a sidecar we have misread, not a frame.
    if (frame.w > 0 && frame.h > 0) out.frames.push_back(std::move(frame));

    skip_space(p, frames_end);
    if (p < frames_end && *p == ',') p++;
  }
  return !out.frames.empty();
}

std::vector<uint32_t> cut_hd_frame(const uint32_t* atlas, int atlas_w, int atlas_h,
                                   const HdFrame& frame, int tile_px, int* out_w,
                                   int* out_h) {
  if (out_w) *out_w = 0;
  if (out_h) *out_h = 0;
  if (!atlas || atlas_w <= 0 || atlas_h <= 0 || tile_px <= 0) return {};
  const int source_w = frame.source_w > 0 ? frame.source_w : frame.w;
  const int source_h = frame.source_h > 0 ? frame.source_h : frame.h;
  if (source_w <= 0 || source_h <= 0) return {};

  // Un-trimmed first: the frame put back on the canvas the artist drew, so
  // every frame of a sprite shares one origin.
  std::vector<uint32_t> canvas(size_t(source_w) * size_t(source_h), 0);
  for (int y = 0; y < frame.h; y++) {
    for (int x = 0; x < frame.w; x++) {
      // A packer may turn a frame a quarter turn to fit it; undo that here so
      // nothing downstream has to know.
      const int ax = frame.rotated ? frame.x + y : frame.x + x;
      const int ay = frame.rotated ? frame.y + (frame.w - 1 - x) : frame.y + y;
      if (ax < 0 || ay < 0 || ax >= atlas_w || ay >= atlas_h) continue;
      const int cx = frame.offset_x + x;
      const int cy = frame.offset_y + y;
      if (cx < 0 || cy < 0 || cx >= source_w || cy >= source_h) continue;
      canvas[size_t(cy) * size_t(source_w) + size_t(cx)] =
          atlas[size_t(ay) * size_t(atlas_w) + size_t(ax)];
    }
  }

  // One scale for everything, taken from the artwork rather than from this
  // frame: fitting each canvas into a box would scale every sprite by its own
  // padding, which is how a Footman ends up two thirds the size of the tile
  // it stands on. See kHdPixelsPerTile.
  const int dw = std::max(1, int(int64_t(source_w) * tile_px / kHdPixelsPerTile));
  const int dh = std::max(1, int(int64_t(source_h) * tile_px / kHdPixelsPerTile));
  std::vector<uint32_t> scaled;
  if (dw == source_w && dh == source_h) scaled = std::move(canvas);
  else scale_box(canvas, source_w, source_h, scaled, dw, dh);

  if (out_w) *out_w = dw;
  if (out_h) *out_h = dh;
  return scaled;
}

// ------------------------------------------------------------------ terrain

int hd_terrain_frame(int megatile, int frames_in_atlas) {
  const int frame = megatile - kHdTerrainSkipped;
  if (frame < 0 || frame >= frames_in_atlas) return -1;
  return frame;
}

namespace {

/// The frame put back on its source canvas, which both cutters need.
std::vector<uint32_t> untrimmed(const uint32_t* atlas, int atlas_w, int atlas_h,
                                const HdFrame& frame, int& source_w, int& source_h) {
  source_w = frame.source_w > 0 ? frame.source_w : frame.w;
  source_h = frame.source_h > 0 ? frame.source_h : frame.h;
  std::vector<uint32_t> canvas(size_t(source_w) * size_t(source_h), 0);
  for (int y = 0; y < frame.h; y++) {
    for (int x = 0; x < frame.w; x++) {
      const int ax = frame.rotated ? frame.x + y : frame.x + x;
      const int ay = frame.rotated ? frame.y + (frame.w - 1 - x) : frame.y + y;
      if (ax < 0 || ay < 0 || ax >= atlas_w || ay >= atlas_h) continue;
      const int cx = frame.offset_x + x;
      const int cy = frame.offset_y + y;
      if (cx < 0 || cy < 0 || cx >= source_w || cy >= source_h) continue;
      canvas[size_t(cy) * size_t(source_w) + size_t(cx)] =
          atlas[size_t(ay) * size_t(atlas_w) + size_t(ax)];
    }
  }
  return canvas;
}

}  // namespace

std::vector<uint32_t> cut_hd_frame_fit(const uint32_t* atlas, int atlas_w, int atlas_h,
                                       const HdFrame& frame, int box_w, int box_h,
                                       int* out_w, int* out_h) {
  if (out_w) *out_w = 0;
  if (out_h) *out_h = 0;
  if (!atlas || atlas_w <= 0 || atlas_h <= 0 || box_w <= 0 || box_h <= 0) return {};
  int source_w = 0, source_h = 0;
  std::vector<uint32_t> canvas =
      untrimmed(atlas, atlas_w, atlas_h, frame, source_w, source_h);
  if (source_w <= 0 || source_h <= 0) return {};

  // The smaller of the two ratios, so the whole frame fits and keeps its shape.
  int dw = box_w, dh = box_h;
  if (int64_t(source_w) * box_h > int64_t(source_h) * box_w) {
    dh = std::max(1, int(int64_t(source_h) * box_w / source_w));
  } else {
    dw = std::max(1, int(int64_t(source_w) * box_h / source_h));
  }
  std::vector<uint32_t> scaled;
  if (dw == source_w && dh == source_h) scaled = std::move(canvas);
  else scale_box(canvas, source_w, source_h, scaled, dw, dh);
  if (out_w) *out_w = dw;
  if (out_h) *out_h = dh;
  return scaled;
}

// ------------------------------------------------------------- the import

std::vector<HdWanted> hd_wanted_sprites() {
  std::vector<HdWanted> out;
  for (int unit = 0; unit < kUnitCount; unit++) {
    const char* path = kUnitSprites[unit];
    if (!path || !*path) continue;              // a slot with no artwork
    const char* slash = std::strchr(path, '/');
    if (!slash) continue;
    HdWanted want;
    want.unit_id = unit;
    want.race.assign(path, size_t(slash - path));
    want.stem = slash + 1;
    want.frames = std::max(1, pf_unit_facing_count(unit));

    // One entry per sprite, not per unit: a Footman and an Attack Peasant
    // draw the same file, and the cache should hold it once. The wider of the
    // two frame counts wins, so neither is short of a facing.
    bool merged = false;
    for (HdWanted& seen : out) {
      if (seen.race != want.race || seen.stem != want.stem) continue;
      seen.frames = std::max(seen.frames, want.frames);
      merged = true;
      break;
    }
    if (!merged) out.push_back(std::move(want));
  }
  return out;
}

const HdCachedSprite* HdCache::find(int unit_id) const {
  if (unit_id < 0 || unit_id >= kUnitCount) return nullptr;
  const char* path = kUnitSprites[unit_id];
  if (!path || !*path) return nullptr;
  const char* slash = std::strchr(path, '/');
  if (!slash) return nullptr;
  const std::string race(path, size_t(slash - path));
  const std::string stem = slash + 1;
  for (const HdCachedSprite& sprite : sprites) {
    if (sprite.race == race && sprite.stem == stem) return &sprite;
  }
  return nullptr;
}

const uint32_t* HdCachedTiles::tile(int megatile) const {
  if (megatile < 0 || megatile >= count || size <= 0) return nullptr;
  const size_t at = size_t(megatile) * size_t(size) * size_t(size);
  if (at + size_t(size) * size_t(size) > pixels.size()) return nullptr;
  return pixels.data() + at;
}

const HdCachedTiles* HdCache::tiles(int tileset) const {
  for (const HdCachedTiles& set : tilesets) {
    if (set.tileset == tileset) return &set;
  }
  return nullptr;
}

namespace {

constexpr uint32_t kCacheMagic = 0x44485046;   // "PFHD", little end first
constexpr uint32_t kCacheVersion = 2;

void put32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v >> 16));
  out.push_back(uint8_t(v >> 24));
}

uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

}  // namespace

std::vector<uint8_t> write_hd_cache(const HdCache& cache) {
  std::vector<uint8_t> out;
  put32(out, kCacheMagic);
  put32(out, kCacheVersion);
  put32(out, uint32_t(cache.tile_px));
  put32(out, uint32_t(cache.stamp.size()));
  out.insert(out.end(), cache.stamp.begin(), cache.stamp.end());
  put32(out, uint32_t(cache.sprites.size()));
  for (const HdCachedSprite& sprite : cache.sprites) {
    put32(out, uint32_t(sprite.race.size()));
    out.insert(out.end(), sprite.race.begin(), sprite.race.end());
    put32(out, uint32_t(sprite.stem.size()));
    out.insert(out.end(), sprite.stem.begin(), sprite.stem.end());
    put32(out, uint32_t(sprite.width));
    put32(out, uint32_t(sprite.height));
    put32(out, uint32_t(sprite.frames));
    put32(out, sprite.mask.empty() ? 0u : 1u);
    for (uint32_t px : sprite.pixels) put32(out, px);
    for (uint32_t px : sprite.mask) put32(out, px);
  }
  put32(out, uint32_t(cache.tilesets.size()));
  for (const HdCachedTiles& set : cache.tilesets) {
    put32(out, uint32_t(set.tileset));
    put32(out, uint32_t(set.size));
    put32(out, uint32_t(set.count));
    for (uint32_t px : set.pixels) put32(out, px);
  }
  return out;
}

bool read_hd_cache(const uint8_t* bytes, size_t length, int want_tile_px,
                   const std::string& want_stamp, HdCache& out) {
  out = HdCache();
  if (!bytes || length < 20) return false;
  size_t at = 0;
  if (get32(bytes) != kCacheMagic) return false;
  if (get32(bytes + 4) != kCacheVersion) return false;
  out.tile_px = int(get32(bytes + 8));
  const uint32_t stamp_len = get32(bytes + 12);
  at = 16;
  if (at + stamp_len + 4 > length) return false;
  out.stamp.assign(reinterpret_cast<const char*>(bytes + at), stamp_len);
  at += stamp_len;

  // Built for another setting or another game is not a cache, it is a
  // rebuild. Checked here rather than by the caller so no host can forget.
  if (want_tile_px > 0 && out.tile_px != want_tile_px) return false;
  if (!want_stamp.empty() && out.stamp != want_stamp) return false;

  const uint32_t count = get32(bytes + at);
  at += 4;
  out.sprites.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    HdCachedSprite sprite;
    for (std::string* name : {&sprite.race, &sprite.stem}) {
      if (at + 4 > length) return false;
      const uint32_t n = get32(bytes + at);
      at += 4;
      if (n > length - at) return false;
      name->assign(reinterpret_cast<const char*>(bytes + at), n);
      at += n;
    }
    if (at + 16 > length) return false;
    sprite.width = int(get32(bytes + at));
    sprite.height = int(get32(bytes + at + 4));
    sprite.frames = int(get32(bytes + at + 8));
    const bool has_mask = get32(bytes + at + 12) != 0;
    at += 16;
    if (sprite.width <= 0 || sprite.height <= 0 || sprite.frames <= 0) return false;
    const size_t pixels = size_t(sprite.width) * size_t(sprite.height) * size_t(sprite.frames);
    // The length check that stops a truncated file being read as a huge one.
    if (pixels > (length - at) / 4) return false;
    sprite.pixels.resize(pixels);
    for (size_t p = 0; p < pixels; p++) sprite.pixels[p] = get32(bytes + at + p * 4);
    at += pixels * 4;
    if (has_mask) {
      if (pixels > (length - at) / 4) return false;
      sprite.mask.resize(pixels);
      for (size_t p = 0; p < pixels; p++) sprite.mask[p] = get32(bytes + at + p * 4);
      at += pixels * 4;
    }
    out.sprites.push_back(std::move(sprite));
  }

  // Tilesets came later than sprites; a cache written before them simply ends
  // here, and reading one is not an error.
  if (at + 4 > length) return true;
  const uint32_t sets = get32(bytes + at);
  at += 4;
  for (uint32_t i = 0; i < sets; i++) {
    if (at + 12 > length) return false;
    HdCachedTiles set;
    set.tileset = int(get32(bytes + at));
    set.size = int(get32(bytes + at + 4));
    set.count = int(get32(bytes + at + 8));
    at += 12;
    if (set.size <= 0 || set.count <= 0) return false;
    const size_t pixels = size_t(set.size) * size_t(set.size) * size_t(set.count);
    if (pixels > (length - at) / 4) return false;
    set.pixels.resize(pixels);
    for (size_t p = 0; p < pixels; p++) set.pixels[p] = get32(bytes + at + p * 4);
    at += pixels * 4;
    out.tilesets.push_back(std::move(set));
  }
  return true;
}

}  // namespace pf
