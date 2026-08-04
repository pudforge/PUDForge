// What each terrain is called, in each tileset.
//
// The ten corner classes mean the same thing everywhere; what they look like
// does not. Winter's light ground is snow, wasteland's and swamp's is dirt, so
// calling all three "grass" would mislead about three tilesets out of four.
// Only the labels move — nothing here changes what a brush paints.
//
// Hand-written because no file in the game holds these words.

#include "../constants.hpp"
#include "../terrain.hpp"

namespace pf {
namespace {

/// Ordered by `pf_terrain`, one row per `pf_tileset`.
struct TerrainNames {
  const char* of[kTerrainCount];
};

const TerrainNames kNames[4] = {
    // Forest
    {{"Dark Water", "Water", "Dark Dirt", "Dirt", "Grass", "Dark Grass",
      "Forest", "Rock", "Human Wall", "Orc Wall", "Unknown"}},
    // Winter — the ground is snow and the shoreline freezes.
    {{"Dark Water", "Water", "Dark Ice", "Ice", "Snow", "Dark Snow",
      "Forest", "Rock", "Human Wall", "Orc Wall", "Unknown"}},
    // Wasteland — dirt where the forest has grass, mud where it has dirt.
    {{"Dark Water", "Water", "Dark Mud", "Mud", "Dirt", "Dark Dirt",
      "Forest", "Rock", "Human Wall", "Orc Wall", "Unknown"}},
    // Swamp, which reads the same way as the wasteland.
    {{"Dark Water", "Water", "Dark Mud", "Mud", "Dirt", "Dark Dirt",
      "Forest", "Rock", "Human Wall", "Orc Wall", "Unknown"}},
};

}  // namespace

const char* terrain_name(int terrain, int tileset) {
  if (terrain < 0 || terrain >= kTerrainCount) return nullptr;
  const int set = (tileset >= 0 && tileset < 4) ? tileset : 0;
  return kNames[set].of[terrain];
}

}  // namespace pf
