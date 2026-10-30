# Stages 2 to 6: architecture spec

Written 2026-10-30 by the chief session before any stage 2 code. This is the
shape the preview stages build against so they fit together. Design intent
stays in `CAMPSITE_SCREENSAVER_BRIEF.md`; stage order and budgets are in
`START_HERE.md`. Status lines at the bottom are updated as stages land.

## Layering (what lives where)

```
core/scene/
  landscape.*   static geometry + static drawing + clouds (stage 1, extended)
  world.h/.cpp  World: everything dynamic in one struct; dirty tracking; draw order
  ambient.*     grass sway, stream flow, fire flicker, distant bird, smoke
  buildup.*     the build-up beat: sky fade, trees stand up, stream rolls in, plants pop
  hiker.*       the hiker rig: joints, poses, walk cycle, carried props (stage 3)
  props.*       tent, fire pit, food bag, pack, cairn, small items (stage 4)
  spine.*       story spine beats and their variants (stage 4)
  critters.*    animals and their behaviors (stage 5)
  minors.*      minor-effect scheduler (stage 5)
  majors.*      surprise pool and the variety system (stage 6)
  memory.*      world state that survives across loops (stage 6)
  app.cpp       implements ss_app.h; owns World, Timeline, Renderer
```

Rules from `ss_platform.h` stand: no allocation after init, single precision,
no platform calls, `sizeof(App) <= SS_APP_BYTES` (96 KB, of which the strip
buffer is 51 KB). Every new struct lives inside `App` by value.

## Static cache vs dynamic layer

The renderer (`render/renderer.h`) keeps the static scene in a full-frame
cache and composites dirty strips. The split is:

- **In the cache:** sky bands, sun/moon, stars, ridges, far trees, mid pines,
  ground, stream body and far bank, rocks, the camp pine, foreground pines,
  and the tufts that do not sway.
- **Dynamic (drawn per dirty strip, every frame they change):** clouds, bird,
  in-progress build-up layers, stream highlights, swaying tufts, fire, smoke,
  tent, props, hiker, critters.

The cache is rebuilt on palette change and at build-up phase boundaries (a
handful of times per loop), never per frame.

### Draw order inside a strip (far to near)

1. clouds, bird (sky)
2. build-up in-progress layers (only during the build beat)
3. stream highlights (ripples drift along the stream)
4. ground actors sorted by base y, front-most last: tent, fire pit, props,
   hiker, critters. A small insertion sort over at most ~24 entries per frame.
5. smoke (above the fire, behind nothing that matters)
6. foreground swaying tufts

### Dirty tracking

Each dynamic element has a bounds rect in frame space. `World` keeps each
element's previous bounds; every tick it marks previous and current bounds
dirty through `Renderer::markDirtyRect`. Elements that did not move mark
nothing. Slow movers (clouds at 6 px/s) only mark when their integer position
changes, so a cloud costs strips only every few ticks.

Ambient elements on the ground (grass, stream highlights) run on a sub-rate:
grass advances every 2nd tick, which is still 12.5 Hz and reads as wind. The
stream highlight moves every tick but only within strips 11 to 14.

## Budget (proposed, replaced by stage 7 measurements)

- Strips per frame: average ≤ 8 of 15 during ambient-only stretches.
- PC frame time: ≤ 2 ms average (`ss_preview` reports it), measured on the
  same machine as the 0.24 ms stage 1 baseline.
- Anti-aliased primitives per frame: aim under ~150 (rasterizer cost is
  roughly 4 to 10 µs each on a laptop).
- `sizeof(App)` ≤ 96 KB; the compile-time assert in `app.cpp` enforces it.

Each stage's `reviews/stageN/notes.md` records these numbers.

## World and the scene context

```cpp
struct World {
  Landscape land;  Cloud clouds[2];
  BuildState build;          // which static layers are in the cache; in-progress layer and its t
  Ambient amb;               // grass phases, stream offset, bird, wind
  FirePit fire;  Tent tent;  // stage 2 fire pit (placeholder until stage 4 wires the story)
  Hiker hiker;               // stage 3 module, placed by stage 4
  Prop props[PROP_MAX];      // stage 4
  Critter critters[CRITTER_MAX];   // stage 5
  Minors minors;             // stage 5
  Memory memory;             // stage 6, copied across beginLoop()
  LoopPlan plan;             // this loop's variant picks and major schedule (stage 4/6)
};
struct SceneCtx { App* app; World* w; Rng* rng; const Inputs* in; Renderer* r; float dt; };
```

Beats (`anim/scheduler.h`) get `SceneCtx&` and `t` in 0..1 and drive the
world; they never draw. `World` draws itself in `drawDynamic`.

## Hiker rig interface (stage 3; stage 4 scripts against it)

```cpp
enum PoseId { POSE_STAND, POSE_LOOK_AROUND, POSE_CROUCH, POSE_SIT, POSE_REACH_UP, POSE_CARRY,
              POSE_BLOW, POSE_YELP, POSE_LIE, POSE_WAVE, POSE_HANDS_ON_HIPS, POSE_SCRATCH_HEAD,
              POSE_HOLD_MUG, POSE_READ, POSE_COUNT };
enum CarryId { CARRY_NONE, CARRY_WOOD, CARRY_MUG, CARRY_BOOK, CARRY_BAG, CARRY_STICK, CARRY_ROCK, CARRY_JAR, CARRY_COUNT };
struct Hiker {
  Vec2 feet; int facing;           // feet point on the ground; facing +1 right, -1 left
  float height;                    // about 90 px
  bool visible;
  // pose blending, walk state, carried item, breathing phase: owned by hiker.cpp
};
void hikerInit(Hiker& h, Vec2 feet, int facing);
void hikerSetPose(Hiker& h, PoseId p, float blend_seconds);   // ease into pose; keeps walking off
void hikerWalkTo(Hiker& h, float x, float px_per_s);          // walk cycle until arrival, then POSE_STAND
bool hikerArrived(const Hiker& h);
void hikerCarry(Hiker& h, CarryId c);
void hikerTick(Hiker& h, float dt);                           // advances blend, walk, breathing
Rect hikerBounds(const Hiker& h);
void hikerDraw(const Hiker& h, Surface& s, const Palette& pal, const Rect& clip);
```

The hiker is a black silhouette (`ACTOR` slot) with the hat brim and pack as
its unmistakable features. Fire-side lighting is a palette matter (`ACTOR_LIGHT`
on the fire-facing edge), not a glow.

## Spine, minors, majors (stages 4 to 6)

- `spine.cpp` builds this loop's beat list from `LoopPlan` at `beginLoop()`:
  build-up, arrival, camp setup, firewood, fire, dinner, food bag, bear,
  close, with calm fillers between. Each spine beat has 2 or 3 variants,
  chosen by the loop RNG and recorded in `LoopPlan` so the preview can print
  them.
- `minors.cpp` runs beside the timeline: every 10 to 30 s pick from a weighted
  pool with a ring buffer of the last 6 picks; each minor is a small state
  machine over one or two critters or an effect (leaf, moth, bee).
- `majors.cpp`: 2 to 4 majors per loop placed in gaps between spine beats
  with a stillness beat before and after; weighted pool with a ring buffer;
  gags take arguments from the live scene (what was set down last, what is
  near the tree).
- `memory.cpp`: cairn height, mouse hole, raccoon has the shoe, last stolen
  item, loop count. Survives `beginLoop()`; the preview can print it.

## Preview support (stage 2 onward)

- `--every N`: render every Nth tick (generalizes `--fps`).
- `--log-beats`: print `tick=T beat=NAME` to stdout at each beat change, and
  the loop's variant picks at each loop start, so clips can be cut by tick.
- `--loops N`: simulate N loops with no rendering and print one line per loop
  (seed, spine variants, majors, memory), for the randomness table.

## Status

- Stage 2: built 2026-10-30, awaiting review. Build-up beat (13 s, 6 phases, 6 cache rebuilds per loop) and ambient layer (grass, stream highlights, fire and smoke, bird) in `world.*`, `buildup.*`, `ambient.*`. Ambient average 6.0 to 6.5 strips per frame, 0.135 ms/frame in the preview, `sizeof(App)` 58,600 bytes. Notes: `reviews/stage2/notes.md`.
- Stage 3: module built, not yet placed in the scene
- Stage 4: not started
- Stage 5: not started
- Stage 6: not started
