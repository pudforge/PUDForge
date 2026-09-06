// Finding Warcraft II.
//
// The artwork is not ours to ship, so the editor reads it out of the copy the
// user already owns. Asked once and remembered under HKCU, the same shape as
// the web client's first-run setup.
//
// Two products, and the folder the data is in is not the folder the game is
// installed in. Measured with --data against both installs on one machine:
//
//     C:\Program Files (x86)\War2Combat                      reads
//     C:\Program Files (x86)\Warcraft II Remastered          no
//     C:\Program Files (x86)\Warcraft II Remastered\x86      no
//     C:\Program Files (x86)\Warcraft II Remastered\x86\Data reads
//
// So everything here adopts a *tree*: the folder offered, then the handful of
// places inside it a copy of the data is known to sit. A person who points at
// "where Warcraft II is installed" is right either way.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "pudforge/pudforge.h"

namespace pfwin {

/// UTF-16 to UTF-8, because the ABI takes char* paths.
std::string ToUtf8(const std::wstring& text);
std::wstring FromUtf8(const std::string& text);

/// A copy of the game somebody has installed, and where it was found.
///
/// The label is what the setup dialog shows: the two products carry different
/// artwork for the same units, so which one is picked is a real choice and the
/// list has to say which is which.
struct GameInstall {
  std::wstring label;
  std::wstring path;
};

/// Every install this machine admits to, best evidence first: an INI beside the
/// exe, then what the installers wrote to the registry, then the folders they
/// use by default. Each one is verified by opening a tileset out of it, so a
/// leftover registry key for something uninstalled is not offered.
///
/// Slow enough to notice — it reads a hundred uninstall keys and opens a tileset
/// per candidate — so it runs on first setup and not on every start.
std::vector<GameInstall> FindGameInstalls();

class GameData {
 public:
  ~GameData();

  /// Whatever was remembered, then near the exe, then the usual install
  /// paths. False if none of them holds the game.
  bool Restore();
  /// Ask, with a folder picker. False when the user cancels or picks wrongly.
  /// `picked` tells the two apart, so a caller can complain about a folder with
  /// no game in it without also complaining when somebody pressed Cancel.
  bool Ask(HWND owner, bool* picked = nullptr);
  /// Take a folder the user chose and keep it for next time.
  bool Choose(const std::wstring& folder);

  bool ready() const { return source_ != nullptr; }
  const std::wstring& folder() const { return folder_; }

  /// Artwork for a tileset, or null. Caller frees.
  pf_tileset_art* OpenTileset(int tileset);
  /// Every sprite a map needs. Caller frees.
  pf_sprite_set* OpenSprites(const pf_map* map, const pf_tileset_art* art);

  /// Load whatever the map now uses that the set does not hold - placing a
  /// unit type the map never had must not draw a placeholder outline forever.
  /// @return how many sprites were added
  int AddMissingSprites(const pf_map* map, pf_sprite_set* set);

  /// The command-button icon sheet for a tileset, or null. Caller frees.
  ///
  /// `owner` only matters for the Remastered artwork, which carries its team
  /// colour in the pixels; the game's own sheet is tinted later through the
  /// palette and answers the same whoever asks.
  pf_sprite* OpenPortraits(int tileset, int owner = 0);
  /// One unit's sprite, with the forest fallback. Caller frees.
  ///
  /// Where the Remastered artwork is in use this answers from that instead,
  /// which is the single place the two kinds of art meet: everything above
  /// asks for a sprite and does not care which it got.
  /// `owner` decides the team colour when the Remastered artwork is in use;
  /// the game's own sprites are tinted later, at the palette.
  pf_sprite* OpenUnitSprite(int unit_id, int tileset, int owner = 0);

#ifdef PF_ENABLE_HD_ART
  /// Take up the Remastered artwork at this tile size, importing it the first
  /// time and reading the cache after that. `tile_px` of 0 puts it away again.
  ///
  /// False when the install has no HD artwork, when the import found nothing,
  /// or when it was turned off — in every case the classic sprites carry on,
  /// which is why nothing above this has to handle a failure.
  bool UseHdArt(int tile_px);

  /// Everything the import needs, gathered on the thread that owns the game
  /// data so the worker can touch none of it.
  struct HdRequest {
    std::wstring hd_dir;
    std::wstring cache_path;
    std::string stamp;
    int tile_px = 0;
    int icon_w = 0;
    int icon_h = 0;
  };

  /// Fill a request, or answer false when this install has no HD artwork.
  bool PrepareHdRequest(int tile_px, HdRequest& out);

  /// Do the reading and decoding. Static and takes only the request, because
  /// it runs on a worker thread: nothing it touches is shared with the one
  /// drawing. Null when there is nothing to draw from.
  static pf_hd_cache* BuildHdCache(const HdRequest& request);

  /// Take a built cache. On the thread that draws, once the worker is done.
  void AdoptHdCache(pf_hd_cache* cache, int tile_px);
  bool hd_ready() const { return hd_ready_; }
  /// Draw this tileset's terrain from the Remastered artwork, if it is in use
  /// and the cache holds that tileset. The art is left alone otherwise.
  bool ApplyHdTerrain(pf_tileset_art* art, int tileset);
  /// How long the last import took, in seconds, for the line that reports it.
  double hd_import_seconds() const { return hd_seconds_; }
#endif

  /// Whether this install has the Remastered artwork to draw from at all.
  ///
  /// The setting is offered only where something is behind it. Most installs
  /// are War2Combat with no Remastered tree beside them, and a tick that
  /// silently changes nothing is worse than no tick.
  bool hd_art_available() {
#ifdef PF_ENABLE_HD_ART
    HdRequest probe;
    return PrepareHdRequest(64, probe);
#else
    return false;
#endif
  }

  /// Whether a command icon depends on whose unit it is.
  ///
  /// Only the Remastered icons do. The game's own sheet is palette-indexed and
  /// tinted as it is drawn, so one sheet answers for every player — and this
  /// is false outright in a build without the module.
  bool icons_vary_by_owner() const {
#ifdef PF_ENABLE_HD_ART
    return hd_ready_;
#else
    return false;
#endif
  }

  /// One file out of the archives, by its path inside them. Empty when the game
  /// is not to hand or the archives do not carry it.
  ///
  /// Everything else here hands back a decoded object, because the core knows
  /// what a .grp or a tileset is. A .wav is bytes the system plays, so the raw
  /// file is the right shape.
  std::vector<uint8_t> ReadFile(const std::string& path) const;

  /// The game's own names, installed into the core so everything that asks for
  /// one gets the game's word for it — including a localised install's.
  ///
  /// Owned here and kept for the lifetime of the folder, because the core
  /// borrows it: see pf_use_strings. Called by Adopt.
  void UseGameStrings();

  /// The game's AI script table, or null when the game is not to hand. Caller
  /// frees. A PUD stores a script *number*, so this is the only place "what does
  /// that number do" can be answered from.
  pf_ai_scripts* OpenAiScripts();

  /// Point at a folder explicitly, verifying it holds the game. Public because
  /// the headless capture takes `--data` and must fail loudly on a wrong path
  /// rather than quietly drawing flat colours.
  bool Adopt(const std::wstring& folder);

  /// The folder, then the places inside it the data is known to sit. This is
  /// what a person means when they point at an installation.
  bool AdoptTree(const std::wstring& root);

 private:
  bool Search();
  void Remember() const;

#ifdef PF_ENABLE_HD_ART
  /// Where the built cache is kept, under the user's profile: it is derived
  /// from their install and belongs with their data, not ours.
  std::wstring HdCachePath(int tile_px) const;
  /// What the cache was built from, so a patched game rebuilds rather than
  /// draws last year's art. Size and write time of the largest atlas.
  std::wstring HdStamp(const std::wstring& hd_dir) const;

  /// The imported artwork, or null. Held through the ABI like everything
  /// else the core owns: the cache is C++ inside and opaque out here.
  pf_hd_cache* hd_cache_ = nullptr;
  bool hd_ready_ = false;
  int hd_tile_px_ = 0;
  double hd_seconds_ = 0;
#endif

  std::wstring folder_;
  pf_data_source* source_ = nullptr;
  /// Installed in the core, which borrows it, so this outlives every name it
  /// handed out and is replaced only when the folder changes.
  pf_strings* strings_ = nullptr;
};

}  // namespace pfwin
