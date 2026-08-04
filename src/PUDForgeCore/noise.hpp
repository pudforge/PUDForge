// Gradient noise for map generation.
//
// Ken Perlin's 2D simplex noise, on the simplex grid rather than the square
// one, which is what gives it no directional bias. Not OpenSimplex2 — that
// dodges a patent on the 3D and higher cases, which does not apply here.
//
// Deterministic from a seed, because a generated map is only worth anything if
// it can be reproduced.

#pragma once

#include <cstdint>
#include <vector>

namespace pf {

/// One octave: a frequency, a seed, and how much it contributes.
struct NoiseLayer {
  float scale;    ///< features per tile; smaller is broader
  uint32_t seed;
  float weight;
};

/// 2D simplex noise in [-1, 1], from a seeded permutation.
class Noise {
 public:
  explicit Noise(uint32_t seed);
  float at(float x, float y) const;

 private:
  uint8_t perm_[512];
};

/**
 * Several octaves summed, normalised into [0, 1].
 *
 * Layers are independent rather than a fixed lacunarity ladder, so a caller can
 * mix a broad continent with fine coastline detail at whatever ratio it likes.
 */
class LayeredNoise {
 public:
  LayeredNoise(const NoiseLayer* layers, int count);
  float at(float x, float y) const;

 private:
  std::vector<NoiseLayer> layers_;
  std::vector<Noise> noises_;
  float total_ = 0.0f;
};

}  // namespace pf
