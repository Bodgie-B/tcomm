// Morning Tai Chi as a surprise major. See day_taichi.h.
//
// OWNERSHIP: step C2 (the integrator). The form itself is taichi.h (step B2): this file is the World side.
//
// The script is the hiker's half: the walk to the bank, the hat off and laid on the grass, the step down to
// the practice line, `until(formDone)` while the module poses him, then the hat again and the walk to the
// fire. The beat's tick (taichiMajorTick) runs the module's clock (so the doorbell freezes the form for
// free), reads its events and drives the guests: the heron that mirrors White Crane (chapter 1), the bird
// that lands on the hook hand of Single Whip (2), the chipmunk that copies him (3), the deer at the tree
// line (4), and Copper's bump. No random draws: everything comes from the plan's byte, the layout and the
// module's clock.
//
// Mike, 2026-11-18: "a slow progression of many exercises and not be in a hurry to finish": nothing here
// shortens or skips a posture. The beat's seconds are the form's own plus the walks; the loop is fitted
// round it (majorKept, spineFitLength's floor).
//
// Every look number is PROPOSED until Mike has seen the stills; nothing is checked on the panel.
#include "day_taichi.h"
#include "day_trip.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "director.h"
#include "housedogs.h"
#include "landscape.h"
#include "memory.h"
#include "reeds.h"
#include "stream.h"
#include "taichi.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

MajorState& M(World& w) { return w.major; }
DayTaichi& D(World& w) { return w.day.taichi; }
bool live(const World& w) { return w.day.kind == DAY_TAICHI && w.major.on && w.major.id == MJ_TAICHI; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

// ---- the variant byte (TAICHI_PLAN 6): bits 0-1 the chapter, bit 3 DAY_NOSHOW, bits 4-5 the gag, bit 6 guests
constexpr uint8_t kVarChapter = 3, kVarGagShift = 4, kVarGuests = 64;

// ---- tuning (PROPOSED; how it looks is UNVERIFIED) ---------------------------------------------------------
constexpr float kWalkIn = 28.f, kWalkOut = 24.f;   // slower than the 34 stroll: he is not going anywhere
constexpr float kHatSh = 2.75f, kHatEl = 2.0f;     // the near arm up to the brim (as the midday's hat)
constexpr float kBirdIn = 2.1f, kBirdOff = 2.5f;   // the bird's flights, s
constexpr float kChipRun = 70.f, kDeerWalk = 22.f, kDeerNear = 14.f, kDeerOut = 28.f;
constexpr float kCopperTrot = 60.f, kCopperOn = 40.f;
constexpr float kBumpCue = 0.8f;                   // seconds into the bump key's hold the module calls for him (taichi.cpp kBumpAt)
constexpr float kKnow = 0.06f;                     // rad: his head lifts when the bird lands
constexpr float kGlance = 0.10f, kGlanceS = 1.4f;  // a fish jumps: his head turns to it and back
constexpr float kFishNear = 160.f;
// Beat seconds by chapter (TAICHI_PLAN 5.3: the script plus slack for the longer walks), before the gag's.
const int kSeconds[4] = {143, 153, 145, 145};

float bankY(const World& w, float x) {   // the camp side's water edge at x
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? cy - half : 386.f;
}

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}
void flyAlong(Critter& c, Vec2 a, Vec2 ctl, Vec2 b, float u) {
  const Vec2 p = bez(a, ctl, b, u);
  if (fabsf(p.x - c.feet.x) > 0.05f) c.facing = p.x < c.feet.x ? -1 : 1;
  c.feet = p;
}

// ---- the guest's critter (MajorState::c[0], its stage and stage clock) -------------------------------------

bool own(const World& w) { return w.major.c[0] >= 0 && directorOwns(w, w.major.c[0], CT_MAJOR); }
Critter& cr(World& w) { return w.critters[M(w).c[0]]; }
void stageTo(World& w, int st) {
  M(w).stage[0] = (uint8_t)st;
  M(w).st[0] = 0.f;
}
bool spawnGuest(World& w, CritterKind kind, Vec2 feet, int facing) {
  MajorState& m = M(w);
  const int s = directorSpawn(w, kind, feet, facing);
  m.c[0] = (int8_t)s;
  if (s < 0) return false;
  w.crit[s].tag = CT_MAJOR;
  w.crit[s].flags = CF_BRAVE;
  memoryApplyPersona(w.memory, w.critters[s]);
  return true;
}
inline bool crossed(float t, float dt, float at) { return t >= at && t - dt < at; }

// The hand behind him: the hook hand of the single whip.
int hookHand(const TaichiState& s) {
  const float f = s.facing < 0 ? -1.f : 1.f;
  return (s.hand[0].x - s.hand[1].x) * f <= 0.f ? 0 : 1;
}
Vec2 perchOn(const TaichiState& s) {   // the bird's feet on the hook hand (guests read TaichiState::hand: it follows the drawing)
  const Vec2 h = s.hand[hookHand(s)];
  return Vec2{h.x, h.y - 2.f};
}

// ---- his actions --------------------------------------------------------------------------------------------

bool hatOnP(const World& w) { return w.hiker.hat_on; }
bool hatDownP(const World& w) { return !w.hiker.hat_on && w.items[ITEM_HAT].down; }

// The near hand up to the brim, a small bow of the head, the hat comes off and is held.
void actHatOff(World& w, float u) {
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  const float e = scriptEnvelope(u, 0.35f, 0.35f);
  scriptSetArm(w, 0, e, kHatSh, kHatEl);
  w.fx.head = 0.12f * e;
  if (u >= 0.45f && h.hat_on) {
    h.hat_on = false;
    hat.held = true;
    hat.down = false;
    hat.critter = 0;
    hat.on_fire = false;
    hat.facing = (int8_t)h.facing;
    hat.size = 1.f;
  }
  if (u >= 1.f) scriptFxRest(w);
}
void doHatDown(World& w, float) {
  if (w.hiker.hat_on) return;
  scriptSetDown(w, ITEM_HAT, M(w).p[1]);
}
void doHatUp(World& w, float) {
  Item& hat = w.items[ITEM_HAT];
  hat.down = false;
  hat.held = true;
  hat.facing = (int8_t)w.hiker.facing;
  if (live(w)) taichiFlyLeave(D(w).t);   // he walks off: the butterfly goes
}
void actHatOn(World& w, float u) {
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  const float e = scriptEnvelope(u, 0.4f, 0.4f);
  scriptSetArm(w, 0, e, kHatSh, kHatEl);
  if (u >= 0.45f && !h.hat_on) {
    h.hat_on = true;
    hat.held = hat.down = false;
  }
  if (u >= 1.f) scriptFxRest(w);
}

// The module comes on: its weight eases in over the first 1.5 s of the opening stillness.
void doFormBegin(World& w, float) {
  if (!live(w)) return;
  DayTaichi& d = D(w);
  MajorState& m = M(w);
  const Hiker& h = w.hiker;
  const uint8_t chapter = (uint8_t)(m.var & kVarChapter), gag = (uint8_t)((m.var >> kVarGagShift) & 3u);
  const float g = clampf(((float)w.land.camp_x - 73.f - m.p[0].x) / 50.4f, 0.85f, 1.f);   // TAICHI_PLAN 3: the pine's skirt
  // Small per-loop variation in sink and hand height (never in timing): from the layout and the loop count.
  const uint32_t vary = (uint32_t)(m.p[0].x * 16.f) * 2654435761u ^ (w.memory.loop_count * 40503u);
  const uint8_t guests = d.guests;
  taichiInit(d.t, chapter, gag, h.feet.x, h.feet.y, g, 1, vary);
  d.began = 1;
  // The bump's call (the module raises it kBumpCue into its bump key's hold): Copper is sent for ahead of it.
  d.bump_at = -1.f;
  if (gag == TG_BUMP) {
    for (int k = 0; k < taichiKeyCount(chapter); ++k)
      if (taichiKey(chapter, k).flags & TK_BUMP) {
        d.bump_at = taichiKeyArrive(chapter, k) + kBumpCue * taichiPace();
        break;
      }
  }
  // The butterfly: the warm months (or no clock), and only with the guests.
  const uint8_t mo = w.stream.month;
  taichiFlyEnable(d.t, guests && (mo == 0 || (mo >= 4 && mo <= 9)));
}
bool formDone(const World& w) { return !live(w) || !w.day.taichi.began || w.day.taichi.finished; }
bool guestsGone(const World& w) {
  const int s = w.major.c[0];
  return s < 0 || !w.crit[s].in_use || w.crit[s].tag != CT_MAJOR;
}

const Step kTai[] = {
    pose(POSE_STAND, 0.8f, 0.3f),
    go(AT_M2, kWalkIn), face(LEFT),
    skipUnless(hatOnP, 4), act(actHatOff, 2.2f), pose(POSE_CROUCH, 0.7f, 0.2f), doit(doHatDown), pose(POSE_STAND, 0.8f, 0.4f),
    walk(AT_M0, 16.f), face(RIGHT), still(1.2f),   // down the bank to the line; he looks upstream
    doit(doFormBegin),
    until(formDone),
    still(2.0f),
    go(AT_M2, kWalkOut), face(LEFT),
    skipUnless(hatDownP, 5), pose(POSE_CROUCH, 0.7f, 0.2f), doit(doHatUp), pose(POSE_STAND, 0.7f, 0.2f), act(actHatOn, 2.0f), still(0.8f),
    until(guestsGone),
    go(AT_SIT, kWalkIn), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f),
};

// ---- the guests -----------------------------------------------------------------------------------------------

// Chapter 1: the stream's own heron (stream.h). It stands beside the cattails when RS_NEAR is there
// (INTEGRATION_PLAN 3.2): 30 px or more right of the clump's base.
float heronX(const World& w) {  // (reads the major's points)
  float x = w.major.p[0].x + 30.f;
  ReedClumpInfo ci;
  if (reedsClump(w, RS_NEAR, ci) && ci.present) x = maxf(x, ci.base.x + 30.f);
  return clampf(x, 400.f, 480.f);
}
void heronTick(World& w, uint16_t ev, float dt) {
  DayTaichi& d = D(w);
  if (d.heron && !streamHeronIsGuest(w)) d.heron = 0;   // a dog sent it up, or the stream took it back
  if ((ev & TE_GUEST) && d.guests && !d.heron && streamHeronGuest(w, heronX(w), 1)) d.heron = 1;
  if (!d.heron) return;
  if (ev & TE_SIGN) {   // white crane: one leg up, a wing half open, a beat behind him (the stream eases it)
    streamHeronMirror(w, true);
    d.heron = 2;
    d.fold_t = 0.f;
  }
  if (ev & TE_SIGN_END) d.fold_t = 0.8f;
  if (d.fold_t > 0.f) {
    d.fold_t -= dt;
    if (d.fold_t <= 0.f) {
      d.fold_t = 0.f;
      streamHeronMirror(w, false);
      d.heron = 1;
    }
  }
  if (ev & TE_CLOSING) {   // it goes, unhurried, while he closes
    streamHeronRelease(w);
    d.heron = 0;
  }
}

// Chapter 2: a small bird comes down on the hook hand of the single whip, looks about, and goes as the arm
// comes in.
enum { BS_NONE = 0, BS_IN, BS_SIT, BS_HOP, BS_OFF };
void birdOff(World& w) {
  if (!own(w)) return;
  M(w).v0 = cr(w).feet;
  critterSetAction(cr(w), CA_HOP, 0.15f);
  stageTo(w, BS_HOP);
}
void birdTick(World& w, uint16_t ev, float dt) {
  DayTaichi& d = D(w);
  MajorState& m = M(w);
  const TaichiState& s = d.t;
  const float side = s.hand[hookHand(s)].x >= s.hip_x ? 1.f : -1.f;   // the side the hook hand is on
  if ((ev & TE_SIGN) && d.guests && m.stage[0] == BS_NONE && m.c[0] < 0) {
    const Vec2 from{side > 0.f ? (float)SS_WIDTH + 24.f : -24.f, 150.f};
    if (spawnGuest(w, CR_BIRD, from, side > 0.f ? -1 : 1)) {
      w.crit[m.c[0]].sort_y = (int16_t)(s.y0 + 2.f);   // in front of him
      critterSetAction(cr(w), CA_MOVE, 0.1f);
      m.v0 = from;
      stageTo(w, BS_IN);
    }
  }
  float know = 0.f;
  if (m.stage[0] != BS_NONE && own(w)) {
    Critter& c = cr(w);
    const float t = m.st[0];
    const Vec2 on = perchOn(s);
    switch (m.stage[0]) {
      case BS_IN: {
        const float u = clamp01(t / kBirdIn);
        flyAlong(c, m.v0, Vec2{lerp(m.v0.x, on.x, 0.6f), on.y - 46.f}, on, easeOutCubic(u));
        if (u >= 1.f) {
          c.facing = s.facing < 0 ? -1 : 1;
          critterSetAction(c, CA_IDLE, 0.25f);
          stageTo(w, BS_SIT);
        }
        if (ev & TE_SIGN_END) birdOff(w);   // (the hold ended under it: it turns away)
        break;
      }
      case BS_SIT:
        c.feet = on;
        know = 1.f;
        if (crossed(t, dt, 0.8f)) critterSetAction(c, CA_LOOK, 0.25f);
        if (ev & (TE_SIGN_END | TE_CLOSING)) birdOff(w);
        break;
      case BS_HOP:
        c.feet = on;
        if (t >= 0.35f) {
          m.v0 = c.feet;
          critterSetAction(c, CA_MOVE, 0.1f);
          stageTo(w, BS_OFF);
        }
        break;
      case BS_OFF: {
        const Vec2 to{side > 0.f ? (float)SS_WIDTH + 24.f : -24.f, 110.f};
        const float u = clamp01(t / kBirdOff);
        flyAlong(c, m.v0, Vec2{lerp(m.v0.x, to.x, 0.35f), m.v0.y - 60.f}, to, easeInOut(u));
        if (u >= 1.f) {
          directorRelease(w, m.c[0]);
          m.c[0] = -1;
          stageTo(w, BS_NONE);
        }
        break;
      }
      default: break;
    }
  }
  d.know += clampf(know - d.know, -dt / 0.8f, dt / 0.8f);   // his head lifts: he knows
}

// Chapter 3: a chipmunk runs in along the bank, sits up facing him, drops when he creeps down in the snake,
// and stands up taller and stiller than he does for the rooster.
enum { CS_NONE = 0, CS_IN, CS_UP, CS_DOWN, CS_TALL, CS_OUT, CS_EXIT };
Vec2 chipSpot(const World& w) { return w.major.p[3]; }
Vec2 chipEdge(const World& w) {   // where it comes from: the right edge, on a line that stays on dry ground
  const Vec2 sp = chipSpot(w);
  return Vec2{(float)SS_WIDTH + 22.f, directorDryY(w, sp, (float)SS_WIDTH + 22.f)};
}
void chipTick(World& w, uint16_t ev, float dt) {
  DayTaichi& d = D(w);
  MajorState& m = M(w);
  if ((ev & TE_GUEST) && d.guests && m.stage[0] == CS_NONE && m.c[0] < 0) {
    if (spawnGuest(w, CR_CHIPMUNK, chipEdge(w), -1)) {
      directorWalk(w, m.c[0], chipSpot(w), kChipRun, CA_RUN);
      stageTo(w, CS_IN);
    }
  }
  if (m.stage[0] == CS_NONE || !own(w)) return;
  Critter& c = cr(w);
  const float t = m.st[0];
  switch (m.stage[0]) {
    case CS_IN:
      if (critterArrived(c)) {
        c.facing = -1;   // he is downstream of it
        critterSetAction(c, CA_SIT, 0.3f);
        // The upstream cattails stand at the waterline just below it: it sits in front of them.
        ReedClumpInfo ci;
        if (reedsClump(w, RS_UPSTREAM, ci) && ci.present && fabsf(ci.base.x - c.feet.x) < ci.half_w + 14.f)
          w.crit[m.c[0]].sort_y = (int16_t)(ci.base.y + 2.f);
        stageTo(w, CS_UP);
      }
      break;
    case CS_UP:
      if (ev & TE_SIGN) {   // the snake: down on all fours as he goes down
        critterSetAction(c, CA_IDLE, 0.5f);
        stageTo(w, CS_DOWN);
      } else if (ev & TE_SIGN2) {
        stageTo(w, CS_DOWN);
      }
      break;
    case CS_DOWN:
      if (ev & TE_SIGN2) M(w).u0 = 0.6f;   // the rooster: up again, 0.6 s behind him
      if (M(w).u0 > 0.f) {
        M(w).u0 -= dt;
        if (M(w).u0 <= 0.f) {
          M(w).u0 = 0.f;
          critterSetAction(c, CA_SIT, 0.35f);
          stageTo(w, CS_TALL);
        }
      }
      break;
    case CS_TALL:
      if (ev & TE_GAG) critterSetAction(c, CA_STARTLE, 0.15f);        // he wobbles: it starts,
      if (ev & TE_GAG) M(w).u1 = 0.7f;
      if (M(w).u1 > 0.f) {                                            // then sits bolt upright through his recovery
        M(w).u1 -= dt;
        if (M(w).u1 <= 0.f) {
          M(w).u1 = 0.f;
          critterSetAction(c, CA_SIT, 0.3f);
        }
      }
      if (crossed(t, dt, 7.f) && M(w).u1 <= 0.f) critterSetAction(c, CA_LOOK, 0.4f);   // (F22: the chipmunk has no head turn)
      break;
    case CS_OUT:
      if (critterArrived(c)) {
        directorExit(w, m.c[0], 1, kChipRun, CA_RUN);
        stageTo(w, CS_EXIT);
      }
      break;
    default: break;
  }
  if ((ev & TE_CLOSING) && m.stage[0] >= CS_IN && m.stage[0] <= CS_TALL) {   // back the way it came
    const Vec2 e = chipEdge(w);
    const Vec2 sp = c.feet;
    const float x = (float)SS_WIDTH - 14.f;
    const float u = (x - sp.x) / (e.x - sp.x);
    w.crit[m.c[0]].sort_y = 0;
    directorWalk(w, m.c[0], Vec2{x, lerp(sp.y, e.y, u)}, kChipRun, CA_RUN);
    stageTo(w, CS_OUT);
  }
}

// Chapter 4: a deer walks in beyond the pine and watches, grazing between looks; its head goes up at the
// kick; when he folds down out of its sight (the needle) it comes three steps nearer.
enum { DS_NONE = 0, DS_IN, DS_WATCH, DS_UP, DS_NEAR, DS_WATCH2, DS_OUT };
Vec2 deerSpot(const World& w) { return w.major.p[3]; }
// Graze 3 to 5 s, head up 2 s, from the stage clock (INTEGRATION_PLAN 3.2: a deer at the tree line, not a statue).
void deerGraze(World& w, float t, float dt) {
  Critter& c = cr(w);
  const float cyc = 6.4f, ph = fmodf(t, cyc);
  if (crossed(ph, dt, 2.0f)) critterSetAction(c, CA_EAT, 0.8f);
  else if (ph < dt) critterSetAction(c, CA_LOOK, 0.6f);
}
void deerLeave(World& w) {
  if (!own(w)) return;
  critterSetAction(cr(w), CA_IDLE, 0.4f);
  directorExit(w, M(w).c[0], 1, kDeerOut, CA_MOVE);
  stageTo(w, DS_OUT);
}
bool deerAllowed(const World& w) {
  for (int i = 0; i < w.plan.n_majors; ++i)
    if (w.plan.major[i] == MJ_DEER_STICK) return false;   // one deer story a loop (INTEGRATION_PLAN 4.8)
  const StreamVisit& v = w.stream.visit;
  return !(v.kind == SV_DEER && v.stage >= GS_IN && v.stage <= GS_OUT);
}
void deerTick(World& w, uint16_t ev, float dt) {
  DayTaichi& d = D(w);
  MajorState& m = M(w);
  if ((ev & TE_GUEST) && d.guests && m.stage[0] == DS_NONE && m.c[0] < 0 && deerAllowed(w)) {
    const Vec2 sp = deerSpot(w);
    const float ex = (float)SS_WIDTH + 40.f;
    if (spawnGuest(w, CR_DEER, Vec2{ex, directorDryY(w, sp, ex)}, -1)) {
      directorWalk(w, m.c[0], sp, kDeerWalk, CA_MOVE);
      stageTo(w, DS_IN);
    }
  }
  if (m.stage[0] == DS_NONE || !own(w)) return;
  Critter& c = cr(w);
  const float t = m.st[0];
  switch (m.stage[0]) {
    case DS_IN:
      if (critterArrived(c)) {
        c.facing = -1;
        critterSetAction(c, CA_LOOK, 0.6f);
        stageTo(w, DS_WATCH);
      }
      break;
    case DS_WATCH:
    case DS_WATCH2:
      deerGraze(w, t, dt);
      if (ev & TE_GAG) critterSetAction(c, CA_HEADTURN, 0.4f);   // he wobbles: an ear, a look
      break;
    case DS_UP:
      if (ev & TE_GAG) critterSetAction(c, CA_HEADTURN, 0.4f);
      if (t > 7.f) stageTo(w, DS_WATCH);   // and back to its grass
      break;
    case DS_NEAR:
      if (critterArrived(c)) {
        c.facing = -1;
        critterSetAction(c, CA_LOOK, 0.6f);
        stageTo(w, DS_WATCH2);
      }
      break;
    default: break;
  }
  if (m.stage[0] >= DS_WATCH && m.stage[0] <= DS_WATCH2 && m.stage[0] != DS_NEAR) {
    if (ev & TE_SIGN) {   // the kick: its head goes up
      critterSetAction(c, CA_LOOK, 0.3f);
      stageTo(w, DS_UP);
    }
    if (ev & TE_SIGN2) {   // the needle: three steps nearer
      const Vec2 sp = deerSpot(w);
      directorWalk(w, m.c[0], Vec2{sp.x - 26.f, sp.y}, kDeerNear, CA_MOVE);
      stageTo(w, DS_NEAR);
    }
  }
  if ((ev & TE_CLOSING) && m.stage[0] >= DS_IN && m.stage[0] <= DS_WATCH2) deerLeave(w);
}

// A house dog is about after all: the wild guests go, calmly (TAICHI_PLAN 6.3).
void guestsOff(World& w) {
  DayTaichi& d = D(w);
  MajorState& m = M(w);
  d.guests = 0;
  if (d.heron) {   // handed back to the stream: its own rules send it up when the dog comes near
    streamHeronMirror(w, false);
    streamHeronHandBack(w);
    d.heron = 0;
  }
  if (!own(w)) return;
  const int ch = d.t.chapter;
  if (ch == 1) {
    if (m.stage[0] == BS_IN || m.stage[0] == BS_SIT) birdOff(w);
  } else if (ch == 2) {
    if (m.stage[0] >= CS_IN && m.stage[0] <= CS_TALL) {
      w.crit[m.c[0]].sort_y = 0;
      directorExit(w, m.c[0], 1, kChipRun, CA_RUN);
      stageTo(w, CS_EXIT);
    }
  } else if (ch == 3) {
    if (m.stage[0] >= DS_IN && m.stage[0] <= DS_WATCH2) deerLeave(w);
  }
}

// ---- Copper's bump (TAICHI_PLAN 6.3) ----------------------------------------------------------------------------

void bumpOver(World& w) {
  DayTaichi& d = D(w);
  if (d.dog >= 0) houseDogsGiveBack(w);
  d.dog = -1;
  d.bump = 3;
}
void bumpTick(World& w, uint16_t ev) {
  DayTaichi& d = D(w);
  TaichiState& s = d.t;
  if (d.bump_at < 0.f || d.bump == 3) return;
  const HouseDog& cop = w.dogs.d[HD_COPPER];
  const int cs = CRITTER_STORY + HD_COPPER;
  if (d.bump == 0) {
    if (s.t > d.bump_at) {   // the moment passed with no dog: the module waits it out and adds stillness at the end
      d.bump = 3;
      return;
    }
    if (cop.phase != HP_HERE || cop.slot != cs || !w.crit[cs].in_use || !w.critters[cs].visible) return;
    // Where he will stand at the bump's key, and Copper's way there: sent so that he arrives just after the call.
    int bk = 0;
    for (int k = 0; k < taichiKeyCount(s.chapter); ++k)
      if (taichiKey(s.chapter, k).flags & TK_BUMP) bk = k;
    const float hip = s.x0 + (float)s.dir * 0.01f * (float)taichiKey(s.chapter, bk).hip * s.g * w.hiker.height;
    const Critter& c = w.critters[cs];
    const int side = c.feet.x <= hip ? 1 : -1;
    const Vec2 to{hip - (float)side * 12.f, s.y0 + 3.f};
    const float lead = fabsf(to.x - c.feet.x) / kCopperTrot - 0.5f;
    if (s.t < d.bump_at - lead) return;
    const int got = houseDogsLend(w, HD_COPPER);
    if (got != cs) {   // Lucy, or nobody: no bump
      if (got >= 0) houseDogsGiveBack(w);
      d.bump = 3;
      return;
    }
    d.dog = (int8_t)cs;
    d.side = (int8_t)side;
    d.sat = 0;
    critterSetAction(w.critters[cs], CA_IDLE, 0.3f);
    directorWalk(w, cs, to, kCopperTrot, CA_MOVE);
    d.bump = 1;
    return;
  }
  if (w.dogs.lent != HD_COPPER + 1) {   // he went back indoors: the house has him
    d.dog = -1;
    d.bump = 3;
    return;
  }
  Critter& c = w.critters[d.dog];
  if (d.bump == 1) {
    if (s.phase == TP_WAIT && critterArrived(c)) {
      if (taichiGag(s, TG_BUMP, d.side)) {   // he shoulders past: the stumble goes the way he pushed
        directorWalk(w, d.dog, Vec2{s.hip_x + (float)d.side * 24.f, s.y0 + 3.f}, kCopperOn, CA_MOVE);
        d.bump = 2;
      } else {
        bumpOver(w);
      }
    } else if (s.gag_done) {   // the module gave up waiting
      bumpOver(w);
    }
    return;
  }
  // Playing: Copper sits and looks up at him; his own wag runs on (housedogs.cpp).
  if (!d.sat && critterArrived(c)) {
    c.facing = -d.side;
    critterSetAction(c, CA_SIT, 0.4f);
    d.sat = 1;
  }
  if ((ev & TE_GAG_END) || s.phase != TP_GAG) bumpOver(w);
}

float glanceEnv(float t) {
  if (t <= 0.f || t >= kGlanceS) return 0.f;
  return easeInOut(clamp01(minf(t / 0.4f, (kGlanceS - t) / 0.5f)));
}

}  // namespace

// ---- the beat ------------------------------------------------------------------------------------------------

uint8_t taichiPlanVar(uint32_t r_var, const Memory& mem, int dog_out) {
  // The chapter: the memory's when it holds one (bit 7), else from the plan's own draw, so a fresh memory does
  // not always open with chapter 1 (INTEGRATION_PLAN 3.1 point 7).
  const uint32_t chapter = (mem.taichi_next & 0x80u) ? (mem.taichi_next & 3u) : ((r_var >> 9) & 3u);
  const uint32_t roll = (r_var >> 20) % 20u;
  uint32_t gag = roll >= 12u && roll <= 14u ? TG_WOBBLE : (roll >= 15u && roll <= 17u ? TG_ITCH : TG_NONE);
  if ((dog_out & 2) && roll >= 12u) gag = TG_BUMP;   // Copper is out: his bump instead
  const uint32_t guests = ((r_var >> 16) & 3u) != 0u ? kVarGuests : 0u;
  return (uint8_t)(chapter | (gag << kVarGagShift) | guests);
}

int taichiSeconds(uint8_t var) {
  if (var & DAY_NOSHOW) return 46;   // (never reached from a random rain: REVIEW_FIXES F27, see taichiMajorBegin)
  const int ch = var & kVarChapter;
  return kSeconds[ch] + (int)ceilf(taichiGagSeconds(ch, (uint8_t)((var >> kVarGagShift) & 3u)) - 1e-3f);
}

const Step* taichiScript(uint8_t var, uint8_t& n) {
  if (var & DAY_NOSHOW) return dayNoShowScript(n);
  n = (uint8_t)(sizeof(kTai) / sizeof(kTai[0]));
  return kTai;
}

void taichiMajorBegin(World& w) {
  MajorState& m = M(w);
  Memory& mem = w.memory;
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // the daytime majors share one union (day.h)
  reedsDropChew(w);   // the stem falls as he begins (INTEGRATION_PLAN 4.4; Mike, 2026-11-18)
  // The chapter this session plays is the memory's from now on, and the morning has had its tai chi: a
  // session that began counts (Mike, 2026-11-18), so a no-show or a form cut short can never force tai chi
  // into every morning loop. The chapter itself only advances when the form reaches its end (taichiMajorEnd).
  mem.taichi_next = (uint8_t)(0x80u | 4u | (m.var & kVarChapter) | (mem.taichi_next & (FROG_ASK | 0x30u)));   // (bit 3 is the frog's ask, 4-5 the Christmas evening's: xmas.h)
  // A rain the plan could not know of: he only looks at the sky (dayNoShowScript). The beat keeps its planned
  // seconds (the timeline was built at loop start, REVIEW_FIXES F27): he sits it out by the cold fire. Rare:
  // only a random rain, with no OUTSIDE reading.
  if (dayWet(w)) {
    m.var = (uint8_t)(m.var | DAY_NOSHOW);
    return;
  }
  w.day.kind = DAY_TAICHI;
  DayTaichi& d = D(w);
  d.dog = -1;
  // The practice line (TAICHI_PLAN 3): the camp-side bank just east of the cairn, 9 px above the water's edge.
  const float x0 = w.spine.camp.cairn.x + 18.f;
  const float y0 = clampf(minf(bankY(w, x0 - 8.f), bankY(w, x0 + 66.f)) - 9.f, 366.f, 392.f);
  m.p[0] = Vec2{x0, y0};
  m.p[1] = Vec2{x0 - 30.f, y0 - 16.f};   // the hat's place, up the bank behind the line
  m.p[2] = Vec2{x0 - 16.f, y0 - 16.f};   // where he stands to put it down and pick it up
  const int ch = m.var & kVarChapter;
  if (ch == 2) m.p[3] = Vec2{x0 + 78.f, y0 + 5.f};                                             // the chipmunk
  else if (ch == 3) m.p[3] = Vec2{clampf((float)w.land.camp_x + 74.f, 620.f, 650.f), 338.f};   // the deer
  else m.p[3] = m.p[0];
  // No wild guests while a house dog is about (they would not come; and the dogs chase the heron).
  d.guests = (m.var & kVarGuests) != 0 && w.dogs.want == 0;
}

void taichiMajorTick(World& w, float dt) {
  if (!live(w)) return;
  DayTaichi& d = D(w);
  if (!d.began || d.finished) return;
  TaichiState& s = d.t;
  taichiStep(s, dt);
  const uint16_t ev = taichiEvents(s);
  if (ev & TE_DONE) d.done = 1;
  if (d.guests && w.dogs.want != 0) guestsOff(w);
  switch (s.chapter) {
    case 0: heronTick(w, ev, dt); break;
    case 1: birdTick(w, ev, dt); break;
    case 2: chipTick(w, ev, dt); break;
    default: deerTick(w, ev, dt); break;
  }
  bumpTick(w, ev);
  // A fish jumps near him: his head turns to it and back; the form does not break (TAICHI_PLAN 6.1).
  const StreamFish& f = w.stream.fish;
  if (f.kind != FISH_JUMP) d.fish_seen = 0;
  else if (!d.fish_seen && f.t < 0.4f && fabsf((float)f.x - s.hip_x) < kFishNear && d.glance <= 0.f && s.phase == TP_FORM) {
    d.fish_seen = 1;
    d.glance = 1e-3f;
  }
  if (d.glance > 0.f) {
    d.glance += dt;
    if (d.glance >= kGlanceS) d.glance = 0.f;
  }
  if (taichiDone(s)) {   // the weight is back with the rig: the script goes on
    taichiFinish(s, w.hiker);
    d.finished = 1;
    d.know = 0.f;
    d.glance = 0.f;
  }
}

void taichiMajorEnd(World& w) {
  Memory& mem = w.memory;
  if (w.day.kind == DAY_TAICHI) {
    DayTaichi& d = D(w);
    if (d.began && !d.finished) taichiFinish(d.t, w.hiker);   // an overrun: his feet and breath as the rig has them
    if (d.done)   // the next chapter, next time; a waiting frog ask (bit 3) stays
      mem.taichi_next = (uint8_t)(0x80u | 4u | ((d.t.chapter + 1u) & 3u) | (mem.taichi_next & (FROG_ASK | 0x30u)));
    if (d.dog >= 0) houseDogsGiveBack(w);
    if (d.heron) {
      streamHeronMirror(w, false);
      streamHeronHandBack(w);
    }
  }
  // The hat is on his head again whatever the script did (majorEnd sets hat_on): never also on the grass.
  Item& hat = w.items[ITEM_HAT];
  hat.held = hat.down = false;
  w.hiker.foot_turn = 0.f;
  memset(static_cast<void*>(&w.day), 0, sizeof(DayState));   // (dayTrack marks where the butterfly was)
}

// ---- questions the rest of the scene asks ----------------------------------------------------------------------

bool taichiInForm(const World& w) { return live(w) && w.day.taichi.began && !w.day.taichi.finished; }
bool taichiHatInHand(const World& w) {
  return w.major.on && w.major.id == MJ_TAICHI && !w.hiker.hat_on && w.items[ITEM_HAT].held;
}
bool taichiWatchSpot(const World& w, Vec2& out) {
  if (!live(w)) return false;
  const Vec2 p = w.major.p[0];
  out = Vec2{p.x - 80.f, p.y - 2.f};   // left of the line and of his hat (the plan's x0 - 52 hid the hat behind her), facing him
  return true;
}
bool taichiFogWaits(const World& w) {
  return taichiInForm(w) && !taichiHolding(w.day.taichi.t, 2.0f);
}

// ---- the day hooks -----------------------------------------------------------------------------------------------

Rect taichiDayBounds(const DayTaichi& d) { return taichiFlyBounds(d.t); }

void taichiDayPose(World& w) {
  DayTaichi& d = D(w);
  if (!d.began || d.finished) return;
  taichiPose(d.t, w.hiker);
  // Small things laid over the form (pure functions of the state: this also runs frozen).
  w.hiker.pose.head += -kKnow * easeInOut(clamp01(d.know)) + kGlance * glanceEnv(d.glance);
}

bool taichiDayHoldsFacing(const World& w) { return w.day.taichi.began && !w.day.taichi.finished; }

void taichiDayStep(World& w, bool) {
  DayTaichi& d = D(w);
  if (d.began) taichiFlyStep(d.t, w.tick);   // ambient: it keeps circling round a frozen hand
}

void taichiDayTrack(const DayTaichi& prev, const DayTaichi& cur, const World&, Renderer& r) {
  Rect rr[TAICHI_DIRTY_MAX];
  const int n = taichiDirty(prev.t, cur.t, rr);
  for (int i = 0; i < n; ++i)
    if (!rr[i].empty() && rr[i].x1 > 0 && rr[i].x0 < SS_WIDTH) r.markDirtyRect(rr[i]);
}

bool taichiDayPartY(const World& w, int part, int16_t& y) {
  if (part != 1 || !taichiFlyShown(w.day.taichi.t)) return false;
  y = (int16_t)(floorf(w.hiker.feet.y) + 2.f);   // in front of him
  return true;
}

void taichiDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal) {
  if (part == 1) taichiFlyDraw(w.day.taichi.t, s, clip, pal);
}

}  // namespace ss
