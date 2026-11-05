// The boomerang and its eagle as a surprise major (majors.h, id MJ_BOOMERANG, name "boomerang"):
// midday only and by day. MORNING_DAYTIME_PLAN.md section 3. The flight itself is the standalone
// module in boomerang.h; this file is the scene around it, in its own file like fetch.h: the hiker's
// script, a begin, a tick and an end for the major table, and the World hooks that keep it in his
// hand and put it back in the tree.
//
// He stands by the fire pit's right side, facing LEFT, and throws it out over the camp and back
// (the module takes either side of the hand; the camp pine, the tree behind him, is on his right).
//
// Variants (the plan's major_var byte, see boomPlanVar):
//   0  A, the catch: he turns it over in his hand, winds up and throws; it swings out and back
//      and he catches it, a little surprised, and stands proud
//   1  B, the eagle: the same throw, but an eagle glides in high over the far edge, closes its
//      talons on the boomerang out in the open and climbs away with it; he stands, arm still out,
//      watching it go, and shrugs
//   2  C, it sticks: it comes back, misses him by a head, flies on over the camp and thunks into
//      the camp pine's trunk (tip in the bark, wobbling), where it stays. He walks over, tugs
//      at it twice, cannot get it out, gives up
//
// Memory. After C it stays in the pine for the rest of the loop and the next whole loop
// (Memory::boom_stuck: 2 when it sticks, one less at the end of each camp loop). At the start of
// that loop memoryBeginLoop puts it back, in the tree of that loop's landscape
// (boomRestoreStuck), and the boomerang major cannot be planned while it is up there (majorsPlan).
// Why the camp pine: it is the tree in mid-camp, on the same ground as the tent, its bare trunk
// shows under the lowest foliage, and he can walk to it. Where the stuck one is drawn: World::boom
// sorts it at the foot of the trunk (world.cpp sortActors), once the build has finished.
//
// Time and the doorbell. Everything runs on the beat's own clock: boomStep (the module's clock,
// started at boomPlan) is called from the major's tick, so the reaction (which stops the beat and
// the tick) freezes the flight, the eagle and his arm with it. boomWorldStep keeps a boomerang held
// in his hand in his hand (also during the reaction, so it goes with his waving arm).
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t BOOM_VARIANTS = 3;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t boomPlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int boomSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* boomScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void boomMajorBegin(World& w);
void boomMajorTick(World& w, float dt);
void boomMajorEnd(World& w);

// From worldTick, after the hiker: a boomerang in his hand (before the throw, after the catch)
// goes where his hand is.
void boomWorldStep(World& w);
// Loop start (memoryBeginLoop) while Memory::boom_stuck is set: the boomerang is in the camp pine,
// as it was left. Not in a Lookout loop.
void boomRestoreStuck(World& w);

}  // namespace ss
