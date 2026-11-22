// The mushroom trip's sky, drawn by itself (stage 22 step B4; MUSHROOM_PLAN.md sections 6.2 and 11.2,
// REVIEW_FIXES F1, F2, F7). Not yet through drawSky (that is step C5b):
//   - at clock 0 the band draw gives exactly the natural sky's bands (the old fillRect path);
//   - it writes every pixel of its clip's sky rows, whatever was there, and a band drawn into its own
//     32-row strip equals the same rows of a whole frame drawn band by band (with a ramp of clocks);
//   - the edges stay inside rest +- swing, never cross, and the lowest stays above row 240;
//   - no flashing: stepping the sky a tick at a time through its 104 s, the mean luminance of the sky
//     and of each of the ten bands moves by at most two RGB565 levels a tick (the plan's 0.4 % is less
//     than one level of green), at most 5 % in any second, and never turns round by more than 1 % three
//     times within a second;
//   - the night rule: every trip colour under NIGHT_LUM_MAX and the stars and halo under
//     NIGHT_HIGHLIGHT_MAX at night_dim 1; by day no band brighter than the palette's brightest sky slot;
//   - speeds: no star, edge or halo moves more than 1.5 px between two repaints of its band (0.4 s);
//   - the latch: the rolling repaint leaves every band at 0 (natural) once the trip is over (F2);
//   - the halo stays inside skyTripHaloReach (F1); the stars keep away from it;
//   - the deck palette is the plain one when the colour is off.
// Plain executable; returns non-zero on failure.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>

#include <time.h>

#include "render/renderer.h"
#include "scene/day_trip.h"
#include "scene/landscape.h"
#include "scene/skytrip.h"
#include "scene/spine.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

static int g_fail = 0;
static long g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static const size_t kFrame = (size_t)SS_WIDTH * SS_HEIGHT;

static float lum(Pixel p) { return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f; }

static void dayPal(Palette& pal, float mood = 3.f) {
  WeatherTint wt;
  memset(&wt, 0, sizeof wt);
  computePalette(mood, wt, pal);
}

static void makeLand(uint64_t seed, Landscape& ls) {
  memset(&ls, 0, sizeof ls);
  Rng rng;
  rng.seed(seed);
  landscapeGenerate(ls, rng);
}

static SkyTripSun sunAt(const Palette& pal, float x, float y) {
  SkyTripSun s;
  s.on = true;
  s.x = x;
  s.y = y;
  s.r = 30.f;
  s.col = pal[SUN];
  return s;
}

// The whole sky drawn band by band, band i at clock tq[i] seconds.
static void drawBands(const Landscape& ls, Pixel* px, const Palette& pal, const SkyTripSun& sun, const float* t) {
  Surface s{px, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  for (int i = 0; i < SKYTRIP_BANDS; ++i)
    skyTripBand(ls, s, pal, sun, t[i], Rect{0, i * SKYTRIP_BAND_ROWS, SS_WIDTH, (i + 1) * SKYTRIP_BAND_ROWS});
}

static void testNatural(Pixel* a, Pixel* b) {
  Landscape ls;
  makeLand(5, ls);
  Palette pal;
  dayPal(pal);
  memset(a, 0, kFrame * sizeof(Pixel));
  memset(b, 0, kFrame * sizeof(Pixel));
  Surface sa{a, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  static const int kBand[6] = {0, 58, 118, 178, 228, LS_GROUND_Y};
  for (int k = 0; k < 5; ++k) fillRect(sa, Rect{0, kBand[k], SS_WIDTH, kBand[k + 1]}, pal[(Slot)(SKY0 + k)], sa.bounds());
  SkyTripSun none = {};
  const float t0[SKYTRIP_BANDS] = {0.f};
  drawBands(ls, b, pal, none, t0);
  CHECK(memcmp(a, b, kFrame * sizeof(Pixel)) == 0, "clock 0 is the natural sky, bit for bit");
  CHECK(skyTripWave(0.f) == 0.f && skyTripColour(0.f) == 0.f && skyTripStarK(0, 0.f) == 0.f, "everything off at 0");
  CHECK(skyTripWave(103.f) == 0.f && skyTripColour(103.f) == 0.f, "everything off at the end");
  for (int i = 0; i < LS_STAR_COUNT; ++i) CHECK(skyTripStarK(i, 101.f) == 0.f, "star %d gone by 101 s", i);
}

static void testPurity(Pixel* a, Pixel* b) {
  Landscape ls;
  makeLand(11, ls);
  Palette pal;
  dayPal(pal);
  const SkyTripSun sun = sunAt(pal, 560.f, 96.f);
  for (float base : {3.f, 20.f, 50.f, 88.f, 97.f}) {
    float t[SKYTRIP_BANDS];
    for (int i = 0; i < SKYTRIP_BANDS; ++i) t[i] = base + 0.04f * (float)((i * 7) % 10);   // a ramp of latched clocks
    // Garbage underneath must not matter: every pixel of the clip's sky rows is written.
    for (size_t k = 0; k < kFrame; ++k) a[k] = (Pixel)(k * 2654435761u >> 7);
    memset(b, 0, kFrame * sizeof(Pixel));
    drawBands(ls, a, pal, sun, t);
    drawBands(ls, b, pal, sun, t);
    CHECK(memcmp(a, b, (size_t)SS_WIDTH * LS_GROUND_Y * sizeof(Pixel)) == 0, "every sky pixel written (t %.0f)", (double)base);
    // Each band alone in its own strip equals the frame's rows.
    static Pixel strip[SS_WIDTH * SKYTRIP_BAND_ROWS];
    for (int i = 0; i < SKYTRIP_BANDS; ++i) {
      for (int k = 0; k < SS_WIDTH * SKYTRIP_BAND_ROWS; ++k) strip[k] = 0x5555;
      Surface ss{strip, SS_WIDTH, SKYTRIP_BAND_ROWS, SS_WIDTH, i * SKYTRIP_BAND_ROWS};
      skyTripBand(ls, ss, pal, sun, t[i], ss.bounds());
      const int rows = i == SKYTRIP_BANDS - 1 ? LS_GROUND_Y - i * SKYTRIP_BAND_ROWS : SKYTRIP_BAND_ROWS;
      CHECK(memcmp(strip, b + (size_t)i * SKYTRIP_BAND_ROWS * SS_WIDTH, (size_t)rows * SS_WIDTH * sizeof(Pixel)) == 0,
            "band %d alone equals the frame (t %.0f)", i, (double)base);
      if (i == SKYTRIP_BANDS - 1) CHECK(strip[(size_t)(LS_GROUND_Y - i * SKYTRIP_BAND_ROWS) * SS_WIDTH] == 0x5555, "nothing below the ground line");
    }
    // A clip smaller than the band (a dirty rectangle, or the sun's repaint rows cut to the band) gives
    // the same pixels inside it. The one-row clips sit round every star's reach (its circle, its dot and
    // the antialiasing), where a star skipped too early would leave its rows unpainted.
    int bad = 0;
    auto subClip = [&](const Rect& r0) {
      const int band = r0.y0 / SKYTRIP_BAND_ROWS;
      if (r0.y0 < 0 || band >= SKYTRIP_BANDS) return;
      const Rect r = r0.clipped(Rect{0, band * SKYTRIP_BAND_ROWS, SS_WIDTH, (band + 1) * SKYTRIP_BAND_ROWS});
      if (r.empty()) return;
      for (size_t k = 0; k < kFrame; ++k) a[k] = (Pixel)(k * 2246822519u >> 9);
      Surface sa{a, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
      skyTripBand(ls, sa, pal, sun, t[band], r);
      for (int y = r.y0; y < r.y1 && y < LS_GROUND_Y; ++y)
        for (int x = r.x0; x < r.x1; ++x)
          if (a[(size_t)y * SS_WIDTH + x] != b[(size_t)y * SS_WIDTH + x]) ++bad;
    };
    for (int i = 0; i < SKYTRIP_BANDS; ++i) {
      const int y0 = i * SKYTRIP_BAND_ROWS;
      subClip(Rect{0, y0 + 5, SS_WIDTH, y0 + 19});
      subClip(Rect{137, y0 + 1, 611, y0 + 30});
      subClip(Rect{0, y0 + 31, SS_WIDTH, y0 + 32});
    }
    for (int i = 0; i < LS_STAR_COUNT; ++i)
      for (int off = -17; off <= 17; ++off) {
        const int y = ls.stars[i].y + off;
        if (off < -11 || off > 11) subClip(Rect{0, y, SS_WIDTH, y + 1});
      }
    CHECK(bad == 0, "a smaller clip gives the same pixels (t %.0f: %d differ)", (double)base, bad);
  }
}

static void testEdges() {
  float lowest = 0.f;
  for (int T = 0; T <= SKYTRIP_SECONDS * SS_TICK_HZ; ++T) {
    const float t = (float)T / (float)SS_TICK_HZ;
    for (int x = -16; x <= 816; x += 8) {
      float prev = -1e9f;
      for (int j = 0; j < 4; ++j) {
        const float y = skyTripEdgeY(j, (float)x, t);
        CHECK(fabsf(y - (float)skyTripEdgeRest(j)) <= skyTripEdgeAmp(j) + 1e-3f, "edge %d inside its swing at t %.2f x %d", j,
              (double)t, x);
        CHECK(y > prev + 20.f, "edges never cross (j %d t %.2f x %d)", j, (double)t, x);
        prev = y;
        if (j == 3 && y > lowest) lowest = y;
      }
    }
  }
  CHECK(lowest < 240.f, "the lowest edge stays above row 240 (%.1f)", (double)lowest);
}

// Mean luminance of rows [y0, y1) every 2nd pixel.
static float meanLum(const Pixel* px, int y0, int y1) {
  double s = 0.0;
  long n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < SS_WIDTH; x += 2) {
      s += (double)lum(px[(size_t)y * SS_WIDTH + x]);
      ++n;
    }
  return (float)(s / (double)n);
}

static void testNoFlashing(Pixel* px) {
  Landscape ls;
  makeLand(23, ls);
  Palette pal;
  dayPal(pal);
  const SkyTripSun sun = sunAt(pal, 300.f, 110.f);
  const int kN = SKYTRIP_SECONDS * SS_TICK_HZ + 1;
  const int kRegions = SKYTRIP_BANDS + 1;   // the ten bands and the whole sky
  static float m[SKYTRIP_SECONDS * SS_TICK_HZ + 1][SKYTRIP_BANDS + 1];
  float worst = 0.f;
  for (int T = 0; T < kN; ++T) {
    float t[SKYTRIP_BANDS];
    for (int i = 0; i < SKYTRIP_BANDS; ++i) t[i] = (float)T / (float)SS_TICK_HZ;   // every band current
    drawBands(ls, px, pal, sun, t);
    for (int r = 0; r < SKYTRIP_BANDS; ++r) {
      const int y1 = r == SKYTRIP_BANDS - 1 ? LS_GROUND_Y : (r + 1) * SKYTRIP_BAND_ROWS;
      m[T][r] = meanLum(px, r * SKYTRIP_BAND_ROWS, y1);
    }
    m[T][SKYTRIP_BANDS] = meanLum(px, 0, LS_GROUND_Y);
  }
  int worst_rev = 0;
  float worst_s = 0.f;
  for (int r = 0; r < kRegions; ++r) {
    // A turn is the luminance coming back by more than 1 % from its last high or low (the measure of a
    // flash, not of the one-level RGB565 steps each channel takes at its own moment as a colour mixes).
    static int turn_at[SKYTRIP_SECONDS * SS_TICK_HZ + 1];
    int n_turns = 0, dir = 0;
    float ext = m[0][r];
    for (int T = 1; T < kN; ++T) {
      // The plan asked for under 0.4 % a tick. In RGB565 a flat band cannot change by less than one
      // level of one channel: 0.67 % (red) or 1.1 % (green) of luminance. So: at most two such steps in
      // one tick for a band, 1 % for the whole sky, and at most 5 % over any second (a slow creep, the
      // way every palette change in this scene already moves).
      const float d = m[T][r] - m[T - 1][r];
      if (fabsf(d) > worst) worst = fabsf(d);
      const float lim = r == SKYTRIP_BANDS ? 0.01f : 0.0225f;
      CHECK(fabsf(d) <= lim, "region %d tick %d: luminance step %.4f", r, T, (double)d);
      if (T >= SS_TICK_HZ) {
        const float ds = m[T][r] - m[T - SS_TICK_HZ][r];
        if (fabsf(ds) > worst_s) worst_s = fabsf(ds);
        CHECK(fabsf(ds) <= 0.05f, "region %d: %.3f in the second to tick %d", r, (double)ds, T);
      }
      const float v = m[T][r];
      if (dir >= 0 && v > ext) ext = v;
      else if (dir <= 0 && v < ext) ext = v;
      if (dir >= 0 && v < ext - 0.01f) {
        if (dir > 0) turn_at[n_turns++] = T;
        dir = -1;
        ext = v;
      } else if (dir <= 0 && v > ext + 0.01f) {
        if (dir < 0) turn_at[n_turns++] = T;
        dir = 1;
        ext = v;
      }
    }
    for (int k = 0; k + 2 < n_turns; ++k) {
      const bool three = turn_at[k + 2] - turn_at[k] < SS_TICK_HZ;
      if (three) worst_rev = 3;
      CHECK(!three, "region %d: three turns within a second from tick %d", r, turn_at[k]);
    }
    if (n_turns > 0 && worst_rev < 1) worst_rev = 1;
  }
  printf("  no flashing: worst luminance step %.4f a tick, %.3f in a second; %s\n", (double)worst, (double)worst_s,
         worst_rev >= 3 ? "three 1 pc turns inside a second" : (worst_rev ? "some 1 pc turns, never three in a second" : "no 1 pc turn at all"));
}

// The same, as the screen will really show it: one band repainted a tick from its latched clock (the
// rolling repaint dayTrack will run), so a band jumps by 0.4 s of change at once and the newest band
// meets one 0.36 s older. Run past the end so the hand-back to the natural sky (F2) is measured too.
static void testNoFlashingRolled(Pixel* px) {
  Landscape ls;
  makeLand(23, ls);
  Palette pal;
  dayPal(pal);
  const SkyTripSun sun = sunAt(pal, 300.f, 110.f);
  const int kN = (SKYTRIP_SECONDS + 2) * SS_TICK_HZ;
  static float m[(SKYTRIP_SECONDS + 2) * SS_TICK_HZ][SKYTRIP_BANDS + 1];
  LsTrip trip;
  memset(&trip, 0, sizeof trip);
  float worst_band = 0.f, worst_sky = 0.f;
  for (int T = 0; T < kN; ++T) {
    trip.tq[T % SKYTRIP_BANDS] = skyTripLatch((float)T / (float)SS_TICK_HZ);
    float t[SKYTRIP_BANDS];
    for (int i = 0; i < SKYTRIP_BANDS; ++i) t[i] = skyTripSeconds(trip.tq[i]);
    drawBands(ls, px, pal, sun, t);
    for (int r = 0; r < SKYTRIP_BANDS; ++r) {
      const int y1 = r == SKYTRIP_BANDS - 1 ? LS_GROUND_Y : (r + 1) * SKYTRIP_BAND_ROWS;
      m[T][r] = meanLum(px, r * SKYTRIP_BAND_ROWS, y1);
    }
    m[T][SKYTRIP_BANDS] = meanLum(px, 0, LS_GROUND_Y);
  }
  CHECK(!skyTripOn(trip), "the rolled sky is natural again after the trip");
  for (int r = 0; r <= SKYTRIP_BANDS; ++r) {
    static int turn_at[(SKYTRIP_SECONDS + 2) * SS_TICK_HZ];
    int n_turns = 0, dir = 0;
    float ext = m[0][r];
    for (int T = 1; T < kN; ++T) {
      const float d = fabsf(m[T][r] - m[T - 1][r]);
      // A band takes ten ticks of the slow change at once: at most about 1.5 % of luminance (the
      // fastest second of the colour ramp is under 3 %). The whole sky sees one band of ten move.
      if (r == SKYTRIP_BANDS) worst_sky = fmaxf(worst_sky, d);
      else worst_band = fmaxf(worst_band, d);
      CHECK(d <= (r == SKYTRIP_BANDS ? 0.01f : 0.03f), "rolled region %d tick %d: luminance step %.4f", r, T, (double)d);
      const float v = m[T][r];
      if (dir >= 0 && v > ext) ext = v;
      else if (dir <= 0 && v < ext) ext = v;
      if (dir >= 0 && v < ext - 0.01f) {
        if (dir > 0) turn_at[n_turns++] = T;
        dir = -1;
        ext = v;
      } else if (dir <= 0 && v > ext + 0.01f) {
        if (dir < 0) turn_at[n_turns++] = T;
        dir = 1;
        ext = v;
      }
    }
    for (int k = 0; k + 2 < n_turns; ++k)
      CHECK(turn_at[k + 2] - turn_at[k] >= SS_TICK_HZ, "rolled region %d: three turns within a second from tick %d", r, turn_at[k]);
  }
  printf("  no flashing, rolled one band a tick: worst band step %.4f, worst whole-sky step %.4f a tick\n", (double)worst_band,
         (double)worst_sky);
}

static void testNightRule(Pixel* px) {
  Landscape ls;
  makeLand(3, ls);
  for (float mood : {0.f, 0.3f, 5.7f, 3.f}) {
    WeatherTint wt;
    memset(&wt, 0, sizeof wt);
    wt.night_dim = 1.f;
    Palette pal;
    computePalette(mood, wt, pal);
    const SkyTripSun sun = sunAt(pal, 400.f, 120.f);
    float worst_band = 0.f, worst_px = 0.f;
    for (int T = 0; T <= SKYTRIP_SECONDS * SS_TICK_HZ; T += 25) {
      const float t = (float)T / (float)SS_TICK_HZ;
      for (int b = 0; b < 5; ++b) worst_band = fmaxf(worst_band, lum(skyTripBandColour(pal, b, t)));
      float tt[SKYTRIP_BANDS];
      for (int i = 0; i < SKYTRIP_BANDS; ++i) tt[i] = t;
      if (T % 250 == 0) {
        drawBands(ls, px, pal, sun, tt);
        for (size_t k = 0; k < (size_t)SS_WIDTH * LS_GROUND_Y; ++k) worst_px = fmaxf(worst_px, lum(px[k]));
      }
    }
    CHECK(worst_band <= NIGHT_LUM_MAX + 0.005f, "night (mood %.1f): bands at most %.2f (%.3f)", (double)mood, (double)NIGHT_LUM_MAX,
          (double)worst_band);
    CHECK(worst_px <= NIGHT_HIGHLIGHT_MAX + 0.005f, "night (mood %.1f): stars and halo at most %.2f (%.3f)", (double)mood,
          (double)NIGHT_HIGHLIGHT_MAX, (double)worst_px);
  }
  // By day: never brighter than the palette's own brightest sky slot.
  for (float mood : {2.5f, 3.f, 3.5f, 4.2f}) {
    Palette pal;
    dayPal(pal, mood);
    float cap = 0.f;
    for (int b = 0; b < 5; ++b) cap = fmaxf(cap, lum(pal[(Slot)(SKY0 + b)]));
    for (int T = 0; T <= SKYTRIP_SECONDS * SS_TICK_HZ; T += 10)
      for (int b = 0; b < 5; ++b) {
        const float l = lum(skyTripBandColour(pal, b, (float)T / (float)SS_TICK_HZ));
        CHECK(l <= cap + 0.005f, "day (mood %.1f) band %d at %.2f over the sky's own %.2f", (double)mood, b, (double)l, (double)cap);
      }
  }
}

static void testSpeeds() {
  Landscape ls;
  makeLand(7, ls);
  Palette pal;
  dayPal(pal);
  SkyTripSun none = {};
  const float dt = (float)SKYTRIP_BANDS * (float)kTripBandEvery / (float)SS_TICK_HZ;   // between two repaints of a band
  float worst_star = 0.f, worst_edge = 0.f;
  for (int T = 0; T + 10 <= SKYTRIP_SECONDS * SS_TICK_HZ; ++T) {
    const float t = (float)T / (float)SS_TICK_HZ;
    for (int i = 0; i < LS_STAR_COUNT; ++i) {
      SkyTripStar a, b;
      if (!skyTripStar(ls, none, i, t, a) || !skyTripStar(ls, none, i, t + dt, b)) continue;
      const float d = hypotf(b.p[0].x - a.p[0].x, b.p[0].y - a.p[0].y);
      worst_star = fmaxf(worst_star, d);
      CHECK(d <= 1.5f, "star %d moves %.2f px in %.1f s at t %.2f", i, (double)d, (double)dt, (double)t);
    }
    for (int j = 0; j < 4; ++j)
      for (int x = 0; x <= 800; x += 4) {
        const float d = fabsf(skyTripEdgeY(j, (float)x, t + dt) - skyTripEdgeY(j, (float)x, t));
        worst_edge = fmaxf(worst_edge, d);
        CHECK(d <= 1.5f, "edge %d moves %.2f px at x %d t %.2f", j, (double)d, x, (double)t);
      }
  }
  // The halo's breathing, measured on the pixels: how far right of the sun the band with the sun's row
  // differs from the same band with no sun, at t and one repaint later. Whole pixels, so 1 px of the
  // allowance is the rounding.
  static Pixel with_sun[SS_WIDTH * SKYTRIP_BAND_ROWS], no_sun[SS_WIDTH * SKYTRIP_BAND_ROWS];
  const SkyTripSun sun = sunAt(pal, 300.f, 110.f);
  const int band = 110 / SKYTRIP_BAND_ROWS, row = 110 - band * SKYTRIP_BAND_ROWS;
  auto reachAt = [&](float t) {
    Surface sa{with_sun, SS_WIDTH, SKYTRIP_BAND_ROWS, SS_WIDTH, band * SKYTRIP_BAND_ROWS};
    Surface sb{no_sun, SS_WIDTH, SKYTRIP_BAND_ROWS, SS_WIDTH, band * SKYTRIP_BAND_ROWS};
    skyTripBand(ls, sa, pal, sun, t, sa.bounds());
    skyTripBand(ls, sb, pal, none, t, sb.bounds());
    // Only out to the halo's largest reach and a little more: a star the sun hides (68 px and more from
    // it) must not count as halo.
    int far = 0;
    for (int x = 300; x < 300 + 56; ++x)
      if (with_sun[(size_t)row * SS_WIDTH + (size_t)x] != no_sun[(size_t)row * SS_WIDTH + (size_t)x]) far = x - 300;
    return far;
  };
  int worst_halo = 0, halo_min = 1000, halo_max = 0;
  for (int T = 30 * SS_TICK_HZ; T + 10 <= 84 * SS_TICK_HZ; T += 3) {
    const float t = (float)T / (float)SS_TICK_HZ;
    const int r0 = reachAt(t), r1 = reachAt(t + dt);
    halo_min = r0 < halo_min ? r0 : halo_min;
    halo_max = r0 > halo_max ? r0 : halo_max;
    const int d = r1 > r0 ? r1 - r0 : r0 - r1;
    worst_halo = d > worst_halo ? d : worst_halo;
    CHECK(d <= 2, "the halo's edge moves %d px in %.1f s at t %.2f", d, (double)dt, (double)t);
  }
  CHECK(halo_max - halo_min >= 3, "the halo breathes (%d to %d px)", halo_min, halo_max);
  printf("  speeds over %.1f s: stars %.2f px, edges %.2f px, the halo's edge %d px at most (it breathes %d to %d px)\n",
         (double)dt, (double)worst_star, (double)worst_edge, worst_halo, halo_min, halo_max);
}

static void testLatch() {
  CHECK(skyTripLatch(0.f) == 0 && skyTripLatch(102.f) == 0 && skyTripLatch(110.f) == 0, "0 outside the trip");
  CHECK(skyTripLatch(0.04f) == 1 && skyTripLatch(50.f) == 1250, "ticks inside it");
  CHECK(skyTripLatch(0.001f) == 1, "never 0 while it shows");
  // The rolling repaint, one band a tick, over the whole trip and on: every band ends natural.
  LsTrip trip;
  memset(&trip, 0, sizeof trip);
  bool on_mid = false;
  for (int T = 0; T <= SKYTRIP_SECONDS * SS_TICK_HZ; ++T) {
    trip.tq[T % SKYTRIP_BANDS] = skyTripLatch((float)T / (float)SS_TICK_HZ);
    if (T == 50 * SS_TICK_HZ) on_mid = skyTripOn(trip);
    if (T >= 102 * SS_TICK_HZ) {   // every ramp is flat by then, so a band at its last clock already looks natural
      const float t = skyTripSeconds(trip.tq[T % SKYTRIP_BANDS]);
      CHECK(skyTripWave(t) == 0.f && skyTripColour(t) == 0.f, "flat at the hand-back");
    }
  }
  CHECK(on_mid, "on in the middle");
  CHECK(!skyTripOn(trip), "every band back to 0 by the end (F2)");
  for (int i = 0; i < SKYTRIP_BANDS; ++i) {
    const float t = 101.6f + 0.04f * (float)i;   // the last clocks a band can hold before 0
    CHECK(skyTripWave(t) < 0.005f && skyTripColour(t) == 0.f, "nearly natural before the hand-back (band %d)", i);
  }
}

static void testHalo(Pixel* a, Pixel* b) {
  Landscape ls;
  makeLand(9, ls);
  Palette pal;
  dayPal(pal);
  const SkyTripSun sun = sunAt(pal, 480.f, 110.f);
  SkyTripSun none = {};
  // No stars yet at 14.5 s, so the only difference is the halo.
  float t[SKYTRIP_BANDS];
  for (int i = 0; i < SKYTRIP_BANDS; ++i) t[i] = 14.5f;
  drawBands(ls, a, pal, sun, t);
  drawBands(ls, b, pal, none, t);
  const int reach = skyTripHaloReach(30);
  int out = 0, in = 0;
  for (int y = 0; y < LS_GROUND_Y; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) {
      if (a[(size_t)y * SS_WIDTH + x] == b[(size_t)y * SS_WIDTH + x]) continue;
      const float dx = (float)x + 0.5f - sun.x, dy = (float)y + 0.5f - sun.y;
      if (dx * dx + dy * dy > (float)(reach * reach)) ++out;
      else ++in;
    }
  CHECK(in > 1000, "the halo shows (%d px)", in);
  CHECK(out == 0, "the halo stays inside skyTripHaloReach (%d px out)", out);
  for (int i = 0; i < LS_STAR_COUNT; ++i) {
    SkyTripSun near = sun;
    near.x = (float)ls.stars[i].x;
    near.y = (float)ls.stars[i].y;
    SkyTripStar st;
    CHECK(!skyTripStar(ls, near, i, 50.f, st), "star %d hides by the sun", i);
  }
  // The deck palette is the plain one with the colour off, and changes with it on.
  Palette d;
  skyTripDeckPal(pal, 0.f, d);
  CHECK(memcmp(&d, &pal, sizeof pal) == 0, "deck palette unchanged at 0");
  skyTripDeckPal(pal, 50.f, d);
  CHECK(d.c[CLOUD_SHADE] != pal.c[CLOUD_SHADE], "the deck takes the tint");
  CHECK(skyTripRidgeLift(0.f) == 0.f && skyTripRidgeLift(50.f) <= 2.f, "the ridge lifts 2 px at most");
}

// ---- through the renderer and the app (step C5b; REVIEW_FIXES F1, F2, F3, F6, F28) --------------------------------

// tripSkyTrack against a bare Renderer: whatever the renders' cadence, and with a sun repaint waiting, it
// never makes a waiting repaint taller than two bands; a band that cannot join waits and the counter stays.
static void testPatches() {
  World* w = (World*)calloc(1, sizeof(World));
  Renderer r;
  memset(static_cast<void*>(&r), 0, sizeof r);
  for (int every = 1; every <= 7; ++every) {
    for (int sun = 0; sun < 2; ++sun) {
      memset(static_cast<void*>(w), 0, sizeof(World));
      tripSkyOnly(*w, false);
      r.patch_n = 0;
      int worst = 0, latched = 0, stale_max = 0;
      uint16_t last[SKYTRIP_BANDS] = {0};
      int age[SKYTRIP_BANDS] = {0};
      for (int t = 0; t < 1500 && w->day.kind == DAY_TRIP; ++t) {
        if (sun && t % 37 == 5) r.repaintRows(40, 150);   // the real sun moved: its rows wait too
        int before[2] = {0, 0};
        for (int k = 0; k < r.patch_n; ++k) before[k] = r.patch_y1[k] - r.patch_y0[k];
        tripDayStep(*w, false);
        tripSkyTrack(*w, r);
        for (int k = 0; k < r.patch_n; ++k) {
          const int h = r.patch_y1[k] - r.patch_y0[k];
          const int allowed = before[k] > 2 * SKYTRIP_BAND_ROWS ? before[k] : 2 * SKYTRIP_BAND_ROWS;
          if (h > allowed && h > worst) worst = h;
        }
        for (int i = 0; i < SKYTRIP_BANDS; ++i) {
          ++age[i];
          if (w->land.trip.tq[i] != last[i]) {
            last[i] = w->land.trip.tq[i];
            if (t > 40 && age[i] > stale_max) stale_max = age[i];
            age[i] = 0;
            ++latched;
          }
        }
        if (t % every == every - 1) r.patch_n = 0;   // a render
      }
      CHECK(worst == 0, "one render every %d ticks%s: a repaint grew to %d rows (64 at most)", every, sun ? ", sun moving" : "", worst);
      CHECK(latched > 1500 / (every > 3 ? every : 3) / 2, "one render every %d ticks: the sky still moves (%d bands latched)", every, latched);
      if (every == 1 && !sun) CHECK(stale_max == SKYTRIP_BANDS, "rendered every tick each band is repainted every 10 ticks (%d)", stale_max);
      if (every == 3 && !sun) printf("    three ticks a render: three bands a render, the stalest band %d ticks old\n", stale_max);
      if (every == 4 && !sun) printf("    four ticks a render: three bands a render, the stalest band %d ticks old\n", stale_max);
    }
  }
  // The way home after a beat cut short: two bands a tick, never more.
  memset(static_cast<void*>(w), 0, sizeof(World));
  for (int i = 0; i < SKYTRIP_BANDS; ++i) w->land.trip.tq[i] = 1200;
  r.patch_n = 0;
  for (int t = 0; t < 5; ++t) {
    tripSkyTrack(*w, r);
    int left = 0;
    for (int i = 0; i < SKYTRIP_BANDS; ++i) left += w->land.trip.tq[i] != 0;
    CHECK(left == SKYTRIP_BANDS - 2 * (t + 1) && r.patch_n == 2, "the way home, tick %d: %d bands left, %d repaints waiting", t, left, r.patch_n);
    r.patch_n = 0;
  }
  CHECK(!skyTripOn(w->land.trip), "natural after five ticks (F2)");
  free(w);
}

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }

// The sky alone through the app, drawn tick by tick: equal to a full redraw at checkpoints, on the tick the
// real sun moves one pixel (F1) and after it; at one band every second tick the clock runs at half speed
// and no band's latched clock jumps more than 0.4 s (F3); natural again at the end; and what it costs.
static void testThroughApp(bool slow, bool real_sun) {
  const HostAlloc alloc = {hostAlloc, hostFree, nullptr};
  void* mem = malloc(SS_APP_BYTES);
  Pixel* fb = (Pixel*)calloc(kFrame, sizeof(Pixel));
  Pixel* keep = (Pixel*)calloc(kFrame, sizeof(Pixel));
  Surface surf{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_EVENING);   // an evening loop by day: no daytime major takes the day union from the hook
  in.have_outside = true;
  if (real_sun) {
    in.sky.have = true;
    in.sky.sun_az = 205.f;
    in.sky.sun_alt = 38.f;
    in.sky.moon_az = 20.f;
    in.sky.moon_alt = -30.f;
  }
  App* app = appStart(mem, alloc, 4, in);
  CHECK(app != nullptr, "the app starts");
  if (!app) return;
  World* w = const_cast<World*>(appWorld(app));
  const char* tag = slow ? "slow" : (real_sun ? "full pace" : "parked sun");
  int diffs = 0, checks = 0;
  auto fullCheck = [&](const char* what, int t) {
    memcpy(keep, fb, kFrame * sizeof(Pixel));
    appInvalidate(app);
    appRender(app, surf);
    ++checks;
    if (memcmp(keep, fb, kFrame * sizeof(Pixel)) != 0) {
      ++diffs;
      int first = -1, cnt = 0;
      for (size_t i = 0; i < kFrame; ++i)
        if (keep[i] != fb[i]) {
          if (first < 0) first = (int)i;
          ++cnt;
        }
      if (diffs <= 4) printf("    %s: %s at tick %d: %d px differ from a full redraw, first at (%d, %d)\n", tag, what, t, cnt, first % SS_WIDTH, first / SS_WIDTH);
    }
  };
  long strips_before = 0, strips_sky = 0;
  int n_before = 0, n_sky = 0;
  double us_before = 0., us_sky = 0.;
  auto frame = [&](long& strips, int& n, double& us) {
    appTick(app);
    timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    strips += appRender(app, surf);
    clock_gettime(CLOCK_MONOTONIC, &b);
    us += (double)(b.tv_sec - a.tv_sec) * 1.0e6 + (double)(b.tv_nsec - a.tv_nsec) * 1.0e-3;
    ++n;
  };
  for (int t = 0; t < 400; ++t) {   // past the build-up
    appTick(app);
    appRender(app, surf);
  }
  for (int t = 0; t < 1000; ++t) frame(strips_before, n_before, us_before);
  tripSkyOnly(*w, slow);
  uint16_t last[SKYTRIP_BANDS] = {0};
  int jump_max = 0, moves = 0;
  float clock_end = 0.f;
  const SkyDraw sky0 = landscapeSky(in.sky, 3.f, 0.f);
  int t = 0;
  for (; t < 7000 && w->day.kind == DAY_TRIP; ++t) {
    const float st = tripSkyClock(*w);
    clock_end = st;
    // From 40 s on the real sun moves a pixel now and then, sideways and down, as in an afternoon.
    const bool move = real_sun && st >= 40.f && st < 80.f && t % 53 == 0;
    if (move) {
      const SkyDraw was = landscapeSky(in.sky, 3.f, 0.f);
      for (int guard = 0; guard < 200; ++guard) {
        in.sky.sun_az += 0.05f;
        if (moves % 3 == 2) in.sky.sun_alt -= 0.05f;
        const SkyDraw now = landscapeSky(in.sky, 3.f, 0.f);
        if (now.sun_x != was.sun_x || now.sun_y != was.sun_y) break;
      }
      appSetInputs(app, in);
      ++moves;
    }
    frame(strips_sky, n_sky, us_sky);
    for (int i = 0; i < SKYTRIP_BANDS; ++i) {
      const uint16_t v = w->land.trip.tq[i];
      if (v != last[i] && v != 0 && last[i] != 0 && (int)v - (int)last[i] > jump_max) jump_max = (int)v - (int)last[i];
      last[i] = v;
    }
    if (move) fullCheck("the sun moved a pixel", t);
    else if (t % 257 == 100) fullCheck("the sky alone", t);
  }
  const SkyDraw sky1 = landscapeSky(in.sky, 3.f, 0.f);
  const int sun_px = abs(sky1.sun_x - sky0.sun_x) + abs(sky1.sun_y - sky0.sun_y);
  CHECK(w->day.kind == DAY_NONE, "%s: the sky ends by itself (%d ticks)", tag, t);
  if (real_sun) CHECK(moves >= 10 && sun_px >= 10, "%s: the sun moved %d times, %d px in all", tag, moves, sun_px);
  for (int k = 0; k < 12; ++k) {
    appTick(app);
    appRender(app, surf);
  }
  CHECK(!skyTripOn(w->land.trip), "%s: every band natural after the sky", tag);
  fullCheck("after the sky", t);
  CHECK(checks > 10 && diffs == 0, "%s: %d of %d frames differ from a full redraw", tag, diffs, checks);
  // One band's latched clock never jumps more than ten clock steps (0.4 s of the sky clock), plus two for a
  // band that waited for the sun's repaint: so nothing moves further between two repaints than testSpeeds allows.
  CHECK(jump_max <= SKYTRIP_BANDS + 2, "%s: a band's clock jumped %d ticks (0.4 s is 10)", tag, jump_max);
  const float want_ticks = (float)SKYTRIP_SECONDS * (float)SS_TICK_HZ * (slow ? 2.f : 1.f);
  CHECK(fabsf((float)t - want_ticks) < 30.f, "%s: the sky's 104 s took %d ticks (want %.0f)", tag, t, (double)want_ticks);
  CHECK(clock_end > 103.5f, "%s: the clock reached its end (%.2f)", tag, (double)clock_end);
  printf("    the sky alone, %s (%s): strips a frame %.2f (the same scene before it %.2f); host ms a frame %.3f (before %.3f): +%.3f ms; widest band jump %d ticks\n",
         slow ? "one band every second tick" : "one band a tick", real_sun ? "real sun" : "parked sun", (double)strips_sky / n_sky,
         (double)strips_before / n_before, us_sky / n_sky / 1000., us_before / n_before / 1000., (us_sky / n_sky - us_before / n_before) / 1000., jump_max);
  appStop(app, alloc);
  free(mem);
  free(fb);
  free(keep);
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  testPatches();
  testThroughApp(false, true);
  testThroughApp(true, true);
  testThroughApp(false, false);
  Pixel* a = (Pixel*)calloc(kFrame, sizeof(Pixel));
  Pixel* b = (Pixel*)calloc(kFrame, sizeof(Pixel));
  testNatural(a, b);
  testPurity(a, b);
  testEdges();
  testNoFlashing(a);
  testNoFlashingRolled(a);
  testNightRule(a);
  testSpeeds();
  testLatch();
  testHalo(a, b);
  printf("  sizeof(LsTrip) %u\n", (unsigned)sizeof(LsTrip));
  free(a);
  free(b);
  if (g_fail) {
    printf("test_skytrip: %d FAILED of %ld checks\n", g_fail, g_checks);
    return 1;
  }
  printf("test_skytrip: ok (%ld checks)\n", g_checks);
  return 0;
}
