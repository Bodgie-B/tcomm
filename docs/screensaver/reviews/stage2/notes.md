# Stage 2: scene build-up and the ambient layer

Written 2026-10-30. Not committed; the chief reviews and commits. No video in
this folder yet (the chief cuts those).

## What was built

New files in `screensaver/core/scene/`:

- `world.h/.cpp`: `World` (landscape, clouds, `BuildState`, `Ambient`,
  `FirePit`, sorted ground actors, previous bounds per element) and the new
  `SceneCtx {app, w, rng, in, r, dt}`. `worldTick` runs after the timeline:
  ambient step, actor sort, dirty marks, cache invalidation at build-up phase
  boundaries. `worldDrawDynamic` is the one place that holds the per-strip draw
  order.
- `buildup.h/.cpp`: the `build` beat (13 s) and the layer mechanism.
- `ambient.h/.cpp`: swaying grass, stream highlights, fire flicker and smoke,
  the distant bird.

Changed: `landscape.h/.cpp` (layered static drawing with build poses, stream
sampling, mushroom, clouds drawn at integer x), `app.cpp` (owns `World` by
value; two beats: `build` 13 s, `ambient` 587 s; loop still 600 s),
`renderer.h` (`invalidateCache()`), `ss_app.h` (added `appBuildPhase`,
`appAppBytes`; no existing signature changed), `tests/test_scene.cpp`.

### Build-up (13 s)

| Phase | Seconds | What moves |
|---|---|---|
| sky | 0 to 1.8 | Flat `SKY4` field over the ground, short hold, then every sky slot (bands, sun or moon, stars, clouds) eases from `SKY4` to its color |
| ridges | 1.8 to 3.8 | Far, mid, near ridge rise from behind the ground line, staggered, `easeOutCubic` |
| trees | 3.8 to 6.6 | Far tree teeth and mid pines stand up left to right with `easeOutBack` (height grows from the base, width a little narrow until full height); then the tall foreground pine, the camp pine and the short foreground pine |
| stream | 6.6 to 8.8 | Water and far bank roll in from the right edge to the lower left along the centerline, leading edge a rounded tongue (0.6 point units, about 40 px) |
| plants | 8.8 to 11.6 | Rocks and tufts pop in scattered order (`easeOutBack`, 0.34 s each), a beat of stillness, then the mushroom: cap peeks, holds, pops with overshoot, cap wobbles to rest |
| fire | 11.6 to 13 | Six stones laid one by one with a pop, a pause, then the fire catches and grows to full over 1.8 s |

Mechanism, as the spec asked: static layers in cache order (`BuildLayer`:
sky, ridges, far trees, mid pines, ground, stream, camp pine, rocks,
foreground pines, tufts, mushroom). The cache holds only built layers below
the first active one; the active layers and every built layer in front of
them are drawn per strip, so depth stays right (the ground covers rising
ridges, the foreground pines stay in front of the stream rolling in) with no
special-case clipping. The cache is rebuilt when that set changes: 6 rebuilds
per loop (loop start plus 5 phase boundaries), never per frame.

### Ambient layer

- **Grass:** 14 of the existing tufts sway (flag `LsTuft::dyn`; the static
  draw skips them). Picked by farthest-point sampling over the tufts (y
  weighted double), so sway shows at the clearing edges, both stream banks and
  the foreground. Tufts that could lean into the reserved corner (x > 625,
  y > 400) or onto the mushroom are never picked. Each blade is a 5-point bent
  polygon whose tip moves `sumSines(wind_t*0.32 + x*0.0035, 3)` px with a 20 s
  gust swell; wind blows left, the way the clouds drift, so gusts roll right
  to left. Advances every 2nd tick.
- **Stream flow:** 9 `WATER_LIGHT` dashes (2 px capsules, 9 to 18 px long)
  ride the centerline in lanes within 0.3 of the half width, at 26 px/s where
  the stream is 40 px wide and slower where it is wider; they shimmer in
  length and wrap off-screen at both ends. About 1 px per tick, so no strobing.
  The static ripples are no longer drawn (still generated so each seed keeps
  its stage 1 place).
- **Fire pit:** `FirePit` in `world.h`, at x 238 to 262, y 393 to 403 in the
  clearing. Back stones, a `FIRE` pool of four tongues and three valleys (each
  height and lean its own sum of sines, top octave 4.3 Hz), a `FIRE_CORE` pool
  at half size, front stones. About 32 px tall at full intensity.
- **Smoke:** 5 `SMOKE` ellipses on a 7 s cycle, rising 55 px and trailing left
  with the wind, growing fast then shrinking to nothing (no alpha). Advances
  every 2nd tick.
- **Bird:** a two-capsule V in `TREES_FAR`, 11 px wide, crossing at y 61 to
  149 on a slow sine path at 28 to 42 px/s, flapping at 2.6 beats per second
  with glides. First crossing 15 to 45 s after the build-up ends, then every 40
  to 90 s start to start, direction random. Seed 11: crossings start 28 s and
  77 s after the build.

## Numbers

| Measure | Value | Budget |
|---|---|---|
| Ambient strips per frame, ticks 400 to 900, seed 11 | avg 6.04 (min 4, max 9) | ≤ 9 (spec proposes ≤ 8) |
| Same, seed 7 / seed 42 | avg 6.12 / 6.47 (max 11 / 10) | |
| `ss_preview --seed 11 --mood 3 --frames 400` (build-up then ambient) | 0.135 ms/frame avg (3 runs: 0.130, 0.135, 0.135) | ≤ 2 ms |
| Same with `--skip 400` (ambient only) | 0.071 ms/frame | |
| `sizeof(App)` | 58,600 bytes | ≤ 98,304 |
| Cache rebuilds per loop | 6 | a handful |

Machine: Mark's Mac (Apple silicon), same build dir as the stage 1 baseline
(0.12 ms/frame there for clouds only). Primitives per frame were not counted;
by construction the ambient scene is about 75 primitives (42 grass blades, 9
dashes, 14 cloud ellipses, 2 flame pools, 6 stones, 5 smoke puffs, 2 bird
wings), each rasterized once per dirty strip it crosses.

Test output (`ctest`: 5/5 passed, including `test_hiker` from the stage 3
agent):

```
sizeof(App) = 58600 bytes (budget 98304)
seed 11: ambient strips per frame over ticks 400..900: avg 6.04, min 4, max 9
seed 7: ambient strips per frame over ticks 400..900: avg 6.12, min 3, max 11
seed 42: ambient strips per frame over ticks 400..900: avg 6.47, min 4, max 10
mood 3 -> 5 rebuild wrote 15 strips
loop rollover at tick 15000 (nominal 15000)
test_scene: all passed
```

`test_scene` now checks: determinism at ticks 50, 300 and 700 (two Apps, same
seed; one renders every tick, the other only at the checkpoints, so this also
proves incremental equals full); incremental equals full at 23 checkpoints
across every build phase and its boundaries plus a 7000-tick ambient run; the
strip budget above; the reserved corner holding only `GROUND`,
`GROUND_DARK` or `GRASS` for 60 seeds over all moods after the build, and for
3 seeds x 2 moods at ticks 0, 100, 250 and 500; palette rebuilds after the
build; loop rollover at exactly tick 15000; `sizeof(App)`.

## Decisions beyond the brief

1. **The cache holds only layers below the first active one** (not "built
   layers plus the in-progress one drawn on top"). Drawing built front layers
   dynamically during a phase keeps depth right without re-blending their
   anti-aliased edges twice.
2. **Ground is there from the first tick**, and the lowest sky band runs down
   to the ground line, so the opening frame is a flat `SKY4` sky over flat
   ground and the ridges have something to rise from behind. After the build
   the ridges cover that band, so the finished image is unchanged.
3. **Near trees stand up too.** The camp pine and both foreground pines stand
   up at the end of the tree phase, after the background has filled in.
4. **Swaying tufts are sorted with the ground actors by base y** instead of
   being drawn last. A tuft on the clearing's back edge (y ~330) would
   otherwise draw over a hiker standing in front of it in stage 3/4.
5. **Clouds are drawn at `floor(x)`**, so their image only changes when they
   dirty strips; framebuffers stay identical to a full render.
6. **Mushroom colors:** `WATER_LIGHT` stem (pale by day, dim at night),
   `FIRE` cap with two `FIRE_CORE` spots. About 14 px wide and tall, placed in
   the foreground at x 470 to 630, y 450 to 470, off the water and away from
   rocks.
7. **The fire pit is fully dynamic** (stones too), so back stones sit behind
   the flames and front stones in front. It is lit at the end of the build for
   review; `FIRE_EMBERS` exists (eases to 0.25) for stage 4.
8. **The bird is suppressed during the build-up** and its schedule counts from
   the build's end.
9. **Highlight lanes stay within 0.3 of the half width.** By my estimate from the
   generator, rocks reach up to about 9 px into the water and near-bank tufts
   up to 13 px (not measured); highlights are
   drawn over the cache, so keeping them central stops them crossing those.

## Open

- **The one-hour run** from START_HERE (`--frames 90000 --every 25`) was not
  done: `--every` is a `sim/` change and `sim/` belonged to another agent this
  stage. The tests cover 7000 ambient ticks with full-render comparisons and a
  full 15,000-tick loop with rollover.
- **Preview flags** from the spec (`--every`, `--log-beats`, `--loops`) are not
  added, for the same reason. `appBuildPhase` is there for the HUD or logs.
- **Smoke look:** the puffs now overlap into a column but still read as
  slightly beaded. Worth a look in the clip; wider-than-tall ellipses or a
  sixth puff are the cheap next steps.
- **Max strips per frame** reaches 10 to 11 when a cloud step, the bird and
  the grass sub-tick fall on the same tick. The average is the budget; the
  peaks matter on the board (stage 7).
- **The camp pine's tip (y 170) can sit under the lower cloud** (bottom up to
  y ~172). This is from stage 1 and was not changed.
- **Night:** the bird in `TREES_FAR` is barely visible against the night sky,
  which is probably right. The `FIRE` mushroom cap keeps its bright orange at
  night (the fire slot does not dim); it is small, but check it on the panel
  against the night rule.
- All timings are PC numbers; nothing here is verified on the ESP32-S3.
