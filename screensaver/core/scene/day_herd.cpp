// The deer visit: the World side. See day_herd.h.
//
// OWNERSHIP: skeleton from the foundation step (A1); herdPlanPick from step C0; everything else is step C4
// (2026-11-19; Mike's words, 2026-11-18, are in herd.h).
//
// The pure part (herd.h) is a state machine that says what each deer should do; this file carries its
// orders out on the director and the critter rigs, tells it what it needs to know (who has arrived, the
// doorbell, a house dog wanted out, lightning, a visitor at the panel), and keeps the few things that
// belong to the World: the tent's twitch and its flap, water rings and drops, the dust of a stamp, the hoof
// prints' place on the ground, and the two meetings with the reeds.
//
// Determinism: the only random numbers are DayHerd::rng's (seeded at the beat's begin from the layout), one
// call per statement; the clock is HerdState::t, advanced by herdTick once per story tick (the beat's
// update, so it stands still with the story); nothing reads w.tick. The beat does not set w.busy (a house
// dog let out must be free to chase, DEER_PLAN 6.1); minors, the stream's visitors, the doorbell reaction,
// fire and ice and the weather's gags cannot start while he is away (DEER_PLAN 1).
#include "day_herd.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../anim/rng.h"
#include "../ss_app.h"
#include "director.h"
#include "majors.h"
#include "memory.h"
#include "reeds.h"
#include "script.h"
#include "stage22_switch.h"
#include "weather.h"
#include "world.h"

namespace ss {

namespace {
constexpr uint64_t kHerdStream = 0x4845524444454552ull;   // "HERDDEER": the pick's own stream

// INTEGRATION_PLAN section 2 (PROPOSED defaults): the second doe eats the fireside cattail; the fawn's third
// dash ends in the upstream clump.
constexpr bool kHerdNibble = true;
constexpr bool kHerdFawnReeds = true;
constexpr float kNibbleBite = 2.8f;      // seconds after she reaches that the head comes off
constexpr float kNibbleBend = 6.f;       // px the stalk may bend to meet her
constexpr float kFawnReedMax = 90.f;     // the clump must be within this far east of its water point

inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

DayHerd& D(World& w) { return w.day.herd; }
const DayHerd& D(const World& w) { return w.day.herd; }
bool live(const World& w) { return w.day.kind == DAY_HERD; }
bool ours(const World& w, int slot) {
  return slot >= 0 && slot < CRITTER_MAX && w.crit[slot].in_use && w.crit[slot].tag == CT_HERD;
}

uint64_t seedOf(const World& w) {
  return ((uint64_t)(uint32_t)(w.fire.pos.x * 16.f) << 22) ^ (uint64_t)(uint32_t)(w.fire.pos.y * 16.f) ^ 0x4EED0000ull;
}

// The camp-side water's edge at x (the drawn water is 1.06 half widths wide), or `fallback`.
float bankTop(const World& w, float x, float fallback) {
  float cy, half;
  return landscapeStreamSpan(w.land, x, cy, half) ? cy - 1.06f * half : fallback;
}

// A second doe's muzzle relative to her feet once the stretch has landed, facing left (frame px).
Vec2 reachNose(float scale) {
  Critter c;
  critterInit(c, CR_DEER, Vec2{0.f, 0.f}, -1);
  c.scale = scale;
  c.speed_dial = c.jitter_dial = 0.f;
  critterSetAction(c, CA_REACH, 0.02f);
  for (int k = 0; k < 4; ++k) critterTick(c, SS_TICK_DT);
  return critterDeerNose(c);
}

// The stalk the second doe bites: the fireside clump's first cattail that still has its head; in spring
// (no heads on the new growth) or with none left, the nearest reed. -1: nothing to eat there.
int nibbleStalk(const World& w, const ReedClumpInfo& ci) {
  const uint8_t month = w.reeds.month;
  const bool spring = month >= 3 && month <= 5;
  if (!spring) {
    for (int s = 0; s < ci.n_cat; ++s) {
      ReedStalkInfo si;
      if (reedsStalk(w, RS_FIRESIDE, s, si) && si.present && si.cattail) return s;
    }
  }
  return reedsNearestStalk(w, RS_FIRESIDE, ci.base, true);
}

void layoutReeds(const World& w, HerdLayout& L, HerdDrive& v) {
  v.nib_clump = v.nib_stalk = -1;
  v.reed_clump = -1;
  ReedClumpInfo ci;
  if (kHerdNibble && reedsClump(w, RS_FIRESIDE, ci) && ci.present && ci.camp_bank) {
    const int stalk = nibbleStalk(w, ci);
    ReedStalkInfo si;
    if (stalk >= 0 && reedsStalk(w, RS_FIRESIDE, stalk, si) && si.present) {
      // She stands to the right of the clump, facing it, so that her stretched muzzle meets the stalk's
      // tip; never lower than 5 px above the water (the stalk bends up to her for the rest).
      const Vec2 nose = reachNose(herdScale(HR_GRAZER));
      const float x = si.tip.x - nose.x;
      const float y = minf(si.tip.y - nose.y, bankTop(w, x, w.fire.pos.y + 12.f) - 5.f);
      // Step C6 (test_stage22 [7]): when the bank holds her higher than the stalk's tip by more than the
      // stalk can bend (a short cattail), her muzzle never met it (5 px off in seed 401); she goes to G4.
      const float bend = si.tip.y - nose.y - y;
      if (y >= 343.f && x < L.fire.x + 132.f && bend <= kNibbleBend + 2.f) {
        L.nibble_on = true;
        L.nibble = Vec2{floorf(x + 0.5f), floorf(y + 0.5f)};
        v.nib_clump = RS_FIRESIDE;
        v.nib_stalk = (int8_t)stalk;
      }
    }
  }
  if (reedsClump(w, RS_UPSTREAM, ci) && ci.present && ci.camp_bank) {
    // Step C6 (test_stage22 [6]): the doe's grazing place (G0, about F.x + 232) stands 12 px or more left of
    // the upstream clump's leftmost leaf or stalk, with 4 px for the wind's lean now (it stood 4 px from a
    // stalk in seed 172, in about one herd loop in seven).
    float left = ci.base.x - ci.half_w;
    for (int k = 0; k < ci.n_cat + ci.n_reed; ++k) {
      ReedStalkInfo si;
      if (reedsStalk(w, RS_UPSTREAM, k, si)) left = minf(left, minf(si.tip.x, si.foot.x));
    }
    L.g0_max = floorf(left - 12.f - 4.f);
  }
  if (kHerdFawnReeds && reedsClump(w, RS_UPSTREAM, ci) && ci.present && ci.camp_bank) {
    const float x = ci.base.x - ci.half_w - 14.f;   // its nose about 6 px into the leaves
    // (a dash of 22 to 70 px from where its second bound ended, 8 px left of its water point)
    if (ci.base.x > L.splash.x && ci.base.x <= L.splash.x + kFawnReedMax && x >= L.splash.x + 14.f && x <= L.splash.x + 62.f) {
      L.fawn_reed = Vec2{floorf(x + 0.5f), floorf(bankTop(w, x, L.splash.y - 4.f) + 4.f + 0.5f)};
      v.reed_clump = RS_UPSTREAM;
    }
  }
}

// ---- carrying out one order -----------------------------------------------------------------------------------

float trueSpeed(const Critter& c, float px_per_s) { return px_per_s / (0.8f + 0.4f * c.speed_dial); }

void ring(World& w, Vec2 at, float size) {
  const int b = worldTakeBit(w, BIT_RING);
  if (b < 0) return;
  Bit& r = w.bits[b];
  r.pos = at;
  r.spin = size;
  r.land_y = at.y;
}

void order(World& w, DayHerd& d, const HerdEv& e) {
  HerdState& h = d.h;
  HerdDrive& v = d.v;
  HerdDeer& hd = h.d[e.deer];
  if (e.cmd == HC_ENTER) {
    const int s = directorSpawn(w, CR_DEER, Vec2{kHerdSpawnX, (float)e.a}, 1);
    hd.slot = (int8_t)s;
    if (s < 0) return;   // no slot: the machine sees in_use false and lets it go
    w.crit[s].tag = CT_HERD;
    w.crit[s].flags = CF_BRAVE;   // nothing scatters them but their own alarm
    Critter& c = w.critters[s];
    c.scale = herdScale(hd.role);
    c.tone = hd.role == HR_FAWN ? kDeerFawn : 0;
    memoryApplyPersona(w.memory, c);
    return;
  }
  if (e.cmd == HE_GONE) {
    if (ours(w, hd.slot)) directorRelease(w, hd.slot);
    hd.slot = -1;
    return;
  }
  // The flap is the tent's, not a deer's.
  if (e.cmd == HC_DOOR) {
    v.door_to = (float)e.a * 0.01f;
    const float secs = maxf(0.04f, (float)e.b * 0.01f);
    v.door_rate = fabsf(v.door_to - v.door) / secs;
    return;
  }
  if (e.cmd == HC_WOBBLE) {
    v.wob_kind = (uint8_t)e.a;
    v.wob_tick = 0;
    return;
  }
  const int s = hd.slot;
  if (!ours(w, s)) return;
  Critter& c = w.critters[s];
  CritterSlot& cs = w.crit[s];
  switch (e.cmd) {
    case HC_WALK:
    case HC_RUN: {
      Vec2 to = herdSpot(h, (uint8_t)e.a, hd.role);
      const CritterAction gait = e.cmd == HC_RUN ? CA_RUN : CA_MOVE;
      const bool out = e.a == HS_OUT_L || e.a == HS_OUT_R;
      if (out) {   // past the frame's edge: the director lets it go once it is out of sight
        cs.mode = CM_FLEE;
        cs.t = 0.f;
        cs.flee_x = to.x;
      }
      float v_px = (float)e.b;
      // The dash into the reeds is longer than the plain one: quick enough to land in 0.8 s (4.2).
      if (e.a == HS_SP10 && hd.role == HR_FAWN && v.reed_clump >= 0) v_px = maxf(v_px, fabsf(to.x - c.feet.x) / 0.8f);
      directorWalk(w, s, to, trueSpeed(c, v_px), gait);
      // To the right (a dog is coming in from the left): up onto the dry line above the water by x 520,
      // then along it (DEER_PLAN 2).
      if (e.a == HS_OUT_R && c.feet.x < 520.f) cs.path_x1 = maxf(520.f, c.feet.x + 40.f);
      if (e.a == HS_SP10 && hd.role == HR_FAWN && v.reed_clump >= 0 && v.fawn_reed == 0) v.fawn_reed = 1;
      if (e.a == HS_GF && hd.role == HR_FAWN && v.fawn_reed == 2) {   // it turns to climb out: the clump swings back
        reedsPush(w, v.reed_clump, -1.f, 0.4f);
        v.fawn_reed = 3;
      }
      break;
    }
    case HC_BACK:
      directorWalk(w, s, Vec2{c.feet.x - (float)c.facing * (float)e.a, c.feet.y}, trueSpeed(c, (float)e.b),
                   e.b >= 60 ? CA_RUN : CA_MOVE, true);
      break;
    case HC_FACE:
      if (c.facing != (e.a > 0 ? 1 : -1) && !c.moving)
        directorWalk(w, s, Vec2{c.feet.x + (e.a > 0 ? 1.f : -1.f), c.feet.y}, 12.f, CA_MOVE);
      break;
    case HC_ACT:
      critterSetAction(c, (CritterAction)e.a, maxf(0.05f, (float)e.b * 0.01f));
      cs.path_on = false;
      break;
    case HE_STEP:
      directorWalk(w, s, Vec2{c.feet.x + (float)c.facing * (float)e.a, c.feet.y}, trueSpeed(c, (float)e.b), CA_MOVE);
      break;
    case HE_HIT:
      if (e.a == 1) {   // a stamp with a little dust at the hoof
        const int b = worldTakeBit(w, BIT_DUST);
        if (b >= 0) {
          Bit& bt = w.bits[b];
          bt.pos = Vec2{c.feet.x + (float)c.facing * 9.f * c.scale, c.feet.y - 2.f};
          bt.vel = Vec2{0.f, -6.f};
          bt.land_y = c.feet.y;
        }
      } else if (e.a == 2) {   // pawing the water
        ring(w, Vec2{c.feet.x + (float)c.facing * 8.f * c.scale, c.feet.y + 1.f}, 0.6f);
      }
      break;
    case HC_RING: {
      const Vec2 nose = critterDeerNose(c);
      ring(w, Vec2{nose.x, c.feet.y + 1.f}, (float)e.a * 0.01f);
      break;
    }
    case HC_DROPS:
      for (int k = 0; k < e.a; ++k) {
        const float vx = d.rng.range(-24.f, 24.f);
        const float vy = d.rng.range(-74.f, -46.f);
        const float ox = d.rng.range(-5.f, 5.f);
        const int b = worldTakeBit(w, BIT_DROP);
        if (b < 0) continue;
        Bit& bt = w.bits[b];
        bt.pos = Vec2{c.feet.x + ox, c.feet.y - 3.f};
        bt.vel = Vec2{vx, vy};
        bt.ground_y = c.feet.y + 1.f;
        bt.land_y = c.feet.y + 1.f;
      }
      break;
    case HC_PRINT:
      herdPrintAdd(h, (int)floorf(c.feet.x + (float)e.b - 2.f + 0.5f), (int)floorf(c.feet.y - 1.f + 0.5f), e.a == HP_WET);
      break;
    case HC_NIBBLE:
      if (v.nib_clump >= 0 && v.nib == 0) {
        v.nib = 1;
        v.nib_t = 0.f;
        v.nib_off = Vec2{0.f, 0.f};
      }
      break;
    default: break;
  }
}

// The tent's small moves, whole pixels: 1 the fabric moves (+2 for 4 ticks, -1 for 4), 2 a touch (+1 for 3),
// 3 the zip gives (+2, -2, +1 over 12), 4 a tug at the flap (+1 for 2, -1 for 2).
void stepWobble(World& w, HerdDrive& v) {
  if (v.wob_kind == 0) return;
  const int t = v.wob_tick;
  float x = 0.f;
  bool done = false;
  switch (v.wob_kind) {
    case 1: x = t < 4 ? 2.f : (t < 8 ? -1.f : 0.f); done = t >= 8; break;
    case 2: x = t < 3 ? 1.f : 0.f; done = t >= 3; break;
    case 3: x = t < 4 ? 2.f : (t < 8 ? -2.f : (t < 12 ? 1.f : 0.f)); done = t >= 12; break;
    default: x = t < 2 ? 1.f : (t < 4 ? -1.f : 0.f); done = t >= 4; break;
  }
  w.tent.wobble = x;
  if (done) v.wob_kind = 0;
  else ++v.wob_tick;
}

// The second doe at the cattail: the stalk bends to her muzzle as she reaches, follows her tugs, and at
// kNibbleBite the head is in her mouth: the stub springs and the neighbours shiver.
void stepNibble(World& w, DayHerd& d) {
  HerdDrive& v = d.v;
  if (v.nib != 1) return;
  const int i = herdIndexOf(d.h, HR_GRAZER);
  if (i < 0 || !ours(w, d.h.d[i].slot) || d.h.d[i].mode == HM_ALERT || d.h.d[i].mode == HM_OUT) {
    v.nib = 2;   // the alarm took her off it: the stalk is let go and keeps its head
    return;
  }
  v.nib_t += SS_TICK_DT;
  if (v.nib_t >= kNibbleBite) {
    reedsTake(w, v.nib_clump, v.nib_stalk, true);
    reedsPush(w, v.nib_clump, 1.f, 0.3f);
    v.nib = 2;
    return;
  }
  const float k = easeInOut(clamp01((v.nib_t - 0.8f) / 0.4f));
  if (k <= 0.f) return;
  const Critter& c = w.critters[d.h.d[i].slot];
  const Vec2 nose = critterDeerNose(c);
  const Vec2 tip = reedsTip(w, v.nib_clump, v.nib_stalk);
  Vec2 want{nose.x - (tip.x - v.nib_off.x), nose.y - (tip.y - v.nib_off.y)};
  const float len = sqrtf(want.x * want.x + want.y * want.y);
  if (len > kNibbleBend) {
    want.x *= kNibbleBend / len;
    want.y *= kNibbleBend / len;
  }
  // Whole half pixels (the hold's own grid), so what was asked is what the tip shows next tick.
  v.nib_off = Vec2{floorf(want.x * k * 2.f + 0.5f) * 0.5f, floorf(want.y * k * 2.f + 0.5f) * 0.5f};
  reedsBend(w, v.nib_clump, v.nib_stalk, v.nib_off);
}

}  // namespace

uint8_t herdPlanPick(const Rng& loop_rng, const Inputs& in, Memory& mem, bool mushroom_plays) {
  if ((majorsS22Plans(mem) & S22_HERD) == 0) return 0;
  if (mem.herd_wait > 0) --mem.herd_wait;   // never two midday loops running
  Rng pick;
  pick.seed(loop_rng.state, kHerdStream);
  // One rng call per statement, always all three.
  const bool come = pick.chance(kHerdShare);
  static const float kVariant[3] = {0.5f, 0.3f, 0.2f};   // A, B the false alarm, C the fawn copies
  const int variant = pick.weighted(kVariant, 3);
  static const float kSize[3] = {0.60f, 0.25f, 0.15f};   // five, six, four
  const int size = pick.weighted(kSize, 3);
  if (!come) return 0;
  const uint8_t wk = in.have_outside ? weatherKindForCode(in.weather_code) : (uint8_t)WX_CLEAR;
  if (in.dog_out != 0 || wk == WX_RAIN || wk == WX_STORM || mem.herd_wait > 0 || mushroom_plays) return 0;
  mem.herd_wait = 2;
  return (uint8_t)((unsigned)(variant + 1) | ((unsigned)size << 2));
}

int herdSeconds(uint8_t herd) { return herd ? (int)(kHerdSeconds + 0.5f) : 0; }
int herdReturnExtra(uint8_t herd, int return_variant) { return herd ? (return_variant == 1 ? 4 : 16) : 0; }

void herdBeatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  memset(&w.day, 0, sizeof w.day);
  // A no-show (REVIEW_FIXES F21): a house dog wanted out since the build began keeps the deer away. The dog
  // itself cannot be in the scene yet (it waits for the build), so the test is the wish, not the dog.
  if (w.dogs.want != 0 || w.plan.herd == 0) return;
  w.day.kind = DAY_HERD;
  DayHerd& d = D(w);
  d.rng.seed(seedOf(w));
  const CampLayout& c = w.spine.camp;
  HerdLayout L;
  L.fire = w.fire.pos;
  L.tent = c.tent;
  L.door = c.door;
  L.camp_x = (float)w.land.camp_x;
  for (int k = 0; k < 2; ++k) L.edge_x = maxf(L.edge_x, (float)landscapeFgPineBounds(w.land, k).x1);
  const float sx = w.fire.pos.x + 190.f;
  L.splash = Vec2{sx, floorf(bankTop(w, sx, w.fire.pos.y + 3.f) + 4.f + 0.5f)};
  layoutReeds(w, L, d.v);
  const int8_t nc = d.v.nib_clump, ns = d.v.nib_stalk, rc = d.v.reed_clump;
  herdInit(d.h, d.rng, L, w.plan.herd, !memoryBrave(w.memory, CR_DEER));
  d.v = HerdDrive{};
  d.v.nib_clump = nc;
  d.v.nib_stalk = ns;
  d.v.reed_clump = rc;
  d.v.beat_on = 1;
  d.v.door = d.v.door_to = w.tent.door;
}

void herdBeatUpdate(SceneCtx& ctx, float, float) {
  World& w = *ctx.w;
  if (!live(w) || !D(w).v.beat_on) return;
  DayHerd& d = D(w);
  HerdState& h = d.h;
  HerdDrive& v = d.v;

  HerdIn in{};
  in.dt = SS_TICK_DT;
  uint8_t fl = 0;
  if (h.events & 1) fl = (uint8_t)(fl | HI_BELL);
  h.events = 0;
  if (ctx.in && ctx.in->passer_by) fl = (uint8_t)(fl | HI_PASSER);
  if (w.dogs.want != 0) fl = (uint8_t)(fl | HI_DOG);
  if (w.weather.flash > 0.5f) fl = (uint8_t)(fl | HI_FLASH);
  if (w.weather.kind == WX_WIND || w.weather.kind == WX_STORM || w.weather.gust > 0.5f) fl = (uint8_t)(fl | HI_WIND);
  if (w.weather.rain > 0.2f) fl = (uint8_t)(fl | HI_RAIN);
  in.flags = fl;
  for (int i = 0; i < h.n; ++i) {
    const int s = h.d[i].slot;
    if (ours(w, s)) {
      in.in_use[i] = true;
      in.arrived[i] = critterArrived(w.critters[s]);
      in.x[i] = w.critters[s].feet.x;
    } else {   // not spawned yet (it counts as held), or a spawn that found no slot, or let go by the director
      in.in_use[i] = h.d[i].entered == 0;
      in.arrived[i] = false;
      in.x[i] = kHerdSpawnX;
    }
  }
  HerdOut out;
  herdTick(h, d.rng, in, out);
  for (int k = 0; k < out.n; ++k) order(w, d, out.ev[k]);

  // What each deer wears this tick: the ears, the white flag, a sniff, the lifted hoof.
  for (int i = 0; i < h.n; ++i) {
    const int s = h.d[i].slot;
    if (!ours(w, s)) continue;
    Critter& c = w.critters[s];
    const HerdView hv = herdView(h.d[i]);
    c.tone = (uint8_t)((h.d[i].role == HR_FAWN ? kDeerFawn : 0) | hv.ear);
    c.tail_add = hv.tail;
    c.head_add = hv.head_add;
    c.fore_add = hv.fore_add;
  }

  // The fawn has landed with its nose in the reeds: the whole clump leans away and rings.
  if (v.fawn_reed == 1) {
    const int i = herdIndexOf(h, HR_FAWN);
    const int s = i >= 0 ? h.d[i].slot : -1;
    if (!ours(w, s) || h.d[i].mode == HM_ALERT || h.d[i].mode == HM_OUT) {
      v.fawn_reed = 3;
    } else if (!w.critters[s].moving && fabsf(w.critters[s].feet.x - h.fawn_reed.x) < 1.5f) {
      reedsPush(w, v.reed_clump, 1.f, 0.8f);
      v.fawn_reed = 2;
    }
  }
  stepNibble(w, d);
  stepWobble(w, v);
  if (v.door != v.door_to) {
    const float step = v.door_rate * SS_TICK_DT;
    if (fabsf(v.door_to - v.door) <= step) v.door = v.door_to;
    else v.door += v.door_to > v.door ? step : -step;
  }
  w.tent.door = v.door;
}

void herdBeatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  if (!live(w)) return;
  DayHerd& d = D(w);
  bool late = false;
  for (int i = 0; i < d.h.n; ++i) {
    const int s = d.h.d[i].slot;
    d.h.d[i].slot = -1;
    if (!ours(w, s)) continue;
    // Still in the scene: off to the left, fast, and gone within about 2.5 s. Never to the right: he walks in
    // at the ford on the right in the very next tick (RETURN_IN), and a deer bolting right ran through him
    // (C4 review, 2026-11-19: up to 2 s of deer over the hiker after a cut-short beat).
    late = true;
    w.critters[s].tail_add = 1.f;
    directorExit(w, s, -1, trueSpeed(w.critters[s], maxf(140.f, (w.critters[s].feet.x - kHerdExitLX) / 2.5f)), CA_RUN);
  }
  if (late && w.spine.overruns < 255) ++w.spine.overruns;
  d.v.overrun = late ? 1 : 0;
  w.tent.wobble = 0.f;
  d.v.wob_kind = 0;
  d.v.door = d.v.door_to;
  w.tent.door = d.v.door;
  d.v.beat_on = 0;
  d.v.over = 1;
  d.v.nib = 2;
  d.v.fawn_reed = 3;
  if (d.v.door <= 0.02f) d.h.signs = (uint8_t)(d.h.signs & ~1u);
  if ((d.h.signs & 3) == 0) memset(&w.day, 0, sizeof w.day);   // nothing was left behind
}

void herdBell(World& w) {
  if (live(w) && D(w).v.beat_on) D(w).h.events = (uint8_t)(D(w).h.events | 1);
}

bool herdGuestAt(const World& w, Vec2& out) {
  if (!live(w) || !D(w).v.beat_on) return false;
  // The deer nearest the dog's side (the last one out) that is still well inside the frame (DEER_PLAN
  // risk 6): one already past x 760, or below x 40, is not worth a chase.
  bool any = false;
  float best = 1e9f;
  for (int i = 0; i < D(w).h.n; ++i) {
    const int s = D(w).h.d[i].slot;
    if (!ours(w, s) || !w.critters[s].visible) continue;
    const Vec2 f = w.critters[s].feet;
    if (f.x < 40.f || f.x > 760.f) continue;
    if (f.x < best) {
      best = f.x;
      out = f;
      any = true;
    }
  }
  return any;
}

bool herdOnlySigns(const World& w) { return live(w) && D(w).v.over && !D(w).v.beat_on; }

int herdDeerSlots(const World& w, int8_t slots[HERD_MAX]) {
  int n = 0;
  for (int i = 0; i < HERD_MAX; ++i) {
    slots[i] = -1;
    if (!live(w) || !D(w).v.beat_on || i >= D(w).h.n) continue;
    const int s = D(w).h.d[i].slot;
    if (!ours(w, s)) continue;
    slots[i] = (int8_t)s;
    ++n;
  }
  return n;
}

// ---- what he finds --------------------------------------------------------------------------------------------

bool herdSigns(const World& w) { return herdOnlySigns(w) && (D(w).h.signs & 3) != 0 && (D(w).h.signs & 4) == 0; }
bool herdFlapAjar(const World& w) { return herdOnlySigns(w) && (D(w).h.signs & 1) != 0 && w.tent.door > 0.02f; }

void herdActLookDown(World& w, float u) {
  w.fx.head = -0.5f * scriptEnvelope(u, 0.35f, 0.3f);   // (-0.4 until the C4 review: too small to see at 1x)
  if (u >= 1.f) scriptFxRest(w);
}

void herdActZip(World& w, float u) {
  if (!live(w)) return;
  HerdDrive& v = D(w).v;
  const float k = scriptEnvelope(u, 0.25f, 0.25f);
  scriptSetArm(w, 0, k, 1.35f, 0.25f);   // the near hand out to the zip
  w.tent.door = v.door * (1.f - easeInOut(u));
  if (u >= 1.f) {
    scriptFxRest(w);
    w.tent.door = 0.f;
    v.door = v.door_to = 0.f;
    D(w).h.signs = (uint8_t)(D(w).h.signs & ~1u);
  }
}

void herdDoSeen(World& w, float) {
  if (live(w)) herdPrintsSeen(D(w).h);
}
// At the ford: when there is no flap for him to see to (the alarm came before the tug, or this is the
// limping return, which goes straight to his seat), the look was all of it.
void herdDoLooked(World& w, float) {
  if (!live(w)) return;
  if (!herdFlapAjar(w) || w.plan.fvar[0] == 1) herdPrintsSeen(D(w).h);
}

// ---- the day hooks --------------------------------------------------------------------------------------------

Rect herdDayBounds(const DayHerd& d) { return herdPrintsBounds(d.h); }
void herdDayPose(World&) {}
bool herdDayHoldsFacing(const World&) { return false; }

void herdDayStep(World& w, bool frozen) {
  if (frozen || !live(w)) return;
  DayHerd& d = D(w);
  if (!d.v.over) return;   // the beat ages its own clock
  herdPrintsStep(d.h, SS_TICK_DT);
  // He has looked and the prints have faded: the union is free again. (A flap left ajar by the limping
  // return stays ajar: it is the tent's own state, and nothing at midday uses the door.)
  if (herdPrintsDone(d.h)) memset(&w.day, 0, sizeof w.day);
}

void herdDayTrack(const DayHerd& prev, const DayHerd& cur, const World&, Renderer& r) {
  Rect out;
  if (herdDirty(prev.h, cur.h, out)) r.markDirtyRect(out);
}

bool herdDayPartY(const World& w, int part, int16_t& y) {
  if (part != 0 || !herdPrintsLive(D(w).h)) return false;
  y = 319;   // flat on the ground, under every ground actor
  return true;
}

void herdDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal) {
  if (part == 0) herdPrintsDraw(D(w).h, s, pal, clip);
}

}  // namespace ss
