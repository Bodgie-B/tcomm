// The Lookout (SF_LOOKOUT, SPINES_WEATHER_LOOKOUT_SPEC.md section 5): the
// sunset set piece on its own landscape (lookout_land.h). He walks up a
// switchback trail getting smaller, stops to breathe while a hawk drifts
// over, the fire lookout rises above the ridge, he climbs its stairs, looks
// round the cab, sits at the west rail and watches the sun go down: the
// palette steps from golden hour to dusk while birds cross, one star comes
// out and the frame fades.
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

// One crossing in the sunset, planned at loop start.
struct LkBird {
  float at;       // seconds into the sunset beat
  float y;        // flight line
  float speed;    // px/s
  int8_t dir;     // +1 flies right
  bool pair;
  bool lands;     // the pair that circles and lands in the far treeline (once a loop)
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
  Vec2 star;             // where the one star comes out
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
// The palette's Lookout touch (app.cpp computeAppPalette): through the
// sunset the horizon clouds hold their colour, a few tenths of mood behind
// the sky (the same weather and night rule), so they are the last to go.
void lookoutPaletteTweak(const World& w, float mood, const WeatherTint& t, float flash, Palette& p);

}  // namespace ss
