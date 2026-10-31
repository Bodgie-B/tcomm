// Weather. See weather.h.
//
// Costs (strips are whole 800 x 32 px rows of the frame, see renderer.h):
//   rain, snow  every strip, each redrawn every 3rd tick, staggered by its
//               index (5 strips a tick, 8.3 Hz a strip); the drawn amount in
//               1/16 steps, a step repaints everything (a shower: ~10 steps in, ~10 out)
//   shimmer     strips 7 and 8, on the 12.5 Hz sub-tick, only when a row's offset changes
//   fog bands   their own rows (strips 8 to 10) when a band's whole-px x or its
//               breathing alpha step changes
//   flash       palette steps 1, 0.5, 0: three cache rebuilds a strike, a strike
//               every 100 to 140 s (about 20 rebuilds a storm loop with rain_shelter)
//   the rest    small bounds, tracked like any actor
// No per-drop state: a strip's streaks and flakes are a pure function of
// (tick, strip, amount), so they cost nothing to keep and a full repaint
// draws exactly what the strips drew.
#include "weather.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "../render/renderer.h"
#include "../ss_app.h"
#include "director.h"
#include "landscape.h"
#include "script.h"
#include "world.h"

namespace ss {

namespace {

constexpr float kDt = SS_TICK_DT;
const Rect kNone{0, 0, 0, 0};

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline uint32_t ticksOf(float s) { return (uint32_t)(s * (float)SS_TICK_HZ + 0.5f); }

// A repeatable 0..1 from two integers.
float hash01(uint32_t a, uint32_t b) {
  uint32_t x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
  x ^= x >> 15;
  x *= 0x2C1B3C6Du;
  x ^= x >> 12;
  x *= 0x297A2D39u;
  x ^= x >> 15;
  return (float)(x >> 8) * (1.f / 16777216.f);
}

inline bool sameRect(const Rect& a, const Rect& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }
inline void markOn(Renderer& r, const Rect& b) {
  if (!b.empty() && b.x1 > 0 && b.x0 < SS_WIDTH) r.markDirtyRect(b);
}
void track(Renderer& r, Rect& prev, const Rect& cur, bool changed) {
  if (!changed && sameRect(prev, cur)) return;
  markOn(r, prev);
  markOn(r, cur);
  prev = cur;
}

// ---- the mapping table and the random draw -----------------------------------------------

// Inputs::weather_code (ss_app.h): 0 clear, 1 rain, 2 snow, 3 wind, 4 heat,
// 5 fog, 6 chill, 7 storm. Edit here when WeatherGate's real codes are known.
const uint8_t kCodeKinds[] = {WX_CLEAR, WX_RAIN, WX_SNOW, WX_WIND, WX_HEAT, WX_FOG, WX_CHILL, WX_STORM};
const char* const kKindNames[WX_COUNT] = {"clear", "wind", "rain", "snow", "fog", "heat", "chill", "storm"};

// No data: the half that is not clear, weighted by the mood key (night, dawn,
// morning, day, golden, dusk), interpolated between keys. Kind order as
// WeatherKind from WX_WIND. Fog likelier at dawn, heat by day, storm at
// golden hour, chill at night; snow only cold (night, dawn, dusk), never by day.
//                               wind   rain   snow   fog    heat   chill  storm
const float kMoodWeights[6][7] = {{0.15f, 0.15f, 0.10f, 0.10f, 0.00f, 0.38f, 0.12f},   // night
                                  {0.10f, 0.12f, 0.08f, 0.45f, 0.00f, 0.25f, 0.00f},   // dawn
                                  {0.25f, 0.20f, 0.00f, 0.25f, 0.10f, 0.15f, 0.05f},   // morning
                                  {0.25f, 0.15f, 0.00f, 0.05f, 0.42f, 0.00f, 0.13f},   // day
                                  {0.28f, 0.17f, 0.00f, 0.05f, 0.12f, 0.05f, 0.33f},   // golden
                                  {0.25f, 0.20f, 0.05f, 0.10f, 0.00f, 0.25f, 0.15f}};  // dusk

uint8_t randomKind(Rng& rng, const Inputs& in) {
  const float clear = rng.uniform();
  float wt[7];
  float m = in.mood - 6.f * floorf(in.mood / 6.f);
  if (!(m >= 0.f && m < 6.f)) m = 0.f;
  int i0 = (int)floorf(m);
  if (i0 > 5) i0 = 5;
  const int i1 = (i0 + 1) % 6;
  const float f = in.night_mode ? 0.f : m - (float)i0;
  if (in.night_mode) i0 = 0;
  for (int k = 0; k < 7; ++k) wt[k] = lerp(kMoodWeights[i0][k], kMoodWeights[i1][k], f);
  const int pick = rng.weighted(wt, 7);
  return clear < 0.5f ? (uint8_t)WX_CLEAR : (uint8_t)(WX_WIND + pick);
}

// A breath puff (chill): the boil-over's puff shape (props Steam, one puff,
// SMOKE), but small and drifting out ahead of his face, not up. Steam::n
// holds which way he faced (1 right, 2 left); t < 0 is off.
constexpr float kBreathLife = 1.3f;
bool breathGeom(const Steam& b, Vec2& c, float& r) {
  if (b.t < 0.f || b.t >= kBreathLife) return false;
  const float u = b.t / kBreathLife;
  const float f = b.n == 2 ? -1.f : 1.f;
  c = Vec2{b.at.x + f * 11.f * easeOutCubic(u), b.at.y - 5.f * u};
  r = (1.2f + 3.2f * easeOutCubic(u)) * (1.f - u * u);
  return r > 0.5f;
}
Rect breathBounds(const Steam& b) {
  Vec2 c;
  float r;
  if (!breathGeom(b, c, r)) return Rect{0, 0, 0, 0};
  return Rect{(int)floorf(c.x - 1.3f * r) - 1, (int)floorf(c.y - r) - 1, (int)ceilf(c.x + 1.3f * r) + 1, (int)ceilf(c.y + r) + 1};
}

bool windy(const Weather& x) { return x.kind == WX_WIND || x.kind == WX_STORM || x.extra == WXE_GUST; }

// ---- the weather's bits ---------------------------------------------------------------------

enum WxRole : uint8_t { WR_NONE = 0, WR_LEAF, WR_DRIP, WR_RING, WR_SWEAT, WR_PUFF };

int takeBit(Weather& x, uint8_t kind, uint8_t role) {
  for (int i = 0; i < WX_BITS; ++i) {
    if (x.bit[i].kind != BIT_NONE) continue;
    uint16_t& n = role == WR_LEAF ? x.n_leaf : (role == WR_RING ? x.n_ring : (role == WR_PUFF ? x.n_puff : x.n_drip));
    if (n < 0xFFFF) ++n;
    x.bit[i] = Bit{};
    x.bit[i].kind = kind;
    x.role[i] = role;
    x.aux[i] = 0.f;
    return i;
  }
  return -1;
}
int countRole(const Weather& x, uint8_t role) {
  int n = 0;
  for (int i = 0; i < WX_BITS; ++i) n += x.bit[i].kind != BIT_NONE && x.role[i] == role ? 1 : 0;
  return n;
}

void stepBits(World& w) {
  Weather& x = w.weather;
  for (int i = 0; i < WX_BITS; ++i) {
    Bit& b = x.bit[i];
    if (b.kind == BIT_NONE) continue;
    if (x.role[i] == WR_LEAF) {   // across on the wind, bobbing and turning
      b.t += kDt;
      const float v = x.aux[i] * (0.55f + 0.9f * x.gust);
      b.pos.x -= v * kDt;
      b.pos.y = b.land_y - 26.f - 14.f * sinT(b.t * 0.45f + (float)i * 0.3f) - 6.f * sinT(b.t * 1.3f);
      b.spin += kDt * 1.1f;
      if (b.pos.x < -20.f) b.kind = BIT_NONE;
      continue;
    }
    bitStep(b, kDt);
    if (x.role[i] == WR_SWEAT && b.kind == BIT_RING) b.kind = BIT_NONE;   // a sweat drop just lands
    if (x.role[i] == WR_PUFF && b.kind == BIT_DUST) b.pos.y += 20.f * kDt; // snow sliding off: down, not up
  }
}

// ---- the hiker overlay ------------------------------------------------------------------------

bool isPause(const World& w) { return w.minors.beat && strcmp(w.minors.beat, "pause") == 0; }

// He is ours for `need` seconds: in a pause with that long left, nothing
// holding him (no script, major or minor), still, standing or sitting.
// `quiet`: also no critter about and no minor due before then, for the
// overlays that move him (the hat) or take his hat (the fan).
bool hikerFree(const World& w, float need, bool quiet) {
  const Hiker& h = w.hiker;
  if (!w.fx.on || !h.visible || w.busy || w.major.on || !scriptDone(w)) return false;
  if (!isPause(w) || w.minors.beat_left < need + 1.f) return false;
  if (h.walking || h.blending || (h.target != POSE_SIT && h.target != POSE_STAND)) return false;
  if (w.minors.cur != MN_NONE || w.minors.fx_head || w.minors.fx_arm) return false;
  if (quiet && directorCount(w) > 0) return false;   // (the minors wait for a hat or fan gag: minors.cpp)
  return true;
}

void endGag(Weather& x) {
  x.gag = WG_NONE;
  x.stage = 0;
  x.gag_t = 0.f;
  x.ov_w = 0.f;
}

// Where the blown hat comes down: downwind (left), on the clearing, not in
// the stream, clear of the frame's edge. Returns false when nowhere fits.
bool hatLanding(const World& w, Vec2& to) {
  const Hiker& h = w.hiker;
  static const float kDist[3] = {62.f, 48.f, 36.f};
  for (float d : kDist) {
    const float x = h.feet.x - d;
    if (x < 60.f) continue;
    bool ok = true;
    for (float xx = x; xx <= h.feet.x && ok; xx += 8.f) {
      float cy, half;
      if (landscapeStreamSpan(w.land, xx, cy, half) && fabsf(h.feet.y - cy) < half + 14.f) ok = false;
    }
    if (!ok) continue;
    to = Vec2{x, h.feet.y};
    return true;
  }
  return false;
}

// The hat on the wind: up off his head, down, two hops.
Vec2 hatFlight(const Weather& x, float u) {
  const Vec2 a = x.hat0, b = x.hat1;
  const float xa = lerp(a.x, b.x, 0.62f), xb = lerp(a.x, b.x, 0.88f);
  if (u < 0.5f) {
    const float v = u / 0.5f;
    return Vec2{lerp(a.x, xa, v), lerp(a.y, b.y, v * v) - 16.f * sinT(0.5f * v)};
  }
  if (u < 0.78f) {
    const float v = (u - 0.5f) / 0.28f;
    return Vec2{lerp(xa, xb, v), b.y - 10.f * 4.f * v * (1.f - v)};
  }
  const float v = (u - 0.78f) / 0.22f;
  return Vec2{lerp(xb, b.x, v), b.y - 4.f * 4.f * v * (1.f - v)};
}

void stepHat(World& w) {
  Weather& x = w.weather;
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  x.gag_t += kDt;
  // Lost him to something else (should not happen inside a pause): put it all back.
  if (w.major.on || !scriptDone(w) || !w.fx.on) {
    hat.down = hat.held = false;
    h.hat_on = true;
    w.fx.path_on = x.home_path;
    endGag(x);
    return;
  }
  switch (x.stage) {
    case 0: {   // it goes: he reaches up after it
      const float u = clamp01(x.gag_t / 1.4f);
      hat.pos = hatFlight(x, u);
      if (u >= 1.f) {
        hat.pos = x.hat1;
        hikerWalkTo(h, x.hat1.x + 13.f, 70.f);
        x.stage = 1;
        x.gag_t = 0.f;
      }
      break;
    }
    case 1:   // after it
      if (hikerArrived(h)) {
        h.facing = -1;
        hikerSetPose(h, POSE_CROUCH, 0.35f);
        x.stage = 2;
        x.gag_t = 0.f;
      }
      break;
    case 2:   // picks it up, back on his head
      if (x.gag_t >= 0.5f && !h.hat_on) {
        hat.down = false;
        h.hat_on = true;
      }
      if (x.gag_t >= 0.75f) {
        hikerSetPose(h, POSE_STAND, 0.4f);
        x.stage = 3;
        x.gag_t = 0.f;
      }
      break;
    case 3:
      if (x.gag_t >= 0.5f) {
        hikerWalkTo(h, x.home.x, 55.f);
        x.stage = 4;
        x.gag_t = 0.f;
      }
      break;
    case 4:   // back where he was, as he was
      if (hikerArrived(h)) {
        h.feet = x.home;
        h.facing = x.home_face;
        hikerSetPose(h, (PoseId)x.home_pose, x.home_pose == POSE_SIT ? 0.9f : 0.5f);
        x.stage = 5;
        x.gag_t = 0.f;
      }
      break;
    default:
      if (x.gag_t >= (x.home_pose == POSE_SIT ? 1.f : 0.6f)) {
        w.fx.path_on = x.home_path;
        endGag(x);
      }
      break;
  }
}

// Heat: the hat off, fanned with the near hand, back on.
void stepFan(World& w) {
  Weather& x = w.weather;
  Hiker& h = w.hiker;
  Item& hat = w.items[ITEM_HAT];
  x.gag_t += kDt;
  const bool lost = w.major.on || !scriptDone(w) || !w.fx.on || h.walking;
  if (lost || x.stage > 3) {
    hat.held = hat.down = false;
    h.hat_on = true;
    endGag(x);
    return;
  }
  constexpr float kHatSh = 2.75f, kHatEl = 2.0f;
  switch (x.stage) {
    case 0:   // up to the hat
      x.ov_w = easeInOut(clamp01(x.gag_t / 0.6f));
      x.ov_sh = kHatSh;
      x.ov_el = kHatEl;
      if (x.gag_t >= 0.6f) {
        h.hat_on = false;
        hat.held = true;
        hat.down = false;
        hat.critter = 0;
        hat.facing = (int8_t)h.facing;
        hat.size = 0.9f;
        x.stage = 1;
        x.gag_t = 0.f;
        x.fan_ph = 0.f;
      }
      break;
    case 1: {   // fanning: the arm swings in front of his face
      x.fan_ph += kDt * 2.1f;
      const float in = easeInOut(clamp01(x.gag_t / 0.4f));
      x.ov_sh = lerp(kHatSh, 1.95f + 0.32f * sinT(x.fan_ph), in);
      x.ov_el = lerp(kHatEl, 1.15f + 0.25f * sinT(x.fan_ph + 0.2f), in);
      if (x.gag_t >= 3.2f) {
        x.stage = 2;
        x.gag_t = 0.f;
      }
      break;
    }
    case 2: {   // back on
      const float u = clamp01(x.gag_t / 0.6f);
      x.ov_sh = lerp(x.ov_sh, kHatSh, u);
      x.ov_el = lerp(x.ov_el, kHatEl, u);
      if (u >= 1.f) {
        h.hat_on = true;
        hat.held = false;
        x.stage = 3;
        x.gag_t = 0.f;
      }
      break;
    }
    case 3:
      x.ov_w = 1.f - easeInOut(clamp01(x.gag_t / 0.5f));
      if (x.gag_t >= 0.5f) endGag(x);
      break;
    default: break;
  }
}

// Chill: the hug in, held, out. It lets go early (0.4 s) if anything else wants him.
void stepHug(World& w) {
  Weather& x = w.weather;
  x.gag_t += kDt;
  const bool keep = hikerFree(w, 0.f, false);
  if (x.stage == 0) {
    x.hug_w = minf(1.f, x.gag_t / 1.f);
    if (!keep) x.stage = 2;
    else if (x.gag_t >= 1.f + x.gag_len) x.stage = 1;
  } else {
    const float rate = keep ? 1.f : 2.5f;
    x.hug_w = maxf(0.f, x.hug_w - kDt * rate);
    if (x.hug_w <= 0.f) endGag(x);
  }
}

void stepOverlay(World& w) {
  Weather& x = w.weather;
  Hiker& h = w.hiker;
  const uint32_t t = w.tick;
  switch (x.gag) {
    case WG_HAT: stepHat(w); break;
    case WG_FAN: stepFan(w); break;
    case WG_HUG: stepHug(w); break;
    default: break;
  }
  // A new one?
  if (x.gag == WG_NONE && t >= x.next_gag) {
    if (x.kind == WX_WIND && x.hats == 0 && h.hat_on && h.carry == CARRY_NONE &&
        hikerFree(w, h.target == POSE_SIT ? 7.5f : 6.5f, true)) {
      Vec2 to;
      if (hatLanding(w, to)) {
        float r = 4.f;
        const Vec2 hd = hikerHeadPos(h, &r);
        x.gag = WG_HAT;
        x.stage = 0;
        x.gag_t = 0.f;
        x.home = h.feet;
        x.home_face = (int8_t)h.facing;
        x.home_pose = (uint8_t)h.target;
        x.home_path = w.fx.path_on;
        w.fx.path_on = false;
        x.hat0 = Vec2{hd.x, hd.y - r + 4.f};
        x.hat1 = to;
        Item& hat = w.items[ITEM_HAT];
        h.hat_on = false;
        hat.down = true;
        hat.held = false;
        hat.on_fire = false;
        hat.critter = 0;
        hat.facing = (int8_t)h.facing;
        hat.size = 1.f;
        hat.pos = x.hat0;
        hikerSetPose(h, POSE_REACH_UP, 0.3f);
        ++x.hats;
        // Its own gust (the leaves and the grass go with it).
        x.next_gust = t;
      }
    } else if (x.kind == WX_HEAT && x.fans < 2 && h.hat_on && h.carry == CARRY_NONE && hikerFree(w, 5.5f, true)) {
      x.gag = WG_FAN;
      x.stage = 0;
      x.gag_t = 0.f;
      ++x.fans;
      x.next_gag = t + ticksOf(x.rng.range(40.f, 80.f));
    } else if (x.kind == WX_CHILL && hikerFree(w, 5.f, false)) {
      x.gag = WG_HUG;
      x.stage = 0;
      x.gag_t = 0.f;
      x.gag_len = x.rng.range(3.f, 7.f);
      if (x.gag_len + 2.f > w.minors.beat_left - 1.f) x.gag_len = maxf(1.f, w.minors.beat_left - 3.5f);
      x.next_gag = t + ticksOf(x.gag_len + x.rng.range(8.f, 16.f));
    }
  }
  // A shiver now and then (chill), while he is still and visible.
  if (x.shiver > 0.f) {
    x.shiver -= kDt;
    x.shiver_dx = x.shiver > 0.f ? (((t >> 1) & 1u) ? 1.5f : -1.5f) : 0.f;
  } else if (x.kind == WX_CHILL && t >= x.next_shiver) {
    x.next_shiver = t + ticksOf(x.rng.range(9.f, 18.f));
    if (w.fx.on && h.visible && !h.walking && !w.major.on) x.shiver = 0.45f;
  }
}

// Breath, sweat, the shake-off: small things about him.
void stepAboutHim(World& w) {
  Weather& x = w.weather;
  Hiker& h = w.hiker;
  const uint32_t t = w.tick;
  const bool still = w.fx.on && h.visible && !h.walking;
  if (x.breath.t >= 0.f) {
    x.breath.t += kDt;
    if (x.breath.t >= kBreathLife) x.breath.t = -1.f;
  }
  if (x.kind == WX_CHILL && t >= x.next_breath) {
    x.next_breath = t + ticksOf(x.rng.range(4.f, 6.f));
    if (still && x.breath.t < 0.f) {
      float r = 4.f;
      const Vec2 hd = hikerHeadPos(h, &r);
      x.breath = Steam{Vec2{hd.x + (float)h.facing * (r + 2.f), hd.y + 0.45f * r}, 0.f, (uint8_t)(h.facing < 0 ? 2 : 1)};
    }
  }
  if (x.kind == WX_HEAT && t >= x.next_sweat) {
    x.next_sweat = t + ticksOf(x.rng.range(9.f, 16.f));
    if (still && x.gag == WG_NONE) {
      const int s = takeBit(x, BIT_DROP, WR_SWEAT);
      if (s >= 0) {
        float r = 4.f;
        const Vec2 hd = hikerHeadPos(h, &r);
        Bit& b = x.bit[s];
        b.pos = Vec2{hd.x + (float)h.facing * 0.8f * r, hd.y + 0.2f * r};
        b.vel = Vec2{(float)h.facing * 10.f, -20.f};
        b.ground_y = b.land_y = h.feet.y - 1.f;
      }
    }
  }
}

}  // namespace

// ---- public ------------------------------------------------------------------------------------

uint8_t weatherKindForCode(int code) {
  const int n = (int)(sizeof kCodeKinds / sizeof kCodeKinds[0]);
  return code >= 0 && code < n ? kCodeKinds[code] : (uint8_t)WX_CLEAR;
}

const char* weatherKindName(int kind) { return kind >= 0 && kind < WX_COUNT ? kKindNames[kind] : "?"; }

bool weatherPlannedStorm(const Inputs& in) { return in.have_outside && weatherKindForCode(in.weather_code) == WX_STORM; }

void weatherPlan(World& w, uint64_t loop_seed, const Inputs& in, uint32_t loop_ticks) {
  Weather& x = w.weather;
  x = Weather{};
  x.rng.seed(loop_seed ^ 0x5745415448455221ull, 0x5745415448455203ull);
  if (in.have_outside) {
    x.kind = weatherKindForCode(in.weather_code);
    x.from_data = true;
  } else {
    x.kind = randomKind(x.rng, in);
  }
  // One rng call per statement from here on.
  const float er = x.rng.uniform();
  const float ek = x.rng.uniform();
  const bool can_gust = x.kind == WX_CLEAR || x.kind == WX_HEAT || x.kind == WX_CHILL || x.kind == WX_RAIN;
  const bool can_shower = x.kind == WX_CLEAR || x.kind == WX_WIND || x.kind == WX_CHILL;
  if (er < 0.3f) {
    if (can_gust && (!can_shower || ek < 0.5f)) x.extra = WXE_GUST;
    else if (can_shower) x.extra = WXE_SHOWER;
  }
  // The extra comes somewhere in the middle of the loop.
  const float len_s = (float)loop_ticks / (float)SS_TICK_HZ;
  const float at = x.rng.range(90.f, maxf(120.f, len_s - 120.f));
  x.extra_at = ticksOf(at);
  x.shower_len = ticksOf(x.rng.range(25.f, 45.f));
  const float kr = x.rng.range(0.6f, 1.f);
  x.k = x.k_to = x.kind == WX_RAIN ? kr : 1.f;
  const float wr = x.rng.range(0.6f, 1.f);
  x.wind = x.kind == WX_WIND ? -wr : (x.kind == WX_STORM ? -0.65f : (x.extra == WXE_GUST ? -0.8f : 0.f));
  for (int i = 0; i < WX_BANDS; ++i) {
    const float bx = x.rng.range(0.f, 1.f);
    const float by = x.rng.range(-6.f, 6.f);
    const float rx = x.rng.range(170.f, 260.f);
    const float ry = x.rng.range(13.f, 19.f);
    const float ph = x.rng.uniform();
    x.band_x[i] = (float)i * 330.f + 200.f * bx;
    x.band_y[i] = 284.f + 14.f * (float)i + by;
    x.band_rx[i] = rx;
    x.band_ry[i] = ry;
    x.band_ph[i] = ph;
  }
  x.breath = Steam{Vec2{0.f, 0.f}, -1.f, 1};
  x.d_breath_s = x.breath;
  x.next_flash = x.next_gust = x.next_gag = 0xFFFFFFFFu;
}

void weatherStep(World& w) {
  Weather& x = w.weather;
  if (!buildDone(w.build)) return;
  const uint32_t t = w.tick;
  if (!x.live) {   // the build just ended: the weather's clocks start now
    x.live = true;
    const float g = x.rng.range(3.f, 9.f);
    const float f = x.rng.range(20.f, 40.f);
    const float n = x.rng.range(10.f, 25.f);
    if (x.kind == WX_WIND || x.kind == WX_STORM) x.next_gust = t + ticksOf(g);
    else if (x.extra == WXE_GUST) x.next_gust = x.extra_at > t ? x.extra_at : t + ticksOf(g);
    if (x.kind == WX_STORM) x.next_flash = t + ticksOf(f);
    x.next_gag = t + ticksOf(n);
    x.next_drip = x.next_ring = x.next_sweat = x.next_hiss = x.next_breath = x.next_shiver = t + ticksOf(2.f);
  }

  // Intensity: a storm's rain comes back after rain_shelter eased it.
  if (x.kind == WX_STORM && x.k_to < 1.f && t >= x.ease_until) x.k_to = 1.f;
  const float dk = x.k_to - x.k, kstep = kDt / 5.f;
  x.k += dk > kstep ? kstep : (dk < -kstep ? -kstep : dk);

  // Gusts.
  if (windy(x) && t >= x.next_gust) {
    x.gust_t0 = t;
    const float gl = x.rng.range(2.5f, 4.5f);
    const float gn = x.rng.range(6.f, 14.f);
    x.gust_len = ticksOf(gl);
    x.next_gust = x.extra == WXE_GUST && x.kind != WX_WIND && x.kind != WX_STORM ? 0xFFFFFFFFu : t + ticksOf(gl + gn);
    if (x.kind != WX_STORM) {   // leaves on the gust, from off the right edge
      const int want = x.rng.rangeInt(2, 3);
      for (int i = 0; i < want; ++i) {
        const int s = takeBit(x, BIT_LEAF, WR_LEAF);
        const float y = x.rng.range(345.f, 405.f);
        const float v = x.rng.range(80.f, 130.f);
        const float dx = x.rng.range(0.f, 70.f);
        if (s < 0) continue;
        Bit& b = x.bit[s];
        b.land_y = y;
        b.pos = Vec2{(float)SS_WIDTH + 20.f + dx, y - 26.f};
        b.spin = (float)i * 0.3f;
        x.aux[s] = v;
      }
    }
  }
  x.gust = 0.f;
  if (x.gust_len > 0 && t >= x.gust_t0 && t < x.gust_t0 + x.gust_len) {
    const float u = (float)(t - x.gust_t0) / (float)x.gust_len;
    x.gust = easeInOut(window(u, 0.f, 0.25f)) * (1.f - easeInOut(window(u, 0.55f, 1.f)));
  }

  // The shower extra.
  x.shower = 0.f;
  if (x.extra == WXE_SHOWER && t >= x.extra_at) {
    const float s = (float)(t - x.extra_at) / (float)SS_TICK_HZ;
    const float hold = (float)x.shower_len / (float)SS_TICK_HZ;
    x.shower = easeInOut(window(s, 0.f, 8.f)) * (1.f - easeInOut(window(s, 8.f + hold, 18.f + hold)));
  }
  x.rain = maxf(x.kind == WX_RAIN || x.kind == WX_STORM ? x.k : 0.f, 0.55f * x.shower);
  x.rain_q = (uint8_t)floorf(x.rain * 16.f + 0.5f);
  x.snow_q = x.kind == WX_SNOW ? (uint8_t)floorf(x.k * 16.f + 0.5f) : 0;

  // Lightning: a strike every 100 to 140 s (none while rain_shelter runs: it
  // calls its own two). Each strike is three palette steps (1, 0.5, 0), so a
  // storm loop costs about 5 strikes x 3 + the shelter's 2 x 3 rebuilds.
  const bool sheltering = w.major.on && w.major.id == MJ_RAIN_SHELTER;
  if (x.kind == WX_STORM && !sheltering && t >= x.next_flash) {
    weatherStrike(w);
    const float gap = x.rng.range(100.f, 140.f);
    x.next_flash = t + ticksOf(gap);
  }
  x.flash = maxf(0.f, x.flash - kDt / 0.3f);
  x.flash_q = x.flash > 0.6f ? 2 : (x.flash > 0.2f ? 1 : 0);

  // The tent: the gusts lean it; snow builds on it and comes off once when
  // he walks past; rain drips off its ridge.
  Tent& tent = w.tent;
  const bool pitched = tent.state == TENT_UP && tent.sag < 0.05f;
  if (windy(x)) x.sway_px = (int8_t)floorf(0.5f + 3.f * fabsf(x.wind) * (0.25f + 0.75f * x.gust));
  if (x.shake > 0.f) {
    x.shake -= kDt;
    x.sway_px = (int8_t)floorf(0.5f + 2.f * sinT(x.shake * 3.f));
    if (x.shake <= 0.f) x.sway_px = 0;
  }
  if (windy(x) || x.kind == WX_SNOW) tent.sway = -(float)x.sway_px;
  if (x.kind == WX_SNOW) {
    if (pitched) x.snow_roof = minf(1.f, x.snow_roof + x.k * kDt / 140.f);
    const Hiker& h = w.hiker;
    if (pitched && x.shakes == 0 && x.snow_roof > 0.4f && w.fx.on && h.walking && fabsf(h.feet.x - tent.pos.x) < 30.f) {
      ++x.shakes;
      x.snow_roof *= 0.2f;
      x.shake = 0.9f;
      for (int k = 0; k < 2; ++k) {
        const int s = takeBit(x, BIT_DUST, WR_PUFF);
        if (s < 0) break;
        const Vec2 e = tentRidgeEnd(tent, k);
        x.bit[s].pos = Vec2{e.x + (k ? 6.f : -6.f), e.y + 6.f};
        x.bit[s].vel = Vec2{k ? 10.f : -10.f, 0.f};
        x.bit[s].land_y = tent.pos.y;
      }
    }
    tent.snow = tent.state == TENT_UP ? floorf(x.snow_roof * 12.f) / 12.f : 0.f;
  }
  if (x.rain_q >= 4 && pitched && t >= x.next_drip) {
    const float gap = x.rng.range(1.0f, 2.4f);
    const float side = x.rng.uniform();
    x.next_drip = t + ticksOf(gap * (1.4f - 0.6f * x.rain));
    const int s = takeBit(x, BIT_DROP, WR_DRIP);
    if (s >= 0) {
      const Vec2 e = tentRidgeEnd(tent, side < 0.5f ? 0 : 1);
      Bit& b = x.bit[s];
      b.pos = Vec2{e.x + (side < 0.5f ? -1.f : 1.f), e.y + 2.f};
      b.vel = Vec2{x.wind * 12.f, 0.f};
      b.ground_y = b.land_y = tent.pos.y + 3.f;
    }
  }
  // Rings on the stream where the rain hits it.
  if (x.rain_q >= 3 && t >= x.next_ring) {
    const float gap = x.rng.range(0.6f, 1.6f);
    const float rx = x.rng.range(40.f, 640.f);
    const float ry = x.rng.range(-0.55f, 0.55f);
    const float rs = x.rng.range(0.3f, 0.6f);
    x.next_ring = t + ticksOf(gap * (1.5f - 0.7f * x.rain));
    float cy, half;
    if (landscapeStreamSpan(w.land, rx, cy, half) && countRole(x, WR_RING) < 2) {
      const int s = takeBit(x, BIT_RING, WR_RING);
      if (s >= 0) {
        Bit& b = x.bit[s];
        b.pos = Vec2{rx, cy + ry * half};
        b.spin = rs;
        b.land_y = b.pos.y;
      }
    }
  }
  // The fire hisses in the rain.
  if (x.hiss.kind != GL_NONE) {
    x.hiss_t += kDt;
    const float u = x.hiss_t / 0.9f;
    x.hiss.scale = 4.f + 5.f * u;
    x.hiss.k = 1.2f * (1.f - u);
    if (u >= 1.f) x.hiss.kind = GL_NONE;
  } else if (x.rain_q >= 5 && w.fire.state == FIRE_LIT && w.fire.intensity > 0.15f && t >= x.next_hiss) {
    const float gap = x.rng.range(5.f, 10.f);
    const float side = x.rng.range(-6.f, 6.f);
    x.next_hiss = t + ticksOf(gap);
    x.hiss = Glyph{};
    x.hiss.kind = GL_ARCS;
    x.hiss.slot = SMOKE;
    x.hiss.facing = 1;
    x.hiss.pos = Vec2{w.fire.pos.x + side, w.fire.pos.y - 14.f - 30.f * w.fire.intensity};
    x.hiss.rot = -0.25f;
    x.hiss.scale = 4.f;
    x.hiss.k = 1.2f;
    x.hiss.base_y = (int16_t)(w.fire.pos.y + 6.f);
    x.hiss_t = 0.f;
  }

  stepBits(w);
  stepAboutHim(w);
  stepOverlay(w);
}

void weatherPlaceHeld(World& w) {
  const Weather& x = w.weather;
  Item& hat = w.items[ITEM_HAT];
  if (x.gag == WG_FAN && hat.held && !hat.critter) {
    const Vec2 p = hikerHandPos(w.hiker, 0);
    hat.pos = Vec2{p.x, p.y + 3.f};
  }
}

void weatherStrike(World& w) {
  Weather& x = w.weather;
  x.flash = 1.f;
  x.flash_q = 2;
  if (x.flashes < 0xFFFF) ++x.flashes;
  const uint32_t soon = w.tick + ticksOf(45.f);   // the next natural one keeps its distance
  if (x.next_flash < soon) x.next_flash = soon;
}

void weatherEaseRain(World& w, float k, float hold) {
  Weather& x = w.weather;
  x.k_to = k;
  x.ease_until = w.tick + ticksOf(hold);
}

float weatherTint(const World& w, WeatherTint& t) {
  const Weather& x = w.weather;
  if (x.kind == WX_CLEAR && x.extra == WXE_NONE) return 0.f;
  float oc = 0.f;
  if (x.kind == WX_RAIN) oc = 0.6f;
  else if (x.kind == WX_STORM) oc = 0.8f;
  else if (x.kind == WX_SNOW) oc = 0.7f;
  if (oc > t.overcast) t.overcast = oc;
  // Fog: screened over what is there (the morning's own lifting fog keeps
  // every one of its 0.05 steps a visible step: 0.05 x 0.55 > the app's 0.02).
  if (x.kind == WX_FOG) t.fog = 1.f - (1.f - clamp01(t.fog)) * (1.f - 0.45f);
  static const float kFlash[3] = {0.f, 0.5f, 1.f};
  return kFlash[x.flash_q < 3 ? x.flash_q : 2];
}

float weatherSwayMul(const World& w) {
  const Weather& x = w.weather;
  if (!windy(x) || !x.live) return 1.f;
  const float base = x.kind == WX_WIND || x.kind == WX_STORM ? 0.35f : 0.f;
  return 1.f + 1.5f * fabsf(x.wind) * (base + (1.f - base) * x.gust);
}

float weatherSmokeMul(const World& w) { return w.weather.rain > 0.05f ? 1.f + 0.6f * w.weather.rain : 1.f; }
float weatherFireCap(const World& w) { return w.weather.rain > 0.05f ? lerp(1.f, 0.5f, w.weather.rain) : 1.f; }

void weatherApplyPose(const World& w, HikerPose& p, float height) {
  const Weather& x = w.weather;
  if (x.hug_w > 0.f) {
    const HikerPose& H = hikerPoseDef(POSE_HUG);
    const float e = easeInOut(x.hug_w);
    p.lean += 0.11f * e;
    p.head += 0.2f * e;
    for (int i = 0; i < 2; ++i) {
      p.shoulder[i] = lerp(p.shoulder[i], H.shoulder[i], e);
      p.elbow[i] = lerp(p.elbow[i], H.elbow[i], e);
    }
  }
  if (x.ov_w > 0.f) {
    p.shoulder[0] = lerp(p.shoulder[0], x.ov_sh, x.ov_w);
    p.elbow[0] = lerp(p.elbow[0], x.ov_el, x.ov_w);
  }
  if (x.shiver_dx != 0.f && height > 1.f) p.dx += x.shiver_dx / height;
}

// ---- drawing ------------------------------------------------------------------------------------

namespace {

constexpr int kShimRows = 24;
constexpr int kShimY0 = LS_HORIZON - kShimRows;

bool shimmerOn(const World& w) { return w.weather.kind == WX_HEAT && w.weather.live && buildDone(w.build); }

// Row offsets on the 12.5 Hz sub-tick: a slow sine up the band, stronger near the horizon.
int shimmerDx(const World& w, int row) {
  const float tt = (float)(w.tick >> 1) * (2.f / (float)SS_TICK_HZ);
  const float amp = w.weather.k * (0.5f + 1.7f * (float)row / (float)kShimRows);
  const float v = sinT((float)row * 0.083f + tt * 0.55f) + 0.5f * sinT((float)row * 0.21f - tt * 0.9f);
  return (int)floorf(0.5f + amp * 0.75f * v);
}

void bandGeom(const World& w, int i, int& cx, float& cy, float& rx, float& ry, uint8_t& alpha) {
  const Weather& x = w.weather;
  const float s = (float)w.tick / (float)SS_TICK_HZ;
  rx = x.band_rx[i];
  ry = x.band_ry[i];
  const float span = (float)SS_WIDTH + 2.f * rx;
  float px = x.band_x[i] - 3.f * s + rx;   // drifting left at 3 px/s
  px -= span * floorf(px / span);
  cx = (int)floorf(px - rx);
  cy = x.band_y[i] + 2.f * sinT(x.band_ph[i] + s / 23.f);
  // Breathing: slow, in steps of 6 (each step is one repaint of its rows).
  const float a = x.k * (80.f + 40.f * sinT(x.band_ph[i] * 2.f + s / 13.f));
  alpha = (uint8_t)(6 * (int)(a / 6.f));
  cy = floorf(cy);
}

bool fogOn(const World& w) { return w.weather.kind == WX_FOG && w.weather.live; }

Rect bandBounds(const World& w, int i) {
  int cx;
  float cy, rx, ry;
  uint8_t a;
  bandGeom(w, i, cx, cy, rx, ry, a);
  if (a == 0) return kNone;
  return Rect{cx - (int)rx - 1, (int)(cy - ry) - 1, cx + (int)rx + 2, (int)(cy + ry) + 2};
}

inline bool quiet(int x, int y) { return x >= LS_QUIET_X0 && y >= LS_QUIET_Y0; }

void plot(Surface& s, const Rect& clip, int x, int y, Pixel c, uint8_t a) {
  if (x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1 || quiet(x, y)) return;
  Pixel* p = s.row(y) + x;
  *p = blend565(*p, c, a);
}

void drawRain(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Weather& x = w.weather;
  const float rain = (float)x.rain_q / 16.f;
  const int n = (int)(rain * 40.f + 0.5f);
  const float slant = 0.3f + 0.45f * fabsf(x.wind) + (x.kind == WX_STORM ? 0.1f : 0.f);
  const Pixel sky = mixRGB(pal[CLOUD_SHADE], pal[WATER_LIGHT], 0.35f);
  const Pixel low = pal[WATER_LIGHT];
  for (int st = clip.y0 / SS_STRIP_ROWS; st * SS_STRIP_ROWS < clip.y1 && st < SS_STRIP_COUNT; ++st) {
    const int y0 = st * SS_STRIP_ROWS;
    const uint32_t step = (w.tick + (uint32_t)st) / 3u;
    for (int j = 0; j < n; ++j) {
      const uint32_t key = step * 131u + (uint32_t)st;
      const int sx = (int)(hash01(key, (uint32_t)j * 3u) * 840.f) - 20;
      const int sy = y0 + (int)(hash01(key, (uint32_t)j * 3u + 1u) * (float)SS_STRIP_ROWS);
      const int len = 4 + (int)(hash01(key, (uint32_t)j * 3u + 2u) * (3.f + 5.f * rain));
      for (int i = 0; i < len; ++i) {
        const int py = sy + i;
        if (py >= y0 + SS_STRIP_ROWS) break;
        const int px = sx - (int)((float)i * slant);
        plot(s, clip, px, py, py < LS_GROUND_Y ? sky : low, (uint8_t)(i == 0 || i == len - 1 ? 70 : 135));
      }
    }
  }
}

void drawSnow(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Weather& x = w.weather;
  const int n = (int)((float)x.snow_q / 16.f * 150.f + 0.5f);
  const Pixel c = pal[WATER_LIGHT];
  for (int st = clip.y0 / SS_STRIP_ROWS; st * SS_STRIP_ROWS < clip.y1 && st < SS_STRIP_COUNT; ++st) {
    const int y0 = st * SS_STRIP_ROWS, y1 = y0 + SS_STRIP_ROWS;
    const uint32_t ts = ((w.tick + (uint32_t)st) / 3u) * 3u;   // this strip's last step
    const float secs = (float)ts / (float)SS_TICK_HZ;
    for (int j = 0; j < n; ++j) {
      const float h0 = hash01(0x534Eu, (uint32_t)j * 4u), h1 = hash01(0x534Eu, (uint32_t)j * 4u + 1u);
      const float h2 = hash01(0x534Eu, (uint32_t)j * 4u + 2u), h3 = hash01(0x534Eu, (uint32_t)j * 4u + 3u);
      const float speed = 11.f + 12.f * h1;
      float fy = h0 * 500.f + speed * secs;
      fy -= 500.f * floorf(fy / 500.f);
      const int iy = (int)fy - 10;
      if (iy + 2 <= y0 || iy >= y1) continue;
      float fx = h2 * 840.f + x.wind * 9.f * secs + (5.f + 8.f * h3) * sinT(h3 + secs * (0.12f + 0.1f * h1));
      fx -= 840.f * floorf(fx / 840.f);
      const int ix = (int)fx - 20;
      // Two sizes: a 2 px dot, or (the nearer third) a 3 px one with soft corners.
      const uint8_t a = (uint8_t)(170 + (int)(80.f * h3));
      if (h3 < 0.66f) {
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) plot(s, clip, ix + dx, iy + dy, c, a);
      } else {
        for (int dy = 0; dy < 3; ++dy)
          for (int dx = 0; dx < 3; ++dx) plot(s, clip, ix + dx, iy + dy, c, (dx == 1 || dy == 1) ? a : (uint8_t)(a / 3));
      }
    }
  }
}

}  // namespace

namespace {
// Shift pixels [x0, x1) of a row by dx, the vacated end filled from its edge.
void shiftRun(Pixel* row, int x0, int x1, int dx) {
  const int n = x1 - x0;
  if (dx == 0 || n <= 4) return;
  if (dx > 0) {
    if (dx >= n) return;
    memmove(row + x0 + dx, row + x0, (size_t)(n - dx) * sizeof(Pixel));
    for (int i = 0; i < dx; ++i) row[x0 + i] = row[x0 + dx];
  } else {
    const int d = -dx;
    if (d >= n) return;
    memmove(row + x0, row + x0 + d, (size_t)(n - d) * sizeof(Pixel));
    for (int i = 0; i < d; ++i) row[x1 - 1 - i] = row[x1 - 1 - d];
  }
}
}  // namespace

void weatherDrawShimmer(const World& w, Surface& s, const Rect& clip) {
  if (!shimmerOn(w)) return;
  const int ya = clip.y0 > kShimY0 ? clip.y0 : kShimY0;
  const int yb = clip.y1 < LS_HORIZON ? clip.y1 : LS_HORIZON;
  if (ya >= yb) return;
  // The near pines stand in front of the haze: their columns keep still.
  Rect skip[3] = {landscapeCampPineBounds(w.land), landscapeFgPineBounds(w.land, 0), landscapeFgPineBounds(w.land, 1)};
  for (int i = 1; i < 3; ++i)   // in x order
    for (int j = i; j > 0 && skip[j].x0 < skip[j - 1].x0; --j) {
      const Rect t = skip[j];
      skip[j] = skip[j - 1];
      skip[j - 1] = t;
    }
  for (int y = ya; y < yb; ++y) {
    const int dx = shimmerDx(w, y - kShimY0);
    if (dx == 0) continue;
    Pixel* row = s.row(y);
    int x = clip.x0;
    for (const Rect& k : skip) {
      if (k.y0 > y || k.y1 <= y || k.x1 <= x) continue;
      const int e = k.x0 < clip.x1 ? k.x0 : clip.x1;
      if (e > x) shiftRun(row, x, e, dx);
      x = k.x1;
      if (x >= clip.x1) break;
    }
    if (x < clip.x1) shiftRun(row, x, clip.x1, dx);
  }
}

void weatherDrawFog(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!fogOn(w)) return;
  const Pixel c = pal[SKY4];
  for (int i = 0; i < WX_BANDS; ++i) {
    int cx;
    float cy, rx, ry;
    uint8_t a;
    bandGeom(w, i, cx, cy, rx, ry, a);
    if (a == 0) continue;
    const int ya = (int)(cy - ry), yb = (int)(cy + ry) + 1;
    for (int y = ya < clip.y0 ? clip.y0 : ya; y < yb && y < clip.y1; ++y) {
      const float v = ((float)y + 0.5f - cy) / ry;
      if (v <= -1.f || v >= 1.f) continue;
      const float shape = sqrtf(1.f - v * v);
      const float ar = (float)a * (1.f - v * v);
      // Three nested spans: soft ends without per-pixel alpha.
      static const float kW[3] = {1.f, 0.72f, 0.45f};
      for (float kw : kW) {
        const int hw = (int)(rx * shape * kw);
        int xa = cx - hw, xb = cx + hw;
        if (xa < clip.x0) xa = clip.x0;
        if (xb > clip.x1) xb = clip.x1;
        if (y >= LS_QUIET_Y0 && xb > LS_QUIET_X0) xb = LS_QUIET_X0;
        const uint8_t al = (uint8_t)(ar * 0.34f);
        if (xb > xa && al > 0) blendSpan(s, y, xa, xb, c, al);
      }
    }
    // The near pines stand in front of the fog.
    const Rect b = Rect{cx - (int)rx - 1, ya, cx + (int)rx + 2, yb}.clipped(clip);
    if (!b.empty()) landscapeDrawNearPines(w.land, s, b, pal);
  }
}

void weatherDrawNear(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const Weather& x = w.weather;
  Vec2 bc;
  float br;
  if (breathGeom(x.breath, bc, br) && !breathBounds(x.breath).clipped(clip).empty())
    fillEllipse(s, bc, 1.3f * br, br, pal[SMOKE], clip);
  if (x.hiss.kind != GL_NONE) glyphDraw(x.hiss, s, clip, pal);
  if (x.rain_q > 0) drawRain(w, s, clip, pal);
  if (x.snow_q > 0) drawSnow(w, s, clip, pal);
}

void weatherTrack(World& w, Renderer& r) {
  Weather& x = w.weather;
  // Rain and snow: the strips whose step changed this tick; everything when the amount did.
  if (x.rain_q != x.d_rain_q || x.snow_q != x.d_snow_q) {
    r.markAllDirty();
    x.d_rain_q = x.rain_q;
    x.d_snow_q = x.snow_q;
  } else if (x.rain_q > 0 || x.snow_q > 0) {
    for (int st = 0; st < SS_STRIP_COUNT; ++st)
      if ((w.tick + (uint32_t)st) % 3u == 0u) r.markDirtyRows(st * SS_STRIP_ROWS, (st + 1) * SS_STRIP_ROWS);
  }
  // The shimmer's rows, when an offset changed.
  bool shim = false;
  const bool on = shimmerOn(w);
  for (int i = 0; i < kShimRows; ++i) {
    const int8_t dx = on ? (int8_t)shimmerDx(w, i) : (int8_t)0;
    if (dx != x.d_shim[i]) shim = true;
    x.d_shim[i] = dx;
  }
  if (shim) r.markDirtyRows(kShimY0, LS_HORIZON);
  // Fog bands.
  for (int i = 0; i < WX_BANDS; ++i) {
    int cx = 0;
    float cy, rx, ry;
    uint8_t a = 0;
    Rect cur = kNone;
    if (fogOn(w)) {
      bandGeom(w, i, cx, cy, rx, ry, a);
      cur = bandBounds(w, i);
    }
    const bool changed = cx != x.d_band_x[i] || a != x.d_band_a[i];
    track(r, x.d_band[i], cur, changed);
    x.d_band_x[i] = (int16_t)cx;
    x.d_band_a[i] = a;
  }
  for (int i = 0; i < WX_BITS; ++i) {
    track(r, x.d_bit[i], bitBounds(x.bit[i]), !bitSame(x.bit[i], x.d_bit_s[i]));
    x.d_bit_s[i] = x.bit[i];
  }
  track(r, x.d_breath, breathBounds(x.breath), !steamSame(x.breath, x.d_breath_s));
  x.d_breath_s = x.breath;
  const Rect hb = x.hiss.kind != GL_NONE ? glyphBounds(x.hiss) : kNone;
  const bool hs = x.hiss.kind == x.d_hiss_s.kind && x.hiss.scale == x.d_hiss_s.scale && x.hiss.k == x.d_hiss_s.k &&
                  x.hiss.pos.x == x.d_hiss_s.pos.x && x.hiss.pos.y == x.d_hiss_s.pos.y;
  track(r, x.d_hiss, hb, !hs);
  x.d_hiss_s = x.hiss;
}

}  // namespace ss
