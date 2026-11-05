// The morning where the camp stays up (MORNING_DAYTIME_PLAN.md section 4), the other half of the
// morning family beside Mark's "break camp and leave" (spine_morning.cpp). morningPlan picks it per
// loop (LoopPlan::stay; 60 %, PROPOSED). Every look and every number below is PROPOSED, and the look
// UNVERIFIED until Mike has seen the contact sheets.
//
// Beats: wake and coffee are Mark's (his scripts and seconds; coffee has a fourth variant, d, the
// boil-over, that only this morning draws), then
//   pee        the sheepish glance at us, off behind the big pine (always the taller foreground one),
//              out of sight 12 to 18 s: the pine rustles, a squirrel leaps off a branch and bolts, the
//              raccoon slips in and takes his shoe (else the mug, else the book: the night thief's
//              rule, spine.cpp hookTheft) and goes back out behind the pine; he comes back and looks
//              down at what is left;
//   breakfast  the pan on the fire and the pancake flip: a it lands back in the pan, b it lands on his
//              hat, c a bird takes it out of the air;
//   wash       the stream: splashes, a shudder, the toothbrush, a spit;
//   tidy       the sleeping bag into its stuff sack: it bulges, he sits on it, ties it, stands: it
//              springs open (variant a: the second try holds; b: it springs again and he takes it
//              into the tent as it is);
//   day_hike   the fire kicked out, the day pack on, a look at the map, off right over the ford; the
//              camp stays up (tent, wood, cairn, pot and pan by the fire) and the animals creep back
//              before the fade.
//
// Same idioms as spine_morning.cpp: actions take u 0..1 and leave the world at rest at u = 1; scripts
// are const Step tables; a beat's seconds are fixed (the pee's depend on the plan's away_s) and
// longer than its script, an overrun counts in SpineState::overruns. One rng call per statement; the
// plan draws from the stay's own stream (morningPlan). The critters' stage machines (stayTick) run
// from SpineState::xstage on the step clock (xt, xt2: summed from SS_TICK_DT, so the doorbell reaction
// freezes them with the story), never from World::tick. Palette slots only; no allocation.
#include <math.h>
#include <stdio.h>

#include "../anim/ease.h"
#include "../ss_app.h"
#include "director.h"
#include "script.h"
#include "spine_family.h"
#include "world.h"

namespace ss {

using namespace sc;

namespace {

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

float plateau(float s, float c, float up, float hold) {
  return easeInOut(window(s, c, c + up)) * (1.f - easeInOut(window(s, c + up + hold, c + 2.f * up + hold)));
}
inline float envelope(float u, float a, float b) { return scriptEnvelope(u, a, b); }
inline void setArm(World& w, int i, float k, float sh, float el) { scriptSetArm(w, i, k, sh, el); }
inline void fxRest(World& w) { scriptFxRest(w); }
inline void setPose(World& w, PoseId p, float s) { scriptSetPose(w, p, s); }

constexpr float kFadeS = 2.5f;

// xbits: what the stage machines tell each other and the scripts.
enum : uint8_t {
  XB_RAID_OVER = 1,   // the raccoon is out of sight again (or never came): he may come back
  XB_BIRD_OK = 2,     // breakfast c: the bird is perched on the tent ridge
  XB_LAUNCH = 4,      // breakfast c: the cake has left the pan (the bird dives)
  XB_SNATCHED = 8,    // breakfast c: the bird has it
  XB_ON_HEAD = 16,    // breakfast b: the cake is on his hat
  XB_IN_HAND = 32,    // breakfast b: he holds it
  XB_PAN_HELD = 64,   // the pan is in his hands (carried to the fire)
  XB_MAP = 128        // the map is in his hands
};
enum XStage : uint8_t { XS_NONE = 0, XS_PEE, XS_BIRD, XS_CREEP };

// ---- places --------------------------------------------------------------------------------

// The tree he goes behind: the taller foreground pine (fg[1]), always. Its right edge at height y
// (landscape.cpp drawPine and tierGeom: keep in step).
const LsPine& bigPine(const World& w) { return w.land.fg[1]; }
float pineEdge(const LsPine& p, float y) {
  const float h = (float)p.h, top = (float)p.base_y - h;
  const float fbot = (float)p.base_y - maxf(4.f, h * 0.28f), F = fbot - top, th = 0.42f * F;
  const int n = p.tiers;
  const float sp = n > 1 ? (F - th) / (float)(n - 1) : 0.f;
  float best = 0.f;
  for (int i = 0; i < n; ++i) {
    const float apex = top + (float)i * sp, base = apex + th;
    if (y < apex || y > base) continue;
    const float half = 0.5f * ((float)p.width_pct * 0.01f) * h * (1.f - 0.2f * (float)(n - 1 - i));
    best = maxf(best, half * (y - apex) / th);
  }
  return (float)p.x + best;
}
Vec2 hideSpot(const World& w) { return Vec2{(float)bigPine(w).x + 4.f, 372.f}; }
constexpr float kRustleY = 338.f;   // the height on the pine's edge where it shakes
Vec2 rustleAt(const World& w) { return Vec2{pineEdge(bigPine(w), kRustleY), kRustleY}; }
// Where the shoes lie (Mark's wake b has it too): beside the tent's front corner.
Vec2 shoeAt(const World& w) { return Vec2{w.spine.camp.door.x + 30.f, w.spine.camp.door.y + 3.f}; }
Vec2 sackAt(const World& w) { return Vec2{w.spine.camp.tent.x + 98.f, w.spine.camp.tent.y + 16.f}; }
Vec2 panRest(const World& w) { return Vec2{w.fire.pos.x - 24.f, w.fire.pos.y + 12.f}; }
Vec2 panTop(const World& w) { return Vec2{w.fire.pos.x, w.fire.pos.y - 5.f}; }
Vec2 ridgeTop(const World& w) { return Vec2{w.tent.pos.x + 11.f, w.tent.pos.y - 49.f}; }   // as Mark's (props.cpp tentGeom)

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}
void flyAlong(Critter& c, Vec2 a, Vec2 ctl, Vec2 b, float u) {
  const Vec2 p = bez(a, ctl, b, u);
  if (fabsf(p.x - c.feet.x) > 0.05f) c.facing = p.x < c.feet.x ? -1 : 1;
  c.feet = p;
}

// ---- critters ------------------------------------------------------------------------------

bool live(const World& w, int s) { return s >= 0 && w.crit[s].in_use; }
int spawn(World& w, CritterKind k, Vec2 feet, int facing, uint8_t tag) {
  const int s = directorSpawn(w, k, feet, facing);
  if (s < 0) return -1;
  w.crit[s].tag = tag;
  w.crit[s].flags = CF_BRAVE;   // he is hidden or gone when they work; nothing scatters them
  memoryApplyPersona(w.memory, w.critters[s]);
  return s;
}
// Send a critter off by the nearer edge (a beat's end, an overrun).
void sendOff(World& w, int s) {
  if (!live(w, s) || w.crit[s].mode != CM_FREE) return;
  const Critter& c = w.critters[s];
  const bool big = c.kind == CR_RACCOON;
  directorExit(w, s, c.feet.x < 0.5f * (float)SS_WIDTH ? -1 : 1, big ? 80.f : 90.f, CA_RUN);
}

// ---- wake and coffee are Mark's: nothing here --------------------------------------------------

// ---- pee -------------------------------------------------------------------------------------

// "Just going over here": he looks out at us, a shrug, and points at the tree.
constexpr float kGlanceS = 3.4f;
void actGlance(World& w, float u) {
  const float s = u * kGlanceS;
  const float look = plateau(s, 0.2f, 0.4f, 0.9f);
  w.fx.head = -0.3f * look;
  w.fx.lift = 0.01f * look;
  setArm(w, 0, 0.8f * look, 0.3f, 1.55f);   // palms up
  setArm(w, 1, 0.8f * look, 0.3f, 1.55f);
  const float point = plateau(s, 2.0f, 0.3f, 0.6f);
  if (point > 0.f) {
    w.fx.reach_to = 1.5f;
    w.fx.reach = point * (0.85f + 0.15f * sinT(s * 6.f));
    w.fx.head = 0.08f * point;
  }
  if (u >= 1.f) fxRest(w);
}

// The raid's loot, by the night thief's rule: the shoe if it is out, else the mug, else the book.
uint8_t raidLoot(const World& w) {
  if (w.items[ITEM_SHOE].down) return ITEM_SHOE + 1;
  if (w.items[ITEM_MUG].down) return ITEM_MUG + 1;
  if (w.items[ITEM_BOOK].down) return ITEM_BOOK + 1;
  return 0;
}
void doPeeStart(World& w, float) {
  SpineState& sp = w.spine;
  sp.xstage = XS_PEE;
  sp.xt = 0.f;
  sp.xt2 = 0.f;
  sp.xstep = 0;
  sp.xbits = (uint8_t)(sp.xbits & ~XB_RAID_OVER);
  sp.xloot = raidLoot(w);
  w.busy = true;
}
bool raidOver(const World& w) { return (w.spine.xbits & XB_RAID_OVER) != 0; }

// He looks down at what is left, a slow shake of the head.
void actLookDown(World& w, float u) {
  const float k = envelope(u, 0.2f, 0.25f);
  w.fx.head = k * (0.5f + 0.06f * sinT(u * 3.f));
  w.fx.lean = 0.07f * k;
  if (u >= 1.f) fxRest(w);
}
void actShrug(World& w, float u) {
  const float k = easeOutCubic(window(u, 0.f, 0.25f)) * (1.f - easeInOut(window(u, 0.6f, 1.f)));
  w.fx.lift = 0.024f * k;
  w.fx.head = 0.1f * k;
  setArm(w, 0, 0.75f * k, 0.3f, 1.55f);
  setArm(w, 1, 0.75f * k, 0.3f, 1.55f);
  if (u >= 1.f) fxRest(w);
}

constexpr float kRustleAtS = 0.9f;     // seconds after he is behind the tree: it shakes
constexpr float kSquirrelAtS = 1.7f;   // the squirrel goes
constexpr float kRaccoonMinS = 2.8f;   // the raccoon is in by then, or later so it is out as he comes back

// The squirrel: sits hidden in the foliage on a branch, startled, leaps out and down in an arc,
// lands and bolts to the right.
void squirrelTick(World& w, float t) {
  SpineState& sp = w.spine;
  const Vec2 edge = rustleAt(w);
  const Vec2 branch{edge.x - 9.f, kRustleY - 2.f}, land{edge.x + 40.f, 371.f};
  if (sp.xcrit[0] == -1 && t >= kSquirrelAtS) {
    const int s = spawn(w, CR_SQUIRREL, branch, 1, CT_DINNER);
    sp.xcrit[0] = (int8_t)(s < 0 ? -2 : s);   // -2: none to spare, it never comes
    if (s >= 0) critterSetAction(w.critters[s], CA_STARTLE, 0.25f);
  }
  const int s = sp.xcrit[0];
  if (s < 0 || !live(w, s) || w.crit[s].mode != CM_FREE || w.crit[s].tag != CT_DINNER) return;
  Critter& c = w.critters[s];
  const float k = (t - kSquirrelAtS - 0.35f) / 0.75f;   // the leap: 0.35 s after the start, 0.75 s long
  if (k < 0.f) return;
  if (k < 1.f) {
    c.feet = Vec2{lerp(branch.x, land.x, k), lerp(branch.y, land.y, k) - 26.f * 4.f * k * (1.f - k)};
  } else {
    c.feet = land;
    directorExit(w, s, 1, 100.f, CA_RUN);
  }
}

// The raccoon: in from the left edge behind the pine, to the shoes, a sniff, one taken, a guilty look
// toward the tree, and out the way it came. Stages in SpineState::xstep, its clock xt2.
void raccoonTick(World& w, float t, float tr) {
  SpineState& sp = w.spine;
  const Vec2 at = shoeAt(w);
  const uint8_t loot = sp.xloot;
  const Vec2 goal = loot == ITEM_SHOE + 1 ? Vec2{at.x - 22.f, at.y + 1.f}
                                           : (loot ? Vec2{w.items[loot - 1].pos.x - 12.f, w.items[loot - 1].pos.y - 1.f}
                                                   : Vec2{at.x - 22.f, at.y + 1.f});
  if (sp.xstep == 0) {
    if (t < tr) return;
    const int s = spawn(w, CR_RACCOON, Vec2{-38.f, 369.f}, 1, CT_THIEF);
    if (s < 0) {   // no room: no raid
      sp.xbits = (uint8_t)(sp.xbits | XB_RAID_OVER);
      sp.xstep = 9;
      return;
    }
    sp.xcrit[1] = (int8_t)s;
    directorWalk(w, s, goal, 62.f, CA_MOVE);
    sp.xstep = 1;
    sp.xt2 = 0.f;
    return;
  }
  const int s = sp.xcrit[1];
  sp.xt2 += SS_TICK_DT;
  const float t2 = sp.xt2;
  if (sp.xstep == 9) return;
  if (!live(w, s) || w.crit[s].tag != CT_THIEF) {   // out of sight
    sp.xbits = (uint8_t)(sp.xbits | XB_RAID_OVER);
    sp.xstep = 9;
    return;
  }
  if (w.crit[s].mode != CM_FREE) return;   // already on its way out
  Critter& c = w.critters[s];
  auto next = [&](uint8_t st) {
    sp.xstep = st;
    sp.xt2 = 0.f;
  };
  switch (sp.xstep) {
    case 1:
      if (critterArrived(c)) {
        critterSetAction(c, CA_SNIFF, 0.4f);
        next(2);
      }
      break;
    case 2:   // a good long sniff
      if (t2 > 1.4f) {
        critterSetAction(c, loot ? CA_CARRY : CA_LOOK, 0.4f);
        next(loot ? 3 : 5);
      }
      break;
    case 3:   // picks it up
      if (t2 > 0.3f) {
        if (loot == ITEM_SHOE + 1) {   // one of the pair: the raccoon carries it itself (the rig draws it)
          w.items[ITEM_SHOE].alt = false;
          c.carry = CC_SHOE;
          w.memory.shoe_taken = true;
        } else if (w.items[loot - 1].down) {   // the mug or the book, in its mouth
          Item& it = w.items[loot - 1];
          it.down = false;
          it.held = true;
          it.critter = (uint8_t)(s + 1);
          it.angle = 0.f;
          it.placed = false;
        }
        w.memory.last_stolen = loot;
        next(4);
      }
      break;
    case 4:   // the guilty look, toward the tree, then off
      if (t2 > 0.3f && c.action != CA_LOOK) critterSetAction(c, CA_LOOK, 0.3f);
      if (t2 > 1.4f) {
        directorExit(w, s, -1, 88.f, CA_RUN);
        next(6);
      }
      break;
    case 5:   // nothing to take: a look about
      if (t2 > 1.8f) {
        directorExit(w, s, -1, 70.f, CA_MOVE);
        next(6);
      }
      break;
    default: break;
  }
}

// Two leaves the shaking sets loose: out of the foliage, turning and swaying, down to the ground (a
// World::bits BIT_LEAF each, placed here every tick as the minors place theirs; once down they rest and
// may be recycled).
constexpr float kLeafAtS[2] = {kRustleAtS + 0.4f, kRustleAtS + 1.1f};
constexpr float kLeafFallS = 2.2f;
void leafTick(World& w, float t) {
  SpineState& sp = w.spine;
  const Vec2 edge = rustleAt(w);
  for (int i = 0; i < 2; ++i) {
    const float u = (t - kLeafAtS[i]) / kLeafFallS;
    if (u < 0.f) continue;
    if (sp.xbit[i] < 0) {
      const int s = worldTakeBit(w, BIT_LEAF);
      if (s < 0) continue;   // none to spare
      sp.xbit[i] = (int8_t)s;
    }
    Bit& b = w.bits[sp.xbit[i]];
    if (b.kind != BIT_LEAF || b.resting) continue;
    const float k = clamp01(u);
    const float x0 = edge.x + (i ? 3.f : -5.f), y0 = kRustleY - 16.f + 8.f * (float)i, ground = 371.f + 2.f * (float)i;
    b.pos = Vec2{x0 + (i ? 12.f : 9.f) * sinT(0.5f * k) + 5.f * sinT(1.5f * k), y0 + (ground - y0) * k * (0.3f + 0.7f * k)};
    b.spin = 0.15f * (float)i + 1.3f * k;
    b.land_y = ground;
    if (k >= 1.f) b.resting = true;
  }
}

void peeTick(World& w) {
  SpineState& sp = w.spine;
  sp.xt += SS_TICK_DT;
  const float t = sp.xt;
  const float rt = t - kRustleAtS;
  w.rustle.at = rustleAt(w);
  w.rustle.t = rt >= 0.f && rt < RUSTLE_S ? rt : -1.f;
  leafTick(w, t);
  squirrelTick(w, t);
  raccoonTick(w, t, maxf(kRaccoonMinS, (float)w.plan.away_s - 11.5f));
  if (t > (float)w.plan.away_s + 28.f) sp.xbits = (uint8_t)(sp.xbits | XB_RAID_OVER);   // never hold him back for good
  if ((sp.xbits & XB_RAID_OVER) && w.rustle.t < 0.f) sp.xstage = XS_NONE;
}

// ---- breakfast -------------------------------------------------------------------------------

bool potOnFire(const World& w) { return w.items[ITEM_POT].on_fire; }

// The pan out of the pack, in his hands (handle forward); onto the fire from them.
void doPanOut(World& w, float) {
  Item& p = w.items[ITEM_PAN];
  p = Item{panTop(w), false, true, 1, 1.f, w.hiker.feet.y, false, 0, 0.f, 0.f, 1.f, false};
  hikerCarry(w.hiker, CARRY_MUG);
  w.spine.xbits = (uint8_t)(w.spine.xbits | XB_PAN_HELD);
}
void actPanOnFire(World& w, float u) {
  Item& p = w.items[ITEM_PAN];
  if (u <= 0.f) {
    w.spine.v0 = hikerHandsMid(w.hiker);
    w.spine.xbits = (uint8_t)(w.spine.xbits & ~XB_PAN_HELD);
    p.held = false;
    p.down = true;
    p.on_fire = false;
    p.facing = 1;
    hikerCarry(w.hiker, CARRY_NONE);
  }
  const Vec2 top = panTop(w);
  const float k = easeInOut(window(u, 0.1f, 0.8f));
  p.pos = Vec2{lerp(w.spine.v0.x - 26.f * (float)w.hiker.facing, top.x, k), lerp(w.spine.v0.y, top.y, k) - 5.f * sinT(0.5f * k)};
  p.on_fire = k > 0.5f;
  w.fx.reach_to = 1.2f;
  w.fx.reach = envelope(u, 0.2f, 0.3f);
  if (u >= 1.f) {
    p.pos = top;
    p.on_fire = true;
    fxRest(w);
  }
}
// The batter poured from the mug: a pour, a hiss of steam, the pancake there and cooking.
void actBatter(World& w, float u) {
  Item& p = w.items[ITEM_PAN];
  const float k = envelope(u, 0.2f, 0.3f);
  w.fx.reach_to = 1.2f;
  w.fx.reach = 0.85f * k;
  w.fx.lean = 0.1f * k;
  if (u >= 0.5f) p.fill = 1.f;
  Steam& st = w.steam;
  st.at = Vec2{panTop(w).x, panTop(w).y - 8.f};
  st.n = 2;
  st.t = u >= 1.f ? -1.f : window(u, 0.5f, 1.f) * 1.0f;
  if (u >= 1.f) fxRest(w);
}
// It cooks: a wisp now and then, he leans in to watch.
void actSizzle(World& w, float u) {
  const float s = u * 3.2f;
  Steam& st = w.steam;
  st.at = Vec2{panTop(w).x, panTop(w).y - 8.f};
  st.n = 1;
  st.t = u >= 1.f ? -1.f : s - 1.1f * floorf(s / 1.1f);
  const float k = envelope(u, 0.2f, 0.2f);
  w.fx.reach_to = 1.2f;
  w.fx.reach = 0.7f * k;
  w.fx.lean = 0.1f * k;
  w.fx.head = 0.15f * k;
  if (u >= 1.f) fxRest(w);
}
void doBirdWatch(World& w, float) {   // breakfast c: a bird comes in and perches on the tent's ridge
  SpineState& sp = w.spine;
  const Vec2 from{(float)SS_WIDTH + 24.f, 150.f};
  const int s = spawn(w, CR_BIRD, from, -1, CT_DINNER);
  if (s < 0) return;
  w.crit[s].sort_y = (int16_t)(w.tent.pos.y + 1.f);
  sp.xcrit[0] = (int8_t)s;
  sp.xstage = XS_BIRD;
  sp.xt = 0.f;
  sp.xbits = (uint8_t)(sp.xbits & ~(XB_BIRD_OK | XB_LAUNCH | XB_SNATCHED));
  critterSetAction(w.critters[s], CA_MOVE, 0.1f);
  w.busy = true;
}

// The flip: the pan dips, flicks up, and the pancake goes (outcome 0 back into the pan, 1 onto his
// hat, 2 into a bird's feet), spinning edge over edge.
constexpr float kLaunchS = 0.8f;
constexpr float kFlightS[3] = {1.1f, 1.4f, 1.5f};
constexpr float kFlipS[3] = {2.8f, 3.1f, 3.2f};
Vec2 headTarget(const World& w) {
  float r = 0.f;
  const Vec2 hd = hikerHeadPos(w.hiker, &r);
  return Vec2{hd.x, hd.y - r - 7.f};   // the crown of his hat (UNVERIFIED: tuned by eye)
}
void flip(World& w, float u, int outcome) {
  SpineState& sp = w.spine;
  Item& pan = w.items[ITEM_PAN];
  Item& cake = w.items[ITEM_CAKE];
  const float s = u * kFlipS[outcome];
  const Vec2 top = panTop(w), c0{top.x, top.y - 8.9f};
  // The hand on the handle: a dip (wind-up), then the flick.
  const float dip = plateau(s, 0.1f, 0.3f, 0.1f);
  const float flick = easeOutCubic(window(s, kLaunchS - 0.15f, kLaunchS)) * (1.f - easeInOut(window(s, kLaunchS, kLaunchS + 0.4f)));
  w.fx.reach_to = 1.2f - 0.2f * dip + 0.45f * flick;
  w.fx.reach = 0.9f;
  w.fx.lean = 0.1f - 0.08f * dip - 0.04f * flick;
  w.fx.head = 0.1f - 0.2f * flick;
  pan.angle = -0.02f * dip + 0.1f * flick;
  pan.pos = Vec2{top.x, top.y + 1.5f * dip};
  const bool bird = outcome == 2 && (sp.xbits & XB_BIRD_OK);
  const int how = outcome == 2 && !bird ? 0 : outcome;   // no bird to spare: it lands back
  if (s >= kLaunchS && !(sp.target & 1u) && pan.fill > 0.5f) {   // it leaves the pan (target bit 0: it has)
    sp.target = (uint8_t)(sp.target | 1u);
    pan.fill = 0.f;
    cake = Item{c0, true, false, 1, 1.f, c0.y, false, 0, 0.f, 0.f, 1.f, false};
    cake.fill = 1.f;
    if (how == 1) w.spine.v0 = headTarget(w);
    if (how == 2) sp.xbits = (uint8_t)(sp.xbits | XB_LAUNCH);
  }
  if (cake.down && !(sp.xbits & (XB_SNATCHED | XB_ON_HEAD))) {
    const float fl = clamp01((s - kLaunchS) / kFlightS[how]);
    Vec2 p = c0;
    float spins = 1.f;
    if (how == 0) {
      p = Vec2{c0.x + 2.f * sinT(0.5f * fl), c0.y - 4.f * 34.f * fl * (1.f - fl)};
    } else if (how == 1) {
      const Vec2 T = w.spine.v0;
      p = Vec2{lerp(c0.x, T.x, fl), lerp(c0.y, T.y, fl) - 4.f * 52.f * fl * (1.f - fl)};
      spins = 2.f;
    } else {
      p = Vec2{lerp(c0.x, c0.x + 14.f, fl), c0.y - 4.f * 46.f * fl * (1.f - fl)};
    }
    cake.pos = p;
    cake.fill = fabsf(cosT(0.5f * spins * easeInOut(fl)));
    cake.angle = 0.05f * sinT(fl * 2.f);
    cake.ground_y = p.y;
    if (how == 2 && fl >= 0.5f && (sp.xbits & XB_LAUNCH)) sp.xbits = (uint8_t)(sp.xbits | XB_SNATCHED);   // the bird has it
    if (fl >= 1.f) {
      if (how == 0) {   // back in the pan: it bounces once
        cake.down = false;
        pan.fill = 1.f;
      } else if (how == 1) {   // draped over his hat
        cake.down = false;
        cake.held = true;
        cake.alt = true;
        cake.fill = 1.f;
        sp.xbits = (uint8_t)(sp.xbits | XB_ON_HEAD);
      }
    }
  }
  if (how == 0 && pan.fill > 0.5f && !cake.down)   // the cake lands in it: the pan gives a px and comes back
    pan.pos.y = top.y + 1.5f * sinT(0.5f * window(s, kLaunchS + kFlightS[0], kLaunchS + kFlightS[0] + 0.35f));
  if (u >= 1.f) {
    pan.angle = 0.f;
    pan.pos = top;
    fxRest(w);
  }
}
void actFlipA(World& w, float u) { flip(w, u, 0); }
void actFlipB(World& w, float u) { flip(w, u, 1); }
void actFlipC(World& w, float u) { flip(w, u, 2); }

// The reactions: a nod; hands out, frozen, with it on his hat; a look up after the bird, a shrug.
void actNodA(World& w, float u) { w.fx.head = 0.28f * (0.5f - 0.5f * cosT(2.f * u)) + 0.f; if (u >= 1.f) fxRest(w); }
void actFrozen(World& w, float u) {
  const float k = envelope(u, 0.1f, 0.2f);
  setArm(w, 0, 0.8f * k, 0.9f, 0.6f);
  setArm(w, 1, 0.8f * k, 0.9f, 0.6f);
  w.fx.head = 0.05f * k;
  if (u >= 1.f) fxRest(w);
}
// Takes it off his head (the cake into his hand), and eats it: three bites.
void actCakeOff(World& w, float u) {
  Item& cake = w.items[ITEM_CAKE];
  const float lift = easeInOut(window(u, 0.f, 0.25f));
  if (u >= 0.3f && (w.spine.xbits & XB_ON_HEAD)) {
    w.spine.xbits = (uint8_t)((w.spine.xbits & ~XB_ON_HEAD) | XB_IN_HAND);
    cake.alt = false;
  }
  const float s = (u - 0.35f) / 0.65f * 3.f;
  const float ph = s - floorf(s);
  const float bite = u > 0.35f ? plateau(ph, 0.1f, 0.25f, 0.2f) : 0.f;
  setArm(w, 0, 1.f, lerp(2.7f, 0.9f, easeInOut(window(u, 0.2f, 0.4f))) - 0.05f * bite, lerp(0.4f, 2.4f, easeInOut(window(u, 0.2f, 0.4f))));
  w.fx.head = lift * -0.2f * (1.f - easeInOut(window(u, 0.2f, 0.4f))) - 0.12f * bite;
  if (u >= 0.95f) {
    cake.held = false;
    cake.down = false;
    w.spine.xbits = (uint8_t)(w.spine.xbits & ~XB_IN_HAND);
  }
  if (u >= 1.f) fxRest(w);
}
// Eats what is in the pan, down to the pan and up to his mouth, three times.
void actEatPan(World& w, float u) {
  const float s = u * 3.f;
  const float env = envelope(u, 0.05f, 0.08f);
  const float ph = s - floorf(s);
  const float dip = plateau(ph, 0.f, 0.18f, 0.08f);
  const float lift = plateau(ph, 0.42f, 0.16f, 0.14f);
  setArm(w, 0, env, 0.5f + 0.25f * dip + 0.4f * lift, 1.3f - 1.1f * dip + 1.15f * lift);
  w.fx.head = env * (0.08f + 0.25f * dip - 0.14f * lift);
  if (u >= 0.9f) w.items[ITEM_PAN].fill = 0.f;
  if (u >= 1.f) fxRest(w);
}
// After the bird: he watches it go, up and away, then looks at the empty pan.
void actWatchCake(World& w, float u) {
  const float up = plateau(u * 3.4f, 0.1f, 0.4f, 1.4f);
  const float down = plateau(u * 3.4f, 2.0f, 0.3f, 0.4f);
  w.fx.head = -0.4f * up + 0.4f * down;
  w.fx.lean = -0.08f * up;
  if (u >= 1.f) fxRest(w);
}
// The pan off the fire to the ground in front of the pit.
void actPanDown(World& w, float u) {
  Item& p = w.items[ITEM_PAN];
  const Vec2 top = panTop(w), dst = panRest(w);
  w.fx.reach_to = 1.25f;
  w.fx.reach = envelope(u, 0.2f, 0.25f);
  const float k = easeInOut(window(u, 0.25f, 0.8f));
  p.on_fire = true;
  p.pos = Vec2{lerp(top.x, dst.x, k), lerp(top.y, dst.y, k) - 8.f * sinT(0.5f * k)};
  if (u >= 1.f) {
    p.on_fire = false;
    p.down = true;
    p.pos = dst;
    fxRest(w);
  }
}

bool birdGone(const World& w) { return w.spine.xstage != XS_BIRD; }

// The bird's machine (breakfast c): in from the upper right to the tent's ridge, a long look; when the
// cake goes it dives, takes it at the top of its arc and carries it off up and to the left. Released
// out of sight; the cake goes with it.
void birdTick(World& w) {
  SpineState& sp = w.spine;
  const int s = sp.xcrit[0];
  if (!live(w, s) || w.crit[s].tag != CT_DINNER) {
    sp.xcrit[0] = -1;
    sp.xstage = XS_NONE;
    w.items[ITEM_CAKE].down = false;
    if (!(sp.xbits & XB_ON_HEAD)) w.items[ITEM_CAKE].held = false;
    w.busy = false;
    return;
  }
  Critter& c = w.critters[s];
  sp.xt += SS_TICK_DT;
  const float t = sp.xt;
  Item& cake = w.items[ITEM_CAKE];
  if (!(sp.xbits & XB_BIRD_OK)) {   // in
    const Vec2 a{(float)SS_WIDTH + 24.f, 150.f}, b = ridgeTop(w);
    const float u = clamp01(t / 2.6f);
    flyAlong(c, a, Vec2{lerp(a.x, b.x, 0.55f), b.y + 22.f}, b, easeOutCubic(u));
    if (u >= 1.f) {
      c.facing = 1;
      critterSetAction(c, CA_LOOK, 0.25f);
      sp.xbits = (uint8_t)(sp.xbits | XB_BIRD_OK);
      sp.v0 = ridgeTop(w);
      sp.xt2 = 0.f;
    }
    return;
  }
  if (!(sp.xbits & XB_LAUNCH)) return;   // perched, watching the pan
  sp.xt2 += SS_TICK_DT;
  const float t2 = sp.xt2;
  const Vec2 pt{panTop(w).x + 7.f, panTop(w).y - 8.9f - 4.f * 46.f * 0.25f};   // the cake at the top of its arc
  if (t2 < 0.78f) {   // the dive: ridge to the arc's top
    w.crit[s].sort_y = 0;
    critterSetAction(c, CA_MOVE, 0.1f);
    const Vec2 a = sp.v0;
    flyAlong(c, a, Vec2{pt.x, a.y + 10.f}, pt, easeInOut(clamp01(t2 / 0.78f)));
    return;
  }
  if (!(sp.xbits & XB_SNATCHED)) sp.xbits = (uint8_t)(sp.xbits | XB_SNATCHED);
  const float u = clamp01((t2 - 0.78f) / 2.6f);   // away, up and to the left, the cake in its feet
  const Vec2 b{-40.f, 90.f};
  flyAlong(c, pt, Vec2{pt.x - 60.f, pt.y - 60.f}, b, easeInOut(u));
  cake.down = true;
  cake.held = false;
  cake.pos = Vec2{c.feet.x, c.feet.y + 3.f};
  cake.fill = 0.55f;
  cake.angle = 0.f;
  cake.ground_y = c.feet.y;
  if (u >= 1.f) {
    cake.down = false;
    directorRelease(w, s);
    sp.xcrit[0] = -1;
    sp.xstage = XS_NONE;
    w.busy = false;
  }
}

// ---- wash ------------------------------------------------------------------------------------

// A splash: a few drops thrown up from `from`, falling into the stream.
void splash(World& w, Vec2 from, float dir, int n, float up) {
  float cy, half;
  const Vec2 h = w.hiker.feet;
  const bool in = landscapeStreamSpan(w.land, h.x, cy, half);
  const float water = in ? minf(h.y + 5.f, cy + 0.5f * half) : h.y + 5.f;
  for (int i = 0; i < n; ++i) {
    const int b = worldTakeBit(w, BIT_DROP);
    if (b < 0) break;
    Bit& d = w.bits[b];
    d.pos = Vec2{from.x + dir * 2.f * (float)i, from.y};
    d.vel = Vec2{dir * (10.f + 12.f * (float)i), -up + 14.f * (float)i};
    d.ground_y = water;
    d.land_y = water;
  }
}
// Two scoops of water: the hand down into the stream, up to his face, drops flying.
constexpr float kScoopS = 6.0f;
void actSplashFace(World& w, float u) {
  const float s = u * kScoopS;
  if (u <= 0.f) w.spine.target = 0;
  const float env = envelope(u, 0.04f, 0.06f);
  float dip = 0.f, face = 0.f;
  for (int k = 0; k < 2; ++k) {
    dip = maxf(dip, plateau(s, 0.2f + 3.0f * (float)k, 0.5f, 0.2f));
    face = maxf(face, plateau(s, 1.2f + 3.0f * (float)k, 0.45f, 0.5f));
  }
  w.fx.reach_to = 0.2f;
  w.fx.reach = dip * env;
  w.fx.lean = 0.45f * maxf(dip, 0.6f * face) * env;
  setArm(w, 0, face * env, 2.0f, 2.3f);   // cupped hands at his face
  setArm(w, 1, face * env, 1.8f, 2.4f);
  w.fx.head = 0.25f * face * env;
  // The drops go up as the hands leave the water, and again as they hit his face.
  const int n = (int)floorf(s / 1.5f);
  if (n >= 0 && n < 4 && w.spine.target < (uint8_t)(n + 1) && fmodf(s, 1.5f) > 0.45f) {
    w.spine.target = (uint8_t)(n + 1);
    const Vec2 hd = hikerHandPos(w.hiker, 0);
    splash(w, hd, (float)w.hiker.facing, 3, 60.f);
  }
  if (u >= 1.f) fxRest(w);
}
// Cold: arms tight, the head going, teeth chattering. (The pose is the hug; the head shakes small and fast.)
constexpr float kShudderS = 3.2f;
void actShudder(World& w, float u) {
  const float s = u * kShudderS;
  const float k = envelope(u, 0.1f, 0.15f);
  w.fx.head = k * (0.05f * sinT(s * 13.f) + 0.03f * sinT(s * 21.f));
  w.fx.lean = k * 0.02f * sinT(s * 9.f);
  w.fx.lift = k * 0.004f * (0.5f + 0.5f * sinT(s * 17.f));
  if (u >= 1.f) fxRest(w);
}
void doBrushUp(World& w, float) {
  w.items[ITEM_BRUSH] = Item{hikerHandPos(w.hiker, 0), false, true, (int8_t)w.hiker.facing, 1.f, w.hiker.feet.y, false, 0, 0.f, 0.f, 1.f, false};
}
void doBrushAway(World& w, float) { w.items[ITEM_BRUSH].held = false; }
// Brushing his teeth: the brush at his mouth, the arm scrubbing, a pause to look at the stream.
constexpr float kBrushS = 5.4f;
void actBrush(World& w, float u) {
  const float s = u * kBrushS;
  const float env = envelope(u, 0.06f, 0.1f);
  const float scrub = sinT(s * 5.5f);
  float breaks = plateau(s, 2.4f, 0.2f, 0.5f);   // a pause: he looks out
  setArm(w, 0, env, 0.95f + 0.07f * scrub * (1.f - breaks), 2.4f + 0.12f * scrub * (1.f - breaks));
  w.fx.head = env * (0.06f - 0.06f * breaks);
  w.fx.lean = 0.02f * env;
  if (u >= 1.f) fxRest(w);
}
// The spit: leans forward, head down, a small arc of drops into the stream, a ring.
void actSpit(World& w, float u) {
  const float k = envelope(u, 0.25f, 0.3f);
  w.fx.lean = 0.2f * k;
  w.fx.head = 0.3f * k;
  if (u >= 0.5f && w.spine.target < 200) {
    w.spine.target = 200;
    float r = 0.f;
    const Vec2 hd = hikerHeadPos(w.hiker, &r);
    splash(w, Vec2{hd.x + (float)w.hiker.facing * (r + 2.f), hd.y + 3.f}, (float)w.hiker.facing, 3, 20.f);
  }
  if (u >= 1.f) fxRest(w);
}

// ---- tidy ------------------------------------------------------------------------------------

void actTentReach(World& w, float u) {   // an arm into the tent after the bag: the tent shivers
  const float k = envelope(u, 0.15f, 0.2f);
  w.fx.reach_to = 1.0f;
  w.fx.reach = k * (0.8f + 0.1f * sinT(u * 4.f));
  w.fx.lean = 0.2f * k;
  w.tent.wobble = u >= 1.f ? 0.f : floorf(0.5f + 1.5f * k * (0.5f + 0.5f * sinT(u * 3.f)));
  if (u >= 1.f) fxRest(w);
}
void doBagArms(World& w, float) { hikerCarry(w.hiker, CARRY_BAG); }
void doSackOut(World& w, float) {
  hikerCarry(w.hiker, CARRY_NONE);
  const Vec2 at = sackAt(w);
  Item& s = w.items[ITEM_SACK];
  s = Item{at, true, false, 1, 1.f, at.y, false, 0, 0.f, 0.f, 1.f, false};
}
void doSackTake(World& w, float) {   // tidy b: it goes into the tent as it is
  w.items[ITEM_SACK].down = false;
  hikerCarry(w.hiker, CARRY_BAG);
}
// Pushes the bag in, a hand at a time: it fills and bulges at the mouth.
// `from` and `to` are the sack's fill and mouth-bulge at the start and end of the push.
void pushIn(World& w, float u, float fill0, float fill1, float puff0, float puff1) {
  Item& s = w.items[ITEM_SACK];
  const float k = easeInOut(u);
  const float p3 = u * 3.f;
  const float p = sinT(0.5f * (p3 - floorf(p3)));   // the hand goes in and out three times
  s.fill = lerp(fill0, fill1, k);
  s.puff = maxf(0.f, lerp(puff0, puff1, k) + 0.07f * p);
  const float env = envelope(u, 0.08f, 0.12f);
  w.fx.reach_to = 0.45f;
  w.fx.reach = env * (0.5f + 0.5f * p);
  w.fx.lean = env * (0.15f + 0.12f * p);
}
void actStuff1(World& w, float u) {
  pushIn(w, u, 0.f, 0.9f, 0.1f, 0.35f);
  if (u >= 1.f) fxRest(w);
}
void actStuff2(World& w, float u) {
  pushIn(w, u, 0.9f, 0.9f, 1.f, 0.3f);
  if (u >= 1.f) fxRest(w);
}
// Sits on it: it squashes down, the bag peeking out less; he bounces.
void sitOn(World& w, float u, float puff0, float puff1) {
  Item& s = w.items[ITEM_SACK];
  const float k = easeInOut(window(u, 0.15f, 0.7f));
  const float b = sinT(0.5f * window(u, 0.1f, 0.35f)) + 0.7f * sinT(0.5f * window(u, 0.45f, 0.7f));
  s.squash = k;
  s.puff = lerp(puff0, puff1, k);
  w.fx.lift = 0.024f * b;
}
void actSit1(World& w, float u) {
  sitOn(w, u, 0.35f, 0.05f);
  if (u >= 1.f) fxRest(w);
}
void actSit2(World& w, float u) {
  sitOn(w, u, 0.3f, 0.f);
  if (u >= 1.f) fxRest(w);
}
// Pulls the drawstring shut and knots it.
void actTie(World& w, float u) {
  Item& s = w.items[ITEM_SACK];
  const float k = envelope(u, 0.15f, 0.2f);
  w.fx.reach_to = 0.4f;
  w.fx.reach = k * (0.8f + 0.12f * sinT(u * 5.f));
  w.fx.lean = 0.15f * k;
  s.puff = 0.f;
  s.alt = u >= 0.6f;
  s.squash = 1.f - 0.6f * easeInOut(window(u, 0.f, 0.8f));
  if (u >= 1.f) fxRest(w);
}
// Gets up, and it springs open: the knot flies, the bag erupts, he jumps back with his arms up.
void actSpring(World& w, float u) {
  Item& s = w.items[ITEM_SACK];
  Hiker& h = w.hiker;
  if (u <= 0.f) w.spine.u0 = h.feet.x;
  const float pop = easeOutBack(window(u, 0.1f, 0.5f));
  s.alt = false;
  s.puff = clamp01(pop);
  s.squash = 0.f;
  const float k = easeOutCubic(window(u, 0.12f, 0.3f)) * (1.f - easeInOut(window(u, 0.6f, 1.f)));
  w.fx.lean = -0.2f * k;
  w.fx.head = -0.25f * k;
  w.fx.lift = 0.03f * k;
  setArm(w, 0, 0.6f * k, 2.5f, 0.5f);
  setArm(w, 1, 0.6f * k, 2.1f, 0.7f);
  h.feet.x = w.spine.u0 - (float)h.facing * 8.f * easeOutCubic(window(u, 0.1f, 0.5f));
  if (u >= 1.f) fxRest(w);
}
// It holds: he stands, hands on hips, a nod.
void actHolds(World& w, float u) {
  w.fx.head = 0.26f * (0.5f - 0.5f * cosT(2.f * u));
  if (u >= 1.f) fxRest(w);
}

// ---- day hike ----------------------------------------------------------------------------------

Glyph& mapGlyph(World& w) { return w.major.g[0]; }
void doMapOut(World& w, float) {
  Glyph& g = mapGlyph(w);
  g = Glyph{};
  g.kind = GL_SHAPE;
  g.shape = SH_MAP;
  g.slot = CLOUD_LIT;
  g.facing = (int8_t)w.hiker.facing;
  g.k = 0.f;
  g.scale = 1.15f;
  g.pos = hikerHandsMid(w.hiker);
  g.base_y = (int16_t)(w.hiker.feet.y + 2.f);
  w.spine.xbits = (uint8_t)(w.spine.xbits | XB_MAP);
}
void holdArms(World& w, float k, float lift) {
  setArm(w, 0, k, 1.25f + lift, 0.8f);
  setArm(w, 1, k, 1.12f + lift, 0.95f);
}
void actMapOpen(World& w, float u) {
  holdArms(w, easeInOut(window(u, 0.f, 0.45f)), 0.f);
  w.fx.head = 0.25f * easeInOut(window(u, 0.f, 0.5f));
  mapGlyph(w).k = easeInOut(window(u, 0.35f, 1.f));
}
void actMapStudy(World& w, float u) {   // a look at the map, a look at the ridge, the map again
  const float ridge = plateau(u * 3.f, 0.9f, 0.3f, 0.9f);
  holdArms(w, 1.f, 0.04f * sinT(u * 1.3f) - 0.3f * ridge);
  w.fx.head = lerp(0.25f + 0.05f * sinT(u * 1.7f), -0.3f, ridge);
  w.fx.lean = -0.04f * ridge;
}
void actMapFold(World& w, float u) {
  const float k = 1.f - easeInOut(window(u, 0.7f, 1.f));
  holdArms(w, k, -0.1f * u);
  w.fx.head = 0.25f * k;
  Glyph& g = mapGlyph(w);
  g.k = 1.f - 0.25f * floorf(minf(4.f, u * 5.f));
  if (u >= 0.85f) {
    g.kind = GL_NONE;
    w.spine.xbits = (uint8_t)(w.spine.xbits & ~XB_MAP);
  }
  if (u >= 1.f) fxRest(w);
}
bool creepOver(const World& w) { return (w.spine.xbits & XB_RAID_OVER) != 0; }
void doCreepStart(World& w, float) {
  SpineState& sp = w.spine;
  sp.xstage = XS_CREEP;
  sp.xt = 0.f;
  sp.xt2 = 0.f;
  sp.xstep = 0;
  sp.xbits = (uint8_t)(sp.xbits & ~XB_RAID_OVER);
  w.busy = true;
}
// The empty camp: a chipmunk runs in from the left to the pot and sits up, sniffing; a bird lands on
// the tent's ridge. Then they go (before the fade).
void creepTick(World& w) {
  SpineState& sp = w.spine;
  sp.xt += SS_TICK_DT;
  const float t = sp.xt;
  const Vec2 pot = w.spine.camp.pot_down;
  // The chipmunk (slot 0).
  if (sp.xcrit[0] == -1 && t >= 0.6f) {
    const int s = spawn(w, CR_CHIPMUNK, Vec2{-16.f, pot.y - 2.f}, 1, CT_DINNER);
    sp.xcrit[0] = (int8_t)(s < 0 ? -2 : s);
    if (s >= 0) directorWalk(w, s, Vec2{pot.x - 20.f, pot.y - 1.f}, 90.f, CA_RUN);
  }
  if (live(w, sp.xcrit[0]) && w.crit[sp.xcrit[0]].mode == CM_FREE) {
    Critter& c = w.critters[sp.xcrit[0]];
    if (sp.xstep == 0 && critterArrived(c)) {
      c.facing = 1;
      critterSetAction(c, CA_SIT, 0.3f);
      sp.xstep = 1;
      sp.xt2 = 0.f;
    } else if (sp.xstep >= 1) {
      sp.xt2 += SS_TICK_DT;
      if (sp.xstep == 1 && sp.xt2 > 1.0f) {
        critterSetAction(c, CA_SNIFF, 0.3f);
        sp.xstep = 2;
      }
      if (sp.xstep == 2 && sp.xt2 > 3.0f) {
        directorExit(w, sp.xcrit[0], -1, 90.f, CA_RUN);
        sp.xstep = 3;
      }
    }
  }
  // The bird (slot 1): in from the upper right to the ridge, a look, then up and away to the left.
  if (sp.xcrit[1] == -1 && t >= 1.6f) {
    const int s = spawn(w, CR_BIRD, Vec2{(float)SS_WIDTH + 24.f, 150.f}, -1, CT_THIEF);
    sp.xcrit[1] = (int8_t)(s < 0 ? -2 : s);
    if (s >= 0) {
      w.crit[s].sort_y = (int16_t)(w.tent.pos.y + 1.f);
      critterSetAction(w.critters[s], CA_MOVE, 0.1f);
    }
  }
  if (live(w, sp.xcrit[1]) && w.crit[sp.xcrit[1]].tag == CT_THIEF) {
    Critter& c = w.critters[sp.xcrit[1]];
    const float u = t - 1.6f;
    const Vec2 a{(float)SS_WIDTH + 24.f, 150.f}, b = ridgeTop(w);
    if (u < 2.6f) {
      flyAlong(c, a, Vec2{lerp(a.x, b.x, 0.55f), b.y + 22.f}, b, easeOutCubic(clamp01(u / 2.6f)));
      if (u >= 2.55f && c.action == CA_MOVE) {
        c.facing = -1;
        critterSetAction(c, CA_LOOK, 0.25f);
      }
    } else if (u > 4.2f) {
      critterSetAction(c, CA_MOVE, 0.12f);
      const float o = clamp01((u - 4.2f) / 2.2f);
      flyAlong(c, b, Vec2{lerp(b.x, -30.f, 0.4f), b.y - 50.f}, Vec2{-30.f, 120.f}, easeInOut(o));
      w.crit[sp.xcrit[1]].sort_y = 0;
      if (o >= 1.f) {
        directorRelease(w, sp.xcrit[1]);
        sp.xcrit[1] = -3;
      }
    }
  }
  // Over when both have come and gone (or could not come), or by the safety time.
  const bool a_done = sp.xcrit[0] == -2 || (sp.xcrit[0] >= 0 && !live(w, sp.xcrit[0]));
  const bool b_done = sp.xcrit[1] == -2 || sp.xcrit[1] == -3 || (sp.xcrit[1] >= 0 && !live(w, sp.xcrit[1]));
  if ((a_done && b_done) || t > 16.f) sp.xbits = (uint8_t)(sp.xbits | XB_RAID_OVER);
}

// ---- what his hands hold, every tick ----------------------------------------------------------

void holdTick(World& w) {
  SpineState& sp = w.spine;
  const Hiker& h = w.hiker;
  if (sp.xbits & XB_PAN_HELD) {   // by the handle, the pan out in front
    Item& p = w.items[ITEM_PAN];
    const Vec2 m = hikerHandsMid(h);
    p.pos = Vec2{m.x - (float)h.facing * 26.f, m.y + 4.f};
    p.facing = (int8_t)h.facing;
  }
  if (sp.xbits & XB_ON_HEAD) w.items[ITEM_CAKE].pos = headTarget(w);
  if (sp.xbits & XB_IN_HAND) {
    const Vec2 p = hikerHandPos(h, 0);
    w.items[ITEM_CAKE].pos = Vec2{p.x, p.y};
  }
  if (sp.xbits & XB_MAP) {
    Glyph& g = mapGlyph(w);
    if (g.kind == GL_SHAPE) {
      const Vec2 m = hikerHandsMid(h);
      g.pos = Vec2{m.x, m.y - 3.f};
      g.facing = (int8_t)h.facing;
      g.base_y = (int16_t)(h.feet.y + 2.f);
    }
  }
}

// ---- scripts -----------------------------------------------------------------------------------

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
#define FROM_REST pose(POSE_STAND, 0.8f, 0.3f)
#define TO_SIT go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f)
#define PACK_CROUCH go(AT_PACK_WORK, kStroll), face(LEFT), pose(POSE_CROUCH, 0.6f, 0.6f)
#define FIRE_SIDE go(AT_BLOW, kStroll, 21.f, 0.f), face(LEFT)

// Pee: the glance, off behind the pine (fp[0]); out of sight for the plan's away_s while the stage
// machine plays the rustle, the squirrel and the raccoon; back to the shoes (fp[1]); looks down.
const Step kPee[] = {
    FROM_REST,
    still(0.4f),
    act(actGlance, kGlanceS),
    face(LEFT),
    go(AT_FAM0, kStroll),
    pose(POSE_STAND, 0.5f, 0.1f),
    doit(doPeeStart),
    waitAway(),
    until(raidOver),
    face(RIGHT),
    still(0.4f),
    go(AT_FAM1, kStroll),
    face(RIGHT),
    pose(POSE_STAND, 0.5f, 0.2f),
    act(actLookDown, 3.2f),
    pose(POSE_SCRATCH_HEAD, 0.7f, 1.4f),
    pose(POSE_STAND, 0.5f, 0.2f),
    act(actShrug, 1.6f),
    TO_SIT,
};

// Breakfast, shared start: the pot off the fire if it is still on it; the pan out of the pack, onto the
// fire; the batter; it cooks.
#define BREAKFAST_START                                                                                 \
  FROM_REST, FIRE_SIDE, skipUnless(potOnFire, 2), pose(POSE_CROUCH, 0.6f, 0.1f),                    \
      act(morningActPotOff, 1.2f), pose(POSE_STAND, 0.6f, 0.1f), PACK_CROUCH, act(morningActRummage, 1.4f), \
      doit(doPanOut), pose(POSE_CARRY, 0.6f, 0.1f), FIRE_SIDE, pose(POSE_CROUCH, 0.6f, 0.2f),         \
      act(actPanOnFire, 1.0f), act(actBatter, 2.2f), still(0.6f)
#define BREAKFAST_END pose(POSE_STAND, 0.6f, 0.2f), FIRE_SIDE, pose(POSE_CROUCH, 0.6f, 0.1f), act(actPanDown, 1.2f), pose(POSE_STAND, 0.6f, 0.2f), TO_SIT

// Breakfast a: it flips once and lands back in the pan; a nod; he eats it.
const Step kBreakfastA[] = {
    BREAKFAST_START, act(actSizzle, 3.2f), act(actFlipA, kFlipS[0]), act(actNodA, 1.4f), still(0.4f),
    act(actEatPan, 4.2f), BREAKFAST_END,
};
// Breakfast b: it flips too high and lands on his hat; he freezes; takes it off and eats it anyway.
const Step kBreakfastB[] = {
    BREAKFAST_START, act(actSizzle, 3.2f), act(actFlipB, kFlipS[1]), act(actFrozen, 2.0f), act(actCakeOff, 5.0f),
    BREAKFAST_END,
};
// Breakfast c: a bird has been watching from the tent; it takes the pancake out of the air.
const Step kBreakfastC[] = {
    BREAKFAST_START, doit(doBirdWatch), act(actSizzle, 3.2f), still(0.6f), act(actFlipC, kFlipS[2]),
    act(actWatchCake, 3.4f), act(actShrug, 1.6f), until(birdGone), BREAKFAST_END,
};

// Wash: to the bank, kneels, two scoops of water at his face; up, cold, the shudder; the toothbrush; a
// spit into the stream; back to the fire.
const Step kWash[] = {
    FROM_REST,
    go(AT_COOL, kStroll),
    face(RIGHT),
    pose(POSE_CROUCH, 0.6f, 0.3f),
    act(actSplashFace, kScoopS),
    pose(POSE_STAND, 0.7f, 0.2f),
    pose(POSE_HUG, 0.6f, 0.1f),
    act(actShudder, kShudderS),
    pose(POSE_STAND, 0.6f, 0.2f),
    doit(doBrushUp),
    act(actBrush, kBrushS),
    act(actSpit, 1.8f),
    doit(doBrushAway),
    pose(POSE_STAND, 0.6f, 0.4f),
    TO_SIT,
};

// Tidy, shared: the bag out of the tent, to the sack, in it goes (it bulges), he sits on it and ties it;
// he gets up and it springs open.
#define TIDY_START                                                                                      \
  FROM_REST, go(AT_DOOR, kStroll), face(RIGHT), pose(POSE_CROUCH, 0.6f, 0.2f), act(actTentReach, 2.0f), \
      doit(doBagArms), pose(POSE_CARRY, 0.6f, 0.1f), go(AT_FAM0, kStroll), face(RIGHT),                 \
      pose(POSE_CROUCH, 0.6f, 0.2f), doit(doSackOut), act(actStuff1, 3.0f), pose(POSE_STAND, 0.5f, 0.1f), \
      go(AT_FAM1, 20.f), face(RIGHT), pose(POSE_SIT, 0.7f, 0.1f), act(actSit1, 2.6f), pose(POSE_STAND, 0.7f, 0.1f), \
      pose(POSE_CROUCH, 0.5f, 0.1f), act(actTie, 1.8f), pose(POSE_STAND, 0.6f, 0.1f), act(actSpring, 1.4f),     \
      still(0.8f), pose(POSE_HANDS_ON_HIPS, 0.6f, 1.2f), pose(POSE_STAND, 0.5f, 0.1f)
// The second try: in again, sat on again, tied.
#define TIDY_SECOND                                                                                     \
  go(AT_FAM0, kStroll), face(RIGHT), pose(POSE_CROUCH, 0.6f, 0.2f), act(actStuff2, 2.6f), pose(POSE_STAND, 0.5f, 0.1f), \
      go(AT_FAM1, 20.f), face(RIGHT), pose(POSE_SIT, 0.7f, 0.1f), act(actSit2, 2.6f), pose(POSE_STAND, 0.7f, 0.1f),     \
      pose(POSE_CROUCH, 0.5f, 0.1f), act(actTie, 1.8f), pose(POSE_STAND, 0.6f, 0.1f)

// Tidy a: the second try holds; a nod.
const Step kTidyA[] = {TIDY_START, TIDY_SECOND, still(1.4f), act(actHolds, 1.2f), TO_SIT};
// Tidy b: it springs again; he gives up on the sack and takes the lot into the tent as it is.
const Step kTidyB[] = {
    TIDY_START, TIDY_SECOND, act(actSpring, 1.4f), still(0.6f), pose(POSE_HANDS_ON_HIPS, 0.6f, 1.4f),
    pose(POSE_STAND, 0.5f, 0.1f), pose(POSE_CROUCH, 0.6f, 0.2f), doit(doSackTake), pose(POSE_CARRY, 0.6f, 0.1f),
    go(AT_DOOR, kStroll), face(RIGHT), pose(POSE_CROUCH, 0.6f, 0.2f), act(actTentReach, 2.0f), carry(CARRY_NONE),
    pose(POSE_STAND, 0.6f, 0.2f), TO_SIT,
};

// Day hike: the fire kicked out, the day pack on, a look at the map; off right over the ford; the
// empty camp, the animals creeping back.
#define OFF_RIGHT pose(POSE_STAND, 0.6f, 0.1f), go(AT_FORD1, kStroll), walk(AT_FORD0, kWade), walk(AT_EDGE_R, kStroll)
const Step kDayhike[] = {
    FROM_REST,
    FIRE_SIDE,
    act(morningActKick, 3.0f),
    act(morningActLastPuff, 2.0f),
    pose(POSE_STAND, 0.6f, 0.2f),
    PACK_CROUCH,
    act(morningActPackLift, 0.6f),
    pose(POSE_STAND, 0.6f, 0.1f),
    act(morningActPackOn, 1.2f),
    face(RIGHT),
    pose(POSE_STAND, 0.5f, 0.1f),
    doit(doMapOut),
    act(actMapOpen, 1.6f),
    act(actMapStudy, 3.4f),
    act(actMapFold, 1.4f),
    OFF_RIGHT,
    doit(doCreepStart),
    until(creepOver),
};

#undef FROM_REST
#undef TO_SIT
#undef PACK_CROUCH
#undef FIRE_SIDE
#undef BREAKFAST_START
#undef BREAKFAST_END
#undef TIDY_START
#undef TIDY_SECOND
#undef OFF_RIGHT

struct Script {
  const Step* steps;
  uint8_t n;
};
#define SCRIPT(a) Script{a, N(a)}
#define NO_SCRIPT Script{nullptr, 0}
// Beats SY_PEE .. SY_DAYHIKE (wake and coffee are Mark's: morningSharedScript).
const Script kScripts[SY_COUNT][3] = {
    {NO_SCRIPT, NO_SCRIPT, NO_SCRIPT},
    {NO_SCRIPT, NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kPee), NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kBreakfastA), SCRIPT(kBreakfastB), SCRIPT(kBreakfastC)},
    {SCRIPT(kWash), NO_SCRIPT, NO_SCRIPT},
    {SCRIPT(kTidyA), SCRIPT(kTidyB), NO_SCRIPT},
    {SCRIPT(kDayhike), NO_SCRIPT, NO_SCRIPT},
};
#undef SCRIPT
#undef NO_SCRIPT
#undef N

// Variants each beat draws from: wake 3 and coffee 4 are Mark's (coffee d, the boil-over, only here).
const uint8_t kVariants[SY_COUNT] = {3, 4, 1, 3, 1, 2, 1};
const char* const kBeatNames[SY_COUNT] = {"wake", "coffee", "pee", "breakfast", "wash", "tidy", "day_hike"};

// Beat seconds (whole; the script plus room for the longer walks). UNVERIFIED: tune with SS_SPINE_TRACE.
const uint8_t kBreakfastS[3] = {42, 43, 43};
const uint8_t kTidyS[2] = {50, 60};
constexpr int kPeeBaseS = 38, kWashS = 32, kDayhikeS = 62;
int beatSeconds(const LoopPlan& p, int b) {
  const int v = p.fvar[b];
  switch (b) {
    case SY_WAKE:
    case SY_COFFEE: return morningSharedSeconds(b, v);
    case SY_PEE: return kPeeBaseS + (int)p.away_s;
    case SY_BREAKFAST: return kBreakfastS[v < 3 ? v : 0];
    case SY_WASH: return kWashS;
    case SY_TIDY: return kTidyS[v < 2 ? v : 0];
    default: return kDayhikeS;
  }
}

// ---- beats -------------------------------------------------------------------------------------

// A beat's own points (AT_FAM0, AT_FAM1), set as it starts.
void beatPoints(World& w, int b) {
  SpineState& sp = w.spine;
  sp.target = 0;
  if (b == SY_PEE) {
    sp.fp[0] = hideSpot(w);
    const Vec2 at = shoeAt(w);
    sp.fp[1] = Vec2{at.x - 26.f, at.y + 5.f};   // where the missing shoe lay, a step back
  } else if (b == SY_TIDY) {
    const Vec2 s = sackAt(w);
    sp.fp[0] = Vec2{s.x - 15.f, s.y + 2.f};   // beside it, where he kneels
    sp.fp[1] = Vec2{s.x + 4.f, s.y + 2.f};    // over it, where he sits
  }
}

template <int B>
void beatBegin(SceneCtx& ctx) {
  World& w = *ctx.w;
  beatPoints(w, B);
  const Script& s = kScripts[B][w.plan.fvar[B]];
  scriptStart(w, s.steps, s.n);
}
template <int B>
void sharedBegin(SceneCtx& ctx) { morningSharedBegin(ctx, B); }
void beatUpdate(SceneCtx& ctx, float, float) { scriptRun(*ctx.w); }

// A beat's end: a script still running counts an overrun, and so does a critter of the stay's still in
// the scene (sent off by the nearer edge); the stage machine, the props it set and the rustle stop.
void beatEnd(SceneCtx& ctx) {
  World& w = *ctx.w;
  SpineState& sp = w.spine;
  bool late = !scriptDone(w);
  for (int i = 0; i < 2; ++i) {
    const int s = sp.xcrit[i];
    if (!live(w, s)) continue;
    late = true;
    sendOff(w, s);
  }
  if (late && sp.overruns < 255) ++sp.overruns;
  sp.run.steps = nullptr;
  sp.xstage = XS_NONE;
  sp.xcrit[0] = sp.xcrit[1] = -1;
  sp.xbit[0] = sp.xbit[1] = -1;
  if (sp.xbits & XB_MAP) mapGlyph(w).kind = GL_NONE;
  sp.xbits = 0;
  sp.target = 0;
  w.busy = false;
  w.rustle.t = -1.f;
  w.tent.wobble = 0.f;
  w.steam.t = -1.f;
  w.items[ITEM_PAN].held = false;
}
// The last beat: the script, and the frame fading to black over the last kFadeS seconds; the script
// (him out of the frame, every critter gone) must be done before the fade starts.
void dayhikeUpdate(SceneCtx& ctx, float t, float) {
  World& w = *ctx.w;
  scriptRun(w);
  const float d = (float)beatSeconds(w.plan, SY_DAYHIKE);
  const float fade = easeInOut(window(t, 1.f - kFadeS / d, 1.f));
  const bool busy = !scriptDone(w) || directorCount(w) > 0;
  if (w.fade == 0.f && fade > 0.f && busy && w.spine.overruns < 255) ++w.spine.overruns;
  w.fade = fade;
}

typedef void (*BeginFn)(SceneCtx&);
const BeginFn kBegin[SY_COUNT] = {sharedBegin<SY_WAKE>, sharedBegin<SY_COFFEE>, beatBegin<SY_PEE>, beatBegin<SY_BREAKFAST>,
                                  beatBegin<SY_WASH>, beatBegin<SY_TIDY>, beatBegin<SY_DAYHIKE>};

// ---- plan --------------------------------------------------------------------------------------

constexpr int kAimMin = SS_LOOP_MIN_SECONDS + 10;
constexpr uint8_t kPauseMin = 8, kPauseMax = 20;

int busySeconds(const LoopPlan& p) {
  int s = (int)(SS_BUILD_SECONDS + 0.5f);
  for (int b = 0; b < SY_COUNT; ++b) s += beatSeconds(p, b);
  for (int i = 0; i < p.n_fillers; ++i) s += spineFillerSeconds(p, p.filler[i]);
  for (int i = 0; i < p.n_majors; ++i)
    s += majorSeconds(p.major[i], p.major_var[i]) + p.major_pre_s[i] + p.major_post_s[i];
  return s;
}

// Fillers after coffee, the pee, breakfast and the wash (in play order).
void fillerSlots(LoopPlan& p) {
  static const uint8_t kSlots[SPINE_FILLER_MAX + 1][SPINE_FILLER_MAX] = {
      {0, 0, 0, 0},
      {SY_COFFEE, 0, 0, 0},
      {SY_COFFEE, SY_WASH, 0, 0},
      {SY_COFFEE, SY_PEE, SY_WASH, 0},
      {SY_COFFEE, SY_PEE, SY_BREAKFAST, SY_WASH}};
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler_after[i] = i < p.n_fillers ? kSlots[p.n_fillers][i] : 0;
}

const FitSpec kStayFit = {busySeconds, SY_COUNT - 1, fillerSlots};

// Ticks from the loop start to the start of the timeline entry after beat `last`'s own pause (the fog
// lifts to there).
uint32_t ticksThroughPause(const LoopPlan& p, int last) {
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  int s = 0;
  for (int b = 0; b <= last; ++b) {
    s += beatSeconds(p, b) + p.pause_s[b];
    if (b == last) break;
    for (int i = 0; i < p.n_fillers; ++i)
      if (p.filler_after[i] == b) s += spineFillerSeconds(p, p.filler[i]) + p.filler_pause_s[i];
    for (int i = 0; i < p.n_majors; ++i)
      if (p.major_after[i] == b) s += p.major_pre_s[i] + majorSeconds(p.major[i], p.major_var[i]) + p.major_post_s[i];
  }
  return build + (uint32_t)s * SS_TICK_HZ;
}

}  // namespace

// ---- public ------------------------------------------------------------------------------------

const char* stayBeatName(int beat) { return beat >= 0 && beat < SY_COUNT ? kBeatNames[beat] : "?"; }
int stayVariantCount(int beat) { return beat >= 0 && beat < SY_COUNT ? kVariants[beat] : 0; }

void stayPlan(LoopPlan& p, Rng& rng, const Inputs& in, Memory& mem) {
  // One rng call per statement. The stay's variants: one the last stay morning played is a quarter as likely.
  const bool again = mem.last_stay && mem.loop_count > 1;
  p.stay = 1;
  p.n_fbeats = SY_COUNT;
  for (int b = 0; b < 8; ++b) p.fvar[b] = 0;
  for (int b = 0; b < SY_COUNT; ++b) {
    const int n = kVariants[b];
    float wt[4] = {1.f, 1.f, 1.f, 1.f};
    for (int k = n; k < 4; ++k) wt[k] = 0.f;
    if (again && mem.last_sfvar[b] < n && n > 1) wt[mem.last_sfvar[b]] = 0.25f;
    const int v = rng.weighted(wt, 4);
    p.fvar[b] = (uint8_t)v;
  }
  for (uint8_t& s : p.pause_s) s = 0;
  for (int b = 0; b < SY_COUNT; ++b) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    p.pause_s[b] = b == SY_DAYHIKE ? 0 : s;   // the loop ends with the day hike
  }
  p.away_s = (uint8_t)rng.rangeInt(12, 18);   // behind the tree: Mike's 12 to 18 s

  // Fillers: 2 to 4 of the morning's, weighted, no repeats (the length policy may drop some).
  float wt[FILL_COUNT];
  for (int f = 0; f < FILL_COUNT; ++f) wt[f] = spineFillerAllowed(SF_MORNING, f) ? (f == FILL_CAIRN ? 1.2f : 1.f) : 0.f;
  p.n_fillers = (uint8_t)rng.rangeInt(2, SPINE_FILLER_MAX);
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler[i] = p.filler_after[i] = p.filler_pause_s[i] = 0;
  for (int i = 0; i < p.n_fillers && i < SPINE_FILLER_MAX; ++i) {
    const int f = rng.weighted(wt, FILL_COUNT);
    p.filler[i] = (uint8_t)f;
    wt[f] = 0.f;
  }
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) {
    const uint8_t s = (uint8_t)rng.rangeInt(kPauseMin, kPauseMax);
    if (i < p.n_fillers) p.filler_pause_s[i] = s;
  }
  p.cairn_start = mem.cairn_n > 0 ? (uint8_t)(mem.cairn_n - 1) : 0;
  const int add = rng.rangeInt(4, 6);
  p.cairn_add = (uint8_t)(add < CAIRN_MAX - p.cairn_start ? add : CAIRN_MAX - p.cairn_start);
  p.patch = false;
  p.skip_n = 3;
  p.steam_n = (uint8_t)rng.rangeInt(3, 5);
  p.fog_steps = (uint8_t)rng.rangeInt(11, 16);   // 0.55 .. 0.8

  // Majors after the wake, the coffee and breakfast (slot 1 stays the coffee: rain_shelter's).
  static const MajorSlots kStaySlots = {SF_MORNING, {SY_WAKE, SY_COFFEE, SY_BREAKFAST, 0}, 1 | 2 | 4};
  majorsPlan(p, rng, in, mem, kStaySlots);
  spineFitLength(p, mem, kStayFit);
  // Still short (few, short majors and fillers): a filler back in, if one is free.
  while (busySeconds(p) + spinePauseSeconds(p, SY_COUNT - 1) < kAimMin && p.n_fillers < SPINE_FILLER_MAX) {
    int f = -1;
    for (int k = 0; k < FILL_COUNT && f < 0; ++k) {
      if (!spineFillerAllowed(SF_MORNING, k)) continue;
      bool used = false;
      for (int i = 0; i < p.n_fillers; ++i) used = used || p.filler[i] == k;
      if (!used) f = k;
    }
    if (f < 0) break;
    p.filler[p.n_fillers] = (uint8_t)f;
    p.filler_pause_s[p.n_fillers] = kPauseMin;
    ++p.n_fillers;
    fillerSlots(p);
    if (busySeconds(p) + spinePauseSeconds(p, SY_COUNT - 1) > SS_LOOP_MAX_SECONDS - 10) {   // too much: back out
      --p.n_fillers;
      p.filler[p.n_fillers] = p.filler_pause_s[p.n_fillers] = 0;
      fillerSlots(p);
      break;
    }
  }
  p.fog_t0 = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  p.fog_t1 = ticksThroughPause(p, SY_COFFEE);
}

void stayLayout(World& w) {
  const CampLayout& c = w.spine.camp;
  const LoopPlan& p = w.plan;
  // The shoes, a pair, by the tent's front corner (unless the raccoon has them). Mark's wake b already
  // put the shoe (or the mug) there for him to tread on; the other wakes get the pair too.
  const Vec2 at{c.door.x + 30.f, c.door.y + 3.f};
  if (!w.memory.shoe_taken) {
    Item& s = w.items[ITEM_SHOE];
    if (p.fvar[SY_WAKE] != 1) {
      s.down = true;
      s.held = false;
      s.pos = at;
      s.facing = 1;
      s.size = 0.f;   // pops in with the build (campPop)
    }
    s.alt = true;
  }
}

void stayBuildTimeline(Timeline& tl, const LoopPlan& p) {
  tl.add(morningBuildBeat());
  for (int b = 0; b < SY_COUNT; ++b) {
    const bool shared = b == SY_WAKE || b == SY_COFFEE;
    tl.add(Beat{kBeatNames[b], (float)beatSeconds(p, b), kBegin[b], b == SY_DAYHIKE ? dayhikeUpdate : beatUpdate,
                shared ? morningSharedEnd : beatEnd});
    if (b == SY_DAYHIKE) break;   // the loop ends here
    tl.add(Beat{"pause", (float)p.pause_s[b], nullptr, nullptr, nullptr});
    for (int i = 0; i < p.n_fillers; ++i) {
      if (p.filler_after[i] != b) continue;
      tl.add(spineFillerBeat(p, p.filler[i]));
      tl.add(Beat{"pause", (float)p.filler_pause_s[i], nullptr, nullptr, nullptr});
    }
    for (int i = 0; i < p.n_majors; ++i) {
      if (p.major_after[i] != b) continue;
      tl.add(Beat{"still", (float)p.major_pre_s[i], nullptr, nullptr, nullptr});
      tl.add(majorBeat(p, i));
      tl.add(Beat{"still", (float)p.major_post_s[i], nullptr, nullptr, nullptr});
    }
  }
}

uint32_t stayLoopTicks(const LoopPlan& p) {
  const uint32_t build = (uint32_t)ceilf(SS_BUILD_SECONDS * (float)SS_TICK_HZ - 1e-3f);
  const int rest = busySeconds(p) - (int)(SS_BUILD_SECONDS + 0.5f) + spinePauseSeconds(p, SY_COUNT - 1);
  return build + (uint32_t)rest * SS_TICK_HZ;
}

int stayFormatPlan(const LoopPlan& p, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = snprintf(buf, n, "family=morning stay=1");
  auto put = [&](const char* str) {
    if (k < 0 || (size_t)k >= n) return;
    const int m = snprintf(buf + k, n - (size_t)k, "%s", str);
    if (m > 0) k += m;
  };
  char tmp[28];
  for (int b = 0; b < SY_COUNT; ++b) {
    snprintf(tmp, sizeof tmp, " %s=%d", kBeatNames[b], (int)p.fvar[b]);
    put(tmp);
  }
  put(" fillers=");
  if (p.n_fillers == 0) put("none");
  for (int i = 0; i < p.n_fillers && i < SPINE_FILLER_MAX; ++i) {
    if (i) put(",");
    put(spineFillerName(p.filler[i]));
  }
  put(" majors=");
  if (p.n_majors == 0) put("none");
  for (int i = 0; i < p.n_majors && i < SPINE_MAJOR_MAX; ++i) {
    if (i) put(",");
    put(majorName(p.major[i]));
  }
  snprintf(tmp, sizeof tmp, " fog=%d", (int)p.fog_steps);
  put(tmp);
  return k;
}

void stayTick(World& w) {
  switch (w.spine.xstage) {
    case XS_PEE: peeTick(w); break;
    case XS_BIRD: birdTick(w); break;
    case XS_CREEP: creepTick(w); break;
    default: break;
  }
  holdTick(w);
}

}  // namespace ss
