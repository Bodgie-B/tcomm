# Campsite screensaver: start here

Written 2026-10-30 for the Claude Code session that picks up the screensaver
on Mike's Windows laptop. Read this file first, then `CLAUDE.md`,
`docs/screensaver/CAMPSITE_SCREENSAVER_BRIEF.md` in full, and
`docs/screensaver/SCREENSAVER_HANDOFF.md`. This file says what is done, what
was verified and by whom, who you are working with, and the agreed order of
work from here. Where it disagrees with the older handoff on process or stage
numbering, this file is newer; on design intent, the brief wins.

A human-readable version of the Stage 1 results, with renders, is in
`docs/screensaver/stage1-report/index.html`. Its images are the reference
look: use them to compare Windows preview output and, later, panel photos.

## The plan has been reordered: animation first, board second

The brief's build order (section 15) put "on the board" at stage 2, right
after the foundation. Mike and Mark changed that on 2026-10-30. The new order:

| Stage | What | Where it runs |
|---|---|---|
| 1 | Foundation: rasterizer, palettes, scheduler, landscape, preview | PC. **Done.** |
| 2 | Scene build-up and the ambient layer | PC preview. **Done.** |
| 3 | Hiker rig | PC preview. **Done.** |
| 4 | Story spine, with variants | PC preview. **Done.** |
| 5 | Critters and the minor-effect scheduler | PC preview. **Done.** |
| 6 | Surprise pool, variety system, per-loop seed | PC preview. **Done.** |
| Review gate 1 | Mike approved the evening spine on the draft PR (2026-10-31). **Done.** |
| 6b | Morning and midday spines, the weather effects, the Lookout (`SPINES_WEATHER_LOOKOUT_SPEC.md`) | PC preview. **Done.** |
| **Review gate 2** | **Now.** Mike and Mark watch `docs/screensaver/reviews/stages2-6/index.html` (the evening, morning and midday spines beat by beat, every weather kind by day and night, the Lookout) and decide. Mike's first pass (1 Nov): all good; two Lookout changes asked for and built (spec section 5b: the sun sinks, the stars come out one by one), then handoff. Fixes are preview work. Only after that does the work move to hardware. | |
| 7 | On the board (the brief's old stage 2) | ESP32-S3 panel |
| 8 | Integration hooks: status chip, doorbell, touch reveal, real inputs (clock, OUTSIDE weather, the sunset window from the untracked lat/long) | Panel |
| 9 | Night mode and weather on the panel: the night rule and the palette-step costs measured in a dark room (the systems exist since 6b) | Panel |
| 10 | Lookout on the panel, gated to real sunset (the scene exists since 6b) | Panel |
| 11 | Optional ambient sound, off by default | Panel |

Why: the brief already makes the PC preview the main development loop, the
creative work is the part Mike and Mark want to judge, and nothing about the
animation needs the panel to exist. The cost of this order is that hardware
limits show up late. The "budget discipline" section below is how that risk
is kept small. Stage 7 is unchanged from the old plan and is written out
below, so nothing is lost if someone wants to run a board smoke test early.

The done-when for each stage is in brief section 15 and repeated below.

## Who is working on what (2026-10-31)

Mark is carrying the screensaver forward on his Mac with Claude (Fable as
chief, Opus and Sonnet subagents) and pushes to `screensaver`. Mike reviews
each stage from the clips in `docs/screensaver/reviews/` and from the draft
pull request, and answers through GitHub issues on `Bodgie-B/tcomm` (see
issue 1 for his stage 5b review). Mike's laptop takes over at stage 7, when
the board comes in. Until then a session on Mike's machine should only read
and comment, not edit the scene code.

## Who you are working with

- **Mike** owns the repo (GitHub `Bodgie-B`), the Waveshare panel and the
  Windows laptop described in `docs/CLAUDE_CODE_HANDOFF.md` (one USB-C port;
  the board has shown up as COM5). **Mark** (`mtburbridge`) built Stage 1
  with Claude and reviews stage output alongside Mike. Stage reviews are
  joint: send both of them something to watch.
- Mike runs Claude Code with Opus. Claude handles the git work and keeps the
  explanations plain. Keep the one rule from the earlier chief/specialist
  setup that matters: **every stage diff gets an independent review before it
  is committed** (spawn a reviewer subagent that did not write the code; give
  it the diff and the brief, not your conclusions).

## Git rules for this repo

- Work on the `screensaver` branch. It already exists on GitHub. Never commit
  to `main` directly, never force-push, never rewrite pushed history.
- Before the first commit, check `git config user.name` / `user.email`; if
  unset, ask Mike what to use.
- Commit in small, reviewed pieces with plain-English messages. Say what is
  being pushed before pushing. The first push may open a GitHub sign-in
  window (Git Credential Manager); Mike signs in as `Bodgie-B`.
- No pull request exists yet. Open one from `screensaver` to `main` **as a
  draft** once Stage 2 work starts landing, and keep later stages on the same
  branch. If `gh` is not installed, use GitHub's "Compare & pull request"
  button.
- The repo is public. Never commit Wi-Fi passwords, ESP-NOW keys, tokens, the
  house's latitude/longitude, or anything from Mike's other projects. Use
  `config_local.*` / `secrets.*` (already ignored).
- Preview output (`*.ppm`, `*.mp4`, `*.gif`, `*.png`) is ignored everywhere
  **except** `docs/screensaver/stage1-report/img/` and
  `docs/screensaver/reviews/`. Curated review videos go in `reviews/`; raw
  frame dumps never go in git.

## Verified state as of 2026-10-30

Code: `screensaver` branch, 33 files, about 5,000 lines, in `screensaver/`.

Verified on macOS (Apple clang 21, SDL2 2.32) on 2026-10-30 by rebuilding
from GitHub with the same flags CMake uses:

- `core/` compiles clean under `-fno-exceptions -fno-rtti -Wall -Wextra
  -Wconversion -Wdouble-promotion -Wshadow -Werror -ffp-contract=off`.
- All four tests pass: `test_raster` (47/47), `test_palette`, `test_anim`,
  `test_scene` (clouds dirty 5 strips/frame, mood jump rebuilds 15, loop rolls
  over at tick 15000 = 10 min).
- `ss_preview` renders correctly at every mood, with overcast, fog and night
  mode, and for several seeds (see the report images). About 0.24 ms per
  frame on an Apple Silicon Mac, which is the baseline for the budget below.
- `ss_window` compiles and links against real SDL2 and ran for several
  seconds without crashing. Its window contents and keys were **not** checked.
  The README still calls it unverified; update that once Mike has used it.
- That machine had no `cmake`, so the CMake files were not exercised; the
  compile/link commands mirrored `screensaver/CMakeLists.txt`.

Unverified:

- **Any Windows build.** `CMakeLists.txt` passes GCC/Clang-style flags, so
  MSVC (`cl.exe`) will reject them. See step 1.
- **Everything on the ESP32-S3**: speed, memory placement, panel setup,
  tearing, colors on the real screen.
- Whether PC and ESP32 float results match bit for bit (the reason for
  `-ffp-contract=off`).

## Budget discipline for the preview stages

Because the panel is not measured yet, every preview stage has to stay inside
a budget that is a stand-in for the hardware. These numbers are **proposed**,
not measured; the first board run (Stage 7) replaces them with real ones.
Until then they are the rule.

- **Strips per frame.** Report average and worst strips written per frame
  (`appRender` returns it; `test_scene` already prints it). Proposed: average
  under 8 of 15 during ambient-only stretches, worst 15 only during scene
  build-up, the fire, and major effects. Grass, stream and fire will touch the
  lower strips every frame; that is expected and is why strips were chosen.
- **PC frame time.** Keep `ss_preview`'s average under about 2 ms per frame
  on Mike's laptop (compare against the 0.24 ms Stage 1 baseline on the same
  machine, not across machines). The ESP32-S3 at 240 MHz with no cache help
  from PSRAM is guessed at 30 to 80 times slower than a laptop core; 2 ms on
  the PC is the rough edge of a 40 ms frame at 25 fps. Unverified. Treat a
  stage that doubles PC frame time as a design problem to fix now, not later.
- **Memory.** `sizeof(App)` must stay under `SS_APP_BYTES` (96 KB) and the
  compile-time check in `app.cpp` must keep passing. No new large buffers;
  per-frame working data stays small enough for internal SRAM. The static
  cache is the only full-frame buffer.
- **Rasterizer cost.** `test_raster` prints microseconds per shape. Keep the
  count of anti-aliased primitives per frame in mind: a 90 px hiker of 12
  capsules plus 6 critters of 5 primitives each is about 50 shapes a frame,
  which is fine on the PC numbers; a grass field drawn as 400 individual
  blades is not. Draw grass as a few wide polygons with a sine offset, not
  as blades.
- **No new dependencies in `core/`.** Plain C++17, no STL containers that
  allocate, no floating-point `printf`, no platform calls.

Log these numbers in each stage's review notes so the trend is visible.

## Reviews: what each stage hands back

Each stage ends with a folder `docs/screensaver/reviews/stageN/` holding:

- `notes.md`: what was built, the budget numbers, decisions made, what was
  left open, and the exact `ss_preview` commands used, so Mark can reproduce
  any clip on his Mac.
- Two or three short MP4s from `tools/make_video.sh`: an 8x fast-forward of
  a whole loop (`--fps` trick: render every 8th tick) and one or two real-speed
  clips of the stage's key beats. Keep the folder under about 15 MB.

Then stop and summarize for Mike before starting the next stage. Mike and
Mark will comment; fold the comments into the next stage rather than reopening
the finished one, unless they ask.

## Step 0: orient (first session, before changing anything)

1. Confirm the checkout is on `screensaver` and clean, and that
   `git log origin/screensaver -1` matches local.
2. Tell Mike in a few sentences what Stage 1 built and what Stage 2 is now,
   and point him at the report page.
3. Ask him to confirm the reordered plan above is what he wants. If he would
   rather see it on the board first, the old Stage 2 is written out below as
   Stage 7; swap the order and carry on.

## Step 1: get the PC preview working on Windows

This is the development loop for Stages 2 to 6, so it comes first.
Recommended route: **MSYS2, UCRT64 environment** (GCC, CMake, Ninja, SDL2 and
ffmpeg from one package manager; the existing flags work unchanged). Mike
installs MSYS2 from msys2.org; then the packages are:

```
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-ffmpeg
```

Claude Code's shell on Windows is Git Bash, not MSYS2. Run builds through
`C:/msys64/usr/bin/bash.exe -lc "..."` with `MSYSTEM=UCRT64`, or put
`C:\msys64\ucrt64\bin` on PATH for the session. Unverified; adjust to what
works and write down what did.

Then:

1. Fix open item 5 from the handoff first: `mkdirP` in
   `sim/preview_main.cpp` splits only on `/`, so `C:\...` or `C:/...` output
   paths can fail. Accept `\` as a separator and skip the drive prefix. Use
   relative `--out` paths until fixed.
2. `cmake -G Ninja ..`, build, `ctest`. All four must pass.
3. Determinism check: `./ss_preview --seed 11 --mood 4 --frames 1 --out f_golden`
   and compare with `docs/screensaver/stage1-report/img/golden.png` (convert
   the PPM with ffmpeg and compare decoded pixels, not file bytes). Identical
   pixels mean GCC on Windows matches Clang on macOS. A difference is a
   finding to record, not a blocker.
4. Have Mike run `ss_window` and try the keys in `screensaver/README.md`;
   update the README's "unverified" note with what actually worked.
5. Add a review-video mode to the preview if it is not already easy: a
   `--every N` option (render every Nth tick) makes the 8x loop overview
   cheap. Keep `--fps` meaning what it means now.

If MSYS2 is a struggle, WSL2 (Ubuntu) is the fallback for the preview.

Before downloading any toolchain or Waveshare's examples, tell Mike what will
be downloaded and from where.

## Stage 2: scene build-up and the ambient layer

Brief sections 4, 6 (beat 1) and 9. Build, in the preview:

- **Build-up (spine beat 1).** The sky fades in; background trees stand up
  from the ground with an overshoot and settle; the stream rolls in from
  right to left; small plants pop up in the foreground, the last one a
  mushroom. Each as a beat on the Timeline, with easing from `anim/ease`.
- **Ambient layer, always on.** Grass sway (wide polygons with a sine
  offset, not blades), stream flow (a moving highlight pattern by palette
  index, not transparency), cloud drift (exists), fire flicker on a
  placeholder fire pit in the clearing (a flat warm pool that changes shape,
  by palette index, never a glow), and a distant bird now and then.
- The cache-vs-dynamic split: anything that moves every frame leaves the
  static cache and is drawn per strip. Work out the per-strip draw order
  (far to near) and keep it in one place.

Done when: it runs for an hour in the preview without a glitch (use
`--frames 90000 --every 25` and scan the output, plus a shorter real-speed
run), and the budget numbers are in the notes. Open items 1, 2 and 4 from the
handoff (cache rebuild path, alignment check, moon fade) fit here if cheap.

## Stage 3: the hiker rig

Brief section 9 and the "Creative plan" in the handoff. About 90 px tall,
brimmed hat, tall pack, built from capsules on a jointed rig, drawn as a
black silhouette first. Walk cycle, idle breathing, a set of poses (stand,
look around, crouch, sit, reach up, carry, blow on fire, yelp, sleep). Two
or three frames of anticipation before each action, overshoot and settle.

Done when: his silhouette reads clearly at the sizes the scenes use, in
every mood including night, in a contact sheet of all poses and a walk-cycle
clip. Sizes and pose names go in `notes.md`.

## Stage 4: the story spine

Brief section 6, beats 2 to 9: arrival, camp setup (bent peg, rock), firewood
(leaves and returns), fire (burn, yelp, stream), dinner, food bag, bear, close.
Each beat gets two or three variants, picked per loop from the seed. The tent
light and the hiker's shadow inside it are palette-index pools. Add the
morning variant of the spine (wake in fog, coffee, break camp) if time
allows; otherwise note it as open.

Done when: a full loop plays start to finish in the preview with the pacing
rules from section 4 honored (real 20 to 40 s pauses, a beat of stillness
around each major), and a 10-minute loop at 8x is in the review folder.

## Stage 5: critters and the minor-effect scheduler

Brief section 8 (fauna, vegetation, bathroom break) and section 4's minor
effects layer. Animals are 20 to 50 px, four to six primitives each with one
unmistakable silhouette feature. Start with the chipmunk (recurring runner),
squirrel, frog, deer, raccoon, owl and bird; the bear exists from Stage 4.
Minor effects fire every 10 to 30 s from a weighted pool with a ring buffer
of recent picks. Each critter gets a personality dial per loop.

Done when: ten minutes of ambient plus minors, with no major effects, feels
calm and alive and never busy, by Mike's and Mark's judgment of the clip.

## Stage 6: surprise pool, variety system, per-loop seed

Brief section 13 and the "Variety levers" in the handoff. Majors from the
comic and calm pools (two to four per loop, randomized gaps, stillness around
each), the five original scenes from the handoff where they fit (shadow
theater, ranger on the ridge, echo, rain shelter, firefly jar), gags that take
arguments from the live scene, and the small world state that survives across
loops (the cairn, the mouse hole, the raccoon's shoe).

Done when: ten loops in a row in the preview show no two alike in their
majors and spine variants, shown as a table in `notes.md` plus clips of the
best three loops.

## Review gate

Mike and Mark watch several whole loops (the 8x overviews plus real-speed
clips, or `ss_window` live). Questions to answer: does the look hold up, is it
calm, is it funny, does it ever feel like a clock. If yes, go to Stage 7. If
not, the fixes are preview work and stay in the preview stages.

## Stage 7: on the board (the brief's old stage 2)

Done when: the scene runs on the panel at a steady 20+ fps with no visible
tearing or picture shift, and frame time, PSRAM use and flash use are logged.

This stage overlaps with TComm's own build-order step 1 ("Bring-up") in
`docs/CLAUDE_CODE_HANDOFF.md`. Do the toolchain and display-demo part once
and record it so it counts for both.

Needed from Mike: the Waveshare example he flashed from (folder name or
link); a decision, or permission to decide, between Arduino and ESP-IDF; the
board plugged in, with the COM port confirmed before each flash.

1. **Choose the board toolchain, together with Mike.** This choice is for
   the whole TComm firmware, so make it once and record it in
   `docs/CLAUDE_CODE_HANDOFF.md`. The main brief leans **Arduino** starting
   from Waveshare's 4.3C examples, with app logic kept hardware-free (which
   `core/` is). The screensaver needs ESP-IDF's `esp_lcd` RGB panel API
   (bounce buffers, framebuffer count, pixel clock, direct framebuffer
   access, vsync callbacks); Arduino-ESP32 3.x sits on ESP-IDF 5.x and can
   call `esp_lcd` directly, unverified for Waveshare's example. Arduino snag:
   a sketch cannot easily compile sources outside its folder; make
   `screensaver/core` an Arduino library (`library.properties`) or copy it
   into the sketch's `src/` with a script. Do not duplicate `core/` in git.
   Recommendation: Arduino if Waveshare's Arduino LCD example uses `esp_lcd`
   and exposes bounce-buffer settings; otherwise ESP-IDF. Read their code
   first; tell Mike the choice and why in two sentences.
2. **Read the panel setup, then flash a known-good demo.** Record in
   `SCREENSAVER_HANDOFF.md`: pixel clock, timings, bounce buffer size,
   framebuffer count, RGB565 byte order, whether LVGL is in the loop, PSRAM
   mode. Build and flash Waveshare's own display demo unchanged. **Confirm the
   COM port with Mike first. Never erase flash without asking.** Download mode
   if auto-reset fails: hold BOOT, unplug and replug USB-C, release, flash,
   press RESET. Factory restore is in `docs/CLAUDE_CODE_HANDOFF.md`; never
   open the factory firmware's AIChats app.
3. **Build `screensaver/port_esp32/`.** `HostAlloc` backed by
   `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for the static cache; `App`
   storage (`SS_APP_BYTES`) in internal SRAM via `heap_caps_aligned_alloc`
   with `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` (if tight, add an internal
   hook to `HostAlloc` for just the strip buffer); a 25 Hz tick task at lower
   priority than radio and touch that skips a frame rather than blocks;
   `appRender` into the panel framebuffer, `appInvalidate` after anything
   else touches it; a serial log every few seconds of average and worst
   frame time, strips per frame, free internal heap, free PSRAM, flash use.
   Fixed inputs are fine. Then measure handoff open item 1 (cache straight
   into PSRAM vs. strip by strip) and keep the faster one.
4. **Finish.** Run at least one full loop; check the log for spikes. Ask
   Mike for a phone video including dusk or night. Replace the proposed
   budget numbers above with measured ones. Palette tuning (open item 3) is
   a one-table job in `core/palette.cpp`; do it only if asked. Independent
   review, commit, push, update the draft PR, append a Stage 7 section to
   `SCREENSAVER_HANDOFF.md`, and update this file's "Verified state".

## Stages 8 to 11

As in brief section 15: integration hooks (status chip, doorbell reaction,
touch reveal under half a second, wake-only touch, real inputs with fakes);
night mode and weather, reviewed in a dark room on the panel; the Lookout
scene gated to real sunset; optional ambient sound, off by default. Plan
each in detail when its turn comes, in this file.

## Working rules (carried over, non-negotiable)

- Confirm the COM port with Mike before every flash. Never erase flash
  without asking.
- Mark anything unverified in code comments and in reports. Do not fill gaps
  with guesses.
- Ask before any purchase suggestion or download.
- Stop at the end of each stage with a short summary and something Mike and
  Mark can watch.
- Keep `core/` free of platform calls; it must build unchanged on PC and
  ESP32.
- Every stage diff gets an independent review before it is committed.
