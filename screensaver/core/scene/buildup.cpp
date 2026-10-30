// The build-up beat. See buildup.h.
//
// One beat with six sequential phases. Each phase owns a set of layers; the
// per-element easing (stagger, overshoot) is computed from the phase progress
// by the functions below, which the landscape and ambient drawers call.
#include "buildup.h"

#include "../anim/ease.h"
#include "world.h"

namespace ss {

namespace {

// Phase start times in seconds; the last entry is the beat's end.
const float kPhaseStart[BP_DONE + 1] = {0.0f, 1.8f, 3.8f, 6.6f, 8.8f, 11.6f, SS_BUILD_SECONDS};

const uint16_t kPhaseLayers[BP_DONE + 1] = {
    BL_BIT(BL_SKY),
    BL_BIT(BL_RIDGES),
    (uint16_t)(BL_BIT(BL_FAR_TREES) | BL_BIT(BL_MID_PINES) | BL_BIT(BL_CAMP_PINE) | BL_BIT(BL_FG_PINES)),
    BL_BIT(BL_STREAM),
    (uint16_t)(BL_BIT(BL_ROCKS) | BL_BIT(BL_TUFTS) | BL_BIT(BL_MUSHROOM)),
    0,   // fire: the pit is a world element, not a cache layer
    0,
};

// Rows each phase's active layers can reach (overshoot included).
const int16_t kPhaseRows[BP_DONE + 1][2] = {
    {0, 318},     // sky and the clouds fading with it
    {150, 318},   // ridges rise behind the ground line
    {140, 480},   // trees, the near pines reach y ~150 at full overshoot
    {330, 480},   // stream and its far bank
    {270, 480},   // rocks, tufts, mushroom
    {0, 0},       // the fire pit marks its own bounds
    {0, 0},
};

uint16_t cachedFor(uint16_t built, uint16_t active) {
  if (active == 0) return built;
  int f = 0;
  while (!(active & BL_BIT(f))) ++f;
  return (uint16_t)(built & (BL_BIT(f) - 1u));
}

// Stable 0..1 value per position, so a plant pops at the same moment whether
// it is drawn into the cache or by the ambient layer.
float hash01(float x, float y) {
  uint32_t h = (uint32_t)(int32_t)x * 73856093u ^ (uint32_t)(int32_t)y * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return (float)(h & 0xFFFFu) / 65536.f;
}

// -1 before the phase, 0..1 within it, 2 after.
float phaseU(const BuildState& b, int phase) {
  if (b.phase < phase) return -1.f;
  if (b.phase > phase) return 2.f;
  return b.u;
}

void beatUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  buildSetProgress(w.build, t);
  // The ring of stones is laid; the hiker lights it in the fire beat.
  w.fire.stones = window(buildFireT(w.build), 0.0f, 0.7f);
}

// Leaving the beat early (a jump) must still finish the build.
void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  buildSetProgress(w.build, 1.f);
  w.fire.stones = 1.f;
}

}  // namespace

const Beat kBuildBeat = {"build", SS_BUILD_SECONDS, nullptr, beatUpdate, beatEnd};

void buildReset(BuildState& b) { buildSetProgress(b, 0.f); }

void buildSetProgress(BuildState& b, float t) {
  const float sec = clamp01(t) * SS_BUILD_SECONDS;
  int p = 0;
  while (p < BP_DONE && sec >= kPhaseStart[p + 1]) ++p;
  b.phase = (uint8_t)p;
  uint16_t built = BL_BIT(BL_GROUND);
  for (int i = 0; i < p; ++i) built = (uint16_t)(built | kPhaseLayers[i]);
  b.built = built;
  b.active = kPhaseLayers[p];
  b.cached = cachedFor(b.built, b.active);
  b.u = p < BP_DONE ? window(sec, kPhaseStart[p], kPhaseStart[p + 1]) : 1.f;
}

float buildSkyFade(const BuildState& b) {
  const float u = phaseU(b, BP_SKY);
  return easeInOut(window(u, 0.12f, 1.0f));   // a short hold on the flat color first
}

float buildRidgeDrop(const BuildState& b, int ridge) {
  const float u = phaseU(b, BP_RIDGES);
  const float a = 0.14f * (float)ridge;   // far ridge leads, near ridge follows
  return 1.f - easeOutCubic(window(u, a, a + 0.72f));
}

float buildTreeScale(const BuildState& b, float x) {
  const float u = phaseU(b, BP_TREES);
  const float d = 0.5f * clamp01(x / (float)SS_WIDTH);   // left to right
  return easeOutBack(window(u, d, d + 0.3f));
}

float buildNearTreeScale(const BuildState& b, int which) {
  // After the background has filled in: the tall foreground pine, the camp
  // pine, then the short foreground pine.
  static const float kStart[3] = {0.60f, 0.66f, 0.56f};
  const float u = phaseU(b, BP_TREES);
  const int k = which < 0 ? 0 : (which > 2 ? 2 : which);
  return easeOutBack(window(u, kStart[k], kStart[k] + 0.32f));
}

float buildStreamReveal(const BuildState& b) {
  return easeInOut(window(phaseU(b, BP_STREAM), 0.0f, 0.94f));
}

float buildPlantScale(const BuildState& b, float x, float y) {
  const float u = phaseU(b, BP_PLANTS);
  const float d = 0.62f * hash01(x, y);   // popcorn order, done by u = 0.74
  return easeOutBack(window(u, d, d + 0.12f));
}

float buildMushroomT(const BuildState& b) {
  // After a short stillness once the last plant has landed.
  return window(phaseU(b, BP_PLANTS), 0.80f, 1.0f);
}

float buildFireT(const BuildState& b) { return clamp01(phaseU(b, BP_FIRE)); }

void buildPhaseRows(const BuildState& b, int& y0, int& y1) {
  y0 = kPhaseRows[b.phase][0];
  y1 = kPhaseRows[b.phase][1];
}

}  // namespace ss
