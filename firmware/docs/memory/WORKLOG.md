# Memory work log

Started 2026-11-21, after the upstairs panel measured about 480 KB less PSRAM free than build 3260e2c,
in both states (idle 966,592 free, screensaver running 165,744). No commits, no flashing, no panel or
COM port touched. Line endings: CRLF, like the other docs.

## 2026-11-21: the danger-cold icicles (other agent)

The main screen's icicles no longer hold an 800 x 100 picture (240,000 bytes of PSRAM): they are drawn
strip by strip while LVGL redraws the top 100 rows, from one block of about 14 KB made when the cold
comes and freed when it ends. Simulator shots before and after are byte-identical
(docs/memory/icicles_before.png, icicles_after.png). Files: tcomm/src/ui/ui.cpp, art.cpp,
art.h, ui.h, gfx/canvas.cpp, canvas.h, tcomm.ino (the info line), tests/test_icicles.cpp.

## 2026-11-21: one page less of code copy (sync_ss_core.py only)

The panel copies its code (text) and read-only data (rodata) into PSRAM in 64 KB pages. From the
2026-11-21 15:09 build (t32radio map and elf): text 1,801,824 bytes in 28 pages (33,184 spare), rodata
718,736 in 11 pages (only 2,160 spare): 39 pages, 2,555,904 bytes of PSRAM in both states.

Not available: compiling story files at -Os "with a per-file flag". Everything Arduino builds is already
-Os (platform.txt compiler.optimization_flags=-Os, and -Os is on every compile line); the screensaver is
at -O2 only because tools/sync_ss_core.py puts `#pragma GCC optimize("O2")` on each of its files.

What changed, all in tools/sync_ss_core.py (tools/fw.sh untouched; it already calls the script):
1. AT_OS: 38 story files (majors, bike, minors, visitors, reeds, the day_* and stage 22 plays...)
   no longer get the -O2 pragma, so they build at the firmware's -Os. They place characters and hand
   every pixel to gfx/raster.cpp, which stays at -O2, as do landscape, stream, ambient, world, app,
   the spines, critters and props. (Started by an earlier run of this step at 16:32; checked and kept.)
2. NOEXC: every screensaver .cpp gets `#pragma GCC optimize("no-exceptions")` on the line after its
   last #include (after, because the C++ library headers need exceptions on). The core never throws
   or catches, its host build is -fno-exceptions already (screensaver/CMakeLists.txt:37), and no
   firmware file has a try or catch, so a throw ends in the same reset either way. What it removes is
   the unwind tables (.eh_frame), which sit in rodata and so in PSRAM.

How it was measured: the panel compiler run by hand into the scratchpad with the exact compile lines of
the last build (compile_commands.json), the changed objects swapped into copies of its archives, and the
firmware relinked with the same linker line (no arduino-cli, no fw.sh). A relink of the unchanged
archives comes out 7,128 bytes of text larger than Arduino's own link (call relaxation depends on
layout) and 8 bytes of rodata, so the changes are compared relink to relink. The icicle agent's
ui.cpp, art.cpp, canvas.cpp and tcomm.ino were compiled in too.

| Relink | text (pages, spare) | rodata (pages, spare) | PSRAM copy |
|---|---|---|---|
| 15:09 build as it was | 1,808,952 (28, 26,056) | 718,744 (11, 2,152) | 2,555,904 |
| + icicles | 1,810,460 (28, 24,548) | 718,832 (11, 2,064) | 2,555,904 |
| + icicles + AT_OS at -Os | 1,736,032 (27, 33,440) | 720,528 (11, **368**) | 2,490,368 |
| + icicles + AT_OS + NOEXC (now) | 1,741,412 (27, 28,060) | 685,280 (11, 35,616) | 2,490,368 |

- AT_OS alone saves 74,628 bytes of text, but its extra out-of-line functions add 1,632 bytes of unwind
  tables to rodata, which would leave rodata 368 bytes from a twelfth page (the next few strings would
  cost 65,536). NOEXC removes 35,248 bytes of them (scene .eh_frame 35,560 -> 488) and costs 5,380 bytes
  of text (the compiler inlines a little more without them). Rodata then has 35,616 spare, text about
  28,060 in the relink, probably about 35,000 in Arduino's own link.
- Flash image about 102,600 bytes smaller (text -69,048, rodata -33,552 against "+ icicles").
- Static internal RAM unchanged by this: data + bss + noinit 117,864 -> 118,008 in the relinks, the 144
  being the icicle agent's; Arduino's figure should read about 118,016 (limit 121,000).
- Speed: the earlier run of this step timed every frame of six seeds on the PC, all -O2 against the
  AT_OS set at -Os: within 2 percent either way, the same as run to run, with identical frame checksums
  (scratchpad mem/bench). NOEXC can't be timed on the PC (the PC build never had unwind tables); on the
  panel it removes tables only and lets a few calls inline, so it should be no slower. Not measured on
  the panel: watch ss stats (12.8 fps, 58 ms a frame before).
- Picture: the PC build is unchanged (the pragmas are only in the panel's copy). On the panel, different
  inlining could move a rounding by one unit in a few places; no visible change expected, not checked.

## Estimate for the next flash (same conditions as 2026-11-21: same ringtone, voice notes used)

| State | 2026-11-21 measured | Expected now |
|---|---|---|
| Idle, PSRAM free (largest) | 966,592 (950,260) | about 1,032,128 (about 1,015,800) |
| Screensaver running | 165,744 (163,828) | about 231,280 (about 229,360) |
| Danger cold during the screensaver | 240,000 needed, did not fit | about 14 KB needed, fits |

The page comes back as 65,536 contiguous bytes at the start of the PSRAM heap, so the largest block
should grow by the same. Check with `info` (memory line) idle and `ss stats` with the screensaver on.

## Looked at and left alone

- The ding is already rendered, played and freed (board_audio.cpp:367-380). Voice dump, mic raw and the
  voice clip are freed after use too.
- Upstairs voice relay queue (32 x 252 = 8,064 + 2,560 stack, board_radio.cpp:70-78, 249-256): sized
  for relaying a whole clip; shrinking it is a voice-reliability question, not a free saving.
- Voice UI state kept by ui_keep (about 4 KB) and calendar icons drawn once (ui_calendar.cpp:241-293,
  unchanged since 3260e2c): small, and freeing them means rebuilding screens that are reused.
- Montserrat 26 (32,889 bytes of rodata) is used by Settings (ui_settings.cpp:279, 295, 589).

## Needs Mike

1. The ringtone. Resident clips: chosen ringtone (Chickadee 179,928 up to Westminster 736,470), soft chime
   224,910, dog alert 175,516, replies 62,622 each. The trace found about 255 KB of the 480 KB growth that
   no code change explains; a different chosen ringtone at the two measurements would. The audio report
   ("rendered X chosen Y") on the next flash settles it.
2. The two reply sounds (125,244 bytes held) could be rendered when needed, like the ding: a few hundred
   ms before "come on" or "not now" plays. A behaviour change, so not done.
3. The rest of the firmware carries about 22 KB of unwind tables (ui files 10,340, controller 3,228,
   storage, device platform...), plus 14 KB in the Arduino libraries. Only a build flag in tools/fw.sh
   would remove the firmware's share; not needed while rodata has 35 KB spare.
