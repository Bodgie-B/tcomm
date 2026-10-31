// The critter director. See director.h.
#include "director.h"

#include <math.h>

#include "../anim/ease.h"
#include "landscape.h"
#include "world.h"

namespace ss {

namespace {

// Half the figure's length (feet to either end, both facings, with margin)
// and its tallest height (the bear reared and reaching), px at scale 1.
struct KindReach {
  float half;
  float tall;
  float flee;            // px/s when scattered
  CritterAction flee_gait;
};
const KindReach kReach[CR_COUNT] = {
    {16.f, 24.f, 110.f, CA_RUN},   // chipmunk
    {18.f, 32.f, 100.f, CA_RUN},   // squirrel
    {14.f, 30.f, 60.f, CA_HOP},    // frog (hops reach about 20 px up)
    {32.f, 56.f, 140.f, CA_RUN},   // deer
    {28.f, 26.f, 80.f, CA_RUN},    // raccoon
    {12.f, 30.f, 0.f, CA_IDLE},    // owl: does not move
    {12.f, 30.f, 120.f, CA_MOVE},  // bird: flies
    {48.f, 114.f, 120.f, CA_RUN},  // bear
    {10.f, 12.f, 90.f, CA_RUN},    // mouse
};

constexpr float kStartleS = 0.3f;
constexpr float kHikerNear = 80.f;   // px between bounds, horizontally
constexpr float kHikerNearY = 40.f;

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

inline bool valid(int slot) { return slot >= 0 && slot < CRITTER_MAX; }

// The quiet corner band: feet at or below this y would put the figure's
// bounds (feet + 2) into the reserved region.
constexpr float kCornerFeetY = (float)LS_QUIET_Y0 - 3.f;

void startScatter(World& w, int i, float from_x) {
  CritterSlot& s = w.crit[i];
  Critter& c = w.critters[i];
  s.mode = CM_STARTLE;
  s.t = 0.f;
  s.path_on = false;
  const int away = c.feet.x < from_x ? -1 : 1;
  const Vec2 to = directorClamp(w, i, Vec2{away < 0 ? -80.f : (float)SS_WIDTH + 80.f, c.feet.y}, true);
  s.flee_x = to.x;
  critterSetAction(c, CA_STARTLE, kStartleS);
}

}  // namespace

float directorDryY(const World& w, Vec2 from, float to_x) {
  float y = from.y;
  float cy, half;
  if (landscapeStreamSpan(w.land, from.x, cy, half) && from.y > cy - 1.06f * half - 3.f) return from.y;
  for (int k = 1; k <= 16; ++k) {
    const float u = (float)k / 16.f;
    const float x = lerp(from.x, to_x, u);
    if (x < 0.f || x > (float)SS_WIDTH || !landscapeStreamSpan(w.land, x, cy, half)) continue;
    const float top = cy - 1.06f * half - 3.f;
    y = minf(y, from.y + (top - from.y) / u);
  }
  return maxf(minf(320.f, from.y), y);
}

int directorSpawn(World& w, CritterKind k, Vec2 feet, int facing) {
  for (int i = 0; i < CRITTER_MAX; ++i) {
    CritterSlot& s = w.crit[i];
    if (s.in_use) continue;
    Critter& c = w.critters[i];
    critterInit(c, k, feet, facing);
    c.speed_dial = s.speed_dial;
    c.jitter_dial = s.jitter_dial;
    c.visible = false;   // directorStep decides from its bounds
    s.in_use = true;
    s.tag = CT_NONE;
    s.mode = CM_FREE;
    s.flags = 0;
    s.t = 0.f;
    s.flee_x = feet.x;
    s.path_on = false;
    s.path_x0 = s.path_x1 = feet.x;
    s.path_y0 = s.path_y1 = feet.y;
    w.critter_rect[i] = Rect{0, 0, 0, 0};
    return i;
  }
  return -1;
}

void directorRelease(World& w, int slot) {
  if (!valid(slot)) return;
  CritterSlot& s = w.crit[slot];
  s.in_use = false;
  s.tag = CT_NONE;
  s.mode = CM_FREE;
  s.flags = 0;
  s.path_on = false;
  w.critters[slot].visible = false;
  w.critter_rect[slot] = Rect{0, 0, 0, 0};
  // Anything it held is dropped where it is.
  for (Item& it : w.items) {
    if (it.critter != slot + 1) continue;
    it.critter = 0;
    it.held = false;
    it.down = true;
    it.pos.y = w.critters[slot].feet.y + 1.f;
  }
}

void directorScatter(World& w, Vec2 from, float radius) {
  for (int i = 0; i < CRITTER_MAX; ++i) {
    const CritterSlot& s = w.crit[i];
    if (!s.in_use || s.mode != CM_FREE || (s.flags & CF_BRAVE)) continue;
    const Vec2 f = w.critters[i].feet;
    const float dx = f.x - from.x, dy = f.y - from.y;
    if (dx * dx + dy * dy <= radius * radius) startScatter(w, i, from.x);
  }
}

void directorAvoidHiker(World& w) {
  const Hiker& h = w.hiker;
  if (!h.visible) return;
  const bool walking = h.walking;
  const bool stirring = walking || (h.blending && h.target != POSE_SIT && h.target != POSE_LIE &&
                                    h.target != POSE_READ && h.target != POSE_HOLD_MUG);
  if (!stirring) return;
  const Rect& hr = w.hiker_rect;
  for (int i = 0; i < CRITTER_MAX; ++i) {
    const CritterSlot& s = w.crit[i];
    if (!s.in_use || s.mode != CM_FREE || (s.flags & CF_BRAVE)) continue;
    bool go = (s.flags & CF_SHY) != 0;
    if (!go && walking) {
      const Rect r = critterBounds(w.critters[i]);
      const float gx = maxf(0.f, (float)maxf((float)(r.x0 - hr.x1), (float)(hr.x0 - r.x1)));
      const float gy = maxf(0.f, (float)maxf((float)(r.y0 - hr.y1), (float)(hr.y0 - r.y1)));
      go = gx < kHikerNear && gy < kHikerNearY;
    }
    if (go) startScatter(w, i, h.feet.x);
  }
}

Vec2 directorClamp(const World& w, int slot, Vec2 to, bool exiting) {
  if (!valid(slot)) return to;
  const Critter& c = w.critters[slot];
  const KindReach& k = kReach[c.kind < CR_COUNT ? c.kind : 0];
  const float half = k.half * c.scale, tall = k.tall * c.scale;
  to.y = minf((float)SS_HEIGHT - 3.f, maxf(tall + 2.f, to.y));
  const float lo = half + 2.f, hi = (float)SS_WIDTH - half - 2.f;
  if (!exiting) to.x = minf(hi, maxf(lo, to.x));
  // Out of the corner: if either end of the move is in the band, the move
  // stays left of it (an exit to the right turns into one to the left).
  if (to.y > kCornerFeetY || c.feet.y > kCornerFeetY) {
    const float edge = (float)LS_QUIET_X0 - half - 2.f;
    if (to.x > edge) to.x = exiting ? -half - 40.f : edge;
  }
  return to;
}

void directorWalk(World& w, int slot, Vec2 to, float px_per_s, CritterAction gait) {
  if (!valid(slot) || !w.crit[slot].in_use) return;
  Critter& c = w.critters[slot];
  CritterSlot& s = w.crit[slot];
  to = directorClamp(w, slot, to, s.mode == CM_FLEE);
  s.path_on = true;
  s.path_x0 = c.feet.x;
  s.path_y0 = c.feet.y;
  s.path_x1 = to.x;
  s.path_y1 = to.y;
  if (to.x == c.feet.x) c.feet.y = to.y;
  if (gait != c.action && critterSupports(c.kind, gait) && (gait == CA_MOVE || gait == CA_RUN || gait == CA_HOP))
    critterSetAction(c, gait, 0.2f);
  critterMoveTo(c, to.x, px_per_s);
}

void directorExit(World& w, int slot, int side, float px_per_s, CritterAction gait) {
  if (!valid(slot) || !w.crit[slot].in_use) return;
  CritterSlot& s = w.crit[slot];
  const Critter& c = w.critters[slot];
  const float half = kReach[c.kind < CR_COUNT ? c.kind : 0].half * c.scale;
  const Vec2 to = directorClamp(w, slot, Vec2{side < 0 ? -half - 40.f : (float)SS_WIDTH + half + 40.f, c.feet.y}, true);
  s.mode = CM_FLEE;
  s.t = 0.f;
  s.flee_x = to.x;
  directorWalk(w, slot, to, px_per_s, gait);
}

bool directorOwns(const World& w, int slot, uint8_t tag) {
  return valid(slot) && w.crit[slot].in_use && w.crit[slot].tag == tag && w.crit[slot].mode == CM_FREE;
}

int directorCount(const World& w) {
  int n = 0;
  for (const CritterSlot& s : w.crit) n += s.in_use ? 1 : 0;
  return n;
}

void directorStep(World& w) {
  directorAvoidHiker(w);
  for (int i = 0; i < CRITTER_MAX; ++i) {
    CritterSlot& s = w.crit[i];
    if (!s.in_use) {
      w.critter_rect[i] = Rect{0, 0, 0, 0};
      continue;
    }
    Critter& c = w.critters[i];
    s.t += SS_TICK_DT;
    if (s.mode == CM_STARTLE && s.t >= kStartleS) {
      const KindReach& k = kReach[c.kind < CR_COUNT ? c.kind : 0];
      if (k.flee > 0.f) {
        s.mode = CM_FLEE;
        s.t = 0.f;
        directorWalk(w, i, Vec2{s.flee_x, directorDryY(w, c.feet, s.flee_x)}, k.flee, k.flee_gait);
      } else {
        s.mode = CM_FREE;   // the owl stays put
      }
    }
    critterTick(c, SS_TICK_DT);
    if (s.path_on && c.moving) {
      const float span = s.path_x1 - s.path_x0;
      const float u = span != 0.f ? clamp01((c.feet.x - s.path_x0) / span) : 1.f;
      c.feet.y = lerp(s.path_y0, s.path_y1, u);
    } else if (s.path_on && !c.moving && fabsf(c.feet.x - s.path_x1) < 0.5f) {
      c.feet.y = s.path_y1;   // arrived: the last step's y too
      s.path_on = false;
    }
    const Rect r = critterBounds(c);
    c.visible = r.x1 > 0 && r.x0 < SS_WIDTH;
    if (s.mode == CM_FLEE && !c.moving && !c.visible) {
      directorRelease(w, i);
      continue;
    }
    w.critter_rect[i] = c.visible ? r : Rect{0, 0, 0, 0};
    // Whatever it holds stays at its paw.
    for (Item& it : w.items)
      if (it.critter == i + 1) {
        it.pos = critterPaw(c);
        it.ground_y = c.feet.y;
      }
  }
}

}  // namespace ss
