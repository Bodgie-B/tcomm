// Palette system: key palettes per mood, mood interpolation, weather overlays.
//
// The key colors live in the readable tables below (one block per mood, slots in
// enum order). Tune them by eye there. Everything else in this file is plumbing.
#include "palette.h"
#include "anim/ease.h"

#include <math.h>

namespace ss {

namespace {

// ---------------------------------------------------------------------------
// Key colors, 0xRRGGBB. Slot order matches enum Slot in palette.h.
// Rules every block follows (checked by tests/test_palette.cpp):
//   - Atmospheric perspective: RIDGE_FAR nearest SKY4 in luminance, RIDGE_MID
//     darker, RIDGE_NEAR darker still; TREES_FAR between MID and NEAR;
//     TREES_MID and the near-tree slots darker than RIDGE_NEAR.
//   - Night stays dim: nothing above ~60% luminance except FIRE_CORE, STAR,
//     TENT_LIT and SUN (the moon).
// All values are unverified on the real panel (IPS + RGB565 shift colors).
// ---------------------------------------------------------------------------

// NIGHT: brief section 2 for sky ends, far ridge, mid trees, near ground, fire,
// star. SKY3 interpolated between the horizon pair. Everything else invented.
constexpr uint32_t KEY_NIGHT[] = {
  /* SKY0..SKY4        */ 0x0E1A2E, 0x13233B, 0x1A2D47, 0x1E3250, 0x22385A,
  /* SUN (moon)        */ 0xD8E0EE,
  /* CLOUD_LIT, SHADE  */ 0x1D304C, 0x15263F,
  /* RIDGE F, M, N     */ 0x1B2A40, 0x16253B, 0x132036,
  /* TREES_FAR, MID    */ 0x142138, 0x121D2E,
  /* GROUND, DARK      */ 0x0F1A28, 0x0B1420,
  /* GRASS             */ 0x142332,
  /* WATER, LIGHT      */ 0x2A4A74, 0x5A7AA0,
  /* ROCK              */ 0x1C2838,
  /* TRUNK, FOL, FOLD  */ 0x0E1520, 0x0F1C2A, 0x0A131E,
  /* TENT, TENT_LIT    */ 0x8A4A2A, 0xF0C070,
  /* FIRE, CORE, SMOKE */ 0xE07B39, 0xFFE3A0, 0x2E4058,
  /* ACTOR, LIGHT      */ 0x05080F, 0x0C121C,
  /* STAR              */ 0xFFF1C2,
  /* CHIP_OUTLINE      */ 0x3A5478,
};

// DAWN: invented. Cool pale violet-gray sky to a peach horizon, muted blue-gray
// ridges, low contrast, soft fog feel.
constexpr uint32_t KEY_DAWN[] = {
  /* SKY0..SKY4        */ 0x7F86A8, 0x9A9BB8, 0xB8AAC0, 0xD6BCC0, 0xEDCDB8,
  /* SUN               */ 0xF6C9A2,
  /* CLOUD_LIT, SHADE  */ 0xC8B4C0, 0xA6A0BC,
  /* RIDGE F, M, N     */ 0xB9B0BF, 0xA09AB0, 0x8884A0,
  /* TREES_FAR, MID    */ 0x9490A8, 0x6F6C8A,
  /* GROUND, DARK      */ 0x50566A, 0x3F4558,
  /* GRASS             */ 0x5F6E74,
  /* WATER, LIGHT      */ 0xF2CFC0, 0xFFE6D8,
  /* ROCK              */ 0x7A7A90,
  /* TRUNK, FOL, FOLD  */ 0x3A3A4C, 0x4E5870, 0x3A4258,
  /* TENT, TENT_LIT    */ 0xC47E50, 0xF3D08A,
  /* FIRE, CORE, SMOKE */ 0xE07B39, 0xFFE6A8, 0xA8A4B8,
  /* ACTOR, LIGHT      */ 0x1E2230, 0x303648,
  /* STAR              */ 0xFFF1C2,
  /* CHIP_OUTLINE      */ 0x8F8AAA,
};

// MORNING: invented. Clear, a little cooler and paler than Day, warm low sun.
constexpr uint32_t KEY_MORNING[] = {
  /* SKY0..SKY4        */ 0x6B9BB8, 0x86B0C6, 0xA4C5D0, 0xBDD5D6, 0xD5E2DB,
  /* SUN               */ 0xF2CF8A,
  /* CLOUD_LIT, SHADE  */ 0xB2CDD2, 0x90B6C8,
  /* RIDGE F, M, N     */ 0x8DB0B4, 0x78A0A2, 0x5A8880,
  /* TREES_FAR, MID    */ 0x6B9490, 0x466F62,
  /* GROUND, DARK      */ 0x3C5E4A, 0x2F4E40,
  /* GRASS             */ 0x5E8C62,
  /* WATER, LIGHT      */ 0xA7D6DD, 0xE6F4F0,
  /* ROCK              */ 0x6C7C7A,
  /* TRUNK, FOL, FOLD  */ 0x40322A, 0x365A4B, 0x284838,
  /* TENT, TENT_LIT    */ 0xCC7440, 0xF2D286,
  /* FIRE, CORE, SMOKE */ 0xE07B39, 0xFFE8A8, 0xA0B4B6,
  /* ACTOR, LIGHT      */ 0x15251E, 0x24392E,
  /* STAR              */ 0xFFF1C2,
  /* CHIP_OUTLINE      */ 0x6FA0B4,
};

// DAY: brief section 2 for sky (SKY3 interpolated between the horizon pair),
// far ridge, mid trees (TREES_MID), near ground (GROUND_DARK), sun. Rest invented.
constexpr uint32_t KEY_DAY[] = {
  /* SKY0..SKY4        */ 0x4F86A8, 0x6E9FB8, 0x94B9C6, 0xABC6C7, 0xC3D3C9,
  /* SUN               */ 0xE8C170,
  /* CLOUD_LIT, SHADE  */ 0xA6C4C6, 0x7EA6BD,
  /* RIDGE F, M, N     */ 0x7FA3A8, 0x6A9494, 0x4C7B72,
  /* TREES_FAR, MID    */ 0x5A8883, 0x3E6658,
  /* GROUND, DARK      */ 0x3A5A44, 0x2C4A3C,
  /* GRASS             */ 0x55855A,
  /* WATER, LIGHT      */ 0x8CC4CF, 0xDCEEEA,
  /* ROCK              */ 0x5F6D6B,
  /* TRUNK, FOL, FOLD  */ 0x3A2E26, 0x2F5646, 0x22402F,
  /* TENT, TENT_LIT    */ 0xC96F3C, 0xF0CF7A,
  /* FIRE, CORE, SMOKE */ 0xE07B39, 0xFFE9A0, 0x8FA5AB,
  /* ACTOR, LIGHT      */ 0x14231C, 0x22382B,
  /* STAR              */ 0xFFF1C2,
  /* CHIP_OUTLINE      */ 0x5F8FA8,
};

// GOLDEN HOUR: brief section 2 for sky (SKY3 interpolated), far ridge, mid
// trees (TREES_MID), near ground (GROUND_DARK), sun. Ridge M/N and the rest invented.
constexpr uint32_t KEY_GOLDEN[] = {
  /* SKY0..SKY4        */ 0xD9774A, 0xE89A55, 0xF2BC6B, 0xF4C87B, 0xF7D58C,
  /* SUN               */ 0xFFE3A3,
  /* CLOUD_LIT, SHADE  */ 0xF2B86E, 0xEB9F5E,
  /* RIDGE F, M, N     */ 0xB8705A, 0x9A5F54, 0x7F5450,
  /* TREES_FAR, MID    */ 0x8E5A52, 0x6B4A45,
  /* GROUND, DARK      */ 0x4A3A3F, 0x3A2E35,
  /* GRASS             */ 0x6A5A3E,
  /* WATER, LIGHT      */ 0xF8CB78, 0xFFEBB8,
  /* ROCK              */ 0x7A5E58,
  /* TRUNK, FOL, FOLD  */ 0x3A2A2A, 0x4A3A3F, 0x2E2429,
  /* TENT, TENT_LIT    */ 0xC8662F, 0xF6C766,
  /* FIRE, CORE, SMOKE */ 0xE07B39, 0xFFE3A0, 0xA9857F,
  /* ACTOR, LIGHT      */ 0x1A1318, 0x2C2024,
  /* STAR              */ 0xFFF1C2,
  /* CHIP_OUTLINE      */ 0xC47A52,
};

// DUSK: brief section 2 for sky (SKY3 interpolated), far ridge, mid trees
// (TREES_MID), near ground (GROUND_DARK), fire accent. Ridge M/N and the rest invented.
constexpr uint32_t KEY_DUSK[] = {
  /* SKY0..SKY4        */ 0x3B3A6B, 0x6C4A7A, 0xB5607A, 0xCD7572, 0xE58A6B,
  /* SUN               */ 0xF0985E,
  /* CLOUD_LIT, SHADE  */ 0xB86478, 0x7C4E7C,
  /* RIDGE F, M, N     */ 0x5A4466, 0x4A3A5A, 0x3D3354,
  /* TREES_FAR, MID    */ 0x443A57, 0x33304D,
  /* GROUND, DARK      */ 0x2A2842, 0x201E33,
  /* GRASS             */ 0x3C4258,
  /* WATER, LIGHT      */ 0xEF9A7C, 0xFFC9A0,
  /* ROCK              */ 0x4A4260,
  /* TRUNK, FOL, FOLD  */ 0x2A2038, 0x2A2E48, 0x1E2038,
  /* TENT, TENT_LIT    */ 0xC8683C, 0xF6C070,
  /* FIRE, CORE, SMOKE */ 0xF2A65A, 0xFFE6A0, 0x8A7490,
  /* ACTOR, LIGHT      */ 0x0E0D1A, 0x1A1828,
  /* STAR              */ 0xFFF1C2,
  /* CHIP_OUTLINE      */ 0x8A5A80,
};

static_assert(sizeof(KEY_NIGHT)   / sizeof(uint32_t) == SLOT_COUNT, "night table size");
static_assert(sizeof(KEY_DAWN)    / sizeof(uint32_t) == SLOT_COUNT, "dawn table size");
static_assert(sizeof(KEY_MORNING) / sizeof(uint32_t) == SLOT_COUNT, "morning table size");
static_assert(sizeof(KEY_DAY)     / sizeof(uint32_t) == SLOT_COUNT, "day table size");
static_assert(sizeof(KEY_GOLDEN)  / sizeof(uint32_t) == SLOT_COUNT, "golden table size");
static_assert(sizeof(KEY_DUSK)    / sizeof(uint32_t) == SLOT_COUNT, "dusk table size");

struct KeySet {
  Palette k[SS_MOOD_COUNT];
};

constexpr KeySet buildKeys() {
  KeySet ks{};
  const uint32_t* const tables[SS_MOOD_COUNT] = {KEY_NIGHT, KEY_DAWN, KEY_MORNING,
                                                 KEY_DAY, KEY_GOLDEN, KEY_DUSK};
  for (int m = 0; m < SS_MOOD_COUNT; ++m) {
    for (int s = 0; s < SLOT_COUNT; ++s) ks.k[m].c[s] = rgb565_hex(tables[m][s]);
  }
  return ks;
}

constexpr KeySet g_keys = buildKeys();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline uint8_t roundU8(float v) {
  if (v <= 0.f) return 0;
  if (v >= 255.f) return 255;
  return (uint8_t)(v + 0.5f);
}

// Scale every channel by s (8-bit space), clamped.
Pixel scalePx(Pixel p, float s) {
  return rgb565(roundU8((float)red8(p) * s), roundU8((float)green8(p) * s),
                roundU8((float)blue8(p) * s));
}

// Desaturate toward the pixel's own luma by `amount` 0..1.
Pixel desaturate(Pixel p, float amount) {
  const float r = (float)red8(p), g = (float)green8(p), b = (float)blue8(p);
  const float y = 0.299f * r + 0.587f * g + 0.114f * b;
  return rgb565(roundU8(lerp(r, y, amount)), roundU8(lerp(g, y, amount)),
                roundU8(lerp(b, y, amount)));
}

}  // namespace

const Palette& moodKey(Mood m) {
  int i = (int)m;
  if (i < 0 || i >= SS_MOOD_COUNT) i = 0;   // out of range: night (defensive)
  return g_keys.k[i];
}

Pixel mixRGB(Pixel a, Pixel b, float t) {
  const float u = clamp01(t);
  const float ar = (float)red8(a), ag = (float)green8(a), ab = (float)blue8(a);
  const float br = (float)red8(b), bg = (float)green8(b), bb = (float)blue8(b);
  return rgb565(roundU8(ar + (br - ar) * u), roundU8(ag + (bg - ag) * u),
                roundU8(ab + (bb - ab) * u));
}

Pixel depthRamp(Pixel base, Pixel horizon, float depth) {
  return mixRGB(base, horizon, clamp01(depth));
}

void computePalette(float mood, const WeatherTint& w, Palette& out) {
  // Wrap mood into [0, SS_MOOD_COUNT).
  const float n = (float)SS_MOOD_COUNT;
  float m = fmodf(mood, n);
  if (m < 0.f) m += n;
  if (m >= n) m = 0.f;   // m + n can round up to exactly n for tiny negatives
  int i0 = (int)floorf(m);
  if (i0 > SS_MOOD_COUNT - 1) i0 = SS_MOOD_COUNT - 1;
  const int i1 = (i0 + 1) % SS_MOOD_COUNT;   // mood 5.x blends toward key 0
  const float f = m - (float)i0;

  const Palette& a = g_keys.k[i0];
  const Palette& b = g_keys.k[i1];
  for (int s = 0; s < SLOT_COUNT; ++s) out.c[s] = (f <= 0.f) ? a.c[s] : mixRGB(a.c[s], b.c[s], f);
  for (int s = SLOT_COUNT; s < 32; ++s) out.c[s] = 0;

  // Overcast: sky, clouds, ridges and far trees go toward a gray-blue that is a
  // desaturated SKY2 (taken before any overcast change). The sun is darkened
  // toward the (already pulled) SKY2 so it dissolves into the cloud deck.
  const float oc = clamp01(w.overcast);
  if (oc > 0.f) {
    const Pixel gray = desaturate(out.c[SKY2], 0.65f);
    const float k = oc * 0.6f;
    static const Slot kPulled[] = {SKY0, SKY1, SKY2, SKY3, SKY4, CLOUD_LIT, CLOUD_SHADE,
                                   RIDGE_FAR, RIDGE_MID, RIDGE_NEAR, TREES_FAR};
    for (Slot s : kPulled) out.c[s] = mixRGB(out.c[s], gray, k);
    out.c[SUN] = mixRGB(out.c[SUN], out.c[SKY2], oc);
  }

  // Fog: far layers lift toward the (post-overcast) horizon band.
  const float fg = clamp01(w.fog);
  if (fg > 0.f) {
    const Pixel h = out.c[SKY4];
    out.c[RIDGE_FAR]  = mixRGB(out.c[RIDGE_FAR],  h, fg * 0.7f);
    out.c[RIDGE_MID]  = mixRGB(out.c[RIDGE_MID],  h, fg * 0.5f);
    out.c[TREES_FAR]  = mixRGB(out.c[TREES_FAR],  h, fg * 0.4f);
    out.c[RIDGE_NEAR] = mixRGB(out.c[RIDGE_NEAR], h, fg * 0.25f);
  }

  // Night dim: scale everything, but keep small light sources nearly intact.
  const float nd = clamp01(w.night_dim);
  if (nd > 0.f) {
    const float sAll = lerp(1.f, 0.55f, nd);
    const float sLight = lerp(1.f, 0.85f, nd);
    for (int s = 0; s < SLOT_COUNT; ++s) {
      const bool light = (s == STAR || s == FIRE || s == FIRE_CORE || s == TENT_LIT);
      out.c[s] = scalePx(out.c[s], light ? sLight : sAll);
    }
  }
}

}  // namespace ss
