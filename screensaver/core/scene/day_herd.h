// The deer visit while the hiker is away: the World side (DEER_PLAN.md sections 3 to 7.3, as
// INTEGRATION_PLAN.md changed them; Mike, 2026-11-18). Not a major: a midday timeline beat "herd" after
// the build, picked by middayPlan from a private stream (plan token " herd=N").
//
// OWNERSHIP: skeleton made by the foundation step (A1); herdPlanPick by step C0; the rest by step C4
// (2026-11-19): the beat's three hooks, the orders of the pure part (herd.h, step B3) carried out on the
// director and the critters, the triggers, the signs he finds, and the two meetings with the reeds
// (INTEGRATION_PLAN 4.1 the second doe bites the head off the fireside cattail, 4.2 the fawn's third dash
// ends nose first in the upstream clump). REVIEW_FIXES F12 (the way out), F21 (the no-show) and F25 (fire
// and ice while only the prints lie) are applied here.
//
// Where it is wired:
//   spine_midday.cpp middayBuildTimeline:       the beat "herd" after the build, when LoopPlan::herd is not 0
//   spine_midday.cpp busySeconds, beatSeconds:  herdSeconds and herdReturnExtra (the fit counts them)
//   spine_midday.cpp the three return scripts:  herdSigns, herdFlapAjar and the acts below
//   app.cpp appTick, in the EV_DOORBELL loop:   herdBell (the doorbell is a hard alarm)
//   housedogs.cpp maybeChase:                   herdGuestAt (a dog let out chases the last deer)
//   fireice.cpp fireIceCalm:                    herdOnlySigns (F25)
//   day.cpp, case DAY_HERD:                     the herdDay* hooks below
#pragma once

#include <stdint.h>

#include "../gfx/surface.h"
#include "../palette.h"
#include "day.h"

namespace ss {

struct World;
struct Renderer;
struct Rng;
struct Inputs;
struct Memory;
struct SceneCtx;

// ---- the plan (step C0; middayPlan) ----

// LoopPlan::herd's packing (DEER_PLAN 4): bits 0-1 the variant (1 A, 2 B, 3 C), bits 2-3 the herd's size
// (0 five, 1 six, 2 four: no D4). 0: no visit.
inline uint8_t herdVariant(uint8_t herd) { return (uint8_t)(herd & 3u); }
inline uint8_t herdSize(uint8_t herd) { return (uint8_t)((herd >> 2) & 3u); }
// Share of midday loops that pass the gates which the deer visit (DEER_PLAN 5, PROPOSED; INTEGRATION 5.6).
constexpr float kHerdShare = 0.30f;
// The deer visit's pick, the first thing middayPlan does (DEER_PLAN 5, INTEGRATION_PLAN 3.1 point 10 and
// 4.8). Its own stream, seeded from the loop rng's state without drawing from it, so the loop rng and
// every plan without a herd are what they were; always three draws (the share, the variant, the size).
// Then the gates drop it: a house dog out, the data's rain or storm (snow is fine), a herd in the last
// two midday loops (Memory::herd_wait, counted down here and set to 2 by a visit), or the mushroom major
// playing this loop (`mushroom_plays`, majorsMushroomPlays). Returns LoopPlan::herd. Returns 0 and
// touches nothing while the herd does not plan (majorsS22Plans: SS_S22 off, or step C4 not landed).
uint8_t herdPlanPick(const Rng& loop_rng, const Inputs& in, Memory& mem, bool mushroom_plays);

// ---- the beat (step C4; middayBuildTimeline) ----

// The beat's whole seconds (kHerdSeconds), 0 for no visit; and the seconds the return beat is longer in a
// herd loop (he stops at the ford and looks; in variants a and c he also goes to the door, zips the flap
// and looks at the prints): 4 for return b, 16 for a and c, 0 with no visit.
int herdSeconds(uint8_t herd);
int herdReturnExtra(uint8_t herd, int return_variant);
// The timeline beat "herd". Begin: the union is zeroed and, unless a house dog is wanted out already (the
// no-show, REVIEW_FIXES F21: then no deer come and the union stays empty), set up as DAY_HERD. Update: one
// herdTick and its orders. End: a deer still in the scene counts an overrun and is sent off; the union is
// kept only while the flap is ajar or prints lie.
void herdBeatBegin(SceneCtx& ctx);
void herdBeatUpdate(SceneCtx& ctx, float t, float dt);
void herdBeatEnd(SceneCtx& ctx);

// A doorbell rang: if the herd is in camp it is a hard alarm (they leave to the left).
void herdBell(World& w);
// The feet of the last deer still well inside the frame (the house dog's chase); false when none is.
bool herdGuestAt(const World& w, Vec2& out);
// The beat is over and only the signs are left (the flap, the prints): fire and ice and the visiting
// couple may start over them (REVIEW_FIXES F25; DEER_PLAN 6.2: the fade just waits meanwhile).
bool herdOnlySigns(const World& w);
// The herd's deer in the scene now (tests and stills): fills slots[0..HERD_MAX) with critter slots, -1
// for a deer not in; returns how many are in.
int herdDeerSlots(const World& w, int8_t slots[HERD_MAX]);

// ---- what he finds (the return scripts, spine_midday.cpp) ----
bool herdSigns(const World& w);       // the visit left a sign he has not looked at yet
bool herdFlapAjar(const World& w);    // the tent's flap is the sign (he goes and zips it)
void herdActLookDown(World& w, float u);   // his head tips down to the ground in front of him, and back
void herdActZip(World& w, float u);        // crouched at the door: the flap back to shut
void herdDoLooked(World& w, float);        // at the ford: with no flap to see to, the prints' fade starts
void herdDoSeen(World& w, float);          // the prints' fade starts

// ---- the day hooks (day.cpp, case DAY_HERD) ----

Rect herdDayBounds(const DayHerd& d);
void herdDayPose(World& w);
bool herdDayHoldsFacing(const World& w);
void herdDayStep(World& w, bool frozen);            // ages the prints; zeroes the union when they are gone
void herdDayTrack(const DayHerd& prev, const DayHerd& cur, const World& w, Renderer& r);
bool herdDayPartY(const World& w, int part, int16_t& y);
void herdDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
