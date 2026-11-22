// Lucy and Copper in camp. See housedogs.h.
#include "housedogs.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day_herd.h"
#include "day_plink.h"
#include "director.h"
#include "day_taichi.h"
#include "landscape.h"
#include "majors.h"
#include "minors.h"
#include "script.h"
#include "stream.h"
#include "world.h"

namespace ss {

namespace {

constexpr float kDt = SS_TICK_DT;

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Each dog's nature. PROPOSED.
struct Nature {
  uint8_t tone;        // DogTone
  float trot, run;     // px/s
  float calm;          // 0 busy .. 1 calm: how long it holds what it is doing
  float wag_rate;      // wag cycles a second
  float wag;           // its usual swing (Critter::tail_add)
  float speed_dial, jitter_dial;   // the rig's own dials (critters.h), fixed: always the same dogs
};
const Nature kNature[HD_COUNT] = {
    {DT_LUCY, 50.f, 125.f, 1.f, 1.5f, 0.28f, 0.35f, 0.2f},
    {DT_COPPER, 44.f, 105.f, 0.45f, 3.4f, 0.36f, 0.8f, 0.85f},
};

const char* const kActNames[HA_COUNT] = {"none", "roam", "tent", "fire", "drink", "watch",
                                         "leaf", "pat",  "nap",  "play", "settle", "chase"};

// ---- small helpers -------------------------------------------------------------------------

uint32_t nextR(HouseDog& d) {
  uint32_t x = d.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  d.rng = x ? x : 0x9E3779B9u;
  return d.rng;
}
float rnd(HouseDog& d, float a, float b) { return a + (b - a) * (float)(nextR(d) >> 8) * (1.f / 16777216.f); }

int slotOf(int k) { return CRITTER_STORY + k; }
bool inScene(const World& w, const HouseDog& d) {
  return d.slot >= 0 && w.crit[d.slot].in_use && w.crit[d.slot].tag == CT_HOUSEDOG;
}
Critter& cr(World& w, const HouseDog& d) { return w.critters[d.slot]; }

// critterMoveTo scales its speed by the dial (0.8..1.2): undo it, so a speed is a speed.
float vArg(const Critter& c, float v) { return v / (0.8f + 0.4f * c.speed_dial); }
void walkTo(World& w, HouseDog& d, Vec2 to, float v, CritterAction gait) {
  directorWalk(w, d.slot, to, vArg(cr(w, d), v), gait);
}
// Turn on the spot to face `dir` (a tiny move the rig turns round for).
void faceTo(World& w, HouseDog& d, int dir) {
  Critter& c = cr(w, d);
  if (dir != 0 && c.facing != dir) walkTo(w, d, Vec2{c.feet.x + 1.5f * (float)dir, c.feet.y}, 8.f, CA_MOVE);
}
int dirTo(float from, float to) { return to >= from ? 1 : -1; }

// Dry ground at x near y: higher up the bank where the stream comes close, never above the clearing.
float groundY(const World& w, float x, float y) {
  return maxf((float)LS_GROUND_Y + 14.f, scriptGroundAt(w, x, y));
}

// The camp is built and the loop's fade is off: a dog may trot in.
bool sceneReady(const World& w) {
  if (w.build.phase != BP_DONE || w.fade > 0.f) return false;
  const char* b = w.minors.beat;
  return !(b && strcmp(b, "close") == 0);
}
// The fetch, or the plinking (Mike, 2026-11-17: no dogs about while he shoots): the dogs keep away.
bool fetchOn(const World& w) { return (w.major.on && w.major.id == MJ_FETCH) || plinkHoldsDogs(w); }

bool tentUp(const World& w) { return w.tent.state == TENT_UP; }
bool fireLit(const World& w) { return w.fire.state == FIRE_LIT || w.fire.state == FIRE_EMBERS; }

// A bit slot nobody is using, from the top (the bear beat writes the bottom four by index).
int freeBit(const World& w) {
  for (int i = BITS_MAX - 1; i >= BITS_MAX - 2; --i)
    if (w.bits[i].kind == BIT_NONE) return i;
  return -1;
}

// ---- the hiker -------------------------------------------------------------------------------

// He holds still long enough for a pat: sitting (or, for Lucy, standing), hands free, in a calm
// beat, nobody else using his arm, and the step he is on holds him for a while yet.
bool patOk(const World& w, int k) {
  const Hiker& h = w.hiker;
  if (!h.visible || h.walking || h.blending || h.carry != CARRY_NONE) return false;
  if (!(h.target == POSE_SIT || (k == HD_LUCY && h.target == POSE_STAND))) return false;
  const HikerFx& fx = w.fx;
  if (fx.reach != 0.f || fx.arm_w[0] != 0.f || fx.arm_w[1] != 0.f || fx.wade || fx.stomp != 0.f) return false;
  if (w.busy || w.react.active || w.minors.cur != MN_NONE || w.minors.fx_arm) return false;
  if (reedsPickActive(w)) return false;   // his hand is on a reed (reeds.h, stage 22)
  if (worldBeatOwnsCritters(w)) return false;
  if (h.feet.x < 60.f || h.feet.x > 600.f) return false;
  const ScriptRun& r = w.spine.run;
  if (r.steps && r.i < r.n) {
    const Step& s = r.steps[r.i];
    if (s.op != OP_WAIT && s.op != OP_POSE) return false;
    if (s.s - (float)r.ticks * kDt < 6.5f) return false;
  }
  return true;
}

// Where a dog stands for its pat: on his facing side, a little in front of him.
Vec2 patSpot(const World& w, int k) {
  const Hiker& h = w.hiker;
  const bool sit = h.target == POSE_SIT;
  const float dx = k == HD_LUCY ? (sit ? 34.f : 30.f) : (sit ? 26.f : 22.f);
  const float x = h.feet.x + (float)h.facing * dx;
  return Vec2{x, groundY(w, x, h.feet.y + 3.f)};
}

// ---- choosing what to do -------------------------------------------------------------------

void beginAct(World& w, int k, uint8_t act);

uint8_t pickAct(World& w, int k) {
  HouseDogs& H = w.dogs;
  HouseDog& d = H.d[k];
  if (w.busy) return HA_SETTLE;
  const bool night = H.night;
  const HouseDog& o = H.d[1 - k];
  const bool other_here = o.phase == HP_HERE && inScene(w, o);
  float wt[HA_COUNT] = {};
  wt[HA_ROAM] = k == HD_COPPER ? 2.6f : 1.6f;
  wt[HA_TENT] = tentUp(w) ? (k == HD_COPPER ? 2.f : 1.4f) : 0.f;
  wt[HA_FIRE] = fireLit(w) ? (night ? 5.f : 2.5f) : (night ? 1.f : 1.2f);
  wt[HA_DRINK] = night ? 0.5f : 1.4f;
  wt[HA_WATCH] = w.hiker.visible ? 2.f : 0.6f;
  wt[HA_LEAF] = freeBit(w) >= 0 ? (night ? 0.2f : (k == HD_COPPER ? 1.6f : 0.8f)) : 0.f;
  wt[HA_PAT] = patOk(w, k) && H.pat == 0 && !(other_here && o.act == HA_PAT) ? 3.f : 0.f;
  wt[HA_NAP] = k == HD_LUCY ? (night ? 3.f : 1.4f) : (night ? 1.2f : 0.4f);
  wt[HA_PLAY] = k == HD_COPPER && other_here && o.act != HA_PAT && !night ? 1.f : 0.f;
  for (int i = 0; i < 2; ++i) wt[d.prev[i]] = 0.f;   // nothing straight again
  float sum = 0.f;
  for (float v : wt) sum += v;
  if (sum <= 0.f) return HA_ROAM;
  float r = rnd(d, 0.f, sum);
  for (int a = 1; a < HA_COUNT; ++a) {
    if (r < wt[a]) return (uint8_t)a;
    r -= wt[a];
  }
  return HA_ROAM;
}

// A spot away from the other dog's (they keep a little apart).
Vec2 apart(const World& w, int k, Vec2 p) {
  const HouseDog& o = w.dogs.d[1 - k];
  if (o.phase != HP_HERE || !inScene(w, o)) return p;
  const Vec2 q = o.act != HA_NONE ? o.to : w.critters[o.slot].feet;
  if (absf(q.x - p.x) < 44.f && absf(q.y - p.y) < 18.f) {
    p.x += p.x < q.x ? -46.f : 46.f;
    p.y = groundY(w, p.x, p.y);
  }
  return p;
}

// Where an activity takes the dog, and which way it faces there.
Vec2 actSpot(World& w, int k, uint8_t act, int& face) {
  HouseDog& d = w.dogs.d[k];
  const Critter& c = cr(w, d);
  const CampLayout& L = w.spine.camp;
  const Vec2 F = w.fire.pos;
  const bool lucy = k == HD_LUCY;
  face = 0;
  switch (act) {
    case HA_TENT: {
      const float side = rnd(d, 0.f, 1.f) < 0.5f ? -1.f : 1.f;   // the back corner or the door side
      const float x = L.tent.x + side * (lucy ? 44.f : 34.f) + rnd(d, -4.f, 4.f);
      face = side < 0.f ? 1 : -1;
      return Vec2{x, groundY(w, x, L.tent.y + rnd(d, 2.f, 8.f))};
    }
    case HA_FIRE: {
      const float side = L.sit.x > F.x ? -1.f : 1.f;   // the side he doesn't sit on
      const float x = F.x + side * (lucy ? 48.f : 38.f);
      face = side < 0.f ? 1 : -1;
      return Vec2{x, groundY(w, x, F.y + rnd(d, 4.f, 10.f))};
    }
    case HA_DRINK: {
      const float x = L.cool.x + rnd(d, -14.f, 14.f);
      float cy, half;
      float y = L.cool.y;
      if (landscapeStreamSpan(w.land, x, cy, half)) y = cy - 1.06f * half - 2.f;   // right at the edge
      face = rnd(d, 0.f, 1.f) < 0.5f ? -1 : 1;
      return Vec2{x, y};
    }
    case HA_WATCH: {
      const Vec2 h = w.hiker.visible ? w.hiker.feet : L.tent;
      const float side = c.feet.x < h.x ? -1.f : 1.f;
      float x = h.x + side * rnd(d, lucy ? 80.f : 66.f, lucy ? 120.f : 104.f);
      if (x < 40.f || x > 640.f) x = h.x - side * rnd(d, 80.f, 110.f);
      face = dirTo(x, h.x);
      return Vec2{x, groundY(w, x, h.y + rnd(d, -6.f, 12.f))};
    }
    case HA_PAT: {
      face = -w.hiker.facing;
      return patSpot(w, k);
    }
    case HA_PLAY: {
      const Critter& l = w.critters[w.dogs.d[HD_LUCY].slot];
      const float x = l.feet.x + (float)l.facing * 40.f;
      face = dirTo(x, l.feet.x);
      return Vec2{x, groundY(w, x, l.feet.y + 4.f)};
    }
    case HA_SETTLE: {   // beside the tent's back corner, out of everyone's way
      if (w.fi.on && w.fi.mode == FI_COLD) {   // the cold (fireice.h): in front of the blaze, in the warm circle
        const float back = fireIceClearance(w);   // clear of the big blaze's flames
        const float x = F.x + (lucy ? -30.f - back : 20.f + back);
        face = lucy ? 1 : -1;
        return Vec2{x, groundY(w, x, F.y + 18.f + 0.3f * back)};
      }
      Vec2 watch;
      if (lucy && taichiWatchSpot(w, watch)) {   // tai chi: she lies near him and watches (Mike, 2026-11-18)
        face = 1;
        return watch;
      }
      const float x = L.tent.x - (lucy ? 58.f : 40.f) - (float)k * 6.f;
      face = 1;
      return Vec2{x, groundY(w, x, L.tent.y + 10.f + 6.f * (float)k)};
    }
    case HA_NAP:
      if (rnd(d, 0.f, 1.f) < 0.5f) {   // where it is
        face = 0;
        return c.feet;
      }
      [[fallthrough]];
    default: {   // somewhere in the clearing
      const float x = rnd(d, 70.f, 600.f);
      return Vec2{x, groundY(w, x, F.y - rnd(d, -8.f, 46.f))};
    }
  }
}

// What it holds at the spot, and for how long.
void actHold(World& w, int k, uint8_t act) {
  HouseDog& d = w.dogs.d[k];
  const float calm = kNature[k].calm;
  switch (act) {
    case HA_TENT: d.hold = CA_SNIFF; d.left = rnd(d, 2.5f, 4.5f); break;
    case HA_FIRE: d.hold = CA_LIE; d.left = rnd(d, 14.f, 32.f) * (0.55f + 0.45f * calm); break;
    case HA_DRINK: d.hold = CA_SNIFF; d.left = rnd(d, 3.f, 5.5f); break;
    case HA_WATCH: d.hold = CA_SIT; d.left = rnd(d, 8.f, 20.f) * (0.6f + 0.4f * calm); break;
    case HA_PAT: d.hold = CA_SIT; d.left = 1.0f; break;   // then the pat itself
    case HA_NAP: d.hold = CA_LIE; d.left = rnd(d, 22.f, 50.f) * (0.4f + 0.6f * calm); break;
    case HA_PLAY: d.hold = CA_BOW; d.left = rnd(d, 1.2f, 1.8f); break;
    case HA_SETTLE: d.hold = CA_LIE; d.left = 1.0e9f; break;
    default: d.hold = CA_SNIFF; d.left = rnd(d, 1.5f, 3.f); break;
  }
}

void beginAct(World& w, int k, uint8_t act) {
  HouseDog& d = w.dogs.d[k];
  Critter& c = cr(w, d);
  d.prev[1] = d.prev[0];
  d.prev[0] = act;
  d.act = act;
  d.step = 0;
  d.t = 0.f;
  d.round = 0;
  c.head_add = 0.f;
  d.head = 0.f;
  if (act == HA_LEAF) {   // no walk first: it happens where the dog is
    d.step = 10;
    critterSetAction(c, CA_LOOK, 0.3f);
    return;
  }
  int face = 0;
  d.to = actSpot(w, k, act, face);
  // (The cold's spots by the fire are apart already, and must stay inside the warm circle.)
  const bool cold = act == HA_SETTLE && w.fi.on && w.fi.mode == FI_COLD;
  if (act != HA_PAT && act != HA_PLAY && !cold) d.to = apart(w, k, d.to);
  d.face = (int8_t)face;
  actHold(w, k, act);
  const float far = absf(d.to.x - c.feet.x);
  const float v = far > 260.f && act != HA_NAP ? kNature[k].trot * 1.25f : kNature[k].trot;
  if (far > 2.f || absf(d.to.y - c.feet.y) > 2.f) walkTo(w, d, d.to, v, CA_MOVE);
}

// The activity is over: a breath (stand, look about), then the next one.
void endAct(World& w, int k) {
  HouseDog& d = w.dogs.d[k];
  Critter& c = cr(w, d);
  c.head_add = 0.f;
  d.head = 0.f;
  d.act = HA_NONE;
  d.step = 0;
  d.t = 0.f;
  d.left = rnd(d, 0.8f, 2.6f) * (0.6f + 0.6f * kNature[k].calm);
  critterSetAction(c, rnd(d, 0.f, 1.f) < 0.5f ? CA_LOOK : CA_IDLE, 0.45f);
}

// ---- the pat ---------------------------------------------------------------------------------

constexpr float kPatS = 3.6f;

void patEnd(World& w) {
  HouseDogs& H = w.dogs;
  if (!H.pat) return;
  H.pat = 0;
  H.pat_t = 0.f;
  w.fx.reach = 0.f;
  w.fx.head = 0.f;
}

// His near arm out to the dog's head, a stroke or two, and back; his head bows to it. Ends early
// (the arm back at once) if he starts to move.
void patStep(World& w) {
  HouseDogs& H = w.dogs;
  if (!H.pat) return;
  const int k = H.pat - 1;
  HouseDog& d = H.d[k];
  const Hiker& h = w.hiker;
  if (!inScene(w, d) || d.act != HA_PAT || h.walking || h.blending || !h.visible || w.react.active) {
    patEnd(w);
    return;
  }
  H.pat_t += kDt;
  const float u = minf(1.f, H.pat_t / kPatS);
  const Critter& c = cr(w, d);
  const float S = c.scale;
  float r = 0.f;
  const Vec2 hd = hikerHeadPos(h, &r);
  const Vec2 sh{hd.x, hd.y + 1.9f * r};
  const Vec2 m = critterMouth(c);
  const Vec2 top{m.x - (float)c.facing * 6.f * S, m.y - 7.f * S};   // the top of its head
  const float dx = (top.x - sh.x) * (float)h.facing, dy = top.y - sh.y;
  w.fx.reach_to = clampf(atan2f(dx, dy > 1.f ? dy : 1.f), 0.15f, 1.7f);
  const float e = scriptEnvelope(u, 0.22f, 0.22f);
  w.fx.reach = e * (0.82f + 0.1f * sinT(u * 4.f));
  w.fx.head = 0.14f * e;
  if (u >= 1.f) patEnd(w);
}

// ---- running an activity -----------------------------------------------------------------------

float wagFor(const HouseDog& d, int k) {
  const float base = kNature[k].wag;
  switch (d.act) {
    case HA_PAT: return base * 1.9f;
    case HA_PLAY: case HA_LEAF: case HA_CHASE: return base * 1.5f;
    case HA_WATCH: return base * 1.1f;
    case HA_FIRE: case HA_SETTLE: return base * 0.4f;
    case HA_NAP: return d.step >= 2 ? 0.f : base * 0.3f;
    default: return base * 0.7f;
  }
}

void runLeaf(World& w, int k) {
  HouseDog& d = w.dogs.d[k];
  Critter& c = cr(w, d);
  Bit* leaf = d.bit >= 0 && w.bits[d.bit].kind == BIT_LEAF ? &w.bits[d.bit] : nullptr;
  switch (d.step) {
    case 10:   // it spots something; a breath of wind lifts a leaf in front of it
      if (d.t >= 0.45f) {
        const int b = freeBit(w);
        if (b < 0) {
          endAct(w, k);
          return;
        }
        Bit& n = w.bits[b];
        n = Bit{};
        n.kind = BIT_LEAF;
        const float dir = c.feet.x < 400.f ? 1.f : -1.f;   // off toward the open side
        n.pos = Vec2{c.feet.x + dir * 30.f * c.scale, c.feet.y - 1.f};
        n.ground_y = c.feet.y - 1.f;
        n.land_y = c.feet.y;
        n.vel = Vec2{dir * 22.f, 0.f};
        n.spin = 0.15f * dir;
        d.bit = (int8_t)b;
        faceTo(w, d, (int)dir);
        d.step = 11;
        d.t = 0.f;
      }
      break;
    case 11:   // a play-bow at it
      if (critterArrived(c) && c.action != CA_BOW) critterSetAction(c, CA_BOW, 0.35f);
      if (d.t >= 0.75f) {
        d.step = 12;
        d.t = 0.f;
      }
      break;
    case 12:   // after it
      if (!leaf || d.t >= 2.4f) {
        critterSetAction(c, CA_HOP, 0.55f);
        d.step = 13;
        d.t = 0.f;
        break;
      }
      if (critterArrived(c) || (int)(d.t * 4.f) != (int)((d.t - kDt) * 4.f)) {
        const float x = leaf->pos.x;
        if (absf(x - c.feet.x) < 14.f * c.scale || x < 30.f || x > 620.f) {
          critterSetAction(c, CA_HOP, 0.55f);   // the pounce
          d.step = 13;
          d.t = 0.f;
          break;
        }
        walkTo(w, d, Vec2{x, groundY(w, x, c.feet.y)}, kNature[k].run, CA_RUN);
      }
      break;
    case 13:
      if (d.t >= 0.6f) {
        critterSetAction(c, CA_LOOK, 0.3f);   // it watches the leaf go
        d.step = 14;
        d.t = 0.f;
      }
      break;
    default:
      if (d.t >= 1.4f) {
        d.bit = -1;
        endAct(w, k);
      }
      break;
  }
}

void runPlay(World& w, int k) {   // Copper; step 2 is the bow (the hold)
  HouseDog& d = w.dogs.d[k];
  Critter& c = cr(w, d);
  const HouseDog& l = w.dogs.d[HD_LUCY];
  if (!inScene(w, l)) {
    endAct(w, k);
    return;
  }
  const float lx = w.critters[l.slot].feet.x;
  switch (d.step) {
    case 3: {   // the dash: off past her one way
      const float side = c.feet.x < lx ? -1.f : 1.f;
      const float x = clampf(lx - side * 80.f, 40.f, 620.f);
      walkTo(w, d, Vec2{x, groundY(w, x, c.feet.y + rnd(d, -6.f, 6.f))}, kNature[k].run, CA_RUN);
      d.step = 4;
      d.t = 0.f;
      break;
    }
    case 4:
      if (critterArrived(c) || d.t > 3.f) {
        if (d.round == 0) {   // and back round her the other way
          d.round = 1;
          const float side = c.feet.x < lx ? -1.f : 1.f;
          const float x = clampf(lx - side * 60.f, 40.f, 620.f);
          walkTo(w, d, Vec2{x, groundY(w, x, c.feet.y + rnd(d, -6.f, 6.f))}, kNature[k].run, CA_RUN);
          d.t = 0.f;
        } else {
          faceTo(w, d, dirTo(c.feet.x, lx));
          d.step = 5;
          d.t = 0.f;
        }
      }
      break;
    default:
      if (critterArrived(c) && c.action != CA_SIT) critterSetAction(c, CA_SIT, 0.5f);
      if (d.t >= 2.f) endAct(w, k);
      break;
  }
}

// ---- a visitor at the stream ------------------------------------------------------------------
//
// The heron in the shallows or the deer at the bank (stream.h) while the dog is about: it notices it
// (head up, a beat: kChaseNotice), runs at it (the stream sees the dog charging and the visitor goes at
// once, stream.cpp threatNear), stops at the camp-side bank short of where it was (the heron is across
// the water; the deer gets kChaseShort px of start), looks after it, then takes up what it was doing.
// No random draws but the ones beginAct and endAct make anyway, all from the dog's own stream.
constexpr float kChaseNotice = 0.7f;   // s: head up, looking, before it runs (PROPOSED)
constexpr float kChaseShort = 46.f;    // px: where it pulls up short of the deer
constexpr float kChaseRunMax = 5.f;    // s: never stuck on the way

// The activities it breaks off for a visitor, and takes up again after.
bool chaseFrom(uint8_t act) {
  return act == HA_NONE || act == HA_ROAM || act == HA_TENT || act == HA_FIRE || act == HA_DRINK || act == HA_WATCH ||
         act == HA_NAP;
}

void maybeChase(World& w, int k) {
  HouseDogs& H = w.dogs;
  HouseDog& d = H.d[k];
  if (!chaseFrom(d.act) || w.busy || w.fi.on || visitorsHolds(w) || w.react.active) return;
  const HouseDog& o = H.d[1 - k];
  if (o.act == HA_CHASE && inScene(w, o)) return;   // one dog after it is enough
  Vec2 at;
  if (!streamGuestAt(w, at) && !herdGuestAt(w, at)) return;   // the stream's guest, or the last of the deer herd (day_herd.h)
  Critter& c = cr(w, d);
  if (!c.visible || c.feet.x < 20.f || c.feet.x > (float)SS_WIDTH - 20.f) return;
  d.resume = d.act;
  c.head_add = 0.f;
  d.head = 0.f;
  d.act = HA_CHASE;
  d.step = 0;
  d.t = 0.f;
  d.to = at;
  critterSetAction(c, CA_LOOK, 0.25f);
  faceTo(w, d, dirTo(c.feet.x, at.x));
}

void runChase(World& w, int k) {
  HouseDog& d = w.dogs.d[k];
  Critter& c = cr(w, d);
  switch (d.step) {
    case 0:   // it has seen it: head up, still
      d.head = maxf(-0.14f, d.head - kDt * 0.6f);
      if (d.t >= kChaseNotice) {
        float cy, half;
        float x = d.to.x;
        const bool deer = !landscapeStreamSpan(w.land, x, cy, half) || d.to.y < cy;   // the deer is on this bank
        if (deer) x -= (d.to.x >= c.feet.x ? 1.f : -1.f) * kChaseShort;
        x = clampf(x, 30.f, 640.f);
        float y = d.to.y;
        if (landscapeStreamSpan(w.land, x, cy, half)) y = cy - half - 3.f;   // the camp side's water edge
        d.to = Vec2{x, groundY(w, x, y)};
        d.head = 0.f;
        walkTo(w, d, d.to, kNature[k].run, CA_RUN);
        d.step = 1;
        d.t = 0.f;
      }
      break;
    case 1:   // running at it
      if (critterArrived(c) || d.t >= kChaseRunMax) {
        critterSetAction(c, CA_LOOK, 0.3f);
        d.left = k == HD_LUCY ? 2.6f : 1.8f;
        d.step = 2;
        d.t = 0.f;
      }
      break;
    case 2:   // at the bank, looking after it
      d.head = maxf(-0.12f, d.head - kDt * 0.5f);
      d.left -= kDt;
      if (d.left <= 0.f) {
        const uint8_t back = d.resume;
        d.resume = HA_NONE;
        if (back != HA_NONE && back != HA_CHASE) beginAct(w, k, back);
        else endAct(w, k);
      }
      break;
    default: endAct(w, k); break;
  }
}

void runAct(World& w, int k) {
  HouseDogs& H = w.dogs;
  HouseDog& d = H.d[k];
  Critter& c = cr(w, d);
  d.t += kDt;
  if (d.act == HA_CHASE && !w.busy) {
    runChase(w, k);
    return;
  }
  if (d.act == HA_NONE) {   // the breath between activities
    d.left -= kDt;
    if (d.left <= 0.f) beginAct(w, k, pickAct(w, k));
    return;
  }
  if (d.act == HA_SETTLE && !w.busy) {   // the beat that needed the camp is over
    endAct(w, k);
    return;
  }
  if (d.act == HA_SETTLE && k == HD_LUCY) {   // settled by the tent before the tai chi began: she moves over to watch
    Vec2 watch;
    if (taichiWatchSpot(w, watch) && absf(d.to.x - watch.x) > 40.f && !(w.fi.on && w.fi.mode == FI_COLD)) {
      critterSetAction(c, CA_IDLE, 0.4f);
      beginAct(w, k, HA_SETTLE);
      return;
    }
  }
  if (d.act != HA_SETTLE && w.busy && d.act != HA_LEAF) {   // a beat needs the camp: out of the way
    if (H.pat == k + 1) patEnd(w);
    beginAct(w, k, HA_SETTLE);
    return;
  }
  if (d.act == HA_LEAF) {
    runLeaf(w, k);
    return;
  }
  switch (d.step) {
    case 0:   // walking there
      if (d.act == HA_PAT && !patOk(w, k)) {   // he got up meanwhile
        endAct(w, k);
        return;
      }
      if (critterArrived(c)) {
        faceTo(w, d, d.face);
        d.step = 1;
        d.t = 0.f;
      } else if (d.t > 20.f) {   // never stuck on the way
        endAct(w, k);
      }
      break;
    case 1:   // turned: settle into the hold
      if (critterArrived(c)) {
        critterSetAction(c, (CritterAction)d.hold, d.hold == CA_LIE ? 0.9f : (d.hold == CA_SIT ? 0.5f : 0.35f));
        d.step = 2;
        d.t = 0.f;
        d.ring_t = 0.6f;
      }
      break;
    case 2:   // the hold
      d.left -= kDt;
      switch (d.act) {
        case HA_DRINK:   // head right down to the water, rings spreading where it laps
          d.head = minf(0.35f, d.head + kDt * 0.8f);
          d.ring_t -= kDt;
          if (d.ring_t <= 0.f) {
            d.ring_t = rnd(d, 0.8f, 1.3f);
            const int b = freeBit(w);
            if (b >= 0) {
              const Vec2 m = critterMouth(c);
              Bit& n = w.bits[b];
              n = Bit{};
              n.kind = BIT_RING;
              float cy, half;
              const float y = landscapeStreamSpan(w.land, m.x, cy, half) ? cy - 0.9f * half : c.feet.y + 4.f;
              n.pos = Vec2{m.x + (float)c.facing * 2.f, maxf(y, m.y + 2.f)};
              n.land_y = n.pos.y;
              n.spin = 0.35f + 0.25f * c.scale;
            }
          }
          break;
        case HA_FIRE:
        case HA_NAP:   // after a while the chin goes down
          if (d.t > 4.f) d.head = minf(0.28f, d.head + kDt * 0.15f);
          break;
        case HA_WATCH:   // keeps an eye on him: turns when he passes to its other side
          if (w.hiker.visible && (int)(d.t * 2.f) != (int)((d.t - kDt) * 2.f)) {
            const int want = dirTo(c.feet.x, w.hiker.feet.x);
            if (want != c.facing && absf(w.hiker.feet.x - c.feet.x) > 20.f) {
              faceTo(w, d, want);
              d.step = 1;   // and sits again after the turn
              d.t = 0.f;
            }
          }
          break;
        case HA_SETTLE: {   // a bear in camp: sit up and watch it
          bool bear = false;
          for (int i = 0; i < CRITTER_STORY; ++i)
            if (w.crit[i].in_use && w.critters[i].kind == CR_BEAR && w.critters[i].visible) bear = true;
          const CritterAction want = bear ? CA_SIT : CA_LIE;
          if (c.action != want && critterArrived(c)) critterSetAction(c, want, 0.7f);
          break;
        }
        case HA_PAT:
          if (d.left <= 0.f && H.pat == 0) {
            if (patOk(w, k)) {
              H.pat = (uint8_t)(k + 1);
              H.pat_t = 0.f;
              d.step = 3;
              d.t = 0.f;
            } else {
              endAct(w, k);
            }
            return;
          }
          break;
        default: break;
      }
      if (d.left <= 0.f) {
        if (d.act == HA_PLAY) {
          d.step = 3;
          d.t = 0.f;
          runPlay(w, k);
        } else if ((d.act == HA_ROAM || d.act == HA_TENT) && d.round == 0 && rnd(d, 0.f, 1.f) < 0.55f) {
          // a few steps on and another sniff
          d.round = 1;
          const float x = clampf(c.feet.x + (float)c.facing * rnd(d, 14.f, 30.f), 40.f, 620.f);
          d.to = Vec2{x, groundY(w, x, c.feet.y + rnd(d, -4.f, 4.f))};
          d.face = 0;
          d.left = rnd(d, 1.2f, 2.4f);
          walkTo(w, d, d.to, kNature[k].trot * 0.7f, CA_MOVE);
          d.step = 0;
          d.t = 0.f;
        } else {
          endAct(w, k);
        }
      }
      break;
    default:
      if (d.act == HA_PLAY) {
        runPlay(w, k);
      } else if (d.act == HA_PAT) {   // the pat runs (patStep); then a little longer sitting by him
        if (H.pat == 0 && d.t >= kPatS + 1.2f) endAct(w, k);
        if (d.t > kPatS + 4.f) endAct(w, k);
      } else {
        endAct(w, k);
      }
      break;
  }
}

// ---- coming and going ------------------------------------------------------------------------

void comeIn(World& w, int k) {
  HouseDog& d = w.dogs.d[k];
  const Nature& n = kNature[k];
  const int s = slotOf(k);
  const float scale = dogToneScale(n.tone);
  const float y = groundY(w, 20.f, w.fire.pos.y - rnd(d, 0.f, 26.f));
  directorSpawnAt(w, s, CR_DOG, Vec2{-30.f - 40.f * scale, y}, 1);
  CritterSlot& cs = w.crit[s];
  cs.tag = CT_HOUSEDOG;
  cs.flags = CF_BRAVE;
  cs.speed_dial = n.speed_dial;
  cs.jitter_dial = n.jitter_dial;
  Critter& c = w.critters[s];
  c.tone = n.tone;
  c.scale = scale;
  c.speed_dial = n.speed_dial;
  c.jitter_dial = n.jitter_dial;
  d.slot = (int8_t)s;
  d.phase = HP_HERE;
  d.bit = -1;
  d.wag_amp = 0.f;
  // In with something to do: a look round the camp, a drink, or straight to him.
  uint8_t first = pickAct(w, k);
  if (first == HA_LEAF || first == HA_PLAY || first == HA_NAP) first = HA_ROAM;
  beginAct(w, k, first);
}

void goOut(World& w, int k, bool run) {
  HouseDogs& H = w.dogs;
  HouseDog& d = H.d[k];
  if (H.pat == k + 1) patEnd(w);
  Critter& c = cr(w, d);
  c.head_add = 0.f;
  d.head = 0.f;
  d.act = HA_NONE;
  d.bit = -1;
  directorExit(w, d.slot, -1, vArg(c, run ? kNature[k].run : kNature[k].trot * 1.15f), run ? CA_RUN : CA_MOVE);
  d.phase = run ? HP_STEPPED_OUT : HP_GOING;
}

void stepDog(World& w, int k, bool fetch) {
  HouseDogs& H = w.dogs;
  HouseDog& d = H.d[k];
  const bool want = ((H.want >> k) & 1) != 0;
  if (d.slot >= 0 && !inScene(w, d)) {   // out of sight and released by the director
    d.slot = -1;
    if (d.phase == HP_STEPPED_OUT) d.wait = rnd(d, 2.f, 4.f);   // back in once the fetch is over
    d.phase = HP_AWAY;
  }
  switch (d.phase) {
    case HP_AWAY:
      if (!want) {
        d.wait = -1.f;
        break;
      }
      if (d.wait < 0.f) d.wait = rnd(d, 1.f, 3.5f);   // PROPOSED: a few seconds to get out of the door
      if (fetch || !sceneReady(w)) break;
      d.wait -= kDt;
      if (d.wait <= 0.f) comeIn(w, k);
      break;
    case HP_HERE:
      if (H.lent == k + 1 && (!want || fetch)) H.lent = 0;   // it went in: the game lets it go
      if (!want) goOut(w, k, false);
      else if (fetch) goOut(w, k, true);
      else if (H.lent != k + 1) {   // lent: the visitors' game drives it
        maybeChase(w, k);
        runAct(w, k);
      }
      break;
    case HP_GOING:
      if (want && !fetch) {   // out again before it was gone: it turns back
        w.crit[d.slot].mode = CM_FREE;
        d.phase = HP_HERE;
        beginAct(w, k, HA_WATCH);
      }
      break;
    default: break;   // stepped out: on its way off
  }
  if (inScene(w, d)) {   // the wag and the chin, on its own clock
    Critter& c = cr(w, d);
    const float want_amp = d.phase == HP_HERE ? wagFor(d, k) : kNature[k].wag * 0.5f;
    d.wag_amp += (want_amp - d.wag_amp) * minf(1.f, kDt * 3.f);
    const float rate = kNature[k].wag_rate * (d.act == HA_PAT ? 1.5f : 1.f);
    d.wag += kDt * rate;
    d.wag -= floorf(d.wag);
    float mul = 1.f;
    if (c.action == CA_MOVE || c.action == CA_RUN || c.action == CA_HOP) mul = 0.3f;
    if (c.action == CA_LIE) mul = 0.6f;
    c.tail_add = d.wag_amp * mul * sinT(d.wag);
    c.head_add = -d.head;
  }
}

}  // namespace

void houseDogsBeginLoop(World& w, uint64_t loop_seed, int dog_out, bool night) {
  HouseDogs& H = w.dogs;
  memset(&H, 0, sizeof H);
  H.want = (uint8_t)(dog_out > 0 ? (dog_out & 3) : 0);
  H.night = night;
  for (int k = 0; k < HD_COUNT; ++k) {
    HouseDog& d = H.d[k];
    d.slot = -1;
    d.bit = -1;
    d.wait = -1.f;
    const uint64_t x = loop_seed * 0x9E3779B97F4A7C15ull + 0xD06D06ull * (uint64_t)(k + 1);
    d.rng = (uint32_t)(x >> 32) ^ (uint32_t)x;
    if (d.rng == 0) d.rng = 0x2545F491u;
  }
}

void houseDogsSetInputs(World& w, int dog_out, bool night) {
  w.dogs.want = (uint8_t)(dog_out > 0 ? (dog_out & 3) : 0);
  w.dogs.night = night;
}

void houseDogsStep(World& w) {
  HouseDogs& H = w.dogs;
  if (!H.want && H.d[0].phase == HP_AWAY && H.d[1].phase == HP_AWAY && H.d[0].slot < 0 && H.d[1].slot < 0) return;
  const bool fetch = fetchOn(w);
  for (int k = 0; k < HD_COUNT; ++k) stepDog(w, k, fetch);
  patStep(w);
}

bool houseDogsClear(const World& w) {
  for (const HouseDog& d : w.dogs.d)
    if (inScene(w, d) && w.critters[d.slot].visible) return false;
  return true;
}

int houseDogsFetchTone(const World& w, int k) {
  const uint8_t want = w.dogs.want;
  if (k == 0) return (want & 1) ? DT_LUCY : ((want & 2) ? DT_COPPER : DT_CAMP);
  return want == 3 ? DT_COPPER : -1;
}

const char* houseDogActName(int act) { return act >= 0 && act < HA_COUNT ? kActNames[act] : "?"; }

int houseDogsLend(World& w, int first) {
  HouseDogs& H = w.dogs;
  if (H.lent) return -1;
  const int a = first == HD_LUCY ? HD_LUCY : HD_COPPER;   // anything else asks for Copper, as before
  const int order[HD_COUNT] = {a, a == HD_LUCY ? HD_COPPER : HD_LUCY};
  for (int k : order) {
    HouseDog& d = H.d[k];
    if (d.phase != HP_HERE || !inScene(w, d) || d.act == HA_LEAF || !((H.want >> k) & 1)) continue;
    if (H.pat == k + 1) patEnd(w);
    Critter& c = cr(w, d);
    c.head_add = 0.f;
    d.head = 0.f;
    d.act = HA_WATCH;   // its wag reads this: a keen one
    d.step = 0;
    H.lent = (uint8_t)(k + 1);
    return d.slot;
  }
  return -1;
}

void houseDogsGiveBack(World& w) {
  HouseDogs& H = w.dogs;
  if (!H.lent) return;
  const int k = H.lent - 1;
  H.lent = 0;
  HouseDog& d = H.d[k];
  if (d.phase == HP_HERE && inScene(w, d)) endAct(w, k);
}

}  // namespace ss
