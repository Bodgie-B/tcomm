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
#include "boomerang.h"
#include "buildup.h"
#include "critters.h"
#include "day.h"
#include "fireice.h"
#include "housedogs.h"
#include "mailbird.h"
#include "hiker.h"
#include "kite.h"
#include "landscape.h"
#include "lookout.h"
#include "lookout_land.h"
#include "props.h"
#include "reeds.h"
#include "spine.h"
#include "stream.h"
#include "sunrise.h"
#include "visitors.h"
#include "minors.h"
#include "majors.h"
#include "memory.h"
#include "weather.h"
#include "xmas.h"

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
  float ash;         // 0..1 last night's ash, a low grey mound in the pit (the morning); 0 none
};

// Ground actors are drawn far to near by base y. FGPINE is a foreground pine
// drawn again over an actor that walks behind it, only inside that actor's
// bounds: idx bit 0 is the pine, idx >> 1 the actor (0 the hiker, 1 + slot a
// critter).
enum ActorKind : uint8_t {
  ACT_FIRE = 0, ACT_TUFT, ACT_HIKER, ACT_TENT, ACT_PACK, ACT_BAG, ACT_CAIRN, ACT_ITEM, ACT_FGPINE, ACT_LEAF,
  ACT_CRITTER, ACT_BIT, ACT_MAJOR, ACT_WXBIT,
  // The daytime majors' flyers (day_kite.h, day_boomerang.h): the kite with its line, the boomerang
  // (flying, in his hand, or stuck in the camp pine) and the eagle. Sorted just after the hiker.
  ACT_KITE, ACT_BOOM, ACT_EAGLE,
  // The bike, the archery, the slackline or the ropes course (day.h): idx is the part (DAY_PARTS),
  // each sorted at the y dayPartY gives.
  ACT_DAY,
  // Fire and ice (fireice.h): idx a small fire, or the set-down bucket. The mail bird's letter on the
  // ground (mailbird.h).
  ACT_FX, ACT_MAIL,
  // The visiting couple (visitors.h): idx 0, 1 Ann and Ben, 2, 3 their packs, 4 the disc, 5, 6 the telescopes.
  ACT_VISITOR,
  // The boulder in the current (stream.h), sorted at its waterline: the rock over what passed behind it,
  // a snagged floater, the foam.
  ACT_STREAM,
  // A clump of reeds and cattails (reeds.h): idx the clump (ReedSiteId), sorted at its root on the waterline.
  ACT_REED,
#if SS_XMAS
  // The Christmas mockups (xmas.h; only while its mock is posed): idx the piece. Added last, after the
  // glyphs, so a full list drops them and nothing of the scene's (WORLD_ACTOR_MAX is not raised for them).
  ACT_XMAS
#endif
};
struct ActorRef {
  uint8_t kind;
  uint8_t idx;
  int16_t base_y;
};
// Fire 1, tufts 14, hiker 1 + 2 pines, props 4 + 10 items, leaf 1 = 33;
// critters 8 (+ 2 pine redraws each near the left edge), bits 6. sortActors
// stops adding at the cap (tufts are added before the critters, so a full
// list drops critters at the far left first, never the hiker or props).
// A major's glyphs (up to GLYPH_MAX 28: kept stars and fireflies) come last;
// 88 holds them all; the weather's bits (WX_BITS 4) on top; the kite, the
// boomerang and the eagle (3) on top of those: the stuck boomerang stays in
// the camp pine into the next loop, so it can meet the evening's 28 glyphs.
#define WORLD_ACTOR_MAX 136   // the visiting couple's 7 and their pine redraws (they never meet fire and ice's 7); 98 and the morning stay's four items (pan, cake, brush, sack), and the day pieces' second part (day.h);
                              // Lucy and Copper (housedogs.h) 2 more critters with their pine redraws
                              // the mail bird with its pine redraws and its letter (4), fire and ice's 7 (FI_ACTORS)
                              // the boulder in the current and the heron (2, stream.h)
                              // stage 22 (Mike, 2026-11-18): the five reed clumps (REED_CLUMPS, reeds.h) and the
                              // day pieces' third part (DAY_PARTS 3, day.h): 130 + 6

// Critters in the scene: slots the director (director.h) hands out. The story's
// critters get the first CRITTER_STORY; the last two belong to Lucy and Copper
// (housedogs.h, slot CRITTER_STORY + dog), so the house dogs never take a slot
// the story wanted and a loop with no dog out is what it was. After them, the mail bird's
// (mailbird.h, slot CRITTER_MAIL), on the same terms.
#define CRITTER_STORY 8
#define CRITTER_MAIL 10
#define CRITTER_MAX 11

// Who holds a critter slot, and how the director treats it.
enum CritterTag : uint8_t { CT_NONE = 0, CT_DINNER, CT_BEAR, CT_THIEF, CT_MINOR, CT_MAJOR, CT_HOUSEDOG, CT_MAIL,
                            CT_STREAM,     // CT_STREAM: the stream's visiting deer (stream.h)
                            CT_HERD };     // CT_HERD: the deer of the midday visit while he is away (day_herd.h)
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
  int16_t sort_y;    // nonzero: sorts among the ground actors at this base y (a bird on the tent's ridge)
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
  float stomp;                     // 0..1 a cross, stamping walk: arms swung high, head down (fishing_small)
  // In the stream: with `wade` on, World works out the waterline on him each
  // tick from how far into the water his feet are (shin deep standing in the
  // middle, belly deep sitting there) and redraws the water over him below
  // it (wet_y; 0 when he is dry or `wade` is off).
  bool wade;
  float wet_y;
};

// The hiker's doorbell reaction (react.h). While `active` the story holds
// still; his facing and pose offsets go back as they were when it ends.
struct DoorbellReact {
  bool active;
  bool hands_full;   // carrying something: a hop and a look, no wave
  bool look_only;    // the bow up (archery): he only looks round
  bool wave;         // a passer-by at the upstairs panel: just a wave to the viewer, no jump or point
  int facing;        // his facing at the bell
  int ticks;         // since the bell
  HikerFx fx;        // his pose offsets at the bell
};

// Previous bounds per element, so a move dirties both where it was and where it is.
struct WorldDirty {
  Rect cloud[2];
  Rect high[CLOUD_HIGH_COUNT];   // the high clouds of cover 2-3 (landscape.h cloudHigh); empty when not shown
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
  Rect steam, skip, leaf, rustle;
  Steam steam_s;
  Skip skip_s;
  Leaf leaf_s;
  Rustle rustle_s;
  // The daytime majors' union as it was last tick (dayTrack marks what changed between it and the
  // live one: the kite's kiteDirty, the bike's bikeDirty and splashDirty, and so on), the
  // boomerang and the eagle (their bounds, while drawn; boomerangSame, eagleSame).
  DayState day_s;
  Boomerang boom_s;
  Eagle eagle_s;
  Rect boom, eagle;
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
  float fire_ash;
};

struct World {
  Landscape land;
  // The set pieces' own landscapes (only their loops generate and draw them) and the visiting couple
  // (only a camp loop has them): they never meet in one loop, so they share one block. The Lookout
  // (lookout.h) keeps its state beside it.
  union {
    LookoutLand lk;
    Sunrise sr;       // the sunrise overlook (sunrise.h)
    Visitors vis;     // the visiting couple (visitors.h)
  };
  // The Lookout's running state, and the camp's own that a Lookout or sunrise loop never touches: the
  // Christmas mockups (xmas.h) and the stream's life (stream.h). A camp loop never touches the Lookout's,
  // a set-piece loop never steps, tracks or draws these, so they share the bytes. worldBeginLoop zeroes
  // the World and streamInit seeds the stream; a Lookout loop then resets `look` whole
  // (lookoutBeginLoop), and the app gives the stream its inputs in camp loops only.
  union {
    LookoutState look;
    struct {
#if SS_XMAS
      XmasState xmas;   // only in the host builds (xmas_switch.h): the panel's World has no room for it
#endif
      StreamLife stream;   // the boulder's foam and wake, things drifting by, fish, the visitors (stream.h)
      Reeds reeds;         // the reeds and cattails along it and the stem in his mouth (reeds.h); camp loops only
    };
  };
  Cloud clouds[2];
  BuildState build;
  Ambient amb;
  FirePit fire;
  Hiker hiker;
  HikerFx fx;
  DoorbellReact react;
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
  Rustle rustle;     // the big pine shaking where he went in behind it (the morning stay's pee)
  Critter critters[CRITTER_MAX];
  CritterSlot crit[CRITTER_MAX];
  Rect critter_rect[CRITTER_MAX];   // this tick's bounds; empty when hidden or free
  Bit bits[BITS_MAX];               // pine cones and dust (the bear beat)
  Memory memory;     // kept across worldBeginLoop
  // A beat that owns critters is running (dinner, bear, close, a major): the
  // minors wait. Set and cleared by those beats' begin and end hooks.
  bool busy;
  // The cloud cover in eighths the cache was drawn with (App::sky.cover, landscape.h SkyDraw), set by
  // the app at loop start and before every worldTick: cover 2-3 brings the three drifting high clouds
  // (cloudHigh), which worldTick tracks and worldDrawDynamic draws. 0 in the Lookout and the sunrise.
  uint8_t sky_cover;
  MajorState major;  // the surprise major running now, and its glyphs (majors.h)
  // The daytime majors' own pieces. day: the kite, the bike, the archery, the slackline or the ropes
  // course, whichever runs (a union: day.h), none (DAY_NONE, all zero) otherwise. boom: the boomerang
  // (day_boomerang.h), hidden unless its major runs but for the one stuck in the camp pine
  // (Memory::boom_stuck puts it back at loop start), so it is not in the union.
  DayState day;
  BoomFlight boom;
  Minors minors;     // the minor-effect layer (minors.h)
  HouseDogs dogs;    // Lucy and Copper while they are out (housedogs.h)
  FireIce fi;        // the tenant's too hot / too cold played out (fireice.h)
  MailBird mail;     // the downstairs mail's bird (mailbird.h)
  // Whole-frame fade to ACTOR (near black): 0 none, 1 black. The close ends
  // on 1; the build beat brings the next loop up from it.
  float fade;
  // Fog the scene adds to the weather's (0..1; the app adds it in
  // computeAppPalette, clamped). The morning opens in it and lifts it in 0.05
  // steps, each a palette change and a cache rebuild.
  float fog_extra;
  // The palette's mood when the scene sets it (>= 0), instead of the host's
  // Inputs::mood (app.cpp computeAppPalette): the Lookout's sunset steps it.
  // -1 from worldBeginLoop: the host's.
  float mood_override;
  // This loop's weather (weather.h): weatherPlan at loop start (app.cpp).
  Weather weather;
  LoopPlan plan;
  SpineState spine;
  int n_actors;
  ActorRef actors[WORLD_ACTOR_MAX];
  WorldDirty dirty;
  uint32_t tick;     // ticks since loop start
  uint32_t frozen;   // ticks of this loop the doorbell reaction held the story (react.h)
};

// The shared block costs no more than the Lookout's landscape did.
static_assert(sizeof(Sunrise) <= sizeof(LookoutLand), "the sunrise must fit in the Lookout's place");
static_assert(sizeof(Visitors) <= sizeof(LookoutLand), "the visitors must fit in the Lookout's place");
#if SS_XMAS
static_assert(sizeof(XmasState) + sizeof(StreamLife) + sizeof(Reeds) <= sizeof(LookoutState),
              "the Christmas mockups, the stream's life and the reeds must fit in the Lookout's running state");
#else
static_assert(sizeof(StreamLife) + sizeof(Reeds) <= sizeof(LookoutState),
              "the stream's life and the reeds must fit in the Lookout's running state");
#endif

// The story's clock: ticks since loop start, less the doorbell freezes. Every
// deadline the story keeps (the minors' gaps, the weather's gags and gusts, the
// morning's fog lift) runs on this, so a bell delays them instead of using them
// up; the ambient animation (rain, the tent's breathing) runs on World::tick.
inline uint32_t storyTick(const World& w) { return w.tick - w.frozen; }

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
// The one way to get a World::bits slot (the bear, the majors and the minors
// all use it, never a fixed index): a free one, else the one that has lain
// longest (a resting cone or leaf). The slot is cleared and given `kind`.
// -1 when every slot is busy (falling, flying, spreading).
int worldTakeBit(World& w, uint8_t kind);

// The stream's water body (as the landscape draws it into the cache) over
// whatever is inside `clip`: the hiker below his waterline, a bird sitting
// low on the water (a major's GL_WATER glyph).
void worldDrawWater(const World& w, Surface& s, const Rect& clip, const Palette& pal);

// `sky`: the real sun and moon and the cloud cover (landscape.h SkyDraw); null is the old sky.
void worldDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood, const SkyDraw* sky = nullptr);
void worldDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal, float mood,
                      const SkyDraw* sky = nullptr);

}  // namespace ss
