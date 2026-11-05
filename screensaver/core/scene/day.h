// The daytime majors' shared piece of the World: one union that holds the state of whichever of the
// kite, the mountain bike, the archery, the slackline or the ropes course is running (day_kite.h,
// day_bike.h, day_archery.h, day_slackline.h, day_ropes.h), the hooks World calls for it, and what
// the five have in common (the rain fallback, the weather gate). Only one major runs at a time, so
// the five share the bytes: the bike's water (SplashState, 1,632 bytes) alone is bigger than the
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
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"
#include "archery.h"
#include "bike.h"
#include "critters.h"
#include "kite.h"
#include "ropes.h"
#include "script.h"
#include "slackline.h"

namespace ss {

struct World;
struct Renderer;

enum DayKind : uint8_t { DAY_NONE = 0, DAY_KITE, DAY_BIKE, DAY_ARCHERY, DAY_SLACK, DAY_ROPES };

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
  };
};

// Parts a live piece sorts among the ground actors (ACT_DAY idx 0..DAY_PARTS-1).
#define DAY_PARTS 2

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
