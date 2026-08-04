#include "Sounds.hpp"

#include <mmsystem.h>

#include "GameData.hpp"

namespace pfwin {

SoundPlayer::~SoundPlayer() { Close(); }

void SoundPlayer::Open(GameData* game) {
  Close();
  game_ = game;
}

void SoundPlayer::Close() {
  // Stop before the buffer goes: PlaySound is playing straight out of
  // `playing_`, and freeing it underneath the mixer is a crash that happens
  // once in fifty runs and never while anybody is watching.
  PlaySoundW(nullptr, nullptr, SND_PURGE);
  playing_.clear();
  clips_.clear();
  game_ = nullptr;
}

void SoundPlayer::SetEnabled(bool on) {
  enabled_ = on;
  if (!on) {
    PlaySoundW(nullptr, nullptr, SND_PURGE);
    playing_.clear();
  }
}

const std::vector<uint8_t>& SoundPlayer::Clip(const std::string& path) {
  auto found = clips_.find(path);
  if (found != clips_.end()) return found->second;
  std::vector<uint8_t> bytes = game_ ? game_->ReadFile(path) : std::vector<uint8_t>();
  return clips_.emplace(path, std::move(bytes)).first->second;
}

void SoundPlayer::Play(int unit, pf_sound_kind kind) {
  if (!enabled_ || !game_ || unit < 0) return;

  // A fresh number per click rather than one derived from the unit, so a
  // footman clicked twice answers twice differently — which is what the game
  // does, and the reason it has six lines for him rather than one. The core
  // stays a pure function of the number it is handed; the randomness is
  // policy, and policy is the client's.
  const int salt = int(pick_(rng_));
  char path[128] = {};
  if (pf_unit_sound_path(unit, int(kind), salt, path, int(sizeof(path))) <= 0) {
    return;   // this unit is not heard doing this
  }
  const std::vector<uint8_t>& clip = Clip(path);
  if (clip.empty()) return;   // not in the archives this install carries

  // Copied out of the cache rather than played from it. The cache can be
  // emptied — a new game folder, the option going off — while a clip is still
  // sounding, and SND_ASYNC means the pointer outlives this call.
  playing_ = clip;
  // SND_NOSTOP is deliberately absent: placing a row of footmen should sound
  // like the last one placed, not like the first one still finishing.
  PlaySoundW(reinterpret_cast<LPCWSTR>(playing_.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

}  // namespace pfwin
