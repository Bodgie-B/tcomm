# Campsite screensaver: handoff after stage 1

Written 2026-10-30 at the end of stage 1, for the session that continues the
work. Read `CLAUDE.md`, then `docs/screensaver/CAMPSITE_SCREENSAVER_BRIEF.md`
in full, then this file. The brief is the source of intent; this file records
what has been built, what was decided along the way, and what comes next.

## State

- Stage 1 of the build order (brief section 15) is complete and reviewed.
- Everything lives on the branch `screensaver`, in `screensaver/`.
- The branch was built in a cloud session that could not push to GitHub
  (the Claude GitHub App is not installed on the `Bodgie-B` account). Mike
  pushes from his own computer. Check `git log origin/screensaver` to see
  what has reached GitHub; if the branch is missing there, Mike still has to
  push it.
- No pull request exists yet. Open one from `screensaver` to `main` as a
  draft and keep landing stages on the same branch.
- Nothing has run on the board. Every statement about the ESP32-S3 in this
  repo is unverified until stage 2.

## How to build and look at it

```
cd screensaver
mkdir -p build && cd build
cmake .. && make && ctest
./ss_preview --seed 11 --mood 2 --frames 500 --mood-sweep 2 6 --out frames
../tools/make_video.sh frames sweep.mp4 25
```

`ss_preview` is headless and writes PPM frames; `make_video.sh` needs ffmpeg.
`ss_window` (SDL2, with live keys listed in `screensaver/README.md`) builds
only when CMake finds SDL2 and has never been compiled against real SDL2.
`test_scene` also writes `build/landscape_{day,golden,dusk,night}.ppm`.

## What exists

| Path | What it is |
|---|---|
| `core/ss_platform.h` | Rules for core code, screen size, tick rate, strip size, `HostAlloc` |
| `core/ss_app.h` | The whole interface the panel firmware and the preview call |
| `core/gfx/surface.h`, `surface.cpp` | RGB565 surface with `origin_y` for strips, color helpers, two-plane blend |
| `core/gfx/raster.h`, `raster.cpp` | Exact-area anti-aliased polygon, circle, ellipse, capsule, rect fills; clipped; about 11 KB static scratch; not reentrant |
| `core/palette.h`, `palette.cpp` | 32 slots, six mood keys on a solar axis, `computePalette(mood, weather)` with overcast, fog and night-dim overlays |
| `core/anim/ease.*`, `rng.*`, `scheduler.*` | Easings, 256-entry sine table, sum-of-sines noise, PCG32, whole-tick beat Timeline with an event queue |
| `core/render/renderer.*` | Static layer cache (full frame, host large memory) plus 15 strips of 32 rows composited in a 51 KB strip buffer; dirty mask per strip |
| `core/scene/landscape.*` | Per-loop landscape geometry generated from the seed, and its static drawing |
| `core/scene/app.cpp` | `App`: inputs, Rng, Timeline, Renderer, Landscape, two drifting clouds, palette recompute thresholds, loop rollover |
| `sim/` | `preview_common.*` shared driver, `preview_main.cpp` headless, `window_main.cpp` SDL2 (unverified) |
| `tests/test_*.cpp` | Plain executables, run by ctest; all four pass |
| `tools/make_video.sh` | PPM frames to MP4 or GIF |

## Decisions made during stage 1 (beyond the brief)

- **Strips instead of dirty rectangles.** Grass, stream and fire will touch
  most of the lower screen every frame, so per-sprite rectangles would leave
  the ground stale. Each dirty strip is rebuilt from the cache in a small
  buffer and written to the framebuffer in one sequential run, which is what
  the PSRAM note in brief section 10 asks for. Only strips something moved in
  are processed (two clouds cost 5 strips per frame).
- **Palette curves, not fixed tables.** Mood is a float on a 0..6 axis
  (0 night, 1 dawn, 2 morning, 3 day, 4 golden, 5 dusk, 6 = 0). The port maps
  clock and sunrise/sunset to it; core never reads wall-clock time. Day to
  dusk, the Lookout sunset and fog are one mechanism. The palette is
  recomputed only when mood moves more than 1/48 or weather changes by more
  than 0.02 or night mode toggles, because a recompute rebuilds the whole
  cache.
- **Determinism is seed plus event log.** External events (doorbell, status,
  dog, weather) are queued on the Timeline with `post()` and drained by the
  scene with `takeEvents()` at the start of its tick, so a run replays from
  (seed, inputs, events). Later loops reseed from the loop's own RNG.
- **Whole-tick scheduler.** Beats last `ceil(duration * 25)` ticks; `t`
  runs from exactly 0 on a beat's first update to exactly 1 on its last, so
  final poses are reached. Nothing carries across beats.
- **Composition.** Horizon at y 272. Five sky bands with boundaries at
  0, 58, 118, 178, 228. Three ridges (bases 238, 262, 290, clamped to 314)
  from seeded sum-of-sines, a serrated far tree line on the near crest, a
  mid pine band, ground from y 318, the stream entering the right edge at
  y 352..392 and curving to the lower left through (470,420) (300,448)
  (120,476). The campsite clearing is x 200..460, y 335..425 and is clear of
  props; the stream clips its lower-right corner, so usable flat ground is
  left of about x 300 at full depth. The bag-hanging pine stands at x about
  560, base 340, height 170, with a bare branch leaving the trunk at y 302
  and reaching 82 px to the left (the food bag hangs from its tip). The
  bottom-right 140x60 (x >= 660, y >= 420) is reserved for the status chip
  and holds only ground and grass; a test enforces it across seeds and moods.
- **Sun and moon** follow an eight-segment path over the mood axis and set
  and rise behind the ridges (the sun sinks between mood 5 and 5.5, the moon
  rises between 5.5 and 6, and the reverse around dawn). Stars fade in within
  0.5 of night.
- **`App` is placed by the host** in `SS_APP_BYTES` (96 KB) of memory it
  supplies; `sizeof(App)` is about 57 KB, 51 KB of which is the strip buffer.
  That buffer must end up in internal SRAM for the strip design to pay off,
  so either the host puts `App` in internal SRAM or stage 2 adds an internal
  allocation hook to `HostAlloc`. Decide this when reading Waveshare's panel
  setup.
- **Build flags.** Core compiles with `-fno-exceptions -fno-rtti -Wall
  -Wextra -Wconversion -Wdouble-promotion -Wshadow -Werror -ffp-contract=off`.
  The last one is so PC and ESP32 float results can match; unverified on
  hardware.

## Creative plan (agreed with Mike)

Visual approach: one fixed camera, flat stacked silhouettes, actors designed
as black shapes first (the hiker about 90 px tall with a brimmed hat and a
tall pack, built from capsules on a jointed rig; animals 20 to 50 px, four to
six primitives each with one unmistakable silhouette feature). Fire and tent
light are flat warm pools done by palette index, never glow.

Variety levers, all cheap: real time and weather pick the palette and spine
variant; a per-loop seed drives weighted picks with a ring buffer of recent
picks; gags take arguments from the live scene (the thief steals what was set
down last, the bear picks a tactic from what is near the tree); each critter
gets a personality dial per loop; timing, entry side, cloud shapes and bird
paths jitter within bounds; and a small world state survives across loops
(the cairn from last loop is still there a bit shorter, the mouse hole stays
until patched, the raccoon returns last loop's shoe and takes something else).

Five scenes of our own, beyond the brief's pools, to build in stages 5 to 7:

1. Shadow theater: his silhouette in the lit tent makes hand-shadow animals;
   the real animals gather to watch; he makes a bear; the real bear's
   silhouette appears beside the tent; everyone scatters but him.
2. The ranger on the ridge: a tiny figure on the far ridge trades lantern
   blinks with the hiker. This is also the doorbell reaction: the lantern
   blinks, the hiker stops and waves toward the screen edge, the chip pulses.
3. Echo: he shouts at the valley (an arc glyph), the echo comes back, a
   smaller one comes back, then a tiny one from the squirrel on the stump.
4. Rain shelter: animals slide in under the tent awning one by one and he
   scoots over each time; the bear arrives; everyone leaves except the frog.
5. Firefly jar: he catches fireflies, the light pool shrinks and moves with
   the jar, he looks for a long beat, opens the lid, the light spreads back.

## Open items from the stage 1 review (do later, in order of value)

1. Rebuild the cache strip by strip through the strip buffer instead of
   drawing the whole static scene straight into PSRAM; measure on the board.
2. `appStart` does not check `storage` alignment; add
   `if ((uintptr_t)storage % alignof(App)) return nullptr;`.
3. Palette tuning on the real panel: night ridge steps are small (silhouettes
   may vanish), the stream is the loudest element in bright moods, several
   WATER and SMOKE slots are guesses. All values are in one table in
   `core/palette.cpp`.
4. The moon still appears over about half a mood step; a radius or color fade
   at its low endpoint would soften it further.
5. Preview `mkdir -p` splits only on `/`; a Windows drive path would fail.
   The SDL window screenshot name uses the first seed, not the current loop's.
6. Large circles (radius over about 124) exceed 0.1 px facet error because of
   the 64-point cap; fine for the sun and moon as sized now.

## Stage 2: on the board

Build the same landscape on the panel with the layer cache, strips and a
frame timer. Done when a moving element runs at a steady 20+ fps with no
tearing or picture shift, and frame time, PSRAM use and flash use are logged.

Needed from Mike before starting:

- The Waveshare example he flashed from (folder name or link), so the RGB
  panel init (bounce buffers, double framebuffer, pixel clock, RGB565 byte
  order) is read rather than guessed. The brief marks all of this unverified.
- Arduino sketch or ESP-IDF project for `port_esp32/`. The handoff brief
  leans Arduino (Waveshare's examples) with application logic kept
  hardware-free, which `core/` already is.
- The COM port, confirmed before every flash. Never erase flash without
  asking.

`port_esp32/` is empty. It needs: PSRAM allocation for the cache through
`HostAlloc`, placement of `App` (see the strip buffer note above), a
25 Hz tick task at lower priority than radio and touch that skips a frame
rather than blocks, the framebuffer handoff with or without LVGL, and a
serial log of frame time and memory. The touch reveal, status chip and real
inputs are stage 8, not stage 2.

## Working rules carried over

- Never commit Wi-Fi passwords, ESP-NOW keys, tokens, or the house's
  latitude and longitude. `config_local.*` and `secrets.*` are ignored.
- Confirm the COM port with Mike before every flash. Never erase flash
  without asking.
- Mark anything unverified, in code comments and in reports.
- Stop at the end of each stage with a summary and something Mike can look
  at (frames or a video from the preview, or a phone video of the panel).
- Mike works with a chief/specialist/worker split: Fable plans and gates,
  Opus takes the hard or risky pieces and reviews diffs it did not write,
  Sonnet does well-specified implementation and tests. Every stage diff gets
  an independent review before it is committed.
