// Weather tests (SPINES_WEATHER_LOOKOUT_SPEC.md section 4): the code -> kind
// table; every kind plays a whole loop in the evening and the morning on 10
// seeds with no overrun, the reserved corner clean and the hiker's overlays
// (hat, fan, hug) never left running into a beat; the storm brings
// rain_shelter and it plays clean; the hashed rain draws the same twice;
// incremental rendering equals a full render through rain, snow, the
// shimmer, fog and lightning; strips per frame and cache rebuilds per kind;
// the random draw keeps the loop rng (the evening's plan) as it was;
// sizeof(App). The night caps at flash 1 are in test_palette.
// Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "scene/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

#define QX0 660
#define QY0 420

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

static const int kForceMorning = 1 + SF_MORNING;

// Inputs::weather_code for each kind (the inverse of the table, checked below).
static int codeFor(int kind) {
  for (int c = 0; c < 16; ++c)
    if (weatherKindForCode(c) == kind) return c;
  return 0;
}

static Inputs makeInputs(float mood, int kind, int force = 0, bool night = false) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.weather.night_dim = night ? 1.0f : 0.0f;
  in.night_mode = night;
  in.spine_force = (uint8_t)force;
  in.have_outside = kind >= 0;
  in.weather_code = kind >= 0 ? codeFor(kind) : 0;
  return in;
}

static const World& W(const Rig& r) { return *appWorld(r.app); }

// The palette the app draws with now (as app.cpp sceneWeather has it).
static void scenePalette(const App* a, const Inputs& in, Palette& pal) {
  WeatherTint w = in.weather;
  const float fog = appWorld(a)->fog_extra;
  if (fog > 0.f) w.fog = w.fog + fog > 1.f ? 1.f : w.fog + fog;
  const float flash = weatherTint(*appWorld(a), w);
  if (in.night_mode && w.night_dim < 1.f) w.night_dim = 1.f;
  computePalette(in.mood, w, flash, pal);
}

static int badCorner(const Rig& a, const Palette& pal) {
  int bad = 0;
  for (int y = QY0; y < SS_HEIGHT; ++y)
    for (int x = QX0; x < SS_WIDTH; ++x) {
      const Pixel p = a.fb[y * SS_WIDTH + x];
      if (p != pal[GROUND] && p != pal[GROUND_DARK] && p != pal[GRASS]) ++bad;
    }
  return bad;
}

static bool sameFb(const Rig& a, const Rig& b, long* at) {
  for (long i = 0; i < (long)SS_WIDTH * SS_HEIGHT; ++i)
    if (a.fb[i] != b.fb[i]) {
      if (at) *at = i;
      return false;
    }
  return true;
}

// ---- the table ------------------------------------------------------------------------------

static void testTable() {
  static const int want[8] = {WX_CLEAR, WX_RAIN, WX_SNOW, WX_WIND, WX_HEAT, WX_FOG, WX_CHILL, WX_STORM};
  for (int c = 0; c < 8; ++c) CHECK(weatherKindForCode(c) == want[c], "code %d -> %d", c, weatherKindForCode(c));
  CHECK(weatherKindForCode(-1) == WX_CLEAR && weatherKindForCode(99) == WX_CLEAR, "unknown codes are clear");
  for (int k = 0; k < WX_COUNT; ++k) CHECK(strcmp(weatherKindName(k), "?") != 0, "kind %d has a name", k);
  Inputs in = makeInputs(4.f, WX_STORM);
  CHECK(weatherPlannedStorm(in), "a storm from the data plans the shelter");
  in.have_outside = false;
  CHECK(!weatherPlannedStorm(in), "a random storm does not");
}

// ---- every kind, both families, 10 seeds, whole loops ---------------------------------------------

struct KindStats {
  int loops = 0, overruns = 0, corner = 0, leftover = 0, shelters = 0, shelter_bad = 0;
  int hats = 0, fans = 0, hug_ticks = 0, breaths = 0, shakes = 0, flashes = 0, leaves = 0, rings = 0, drips = 0;
  int reb_lo = 1 << 30, reb_hi = 0;
};

static void runLoops(int kind, int family, KindStats& st) {
  const float mood = family == SF_MORNING ? 1.5f : 4.f;
  const int force = family == SF_MORNING ? kForceMorning : 0;
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    const Inputs in = makeInputs(mood, kind, force);
    Rig a;
    CHECK(a.start(seed * 7 + (uint64_t)kind, in), "start");
    CHECK(W(a).weather.kind == kind, "kind %d from the data (got %d)", kind, W(a).weather.kind);
    char plan[320];
    appLoopPlan(a.app, plan, sizeof plan);
    const bool shelter = strstr(plan, "rain_shelter") != nullptr;
    if (shelter) ++st.shelters;
    CHECK(kind == WX_STORM || !shelter, "rain_shelter only in a storm (kind %d): %s", kind, plan);
    const int len = (int)appLoopTicks(a.app);
    char prev[48] = "";
    bool in_pause = false;
    int last_flashes = 0;
    Palette pal;
    for (int t = 1; t < len; ++t) {
      // The palette this tick renders with is computed at the start of the
      // tick from the state the last one left (app.cpp appTick).
      if (t % 60 == 0) scenePalette(a.app, in, pal);
      appTick(a.app);
      const World& w = W(a);
      const Weather& x = w.weather;
      if (x.hug_w > 0.f) ++st.hug_ticks;
      if (x.breath.t == 0.f) ++st.breaths;
      last_flashes = x.flashes;
      const char* beat = appBeatName(a.app);
      if (strcmp(beat, prev) != 0) {
        // A pause just ended: whatever the weather did to him is over.
        if (in_pause && x.gag != WG_NONE) {
          if (st.leftover < 3) printf("kind %s seed %d tick %d: gag %d still on into %s\n", weatherKindName(kind), (int)seed, t, x.gag, beat);
          ++st.leftover;
        }
        if (in_pause && (x.hug_w > 0.f || x.ov_w > 0.f || !w.hiker.hat_on)) {
          if (w.fx.on && st.leftover < 3) printf("kind %s seed %d tick %d: hug %.2f arm %.2f hat %d into %s\n", weatherKindName(kind), (int)seed, t, (double)x.hug_w, (double)x.ov_w, (int)w.hiker.hat_on, beat);
          if (w.fx.on) ++st.leftover;
        }
        in_pause = strcmp(beat, "pause") == 0;
        snprintf(prev, sizeof prev, "%s", beat);
      }
      if (t % 60 == 0 && appFade(a.app) == 0.f) {
        appInvalidate(a.app);
        appRender(a.app, a.surf);
        const int b = badCorner(a, pal);
        if (b && st.corner == 0) printf("kind %s seed %d tick %d: %d corner pixels\n", weatherKindName(kind), (int)seed, t, b);
        st.corner += b;
      }
    }
    const int reb = appPaletteRebuilds(a.app);
    if (reb < st.reb_lo) st.reb_lo = reb;
    if (reb > st.reb_hi) st.reb_hi = reb;
    const World& w = W(a);
    st.hats += w.weather.hats;
    st.fans += w.weather.fans;
    st.shakes += w.weather.shakes;
    st.flashes += last_flashes;
    st.leaves += w.weather.n_leaf;
    st.rings += w.weather.n_ring;
    st.drips += w.weather.n_drip;
    const int ov = appSpineOverruns(a.app);
    st.overruns += ov;
    if (shelter && ov) ++st.shelter_bad;
    if (ov) printf("kind %s family %d seed %d: %d overruns (%s)\n", weatherKindName(kind), family, (int)seed, ov, plan);
    ++st.loops;
    a.stop();
  }
}

static void testKinds() {
  printf("[kinds] whole loops, 10 seeds, evening (mood 4) and morning (mood 1.5)\n");
  for (int fam = 0; fam < 2; ++fam) {
    const int family = fam == 0 ? SF_EVENING : SF_MORNING;
    for (int kind = 0; kind < WX_COUNT; ++kind) {
      KindStats st;
      runLoops(kind, family, st);
      printf("  %-7s %-7s loops %d overruns %d corner %d leftover %d | rebuilds %d..%d flashes %d shelters %d hats %d fans %d hug %.0fs breaths %d shakes %d leaves %d rings %d drips %d\n",
             family == SF_MORNING ? "morning" : "evening", weatherKindName(kind), st.loops, st.overruns, st.corner,
             st.leftover, st.reb_lo, st.reb_hi, st.flashes, st.shelters, st.hats, st.fans,
             (double)st.hug_ticks / SS_TICK_HZ, st.breaths, st.shakes, st.leaves, st.rings, st.drips);
      CHECK(st.overruns == 0, "%s %s: %d overruns", spineFamilyName(family), weatherKindName(kind), st.overruns);
      CHECK(st.corner == 0, "%s %s: %d corner pixels", spineFamilyName(family), weatherKindName(kind), st.corner);
      // The board's budget for cache rebuilds (the chief, 2026-10-31): about 0
      // from the weather in every kind but the storm, about 25 in a storm.
      // The morning's own fog lift (up to 17 steps) comes on top.
      const int own = family == SF_MORNING ? 17 : 1;
      CHECK(st.reb_hi <= own + (kind == WX_STORM ? 26 : 0), "%s %s: up to %d palette rebuilds a loop",
            spineFamilyName(family), weatherKindName(kind), st.reb_hi);
      CHECK(st.leftover == 0, "%s %s: an overlay ran into a beat %d times", spineFamilyName(family), weatherKindName(kind), st.leftover);
      if (kind == WX_STORM) {
        CHECK(st.shelters >= 5, "%s storm: rain_shelter in only %d of 10 loops", spineFamilyName(family), st.shelters);
        CHECK(st.flashes >= 10 * 4, "%s storm: only %d strikes in 10 loops", spineFamilyName(family), st.flashes);
        CHECK(st.overruns == 0 && st.shelter_bad == 0, "storm: rain_shelter overran %d times", st.shelter_bad);
      }
      if (kind == WX_CHILL) CHECK(st.breaths > 100 && st.hug_ticks > 0, "chill: breath %d hug ticks %d", st.breaths, st.hug_ticks);
      if (kind == WX_HEAT) CHECK(st.fans > 0 && st.drips > 0, "heat: fans %d sweat drops %d", st.fans, st.drips);
      if (kind == WX_WIND) CHECK(st.leaves > 0, "wind: no leaves");
      if (kind == WX_RAIN) CHECK(st.rings > 0 && st.drips > 0, "rain: rings %d drips %d", st.rings, st.drips);
      if (kind == WX_SNOW && family == SF_EVENING) CHECK(st.shakes > 0, "snow: never shaken off the tent");
    }
  }
}

// The hat gag is rare by design (once a loop at most, only when he stands
// still in a long enough pause in a gust); over 30 wind loops it must come.
static void testHatGag() {
  int hats = 0, bad = 0;
  for (uint64_t seed = 1; seed <= 30; ++seed) {
    for (int fam = 0; fam < 2; ++fam) {
      const Inputs in = makeInputs(fam ? 1.5f : 4.f, WX_WIND, fam ? kForceMorning : 0);
      Rig a;
      CHECK(a.start(seed + 100, in), "start");
      const int len = (int)appLoopTicks(a.app);
      Vec2 home{0.f, 0.f};
      int face = 0;
      bool on = false;
      for (int t = 1; t < len; ++t) {
        appTick(a.app);
        const World& w = W(a);
        const bool g = w.weather.gag == WG_HAT;
        if (g && !on) {
          home = w.weather.home;
          face = w.weather.home_face;
          ++hats;
        }
        if (g) {   // the hat stays in frame and out of the corner
          const Item& hat = w.items[ITEM_HAT];
          if (hat.down && (hat.pos.x < 10.f || hat.pos.x > 650.f || hat.pos.y > 415.f)) ++bad;
        }
        if (!g && on) {   // back where he was, as he was
          if (w.hiker.feet.x != home.x || w.hiker.feet.y != home.y || w.hiker.facing != face || !w.hiker.hat_on) {
            if (bad < 3) printf("hat gag seed %d: ends at %.1f,%.1f face %d hat %d (home %.1f,%.1f face %d)\n", (int)seed,
                                (double)w.hiker.feet.x, (double)w.hiker.feet.y, w.hiker.facing, (int)w.hiker.hat_on,
                                (double)home.x, (double)home.y, face);
            ++bad;
          }
        }
        on = g;
      }
      CHECK(appSpineOverruns(a.app) == 0, "wind seed %d: overruns", (int)seed);
      a.stop();
    }
  }
  printf("[hat] the hat blew off in %d of 60 wind loops; %d bad\n", hats, bad);
  CHECK(hats > 0, "the hat never blew off in 60 wind loops");
  CHECK(bad == 0, "%d hat gag faults", bad);
}

// ---- rendering --------------------------------------------------------------------------------

// Rendered every tick over [t0, t1): strips per frame; at checkpoints (and
// on every lightning step) a full render of the twin must equal it.
static void testIncremental(int kind, int family, uint64_t seed, int t0, int t1, bool night = false) {
  const Inputs in = makeInputs(family == SF_MORNING ? 1.5f : (night ? 0.f : 4.f), kind,
                               family == SF_MORNING ? kForceMorning : 0, night);
  Rig a, b;
  CHECK(a.start(seed, in) && b.start(seed, in), "start");
  const int len = (int)appLoopTicks(a.app);
  if (t1 > len) t1 = len;
  long sum = 0, frames = 0;
  int worst = 0, checks = 0, diffs = 0, flash_checks = 0;
  uint8_t last_q = 0;
  for (int t = 1; t < t1; ++t) {
    appTick(a.app);
    appTick(b.app);
    if (t < t0) continue;
    const int n = appRender(a.app, a.surf);
    sum += n;
    ++frames;
    if (n > worst) worst = n;
    const uint8_t q = W(a).weather.flash_q;
    const bool flash_step = q != last_q;
    last_q = q;
    if (t % 97 == 0 || flash_step) {
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++checks;
      if (flash_step) ++flash_checks;
      long at = 0;
      if (!sameFb(a, b, &at)) {
        if (diffs < 3)
          printf("%s %s seed %d tick %d (%s): incremental differs from full at (%ld,%ld)\n", spineFamilyName(family),
                 weatherKindName(kind), (int)seed, t, appBeatName(a.app), at % SS_WIDTH, at / SS_WIDTH);
        ++diffs;
      }
    }
  }
  printf("  %-7s %-6s%s seed %d ticks %d..%d: strips avg %.2f max %d; %d checks (%d on flash steps), %d differ\n",
         spineFamilyName(family), weatherKindName(kind), night ? " night" : "", (int)seed, t0, t1,
         frames ? (double)sum / (double)frames : 0.0, worst, checks, flash_checks, diffs);
  CHECK(diffs == 0, "%s %s: incremental != full at %d of %d checks", spineFamilyName(family), weatherKindName(kind), diffs, checks);
  if (kind == WX_STORM) CHECK(flash_checks > 0, "storm: no lightning in the window");
  a.stop();
  b.stop();
}

static void testRendering() {
  printf("[render] incremental == full, strips per frame (build excluded)\n");
  testIncremental(WX_CLEAR, SF_EVENING, 3, 1200, 6200);
  testIncremental(WX_RAIN, SF_EVENING, 3, 1200, 6200);
  testIncremental(WX_SNOW, SF_EVENING, 3, 1200, 6200);
  testIncremental(WX_HEAT, SF_EVENING, 3, 1200, 6200);
  testIncremental(WX_FOG, SF_MORNING, 3, 1200, 6200);
  testIncremental(WX_WIND, SF_EVENING, 3, 1200, 6200);
  testIncremental(WX_CHILL, SF_EVENING, 3, 1200, 6200, true);
  testIncremental(WX_STORM, SF_EVENING, 3, 1200, 1 << 30);
  testIncremental(WX_STORM, SF_EVENING, 4, 1200, 6200, true);
  testIncremental(WX_STORM, SF_MORNING, 3, 1200, 1 << 30);
}

// The rain is a pure function of (tick, strip, amount): two runs draw it the same.
static void testRainRepeats() {
  const Inputs in = makeInputs(4.f, WX_RAIN);
  Rig a, b;
  CHECK(a.start(5, in) && b.start(5, in), "start");
  int same = 0, checks = 0;
  for (int t = 1; t < 4000; ++t) {
    appTick(a.app);
    appTick(b.app);
    if (t % 211 == 0) {
      appInvalidate(a.app);
      appRender(a.app, a.surf);
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++checks;
      same += sameFb(a, b, nullptr) ? 1 : 0;
    }
  }
  printf("[rain] two runs identical at %d of %d checks\n", same, checks);
  CHECK(same == checks, "the rain differs between two runs of one seed");
  a.stop();
  b.stop();
}

// The random draw (no data) is the weather's own stream: the evening's plan
// is what it would be in clear weather, loop after loop, and the kinds vary.
static void testRandomKeepsPlan() {
  static const uint64_t kSeeds[2] = {2, 9};
  for (uint64_t seed : kSeeds) {
    Rig a, b;
    CHECK(a.start(seed, makeInputs(4.f, -1)) && b.start(seed, makeInputs(4.f, WX_CLEAR)), "start");
    int kinds = 0, differ = 0;
    for (int loop = 0; loop < 6; ++loop) {
      char pa[320], pb[320];
      appLoopPlan(a.app, pa, sizeof pa);
      appLoopPlan(b.app, pb, sizeof pb);
      if (strcmp(pa, pb) != 0) ++differ;
      kinds |= 1 << W(a).weather.kind;
      const uint32_t la = appLoopTicks(a.app);
      for (uint32_t t = 0; t < la; ++t) appTick(a.app);
      const uint32_t lb = appLoopTicks(b.app);
      for (uint32_t t = 0; t < lb; ++t) appTick(b.app);
      CHECK(appLoopSeed(a.app) == appLoopSeed(b.app), "seed %d loop %d: the next loop's seed moved", (int)seed, loop);
    }
    CHECK(differ == 0, "seed %d: %d random-weather plans differ from clear", (int)seed, differ);
    printf("[random] seed %d: 6 loops, plans as in clear weather (%d differ), kinds seen mask %x\n", (int)seed, differ, kinds);
    a.stop();
    b.stop();
  }
}

int main() {
  printf("sizeof(App) = %u of %u, sizeof(Weather) = %u\n", appAppBytes(), (unsigned)SS_APP_BYTES, (unsigned)sizeof(Weather));
  CHECK(appAppBytes() <= SS_APP_BYTES, "App too big");
  testTable();
  testRandomKeepsPlan();
  testRainRepeats();
  testRendering();
  testHatGag();
  testKinds();
  if (g_fail) printf("test_weather: %d FAILED\n", g_fail);
  else printf("test_weather: all passed\n");
  return g_fail ? 1 : 0;
}
