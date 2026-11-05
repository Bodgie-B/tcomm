// Screensaver application: implements ss_app.h.
//
// Each loop opens with the build-up beat, then the story spine (spine.h):
// arrival, setup, firewood, fire, dinner, food bag, bear and close, with pauses
// and calm fillers between. The loop ends when the close does (8 to 11 min). The World (world.h) holds
// everything dynamic; App owns it by value with the timeline, palette and
// renderer, and delegates drawing to it.
#include <math.h>
#include <string.h>
#include <new>

#include "../gfx/raster.h"
#include "../render/renderer.h"
#include "../ss_app.h"
#include "chip.h"
#include "director.h"
#include "react.h"
#include "reveal.h"
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
  float pal_flash;        // the lightning's flash step it was computed with (weather.h)
  bool pal_night;

  uint32_t events_seen;   // drained events; ignored until later stages
  uint16_t pal_rebuilds;  // computeAppPalette calls this loop (appPaletteRebuilds)

  // SS_FAST_BYTES: the strip buffer, then the rasterizer's scratch. Lent by the
  // host (HostAlloc::fast) or, when it lends none, taken from alloc_large.
  void* fast;
  bool fast_owned;

  RevealState reveal;     // the touch reveal; while active the scene is frozen
  ChipState chip;         // the other floor's status, the dog badges, the doorbell pulse

  Renderer renderer;
};

static_assert(sizeof(App) <= SS_APP_BYTES, "App exceeds SS_APP_BYTES");

namespace {

constexpr float kMoodEps = 1.0f / 48.0f;
constexpr float kWeatherEps = 0.02f;
// Steps this small may be spread (computeAppPalette): the panel's clock moves the mood 1/25 of a
// step a minute through a sky change; a forced phase or a new reading is a jump, shown at once.
constexpr float kSpreadMood = 0.15f;
constexpr float kSpreadTint = 0.12f;

void drawStatic(void* user, Surface& frame, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  worldDrawStatic(a->world, frame, pal, a->pal_mood);
}

void drawDynamic(void* user, Surface& strip, const Rect& clip, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  worldDrawDynamic(a->world, strip, clip, pal, a->pal_mood);
  if (a->in.chip) chipDraw(a->chip, strip, clip, pal);   // over everything, the loop's fade included
}

float moodDistance(float a, float b) {
  float d = fabsf(a - b);
  d -= (float)SS_MOOD_COUNT * floorf(d / (float)SS_MOOD_COUNT);   // wrap into [0, 6)
  return d > 3.f ? (float)SS_MOOD_COUNT - d : d;
}

// The weather the palette is computed from: the inputs', plus the fog the
// scene adds (the morning opens in fog, World::fog_extra), plus the loop's
// weather (weather.h: overcast, fog, the lightning's flash, in steps).
// Untouched when they add none, so a clear loop is bit for bit what it was.
WeatherTint sceneWeather(const App* a, float* flash) {
  WeatherTint w = a->in.weather;
  if (a->world.fog_extra > 0.f) {
    const float f = w.fog + a->world.fog_extra;
    w.fog = f > 1.f ? 1.f : f;
  }
  *flash = weatherTint(a->world, w);
  return w;
}

// The palette's mood: the scene's own when it sets one (the Lookout's sunset
// steps World::mood_override), else the host's.
float sceneMood(const App* a) { return a->world.mood_override >= 0.f ? a->world.mood_override : a->in.mood; }

// `force`: the loop's geometry changed (loop start), so the cache is rebuilt
// even when the palette came out the same; otherwise a palette byte for byte
// the one in use costs nothing (an input step that lands on the same 565
// colours). `spread`: a small step (mood or weather tint) the renderer may
// show a few strips a frame (Renderer::spread_per_frame; off unless the host
// turns it on with appSetPaletteSpread).
void computeAppPalette(App* a, bool force, bool spread = false) {
  float flash = 0.f;
  WeatherTint w = sceneWeather(a, &flash);
  a->pal_weather = w;
  a->pal_flash = flash;
  if (a->in.night_mode && w.night_dim < 1.f) w.night_dim = 1.f;   // night mode alone must dim
  Palette p;
  const float mood = sceneMood(a);
  computePalette(mood, w, flash, p);
  if (a->world.plan.family == SF_LOOKOUT) lookoutPaletteTweak(a->world, mood, w, flash, p);
  a->pal_mood = mood;
  a->pal_night = a->in.night_mode;
  if (!force && memcmp(&p, &a->pal, sizeof p) == 0) return;
  a->pal = p;
  a->renderer.setPalette(a->pal, spread && !force);   // a cache rebuild; every strip now, or spread
  if (a->pal_rebuilds < 0xFFFF) ++a->pal_rebuilds;
}

// Generate a loop's geometry from the loop seed and restart the timeline.
void beginLoop(App* a) {
  a->rng.seed(a->loop_seed);
  worldBeginLoop(a->world, a->rng);
  // The family is a function of the inputs alone: it costs the loop rng nothing.
  a->world.plan.family = spineFamilyFor(a->in);
  spinePlan(a->world.plan, a->rng, a->in, a->world.memory);
  memoryBeginLoop(a->world, a->rng);
  spineBeginLoop(a->world, a->rng);
  // The weather: its own rng stream from the loop seed (the loop rng never sees it).
  weatherPlan(a->world, a->loop_seed, a->in, spineLoopTicks(a->world.plan));
  a->pal_rebuilds = 0;
  a->timeline.reset();
  spineBuildTimeline(a->timeline, a->world.plan);
  a->ctx = SceneCtx{a, &a->world, &a->rng, &a->in, &a->renderer, SS_TICK_DT};
  a->timeline.start(a->ctx);
}

}  // namespace

App* appStart(void* storage, const HostAlloc& alloc, uint64_t seed, const Inputs& in) {
  App* a = new (storage) App();
  a->fast = alloc.fast;
  a->fast_owned = false;
  if (a->fast == nullptr && alloc.alloc_large != nullptr) {
    a->fast = alloc.alloc_large(SS_FAST_BYTES);
    a->fast_owned = a->fast != nullptr;
  }
  if (a->fast == nullptr || !a->renderer.init(alloc, static_cast<Pixel*>(a->fast))) {
    if (a->fast_owned && alloc.free_large != nullptr) alloc.free_large(a->fast);
    a->~App();
    return nullptr;
  }
  chipInit(a->chip);
  a->in = in;
  a->loop_seed = seed;
  a->draw.user = a;
  a->draw.draw_static = drawStatic;
  a->draw.draw_dynamic = drawDynamic;
  beginLoop(a);
  computeAppPalette(a, true);
  // The chip's first update sets its look with no fade, so the first frame shows the real status.
  if (in.chip) chipUpdate(a->chip, in.other_status, in.dog_out, in.night_mode, 0.f);
  return a;
}

void appStop(App* app, const HostAlloc& alloc) {
  app->renderer.shutdown(alloc);
  if (app->fast_owned && alloc.free_large != nullptr) alloc.free_large(app->fast);
  app->~App();
}

void appSetInputs(App* app, const Inputs& in) {
  if (in.chip != app->in.chip) app->renderer.markDirtyRect(chipBounds(app->chip));   // shown or hidden
  app->in = in;
}

bool appPostEvent(App* app, Event e) { return app->timeline.post(e); }

bool appTick(App* a) {
  if (a->reveal.active) return false;   // frozen under the reveal
  Event ev[SS_MAX_PENDING_EVENTS];
  const int n_ev = a->timeline.takeEvents(ev, SS_MAX_PENDING_EVENTS);
  a->events_seen += (uint32_t)n_ev;
  for (int i = 0; i < n_ev; ++i) {
    if (ev[i].type != EV_DOORBELL) continue;
    if (a->in.chip) chipDoorbell(a->chip);
    if (reactCanStart(a->world)) reactStart(a->world);
  }
  if (a->in.chip) {
    const Rect d = chipUpdate(a->chip, a->in.other_status, a->in.dog_out, a->in.night_mode, SS_TICK_DT);
    if (!d.empty()) a->renderer.markDirtyRect(d);
  }

  // Palette follows the inputs only when they moved enough to matter.
  float flash = 0.f;
  const WeatherTint w = sceneWeather(a, &flash);
  if (moodDistance(sceneMood(a), a->pal_mood) > kMoodEps ||
      fabsf(w.overcast - a->pal_weather.overcast) > kWeatherEps ||
      fabsf(w.fog - a->pal_weather.fog) > kWeatherEps ||
      fabsf(w.night_dim - a->pal_weather.night_dim) > kWeatherEps ||
      w.night_gain != a->pal_weather.night_gain ||   // the host's setting: rare, every change counts
      flash != a->pal_flash ||   // the flash moves in a few fixed steps: every step counts
      a->in.night_mode != a->pal_night) {
    // A small step may be spread over a few frames; lightning, the night rule switching on or off
    // and a new night gain change the whole frame at once.
    const bool small = flash == a->pal_flash && a->in.night_mode == a->pal_night &&
                       w.night_gain == a->pal_weather.night_gain &&
                       moodDistance(sceneMood(a), a->pal_mood) <= kSpreadMood &&
                       fabsf(w.overcast - a->pal_weather.overcast) <= kSpreadTint &&
                       fabsf(w.fog - a->pal_weather.fog) <= kSpreadTint &&
                       fabsf(w.night_dim - a->pal_weather.night_dim) <= kSpreadTint;
    computeAppPalette(a, false, small);
  }

  // The doorbell reaction holds the story still (react.h): no beat or minor
  // advances, only the world's ambient layer and his reaction.
  if (a->world.react.active) {
    worldTick(a->world, a->rng, a->renderer);
    return false;
  }

  // Beats drive the world first, then the world steps its ambient layer and
  // marks what changed.
  if (!a->timeline.tick(a->ctx)) {
    // Loop over: what it leaves for the next (memory), new seed from the
    // running RNG, new place, same inputs.
    memoryEndLoop(a->world);
    a->loop_seed = (uint64_t)a->rng.next();
    beginLoop(a);
    computeAppPalette(a, true);   // rebuilds the cache for the new geometry
    return true;
  }
  minorsSync(a->world, a->timeline, a->in);
  worldTick(a->world, a->rng, a->renderer);
  return false;
}

int appRender(App* app, Surface& fb) {
  if (app->reveal.active) return 0;   // the cache holds the main screen now
  // All of the app's drawing happens in here, with its own scratch. Its
  // coverage row is zeroed every frame: a lent block can take a stray write
  // from its owner (on the panel, LVGL's buffer), and that would streak every
  // shape after it.
  void* scratch = static_cast<uint8_t*>(app->fast) + SS_STRIP_BYTES;
  rasterResetScratch(scratch);
  void* prev = rasterSetScratch(scratch);
  const int n = app->renderer.render(fb, app->draw);
  rasterSetScratch(prev);
  return n;
}

Pixel* appRevealBegin(App* app, int x, int y) {
  if (!app->reveal.active) revealBegin(app->reveal, x, y);
  return app->renderer.cache;
}

void appSetClock(App* app, uint32_t (*us)()) { app->renderer.clock_us = us; }

bool appPaletteSettled(const App* app) { return !app->renderer.spread_next && app->renderer.spread_mask == 0; }

void appSetPaletteSpread(App* app, int strips_per_frame) {
  app->renderer.spread_per_frame = (uint8_t)(strips_per_frame < 0 ? 0 : (strips_per_frame > SS_STRIP_COUNT ? SS_STRIP_COUNT : strips_per_frame));
}

RenderProfile appProfile(App* app) {
  const RenderProfile p = app->renderer.prof;
  memset(&app->renderer.prof, 0, sizeof app->renderer.prof);
  return p;
}

bool appRevealStep(App* app, Surface& fb, float dt) {
  if (!app->reveal.active) return false;
  if (fb.width < SS_WIDTH || fb.height < SS_HEIGHT || fb.origin_y != 0 || fb.stride < SS_WIDTH) return false;
  return revealStep(app->reveal, fb, app->renderer.cache, app->pal[CLOUD_LIT], dt);
}

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
int appMemory(const App* app, char* buf, size_t n) { return memoryFormat(app->world.memory, buf, n); }
bool appBusy(const App* app) { return app->world.busy; }

int appSpineOverruns(const App* app) { return app->world.spine.overruns; }
uint32_t appLoopTicks(const App* app) { return spineLoopTicks(app->world.plan); }
float appFade(const App* app) { return app->world.fade; }
int appPaletteRebuilds(const App* app) { return app->pal_rebuilds; }
int appSpineFamily(const App* app) { return app->world.plan.family; }

const char* appMinorName(const App* app) { return minorsName(app->world); }
int appMinorCount(const App* app) { return app->world.minors.fired; }
int appMinorCritters(const App* app) { return minorsCritters(app->world); }
int appMinorForced(const App* app) { return minorsForced(app->world); }
int appLeaves(const App* app) {
  int n = 0;
  for (const Bit& b : app->world.bits) n += b.kind == BIT_LEAF ? 1 : 0;
  return n;
}
const World* appWorld(const App* app) { return &app->world; }
const Palette* appPalette(const App* app) { return &app->pal; }

}  // namespace ss
