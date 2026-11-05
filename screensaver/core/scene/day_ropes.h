// The ropes course as a surprise major (majors.h, id MJ_ROPES, name "ropes"): midday only, by day, and
// never in a rain, snow or storm the data says is there. MORNING_DAYTIME_PLAN.md section 8, the largest
// piece. The posts, the bridge, the tyre swing and the cargo net, and his walk from item to item, are the
// standalone module in ropes.h; this file is the scene around it, in its own file like day_kite.h: the
// hiker's script, a begin, a tick and an end for the major table, and the layout.
//
// The course stands at the back of the clearing: its ground line is y = camp pine's foot - 6 (about 334,
// well under the 385 the chip's corner needs), its left foot just right of the tent (tent x + 28), so its
// 301 px end before the camp pine's trunk. He walks round behind the tent to a spot 40 px left of the
// ramp (the module's start), the posts stand up, and then the module has him: it walks him, poses him,
// swings him and drops him through the public rig calls (ropes.h), and the script only waits for
// ropesDone, with a look round while the posts go up. At the end the posts come down and he walks back to
// the fire.
//
// Variants (the plan's major_var byte, see ropesPlanVar; the module's own letters):
//   0  A, all three: the bridge, the tyre swing and the cargo net with the mishap (about 85 s in the
//      module). Drawn a tenth of the time: with the walks it is the one beat over 90 s (about 105).
//   1  B, the bridge and the swing (about 62 s)
//   2  C, the swing and the net with the mishap (about 76 s)
// The seed (the layout) changes the wobble, the hesitations, the spins and which rung catches his foot.
//
// The doorbell: the beat stands still during the reaction, so ropesStep does; dayPose keeps ropesPose's
// overlay on him.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t ROPES_MAJOR_VARIANTS = 3;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t ropesPlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int ropesSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* ropesScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void ropesMajorBegin(World& w);
void ropesMajorTick(World& w, float dt);
void ropesMajorEnd(World& w);

}  // namespace ss
