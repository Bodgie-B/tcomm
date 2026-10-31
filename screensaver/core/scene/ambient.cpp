// The ambient layer. See ambient.h.
//
// Every motion is a sum of sines on a clock, with frequencies kept well under
// the 12.5 Hz Nyquist limit of a 25 Hz tick (and of the 12.5 Hz grass sub-rate)
// so nothing strobes. Shapes are flat palette-slot pools; no alpha, no glow.
#include "ambient.h"

#include <math.h>

#include "../anim/ease.h"
#include "../gfx/raster.h"
#include "world.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

const Rect kEmpty{0, 0, 0, 0};

// Wind blows toward -x, the way the clouds drift: grass leans left, smoke
// trails left, gusts travel right to left across the field.
constexpr float kGustTurnsPerSec = 0.32f;   // top octave 0.32 * 4.1 = 1.3 Hz
constexpr float kGustPerPx = 0.0035f;       // gust wavelength ~290 px

// Stream flow speed at a 20 px half width; wider water flows slower.
constexpr float kFlowPxPerSec = 26.f;
constexpr float kGlintWrapAt = 12.9f;       // past the left edge (x ~ -15)
constexpr float kGlintWrapBy = 12.4f;       // back to just right of the right edge

constexpr float kSmokeLife = 7.f;           // seconds per puff
constexpr float kSmokeRise = 55.f;          // px over a puff's life; puffs overlap into one column

constexpr float kBirdHalfSpan = 5.5f;       // V is ~11 px wide

void updateSway(World& w) {
  Ambient& a = w.amb;
  const float gust = 0.75f + 0.25f * sinT(a.wind_t * 0.05f);   // slow swell, 20 s
  for (int i = 0; i < a.n_tufts; ++i) {
    const float x = fl(w.land.tufts[a.tuft[i]].x);
    a.sway[i] = gust * (-0.8f + 2.6f * sumSines(a.wind_t * kGustTurnsPerSec + x * kGustPerPx, 3));
  }
}

// Pick the swaying tufts by farthest-point sampling (y weighted double, the
// field is wide and shallow), so the sway reads everywhere: clearing edge,
// stream banks, foreground. Tufts that could lean into the reserved corner or
// over the mushroom are skipped. No randomness: the tufts are already seeded.
void pickTufts(World& w) {
  Landscape& ls = w.land;
  Ambient& a = w.amb;
  bool cand[LS_MAX_TUFTS];
  float best[LS_MAX_TUFTS];
  int first = -1;
  for (int i = 0; i < ls.n_tufts; ++i) {
    const LsTuft& t = ls.tufts[i];
    const float mx = fl(t.x - ls.mush_x), my = fl(t.y - ls.mush_y);
    cand[i] = !(t.x > LS_QUIET_X0 - 35 && t.y > LS_QUIET_Y0 - 20) && (mx * mx + my * my > 18.f * 18.f);
    best[i] = 3.0e38f;
    if (cand[i] && (first < 0 || t.x < ls.tufts[first].x)) first = i;
  }
  a.n_tufts = 0;
  int pick = first;
  while (pick >= 0 && a.n_tufts < AMB_TUFT_MAX) {
    a.tuft[a.n_tufts++] = (uint8_t)pick;
    ls.tufts[pick].dyn = 1;
    cand[pick] = false;
    const float px = fl(ls.tufts[pick].x), py = fl(ls.tufts[pick].y);
    pick = -1;
    float far = -1.f;
    for (int i = 0; i < ls.n_tufts; ++i) {
      if (!cand[i]) continue;
      const float dx = fl(ls.tufts[i].x) - px, dy = 2.f * (fl(ls.tufts[i].y) - py);
      best[i] = minf(best[i], dx * dx + dy * dy);
      if (best[i] > far) { far = best[i]; pick = i; }
    }
  }
}

bool glintGeom(const World& w, int i, Vec2& a, Vec2& b) {
  const StreamGlint& g = w.amb.glint[i];
  const float reveal = buildStreamReveal(w.build);
  if (reveal <= 0.f) return false;
  float vis = 1.f;
  if (reveal < 1.f) vis = clamp01((landscapeStreamTip(reveal) - LS_STREAM_TAPER - g.s) / 0.4f);
  const float len = g.len * (0.7f + 0.3f * sinT(g.phase)) * vis;
  if (len < 1.f) return false;
  const LsStreamPt q = landscapeStreamAt(w.land, g.s);
  const LsStreamPt q2 = landscapeStreamAt(w.land, g.s + 0.05f);
  float tx = q2.x - q.x, ty = q2.y - q.y;
  float tl = sqrtf(tx * tx + ty * ty);
  if (tl < 1e-4f) tl = 1.f;
  tx /= tl;
  ty /= tl;
  const float off = (g.lane + 0.04f * sinT(g.phase * 0.5f)) * q.hw;
  const float cx = q.x + q.nx * off, cy = q.y + q.ny * off;
  a = Vec2{cx - tx * len * 0.5f, cy - ty * len * 0.5f};
  b = Vec2{cx + tx * len * 0.5f, cy + ty * len * 0.5f};
  return true;
}

bool smokeGeom(const World& w, int i, Vec2& c, float& r) {
  const FirePit& f = w.fire;
  if (f.intensity <= 0.05f) return false;
  float age = w.amb.wind_t / kSmokeLife + fl(i) / fl(AMB_SMOKE_MAX);
  age -= floorf(age);
  // Grows fast, then shrinks away as it rises: fading without alpha.
  r = 13.f * minf(1.f, age * 3.f) * (1.f - age) * f.intensity * w.amb.smoke_k;
  if (r < 0.6f) return false;
  const float drift = -40.f * age * sqrtf(age);
  const float pulled = (w.amb.smoke_to_x - f.pos.x) * minf(1.f, age * 1.8f);
  c.x = f.pos.x + lerp(drift, pulled, w.amb.smoke_pull) + 3.f * sinT(age * 1.3f + fl(i) * 0.27f);
  c.y = f.pos.y - 24.f - kSmokeRise * age;
  return true;
}

void birdGeom(const Bird& b, Vec2& l, Vec2& r) {
  // Wing angle in turns: flapping, with glides where the gate closes.
  const float gate = clamp01(0.4f + sinT(b.x / 170.f + b.path_ph));
  const float ang = 0.045f + 0.05f * sinT(b.flap) * gate;
  const float dx = kBirdHalfSpan * cosT(ang), dy = kBirdHalfSpan * sinT(ang);
  l = Vec2{b.x - dx, b.y - dy};
  r = Vec2{b.x + dx, b.y - dy};
}

Rect boundsOf(float x0, float y0, float x1, float y1) {
  return Rect{(int)floorf(x0), (int)floorf(y0), (int)ceilf(x1), (int)ceilf(y1)};
}

// One flame pool: a flat polygon whose top edge is four tongues and three
// valleys, every height and lean from its own sum of sines (top octave
// 1.05 * 4.1 = 4.3 Hz, a lively flicker that a 25 Hz tick can show).
void flamePool(Surface& s, Vec2 base, float bw, float H, float clock, float ph, Pixel col,
               const Rect& clip) {
  const int T = 4;
  Vec2 p[2 * T + 4];
  int n = 0;
  p[n++] = Vec2{base.x - bw, base.y - 1.f};
  for (int j = 0; j < T; ++j) {
    const float u = (fl(j) + 0.5f) / fl(T);
    const float env = 1.f - 0.5f * fabsf(2.f * u - 1.f);   // taller in the middle
    const float hj = H * env * (0.62f + 0.38f * sumSines(clock * 1.05f + fl(j) * 0.29f + ph, 3));
    const float lean = (2.4f * sumSines(clock * 0.7f + fl(j) * 0.53f + ph, 2) - 1.f) * (hj / 32.f);
    p[n++] = Vec2{base.x - bw + 2.f * bw * u + lean, base.y - hj};
    if (j + 1 < T) {
      const float vu = fl(j + 1) / fl(T);
      const float vh = H * (0.3f + 0.08f * sumSines(clock * 1.3f + fl(j) + ph, 2));
      p[n++] = Vec2{base.x - bw + 2.f * bw * vu, base.y - vh};
    }
  }
  p[n++] = Vec2{base.x + bw, base.y - 1.f};
  p[n++] = Vec2{base.x + bw * 0.6f, base.y + 1.5f};
  p[n++] = Vec2{base.x - bw * 0.6f, base.y + 1.5f};
  fillPolygon(s, p, n, col, clip);
}

// Ring of six stones on an ellipse round the pit; laid in this order.
const float kStoneAng[6] = {0.50f, 0.66f, 0.84f, 0.02f, 0.18f, 0.34f};   // turns; sin > 0 is the front

void drawStones(const FirePit& f, Surface& s, const Rect& clip, Pixel col, bool front) {
  for (int k = 0; k < 6; ++k) {
    const float sn = sinT(kStoneAng[k]);
    if ((sn > 0.f) != front) continue;
    const float d = 0.1f * fl(k);
    const float sc = easeOutBack(window(f.stones, d, d + 0.4f));
    if (sc <= 0.01f) continue;
    const float sx = f.pos.x + 18.f * cosT(kStoneAng[k]);
    const float sy = f.pos.y + 5.f * sn;
    fillEllipse(s, Vec2{sx, sy - 1.7f * sc}, 5.f * sc, 3.4f * sc, col, clip);
  }
}

}  // namespace

void ambientInit(World& w, Rng& rng) {
  Ambient& a = w.amb;
  a.wind_t = 0.f;
  a.smoke_pull = a.smoke_pull_to = 0.f;
  a.smoke_to_x = w.fire.pos.x;
  a.smoke_k = a.smoke_k_to = 1.f;
  pickTufts(w);
  updateSway(w);
  for (int i = 0; i < AMB_GLINT_MAX; ++i) {
    StreamGlint& g = a.glint[i];
    g.s = 0.4f + kGlintWrapBy * (fl(i) + rng.range(0.f, 0.6f)) / fl(AMB_GLINT_MAX);
    g.lane = rng.range(-0.3f, 0.3f);   // inside rocks and bank tufts that reach into the water
    g.len = rng.range(9.f, 18.f);
    g.phase = rng.uniform();
  }
  Bird& b = a.bird;
  b.active = false;
  b.x = b.y = b.y0 = 0.f;
  b.dir = 1.f;
  b.speed = 0.f;
  b.path_ph = 0.f;
  b.flap = 0.f;
  b.wait = (uint32_t)(rng.range(15.f, 45.f) * (float)SS_TICK_HZ);   // counted from the end of the build
}

bool ambientSubTick(const World& w) { return (w.tick & 1u) == 0u; }

void ambientStep(World& w, Rng& rng) {
  Ambient& a = w.amb;
  const float dt = SS_TICK_DT;

  if (ambientSubTick(w)) {
    a.wind_t += 2.f * dt;
    updateSway(w);
    // Smoke bias moves on the sub-rate with the puffs, so their dirty marks cover it.
    const float step = 2.f * dt / 1.2f;
    const float d = a.smoke_pull_to - a.smoke_pull;
    a.smoke_pull += d > step ? step : (d < -step ? -step : d);
    const float kstep = 2.f * dt / 3.f;   // about 3 s to stop
    const float dk = a.smoke_k_to - a.smoke_k;
    a.smoke_k += dk > kstep ? kstep : (dk < -kstep ? -kstep : dk);
  }

  for (int i = 0; i < AMB_GLINT_MAX; ++i) {
    StreamGlint& g = a.glint[i];
    int k = (int)g.s;
    if (k > LS_STREAM_PTS - 2) k = LS_STREAM_PTS - 2;
    const LsStreamPt& p0 = w.land.stream[k];
    const LsStreamPt& p1 = w.land.stream[k + 1];
    const float seg = sqrtf((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
    const float hw = maxf(10.f, landscapeStreamAt(w.land, g.s).hw);
    const float px_per_tick = kFlowPxPerSec * (20.f / hw) * dt;
    g.s += px_per_tick / maxf(seg, 1.f);
    if (g.s > kGlintWrapAt) g.s -= kGlintWrapBy;
    g.phase += dt * 0.45f;
    if (g.phase >= 1.f) g.phase -= 1.f;
  }

  // Fire eases toward its state: about 1.8 s to catch, slower to die down.
  // Kindling answers each blow within a third of a second and fades in one.
  FirePit& f = w.fire;
  const bool kindling = f.state == FIRE_KINDLING;
  const float target = f.state == FIRE_LIT ? f.fuel : (f.state == FIRE_EMBERS ? 0.25f : (kindling ? f.kindle : 0.f));
  const float rise = kindling ? 0.35f : 1.8f, fall = kindling ? 1.0f : 4.f;
  if (f.intensity < target) f.intensity = minf(target, f.intensity + dt / rise);
  else if (f.intensity > target) f.intensity = maxf(target, f.intensity - dt / fall);
  if (f.intensity > 0.f) f.clock += dt;

  // The bird waits for the build to finish, then crosses every 40 to 90 s
  // (start to start; a crossing takes 20 to 30 s).
  Bird& b = a.bird;
  if (!buildDone(w.build)) return;
  if (b.wait > 0) --b.wait;
  if (!b.active && b.wait == 0) {
    b.active = true;
    b.dir = rng.chance(0.5f) ? 1.f : -1.f;
    b.x = b.dir > 0.f ? -14.f : fl(SS_WIDTH) + 14.f;
    b.y0 = rng.range(70.f, 140.f);   // under 160: the camp pine's tip is at y 170
    b.speed = rng.range(28.f, 42.f) * dt;
    b.path_ph = rng.uniform();
    b.flap = 0.f;
    b.wait = (uint32_t)(rng.range(40.f, 90.f) * (float)SS_TICK_HZ);
  }
  if (b.active) {
    b.x += b.dir * b.speed;
    b.flap += dt * 2.6f;   // wing beats per second
    if (b.flap >= 1.f) b.flap -= 1.f;
    b.y = b.y0 + 9.f * sinT(b.x / 320.f + b.path_ph);
    if (b.x < -16.f || b.x > fl(SS_WIDTH) + 16.f) b.active = false;
  }
}

Rect ambientTuftBounds(const World& w, int i) {
  const LsTuft& t = w.land.tufts[w.amb.tuft[i]];
  if (buildPlantScale(w.build, fl(t.x), fl(t.y)) <= 0.01f) return kEmpty;
  // off +-4, lean +-4, sway up to ~4.4 at full height, 1.5 px half base, overshoot 1.1
  return Rect{t.x - 16, t.y - 22, t.x + 16, t.y + 2};
}

Rect ambientGlintBounds(const World& w, int i) {
  Vec2 a, b;
  if (!glintGeom(w, i, a, b)) return kEmpty;
  return boundsOf(minf(a.x, b.x) - 2.f, minf(a.y, b.y) - 2.f, maxf(a.x, b.x) + 2.f, maxf(a.y, b.y) + 2.f);
}

Rect ambientBirdBounds(const World& w) {
  const Bird& b = w.amb.bird;
  if (!b.active) return kEmpty;
  return boundsOf(b.x - 8.f, b.y - 8.f, b.x + 8.f, b.y + 4.f);
}

Rect ambientSmokeBounds(const World& w, int i) {
  Vec2 c{0.f, 0.f};
  float r = 0.f;
  if (!smokeGeom(w, i, c, r)) return kEmpty;
  return boundsOf(c.x - r - 2.f, c.y - r - 2.f, c.x + r + 2.f, c.y + r + 2.f);
}

Rect fireBounds(const FirePit& f) {
  if (f.stones <= 0.f && f.intensity <= 0.f) return kEmpty;
  const int x = (int)floorf(f.pos.x), y = (int)floorf(f.pos.y);
  return Rect{x - 27, y - 40, x + 28, y + 10};
}

void ambientDrawTuft(const World& w, int i, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(ambientTuftBounds(w, i), clip)) return;
  const LsTuft& t = w.land.tufts[w.amb.tuft[i]];
  const float hs = buildPlantScale(w.build, fl(t.x), fl(t.y));
  const float y = fl(t.y);
  for (int k = 0; k < t.n; ++k) {
    const float H = fl(t.h[k]) * hs;
    const float sw = w.amb.sway[i] * (fl(t.h[k]) / 18.f) * hs;
    const float bx = fl(t.x + t.off[k]);
    const float lean = fl(t.lean[k]) * hs;
    // Bent blade: the top moves most, the middle a third as much.
    const float mx = bx + lean * 0.5f + sw * 0.3f, my = y - 0.5f * H;
    const Vec2 blade[5] = {{bx - 1.5f, y}, {bx + 1.5f, y}, {mx + 0.8f, my}, {bx + lean + sw, y - H},
                           {mx - 0.8f, my}};
    fillPolygon(s, blade, 5, pal[GRASS], clip);
  }
}

void ambientDrawGlints(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  for (int i = 0; i < AMB_GLINT_MAX; ++i) {
    Vec2 a, b;
    if (!glintGeom(w, i, a, b)) continue;
    const Rect r = boundsOf(minf(a.x, b.x) - 2.f, minf(a.y, b.y) - 2.f, maxf(a.x, b.x) + 2.f, maxf(a.y, b.y) + 2.f);
    if (!overlaps(r, clip)) continue;
    strokeCapsule(s, a, b, 2.f, pal[WATER_LIGHT], clip);
  }
}

void ambientDrawBird(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(ambientBirdBounds(w), clip)) return;
  const Bird& b = w.amb.bird;
  Vec2 l, r;
  birdGeom(b, l, r);
  strokeCapsule(s, Vec2{b.x, b.y}, l, 1.7f, pal[TREES_FAR], clip);
  strokeCapsule(s, Vec2{b.x, b.y}, r, 1.7f, pal[TREES_FAR], clip);
}

void ambientDrawSmoke(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  for (int i = 0; i < AMB_SMOKE_MAX; ++i) {
    if (!overlaps(ambientSmokeBounds(w, i), clip)) continue;
    Vec2 c{0.f, 0.f};
    float r = 0.f;
    smokeGeom(w, i, c, r);
    fillEllipse(s, c, r, r * 0.85f, pal[SMOKE], clip);
  }
}

void fireDraw(const FirePit& f, Surface& s, const Rect& clip, const Palette& pal) {
  if (!overlaps(fireBounds(f), clip)) return;
  drawStones(f, s, clip, pal[ROCK], false);
  if (f.ash > 0.f && f.stones > 0.f) {   // last night's ash: a low grey mound, a dark char log across it
    const float k = f.ash * f.stones;
    fillEllipse(s, Vec2{f.pos.x, f.pos.y - 1.5f * k}, 10.f * k, 3.6f * k, mixRGB(pal[ROCK], pal[SMOKE], 0.55f), clip);
    strokeCapsule(s, Vec2{f.pos.x - 6.f * k, f.pos.y - 1.f * k}, Vec2{f.pos.x + 5.f * k, f.pos.y - 3.2f * k}, 1.6f * k,
                  mixRGB(pal[TRUNK], pal[ACTOR], 0.4f), clip);
  }
  if (f.intensity > 0.03f) {
    const float I = f.intensity;
    const float H = 32.f * I, bw = 10.f + 3.f * I;   // about a third of the hiker's height
    flamePool(s, f.pos, bw, H, f.clock, 0.f, pal[FIRE], clip);
    flamePool(s, Vec2{f.pos.x, f.pos.y - 0.5f}, bw * 0.55f, H * 0.5f, f.clock, 0.5f, pal[FIRE_CORE], clip);
  }
  drawStones(f, s, clip, pal[ROCK], true);
}

}  // namespace ss
