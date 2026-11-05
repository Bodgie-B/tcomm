// World memory: what survives from one loop to the next (brief section 13,
// handoff "variety levers"). Lives in World::memory; worldBeginLoop copies it
// across the clear, memoryBeginLoop applies it to the new loop, and
// memoryEndLoop records what this loop leaves behind.
//
// | Field          | Effect next loop                                                     |
// |----------------|----------------------------------------------------------------------|
// | cairn_n        | the cairn stands from loop start, one stone shorter; the filler adds |
// | shoe_taken     | the raccoon (raccoon_pack major) brings the shoe back first; no shoe |
// |                | is left at the tent door (the night thief takes the mug or book)     |
// | shoe_back      | (this loop only) it came back: he keeps it in the tent tonight       |
// | last_stolen    | the last thing a raccoon took (shoe, mug or book), ItemId + 1        |
// | mouse_hole     | the tent has a bite out of its base until he patches it in setup     |
// | rod_lost       | the rod went across (or down) the stream: the next fishing major     |
// |                | uses a stick off the wood pile, and clears the flag                  |
// | boom_stuck     | the boomerang (boomerang major, variant C) is stuck in the camp     |
// |                | pine: 2 when it sticks, one less at the end of each camp loop; while|
// |                | it is up there the loop starts with it in the tree (day_boomerang.h)|
// |                | and the boomerang major is not planned                              |
// | ring, sets     | the last MEMORY_RING majors picked and the last loops' sets: avoided |
// | brave, persona | which critter kinds are bold this loop; re-rolled every 2 loops      |
// | last_family,   | the family and variants the last loop played (the morning draws a   |
// |   last_fvar    | variant it played last time a quarter as often)                      |
// | last_stay,     | the last loop was the morning where the camp stays up, and the     |
// |   last_sfvar   | variants it played (that morning's own quarter-weight rule)        |
// | loop_count     | loops started, this one included                                    |
#pragma once

#include <stddef.h>

#include "../anim/rng.h"
#include "critters.h"
#include "majors.h"   // MJ_COUNT

namespace ss {

struct World;

#define MEMORY_RING 8
#define MEMORY_SETS 9   // so ten loops in a row never play the same set of majors

// A set of majors, one bit per MajorId (bit 1u << id). Every id fits, rain_shelter
// (16) included. A 16-bit mask would lose the bit of any id past 15, and a set
// holding one would then match the set without it.
typedef uint32_t MajorSet;
static_assert(MJ_COUNT <= 8 * sizeof(MajorSet), "every major id must fit a MajorSet: widen the typedef");
static_assert(CR_COUNT <= 16, "Memory::brave is a 16-bit mask of critter kinds: widen it");

struct Memory {
  bool shoe_taken;       // the raccoon has the shoe
  bool shoe_back;        // this loop: raccoon_pack brought it back (he keeps it in the tent tonight)
  uint8_t cairn_n;       // stones on the cairn at the end of the last loop
  bool mouse_hole;       // the mice got into the tent (until he patches it)
  bool rod_lost;         // the fishing rod was thrown or towed away (fishing_small, fishing_log)
  uint8_t boom_stuck;    // camp loops the boomerang is still stuck in the camp pine (0 not; 2 just stuck; 1 the next loop)
  uint32_t loop_count;   // loops started, this one included
  uint8_t last_stolen;   // ItemId + 1 of the last thing stolen, 0 none
  uint8_t ring[MEMORY_RING];   // majors picked, oldest first
  uint8_t ring_n;
  uint8_t last_n;              // how many of the newest ring entries the last loop picked
  MajorSet sets[MEMORY_SETS];  // the last loops' majors as bit sets (bit = MajorId), newest last
  uint8_t sets_n;
  uint8_t mouse_tent_runs;     // this loop: times the mouse_tent minor ran (memoryNoteMouseTent)
  uint16_t brave;        // bit k: critter kind k is bold (else shy) this loop
  uint8_t persona_age;   // loops the current brave/shy split has lasted
  // The family the last loop played (SpineFamily) and its variant per beat
  // (LoopPlan::fvar), so a family's plan can make the same variant twice
  // running less likely. Not printed by memoryFormat.
  uint8_t last_family;
  uint8_t last_fvar[8];
  // The morning where the camp stays up (morning_stay.cpp) keeps its own: last loop was one, and
  // its variants (LoopPlan::fvar). It leaves last_fvar alone, so Mark's morning plans as it did.
  bool last_stay;
  uint8_t last_sfvar[8];
};

// Loop start, after worldBeginLoop (one rng call, every loop): the cairn from
// last time one stone shorter, the mouse hole in the tent, the brave/shy split
// (kept for 2 loops, then drawn again).
void memoryBeginLoop(World& w, Rng& rng);
// Loop end, before the next worldBeginLoop: the cairn's height, the mouse hole.
void memoryEndLoop(World& w);

// The mouse_tent minor calls this each time it runs; twice in one loop and the
// tent has a hole the next loop.
void memoryNoteMouseTent(Memory& m);

// The majors' ring buffer: push a pick; how many picks ago `id` was picked
// (0 the newest), or -1 when it is not in the ring.
void memoryPushMajor(Memory& m, uint8_t id);
// A loop's whole set of majors (bit = MajorId), and whether a set was played
// in the last MEMORY_SETS loops.
void memoryPushSet(Memory& m, MajorSet set);
bool memoryRecentSet(const Memory& m, MajorSet set);
int memoryMajorAge(const Memory& m, uint8_t id);

// This loop's personality for a kind, laid over a critter just spawned: bold
// ones move quicker and fidget less, shy ones are slow and jumpy.
bool memoryBrave(const Memory& m, CritterKind k);
void memoryApplyPersona(const Memory& m, Critter& c);

// "loop=3 cairn=4 shoe=0 stolen=mug hole=0 rod=0 brave=deer,squirrel ring=echo,pinecone"
int memoryFormat(const Memory& m, char* buf, size_t n);

}  // namespace ss
