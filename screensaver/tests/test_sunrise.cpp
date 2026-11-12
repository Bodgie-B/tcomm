// Tests for stage 15's sunrise overlook (core/scene/sunrise.cpp) and the once-a-day rule for both
// set pieces (core/set_piece_day.h):
//
//   [1] the family: the sunrise window picks it; rain, storm and snow skip it; the sunset window wins
//   [2] whole loops over several seeds: no overruns, its length, the sun breaks once in the sunbreak
//       beat (the key moment), the stars are gone by then, the mood only climbs, the loop ends black
//   [3] every frame equals a full render (the build, the sun, the stars, the light's repaints)
//   [4] the same seed replays frame for frame
//   [5] a gray, dry morning plays muted; a dog that is out sits beside him
//   [6] the once-a-day rule: a touch before the key moment leaves the day's showing unused (for the
//       sunrise and the Lookout), one played to the key moment uses it, a forced one never counts,
//       the next day it is wanted again; the camp loop follows
//   [7] memory: the scene block within its budget, the sunrise in the Lookout's place
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "set_piece_day.h"
#include "scene/spine.h"
#include "scene/spine_family.h"
#include "scene/sunrise.h"
#include "scene/world.h"

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
      printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

static Inputs sunriseInputs() {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 1.f;
  in.have_outside = true;
  in.sunrise_window = true;
  in.chip = true;
  in.sky.have = true;
  in.sky.sun_az = 100.f;
  in.sky.sun_alt = -1.f;
  in.sky.moon_alt = -30.f;
  return in;
}

struct Rig {
  void* mem = nullptr;
  std::vector<Pixel> fb, full;
  Surface surf{}, fsurf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    full.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    fsurf = Surface{full.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = mem ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  bool step() {
    const bool n = appTick(app);
    appRender(app, surf);
    return n;
  }
  int fullDiff() {
    appInvalidate(app);
    appRender(app, fsurf);
    int n = 0;
    for (size_t i = 0; i < fb.size(); ++i) n += fb[i] != full[i] ? 1 : 0;
    return n;
  }
  const World& w() const { return *appWorld(app); }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
    mem = nullptr;
  }
};

static uint32_t hashFb(const std::vector<Pixel>& fb) {
  uint32_t h = 2166136261u;
  for (Pixel p : fb) h = (h ^ p) * 16777619u;
  return h;
}

static void testFamily() {
  printf("[1] the family\n");
  Inputs in = sunriseInputs();
  CHECK(spineFamilyFor(in) == SF_SUNRISE, "window: family %d", spineFamilyFor(in));
  in.night_mode = true;
  CHECK(spineFamilyFor(in) == SF_SUNRISE, "window beats the night: family %d", spineFamilyFor(in));
  for (int code : {1, 2, 7}) {
    Inputs r = sunriseInputs();
    r.weather_code = code;
    CHECK(spineFamilyFor(r) != SF_SUNRISE, "code %d skips it", code);
  }
  Inputs g = sunriseInputs();
  g.weather.overcast = 0.7f;   // gray and dry: still plays
  CHECK(spineFamilyFor(g) == SF_SUNRISE, "gray plays");
  Inputs l = sunriseInputs();
  l.lookout_window = true;
  CHECK(spineFamilyFor(l) == SF_LOOKOUT, "the sunset window wins");
  Inputs f;
  memset(&f, 0, sizeof f);
  f.spine_force = 1 + SF_SUNRISE;
  CHECK(spineFamilyFor(f) == SF_SUNRISE, "forced");
  Inputs z;
  memset(&z, 0, sizeof z);
  z.mood = 1.f;
  CHECK(spineFamilyFor(z) == SF_MORNING, "no window: the morning camp");
}

static void testLoops() {
  printf("[2] whole loops\n");
  for (uint64_t seed = 1; seed <= 6; ++seed) {
    Rig r;
    CHECK(r.start(seed, sunriseInputs()), "start");
    CHECK(appSpineFamily(r.app) == SF_SUNRISE, "seed %d family %d", (int)seed, appSpineFamily(r.app));
    const uint32_t len = appLoopTicks(r.app);
    CHECK(len >= 240u * SS_TICK_HZ && len <= 320u * SS_TICK_HZ, "seed %d length %u s", (int)seed, len / SS_TICK_HZ);
    float mood = -1.f;
    bool mood_ok = true, shown_before = false, broke_in_beat = false, stars_gone = true, in_frame = true;
    uint32_t ticks = 0;
    int breaks = 0;
    uint32_t last_broke = 0;
    int overruns = 0;
    for (;;) {
      const World& w = r.w();
      const float fade_before = w.fade;
      overruns = appSpineOverruns(r.app);
      const bool script_left = w.spine.run.steps && w.spine.run.i < w.spine.run.n;
      if (appTick(r.app)) {
        CHECK(!script_left, "seed %d: the leave's script ran out of time", (int)seed);
        CHECK(fade_before >= 0.99f, "seed %d: the loop ends black (fade %.2f)", (int)seed, (double)fade_before);
        break;
      }
      ++ticks;
      const char* b = appBeatName(r.app);
      if (strcmp(b, "leave") != 0 && w.mood_override + 1e-4f < mood) mood_ok = false;
      mood = w.mood_override;
      if (w.sr.broke_tick != last_broke) {
        ++breaks;
        last_broke = w.sr.broke_tick;
        broke_in_beat = strcmp(b, "sunbreak") == 0;
        if (sunriseStarsOut(w) != 0) stars_gone = false;
      }
      if (w.sr.broke_tick == 0 && appSetPieceShown(r.app)) shown_before = true;
      if (w.hiker.visible && (w.hiker_full.x0 < -80 || w.hiker_full.x1 > SS_WIDTH - 140)) in_frame = false;
      if (ticks > len + 10) break;
    }
    CHECK(overruns == 0, "seed %d overruns %d", (int)seed, overruns);
    CHECK(breaks == 1 && broke_in_beat, "seed %d: the sun broke %d times, in the sunbreak %d", (int)seed, breaks, (int)broke_in_beat);
    CHECK(stars_gone, "seed %d: stars still out at the break", (int)seed);
    CHECK(mood_ok, "seed %d: the mood went back", (int)seed);
    CHECK(!shown_before, "seed %d: shown before the key moment", (int)seed);
    CHECK(in_frame, "seed %d: he left the frame or entered the chip's corner", (int)seed);
    CHECK(ticks + 1 == len, "seed %d: the loop ran %u of %u ticks", (int)seed, ticks + 1, len);
    r.stop();
  }
}

static void testFull() {
  printf("[3] incremental equals full\n");
  for (uint64_t seed : {2ull, 5ull}) {
    Rig r;
    r.start(seed, sunriseInputs());
    uint8_t lit[SR_RIDGES] = {}, glow = 0, thin = 0;
    int checks = 0, worst = 0;
    uint32_t broke = 0;
    for (int t = 0; t < 7600; ++t) {
      if (r.step()) break;
      const World& w = r.w();
      bool now = t % 300 == 0;
      for (int k = 0; k < SR_RIDGES; ++k)
        if (w.sr.lit[k] != lit[k]) now = true;
      if (w.sr.fog_glow != glow || w.sr.fog_thin != thin || w.sr.broke_tick != broke) now = true;
      if (w.sr.broke_tick && w.tick < w.sr.broke_tick + 60 && w.tick % 7 == 0) now = true;
      for (int k = 0; k < SR_RIDGES; ++k) lit[k] = w.sr.lit[k];
      glow = w.sr.fog_glow;
      thin = w.sr.fog_thin;
      broke = w.sr.broke_tick;
      if (!now) continue;
      const int d = r.fullDiff();
      ++checks;
      if (d > worst) worst = d;
      if (d) printf("    seed %d tick %d beat %s: %d px differ\n", (int)seed, t, appBeatName(r.app), d);
    }
    CHECK(worst == 0 && checks > 30, "seed %d: %d checks, worst %d px", (int)seed, checks, worst);
    r.stop();
  }
}

static void testReplay() {
  printf("[4] replay\n");
  std::vector<uint32_t> h[2];
  for (int run = 0; run < 2; ++run) {
    Rig r;
    Inputs in = sunriseInputs();
    in.dog_out = 3;
    r.start(9, in);
    for (int t = 0; t < 7000; ++t) {
      r.step();
      if (t % 97 == 0) h[run].push_back(hashFb(r.fb));
    }
    r.stop();
  }
  CHECK(h[0] == h[1], "the same seed replays");
}

static void testMutedAndDog() {
  printf("[5] gray morning, the dogs\n");
  Rig r;
  Inputs in = sunriseInputs();
  in.weather.overcast = 0.7f;
  in.dog_out = 1;
  r.start(4, in);
  CHECK(r.w().sr.muted, "gray: muted");
  bool sat = false;
  for (int t = 0; t < 7000; ++t) {
    r.step();
    const World& w = r.w();
    if (strcmp(appBeatName(r.app), "dawn") == 0 && w.sr.dogs[0].slot >= 0) {
      const Critter& c = w.critters[w.sr.dogs[0].slot];
      if (c.visible && c.action == CA_SIT && c.feet.x > w.sr.land.ft.x - 70.f && c.feet.x < w.hiker.feet.x) sat = true;
    }
  }
  CHECK(sat, "Lucy sits beside him on the ledge");   // near its end (within 70 px of the tip)
  r.stop();
  Rig c;
  c.start(4, sunriseInputs());
  CHECK(!c.w().sr.muted, "clear: not muted");
  c.stop();
}

// Plays the host's rule: the day's window, a start, ticks, maybe a touch (a stop), the note.
static void testOnceADay() {
  printf("[6] once a day\n");
  for (int piece = 0; piece < 2; ++piece) {   // 0 the sunrise, 1 the Lookout
    const int fam = piece == 0 ? SF_SUNRISE : SF_LOOKOUT;
    SetPieceDay day;
    setPieceReset(day);
    int32_t today = 400;
    auto inputs = [&](bool want) {
      Inputs in = sunriseInputs();
      in.sunrise_window = piece == 0 && want;
      in.lookout_window = piece == 1 && want;
      in.mood = piece == 0 ? 1.f : 4.f;
      return in;
    };
    // A start in the window; touched after 8 s.
    CHECK(setPieceWanted(day, today, true), "wanted at first");
    Rig r;
    r.start(11, inputs(setPieceWanted(day, today, true)));
    CHECK(appSpineFamily(r.app) == fam, "plays it");
    bool counted = false;
    for (int t = 0; t < 200; ++t) {
      r.step();
      counted = setPieceNote(day, today, appSpineFamily(r.app) == fam, appSetPieceShown(r.app), false) || counted;
    }
    r.stop();
    CHECK(!counted && setPieceWanted(day, today, true), "piece %d: a touch in the first seconds leaves the day's showing", piece);
    // Started again: played to its key moment.
    r.start(12, inputs(setPieceWanted(day, today, true)));
    CHECK(appSpineFamily(r.app) == fam, "plays it again");
    int at = -1;
    for (int t = 0; t < 25 * 330 && at < 0; ++t) {
      r.step();
      if (setPieceNote(day, today, appSpineFamily(r.app) == fam, appSetPieceShown(r.app), false)) at = t;
    }
    CHECK(at > 0 && !setPieceWanted(day, today, true), "piece %d: counted at the key moment (tick %d)", piece, at);
    // The host stops asking; the loop after it is the camp.
    Inputs camp = inputs(setPieceWanted(day, today, true));
    appSetInputs(r.app, camp);
    int next = -1;
    for (int t = 0; t < 25 * 330; ++t)
      if (r.step()) {
        next = appSpineFamily(r.app);
        break;
      }
    CHECK(next >= 0 && !spineSetPiece(next), "piece %d: the next loop is the camp (family %d)", piece, next);
    r.stop();
    // A forced one never counts; the next day it is wanted again.
    SetPieceDay d2;
    setPieceReset(d2);
    CHECK(!setPieceNote(d2, today, true, true, true), "forced never counts");
    CHECK(setPieceNote(d2, today, true, true, false) && !setPieceNote(d2, today, true, true, false), "counts once");
    CHECK(setPieceWanted(d2, today + 1, true) && !setPieceWanted(d2, today + 1, false) && !setPieceWanted(d2, -1, true),
          "the next day, in the window, on a known day");
  }
}

static void testMemory() {
  printf("[7] memory\n");
  printf("    sizeof(App) %u, Sunrise %u, Visitors %u, LookoutLand %u\n", appAppBytes(), (unsigned)sizeof(Sunrise),
         (unsigned)sizeof(Visitors), (unsigned)sizeof(LookoutLand));
  CHECK(appAppBytes() <= SS_APP_BYTES, "App %u", appAppBytes());
  CHECK(appAppBytes() <= 29768u + 2400u, "stage 15 within its 2,400 bytes: App %u", appAppBytes());
  CHECK(sizeof(Sunrise) <= sizeof(LookoutLand), "the sunrise in the Lookout's place");
}

int main() {
  testFamily();
  testLoops();
  testFull();
  testReplay();
  testMutedAndDog();
  testOnceADay();
  testMemory();
  printf("test_sunrise: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
