// Seeded random source (PCG32). One instance per loop so a seed replays a loop.
#pragma once

#include "../ss_platform.h"

namespace ss {

struct Rng {
  uint64_t state;
  uint64_t inc;

  void seed(uint64_t seed, uint64_t stream = 0x9E3779B97F4A7C15ull);
  uint32_t next();                       // 32 random bits
  float uniform();                       // [0, 1)
  float range(float lo, float hi);       // [lo, hi)
  int rangeInt(int lo, int hi);          // [lo, hi] inclusive
  bool chance(float p);                  // true with probability p

  // Weighted pick over `n` weights (any non-negative floats). Returns index.
  // All-zero weights return 0.
  int weighted(const float* weights, int n);
};

}  // namespace ss
