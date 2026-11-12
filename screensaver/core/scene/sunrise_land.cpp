// The sunrise overlook's static landscape. See sunrise.h.
//
// Flat shapes in palette slots, or fixed mixes of two (still one flat colour a shape), as the
// Lookout's: the sky in five bands; four long, smooth ridgelines, each lighter and bluer the farther
// it is (mixed toward the horizon colour), with a paler band of haze at its foot where the next
// ridge rises in front; the valley floor; low fog banks in the valley; the wooded slope behind the
// ledge with its scrub pines and laurel; and the ledge itself: a pale slab of layered rock coming
// out of the lower left to about the middle of the frame, its front face banded, an undercut beneath
// its end and the support cliff well back from the tip, so there is air under it. The morning light is drawn into the cache
// as a few fixed steps per ridge (each step repaints only that ridge's rows), and the fog's glow and
// thinning the same way. Nothing is random at draw time.
#include "sunrise.h"

#include <math.h>

#include "../anim/ease.h"

namespace ss {

namespace {

inline float fl(int v) { return (float)v; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }

// The sky's bands: narrower than the camp's, so the lowest (the horizon glow) shows as a strip over
// the far ridge.
const int kBand[6] = {0, 48, 96, 144, 192, SS_HEIGHT};

// Ridge k's colour: its slot (or a mix between two), then the haze toward the horizon colour. The
// ranges read by this alone, as real ones do: each farther ridge a flat fill a little paler and
// closer to the sky, overlapping the one behind, with no outline or rim along any crest.
const float kHaze[SR_RIDGES] = {0.50f, 0.34f, 0.19f, 0.06f};
const float kLitStep = 0.09f;
// Where the ledge's tip ends, x (the frame is 800 wide): about the middle.
constexpr float kTipX0 = 392.f, kTipX1 = 418.f;   // each light step mixes the ridge this much toward the sun's colour (PROPOSED)

float crestAt(const float* c, float x) {
  float f = (x - fl(LS_CREST_X0)) / fl(LS_CREST_STEP);
  if (f <= 0.f) return c[0];
  if (f >= fl(LS_CREST_N - 1)) return c[LS_CREST_N - 1];
  const int i = (int)f;
  f -= fl(i);
  return lerp(c[i], c[i + 1], f);
}

// Stable 0..1 per position, so a plant pops at the same moment in every strip.
float hash01(float x, float y) {
  uint32_t h = (uint32_t)(int32_t)x * 73856093u ^ (uint32_t)(int32_t)y * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return (float)(h & 0xFFFFu) / 65536.f;
}
float plantScale(float u, float x, float y) {
  if (u >= 2.f) return 1.f;
  const float d = 0.62f * hash01(x, y);
  return easeOutBack(window(u, d, d + 0.12f));
}

Pixel ridgeColour(const Palette& pal, int k, int lit, bool muted) {
  Pixel c;
  if (k == 0) c = pal[RIDGE_FAR];
  else if (k == 1) c = mixRGB(pal[RIDGE_FAR], pal[RIDGE_MID], 0.5f);
  else if (k == 2) c = pal[RIDGE_MID];
  else c = pal[RIDGE_NEAR];
  c = mixRGB(c, pal[SKY4], kHaze[k]);
  if (lit > 0) c = mixRGB(c, pal[SUN], kLitStep * (float)lit * (muted ? 0.5f : 1.f));
  return c;
}

void drawCrest(Surface& s, const float* crest, Pixel col, const Rect& clip, float dy) {
  Vec2 p[LS_CREST_N + 2];
  for (int i = 0; i < LS_CREST_N; ++i) p[i] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * i), crest[i] + dy};
  p[LS_CREST_N] = Vec2{fl(LS_CREST_X0 + LS_CREST_STEP * (LS_CREST_N - 1)), fl(SS_HEIGHT + 2)};
  p[LS_CREST_N + 1] = Vec2{fl(LS_CREST_X0), fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, LS_CREST_N + 2, col, clip);
}

float hillAt(const SunriseLand& sl, float x) {
  const float t = clamp01((x - fl(LS_CREST_X0)) / (sl.hill_x - fl(LS_CREST_X0)));
  const float sm = t * t * (3.f - 2.f * t);
  return lerp(sl.hill[0], sl.hill[1], sm) + 4.f * sinT(x / 90.f) * (1.f - t);
}

// A scrub pine: a short crooked trunk and three or four flat, wind-swept tiers leaning right.
void drawPine(Surface& s, const SrPine& p, Pixel body, Pixel trunk, const Rect& clip, float k, float dy) {
  if (k <= 0.01f) return;
  const float h = fl(p.h) * k, w = fl(p.w) * k;
  const float bx = fl(p.x), by = fl(p.base_y) + dy;
  strokeCapsule(s, Vec2{bx, by}, Vec2{bx + 0.12f * h, by - 0.55f * h}, maxf(1.6f, 0.07f * h), trunk, clip);
  const int tiers = 3 + (p.h & 1);
  for (int i = 0; i < tiers; ++i) {
    const float f = (float)i / (float)tiers;
    const float cy = by - h * (0.32f + 0.62f * f);
    const float cx = bx + 0.14f * h * (0.4f + f);
    const float hw = w * (0.62f - 0.34f * f);
    const Vec2 tri[4] = {{cx - hw * 0.7f, cy + 0.07f * h}, {cx + hw * 1.15f, cy + 0.05f * h}, {cx + hw * 0.6f, cy - 0.1f * h},
                         {cx - hw * 0.35f, cy - 0.08f * h}};
    fillPolygon(s, tri, 4, body, clip);
  }
}

// A laurel clump: a dark rounded mass with a lighter top.
void drawBush(Surface& s, const SrBush& b, Pixel dark, Pixel light, const Rect& clip, float k, float dy) {
  if (k <= 0.01f) return;
  const float rx = fl(b.rx) * k, ry = fl(b.ry) * k;
  const Vec2 c{fl(b.x), fl(b.y) + dy - ry * 0.6f};
  fillEllipse(s, c, rx, ry, dark, clip);
  fillEllipse(s, Vec2{c.x - rx * 0.2f, c.y - ry * 0.35f}, rx * 0.6f, ry * 0.45f, light, clip);
}

Vec2 vadd(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }

// ---- the mountain's own near ground (Mike, 2026-11-12: the lower right looked unfinished) --------
// Under and right of the ledge, near to far: a rounded rock mass in the corner, two bands of forest
// canopy on the mountain's flank coming out from behind the support cliff and falling away to the
// right, then two wooded foothills over the valley, then the far ridges. Flat fills only, each
// layer darker and more saturated the nearer it is; the crowns catch the morning light. A hollow in
// the foothills under the ledge's tip keeps air under it. Their shapes come from a hash of the
// ledge and the ridges (no stored state, no extra random draws).
constexpr float kFlankTop = 392.f;     // the upper canopy's top just right of the cliff (PROPOSED)
constexpr float kFlankFall = 0.055f;   // how fast it falls away to the right, px a px
constexpr float kNearCanopy = 438.f;   // the nearer canopy band's top (PROPOSED)
constexpr float kFoot1 = 338.f, kFoot2 = 360.f;   // the foothills' mean crests, far and near
constexpr float kHollowW = 95.f, kHollowD = 20.f;  // the hollow under the tip: half width, depth
constexpr float kCanopyLit = 0.07f;
constexpr float kCanopyGreen = 0.08f;  // each step of light greens the canopy's body this much toward FOLIAGE    // each step of morning light mixes the crowns' lit side toward the sun

float landHash(const SunriseLand& sl, int i) {
  return hash01(sl.ft.x * 13.f + fl(i) * 101.f, sl.crest[3][5] * 3.f + fl(i) * 7.f);
}
float hollowAt(const SunriseLand& sl, float x) {
  const float t = 1.f - fabsf(x - (sl.ft.x + 25.f)) / kHollowW;
  return t > 0.f ? kHollowD * t * t * (3.f - 2.f * t) : 0.f;
}
// Foothill k's crest (0 the farther, 1 the nearer).
float footAt(const SunriseLand& sl, int k, float x) {
  const float p0 = landHash(sl, 10 + 2 * k), p1 = landHash(sl, 11 + 2 * k);
  if (k == 0) return kFoot1 + 9.f * sinT(x / 190.f + p0) + 5.f * sinT(x / 74.f + p1) + hollowAt(sl, x);
  return kFoot2 + 8.f * sinT(x / 140.f + p0) + 4.f * sinT(x / 57.f + p1) + 0.6f * hollowAt(sl, x);
}
// The flank's canopy tops: band 0 from behind the cliff falling away right, band 1 nearer and lower.
float flankAt(const SunriseLand& sl, int k, float x) {
  const float p = landHash(sl, 20 + k);
  if (k == 0) return kFlankTop + kFlankFall * (x - sl.under.x) + 5.f * sinT(x / 120.f + p);
  return kNearCanopy - 0.025f * (x - sl.under.x) + 6.f * sinT(x / 150.f + p);
}

// A body of land under a crest line from x0 to the frame's right, down to the bottom.
void fillUnder(Surface& s, const SunriseLand& sl, int k, bool foot, float x0, float dy, Pixel col, const Rect& clip) {
  Vec2 p[60];
  int n = 0;
  for (float x = x0; n < 56; x += 14.f) {
    p[n++] = Vec2{x, (foot ? footAt(sl, k, x) : flankAt(sl, k, x)) + dy};
    if (x > fl(SS_WIDTH) + 14.f) break;
  }
  const float xr = p[n - 1].x;
  p[n++] = Vec2{xr, fl(SS_HEIGHT + 2)};
  p[n++] = Vec2{x0, fl(SS_HEIGHT + 2)};
  fillPolygon(s, p, n, col, clip);
}

// A wooded edge along a crest: `rows` rows of rounded crowns (a few pointed conifers among them),
// overlapping and jostled, the top row's crowns lit on their sunward (upper right) side, the rows
// below more faintly. `r0` the crowns' size.
void drawCanopy(Surface& s, const SunriseLand& sl, int k, bool foot, float x0, float r0, int rows, float dy, Pixel body,
                Pixel lit, const Rect& clip) {
  for (int j = rows - 1; j >= 0; --j) {   // the lower rows first: the top row overlaps them
    const Pixel lj = j == 0 ? lit : mixRGB(lit, body, 0.35f + 0.2f * (float)j);
    for (float x = x0 + (float)j * 0.6f * r0; x < fl(SS_WIDTH) + 12.f;) {
      const float h = hash01(x * 3.f + fl(k) * 57.f + fl(j) * 13.f, 11.f + (foot ? 5.f : 0.f));
      const float r = r0 * (0.75f + 0.5f * h);
      const float y = (foot ? footAt(sl, k, x) : flankAt(sl, k, x)) + dy + (float)j * 1.15f * r0 +
                      (hash01(x, 41.f + fl(j)) - 0.5f) * 0.7f * r;
      const float step = r * (0.95f + 0.4f * hash01(x, 29.f));
      if (y - 2.4f * r > fl(clip.y1) || y + r < fl(clip.y0) || x + r < fl(clip.x0) - 2.f || x - r > fl(clip.x1) + 2.f) {
        x += step;
        continue;
      }
      if (h < 0.16f) {   // a conifer: a narrow spire, its right side lit
        const Vec2 t[3] = {{x - 0.8f * r, y + 0.4f * r}, {x + 0.8f * r, y + 0.4f * r}, {x, y - 2.3f * r}};
        fillPolygon(s, t, 3, body, clip);
        const Vec2 l[3] = {{x + 0.05f * r, y - 1.7f * r}, {x + 0.5f * r, y - 0.1f * r}, {x, y - 2.2f * r}};
        fillPolygon(s, l, 3, lj, clip);
      } else {           // a rounded crown: lit, then its shaded body over most of it, so the light is a crescent
        fillEllipse(s, Vec2{x, y - 0.55f * r}, r, 0.85f * r, lj, clip);
        fillEllipse(s, Vec2{x - 0.22f * r, y - 0.38f * r}, 0.9f * r, 0.78f * r, body, clip);
      }
      x += step;
    }
  }
}

// The near ground (the flank's canopy and the corner rock), in the slope's layer.
void drawFlank(const SunriseLand& sl, Surface& s, const Palette& pal, const Rect& clip, const SrDraw& d) {
  const float dy = d.fg_drop;
  const float L = ((float)d.lit[SR_RIDGES - 1] + 2.f * (float)d.slab_lit) * (d.muted ? 0.5f : 1.f);   // 0..4
  const float x0 = sl.under.x - 70.f;   // its left end is behind the support cliff
  for (int k = 0; k < 2; ++k) {
    // The canopy greens a little as the light comes down onto it; its crowns' sunward sides more.
    Pixel body = k == 0 ? mixRGB(pal[FOLIAGE_DARK], pal[ACTOR], 0.08f) : mixRGB(pal[FOLIAGE_DARK], pal[ACTOR], 0.35f);
    body = mixRGB(body, pal[FOLIAGE], kCanopyGreen * L * (k == 0 ? 1.f : 0.6f));
    Pixel lit = mixRGB(body, pal[FOLIAGE], k == 0 ? 0.7f : 0.6f);
    lit = mixRGB(lit, pal[SUN], kCanopyLit * L);
    fillUnder(s, sl, k, false, x0 + 40.f * (float)k, dy, body, clip);
    drawCanopy(s, sl, k, false, x0 + 40.f * (float)k, k == 0 ? 7.f : 9.5f, 3, dy, body, lit, clip);
  }
  // The corner rock: a rounded grey mass, close and dark, a lighter top, a hollow and two cracks.
  const Pixel rock = mixRGB(mixRGB(pal[ROCK], pal[GROUND_DARK], 0.45f), pal[SUN], 0.03f * L);
  const Pixel top = mixRGB(rock, pal[CLOUD_LIT], 0.22f);
  const Pixel dark = mixRGB(rock, pal[ACTOR], 0.5f);
  const float cx = fl(SS_WIDTH) - 150.f + 30.f * landHash(sl, 30);
  const Vec2 m[9] = {{cx - 140.f, fl(SS_HEIGHT + 2)}, {cx - 118.f, 462.f + dy}, {cx - 78.f, 446.f + dy}, {cx - 20.f, 440.f + dy},
                     {cx + 50.f, 438.f + dy}, {cx + 120.f, 444.f + dy}, {cx + 190.f, 450.f + dy},
                     {cx + 190.f, fl(SS_HEIGHT + 2)}, {cx - 140.f, fl(SS_HEIGHT + 2)}};
  fillPolygon(s, m, 9, rock, clip);
  const Vec2 t[6] = {{cx - 104.f, 459.f + dy}, {cx - 72.f, 449.f + dy}, {cx - 18.f, 443.f + dy}, {cx + 50.f, 441.f + dy},
                     {cx + 30.f, 452.f + dy}, {cx - 60.f, 457.f + dy}};
  fillPolygon(s, t, 6, top, clip);
  fillEllipse(s, Vec2{cx - 48.f, 466.f + dy}, 13.f, 4.f, dark, clip);
  strokeCapsule(s, Vec2{cx - 2.f, 446.f + dy}, Vec2{cx + 14.f, 470.f + dy}, 1.3f, dark, clip);
  strokeCapsule(s, Vec2{cx + 60.f, 446.f + dy}, Vec2{cx + 52.f, 466.f + dy}, 1.2f, dark, clip);
}

// ---- the ledge: weathered sandstone (Mike, 2026-11-12: "less like a poured smooth sidewalk") -------
// The figures walk, stand and sit on the straight lines between the back and front edges
// (sunriseSlabAt); the rock's edges wander only a couple of px off those lines, and only outward
// (the front edge down, the back edge up), so no foot ever floats or sinks. Everything else is
// broken: a lumpy front lip with a step and two notches, a blunt nose a little raised, a thick
// undercut block at the tip with a stepped underside, a cliff of blocks with joints and bedding
// breaks of uneven length, a ledge standing off it, pans and cracks and patches on top.
constexpr int kLipN = 26;              // points along the front lip
constexpr float kLipLump = 1.3f;       // the lip's lumps, px (outward only)
constexpr float kLipStep = 4.f;        // the step down in the lip partway out, px
constexpr float kNoseRise = 1.6f;      // the nose's lift at the very tip, px

// The front lip at u (0 the left edge, 1 the tip): the straight front edge plus lumps, a step,
// two notches, the nose; y offsets are >= 0 (down) except the nose's small lift.
Vec2 lipAt(const SunriseLand& sl, float u) {
  const Vec2 p{lerp(sl.fl.x, sl.ft.x, u), lerp(sl.fl.y, sl.ft.y, u)};
  const float p0 = landHash(sl, 40), p1 = landHash(sl, 41);
  // Lumps: bigger where the slab is deep (left), small at the narrow tip.
  float d = kLipLump * (1.f + 0.6f * sinT(u * 6.3f + p0) + 0.4f * sinT(u * 17.f + p1)) * (2.6f - 1.8f * u);
  const float us = 0.38f + 0.2f * landHash(sl, 42);   // the step: higher rock nearer the tip
  d += kLipStep * clamp01((us - u) * 30.f);
  for (int i = 0; i < 2; ++i) {                        // two notches
    const float un = 0.15f + 0.6f * landHash(sl, 43 + i);
    const float t = 1.f - fabsf(u - un) * 45.f;
    if (t > 0.f) d += 3.f * t;
  }
  if (u > 0.9f) d = lerp(d, -kNoseRise, clamp01((u - 0.9f) * 12.f));   // the blunt nose, raised a little
  return Vec2{p.x, p.y + d};
}

// The face's bottom at u: thicker at the tip (a blunt block), stepped where the beds broke off.
float faceDepthAt(const SunriseLand& sl, float u) {
  float d = lerp(sl.face, sl.face_tip, u);
  for (int i = 0; i < 3; ++i) {                        // blocks broken off the face's foot: steps up
    const float a = 0.12f + 0.25f * (float)i + 0.12f * landHash(sl, 50 + i);
    if (u > a && u < a + 0.07f + 0.05f * landHash(sl, 53 + i)) d -= 4.f + 3.f * landHash(sl, 56 + i);
  }
  return d;
}

// The y of the face's bottom at x.
float faceBottomAt(const SunriseLand& sl, float x) {
  const float u = clamp01((x - sl.fl.x) / (sl.ft.x - sl.fl.x));
  return lerp(sl.fl.y, sl.ft.y, u) + faceDepthAt(sl, u);
}

void drawSlab(const SunriseLand& sl, Surface& s, const Palette& pal, const Rect& clip, const SrDraw& d) {
  const float dy = d.fg_drop;
  const Vec2 D{0.f, dy};
  const Pixel rock = pal[ROCK];
  const float warm = d.slab_lit ? (d.muted ? 0.08f : 0.16f) : 0.f;
  const Pixel top = mixRGB(mixRGB(rock, pal[CLOUD_LIT], 0.5f), pal[SUN], warm);
  const Pixel top_lo = mixRGB(top, pal[GROUND_DARK], 0.2f);    // weathered patches
  const Pixel top_hi = mixRGB(top, pal[CLOUD_LIT], 0.22f);      // bleached patches
  const Pixel face = mixRGB(mixRGB(rock, pal[GROUND_DARK], 0.25f), pal[SUN], 0.5f * warm);
  const Pixel face_lit = mixRGB(face, top, 0.45f);              // faces turned to the sun (right)
  const Pixel joint = mixRGB(rock, pal[GROUND_DARK], 0.7f);
  const Pixel cliff = mixRGB(mixRGB(rock, pal[GROUND_DARK], 0.55f), pal[SUN], 0.3f * warm);
  const Pixel cliff_lit = mixRGB(cliff, face, 0.55f);
  const Pixel under = mixRGB(pal[GROUND_DARK], pal[ACTOR], 0.35f);
  const Pixel deep = mixRGB(pal[GROUND_DARK], pal[ACTOR], 0.6f);
  const Pixel pan = mixRGB(top, pal[SKY3], 0.45f);
  const float ux = sl.under.x;
  const float uu = clamp01((ux - sl.fl.x) / (sl.ft.x - sl.fl.x));   // where the overhang begins, as u
  auto fb = [&](float x) { return faceBottomAt(sl, x) + dy; };

  // 1. The support cliff: blocks stepping down and back from under the overhang to the frame's foot.
  {
    const float b0 = fb(ux);
    const Vec2 p[10] = {{sl.fl.x, fb(sl.fl.x)}, {ux, b0},
                        {ux - 7.f, b0 + 16.f}, {ux - 2.f, b0 + 24.f},            // a jutting block
                        {ux - 3.f, b0 + 46.f}, {ux - 16.f, b0 + 52.f},           // a chipped corner, a step back
                        {ux - 18.f, b0 + 78.f}, {ux - 40.f, fl(SS_HEIGHT + 2)},
                        {-20.f, fl(SS_HEIGHT + 2)}, {-20.f, fb(-20.f)}};
    fillPolygon(s, p, 10, cliff, clip);
    // The blocks' sunward faces, lighter.
    const Vec2 l1[4] = {{ux - 7.f, b0 + 16.f}, {ux - 2.f, b0 + 24.f}, {ux - 3.f, b0 + 46.f}, {ux - 12.f, b0 + 40.f}};
    fillPolygon(s, l1, 4, cliff_lit, clip);
    const Vec2 l2[4] = {{ux - 16.f, b0 + 52.f}, {ux - 18.f, b0 + 78.f}, {ux - 30.f, b0 + 110.f}, {ux - 28.f, b0 + 66.f}};
    fillPolygon(s, l2, 4, cliff_lit, clip);
    // Blocks: big irregular faces in a few tones, split by dark wedge-shaped joints that open at
    // the top and close as they go down; a bedding break here and there where a block steps back.
    const Pixel tones[3] = {mixRGB(cliff, joint, 0.25f), cliff_lit, mixRGB(cliff, face, 0.3f)};
    float x = -20.f;
    for (int i = 0; i < 6 && x < ux - 30.f; ++i) {
      const float w = 34.f + 30.f * landHash(sl, 60 + i);
      const float xb = minf(x + w, ux - 22.f);
      const float y0 = fb(x) + 4.f + 26.f * landHash(sl, 66 + i), y1 = y0 + 40.f + 70.f * landHash(sl, 72 + i);
      const float sk = 6.f * (landHash(sl, 78 + i) - 0.5f);
      const Vec2 q[5] = {{x + 3.f, y0}, {xb - 2.f, y0 + sk + 2.f}, {xb - 4.f + sk, y1}, {x + 6.f, y1 + 3.f},
                         {x + 1.f, 0.5f * (y0 + y1)}};
      fillPolygon(s, q, 5, tones[(i + (int)(3.f * landHash(sl, 84 + i))) % 3], clip);
      const Vec2 j[3] = {{xb - 2.f, fb(xb) + 2.f}, {xb + 2.5f, fb(xb) + 2.f}, {xb + 0.5f, fb(xb) + 30.f + 50.f * landHash(sl, 90 + i)}};
      fillPolygon(s, j, 3, joint, clip);   // the joint at its right side
      x = xb + 2.f;
    }
        // A ledge standing off the cliff part way down: its lit top and its shadow.
    const float lx = 30.f + (ux - 120.f) * landHash(sl, 96), ly = fb(lx) + 58.f + 20.f * landHash(sl, 97);
    const Vec2 lt[4] = {{lx - 26.f, ly}, {lx + 22.f, ly - 2.f}, {lx + 26.f, ly + 3.f}, {lx - 22.f, ly + 4.f}};
    fillPolygon(s, lt, 4, face_lit, clip);
    const Vec2 ls[4] = {{lx - 22.f, ly + 4.f}, {lx + 26.f, ly + 3.f}, {lx + 20.f, ly + 9.f}, {lx - 16.f, ly + 10.f}};
    fillPolygon(s, ls, 4, under, clip);
  }

  // 2. The underside of the overhang: dark, stepped where blocks fell out, deepest under the lip.
  {
    Vec2 p[2 * 12 + 2];
    int n = 0;
    for (int i = 0; i <= 11; ++i) {   // along the face's foot, out to the tip
      const float u = lerp(uu, 1.f, (float)i / 11.f);
      const float x = lerp(sl.fl.x, sl.ft.x, u);
      p[n++] = Vec2{x, fb(x) - 1.f};
    }
    for (int i = 11; i >= 0; --i) {   // the underside: a broken line rising toward the tip
      const float u = lerp(uu, 1.f, (float)i / 11.f);
      const float x = lerp(sl.fl.x, sl.ft.x, u);
      const float step = (i % 3 == 1 ? 3.f : 0.f) + 2.f * landHash(sl, 100 + i);
      p[n++] = Vec2{x - 3.f, fb(x) + lerp(18.f, 6.f, (float)i / 11.f) + step};
    }
    fillPolygon(s, p, n, under, clip);
    for (int i = 0; i < 11; ++i) {    // the deep shadow right under the lip
      const float ua = lerp(uu, 1.f, (float)i / 11.f), ub = lerp(uu, 1.f, (float)(i + 1) / 11.f);
      const float xa = lerp(sl.fl.x, sl.ft.x, ua), xb = lerp(sl.fl.x, sl.ft.x, ub);
      const Vec2 q[4] = {{xa, fb(xa) - 1.f}, {xb, fb(xb) - 1.f}, {xb - 2.f, fb(xb) + 4.f}, {xa - 2.f, fb(xa) + 5.f}};
      fillPolygon(s, q, 4, deep, clip);
    }
  }

  // 3. The front face: under the lip down to its broken foot, thick at the tip; the tip's blunt end
  // catches the sun.
  {
    Vec2 p[2 * kLipN + 2];
    int n = 0;
    for (int i = 0; i < kLipN; ++i) p[n++] = vadd(lipAt(sl, (float)i / (float)(kLipN - 1)), D);
    for (int i = kLipN - 1; i >= 0; --i) {
      const float u = (float)i / (float)(kLipN - 1);
      const float x = lerp(sl.fl.x, sl.ft.x, u);
      p[n++] = Vec2{x, fb(x)};
    }
    fillPolygon(s, p, n, face, clip);
    // The face in blocks of slightly different tone, split by dark wedge joints from the lip down.
    float u0 = 0.f;
    for (int i = 0; i < 7 && u0 < 0.97f; ++i) {
      const float u1 = minf(1.f, u0 + 0.1f + 0.1f * landHash(sl, 110 + i));
      const float h = landHash(sl, 117 + i);
      if (h > 0.35f) {   // this block a little lighter or darker than the face
        Vec2 q[8];
        int m = 0;
        for (int k = 0; k <= 3; ++k) q[m++] = vadd(lipAt(sl, lerp(u0, u1, (float)k / 3.f)), Vec2{0.f, dy + 1.f});
        for (int k = 3; k >= 0; --k) {
          const float xx = lerp(sl.fl.x, sl.ft.x, lerp(u0, u1, (float)k / 3.f));
          q[m++] = Vec2{xx, fb(xx) - 0.5f};
        }
        fillPolygon(s, q, m, h > 0.7f ? mixRGB(face, face_lit, 0.45f) : mixRGB(face, joint, 0.22f), clip);
      }
      if (u1 < 0.97f) {   // the joint at its right end: open at the lip, closing downward
        const Vec2 a = vadd(lipAt(sl, u1), D);
        const float xx = a.x, b = fb(xx);
        const Vec2 j[3] = {{xx - 1.5f, a.y + 1.f}, {xx + 1.5f, a.y + 1.f}, {xx + 0.5f, lerp(a.y, b, 0.55f + 0.4f * landHash(sl, 124 + i))}};
        fillPolygon(s, j, 3, joint, clip);
      }
      u0 = u1;
    }
        // The lip's rounded shoulder: the top's edge curving over into the face, a lighter band of
    // wandering depth under the lip (rock weathers round, not square).
    {
      Vec2 q[2 * kLipN];
      int m = 0;
      for (int i = 0; i < kLipN; ++i) q[m++] = vadd(lipAt(sl, (float)i / (float)(kLipN - 1)), Vec2{0.f, dy - 0.5f});
      for (int i = kLipN - 1; i >= 0; --i) {
        const float u = (float)i / (float)(kLipN - 1);
        const float r = (2.f + 3.5f * landHash(sl, 230 + i)) * (1.f - 0.45f * u);
        q[m++] = vadd(lipAt(sl, u), Vec2{0.f, dy + r});
      }
      fillPolygon(s, q, m, mixRGB(face, top, 0.55f), clip);
    }
    // The tip's end: a blunt block face turned to the sun, chipped at its foot.
    const Vec2 tip = vadd(lipAt(sl, 1.f), D);
    const float tb = fb(sl.ft.x);
    const Vec2 e[5] = {{tip.x - 1.f, tip.y}, {tip.x + 4.f, tip.y + 1.f}, {tip.x + 5.f, tb - 9.f}, {tip.x + 2.f, tb - 3.f},
                       {tip.x - 2.f, tb}};
    fillPolygon(s, e, 5, face_lit, clip);
  }

  // 4. The top: lumpy lip in front, a wandering back edge, the blunt nose; patches, pans and cracks.
  {
    Vec2 p[kLipN + 16];
    int n = 0;
    for (int i = 0; i <= 12; ++i) {   // the back edge, from the left out to the tip; it only wanders up
      const float u = (float)i / 12.f;
      const float up = (i == 0 || i == 12) ? 0.f : (3.f - 2.f * u) * landHash(sl, 130 + i);
      p[n++] = vadd(Vec2{lerp(sl.bl.x, sl.bt.x, u), lerp(sl.bl.y, sl.bt.y, u) - up}, D);
    }
    const Vec2 tip = vadd(lipAt(sl, 1.f), D);
    p[n++] = Vec2{sl.bt.x + 5.f, sl.bt.y + dy + 1.f};             // the nose: blunt, square-ish
    p[n++] = Vec2{tip.x + 4.f, tip.y + 1.f - 0.4f * (tip.y - sl.bt.y - dy)};
    for (int i = kLipN - 1; i >= 0; --i) p[n++] = vadd(lipAt(sl, (float)i / (float)(kLipN - 1)), D);
    fillPolygon(s, p, n, top, clip);
    // Weathered patches, darker and lighter: clusters of overlapping flat blobs across the top.
    for (int i = 0; i < 8; ++i) {
      const float u = 0.03f + 0.12f * (float)i + 0.05f * landHash(sl, 140 + i);
      const float v = 0.2f + 0.55f * landHash(sl, 146 + i);
      const Pixel c = (i % 3 == 1) ? top_hi : top_lo;
      for (int k = 0; k < 3; ++k) {
        const float h = landHash(sl, 200 + 3 * i + k);
        const Vec2 o = vadd(sunriseSlabAt(sl, u + 0.025f * (float)k, clamp01(v + 0.18f * (h - 0.5f))), D);
        const float rx = (7.f + 12.f * h) * (1.f - 0.4f * u);
        fillEllipse(s, o, rx, maxf(1.3f, 0.26f * rx), c, clip);
      }
    }
    // Shallow weathering pans: a dark rim of hollow with the sky's colour lying in it.
    for (int i = 0; i < 3; ++i) {
      const float u = 0.12f + 0.27f * (float)i + 0.08f * landHash(sl, 164 + i);
      const Vec2 c = vadd(sunriseSlabAt(sl, u, 0.3f + 0.35f * landHash(sl, 167 + i)), D);
      const float w = 8.f + 8.f * landHash(sl, 170 + i);
      fillEllipse(s, c, w, 2.6f, mixRGB(top, pal[GROUND_DARK], 0.4f), clip);
      fillEllipse(s, Vec2{c.x + 0.8f, c.y + 0.5f}, w - 2.2f, 1.4f, mixRGB(pan, top_lo, 0.35f), clip);
    }
    // Cracks: short, kinked, running back from the lip.
    for (int i = 0; i < 5; ++i) {
      const float u = 0.1f + 0.18f * (float)i + 0.05f * landHash(sl, 175 + i);
      const Vec2 a = vadd(sunriseSlabAt(sl, u, 0.98f), D);
      const Vec2 m = vadd(sunriseSlabAt(sl, u + 0.012f, 0.62f), D);
      const Vec2 b = vadd(sunriseSlabAt(sl, u - 0.006f, 0.3f + 0.2f * landHash(sl, 180 + i)), D);
      const Pixel c = mixRGB(top, pal[GROUND_DARK], 0.4f);
      strokeCapsule(s, a, m, 1.1f, c, clip);
      if (i & 1) strokeCapsule(s, m, b, 1.f, c, clip);
    }
    // A tuft of grass in a crack at the back, near the slope (clear of his path).
    const Vec2 g = vadd(sunriseSlabAt(sl, 0.2f + 0.08f * landHash(sl, 186), 0.08f), D);
    for (int i = 0; i < 7; ++i) {
      const float a = -0.75f + 0.25f * (float)i;
      const Vec2 tipg{g.x + 7.f * a + 1.5f, g.y - 10.f + 3.f * fabsf(a)};
      const Vec2 blade[3] = {{g.x - 1.2f + 0.8f * (float)i * 0.5f, g.y + 1.f}, {g.x + 1.2f + 0.8f * (float)i * 0.5f, g.y + 1.f}, tipg};
      fillPolygon(s, blade, 3, mixRGB(pal[GRASS], pal[SUN], 0.5f * warm), clip);
    }
  }
}

}  // namespace

// ---- public -----------------------------------------------------------------------------------

int sunriseSkyBand(int y) {
  int b = 0;
  while (b < 4 && y >= kBand[b + 1]) ++b;
  return b;
}

float sunriseRidgeTop(const SunriseLand& sl, float x) {
  float y = crestAt(sl.crest[0], x);
  for (int k = 1; k < SR_RIDGES; ++k) y = minf(y, crestAt(sl.crest[k], x));
  return y;
}

Vec2 sunriseSlabAt(const SunriseLand& sl, float u, float v) {
  const Vec2 b{lerp(sl.bl.x, sl.bt.x, u), lerp(sl.bl.y, sl.bt.y, u)};
  const Vec2 f{lerp(sl.fl.x, sl.ft.x, u), lerp(sl.fl.y, sl.ft.y, u)};
  return Vec2{lerp(b.x, f.x, v), lerp(b.y, f.y, v)};
}

void sunriseRidgeRows(const SunriseLand& sl, int k, int& y0, int& y1) {
  if (k < 0) {   // the fog
    y0 = (int)floorf(sl.fog_top);
    y1 = (int)ceilf(sl.fog_bot);
    return;
  }
  if (k >= SR_RIDGES) {   // the slab
    y0 = (int)floorf(minf(sl.bt.y, sl.bl.y)) - 4;
    y1 = SS_HEIGHT;
    return;
  }
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < LS_CREST_N; ++i) lo = minf(lo, sl.crest[k][i]);
  if (k + 1 < SR_RIDGES) {
    for (int i = 0; i < LS_CREST_N; ++i) hi = maxf(hi, sl.crest[k + 1][i]);
    y1 = (int)ceilf(hi) + 3;
  } else {
    y1 = SS_HEIGHT;
  }
  y0 = (int)floorf(lo) - 3;
}

void sunriseLandGenerate(SunriseLand& sl, Rng& rng) {
  // Four long, smooth ridgelines (old mountains: no peaks, long gentle swells), far to near.
  static const float kBase[SR_RIDGES] = {208.f, 238.f, 268.f, 300.f};
  static const float kAmp[SR_RIDGES][3] = {{11.f, 5.f, 1.5f}, {13.f, 6.f, 2.f}, {15.f, 7.f, 2.5f}, {17.f, 8.f, 3.f}};
  static const float kWave[SR_RIDGES][3] = {{400.f, 160.f, 70.f}, {340.f, 140.f, 60.f}, {290.f, 120.f, 52.f}, {250.f, 105.f, 46.f}};
  for (int k = 0; k < SR_RIDGES; ++k) {
    float ph[3];
    for (int j = 0; j < 3; ++j) ph[j] = rng.uniform();
    for (int i = 0; i < LS_CREST_N; ++i) {
      const float x = fl(LS_CREST_X0 + LS_CREST_STEP * i);
      float y = kBase[k];
      for (int j = 0; j < 3; ++j) y += kAmp[k][j] * sinT(x / kWave[k][j] + ph[j]);
      sl.crest[k][i] = y;
    }
  }
  // The ledge: its tip out over the valley at about the middle of the frame (Mike, 2026-11-12: "only
  // to midway"), the front edge coming up from the lower left, the back edge behind it; the support
  // cliff well back from the tip.
  const float tip_x = rng.range(kTipX0, kTipX1);
  const float tip_y = rng.range(326.f, 336.f);
  const float left_y = rng.range(404.f, 416.f);
  sl.ft = Vec2{tip_x, tip_y};
  sl.fl = Vec2{-24.f, left_y};
  sl.bt = Vec2{tip_x - 34.f, tip_y - 13.f};
  sl.bl = Vec2{-24.f, left_y - 46.f};
  sl.face = rng.range(30.f, 36.f);
  sl.face_tip = 2.f * rng.range(11.f, 14.f);   // a thick, blunt block at the tip
  const float ux = tip_x - rng.range(120.f, 145.f);
  sl.under = Vec2{ux, 0.f};
  // The slope behind: from high on the left down to the slab's back edge.
  sl.hill[0] = rng.range(246.f, 268.f);
  sl.hill_x = rng.range(230.f, 270.f);
  sl.hill[1] = lerp(sl.bl.y, sl.bt.y, (sl.hill_x - sl.bl.x) / (sl.bt.x - sl.bl.x)) - 6.f;
  // Scrub pines on the slope, standing behind the slab (their feet hidden by its back edge or the laurel).
  sl.n_pines = SR_PINES;
  for (int i = 0; i < SR_PINES; ++i) {
    SrPine& p = sl.pines[i];
    const float x = fl(i) * (sl.hill_x - 40.f) / (float)SR_PINES + rng.range(4.f, 30.f);
    const float top = hillAt(sl, x);
    const float back = lerp(sl.bl.y, sl.bt.y, (x - sl.bl.x) / (sl.bt.x - sl.bl.x));
    const float by = rng.range(top + 10.f, maxf(top + 12.f, back - 2.f));
    p.x = (int16_t)floorf(x + 0.5f);
    p.base_y = (int16_t)floorf(by + 0.5f);
    p.h = (uint8_t)rng.rangeInt(30, 52);
    p.w = (uint8_t)rng.rangeInt(20, 32);
  }
  // Laurel along the slab's back edge.
  sl.n_bushes = SR_BUSHES;
  for (int i = 0; i < SR_BUSHES; ++i) {
    SrBush& b = sl.bushes[i];
    const float x = -10.f + fl(i) * (sl.hill_x + 20.f) / (float)SR_BUSHES + rng.range(0.f, 18.f);
    const float back = lerp(sl.bl.y, sl.bt.y, (x - sl.bl.x) / (sl.bt.x - sl.bl.x));
    b.x = (int16_t)floorf(x + 0.5f);
    b.y = (int16_t)floorf(back + 3.f + 0.5f);
    b.rx = (uint8_t)rng.rangeInt(14, 24);
    b.ry = (uint8_t)rng.rangeInt(8, 13);
  }
  // Mist: thin wisps lying in the hollows, along the foot of the far foothill or the top of the near
  // one, one in the hollow under the ledge's tip. (The same random draws as the old fog banks.)
  sl.n_fog = SR_FOG;
  sl.fog_top = 1e9f;
  sl.fog_bot = -1e9f;
  for (int i = 0; i < SR_FOG; ++i) {
    SrFog& f = sl.fog[i];
    float x = 250.f + fl(i) * 600.f / (float)SR_FOG + rng.range(-20.f, 20.f);
    const float yr = rng.range(334.f, 372.f);
    if (i == 1) x = sl.ft.x + 30.f;
    const float y = (i & 1) ? footAt(sl, 0, x) + 3.f + 0.1f * (yr - 334.f) : footAt(sl, 1, x) + 1.f;
    f.x = (int16_t)floorf(x + 0.5f);
    f.y = (int16_t)floorf(y + 0.5f);
    f.rx = (uint8_t)(rng.rangeInt(54, 104) * 3 / 5);
    f.ry = (uint8_t)(rng.rangeInt(8, 15) / 6 + 1);
    sl.fog_top = minf(sl.fog_top, fl(f.y - f.ry) - 2.f);
    sl.fog_bot = maxf(sl.fog_bot, fl(f.y + f.ry) + 2.f);
  }
  // The skyline over the columns the sun can be in: the ridges' (the slab and the slope are lower there).
  for (int i = 0; i < SR_SUN_W; ++i) {
    const float x = fl(SR_SUN_X0 + i);
    const float y = minf(sunriseRidgeTop(sl, x), minf(sunriseRidgeTop(sl, x + 0.5f), sunriseRidgeTop(sl, x + 1.f)));
    sl.sun_top[i] = (int16_t)floorf(y);
  }
}

SrDraw sunriseDrawDone(uint16_t layers) {
  SrDraw d;
  d.layers = layers;
  d.sky = 1.f;
  for (float& v : d.drop) v = 0.f;
  d.fog_u = 1.f;
  d.fg_drop = 0.f;
  d.plants_u = 2.f;
  for (uint8_t& v : d.lit) v = 0;
  d.fog_glow = d.fog_thin = d.slab_lit = 0;
  d.muted = false;
  return d;
}

void sunriseLandDraw(const SunriseLand& sl, Surface& s, const Rect& clip_in, const Palette& pal, const SrDraw& d) {
  const Rect clip = clip_in.clipped(s.bounds());
  if (clip.empty()) return;
  const uint16_t L = d.layers;
  if (L & SRL_BIT(SRL_SKY)) {
    const float k = clamp01(d.sky);
    for (int b = 0; b < 5; ++b) {
      const Pixel c = k >= 1.f ? pal[(Slot)(SKY0 + b)] : mixRGB(pal[SKY4], pal[(Slot)(SKY0 + b)], k);
      fillRect(s, Rect{0, kBand[b], SS_WIDTH, kBand[b + 1]}, c, clip);
    }
  }
  if (L & SRL_BIT(SRL_RIDGES)) {
    for (int k = 0; k < SR_RIDGES; ++k) {
      const Pixel c = ridgeColour(pal, k, d.lit[k], d.muted);
      // A flat fill, nothing along its crest: the light that reaches it only warms the whole ridge.
      drawCrest(s, sl.crest[k], c, clip, d.drop[k]);
    }
    // The valley floor, far below, in shadow.
    float vfloor[LS_CREST_N];
    for (int i = 0; i < LS_CREST_N; ++i) vfloor[i] = 374.f + 5.f * sinT(fl(i) / 9.f) + d.drop[SR_RIDGES - 1];
    const Pixel vf = mixRGB(pal[RIDGE_NEAR], pal[TREES_MID], 0.55f);
    drawCrest(s, vfloor, vf, clip, 0.f);
    for (int x = -8; x < SS_WIDTH + 8; x += 11) {   // the valley's woods: a row of treetops along its edge
      const float y = 374.f + 5.f * sinT(fl(x + 16) / (9.f * fl(LS_CREST_STEP))) + d.drop[SR_RIDGES - 1];
      const float h = 6.f + 5.f * hash01(fl(x), 3.f);
      const Vec2 t[3] = {{fl(x) - 5.f, y + 1.f}, {fl(x) + 5.f, y + 1.f}, {fl(x), y - h}};
      fillPolygon(s, t, 3, mixRGB(vf, pal[TREES_MID], 0.5f), clip);
    }
    // Two wooded foothills over the valley, between the near ridge and the mountain's flank: each
    // nearer one darker and greener, a bumpy line of treetops along its crest, warmed with the near
    // ridge's light. A hollow under the ledge's tip.
    const float dyf = d.drop[SR_RIDGES - 1];
    const float lf = kLitStep * 0.8f * (float)d.lit[SR_RIDGES - 1] * (d.muted ? 0.5f : 1.f);
    for (int k = 0; k < 2; ++k) {
      Pixel body = k == 0 ? mixRGB(pal[RIDGE_NEAR], pal[TREES_MID], 0.7f) : mixRGB(pal[TREES_MID], pal[FOLIAGE_DARK], 0.6f);
      body = mixRGB(body, pal[SUN], lf);
      const float x0 = sl.under.x - 60.f + 30.f * (float)k;
      fillUnder(s, sl, k, true, x0, dyf, body, clip);
      drawCanopy(s, sl, k, true, x0, k == 0 ? 2.6f : 3.6f, 1, dyf, body, mixRGB(body, pal[SUN], 0.06f + lf), clip);
    }
  }
  if ((L & SRL_BIT(SRL_FOG)) && d.fog_u > 0.f) {
    // Pale and cool, the bank's edge softer (a wider, fainter pass under the core).
    // Thin wisps, only a little paler than the hills they lie on.
    Pixel c = mixRGB(mixRGB(mixRGB(pal[SKY2], pal[CLOUD_LIT], 0.6f), pal[SKY4], 0.25f), pal[TREES_MID], 0.58f);
    if (d.fog_glow) c = mixRGB(c, pal[SUN], (d.muted ? 0.05f : 0.1f) * (float)d.fog_glow);
    const Pixel soft = mixRGB(c, mixRGB(pal[RIDGE_NEAR], pal[TREES_MID], 0.55f), 0.5f);
    const float thin = 1.f - 0.2f * (float)d.fog_thin;
    const int n = sl.n_fog - 2 * d.fog_thin;
    for (int i = 0; i < n; ++i) {
      const SrFog& f = sl.fog[i];
      const float k = easeOutCubic(clamp01(d.fog_u * 1.6f - 0.6f * hash01(fl(f.x), fl(f.y))));
      if (k <= 0.f) continue;
      fillEllipse(s, Vec2{fl(f.x), fl(f.y) + 1.f}, (fl(f.rx) + 12.f) * k * thin, (fl(f.ry) + 1.5f) * k * thin, soft, clip);
      fillEllipse(s, Vec2{fl(f.x), fl(f.y)}, fl(f.rx) * k * thin, fl(f.ry) * k * thin, c, clip);
    }
  }
  if (L & SRL_BIT(SRL_HILL)) {
    drawFlank(sl, s, pal, clip, d);   // the mountain's flank under the ledge (the cliff covers its left end)
    const float dy = d.fg_drop;
    Vec2 p[24];
    int n = 0;
    for (int i = 0; i <= 20; ++i) {
      const float x = fl(LS_CREST_X0) + (sl.hill_x - fl(LS_CREST_X0)) * (float)i / 20.f;
      p[n++] = Vec2{x, hillAt(sl, x) + dy};
    }
    p[n++] = Vec2{sl.hill_x, fl(SS_HEIGHT + 2)};
    p[n++] = Vec2{fl(LS_CREST_X0), fl(SS_HEIGHT + 2)};
    fillPolygon(s, p, n, mixRGB(pal[FOLIAGE_DARK], pal[GROUND_DARK], 0.4f), clip);
    if (d.plants_u >= 0.f) {
      for (int i = 0; i < sl.n_pines; ++i) {
        const SrPine& q = sl.pines[i];
        drawPine(s, q, pal[FOLIAGE_DARK], pal[TRUNK], clip, plantScale(d.plants_u, fl(q.x), fl(q.base_y)), dy);
      }
      for (int i = 0; i < sl.n_bushes; ++i) {
        const SrBush& b = sl.bushes[i];
        drawBush(s, b, mixRGB(pal[FOLIAGE_DARK], pal[ACTOR], 0.25f), pal[FOLIAGE], clip,
                 plantScale(d.plants_u, fl(b.x), fl(b.y)), dy);
      }
    }
  }
  if (L & SRL_BIT(SRL_SLAB)) drawSlab(sl, s, pal, clip, d);
}

}  // namespace ss
