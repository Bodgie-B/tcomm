// Screensaver application: implements ss_app.h.
//
// Stage 1 scene: a static layered landscape in the layer cache and two clouds
// drifting over it. The timeline holds one placeholder beat; story beats come
// in later stages.
#include <math.h>
#include <new>

#include "../render/renderer.h"
#include "../ss_app.h"
#include "landscape.h"

namespace ss {

struct App;

struct SceneCtx {
  App* app;
};

struct App {
  Inputs in;
  Rng rng;
  uint64_t loop_seed;
  Timeline timeline;
  SceneCtx ctx;
  SceneDraw draw;
  Landscape land;
  Cloud clouds[2];

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

void beatUpdate(SceneCtx&, float, float) {}   // placeholder, does nothing

const Beat kLandscapeBeat = {"landscape", 600.f, nullptr, beatUpdate, nullptr};

void drawStatic(void* user, Surface& frame, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  landscapeDrawStatic(a->land, frame, pal, a->pal_mood);
}

void drawDynamic(void* user, Surface& strip, const Rect& clip, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  for (int i = 0; i < 2; ++i) cloudDraw(a->clouds[i], strip, pal, clip);
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
  landscapeGenerate(a->land, a->rng);
  cloudGenerate(a->clouds[0], a->rng, a->rng.range(0.f, 800.f), 96.f + a->rng.range(-4.f, 4.f), 6.f);
  cloudGenerate(a->clouds[1], a->rng, a->rng.range(0.f, 800.f), 166.f + a->rng.range(-4.f, 4.f), 4.f);
  a->timeline.reset();
  a->timeline.add(kLandscapeBeat);
  a->ctx.app = a;
  a->timeline.start(a->ctx);
}

bool onScreen(const Rect& r) { return r.x1 > 0 && r.x0 < SS_WIDTH && !r.empty(); }

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

  // Clouds: dirty the strips under the old and the new bounds.
  for (int i = 0; i < 2; ++i) {
    const Rect before = cloudBounds(a->clouds[i]);
    cloudStep(a->clouds[i]);
    const Rect after = cloudBounds(a->clouds[i]);
    if (onScreen(before)) a->renderer.markDirtyRect(before);
    if (onScreen(after)) a->renderer.markDirtyRect(after);
  }

  // Palette follows the inputs only when they moved enough to matter.
  const WeatherTint& w = a->in.weather;
  if (moodDistance(a->in.mood, a->pal_mood) > kMoodEps ||
      fabsf(w.overcast - a->pal_weather.overcast) > kWeatherEps ||
      fabsf(w.fog - a->pal_weather.fog) > kWeatherEps ||
      fabsf(w.night_dim - a->pal_weather.night_dim) > kWeatherEps ||
      a->in.night_mode != a->pal_night) {
    computeAppPalette(a);
  }

  if (!a->timeline.tick(a->ctx)) {
    // Loop over: new seed from the running RNG, new place, same inputs.
    a->loop_seed = (uint64_t)a->rng.next();
    beginLoop(a);
    computeAppPalette(a);   // rebuilds the cache for the new geometry
    return true;
  }
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

}  // namespace ss
