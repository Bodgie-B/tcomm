// Reeds and cattails along the stream, and the stem in his mouth (CATTAILS_PLAN.md, as
// INTEGRATION_PLAN.md and REVIEW_FIXES.md changed it; Mike, 2026-11-18: "I would like the reeds and
// cattails to feature as a passive content that is in all daily modes from morning until night").
//
// OWNERSHIP: skeleton made by the foundation step (A1). Step B1 (the plants, Opus) fills in reeds.cpp
// and may add to this header; step C1 adds the pick, the stem and the small life. The struct below
// and the signatures World, the landscape, the weather, fire and ice, react.cpp and the app call are
// the foundation's: if one must change, ask the integrator, who changes the call site.
//
// Changes from the cattails plan already folded in here:
//   - reedsDream and Reeds::dream_q are gone; reedsWind(w, x) is a thin wrapper of ambientWindAt
//     (INTEGRATION 3.1 point 1). The mushroom sky reaches the reeds through ambientWindAt itself.
//   - Two quantized sway values per clump, leaves and heads (REVIEW_FIXES F10): sway_q[c][0] the
//     leaves, sway_q[c][1] the heads; the reeds read the heads' value times 1.3 and stalk k a fixed blend
//     of the two. d_sway likewise. pad0 and the seeds' dirty words are dropped to pay for it (recompute a
//     seed's last place from its age).
//   - The sedge fringe lives in the Landscape (REVIEW_FIXES F18): reedsInit writes Landscape::reed_fringe
//     and reedsDrawStatic reads only that, so landscape.cpp can call it with no World.
//   - sizeof(Reeds) <= 224 (INTEGRATION 3.1 point 8).
// Read REVIEW_FIXES F9/F23 (push from displacement, not from the moving flag), F11 (fireflies), F17
// (clamp the tip offset to the dirty pad) and F24 (RS_FIRESIDE's pick reed) before building.
//
// Where World calls in (world.cpp, all wired by the foundation, all no-ops until B1 fills them in):
//   worldBeginLoop, after streamInit:          reedsInit (from a copy of the loop RNG: never advance it)
//   worldTick, after streamStep (ambient):     reedsStep
//   worldTick, after weatherStep (story):      reedsStoryStep (return at once while react.active,
//                                              fi.on or visitorsHolds)
//   worldTick, after streamTrack:              reedsTrack
//   stepHiker, after weatherApplyPose:         reedsApplyPose
//   coverHiker:                                reedsMouthBounds united into hiker_rect (not hiker_full)
//   sortActors (camp), after the stream's two: reedsActorY per clump, ACT_REED idx = clump
//   worldDrawDynamic:                          ACT_REED -> reedsDraw; in ACT_HIKER after armloadDraw
//                                              reedsDrawMouth; after the actor loop, before the smoke,
//                                              reedsDrawAir
//   landscape.cpp drawLayer, BL_TUFTS:         reedsDrawStatic (after the tuft loop)
//   app.cpp appTick, beside streamSetInputs:   reedsSetInputs (camp loops only)
//   weather.cpp hikerFree, fireice.cpp fireIceCalm, react.cpp reactWaveOk:   reedsPickActive
// The Reeds struct shares the Lookout's running state with StreamLife (world.h), so it exists in camp
// loops only: a Lookout or sunrise loop never steps, tracks or draws it.
//
// Note for B1: reedsInit runs inside worldBeginLoop, before the app sets the loop's family and before
// spineBeginLoop's family layout (spineLayout's evening camp layout is what exists then). Story spots
// that move with the family layout must be read later (reedsSetInputs runs every camp tick).
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

struct World;
struct Rng;
struct Renderer;
struct Landscape;
struct BuildState;

#define REED_CLUMPS 5
#define REED_STALKS 5            // per clump: cattails first (0..n_cat-1), then reeds
enum ReedSiteId : uint8_t { RS_FORE = 0, RS_NEAR, RS_UPSTREAM, RS_FIRESIDE, RS_FAR };

// ---- the state (in World, beside StreamLife) -------------------------------------------------------

struct ReedSite {              // 10 bytes
  int16_t x, y;                // base
  uint8_t h_cat, h_reed;       // tallest cattail and the first reed, px
  uint8_t n_cat, n_reed;
  uint8_t flags;               // present, camp_bank, warm (firelight)
  uint8_t gone;                // bit per stalk: upper part taken
};
struct ReedHold {              // 6 bytes
  int8_t clump, stalk;         // -1 free
  int8_t dx, dy;               // half px
  uint8_t ttl, pad;
};
struct ReedSeed {              // 8 bytes: its place is a pure function of age
  int16_t x0, y0;
  uint16_t age;                // ticks
  uint8_t on, speed;
};
struct Reeds {
  uint32_t rng;                                  // own stream (a 32 bit generator as housedogs.cpp uses), seeded from a copy of the loop RNG
  ReedSite site[REED_CLUMPS];
  // The heads' spring (B1): the heads ride the wind through a damped spring, so they lag the leaves, smooth
  // the grass's quick flutter and swing back once after a gust; a brush, a push or a blast kicks it. In the
  // wind's own units (px of lean at an 18 px blade): position in 1/256, speed in 1/1024 per tick.
  int16_t push[REED_CLUMPS], push_v[REED_CLUMPS];
  // What the draw reads, in quarter units of the same wind: [0] the leaves (the wind at the clump as it
  // is), [1] the heads (the spring). Stalk k reads a fixed blend of the two (REVIEW_FIXES F10).
  int8_t sway_q[REED_CLUMPS][2];
  int8_t d_sway[REED_CLUMPS][2];                 // last drawn
  uint8_t d_look[REED_CLUMPS];                   // last drawn: gone, snow, warm, holds folded into a byte
  ReedHold hold[2];
  uint8_t month, season, day, dusk, snow_q, guest_near;   // inputs and small flags
  // the pick and the stem (step C1): want_pick 1 while this loop still wants one; pick the overlay's stage
  // (0 none); pick_form 1 seated, 2 the stroll, with bit 7 set once the reach failed (nothing taken);
  // chew 0 none, 1 in his hand, 2 in his mouth
  uint8_t want_pick, pick, pick_form, pick_clump, pick_stalk, chew;
  uint16_t pick_t;                               // ticks in the stage (story time)
  int16_t reach_sh, reach_el;                    // the pick's reach for the reed (near arm, milliradians)
  uint32_t pick_after, chew_until;               // storyTick deadlines
  int16_t fall_x, fall_y;                        // a dropped or tossed stem: where it left him
  uint8_t fall_t, fall_kind;                     // ticks since; 0 none, 1 dropped, 2 tossed right, 3 tossed left
  int16_t fall_g;                                // a falling stem's ground (its own: a drop during form B keeps home_y)
  int16_t home_x, home_y;                        // form B: where and how he was
  int8_t home_face;
  uint8_t home_pose, home_path;
  // small life
  uint8_t fly, fly_at;                           // dragonfly: state | darts left << 4; the stalk it sits on (clump * 8 + stalk)
  uint16_t fly_t, fly_len;
  int16_t fly_from[2], fly_to[2];
  uint32_t fly_next, seed_next;
  ReedSeed seed[3];
  // dirty tracking (the seeds' last places are recomputed from their age: F10)
  int16_t d_fly[2], d_fall[2];
  uint8_t d_ff;                                  // fireflies lit last tick (3 bits)
};
static_assert(sizeof(Reeds) <= 224, "the reeds share the Lookout's running state: INTEGRATION_PLAN 3.1 point 8");

// ---- the interface the other pieces use (CATTAILS_PLAN 6.1) -----------------------------------------

struct ReedClumpInfo {
  bool present;                  // this loop has it
  bool camp_bank;                // false: the near bank (nobody on the camp ground can reach it)
  Vec2 base;                     // root at the waterline (also its sort y)
  float half_w;                  // half width of the leaves at rest, px
  float height;                  // tallest stalk, px
  uint8_t n_cat, n_reed;
};
struct ReedStalkInfo {
  bool present;                  // false: taken this loop (the sheath stub remains)
  bool cattail;                  // false: a slender reed
  Vec2 foot, tip;                // as drawn this tick (wind, brush and holds included)
  Vec2 head;                     // middle of the seed head (cattails; the plume for reeds)
};

// Enumerate.
bool reedsClump(const World& w, int clump, ReedClumpInfo& out);
bool reedsStalk(const World& w, int clump, int stalk, ReedStalkInfo& out);
Vec2 reedsTip(const World& w, int clump, int stalk);
// The nearest present clump to p within max_d px, or -1. camp_only: skip the near bank.
int reedsNearest(const World& w, Vec2 p, float max_d, bool camp_only);
// The nearest present stalk of a clump to p (reeds_only: skip the cattails), or -1.
int reedsNearestStalk(const World& w, int clump, Vec2 p, bool reeds_only);
// Dry ground beside a camp-bank clump for feet: side -1 left of it, +1 right. False for the near bank.
bool reedsStandPoint(const World& w, int clump, int side, Vec2& feet);
// Everything a clump can draw (its fixed dirty box).
Rect reedsBounds(const World& w, int clump);
// Antialiased primitives the clump draws this frame (0 when not shown): for the budget reports and tests.
int reedsPrimitives(const World& w, int clump);

// Touch. All safe to call from a script action, a major's tick or a minor's step.
void reedsPush(World& w, int clump, float dir, float strength);    // a body through it: dir -1/+1, strength 0..1
void reedsBlast(World& w, Vec2 at, float radius, float strength);  // every clump within radius leans away, then rings
void reedsBend(World& w, int clump, int stalk, Vec2 tip_offset);   // hold one stalk bent; call every tick, lets go by itself
bool reedsTake(World& w, int clump, int stalk, bool allow_cattail = false);   // bite or pick off its upper part

// The wind as the reeds show it now: signed px of lean at an 18 px blade (negative: leaning left). A
// thin wrapper of ambientWindAt (ambient.h), so grass, reeds and (through ambientWindAt) the mushroom
// sky's slow wave are one field.
float reedsWind(const World& w, float x);

// The stem in his mouth.
bool reedsChewing(const World& w);
void reedsGiveChew(World& w, float seconds);    // straight into his mouth (a script that staged its own pick)
void reedsDropChew(World& w);                   // it falls from his mouth now
// The pick overlay has him this tick (others wait, as they wait for the hat gag).
bool reedsPickActive(const World& w);

// The red-winged blackbird (minors.cpp, MN_REED_BIRD; step C1): the clump it may land on now, RS_NEAR or
// RS_UPSTREAM, whichever is farther from him, with its tallest cattail still whole; -1 when none may (night,
// rain, snow or storm, the wrong month, he is within 130 px, neither clump here).
int reedsBirdPerch(const World& w);
// Its weight on the clump's tallest cattail this tick: the tip held (ox, oy) px from where the wind has it.
// Call every tick; let go, the stalk rings back by itself.
void reedsBirdHold(World& w, int clump, float ox, float oy);

// ---- what World, the landscape and the app call (wired by the foundation) ---------------------------

void reedsInit(World& w, const Rng& loop_rng);
void reedsSetInputs(World& w, float mood, bool night, uint8_t month);
void reedsStep(World& w);                       // ambient: wind, springs, holds, small life, brush
void reedsStoryStep(World& w);                  // story: the pick overlay and the stem's ending
void reedsApplyPose(const World& w, HikerPose& p);
void reedsTrack(World& w, Renderer& r);
bool reedsActorY(const World& w, int clump, int16_t& y);
void reedsDraw(const World& w, int clump, Surface& s, const Rect& clip, const Palette& pal);
// The sedge fringe into the static cache: reads only Landscape::reed_fringe (written by reedsInit) and
// applies buildPlantScale(b, ...) per spike itself.
void reedsDrawStatic(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, const BuildState& b);
void reedsDrawAir(const World& w, Surface& s, const Rect& clip, const Palette& pal);     // dragonfly, fireflies, seeds, a falling stem
void reedsDrawMouth(const World& w, Surface& s, const Rect& clip, const Palette& pal);   // the stem in his mouth or hand
Rect reedsMouthBounds(const World& w);

}  // namespace ss
