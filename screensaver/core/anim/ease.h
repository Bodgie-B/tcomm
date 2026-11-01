// Easing, sine table and small math helpers. All single precision.
#pragma once

#include "../ss_platform.h"

namespace ss {

inline float clamp01(float t) { return t < 0.f ? 0.f : (t > 1.f ? 1.f : t); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Standard easings; t in 0..1, return in 0..1 (back/elastic overshoot briefly).
float easeInOut(float t);        // smoothstep
float easeInCubic(float t);
float easeOutCubic(float t);
float easeInOutCubic(float t);
float easeOutBack(float t);      // overshoot then settle (pops, landings)
float easeOutElastic(float t);   // cheap approximation, one or two wobbles
float easeOutBounce(float t);

// Map t in [a, b] to 0..1, clamped. Handy for sub-beats inside a beat.
float window(float t, float a, float b);

// Sine from a 256-entry table. `phase` in turns (0..1 = one full cycle).
// Accurate to about 1e-3, far cheaper than sinf on the S3.
float sinT(float phase);
float cosT(float phase);

// Sum-of-sines noise for grass, water, fire and breathing. `x` is position or
// time; `octaves` 1..4. Returns roughly -1..1. Deterministic.
float sumSines(float x, int octaves);

}  // namespace ss
