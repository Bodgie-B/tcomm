// Geometry check for the hiker's passer-by wave (2026-11-16): for every tick of the wave, for either
// facing, renders the frame and the same frame with the near arm left out (react.hands_full set, which
// keeps the head tilt and bounces), and measures the gap in pixels between the arm's own pixels and the
// head and hat. Prints the smallest gap seen. Build as wave_stills.cpp does:
//
//   g++ -std=c++17 -O2 -DSS_XMAS=1 -I screensaver/core screensaver/tools/wave_check.cpp BUILD/libss_core.a -o BUILD/wave_check
//   BUILD/wave_check
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

struct Img {
  std::vector<Pixel> px;
};

bool same(Pixel a, Pixel b) { return a == b; }

// One facing: returns the smallest gap over the wave (a big number when the arm never shows).
int run(int want_facing, const char* dump_dir) {
  const uint64_t seed = 2;
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  in.mood = 3.f;
  in.spine_force = 1 + SF_MIDDAY;
  int min_gap = 9999;
  int worst_tick = -1;
  // Two apps, identical, one with the arm left out.
  void* mem[2] = {calloc(1, SS_APP_BYTES), calloc(1, SS_APP_BYTES)};
  std::vector<Pixel> fb[2] = {std::vector<Pixel>((size_t)SS_WIDTH * SS_HEIGHT), std::vector<Pixel>((size_t)SS_WIDTH * SS_HEIGHT)};
  Surface sf[2] = {{fb[0].data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0}, {fb[1].data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0}};
  App* app[2];
  for (int i = 0; i < 2; ++i) app[i] = appStart(mem[i], kAlloc, seed, in);
  int t = 0;
  for (; t < 9000; ++t) {
    for (int i = 0; i < 2; ++i) {
      appTick(app[i]);
      appRender(app[i], sf[i]);
    }
    if (t > 800 && reactWaveOk(*appWorld(app[0])) && appWorld(app[0])->hiker.facing == want_facing) break;
  }
  in.passer_by = true;
  for (int i = 0; i < 2; ++i) appSetInputs(app[i], in);
  for (int k = 0; k < 100; ++k) {
    for (int i = 0; i < 2; ++i) {
      appTick(app[i]);
      if (i == 1) const_cast<World*>(appWorld(app[1]))->react.hands_full = true;
      appInvalidate(app[i]);
      appRender(app[i], sf[i]);
    }
    const World& w = *appWorld(app[0]);
    if (!w.react.active) break;
    Rect rc{0, 0, 0, 0};
    appHikerRect(app[1], &rc);
    const Pixel sil = fb[1][(size_t)((rc.y0 + rc.y1) / 2) * SS_WIDTH + (rc.x0 + rc.x1) / 2 - 0];
    (void)sil;
    // Silhouette colour: darkest pixel in the rect of the arm-less frame at the chest.
    const int hh = rc.y1 - rc.y0;
    const int head_y1 = rc.y0 + (int)(0.20f * hh);   // head and hat zone, no arm included
    // find silhouette colour from the middle of the torso
    Pixel sc = 0;
    {
      int best = 1 << 30;
      for (int y = rc.y0; y < rc.y1; ++y)
        for (int x = rc.x0; x < rc.x1; ++x) {
          const Pixel p = fb[1][(size_t)y * SS_WIDTH + x];
          const int sum = (int)red8(p) + green8(p) + blue8(p);
          if (sum < best) { best = sum; sc = p; }
        }
    }
    std::vector<unsigned char> head((size_t)SS_WIDTH * SS_HEIGHT, 0), arm((size_t)SS_WIDTH * SS_HEIGHT, 0);
    for (int y = rc.y0 - 20; y < rc.y1; ++y)
      for (int x = rc.x0 - 40; x < rc.x1 + 40; ++x) {
        if (y < 0 || x < 0 || x >= SS_WIDTH) continue;
        const size_t o = (size_t)y * SS_WIDTH + x;
        const bool a = same(fb[0][o], sc), b = same(fb[1][o], sc);
        if (b && y < head_y1) head[o] = 1;
        if (a && !b) arm[o] = 1;
      }
    // Gap: smallest Chebyshev distance from an arm pixel above the chin to any head/hat pixel.
    int gap = 9999;
    for (int y = rc.y0 - 20; y < head_y1 + 6; ++y)
      for (int x = rc.x0 - 40; x < rc.x1 + 40; ++x) {
        if (y < 0 || x < 0 || x >= SS_WIDTH || !arm[(size_t)y * SS_WIDTH + x]) continue;
        for (int dy = -8; dy <= 8; ++dy)
          for (int dx = -8; dx <= 8; ++dx) {
            const int yy = y + dy, xx = x + dx;
            if (yy < 0 || xx < 0 || xx >= SS_WIDTH || !head[(size_t)yy * SS_WIDTH + xx]) continue;
            const int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            if (d < gap) gap = d;
          }
      }
    if (gap < min_gap) { min_gap = gap; worst_tick = k; }
    if (k % 5 == 0) printf("  facing %+d tick %2d: gap to head/hat %d px\n", want_facing, k, gap);
    (void)dump_dir;
  }
  printf("facing %+d: smallest gap %d px at tick %d (9999 = arm never near)\n", want_facing, min_gap, worst_tick);
  for (int i = 0; i < 2; ++i) { appStop(app[i], kAlloc); free(mem[i]); }
  return min_gap;
}
}  // namespace

int main() {
  const int a = run(1, nullptr);
  const int b = run(-1, nullptr);
  printf("overall smallest gap %d px\n", a < b ? a : b);
  return 0;
}
