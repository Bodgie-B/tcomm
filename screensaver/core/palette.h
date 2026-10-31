// Palette system.
//
// Shapes store a palette slot, never a color. The current palette is computed
// from a mood position on a solar-day axis plus weather overlays. Changing the
// time of day is a palette swap plus a redraw of the affected layers.
//
// Mood axis (float `mood`, wraps at SS_MOOD_COUNT):
//   0 night, 1 dawn, 2 morning, 3 day, 4 golden hour, 5 dusk, (6 = 0 night)
// The port maps clock time and sunrise/sunset to this axis; core never looks
// at wall-clock time. Fractional values interpolate between neighbouring keys.
#pragma once

#include "gfx/surface.h"

namespace ss {

enum Slot : uint8_t {
  SKY0 = 0, SKY1, SKY2, SKY3, SKY4,     // sky bands, top to horizon
  SUN,                                  // sun or moon disc
  CLOUD_LIT, CLOUD_SHADE,
  RIDGE_FAR, RIDGE_MID, RIDGE_NEAR,     // stacked silhouettes, far to near
  TREES_FAR, TREES_MID,                 // tree lines
  GROUND, GROUND_DARK,                  // campsite ground and its shadow side
  GRASS,
  WATER, WATER_LIGHT,                   // stream body and highlight ripples
  ROCK,
  TRUNK, FOLIAGE, FOLIAGE_DARK,         // near trees
  TENT, TENT_LIT,
  FIRE, FIRE_CORE, SMOKE,
  ACTOR, ACTOR_LIGHT,                   // hiker and animals: body, lit side
  STAR,
  CHIP_OUTLINE,                         // status chip outline in scene palette
  MUSHROOM,                             // the foreground mushroom's cap (dim at night)
  SLOT_COUNT                            // must stay <= 32
};

static_assert(SLOT_COUNT <= 32, "palette slots must fit in 32");

struct Palette {
  Pixel c[32];
  Pixel operator[](Slot s) const { return c[s]; }
};

enum Mood : uint8_t { MOOD_NIGHT = 0, MOOD_DAWN, MOOD_MORNING, MOOD_DAY, MOOD_GOLDEN, MOOD_DUSK, SS_MOOD_COUNT };

// Weather overlays applied after mood interpolation. All 0..1.
struct WeatherTint {
  float overcast;   // desaturates and flattens the sky toward a gray-blue
  float fog;        // lifts far layers toward the horizon color
  float night_dim;  // 1 = night rule in force: clamp bright slots, dim overall
};

// Night rule limits (brief section 11: no large bright areas, no pure white),
// as Rec.709 luminance of the 8-bit expansion, 0..1, applied at night_dim = 1.
// Every slot stays at or under NIGHT_LUM_MAX except the small-highlight slots
// (ACTOR_LIGHT, FIRE_CORE, STAR, SUN), which stay under NIGHT_HIGHLIGHT_MAX.
// Unverified on the panel.
constexpr float NIGHT_LUM_MAX = 0.60f;
constexpr float NIGHT_HIGHLIGHT_MAX = 0.82f;
inline bool isNightHighlight(int s) { return s == ACTOR_LIGHT || s == FIRE_CORE || s == STAR || s == SUN; }

// The key palette for an integer mood. These hold the brief's starting values
// (section 2) and are tuned by eye later on the panel.
const Palette& moodKey(Mood m);

// Compute the palette for a fractional mood position and weather.
// `mood` in [0, SS_MOOD_COUNT); values wrap. Deterministic and allocation-free.
void computePalette(float mood, const WeatherTint& w, Palette& out);

// Atmospheric perspective helper: move `base` toward `horizon` by `depth` 0..1.
// Far layers use a higher depth. Used for extra ridge layers and fog.
Pixel depthRamp(Pixel base, Pixel horizon, float depth);

// Per-channel RGB lerp in 8-bit space, t in 0..1, result packed to 565.
Pixel mixRGB(Pixel a, Pixel b, float t);

}  // namespace ss
