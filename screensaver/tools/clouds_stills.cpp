// Stills for Mike's stage 19 review: the three high clouds of cloud cover 2-3 now drift with the two
// ordinary clouds (landscape.h cloudHigh). The day scene with cover 3 at three moments 20 s apart
// (flip through them: all five clouds move left together), then dusk and night with cover 3.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -I screensaver/core screensaver/tools/clouds_stills.cpp BUILD/libss_core.a
//     (-DSS_XMAS=1 as the host build has it, CMakeLists.txt; with -DSS_XMAS=OFF builds, 0)
//     -o BUILD/clouds_stills
//   BUILD/clouds_stills OUT_DIR    writes OUT_DIR/NN_name.ppm (800 x 480)
//
// Each still runs the scene (rendering every tick, as the panel does) to its tick and takes a full
// render. The cover comes from the palette's overcast (Inputs::weather.overcast 0.4: 3 eighths) on a
// clear reading, so the loop's weather adds none.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "ss_app.h"
#include "scene/landscape.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

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

enum Look { DAY, DUSK, NIGHT };

Inputs inputsFor(Look look) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  in.weather.overcast = 0.4f;   // cover 3
  switch (look) {
    case DAY: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
    case DUSK: in.mood = 4.85f; in.spine_force = 1 + SF_EVENING; break;
    case NIGHT:
      in.mood = 0.f;
      in.spine_force = 1 + SF_EVENING;
      in.night_mode = true;
      in.weather.night_dim = 1.f;
      in.weather.night_gain = 1.6f;
      break;
  }
  return in;
}

struct Shot { const char* name; int tick; };

// One run of `look` from `seed`, taking each shot (ticks ascending) as a full render.
void run(Look look, uint64_t seed, const Shot* shots, int n, const char* out) {
  void* mem = calloc(1, SS_APP_BYTES);
  std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
  Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* app = appStart(mem, kAlloc, seed, inputsFor(look));
  int k = 0;
  for (int tick = 0; k < n; ++tick) {
    appTick(app);
    appRender(app, surf);
    if (tick != shots[k].tick) continue;
    appInvalidate(app);
    appRender(app, surf);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", out, shots[k].name);
    writePPM(path, fb.data());
    const World& w = *appWorld(app);
    printf("  %-22s seed %llu tick %5d  cover %d  high clouds at x", shots[k].name, (unsigned long long)seed, tick, w.sky_cover);
    for (int i = 0; i < CLOUD_HIGH_COUNT; ++i) {
      Cloud c;
      cloudHigh(w.land, i, w.tick, c);
      printf(" %4d", (int)c.x);
    }
    printf("  lower clouds at x %4d %4d\n", (int)w.clouds[0].x, (int)w.clouds[1].x);
    ++k;
  }
  appStop(app, kAlloc);
  free(mem);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: clouds_stills OUT_DIR\n");
    return 2;
  }
  const char* out = argv[1];
  if (ssXmasSwitch() != SS_XMAS) {   // the World's layout depends on it (scene/xmas_switch.h)
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  static const Shot kDay[] = {{"01_day_cover3_t0", 1200}, {"02_day_cover3_t20s", 1700}, {"03_day_cover3_t40s", 2200}};
  run(DAY, 2, kDay, 3, out);
  static const Shot kDusk[] = {{"04_dusk_cover3", 1700}};
  run(DUSK, 2, kDusk, 1, out);
  static const Shot kNight[] = {{"05_night_cover3", 1700}};
  run(NIGHT, 2, kNight, 1, out);
  return 0;
}
