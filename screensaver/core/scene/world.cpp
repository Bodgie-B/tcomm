// World: dynamic state, dirty tracking and the per-strip draw order. See world.h.
#include "world.h"

#include "../anim/ease.h"

namespace ss {

namespace {

const Rect kEmpty{0, 0, 0, 0};

inline bool sameRect(const Rect& a, const Rect& b) {
  return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

inline void markOn(Renderer& r, const Rect& b) {
  if (!b.empty() && b.x1 > 0 && b.x0 < SS_WIDTH) r.markDirtyRect(b);
}

// Dirty where the element was and where it is, if it moved or changed shape.
void track(Renderer& r, Rect& prev, const Rect& cur, bool changed) {
  if (!changed && sameRect(prev, cur)) return;
  markOn(r, prev);
  markOn(r, cur);
  prev = cur;
}

// Ground actors far to near by base y; insertion sort keeps equal y in list order.
void sortActors(World& w) {
  int n = 0;
  w.actors[n++] = ActorRef{ACT_FIRE, 0, (int16_t)(w.fire.pos.y + 5.f)};   // front stones
  for (int i = 0; i < w.amb.n_tufts && n < WORLD_ACTOR_MAX; ++i) {
    w.actors[n++] = ActorRef{ACT_TUFT, (uint8_t)i, w.land.tufts[w.amb.tuft[i]].y};
  }
  for (int i = 1; i < n; ++i) {
    const ActorRef key = w.actors[i];
    int j = i - 1;
    while (j >= 0 && w.actors[j].base_y > key.base_y) {
      w.actors[j + 1] = w.actors[j];
      --j;
    }
    w.actors[j + 1] = key;
  }
  w.n_actors = n;
}

}  // namespace

void worldBeginLoop(World& w, Rng& rng) {
  landscapeGenerate(w.land, rng);
  cloudGenerate(w.clouds[0], rng, rng.range(0.f, 800.f), 96.f + rng.range(-4.f, 4.f), 6.f);
  cloudGenerate(w.clouds[1], rng, rng.range(0.f, 800.f), 166.f + rng.range(-4.f, 4.f), 4.f);

  // Fire pit in the left half of the clearing, clear of the tent's side.
  FirePit& f = w.fire;
  f.pos = Vec2{250.f + rng.range(-12.f, 12.f), 398.f + rng.range(-5.f, 5.f)};
  f.state = FIRE_UNLIT;
  f.intensity = 0.f;
  f.stones = 0.f;
  f.clock = 0.f;

  buildReset(w.build);
  ambientInit(w, rng);
  w.tick = 0;
  sortActors(w);

  WorldDirty& d = w.dirty;
  d.cloud[0] = d.cloud[1] = d.bird = d.fire = kEmpty;
  for (Rect& r : d.glint) r = kEmpty;
  for (Rect& r : d.tuft) r = kEmpty;
  for (Rect& r : d.smoke) r = kEmpty;
  d.cached = w.build.cached;
  d.phase = w.build.phase;
  d.u = w.build.u;
  d.fire_stones = 0.f;
  d.fire_int = 0.f;
}

void worldTick(World& w, Rng& rng, Renderer& r) {
  ++w.tick;
  for (Cloud& c : w.clouds) cloudStep(c);
  ambientStep(w, rng);
  sortActors(w);

  WorldDirty& d = w.dirty;
  const BuildState& b = w.build;

  // Build-up: rebuild the cache when the set of cached layers changes (once
  // per phase boundary), and redraw the active layers' rows while they move.
  if (b.cached != d.cached) {
    r.invalidateCache();
    d.cached = b.cached;
  }
  if (b.phase != d.phase) {
    r.markAllDirty();
  } else if (b.u != d.u) {
    int y0, y1;
    buildPhaseRows(b, y0, y1);
    r.markDirtyRows(y0, y1);
  }
  d.phase = b.phase;
  d.u = b.u;

  const bool sky_fading = buildSkyFade(b) < 1.f;   // clouds fade in with the sky
  for (int i = 0; i < 2; ++i) track(r, d.cloud[i], cloudBounds(w.clouds[i]), sky_fading);
  track(r, d.bird, ambientBirdBounds(w), w.amb.bird.active);

  const FirePit& f = w.fire;
  const bool fire_changed = f.intensity > 0.f || f.intensity != d.fire_int || f.stones != d.fire_stones;
  track(r, d.fire, fireBounds(f), fire_changed);

  for (int i = 0; i < AMB_GLINT_MAX; ++i) track(r, d.glint[i], ambientGlintBounds(w, i), true);

  const bool sub = ambientSubTick(w);
  for (int i = 0; i < w.amb.n_tufts; ++i) track(r, d.tuft[i], ambientTuftBounds(w, i), sub);
  const bool smoke_changed = sub || f.intensity != d.fire_int;
  for (int i = 0; i < AMB_SMOKE_MAX; ++i) track(r, d.smoke[i], ambientSmokeBounds(w, i), smoke_changed);

  d.fire_int = f.intensity;
  d.fire_stones = f.stones;
}

void worldDrawStatic(const World& w, Surface& frame, const Palette& pal, float mood) {
  landscapeDrawStatic(w.land, frame, pal, mood, w.build);
}

// The per-strip draw order, far to near. Everything dynamic is drawn here and
// nowhere else.
void worldDrawDynamic(const World& w, Surface& strip, const Rect& clip, const Palette& pal, float mood) {
  const BuildState& b = w.build;

  // 1. The sky, while it fades in (otherwise it is in the cache).
  landscapeDrawLayers(w.land, strip, clip, pal, mood, b, BL_SKY, BL_SKY);

  // 2. Sky actors: clouds (fading in with the sky) and the distant bird.
  const float fade = buildSkyFade(b);
  if (fade < 1.f) {
    Palette cp = pal;
    cp.c[CLOUD_LIT] = mixRGB(pal[SKY4], pal[CLOUD_LIT], fade);
    cp.c[CLOUD_SHADE] = mixRGB(pal[SKY4], pal[CLOUD_SHADE], fade);
    for (const Cloud& c : w.clouds) cloudDraw(c, strip, cp, clip);
  } else {
    for (const Cloud& c : w.clouds) cloudDraw(c, strip, pal, clip);
  }
  ambientDrawBird(w, strip, clip, pal);

  // 3. Build-up: the layers animating now and every built layer in front of them.
  landscapeDrawLayers(w.land, strip, clip, pal, mood, b, BL_RIDGES, BL_COUNT - 1);

  // 4. Stream highlights.
  ambientDrawGlints(w, strip, clip, pal);

  // 5. Ground actors far to near.
  for (int i = 0; i < w.n_actors; ++i) {
    const ActorRef& a = w.actors[i];
    if (a.kind == ACT_FIRE) fireDraw(w.fire, strip, clip, pal);
    else ambientDrawTuft(w, a.idx, strip, clip, pal);
  }

  // 6. Smoke above the fire.
  ambientDrawSmoke(w, strip, clip, pal);
}

}  // namespace ss
