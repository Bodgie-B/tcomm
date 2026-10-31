// The minor-effect layer (brief section 4): every 10 to 30 s a small reward
// for anyone glancing at the screen, beside the story spine.
//
// One minor runs at a time. Each is a small state machine (minors.cpp) over at
// most two critters (driven through the director, tag CT_MINOR, released at
// the end) or a small effect (a Bit: leaf, moth, bee, rings; or a cloud's
// ear). The scheduler picks from a weighted pool: weights are drawn per loop,
// a ring buffer keeps the last 6 picks out, and scene gates take out what
// cannot play now (no tent, no lit tent, not dark enough, hiker in the way).
//
// Firing policy (minorsStep): a new minor starts only when none is running,
// the gap since the last one started (10 to 30 s, drawn per minor) has passed,
// at least 6 s have passed since a beat other than a pause ended (spine beat,
// filler or major) and 5 s since the last minor ended, the current beat does
// not own the critters (dinner, bear, close, the build, or any beat this file
// does not know, which covers majors placed as beats), no critter of anyone
// else is in the scene, and the hiker is not walking. When a beat that owns the
// critters starts while a minor runs, its critters leave the frame at once.
//
// minorsSync (app.cpp, before worldTick) hands it the timeline's beat and the
// mood; minorsStep runs from worldTick between the spine's critter scenes and
// the director. Randomness comes from the Minors' own Rng, seeded at loop
// start from a copy of the loop RNG, so the minors never shift the loop RNG's
// sequence (spine picks, ambient) for a given seed.
#pragma once

#include "../anim/rng.h"
#include "../gfx/surface.h"

namespace ss {

struct World;
struct Timeline;
struct Inputs;
struct Renderer;

enum MinorId : uint8_t {
  MN_CHIPMUNK_CROSS = 0, MN_SQUIRREL_NUT, MN_FROG_SPLASH, MN_BIRD_CHIP, MN_DEER_EDGE, MN_OWL_WATCH,
  MN_MOUSE_TENT, MN_LEAF_FALL, MN_MOTH_LANTERN, MN_BEE_VISIT, MN_CLOUD_ANIMAL, MN_COUNT,
  MN_NONE = 0xFF
};

#define MINOR_RING 6

struct Minors {
  Rng rng;                   // own stream (see the top of this file)
  float weight[MN_COUNT];    // this loop's weights
  uint32_t next_tick;        // earliest start of the next minor (its gap)
  uint32_t hold_tick;        // no start before this (a beat just ended, a minor just ended)
  uint32_t last_start;       // tick the last minor started
  uint8_t cur;               // MinorId running, MN_NONE when idle
  uint8_t stage;             // its state; MINOR_ENDING once it has let go
  bool quit;                 // told to wrap up (a beat that owns the critters began)
  float t;                   // seconds in the stage
  float age;                 // seconds since it started
  int8_t slot[2];            // critter slots held (-1 none)
  int8_t leave_side[2];      // leaving by this edge (0 not leaving): see leaveAt in minors.cpp
  float leave_speed[2];
  uint8_t leave_gait[2];
  int8_t bit[2];             // World::bits held (-1 none)
  int8_t cloud;              // cloud with the ear (-1 none)
  int8_t dir;                // a side or direction, +1 / -1
  uint8_t n;                 // a count (spots dug)
  Vec2 p0, p1, p2, p3;       // scratch points (spots, path ends, control points)
  float u0;                  // scratch value
  bool fx_head, fx_arm;      // the hiker's head / near arm are ours (restored at the end)
  uint8_t ring[MINOR_RING];  // last picks, oldest overwritten
  uint8_t ring_n, ring_i;
  uint16_t fired;            // minors started this loop
  uint8_t fired_by[MN_COUNT];
  uint8_t chip_n;            // chipmunk_cross picks so far (what it carries escalates)
  // From the timeline and the inputs, each tick (minorsSync).
  const char* beat;
  int beat_idx, prev_idx;
  bool prev_pause;
  float beat_left;           // seconds left in the current beat
  float mood;
  bool night;
};

#define MINOR_ENDING 0xFF

// Loop start (from worldBeginLoop, after the World is cleared): own Rng from
// a copy of `rng` (not advanced), this loop's weights, nothing running.
void minorsInit(World& w, const Rng& rng);
// Before worldTick each tick: the running beat (name, index, time left) and
// the mood.
void minorsSync(World& w, const Timeline& tl, const Inputs& in);
// Every tick from worldTick, after spineCritterScenes and before
// directorStep: fire, run and end minors. `r` takes the dirty marks the
// cloud's ear needs (everything else is tracked by World).
void minorsStep(World& w, Renderer& r);

// The critters are not ours: World::busy is set (a major, dinner, bear or
// close), or the beat in `w.minors.beat` is dinner, bear, close, the build, or
// a name not known here (a major, or the stillness around one).
bool worldBeatOwnsCritters(const World& w);
// The running minor's name, or "" when none.
const char* minorsName(const World& w);
const char* minorName(int id);
// Critter slots in use with tag CT_MINOR.
int minorsCritters(const World& w);

}  // namespace ss
