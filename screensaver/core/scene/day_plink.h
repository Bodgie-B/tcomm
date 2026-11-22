// The plinking as a surprise major (majors.h, id MJ_PLINK, name "plink"): midday only, by day, never in a
// rain, snow or storm the data says is there and never while a house dog is out (Mike, 2026-11-17: no
// shooting with Lucy or Copper about). The cans, the rifle and the shots are the standalone module in
// plink.h; this file is the scene around it, in its own file like day_archery.h: the hiker's script, a
// begin, a tick and an end for the major table.
//
// The story (every variant): he takes a stick from the wood pile and a sack of cans from his pack, walks
// under the food bag's branch and lifts six cans of five sizes onto it with the stick, one by one, along
// its full length. He lays the stick and the sack down, takes a takedown .22 out of the pack in two
// halves, joins them, walks to the lower left corner at the end of the water bank, kneels (the steady
// position, and it keeps the muzzle under the tent in the picture), loads, shoulders and shoots. Then he
// unloads (he looks into the action) before he stands, takes the rifle apart into the pack, picks up the
// stick and the sack, collects every can (and the food), puts the stick on the wood pile, the sack in the
// pack and sits again. No sound anywhere.
//
// Variants (the plan's major_var byte, see plinkPlanVar):
//   0  A, the clean run: six shots, six cans down. The third can only hops, lands back on the branch,
//      teeters and topples by itself.
//   1  B, the one that will not fall: five cans down, then three shots that only chip the bark round the
//      last (smallest) can and rock it; he checks his sights, shrugs, and in the clean-up pokes it off
//      with the stick (it bounces off his hat).
//   2  C, the food bag: a shot cuts the bag's line; it drops and the food spills out; he scratches his
//      head, shoots the last can, and in the clean-up gathers the food, stuffs the bag and tosses it back
//      over the branch.
//
// Wildlife: at the first shot two birds burst from the camp pine and a squirrel dashes off. The house dogs
// keep away while it plays (housedogs.cpp asks plinkHoldsDogs); a dog already in camp trots off.
//
// Scratch use of MajorState, private to this file:
//   p[0] the firing spot   p[1] where the stick and sack lie   p[2] B: under the stubborn can, C: left of
//   the bag   p[3] the moving mark   count: the can being placed   stage[0]: the pair being picked up
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED until Mike has seen it
// (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;

constexpr uint8_t PLINK_MAJOR_VARIANTS = 3;

// The plan's byte from the plan's own random draw `r` (already taken). Pure.
uint8_t plinkPlanVar(uint32_t r);
// Seconds the beat lasts for a variant byte.
int plinkSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* plinkScript(uint8_t var, uint8_t& n);
// Beat begin, tick and end (majors.cpp: majorBegin, majorUpdate, majorEnd).
void plinkMajorBegin(World& w);
void plinkMajorTick(World& w, float dt);
void plinkMajorEnd(World& w);

// The plinking is running (the house dogs stay out; housedogs.cpp).
bool plinkHoldsDogs(const World& w);
// The rifle is in his hands in some form (the doorbell only makes him look round), or anything is.
bool plinkRifleInHands(const World& w);
bool plinkHandsFullNow(const World& w);

// Shared with the long shot (day_longshot.h; LONGRANGE_PLAN section 3, Mike 2026-11-18), which runs on
// DAY_PLINK with the same rifle. Script actions (sc::act), behaviour unchanged from the plinking's: the two
// halves out of the pack (and the stick and the sack laid at MajorState::p[1] when they are in his hands),
// the halves joined, the magazine in and a round chambered, the magazine out and the action looked into,
// the rifle apart and into the pack (and the stick and the sack back in his hands when they lie there).
void plinkActRifleOut(World& w, float u);
void plinkActAssemble(World& w, float u);
void plinkActLoad(World& w, float u);
void plinkActUnload(World& w, float u);
void plinkActStow(World& w, float u);
// The first shot's camp startle: two birds out of the camp pine, a squirrel off to the right, and the
// director scatters the rest from the muzzle. Critters it gets go into MajorState::c[0..2].
void plinkStartle(World& w);

}  // namespace ss
