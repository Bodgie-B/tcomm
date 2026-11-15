// The high clouds of cloud cover 2-3 (stage 19, landscape.h cloudHigh): they used to sit still in the
// static cache while the two ordinary clouds drifted under them; now they drift with them.
//
// (1) At tick 0 they are exactly the still clouds they replace: the same shapes, the same pixels (the
//     old drawCloudCover code for cover 2-3 is copied below as the reference).
// (2) They drift left at CLOUD_HIGH_PX_PER_S, all three together (their spacing kept), one whole
//     pixel at a time, and wrap round as cloudStep does (from x -141 to 939).
// (3) Their bounds hold every pixel they draw, and nothing is drawn below the horizon.
// (4) In the app: incremental rendering equals a full render with cover 2 and 3 over a long run
//     (every cloud wraps at least once), and across cover changes 3 to 4, 4 to 3 and 2 to 1 mid-loop;
//     two runs from one seed agree; World::sky_cover follows the cache's cover; the doorbell does
//     not stop them.
#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/landscape.h"
#include "scene/react.h"
#include "scene/world.h"

#include <math.h>
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

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

static const Pixel kMark = 0x1234;   // a colour no palette slot here has

static void makeLand(Landscape& ls, uint64_t seed) {
  Rng rng;
  rng.seed(seed);
  landscapeGenerate(ls, rng);
}

// The pre-stage-19 drawCloudCover for cover 2-3, as it was (landscape.cpp), shifted by `shift` px
// with the same wrap: the reference the drifting clouds must match.
static void oldHighClouds(const Landscape& ls, int shift, Surface& s, const Palette& pal, const Rect& clip) {
  Rng rng;
  rng.seed(0x434C4F5544ull + (uint64_t)(uint16_t)ls.stars[0].x * 65537u + (uint64_t)(uint16_t)ls.stars[1].y);
  static const float kAt[3][2] = {{130.f, 64.f}, {410.f, 40.f}, {690.f, 78.f}};
  for (const float* at : kAt) {
    Cloud c;
    const float jx = rng.range(-50.f, 50.f);
    const float jy = rng.range(-8.f, 8.f);
    cloudGenerate(c, rng, at[0] + jx, at[1] + jy, 0.f);
    int x = (int)floorf(c.x) - shift;
    if (x < -140) x += 1080;
    c.x = (float)x;
    cloudDraw(c, s, pal, clip);
  }
}

static void fill(std::vector<Pixel>& px, Pixel v) {
  for (Pixel& p : px) p = v;
}

// (1) and (3)
static void testShapesAndBounds() {
  Palette pal;
  computePalette(3.f, WeatherTint{}, pal);
  std::vector<Pixel> a((size_t)SS_WIDTH * SS_HEIGHT), b(a.size());
  Surface sa{a.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0}, sb{b.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  const Rect full{0, 0, SS_WIDTH, SS_HEIGHT};
  int same = 0, tried = 0, spill = 0, below = 0, drawn = 0;
  for (uint64_t seed = 1; seed <= 12; ++seed) {
    Landscape ls;
    makeLand(ls, seed);
    for (uint32_t tick = 0; tick < 6000; tick += 173) {
      fill(a, kMark);
      fill(b, kMark);
      oldHighClouds(ls, (int)((uint64_t)tick * CLOUD_HIGH_PX_PER_S / SS_TICK_HZ % 1080u), sa, pal, full);
      cloudHighDraw(ls, tick, sb, pal, full);
      ++tried;
      if (memcmp(a.data(), b.data(), a.size() * sizeof(Pixel)) == 0) ++same;
      Rect bb[CLOUD_HIGH_COUNT];
      cloudHighBounds(ls, tick, bb);
      for (int y = 0; y < SS_HEIGHT; ++y)
        for (int x = 0; x < SS_WIDTH; ++x) {
          if (b[(size_t)y * SS_WIDTH + x] == kMark) continue;
          ++drawn;
          if (y >= LS_HORIZON) ++below;
          bool in = false;
          for (const Rect& r : bb) in = in || (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1);
          if (!in) ++spill;
        }
    }
  }
  CHECK(same == tried, "the drifting clouds differ from the old still ones moved by the same shift in %d of %d frames", tried - same,
        tried);
  CHECK(drawn > 0, "nothing drawn");
  CHECK(spill == 0, "%d drawn pixels outside the high clouds' bounds", spill);
  CHECK(below == 0, "%d pixels drawn at or below the horizon", below);
  // A strip drawn alone gives the same pixels as the frame.
  Landscape ls;
  makeLand(ls, 5);
  int strip_bad = 0;
  for (uint32_t tick = 0; tick < 3000; tick += 211) {
    fill(a, kMark);
    fill(b, kMark);
    cloudHighDraw(ls, tick, sa, pal, full);
    for (int k = 0; k < SS_STRIP_COUNT; ++k)
      cloudHighDraw(ls, tick, sb, pal, Rect{0, k * SS_STRIP_ROWS, SS_WIDTH, (k + 1) * SS_STRIP_ROWS});
    if (memcmp(a.data(), b.data(), a.size() * sizeof(Pixel)) != 0) ++strip_bad;
  }
  CHECK(strip_bad == 0, "strip-by-strip drawing differs from the whole frame at %d moments", strip_bad);
  printf("[1,3] shapes as the old still clouds (%d frames), bounds hold every pixel, none below the horizon\n", tried);
}

// (2)
static void testDrift() {
  Landscape ls;
  makeLand(ls, 3);
  Cloud c0[CLOUD_HIGH_COUNT];
  for (int i = 0; i < CLOUD_HIGH_COUNT; ++i) cloudHigh(ls, i, 0, c0[i]);
  int wraps[CLOUD_HIGH_COUNT] = {0, 0, 0};
  int bad_step = 0, bad_speed = 0, bad_spacing = 0, bad_range = 0;
  float prev[CLOUD_HIGH_COUNT];
  for (int i = 0; i < CLOUD_HIGH_COUNT; ++i) prev[i] = c0[i].x;
  for (uint32_t t = 1; t <= 12000; ++t) {
    float xs[CLOUD_HIGH_COUNT];
    for (int i = 0; i < CLOUD_HIGH_COUNT; ++i) {
      Cloud c;
      cloudHigh(ls, i, t, c);
      xs[i] = c.x;
      if (c.x != floorf(c.x)) ++bad_step;
      if (c.x < CLOUD_WRAP_LEFT || c.x >= CLOUD_WRAP_RIGHT) ++bad_range;
      const float d = prev[i] - c.x;
      if (d == 0.f || d == 1.f) {
      } else if (prev[i] == -140.f && c.x == 939.f) {
        ++wraps[i];
      } else {
        ++bad_step;
      }
      // Its x is its start less the whole pixels of 5 px/s, round the wrap.
      int want = (int)c0[i].x - (int)((uint64_t)t * 5u / 25u);
      while (want < -140) want += 1080;
      if ((int)c.x != want) ++bad_speed;
      prev[i] = c.x;
      // The shapes do not change with time.
      if (c.n != c0[i].n || c.y != c0[i].y || memcmp(c.rx, c0[i].rx, sizeof c.rx) != 0) ++bad_step;
    }
    // All three together: the gaps between them stay what they were (round the 1080 px wrap).
    for (int i = 1; i < CLOUD_HIGH_COUNT; ++i) {
      int g0 = (int)(c0[i].x - c0[0].x), g = (int)(xs[i] - xs[0]);
      g0 = ((g0 % 1080) + 1080) % 1080;
      g = ((g % 1080) + 1080) % 1080;
      if (g != g0) ++bad_spacing;
    }
  }
  CHECK(bad_step == 0, "%d steps that were not 0 or 1 whole pixel left (or a wrap from -140 to 939)", bad_step);
  CHECK(bad_speed == 0, "%d positions off 5 px/s", bad_speed);
  CHECK(bad_spacing == 0, "%d ticks where the spacing changed", bad_spacing);
  CHECK(bad_range == 0, "%d positions outside the wrap range", bad_range);
  // 12000 ticks = 480 s = 2400 px: each wraps twice.
  for (int i = 0; i < CLOUD_HIGH_COUNT; ++i) CHECK(wraps[i] == 2, "cloud %d wrapped %d times in 480 s", i, wraps[i]);
  // 25 s: 125 px.
  Cloud a, b;
  cloudHigh(ls, 0, 1000, a);
  cloudHigh(ls, 0, 1000 + 25 * 25, b);
  CHECK(a.x - b.x == 125.f || a.x - b.x == 125.f - 1080.f, "25 s moved %.0f px", (double)(a.x - b.x));
  printf("[2] drift at %d px/s, together, whole pixels, wraps at -140 -> 939\n", CLOUD_HIGH_PX_PER_S);
}

// (4)
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
};

static Inputs inputsFor(int family, float mood, float overcast, bool night) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;   // a clear reading: the loop's weather adds no cover of its own
  in.spine_force = (uint8_t)(1 + family);
  in.weather.overcast = overcast;
  in.night_mode = night;
  in.weather.night_dim = night ? 1.f : 0.f;
  return in;
}

// Overcast giving each cover (landscapeSky rounds cover * 8).
static float overcastFor(int cover) { return ((float)cover + 0.1f) / 8.f; }

struct Change { int tick; int cover; };

static void runIncremental(const char* name, int family, float mood, bool night, int cover0, const Change* ch, int n_ch,
                           int ticks, int every) {
  Rig a, b, d;
  Inputs in = inputsFor(family, mood, overcastFor(cover0), night);
  CHECK(a.start(31, in) && b.start(31, in) && d.start(31, in), "%s: start", name);
  int checks = 0, bad = 0, diff = 0, cover_bad = 0, k = 0;
  for (int t = 0; t < ticks; ++t) {
    if (k < n_ch && ch[k].tick == t) {
      in.weather.overcast = overcastFor(ch[k].cover);
      appSetInputs(a.app, in);
      appSetInputs(b.app, in);
      appSetInputs(d.app, in);
      ++k;
    }
    appTick(a.app);
    appTick(b.app);
    appTick(d.app);
    appRender(a.app, a.surf);
    appRender(d.app, d.surf);
    const int want = k > 0 ? ch[k - 1].cover : cover0;
    if (appWorld(a.app)->sky_cover != want) ++cover_bad;
    // Around each change, every tick for a while; otherwise every `every`th tick.
    const bool near = k > 0 && t - ch[k - 1].tick < 30;
    if (t > 400 && (near || t % every == 0)) {
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++checks;
      if (memcmp(a.fb.data(), b.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) {
        if (++bad == 1) {
          for (size_t i = 0; i < a.fb.size(); ++i)
            if (a.fb[i] != b.fb[i]) {
              printf("  %s tick %d: first difference at x %d y %d\n", name, t, (int)(i % SS_WIDTH), (int)(i / SS_WIDTH));
              break;
            }
        }
      }
      if (memcmp(a.fb.data(), d.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) ++diff;
    }
  }
  CHECK(bad == 0, "%s: %d of %d incremental frames differ from a full render", name, bad, checks);
  CHECK(diff == 0, "%s: two runs differ in %d of %d frames", name, diff, checks);
  CHECK(cover_bad == 0, "%s: World::sky_cover off the asked cover in %d ticks", name, cover_bad);
  printf("  %s: %d frames checked\n", name, checks);
  a.stop();
  b.stop();
  d.stop();
}

static void testApp() {
  printf("[4] incremental equals full, two runs agree\n");
  // 6000 ticks (240 s, 1200 px): every high cloud wraps once.
  runIncremental("midday cover 3", SF_MIDDAY, 3.f, false, 3, nullptr, 0, 6000, 7);
  runIncremental("evening dusk cover 2", SF_EVENING, 5.f, false, 2, nullptr, 0, 6000, 11);
  runIncremental("night cover 3", SF_EVENING, 0.f, true, 3, nullptr, 0, 3000, 11);
  static const Change kChanges[] = {{1500, 4}, {2100, 3}, {2700, 2}, {3300, 1}, {3900, 3}, {4500, 6}, {5100, 3}};
  runIncremental("morning cover changes", SF_MORNING, 1.8f, false, 3, kChanges, 7, 5600, 13);
}

// The doorbell holds the story, not the sky: the high clouds keep drifting by World::tick.
static void testDoorbell() {
  Rig a;
  Inputs in = inputsFor(SF_MIDDAY, 3.f, overcastFor(3), false);
  CHECK(a.start(9, in), "start");
  bool rang = false, active = false;
  int bad = 0;
  for (int t = 0; t < 4000; ++t) {
    if (!rang && t > 1500 && reactCanStart(*appWorld(a.app))) {
      appPostEvent(a.app, Event{EV_DOORBELL, 0});
      rang = true;
    }
    appTick(a.app);
    const World& w = *appWorld(a.app);
    active = active || w.react.active;
    Cloud c, c0;
    cloudHigh(w.land, 0, w.tick, c);
    cloudHigh(w.land, 0, 0, c0);
    int want = (int)c0.x - (int)((uint64_t)w.tick * 5u / 25u % 1080u);
    if (want < -140) want += 1080;
    if ((int)c.x != want) ++bad;
  }
  CHECK(rang && active, "the bell did not ring, or no reaction ran");
  printf("[5] the doorbell rang (reaction ran: %d); the high clouds kept to World::tick\n", (int)active);
  CHECK(bad == 0, "%d ticks off", bad);
  a.stop();
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  testShapesAndBounds();
  testDrift();
  testApp();
  testDoorbell();
  if (g_fail) {
    printf("test_clouds: %d FAILED of %d checks\n", g_fail, g_checks);
    return 1;
  }
  printf("test_clouds: ok (%d checks)\n", g_checks);
  return 0;
}
