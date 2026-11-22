// The mushroom trip's sky. See skytrip.h.
//
// OWNERSHIP: skeleton from the foundation step (A1); step B4 (the sky) filled it in and owns it.
//
// Every number marked PROPOSED is a starting value to tune by eye; how it looks is unverified on the
// panel. The speeds are held so nothing moves more than about 1.4 px between two repaints of its band
// (0.4 s at one band a tick): edges travel 2 to 3 px/s sideways and well under 1 px/s up and down,
// stars at most 3.6 px/s round their circles.
#include "skytrip.h"

#include <math.h>

#include "../anim/ease.h"
#include "landscape.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

// The four edges (plan 6.2 table, PROPOSED): rest y, swing, wavelength, travel speed, direction, phase.
// The top three swing about a quarter more than the plan's 12, 14, 13: at those the first stills read as
// a faintly rippled sky rather than one letting go (B4's own look at its sheets, 2026-11-18; Mike to
// judge). Their up-and-down speed stays under 1.4 px/s. The lowest keeps 9 so it stays above row 240.
struct Edge { int rest; float amp, len, v, dir, off; };
const Edge kEdge[4] = {
    {58, 15.f, 340.f, 2.4f, 1.f, 0.00f},
    {118, 18.f, 270.f, 3.0f, -1.f, 0.31f},
    {178, 16.f, 310.f, 2.0f, 1.f, 0.57f},
    {228, 9.f, 240.f, 2.6f, -1.f, 0.83f},
};
// The flat bottom of each edge's fill: below the next edge's lowest swing (its rest + swing + 4), the
// ground line for the last, so every row is covered whatever the waves do.
const int kFillBottom[4] = {140, 198, 244, LS_GROUND_Y};
// Band 0 is a plain rectangle down past edge 0's lowest swing.
constexpr int kBand0Bottom = 76;
// The natural bands (landscape.cpp drawSky): what a still edge gives back exactly.
const int kRestBand[6] = {0, 58, 118, 178, 228, LS_GROUND_Y};

const uint32_t kPastel[5] = {0xB9A6E8, 0xF2A7C3, 0xFFC9A0, 0xFFE9A8, 0xBFEBD0};   // lilac rose peach butter mint
constexpr float kClimbS = 16.f;   // a pastel climbs one band in this many seconds
constexpr float kMix = 0.8f;      // how far a band goes toward its pastel at kc 1

// The stars (plan 6.2 and REVIEW_FIXES F7): in one every 2 s from 16 s, out one every 1.3 s from 84 s.
// Each fade takes 3 s, not the plan's 1.5: a star repainted every 0.4 s would show its fade in four
// visible steps (Mike, 2026-11-18: unhurried).
constexpr float kStarIn0 = 16.f, kStarInStep = 2.f, kStarOut0 = 84.f, kStarOutStep = 1.3f, kStarFade = 3.f;
const float kStarCircle[3] = {5.f, 8.f, 11.f};
// A warm cream, lit: it dims with the mood but not with a deck (F7). The plan's butter vanished on the
// butter and peach bands in the first stills; cream still reads on all five.
constexpr uint32_t kStarHex = 0xFFF4D8;
constexpr float kHaloOuter = 21.f;        // r + 16 + 5 b at b = 1
// The star itself: the landscape's own size (1.2 to 2 px) and this much more. Mike, 2026-11-19: the first
// sheets' stars (+1.0) read as dust; a little larger, still gentle. Now 3.0 to 3.8 px radius at the head,
// the two tail dots 0.7 and 0.45 of it.
constexpr float kStarGrow = 1.8f;

float rec709(Pixel p) {
  return (0.2126f * fl(red8(p)) + 0.7152f * fl(green8(p)) + 0.0722f * fl(blue8(p))) * (1.f / 255.f);
}

// Scale p down, hue kept, until its luminance is at most `cap` (0..1). The aim is a little under the
// cap because packing to RGB565 and expanding again can add up to 7 levels a channel (about 0.027).
Pixel capLum(Pixel p, float cap) {
  const float y = rec709(p);
  if (y <= cap - 0.028f || y <= 0.f) return p;
  const float k = (cap - 0.028f) / y;
  return rgb565((uint8_t)(fl(red8(p)) * k), (uint8_t)(fl(green8(p)) * k), (uint8_t)(fl(blue8(p)) * k));
}

float skyCap(const Palette& pal) {
  float c = 0.f;
  for (int b = 0; b < 5; ++b) c = maxf(c, rec709(pal[(Slot)(SKY0 + b)]));
  return c;
}

// A circle as a polygon, for the halo's see-through discs (the rasterizer has no alpha circle).
void haloDisc(Surface& s, Vec2 c, float r, Pixel col, float a, const Rect& clip) {
  if (a <= 0.f || r <= 0.f) return;
  if (c.y + r < fl(clip.y0) || c.y - r > fl(clip.y1) || c.x + r < fl(clip.x0) || c.x - r > fl(clip.x1)) return;
  constexpr int kN = 40;
  Vec2 p[kN];
  for (int k = 0; k < kN; ++k) {
    const float a2 = fl(k) / fl(kN);
    p[k] = Vec2{c.x + r * cosT(a2), c.y + r * sinT(a2)};
  }
  const float al = clamp01(a) * 255.f + 0.5f;
  fillPolygonAlpha(s, p, kN, col, (uint8_t)(al > 255.f ? 255.f : al), clip);
}

}  // namespace

bool skyTripOn(const LsTrip& t) {
  for (int i = 0; i < SKYTRIP_BANDS; ++i)
    if (t.tq[i] != 0) return true;
  return false;
}

uint16_t skyTripLatch(float t) {
  if (t <= 0.f || t >= 102.f) return 0;
  const float q = t * fl(SS_TICK_HZ) + 0.5f;
  return (uint16_t)(q < 1.f ? 1.f : q);
}

float skyTripWave(float t) {
  if (t <= 0.f || t >= 102.f) return 0.f;
  if (t < 14.f) return easeInOut(t / 14.f);
  if (t <= 90.f) return 1.f;
  return 1.f - easeInOut((t - 90.f) / 12.f);
}

float skyTripColour(float t) {
  if (t <= 0.f || t >= 100.f) return 0.f;
  // One cubic ease over 30 s: about a third by 14 s (plan: "colour to a third" while the waves
  // arrive), full by 30, with no stall between two ramps.
  if (t < 30.f) return easeInOutCubic(t / 30.f);
  if (t <= 86.f) return 1.f;
  return 1.f - easeInOut((t - 86.f) / 14.f);
}

float skyTripStarK(int i, float t) {
  if (i < 0 || i >= LS_STAR_COUNT) return 0.f;
  const float in0 = kStarIn0 + kStarInStep * fl(i);
  const float out0 = kStarOut0 + kStarOutStep * fl(i);
  return easeInOut((t - in0) / kStarFade) * (1.f - easeInOut((t - out0) / kStarFade));
}

float skyTripEdgeY(int j, float x, float t) {
  const Edge& e = kEdge[j < 0 ? 0 : (j > 3 ? 3 : j)];
  const float kw = skyTripWave(t);
  if (kw <= 0.f) return fl(e.rest);
  const float run = e.dir * e.v * t / e.len;   // turns the wave has travelled
  const float w = sinT(x / e.len + run + e.off) + 0.35f * sinT(x / (0.43f * e.len) - 1.3f * run + 2.f * e.off);
  return fl(e.rest) + e.amp * kw * w * (1.f / 1.35f);
}

int skyTripEdgeRest(int j) { return kEdge[j < 0 ? 0 : (j > 3 ? 3 : j)].rest; }
float skyTripEdgeAmp(int j) { return kEdge[j < 0 ? 0 : (j > 3 ? 3 : j)].amp; }

int skyTripBandAt(float x, float y, float t) {
  int b = 0;
  for (int j = 0; j < 4; ++j)
    if (y >= skyTripEdgeY(j, x, t)) b = j + 1;
  return b;
}

uint32_t skyTripPastel(int k) { return kPastel[((k % 5) + 5) % 5]; }

Pixel skyTripLit(const Palette& pal, uint32_t hex) {
  const Pixel key = moodKey(MOOD_DAY)[SKY2], now = pal[SKY2];
  const float kd = fl(red8(key) + green8(key) + blue8(key));
  const float kn = fl(red8(now) + green8(now) + blue8(now));
  float k = kd > 0.f ? kn / kd : 1.f;
  if (k > 1.f) k = 1.f;
  const float r = fl((int)((hex >> 16) & 0xFFu)) * k + 0.5f;
  const float g = fl((int)((hex >> 8) & 0xFFu)) * k + 0.5f;
  const float b = fl((int)(hex & 0xFFu)) * k + 0.5f;
  return rgb565((uint8_t)minf(r, 255.f), (uint8_t)minf(g, 255.f), (uint8_t)minf(b, 255.f));
}

Pixel skyTripBandColour(const Palette& pal, int b, float t) {
  b = b < 0 ? 0 : (b > 4 ? 4 : b);
  const Pixel base = pal[(Slot)(SKY0 + b)];
  const float kc = skyTripColour(t);
  if (kc <= 0.f) return base;
  const float c = maxf(t, 0.f) / kClimbS;
  const int n = (int)floorf(c);
  const float frac = c - fl(n);
  const Pixel pa = skyTripLit(pal, skyTripPastel(b + n));
  const Pixel pb = skyTripLit(pal, skyTripPastel(b + n + 1));
  // The pastel is capped before the mix, not the mix after it: the band then runs smoothly from its own
  // slot at kc 0 (a cap after the mix jumped by up to 3 % where kc reached 0).
  return mixRGB(base, capLum(mixRGB(pa, pb, frac), skyCap(pal)), kMix * kc);
}

SkyTripSun skyTripSunOf(const SkyDraw& sky, const Palette& pal) {
  SkyTripSun s = {};
  if (!sky.real || !sky.sun || sky.cover >= 4) return s;
  s.on = true;
  s.x = fl(sky.sun_x);
  s.y = fl(sky.sun_y);
  s.r = fl(sky.sun_r);
  s.col = landscapeSunColor(sky, pal);
  return s;
}

int skyTripHaloReach(int sun_r) { return sun_r + (int)kHaloOuter + 3; }

bool skyTripStarHidden(const Landscape& ls, const SkyTripSun& sun, int i) {
  if (i < 0 || i >= LS_STAR_COUNT || !sun.on) return false;
  const float cr = kStarCircle[i % 3];
  const float dx = fl(ls.stars[i].x) - sun.x, dy = fl(ls.stars[i].y) - sun.y, keep = sun.r + kHaloOuter + 12.f + cr;
  return dx * dx + dy * dy < keep * keep;
}

Rect skyTripStarBounds(const Landscape& ls, int i) {
  if (i < 0 || i >= LS_STAR_COUNT) return Rect{0, 0, 0, 0};
  const int reach = (int)(kStarCircle[i % 3] + ls.stars[i].r + kStarGrow + 3.f);
  const int x = ls.stars[i].x, y = ls.stars[i].y;
  return Rect{x - reach, y - reach, x + reach + 1, y + reach + 1}.clipped(Rect{0, 0, SS_WIDTH, LS_GROUND_Y});
}

bool skyTripStar(const Landscape& ls, const SkyTripSun& sun, int i, float t, SkyTripStar& out) {
  if (i < 0 || i >= LS_STAR_COUNT) return false;
  const float k = skyTripStarK(i, t);
  if (k <= 0.f) return false;
  if (skyTripStarHidden(ls, sun, i)) return false;
  const float cr = kStarCircle[i % 3];
  // At most 3.6 px/s round the circle; the 11 px circles take at least 20 s.
  float period = 14.f + 2.f * fl(i % 4);
  if (cr > 10.f && period < 20.f) period = 20.f;
  const Vec2 c{fl(ls.stars[i].x), fl(ls.stars[i].y)};
  const float dir = (i & 1) ? -1.f : 1.f;   // not all one way, so they never turn in lockstep
  const float ph = 0.37f * fl(i) + dir * maxf(t, 0.f) / period;
  const float back[3] = {0.f, 0.06f, 0.12f};
  const float size[3] = {1.f, 0.7f, 0.45f};
  const float wt[3] = {1.f, 0.65f, 0.4f};   // the tail fades as well as thins
  for (int d = 0; d < 3; ++d) {
    const float a = ph - dir * back[d];
    out.p[d] = Vec2{c.x + cr * cosT(a), c.y + cr * sinT(a)};
    out.r[d] = (ls.stars[i].r + kStarGrow) * size[d];
    out.k[d] = wt[d] * k;
  }
  return true;
}

void skyTripBand(const Landscape& ls, Surface& s, const Palette& pal, const SkyTripSun& sun, float t,
                 const Rect& clip0) {
  const Rect c = clip0.clipped(Rect{0, 0, SS_WIDTH, LS_GROUND_Y});
  if (c.empty()) return;
  Pixel col[5];
  for (int b = 0; b < 5; ++b) col[b] = skyTripBandColour(pal, b, t);
  const float kw = skyTripWave(t);
  fillRect(s, Rect{0, 0, SS_WIDTH, kBand0Bottom}, col[0], c);
  for (int j = 0; j < 4; ++j) {
    const Edge& e = kEdge[j];
    const float swing = e.amp * kw;
    if (kFillBottom[j] <= c.y0 || fl(e.rest) - swing - 1.f >= fl(c.y1)) continue;
    if (swing < 0.01f) {   // a still edge: exactly the natural band's rectangle
      fillRect(s, Rect{0, kRestBand[j + 1], SS_WIDTH, kFillBottom[j]}, col[j + 1], c);
      continue;
    }
    Vec2 p[LS_CREST_N + 2];
    for (int k = 0; k < LS_CREST_N; ++k) {
      const float x = fl(LS_CREST_X0 + LS_CREST_STEP * k);
      p[k] = Vec2{x, skyTripEdgeY(j, x, t)};
    }
    p[LS_CREST_N] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1)), fl(kFillBottom[j])};
    p[LS_CREST_N + 1] = Vec2{fl(LS_CREST_X0), fl(kFillBottom[j])};
    fillPolygon(s, p, LS_CREST_N + 2, col[j + 1], c);
  }
  // The halo: two see-through discs behind the sun, breathing over 7 s, coloured toward the sun.
  const float kc = skyTripColour(t);
  if (sun.on && kc > 0.f) {
    const float br = 0.5f + 0.5f * sinT(t / 7.f);
    const Vec2 sc{sun.x, sun.y};
    haloDisc(s, sc, sun.r + 16.f + 5.f * br, sun.col, 0.22f * kc, c);
    haloDisc(s, sc, sun.r + 7.f + 3.f * br, sun.col, 0.45f * kc, c);
  }
  // The stars: cream lit by the sky (F7). Under a palette dim enough for the night rule (no sky slot over
  // NIGHT_LUM_MAX) they are held to the rule's small-highlight cap; by day they keep their full cream
  // (capped all day, as first built, they came out a dull beige).
  Pixel star = skyTripLit(pal, kStarHex);
  if (skyCap(pal) <= NIGHT_LUM_MAX + 0.01f) star = capLum(star, NIGHT_HIGHLIGHT_MAX);
  for (int i = 0; i < LS_STAR_COUNT; ++i) {
    // Skip a star only when none of it can reach the clip: its circle, its dot (skyTripStar's r + 1) and
    // the antialiased edge with a pixel to spare. From the same numbers the draw uses, so a bigger star or
    // circle cannot outgrow a fixed margin (the old 14 px had no room left for the biggest stars).
    const float reach = kStarCircle[i % 3] + ls.stars[i].r + kStarGrow + 2.f;
    if (fl(ls.stars[i].y) + reach < fl(c.y0) || fl(ls.stars[i].y) - reach > fl(c.y1)) continue;
    SkyTripStar st;
    if (!skyTripStar(ls, sun, i, t, st)) continue;
    for (int d = 2; d >= 0; --d) {   // the tail first, the star over it
      const Pixel under = col[skyTripBandAt(st.p[d].x, st.p[d].y, t)];
      fillCircle(s, st.p[d], st.r[d], mixRGB(under, star, st.k[d]), c);
    }
  }
}

void skyTripDeckPal(const Palette& pal, float t, Palette& out) {
  out = pal;
  const float kc = skyTripColour(t);
  if (kc <= 0.f) return;
  out.c[CLOUD_SHADE] = mixRGB(pal[CLOUD_SHADE], skyTripBandColour(pal, 0, t), 0.5f * kc);
  out.c[CLOUD_LIT] = mixRGB(pal[CLOUD_LIT], skyTripBandColour(pal, 1, t), 0.5f * kc);
}

float skyTripRidgeLift(float t) { return 2.f * skyTripWave(t) * (0.5f + 0.5f * sinT(t / 9.f)); }

}  // namespace ss
