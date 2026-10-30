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
#include "hiker.h"
#include "landscape.h"
#include "props.h"
#include "spine.h"

namespace ss {

struct App;
struct Inputs;

// KINDLING: being blown on; intensity follows `kindle` quickly both ways, so a
// blow flares it and a failed try dies back.
enum FireState : uint8_t { FIRE_UNLIT = 0, FIRE_LIT, FIRE_EMBERS, FIRE_KINDLING };

// Fire pit in the campsite clearing. The build-up lays the stones; the fire
// beat (spine.cpp) lights it.
struct FirePit {
  Vec2 pos;          // center of the pit on the ground
  uint8_t state;     // FireState
  float intensity;   // 0..1, eased toward the state's target by the ambient step
  float kindle;      // KINDLING target, 0..1
  float fuel;        // LIT target, 0..1: 1 a fresh fire, lower as it burns down
  float stones;      // 0..1, the ring of stones being laid (1 = all down)
  float clock;       // flicker clock, seconds
};

// Ground actors are drawn far to near by base y. FGPINE is a foreground pine
// drawn again over the hiker when he walks behind it (only inside his bounds).
enum ActorKind : uint8_t {
  ACT_FIRE = 0, ACT_TUFT, ACT_HIKER, ACT_TENT, ACT_PACK, ACT_BAG, ACT_CAIRN, ACT_ITEM, ACT_FGPINE, ACT_LEAF
};
struct ActorRef {
  uint8_t kind;
  uint8_t idx;
  int16_t base_y;
};
// Fire 1, tufts 14, hiker 1, props 4 + 7 items, leaf 1, pines 2 = 30; the
// rest is room for stage 5's critters.
#define WORLD_ACTOR_MAX 40

// What the spine layers on top of the rig, applied by worldTick after
// hikerTick. The pose offsets are added to the composed pose each tick (not
// accumulated); scripts ease them back to 0 before the next pose change.
struct HikerFx {
  bool on;                         // in the scene; visible = on and inside the frame
  bool path_on;                    // feet.y follows feet.x along (x0,y0)-(x1,y1) while walking
  float path_x0, path_y0, path_x1, path_y1;
  float lean, head;                // radians added to the torso lean and head tilt
  float lift;                      // hips raised, units of height (hops, the stumble)
  float reach;                     // 0..1 near arm swung out straight toward reach_to
  float reach_to;                  // its shoulder angle: ~1.3 ahead (into the fire), ~0.25 down (into the water)
  float armload;                   // 0..1 the comically big armload drawn over the carried wood
  // Arm i (0 near) blended by arm_w[i] toward absolute shoulder and elbow
  // angles (sips, bites, a shrug, a throw), before `reach` is applied.
  float arm_w[2], arm_sh[2], arm_el[2];
};

// Previous bounds per element, so a move dirties both where it was and where it is.
struct WorldDirty {
  Rect cloud[2];
  Rect bird;
  Rect fire;
  Rect glint[AMB_GLINT_MAX];
  Rect tuft[AMB_TUFT_MAX];
  Rect smoke[AMB_SMOKE_MAX];
  Rect hiker;
  Rect tent, pack, bag, cairn;
  Rect item[ITEM_COUNT];
  Rect steam, skip, leaf;
  Steam steam_s;
  Skip skip_s;
  Leaf leaf_s;
  float fade;
  Tent tent_s;                     // prop state last tick (a prop that did not change marks nothing)
  PackProp pack_s;
  FoodBag bag_s;
  Cairn cairn_s;
  Item item_s[ITEM_COUNT];
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
  Hiker hiker;
  HikerFx fx;
  Rect hiker_rect;   // what the hiker (and his armload) covers this tick; empty when hidden
  Tent tent;
  PackProp pack;
  FoodBag bag;
  Cairn cairn;
  Item items[ITEM_COUNT];
  Steam steam;       // the pot boiling over (dinner b)
  Skip skip;         // a skipped stone and its rings (skip_stone filler)
  Leaf leaf;         // the leaf that lands on his knee (stream_listen filler)
  // Whole-frame fade to ACTOR (near black): 0 none, 1 black. The close ends
  // on 1; the build beat brings the next loop up from it.
  float fade;
  LoopPlan plan;
  SpineState spine;
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

// Loop start: new place from `rng`, build state reset, ambient seeded, camp
// layout and props reset, hiker out of the scene. The plan is set by the app.
void worldBeginLoop(World& w, Rng& rng);
// After the timeline ticked: ambient step, hiker step (rig, path, overlays,
// fire light, held props), actor sort, dirty marks, cache invalidation at build
// phase boundaries.
void worldTick(World& w, Rng& rng, Renderer& r);

void worldDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood);
void worldDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal, float mood);

}  // namespace ss
