// Mushrooms after rain as a surprise major. See day_trip.h.
//
// OWNERSHIP: skeleton from the foundation step (A1); step C5c (the integrator) filled it in, with step
// C5b's sky clock and band repaint (tripDayStep, tripSkyTrack).
//
// Mike, 2026-11-18: "having mushrooms grow near the one mushroom after a rain storm, having hiker pick
// one, eat one, lay down and have the sky go psychodelic for a couple minutes". Offered and not
// rejected: he wakes and tosses the rest in the stream.
//
// The story (MUSHROOM_PLAN section 5 under INTEGRATION_PLAN and REVIEW_FIXES): he shades his eyes at the
// patch across the water, wades over (paced by the path's length, F5), peers, picks three, sniffs one,
// looks round, eats it, waits with his hands on his hips for nothing to happen, shrugs and wades back.
// It comes on as he walks to the grass: the field leans in one slow wave. He looks at his hand, lies
// down, and the sky lets go (skytrip.h) for 104 s of its own clock. Lucy (or Copper alone) comes and
// lies by his legs. He sees two things, one at a time (the variant's set); then he traces the sky once
// with one arm, it all settles, he wakes, shakes his head at the two that are left and tosses them in
// the stream (one time in four he pockets one first).
//
// Built: set 0 (the big chipmunk; the deer with the tree) and set 1 (the slow fish; smoke rings from the
// cold pit). NOT built: set 2 (the clouds look; the stars settle), so the plan's byte picks between two.
//
// Clocks: the script and the tick run on the beat's clock, so a doorbell freezes all of it. The sky clock
// moves kTripClockStep for every band repainted (tripDayStep): at one band every second tick, which the
// trip switches to while a vision stands taller than row 320 (F3), it runs at half speed. The story's
// own seconds (T) do not slow, so the sky's 104 s take longer on the beat's clock in set 0.
#include "day_trip.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../mush_patch.h"
#include "../render/renderer.h"
#include "director.h"
#include "housedogs.h"
#include "landscape.h"
#include "memory.h"
#include "patch.h"
#include "reeds.h"
#include "skytrip.h"
#include "trip.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }
TripState& S(World& w) { return w.day.trip.t; }
const TripState& S(const World& w) { return w.day.trip.t; }
bool live(const World& w) { return w.day.kind == DAY_TRIP && w.major.on && w.major.id == MJ_MUSHROOM; }
// The mushroom's play or the frog's (both run on DAY_TRIP and its sky).
bool liveAny(const World& w) {
  return w.day.kind == DAY_TRIP && w.major.on && (w.major.id == MJ_MUSHROOM || w.major.id == MJ_FROG_LICK);
}
inline float fl(int v) { return (float)v; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline bool crossed(float t, float dt, float at) { return t >= at && t - dt < at; }
float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }

// ---- the variant byte: bits 0-1 the set, bit 2 the pocket ending, bit 3 DAY_NOSHOW ----------------------------
constexpr uint8_t kVarSet = 3, kVarPocket = 4;
constexpr int kTripSets = 2;   // sets built (set 2, the sky comes down, is not)

// ---- tuning (PROPOSED; how it looks is UNVERIFIED on the panel) -------------------------------------------------
constexpr float kWadeV = 12.f;        // px/s of true distance through the water (F5)
constexpr float kToLie = 26.f;        // the walk to the grass once it has come on: slower than he came
constexpr float kChipIn = 58.f, kChipNear = 16.f, kChipBig = 3.2f, kChipOut = 44.f;
// px from his head where the grown chipmunk sits: beside him, not over his face. At 3.2 times it is about
// 66 px long; at 34 its box lay 14 to 16 px over his head and the two read as one shape at 1x (C5 review).
constexpr float kChipStand = 54.f;
constexpr float kChipLook = 94.f;     // where it stops at its own size for its first look: 40 px still to go
constexpr float kDeerIn = 34.f, kDeerOut = 34.f;
constexpr float kDogWalk = 30.f;
constexpr float kCapFloat = 22.f;     // px/s down the stream
constexpr float kCapLife = 8.f;       // s a tossed cap rides before it slips under
// Beat seconds by set: the script and the sky's 104 s (longer on the beat's clock in set 0, where the sky
// clock runs at half speed under the tall visions), plus slack for the longer walks.
const int kSeconds[kTripSets] = {240, 216};
// When the two visions begin, on the sky clock. Set 0's second waits for the first to leave as well.
constexpr float kV1At = 34.f, kV2At = 62.f, kV2AtSlow = 50.f, kGapS = 4.f;
constexpr float kTraceAt = 88.f, kTraceS = 9.f;

constexpr uint16_t kOnset = 1, kSky = 2, kSkyOver = 256;

float bankY(const World& w, float x) {   // the camp side's water edge at x
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? cy - half : 386.f;
}
float waterY(const World& w, float x, float fallback) {   // the stream's middle at x
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? cy : fallback;
}
void ring(World& w, Vec2 at, float size) {
  const int b = worldTakeBit(w, BIT_RING);
  if (b < 0) return;
  Bit& r = w.bits[b];
  r.pos = at;
  r.spin = size;
  r.land_y = at.y;
}
// Which end of him his head is at (lying, or any time): +1 right of his hips, -1 left.
int headSide(const World& w) {
  float r;
  return hikerHeadPos(w.hiker, &r).x >= w.hiker.feet.x ? 1 : -1;
}

// ---- the wade, paced by the path's length (REVIEW_FIXES F5) ----------------------------------------------------
//
// A walk paces only x and slides y along the path; this crossing is steeper than it is wide. So the walk is
// given the x speed that makes the true speed kWadeV, and the tick adds the stride the rig did not count
// (the rig's walk phase advances with x only), so his steps match the ground he covers.
void wadeTo(World& w, Vec2 to) {
  Hiker& h = w.hiker;
  HikerFx& fx = w.fx;
  const float dx = to.x - h.feet.x, dy = to.y - h.feet.y;
  const float len = sqrtf(dx * dx + dy * dy);
  fx.path_on = true;
  fx.path_x0 = h.feet.x;
  fx.path_y0 = h.feet.y;
  fx.path_x1 = to.x;
  fx.path_y1 = to.y;
  if (fabsf(dx) < 0.5f) {
    h.feet = to;
    return;
  }
  hikerWalkTo(h, to.x, kWadeV * fabsf(dx) / len);
  S(w).wading = 1;
  M(w).u1 = len / fabsf(dx);   // true px per px of x
}
void wadeStride(World& w, float dt) {
  TripState& s = S(w);
  Hiker& h = w.hiker;
  if (!s.wading) return;
  if (!h.walking) {
    s.wading = 0;
    return;
  }
  // The rig's own cycle at its shortest stride (hiker.cpp: 4 (thigh + shin) H sin(amp), amp 0.18 at these
  // speeds; thigh and shin are 0.235 H each).
  const float cycle = 4.f * 0.47f * h.height * sinT(0.18f * (1.f / 6.2831853f));
  const float xstep = h.walk_speed * easeInOut(h.walk_w) * dt;
  if (cycle > 0.f) {
    h.walk_phase += xstep * (M(w).u1 - 1.f) / cycle;
    h.walk_phase -= floorf(h.walk_phase);
  }
  // A small ring round his legs at the waterline every 0.9 s while he is in the water (C5 review: with
  // only the water drawn over his shins, at 1x he read as standing on the bank's edge, not wading). It
  // sorts just in front of him, so the water over his legs does not hide it.
  if (w.fx.wet_y > 0.f && h.visible) {
    s.gap_t += dt;
    if (s.gap_t >= 0.9f) {
      s.gap_t = 0.f;
      const int b = worldTakeBit(w, BIT_RING);
      if (b >= 0) {
        Bit& r = w.bits[b];
        r.pos = Vec2{h.feet.x, w.fx.wet_y + 1.f};
        r.spin = 0.65f;
        r.land_y = h.feet.y + 1.f;
      }
    }
  }
}
void doWadeOn(World& w, float) { w.fx.wade = true; }
void doWadeOff(World& w, float) { w.fx.wade = false; }
void doWadeMid(World& w, float) { wadeTo(w, M(w).p[2]); }
void doWadeNear(World& w, float) { wadeTo(w, M(w).p[1]); }
void doWadeFar(World& w, float) { wadeTo(w, M(w).p[0]); }
bool arrivedP(const World& w) { return hikerArrived(w.hiker); }

// ---- his actions (each sets what it moves as a function of u and rests it on the last tick) --------------------

// He has seen something: the near hand up to shade his eyes, a small lean, and after a moment his head
// tips to one side.
void actSpot(World& w, float u) {
  const float e = env(u, 0.22f, 0.2f);
  scriptSetArm(w, 0, e, 2.3f, 1.7f);
  w.fx.lean = 0.08f * e;
  w.fx.head = 0.10f * e * easeInOut(clamp01((u - 0.43f) / 0.18f));
  if (u >= 1.f) scriptFxRest(w);
}
void actLookDown(World& w, float u) {   // a look at the water before he steps in
  w.fx.head = 0.24f * env(u, 0.3f, 0.3f);
  if (u >= 1.f) scriptFxRest(w);
}
// Midstream: both arms out a little and one slow sway. The stones are slick.
void actBalance(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 1.0f, 0.25f);
  scriptSetArm(w, 1, e, -0.9f, 0.25f);
  w.fx.lean = 0.07f * e * sinT(u);
  if (u >= 1.f) scriptFxRest(w);
}
// Crouched at the patch: his head to one side, then the other; a finger touches the first one and it rocks.
void actPeer(World& w, float u) {
  TripState& s = S(w);
  const float e = env(u, 0.15f, 0.15f);
  w.fx.head = 0.13f * e * sinT(u * 1.1f);
  const float touch = easeInOut(clamp01((u - 0.50f) / 0.10f)) * (1.f - easeInOut(clamp01((u - 0.68f) / 0.12f)));
  w.fx.reach = touch;
  w.fx.reach_to = 0.85f;
  const float after = u - 0.60f;   // it rocks 0.10 rad and settles in half a second
  s.rock = after > 0.f ? 0.016f * sinT(after * 2.2f * 2.5f) * clamp01(1.f - after / 0.25f) : 0.f;
  if (u >= 1.f) {
    s.rock = 0.f;
    scriptFxRest(w);
  }
}
// One pick: the near arm reaches, the mushroom leans to the hand, comes free with a hop and joins the bunch.
void actPick(World& w, float u) {
  TripState& s = S(w);
  if (s.picked >= s.n_pick) return;
  s.pick_i = s.picked;
  s.pick_u = u;
  w.fx.reach = easeInOut(clamp01(u / 0.3f)) * (1.f - easeInOut(clamp01((u - 0.66f) / 0.26f)));
  w.fx.reach_to = 0.85f;
  s.in_far = (uint8_t)(s.picked + (u >= 0.62f ? 1 : 0));
  if (u >= 1.f) {
    ++s.picked;
    s.in_far = s.picked;
    s.pick_i = 3;
    s.pick_u = 0.f;
    scriptFxRest(w);
  }
}
bool morePicks(const World& w) { return live(w) && S(w).picked < S(w).n_pick; }
// One from the bunch into the near hand, up under his nose; his head goes back a touch; two short sniffs;
// then he holds it away at arm's length and looks at it.
void actSniff(World& w, float u) {
  TripState& s = S(w);
  if (u >= 0.08f && !s.in_near && s.in_far > 0 && s.bite == 0) {
    s.in_near = 1;
    --s.in_far;
  }
  const float near_k = easeInOut(clamp01((u - 0.10f) / 0.14f)) * (1.f - easeInOut(clamp01((u - 0.52f) / 0.10f)));
  const float away_k = easeInOut(clamp01((u - 0.56f) / 0.12f)) * (1.f - easeInOut(clamp01((u - 0.90f) / 0.10f)));
  // Under the nose: the upper arm forward and up, the forearm folded back to the face. Away: straight out.
  scriptSetArm(w, 0, maxf(near_k, away_k), near_k * 1.15f + away_k * 1.45f, near_k * 2.25f + away_k * 0.2f);
  const float sn = u > 0.26f && u < 0.50f ? 0.04f * sinT((u - 0.26f) * 2.6f * 2.f) : 0.f;   // two sniffs at 2 Hz
  w.fx.head = -0.07f * near_k + sn + 0.06f * away_k;
  if (u >= 1.f) scriptFxRest(w);
}
// A look over one shoulder, then the other. Nobody.
void actLookRound(World& w, float u) {
  w.hiker.facing = (u > 0.12f && u < 0.52f) ? -1 : 1;
  w.fx.lean = 0.04f * env(u, 0.1f, 0.1f);
  if (u >= 1.f) scriptFxRest(w);
}
// Hand to mouth; three small bites 0.8 s apart, the cap a third smaller with each; a slow chew; the stem
// flicked over his shoulder into the water.
void actNibble(World& w, float u) {
  TripState& s = S(w);
  const float t = u * 3.4f;
  const float k = easeInOut(clamp01(t / 0.5f)) * (1.f - easeInOut(clamp01((t - 2.7f) / 0.4f)));
  scriptSetArm(w, 0, k, 1.15f, 2.3f);
  if (s.in_near) s.bite = (uint8_t)(t >= 2.2f ? 3 : (t >= 1.4f ? 2 : (t >= 0.6f ? 1 : 0)));
  w.fx.head = 0.03f * sinT(t * 1.5f) * k;
  const float flick = easeInOut(clamp01((t - 2.75f) / 0.2f)) * (1.f - easeInOut(clamp01((t - 3.05f) / 0.3f)));
  if (flick > 0.f) scriptSetArm(w, 0, maxf(k, flick), lerp(1.15f, 2.7f, flick), lerp(2.3f, 0.6f, flick));
  if (t >= 2.95f && s.in_near) {   // gone over his shoulder: one small ring where it lands
    s.in_near = 0;
    const float x = w.hiker.feet.x - (float)w.hiker.facing * 26.f;
    ring(w, Vec2{x, waterY(w, x, w.hiker.feet.y - 24.f)}, 0.7f);
  }
  if (u >= 1.f) scriptFxRest(w);
}
void actShrug(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 0.9f, 1.7f);
  scriptSetArm(w, 1, 0.8f * e, 0.8f, 1.6f);
  w.fx.head = 0.2f * e;
  w.fx.lift = 0.012f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// It has come on. The near hand comes up in front of his face; he turns it over, slowly, once; the forearm
// sways and his head follows it.
void actHand(World& w, float u) {
  const float e = env(u, 0.2f, 0.2f);
  const float sway = sinT(u * 4.0f * 0.8f);
  scriptSetArm(w, 0, e, 1.55f, 1.35f + 0.15f * sway);
  w.fx.head = (-0.05f + 0.03f * sway) * e;
  if (u >= 1.f) scriptFxRest(w);
}
// Waking: his head rolls to one side and back, the near hand to his forehead.
void actStir(World& w, float u) {
  const float e = env(u, 0.25f, 0.25f);
  w.fx.head = 0.12f * e * sinT(u);
  scriptSetArm(w, 0, e * easeInOut(clamp01((u - 0.3f) / 0.3f)), 2.1f, 2.2f);
  if (u >= 1.f) scriptFxRest(w);
}
void actRubEyes(World& w, float u) {
  const float e = env(u, 0.2f, 0.25f);
  const float rub = 0.08f * sinT(u * 2.0f * 2.5f);
  scriptSetArm(w, 0, e, 1.2f, 2.3f + rub);
  scriptSetArm(w, 1, e, 1.15f, 2.3f - rub);
  w.fx.head = 0.08f * e;
  if (u >= 1.f) scriptFxRest(w);
}
// He looks at the two beside him, up at the plain sky, back at the two.
void actLookBunch(World& w, float u) {
  const float down1 = env(clamp01(u / 0.42f), 0.25f, 0.25f);
  const float up = env(clamp01((u - 0.42f) / 0.30f), 0.3f, 0.3f);
  const float down2 = env(clamp01((u - 0.72f) / 0.28f), 0.3f, 0.3f);
  w.fx.head = 0.20f * (down1 + down2) - 0.22f * up;
  if (u >= 1.f) scriptFxRest(w);
}
void actHeadShake(World& w, float u) {   // slow, twice
  w.fx.head = 0.07f * env(u, 0.15f, 0.2f) * sinT(u * 2.f);
  if (u >= 1.f) scriptFxRest(w);
}
// The pocket ending: he holds one up, looks left, looks right, and it goes in his pocket with a pat.
void actPocket(World& w, float u) {
  TripState& s = S(w);
  if (u >= 0.06f && s.pocket == 1 && s.in_far > 0) {
    s.pocket = 2;
    s.in_near = 1;
    s.bite = 0;
    --s.in_far;
  }
  const float up = easeInOut(clamp01((u - 0.06f) / 0.12f)) * (1.f - easeInOut(clamp01((u - 0.60f) / 0.10f)));
  const float pat = easeInOut(clamp01((u - 0.66f) / 0.08f)) * (1.f - easeInOut(clamp01((u - 0.92f) / 0.08f)));
  scriptSetArm(w, 0, maxf(up, pat), up * 1.3f + pat * 0.35f, up * 1.2f + pat * (0.7f + 0.12f * sinT(u * 2.6f * 3.f)));
  const int f0 = M(w).count ? 1 : -1;
  w.hiker.facing = (u > 0.20f && u < 0.38f) ? -f0 : f0;
  if (u >= 0.70f && s.pocket == 2) {
    s.pocket = 3;
    s.in_near = 0;
  }
  if (u >= 1.f) scriptFxRest(w);
}
void doFaceKeep(World& w, float) {   // the way he faces the water, for the looks left and right; the toss's clock
  M(w).count = (uint8_t)(w.hiker.facing > 0 ? 1 : 0);
  M(w).u0 = 0.f;
}
bool pocketP(const World& w) { return live(w) && S(w).pocket == 1 && S(w).in_far > 0; }
// An easy underhand lob: the cap arcs out and drops in with a ring, bobs up and rides the current away.
void actToss(World& w, float u) {
  TripState& s = S(w);
  MajorState& m = M(w);
  const float back = easeInOut(clamp01(u / 0.3f)) * (1.f - easeInOut(clamp01((u - 0.3f) / 0.2f)));
  const float fwd = easeInOut(clamp01((u - 0.3f) / 0.22f)) * (1.f - easeInOut(clamp01((u - 0.7f) / 0.3f)));
  scriptSetArm(w, 0, maxf(back, fwd), -0.5f * back + 1.25f * fwd, 0.15f);
  if (u >= 0.46f && m.u0 < 0.46f && s.in_far > 0) {
    --s.in_far;
    const int i = tripPropNew(s, TP_CAP);
    if (i >= 0) {
      TripProp& p = s.prop[i];
      const Vec2 from = hikerHandPos(w.hiker, 0);
      const float tx = from.x + (float)w.hiker.facing * 30.f;
      const Vec2 to{tx, waterY(w, tx, from.y + 20.f)};
      constexpr float kT = 0.7f, kG = 420.f;
      p.pos = from;
      p.vel = Vec2{(to.x - from.x) / kT, (to.y - from.y) / kT - 0.5f * kG * kT};
      p.k = to.y;   // the water it lands on
      p.tint = (uint8_t)s.in_far;
    }
  }
  m.u0 = u;
  if (u >= 1.f) {
    m.u0 = 0.f;
    scriptFxRest(w);
  }
}
bool hasCaps(const World& w) { return live(w) && S(w).in_far > 0; }
bool capsGone(const World& w) {
  if (!live(w)) return true;
  for (int i = 0; i < TRIP_PROPS; ++i)
    if (S(w).prop[i].kind == TP_CAP) return false;
  return true;
}

// ---- cues --------------------------------------------------------------------------------------------------------

// The three pickables leave the cache and become the module's; nothing changes on screen. From here the
// patch is picked over in the memory. The stem falls from his mouth as he crouches to them
// (INTEGRATION_PLAN 4.4).
void doLift(World& w, float) {
  TripState& s = S(w);
  if (s.lifted || s.n_pick == 0) return;
  s.lifted = 1;
  w.land.patch_lifted = (uint8_t)(w.land.patch_lifted | M(w).mode);
  w.memory.mush = mushAfterPick(w.memory.mush);
  reedsDropChew(w);
}
void cueOnset(World& w, float) { M(w).cue = (uint16_t)(M(w).cue | kOnset); }
void doBunchDown(World& w, float) {
  TripState& s = S(w);
  const int hs = headSide(w);
  // Beside him in the grass, past his head, a little behind his line so he lies in front of them.
  // (C5 review: at feet.y - 3 the two caps sat on his black body and read as two dots at his waist; a
  // few px in front of his line they lie on the grass and read as mushrooms.)
  s.bunch_at = Vec2{w.hiker.feet.x + (float)hs * 14.f, w.hiker.feet.y + 6.f};
  s.bunch_down = 1;
}

void doBunchUp(World& w, float) { S(w).bunch_down = 0; }
void cueSky(World& w, float) {
  TripState& s = S(w);
  M(w).cue = (uint16_t)(M(w).cue | kSky);
  s.sky_on = 1;
  s.sky_t = 0.f;
  s.T = 0.f;
  s.band = 0;
  s.phase = 0;
}
bool skyOver(const World& w) { return !liveAny(w) || (w.major.cue & kSkyOver) != 0; }
bool crittersGone(const World& w) {
  for (int k = 0; k < 2; ++k) {
    const int c = w.major.c[k];
    if (c >= 0 && w.crit[c].in_use && w.crit[c].tag == CT_MAJOR) return false;
  }
  return true;
}

const Step kTrip[] = {
    // 5.1 He notices and crosses.
    pose(POSE_STAND, 0.8f, 0.3f),
    faceTo(AT_M1), act(actSpot, 2.8f), still(0.7f),
    go(AT_M0, kStroll),
    faceTo(AT_M1), act(actLookDown, 0.9f),
    doit(doWadeOn), doit(doWadeMid), until(arrivedP), act(actBalance, 1.2f), doit(doWadeNear), until(arrivedP), doit(doWadeOff),
    face(RIGHT), still(0.5f), pose(POSE_CROUCH, 0.9f, 0.6f), act(actPeer, 2.2f),
    // 5.2 He picks three, sniffs one, eats one.
    doit(doLift),
    act(actPick, 1.6f), still(0.3f), repeatWhile(morePicks, 2),
    pose(POSE_STAND, 0.8f, 0.3f),
    act(actSniff, 2.6f),
    act(actLookRound, 2.0f),
    act(actNibble, 3.4f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 3.2f),   // he waits. Nothing happens. This stillness is the joke.
    act(actShrug, 1.4f),
    faceTo(AT_M0), doit(doWadeOn), doit(doWadeMid), until(arrivedP), doit(doWadeFar), until(arrivedP), doit(doWadeOff),
    // 5.3 It comes on, and he lies down.
    doit(cueOnset), go(AT_LIE, kToLie),
    act(actHand, 4.0f),
    face(RIGHT), pose(POSE_CROUCH, 0.7f, 0.2f), doit(doBunchDown), pose(POSE_SIT, 0.9f, 1.5f), pose(POSE_LIE, 1.6f, 1.0f),
    // 5.4 The sky.
    doit(cueSky), until(skyOver),
    // 5.7 After.
    still(2.0f), act(actStir, 2.2f), pose(POSE_SIT, 1.4f, 1.2f), act(actRubEyes, 2.0f),
    act(actLookBunch, 3.1f), act(actHeadShake, 1.4f),
    pose(POSE_CROUCH, 0.6f, 0.2f), doit(doBunchUp), pose(POSE_STAND, 0.9f, 0.3f),
    go(AT_M3, 30.f), faceTo(AT_M2), doit(doFaceKeep),
    skipUnless(pocketP, 1), act(actPocket, 2.6f),
    skipUnless(hasCaps, 2), act(actToss, 1.5f), repeatWhile(hasCaps, 1),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 2.6f),   // he watches them go
    until(capsGone), until(crittersGone),
    go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f),
};

// ---- the tossed caps ----------------------------------------------------------------------------------------------

void capsTick(World& w, float dt) {
  TripState& s = S(w);
  int nth = 0;
  for (int i = 0; i < TRIP_PROPS; ++i) {
    TripProp& p = s.prop[i];
    if (p.kind != TP_CAP) continue;
    ++nth;
    if (p.mode == 0) {   // in the air
      p.vel.y += 420.f * dt;
      p.pos.x += p.vel.x * dt;
      p.pos.y += p.vel.y * dt;
      p.rot = 0.12f * sinT(p.t * 1.4f);
      if (p.vel.y > 0.f && p.pos.y >= p.k) {
        p.pos.y = p.k;
        p.mode = 1;
        p.t = 0.f;
        ring(w, p.pos, 0.9f);
      }
      continue;
    }
    // Riding the current: down the stream to the left, turning slowly, bobbing; a fish rises under one.
    p.pos.x -= kCapFloat * dt;
    const float bob = (p.t < 0.5f ? -2.f * sinT(p.t) : 0.f) + 0.8f * sinT(p.t * 0.6f);
    p.pos.y = floorf(waterY(w, p.pos.x, p.pos.y) + bob + 0.5f);
    p.rot = 0.23f + 0.05f * sinT(p.t * 0.21f);
    if (p.tint == 0 && crossed(p.t, dt, 2.6f)) ring(w, Vec2{p.pos.x - 3.f, p.pos.y + 2.f}, 1.f);
    if (p.t >= kCapLife || p.pos.x < -12.f) {   // it slips under
      ring(w, p.pos, 0.6f);
      p.kind = TP_NONE;
    }
  }
  (void)nth;
}

// ---- the dog (plan 5.6): Lucy first, Copper when only he is out -----------------------------------------------------

enum : uint8_t { DG_NONE = 0, DG_WALK, DG_LIE, DG_CHIN, DG_OFF };

void dogBack(World& w) {
  TripState& s = S(w);
  if (s.dog) {
    const int k = (int)s.dog - 1 - CRITTER_STORY;
    if (w.dogs.lent == k + 1) {
      w.critters[s.dog - 1].head_add = 0.f;
      houseDogsGiveBack(w);
    }
  }
  s.dog = 0;
  s.dog_stage = DG_OFF;
}
void dogTick(World& w, float dt) {
  TripState& s = S(w);
  if (s.dog_stage == DG_OFF) return;
  if (s.dog == 0) {
    // Tried once a second from the time he is down until a minute in: she may not be free at once.
    if (w.dogs.want == 0 || s.T < 14.f || s.T > 60.f || floorf(s.T) == floorf(s.T - dt)) return;
    const int slot = houseDogsLend(w, HD_LUCY);
    if (slot < 0) return;
    s.dog = (uint8_t)(slot + 1);
    // Beside his legs, on the side away from his head, a little in front of his line.
    const float x = w.hiker.feet.x - (float)headSide(w) * 76.f;
    critterSetAction(w.critters[slot], CA_IDLE, 0.3f);
    directorWalk(w, slot, Vec2{x, w.hiker.feet.y + 2.f}, kDogWalk, CA_MOVE);
    s.dog_stage = DG_WALK;
    s.dog_t = 0.f;
    return;
  }
  const int slot = (int)s.dog - 1;
  if (w.dogs.lent != slot - CRITTER_STORY + 1) {   // called in: the house has her, nothing waits on her
    s.dog = 0;
    s.dog_stage = DG_OFF;
    return;
  }
  Critter& c = w.critters[slot];
  s.dog_t += dt;
  switch (s.dog_stage) {
    case DG_WALK:
      if (critterArrived(c)) {
        c.facing = w.hiker.feet.x >= c.feet.x ? 1 : -1;
        critterSetAction(c, CA_LIE, 0.9f);
        s.dog_stage = DG_LIE;
        s.dog_t = 0.f;
      }
      break;
    case DG_LIE:
      if (s.dog_t >= 4.f) {
        s.dog_stage = DG_CHIN;
        s.dog_t = 0.f;
      }
      break;
    case DG_CHIN: {
      // Her chin goes down; at the arm's trace she lifts her head, looks up for 3 s, and puts it down again.
      const float down = easeInOut(clamp01(s.dog_t / 1.5f));
      const float up = easeInOut(clamp01((s.sky_t - kTraceAt) / 0.8f)) * (1.f - easeInOut(clamp01((s.sky_t - kTraceAt - 3.f) / 1.2f)));
      c.head_add = -0.45f * down * (1.f - up);
      break;
    }
    default: break;
  }
}

// ---- what he sees ---------------------------------------------------------------------------------------------------

Critter& crit(World& w, int k) { return w.critters[M(w).c[k]]; }
bool own(const World& w, int k) { return w.major.c[k] >= 0 && directorOwns(w, w.major.c[k], CT_MAJOR); }
bool spawnVis(World& w, int k, CritterKind kind, Vec2 feet, int facing) {
  MajorState& m = M(w);
  const int sl = directorSpawn(w, kind, feet, facing);
  m.c[k] = (int8_t)sl;
  if (sl < 0) return false;
  w.crit[sl].tag = CT_MAJOR;
  w.crit[sl].flags = CF_BRAVE;
  memoryApplyPersona(w.memory, w.critters[sl]);
  return true;
}
void visTo(World& w, int st) {
  S(w).vis = (uint8_t)st;
  S(w).vt = 0.f;
}

// Set 0, the visitors. The chipmunk: in at its own size, a look, then nearer, growing to half his size; it
// sits up behind him, sniffs toward his face, holds, looks (CA_LOOK, REVIEW_FIXES F22); he raises his far
// forearm and lowers it, a hello; it walks off the way it came, shrinking. Then, after 4 s of only the sky,
// the deer: in along the back of the clearing, side on, a look; a small tree grows from its head (28 px,
// REVIEW_FIXES F3), leaves open at the tips, a bird lands; the deer turns its head and the tree turns with
// it; 5 s of stillness; the bird leaves, three leaves let go, the tree draws back in, and it walks out.
enum : uint8_t {
  V_WAIT = 0, VC_IN, VC_LOOK, VC_NEAR, VC_SNIFF, VC_HOLD, VC_HELLO, VC_OFF, VC_GONE,
  V_GAP, VD_IN, VD_LOOK, VD_GROW, VD_BIRD, VD_STILL, VD_SHED, VD_BACK, VD_OUT, V_DONE,
  // Set 1, the water: three slow fish, then five smoke rings from the cold pit.
  VW_FISH, VW_GAP, VW_RINGS
};

void helloArm(World& w, float t, float span) {   // his far forearm up and down: a hello
  const float k = easeInOut(clamp01(t / 0.9f)) * (1.f - easeInOut(clamp01((t - span + 1.0f) / 1.0f)));
  scriptSetArm(w, 1, k, 1.3f, 1.2f + 0.12f * sinT(t * 0.9f));
}

void set0Tick(World& w, float dt) {
  TripState& s = S(w);
  MajorState& m = M(w);
  const Hiker& h = w.hiker;
  float hr;
  const Vec2 head = hikerHeadPos(h, &hr);
  const float lie_y = h.feet.y;
  // The chipmunk comes from beyond his head and sits there (C5 review: the nearer edge put it on his
  // chest whenever he lay right of the middle, about one loop in ten).
  const int side = headSide(w);
  const bool late = s.sky_t >= 83.f;                               // the stars are going out: wind up
  switch (s.vis) {
    case V_WAIT:
      if (s.sky_t < kV1At) break;
      {
        const float ex = side < 0 ? -22.f : (float)SS_WIDTH + 22.f;
        const Vec2 to{head.x + (float)side * kChipLook, lie_y - 6.f};
        if (spawnVis(w, 0, CR_CHIPMUNK, Vec2{ex, directorDryY(w, to, ex)}, -side)) {
          directorWalk(w, m.c[0], to, kChipIn, CA_MOVE);
          visTo(w, VC_IN);
        } else {
          visTo(w, V_GAP);
        }
      }
      break;
    case VC_IN:
      if (!own(w, 0)) { visTo(w, V_GAP); break; }
      if (critterArrived(crit(w, 0))) {
        critterSetAction(crit(w, 0), CA_LOOK, 0.4f);
        visTo(w, VC_LOOK);
      }
      break;
    case VC_LOOK:
      if (!own(w, 0)) { visTo(w, V_GAP); break; }
      if (s.vt >= 1.5f) {
        critterSetAction(crit(w, 0), CA_IDLE, 0.3f);
        directorWalk(w, m.c[0], Vec2{head.x + (float)side * kChipStand, lie_y - 6.f}, kChipNear, CA_MOVE);
        m.u1 = fabsf(crit(w, 0).feet.x - (head.x + (float)side * kChipStand));   // the way still to go
        visTo(w, VC_NEAR);
      }
      break;
    case VC_NEAR: {
      if (!own(w, 0)) { visTo(w, V_GAP); break; }
      Critter& c = crit(w, 0);
      // As it comes it grows: by the ground covered, so a slow chipmunk grows slowly.
      const float left = fabsf(c.feet.x - (head.x + (float)side * kChipStand));
      const float go = m.u1 > 1.f ? clamp01(1.f - left / m.u1) : 1.f;
      c.scale = lerp(1.f, kChipBig, easeInOut(go));
      if (critterArrived(c)) {
        c.scale = kChipBig;
        critterSetAction(c, CA_SIT, 0.8f);
        visTo(w, VC_SNIFF);
      }
      break;
    }
    case VC_SNIFF:
      if (!own(w, 0)) { visTo(w, V_GAP); break; }
      if (crossed(s.vt, dt, 1.0f)) critterSetAction(crit(w, 0), CA_SNIFF, 0.5f);
      if (s.vt >= 4.0f) {
        critterSetAction(crit(w, 0), CA_SIT, 0.5f);
        visTo(w, VC_HOLD);
      }
      break;
    case VC_HOLD:   // four seconds of nothing
      if (!own(w, 0)) { visTo(w, V_GAP); break; }
      if (s.vt >= 4.0f) {
        critterSetAction(crit(w, 0), CA_LOOK, 0.5f);
        visTo(w, VC_HELLO);
      }
      break;
    case VC_HELLO:
      helloArm(w, s.vt, 3.4f);
      if (s.vt >= 3.4f) {
        scriptSetArm(w, 1, 0.f, 0.f, 0.f);
        if (own(w, 0)) {
          critterSetAction(crit(w, 0), CA_IDLE, 0.4f);
          directorExit(w, m.c[0], side, kChipOut, CA_MOVE);
        }
        visTo(w, VC_OFF);
      }
      break;
    case VC_OFF:
      if (own(w, 0)) crit(w, 0).scale = lerp(kChipBig, 1.f, easeInOut(clamp01(s.vt / 3.f)));
      if (s.vt >= 3.f) visTo(w, VC_GONE);
      break;
    case VC_GONE:
      if (m.c[0] < 0 || !w.crit[m.c[0]].in_use || w.crit[m.c[0]].tag != CT_MAJOR) visTo(w, V_GAP);
      break;
    case V_GAP:
      if (late) { visTo(w, V_DONE); break; }
      if (s.vt < kGapS || s.sky_t < kV2AtSlow) break;
      {
        const float ex = (float)SS_WIDTH + 44.f;
        const Vec2 to{minf(h.feet.x + 120.f, 508.f), 346.f};
        if (spawnVis(w, 1, CR_DEER, Vec2{ex, directorDryY(w, to, ex)}, -1)) {
          directorWalk(w, m.c[1], to, kDeerIn, CA_MOVE);
          visTo(w, VD_IN);
        } else {
          visTo(w, V_DONE);
        }
      }
      break;
    case VD_IN:
      if (!own(w, 1)) { visTo(w, V_DONE); break; }
      if (critterArrived(crit(w, 1))) {
        crit(w, 1).facing = -1;
        critterSetAction(crit(w, 1), CA_LOOK, 0.6f);
        visTo(w, VD_LOOK);
      }
      break;
    case VD_LOOK:
      if (!own(w, 1)) { visTo(w, V_DONE); break; }
      if (s.vt >= 1.5f) visTo(w, VD_GROW);
      break;
    case VD_GROW: {   // ten seconds, eased, tips last; from 0.7 grown the leaves open one by one
      if (!own(w, 1)) { visTo(w, VD_BACK); break; }
      const float g = easeInOut(clamp01(s.vt / 10.f));
      s.antler_q = (uint8_t)(g * 64.f + 0.5f);
      const float open = (s.vt - 7.6f) / 0.4f;
      s.leaves = (uint8_t)(open <= 0.f ? 0.f : (open >= 8.f ? 8.f : open + 1.f));
      if (s.vt >= 11.2f) {
        s.leaves = 8;
        const int b = tripPropNew(s, TP_BIRD);
        if (b >= 0) {
          const Vec2 tip = tripAntlerTip(s, 5);
          s.prop[b].pos = Vec2{tip.x + 74.f, tip.y - 12.f};
          s.prop[b].vel = Vec2{-1.f, 0.f};
          s.prop[b].rot = tip.x + 74.f;   // where its flight began
        }
        visTo(w, VD_BIRD);
      }
      break;
    }
    case VD_BIRD: {   // the small bird flutters down onto a tip
      const float u = easeInOut(clamp01(s.vt / 1.8f));
      const Vec2 tip = tripAntlerTip(s, 5);
      for (int i = 0; i < TRIP_PROPS; ++i) {
        TripProp& p = s.prop[i];
        if (p.kind != TP_BIRD) continue;
        p.pos = Vec2{floorf(lerp(p.rot, tip.x, u) + 0.5f), floorf(tip.y - 12.f * (1.f - u) - 5.f * sinT(u * 0.5f) + 0.5f)};
        if (u >= 1.f) p.mode = 1;
      }
      if (s.vt >= 1.8f) visTo(w, VD_STILL);
      break;
    }
    case VD_STILL:   // the deer turns its head, slowly; five seconds of stillness
      if (own(w, 1) && crossed(s.vt, dt, 1.2f)) critterSetAction(crit(w, 1), CA_HEADTURN, 1.2f);
      if (s.vt >= 6.2f || !own(w, 1)) {
        for (int i = 0; i < TRIP_PROPS; ++i) {
          TripProp& p = s.prop[i];
          if (p.kind != TP_BIRD) continue;
          p.mode = 0;
          p.vel = Vec2{1.f, 0.f};
          p.rot = p.pos.x;
          p.k = p.pos.y;
        }
        static const int kLet[3] = {1, 4, 6};
        for (int j = 0; j < 3; ++j) {
          const int l = tripPropNew(s, TP_LEAF);
          if (l < 0) break;
          s.prop[l].pos = tripAntlerTip(s, kLet[j]);
          s.prop[l].vel = Vec2{-2.f + 2.f * fl(j), 8.f + 1.5f * fl(j)};
          s.prop[l].rot = 0.31f * fl(j);
          s.prop[l].t = -0.5f * fl(j);   // one by one
        }
        visTo(w, VD_SHED);
      }
      break;
    case VD_SHED: {   // the bird flies off; the leaves close
      const float u = clamp01(s.vt / 1.6f);
      for (int i = 0; i < TRIP_PROPS; ++i) {
        TripProp& p = s.prop[i];
        if (p.kind != TP_BIRD) continue;
        p.pos = Vec2{floorf(p.rot + 80.f * u * u + 0.5f), floorf(p.k - 16.f * u + 0.5f)};
        if (u >= 1.f) p.kind = TP_NONE;
      }
      const float shut = s.vt / 0.3f;
      s.leaves = (uint8_t)(shut >= 8.f ? 0.f : 8.f - floorf(shut));
      if (s.vt >= 3.0f) {
        s.leaves = 0;
        visTo(w, VD_BACK);
      }
      break;
    }
    case VD_BACK: {   // the tree draws back in over four seconds
      const float g = 1.f - easeInOut(clamp01(s.vt / 4.f));
      s.antler_q = (uint8_t)(g * 64.f + 0.5f);
      s.leaves = 0;
      if (s.vt >= 4.f) {
        s.antler_q = 0;
        if (own(w, 1)) {
          critterSetAction(crit(w, 1), CA_IDLE, 0.4f);
          directorExit(w, m.c[1], 1, kDeerOut, CA_MOVE);
        }
        visTo(w, VD_OUT);
      }
      break;
    }
    case VD_OUT:
      if (m.c[1] < 0 || !w.crit[m.c[1]].in_use || w.crit[m.c[1]].tag != CT_MAJOR) visTo(w, V_DONE);
      break;
    default: break;
  }
  // Wind up early if the sky is about to settle and something is still here.
  if (late && s.vis >= VC_IN && s.vis <= VC_HOLD && own(w, 0)) {
    critterSetAction(crit(w, 0), CA_IDLE, 0.4f);
    directorExit(w, m.c[0], side, kChipOut, CA_MOVE);
    visTo(w, VC_OFF);
  }
  if (s.sky_t >= 92.f && s.vis >= VD_IN && s.vis <= VD_STILL) visTo(w, VD_SHED);
  // The tall ones: the chipmunk grown, the deer and its tree. One band every second tick meanwhile (F3).
  bool tall = false;
  if (own(w, 0) && crit(w, 0).scale > 1.25f) tall = true;
  if (s.vis >= VD_IN && s.vis <= VD_OUT && own(w, 1)) {
    const Rect b = critterBounds(crit(w, 1));
    if (b.x0 < SS_WIDTH && b.x1 > 0) tall = true;
  }
  s.slow = tall ? 1 : 0;
}

// Set 1, the water. Three fish leave the stream one every 6 s, each in a slow high arc downstream, each a
// different pastel, each going back in with one ring and no splash, all at x <= 520. Then five smoke rings
// rise from the cold pit one every 4 s, widening and thinning; a smaller one passes up through the third.
// Nothing here stands taller than row 320 (the rings stop at row 322), so the sky keeps its full pace.
void set1Tick(World& w, float dt) {
  TripState& s = S(w);
  if (s.vis == V_WAIT && s.sky_t >= kV1At) visTo(w, VW_FISH);
  if (s.vis == VW_FISH) {
    static const float kX0[3] = {500.f, 448.f, 396.f};
    for (int j = 0; j < 3; ++j) {
      if (!crossed(s.vt, dt, 0.2f + 6.f * fl(j))) continue;
      const int i = tripPropNew(s, TP_FISH);
      if (i < 0) continue;
      TripProp& p = s.prop[i];
      p.tint = (uint8_t)(j * 2);
      p.rot = kX0[j];
      p.t = 0.f;
      p.k = 0.f;
      p.pos = Vec2{kX0[j], waterY(w, kX0[j], 420.f)};
      ring(w, p.pos, 0.9f);
    }
    for (int i = 0; i < TRIP_PROPS; ++i) {
      TripProp& p = s.prop[i];
      if (p.kind != TP_FISH) continue;
      constexpr float kArc = 3.2f, kUp = 60.f, kAlong = 110.f;
      const float u = clamp01(p.t / kArc);
      const float x0 = p.rot, x1 = p.rot - kAlong;
      const float y0 = waterY(w, x0, 420.f), y1 = waterY(w, x1, 430.f);
      p.k = u;
      p.pos = Vec2{floorf((x0 + (x1 - x0) * u) * 2.f + 0.5f) * 0.5f, floorf((y0 + (y1 - y0) * u - 4.f * kUp * u * (1.f - u)) * 2.f + 0.5f) * 0.5f};
      p.vel = Vec2{x1 - x0, (y1 - y0) - 4.f * kUp * (1.f - 2.f * u)};
      if (u >= 1.f) {
        ring(w, p.pos, 0.9f);
        p.kind = TP_NONE;
      }
    }
    if (s.vt >= 24.f) visTo(w, VW_GAP);
  } else if (s.vis == VW_GAP) {
    if (s.vt >= kGapS && s.sky_t >= kV2At) visTo(w, VW_RINGS);
  } else if (s.vis == VW_RINGS) {
    for (int j = 0; j < 6; ++j) {   // five, and the small one that passes through the third
      const float at = j < 5 ? 0.2f + 4.f * fl(j) : 0.2f + 4.f * 2.f + 2.6f;
      if (!crossed(s.vt, dt, at)) continue;
      const int i = tripPropNew(s, TP_RING);
      if (i < 0) continue;
      s.prop[i].mode = (uint8_t)(j == 5 ? 1 : 0);
      s.prop[i].t = 0.f;
      s.prop[i].k = 0.f;
      s.prop[i].rot = 0.f;
    }
    const Vec2 pit = w.fire.pos;
    for (int i = 0; i < TRIP_PROPS; ++i) {
      TripProp& p = s.prop[i];
      if (p.kind != TP_RING) continue;
      const float life = p.mode ? 5.4f : 9.f;
      const float u = clamp01(p.t / life);
      const float rise = p.mode ? 60.f : 62.f;
      p.k = floorf((p.mode ? 0.45f * u : u) * 48.f + 0.5f) * (1.f / 48.f);   // its width in 48 steps
      p.pos = Vec2{floorf(pit.x - 5.f * u + 0.5f), floorf(pit.y - 8.f - rise * u + 0.5f)};
      if (u >= 1.f) p.kind = TP_NONE;
    }
    if (s.vt >= 26.f) visTo(w, V_DONE);
  }
  s.slow = 0;
}

// One tick of the sky's part of the beat (plan 5.4): his breath, the free layers' weights, the dog, what he
// sees, the arm that traces the sky, and the end.
void skyTick(World& w, float dt) {
  TripState& s = S(w);
  MajorState& m = M(w);
  Hiker& h = w.hiker;
  s.T += dt;
  s.vt += dt;
  const float st = s.sky_t;
  const float out = 1.f - easeInOut(clamp01((st - 90.f) / 8.f));
  const float deep = easeInOut(clamp01(s.T / 10.f)) * out;   // his breathing slows and deepens
  h.breath_rate = lerp(1.f, 0.6f, deep);
  h.breath_scale = lerp(1.f, 1.6f, deep);
  s.kg = easeInOut(clamp01((st - 14.f) / 10.f)) * out;
  dogTick(w, dt);
  if (s.frog) s.slow = 0;   // the frog's play: the sky alone, no visions, full pace
  else if (s.set == 0) set0Tick(w, dt);
  else set1Tick(w, dt);
  // 88 to 97 on the sky clock: he lifts the near arm straight up and slowly traces one wave with it.
  const float tr = (st - kTraceAt) / kTraceS;
  s.trace = clamp01(tr);
  if (tr > 0.f && tr < 1.f) {
    const float t = tr * kTraceS;
    const float up = easeInOut(clamp01(t / 3.f)) * (1.f - easeInOut(clamp01((t - 7.f) / 2.f)));
    scriptSetArm(w, 0, up, 1.57f + 0.30f * sinT((t - 3.f) * 0.25f) * clamp01((t - 2.5f) / 1.f), 0.08f);
  } else if (tr >= 1.f) {
    w.fx.arm_w[0] = 0.f;
  }
  // The sky's 104 s are up. The story goes by the sky clock alone, never by what the renderer has
  // repainted: a band that had to wait (F28) is put back by tripSkyTrack's way home, and from 102 s every
  // band latched is natural already, so in ordinary running all ten are back by now.
  if (st >= (float)SKYTRIP_SECONDS) {
    s.sky_on = 0;
    s.slow = 0;
    s.kg = 0.f;
    h.breath_rate = h.breath_scale = 1.f;
    scriptFxRest(w);
    dogBack(w);   // he is about to sit up: she goes on with her day
    m.cue = (uint16_t)(m.cue | kSkyOver);
  }
}

// ---- the frog he licks (MJ_FROG_LICK; 2026-11-25) --------------------------------------------------------------------
//
// The same DAY_TRIP, sky clock and free layers as the mushroom; TripState::frog marks the play, and skyTick runs no
// visions for it. The frog is critter c[0] (CT_MAJOR, brave). While it sits on his near hand, tripDayStep puts its
// feet there every tick (after his rig, so it is this tick's hand; also through the doorbell's freeze) and sorts it
// in front of him; the director does not move it (it is not walking, and brave). Tuning PROPOSED; how it looks is
// UNVERIFIED on the panel.

enum : uint8_t { FR_IN = 0, FR_SIT, FR_HELD, FR_DOWN, FR_OFF, FR_GONE };
constexpr float kFrogIn = 46.f;      // px/s hopping in (the frog_splash minor's 42)
constexpr float kFrogOut = 56.f;     // hopping off after the fright
constexpr float kFrogGap = 30.f;     // he stands this far left of it to pick it up
constexpr float kGripS = 0.45f;      // it settles onto his palm over this long
// Its size: at 1 (the frog_splash minor's) it is wider than his torso and four times his head; at 0.75 it reads
// as a big frog in one hand (PROPOSED).
constexpr float kFrogScale = 0.75f;
constexpr int kFrogBeatS = 180;      // the beat's seconds: the script is done by about 166 s (the sky's 104 of them)

bool liveFrog(const World& w) { return w.day.kind == DAY_TRIP && w.major.on && w.major.id == MJ_FROG_LICK && S(w).frog; }
Critter* frogCritter(World& w) {
  const int c = w.major.c[0];
  return c >= 0 && w.crit[c].in_use && w.crit[c].tag == CT_MAJOR ? &w.critters[c] : nullptr;
}
void frogTo(World& w, uint8_t st) {
  S(w).frog_st = st;
  S(w).frog_t = 0.f;
}
// The frog's feet on his palm: his hand this tick plus the grip (it settles from where he took it), lowered to the
// ground as he sets it down, never under the ground.
Vec2 frogOnHand(const World& w) {
  const TripState& s = S(w);
  const Hiker& h = w.hiker;
  const Vec2 hand = hikerHandPos(h, 0);
  const Vec2 rest{(float)h.facing * 1.f, 3.f};
  const float k = easeInOut(s.grip_k);
  Vec2 f{hand.x + lerp(s.grip0.x, rest.x, k), hand.y + lerp(s.grip0.y, rest.y, k)};
  const float gy = scriptGroundAt(w, f.x, h.feet.y);
  if (s.lower > 0.f) f.y = lerp(f.y, gy, easeInOut(s.lower));
  if (f.y > gy) f.y = gy;
  return Vec2{floorf(f.x * 2.f + 0.5f) * 0.5f, floorf(f.y * 2.f + 0.5f) * 0.5f};
}
// tripDayStep: the frog rides his hand. Frozen by the doorbell his hand still moves (the hop), so this runs then
// too, and marks the frog's bounds itself (the director does not run then).
void frogFollow(World& w, bool frozen) {
  TripState& s = S(w);
  const int c = w.major.c[0];
  if (!s.held || c < 0 || !w.crit[c].in_use) return;
  if (!frozen && s.grip_k < 1.f) s.grip_k = minf(1.f, s.grip_k + SS_TICK_DT / kGripS);
  Critter& f = w.critters[c];
  f.feet = frogOnHand(w);
  f.facing = -w.hiker.facing;   // it looks at him
  w.crit[c].sort_y = (int16_t)(floorf(w.hiker.feet.y) + 2.f);   // in front of him (his hands' things are at + 1)
  w.crit[c].path_on = false;
  if (frozen) {
    const Rect r = critterBounds(f);
    f.visible = r.x1 > 0 && r.x0 < SS_WIDTH;
    w.critter_rect[c] = f.visible ? r : Rect{0, 0, 0, 0};
  }
}

// The frog's own stages (the major's tick: frozen with the story).
void frogTick(World& w, float dt) {
  TripState& s = S(w);
  s.frog_t += dt;
  Critter* c = frogCritter(w);
  switch (s.frog_st) {
    case FR_IN:
      if (!c) { frogTo(w, FR_GONE); break; }
      if (critterArrived(*c)) {
        c->facing = w.hiker.feet.x < c->feet.x ? -1 : 1;
        critterSetAction(*c, CA_LOOK, 0.4f);
        frogTo(w, FR_SIT);
      }
      break;
    case FR_SIT:   // it sits and blinks; it looks his way
      if (!c) { frogTo(w, FR_GONE); break; }
      if (crossed(s.frog_t, dt, 1.4f) || crossed(s.frog_t, dt, 4.6f)) critterSetAction(*c, CA_BLINK, 0.3f);
      if (crossed(s.frog_t, dt, 2.2f) || crossed(s.frog_t, dt, 5.4f)) critterSetAction(*c, CA_IDLE, 0.3f);
      if (crossed(s.frog_t, dt, 3.0f)) critterSetAction(*c, CA_LOOK, 0.4f);
      if (!w.hiker.walking) c->facing = w.hiker.feet.x < c->feet.x ? -1 : 1;
      break;
    case FR_DOWN:   // set down: a startle, then off to the right in long hops
      if (!c) { frogTo(w, FR_GONE); break; }
      if (s.frog_t >= 0.55f) {
        c->facing = 1;
        critterSetAction(*c, CA_IDLE, 0.15f);
        directorExit(w, w.major.c[0], 1, kFrogOut, CA_HOP);
        CritterSlot& cs = w.crit[w.major.c[0]];   // along the camp side, never through the water
        cs.path_y1 = directorDryY(w, c->feet, cs.path_x1);
        frogTo(w, FR_OFF);
      }
      break;
    case FR_OFF:   // the director lets it go once it is out of sight
      if (!c) frogTo(w, FR_GONE);
      break;
    default: break;   // FR_HELD: tripDayStep has it, the acts give it its blink
  }
}

// He has seen it: a lean and a look down at it, his head tipping as he watches.
void actFrogSpot(World& w, float u) {
  const float e = env(u, 0.2f, 0.25f);
  w.fx.lean = 0.07f * e;
  w.fx.head = (0.12f + 0.09f * easeInOut(clamp01((u - 0.45f) / 0.2f))) * e;
  if (u >= 1.f) scriptFxRest(w);
}
// Crouched by it: the near arm reaches, scoops it up and comes back with it.
void actFrogPick(World& w, float u) {
  TripState& s = S(w);
  w.fx.reach = easeInOut(clamp01(u / 0.32f)) * (1.f - easeInOut(clamp01((u - 0.62f) / 0.30f)));
  w.fx.reach_to = 0.78f;
  w.fx.head = 0.16f * env(u, 0.2f, 0.25f);
  Critter* c = frogCritter(w);
  if (u >= 0.42f && !s.held && c && s.frog_st == FR_SIT) {
    s.held = 1;
    s.grip_k = 0.f;
    s.lower = 0.f;
    s.grip0 = Vec2{c->feet.x - s.hand[0].x, c->feet.y - s.hand[0].y};
    critterSetAction(*c, CA_IDLE, 0.2f);
    reedsDropChew(w);   // a stem in his mouth falls as he takes it (it is going to his face)
    frogTo(w, FR_HELD);
  }
  if (u >= 1.f) scriptFxRest(w);
}
// The tongue this tick: out by `k` (0..1) from his mouth toward the frog, a little down.
void setTongue(World& w, float k) {
  TripState& s = S(w);
  if (k <= 0.12f) {
    s.tongue = 0;
    s.tongue_a = s.tongue_b = Vec2{0.f, 0.f};
    return;
  }
  const Hiker& h = w.hiker;
  const Vec2 m = hikerMouthPos(h);
  const float f = (float)h.facing, len = 7.5f * k;
  s.tongue = (uint8_t)(1.f + len);
  s.tongue_a = Vec2{floorf(m.x * 2.f + 0.5f) * 0.5f, floorf(m.y * 2.f + 0.5f) * 0.5f};
  s.tongue_b = Vec2{floorf((m.x + f * 0.94f * len) * 2.f + 0.5f) * 0.5f, floorf((m.y + 0.34f * len) * 2.f + 0.5f) * 0.5f};
}
// Up to the face (6.4 s): out at arm's length and a long look; to his mouth; two quick licks, the frog blinks;
// a frozen beat; out again, one more look; and down to his side.
constexpr float kExamS = 6.4f;
constexpr float kOutSh = 1.25f, kOutEl = 0.35f;      // out ahead at chest height, a little down
// The frog just in front of and below his mouth, its back at his lips: the hand 12 px ahead of the shoulder and
// 2 below (the arm is 0.16 + 0.15 H: the elbow bends 2.24, the upper arm 0.35 from down; PROPOSED).
constexpr float kMouthSh = 0.35f, kMouthEl = 2.24f;
void actFrogLick(World& w, float u) {
  TripState& s = S(w);
  const float t = u * kExamS;
  const float k = easeInOut(clamp01(t / 0.6f)) * (1.f - easeInOut(clamp01((t - 5.8f) / 0.6f)));
  const float m = easeInOut(clamp01((t - 2.1f) / 0.7f)) * (1.f - easeInOut(clamp01((t - 4.7f) / 0.6f)));
  scriptSetArm(w, 0, k, lerp(kOutSh, kMouthSh, m), lerp(kOutEl, kMouthEl, m));
  // The licks: his head goes in and back, twice, quickly.
  const float l1 = t > 2.85f && t < 3.30f ? sinT(0.5f * (t - 2.85f) / 0.45f) : 0.f;
  const float l2 = t > 3.40f && t < 3.85f ? sinT(0.5f * (t - 3.40f) / 0.45f) : 0.f;
  const float lick = l1 + l2;
  const float look = (0.10f * k + 0.07f * sinT(clamp01((t - 1.0f) / 0.8f) * 0.5f)) * (1.f - m);   // at it; a cock of the head
  const float stun = -0.07f * easeInOut(clamp01((t - 3.9f) / 0.2f)) * (1.f - easeInOut(clamp01((t - 4.6f) / 0.2f)));   // the frozen beat
  w.fx.head = look + 0.22f * lick + stun;
  w.fx.lean = 0.05f * lick;
  setTongue(w, lick);
  Critter* c = frogCritter(w);
  if (c && s.held) {
    if (crossed(t, SS_TICK_DT, 3.05f)) critterSetAction(*c, CA_BLINK, 0.25f);
    if (crossed(t, SS_TICK_DT, 3.9f)) critterSetAction(*c, CA_IDLE, 0.4f);
    if (crossed(t, SS_TICK_DT, 5.3f)) critterSetAction(*c, CA_LOOK, 0.4f);
  }
  if (u >= 1.f) {
    setTongue(w, 0.f);
    scriptFxRest(w);
  }
}
// Crouched again: the hand goes down to the grass and opens. It startles; he draws his hand back.
void actFrogDown(World& w, float u) {
  TripState& s = S(w);
  w.fx.reach = easeInOut(clamp01(u / 0.35f)) * (1.f - easeInOut(clamp01((u - 0.62f) / 0.30f)));
  w.fx.reach_to = 0.78f;
  w.fx.head = 0.14f * env(u, 0.2f, 0.25f);
  if (s.held) s.lower = clamp01((u - 0.12f) / 0.30f);
  if (u >= 0.45f && s.held) {
    Critter* c = frogCritter(w);
    if (c) {
      c->feet = frogOnHand(w);
      c->feet.y = scriptGroundAt(w, c->feet.x, w.hiker.feet.y);
      c->facing = 1;
      w.crit[w.major.c[0]].sort_y = 0;
      critterSetAction(*c, CA_STARTLE, 0.12f);
      frogTo(w, FR_DOWN);
    } else {
      frogTo(w, FR_GONE);
    }
    s.held = 0;
    s.lower = 0.f;
  }
  if (u >= 1.f) scriptFxRest(w);
}
// The taste: he rears back, shakes his head hard four times, spits, and wipes his mouth with the back of his
// near hand.
void actYuck(World& w, float u) {
  const float t = u * 3.6f;
  const float back = easeInOut(clamp01(t / 0.25f)) * (1.f - easeInOut(clamp01((t - 1.8f) / 0.2f)));
  const float shake_e = easeInOut(clamp01((t - 0.2f) / 0.15f)) * (1.f - easeInOut(clamp01((t - 1.65f) / 0.2f)));
  const float shake = 0.34f * sinT(clamp01((t - 0.25f) / 1.6f) * 4.f) * shake_e;   // four in 1.6 s
  const float spit = t > 1.95f && t < 2.35f ? sinT(0.5f * (t - 1.95f) / 0.4f) : 0.f;
  const float wipe_k = easeInOut(clamp01((t - 2.35f) / 0.25f)) * (1.f - easeInOut(clamp01((t - 3.25f) / 0.3f)));
  const float wipe = sinT(clamp01((t - 2.5f) / 0.75f) * 0.5f);   // across the mouth once, out and back in
  w.fx.lean = -0.09f * back + 0.13f * spit;
  w.fx.head = -0.12f * back + shake + 0.30f * spit + 0.08f * wipe_k;
  scriptSetArm(w, 0, wipe_k, lerp(1.35f, 1.0f, wipe), lerp(1.9f, 2.55f, wipe));
  if (u >= 1.f) scriptFxRest(w);
}
bool frogLanded(const World& w) { return !liveFrog(w) || S(w).frog_st != FR_IN; }
bool frogHeld(const World& w) { return liveFrog(w) && S(w).held != 0; }

const Step kFrog[] = {
    // He notices it hopping in along the bank.
    pose(POSE_STAND, 0.8f, 0.3f),
    faceTo(AT_M2), act(actFrogSpot, 2.0f), until(frogLanded), still(0.4f),
    // Over to it; crouch; pick it up.
    go(AT_M1, kStroll), face(RIGHT), still(0.4f),
    pose(POSE_CROUCH, 0.8f, 0.2f), act(actFrogPick, 1.6f),
    pose(POSE_STAND, 0.8f, 0.2f),
    // Held out, looked at, licked twice; a frozen beat; out again.
    skipUnless(frogHeld, 1), act(actFrogLick, kExamS),
    // Set down: it startles and hops off.
    skipUnless(frogHeld, 3), pose(POSE_CROUCH, 0.8f, 0.2f), act(actFrogDown, 1.4f), pose(POSE_STAND, 0.8f, 0.2f),
    // The taste.
    act(actYuck, 3.6f),
    pose(POSE_HANDS_ON_HIPS, 0.6f, 1.6f),
    // He lies down where the mushroom has him lie; in a few seconds it comes on, and the sky lets go.
    go(AT_LIE, kStroll),
    face(RIGHT), pose(POSE_CROUCH, 0.7f, 0.2f), pose(POSE_SIT, 0.9f, 1.0f), pose(POSE_LIE, 1.6f, 1.0f),
    still(1.0f), doit(cueOnset), still(2.5f), doit(cueSky), until(skyOver),
    // After, as the mushroom's.
    still(2.0f), act(actStir, 2.2f), pose(POSE_SIT, 1.4f, 1.2f), act(actRubEyes, 2.0f), act(actHeadShake, 1.4f),
    pose(POSE_CROUCH, 0.6f, 0.2f), pose(POSE_STAND, 0.9f, 0.3f),
    until(crittersGone),
    go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f),
};

// The band's rows may join the renderer's waiting repaints without making one of them taller than two
// bands (REVIEW_FIXES F6, F28: Renderer::repaintRows keeps two bands and widens the second for a third).
bool rowsFit(const Renderer& r, int y0, int y1) {
  if (r.patch_n < 2) return true;
  const int lo = y0 < r.patch_y0[1] ? y0 : r.patch_y0[1];
  const int hi = y1 > r.patch_y1[1] ? y1 : r.patch_y1[1];
  return hi - lo <= 2 * SKYTRIP_BAND_ROWS;
}

}  // namespace

// ---- the beat ---------------------------------------------------------------------------------------------------

uint8_t tripPlanVar(uint32_t r_var) {
  return (uint8_t)(((r_var >> 13) % (uint32_t)kTripSets) | (((r_var >> 5) % 4u == 0u) ? kVarPocket : 0u));
}

int tripSeconds(uint8_t var) {
  const int set = var & kVarSet;
  return kSeconds[set < kTripSets ? set : 0];
}

const Step* tripScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  n = (uint8_t)(sizeof(kTrip) / sizeof(kTrip[0]));
  return kTrip;
}

void tripMajorBegin(World& w) {
  MajorState& m = M(w);
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  // A rain the plan could not know of (no OUTSIDE reading), or no pickable can stand this loop: he only
  // looks at the sky (dayNoShowScript). The patch is not picked and waits for another loop. The beat keeps
  // its planned seconds (REVIEW_FIXES F27): he sits it out. Rare.
  LsPatch pt;
  patchLayout(w.land, pt);
  uint8_t bits = 0;
  int n = 0;
  ShroomLook look[3];
  for (int k = 0; k < 3; ++k) {
    if (pt.m[MUSH_PICK0 + k].step == 0 || pt.m[MUSH_PICK0 + k].lifted) continue;
    look[n++] = patchLook(pt, MUSH_PICK0 + k);
    bits = (uint8_t)(bits | (1u << k));
  }
  if (dayWet(w) || n == 0 || mushStage(w.land.patch_stage) != MUSH_RIPE) {
    m.var = (uint8_t)(m.var | DAY_NOSHOW);
    return;
  }
  w.day.kind = DAY_TRIP;
  TripState& s = S(w);
  s.set = (uint8_t)((m.var & kVarSet) < kTripSets ? (m.var & kVarSet) : 0);
  s.pocket = (m.var & kVarPocket) ? 1 : 0;
  s.n_pick = (uint8_t)n;
  s.pick_i = 3;
  for (int k = 0; k < n; ++k) s.look[k] = look[k];
  m.mode = bits;   // the pickables' bits for Landscape::patch_lifted (doLift)
  // His places (plan 3.2). M1: the crouch at the patch. M0: the camp bank where he steps in, 50 px upstream
  // of it, clear of the cairn. M2: midstream. M3: where he tosses the rest.
  const Vec2 lie = w.spine.camp.lie;
  float x0 = fl(pt.nx) - 50.f;
  if (fabsf(w.spine.camp.cairn.x - x0) < 26.f) x0 = w.spine.camp.cairn.x - 30.f;
  // (C5 review: crouched at the patch's cap, nx 501, his reaching arm stood at x 535, and a doorbell's hop
  // and look in the middle of a pick lifted it to 538, a pixel into the chip's box. 4 px further left.)
  m.p[1] = Vec2{minf(fl(pt.nx), 497.f), fl(pt.ny)};
  m.p[0] = Vec2{x0, bankY(w, x0) - 4.f};
  m.p[2] = Vec2{0.5f * (m.p[0].x + m.p[1].x), 0.5f * (m.p[0].y + m.p[1].y)};
  m.p[3] = Vec2{lie.x + 30.f, bankY(w, lie.x + 30.f) - 3.f};
}

// ---- the frog he licks: the beat -------------------------------------------------------------------------------------

uint8_t frogPlanVar(uint32_t) { return 0; }

int frogSeconds(uint8_t) { return kFrogBeatS; }

const Step* frogScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  n = (uint8_t)(sizeof(kFrog) / sizeof(kFrog[0]));
  return kFrog;
}

void frogMajorBegin(World& w) {
  MajorState& m = M(w);
  w.memory.taichi_next = (uint8_t)(w.memory.taichi_next & ~FROG_ASK);   // asked for once (the carry word): done
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  // Its place: on the camp side of the stream, left of the camp pine's corner and well left of the status chip
  // (x 538 on), a little up the grass from the water's edge. He stands kFrogGap to its left to pick it up.
  // The first of five places right of where he will lie that is clear of the camp bank's reeds and the cairn
  // (in a clump it hides; PROPOSED), else the first.
  const Vec2 lie = w.spine.camp.lie;
  static const float kTry[5] = {120.f, 90.f, 150.f, 60.f, 180.f};
  float fx = clampf(lie.x + kTry[0], 300.f, 500.f);
  for (int k = 0; k < 5; ++k) {
    const float x = clampf(lie.x + kTry[k], 300.f, 500.f);
    bool clear = fabsf(w.spine.camp.cairn.x - x) >= 30.f && fabsf(w.spine.camp.cairn.x - (x - kFrogGap)) >= 22.f;
    for (int c = 0; c < REED_CLUMPS && clear; ++c) {
      ReedClumpInfo ri;
      if (reedsClump(w, c, ri) && ri.present && fabsf(ri.base.x - x) < ri.half_w + 16.f) clear = false;
    }
    if (clear) {
      fx = x;
      break;
    }
  }
  const float fy = scriptGroundAt(w, fx, bankY(w, fx) - 16.f);
  m.p[0] = Vec2{fx, fy};
  m.p[1] = Vec2{fx - kFrogGap, scriptGroundAt(w, fx - kFrogGap, fy)};
  // A rain the plan could not know of: he only looks at the sky (dayNoShowScript), as the mushroom's does.
  if (dayWet(w)) {
    m.var = (uint8_t)(m.var | DAY_NOSHOW);
    return;
  }
  // The frog: in from the right along the camp side. No free slot: the no-show too (rare).
  const float ex = (float)SS_WIDTH + 16.f;
  const Vec2 from{ex, directorDryY(w, m.p[0], ex)};
  const int sl = directorSpawn(w, CR_FROG, from, -1);
  m.c[0] = (int8_t)sl;
  if (sl < 0) {
    m.var = (uint8_t)(m.var | DAY_NOSHOW);
    return;
  }
  w.crit[sl].tag = CT_MAJOR;
  w.crit[sl].flags = CF_BRAVE;
  memoryApplyPersona(w.memory, w.critters[sl]);
  w.critters[sl].scale = kFrogScale;
  directorWalk(w, sl, m.p[0], kFrogIn, CA_HOP);
  m.p[2] = from;   // where he first looks
  w.day.kind = DAY_TRIP;
  TripState& s = S(w);
  s.frog = 1;
  s.pick_i = 3;
  s.frog_st = FR_IN;
}

void tripMajorTick(World& w, float dt) {
  if (!liveAny(w)) return;
  TripState& s = S(w);
  MajorState& m = M(w);
  if (s.frog) {
    frogTick(w, dt);
  } else {
    tripStep(s, dt);
    capsTick(w, dt);
    wadeStride(w, dt);
  }
  if (m.cue & kOnset) {   // it has come on: the field leans in one slow wave until the sky settles
    s.onset_t += dt;
    s.ks = easeInOut(clamp01(s.onset_t / 8.f)) * (1.f - easeInOut(clamp01((s.sky_t - 90.f) / 8.f)));
  }
  if (s.sky_on) skyTick(w, dt);
}

void tripMajorEnd(World& w) {
  if (w.day.kind == DAY_TRIP && !S(w).solo) {
    TripState& s = S(w);
    if (s.dog) dogBack(w);
    if (s.sky_on) scriptFxRest(w);   // an overrun inside the sky: the tick held his arms
    w.hiker.breath_rate = w.hiker.breath_scale = 1.f;
    // (An overrun with the sky still on leaves bands away from natural: tripSkyTrack puts them back two a
    // tick, REVIEW_FIXES F2.)
    memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where its things were)
  }
}

bool tripHandsFull(const World& w) {
  if (liveFrog(w)) return S(w).held != 0;   // the frog on his hand
  if (!live(w)) return false;
  const TripState& s = S(w);
  return !s.bunch_down && (s.in_far > 0 || s.in_near != 0);
}

void tripSkyOnly(World& w, bool slow) {
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));
  w.day.kind = DAY_TRIP;
  TripState& s = S(w);
  s.solo = 1;
  s.sky_on = 1;
  s.slow = slow ? 1 : 0;
  s.pick_i = 3;
}

float tripSkyClock(const World& w) { return w.day.kind == DAY_TRIP ? S(w).sky_t : -1.f; }

// ---- the day hooks -------------------------------------------------------------------------------------------------

Rect tripDayBounds(const DayTrip& d) { return tripBounds(d.t); }

void tripDayPose(World&) {}

// He lies: the bell does not turn him. The frog's play: from the time he lies down until he sits up.
bool tripDayHoldsFacing(const World& w) {
  return (S(w).sky_on && !S(w).solo) || (S(w).frog && w.hiker.target == POSE_LIE);
}

void tripDayStep(World& w, bool frozen) {
  TripState& s = S(w);
  if (!s.solo) {
    // What the draws read from the world, written into the state so they stay a pure function of it.
    s.hand[0] = hikerHandPos(w.hiker, 0);
    s.hand[1] = hikerHandPos(w.hiker, 1);
    if (s.frog) frogFollow(w, frozen);   // the frog on his hand (the frog's play)
    if (frozen && s.tongue) setTongue(w, 0.f);   // the bell: the tongue goes in (the lick sets it again after)
    const MajorState& m = w.major;
    if (s.antler_q && m.c[1] >= 0 && w.crit[m.c[1]].in_use) {
      const Vec2 c = critterDeerCrown(w.critters[m.c[1]]);
      s.antler_x = (int16_t)floorf(c.x + 0.5f);
      s.antler_y = (int16_t)floorf(c.y + 0.5f);
      s.vis_y = (int16_t)(floorf(w.critters[m.c[1]].feet.y) + 1.f);
      for (int i = 0; i < TRIP_PROPS; ++i)   // the perched bird rides the tip
        if (s.prop[i].kind == TP_BIRD && s.prop[i].mode == 1) s.prop[i].pos = tripAntlerTip(s, 5);
    } else if (s.set == 1) {
      s.vis_y = (int16_t)(s.vis == VW_RINGS ? (int)floorf(w.fire.pos.y) + 2 : SS_HEIGHT - 1);
    }
    // Part 0's sort line: the pickables' lowest foot, the laid bunch, or the first floating cap.
    int16_t gy = 0;
    if (s.lifted && s.picked < s.n_pick) {
      for (int k = s.picked; k < (int)s.n_pick; ++k)
        if ((int16_t)s.look[k].y > gy) gy = (int16_t)s.look[k].y;
    } else if (s.bunch_down) {
      gy = (int16_t)s.bunch_at.y;
    } else {
      for (int i = 0; i < TRIP_PROPS && gy == 0; ++i)
        if (s.prop[i].kind == TP_CAP) gy = (int16_t)(floorf(w.hiker.feet.y) + 2.f);
    }
    s.ground_y = gy;
  }
  if (frozen || !s.sky_on) return;   // the doorbell: the sky clock and the band counter stand still
  // The sky clock (step C5b; REVIEW_FIXES F3): one band is due every `every` ticks and the clock moves one
  // tick's worth per band, so at every 2 it runs at half speed.
  const int every = s.slow ? 2 : kTripBandEvery;
  s.phase = (uint8_t)((s.phase + 1) % every);
  if (s.phase == 0) {
    s.sky_t += kTripClockStep;
    s.due = 1;
  }
  if (s.solo && s.sky_t >= (float)SKYTRIP_SECONDS) {
    memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the test hook's sky is over
  }
}

void tripSkyTrack(World& w, Renderer& r) {
  LsTrip& lt = w.land.trip;
  if (w.day.kind == DAY_TRIP && S(w).sky_on) {
    TripState& s = S(w);
    if (!s.due) return;
    s.due = 0;
    const int i = s.band % SKYTRIP_BANDS;
    const uint16_t v = skyTripLatch(s.sky_t);
    if (v != lt.tq[i]) {
      const int y0 = SKYTRIP_BAND_ROWS * i, y1 = y0 + SKYTRIP_BAND_ROWS;
      // A sun or moon repaint is waiting, or frames are running behind: this band waits a tick. Its
      // latched clock simply stays older, and the band counter does not move (F6, F28).
      if (!rowsFit(r, y0, y1)) return;
      lt.tq[i] = v;
      r.repaintRows(y0, y1);
      r.markDirtyRows(y0, y1);
    }
    s.band = (uint8_t)((i + 1) % SKYTRIP_BANDS);
    return;
  }
  // No trip is running but a band is still away from natural (the beat was cut short): back two bands a
  // tick through the same small repaints, never the whole sky in one frame (F2).
  int done = 0;
  for (int i = 0; i < SKYTRIP_BANDS && done < 2; ++i) {
    if (lt.tq[i] == 0) continue;
    const int y0 = SKYTRIP_BAND_ROWS * i, y1 = y0 + SKYTRIP_BAND_ROWS;
    if (!rowsFit(r, y0, y1)) break;
    lt.tq[i] = 0;
    r.repaintRows(y0, y1);
    r.markDirtyRows(y0, y1);
    ++done;
  }
}

void tripDayTrack(const DayTrip& prev, const DayTrip& cur, const World& w, Renderer& r) {
  Rect rr[TRIP_DIRTY_MAX];
  const int n = tripDirty(prev.t, cur.t, rr);
  for (int i = 0; i < n; ++i)
    if (!rr[i].empty() && rr[i].x1 > 0 && rr[i].x0 < SS_WIDTH) r.markDirtyRect(rr[i]);
  if (prev.t.lifted != cur.t.lifted) {   // the pickables left the cache: its rows there are drawn again without them
    const Rect pb = patchBounds(w.land, true).clipped(Rect{0, 0, SS_WIDTH, SS_HEIGHT});
    if (!pb.empty()) {
      r.repaintRows(pb.y0, pb.y1);
      r.markDirtyRect(pb);
    }
  }
}

bool tripDayPartY(const World& w, int part, int16_t& y) {
  const TripState& s = S(w);
  if (part < 0 || part >= TRIP_PARTS || tripPartBounds(s, part).empty()) return false;
  if (part == 0) y = s.ground_y;
  else if (part == 1) y = (int16_t)(floorf(w.hiker.feet.y) + (s.frog ? 3.f : 1.f));   // in his hands: in front of him (the tongue over the frog)
  else y = s.vis_y;
  return true;
}

void tripDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal) {
  tripDraw(S(w), part, s, clip, pal);
}

}  // namespace ss
