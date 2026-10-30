// The critter director: hands out World's critter slots and moves critters
// on behalf of whoever drives them (the spine's bear beat and hooks now, the
// minor-effect scheduler next).
//
// A caller spawns a critter into a slot, tags it (World::crit[slot].tag) so it
// can tell its own critters apart, and drives it with the critter rig
// (critterSetAction) plus directorWalk for moves. Every move target goes
// through directorClamp, so a critter stays inside the frame and out of the
// reserved corner while it is visible; only directorExit and a scatter take it
// past the frame edge, and then it is released when it is out of sight.
//
// directorStep (from worldTick, after the spine) runs the hiker rule, the
// scatter and flee modes, the rig ticks, the walk paths, visibility and
// release. A scattered critter belongs to the director from then on: owners
// check directorOwns before giving it orders.
#pragma once

#include "critters.h"

namespace ss {

struct World;

// A free slot with `k` at `feet`, this loop's dials for the slot, tag CT_NONE,
// mode CM_FREE, no flags. Returns the slot, or -1 when all CRITTER_MAX are taken.
int directorSpawn(World& w, CritterKind k, Vec2 feet, int facing);
void directorRelease(World& w, int slot);
// Every critter in use within `radius` px of `from` (feet to point), not
// CF_BRAVE and not already scattered, startles and runs away from `from` out
// of the frame; it is released once out of sight.
void directorScatter(World& w, Vec2 from, float radius);
// Called each tick (directorStep does it): while the hiker walks, critters
// whose bounds come within ~80 px of his scatter; CF_SHY critters scatter at
// any move of his (walking, or getting up from sitting or lying).
void directorAvoidHiker(World& w);

// The one clamp for move targets. Keeps the critter's whole figure inside the
// frame (unless `exiting`: then x may be off either edge) and its feet out of
// the reserved corner's band: a target at or below y 417 stays left of the
// corner by the critter's half length (an exit there goes left instead).
Vec2 directorClamp(const World& w, int slot, Vec2 to, bool exiting);
// Walk (or run, hop, fly: `gait`, falling back to the kind's own) to `to`
// (clamped), feet.y following the straight line from where it is.
void directorWalk(World& w, int slot, Vec2 to, float px_per_s, CritterAction gait);
// Off the frame toward `side` (-1 left, +1 right; the corner rule may flip it)
// at `px_per_s`; released once out of sight.
void directorExit(World& w, int slot, int side, float px_per_s, CritterAction gait);

// Slot in use, still held by `tag` and not scattered.
bool directorOwns(const World& w, int slot, uint8_t tag);
int directorCount(const World& w);

// Per tick, after the spine: see the top of this file.
void directorStep(World& w);

}  // namespace ss
