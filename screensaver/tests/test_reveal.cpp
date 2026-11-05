// Tests for the touch reveal (core/scene/reveal.h) and its app entry points.
// Plain executable, no framework. Returns non-zero if any check fails.
#include "scene/reveal.h"
#include "ss_app.h"

#include <math.h>
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

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};   // no fast block lent: core takes one

constexpr Pixel kScene = 0x1234;   // the frozen scene
constexpr Pixel kRing = 0xFFE0;

static Pixel mainAt(int x, int y) { return (Pixel)((x * 7 + y * 13) | 0x0821); }   // never kScene or kRing

// Every pixel is the scene, the main screen, the ring or a blend of scene and ring;
// pixels whose centre is inside r are main; pixels past the ring's outer edge are scene.
static void checkStep(const RevealState& rv, const Pixel* fb, float r, const char* what) {
  int bad_inside = 0, bad_outside = 0, bad_band = 0;
  for (int y = 0; y < SS_HEIGHT; ++y) {
    for (int x = 0; x < SS_WIDTH; ++x) {
      const float dx = (float)x + 0.5f - rv.cx, dy = (float)y + 0.5f - rv.cy;
      const float d = sqrtf(dx * dx + dy * dy);
      const Pixel p = fb[y * SS_WIDTH + x];
      if (d < r - 0.01f) {
        if (p != mainAt(x, y)) ++bad_inside;
      } else if (d > r + kRevealRing + 1.01f) {
        if (p != kScene) ++bad_outside;
      } else if (p != kScene && p != kRing && p != mainAt(x, y)) {
        // A blend of scene and ring on the ring's outer edge: between the two in green.
        const int g = (p >> 5) & 63, gs = (kScene >> 5) & 63, gr = (kRing >> 5) & 63;
        if (g < (gs < gr ? gs : gr) || g > (gs > gr ? gs : gr)) ++bad_band;
      }
    }
  }
  CHECK(bad_inside == 0, "%s: %d pixels inside r=%.1f aren't the main screen", what, bad_inside, (double)r);
  CHECK(bad_outside == 0, "%s: %d pixels past the ring aren't the scene", what, bad_outside);
  CHECK(bad_band == 0, "%s: %d ring pixels aren't scene, ring or a blend", what, bad_band);
}

static void testReveal(int tx, int ty, float dt) {
  static Pixel fb[SS_WIDTH * SS_HEIGHT];
  static Pixel main_screen[SS_WIDTH * SS_HEIGHT];
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) {
      fb[y * SS_WIDTH + x] = kScene;
      main_screen[y * SS_WIDTH + x] = mainAt(x, y);
    }
  Surface s{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  RevealState rv;
  revealBegin(rv, tx, ty);
  char what[64];
  int steps = 0;
  bool done = false;
  float t = 0.0f;
  while (!done && steps < 1000) {
    done = revealStep(rv, s, main_screen, kRing, dt);
    t += dt;
    ++steps;
    if (!done) {
      const float k = 1.0f - t / kRevealSeconds;
      snprintf(what, sizeof what, "touch (%d,%d) step %d", tx, ty, steps);
      checkStep(rv, fb, rv.r_max * (1.0f - k * k * k), what);
    }
  }
  CHECK(done, "touch (%d,%d): never finished", tx, ty);
  CHECK(fabsf(t - kRevealSeconds) < dt + 1e-4f, "touch (%d,%d): took %.3f s", tx, ty, (double)t);
  CHECK(memcmp(fb, main_screen, sizeof fb) == 0, "touch (%d,%d): the last frame isn't exactly the main screen", tx, ty);
  CHECK(revealStep(rv, s, main_screen, kRing, dt), "touch (%d,%d): a step after the end isn't done", tx, ty);
}

static void testApp() {
  void* mem = malloc(SS_APP_BYTES);
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.0f;
  App* app = appStart(mem, kAlloc, 5, in);
  CHECK(app != nullptr, "appStart failed");
  if (!app) return;
  static Pixel fb[SS_WIDTH * SS_HEIGHT];
  Surface s{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  for (int i = 0; i < 2000; ++i) appTick(app);
  appRender(app, s);
  const uint32_t ticks = appTickCount(app);
  CHECK(!appRevealStep(app, s, 0.04f), "a step before appRevealBegin did something");

  Pixel* buf = appRevealBegin(app, 700, 400);
  CHECK(buf != nullptr, "appRevealBegin gave no buffer");
  if (!buf) return;
  for (int i = 0; i < SS_WIDTH * SS_HEIGHT; ++i) buf[i] = mainAt(i % SS_WIDTH, i / SS_WIDTH);
  CHECK(appRevealBegin(app, 10, 10) == buf, "a second appRevealBegin changed the buffer");
  CHECK(!appTick(app) && appTickCount(app) == ticks, "the scene moved under the reveal");
  CHECK(appRender(app, s) == 0, "appRender drew under the reveal");
  int steps = 0;
  while (!appRevealStep(app, s, 0.04f) && steps < 100) ++steps;
  CHECK(steps == 8, "the reveal took %d steps of 40 ms, expected 8 (then done on the 9th)", steps);
  bool same = true;
  for (int i = 0; i < SS_WIDTH * SS_HEIGHT && same; ++i) same = fb[i] == mainAt(i % SS_WIDTH, i / SS_WIDTH);
  CHECK(same, "the app's reveal didn't end on the main screen");
  appStop(app, kAlloc);
  free(mem);
}

int main() {
  testReveal(400, 240, 0.04f);   // centre, 25 fps
  testReveal(0, 0, 0.04f);       // corners
  testReveal(799, 479, 0.04f);
  testReveal(5, 470, 0.016f);    // 60 fps
  testReveal(-50, 900, 0.04f);   // off-screen touch is clamped
  testReveal(300, 100, 1.0f);    // one long step finishes at once
  testApp();
  printf("test_reveal: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
