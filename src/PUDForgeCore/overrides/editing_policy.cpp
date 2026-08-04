// Settings that decide how editing feels.
//
// Every number here was measured against the shipped maps or tuned against
// them, and each lived in a client until a second client made that untenable.
// None needs a window to describe, so none is presentation.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "../constants.hpp"
#include "../terrain.hpp"
// For PF_BRUSH_SIZE_CORNER: the ladder below is what the ABI hands out, so the
// rung and the constant naming it have to be the same number.
#include "pudforge/pudforge.h"

namespace pf {
namespace {

/**
 * The zoom ladder, ascending.
 *
 * A list rather than a step size: 25% to 33% is a third as much map again,
 * while 200% to 210% is barely visible, so even steps in percent are uneven
 * steps in what you see. 66 and 33 rather than 67 and 34 because the tile size
 * rounds either way. The rungs between the doublings keep no step worth more
 * than about a third, which is what makes holding the wheel down feel like
 * moving rather than snapping.
 */
constexpr int kZoomLevels[] = {25,  33,  40,  50,  66,  75,  100, 125, 150,
                               175, 200, 250, 300, 400, 500, 600, 800};
constexpr int kZoomLevelCount = int(sizeof(kZoomLevels) / sizeof(kZoomLevels[0]));

/**
 * The brush ladder, ascending.
 *
 * All odd from 1 up, so a brush has a centre tile to aim with. The rung below
 * them is PF_BRUSH_SIZE_CORNER: one corner of the grid rather than a whole
 * tile, which is a quarter of what size 1 lays and the smallest mark the
 * terrain model can hold. It is on the ladder rather than off in a mode of its
 * own because it is what someone reaches for when 1 is still too big.
 */
constexpr int kBrushSizes[] = {PF_BRUSH_SIZE_CORNER, 1,  3,  5,  7,  9,
                               11, 13, 15, 17, 19, 21};

/**
 * The spray can. About eleven puffs a second, ramping over 1.2 seconds from a
 * floor low enough that a tap marks the map rather than covering it.
 */
constexpr int kSprayTickMs = 90;
constexpr int kSprayRampMs = 1200;
constexpr double kSprayFloor = 0.05;

/// What a scatter brush covers at full strength.
constexpr double kScatterDensity = 0.35;

/**
 * The terrain brushes, in palette order.
 *
 * Light and dark are separate brushes rather than one brush and a toggle:
 * PUDDraft had the toggle, and listing them is the same set with less state. A
 * brush is only a terrain class — what it is called depends on the tileset.
 */
struct Brush {
  uint8_t terrain;
  int shade;   ///< 1 light, -1 dark, 0 for a class with one shade
};

constexpr Brush kBrushes[] = {
    {kWaterDark, -1},   {kWaterLight, 1},
    {kCoastLight, 1},   {kCoastDark, -1},
    {kGroundLight, 1},  {kGroundDark, -1},
    {kForest, 0},       {kMountain, 0},
    {kWallHuman, 0},    {kWallOrc, 0},
};

}  // namespace

int zoom_level_count() { return kZoomLevelCount; }

int zoom_level(int index) {
  if (index < 0) return kZoomLevels[0];
  if (index >= kZoomLevelCount) return kZoomLevels[kZoomLevelCount - 1];
  return kZoomLevels[index];
}

int zoom_min() { return kZoomLevels[0]; }
int zoom_max() { return kZoomLevels[kZoomLevelCount - 1]; }

int zoom_snap(int zoom) {
  // The nearest rung, so a value arrived at by any route lands on one rather
  // than between two. Ties go down: a fit that computed 41% wants 33% and the
  // whole map, not 50% and a crop.
  int best = kZoomLevels[0];
  int best_gap = zoom > best ? zoom - best : best - zoom;
  for (int i = 1; i < kZoomLevelCount; i++) {
    const int gap = zoom > kZoomLevels[i] ? zoom - kZoomLevels[i] : kZoomLevels[i] - zoom;
    if (gap < best_gap) { best = kZoomLevels[i]; best_gap = gap; }
  }
  return best;
}

int zoom_step(int zoom, int dir) {
  // The next rung past where the zoom is, rather than the nearest rung stepped
  // from: those differ when the zoom is already between two, and taking the
  // second makes one press do nothing.
  if (dir > 0) {
    for (int i = 0; i < kZoomLevelCount; i++) {
      if (kZoomLevels[i] > zoom) return kZoomLevels[i];
    }
    return zoom_max();
  }
  for (int i = kZoomLevelCount - 1; i >= 0; i--) {
    if (kZoomLevels[i] < zoom) return kZoomLevels[i];
  }
  // Below the ladder's floor already, as a fit of a 128x128 map in a short
  // window can be. Stepping out stays put rather than stepping *in*.
  return zoom < zoom_min() ? zoom : zoom_min();
}

int brush_size_count() { return int(sizeof(kBrushSizes) / sizeof(kBrushSizes[0])); }

int brush_size(int index) {
  if (index < 0 || index >= brush_size_count()) return 1;
  return kBrushSizes[index];
}

int spray_tick_ms() { return kSprayTickMs; }
int spray_ramp_ms() { return kSprayRampMs; }

double spray_density(int held_ms, double full) {
  const double held = held_ms < 0 ? 0.0 : double(held_ms);
  const double gain = held >= double(kSprayRampMs) ? 1.0 : held / double(kSprayRampMs);
  // A can does not keep getting heavier forever: the ramp tops out at whatever
  // the scatter density is set to.
  return kSprayFloor + (full - kSprayFloor) * gain;
}

double scatter_density() { return kScatterDensity; }

int brush_count() { return int(sizeof(kBrushes) / sizeof(kBrushes[0])); }

int brush_terrain(int index) {
  if (index < 0 || index >= brush_count()) return -1;
  return kBrushes[index].terrain;
}

int brush_shade(int index) {
  if (index < 0 || index >= brush_count()) return 0;
  return kBrushes[index].shade;
}

}  // namespace pf

// --------------------------------------------------------- name matching

namespace pf {
namespace {

bool word_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0;
}

std::string trimmed_lower(const char* text) {
  std::string out = text ? text : "";
  size_t begin = 0, end = out.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(out[begin]))) begin++;
  while (end > begin && std::isspace(static_cast<unsigned char>(out[end - 1]))) end--;
  out = out.substr(begin, end - begin);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

int name_score(const char* query, const char* name) {
  const std::string q = trimmed_lower(query);
  if (q.empty()) return 0;
  const std::string lower = trimmed_lower(name);

  int score = 0;
  size_t at = 0;
  long previous = -2;
  for (char ch : q) {
    const size_t found = lower.find(ch, at);
    if (found == std::string::npos) return -1;

    // Initials are how people abbreviate — "dk" for Death Knight — so a letter
    // beginning a word is worth far more than one buried inside it.
    if (found == 0) score += 12;
    else if (!word_char(lower[found - 1])) score += 10;
    else if (long(found) == previous + 1) score += 6;
    else score += 1;

    // Mild pressure towards the front of the name, bounded so that a long name
    // is not ruled out by its length alone.
    score -= int(std::min<size_t>(found - at, 4));

    previous = long(found);
    at = found + 1;
  }
  return score;
}

/// What a resource should hold when the editor places one.
///
/// A gold mine dropped with nothing in it is scenery that does nothing. The
/// numbers are the shipped maps' own habit: 40,000 gold and 20,000 oil are far
/// and away the commonest, stored in units of 2,500, so 16 and 8.
int unit_default_value(int unit_id, int resource) {
  if (resource == 1) return 16;   // gold, 40,000
  if (resource == 2) return 8;    // oil, 20,000
  (void)unit_id;
  return 0;
}

}  // namespace pf
