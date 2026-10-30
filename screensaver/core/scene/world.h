// World: everything dynamic in one struct, its dirty tracking and its draw order.
//
// Beats (anim/scheduler.h) get a SceneCtx and drive the world; they never draw.
// worldTick runs after the timeline each tick: it steps the ambient layer and
// marks dirty strips from each element's previous and current bounds. The
// renderer then calls worldDrawStatic (cache rebuild) and worldDrawDynamic (per
// dirty strip). The per-strip draw order lives in one place, worldDrawDynamic.
#pragma once

#include "../anim/rng.h"
#include "../render/renderer.h"
#include "ambient.h"
#include "buildup.h"
#include "landscape.h"

namespace ss {

struct App;
struct Inputs;

enum FireState : uint8_t { FIRE_UNLIT = 0, FIRE_LIT, FIRE_EMBERS };

// Fire pit in the campsite clearing. Stage 2 lays and lights it at the end of
// the build-up; stage 4 drives its state from the story.
struct FirePit {
  Vec2 pos;          // center of the pit on the ground
  uint8_t state;     // FireState
  float intensity;   // 0..1, eased toward the state's target by the ambient step
  float stones;      // 0..1, the ring of stones being laid (1 = all down)
  float clock;       // flicker clock, seconds
};

// Ground actors are drawn far to near by base y. Stage 2 has the fire and the
// swaying tufts; the tent, props, hiker and critters join in later stages.
enum ActorKind : uint8_t { ACT_FIRE = 0, ACT_TUFT };
struct ActorRef {
  uint8_t kind;
  uint8_t idx;
  int16_t base_y;
};
#define WORLD_ACTOR_MAX 24

// Previous bounds per element, so a move dirties both where it was and where it is.
struct WorldDirty {
  Rect cloud[2];
  Rect bird;
  Rect fire;
  Rect glint[AMB_GLINT_MAX];
  Rect tuft[AMB_TUFT_MAX];
  Rect smoke[AMB_SMOKE_MAX];
  uint16_t cached;   // BuildState::cached the cache was last asked to hold
  uint8_t phase;     // build phase and progress last tick
  float u;
  float fire_stones; // fire pit state last tick
  float fire_int;
};

struct World {
  Landscape land;
  Cloud clouds[2];
  BuildState build;
  Ambient amb;
  FirePit fire;
  int n_actors;
  ActorRef actors[WORLD_ACTOR_MAX];
  WorldDirty dirty;
  uint32_t tick;     // ticks since loop start
};

struct SceneCtx {
  App* app;
  World* w;
  Rng* rng;
  const Inputs* in;
  Renderer* r;
  float dt;
};

// Loop start: new place from `rng`, build state reset, ambient seeded.
void worldBeginLoop(World& w, Rng& rng);
// After the timeline ticked: ambient step, actor sort, dirty marks, cache
// invalidation at build phase boundaries.
void worldTick(World& w, Rng& rng, Renderer& r);

void worldDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood);
void worldDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal, float mood);

}  // namespace ss
