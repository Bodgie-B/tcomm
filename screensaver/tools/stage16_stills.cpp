// Single stills for Mike's stage 16 revisions: the wild blaze against the ice, the shortened sunrise
// ledge, the ridges without outlines, and Ann's new hair.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I screensaver/core screensaver/tools/stage16_stills.cpp BUILD/libss_core.a
//     -o BUILD/stage16_stills
//   BUILD/stage16_stills OUT_DIR    writes OUT_DIR/NN_name.ppm (800 x 480)
//
// Each still runs the scene with its inputs until its moment, then writes that frame and, for the
// fire, two more a few ticks later. Convert to PNG with any image tool (the review used PIL).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <vector>

#include "ss_app.h"
#include "scene/fireice.h"
#include "scene/spine.h"
#include "scene/sunrise.h"
#include "scene/visitors.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

enum Look { COLD_DAY, COLD_NIGHT, SUNRISE, MIDDAY };

Inputs inputsFor(Look look, int dog) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  in.chip = true;
  in.dog_out = dog;
  switch (look) {
    case COLD_DAY: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
    case COLD_NIGHT:
      in.mood = 0.f;
      in.spine_force = 1 + SF_EVENING;
      in.night_mode = true;
      in.weather.night_dim = 1.f;
      in.weather.night_gain = 1.6f;
      break;
    case SUNRISE:
      in.visitors = true;
      in.mood = 1.f;
      in.sunrise_window = true;
      in.sky.have = true;
      in.sky.sun_az = 100.f;
      in.sky.sun_alt = -1.f;
      in.sky.moon_alt = -30.f;
      break;
    case MIDDAY: in.visitors = true; in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
  }
  return in;
}

bool writePPM(const char* path, const Pixel* px) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT);
  std::vector<unsigned char> row((size_t)SS_WIDTH * 3);
  for (int y = 0; y < SS_HEIGHT; ++y) {
    for (int x = 0; x < SS_WIDTH; ++x) {
      const Pixel p = px[(size_t)y * SS_WIDTH + x];
      row[(size_t)x * 3] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  return fclose(f) == 0;
}

typedef std::function<bool(const App*, const World&)> Pred;

struct Still {
  const char* name;
  Look look;
  int dog;
  uint8_t ask;     // the tenant's arrow from 40 s in (0 none)
  bool couple;     // a seed whose first loop has the couple
  Pred want;
  int more[2];     // further frames this many ticks after the first (0: none)
};

bool shoot(const Still& s, const char* out) {
  for (uint64_t seed = 1; seed <= 400; ++seed) {
    Inputs in = inputsFor(s.look, s.dog);
    void* mem = malloc(SS_APP_BYTES);
    std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
    Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    App* app = appStart(mem, kAlloc, seed, in);
    if (s.couple && !appWorld(app)->vis.planned) {
      appStop(app, kAlloc);
      free(mem);
      continue;
    }
    int hit = -1, k = 0;
    for (int tick = 0; tick < 25 * 900; ++tick) {
      if (s.ask && tick == 25 * 40) {
        in.temp_ask[0] = s.ask;
        appSetInputs(app, in);
      }
      if (appTick(app)) break;
      appRender(app, surf);
      char path[512];
      if (hit < 0 && s.want(app, *appWorld(app))) {
        hit = tick;
        snprintf(path, sizeof path, "%s/%s.ppm", out, s.name);
        writePPM(path, fb.data());
        printf("  %-26s seed %llu tick %d\n", s.name, (unsigned long long)seed, tick);
        if (!s.more[0]) break;
      } else if (hit >= 0 && k < 2 && s.more[k] && tick == hit + s.more[k]) {
        snprintf(path, sizeof path, "%s/%s%c.ppm", out, s.name, 'b' + k);
        writePPM(path, fb.data());
        if (++k == 2 || !s.more[k]) break;
      }
    }
    appStop(app, kAlloc);
    free(mem);
    if (hit >= 0) return true;
  }
  printf("  %-26s NOT FOUND\n", s.name);
  return false;
}

bool beatIs(const App* a, const char* b) { return strcmp(appBeatName(a), b) == 0; }
bool stepIs(const World& w, const char* s) { return strcmp(visitorsStepName(w), s) == 0; }
bool stageIs(const World& w, const char* s) { return strcmp(fireIceStageName(w), s) == 0; }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: stage16_stills OUT_DIR\n");
    return 2;
  }
  const char* out = argv[1];
  const Still stills[] = {
      // The blaze at its peak: urgent cold, just fed (the flare), the ice in tight.
      {"01_ice_big_fire", COLD_DAY, 0, 1, false,
       [](const App*, const World& w) { return stageIs(w, "warm") && w.fi.ice < 0.45f && !w.hiker.walking && w.fi.st > 1.2f && w.fire.intensity > 1.6f; }, {6, 13}},
      {"01d_ice_big_fire_night", COLD_NIGHT, 0, 1, false,
       [](const App*, const World& w) { return stageIs(w, "warm") && w.fi.ice < 0.45f && !w.hiker.walking && w.fi.st > 1.2f && w.fire.intensity > 1.6f; }, {0, 0}},
      {"06_ice_icicles", COLD_DAY, 3, 1, false,
       [](const App*, const World& w) { return stageIs(w, "warm") && w.fi.ice < 0.4f && w.fi.pine_q == 2 && w.fi.st > 1.5f; }, {0, 0}},
      {"02_sunrise_ledge", SUNRISE, 0, 0, false,
       [](const App*, const World& w) { return w.sr.lit[1] == 2 && w.sr.lit[2] == 0 && w.sr.sat_tick != 0; }, {0, 0}},
      {"02b_sunrise_ledge_dog", SUNRISE, 1, 0, false,
       [](const App*, const World& w) { return w.sr.broke_tick != 0 && w.tick == w.sr.broke_tick + 200; }, {0, 0}},
      {"02c_sunrise_full_light", SUNRISE, 0, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "linger") && w.sr.slab_lit && w.sr.fog_thin == 3; }, {0, 0}},
      {"03_sunrise_mountains_early", SUNRISE, 0, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "dawn") && w.mood_override >= 0.7f; }, {0, 0}},
      {"03b_sunrise_mountains_blue", SUNRISE, 0, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "dawn") && w.mood_override < 0.5f && w.tick > 25 * 50; }, {0, 0}},
      {"04_couple", MIDDAY, 0, 0, true, [](const App*, const World& w) { return stepIs(w, "settle") && w.vis.st > 0.5f; }, {0, 0}},
  };
  int missing = 0;
  for (const Still& s : stills)
    if (!shoot(s, out)) ++missing;
  return missing ? 1 : 0;
}
