// The game's own voices, played back over an edit.
//
// Placing a Knight and hearing "Your command?" is not information — the status
// bar already said what landed — it is the editor sounding like the game the map
// is for. So it is off unless asked for, and nothing depends on it happening.
//
// The core resolves which file a unit is heard in (pf_unit_sound_path); this
// reads that file out of the archives and hands it to the system. Warcraft II's
// sounds are plain RIFF WAVE, which PlaySound takes as bytes, so the whole
// player is a cache and a call.

#pragma once

#include <windows.h>

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "pudforge/pudforge.h"

namespace pfwin {

class GameData;

class SoundPlayer {
 public:
  ~SoundPlayer();

  /// Where to read the sounds from. Borrowed, and may be null — a client with
  /// no game folder simply stays quiet.
  void Open(GameData* game);
  /// The game folder changed, so every cached clip came from the old one.
  void Close();

  bool enabled() const { return enabled_; }
  void SetEnabled(bool on);

  /// Play what `unit` is heard doing.
  ///
  /// Which of a unit's several selection lines is drawn fresh each call, so a
  /// footman clicked twice answers twice differently — the reason the game gave
  /// him six of them.
  void Play(int unit, pf_sound_kind kind);

 private:
  /// The bytes of one clip, or empty when the archives do not hold it. Cached
  /// either way: a unit whose sound is missing must not send the client back
  /// into the MPQ on every click.
  const std::vector<uint8_t>& Clip(const std::string& path);

  GameData* game_ = nullptr;
  bool enabled_ = false;
  /// Which line a unit answers with. Seeded from the clock: two runs saying the
  /// same things in the same order would be a tell.
  std::mt19937 rng_{std::random_device{}()};
  /// Wider than any folder is deep, so the core's modulo does the choosing and
  /// this never has to know how many lines a unit has.
  std::uniform_int_distribution<int> pick_{0, 1 << 20};
  std::unordered_map<std::string, std::vector<uint8_t>> clips_;
  /// The clip currently handed to PlaySound. It plays asynchronously straight
  /// out of this buffer, so the buffer has to outlive the call — and the cache
  /// alone does not guarantee that, since Close() can empty it mid-sound.
  std::vector<uint8_t> playing_;
};

}  // namespace pfwin
