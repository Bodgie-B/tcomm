// PCG32 (O'Neill), XSH-RR output, reference seeding. See rng.h.
#include "rng.h"

namespace ss {

namespace {
constexpr uint64_t kMult = 6364136223846793005ull;
}

void Rng::seed(uint64_t seed_value, uint64_t stream) {
  state = 0u;
  inc = (stream << 1u) | 1u;
  next();
  state += seed_value;
  next();
}

uint32_t Rng::next() {
  const uint64_t old = state;
  state = old * kMult + inc;
  const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
  const uint32_t rot = (uint32_t)(old >> 59u);
  return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

float Rng::uniform() {
  return (float)(next() >> 8) * (1.0f / 16777216.0f);   // 24 high bits / 2^24
}

float Rng::range(float lo, float hi) { return lo + (hi - lo) * uniform(); }

int Rng::rangeInt(int lo, int hi) {
  if (hi <= lo) return lo;
  const uint64_t span = (uint64_t)((int64_t)hi - (int64_t)lo) + 1u;
  const uint64_t r = ((uint64_t)next() * span) >> 32;   // multiply-shift, [0, span)
  return (int)((int64_t)lo + (int64_t)r);
}

bool Rng::chance(float p) { return uniform() < p; }

int Rng::weighted(const float* weights, int n) {
  if (weights == nullptr || n <= 0) return 0;
  float total = 0.f;
  for (int i = 0; i < n; ++i) {
    if (weights[i] > 0.f) total += weights[i];
  }
  if (!(total > 0.f)) return 0;
  const float r = uniform() * total;
  float acc = 0.f;
  int last = 0;
  for (int i = 0; i < n; ++i) {
    if (!(weights[i] > 0.f)) continue;
    acc += weights[i];
    last = i;
    if (r < acc) return i;
  }
  return last;   // float rounding fell off the end: last positive weight
}

}  // namespace ss
