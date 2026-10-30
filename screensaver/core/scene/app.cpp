// Screensaver application: implements ss_app.h.
//
// Stage 2: each loop opens with the build-up beat, then one long ambient beat
// runs to the end of the 600 s loop. The World (world.h) holds everything
// dynamic; App owns it by value with the timeline, palette and renderer, and
// delegates drawing to it. Story beats replace the ambient beat in stage 4.
#include <math.h>
#include <new>

#include "../render/renderer.h"
#include "../ss_app.h"
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

constexpr float kLoopSeconds = 600.f;

// Ambient only: the ambient layer runs in worldTick on every tick regardless of
// the beat, so this beat has nothing to drive yet.
const Beat kAmbientBeat = {"ambient", kLoopSeconds - SS_BUILD_SECONDS, nullptr, nullptr, nullptr};

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
  a->timeline.reset();
  a->timeline.add(kBuildBeat);
  a->timeline.add(kAmbientBeat);
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

}  // namespace ss
