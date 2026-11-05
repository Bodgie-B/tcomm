// The camp dog and a game of fetch: a surprise major (majors.h, id MJ_FETCH,
// name "fetch"), allowed in the midday family only. MORNING_DAYTIME_PLAN.md
// section 1.
//
// The beat is the usual three things (see majors.cpp): a begin, a tick and a
// script for the hiker. They live here, in their own file, and the pool row in
// majors.cpp points at them. The dog is the CR_DOG critter (critters.h), a
// CT_MAJOR critter the director drives (CF_BRAVE: the hiker's walking never
// scatters it). Everything it does runs from the beat's tick and the script, so
// the doorbell reaction (which stops both, and the director) freezes the whole
// scene with them; no clock here is w.tick, only the beat's own (MajorState::t
// and st[]), so a freeze delays the scene instead of using it up.
//
// Variants (the plan's major_var byte, see fetchPlanVar):
//   0  the good fetch: the stick arcs right, the dog races after it, catches it
//      on the bounce and trots back proud; two or three throws, the last one
//      longer; the dog ends lying down beside him
//   1  into the stream: the stick lands in the water; the dog skids at the
//      bank, looks back at him, wades in, comes out and shakes off next to him
//      (spray; he flinches and wipes his face)
//   2  the wrong stick: the dog runs off right with the stick and comes back
//      dragging a branch three times its size; he shrugs and they both sit
// Dogs out (Inputs::dog_out nonzero): majorsPlan puts a fetch in every midday
// loop (FETCH_FORCED); with both dogs out (3) a second, smaller dog in a darker
// brown (Critter::tone 1, scale 0.8) trots in after the first and shadows it.
//
// The stick: in the dog's mouth it is the rig's own (CC_STICK, critters.h); in
// his hand the rig's CARRY_ROD with rod_stick set (so a doorbell finds him
// carrying, and he does not wave with it); on the ground and in the air a
// glyph (SH_STICK, majors.h) that this file moves each tick. Spray and rings
// are GL_DOT and GL_RING glyphs, the dog's wading a GL_WATER patch over its legs.
//
// Tuning marked PROPOSED is a guess; anything about how it looks is UNVERIFIED
// until Mike has seen it (and on the panel).
#pragma once

#include "script.h"

namespace ss {

struct World;
struct Glyph;

// The variant byte. Low two bits: the variant above. The rest are flags.
constexpr uint8_t FETCH_VARIANTS = 3;
constexpr uint8_t FETCH_SECOND = 4;    // a second dog (dog_out was 3)
constexpr uint8_t FETCH_FORCED = 8;    // planned because a dog is out: the length fit never drops it
constexpr uint8_t FETCH_THREE = 16;    // variant 0: three throws instead of two

// The plan's byte from the plan's own random draw `r` (already taken), the
// inputs' dog_out (0 none, 1 Lucy, 2 Copper, 3 both) and whether the house
// forced it. Pure: no random draw here.
uint8_t fetchPlanVar(uint32_t r, int dog_out, bool forced);
// Seconds the beat lasts for a variant byte.
int fetchSeconds(uint8_t var);
// The hiker's script for a variant byte.
const Step* fetchScript(uint8_t var, uint8_t& n);
// Beat begin (his throw spot, the dog's places, no glyphs yet) and tick.
void fetchBegin(World& w);
void fetchTick(World& w, float dt);

// The stick glyph (ShapeId SH_STICK): pos its centre, rot turns, scale its
// length (1: a 24 px stick). majors.cpp's glyph code calls these.
float fetchStickReach();
void fetchStickDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
