#include "art.hpp"

#include "constants.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// windows.h carries the 16-bit memory-model keywords as empty macros, and this
// file has a local called `far`. Nothing else in the core includes windows.h.
#undef far
#undef near
#endif

namespace pf {

const char* const kTilesetDirs[4] = {"forest", "iceland", "swamp", "xswamp"};

namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

/// 6-bit VGA component to 8-bit, mapping 63 to 255 exactly. Plain `v << 2`
/// tops out at 252 and leaves everything slightly dim.
uint8_t expand6(uint8_t v) { return uint8_t(((v << 2) | (v >> 4)) & 0xff); }

uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b) {
  return (uint32_t(0xff) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

}  // namespace

std::FILE* open_file(const std::string& path, const char* mode) {
#ifdef _WIN32
  // UTF-8 to UTF-16 and _wfopen: the narrow CRT takes the ANSI code page, which
  // on a Western install cannot spell a Cyrillic map name at all — those files
  // simply fail to open, silently.
  const int wide_len =
      MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0);
  if (wide_len <= 0) return nullptr;
  std::wstring wide(size_t(wide_len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), &wide[0], wide_len);
  const std::wstring wmode(mode, mode + std::strlen(mode));
  return _wfopen(wide.c_str(), wmode.c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
  std::FILE* f = open_file(path, "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long len = std::ftell(f);
  if (len < 0) { std::fclose(f); return false; }
  std::fseek(f, 0, SEEK_SET);
  out.resize(size_t(len));
  size_t got = len ? std::fread(out.data(), 1, size_t(len), f) : 0;
  std::fclose(f);
  if (got != size_t(len)) { out.clear(); return false; }
  return true;
}

// ---------------------------------------------------------------- tileset

TilesetArt* TilesetArt::open(const std::string& dir, int tileset) {
  if (tileset < 0 || tileset > 3) return nullptr;
  const std::string name = kTilesetDirs[tileset];
  const std::string base = dir + name + "/" + name;

  std::vector<uint8_t> cv4, vx4, vr4, ppl;
  if (!read_file(base + ".cv4", cv4) || !read_file(base + ".vx4", vx4) ||
      !read_file(base + ".vr4", vr4) || !read_file(base + ".ppl", ppl)) {
    return nullptr;
  }
  return open_bytes(cv4, vx4, vr4, ppl);
}

TilesetArt* TilesetArt::open_bytes(const std::vector<uint8_t>& cv4,
                                   const std::vector<uint8_t>& vx4,
                                   const std::vector<uint8_t>& vr4,
                                   const std::vector<uint8_t>& ppl) {
  if (cv4.size() != size_t(kCv4GroupCount) * kCv4GroupBytes) return nullptr;
  if (ppl.size() != 768) return nullptr;
  if (vx4.size() % (kMegatileRefs * 2) != 0 || vx4.empty()) return nullptr;
  if (vr4.size() % kMinitileBytes != 0 || vr4.empty()) return nullptr;

  auto* art = new TilesetArt();
  art->megatile_count_ = int(vx4.size() / (kMegatileRefs * 2));
  art->minitile_count_ = int(vr4.size() / kMinitileBytes);

  art->groups_.resize(cv4.size() / 2);
  for (size_t i = 0; i < art->groups_.size(); i++) art->groups_[i] = rd16(&cv4[i * 2]);
  art->megatiles_.resize(vx4.size() / 2);
  for (size_t i = 0; i < art->megatiles_.size(); i++) art->megatiles_[i] = rd16(&vx4[i * 2]);
  art->minitiles_ = vr4;

  for (int i = 0; i < 256; i++) {
    art->palette_[i] = pack_rgba(expand6(ppl[size_t(i) * 3]),
                                 expand6(ppl[size_t(i) * 3 + 1]),
                                 expand6(ppl[size_t(i) * 3 + 2]));
  }

  for (int i = 0; i < 256; i++) art->base_palette_[i] = art->palette_[i];

  art->build_averages();
  return art;
}

int TilesetArt::megatile_for(uint16_t tile) const {
  const int group = (tile >> 4) & 0x3ff;
  const int variation = tile & 0xf;
  if (group >= kCv4GroupCount) return -1;
  const uint16_t id = groups_[size_t(group) * 21 + size_t(variation)];
  if (id >= megatile_count_) return -1;
  // Variation 0 of group 0 is the legitimate "nothing" tile, so only treat an
  // id of 0 as missing for other groups.
  if (id == 0 && group != 0) return -1;
  return id;
}

void TilesetArt::set_water_phase(int phase) {
  water_phase_ = ((phase % kWaterCycle) + kWaterCycle) % kWaterCycle;
  for (int i = 0; i < kWaterCycle; i++) {
    palette_[kWaterFirst + i] =
        base_palette_[kWaterFirst + (i + water_phase_) % kWaterCycle];
  }
}

bool TilesetArt::draw_megatile(int megatile, uint32_t* out, int stride, int ox, int oy) const {
  if (megatile < 0 || megatile >= megatile_count_ || !out) return false;

  for (int sub = 0; sub < kMegatileRefs; sub++) {
    // A `vx4` reference is the minitile index in the top 14 bits and a
    // horizontal flip in bit 1.
    //
    // Bit 1, not bit 0, and horizontal, not vertical — both settled by the
    // data. A mirrored megatile lists the same minitiles in reversed column
    // order with bit 1 set, which only composes back into a clean mirror if
    // that bit turns each minitile left-to-right; reading it as a vertical flip
    // measured worse by seam energy than not flipping at all. Bit 0 is set in
    // 22 of forest's 5,952 references and never in either swamp, so it is not a
    // flag of any kind.
    const uint16_t ref = megatiles_[size_t(megatile) * kMegatileRefs + size_t(sub)];
    const int index = ref >> 2;
    if (index >= minitile_count_) continue;
    const bool flip_x = (ref >> 1) & 1;
    const uint8_t* pixels = &minitiles_[size_t(index) * kMinitileBytes];
    const int sx = ox + (sub % 4) * 8;
    const int sy = oy + (sub / 4) * 8;

    for (int y = 0; y < 8; y++) {
      const uint8_t* row = pixels + size_t(y) * 8;
      uint32_t* dst = out + size_t(sy + y) * size_t(stride) + size_t(sx);
      if (flip_x) {
        for (int x = 7; x >= 0; x--) *dst++ = palette_[row[x]];
      } else {
        for (int x = 0; x < 8; x++) *dst++ = palette_[row[x]];
      }
    }
  }
  return true;
}

void TilesetArt::build_averages() {
  averages_.assign(size_t(megatile_count_), 0);
  blank_.assign(size_t(megatile_count_), 0);
  detail_.assign(size_t(megatile_count_), 0);
  std::vector<uint32_t> block(size_t(kTilePx) * kTilePx);
  for (int m = 0; m < megatile_count_; m++) {
    std::fill(block.begin(), block.end(), 0u);
    draw_megatile(m, block.data(), kTilePx);
    uint64_t r = 0, g = 0, b = 0;
    for (uint32_t p : block) {
      r += p & 0xff;
      g += (p >> 8) & 0xff;
      b += (p >> 16) & 0xff;
    }
    const uint64_t n = block.size();
    averages_[size_t(m)] = pack_rgba(uint8_t(r / n), uint8_t(g / n), uint8_t(b / n));
    // Blank means "draws as a black square", judged by the average rather than
    // every pixel: each tileset carries 16 entirely empty megatiles, no shipped
    // map uses one, and painting one leaves a black hole.
    blank_[size_t(m)] = uint8_t((averages_[size_t(m)] & 0x00ffffffu) == 0);

    // Fraction of pixels far from this tile's own mean colour. Scattered rocks
    // on flat ground raise it; flat ground alone does not.
    const int ar = int(r / n), ag = int(g / n), ab = int(b / n);
    int far = 0;
    for (uint32_t p : block) {
      const int dr = int(p & 0xff) - ar;
      const int dg = int((p >> 8) & 0xff) - ag;
      const int db = int((p >> 16) & 0xff) - ab;
      if (dr * dr + dg * dg + db * db > 60 * 60) far++;
    }
    detail_[size_t(m)] = uint8_t(far * 100 / int(block.size()));
  }
}

bool TilesetArt::is_blank(int megatile) const {
  if (megatile < 0 || megatile >= megatile_count_) return true;
  return blank_[size_t(megatile)] != 0;
}

int TilesetArt::detail(int megatile) const {
  if (megatile < 0 || megatile >= megatile_count_) return 0;
  return detail_[size_t(megatile)];
}

uint32_t TilesetArt::average(int megatile) const {
  if (megatile < 0 || megatile >= megatile_count_) return 0xff000000u;
  return averages_[size_t(megatile)];
}

// ----------------------------------------------------------------- sprite

Sprite* Sprite::open_path(const std::string& dir, const std::string& name) {
  std::vector<uint8_t> bytes;
  if (!read_file(dir + name + ".grp", bytes)) return nullptr;
  return open_bytes(std::move(bytes));
}

Sprite* Sprite::open_bytes(std::vector<uint8_t> bytes) {
  if (bytes.size() < 6) return nullptr;

  const int frame_count = rd16(&bytes[0]);
  const int width = rd16(&bytes[2]);
  const int height = rd16(&bytes[4]);
  if (frame_count <= 0 || width <= 0 || height <= 0) return nullptr;
  if (size_t(6) + size_t(frame_count) * 8 > bytes.size()) return nullptr;

  auto* sprite = new Sprite();
  sprite->width_ = width;
  sprite->height_ = height;
  sprite->frames_.reserve(size_t(frame_count));
  for (int i = 0; i < frame_count; i++) {
    const uint8_t* p = &bytes[6 + size_t(i) * 8];
    Frame f{p[0], p[1], p[2], p[3], rd32(p + 4)};
    sprite->frames_.push_back(f);
  }
  sprite->bytes_ = std::move(bytes);
  return sprite;
}

Sprite* Sprite::open(const std::string& dir, int unit_id, int tileset) {
  const std::string path = sprite_path_for(unit_id, tileset);
  if (path.empty()) return nullptr;
  if (Sprite* s = open_path(dir, path)) return s;
  // Tileset variants often don't exist; fall back to the forest original.
  const std::string base = sprite_path_for(unit_id, 0);
  if (!base.empty() && base != path) return open_path(dir, base);
  return nullptr;
}

bool Sprite::draw_frame(int index, const uint32_t* palette, uint32_t* out) const {
  if (index < 0 || size_t(index) >= frames_.size() || !palette || !out) return false;
  const Frame& f = frames_[size_t(index)];
  if (size_t(f.offset) + size_t(f.h) * 2 > bytes_.size()) return false;

  const uint8_t* data = bytes_.data();
  const size_t len = bytes_.size();

  for (int row = 0; row < f.h; row++) {
    const int dy = f.y + row;
    if (dy >= height_) break;
    const size_t row_start = size_t(f.offset) + rd16(data + f.offset + size_t(row) * 2);
    uint32_t* line = out + size_t(dy) * size_t(width_) + size_t(f.x);

    size_t p = row_start;
    int x = 0;
    while (x < f.w && p < len) {
      const uint8_t control = data[p++];
      if (control & 0x80) {
        // Transparent run.
        x += control & 0x7f;
      } else if (control & 0x40) {
        // Repeat one palette index.
        const int run = control & 0x3f;
        if (p >= len) break;
        const uint32_t color = palette[data[p++]];
        for (int i = 0; i < run; i++, x++) {
          if (x < f.w && f.x + x < width_) line[x] = color;
        }
      } else {
        // Literal run. The read pointer must advance by the full count even
        // when the row is clipped, or the next control byte is read mid-run and
        // the rest of the frame smears.
        for (int i = 0; i < control; i++, x++, p++) {
          if (p < len && x < f.w && f.x + x < width_) line[x] = palette[data[p]];
        }
      }
    }
  }
  return true;
}

std::string sprite_path_for(int unit_id, int tileset) {
  if (unit_id < 0 || unit_id >= kUnitCount) return {};
  if (tileset < 0 || tileset > 3) tileset = 0;

  // Critters pick a different animal per tileset rather than a prefixed
  // variant of one, so they bypass the prefix logic entirely.
  if (unit_id == 0x39) return kCritterSprites[tileset];

  const std::string base = kUnitSprites[unit_id];
  if (base.empty()) return {};

  const std::string prefix = kTilesetSpritePrefix[tileset];
  if (prefix.empty()) return base;

  const size_t slash = base.rfind('/');
  if (slash == std::string::npos) return prefix + base;
  return base.substr(0, slash + 1) + prefix + base.substr(slash + 1);
}

void apply_player_color(const uint32_t* base, uint32_t rgb, uint32_t* out) {
  std::memcpy(out, base, 256 * sizeof(uint32_t));
  const uint8_t r = uint8_t((rgb >> 16) & 0xff);
  const uint8_t g = uint8_t((rgb >> 8) & 0xff);
  const uint8_t b = uint8_t(rgb & 0xff);
  // Relative brightness of the shipped red ramp.
  static const double kSteps[kPlayerColorCount] = {1.0, 0.753, 0.56, 0.416};
  for (int i = 0; i < kPlayerColorCount; i++) {
    out[kPlayerColorStart + i] = pack_rgba(uint8_t(r * kSteps[i] + 0.5),
                                           uint8_t(g * kSteps[i] + 0.5),
                                           uint8_t(b * kSteps[i] + 0.5));
  }
}

}  // namespace pf
