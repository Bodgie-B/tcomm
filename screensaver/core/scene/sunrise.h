// The sunrise overlook (SF_SUNRISE, stage 15; Mike, 11 Nov): the Lookout's morning twin.
//
// Mike: "a sunrise scene that is similar to the sunset scene, but with different mountains and the
// hiker arriving to a rock overhang that he sits at and
// watches the sun rise."
//
// The place (sunrise_land.cpp): layered mountain ridgelines, long and smooth, fading into the
// morning haze far to near (flat fills, no outlines); a wide valley below with low fog in it; in
// the foreground a flat slab of pale layered rock jutting out over the valley from the lower left
// to about the middle of the frame, with air under its tip; scrub pines and mountain laurel on the slope behind it.
//
// The beat (sunrise.cpp), about four and a half minutes, played once each morning around the real
// sunrise (the host's window, starting a little before it):
//   arrive    in the blue before dawn, the stars fading, he comes up the trail from behind the
//             laurel with his pack, his headlamp lit, and walks out along the slab
//   settle    sets the pack down, switches the lamp off, steps to the very end and sits with his
//             legs over the edge
//   pour      pours from a flask into his cup; it steams
//   dawn      the sky goes from deep blue to rose (the palette's own mood, in steps); the last stars go
//   sunbreak  the sun breaks over the far ridge (the KEY moment: the host counts the day's showing
//             from here); the light turns gold
//   light     the light walks down the ridges one layer at a time and the valley fog glows and thins;
//             a hawk drifts past below him; he raises his cup to the sun
//   linger    he sits a while
//   leave     packs up and walks back the way he came; the frame fades, and the next loop is the
//             morning camp
// A house dog that is out comes up behind him and sits beside him on the ledge (Lucy, Copper, or
// both), drawn with the house-dog rigs; it trots off if it goes in meanwhile.
//
// Weather: none of its own. On a rain, storm or snow reading the host's window is ignored and the
// camp plays (spine.h spineSunriseWeatherOk). On a gray, dry morning (the reading's overcast at 0.5
// or more) it plays muted: the overcast palette, a paler sun with no glow, less light on the ridges.
//
// The real sky: the sun rises where the real one does (Inputs::sky's bearing, the scene looking
// east), and the scene's own clock times its rise. The camp loop after it opens on the real sky.
//
// Memory: it lives in World's set-piece union (world.h), in the Lookout's place (they never run in
// the same loop), so it adds nothing to the scene block. Nothing here is static or allocated.
// Tuning marked PROPOSED is a guess; how it looks is UNVERIFIED on the panel.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"
#include "landscape.h"

namespace ss {

struct World;
struct Renderer;
struct Inputs;
struct WeatherTint;

#define SR_RIDGES 4
#define SR_PINES 7
#define SR_BUSHES 12
#define SR_FOG 9
#define SR_STARS 24
#define SR_STARS_BRIGHT 6
#define SR_SUN_X0 440   // the columns the sun can be in (its skyline is kept for these)
#define SR_SUN_W 340

struct SrPine { int16_t x, base_y; uint8_t h, w; };   // a scrub pine, wind-bent
struct SrBush { int16_t x, y; uint8_t rx, ry; };     // a laurel clump
struct SrFog { int16_t x, y; uint8_t rx, ry; };      // a fog bank in the valley

struct SunriseLand {
  float crest[SR_RIDGES][LS_CREST_N];   // 0 far .. 3 near ridge crest y, every LS_CREST_STEP px from LS_CREST_X0
  float hill[2];                        // the slope behind the slab: its top at x 0 and where it meets the slab
  float hill_x;                         // the slope's right end
  // The slab. Top surface: back edge from bl to bt, front edge from fl to ft; the tip's end from bt to
  // ft. The front face hangs below the front edge, `face` px deep at the left and `face_tip` at the tip;
  // the underside runs back from the tip's foot to `under` and the support cliff drops from there.
  Vec2 bl, bt, fl, ft;
  float face, face_tip;
  Vec2 under;
  float fog_top, fog_bot;               // the fog's rows (for the repaints)
  int n_pines;
  SrPine pines[SR_PINES];
  int n_bushes;
  SrBush bushes[SR_BUSHES];
  int n_fog;
  SrFog fog[SR_FOG];
  int16_t sun_top[SR_SUN_W];            // the first landscape row in each column the sun can be in
};

// The static layers, far to near (a bit mask picks which ones a draw call draws).
enum SrLayer : uint8_t { SRL_SKY = 0, SRL_RIDGES, SRL_FOG, SRL_HILL, SRL_SLAB, SRL_COUNT };
#define SRL_BIT(l) ((uint16_t)(1u << (l)))
#define SRL_ALL ((uint16_t)((1u << SRL_COUNT) - 1u))

// What a static draw shows: the layers, the build's state and the light.
struct SrDraw {
  uint16_t layers;
  float sky;               // 0 a flat horizon colour (SKY4) .. 1 the sky bands
  float drop[SR_RIDGES];   // px each ridge sits below its place (rising in the build)
  float fog_u;             // 0..1 the fog drifting in
  float fg_drop;           // px the slope and the slab sit below their place
  float plants_u;          // < 0 none, 0..1 popping, >= 1 all
  uint8_t lit[SR_RIDGES];  // the morning light on each ridge, 0 none .. 2 full
  uint8_t fog_glow;        // 0..2
  uint8_t fog_thin;        // 0..3 (3: the fog mostly gone)
  uint8_t slab_lit;        // 0..1
  bool muted;              // a gray morning: half the light
};
SrDraw sunriseDrawDone(uint16_t layers);
void sunriseLandGenerate(SunriseLand& sl, Rng& rng);
void sunriseLandDraw(const SunriseLand& sl, Surface& s, const Rect& clip, const Palette& pal, const SrDraw& d);
// The sky band (0..4, SKY0..SKY4) the sky fills row y with.
int sunriseSkyBand(int y);
// The ridges' skyline at frame x (the highest crest there).
float sunriseRidgeTop(const SunriseLand& sl, float x);
// A point on the slab's top surface: u 0 the left edge .. 1 the tip, v 0 the back edge .. 1 the front.
Vec2 sunriseSlabAt(const SunriseLand& sl, float u, float v);
// The rows ridge k's colour covers (its light step repaints them), and the fog's.
void sunriseRidgeRows(const SunriseLand& sl, int k, int& y0, int& y1);

// ---- the scene's state ----------------------------------------------------------------------

struct SrStar {
  int16_t x, y;
  uint16_t out;      // ticks from loop start to gone (it fades in four steps of 0.5 s before)
  uint8_t r10;       // radius x 10 (0: a single pixel)
  uint8_t lvl_d;     // the step last marked dirty
};

struct SrDog {
  int8_t slot;       // World::critters slot, -1 none
  uint8_t tone;      // DogTone
  uint8_t mode;      // sunrise.cpp DogMode
  float t;
  float u_seat;      // where on the slab it sits (u)
};

struct Sunrise {
  SunriseLand land;
  // ---- loop start ----
  bool muted;              // a gray morning
  float walk_k;            // pace, heights a second
  float sun_x0, sun_r;     // the sun's column at the break and its radius
  float sun_v;             // px a tick it rises
  uint32_t rise_tick;      // the tick its centre starts up from just below the ridge
  SrStar stars[SR_STARS];
  SrDog dogs[2];
  // ---- running ----
  uint8_t stage;           // what the cache holds: 0 the build, 1 all
  float build_t;
  uint32_t broke_tick;     // the first tick a pixel of the sun shows: the key moment; 0 before
  uint32_t sat_tick;       // the tick he is down on the ledge; 0 before
  uint8_t steps;           // palette steps taken
  uint8_t lit[SR_RIDGES];  // the light on the ridges now (SrDraw::lit)
  uint8_t fog_glow, fog_thin, slab_lit;
  float lamp;              // his headlamp, 0 off .. 1 on
  float flask;             // the flask in his far hand: 0 away .. 1 out; > 1 pouring
  float steam_t;           // seconds since the cup was filled; < 0 no cup
  bool cup;                // the cup in his hand
  bool pack_down;          // the pack on the rock beside him
  Vec2 pack_at;
  uint8_t hawk;            // 0 not yet, 1 flying, 2 gone
  float hawk_cut;          // the support cliff's x: the hawk is drawn only right of it
  // ---- dirty tracking ----
  uint16_t d_cached;
  uint8_t d_bphase;
  float d_build_t;
  uint8_t d_lit[SR_RIDGES], d_fog_glow, d_fog_thin, d_slab_lit;
  Rect d_sun, d_props, d_pack, d_dog[2];
};

// Loop start (spine.cpp, after the plan): the place from `rng` after every other draw of the loop
// start, the stars, the beats' fitted seconds, the camp's things off.
void sunriseBeginLoop(World& w, Rng& rng);
// Right after (app.cpp): what it takes from the inputs: a gray morning, the real sun's bearing, the
// dogs that are out.
void sunriseInputs(World& w, const Inputs& in);
// The tick (world.cpp sunriseTick calls these around the hiker's step) and the dirty marks.
void sunriseStep(World& w);
void sunriseHikerPre(World& w);
void sunriseMarkDirty(World& w, Renderer& r);
uint16_t sunriseCached(const World& w);
int sunriseBuildPhase(const World& w);
void sunriseBuildRows(const World& w, int& y0, int& y1);
void sunriseDrawStatic(const World& w, Surface& frame, const Palette& pal);
void sunriseDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal);
// The sun's centre now and whether any of it shows over the ridge (tests, the key moment).
Vec2 sunriseSunAt(const World& w);
bool sunriseSunShown(const World& w);
// Stars still out (any step above 0).
int sunriseStarsOut(const World& w);
// The palette's sunrise touch (app.cpp): rose then gold low in the sky, a warm sun; muted on a
// gray morning; none under the night rule.
void sunrisePaletteTweak(const World& w, float mood, const WeatherTint& t, Palette& p);

}  // namespace ss
