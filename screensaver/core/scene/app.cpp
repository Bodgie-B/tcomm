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
#include "day_herd.h"
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
  uint8_t pal_moonlight;  // the full moon's light on the stream it was computed with, 0..kMoonlightSteps
  uint8_t pal_warm;       // fire and ice's warm step it was computed with (fireice.h)
  bool passer_prev;       // Inputs::passer_by on the last tick (to see a new arrival)
  uint8_t passer_wait;    // ticks left to try the wave for this arrival, 0 once he has waved or given up

  // The sky as the cache has it: the real sun and moon in whole pixels, the cloud cover
  // (landscape.h). appTick moves it on and repaints only the rows that changed.
  SkyDraw sky;

  uint32_t events_seen;   // drained events; ignored until later stages
  uint16_t pal_rebuilds;  // computeAppPalette calls this loop (appPaletteRebuilds)
  uint8_t spread_small;   // bands a frame for a small palette step, and a medium one (appSetPaletteSpread; 0 at once)
  uint8_t spread_medium;

  // SS_FAST_BYTES: the strip buffer, then the rasterizer's scratch. Lent by the
  // host (HostAlloc::fast) or, when it lends none, taken from alloc_large.
  void* fast;
  bool fast_owned;

  RevealState reveal;     // the touch reveal; while active the scene is frozen
  ChipState chip;         // the other floor's status, the dog badges, the request arrows, the doorbell pulse

  // The host's overlay (appSetOverlay): drawn last into every composited strip.
  void (*overlay)(void* user, Surface& strip, const Rect& clip);
  void* overlay_user;

  Renderer renderer;
};

static_assert(sizeof(App) <= SS_APP_BYTES, "App exceeds SS_APP_BYTES");

namespace {

constexpr float kMoodEps = 1.0f / 48.0f;
constexpr float kWeatherEps = 0.02f;
// Steps this small may be spread (computeAppPalette, at the host's speeds, appSetPaletteSpread):
// small, the panel's clock moving the mood 1/25 of a step a minute through a sky change; medium,
// the Lookout's sunset (4.0 to 5.9 in 8 to 10 steps). A forced phase or a new reading is a jump,
// shown at once.
constexpr float kSpreadMood = 0.15f;
constexpr float kSpreadTint = 0.12f;
constexpr float kSpreadMoodMedium = 0.35f;
constexpr float kSpreadTintMedium = 0.25f;

void drawStatic(void* user, Surface& frame, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  worldDrawStatic(a->world, frame, pal, a->pal_mood, &a->sky);
}

void drawDynamic(void* user, Surface& strip, const Rect& clip, const Palette& pal) {
  const App* a = static_cast<const App*>(user);
  worldDrawDynamic(a->world, strip, clip, pal, a->pal_mood, &a->sky);
  if (a->in.chip) chipDraw(a->chip, strip, clip, pal);   // over everything, the loop's fade included
  if (a->overlay) a->overlay(a->overlay_user, strip, clip);   // the host's badge, on top of that
}

// The scene's night for the house dogs' choices: the palette's mood near midnight, or the night rule.
bool dogsNight(const App* a) {
  float m = a->world.mood_override >= 0.f ? a->world.mood_override : a->in.mood;
  m -= 6.f * floorf(m / 6.f);
  return a->in.night_mode || m < 0.75f || m > 5.25f;
}

// The request arrows the chip shows (Inputs::req, oldest first, packed: zeros dropped).
void chipReqs(App* a) {
  uint8_t r[3];
  int n = 0;
  for (uint8_t v : a->in.req)
    if (v) r[n++] = v;
  chipRequests(a->chip, r, n);
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

// The sky the scene should show now: the host's sun and moon, and the cloud cover (the palette's
// overcast, or the weather's own when that is more). The Lookout draws its own sky: none here.
SkyDraw skyNow(const App* a, float overcast) {
  if (spineSetPiece(a->world.plan.family)) return SkyDraw{};
  const float wx = weatherCloudCover(a->world);
  return landscapeSky(a->in.sky, sceneMood(a), wx > overcast ? wx : overcast);
}

// A full moon high in a night sky puts a little more light on the stream: the water a few steps
// toward its own highlight colour (so the night caps still hold). Nothing by day, under cloud
// (the overcast palette dims the moon itself) or in the Lookout.
constexpr int kMoonlightSteps = 4;
constexpr float kMoonlightMix = 0.30f;   // at the most: full, high and full night
int moonlightNow(const App* a) {
  const SkyInputs& s = a->in.sky;
  if (!s.have || spineSetPiece(a->world.plan.family)) return 0;
  const float m = sceneMood(a) - 6.f * floorf(sceneMood(a) / 6.f);
  const float night = 1.f - 2.f * (m < 3.f ? m : 6.f - m);
  const float ph = s.moon_phase - floorf(s.moon_phase);
  const float full = (0.5f * (1.f - cosf(6.2831853f * ph)) - 0.8f) / 0.2f;   // 0 at 80% lit, 1 at full
  const float high = (s.moon_alt - 5.f) / 15.f;
  if (!(night > 0.f && full > 0.f && high > 0.f)) return 0;
  const float k = (night > 1.f ? 1.f : night) * (full > 1.f ? 1.f : full) * (high > 1.f ? 1.f : high);
  return (int)(k * (float)kMoonlightSteps + 0.5f);
}

// `force`: the loop's geometry changed (loop start), so the cache is rebuilt
// even when the palette came out the same; otherwise a palette byte for byte
// the one in use costs nothing (an input step that lands on the same 565
// colours). `spread_bands`: a step (mood or weather tint) the renderer may
// show that many bands a frame (Renderer::spread_per_frame; 0 unless the host
// turns it on with appSetPaletteSpread).
void computeAppPalette(App* a, bool force, int spread_bands = 0) {
  float flash = 0.f;
  WeatherTint w = sceneWeather(a, &flash);
  a->pal_weather = w;
  a->pal_flash = flash;
  if (a->in.night_mode && w.night_dim < 1.f) w.night_dim = 1.f;   // night mode alone must dim
  Palette p;
  const float mood = sceneMood(a);
  computePalette(mood, w, flash, p);
  if (a->world.plan.family == SF_LOOKOUT) lookoutPaletteTweak(a->world, mood, w, flash, p);
  if (a->world.plan.family == SF_SUNRISE) sunrisePaletteTweak(a->world, mood, w, p);
  const int ml = moonlightNow(a);
  if (ml > 0) p.c[WATER] = mixRGB(p.c[WATER], p.c[WATER_LIGHT], kMoonlightMix * (float)ml / (float)kMoonlightSteps);
  a->pal_moonlight = (uint8_t)ml;
  const int warm = fireIceWarmStep(a->world);   // urgent heat: the light a little warmer
  if (warm > 0) fireIcePaletteTweak(warm, p, a->in.night_mode);
  a->pal_warm = (uint8_t)warm;
  a->pal_mood = mood;
  a->pal_night = a->in.night_mode;
  if (!force && memcmp(&p, &a->pal, sizeof p) == 0) return;
  a->pal = p;
  a->renderer.setPalette(a->pal, force ? 0 : spread_bands);   // a cache rebuild; every strip now, or spread
  if (a->pal_rebuilds < 0xFFFF) ++a->pal_rebuilds;
}

// Generate a loop's geometry from the loop seed and restart the timeline.
void beginLoop(App* a) {
  a->rng.seed(a->loop_seed);
  worldBeginLoop(a->world, a->rng);
  // Lucy and Copper: their own streams from the loop seed (the loop rng never sees them).
  houseDogsBeginLoop(a->world, a->loop_seed, a->in.dog_out, dogsNight(a));
  // The mail bird: its own stream too; simply there if the mail is waiting already.
  mailBeginLoop(a->world, a->loop_seed, a->in.mail);
  // The family is a function of the inputs alone: it costs the loop rng nothing.
  a->world.plan.family = spineFamilyFor(a->in);
  spinePlan(a->world.plan, a->rng, a->in, a->world.memory);
  memoryBeginLoop(a->world, a->rng);
  spineBeginLoop(a->world, a->rng);
  if (a->world.plan.family == SF_SUNRISE) sunriseInputs(a->world, a->in);   // a gray morning, the real sun's bearing, the dogs
  // The weather: its own rng stream from the loop seed (the loop rng never sees it).
  weatherPlan(a->world, a->loop_seed, a->in, spineLoopTicks(a->world.plan));
  // The visiting couple: whether they may come this loop (their own stream too).
  visitorsBeginLoop(a->world, a->loop_seed, a->in);
  {   // the new loop's sky, before its cache is built (every caller rebuilds it next)
    float flash = 0.f;
    a->sky = skyNow(a, sceneWeather(a, &flash).overcast);
    a->world.sky_cover = a->sky.cover;   // the world's high clouds (cover 2-3) follow the cache's cover
  }
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
  // What the last run left (Inputs::carry; Mike, 2026-11-18): the new App's memory starts at zero, so
  // without this the mushrooms could never ripen across runs and tai chi would lose its chapter.
  // worldBeginLoop keeps the memory, so the first loop sees it. Zero carries nothing.
  a->world.memory.mush = (uint8_t)(in.carry & 0xFFu);
  a->world.memory.taichi_next = (uint8_t)(in.carry >> 8);
  beginLoop(a);
  computeAppPalette(a, true);
  // The chip's first update sets its look with no fade, so the first frame shows the real status.
  chipReqs(a);
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
    herdBell(a->world);   // deer in camp take it as a hard alarm (day_herd.h); nothing otherwise
    if (reactCanStart(a->world)) reactStart(a->world);
  }
  if (a->in.chip) {
    chipReqs(a);
    const Rect d = chipUpdate(a->chip, a->in.other_status, a->in.dog_out, a->in.night_mode, SS_TICK_DT);
    if (!d.empty()) a->renderer.markDirtyRect(d);
  }
  houseDogsSetInputs(a->world, a->in.dog_out, dogsNight(a));   // read by the dogs and the fetch this tick
  fireIceSetInputs(a->world, a->in.temp_ask);
  mailSetInputs(a->world, a->in.mail);
  visitorsSetInputs(a->world, a->in.visitors, dogsNight(a), a->sky);
  if (!spineSetPiece(a->world.plan.family)) {   // the stream (stream.h; camp loops only: it shares the Lookout's bytes)
    float m = sceneMood(a);
    m -= 6.f * floorf(m / 6.f);
    const bool twilight = (m > 0.6f && m < 1.6f) || (m > 4.4f && m < 5.6f);
    const bool night = m < 0.5f || m > 5.5f;
    // The old sky parks the moon high by mood; the real one has it up or not. A cloud deck hides it.
    const bool moon = night && a->sky.cover < 4 && (!a->sky.real || (a->sky.moon && a->sky.moon_y < LS_HORIZON));
    streamSetInputs(a->world, moon, twilight, night || a->in.night_mode, a->in.month);
    reedsSetInputs(a->world, m, night || a->in.night_mode, a->in.month);   // the reeds share the same block (reeds.h)
  }
  // A passer-by at the upstairs panel: he waves, once per arrival. Mike, 2026-11-16: if his hands are
  // busy he keeps trying for up to kPasserWaitTicks, even after they have gone, then lets it go.
  if (a->in.passer_by && !a->passer_prev) a->passer_wait = (uint8_t)kPasserWaitTicks;
  a->passer_prev = a->in.passer_by;
  if (a->passer_wait > 0) {
    if (reactWaveOk(a->world)) {
      reactStart(a->world, true);
      a->passer_wait = 0;
    } else {
      --a->passer_wait;
    }
  }

  // Palette follows the inputs only when they moved enough to matter.
  float flash = 0.f;
  const WeatherTint w = sceneWeather(a, &flash);
  if (moodDistance(sceneMood(a), a->pal_mood) > kMoodEps ||
      fabsf(w.overcast - a->pal_weather.overcast) > kWeatherEps ||
      fabsf(w.fog - a->pal_weather.fog) > kWeatherEps ||
      fabsf(w.night_dim - a->pal_weather.night_dim) > kWeatherEps ||
      fabsf(w.gloom - a->pal_weather.gloom) > kWeatherEps ||
      moonlightNow(a) != a->pal_moonlight ||   // a step of 1 in kMoonlightSteps: small, so it spreads
      fireIceWarmStep(a->world) != a->pal_warm ||   // the same
      w.night_gain != a->pal_weather.night_gain ||   // the host's setting: rare, every change counts
      flash != a->pal_flash ||   // the flash moves in a few fixed steps: every step counts
      a->in.night_mode != a->pal_night) {
    // A small or medium step may be spread over a few frames; lightning, the night rule switching
    // on or off and a new night gain change the whole frame at once.
    int bands = 0;
    if (flash == a->pal_flash && a->in.night_mode == a->pal_night && w.night_gain == a->pal_weather.night_gain) {
      const float dm = moodDistance(sceneMood(a), a->pal_mood);
      float dt = fabsf(w.overcast - a->pal_weather.overcast);
      if (fabsf(w.fog - a->pal_weather.fog) > dt) dt = fabsf(w.fog - a->pal_weather.fog);
      if (fabsf(w.night_dim - a->pal_weather.night_dim) > dt) dt = fabsf(w.night_dim - a->pal_weather.night_dim);
      if (fabsf(w.gloom - a->pal_weather.gloom) > dt) dt = fabsf(w.gloom - a->pal_weather.gloom);
      if (dm <= kSpreadMood && dt <= kSpreadTint) bands = a->spread_small;
      else if (dm <= kSpreadMoodMedium && dt <= kSpreadTintMedium) bands = a->spread_medium;
    }
    computeAppPalette(a, false, bands);
  }

  // The sky follows the real sun and moon. They move about a pixel a minute, so this is rare:
  // repaint the rows a body left and the rows it is in now (the rest of the cache is right
  // already) and composite just its old and new squares. A change of cloud cover, or the stars
  // changing as the moon sets, redraws the whole cache.
  {
    const SkyDraw sk = skyNow(a, w.overcast);
    if (!landscapeSkySame(sk, a->sky)) {
      if (sk.cover != a->sky.cover || sk.dark != a->sky.dark || sk.real != a->sky.real) {
        a->renderer.invalidateCache();
      } else {
        const SkyDraw& o = a->sky;
        const bool moved[2] = {
            sk.sun != o.sun || sk.sun_x != o.sun_x || sk.sun_y != o.sun_y || sk.sun_r != o.sun_r,
            sk.moon != o.moon || sk.moon_x != o.moon_x || sk.moon_y != o.moon_y || sk.moon_phase != o.moon_phase};
        // Each body's square before and after: [body][0 was, 1 now]. During the mushroom trip the sun
        // has a halo that reaches past its own square (skytrip.h; REVIEW_FIXES F1): the square grows
        // by that much, so the rows repainted and marked hold every pixel the halo can touch.
        const bool trip = skyTripOn(a->world.land.trip);
        Rect area[2][2] = {{landscapeSunBounds(o), landscapeSunBounds(sk)},
                           {landscapeMoonBounds(o), landscapeMoonBounds(sk)}};
        if (trip) {
          const SkyDraw* two[2] = {&o, &sk};
          for (int j = 0; j < 2; ++j) {
            Rect& q = area[0][j];
            if (q.empty()) continue;
            const int more = skyTripHaloReach(two[j]->sun_r) - (two[j]->sun_r + 2);
            q = Rect{q.x0 - more, q.y0 - more, q.x1 + more, q.y1 + more}.clipped(Rect{0, 0, SS_WIDTH, LS_GROUND_Y});
          }
        }
        for (int k = 0; k < 2; ++k) {
          if (!moved[k]) continue;
          int y0 = SS_HEIGHT, y1 = 0;
          for (const Rect& q : area[k]) {
            if (q.empty()) continue;
            if (q.y0 < y0) y0 = q.y0;
            if (q.y1 > y1) y1 = q.y1;
            a->renderer.markDirtyRect(q);
          }
          if (trip && k == 0) {
            // A wheeling star too near the sun is left out; a sun that moved may change that for a
            // star outside the rows above. Its circle is repainted with them (rare, and small).
            const SkyTripSun s0 = skyTripSunOf(o, a->renderer.palette), s1 = skyTripSunOf(sk, a->renderer.palette);
            for (int i = 0; i < LS_STAR_COUNT; ++i) {
              if (skyTripStarHidden(a->world.land, s0, i) == skyTripStarHidden(a->world.land, s1, i)) continue;
              const Rect q = skyTripStarBounds(a->world.land, i);
              if (q.empty()) continue;
              if (q.y0 < y0) y0 = q.y0;
              if (q.y1 > y1) y1 = q.y1;
              a->renderer.markDirtyRect(q);
            }
          }
          a->renderer.repaintRows(y0, y1);
        }
      }
      a->sky = sk;
    }
    a->world.sky_cover = a->sky.cover;   // before any worldTick below: the high clouds go with the cache's cover
  }

  // The doorbell reaction holds the story still (react.h): no beat or minor
  // advances, only the world's ambient layer and his reaction.
  if (a->world.react.active) {
    worldTick(a->world, a->rng, a->renderer);
    return false;
  }
  // Fire and ice takes his story over the same way (fireice.h), an episode of about two minutes a
  // loop from a calm moment until it is done.
  if (fireIceCanStart(a->world)) fireIceBegin(a->world, a->loop_seed ^ ((uint64_t)a->world.tick * 0x9E3779B97F4A7C15ull));
  if (fireIceHolds(a->world)) {
    worldTick(a->world, a->rng, a->renderer);
    return false;
  }
  // And the visiting couple, when they drop by (visitors.h).
  if (visitorsCanStart(a->world)) visitorsBegin(a->world);
  if (visitorsHolds(a->world)) {
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

void appSetPaletteSpread(App* app, int small_per_frame, int medium_per_frame) {
  auto clampBands = [](int n) { return (uint8_t)(n < 0 ? 0 : (n > SS_STRIP_COUNT ? SS_STRIP_COUNT : n)); };
  app->spread_small = clampBands(small_per_frame);
  app->spread_medium = clampBands(medium_per_frame);
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

void appInvalidateRect(App* app, int x0, int y0, int x1, int y1) { app->renderer.markDirtyRect(Rect{x0, y0, x1, y1}); }

void appSetOverlay(App* app, void (*draw)(void* user, Surface& strip, const Rect& clip), void* user) {
  app->overlay = draw;
  app->overlay_user = user;
}

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

bool appHouseDog(const App* app, int dog, Rect* out, int* act) {
  if (dog < 0 || dog >= HD_COUNT) return false;
  const World& w = app->world;
  const HouseDog& d = w.dogs.d[dog];
  const bool in = d.slot >= 0 && w.crit[d.slot].in_use && w.crit[d.slot].tag == CT_HOUSEDOG;
  if (act) *act = in ? d.act : -1;
  if (!in) return false;
  if (out) *out = critterBounds(w.critters[d.slot]);
  return w.critters[d.slot].visible;
}

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
uint16_t appCarry(const App* app) {
  const Memory& m = app->world.memory;
  return (uint16_t)(((unsigned)m.taichi_next << 8) | (unsigned)m.mush);
}
int appMemory(const App* app, char* buf, size_t n) { return memoryFormat(app->world.memory, buf, n); }
bool appBusy(const App* app) { return app->world.busy; }

int appSpineOverruns(const App* app) { return app->world.spine.overruns; }
uint32_t appLoopTicks(const App* app) { return spineLoopTicks(app->world.plan); }
float appFade(const App* app) { return app->world.fade; }
int appPaletteRebuilds(const App* app) { return app->pal_rebuilds; }
int appSpineFamily(const App* app) { return app->world.plan.family; }
bool appSetPieceShown(const App* app) {
  const World& w = app->world;
  if (w.plan.family == SF_LOOKOUT) return w.look.sunset_tick != 0;
  if (w.plan.family == SF_SUNRISE) return w.sr.broke_tick != 0;
  return false;
}
bool appVisitorsHere(const App* app) { return visitorsHolds(app->world); }

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
