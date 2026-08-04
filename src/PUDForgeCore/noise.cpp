#include "noise.hpp"

#include <cmath>

namespace pf {
namespace {

/// The twelve gradient directions of the 2D case, as x,y pairs.
constexpr float kGrad[8][2] = {
    {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
    {1, 0}, {-1, 0}, {0, 1},  {0, -1},
};

// Skew factors between the square lattice the input lives on and the simplex
// lattice the noise is evaluated on.
const float kF2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
const float kG2 = (3.0f - std::sqrt(3.0f)) / 6.0f;

uint32_t next(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

}  // namespace

Noise::Noise(uint32_t seed) {
  uint8_t p[256];
  for (int i = 0; i < 256; i++) p[i] = uint8_t(i);

  // Fisher-Yates from the seed, so two seeds give unrelated permutations
  // rather than two shifts of the same one.
  uint32_t state = seed ? seed : 1u;
  for (int i = 255; i > 0; i--) {
    const int j = int(next(state) % uint32_t(i + 1));
    const uint8_t tmp = p[i];
    p[i] = p[j];
    p[j] = tmp;
  }
  for (int i = 0; i < 512; i++) perm_[i] = p[i & 255];
}

float Noise::at(float x, float y) const {
  // Which simplex cell the point is in.
  const float s = (x + y) * kF2;
  const int i = int(std::floor(x + s));
  const int j = int(std::floor(y + s));
  const float t = float(i + j) * kG2;
  const float x0 = x - (float(i) - t);
  const float y0 = y - (float(j) - t);

  // A square splits into two triangles; this is the one holding the point.
  const int i1 = x0 > y0 ? 1 : 0;
  const int j1 = x0 > y0 ? 0 : 1;

  const float x1 = x0 - float(i1) + kG2;
  const float y1 = y0 - float(j1) + kG2;
  const float x2 = x0 - 1.0f + 2.0f * kG2;
  const float y2 = y0 - 1.0f + 2.0f * kG2;

  const int ii = i & 255;
  const int jj = j & 255;
  const int g0 = perm_[ii + perm_[jj]] & 7;
  const int g1 = perm_[ii + i1 + perm_[jj + j1]] & 7;
  const int g2 = perm_[ii + 1 + perm_[jj + 1]] & 7;

  auto corner = [](float dx, float dy, int g) {
    float t0 = 0.5f - dx * dx - dy * dy;
    if (t0 < 0.0f) return 0.0f;
    t0 *= t0;
    return t0 * t0 * (kGrad[g][0] * dx + kGrad[g][1] * dy);
  };

  // 70 scales the sum of the three corners into roughly [-1, 1].
  return 70.0f * (corner(x0, y0, g0) + corner(x1, y1, g1) + corner(x2, y2, g2));
}

LayeredNoise::LayeredNoise(const NoiseLayer* layers, int count) {
  for (int i = 0; i < count && layers; i++) {
    if (layers[i].weight == 0.0f) continue;
    layers_.push_back(layers[i]);
    noises_.emplace_back(layers[i].seed);
    total_ += std::fabs(layers[i].weight);
  }
}

float LayeredNoise::at(float x, float y) const {
  if (total_ == 0.0f) return 0.5f;
  float sum = 0.0f;
  for (size_t i = 0; i < layers_.size(); i++) {
    sum += noises_[i].at(x * layers_[i].scale, y * layers_[i].scale) * layers_[i].weight;
  }
  return 0.5f + 0.5f * (sum / total_);
}

}  // namespace pf
