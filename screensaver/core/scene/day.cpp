// The daytime majors' shared piece of the World. See day.h.
#include "day.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "day_kite.h"
#include "world.h"

namespace ss {

using namespace sc;

// ---- the shared script pieces ----------------------------------------------------------------------------

bool dayWet(const World& w) {
  const uint8_t k = w.weather.kind;
  return k == WX_RAIN || k == WX_SNOW || k == WX_STORM;
}

namespace {

float env(float u, float a, float b) { return scriptEnvelope(u, a, b); }

// The first drops: a hand out, palm up, a look at the sky.
void actPalm(World& w, float u) {
  const float e = env(u, 0.25f, 0.3f);
  scriptSetArm(w, 0, e, 1.55f, 0.35f);
  w.fx.head = -0.2f * e;
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

const Step kNoShow[] = {
    pose(POSE_STAND, 0.8f, 0.3f),
    still(0.6f), act(actPalm, 3.0f), still(0.8f), act(actShrug, 2.4f), still(0.6f),
    go(AT_SIT, 30.f), face(TO_FIRE), pose(POSE_SIT, 0.9f, 0.6f),
};

inline void markOn(Renderer& r, const Rect& b) {
  if (!b.empty() && b.x1 > 0 && b.x0 < SS_WIDTH) r.markDirtyRect(b);
}
inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}
inline int16_t baseY(float y) { return (int16_t)floorf(y); }

// The stake the slackline's far end is tied to: a TRUNK post from the ground to a little above the knot.
constexpr float kStakeW = 4.f, kStakeTop = 6.f;

Rect slackBounds(const DaySlack& s) {
  Rect r{0, 0, 0, 0};
  if (s.line_on) r = unite(slacklineBounds(s.line), daySlackStakeBounds(s));
  if (s.crit_on) r = unite(r, critterBounds(s.cr));
  return r;
}

size_t sizeOf(uint8_t kind) {
  switch (kind) {
    case DAY_KITE: return sizeof(KiteState);
    case DAY_BIKE: return sizeof(DayBike);
    case DAY_ARCHERY: return sizeof(DayArchery);
    case DAY_SLACK: return sizeof(DaySlack);
    case DAY_ROPES: return sizeof(DayRopes);
    default: return 0;
  }
}

}  // namespace

const Step* dayNoShowScript(uint8_t& n) {
  n = (uint8_t)(sizeof(kNoShow) / sizeof(kNoShow[0]));
  return kNoShow;
}

Rect daySlackStakeBounds(const DaySlack& s) {
  const float x = s.line.a.x;
  return Rect{(int)floorf(x - 0.5f * kStakeW) - 2, (int)floorf(s.line.a.y - kStakeTop) - 2, (int)ceilf(x + 0.5f * kStakeW) + 3,
              (int)ceilf(s.line.ground_y) + 3};
}

Rect dayBounds(const DayState& d) {
  switch (d.kind) {
    case DAY_KITE: return kiteBounds(d.kite);
    case DAY_BIKE: return unite(bikeBounds(d.bike.bike), splashBounds(d.bike.splash));
    case DAY_ARCHERY: return archeryBounds(d.archery.a);
    case DAY_SLACK: return slackBounds(d.slack);
    case DAY_ROPES: return ropesBounds(d.ropes.r);
    default: return Rect{0, 0, 0, 0};
  }
}

// ---- the hooks World calls ---------------------------------------------------------------------------------

void dayPose(World& w) {
  DayState& d = w.day;
  Hiker& h = w.hiker;
  switch (d.kind) {
    case DAY_BIKE:
      if (bikeDriving(d.bike.bike)) bikeApplyRider(d.bike.bike, h);
      break;
    case DAY_ARCHERY: archeryPose(d.archery.a, h.pose); break;
    case DAY_SLACK:
      if (d.slack.line_on && d.slack.walker.mode != SW_OFF) slackWalkerApply(d.slack.walker, d.slack.line, h);
      break;
    case DAY_ROPES:
      if (w.react.active && ropesHikerBusy(d.ropes.r)) h.facing = w.react.facing;   // (the bell does not turn him on the course)
      ropesPose(d.ropes.r, h);
      break;
    default: break;
  }
}

bool dayHoldsFacing(const World& w) {
  const DayState& d = w.day;
  switch (d.kind) {
    case DAY_BIKE: return bikeDriving(d.bike.bike);
    case DAY_SLACK: return d.slack.line_on && d.slack.walker.mode != SW_OFF;
    case DAY_ROPES: return ropesHikerBusy(d.ropes.r);
    default: return false;
  }
}

void dayStep(World& w, bool frozen) {
  DayState& d = w.day;
  const Hiker& h = w.hiker;
  switch (d.kind) {
    case DAY_KITE: kiteWorldStep(w, frozen); break;
    case DAY_ARCHERY:
      archeryFace(d.archery.a, h.facing);   // the bow follows which way he faces (a shot keeps him facing the target)
      archeryStep(d.archery.a, hikerHandPos(h, 0), hikerHandPos(h, 1), frozen ? 0.f : SS_TICK_DT);
      break;
    case DAY_SLACK: {
      DaySlack& s = d.slack;
      if (!s.crit_on || frozen) break;
      slackCritterDrive(s.sc, s.cr);
      critterTick(s.cr, SS_TICK_DT);
      slackCritterPlace(s.sc, s.line, s.cr);
      s.cr.visible = true;
      break;
    }
    default: break;
  }
}

void dayTrack(World& w, Renderer& r) {
  DayState& d = w.dirty.day_s;
  const DayState& c = w.day;
  if (c.kind == DAY_NONE && d.kind == DAY_NONE) return;
  if (c.kind != d.kind) {   // one piece leaves and another arrives (or none)
    markOn(r, dayBounds(d));
    markOn(r, dayBounds(c));
  } else {
    switch (c.kind) {
      case DAY_KITE: {
        Rect kr[KITE_DIRTY_MAX];
        const int n = kiteDirty(d.kite, c.kite, kr);
        for (int i = 0; i < n; ++i) markOn(r, kr[i]);
        break;
      }
      case DAY_BIKE: {
        Rect rb[BIKE_DIRTY_MAX], rs[SPLASH_DIRTY_MAX];
        const int nb = bikeDirty(d.bike.bike, c.bike.bike, rb), ns = splashDirty(d.bike.splash, c.bike.splash, rs);
        for (int i = 0; i < nb; ++i) markOn(r, rb[i]);
        for (int i = 0; i < ns; ++i) markOn(r, rs[i]);
        break;
      }
      case DAY_ARCHERY: {
        Rect ra[ARCHERY_DIRTY_MAX];
        const int n = archeryDirty(d.archery.a, c.archery.a, ra);
        for (int i = 0; i < n; ++i) markOn(r, ra[i]);
        break;
      }
      case DAY_SLACK: {
        const DaySlack& p = d.slack;
        const DaySlack& s = c.slack;
        if (p.line_on != s.line_on) {   // tied up or untied: the line and the stake come or go whole
          markOn(r, unite(slacklineBounds(p.line), daySlackStakeBounds(p)));
          markOn(r, unite(slacklineBounds(s.line), daySlackStakeBounds(s)));
        } else if (s.line_on) {
          Rect rl[SLACK_DIRTY_MAX];
          const int n = slacklineDirty(p.line, s.line, rl);
          for (int i = 0; i < n; ++i) markOn(r, rl[i]);
        }
        if (p.crit_on) markOn(r, critterBounds(p.cr));   // it moves or fidgets every tick it is on the line
        if (s.crit_on) markOn(r, critterBounds(s.cr));
        break;
      }
      case DAY_ROPES: {
        Rect rr[ROPES_DIRTY_MAX];
        const int n = ropesDirty(d.ropes.r, c.ropes.r, rr);
        for (int i = 0; i < n; ++i) markOn(r, rr[i]);
        break;
      }
      default: break;
    }
  }
  d.kind = c.kind;
  d.pack_was = c.pack_was;
  const size_t n = sizeOf(c.kind);
  if (n) memcpy(static_cast<void*>(&d.kite), static_cast<const void*>(&c.kite), n);
}

bool dayPartY(const World& w, int part, int16_t& y) {
  const DayState& d = w.day;
  const Hiker& h = w.hiker;
  switch (d.kind) {
    case DAY_BIKE: {
      const BikeState& b = d.bike.bike;
      if (part == 0) {   // the bike goes under him while he rides, over him when it lies on him
        y = (int16_t)(baseY(h.feet.y) + (bikeInFront(b) ? 1 : -1));
        return true;
      }
      if (part == 1) {   // the water over both
        const float fy = h.feet.y > b.pivot.y ? h.feet.y : b.pivot.y;
        y = (int16_t)(baseY(fy) + 3);
        return true;
      }
      return false;
    }
    case DAY_ARCHERY:
      if (part == 0) {
        y = baseY(d.archery.a.target.y);
        return true;
      }
      if (part == 1) {   // the bow, in his hand: in front of him
        y = baseY(h.feet.y + 1.f);
        return true;
      }
      return false;
    case DAY_SLACK:
      if (part == 0 && d.slack.line_on) {   // the line is behind everything that stands on it
        y = baseY(d.slack.line.ground_y - 40.f);
        return true;
      }
      if (part == 1 && d.slack.crit_on) {
        y = baseY(d.slack.cr.feet.y + 1.f);
        return true;
      }
      return false;
    case DAY_ROPES:
      if (part == 0 && !ropesBounds(d.ropes.r).empty()) {   // the props are behind him on the ground line
        y = (int16_t)(baseY(d.ropes.r.gy) - 1);
        return true;
      }
      return false;
    default: return false;
  }
}

void dayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal) {
  const DayState& d = w.day;
  switch (d.kind) {
    case DAY_BIKE:
      if (part == 0) bikeDraw(d.bike.bike, s, clip, pal);
      else if (part == 1) splashDraw(d.bike.splash, s, clip, pal);
      break;
    case DAY_ARCHERY:
      if (part == 0) {
        archeryArrowsDraw(d.archery.a, s, clip, pal, true);
        archeryTargetDraw(d.archery.a, s, clip, pal);
        archeryArrowsDraw(d.archery.a, s, clip, pal, false);
      } else if (part == 1) {
        archeryBowDraw(d.archery.a, s, clip, pal);
      }
      break;
    case DAY_SLACK:
      if (part == 0 && d.slack.line_on) {
        const DaySlack& k = d.slack;
        const Rect sb = daySlackStakeBounds(k);
        if (!sb.clipped(clip).empty())
          strokeCapsule(s, Vec2{k.line.a.x, k.line.ground_y}, Vec2{k.line.a.x, k.line.a.y - 0.5f * kStakeTop}, kStakeW, pal[TRUNK], clip);
        slacklineDraw(k.line, s, clip, pal);
      } else if (part == 1 && d.slack.crit_on) {
        critterDraw(d.slack.cr, s, pal, clip);
      }
      break;
    case DAY_ROPES: ropesDraw(d.ropes.r, s, clip, pal); break;
    default: break;
  }
}

}  // namespace ss
