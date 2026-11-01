// The Lookout tests (SPINES_WEATHER_LOOKOUT_SPEC.md section 5): the family
// plays when the window is open or when forced; over 20 seeds no beat
// overruns, the loop is 250 to 320 s, the hiker's height never grows on the
// way up and he stays in the frame (but for his walk in), nothing but ground
// and grass in the reserved corner (static and dynamic), the tower's bounds
// clear of it; the sunset's mood steps rise monotonically from 4.0 to 5.9 in
// 8 to 10 steps of 0.19 to 0.24; at most 16 palette rebuilds a loop; the
// night caps hold through the fade (night rule on); the birds, the rail bird,
// the chipmunk, the tap and all the stars show; the sun (spec 5b) sinks
// monotonically from clear of the skyline to 13/16..15/16 below it the tick he
// is down and is set before 30 % of the sunset; the stars come out one by one
// (none before 40 % of the sunset, 20 by its end, all 40 by 7 s into the fade),
// each clear of the skyline, the tower, the corner and each other;
// incremental rendering equals a full render at checkpoints in every beat
// and through the sun's crossing and the stars' coming out; the camp's memory is untouched
// (but the loop count); a camp loop after a Lookout loop plays as it would
// have. Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "scene/spine_family.h"
#include "scene/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

#define QX 660
#define QY 420

static int g_fail = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree};

struct Rig {
  void* mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = (mem && fb) ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    free(fb);
    app = nullptr;
  }
};

static Inputs lookoutInputs(float mood = 4.f, bool force = true) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  if (force) in.spine_force = 1 + SF_LOOKOUT;
  else in.lookout_window = true;
  return in;
}

static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }

static float lum(Pixel p) {
  return (0.2126f * (float)red8(p) + 0.7152f * (float)green8(p) + 0.0722f * (float)blue8(p)) / 255.f;
}

// The slots a Lookout frame draws with (lookout_land.cpp, lookout.cpp, the
// hiker, the birds and glyphs); the camp's tent, fire, water and the like
// are not on screen.
static bool lookoutSlot(int s) {
  switch (s) {
    case SKY0: case SKY1: case SKY2: case SKY3: case SKY4: case CLOUD_LIT: case CLOUD_SHADE:
    case RIDGE_FAR: case RIDGE_MID: case RIDGE_NEAR: case TREES_FAR: case GROUND: case GROUND_DARK:
    case GRASS: case TRUNK: case FOLIAGE_DARK: case ACTOR: case STAR: return true;
    default: return false;
  }
}

static bool inCorner(const Rect& r) { return !r.empty() && r.x1 > QX && r.y1 > QY; }

// Ground and grass only (and their blends, which the trail never makes there:
// the fills and tufts are all the corner holds).
static bool groundish(Pixel p, const Palette& pal) {
  if (p == pal[GROUND] || p == pal[GROUND_DARK] || p == pal[GRASS]) return true;
  const Pixel a = pal[GROUND], b = pal[GROUND_DARK];
  auto between = [](int v, int x, int y) { return v >= (x < y ? x : y) - 1 && v <= (x > y ? x : y) + 1; };
  return between(red8(p), red8(a), red8(b)) && between(green8(p), green8(a), green8(b)) &&
         between(blue8(p), blue8(a), blue8(b));
}

// ---- the pick ---------------------------------------------------------------------------

static void testPick() {
  Inputs in = lookoutInputs(4.f, false);
  CHECK(spineFamilyFor(in) == SF_LOOKOUT, "the window picks family %d", spineFamilyFor(in));
  in.night_mode = true;
  CHECK(spineFamilyFor(in) == SF_LOOKOUT, "the window beats the night: family %d", spineFamilyFor(in));
  in = lookoutInputs(1.5f, true);
  CHECK(spineFamilyFor(in) == SF_LOOKOUT, "forced: family %d", spineFamilyFor(in));
}

// ---- the sun and the stars: places (spec 5b) ------------------------------------------------

static Rect starRect(const LkStar& s) {
  if (s.r10 == 0) return Rect{s.x, s.y, s.x + 1, s.y + 1};
  return Rect{s.x - 2, s.y - 2, s.x + 3, s.y + 3};
}

// Every star at least 8 px above the skyline at its columns, outside the
// tower's bounds and the corner, no two within 10 px. Returns the faults.
static int starPlaces(const World& w, int seed) {
  int bad = 0;
  const LookoutState& L = w.look;
  const Rect tb = lookoutTowerBounds(w.lk);
  for (int i = 0; i < LK_STARS; ++i) {
    const LkStar& s = L.stars[i];
    const Rect b = starRect(s);
    float sky = 1e9f;
    for (int x = b.x0; x < b.x1; ++x)
      for (int k = 0; k <= 2; ++k) {
        const float v = lookoutSkylineAt(w.lk, (float)x + 0.5f * (float)k);
        sky = v < sky ? v : sky;
      }
    const float clear = sky - (float)b.y1;   // rows between its lowest and the skyline
    const bool ok_sky = clear >= 8.f && b.y0 >= 0;
    const bool ok_tower = b.clipped(tb).empty();
    const bool ok_corner = !inCorner(b) && b.x0 >= 0 && b.x1 <= SS_WIDTH;
    bool ok_gap = true;
    for (int j = 0; j < i; ++j) {
      const int dx = L.stars[j].x - s.x, dy = L.stars[j].y - s.y;
      if (dx * dx + dy * dy < 100) ok_gap = false;
    }
    if (!(ok_sky && ok_tower && ok_corner && ok_gap)) {
      if (bad < 4)
        printf("seed %d star %d at %d,%d r %.1f: sky clear %.1f tower %d corner %d gap %d\n", seed, i, s.x, s.y,
               (double)s.r10 * 0.1, (double)clear, ok_tower, ok_corner, ok_gap);
      ++bad;
    }
    if (i > 0 && s.full < L.stars[i - 1].full + 9) ++bad;   // 0.4 s gaps, in array order
  }
  return bad;
}

// The share of the sun's disc above the skyline that the clouds' puffs (lit
// and shade ellipses, unpadded) hide now, every pixel centre; -1 when none of
// it shows.
static bool inEll(float x, float y, float cx, float cy, float rx, float ry) {
  const float a = (x - cx) / rx, b = (y - cy) / ry;
  return a * a + b * b <= 1.f;
}
static float sunHidden(const World& w) {
  const Vec2 c0 = lookoutSunAt(w);
  const Vec2 c{floorf(c0.x) + 0.5f, floorf(c0.y) + 0.5f};
  const float r = w.look.sun_r;
  int vis = 0, hid = 0;
  for (int y = (int)floorf(c.y - r); y <= (int)ceilf(c.y + r); ++y)
    for (int x = (int)floorf(c.x - r); x <= (int)ceilf(c.x + r); ++x) {
      if (x < 0 || x >= SS_WIDTH || y >= lookoutSkyTop(w.lk, x)) continue;
      const float px = (float)x + 0.5f, py = (float)y + 0.5f;
      if ((px - c.x) * (px - c.x) + (py - c.y) * (py - c.y) > r * r) continue;
      ++vis;
      for (int j = 0; j < 2; ++j) {
        const Cloud& k = w.clouds[j];
        const float xq = floorf(k.x);
        bool in = inEll(px, py, xq + k.sdx, k.y + k.sdy, k.srx, k.sry);
        for (int i = 0; i < k.n && !in; ++i) in = inEll(px, py, xq + k.dx[i], k.y + k.dy[i], k.rx[i], k.ry[i]);
        if (in) {
          ++hid;
          break;
        }
      }
    }
  return vis > 0 ? (float)hid / (float)vis : -1.f;
}

// ---- 20 seeds, every tick -------------------------------------------------------------------

struct Seen {
  bool hawk = false, rail_in = false, rail_off = false, chip = false, tap = false, star = false, inside = false;
};

static void testLoops() {
  int overruns = 0, len_bad = 0, grow = 0, out = 0, corner = 0, corner_px = 0, step_bad = 0, reb_bad = 0;
  int caps_bad = 0, busy_bad = 0, landed_bad = 0, birds_lo = 99, birds_hi = 0, reb_lo = 99, reb_hi = 0;
  int len_lo = 1 << 30, len_hi = 0, samples = 0;
  int sun_bad = 0, stars_bad = 0, places_bad = 0, sun_retried = 0, half_seeds = 0;
  int cover_hist[4] = {0, 0, 0, 0};   // the kept start's worst cover: 0, to 0.25, to 0.5, over 0.5
  float cover_hi = 0.f;
  int worst_seed = 0, worst_half_s = 0;
  float sat_lo = 1.f, sat_hi = 0.f;
  float cap_seen = 0.f, cap_seen_day = 0.f;
  Seen seen;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    // Every fourth seed with the night rule on (night_dim 1), for the caps in the fade.
    Inputs in = lookoutInputs();
    in.night_mode = (seed % 4) == 0;
    if (in.night_mode) in.weather.night_dim = 1.f;
    Rig a;
    CHECK(a.start(seed, in), "start");
    const World* w = appWorld(a.app);
    CHECK(appSpineFamily(a.app) == SF_LOOKOUT, "seed %d: family %d", (int)seed, appSpineFamily(a.app));
    const Rect tb = lookoutTowerBounds(w->lk);
    if (inCorner(tb)) {
      ++corner;
      printf("seed %d: tower bounds %d,%d,%d,%d in the corner\n", (int)seed, tb.x0, tb.y0, tb.x1, tb.y1);
    }
    const int len = (int)appLoopTicks(a.app);
    len_lo = len < len_lo ? len : len_lo;
    len_hi = len > len_hi ? len : len_hi;
    if (len < 250 * SS_TICK_HZ || len > 320 * SS_TICK_HZ) {
      ++len_bad;
      printf("seed %d: loop %.1f s\n", (int)seed, (double)len / SS_TICK_HZ);
    }
    float h_prev = 1e9f, mood_prev = 4.f;
    int steps = 0;
    bool in_sunset = false;
    places_bad += starPlaces(*w, (int)seed);
    float below_prev = 0.f, sat_frac = -1.f;
    int out_prev = 0, out_sunset_end = -1, out_fade7 = -1;
    uint32_t set_tick = 0, sunset_t0 = 0;
    int half_s = 0;
    float hid_max = 0.f;
    if (w->look.sun_tries > 1) ++sun_retried;
    {
      const float cv = w->look.sun_cover;
      ++cover_hist[cv <= 0.f ? 0 : (cv <= 0.25f ? 1 : (cv <= 0.5f ? 2 : 3))];
      cover_hi = cv > cover_hi ? cv : cover_hi;
    }
    bool checked30 = false;
    for (int t = 1; t < len; ++t) {
      appTick(a.app);
      const char* beat = appBeatName(a.app);
      // The sun: clear and whole in the frame at the first tick, never rising,
      // 13/16..15/16 below the tick he is down, set before 30 % of the sunset.
      const float below = lookoutSunBelow(*w);
      if (t == 1) {
        const Vec2 c = lookoutSunAt(*w);
        const float r = w->look.sun_r;
        const Rect d{(int)floorf(c.x - r), (int)floorf(c.y - r), (int)ceilf(c.x + r), (int)ceilf(c.y + r)};
        if (below != 0.f || d.x0 < 0 || d.y0 < 0 || d.x1 > SS_WIDTH || d.y1 > SS_HEIGHT || inCorner(d) || r < 20.f || r > 24.f) {
          printf("seed %d: the sun at the start %d,%d,%d,%d below %.3f\n", (int)seed, d.x0, d.y0, d.x1, d.y1, (double)below);
          ++sun_bad;
        }
      }
      if (below < below_prev - 1e-6f) {
        if (sun_bad < 6) printf("seed %d tick %d: the sun rose %.4f after %.4f\n", (int)seed, t, (double)below, (double)below_prev);
        ++sun_bad;
      }
      below_prev = below;
      // Behind the horizon clouds: once a second while any of it shows, the
      // share the puffs hide (the plan scores 9 times against puffs + 4 px).
      if (t % SS_TICK_HZ == 0 && lookoutSunShown(*w)) {
        const float h = sunHidden(*w);
        if (h > 0.5f) ++half_s;
        if (h > hid_max) hid_max = h;
      }
      if (w->look.sat_tick != 0 && sat_frac < 0.f) {
        if (w->look.sat_tick != (uint32_t)t) ++sun_bad;   // set on the tick he is down
        sat_frac = below;
        sat_lo = below < sat_lo ? below : sat_lo;
        sat_hi = below > sat_hi ? below : sat_hi;
        if (below < 13.f / 16.f || below > 15.f / 16.f) {
          printf("seed %d: the sun %.3f below when he is down (tick %d, planned %u)\n", (int)seed, (double)below, t,
                 (unsigned)w->look.sun_tick);
          ++sun_bad;
        }
      }
      if (set_tick == 0 && below >= 1.f && !lookoutSunShown(*w)) set_tick = (uint32_t)t;
      if (beatIs(a.app, "sunset") && sunset_t0 == 0) sunset_t0 = (uint32_t)t;
      if (beatIs(a.app, "sunset") && !checked30 && appBeatProgress(a.app) >= 0.3f) {
        checked30 = true;
        if (below < 1.f) {
          printf("seed %d: the sun %.3f below at 30 %% of the sunset\n", (int)seed, (double)below);
          ++sun_bad;
        }
      }
      // The stars: one by one, none before 40 % of the sunset, 20 by its end, 40 by 7 s into the fade.
      const int out_now = lookoutStarsOut(*w);
      if (out_now < out_prev) ++stars_bad;
      out_prev = out_now;
      int lit = 0;   // at any step, so a first step before 40 % counts too
      for (int i = 0; i < LK_STARS; ++i)
        if (lookoutStarLevel(*w, i) > 0) ++lit;
      if (lit > 0 && !(beatIs(a.app, "sunset") && appBeatProgress(a.app) >= 0.4f) && !beatIs(a.app, "fade")) {
        if (stars_bad < 6) printf("seed %d tick %d (%s %.2f): %d stars lit early\n", (int)seed, t, appBeatName(a.app),
                                  (double)appBeatProgress(a.app), lit);
        ++stars_bad;
      }
      if (beatIs(a.app, "sunset")) out_sunset_end = out_now;
      if (beatIs(a.app, "fade") && out_fade7 < 0 && appBeatProgress(a.app) * (float)w->plan.fsec[LB_FADE] >= 7.f)
        out_fade7 = out_now;
      if (t == len - 1 && out_now == LK_STARS) seen.star = true;
      if (!appBusy(a.app)) ++busy_bad;
      Rect r;
      const bool vis = appHikerBounds(a.app, &r);
      if (beatIs(a.app, "approach") && vis) {
        if (w->hiker.height > h_prev + 1e-5f) {
          if (grow < 5) printf("seed %d tick %d: height %.3f after %.3f\n", (int)seed, t, (double)w->hiker.height, (double)h_prev);
          ++grow;
        }
        h_prev = w->hiker.height;
      }
      if (vis) {
        const bool cols = beatIs(a.app, "approach") ? (r.x1 > 0 && r.x0 < SS_WIDTH) : (r.x0 >= 0 && r.x1 <= SS_WIDTH);
        if (!cols || r.y0 < 0 || r.y1 > SS_HEIGHT) {
          if (out < 5) printf("seed %d tick %d (%s): hiker %d,%d,%d,%d\n", (int)seed, t, beat, r.x0, r.y0, r.x1, r.y1);
          ++out;
        }
        if (inCorner(r)) ++corner;
      }
      if (inCorner(ambientBirdBounds(*w))) ++corner;
      for (int i = 0; i < 2; ++i)
        if (inCorner(cloudBounds(w->clouds[i]))) ++corner;
      for (int i = 0; i < GLYPH_MAX; ++i)
        if (w->major.g[i].kind != GL_NONE && inCorner(glyphBounds(w->major.g[i]))) ++corner;
      // What shows.
      if (w->amb.bird.active && w->amb.bird.scale > 1.5f) seen.hawk = true;
      if (w->look.rail_mode == 2) seen.rail_in = true;
      if (w->look.rail_mode == 4 && seen.rail_in) seen.rail_off = true;
      if (w->look.chip_mode == 2) seen.chip = true;
      if (w->major.g[2].kind == GL_ARCS) seen.tap = true;
      if (w->look.inside) seen.inside = true;
      // The sunset's steps: monotone, 4.0 .. 5.9, 0.19 .. 0.24 each.
      if (beatIs(a.app, "sunset")) {
        in_sunset = true;
        const float m = w->mood_override;
        if (m != mood_prev) {
          ++steps;
          const float q = m - mood_prev;
          if (q < 0.19f - 1e-4f || q > 0.24f + 1e-4f) ++step_bad;
          mood_prev = m;
        }
      } else if (in_sunset && !beatIs(a.app, "fade")) {
        ++step_bad;   // nothing after the sunset but the fade
      }
      // The night caps through the fade (night rule on): every slot of the palette in use.
      if (beatIs(a.app, "fade")) {
        const Palette& p = *appPalette(a.app);
        for (int s = 0; s < SLOT_COUNT; ++s) {
          const float l = lum(p.c[s]);
          const float cap = isNightHighlight(s) ? NIGHT_HIGHLIGHT_MAX : NIGHT_LUM_MAX;
          if (in.night_mode) {
            if (l > cap + 1e-4f) ++caps_bad;
            if (l > cap_seen && !isNightHighlight(s)) cap_seen = l;
          } else if (lookoutSlot(s) && !isNightHighlight(s)) {   // no night rule: the areas on screen keep under it too
            if (l > cap + 1e-4f) ++caps_bad;
            if (l > cap_seen_day && !isNightHighlight(s)) cap_seen_day = l;
          }
        }
      }
      // The reserved corner, rendered (static and dynamic), now and then.
      if (t % 211 == 0 && appFade(a.app) == 0.f) {
        appInvalidate(a.app);
        appRender(a.app, a.surf);
        const Palette& p = *appPalette(a.app);
        ++samples;
        for (int y = QY; y < SS_HEIGHT; ++y)
          for (int x = QX; x < SS_WIDTH; ++x)
            if (!groundish(a.fb[y * SS_WIDTH + x], p)) {
              if (corner_px < 4) printf("seed %d tick %d: corner pixel (%d,%d) %04x\n", (int)seed, t, x, y, a.fb[y * SS_WIDTH + x]);
              ++corner_px;
            }
      }
    }
    if (out_sunset_end < 20 || out_fade7 != LK_STARS) {
      printf("seed %d: %d stars out at the sunset's end, %d at 7 s into the fade\n", (int)seed, out_sunset_end, out_fade7);
      ++stars_bad;
    }
    if (half_s) {
      ++half_seeds;
      printf("seed %d: the sun more than half behind a cloud for %d s (most %.2f; tries %d, planned worst %.2f)\n",
             (int)seed, half_s, (double)hid_max, w->look.sun_tries, (double)w->look.sun_cover);
    }
    if (half_s > worst_half_s) {
      worst_half_s = half_s;
      worst_seed = (int)seed;
    }
    if (sat_frac < 0.f) {
      printf("seed %d: he never sat down\n", (int)seed);
      ++sun_bad;
    }
    if (seed <= 4) {   // for the notes
      const LookoutState& L = w->look;
      const float S = (float)w->plan.fsec[LB_SUNSET];
      printf("sun seed %d: r %.1f start %.1f,%.1f (%.1f px clear) speed %.3f px/s down, sat tick %u (planned %u, %+d), "
             "%.3f below then; set at tick %u (planned %u; %.1f s %s the sunset at tick %u); tries %d, worst cover %.2f "
             "(planned, puffs + 4 px), most hidden %.2f (puffs, every second)\n",
             (int)seed, (double)L.sun_r, (double)L.sun_p0.x, (double)L.sun_p0.y,
             (double)((float)lookoutSkyTop(w->lk, (int)L.sun_p0.x) - L.sun_p0.y - L.sun_r), (double)(L.sun_v.y * (float)SS_TICK_HZ),
             (unsigned)L.sat_tick, (unsigned)L.sun_tick, (int)L.sat_tick - (int)L.sun_tick, (double)sat_frac, (unsigned)set_tick,
             (unsigned)L.sun_set_tick,
             (double)fabsf(((float)set_tick - (float)sunset_t0) / (float)SS_TICK_HZ), set_tick < sunset_t0 ? "before" : "into",
             (unsigned)sunset_t0, L.sun_tries, (double)L.sun_cover, (double)hid_max);
      printf("stars seed %d: sunset %.0f s; full at %.1f s (first), %.1f s (8th), %.1f s (34th), %.1f s (last) from its start\n",
             (int)seed, (double)S, (double)L.stars[0].full / SS_TICK_HZ, (double)L.stars[7].full / SS_TICK_HZ,
             (double)L.stars[33].full / SS_TICK_HZ, (double)L.stars[LK_STARS - 1].full / SS_TICK_HZ);
    }
    if (steps < 8 || steps > 10 || fabsf(mood_prev - 5.9f) > 1e-4f) {
      ++step_bad;
      printf("seed %d: %d sunset steps, ending at %.3f\n", (int)seed, steps, (double)mood_prev);
    }
    if (appSpineOverruns(a.app)) {
      char p[320];
      appLoopPlan(a.app, p, sizeof p);
      printf("seed %d (%s): %d overruns\n", (int)seed, p, appSpineOverruns(a.app));
      ++overruns;
    }
    const int reb = appPaletteRebuilds(a.app);
    reb_lo = reb < reb_lo ? reb : reb_lo;
    reb_hi = reb > reb_hi ? reb : reb_hi;
    if (reb > 16) ++reb_bad;
    const int nb = w->look.birds_flown;
    birds_lo = nb < birds_lo ? nb : birds_lo;
    birds_hi = nb > birds_hi ? nb : birds_hi;
    if (w->look.pairs_landed != 1) {
      ++landed_bad;
      printf("seed %d: %d pairs landed (of %d birds planned, %d flown)\n", (int)seed, w->look.pairs_landed,
             w->look.n_birds, nb);
    }
    CHECK(appFade(a.app) > 0.99f, "seed %d: the loop ends at fade %.3f", (int)seed, (double)appFade(a.app));
    CHECK(appTick(a.app), "seed %d: no rollover at the plan's length", (int)seed);
    a.stop();
  }
  printf("lookout: 20 loops, %.0f..%.0f s, palette rebuilds %d..%d, sunset birds %d..%d a loop, %d corner renders\n",
         (double)len_lo / SS_TICK_HZ, (double)len_hi / SS_TICK_HZ, reb_lo, reb_hi, birds_lo, birds_hi, samples);
  printf("lookout: fade max slot luminance %.3f with the night rule (cap %.2f), %.3f without (the slots on screen)\n", (double)cap_seen,
         (double)NIGHT_LUM_MAX, (double)cap_seen_day);
  CHECK(overruns == 0, "%d loops overran", overruns);
  CHECK(len_bad == 0, "%d loops outside 250..320 s", len_bad);
  CHECK(grow == 0, "his height grew %d times on the approach", grow);
  CHECK(out == 0, "%d hiker samples out of the frame", out);
  CHECK(corner == 0, "%d bounds in the reserved corner", corner);
  CHECK(corner_px == 0, "%d corner pixels not ground or grass", corner_px);
  CHECK(step_bad == 0, "%d bad sunset steps", step_bad);
  CHECK(reb_bad == 0, "%d loops over 16 palette rebuilds", reb_bad);
  CHECK(caps_bad == 0, "%d slot samples over the night caps in the fade", caps_bad);
  CHECK(busy_bad == 0, "%d ticks with World::busy off", busy_bad);
  CHECK(landed_bad == 0, "%d loops without exactly one landing pair", landed_bad);
  CHECK(birds_lo >= 3, "a loop with only %d sunset birds", birds_lo);
  printf("lookout: the sun %.3f..%.3f below the tick he is down (20 seeds); %d seeds drew its start again for the clouds; "
         "kept start's worst cover (puffs + 4 px): %d at 0, %d to 0.25, %d to 0.5, %d over 0.5 (max %.2f); %d seeds more "
         "than half behind a cloud for a second or more (most: seed %d, %d s)\n",
         (double)sat_lo, (double)sat_hi, sun_retried, cover_hist[0], cover_hist[1], cover_hist[2], cover_hist[3],
         (double)cover_hi, half_seeds, worst_seed, worst_half_s);
  CHECK(sun_bad == 0, "%d sun faults", sun_bad);
  CHECK(stars_bad == 0, "%d star schedule faults", stars_bad);
  CHECK(places_bad == 0, "%d star place faults", places_bad);
  CHECK(seen.hawk && seen.rail_in && seen.rail_off && seen.chip && seen.tap && seen.star && seen.inside,
        "not all seen: hawk %d rail %d/%d chip %d tap %d star %d inside %d", seen.hawk, seen.rail_in, seen.rail_off,
        seen.chip, seen.tap, seen.star, seen.inside);
}

// ---- incremental == full ----------------------------------------------------------------------

static void testIncremental() {
  static const uint64_t kSeeds[4] = {1, 2, 3, 7};
  long sum = 0, frames = 0;
  int worst = 0, diffs = 0, checks = 0, sun_checks = 0, star_checks = 0;
  int beats_checked[16] = {};
  static const char* const kNames[12] = {"build", "approach", "trail_pause", "reveal", "pause", "climb",
                                         "explore", "settle", "sunset", "fade"};
  for (uint64_t seed : kSeeds) {
    const Inputs in = lookoutInputs();
    Rig a, b;
    CHECK(a.start(seed, in) && b.start(seed, in), "start");
    const int len = (int)appLoopTicks(a.app);
    char prev[32] = "";
    int since = 0, out_prev = 0;
    const World* wa = appWorld(a.app);
    for (int t = 1; t < len; ++t) {
      appTick(a.app);
      appTick(b.app);
      // The sun's crossing (every second while it is part below, and the tick
      // he is down) and the stars' coming out (every tick a star reaches full).
      const float below = lookoutSunBelow(*wa);
      const bool sun_cp = (below > 0.f && lookoutSunShown(*wa) && t % 25 == 0) || wa->look.sat_tick == (uint32_t)t;
      const int out_now = lookoutStarsOut(*wa);
      const bool star_cp = out_now != out_prev;
      out_prev = out_now;
      const int n = appRender(a.app, a.surf);
      if (t > 330) {
        sum += n;
        ++frames;
        if (n > worst) worst = n;
      }
      const bool changed = !beatIs(a.app, prev);
      if (changed) {
        snprintf(prev, sizeof prev, "%s", appBeatName(a.app));
        since = 0;
      }
      ++since;
      // Every beat a few ticks in and then every 4 s, and through the build every 13 ticks.
      if (since == 7 || since % 100 == 0 || (t < 330 && t % 13 == 0) || sun_cp || star_cp) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (sun_cp) ++sun_checks;
        if (star_cp) ++star_checks;
        for (int k = 0; k < 10; ++k)
          if (beatIs(a.app, kNames[k])) ++beats_checked[k];
        if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
          long i = 0;
          while (a.fb[i] == b.fb[i]) ++i;
          if (diffs < 6)
            printf("seed %d tick %d (%s): incremental differs from full at (%ld,%ld)\n", (int)seed, t,
                   appBeatName(a.app), i % SS_WIDTH, i / SS_WIDTH);
          ++diffs;
        }
      }
    }
    a.stop();
    b.stop();
  }
  printf("lookout strips per frame (4 loops, build excluded): avg %.2f, max %d; %d full-render checks (%d in the sun's "
         "crossing, %d as a star came out)\n",
         frames ? (double)sum / (double)frames : 0.0, worst, checks, sun_checks, star_checks);
  CHECK(sun_checks >= 4 * 20, "only %d checkpoints in the sun's crossing", sun_checks);
  CHECK(star_checks >= 4 * 30, "only %d checkpoints as the stars came out", star_checks);
  CHECK(diffs == 0, "%d checkpoints where incremental and full renders differ", diffs);
  for (int k = 0; k < 10; ++k) CHECK(beats_checked[k] > 0, "no checkpoint in %s", kNames[k]);
}

// ---- memory, and the camp after the Lookout ------------------------------------------------------

static void testMemory() {
  // Two evening loops, then a Lookout loop, then the evening again; against
  // the same run without the Lookout loop: the memory line is the same but
  // for the loop count.
  Inputs ev;
  memset(&ev, 0, sizeof ev);
  ev.mood = 4.f;
  ev.spine_force = 1 + SF_EVENING;
  Rig a;
  CHECK(a.start(11, ev), "start");
  for (int loop = 0; loop < 2; ++loop) {
    const uint32_t len = appLoopTicks(a.app);
    for (uint32_t t = 0; t + 1 < len; ++t) appTick(a.app);
    if (loop == 1) appSetInputs(a.app, lookoutInputs());
    CHECK(appTick(a.app), "rollover");
  }
  char before[320], after[320];
  appMemory(a.app, before, sizeof before);
  CHECK(appSpineFamily(a.app) == SF_LOOKOUT, "loop 2 is family %d", appSpineFamily(a.app));
  const uint32_t len = appLoopTicks(a.app);
  for (uint32_t t = 0; t + 1 < len; ++t) appTick(a.app);
  CHECK(appSpineOverruns(a.app) == 0, "the Lookout after the evening overran");
  // What the loop's end leaves (memoryEndLoop, as appTick calls it at the rollover), on a copy.
  World* copy = (World*)malloc(sizeof(World));
  memcpy(static_cast<void*>(copy), appWorld(a.app), sizeof(World));
  memoryEndLoop(*copy);
  memoryFormat(copy->memory, after, sizeof after);
  free(copy);
  appSetInputs(a.app, ev);
  CHECK(appTick(a.app), "rollover");
  // Strip "loop=N " from both.
  const char* b = strchr(before, ' ');
  const char* c = strchr(after, ' ');
  CHECK(b && c && strcmp(b, c) == 0, "memory moved over the Lookout loop:\n  %s\n  %s", before, after);
  CHECK(appMemoryLoopCount(a.app) == 4, "loop count %u", (unsigned)appMemoryLoopCount(a.app));
  CHECK(appSpineFamily(a.app) == SF_EVENING, "after the Lookout: family %d", appSpineFamily(a.app));
  // The evening after it plays to the end with no overrun.
  const uint32_t len2 = appLoopTicks(a.app);
  for (uint32_t t = 0; t + 1 < len2; ++t) appTick(a.app);
  CHECK(appSpineOverruns(a.app) == 0, "the evening after the Lookout overran");
  CHECK(appCritterCount(a.app) == 0, "critters left");
  a.stop();
  printf("memory over a Lookout loop: %s\n", c ? c + 1 : "?");
}

static void testAppSize() {
  printf("sizeof(App) = %u bytes (budget %d), sizeof(LookoutLand) %u, sizeof(LookoutState) %u\n",
         (unsigned)appAppBytes(), SS_APP_BYTES, (unsigned)sizeof(LookoutLand), (unsigned)sizeof(LookoutState));
  CHECK(appAppBytes() <= (uint32_t)SS_APP_BYTES, "App is %u bytes", (unsigned)appAppBytes());
}

int main() {
  testPick();
  testLoops();
  testIncremental();
  testMemory();
  testAppSize();
  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("all lookout tests passed\n");
  return 0;
}
