// The daytime majors' shared piece of the World: one union that holds the state of whichever of the
// kite, the mountain bike, the archery, the slackline, the ropes course or the plinking is running (day_kite.h,
// day_bike.h, day_archery.h, day_slackline.h, day_ropes.h, day_plink.h), the hooks World calls for it, and what
// the six have in common (the rain fallback, the weather gate). Only one major runs at a time, so
// the six share the bytes: the bike's water (SplashState, 1,632 bytes) alone is bigger than the
// other four together. The boomerang is NOT in the union: a boomerang stuck in the camp pine stays
// there into the next loop (Memory::boom_stuck), so another major can run while it is up there.
//
// DayState::kind says which member is live (DAY_NONE: none, and every byte zero). Each major's begin
// hook zeroes the whole thing and sets the kind; its end hook zeroes it again. Everything that reads
// a member checks the kind first. World keeps a copy in WorldDirty (day_s) the dirty marks compare
// against; a change of kind marks the old piece's bounds and the new one's.
//
// What World does for it (world.cpp):
//   stepHiker, right after hikerTick:   dayPose        the modules that drive his pose (bike, slackline
//                                                      walker, ropes) or bend it (archery)
//   worldTick, after stepHiker:         dayStep        the kite's line, the archery's bow and string,
//                                                      the slackline's critter (also while frozen)
//   worldTick, dirty marks:             dayTrack
//   sortActors / worldDrawDynamic:      dayPartY, dayPartDraw   (ACT_DAY, up to DAY_PARTS parts)
// The beat's own tick (the major's tick function in majors.cpp's table) steps the modules that must
// run before hikerTick (bikeStep, ropesStep, slackWalkerStep and slacklineStep). The beat does not
// run during the doorbell reaction, so the bike, the walker, the line and the rope props stand still
// for free; dayPose keeps the pose and dayStep(frozen) keeps the hands' things in the hands.
//
// Stage 22 (INTEGRATION_PLAN.md; Mike, 2026-11-18) adds three more members: the tai chi (DAY_TAICHI,
// day_taichi.h), the deer herd (DAY_HERD, day_herd.h: it holds the union from its beat into the return
// beat while the hoof prints lie) and the mushroom trip (DAY_TRIP, day_trip.h). The long shot runs on
// DAY_PLINK with the plinking's rifle (day_longshot.h); its far targets are DayPlink::far, all zero in the
// plinking. Each new kind's hooks are functions of its own day_*.h, called from day.cpp's switches.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "archery.h"
#include "bike.h"
#include "critters.h"
#include "herd.h"
#include "kite.h"
#include "plink.h"
#include "ropes.h"
#include "script.h"
#include "slackline.h"
#include "taichi.h"
#include "trip.h"

namespace ss {

struct World;
struct Renderer;

// Stage 22's three kinds are appended in this order (INTEGRATION_PLAN section 1): never reorder.
enum DayKind : uint8_t {
  DAY_NONE = 0, DAY_KITE, DAY_BIKE, DAY_ARCHERY, DAY_SLACK, DAY_ROPES, DAY_PLINK,
  DAY_TAICHI, DAY_HERD, DAY_TRIP
};

// The plan's variant byte for the daytime majors: the variant in the low two bits, and this bit set by
// the begin hook when a rain, snow or storm turns up that the plan could not know of (a random weather
// is drawn after the plan): the major then plays dayNoShowScript instead. Same value as KITE_NOSHOW.
constexpr uint8_t DAY_NOSHOW = 8;
constexpr uint8_t DAY_VARIANTS = 3;

struct DayBike {
  BikeState bike;
  SplashState splash;
  Rng rng;           // the water's own stream, seeded from the layout
};
struct DayArchery {
  ArcheryState a;
  Rng rng;           // the shots' own stream
};
struct DaySlack {
  Slackline line;
  SlackWalker walker;
  SlackCritter sc;
  Critter cr;        // the squirrel or chipmunk on the line: its own rig, not a director slot
  Rng rng;
  uint8_t line_on;   // the line (and its stake) is up
  uint8_t crit_on;   // the critter is on the line
  uint8_t pad[2];
};
struct DayRopes {
  RopesState r;
  Rng rng;
};
// The long shot's far targets (LONGRANGE_PLAN.md section 7; filled by day_longshot.cpp, steps B5 and
// C3). The plinking leaves all of it zero.
struct FarTarget {            // 24 bytes
  Vec2 pos;
  uint8_t kind, layer, glyph, shown;   // FT_TREE, FT_ROCK, FT_CONE, FT_POST, FT_SKY; glyph index + 1
  float flight, sw_t, sw_amp;          // seconds; the swing's clock (s since the hit, < 0 none) and size
};
// Step C3 (2026-11-19): the far side's clocks are seconds counted by the major's tick (frozen with the story
// by the doorbell), each < 0 when not running.
struct FarState {
  FarTarget tg[4];
  uint8_t n, result, target, pre;      // targets; this shot's FarResult, target and FarPre bits
  uint8_t flag_held, in_hold, wind_miss, birds_done;
  uint8_t g_flag, g_puff, g_glint, g_arcs, g_echo, g_bird[3];   // glyph index + 1, 0 none
  float flutter;                       // the ribbon's phase, turns
  float glint_t;                       // a glint showing: seconds left
  float puff_t, arcs_t, echo_t, bird_t, late_t;   // a dust puff, the ring coming back, the first report's echo,
                                                  // the far birds, C's late shot (since the report)
  float drop_t;                        // the tin falling out of its pine
  float breath0, breath1;              // the hold's breath: from this phase to that one (turns)
  float arm_sh, arm_el;                // his near arm as it was when a gesture began (the dial, the fist)
  Vec2 puff_at, echo_at;
};

struct DayPlink {
  PlinkState p;
  Rng rng;           // the shots' own stream (two values a shot) and the spill's
  FarState far;      // the long shot's (day_longshot.h); all zero in the plinking
};
// Mike, 2026-11-17: the plinking lives in the bike's place, so it adds nothing to the World.
static_assert(sizeof(DayPlink) <= sizeof(DayBike), "the plinking must fit in the bike's place");

// Stage 22's members (Mike, 2026-11-18): each in the bike's place too, so the World does not grow.
struct DayTaichi {
  TaichiState t;     // the form (taichi.h)
  // The World side (day_taichi.cpp, step C2): the guests, Copper's bump, his small glances.
  uint8_t began;     // the form has begun (the module's clock runs from the major's tick)
  uint8_t finished;  // the weight is back with the rig: the module no longer poses him
  uint8_t done;      // the form reached its end (the chapter advances in Memory)
  uint8_t guests;    // the wild guests may come (the plan's bit, and no house dog about)
  uint8_t heron;     // the stream's heron is his guest: 1 standing, 2 asked to mirror the crane
  uint8_t bump;      // Copper's bump: 0 not yet, 1 sent for, 2 playing, 3 over or skipped
  int8_t dog;        // the lent dog's critter slot, -1 none
  int8_t side;       // the way Copper pushed, +1 east
  uint8_t sat;       // Copper has sat down by him
  uint8_t fish_seen; // the jumping fish he has already glanced at
  uint8_t pad[2];
  float fold_t;      // seconds until the heron folds its wing again (0: none pending)
  float know;        // 0..1: he knows the bird is on his hand (his head lifts a little)
  float glance;      // seconds into a glance at a jumping fish (0: none)
  float bump_at;     // form seconds when the bump is called
};
// The World side's own small state (day_herd.cpp, step C4): the tent's twitch, the flap, the two meetings
// with the reeds, and whether the beat is still running or only the signs are left.
struct HerdDrive {
  uint8_t beat_on;     // the "herd" beat is running: the deer are driven
  uint8_t over;        // the beat has ended: only the flap and the prints are left (the return finds them)
  uint8_t wob_kind;    // the tent's twitch under way (HC_WOBBLE's a), 0 none
  uint8_t wob_tick;    // ticks into it
  uint8_t nib;         // the doe and the cattail: 0 not yet, 1 under way, 2 the head is bitten off
  int8_t nib_clump;    // the clump and stalk she eats from (-1: no fireside clump this loop)
  int8_t nib_stalk;
  uint8_t fawn_reed;   // the fawn and the upstream clump: 0 not yet, 1 dashing at it, 2 nose in, 3 climbed out
  int8_t reed_clump;   // that clump (-1: none in reach)
  uint8_t overrun;     // a deer was still in the scene when the beat ended (counted once)
  uint8_t pad[2];
  float door, door_to, door_rate;   // the flap: now, where it is going, units a second
  float nib_t;         // seconds since she reached for the stalk
  Vec2 nib_off;        // the stalk's bend last tick (so the next tick knows where it would stand unbent)
};
struct DayHerd {
  HerdState h;       // the deer, their cues, the prints (herd.h)
  Rng rng;           // graze lengths, flick timers, the drops: seeded at the beat's begin from the layout
  HerdDrive v;       // the World side's part
};
struct DayTrip {
  TripState t;       // what he holds and sees (trip.h)
  Rng rng;           // the fish's and the leaves' small spreads, seeded from the layout
};
static_assert(sizeof(DayTaichi) <= sizeof(DayBike), "the tai chi must fit in the bike's place");
static_assert(sizeof(DayHerd) <= sizeof(DayBike), "the herd must fit in the bike's place");
static_assert(sizeof(DayTrip) <= sizeof(DayBike), "the trip must fit in the bike's place");

struct DayState {
  uint8_t kind;        // DayKind
  uint8_t pack_was;    // the bike takes his pack off for the ride: what it was
  uint8_t pad[2];
  union {
    KiteState kite;
    DayBike bike;
    DayArchery archery;
    DaySlack slack;
    DayRopes ropes;
    DayPlink plink;
    DayTaichi taichi;
    DayHerd herd;
    DayTrip trip;
  };
};

// Parts a live piece sorts among the ground actors (ACT_DAY idx 0..DAY_PARTS-1). Three since stage 22:
// the trip needs part 2 for the vision's own drawing (MUSHROOM_PLAN 8.4); tai chi uses part 1, the herd
// part 0. WORLD_ACTOR_MAX counts all three.
#define DAY_PARTS 3

// ---- the shared script pieces ----------------------------------------------------------------------------

// A rain, snow or storm is on now: the begin hooks turn their major into the no-show.
bool dayWet(const World& w);
// He looks at the sky, holds a hand out for the first drops, shrugs and sits: the no-show's script.
const Step* dayNoShowScript(uint8_t& n);

// ---- the hooks World calls ---------------------------------------------------------------------------------

void dayPose(World& w);                      // after hikerTick (stepHiker)
// A module has his facing now (the bike's rider, the walker on the line, the ropes course's items): the
// doorbell reaction does not turn him, it only holds him still (dayPose puts his facing back after it).
bool dayHoldsFacing(const World& w);
void dayStep(World& w, bool frozen);         // after stepHiker; frozen: the doorbell reaction
void dayTrack(World& w, Renderer& r);        // dirty marks; every tick
// Sort y of part `part` of the live piece, false when it has none. Draw it.
bool dayPartY(const World& w, int part, int16_t& y);
void dayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal);
// Everything the live piece covers, the hiker aside (tests).
Rect dayBounds(const DayState& d);
// What the slackline draws besides the line: the stake at the far end (its bounds, and its draw).
Rect daySlackStakeBounds(const DaySlack& s);

}  // namespace ss
