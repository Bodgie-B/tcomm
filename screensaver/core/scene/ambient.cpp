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

// The big blaze (fire and ice, the cold). Only the cold's fuel goes over 1. Over 1 the fire is drawn
// wild: much taller, five uneven tongues that flick up and down on their own, the whole fire leaning
// with gusts, licks of flame leaping off the top, sparks flying high, and a soft glow round it. All
// from the fire's clock: no state. (fireice.cpp kBlazeMild / kBlazeUrgent set the fuel.)
constexpr float kBlazeTall = 3.2f;      // extra height per unit of fuel over 1, times the 32 px fire
constexpr float kBlazeWide = 16.f;      // extra half width (px) per unit of fuel over 1
constexpr float kBlazeGust = 0.30f;     // how far the tips lean with a gust, times their height
constexpr float kBlazeSparkUp = 1.45f;  // the sparks' highest point, times the blaze's height
constexpr float kBlazeGlowW = 2.0f;     // the glow's half width, times the flames' half width
constexpr int kBlazeGlowA = 30;         // each of the glow's three rings, of 255 (the middle gets all three)
constexpr int kBlazeLicks = 3, kBlazeSparks = 16;

inline float fracf(float x) { return x - floorf(x); }
inline float blazeWild(float I) { return minf(1.f, (I - 1.f) / 0.6f); }
inline float blazeH(float I) { return 32.f * (1.f + kBlazeTall * (I - 1.f)); }
inline float blazeBW(float I) { return 13.f + kBlazeWide * (I - 1.f); }
// The gust's lean, times kBlazeGust times wildness: slow, so the fire leans and comes back. Mostly
// downwind (toward -x, as the smoke trails), now and then a moment upright or a little the other way.
inline float blazeGust(float clock, float wild) {
  return -kBlazeGust * wild * (0.35f + 0.65f * sumSines(clock * 0.31f + 0.2f, 3));
}

void updateSway(World& w) {
  Ambient& a = w.amb;
  // Slow swell, 20 s; the weather's wind scales it (exactly 1 when calm).
  const float gust = (0.75f + 0.25f * sinT(a.wind_t * 0.05f)) * weatherSwayMul(w);
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
  // More in the rain; a big blaze (fire and ice) only a little more than a good fire.
  r = 13.f * minf(1.f, age * 3.f) * (1.f - age) * minf(f.intensity, 1.1f) * w.amb.smoke_k * weatherSmokeMul(w);
  if (r < 0.6f) return false;
  const float drift = -40.f * age * sqrtf(age);
  const float pulled = (w.amb.smoke_to_x - f.pos.x) * minf(1.f, age * 1.8f);
  c.x = f.pos.x + lerp(drift, pulled, w.amb.smoke_pull) + 3.f * sinT(age * 1.3f + fl(i) * 0.27f);
  // Above a big blaze too (fireice.h): 24 px for a good fire, and most of what the blaze adds.
  c.y = f.pos.y - (f.intensity > 1.f ? 24.f + 0.95f * (blazeH(f.intensity) - 32.f) : 24.f) - kSmokeRise * age;
  return true;
}

Rect boundsOf(float x0, float y0, float x1, float y1) {
  return Rect{(int)floorf(x0), (int)floorf(y0), (int)ceilf(x1), (int)ceilf(y1)};
}

void birdGeom(const Bird& b, Vec2& l, Vec2& r) {
  // Wing angle in turns: flapping, with glides where the gate closes.
  float gate = clamp01(0.4f + sinT(b.x / 170.f + b.path_ph));
  if (b.glide > 0.f) gate *= 1.f - 0.8f * b.glide;
  const float ang = 0.045f + 0.05f * sinT(b.flap) * gate;
  const float span = kBirdHalfSpan * b.scale;
  const float dx = span * cosT(ang), dy = span * sinT(ang);
  l = Vec2{b.x - dx, b.y - dy};
  r = Vec2{b.x + dx, b.y - dy};
}

// The partner of a pair: the same bird, offset, a little out of step, its own size.
Bird partnerOf(const Bird& b) {
  Bird p = b;
  p.x = b.x + b.pdx * b.scale;
  p.y = b.y + b.pdy * b.scale;
  p.flap = b.flap + 0.37f;
  p.flap -= floorf(p.flap);
  p.scale = b.scale * b.pscale;
  p.pair = false;
  return p;
}

Rect birdRect(const Bird& b) {
  const float s = b.scale;
  return boundsOf(b.x - 8.f * s, b.y - 8.f * s, b.x + 8.f * s, b.y + 4.f * s);
}

void drawOneBird(const Bird& b, Surface& s, const Rect& clip, const Palette& pal) {
  Vec2 l, r;
  birdGeom(b, l, r);
  const float w = b.scale < 1.f ? maxf(1.f, 1.7f * b.scale) : 1.7f * b.scale;
  strokeCapsule(s, Vec2{b.x, b.y}, l, w, pal[(Slot)b.slot], clip);
  strokeCapsule(s, Vec2{b.x, b.y}, r, w, pal[(Slot)b.slot], clip);
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

// ---- the big blaze (fire and ice, the cold) ------------------------------------------------------
// Everything it draws comes from these geometry functions, so fireBounds can fold the very same
// points and the dirty box is no bigger than what is drawn.
struct Blaze {
  Vec2 b;          // the pit
  float H, bw;     // height and half width
  float gust;      // the lean, times a height
  float wild;      // 0..1
  float t;         // the fire's clock
};
Blaze blazeOf(const FirePit& f) {
  const float I = f.intensity;
  return Blaze{f.pos, blazeH(I), blazeBW(I), blazeGust(f.clock, blazeWild(I)), blazeWild(I), f.clock};
}

// The three flame layers, back to front: a deep red edge, orange, a yellow heart.
struct BlazeLayer { float bw, H, dy, ph; };
const BlazeLayer kBlazeLayers[3] = {{1.08f, 1.f, 0.f, 0.f}, {0.85f, 0.86f, -0.5f, 0.13f}, {0.48f, 0.5f, -1.f, 0.5f}};
constexpr int kBlazeTongues = 5, kBlazePts = 2 * kBlazeTongues + 3;

// One wild flame layer's outline: five tongues, each its own height (0.44..1 of the envelope) and
// lean, all sheared by the gust. A flat polygon like flamePool, just more of it.
int blazeLayerPts(const Blaze& z, int k, Vec2* p) {
  const BlazeLayer& L = kBlazeLayers[k];
  const Vec2 base{z.b.x, z.b.y + L.dy};
  const float bw = z.bw * L.bw, H = z.H * L.H, ph = L.ph, t = z.t;
  int n = 0;
  p[n++] = Vec2{base.x - bw, base.y - 1.f};
  for (int j = 0; j < kBlazeTongues; ++j) {
    const float u = (fl(j) + 0.5f) / fl(kBlazeTongues);
    const float env = 1.f - 0.55f * fabsf(2.f * u - 1.f);
    const float jump = sumSines(t * 1.45f + fl(j) * 0.41f + ph, 3);
    const float hj = H * env * (0.72f + 0.28f * jump);
    const float lean = (2.f * sumSines(t * 0.9f + fl(j) * 0.61f + ph, 2)) * (hj / 9.f) + z.gust * hj;
    p[n++] = Vec2{base.x - bw + 2.f * bw * u + lean, base.y - hj};
    if (j + 1 < kBlazeTongues) {
      const float vu = fl(j + 1) / fl(kBlazeTongues);
      const float vh = H * (0.28f + 0.1f * sumSines(t * 1.2f + fl(j) + ph, 2));
      p[n++] = Vec2{base.x - bw + 2.f * bw * vu + z.gust * vh, base.y - vh};
    }
  }
  p[n++] = Vec2{base.x + bw, base.y - 1.f};
  p[n++] = Vec2{base.x + bw * 0.6f, base.y + 1.5f};
  p[n++] = Vec2{base.x - bw * 0.6f, base.y + 1.5f};
  return n;
}

// A lick: a tongue of flame torn off the top, leaping up and shrinking. False while it is not out.
bool blazeLick(const Blaze& z, int i, Vec2* q) {
  const float u = fracf(z.t / (0.62f + 0.17f * fl(i)) + 0.37f * fl(i));
  const float r = (1.f - u) * (2.5f + 3.f * z.wild);
  if (u > 0.85f || r < 0.9f) return false;
  const float up = z.H * (0.62f + 0.6f * u);
  const float x = z.b.x + z.bw * 0.45f * fl(i - 1) + z.gust * up + 3.f * sinT(u * 0.8f + 0.3f * fl(i));
  const float y = z.b.y - up;
  q[0] = Vec2{x, y - 2.8f * r};
  q[1] = Vec2{x + r, y};
  q[2] = Vec2{x, y + 1.3f * r};
  q[3] = Vec2{x - r, y};
  return true;
}

// A spark: a bright dot flying up well above the flames, drifting with the gust.
bool blazeSpark(const Blaze& z, int i, Vec2& c, float& r, bool& hot) {
  const float per = 1.25f + 0.17f * fl(i % 5);
  const float u = fracf(z.t / per + 0.618f * fl(i));
  if (u > 0.93f) return false;
  const float up = z.H * (0.45f + (kBlazeSparkUp - 0.45f) * u);
  const float sx = z.bw * 0.7f * (2.f * fracf(0.37f * fl(i) + 0.11f) - 1.f);
  c = Vec2{z.b.x + sx + 0.6f * z.gust * up + 5.f * sinT(u * 1.3f + 0.27f * fl(i)), z.b.y - up};
  r = u < 0.5f ? 1.7f : 1.2f;
  hot = u < 0.55f;
  return true;
}

// The glow: soft rings of firelight round the flames' middle, breathing with the fire.
constexpr int kGlowRings = 3, kGlowPts = 20;
void blazeGlowPts(const Blaze& z, int k, Vec2* p) {
  const float breathe = 1.f + 0.06f * sumSines(z.t * 0.8f + 0.4f, 2);
  const float s = (1.f - 0.22f * fl(k)) * breathe;
  const float rx = kBlazeGlowW * z.bw * s, ry = 0.55f * z.H * s;
  const Vec2 c{z.b.x + 0.3f * z.gust * z.H, z.b.y - 0.36f * z.H};
  for (int i = 0; i < kGlowPts; ++i) {
    const float a = fl(i) / fl(kGlowPts);
    p[i] = Vec2{c.x + rx * cosT(a), c.y + ry * sinT(a)};
  }
}

void blazeDraw(const FirePit& f, Surface& s, const Rect& clip, const Palette& pal) {
  const Blaze z = blazeOf(f);
  const Pixel cols[3] = {mixRGB(pal[FIRE], pal[ACTOR], 0.28f), pal[FIRE], pal[FIRE_CORE]};
  Vec2 p[kBlazePts];
  for (int k = 0; k < 3; ++k) fillPolygon(s, p, blazeLayerPts(z, k, p), cols[k], clip);
  for (int i = 0; i < kBlazeLicks; ++i)
    if (blazeLick(z, i, p)) fillPolygon(s, p, 4, i == 1 ? pal[FIRE_CORE] : pal[FIRE], clip);
  for (int i = 0; i < kBlazeSparks; ++i) {
    Vec2 c;
    float r;
    bool hot;
    if (blazeSpark(z, i, c, r, hot)) fillCircle(s, c, r, hot ? pal[FIRE_CORE] : pal[FIRE], clip);
  }
}

void blazeGlow(const FirePit& f, Surface& s, const Rect& clip, const Palette& pal) {
  const Blaze z = blazeOf(f);
  Vec2 p[kGlowPts];
  for (int k = 0; k < kGlowRings; ++k) {
    blazeGlowPts(z, k, p);
    fillPolygonAlpha(s, p, kGlowPts, pal[FIRE], (uint8_t)kBlazeGlowA, clip);
  }
}

// What the blaze draws, flames, licks, sparks and glow, folded into one box.
Rect blazeBounds(const FirePit& f) {
  const Blaze z = blazeOf(f);
  float x0 = z.b.x - 27.f, x1 = z.b.x + 28.f, y0 = z.b.y - 40.f, y1 = z.b.y + 10.f;   // the stones' box
  auto fold = [&](Vec2 v, float pad) {
    x0 = minf(x0, v.x - pad);
    x1 = maxf(x1, v.x + pad);
    y0 = minf(y0, v.y - pad);
    y1 = maxf(y1, v.y + pad);
  };
  Vec2 p[kBlazePts];
  for (int k = 0; k < 3; ++k) {
    const int n = blazeLayerPts(z, k, p);
    for (int i = 0; i < n; ++i) fold(p[i], 1.f);
  }
  for (int i = 0; i < kBlazeLicks; ++i)
    if (blazeLick(z, i, p))
      for (int j = 0; j < 4; ++j) fold(p[j], 1.f);
  for (int i = 0; i < kBlazeSparks; ++i) {
    Vec2 c;
    float r;
    bool hot;
    if (blazeSpark(z, i, c, r, hot)) fold(c, r + 1.f);
  }
  Vec2 g[kGlowPts];
  blazeGlowPts(z, 0, g);   // the biggest ring holds the others
  for (int i = 0; i < kGlowPts; ++i) fold(g[i], 1.f);
  return boundsOf(x0 - 1.f, y0 - 1.f, x1 + 1.f, y1 + 1.f);
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
  b.scale = 1.f;
  b.slot = TREES_FAR;
  b.glide = 0.f;
  b.pair = false;
  b.pdx = b.pdy = 0.f;
  b.pscale = 1.f;
  a.bird_manual = false;
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
  float target = f.state == FIRE_LIT ? f.fuel : (f.state == FIRE_EMBERS ? 0.25f : (kindling ? f.kindle : 0.f));
  const float cap = weatherFireCap(w);   // the rain keeps a lit fire low (1: no cap)
  if (f.state == FIRE_LIT && cap < 1.f && target > cap) target = cap;
  const float rise = kindling ? 0.35f : 1.8f, fall = kindling ? 1.0f : 4.f;
  if (f.intensity < target) f.intensity = minf(target, f.intensity + dt / rise);
  else if (f.intensity > target) f.intensity = maxf(target, f.intensity - dt / fall);
  if (f.intensity > 0.f) f.clock += dt;

  // The bird waits for the build to finish, then crosses every 40 to 90 s
  // (start to start; a crossing takes 20 to 30 s).
  Bird& b = a.bird;
  if (a.bird_manual || !buildDone(w.build)) return;
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
  Rect r = birdRect(b);
  if (b.pair) {
    const Rect q = birdRect(partnerOf(b));
    r = Rect{r.x0 < q.x0 ? r.x0 : q.x0, r.y0 < q.y0 ? r.y0 : q.y0, r.x1 > q.x1 ? r.x1 : q.x1, r.y1 > q.y1 ? r.y1 : q.y1};
  }
  return r;
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
  if (f.intensity > 1.f) return blazeBounds(f);   // the big blaze (fire and ice): flames, licks, sparks, glow
  return Rect{x - 27, y - 40, x + 28, y + 10};
}

// A small fire on the ground (fire and ice): the campfire's flame shape at size k, no stones.
Rect smallFireBounds(Vec2 base, float k) {
  if (k <= 0.02f) return kEmpty;
  const float bw = 4.f + 8.f * k, H = 7.f + 22.f * k;
  return boundsOf(base.x - bw - 4.f, base.y - 1.2f * H - 3.f, base.x + bw + 4.f, base.y + 3.f);
}

void smallFireDraw(Vec2 base, float k, float clock, Surface& s, const Rect& clip, const Palette& pal) {
  if (k <= 0.02f || !overlaps(smallFireBounds(base, k), clip)) return;
  const float bw = 4.f + 8.f * k, H = 7.f + 22.f * k;
  flamePool(s, base, bw, H, clock, 0.f, pal[FIRE], clip);
  if (k > 0.3f) flamePool(s, Vec2{base.x, base.y - 0.5f}, bw * 0.5f, H * 0.5f, clock, 0.5f, pal[FIRE_CORE], clip);
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
  drawOneBird(b, s, clip, pal);
  if (b.pair) drawOneBird(partnerOf(b), s, clip, pal);
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

void fireGlowDraw(const FirePit& f, Surface& s, const Rect& clip, const Palette& pal) {
  if (f.intensity <= 1.f || !overlaps(fireBounds(f), clip)) return;
  blazeGlow(f, s, clip, pal);
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
  if (f.intensity > 1.f) {
    blazeDraw(f, s, clip, pal);
  } else if (f.intensity > 0.03f) {
    const float I = f.intensity;
    const float H = 32.f * I, bw = 10.f + 3.f * I;   // about a third of the hiker's height
    flamePool(s, f.pos, bw, H, f.clock, 0.f, pal[FIRE], clip);
    flamePool(s, Vec2{f.pos.x, f.pos.y - 0.5f}, bw * 0.55f, H * 0.5f, f.clock, 0.5f, pal[FIRE_CORE], clip);
  }
  drawStones(f, s, clip, pal[ROCK], true);
}

}  // namespace ss
