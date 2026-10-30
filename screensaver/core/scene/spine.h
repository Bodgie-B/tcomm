// The story spine: this loop's variant picks (LoopPlan), the camp layout the
// beats walk between, and the beats themselves (brief section 6).
//
// Each spine beat is a short script: a list of steps (walk to a place, take a
// pose and hold it, run a timed action on a prop) that a tiny interpreter in
// spine.cpp runs one tick at a time. A variant is just another step list. The
// beat's duration is fixed per variant and longer than its script, so every
// beat ends with the hiker at rest; a "pause" beat (8 to 20 s, only ambient
// moves) follows each one. If a script ever runs past its beat, the beat's end
// hook counts an overrun (tests require zero).
//
// Loop timeline (spineBuildTimeline): build, arrival, setup, firewood, fire,
// dinner, foodbag, bear, close, each but the last followed by a pause, then
// "ambient" for whatever is left of the 600 s.
#pragma once

#include "../anim/rng.h"
#include "../anim/scheduler.h"
#include "../gfx/surface.h"

namespace ss {

struct Inputs;
struct World;

enum SpineBeatId : uint8_t {
  SB_ARRIVAL = 0, SB_SETUP, SB_FIREWOOD, SB_FIRE, SB_DINNER, SB_FOODBAG, SB_BEAR, SB_CLOSE, SB_COUNT
};

// Calm fillers for the long gaps (picked but not scheduled yet: the second
// half of stage 4 adds them).
enum FillerId : uint8_t { FILL_CAIRN = 0, FILL_READ, FILL_MUG, FILL_VIEW, FILL_NAP, FILL_COUNT };

struct LoopPlan {
  uint8_t arrival, setup, firewood, fire, dinner, foodbag, bear, close;   // variant index per beat
  uint8_t n_fillers;
  uint8_t filler[4];            // FillerId
  uint8_t pause_s[SB_COUNT];    // seconds of stillness after each spine beat
  uint8_t away_s;               // firewood: seconds out of frame on the first trip
};

// Variants built for a beat (1 for the placeholders), and a plan's pick.
int spineVariantCount(int beat);
uint8_t spineVariant(const LoopPlan& p, int beat);
const char* spineBeatName(int beat);
const char* spineFillerName(int filler);

// Where things are this loop, from the fire pit and the landscape. Frame px;
// "feet" points are where the hiker stands to do the thing.
struct CampLayout {
  Vec2 spot;        // arrival: where he decides to camp (right of the fire)
  Vec2 past;        // arrival b: where he overshoots to
  Vec2 tent;        // tent base center (left of the fire, behind it)
  Vec2 tent_work;   // feet while pitching it, at its front corner
  Vec2 pack;        // pack's place beside the tent
  Vec2 peg_rock;    // setup a: where he finds a rock to hammer with
  Vec2 wood;        // wood pile base, behind and left of the pit
  Vec2 wood_work;   // feet while setting wood down
  Vec2 blow;        // feet while blowing on the fire
  Vec2 sit;         // feet while sitting by the fire
  Vec2 cool;        // feet at the stream bank, cooling his hand
  Vec2 rock_sit;    // arrival c: feet sitting by a rock across the stream
  Vec2 ford0, ford1;   // arrival c: near and far bank of the crossing
  Vec2 edge_l, edge_r; // just out of frame, left and right
  Vec2 cairn;       // cairn base, on the bank
};

struct Step;   // spine.cpp

struct ScriptRun {
  const Step* steps;
  uint8_t n;
  uint8_t i;           // step running
  bool entered;        // step i has started
  uint16_t ticks;      // ticks since step i started
};

struct SpineState {
  CampLayout camp;
  ScriptRun run;
  uint8_t overruns;    // spine beats that ended before their script
  float u0;            // scratch value an action keeps across its ticks (start of a tween)
};

// Loop start: pick every variant, pause and filler from the loop RNG.
void spinePlan(LoopPlan& p, Rng& rng, const Inputs& in);
// Loop start, after the fire pit is placed: camp layout and the props' homes.
void spineLayout(World& w);
// Add the build beat, the spine beats with their pauses, and a final ambient
// beat that brings the loop to exactly SS_LOOP_SECONDS.
void spineBuildTimeline(Timeline& tl, const LoopPlan& p);
// "arrival=1 setup=0 ... fillers=cairn,read" (fillers=none when empty).
int spineFormatPlan(const LoopPlan& p, char* buf, size_t n);

#define SS_LOOP_SECONDS 600

}  // namespace ss
