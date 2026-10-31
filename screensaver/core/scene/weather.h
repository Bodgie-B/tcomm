// Weather: one state per loop (SPINES_WEATHER_LOOKOUT_SPEC.md section 4).
//
// weatherPlan picks the loop's kind at loop start: from the host's
// Inputs::weather_code when have_outside (the mapping table kCodeKinds in
// weather.cpp), else a random draw weighted by the mood (clear half the
// time), plus maybe one small extra on top (a gust, a short shower) that
// never contradicts the base. Every random number here comes from the
// Weather's own Rng, seeded from the loop seed, so the loop RNG (the story's
// picks, and the next loop's seed) never sees the weather.
//
// What each kind moves (all drawn in the dynamic layer; see worldDrawDynamic):
//
// | Kind  | Effects                                                                      |
// |-------|------------------------------------------------------------------------------|
// | wind  | grass sway x (1 + 1.5 |wind| gust), the tent's apex sways with the gusts,     |
// |       | leaves blow across on the gusts, the hat blows off once (he fetches it, and  |
// |       | goes back to where he stood or sat)                                          |
// | rain  | hashed streaks per strip, drips off the tent ridge into puddle rings, rings  |
// |       | on the stream, the fire low and smoky with a hiss, overcast 0.6              |
// | snow  | hashed flakes drifting on sines, snow building on the tent, shaken off once   |
// | fog   | fog in the palette (0.45 screened over the scene's) and 3 drifting,          |
// |       | breathing bands over the pines (the near pines drawn again in front)         |
// | heat  | the far-ridge band shimmers (row offsets), he fans himself with his hat, a    |
// |       | sweat drop now and then                                                      |
// | chill | breath puffs, the arm hug (POSE_HUG over the upper body) and a shiver         |
// | storm | rain at 1, wind, lightning (a palette `flash` term), the rain_shelter major   |
//
// The overlays on the hiker (hat, fan, hug, shiver) run only while no script
// holds him: in a pause with time left for them, standing or sitting still,
// no major or minor running (and for the hat and the fan, no critter about;
// the minors wait for those two: minors.cpp). They leave him exactly where
// and how they found him, so the story never sees them.
#pragma once

#include "../anim/rng.h"
#include "../gfx/surface.h"
#include "../palette.h"
#include "hiker.h"
#include "majors.h"
#include "props.h"

namespace ss {

struct World;
struct Inputs;
struct Renderer;

enum WeatherKind : uint8_t { WX_CLEAR = 0, WX_WIND, WX_RAIN, WX_SNOW, WX_FOG, WX_HEAT, WX_CHILL, WX_STORM, WX_COUNT };
enum WeatherExtra : uint8_t { WXE_NONE = 0, WXE_GUST, WXE_SHOWER };
// What the hiker overlay is doing (Weather::gag).
enum WeatherGag : uint8_t { WG_NONE = 0, WG_HAT, WG_FAN, WG_HUG };

#define WX_BITS 4    // the weather's own small things (leaves, drips, rings, a sweat drop, snow puffs)
#define WX_BANDS 3   // fog bands

struct Weather {
  uint8_t kind;        // WeatherKind
  uint8_t extra;       // WeatherExtra
  bool from_data;      // the kind came from Inputs::weather_code (have_outside)
  float k, k_to;       // the kind's intensity, easing toward k_to (a storm's rain eases in rain_shelter)
  float wind;          // -1..0: it blows toward -x, as the clouds drift and the smoke trails
  float gust;          // 0..1 the current gust's envelope
  float shower;        // 0..1 the shower extra's envelope
  float flash;         // 0..1 lightning envelope (storm): 1 on the strike, gone in 0.3 s
  float snow_roof;     // 0..1 snow on the tent
  uint32_t next_gust, gust_t0, gust_len;   // ticks
  uint32_t next_flash, extra_at;
  uint32_t ease_until; // storm: the rain stays eased (rain_shelter) until this tick
  uint16_t flashes;    // strikes this loop
  bool live;           // the build is done and the weather has started
  float rain;          // drawn rain 0..1: the kind's k (rain, storm) or the shower
  uint32_t shower_len; // ticks the shower holds
  Rng rng;             // the weather's own stream

  // What is on screen, quantized so the strips change only when it does.
  uint8_t rain_q, snow_q;      // drawn amounts in 1/16
  uint8_t flash_q;             // palette flash step (0 none, 1 half, 2 full)
  int8_t sway_px;              // the tent's apex sway, whole px

  // The weather's small things: Bits drawn and stepped by props.cpp, owned here.
  Bit bit[WX_BITS];
  uint8_t role[WX_BITS];       // what each one is (weather.cpp WxRole)
  float aux[WX_BITS];          // per role: a leaf's speed, ...
  uint32_t next_drip, next_ring, next_sweat, next_hiss, next_breath, next_shiver;
  Steam breath;                // a breath puff (chill): one small puff (n: 1 he faced right, 2 left)
  Glyph hiss;                  // GL_ARCS over the fire in the rain
  float hiss_t;

  // Fog bands: wide low ellipses in SKY4 over the mid pines, drifting and breathing.
  float band_x[WX_BANDS], band_y[WX_BANDS], band_rx[WX_BANDS], band_ry[WX_BANDS], band_ph[WX_BANDS];

  // The hiker overlay.
  uint8_t gag, stage;          // WeatherGag and its stage
  float gag_t;                 // seconds in the stage
  float gag_len;               // the hug's hold, s
  uint8_t hats, fans, shakes;  // done this loop
  uint16_t n_leaf, n_drip, n_ring, n_puff;   // started this loop (tests, notes)
  Vec2 home;                   // where he was when it began
  int8_t home_face;
  uint8_t home_pose;
  bool home_path;
  Vec2 hat0, hat1;             // the blown hat's flight
  uint32_t next_gag;           // the next hug or fan may start from here
  float hug_w;                 // 0..1 POSE_HUG over his upper body
  float ov_w, ov_sh, ov_el;    // near arm toward (shoulder, elbow) by ov_w: the fan, the hat
  float fan_ph;                // turns
  float shiver;                // seconds left of a shiver
  float shiver_dx;             // px, whole
  float shake;                 // seconds left of the tent's snow shake
  float smoke_mul;             // the fire smoke's rain swelling, sampled on the ambient sub-tick
  bool night;                  // the night rule was in force at loop start (the flash is a shift, not a flash)

  // Dirty tracking (weatherTrack).
  Rect d_bit[WX_BITS];
  Bit d_bit_s[WX_BITS];
  Rect d_band[WX_BANDS];
  int16_t d_band_x[WX_BANDS];
  uint8_t d_band_a[WX_BANDS];
  Rect d_breath;
  Steam d_breath_s;
  Rect d_hiss;
  Glyph d_hiss_s;
  uint8_t d_rain_q, d_snow_q;
  int8_t d_shim[24];           // the shimmer's row offsets last drawn
};

// The mapping table: Inputs::weather_code (ss_app.h's list) to a kind.
// Codes outside the table are clear. Unverified against WeatherGate's real
// OUTSIDE codes (the Zambretti list is not in this repo yet).
uint8_t weatherKindForCode(int code);
const char* weatherKindName(int kind);
// The loop's majors may include rain_shelter: a storm from the OUTSIDE data
// (a random storm keeps the plan the loop would have had: the story never
// depends on the weather's own random draw).
bool weatherPlannedStorm(const Inputs& in);

// Loop start, after the World is cleared and the plan made (the loop's
// ticks bound the extra's time). Costs the loop RNG nothing.
void weatherPlan(World& w, uint64_t loop_seed, const Inputs& in, uint32_t loop_ticks);
// Every tick from worldTick, after the hiker, critter and bit steps and
// before the actor sort.
void weatherStep(World& w);
// From worldTick, last: dirty marks for everything the weather draws.
void weatherTrack(World& w, Renderer& r);
// The weather's part of the palette inputs (app.cpp computeAppPalette):
// overcast (max) and fog (added) into `t`, untouched in clear weather; the
// return is the lightning's flash, in fixed steps (0 none).
float weatherTint(const World& w, WeatherTint& t);

// Hooks for the rest of the scene.
float weatherSwayMul(const World& w);   // grass sway amplitude, 1 in calm weather
float weatherSmokeMul(const World& w);  // smoke puff size, 1 when dry
float weatherFireCap(const World& w);   // the lit fire's intensity cap, 1 when dry
// The rig overlay: hug, fan and shiver over the composed pose (stepHiker).
void weatherApplyPose(const World& w, HikerPose& p, float height);
// A held hat goes in his near hand while he fans himself (after stepHiker).
void weatherPlaceHeld(World& w);
// rain_shelter's cues: a strike now; the rain eased to k for `hold` seconds.
void weatherStrike(World& w);
void weatherEaseRain(World& w, float k, float hold);

// Drawing (worldDrawDynamic). The shimmer first (it shifts the cached rows
// of the far-ridge band in place); the fog bands after the static layers,
// before the ground actors; the near layer (breath, hiss, rain, snow) after
// the smoke. The Bits are sorted among the ground actors (ACT_WXBIT).
void weatherDrawShimmer(const World& w, Surface& s, const Rect& clip);
void weatherDrawFog(const World& w, Surface& s, const Rect& clip, const Palette& pal);
void weatherDrawNear(const World& w, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
