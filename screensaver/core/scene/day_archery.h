// The archery as a surprise major (majors.h, id MJ_ARCHERY, name "archery"): midday only, by day, and
// never in a rain, snow or storm the data says is there. MORNING_DAYTIME_PLAN.md section 7. The target,
// the bow and the arrows are the standalone module in archery.h; this file is the scene around it, in
// its own file like day_kite.h: the hiker's script, a begin, a tick and an end for the major table.
//
// The module only runs 9 to 23 s of shooting, so the scene builds the rest: he gets up with the bow
// and walks out to the target spot, crouches and plants the stand (it appears), walks back to his mark
// (the archer's mark is 175 to 215 px from the target, 150 or more as asked) and checks the bow; shoots
// each shot on its own with a still moment between; then walks out, crouches and pulls the arrows and
// takes the stand down (variant B: on to the camp pine for the arrow in the trunk) and goes back to sit.
//
// Variants (the plan's major_var byte, see archeryPlanVar):
//   0  A, two misses and a bullseye: short of the target into the ground, past it (and on), then the
//      bullseye and the proud pose
//   1  B, into the tree: one arrow sails over the target and sticks in the camp pine's trunk, for good;
//      he shrugs, walks out, takes the target down and goes on to the pine and pulls it out
//   2  C, the split arrow: a bullseye, then a second that lands on it and splits its shaft
//
// The layout (loop start, from the fire pit and the camp pine): the lane y = fire.y - 26, behind the
// fire pit; his mark 40 px right of the pit's x; the stand 74 px left of the camp pine's trunk; the
// pine's trunk face at y 330 is where SHOT_TREE sticks. Everything is above y 412 and left of x 538.
//
// The bow in his near hand follows his hand (archeryStep after the hiker's step, every tick, also
// frozen with dt 0 during the doorbell reaction); archeryPose bends his pose (dayPose); neither runs a
// script pose, so the script never poses him while a shot is on (until archeryDone). Each shot is drawn
// from the effect's own Rng (DayArchery::rng, seeded from the layout), three values a shot.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t ARCHERY_MAJOR_VARIANTS = 3;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t archeryPlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int archerySeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* archeryScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void archeryMajorBegin(World& w);
void archeryMajorTick(World& w, float dt);
void archeryMajorEnd(World& w);

}  // namespace ss
