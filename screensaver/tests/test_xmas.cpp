// The Christmas mockups (stage 18, scene/xmas.h): drawings posed from a mock state, nothing scheduled.
//
// (1) Nothing changes with the mock at zero: every family (and the Lookout and the sunrise, whose running
//     state the mock shares) renders frame for frame what it rendered before the module existed. The
//     pinned hashes were taken from the tree just before xmas.h was added (incremental renders, every
//     tick, every 7th frame hashed), with the same code as identRun below.
// (2) Every piece draws inside its bounds, in each mock state, by day and at night.
// (3) Incremental rendering equals a full render while the mock is posed and changed (the critters'
//     group follows his hand while the run of lights hangs from it); two runs draw the same frames.
// (4) The mock costs no App bytes (it shares the Lookout's running state).
// (5) The snow look (XE_SNOW) repaints the static cache: a large part of the ground changes.
//
// With the switch off (SS_XMAS 0, xmas_switch.h: the panel's build, and the host's -DSS_XMAS=OFF) there is
// no mock to pose: only (1) and the size check run, and they prove the scene is the same frame for frame
// as with the switch on and the mock at zero.
#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/world.h"
#include "scene/xmas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

using namespace ss;

static int g_fail = 0, g_checks = 0;
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

// ---- (1) frame identity with the mock at zero -------------------------------------------------------

struct IdentRun {
  const char* name;
  uint64_t seed;
  float mood;
  int ticks;
  int weather_code;   // -1: random
  bool night, lookout, sunrise, extras;
  uint8_t spine_force;
};

static const IdentRun kIdentRuns[] = {
    {"evening_golden_s1", 1, 4.f, 16600, 0, false, false, false, false, 0},
    {"night_s7", 7, 0.f, 6000, 0, true, false, false, false, 0},
    {"midday_s3", 3, 3.f, 6000, 0, false, false, false, false, 1 + 2},
    {"morning_s5", 5, 1.5f, 6000, 0, false, false, false, false, 0},
    {"snow_s2", 2, 3.f, 6000, 2, false, false, false, false, 0},
    {"lookout_s4", 4, 4.5f, 4000, 0, false, true, false, false, 0},
    {"sunrise_s6", 6, 1.f, 4000, 0, false, false, true, false, 0},
    {"dusk_extras_s8", 8, 5.f, 6000, -1, false, false, false, true, 0},
};
// Captured from the tree before scene/xmas.h existed (2026-11-12). Do not re-capture to make this pass.
// Re-captured once, for the stream's round 2 (scene/stream.h: the bigger fish and their lanes, the
// visitors, the months), the six camp runs only: every 7th frame of each run was compared in 16 px
// tiles against the tree before it, and every tile that changed lay inside the stream's fish, floater
// or visitor bounds in that frame (as either tree drew it); the Lookout and sunrise runs did not change.
static const uint64_t kIdentPins[] = {
    0xB699863E1B5071C0ull, 0xD643815CC782C2B3ull, 0x4C83065EFB673C61ull, 0x13951B569334E122ull,
    0x98F2D6B78846A48Bull, 0xC2D74FD9EC26A374ull, 0xE5E267DCA1E35EE2ull, 0x54C198CE0244F0FCull,
};

static inline uint64_t identHash(uint64_t h, const Pixel* px, size_t n) {
  const uint64_t* q = (const uint64_t*)px;
  for (size_t i = 0; i < n * sizeof(Pixel) / 8; ++i) h = (h ^ q[i]) * 0x100000001B3ull;
  return h;
}
static void* identAlloc(size_t n) { return malloc(n); }
static void identFree(void* p) { free(p); }

static Inputs identInputs(const IdentRun& r) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = r.mood;
  in.have_outside = r.weather_code >= 0;
  in.weather_code = r.weather_code >= 0 ? r.weather_code : 0;
  in.night_mode = r.night;
  if (r.night) {
    in.weather.night_dim = 1.f;
    in.weather.night_gain = 1.6f;
  }
  in.lookout_window = r.lookout;
  in.sunrise_window = r.sunrise;
  in.spine_force = r.spine_force;
  if (r.extras) {
    in.dog_out = 3;
    in.mail = true;
    in.visitors = true;
    in.chip = true;
  }
  return in;
}

static uint64_t identRun(const IdentRun& r, bool& ever_on) {
  const HostAlloc al = {identAlloc, identFree, nullptr};
  const Inputs in = identInputs(r);
  void* mem = calloc(1, SS_APP_BYTES);
  Pixel* fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  Surface s{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* app = appStart(mem, al, r.seed, in);
  uint64_t h = 0xCBF29CE484222325ull;
  for (int t = 0; t < r.ticks; ++t) {
    appTick(app);
    appRender(app, s);
    const World& w = *appWorld(app);
#if SS_XMAS
    if (w.plan.family != SF_LOOKOUT && w.plan.family != SF_SUNRISE && xmasOn(w)) ever_on = true;
#else
    (void)ever_on;
#endif
    if (t % 7 == 0) h = identHash(h, fb, (size_t)SS_WIDTH * SS_HEIGHT);
  }
  h = identHash(h, fb, (size_t)SS_WIDTH * SS_HEIGHT);
  appStop(app, al);
  free(fb);
  free(mem);
  return h;
}

static void testIdentity() {
  static_assert(sizeof(kIdentPins) / sizeof(kIdentPins[0]) == sizeof(kIdentRuns) / sizeof(kIdentRuns[0]), "one pin a run");
  for (size_t i = 0; i < sizeof(kIdentRuns) / sizeof(kIdentRuns[0]); ++i) {
    bool on = false;
    const uint64_t h = identRun(kIdentRuns[i], on);
    CHECK(h == kIdentPins[i], "%s: frames changed (hash %016llX, pinned %016llX)", kIdentRuns[i].name, (unsigned long long)h,
          (unsigned long long)kIdentPins[i]);
    CHECK(!on, "%s: the mock came on by itself in a camp loop", kIdentRuns[i].name);
  }
}

#if SS_XMAS
// ---- shared rig --------------------------------------------------------------------------------------

static const HostAlloc kAlloc = {identAlloc, identFree, nullptr};

struct Rig {
  void* mem = nullptr;
  std::vector<Pixel> fb;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = calloc(1, SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = appStart(mem, kAlloc, seed, in);
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
  }
  World& w() { return const_cast<World&>(*appWorld(app)); }
};

static Inputs camp(float mood, bool night) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;
  in.spine_force = 1 + SF_EVENING;
  in.night_mode = night;
  in.weather.night_dim = night ? 1.f : 0.f;
  return in;
}

// The mock states the stills pose.
static const int kStates = 14;
static XmasMock stateOf(int i) {
  XmasMock m{};
  m.gen = 1;
  m.wheel = (uint8_t)(i * 5);
  m.wraps = XMAS_WRAPS;
  m.power = XP_ON;
  switch (i) {
    case 0: m.wraps = 0; m.power = XP_OFF; break;
    case 1: m.spark = 1; m.power = XP_DIM; break;
    case 2: m.spark = 6; m.look = XL_MULTI; break;
    case 3: m.wraps = 2; m.extras = XE_LADDER | XE_COIL; break;
    case 4: m.power = XP_HALF_DEAD; m.garland = XG_FLYING; m.fly = 40; break;
    case 5: m.garland = XG_FLYING; m.fly = 230; break;
    case 6: m.garland = XG_TANGLED; break;
    case 7: m.garland = XG_PART; m.look = XL_MULTI; break;
    case 8: m.garland = XG_DRAPED; m.orns = 0x7F; m.coon = XR_REACH; break;
    case 9: m.garland = XG_DRAPED; m.orns = 0x3F; m.extras = XE_GAP; m.coon = XR_CARRY; break;
    case 10: m.garland = XG_DRAPED; m.orns = 0x3F; m.bush = 3; m.coon = XR_PROUD; break;
    case 11: m.garland = XG_DRAPED; m.orns = 0x7F; m.extras = XE_STAR | XE_PRESENTS | XE_STOCKING | XE_SLEIGH; break;
    case 12: m.garland = XG_DRAPED; m.orns = 0x7F; m.north = 1; m.extras = XE_STAR | XE_PRESENTS | XE_STOCKING | XE_SNOW; break;
    default: m.garland = XG_DRAPED; m.orns = 0x7F; m.north = 3; m.coon = XR_REACH; m.extras = XE_STAR | XE_SNOW | XE_SLEIGH; break;
  }
  return m;
}

// Runs the scene to a moment with the tent up and the fire going (tick 6200 on seed 1, as the stills).
static bool toMoment(Rig& r, uint64_t seed, const Inputs& in) {
  if (!r.start(seed, in)) return false;
  for (int t = 0; t < 6200; ++t) appTick(r.app);
  return true;
}

// ---- (2) inside the bounds ---------------------------------------------------------------------------

static int outside(const std::vector<Pixel>& fb, Pixel bg, const Rect& b) {
  int n = 0;
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x)
      if (fb[(size_t)y * SS_WIDTH + x] != bg && (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1)) ++n;
  return n;
}

static void testBounds() {
  void* scratch = calloc(1, rasterScratchBytes());
  void* old = rasterSetScratch(scratch);
  for (int night = 0; night < 2; ++night) {
    Rig r;
    CHECK(toMoment(r, 1, camp(night ? 0.f : 4.85f, night != 0)), "start");
    World& w = r.w();
    // He stands on the ladder, so the run of lights goes up to his hand.
    w.fx.on = true;
    hikerInit(w.hiker, xmasLadderRung(w.land, 3), -1);
    hikerTick(w.hiker, 0.f);
    w.hiker.visible = true;
    Palette pal;
    WeatherTint wt{};
    wt.night_dim = night ? 1.f : 0.f;
    computePalette(night ? 0.f : 4.85f, wt, pal);
    const Pixel bg = 0x0821;
    std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
    Surface s{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    const Rect all{0, 0, SS_WIDTH, SS_HEIGHT};
    for (int i = 0; i < kStates; ++i) {
      w.xmas.m = stateOf(i);
      for (int part = 0; part < 4 + XMAS_ACTORS; ++part) {
        for (Pixel& p : fb) p = bg;
        Rect b{0, 0, 0, 0};
        if (part == 0) {
          xmasDrawTree(w, s, all, pal);
          b = xmasTreeBounds(w);
        } else if (part == 1) {
          xmasDrawSky(w, s, all, pal);
          b = xmasExtraBounds(w);
        } else if (part < 4) {
          continue;
        } else {
          const int a = part - 4;
          int16_t y;
          if (!xmasActorY(w, a, y)) continue;
          xmasDrawActor(w, a, s, all, pal);
          b = a <= 1 ? xmasGenBounds(w) : (a == 4 || a == 5 ? xmasCritBounds(w) : xmasExtraBounds(w));
        }
        const int n = outside(fb, bg, b);
        int drawn = 0;
        for (Pixel p : fb) drawn += p != bg;
        if (part == 0 && w.xmas.m.wraps > 0) CHECK(drawn > 200, "state %d night %d: the tree's pieces drew only %d pixels", i, night, drawn);
        CHECK(n == 0, "state %d part %d night %d: %d pixels outside its bounds", i, part, night, n);
      }
    }
    r.stop();
  }
  rasterSetScratch(old);
  free(scratch);
}

// ---- (3) incremental equals full ---------------------------------------------------------------------

static void testIncremental() {
  for (int night = 0; night < 2; ++night) {
    const Inputs in = camp(night ? 0.f : 4.85f, night != 0);
    Rig a, b, d;
    CHECK(a.start(1, in) && b.start(1, in) && d.start(1, in), "start");
    int checks = 0, bad = 0, diff = 0;
    for (int t = 0; t < 6200 + kStates * 150; ++t) {
      if (t >= 6200 && (t - 6200) % 150 == 0) {
        const XmasMock m = stateOf((t - 6200) / 150);
        a.w().xmas.m = m;
        b.w().xmas.m = m;
        d.w().xmas.m = m;
      }
      appTick(a.app);
      appTick(b.app);
      appTick(d.app);
      if (t < 6000) continue;
      appRender(a.app, a.surf);
      appRender(d.app, d.surf);
      if (t > 6200 && t % 23 == 0) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb.data(), b.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) {
          if (++bad == 1)
            for (size_t i = 0; i < a.fb.size(); ++i)
              if (a.fb[i] != b.fb[i]) {
                printf("  night %d tick %d: first difference at x %d y %d\n", night, t, (int)(i % SS_WIDTH), (int)(i / SS_WIDTH));
                break;
              }
        }
        if (memcmp(a.fb.data(), d.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++diff;
      }
    }
    CHECK(checks > 50, "too few checks (%d)", checks);
    CHECK(xmasOn(a.w()), "the mock is posed at the end");
    CHECK(bad == 0, "night %d: %d of %d incremental frames differ from a full render", night, bad, checks);
    CHECK(diff == 0, "night %d: two runs differ in %d of %d frames", night, diff, checks);
    a.stop();
    b.stop();
    d.stop();
  }
}

// ---- (5) the snow repaints the cache --------------------------------------------------------------------

static void testSnow() {
  Rig r;
  CHECK(toMoment(r, 1, camp(3.f, false)), "start");
  appInvalidate(r.app);
  appRender(r.app, r.surf);
  const std::vector<Pixel> before = r.fb;
  r.w().xmas.m.extras = XE_SNOW;
  appTick(r.app);
  appRender(r.app, r.surf);   // incremental: the toggle invalidated the cache
  int changed = 0;
  for (int y = LS_GROUND_Y; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) changed += before[(size_t)y * SS_WIDTH + x] != r.fb[(size_t)y * SS_WIDTH + x];
  CHECK(changed > 30000, "the snow changed only %d ground pixels", changed);
  printf("  snow: %d of %d ground pixels changed\n", changed, (SS_HEIGHT - LS_GROUND_Y) * SS_WIDTH);
  r.stop();
}
#endif  // SS_XMAS

// ---- (4) memory ----------------------------------------------------------------------------------------

static void testMemory() {
#if SS_XMAS
  printf("  SS_XMAS on: sizeof(App) %u, World %u, XmasState %u (shares LookoutState %u with StreamLife %u), XmasMock %u\n",
         (unsigned)appAppBytes(), (unsigned)sizeof(World), (unsigned)sizeof(XmasState), (unsigned)sizeof(LookoutState),
         (unsigned)sizeof(StreamLife), (unsigned)sizeof(XmasMock));
#else
  printf("  SS_XMAS off: sizeof(App) %u, World %u (no Christmas state at all)\n", (unsigned)appAppBytes(), (unsigned)sizeof(World));
#endif
  // 29,992 before the mockups; the stream's round 2 moved StreamLife into the same union (world.h), so
  // App is smaller now: the mockups still add nothing.
  CHECK(appAppBytes() <= 29992u, "App is %u bytes, was 29992 before the mockups", (unsigned)appAppBytes());
}

int main() {
  testMemory();
#if SS_XMAS
  testBounds();
  testIncremental();
  testSnow();
#endif
  testIdentity();
  printf("test_xmas: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
