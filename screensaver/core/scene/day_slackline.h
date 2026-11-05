// The slackline as a surprise major (majors.h, id MJ_SLACKLINE, name "slackline"): midday only, by day,
// and never in a rain, snow or storm the data says is there. MORNING_DAYTIME_PLAN.md section 5. The
// line, the walker and the critter on it are the standalone module in slackline.h; this file is the scene
// around it, in its own file like day_kite.h: the hiker's script, a begin, a tick and an end for the
// major table, and the layout.
//
// The line (a 2 px line, 26 px above the ground at both ends) is tied between the camp pine's trunk
// (its left face) and a stake 226 px to the left (the scene draws the stake: day.cpp). He walks out to
// the stake, crouches and ties it (the line and the stake are there as he comes up), walks along under
// it to a few steps from the trunk and steps on; at the end, a squirrel or a chipmunk (the layout picks)
// trots across the line while he stands under it with his hands on his hips, and he crouches at the stake
// again and unties it (it goes). The critter is the module's own rig (DaySlack::cr), not a director slot:
// nothing else here moves it, and it is gone when the line is.
//
// Variants (the plan's major_var byte, see slackPlanVar):
//   0  A, falls and tries again: three steps and a fall, back to the start, mount again, six steps and a
//      fall; the chipmunk runs to the middle, sits, and goes back
//   1  B, across: a pose in the middle (the line bounces), on to a few steps short of the stake and a soft
//      step off; the squirrel trots across both ways and sits in the middle on the way back
//
// While the walker's mode is not SW_OFF the script only waits (until, still): the module owns his feet,
// his pose and his facing there (slackline.h). The walker's step, the critter's trot and the line's spring
// run from the beat's tick (before the hiker's step); the pose is dayPose's, the critter's rig after the
// hiker is dayStep's (day.cpp).
//
// The doorbell: the beat stands still during the reaction, so the walker, the line and the critter do.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t SLACK_MAJOR_VARIANTS = 2;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t slackPlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int slackSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* slackScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void slackMajorBegin(World& w);
void slackMajorTick(World& w, float dt);
void slackMajorEnd(World& w);

}  // namespace ss
