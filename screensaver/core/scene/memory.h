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
// | ring, sets     | the last MEMORY_RING majors picked and the last loops' sets: avoided |
// | brave, persona | which critter kinds are bold this loop; re-rolled every 2 loops      |
// | last_family,   | the family and variants the last loop played (the morning draws a   |
// |   last_fvar    | variant it played last time a quarter as often)                      |
// | loop_count     | loops started, this one included                                    |
#pragma once

#include <stddef.h>

#include "../anim/rng.h"
#include "critters.h"

namespace ss {

struct World;

#define MEMORY_RING 8
#define MEMORY_SETS 9   // so ten loops in a row never play the same set of majors

struct Memory {
  bool shoe_taken;       // the raccoon has the shoe
  bool shoe_back;        // this loop: raccoon_pack brought it back (he keeps it in the tent tonight)
  uint8_t cairn_n;       // stones on the cairn at the end of the last loop
  bool mouse_hole;       // the mice got into the tent (until he patches it)
  bool rod_lost;         // the fishing rod was thrown or towed away (fishing_small, fishing_log)
  uint32_t loop_count;   // loops started, this one included
  uint8_t last_stolen;   // ItemId + 1 of the last thing stolen, 0 none
  uint8_t ring[MEMORY_RING];   // majors picked, oldest first
  uint8_t ring_n;
  uint8_t last_n;              // how many of the newest ring entries the last loop picked
  uint32_t sets[MEMORY_SETS];  // the last loops' majors as bit sets (bit = MajorId), newest last
  uint8_t sets_n;
  uint8_t mouse_tent_runs;     // this loop: times the mouse_tent minor ran (memoryNoteMouseTent)
  uint16_t brave;        // bit k: critter kind k is bold (else shy) this loop
  uint8_t persona_age;   // loops the current brave/shy split has lasted
  // The family the last loop played (SpineFamily) and its variant per beat
  // (LoopPlan::fvar), so a family's plan can make the same variant twice
  // running less likely. Not printed by memoryFormat.
  uint8_t last_family;
  uint8_t last_fvar[8];
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
void memoryPushSet(Memory& m, uint32_t set);
bool memoryRecentSet(const Memory& m, uint32_t set);
int memoryMajorAge(const Memory& m, uint8_t id);

// This loop's personality for a kind, laid over a critter just spawned: bold
// ones move quicker and fidget less, shy ones are slow and jumpy.
bool memoryBrave(const Memory& m, CritterKind k);
void memoryApplyPersona(const Memory& m, Critter& c);

// "loop=3 cairn=4 shoe=0 stolen=mug hole=0 rod=0 brave=deer,squirrel ring=echo,pinecone"
int memoryFormat(const Memory& m, char* buf, size_t n);

}  // namespace ss
