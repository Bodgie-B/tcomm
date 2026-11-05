// Tests for the spread palette step (appSetPaletteSpread, render/renderer.h spread_per_frame): the
// panel shows a small palette step a few strips a frame instead of redrawing the whole frame at once.
// Two apps from one seed: A spreads, B redraws every strip every frame. The mood creeps 1/25 of a
// step at a time, as the panel's clock moves it through a sky change, through all four families.
// Whenever A's palette has settled, its frame must equal B's; A must settle within a few frames of
// each step; and the frame of a step must not redraw every strip.
// Plain executable, no framework. Returns non-zero if any check fails.
#include "ss_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                   \
  do {                                                     \
    ++g_checks;                                            \
    if (!(cond)) {                                         \
      ++g_failures;                                        \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);          \
      printf(__VA_ARGS__);                                 \
      printf("\n");                                        \
    }                                                      \
  } while (0)

static void* hostAlloc(size_t n) { return calloc(1, n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

static Pixel g_fa[SS_WIDTH * SS_HEIGHT], g_fb[SS_WIDTH * SS_HEIGHT];

// One run: returns the number of spread steps seen.
static int run(uint64_t seed, int family, float mood0, int every, int ticks, int per_frame, float step = 0.04f,
               int medium = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood0;
  in.spine_force = (uint8_t)(1 + family);
  in.chip = true;
  void* ma = malloc(SS_APP_BYTES);
  void* mb = malloc(SS_APP_BYTES);
  App* a = appStart(ma, kAlloc, seed, in);
  App* b = appStart(mb, kAlloc, seed, in);
  CHECK(a && b, "appStart");
  if (!a || !b) return 0;
  appSetPaletteSpread(a, per_frame, medium);
  Surface sa{g_fa, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0}, sb{g_fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  int prev = appPaletteRebuilds(a), spreads = 0, last_spread = -1, longest = 0, mismatches = 0, whole = 0;
  for (int t = 0; t < ticks; ++t) {
    if (t > 0 && t % every == 0) {
      in.mood += step;
      if (in.mood >= 6.f) in.mood -= 6.f;
      appSetInputs(a, in);
      appSetInputs(b, in);
    }
    appTick(a);
    appTick(b);
    const int reb = appPaletteRebuilds(a);
    const bool spread = reb != prev && !appPaletteSettled(a);
    prev = reb;
    const int wa = appRender(a, sa);
    appInvalidate(b);
    appRender(b, sb);
    if (spread) {
      ++spreads;
      last_spread = t;
      if (wa == SS_STRIP_COUNT) ++whole;
    }
    if (!appPaletteSettled(a)) {   // still unsettled this long after the latest step
      if (last_spread >= 0 && t - last_spread > longest) longest = t - last_spread;
      continue;
    }
    if (memcmp(g_fa, g_fb, sizeof g_fa) != 0) {
      if (++mismatches <= 3) CHECK(false, "seed %llu family %d tick %d: settled frame differs from a full redraw", (unsigned long long)seed, family, t);
      memcpy(g_fa, g_fb, sizeof g_fa);
      appInvalidate(a);
    }
  }
  CHECK(mismatches == 0, "seed %llu family %d: %d settled frames differ", (unsigned long long)seed, family, mismatches);
  // SS_STRIP_COUNT bands at per_frame a frame: settled that many frames after the latest step, even
  // when steps come faster than that (the morning fog lifting).
  const int speed = step > 0.15f ? medium : per_frame;   // a medium step (the Lookout's) at its own speed
  const int frames = (SS_STRIP_COUNT + speed - 1) / speed;
  CHECK(longest < frames, "seed %llu family %d: still spreading %d frames after the latest step (%d a frame)", (unsigned long long)seed, family, longest, per_frame);
  CHECK(whole * 4 <= spreads, "seed %llu family %d: %d of %d spread steps redrew every strip at once", (unsigned long long)seed, family, whole, spreads);
  appStop(a, kAlloc);
  appStop(b, kAlloc);
  free(ma);
  free(mb);
  return spreads;
}

int main() {
  int spreads = 0;
  spreads += run(62, 0, 4.0f, 150, 6000, 3);   // evening, golden hour into dusk
  spreads += run(3, 1, 1.2f, 150, 6000, 1);    // morning: its fog lifting steps the palette too (the panel's 1 a frame)
  spreads += run(64, 2, 2.6f, 120, 6000, 1);   // midday
  spreads += run(5, 0, 5.6f, 150, 6000, 2);    // evening across dusk into night
  spreads += run(16, 3, 4.0f, 200, 6000, 1, 0.2f, 3);   // medium steps, as the Lookout's sunset (the panel's 3 a frame)
  spreads += run(62, 0, 4.0f, 250, 6000, 1, 0.24f, 5);  // and at 5 a frame
  CHECK(spreads >= 100, "only %d spread steps across the runs", spreads);
  // Off (the default): a palette step redraws every strip, as before.
  {
    Inputs in;
    memset(&in, 0, sizeof in);
    in.mood = 3.0f;
    in.spine_force = 3;
    void* m = malloc(SS_APP_BYTES);
    App* a = appStart(m, kAlloc, 64, in);
    Surface s{g_fa, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    for (int t = 0; t < 400; ++t) {
      appTick(a);
      appRender(a, s);
    }
    in.mood = 3.04f;
    appSetInputs(a, in);
    appTick(a);
    const int w = appRender(a, s);
    CHECK(w == SS_STRIP_COUNT && appPaletteSettled(a), "spread off: a mood step redrew %d strips", w);
    appStop(a, kAlloc);
    free(m);
  }
  printf("test_spread: %d checks, %d failures (%d spread steps)\n", g_checks, g_failures, spreads);
  return g_failures ? 1 : 0;
}
