// The boomerang and the eagle: a standalone prop and actor for the daytime
// pool (MORNING_DAYTIME_PLAN.md section 3). Not wired into any scene yet: a
// scene owns one BoomFlight, calls boomPlan when the beat starts and boomStep
// every tick, and draws the two pieces where its actor sort wants them.
//
// The boomerang is two ACTOR capsules at 108 degrees with a fat elbow and one
// ACTOR_LIGHT stripe, about 18 px across, spinning at about 3 turns a second.
// The eagle is a self-contained rig of ACTOR primitives (not a CritterKind):
// a broad wing with splayed fingers, a hooked bill and a fanned tail are its
// silhouette, about 50 px from wingtip to wingtip with the wings level; seen
// from above and the side, the far wing over the body and the near one under it.
//
// One flight, three endings, picked by the caller:
//   A  BOOM_CATCH  a wide loop out to the right and back; it slows into the
//                  catch point and rests there, still, in the hand.
//   B  BOOM_EAGLE  the same loop, but the eagle glides in over a side edge,
//                  high up and behind the boomerang, flares, closes its talons
//                  on it at a planned point and time and climbs out of frame
//                  with it. The grab is always on the outbound arc, at least
//                  0.6 of the reach from the catch point, so the eagle leaves
//                  away from him, and the glide is held clear above the catch
//                  point's x (76 px of talon height over it) so it never
//                  crosses him. The eagle may be on screen before the throw:
//                  the plan lengthens t_throw by what the glide-in needs.
//   C  BOOM_STICK  it flies past the catch point and sticks by one tip in a
//                  tree trunk at the caller's point and angle, wobbles, and
//                  stays (the final state never changes again).
//
// Everything is a pure function of the plan and the clock: a position is
// computed from time, not integrated, so any dt gives the same path and the
// meeting in B is exact (the eagle's path is planned backward from it). The
// clock starts at boomPlan. The boomerang waits in the hand until t_throw
// (spec.wind_up, or later in B if the eagle needs the lead), then BE_THROWN.
//
// Rules kept (HOW_TO_ADD_SCENES.md sections 1.4, 1.5, 8): plain data by value,
// no allocation, no clock but the caller's dt, single precision, table sines;
// Rng is touched only inside boomPlan, a fixed ten draws whatever the ending;
// draws are pure functions of state, palette and clip; every draw has bounds
// folded from the same primitive list, and an X Same compares every field its
// draw reads. Nothing here rasterizes outside appRender's draw callbacks.
//
// The numbers below are PROPOSED and the look is UNVERIFIED until Mike has seen
// it on the contact sheets and the panel.
#pragma once

#include "../anim/rng.h"
#include "../gfx/raster.h"
#include "../palette.h"

namespace ss {

enum BoomEnding : uint8_t { BOOM_CATCH = 0, BOOM_EAGLE, BOOM_STICK };

// What the scene hands over. Positions are frame space, the point the
// boomerang's centre passes (A: comes to rest there; B and C: the loop is aimed
// at it), so put it where his hand is. Everything is PROPOSED.
struct BoomSpec {
  uint8_t ending = BOOM_CATCH;
  Vec2 hand{0.f, 0.f};        // it leaves here
  Vec2 catch_pt{0.f, 0.f};    // A: it rests here; B: the loop is aimed here; C: the hand it flies over
  Vec2 trunk{0.f, 0.f};       // C: the point on the trunk where the arm's tip goes in
  float trunk_turns = 0.f;    // C: direction from that point out along the stuck arm, in turns
                              // clockwise on screen from +x (0.5 left, 0.25 down); the other arm hangs
                              // below it. For a trunk on its left, the boomerang hangs out to the right: 0.1
  float reach = 240.f;        // px the loop swings out from the hand (negative: to the left)
  float rise = 90.f;          // px above the hand at the top of the outbound arc
  float dip = 34.f;           // px below the hand on the way back
  float seconds = 3.4f;       // throw to catch (A), to the hit (C); B's meeting is a share of it
  float wind_up = 0.f;        // seconds from boomPlan to the throw (B may add lead)
  float miss = 30.f;          // C: px above catch_pt it flies past, at the closest
  float hand_turns = 0.70f;   // its angle in the hand, before the throw and (A) after the catch
};

// The boomerang as drawn. Callers may fill one in themselves (say, to hold it
// in his hand after A) and draw it with boomerangDraw.
struct Boomerang {
  bool on;
  int8_t flip;      // +1 or -1: which way the V is cut (the stripe's arm)
  Vec2 pos;         // centre of the V (its chord, halfway between elbow and tips)
  float rot;        // turns, + clockwise on screen; 0 puts the tips toward +x
};

Rect boomerangBounds(const Boomerang& b);   // every pixel boomerangDraw can touch, plus 2 px, within the frame
void boomerangDraw(const Boomerang& b, Surface& s, const Rect& clip, const Palette& pal);
bool boomerangSame(const Boomerang& a, const Boomerang& b);
int boomerangPrims(const Boomerang& b);     // rasterizer calls one draw issues (budget checks)
// The end of one arm in frame space: the one that goes into a trunk
// (free_arm false) or the striped one (true).
Vec2 boomerangTip(const Boomerang& b, bool free_arm);

// The eagle as drawn. Its talons are the reference point (eagleTalons): the
// body hangs from them by the pose, so a meeting point is where the feet are.
struct Eagle {
  bool on;
  int8_t facing;    // +1 right, -1 left
  Vec2 pos;         // body centre
  float pitch;      // radians, nose up +
  float flap;       // wingbeat phase, turns: 0.25 is the top of the stroke, 0.75 the bottom
  float beat;       // 0 wings held about level .. 1 full deep beats
  float flare;      // 0..1 wings thrown up and held, tail spread: the brake before a grab
  float fan;        // 0 tail closed .. 1 fully fanned
  float reach;      // 0 talons tucked .. 1 reaching out and down
};

Rect eagleBounds(const Eagle& e);
void eagleDraw(const Eagle& e, Surface& s, const Rect& clip, const Palette& pal);
bool eagleSame(const Eagle& a, const Eagle& b);
int eaglePrims(const Eagle& e);
// Where it grips: the point a carried boomerang's centre sits at.
Vec2 eagleTalons(const Eagle& e);

// ---- the whole flight ---------------------------------------------------------

enum BoomPhase : uint8_t {
  BP_WAIT = 0,   // in the hand, before the throw
  BP_FLY,        // spinning along the loop
  BP_CARRIED,    // B: in the eagle's talons
  BP_HAND,       // A: caught, at rest at catch_pt for good
  BP_STUCK,      // C: in the trunk (wobbling, then still for good)
  BP_GONE        // B: out of frame with the eagle
};

// Raised by boomStep (cleared at the start of each), so a scene can cue the
// hiker: the arm swing at BE_THROWN, the reach at BE_CAUGHT, the stare at
// BE_SNATCHED, the tug at BE_STUCK.
enum BoomEvent : uint8_t {
  BE_THROWN = 1, BE_CAUGHT = 2, BE_SNATCHED = 4, BE_STUCK = 8, BE_EAGLE_GONE = 16
};

// Fixed at boomPlan; read by the step. Times are on the module clock.
struct BoomPlan {
  uint8_t ending;
  int8_t flip;
  int8_t facing;        // B: the eagle's way
  uint8_t nk;           // knots in use (5, or 6 for C)
  bool ease;            // the path slows into its end (A, B)
  Vec2 k[6];            // the loop's knots: hand .. catch (or past it, to the trunk)
  float t_throw;        // the throw
  float dur;            // seconds the path takes from the throw
  float t_land;         // end of flight: catch (A), snatch (B), hit (C)
  float spin;           // turns/s, signed
  float slow;           // A: seconds the spin slows over before the catch
  float rot_land;       // its angle at t_land
  float hand_turns;     // its angle in the hand
  Vec2 stick;           // C: the embedded tip
  // B: the eagle's talon path, planned back from the meeting.
  float t_e0, t_e1;     // on screen from t_e0, gone at t_e1 (t_land is the grab)
  Vec2 e_a, e_va;       // entry point (off screen) and its velocity, px/s
  Vec2 e_m, e_vm;       // the meeting point and the velocity through it
  Vec2 e_b, e_vb;       // exit point (off screen) and its velocity
};

struct BoomFlight {
  BoomPlan plan;
  float t;              // seconds since boomPlan
  uint8_t phase;        // BoomPhase
  uint8_t events;       // BoomEvent bits from the last boomStep
  Boomerang boom;
  Eagle eagle;
};

// Plan a flight from `spec` and put the boomerang in the hand (on, BP_WAIT).
// Draws exactly ten values from `rng`, whatever the ending. Resets `f`.
void boomPlan(BoomFlight& f, const BoomSpec& spec, Rng& rng);
void boomStep(BoomFlight& f, float dt);

// Nothing will change any more: A in the hand, C still in the trunk, B gone.
bool boomDone(const BoomFlight& f);
// Both pieces, boomerang first so the talons close over it.
void boomDraw(const BoomFlight& f, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
