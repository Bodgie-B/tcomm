// Easing, sine table and small math helpers. See ease.h.
#include "ease.h"

#include <math.h>

namespace ss {

namespace {

// Sine table built at compile time: 256 entries, one full turn. Uses a float
// Taylor series on [0, pi/2] plus quadrant symmetry, so no sinf at runtime and
// no startup work. Accuracy of the table itself is about 1e-7.
constexpr float kPi = 3.14159265f;

constexpr float taylorSin(float x) {   // x in [0, pi/2]
  const float x2 = x * x;
  float term = x;
  float sum = x;
  for (int k = 1; k <= 7; ++k) {
    term = -term * x2 / (float)((2 * k) * (2 * k + 1));
    sum += term;
  }
  return sum;
}

struct SineTable {
  float v[256];
};

constexpr SineTable buildSineTable() {
  SineTable t{};
  for (int i = 0; i < 256; ++i) {
    const int q = i / 64;
    const int r = i % 64;
    const float a = (float)r * (kPi / 128.f);            // angle within quadrant
    const float c = (float)(64 - r) * (kPi / 128.f);     // complement
    float s = 0.f;
    switch (q) {
      case 0: s = taylorSin(a); break;
      case 1: s = taylorSin(c); break;
      case 2: s = -taylorSin(a); break;
      default: s = -taylorSin(c); break;
    }
    t.v[i] = s;
  }
  return t;
}

constexpr SineTable g_sine = buildSineTable();

}  // namespace

float easeInOut(float t) {
  t = clamp01(t);
  return t * t * (3.f - 2.f * t);
}

float easeInCubic(float t) {
  t = clamp01(t);
  return t * t * t;
}

float easeOutCubic(float t) {
  t = clamp01(t);
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

float easeInOutCubic(float t) {
  t = clamp01(t);
  if (t < 0.5f) return 4.f * t * t * t;
  const float u = -2.f * t + 2.f;
  return 1.f - u * u * u * 0.5f;
}

float easeOutBack(float t) {
  t = clamp01(t);
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.f;
  const float u = t - 1.f;
  return 1.f + c3 * u * u * u + c1 * u * u;
}

float easeOutElastic(float t) {
  // Endpoints are pinned: the closed form leaves a ~1e-3 residue at t = 1.
  if (t <= 0.f) return 0.f;
  if (t >= 1.f) return 1.f;
  const float c4 = 2.0943951f;   // 2*pi/3
  return powf(2.f, -10.f * t) * sinf((t * 10.f - 0.75f) * c4) + 1.f;
}

float easeOutBounce(float t) {
  t = clamp01(t);
  const float n1 = 7.5625f;
  const float d1 = 2.75f;
  if (t < 1.f / d1) return n1 * t * t;
  if (t < 2.f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  }
  if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  }
  t -= 2.625f / d1;
  return n1 * t * t + 0.984375f;
}

float window(float t, float a, float b) {
  if (b <= a) return t >= b ? 1.f : 0.f;
  return clamp01((t - a) / (b - a));
}

float sinT(float phase) {
  const float p = phase - floorf(phase);   // [0, 1], may round to exactly 1
  const float x = p * 256.f;
  const int i = (int)x;
  const float f = x - (float)i;
  const float a = g_sine.v[i & 255];
  const float b = g_sine.v[(i + 1) & 255];
  return a + (b - a) * f;
}

float cosT(float phase) { return sinT(phase + 0.25f); }

float sumSines(float x, int octaves) {
  static const float kFreq[4] = {1.f, 2.3f, 4.1f, 7.9f};      // turns per unit x
  static const float kAmp[4] = {1.f, 0.5f, 0.25f, 0.125f};
  static const float kPhase[4] = {0.f, 0.37f, 0.71f, 0.13f};  // fixed offsets, turns
  if (octaves < 1) octaves = 1;
  if (octaves > 4) octaves = 4;
  float sum = 0.f, norm = 0.f;
  for (int k = 0; k < octaves; ++k) {
    sum += kAmp[k] * sinT(x * kFreq[k] + kPhase[k]);
    norm += kAmp[k];
  }
  return sum / norm;
}

}  // namespace ss
