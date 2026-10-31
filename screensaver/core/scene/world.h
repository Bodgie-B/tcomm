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
#include "critters.h"
#include "hiker.h"
#include "landscape.h"
#include "props.h"
#include "spine.h"
#include "minors.h"
#include "majors.h"
#include "memory.h"

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
// drawn again over an actor that walks behind it, only inside that actor's
// bounds: idx bit 0 is the pine, idx >> 1 the actor (0 the hiker, 1 + slot a
// critter).
enum ActorKind : uint8_t {
  ACT_FIRE = 0, ACT_TUFT, ACT_HIKER, ACT_TENT, ACT_PACK, ACT_BAG, ACT_CAIRN, ACT_ITEM, ACT_FGPINE, ACT_LEAF,
  ACT_CRITTER, ACT_BIT, ACT_MAJOR
};
struct ActorRef {
  uint8_t kind;
  uint8_t idx;
  int16_t base_y;
};
// Fire 1, tufts 14, hiker 1 + 2 pines, props 4 + 7 items, leaf 1 = 30;
// critters 8 (+ 2 pine redraws each near the left edge), bits 4. sortActors
// stops adding at the cap (tufts are added before the critters, so a full
// list drops critters at the far left first, never the hiker or props).
// A major's glyphs (up to 24, the fireflies) come last; 80 holds them all.
#define WORLD_ACTOR_MAX 80

// Critters in the scene: slots the director (director.h) hands out.
#define CRITTER_MAX 8

// Who holds a critter slot, and how the director treats it.
enum CritterTag : uint8_t { CT_NONE = 0, CT_DINNER, CT_BEAR, CT_THIEF, CT_MINOR, CT_MAJOR };
enum CritterMode : uint8_t {
  CM_FREE = 0,     // the owner drives it
  CM_STARTLE,      // scattered: a startle, then CM_FLEE
  CM_FLEE          // running out of the frame; released once it is out
};
enum CritterFlag : uint8_t {
  CF_BRAVE = 1,    // never scattered (the bear, the thief at work)
  CF_SHY = 2       // scatters whenever the hiker moves at all (stands up, walks), at any distance
};
struct CritterSlot {
  bool in_use;
  uint8_t tag;       // CritterTag
  uint8_t mode;      // CritterMode
  uint8_t flags;     // CritterFlag bits
  float speed_dial;  // this loop's personality for the slot (loop RNG)
  float jitter_dial;
  float t;           // seconds in the current mode
  float flee_x;      // CM_FLEE target, off-frame
  bool path_on;      // feet.y follows (x0,y0)-(x1,y1) while it moves (moves only change x)
  float path_x0, path_y0, path_x1, path_y1;
};

// Memory (what survives from one loop to the next) is in memory.h.

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
  Rect critter[CRITTER_MAX];
  Rect bit[BITS_MAX];
  Bit bit_s[BITS_MAX];
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
  Rect hiker_rect;   // what the hiker (and his armload) covers this tick, clipped to the frame; empty when hidden
  Rect hiker_full;   // the same, unclipped (for checks that he stays in frame)
  Tent tent;
  PackProp pack;
  FoodBag bag;
  Cairn cairn;
  Item items[ITEM_COUNT];
  Steam steam;       // the pot boiling over (dinner b)
  Skip skip;         // a skipped stone and its rings (skip_stone filler)
  Leaf leaf;         // the leaf that lands on his knee (stream_listen filler)
  Critter critters[CRITTER_MAX];
  CritterSlot crit[CRITTER_MAX];
  Rect critter_rect[CRITTER_MAX];   // this tick's bounds; empty when hidden or free
  Bit bits[BITS_MAX];               // pine cones and dust (the bear beat)
  Memory memory;     // kept across worldBeginLoop
  // A beat that owns critters is running (dinner, bear, close, a major): the
  // minors wait. Set and cleared by those beats' begin and end hooks.
  bool busy;
  MajorState major;  // the surprise major running now, and its glyphs (majors.h)
  Minors minors;     // the minor-effect layer (minors.h)
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

// Loop start: the World is cleared (Memory kept, loop_count counted), new
// place from `rng`, build state reset, ambient seeded, camp layout and props
// reset, hiker out of the scene, critter slots free with this loop's
// personality dials. The plan is set by the app.
void worldBeginLoop(World& w, Rng& rng);
// After the timeline ticked: ambient step, hiker step (rig, path, overlays,
// fire light, held props), the critter scenes and director (scatter, flee,
// release), critter step, bits, actor sort, dirty marks, cache invalidation at
// build phase boundaries.
void worldTick(World& w, Rng& rng, Renderer& r);

void worldDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood);
void worldDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal, float mood);

}  // namespace ss
