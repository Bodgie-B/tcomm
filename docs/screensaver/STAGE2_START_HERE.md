# Campsite screensaver: start here (Stage 2)

Written 2026-10-30 for the Claude Code session that picks up the screensaver
on Mike's Windows laptop. Read this file first, then `CLAUDE.md`,
`docs/screensaver/CAMPSITE_SCREENSAVER_BRIEF.md` in full, and
`docs/screensaver/SCREENSAVER_HANDOFF.md`. This file says what is done, what
was verified and by whom, who you are working with now, and the recommended
path through Stage 2. Where it disagrees with the older handoff on process,
this file is newer; on design intent, the brief wins.

A human-readable version of the Stage 1 results, with renders, is in
`docs/screensaver/stage1-report/index.html`. Its images are the reference
look: use them to compare Windows preview output and, later, panel photos.

## Who you are working with now

- **Mike** owns the repo (GitHub account `Bodgie-B`), the Waveshare panel and
  the Windows laptop described in `docs/CLAUDE_CODE_HANDOFF.md` (one USB-C
  port, board has shown up as COM5). Stage 1 was built by his sibling with
  Claude in a cloud session; Mike is taking it over from here.
- **Mike is not used to git or GitHub.** You handle all git work. Explain each
  step in one plain sentence before you do it, never assume he knows a git
  term, and tell him exactly what to click or type when something needs his
  hands (a GitHub sign-in window, holding BOOT, replugging the cable).
- Earlier sessions used a Fable-chief / Opus / Sonnet split. Mike runs Opus.
  Keep the one rule from that setup that matters: **every stage diff gets an
  independent review before it is committed** (spawn a reviewer subagent that
  did not write the code, give it the diff and the brief, not your
  conclusions).

## Git rules for this repo

- Work on the `screensaver` branch. It already exists on GitHub. Never commit
  to `main` directly, never force-push, never rewrite pushed history.
- Before the first commit, check `git config user.name` / `user.email` are set;
  if not, ask Mike what to use.
- Commit in small, reviewed pieces with plain-English messages. Push only
  after telling Mike what is being pushed. The first push will probably open
  a GitHub sign-in window (Git Credential Manager); tell him to sign in as
  `Bodgie-B` before it appears.
- No pull request exists yet. Open one from `screensaver` to `main` **as a
  draft** once Stage 2 work starts landing, and keep later stages on the same
  branch. If `gh` is not installed, walk Mike through GitHub's "Compare & pull
  request" button instead.
- The repo is public. Never commit Wi-Fi passwords, ESP-NOW keys, tokens, the
  house's latitude/longitude, or anything from Mike's other projects. Use
  `config_local.*` / `secrets.*` (already ignored).

## Verified state as of 2026-10-30

Code: `screensaver` branch, commit `28955b9` plus the docs commit that added
this file. 33 files, about 5,000 lines, in `screensaver/`.

Verified on macOS (Apple clang 21, SDL2 2.32) on 2026-10-30 by rebuilding
from GitHub with the same flags CMake uses:

- `core/` compiles clean under `-fno-exceptions -fno-rtti -Wall -Wextra
  -Wconversion -Wdouble-promotion -Wshadow -Werror -ffp-contract=off`.
- All four tests pass: `test_raster` (47/47), `test_palette`, `test_anim`,
  `test_scene` (clouds dirty 5 strips/frame, mood jump rebuilds 15, loop rolls
  over at tick 15000 = 10 min).
- `ss_preview` renders correctly at every mood, with overcast, fog and night
  mode, and for several seeds (see the report images).
- `ss_window` now compiles and links against real SDL2 and ran for several
  seconds without crashing. Its window contents and keys were **not** checked.
  The README still calls it unverified; update that line once Mike has used it.
- That machine had no `cmake`, so the CMake files themselves were not
  exercised; the compile/link commands mirrored `screensaver/CMakeLists.txt`.

Unverified:

- **Any Windows build.** `CMakeLists.txt` passes GCC/Clang-style flags, so
  MSVC (`cl.exe`) will reject them. See step 1.
- **Everything on the ESP32-S3**: speed, memory placement, panel setup,
  tearing, colors on the real screen.
- Whether PC and ESP32 float results match bit for bit (the reason for
  `-ffp-contract=off`).

## Recommended path through Stage 2

Stage 2 done-when (brief section 15): the landscape runs on the panel with a
moving element (the clouds) at a steady 20+ fps, no visible tearing or picture
shift, and frame time, PSRAM use and flash use are logged.

Stage 2 overlaps with TComm's own build-order step 1 ("Bring-up": toolchain
set up, Waveshare demos running, repeatable flash path) in
`docs/CLAUDE_CODE_HANDOFF.md`. That step has not formally been done yet. Do
the toolchain and display-demo part once and record it so it counts for both.

### Step 0: orient (first session, before changing anything)

1. Confirm the checkout is on `screensaver` and clean, and that
   `git log origin/screensaver -1` matches local.
2. Tell Mike in a few sentences what Stage 1 built and what Stage 2 is, and
   point him at the report page.
3. Ask him for the three things Stage 2 needs (also listed in the handoff):
   the Waveshare example he flashed from (folder name or link), his preference
   (or permission for you to choose) between Arduino and ESP-IDF, and to plug
   in the board when you get to flashing so the COM port can be confirmed.

### Step 1: get the PC preview working on Windows

This is the main development loop for every later stage, so it is worth
doing first. Recommended route: **MSYS2, UCRT64 environment** (GCC, CMake,
Ninja, SDL2 and ffmpeg from one package manager; the existing flags work
unchanged). Mike installs MSYS2 from msys2.org (you explain the installer);
then the packages are:

```
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-ffmpeg
```

Claude Code's shell on Windows is Git Bash, not MSYS2. Run builds through
`C:/msys64/usr/bin/bash.exe -lc "..."` with `MSYSTEM=UCRT64`, or put
`C:\msys64\ucrt64\bin` on PATH for the session. Unverified; adjust to what
works and write down what did.

Then:

1. Fix open item 5 first: `mkdirP` in `sim/preview_main.cpp` splits only on
   `/`, so `C:\...` or `C:/...` output paths can fail. Accept `\` as a
   separator and skip the drive prefix. Use relative `--out` paths until fixed.
2. `cmake -G Ninja ..`, build, `ctest`. All four must pass.
3. Determinism check: `./ss_preview --seed 11 --mood 4 --frames 1 --out f_golden`
   and compare with `docs/screensaver/stage1-report/img/golden.png` (convert
   the PPM with ffmpeg and compare decoded pixels, not file bytes). Identical
   pixels mean GCC on Windows matches Clang on macOS. A difference is a
   finding to record, not a blocker.
4. Have Mike run `ss_window` and try the keys in `screensaver/README.md`;
   update the README's "unverified" note with what actually worked.

If MSYS2 is a struggle, WSL2 (Ubuntu) is the fallback for the preview; it does
not help with flashing.

### Step 2: choose the board toolchain, together with Mike

This choice is for the whole TComm firmware, not just the screensaver, so
make it once and record it in `docs/CLAUDE_CODE_HANDOFF.md`.

- The main brief leans **Arduino**, starting from Waveshare's 4.3C examples,
  with app logic kept hardware-free (which `core/` already is).
- What the screensaver needs from the panel driver is ESP-IDF's `esp_lcd` RGB
  panel API: bounce buffers, number of framebuffers, pixel clock, direct
  framebuffer access, and the vsync / bounce-buffer callbacks. Arduino-ESP32
  3.x is built on ESP-IDF 5.x and can call `esp_lcd` directly, so Arduino does
  not rule this out. Unverified for Waveshare's specific example code.
- Practical snag with Arduino: a sketch cannot easily compile sources from
  outside its folder. Options: make `screensaver/core` an Arduino library
  (add a `library.properties`), or have a small script copy `core/` into the
  sketch's `src/`. Do not duplicate `core/` in git.
- Recommendation: go with Arduino (arduino-cli or Arduino IDE 2, esp32 core
  3.x) if Waveshare's 4.3C Arduino LCD example uses `esp_lcd` and exposes the
  bounce-buffer settings; otherwise ESP-IDF. Read their code before deciding,
  and tell Mike the choice and why in two sentences.

Before downloading Waveshare's examples or any toolchain, tell Mike what will
be downloaded and from where (github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3C,
espressif.com, arduino.cc).

### Step 3: read the panel setup, then flash a known-good demo

1. From Waveshare's example, record in `SCREENSAVER_HANDOFF.md`: pixel clock,
   timings, bounce buffer size, number of framebuffers, RGB565 byte order,
   whether LVGL is in the loop, and which PSRAM mode (octal, 80 MHz?) the
   project config uses. Mark each as read-from-code, not tested.
2. Build and flash Waveshare's own display demo unchanged, to prove the
   toolchain and flash path. **Confirm the COM port with Mike first. Never
   erase flash without asking.** Download mode if auto-reset fails: hold
   BOOT, unplug and replug USB-C, release, flash, then press RESET.
3. Factory restore if needed is in `docs/CLAUDE_CODE_HANDOFF.md` (flash
   `ESP32-S3-Touch-LCD-4.3C-Test.bin` at 0x00). Do not open the factory
   firmware's AIChats app; it traps the board on a Wi-Fi screen.

### Step 4: build `screensaver/port_esp32/`

`port_esp32/` is empty. Minimum for the done-when:

- `HostAlloc` backed by `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for the
  static layer cache (one full 768,000-byte frame).
- `App` storage (`SS_APP_BYTES`, 96 KB; `sizeof(App)` about 57 KB, 51 KB of it
  the strip buffer) in **internal SRAM**, e.g. `heap_caps_aligned_alloc` with
  `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`. Check free internal heap first; if
  it is tight, add an internal-allocation hook to `HostAlloc` for just the
  strip buffer instead. Fold in open item 2 here: `appStart` should reject a
  misaligned `storage`.
- A 25 Hz tick task at lower priority than (future) radio and touch, which
  skips a frame rather than blocking. Render into the panel's framebuffer via
  `appRender`; call `appInvalidate` after anything else touches it.
- Serial log every few seconds: average and worst frame time, strips per
  frame, free internal heap, free PSRAM, and app/flash partition use.
- Fixed inputs are fine (mood from a constant or a slow sweep). Touch, the
  status chip and real TComm inputs are Stage 8, not now.

Then measure open item 1 (building the cache straight into PSRAM vs. strip by
strip through the internal buffer) and keep whichever the numbers favor.

### Step 5: finish the stage

1. Run the clouds for at least ten minutes (one full loop rollover) and check
   the log for frame-time spikes.
2. Ask Mike for a short phone video of the panel, including a dusk or night
   mood, and note his reaction to the colors. Palette tuning (open item 3) is
   a one-table job in `core/palette.cpp`; only do it if he asks.
3. Independent review of the Stage 2 diff, then commit, push, and open or
   update the draft PR.
4. Append a "Stage 2" section to `SCREENSAVER_HANDOFF.md` (what was built,
   measured numbers, decisions, new open items) and update this file's
   "Verified state" so the next session starts from the truth.
5. Stop and summarize for Mike before starting Stage 3.

## Working rules (carried over, non-negotiable)

- Confirm the COM port with Mike before every flash. Never erase flash
  without asking.
- Mark anything unverified in code comments and in reports. Do not fill gaps
  with guesses.
- Ask before any purchase suggestion or download.
- Stop at the end of each stage with a short summary and something Mike can
  look at (preview video or a phone video of the panel).
- Keep `core/` free of platform calls; it must build unchanged on PC and
  ESP32.
