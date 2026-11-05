// The mountain bike as a surprise major (majors.h, id MJ_BIKE, name "bike"): midday only, by day, and
// never in a rain, snow or storm the data says is there. MORNING_DAYTIME_PLAN.md section 6. The bike
// itself is the standalone module in bike.h; this file is the scene around it, in its own file like
// day_kite.h: the hiker's script, a begin, a tick and an end for the major table, and the course.
//
// He gets up and walks off the left edge to fetch it (the scene is empty a moment), then the bike
// comes in from the left edge with him on it; the module drives him for the whole ride (bike.h: the
// scene never walks him, poses him or moves his feet while bikeDriving).
//
// The lane (bikeCourse) runs along the clearing behind the fire pit (the dry flat, y = fire.y - 28),
// then dips into the stream at about x 465 and rides the water's upper half to the right edge, always
// above the status chip's box (x 538..796, y 412..476): the wheels touch the water at y 400 or less
// there, and the splash lands within a few px of the lane. UNVERIFIED how it looks.
//
// Variants (the plan's major_var byte, see bikePlanVar):
//   0  A, a ride through the stream and back proud: out the right edge, a moment of empty scene, back
//      the other way upright with one arm up, off the left edge; he walks in from the left to the fire
//   1  B, the rock: the front wheel meets the rock in the stream, he goes over the handlebars into the
//      water and sits there; stands up dripping, shrugs, wades to the bike lying on its side, picks it
//      up (it goes) and wades back to the fire
//   2  C, the wheelie: a wheelie on the dry flat, held, and it tips over backward; he lies there with
//      the bike on top of him and its wheels spinning down; sits up, scratches his head, stands, picks
//      the bike up (it goes) and walks back
// The bike disappears while he crouches over it (B, C) as he "picks it up": the module has no way for
// a fallen bike to leave, so it is a pop at his feet. UNVERIFIED whether Mike minds; a fade or a carry
// would be a change to bike.h.
//
// His pack is taken off for the ride (bike.h: a pack on a leaning rider reads wrong) while he is out
// of sight and put back as he walks in again; the end hook puts it back whatever the script did.
//
// The doorbell. The beat does not run during the reaction, so bikeStep does not: the bike, the water
// and the rider stand still, and dayPose (day.h) keeps him posed on it.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has
// seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t BIKE_MAJOR_VARIANTS = 3;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t bikePlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int bikeSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* bikeScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void bikeMajorBegin(World& w);
void bikeMajorTick(World& w, float dt);
void bikeMajorEnd(World& w);

}  // namespace ss
