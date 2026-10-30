// World: dynamic state, dirty tracking and the per-strip draw order. See world.h.
#include "world.h"

#include <math.h>

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

inline bool overlaps(const Rect& a, const Rect& b) { return !a.clipped(b).empty(); }

const Rect kFrame{0, 0, SS_WIDTH, SS_HEIGHT};

inline void unite(Rect& r, const Rect& b) {
  if (b.empty()) return;
  r = Rect{r.x0 < b.x0 ? r.x0 : b.x0, r.y0 < b.y0 ? r.y0 : b.y0, r.x1 > b.x1 ? r.x1 : b.x1, r.y1 > b.y1 ? r.y1 : b.y1};
}

inline int16_t baseY(float y) { return (int16_t)floorf(y); }

// Ground actors far to near by base y; insertion sort keeps equal y in list order.
void sortActors(World& w) {
  int n = 0;
  ActorRef* a = w.actors;
  a[n++] = ActorRef{ACT_FIRE, 0, (int16_t)(w.fire.pos.y + 5.f)};   // front stones
  for (int i = 0; i < w.amb.n_tufts && n < WORLD_ACTOR_MAX; ++i) {
    a[n++] = ActorRef{ACT_TUFT, (uint8_t)i, w.land.tufts[w.amb.tuft[i]].y};
  }
  const Hiker& h = w.hiker;
  const int16_t held_y = baseY(h.feet.y + 1.f);   // things in his hands go in front of him
  if (h.visible) {
    a[n++] = ActorRef{ACT_HIKER, 0, baseY(h.feet.y)};
    for (int k = 0; k < 2; ++k) {
      if (overlaps(w.hiker_rect, landscapeFgPineBounds(w.land, k))) {
        a[n++] = ActorRef{ACT_FGPINE, (uint8_t)k, w.land.fg[k].base_y};
      }
    }
  }
  if (w.tent.state != TENT_PACKED) a[n++] = ActorRef{ACT_TENT, 0, baseY(w.tent.pos.y)};
  if (w.pack.shown && (!w.pack.held || h.visible))
    a[n++] = ActorRef{ACT_PACK, 0, w.pack.held ? held_y : baseY(w.pack.pos.y)};
  if (w.bag.state == BAG_HUNG) a[n++] = ActorRef{ACT_BAG, 0, (int16_t)(w.land.camp.base_y - 1)};
  else if (w.bag.state == BAG_DROPPED) a[n++] = ActorRef{ACT_BAG, 0, baseY(w.bag.pos.y)};
  if (w.cairn.n > 0) a[n++] = ActorRef{ACT_CAIRN, 0, baseY(w.cairn.pos.y)};
  for (int i = 0; i < ITEM_COUNT && n < WORLD_ACTOR_MAX; ++i) {
    const Item& it = w.items[i];
    if (it.held) {
      if (h.visible) a[n++] = ActorRef{ACT_ITEM, (uint8_t)i, held_y};
    }
    else if (it.down) a[n++] = ActorRef{ACT_ITEM, (uint8_t)i, baseY(it.pos.y)};
  }
  for (int i = 1; i < n; ++i) {
    const ActorRef key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j].base_y > key.base_y) {
      a[j + 1] = a[j];
      --j;
    }
    a[j + 1] = key;
  }
  w.n_actors = n;
}

// The hiker's tick: the rig, then what the spine layers on it (feet on the
// walk path, a hop while wading, pose offsets), the fire's rim light, what he
// holds, and whether he is in frame.
void stepHiker(World& w) {
  Hiker& h = w.hiker;
  HikerFx& fx = w.fx;
  if (!fx.on) {
    h.visible = false;
    w.hiker_rect = kEmpty;
    return;
  }
  hikerTick(h, SS_TICK_DT);
  if (h.walking && fx.path_on) {
    const float span = fx.path_x1 - fx.path_x0;
    const float u = span != 0.f ? clamp01((h.feet.x - fx.path_x0) / span) : 1.f;
    h.feet.y = lerp(fx.path_y0, fx.path_y1, u);
  }

  HikerPose& P = h.pose;
  P.lean += fx.lean;
  P.head += fx.head;
  P.lift += fx.lift;
  if (fx.reach > 0.f) {   // near arm out straight
    P.shoulder[0] = lerp(P.shoulder[0], fx.reach_to, fx.reach);
    P.elbow[0] = lerp(P.elbow[0], 0.12f, fx.reach);
  }
  if (h.walking) {
    float cy, half;
    // Wading: each step lifts him a little higher than on the bank.
    if (landscapeStreamSpan(w.land, h.feet.x, cy, half) && fabsf(h.feet.y - cy) < half) {
      const float s = sinT(h.walk_phase * 2.f);
      P.lift += 0.045f * s * s;
    }
    if (fx.armload > 0.f) P.lean += fx.armload * (0.05f + 0.07f * sinT(h.walk_phase));   // staggering
  }

  h.lit_from = 0;
  if (w.fire.state == FIRE_LIT) h.lit_from = w.fire.pos.x < h.feet.x ? -1 : 1;

  const Vec2 m = hikerHandsMid(h);
  if (w.pack.held) {   // hangs from his hands, never below his feet
    w.pack.pos = Vec2{m.x, fminf(m.y + 13.f, h.feet.y + 3.f)};
    w.pack.facing = (int8_t)h.facing;
  }
  for (Item& it : w.items) {
    if (!it.held) continue;
    it.pos = m;
    it.facing = (int8_t)h.facing;
    it.ground_y = h.feet.y;
  }

  // What he covers, with everything he holds, so a foreground pine he walks
  // behind also covers the dragged stick.
  Rect r = hikerBounds(h);
  h.visible = r.x1 > 0 && r.x0 < SS_WIDTH;
  if (h.visible) {
    unite(r, armloadBounds(h, fx.armload));
    if (w.pack.held) unite(r, packBounds(w.pack));
    for (int i = 0; i < ITEM_COUNT; ++i)
      if (w.items[i].held) unite(r, itemBounds((ItemId)i, w.items[i]));
    w.hiker_rect = r.clipped(kFrame);
  } else {
    w.hiker_rect = kEmpty;
  }
}

// Bounds of a prop he holds: nothing while he is out of frame.
inline Rect heldBounds(const World& w, bool held, const Rect& b) { return held && !w.hiker.visible ? kEmpty : b; }

}  // namespace

void worldBeginLoop(World& w, Rng& rng) {
  landscapeGenerate(w.land, rng);
  // One rng call per statement: argument evaluation order differs between
  // compilers, and the PC preview must replay the board bit for bit.
  for (int i = 0; i < 2; ++i) {
    const float cx = rng.range(0.f, 800.f);
    const float jitter = rng.range(-4.f, 4.f);
    // The lower cloud sits at 158, above the camp pine's tip (y 170).
    cloudGenerate(w.clouds[i], rng, cx, (i == 0 ? 96.f : 158.f) + jitter, i == 0 ? 6.f : 4.f);
  }

  // Fire pit in the left half of the clearing, clear of the tent's side.
  FirePit& f = w.fire;
  f.pos = Vec2{250.f + rng.range(-12.f, 12.f), 398.f + rng.range(-5.f, 5.f)};
  f.state = FIRE_UNLIT;
  f.intensity = 0.f;
  f.stones = 0.f;
  f.clock = 0.f;

  f.kindle = 0.f;

  buildReset(w.build);
  ambientInit(w, rng);
  w.tick = 0;

  // Camp: layout from the pit and the landscape; nothing pitched, the hiker
  // waits off-frame until the arrival beat places him.
  spineLayout(w);
  const CampLayout& c = w.spine.camp;
  hikerInit(w.hiker, c.edge_l, 1);
  w.hiker.visible = false;
  HikerFx& fx = w.fx;
  fx.on = fx.path_on = false;
  fx.path_x0 = fx.path_y0 = fx.path_x1 = fx.path_y1 = 0.f;
  fx.lean = fx.head = fx.lift = fx.reach = fx.armload = 0.f;
  fx.reach_to = 1.3f;
  w.hiker_rect = kEmpty;
  tentInit(w.tent, c.tent);
  w.pack = PackProp{c.pack, 1, false, false};
  bagInit(w.bag, Vec2{(float)(w.land.camp_x - 82), 294.f});
  w.cairn = Cairn{c.cairn, 0, 1.f};
  for (Item& it : w.items) it = Item{c.wood, false, false, 1, 1.f, c.wood.y};
  w.items[ITEM_WOOD].pos = c.wood;

  sortActors(w);

  WorldDirty& d = w.dirty;
  d.cloud[0] = d.cloud[1] = d.bird = d.fire = kEmpty;
  for (Rect& r : d.glint) r = kEmpty;
  for (Rect& r : d.tuft) r = kEmpty;
  for (Rect& r : d.smoke) r = kEmpty;
  d.hiker = d.tent = d.pack = d.bag = d.cairn = kEmpty;
  for (Rect& r : d.item) r = kEmpty;
  d.tent_s = w.tent;
  d.pack_s = w.pack;
  d.bag_s = w.bag;
  d.cairn_s = w.cairn;
  for (int i = 0; i < ITEM_COUNT; ++i) d.item_s[i] = w.items[i];
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
  stepHiker(w);
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
    // A cache change already redraws everything; otherwise only the rows the
    // old and the new phase can touch (the fire -> done boundary changes no layer).
    BuildState prev = b;
    prev.phase = d.phase;
    int y0, y1;
    buildPhaseRows(prev, y0, y1);
    r.markDirtyRows(y0, y1);
    buildPhaseRows(b, y0, y1);
    r.markDirtyRows(y0, y1);
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

  // The hiker breathes, so while he is in frame he is redrawn every tick.
  track(r, d.hiker, w.hiker_rect, w.hiker.visible);
  track(r, d.tent, tentBounds(w.tent), !tentSame(w.tent, d.tent_s));
  track(r, d.pack, heldBounds(w, w.pack.held, packBounds(w.pack)), !packSame(w.pack, d.pack_s));
  track(r, d.bag, bagBounds(w.bag), !bagSame(w.bag, d.bag_s));
  track(r, d.cairn, cairnBounds(w.cairn), !cairnSame(w.cairn, d.cairn_s));
  for (int i = 0; i < ITEM_COUNT; ++i) {
    track(r, d.item[i], heldBounds(w, w.items[i].held, itemBounds((ItemId)i, w.items[i])),
          !itemSame(w.items[i], d.item_s[i]));
    d.item_s[i] = w.items[i];
  }
  d.tent_s = w.tent;
  d.pack_s = w.pack;
  d.bag_s = w.bag;
  d.cairn_s = w.cairn;
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
    switch (a.kind) {
      case ACT_FIRE: fireDraw(w.fire, strip, clip, pal); break;
      case ACT_TUFT: ambientDrawTuft(w, a.idx, strip, clip, pal); break;
      case ACT_HIKER:
        hikerDraw(w.hiker, strip, pal, clip);
        armloadDraw(w.hiker, w.fx.armload, strip, clip, pal);
        break;
      case ACT_TENT: tentDraw(w.tent, strip, clip, pal); break;
      case ACT_PACK: packDraw(w.pack, strip, clip, pal); break;
      case ACT_BAG: bagDraw(w.bag, strip, clip, pal); break;
      case ACT_CAIRN: cairnDraw(w.cairn, strip, clip, pal); break;
      case ACT_ITEM: itemDraw((ItemId)a.idx, w.items[a.idx], strip, clip, pal); break;
      case ACT_FGPINE: {   // only over the hiker: elsewhere the cached pine is already right
        const Rect c = clip.clipped(w.hiker_rect);
        if (!c.empty()) landscapeDrawFgPine(w.land, a.idx, strip, c, pal);
        break;
      }
      default: break;
    }
  }

  // 6. Smoke above the fire.
  ambientDrawSmoke(w, strip, clip, pal);
}

}  // namespace ss
