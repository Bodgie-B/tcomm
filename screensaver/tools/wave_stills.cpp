// Stills for Mike's review of the hiker's wave to a passer-by (2026-11-16): the new, livelier wave
// (react.cpp, kWaveTicks 95 = 3.8 s) and, to compare, the old one (65 ticks, 2.6 s), in the daytime
// camp. Eight frames across each wave: before, rising, the peak swings, the bounces, settling back.
// The old row is the same moments scaled to its shorter length, so the phases line up.
//
// Not part of the CMake build. The new wave is this tree's library; the old one is a library built from
// the previous react.cpp / react.h / app.cpp (git show HEAD:...). From the project folder:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -I screensaver/core screensaver/tools/wave_stills.cpp BUILD/libss_core.a -o BUILD/wave_stills
//   BUILD/wave_stills OUT_DIR new     writes OUT_DIR/new_NN.ppm (800 x 480) and OUT_DIR/new_rect.txt
//   (the same, linked against the old library: OUT_DIR old)
//   python -I screensaver/tools/wave_sheet.py OUT_DIR SHEET_DIR
//
// -DSS_XMAS=1 as the host build has it (CMakeLists.txt; with -DSS_XMAS=OFF builds, 0).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "ss_app.h"
#include "scene/react.h"
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

// Eight moments of the new wave, in seconds from the start of the wave (3.8 s long).
const float kNewT[8] = {0.f, 0.3f, 0.7f, 1.05f, 1.5f, 2.2f, 3.3f, 3.7f};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: wave_stills OUT_DIR new|old\n");
    return 2;
  }
  const char* out = argv[1];
  const bool old_wave = strcmp(argv[2], "old") == 0;
  if (ssXmasSwitch() != SS_XMAS) {   // the World's layout depends on it (scene/xmas_switch.h)
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  const uint64_t seed = 2;
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  in.mood = 3.f;
  in.spine_force = 1 + SF_MIDDAY;

  void* mem = calloc(1, SS_APP_BYTES);
  std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
  Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* app = appStart(mem, kAlloc, seed, in);
  // Run to a moment he is standing about with empty hands, a bit into the day.
  int t = 0;
  for (; t < 9000; ++t) {
    appTick(app);
    appRender(app, surf);
    if (t > 800 && reactWaveOk(*appWorld(app)) && appWorld(app)->hiker.facing > 0) break;
  }
  printf("seed %llu: wave moment at tick %d, beat %s\n", (unsigned long long)seed, t, appBeatName(app));
  const float len = old_wave ? 2.6f : 3.8f;
  const float scale = len / 3.8f;
  in.passer_by = true;
  appSetInputs(app, in);
  // The wave starts on the first tick after the arrival; frame k is k ticks into it.
  int tick_in = 0;
  Rect rc{0, 0, 0, 0};
  FILE* rf = nullptr;
  char path[512];
  snprintf(path, sizeof path, "%s/%s_rect.txt", out, old_wave ? "old" : "new");
  rf = fopen(path, "w");
  for (int k = 0; k < 8; ++k) {
    const int want = (int)(kNewT[k] * scale * SS_TICK_HZ + 0.5f) + 1;   // +1: the tick that starts it
    while (tick_in < want) {
      appTick(app);
      appRender(app, surf);
      ++tick_in;
    }
    appInvalidate(app);
    appRender(app, surf);
    snprintf(path, sizeof path, "%s/%s_%02d.ppm", out, old_wave ? "old" : "new", k + 1);
    writePPM(path, fb.data());
    const World& w = *appWorld(app);
    appHikerRect(app, &rc);
    const float secs = (float)(tick_in - 1) / SS_TICK_HZ;
    printf("  frame %d: %.2f s into the wave, react active %d wave %d ticks %d, hiker rect %d,%d-%d,%d\n", k + 1, (double)secs,
           (int)w.react.active, (int)w.react.wave, (int)w.react.ticks, rc.x0, rc.y0, rc.x1, rc.y1);
    if (rf) fprintf(rf, "%d %.2f %d %d %d %d\n", k + 1, (double)secs, rc.x0, rc.y0, rc.x1, rc.y1);
  }
  if (rf) fclose(rf);
  appStop(app, kAlloc);
  free(mem);
  return 0;
}
