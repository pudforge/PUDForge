// Warcraft II artwork decoding: tilesets and unit sprites.
//
// Battle.net Edition stores these as named files inside War2Dat.mpq. This code
// reads them from an unpacked copy on disk; see reference/docs/tileset-format.md.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pf {

constexpr int kCv4GroupCount = 158;
constexpr int kCv4GroupBytes = 42;
constexpr int kMinitileBytes = 64;
constexpr int kMegatileRefs = 16;
constexpr int kTilePx = 32;

/// Tileset directory names indexed by `ERA `. The internal names are shifted
/// from the ones players see: the "wasteland" tileset is stored as `swamp`,
/// and the expansion's swamp is `xswamp`.
extern const char* const kTilesetDirs[4];

/// Decoded tileset artwork.
///
/// The lookup chain is
///   MTXM tile -> cv4[tile >> 4].megatiles[tile & 0xF] -> vx4 megatile
///             -> 16 x (vr4 minitile, hflip, vflip) -> ppl palette
class TilesetArt {
 public:
  /// @param dir directory holding the tileset folders, e.g. ".../art/bgs/"
  static TilesetArt* open(const std::string& dir, int tileset);

  /// Decode from buffers the caller already has.
  ///
  /// The path-based `open` is a convenience over this: a browser has no
  /// filesystem to point at, so the web client reads the four files however it
  /// can and hands the bytes over.
  static TilesetArt* open_bytes(const std::vector<uint8_t>& cv4,
                                const std::vector<uint8_t>& vx4,
                                const std::vector<uint8_t>& vr4,
                                const std::vector<uint8_t>& ppl);

  int megatile_count() const { return megatile_count_; }
  /// Megatile id for a tile value, or -1 when the tile has no artwork.
  int megatile_for(uint16_t tile) const;

  /**
   * Advance the water animation.
   *
   * Warcraft II animates water by cycling palette entries rather than by
   * holding extra frames: indices 38 to 47 are used by water and by nothing
   * else in all four tilesets, and in each they run one rise and fall of a
   * wave. Rotating them walks the highlight across the sea.
   *
   * `phase` is taken modulo `kWaterCycle`; the artwork itself never changes.
   */
  void set_water_phase(int phase);
  int water_phase() const { return water_phase_; }

  /// First index of the cycling band, and how many entries it holds.
  static constexpr int kWaterFirst = 38;
  static constexpr int kWaterCycle = 10;

  /// Rasterise a megatile as 32x32 packed RGBA into `out` at `stride` px/row.
  bool draw_megatile(int megatile, uint32_t* out, int stride, int ox = 0, int oy = 0) const;

  /// Mean colour of a megatile, for minimaps and zoomed-out views.
  uint32_t average(int megatile) const;

  /// How much a megatile deviates from its own mean colour, 0-100.
  ///
  /// Forest and rock score high everywhere because those textures are busy in
  /// themselves, so this is only meaningful compared with other variations of
  /// the same tile group.
  int detail(int megatile) const;

  /// True when a megatile is entirely the palette's index 0, i.e. empty.
  /// Tilesets carry a handful of these in otherwise-populated groups; they
  /// are valid indices but draw as a black square, so painting must skip them.
  bool is_blank(int megatile) const;

  const uint32_t* palette() const { return palette_; }

 private:
  void build_averages();

  std::vector<uint16_t> groups_;     // 158 x 21
  std::vector<uint16_t> megatiles_;  // count x 16 minitile refs
  std::vector<uint8_t> minitiles_;   // 64 bytes each
  std::vector<uint32_t> averages_;
  std::vector<uint8_t> blank_;
  std::vector<uint8_t> detail_;
  uint32_t palette_[256] = {};
  /// The palette as loaded. `palette_` is this with the water band rotated.
  uint32_t base_palette_[256] = {};
  int water_phase_ = 0;
  int megatile_count_ = 0;
  int minitile_count_ = 0;
};

/// A decoded `.grp` sprite: frames of palette indices, RLE-compressed per row.
class Sprite {
 public:
  /// @param dir directory holding the unit folders, e.g. ".../art/unit/"
  static Sprite* open(const std::string& dir, int unit_id, int tileset);
  /// Load a specific file, e.g. "human/thall".
  static Sprite* open_path(const std::string& dir, const std::string& name);
  /// Decode from bytes the caller already has, for hosts with no filesystem.
  static Sprite* open_bytes(std::vector<uint8_t> bytes);

  int width() const { return width_; }
  int height() const { return height_; }
  int frame_count() const { return int(frames_.size()); }

  /// Rasterise a frame into packed RGBA on the full canvas. Untouched pixels
  /// are left alone, so clear `out` first.
  bool draw_frame(int index, const uint32_t* palette, uint32_t* out) const;

 private:
  struct Frame {
    uint8_t x, y, w, h;
    uint32_t offset;
  };
  std::vector<uint8_t> bytes_;
  std::vector<Frame> frames_;
  int width_ = 0;
  int height_ = 0;
};

/// Palette indices the game swaps to tint a unit with its owner's colour.
constexpr int kPlayerColorStart = 208;
constexpr int kPlayerColorCount = 4;

/// Copy `base` with the player-colour ramp replaced by shades of `rgb`.
/// The substitution table lives in the game executable rather than the data
/// files, so the ramp is derived by scaling down the same brightness curve the
/// shipped red ramp uses (166, 125, 93, 69).
void apply_player_color(const uint32_t* base, uint32_t rgb, uint32_t* out);

/// Sprite path for a unit, or an empty string when it has no artwork.
/// Returns the tileset variant when one exists, else the forest original.
std::string sprite_path_for(int unit_id, int tileset);

/// Open a file whose path is UTF-8, on every platform.
///
/// Windows' narrow `fopen` takes the ANSI code page, not UTF-8, so a map named
/// in Cyrillic is unopenable through it however the bytes were obtained. The
/// caller closes it.
std::FILE* open_file(const std::string& path, const char* mode);

/// Read a whole file. Returns false if it could not be opened.
bool read_file(const std::string& path, std::vector<uint8_t>& out);

}  // namespace pf
