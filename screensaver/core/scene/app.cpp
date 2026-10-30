// Screensaver application: implements ss_app.h.
//
// Each loop opens with the build-up beat, then the story spine (spine.h):
// arrival, setup, firewood, fire, dinner, food bag, bear and close, with pauses
// and calm fillers between. The loop ends when the close does (8 to 11 min). The World (world.h) holds
// everything dynamic; App owns it by value with the timeline, palette and
// renderer, and delegates drawing to it.
#include <math.h>
#include <new>

#include "../render/renderer.h"
#include "../ss_app.h"
#include "director.h"
#include "world.h"

namespace ss {

struct App {
  Inputs in;
  Rng rng;
  uint64_t loop_seed;
  Timeline timeline;
  SceneCtx ctx;
  SceneDraw draw;
  World world;

  // Inputs the current palette was computed from.
  Palette pal;
  float pal_mood;
  WeatherTint pal_weather;
  bool pal_night;

  uint32_t events_seen;   // drained events; ignored until later stages

  Renderer renderer;      // last: it carries the 51 KB strip buffer
};

static_assert(sizeof(App) <= SS_APP_BYTES, "App exceeds SS_APP_BYTES");

namespace {

constexpr float kMoodEps = 1.0f / 48.0f;
constexpr float kWeatherEps = 0.02f;

void drawStatic(void* user, Surface& frame, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  worldDrawStatic(a->world, frame, pal, a->pal_mood);
}

void drawDynamic(void* user, Surface& strip, const Rect& clip, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  worldDrawDynamic(a->world, strip, clip, pal, a->pal_mood);
}

float moodDistance(float a, float b) {
  float d = fabsf(a - b);
  d -= (float)SS_MOOD_COUNT * floorf(d / (float)SS_MOOD_COUNT);   // wrap into [0, 6)
  return d > 3.f ? (float)SS_MOOD_COUNT - d : d;
}

void computeAppPalette(App* a) {
  WeatherTint w = a->in.weather;
  if (a->in.night_mode && w.night_dim < 1.f) w.night_dim = 1.f;   // night mode alone must dim
  computePalette(a->in.mood, w, a->pal);
  a->pal_mood = a->in.mood;
  a->pal_weather = a->in.weather;
  a->pal_night = a->in.night_mode;
  a->renderer.setPalette(a->pal);   // forces a cache rebuild and marks every strip
}

// Generate a loop's geometry from the loop seed and restart the timeline.
void beginLoop(App* a) {
  a->rng.seed(a->loop_seed);
  worldBeginLoop(a->world, a->rng);
  spinePlan(a->world.plan, a->rng, a->in);
  a->timeline.reset();
  spineBuildTimeline(a->timeline, a->world.plan);
  a->ctx = SceneCtx{a, &a->world, &a->rng, &a->in, &a->renderer, SS_TICK_DT};
  a->timeline.start(a->ctx);
}

}  // namespace

App* appStart(void* storage, const HostAlloc& alloc, uint64_t seed, const Inputs& in) {
  App* a = new (storage) App();
  if (!a->renderer.init(alloc)) {
    a->~App();
    return nullptr;
  }
  a->in = in;
  a->loop_seed = seed;
  a->draw.user = a;
  a->draw.draw_static = drawStatic;
  a->draw.draw_dynamic = drawDynamic;
  beginLoop(a);
  computeAppPalette(a);
  return a;
}

void appStop(App* app, const HostAlloc& alloc) {
  app->renderer.shutdown(alloc);
  app->~App();
}

void appSetInputs(App* app, const Inputs& in) { app->in = in; }

bool appPostEvent(App* app, Event e) { return app->timeline.post(e); }

bool appTick(App* a) {
  Event ev[SS_MAX_PENDING_EVENTS];
  a->events_seen += (uint32_t)a->timeline.takeEvents(ev, SS_MAX_PENDING_EVENTS);

  // Palette follows the inputs only when they moved enough to matter.
  const WeatherTint& w = a->in.weather;
  if (moodDistance(a->in.mood, a->pal_mood) > kMoodEps ||
      fabsf(w.overcast - a->pal_weather.overcast) > kWeatherEps ||
      fabsf(w.fog - a->pal_weather.fog) > kWeatherEps ||
      fabsf(w.night_dim - a->pal_weather.night_dim) > kWeatherEps ||
      a->in.night_mode != a->pal_night) {
    computeAppPalette(a);
  }

  // Beats drive the world first, then the world steps its ambient layer and
  // marks what changed.
  if (!a->timeline.tick(a->ctx)) {
    // Loop over: new seed from the running RNG, new place, same inputs.
    a->loop_seed = (uint64_t)a->rng.next();
    beginLoop(a);
    computeAppPalette(a);   // rebuilds the cache for the new geometry
    return true;
  }
  worldTick(a->world, a->rng, a->renderer);
  return false;
}

int appRender(App* app, Surface& fb) { return app->renderer.render(fb, app->draw); }

void appInvalidate(App* app) { app->renderer.markAllDirty(); }

const char* appBeatName(const App* app) {
  const Beat* b = app->timeline.currentBeat();
  return b ? b->name : "";
}

float appBeatProgress(const App* app) { return app->timeline.progress(); }
uint64_t appLoopSeed(const App* app) { return app->loop_seed; }
uint32_t appTickCount(const App* app) { return app->timeline.tick_count; }
int appBuildPhase(const App* app) { return app->world.build.phase; }
uint32_t appAppBytes() { return (uint32_t)sizeof(App); }

int appLoopPlan(const App* app, char* buf, size_t n) { return spineFormatPlan(app->world.plan, buf, n); }

bool appHikerRect(const App* app, Rect* out) {
  const World& w = app->world;
  if (out) *out = w.hiker_rect;
  return w.hiker.visible;
}

bool appHikerBounds(const App* app, Rect* out) {
  const World& w = app->world;
  if (out) *out = w.hiker_full;
  return w.hiker.visible;
}

bool appHikerWalking(const App* app) { return app->world.hiker.visible && app->world.hiker.walking; }

int appCritterCount(const App* app) { return directorCount(app->world); }

bool appCritterRect(const App* app, int slot, Rect* out, int* kind) {
  if (slot < 0 || slot >= CRITTER_MAX) return false;
  const World& w = app->world;
  if (!w.crit[slot].in_use) return false;
  if (out) *out = critterBounds(w.critters[slot]);
  if (kind) *kind = w.critters[slot].kind;
  return w.critters[slot].visible;
}

bool appMemoryShoeTaken(const App* app) { return app->world.memory.shoe_taken; }
uint32_t appMemoryLoopCount(const App* app) { return app->world.memory.loop_count; }

int appSpineOverruns(const App* app) { return app->world.spine.overruns; }
uint32_t appLoopTicks(const App* app) { return spineLoopTicks(app->world.plan); }
float appFade(const App* app) { return app->world.fade; }

}  // namespace ss
