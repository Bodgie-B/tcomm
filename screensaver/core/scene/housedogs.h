// Lucy and Copper in camp (stage 13, Mike approved the design 11 Nov).
//
// The panels track the house's two dogs separately (Inputs::dog_out: bit 0
// Lucy, bit 1 Copper). While a dog is out it is in the scene, in every family
// but the Lookout: it trots in from the left a few seconds after it goes out
// (or, at a loop's start, once the camp has built up), stays, and trots off to
// the left when it comes in. Both can be there together.
//
// While it is there it behaves like a dog, one activity at a time, picked on a
// slow random schedule from what the camp offers right then:
//   roam        trots somewhere and sniffs about, once or twice
//   tent        sniffs along the tent (only when it is pitched)
//   fire        lies down by the fire pit (often at night, when it is lit)
//   drink       drinks at the stream bank, rings on the water
//   watch       sits a little way off and watches the hiker
//   leaf        a leaf skitters off in a breath of wind: a play-bow, a chase and a pounce
//   pat         goes to the hiker while he sits (Lucy: or stands) still and gets a pat
//   nap         lies down and puts its chin down (Lucy more than Copper)
//   play        Copper only, with Lucy there: a play-bow at her and a dash round her
//   settle      while a beat needs the camp (a major, dinner, the bear, the close): lies
//               out of the way beside the tent; sits up and watches a bear
//   chase       not picked: a visitor at the stream (stream.h, the heron or the deer) while
//               the dog is about: it notices (head up, a beat), runs at it, the visitor goes
//               before it gets there, it stops at the bank, looks after it, and goes back to
//               what it was doing
// No activity follows itself or the one before it, and each lasts a random
// while, so nothing repeats in an order you can follow. Lucy is calm (long
// lies, a slow heavy wag), Copper busy (short holds, a quick wag, more sniffing
// and chasing).
//
// The fetch major (fetch.h) is a story beat with a dog in it. When it runs
// while a house dog is out, Lucy (or Copper, if only he is out) plays it, and
// Copper is the second dog when both are out: the house dogs here run off to
// the left as it begins, the fetch's dogs trot in from the left with the stick
// (fetch.cpp waits until these are out of sight), and when it is over these
// trot back in. With no dog out the fetch keeps its brown camp dog.
//
// Every choice comes from each dog's own small random stream, seeded from the
// loop's seed (the loop rng never sees it), and from the camp, so a seed and
// the same dog_out changes replay exactly. With no dog out nothing here runs,
// draws or costs anything: houseDogsStep returns at once.
//
// Each dog is a CR_DOG critter (critters.h DogTone) in its own director slot
// (CRITTER_STORY + dog, world.h), tagged CT_HOUSEDOG and CF_BRAVE: the
// hiker's walking never scatters it, the story's critters keep all their
// slots, and directorCount does not count it, so no beat waits for it to go.
//
// Tuning marked PROPOSED is a guess; how it looks is UNVERIFIED on the panel.
#pragma once

#include "../gfx/raster.h"

namespace ss {

struct World;

enum HouseDogId : uint8_t { HD_LUCY = 0, HD_COPPER = 1, HD_COUNT = 2 };
enum HouseDogPhase : uint8_t {
  HP_AWAY = 0,    // not in the scene (in the house, or waiting to trot in)
  HP_HERE,        // in camp, doing an activity
  HP_GOING,       // trotting off: it came in
  HP_STEPPED_OUT  // running off while the fetch major has the dogs
};
enum HouseDogAct : uint8_t {
  HA_NONE = 0, HA_ROAM, HA_TENT, HA_FIRE, HA_DRINK, HA_WATCH, HA_LEAF, HA_PAT, HA_NAP, HA_PLAY, HA_SETTLE,
  HA_CHASE,   // never picked (weight 0): started by a visitor at the stream
  HA_COUNT
};

struct HouseDog {
  int8_t slot;        // its director slot while in the scene, else -1
  uint8_t phase;      // HouseDogPhase
  uint8_t act;        // HouseDogAct
  uint8_t step;       // the activity's own step
  uint8_t prev[2];    // the last two activities (neither is picked next)
  int8_t bit;         // a World::bits slot it set going (the chased leaf), -1 none
  int8_t face;        // the way to face on arrival: -1, +1, 0 any
  uint8_t hold;       // the CritterAction it holds at the spot
  uint8_t round;      // a second sniff, a second dash
  uint8_t resume;     // chase: the activity it broke off, taken up again after
  float wait;         // HP_AWAY: seconds before it trots in (counted once the scene is ready); < 0 unset
  float t;            // seconds in the step
  float left;         // seconds left of the hold
  float wag;          // the wag's own clock, turns
  float wag_amp;      // the wag's swing now (eased toward the activity's)
  float head;         // the chin-down for a nap or a drink (eased; Critter::head_add)
  float ring_t;       // drink: seconds to the next ring on the water
  uint32_t rng;       // its own random stream
  Vec2 to;            // where the activity takes it
};

struct HouseDogs {
  uint8_t want;       // bit 0 Lucy, bit 1 Copper: out now (the app copies Inputs::dog_out every tick)
  bool night;         // the scene's night: calmer choices, the fire more often
  uint8_t pat;        // 1 + the dog he is patting, 0 none
  uint8_t lent;       // 1 + the dog the visitors' frisbee game has (visitors.cpp drives it), 0 none
  float pat_t;        // seconds into the pat
  uint8_t fetch_tone[2];   // the fetch major's dogs (DogTone + 1, 0 none), set by fetch.cpp as they spawn
  HouseDog d[HD_COUNT];
};

// Loop start (after worldBeginLoop cleared the World): each dog's stream from
// the loop seed, the dogs out now, the night. Neither is in the scene yet.
void houseDogsBeginLoop(World& w, uint64_t loop_seed, int dog_out, bool night);
// The inputs as they are this tick (the app, every tick before worldTick).
void houseDogsSetInputs(World& w, int dog_out, bool night);
// Every tick from worldTick, after the spine and the minors, before the director.
void houseDogsStep(World& w);
// Neither house dog is in sight (the fetch's dog waits for this before it trots in).
bool houseDogsClear(const World& w);
// The tone the fetch major's dog `k` (0 the first, 1 the second) should have now:
// Lucy if she is out (Copper if only he is), Copper as the second when both are;
// the camp dog (DT_CAMP) when none is out. -1: no second dog.
int houseDogsFetchTone(const World& w, int k);
const char* houseDogActName(int act);
// The visitors' frisbee (visitors.cpp): borrow a dog that is in camp and free (Copper first: he is
// the busy one), so the game can drive it (directorWalk, critterSetAction) while its wag runs on.
// Returns its slot, or -1. While lent its own activities wait; if it goes in meanwhile it is given
// back and trots off. houseDogsGiveBack hands it back to its own life.
int houseDogsLend(World& w);
void houseDogsGiveBack(World& w);

}  // namespace ss
