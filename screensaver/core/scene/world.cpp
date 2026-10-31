// World: dynamic state, dirty tracking and the per-strip draw order. See world.h.
#include "world.h"

#include <math.h>
#include <string.h>

#include "../anim/ease.h"
#include "director.h"

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
// Every push goes through `add`, which stops at WORLD_ACTOR_MAX.
struct ActorList {
  ActorRef* a;
  int n;
  void add(uint8_t kind, uint8_t idx, int16_t y) {
    if (n < WORLD_ACTOR_MAX) a[n++] = ActorRef{kind, idx, y};
  }
};

// A foreground pine drawn again over `r` (owner 0 the hiker, 1 + slot a critter).
void addPines(ActorList& L, const World& w, const Rect& r, int owner) {
  for (int k = 0; k < 2; ++k)
    if (overlaps(r, landscapeFgPineBounds(w.land, k)))
      L.add(ACT_FGPINE, (uint8_t)(k | (owner << 1)), w.land.fg[k].base_y);
}

void sortActors(World& w) {
  ActorList L{w.actors, 0};
  L.add(ACT_FIRE, 0, (int16_t)(w.fire.pos.y + 5.f));   // front stones
  for (int i = 0; i < w.amb.n_tufts; ++i) L.add(ACT_TUFT, (uint8_t)i, w.land.tufts[w.amb.tuft[i]].y);
  const Hiker& h = w.hiker;
  const int16_t held_y = baseY(h.feet.y + 1.f);   // things in his hands go in front of him
  if (h.visible) {
    L.add(ACT_HIKER, 0, baseY(h.feet.y));
    addPines(L, w, w.hiker_rect, 0);
  }
  if (w.tent.state != TENT_PACKED) L.add(ACT_TENT, 0, baseY(w.tent.pos.y));
  if (w.pack.shown && (!w.pack.held || h.visible)) L.add(ACT_PACK, 0, w.pack.held ? held_y : baseY(w.pack.pos.y));
  if (w.bag.state == BAG_HUNG) L.add(ACT_BAG, 0, (int16_t)(w.land.camp.base_y - 1));
  else if (w.bag.state == BAG_DROPPED) L.add(ACT_BAG, 0, baseY(w.bag.pos.y));
  else if (w.bag.state == BAG_FLYING) L.add(ACT_BAG, 0, held_y);
  if (w.cairn.n > 0) L.add(ACT_CAIRN, 0, baseY(w.cairn.pos.y));
  for (int i = 0; i < ITEM_COUNT; ++i) {
    const Item& it = w.items[i];
    if (it.held && it.critter) {
      const Critter& c = w.critters[it.critter - 1];
      if (c.visible) L.add(ACT_ITEM, (uint8_t)i, baseY(c.feet.y + 1.f));
    } else if (it.held) {
      if (h.visible) L.add(ACT_ITEM, (uint8_t)i, held_y);
    }
    else if (it.on_fire) L.add(ACT_ITEM, (uint8_t)i, (int16_t)(w.fire.pos.y + 6.f));   // in front of the flames
    else if (it.down) L.add(ACT_ITEM, (uint8_t)i, baseY(it.pos.y));
  }
  if (w.leaf.on) L.add(ACT_LEAF, 0, baseY(w.leaf.base_y));
  for (int i = 0; i < CRITTER_MAX; ++i) {
    if (!w.crit[i].in_use || !w.critters[i].visible) continue;
    // A climbing critter sorts by the trunk's foot, not its own feet up the trunk.
    const float y = w.critters[i].pose.climb > 0.5f ? (float)w.land.camp.base_y + 1.f : w.critters[i].feet.y;
    L.add(ACT_CRITTER, (uint8_t)i, baseY(y));
    addPines(L, w, w.critter_rect[i], i + 1);
  }
  for (int i = 0; i < BITS_MAX; ++i) {
    const Bit& b = w.bits[i];
    if (b.kind == BIT_CONE) L.add(ACT_BIT, (uint8_t)i, baseY(b.land_y + 1.f));
    else if (b.kind != BIT_NONE) L.add(ACT_BIT, (uint8_t)i, baseY(b.land_y));
  }
  for (int i = 0; i < GLYPH_MAX; ++i)   // a major's glyphs (majors.h)
    if (w.major.g[i].kind != GL_NONE) L.add(ACT_MAJOR, (uint8_t)i, w.major.g[i].base_y);
  const int n = L.n;
  ActorRef* a = w.actors;
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
    w.hiker_rect = w.hiker_full = kEmpty;
    return;
  }
  const bool was_walking = h.walking;
  hikerTick(h, SS_TICK_DT);
  if (h.walking && fx.path_on) {
    const float span = fx.path_x1 - fx.path_x0;
    const float u = span != 0.f ? clamp01((h.feet.x - fx.path_x0) / span) : 1.f;
    h.feet.y = lerp(fx.path_y0, fx.path_y1, u);
  } else if (was_walking && fx.path_on && fabsf(h.feet.x - fx.path_x1) < 0.5f) {
    h.feet.y = fx.path_y1;   // arrived this tick: the last step's y too
  }

  HikerPose& P = h.pose;
  P.lean += fx.lean;
  P.head += fx.head;
  P.lift += fx.lift;
  for (int i = 0; i < 2; ++i) {
    if (fx.arm_w[i] <= 0.f) continue;
    P.shoulder[i] = lerp(P.shoulder[i], fx.arm_sh[i], fx.arm_w[i]);
    P.elbow[i] = lerp(P.elbow[i], fx.arm_el[i], fx.arm_w[i]);
  }
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
    if (fx.stomp > 0.f) {   // storming off: arms swung high and stiff, head down, leaning into it
      const float s = sinT(h.walk_phase);
      P.shoulder[0] = lerp(P.shoulder[0], 1.15f * s, fx.stomp);
      P.shoulder[1] = lerp(P.shoulder[1], -1.15f * s, fx.stomp);
      P.elbow[0] = lerp(P.elbow[0], 0.35f + (s > 0.f ? 0.9f * s : 0.f), fx.stomp);
      P.elbow[1] = lerp(P.elbow[1], 0.35f + (s < 0.f ? -0.9f * s : 0.f), fx.stomp);
      P.lean += 0.12f * fx.stomp;
      P.head += 0.16f * fx.stomp;
    }
  }
  // The waterline on him: none on the bank; deeper the further in he is.
  fx.wet_y = 0.f;
  if (fx.wade) {
    float cy, half;
    if (landscapeStreamSpan(w.land, h.feet.x, cy, half) && fabsf(h.feet.y - cy) < half) {
      const float in = 1.f - fabsf(h.feet.y - cy) / half;
      fx.wet_y = h.feet.y - 2.f - 0.2f * h.height * in;
    }
  }

  h.lit_from = 0;
  if (w.fire.state == FIRE_LIT) h.lit_from = w.fire.pos.x < h.feet.x ? -1 : 1;

  const Vec2 m = hikerHandsMid(h);
  if (w.pack.held) {   // hangs from his hands, never below his feet
    w.pack.pos = Vec2{m.x, fminf(m.y + 13.f, h.feet.y + 3.f)};
    w.pack.facing = (int8_t)h.facing;
  }
  for (int i = 0; i < ITEM_COUNT; ++i) {
    Item& it = w.items[i];
    if (!it.held || it.critter) continue;   // a critter's paw holds it (directorStep)
    it.pos = i == ITEM_FORK || i == ITEM_FISH ? hikerHandPos(h, 0) : m;   // fork, fish stick: in his near hand
    it.facing = (int8_t)h.facing;
    it.ground_y = h.feet.y;
  }
  if (w.bag.state == BAG_FLYING) w.bag.rope = hikerHandPos(h, 0);

  // What he covers, with everything he holds, so a foreground pine he walks
  // behind also covers the dragged stick.
  Rect r = hikerBounds(h);
  h.visible = r.x1 > 0 && r.x0 < SS_WIDTH;
  if (h.visible) {
    unite(r, armloadBounds(h, fx.armload));
    if (w.pack.held) unite(r, packBounds(w.pack));
    for (int i = 0; i < ITEM_COUNT; ++i)
      if (w.items[i].held && !w.items[i].critter) unite(r, itemBounds((ItemId)i, w.items[i]));
    w.hiker_full = r;
    w.hiker_rect = r.clipped(kFrame);
  } else {
    w.hiker_rect = w.hiker_full = kEmpty;
  }
}

// Bounds of a prop he holds: nothing while he is out of frame.
inline Rect heldBounds(const World& w, bool held, const Rect& b) { return held && !w.hiker.visible ? kEmpty : b; }

// Bounds of an item: held ones only while their holder is in frame.
Rect itemTrackBounds(const World& w, int i) {
  const Item& it = w.items[i];
  const Rect b = itemBounds((ItemId)i, it);
  if (it.held && it.critter) return w.critters[it.critter - 1].visible ? b : kEmpty;
  return heldBounds(w, it.held, b);
}

}  // namespace

int worldTakeBit(World& w, uint8_t kind) {
  int s = -1;
  for (int i = 0; i < BITS_MAX && s < 0; ++i)
    if (w.bits[i].kind == BIT_NONE) s = i;
  if (s < 0) {   // the one that has been about longest (Bit::t runs on while it rests)
    float oldest = -1.f;
    int best = -1;
    for (int i = 0; i < BITS_MAX; ++i) {
      const Bit& b = w.bits[i];
      if (!b.resting || (b.kind != BIT_CONE && b.kind != BIT_LEAF)) continue;
      if (b.t > oldest) {
        oldest = b.t;
        best = i;
      }
    }
    s = best;
  }
  if (s < 0) return -1;
  w.bits[s] = Bit{};
  w.bits[s].kind = kind;
  return s;
}

void worldBeginLoop(World& w, Rng& rng) {
  // A clean World each loop; only Memory carries over.
  const Memory keep = w.memory;
  memset(static_cast<void*>(&w), 0, sizeof(World));
  w.memory = keep;
  ++w.memory.loop_count;

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
  f.fuel = 1.f;

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
  for (int i = 0; i < 2; ++i) fx.arm_w[i] = fx.arm_sh[i] = fx.arm_el[i] = 0.f;
  fx.stomp = fx.wet_y = 0.f;
  fx.wade = false;
  w.hiker_rect = kEmpty;
  tentInit(w.tent, c.tent);
  w.pack = PackProp{c.pack, 1, false, false};
  bagInit(w.bag, Vec2{(float)(w.land.camp_x - LS_BRANCH_LEN), (float)LS_BRANCH_TIP_Y});
  w.cairn = Cairn{c.cairn, 0, 1.f};
  for (Item& it : w.items) it = Item{c.wood, false, false, 1, 1.f, c.wood.y, false, 0, 0.f, 0.f, 1.f, false};
  w.items[ITEM_WOOD].pos = c.wood;
  w.steam = Steam{Vec2{0.f, 0.f}, -1.f, 0};
  w.skip.n = 0;
  w.skip.t = -1.f;
  for (Vec2& p : w.skip.p) p = Vec2{0.f, 0.f};
  w.leaf = Leaf{false, Vec2{0.f, 0.f}, 0.f, 0.f};
  w.fade = 1.f;   // a loop opens from black; the build beat lifts it

  // Critter slots: all free, each with this loop's personality (one rng call
  // per statement).
  for (CritterSlot& s : w.crit) {
    s = CritterSlot{};
    const float sd = rng.range(0.f, 1.f);
    const float jd = rng.range(0.f, 1.f);
    s.speed_dial = sd;
    s.jitter_dial = jd;
  }
  for (int i = 0; i < CRITTER_MAX; ++i) {
    critterInit(w.critters[i], CR_CHIPMUNK, Vec2{0.f, 0.f}, 1);
    w.critters[i].visible = false;
    w.critter_rect[i] = kEmpty;
  }
  for (Bit& b : w.bits) b = Bit{};
  minorsInit(w, rng);

  sortActors(w);

  WorldDirty& d = w.dirty;
  d.cloud[0] = d.cloud[1] = d.bird = d.fire = kEmpty;
  for (Rect& r : d.glint) r = kEmpty;
  for (Rect& r : d.tuft) r = kEmpty;
  for (Rect& r : d.smoke) r = kEmpty;
  d.hiker = d.tent = d.pack = d.bag = d.cairn = kEmpty;
  for (Rect& r : d.critter) r = kEmpty;
  for (Rect& r : d.bit) r = kEmpty;
  for (int i = 0; i < BITS_MAX; ++i) d.bit_s[i] = w.bits[i];
  for (Rect& r : d.item) r = kEmpty;
  d.steam = d.skip = d.leaf = kEmpty;
  d.steam_s = w.steam;
  d.skip_s = w.skip;
  d.leaf_s = w.leaf;
  d.fade = w.fade;
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
  spineCritterScenes(w);
  minorsStep(w, r);
  directorStep(w);
  for (Bit& b : w.bits) bitStep(b, SS_TICK_DT);
  // The shadow in the lit tent breathes: rises up to 2 px and sinks, every 4.8 s.
  // (Whole pixels, so it dirties the tent four times a breath, not every tick.)
  // Frightened (shadow_jitter > 0) it holds its breath and trembles instead.
  Tent& tent = w.tent;
  const bool shown = tent.lit && tent.shadow;
  const float tk = (float)w.tick;
  // Trembling, he also hunches down (up to 3 px).
  if (!shown) tent.shadow_y = 0.f;
  else if (tent.shadow_jitter <= 0.f) tent.shadow_y = -floorf(1.f + sinT(tk / (4.8f * (float)SS_TICK_HZ)));
  else tent.shadow_y = floorf(3.f * tent.shadow_jitter);
  tent.shadow_shake = shown && tent.shadow_jitter > 0.f
                          ? floorf(0.5f + 2.f * tent.shadow_jitter * sumSines(tk * 0.11f, 2))
                          : 0.f;
  sortActors(w);

  WorldDirty& d = w.dirty;
  // A fade that moved repaints every strip.
  if (w.fade != d.fade) {
    r.markAllDirty();
    d.fade = w.fade;
  }
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
    track(r, d.item[i], itemTrackBounds(w, i), !itemSame(w.items[i], d.item_s[i]));
    d.item_s[i] = w.items[i];
  }
  // Critters move or fidget every tick they are in frame.
  for (int i = 0; i < CRITTER_MAX; ++i) track(r, d.critter[i], w.critter_rect[i], !w.critter_rect[i].empty());
  for (int i = 0; i < BITS_MAX; ++i) {
    track(r, d.bit[i], bitBounds(w.bits[i]), !bitSame(w.bits[i], d.bit_s[i]));
    d.bit_s[i] = w.bits[i];
  }
  track(r, d.steam, steamBounds(w.steam), !steamSame(w.steam, d.steam_s));
  track(r, d.skip, skipBounds(w.skip), !skipSame(w.skip, d.skip_s));
  track(r, d.leaf, leafBounds(w.leaf), !leafSame(w.leaf, d.leaf_s));
  d.tent_s = w.tent;
  d.pack_s = w.pack;
  d.bag_s = w.bag;
  d.cairn_s = w.cairn;
  d.steam_s = w.steam;
  d.skip_s = w.skip;
  d.leaf_s = w.leaf;
  majorsTrack(w.major, r);
}

void worldDrawWater(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  if (clip.empty()) return;
  // The same polygon as the landscape's water body (landscape.cpp drawStream;
  // keep the two in step).
  const Landscape& ls = w.land;
  Vec2 p[2 * LS_STREAM_PTS];
  int n = 0;
  for (int i = 0; i < LS_STREAM_PTS; ++i) {
    const LsStreamPt& q = ls.stream[i];
    p[n++] = Vec2{q.x + q.nx * q.hw, q.y + q.ny * q.hw};
  }
  for (int i = LS_STREAM_PTS - 1; i >= 0; --i) {
    const LsStreamPt& q = ls.stream[i];
    p[n++] = Vec2{q.x - q.nx * q.hw, q.y - q.ny * q.hw};
  }
  fillPolygon(s, p, n, pal[WATER], clip);
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

  // 4. Stream highlights, and a skipped stone's rings on the water.
  ambientDrawGlints(w, strip, clip, pal);
  skipDraw(w.skip, strip, clip, pal);

  // 5. Ground actors far to near.
  for (int i = 0; i < w.n_actors; ++i) {
    const ActorRef& a = w.actors[i];
    switch (a.kind) {
      case ACT_FIRE: fireDraw(w.fire, strip, clip, pal); break;
      case ACT_TUFT: ambientDrawTuft(w, a.idx, strip, clip, pal); break;
      case ACT_HIKER:
        hikerDraw(w.hiker, strip, pal, clip);
        armloadDraw(w.hiker, w.fx.armload, strip, clip, pal);
        if (w.fx.wet_y > 0.f) {   // in the stream: the water over him below his waterline
          const Rect& hr = w.hiker_rect;
          worldDrawWater(w, strip, clip.clipped(Rect{hr.x0, (int)ceilf(w.fx.wet_y), hr.x1, hr.y1}), pal);
        }
        break;
      case ACT_TENT: tentDraw(w.tent, strip, clip, pal); break;
      case ACT_PACK: packDraw(w.pack, strip, clip, pal); break;
      case ACT_BAG: bagDraw(w.bag, strip, clip, pal); break;
      case ACT_CAIRN: cairnDraw(w.cairn, strip, clip, pal); break;
      case ACT_ITEM: itemDraw((ItemId)a.idx, w.items[a.idx], strip, clip, pal); break;
      case ACT_LEAF: leafDraw(w.leaf, strip, clip, pal); break;
      case ACT_CRITTER: critterDraw(w.critters[a.idx], strip, pal, clip); break;
      case ACT_BIT: bitDraw(w.bits[a.idx], strip, clip, pal); break;
      case ACT_MAJOR:
        if (w.major.g[a.idx].kind == GL_WATER) worldDrawWater(w, strip, clip.clipped(glyphBounds(w.major.g[a.idx])), pal);
        else glyphDraw(w.major.g[a.idx], strip, clip, pal);
        break;
      case ACT_FGPINE: {   // only over its actor: elsewhere the cached pine is already right
        const int owner = a.idx >> 1;
        const Rect& over = owner == 0 ? w.hiker_rect : w.critter_rect[owner - 1];
        const Rect c = clip.clipped(over);
        if (!c.empty()) landscapeDrawFgPine(w.land, a.idx & 1, strip, c, pal);
        break;
      }
      default: break;
    }
  }

  // 6. Smoke above the fire, and steam off a pot boiling over.
  ambientDrawSmoke(w, strip, clip, pal);
  steamDraw(w.steam, strip, clip, pal);

  // 7. The loop's fade, over everything.
  if (w.fade > 0.f) {
    const float a = w.fade >= 1.f ? 255.f : w.fade * 255.f + 0.5f;
    const uint8_t alpha = (uint8_t)a;
    for (int y = clip.y0; y < clip.y1; ++y) blendSpan(strip, y, clip.x0, clip.x1, pal[ACTOR], alpha);
  }
}

}  // namespace ss
