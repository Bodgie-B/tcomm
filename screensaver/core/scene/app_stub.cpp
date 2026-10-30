// TEMPORARY stub, replaced by scene/app.cpp in stage 1 task D.
//
// Implements the public interface in ss_app.h with no dependence on palette.cpp,
// gfx or anim, so the preview front ends link before the real scene exists.
// Renders five sky-like bands (colors from a local table keyed by (int)mood) and
// a 40x40 square that moves one pixel per tick. A doorbell turns the square red
// for 25 ticks. appTick returns true every 500 ticks (fake "new loop").
#include <new>
#include "ss_app.h"

namespace ss {

struct App {
  Inputs in;
  uint64_t seed;
  uint32_t tick;
  uint32_t bell_until;   // tick before which the square is drawn red
};

static_assert(sizeof(App) <= SS_APP_BYTES, "App exceeds SS_APP_BYTES");

namespace {
// [mood 0..5][band 0..4], top to horizon. Rough night/dawn/morning/day/golden/dusk.
const uint32_t kBands[6][5] = {
  {0x05070F, 0x0A1024, 0x111A38, 0x1A2548, 0x24305A},
  {0x2B3A66, 0x5A5F8C, 0x9A7A94, 0xD39A8A, 0xF2B887},
  {0x4A7FB5, 0x6C9CC8, 0x8DB6D8, 0xB2CDE0, 0xD6DDD8},
  {0x2F6FB8, 0x4B8AC9, 0x6BA3D6, 0x8FBCE0, 0xB4D2E6},
  {0x3C5A8C, 0x7B6F90, 0xC4806A, 0xE8A252, 0xF6C46A},
  {0x1E2750, 0x4A3A6A, 0x8A4A70, 0xC8605C, 0xE8885A},
};
}  // namespace

App* appStart(void* storage, const HostAlloc&, uint64_t seed, const Inputs& in) {
  App* a = new (storage) App();
  a->in = in;
  a->seed = seed;
  a->tick = 0;
  a->bell_until = 0;
  return a;
}

void appStop(App* app, const HostAlloc&) { app->~App(); }

void appSetInputs(App* app, const Inputs& in) { app->in = in; }

bool appPostEvent(App* app, Event e) {
  if (e.type == EV_DOORBELL) app->bell_until = app->tick + 25;
  return true;
}

bool appTick(App* app) {
  app->tick++;
  return app->tick % 500 == 0;
}

int appRender(App* app, Surface& fb) {
  int m = (int)app->in.mood;
  if (m < 0) m = 0;
  if (m > 5) m = 5;
  const int band_h = (fb.height + 4) / 5;
  for (int y = 0; y < fb.height; y++) {
    int b = y / band_h;
    if (b > 4) b = 4;
    const Pixel c = rgb565_hex(kBands[m][b]);
    Pixel* row = fb.row(fb.origin_y + y);
    for (int x = 0; x < fb.width; x++) row[x] = c;
  }
  const int span = fb.width - 40;
  const int sx = (int)(app->tick % (uint32_t)span);
  const int sy = (fb.height - 40) / 2;
  const Pixel sq = app->tick < app->bell_until ? rgb565_hex(0xFF2020) : rgb565_hex(0xFFFFFF);
  for (int y = sy; y < sy + 40; y++) {
    Pixel* row = fb.row(fb.origin_y + y);
    for (int x = sx; x < sx + 40; x++) row[x] = sq;
  }
  return SS_STRIP_COUNT;
}

void appInvalidate(App*) {}

const char* appBeatName(const App*) { return "stub"; }
float appBeatProgress(const App* app) { return (float)(app->tick % 500) / 500.0f; }
uint64_t appLoopSeed(const App* app) { return app->seed; }
uint32_t appTickCount(const App* app) { return app->tick; }

}  // namespace ss
