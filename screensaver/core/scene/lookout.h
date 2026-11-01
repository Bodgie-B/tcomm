// The Lookout (SF_LOOKOUT, SPINES_WEATHER_LOOKOUT_SPEC.md section 5): the
// sunset set piece on its own landscape (lookout_land.h). He walks up a
// switchback trail getting smaller, stops to breathe while a hawk drifts
// over, the fire lookout rises above the ridge, he climbs its stairs, looks
// round the cab, sits at the west rail and watches the sun go down: the
// palette steps from golden hour to dusk while birds cross, the sun sinks
// behind the ridge line, the stars come out one by one and the frame fades.
//
// lookout.cpp holds the plan, the beats and their scripts (the spine's
// interpreter, script.h), and the scene's own per-tick state below; World
// (world.cpp) keeps the Lookout's tick, draw and dirty paths apart from the
// camp's: none of the camp's dynamic things (stream glints, fire, smoke, tent,
// props, critters, the weather's effects) draw or mark in a Lookout loop.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

struct World;
struct Renderer;
struct WeatherTint;

#define LK_BIRDS_MAX 8
#define LK_STARS 40        // stars that come out through the sunset and the fade (spec 5b)
#define LK_STARS_BRIGHT 8  // the first ones: discs as the camp's larger stars; the rest single pixels

// One crossing in the sunset, planned at loop start.
struct LkBird {
  float at;       // seconds into the sunset beat
  float y;        // flight line
  float speed;    // px/s
  int8_t dir;     // +1 flies right
  bool pair;
  bool lands;     // the pair that circles and lands in the far treeline (once a loop)
};

// One star, planned at loop start (lookoutBeginLoop). It comes out in four
// steps of 0.5 s, the last at `full` ticks after the sunset's start.
struct LkStar {
  int16_t x, y;      // the pixel it sits on (a bright one's centre is its middle)
  uint16_t full;     // ticks from the sunset's first tick to full
  uint8_t r10;       // radius x 10 (0: a single pixel)
  uint8_t lvl_d;     // the step last marked dirty (world.cpp lookoutMarkDirty)
};

// What the static cache holds (and so what the dynamic pass adds).
enum LkStage : uint8_t {
  LKS_BUILD = 0,   // the build beat: the layers built so far below the one animating
  LKS_LAND,        // the whole landscape, no tower
  LKS_REVEAL,      // the landscape; the tower drawn dynamically as it rises
  LKS_TOWER        // the landscape and the tower but the cab's front and the rails (drawn round him)
};

struct LookoutState {
  // ---- loop start (lookoutBeginLoop) ----
  float walk_k;          // trail pace, heights per second (0.5 a stroll, faster on a long trail)
  uint8_t pause_at;      // the trail vertex the approach ends at (the last left turn)
  uint8_t n_birds;
  LkBird birds[LK_BIRDS_MAX];
  // (The one star of the first build went with spec 5b; its two rng draws stay.)
  Vec2 land_at;          // where the landing pair comes down in the far treeline
  Vec2 chip_at;          // the chipmunk's place on the frame's middle girt
  Vec2 perch_at;         // the rail bird's place on the deck's front rail

  // ---- running ----
  uint8_t stage;         // LkStage
  float build_t;         // the build beat's progress 0..1
  float reveal;          // the tower's reveal 0..1
  float h_min;           // his height so far (it only ever shrinks on the way up)
  float speed_k;         // walking pace now, heights per second
  float climb_w, climb_to;   // the climbing gait's weight, and where it eases to
  bool inside;           // he is in the cab (behind its front wall)
  uint8_t leg;           // the trail vertex he walks to next
  uint8_t steps;         // sunset palette steps taken
  float sun_s;           // seconds into the sunset (0 before it)
  bool sun_on;
  uint8_t bird_i;        // the next sunset bird
  uint8_t bird_mode;     // LkBirdMode (lookout.cpp)
  float bird_t;          // seconds in the mode
  uint8_t bird_cur;      // the planned bird flying now
  bool hawk;             // the trail_pause's hawk has been sent
  uint8_t rail_mode, chip_mode;   // the rail bird and the chipmunk (glyphs 0, 1)
  float rail_t, chip_t, tap_t;
  uint16_t birds_flown;  // crossings started this loop (tests, notes)
  uint8_t pairs_landed;

  // ---- the sun and the stars (spec 5b; planned at loop start) ----
  float sun_r;           // disc radius
  Vec2 sun_p0;           // its centre at tick 0
  Vec2 sun_v;            // px a tick: down, a little to the right (dx = 0.2 dy)
  uint32_t sun_tick;     // the planned tick he is down on the deck's edge (7/8 below then)
  uint32_t sat_tick;     // the tick he is down (actSitEdge at u = 1); 0 before
  uint32_t sunset_tick;  // the sunset's first tick; 0 before it
  uint32_t sun_set_tick; // the planned first tick no pixel of it shows (the kept start's)
  uint8_t sun_tries;     // starts drawn (1..16; fewer when one has no cloud cover; tests, notes)
  float sun_cover;       // the kept start's worst share of the disc behind a cloud's puffs (+4 px), 0..1
  bool sun_done;         // set (lookoutMarkDirty) the first tick after he is down that no pixel of it
                         // shows; from then it is neither drawn nor tracked, even where the skyline
                         // steps down past a pine corner and a pixel would show again for a few ticks
  LkStar stars[LK_STARS];
  Rect d_sun;            // the sun's bounds last marked (empty once it is set)

  // ---- dirty tracking (world.cpp) ----
  uint16_t d_cached;     // the layers the cache was last asked to hold
  float d_build_t;
  float d_reveal;
  bool d_inside;
  uint8_t d_bphase;
};

// The Lookout's tick from worldTick (after the clouds, before the hiker):
// the build and reveal state, the birds, the rail bird, the chipmunk, the tap.
void lookoutStep(World& w);
// Around the hiker's rig step (world.cpp stepHiker): before it his height
// from where his feet are on the hill (never growing on the way up) and his
// pace from his height; after it the climbing gait.
void lookoutHikerPre(World& w);
void lookoutHikerPost(World& w);
// The layers the static cache holds now (LKL_BIT mask), and the rows the
// build's animating layers touch (y0 >= y1: none).
uint16_t lookoutCached(const World& w);
void lookoutBuildRows(const World& w, int& y0, int& y1);
int lookoutBuildPhase(const World& w);
// Static cache and per-strip draws (worldDrawStatic / worldDrawDynamic).
void lookoutDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood);
void lookoutDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal, float mood);
// The sun and the stars, for the tests and the notes: how much of the disc
// is below the skyline at its centre x (0 above the line .. 1 fully set), and
// how many stars are at full.
float lookoutSunBelow(const World& w);
int lookoutStarsOut(const World& w);
// The sun's centre now (tick w.tick), and whether any pixel of it shows
// above the skyline (false once it is fully set).
Vec2 lookoutSunAt(const World& w);
bool lookoutSunShown(const World& w);
// A star's step now, 0 (not out) .. 4 (full).
int lookoutStarLevel(const World& w, int i);
// The sun's and the stars' dirty marks (world.cpp lookoutTick, after the step).
void lookoutMarkDirty(World& w, Renderer& r);

// The palette's Lookout touch (app.cpp computeAppPalette): through the
// sunset the horizon clouds hold their colour, a few tenths of mood behind
// the sky (the same weather and night rule), so they are the last to go.
void lookoutPaletteTweak(const World& w, float mood, const WeatherTint& t, float flash, Palette& p);

}  // namespace ss
