// Reading the Remastered edition's artwork: atlases, and frames cut out of them.
//
// The Battle.net edition keeps sprites as `.grp` files inside archives, which
// sprite.cpp reads. The Remastered edition packs the same artwork into PNG
// atlases with a TexturePacker sidecar naming every frame's rectangle, so the
// path is different in shape: parse the sidecar, decode the sheet once, and
// cut out the handful of frames an editor actually draws.
//
// What makes that worth doing rather than alarming is the count. The atlases
// hold about thirty thousand frames between them, because the game animates
// walking, attacking and dying; an editor draws idle poses and at most five
// facings, which is 206 frames in all. See docs/hd_art.md for the measurements.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pf {

/// One frame's place in an atlas.
///
/// TexturePacker trims transparent margins away and records what it took, so a
/// frame is a small rectangle plus where it sits on the larger canvas the
/// artist drew. Both are needed: sprites line up by their source box, not by
/// the trimmed pixels.
struct HdFrame {
  std::string key;          ///< "grunt_0" — the .grp stem, then the frame
  int x = 0, y = 0;         ///< the trimmed rectangle in the atlas
  int w = 0, h = 0;
  int offset_x = 0;         ///< where that rectangle sits on the source canvas
  int offset_y = 0;
  int source_w = 0;         ///< the untrimmed canvas the game drew
  int source_h = 0;
  bool rotated = false;     ///< packed turned a quarter turn
};

/// An atlas sidecar: every frame in one sheet, and the sheet's own size.
struct HdAtlas {
  int width = 0;
  int height = 0;
  std::vector<HdFrame> frames;

  /// The frames of one sprite, in frame order — "grunt" gives grunt_0 upward.
  /// Empty when the atlas does not hold that sprite.
  std::vector<const HdFrame*> sprite(const std::string& stem) const;
};

/// Parse a TexturePacker sidecar. False when it is not one, or names nothing.
///
/// Deliberately narrow: it reads the shape TexturePacker writes rather than
/// being a JSON library, because a general parser is a great deal of code to
/// carry for six field names that a tool emits the same way every time.
bool parse_hd_atlas(const char* json, size_t length, HdAtlas& out);

/// How many HD pixels the artwork draws a tile in.
///
/// Measured rather than declared: a terrain frame is 216 px against the
/// classic 32, and the unit sprites agree — the drawn content of eight
/// sprites, classic against HD, averages 6.88x where 216/32 is 6.75.
///
/// It has to be measured this way because the *canvases* do not agree. A
/// Footman is 72 px of classic canvas and 608 of HD, which is 9.9x, and
/// scaling by that lands the unit two thirds of the size it should be. The
/// extra is padding the atlas carries and the .grp does not.
constexpr int kHdPixelsPerTile = 216;

/// Cut a frame out of a decoded atlas, put it back where it was trimmed from,
/// and scale it for a canvas drawing tiles at `tile_px`.
///
/// The scale is `tile_px / kHdPixelsPerTile`, so a unit comes out the size the
/// game draws it beside terrain at the same setting — and every frame of every
/// sprite shares one scale, so they line up with each other and with the map.
///
/// Scaling happens in premultiplied alpha and comes back straight, because
/// averaging colour against transparent pixels without it darkens every soft
/// edge — the fringe that scripts/make-ui-icons.ps1 warns about from the other
/// direction.
std::vector<uint32_t> cut_hd_frame(const uint32_t* atlas, int atlas_w, int atlas_h,
                                   const HdFrame& frame, int tile_px, int* out_w,
                                   int* out_h);

// ------------------------------------------------------------------ terrain

/// The megatiles the HD terrain atlas leaves out.
///
/// Every tileset starts with 16 blank megatiles and every HD terrain atlas
/// holds exactly 16 fewer frames than its tileset has megatiles — forest 356
/// against 372, winter 363 against 379, wasteland 357 against 373, swamp 368
/// against 384. Blizzard packed the drawn ones and skipped the blanks.
constexpr int kHdTerrainSkipped = 16;

/// The atlas frame index for a megatile, or -1 when the atlas has none —
/// which is the answer for the blanks at the front, and for anything past the
/// end of a tileset the artwork does not cover.
int hd_terrain_frame(int megatile, int frames_in_atlas);

/// Cut a frame and fit it inside `box_w` by `box_h`, keeping its shape.
///
/// For artwork that is not laid out in map tiles — the command-button icons,
/// which are a fixed size on a panel rather than a size on the ground, so
/// `kHdPixelsPerTile` says nothing useful about them.
std::vector<uint32_t> cut_hd_frame_fit(const uint32_t* atlas, int atlas_w, int atlas_h,
                                       const HdFrame& frame, int box_w, int box_h,
                                       int* out_w, int* out_h);

// ------------------------------------------------------------- the import
//
// The core decides *what* is wanted and what the cache looks like; a host
// walks the directory and does the reading and writing, because nothing on
// the ABI side of this touches a filesystem.

/// One sprite the editor draws, and how much of it.
struct HdWanted {
  int unit_id = 0;
  std::string race;    ///< "human", "orc", "neutral" — the atlas to prefer
  std::string stem;    ///< "grunt", the .grp name and the atlas frame stem
  int frames = 1;      ///< facings, from pf_unit_facing_count
};

/// Every sprite worth importing, once per distinct sprite rather than once
/// per unit: a Footman and an Attack Peasant share `human/peon`, and the
/// cache would otherwise hold it twice.
///
/// 110 unit types come to 78 distinct sprites and 206 frames, which is the
/// whole working set — see docs/hd_art.md.
std::vector<HdWanted> hd_wanted_sprites();

/// One imported sprite: its frames, all the same size, back to back.
///
/// Named by the sprite it came from rather than by a unit, because several
/// units draw one sprite — a Footman and an Attack Peasant are both
/// `human/peon`. Keying this by unit id would either hold the pixels twice or
/// answer for only one of them.
struct HdCachedSprite {
  std::string race;               ///< "human", "orc", "neutral"
  std::string stem;               ///< "peon"
  int width = 0;
  int height = 0;
  int frames = 0;
  std::vector<uint32_t> pixels;   ///< frames * width * height, RGBA
  /// Where the owner's colour goes, and how strongly: the artwork renders
  /// every unit in one player's colours and carries this beside it. White is
  /// the full colour, darker is the same colour shaded, transparent is a
  /// pixel the owner does not touch. Empty when the sheet had no mask, and
  /// then the sprite draws in whatever colour it was drawn in.
  std::vector<uint32_t> mask;     ///< same shape as `pixels`, or empty
};

/// One tileset's megatiles, square and all the same size.
///
/// Indexed by megatile, not by atlas frame: the atlas omits the blanks and
/// the map does not, so the offset is undone once here rather than at every
/// call. A blank megatile is present and fully transparent.
struct HdCachedTiles {
  int tileset = -1;               ///< pf_tileset
  int size = 0;                   ///< pixels a side
  int count = 0;                  ///< megatiles, blanks included
  std::vector<uint32_t> pixels;   ///< count * size * size, RGBA

  /// One megatile's pixels, or null when it is not in here.
  const uint32_t* tile(int megatile) const;
};

/// The whole cache, as it sits in memory and on disk.
struct HdCache {
  int tile_px = 0;
  std::string stamp;              ///< what it was built from; a game build
  std::vector<HdCachedSprite> sprites;
  std::vector<HdCachedTiles> tilesets;

  /// The tiles for a tileset, or null.
  const HdCachedTiles* tiles(int tileset) const;

  /// The sprite a unit draws, or null. Goes through the same table the
  /// classic path does, so both agree about which units share artwork.
  const HdCachedSprite* find(int unit_id) const;
};

/// Pack a cache into bytes a host can write, and read it back.
///
/// Deliberately not PNG: this is derived data read once at startup, so the
/// cost that matters is decode time rather than disk, and raw is instant.
/// `read_hd_cache` answers false on anything it does not recognise, including
/// a cache built at another tile size or from another game build — the caller
/// passes what it expects and gets a rebuild rather than last year's art.
std::vector<uint8_t> write_hd_cache(const HdCache& cache);
bool read_hd_cache(const uint8_t* bytes, size_t length, int want_tile_px,
                   const std::string& want_stamp, HdCache& out);

}  // namespace pf
