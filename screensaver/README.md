# Campsite screensaver

Procedural animated screensaver for the ESP32-S3 panel (800x480 RGB565). Design
brief: `docs/screensaver/CAMPSITE_SCREENSAVER_BRIEF.md`.

## Directories

- `core/` portable C++17. Scene, scheduler, palettes, rasterizer. Public interface
  is `core/ss_app.h`. No platform calls. Built with strict warnings (`-Werror`).
  - `core/scene/app.cpp` implements `ss_app.h`; `core/scene/landscape.*` holds
    the per-loop landscape geometry and drawing; `core/render/` is the strip
    renderer with the static layer cache.
- `sim/` PC preview. `preview_common.*` is the shared driver, `preview_main.cpp`
  is the headless PPM renderer, `window_main.cpp` is the SDL2 window.
- `port_esp32/` panel firmware glue (not started).
- `tests/` `test_*.cpp`, plain executables run by `ctest`.
- `tools/` `make_video.sh`.

## Build

    cd screensaver
    mkdir -p build && cd build
    cmake ..
    make ss_preview          # headless, always built
    make ss_window           # only exists if cmake found SDL2
    ctest                    # tests

## Headless preview and video

    ./ss_preview --seed 7 --mood 3 --weather rain --frames 250 --out /tmp/frames
    ../tools/make_video.sh /tmp/frames /tmp/out.mp4 25
    ../tools/make_video.sh /tmp/frames /tmp/out.gif 25     # 800 px wide GIF

Needs `ffmpeg` for the video step. Frames are `DIR/frame_%05d.ppm`.

Options (`./ss_preview --help` lists them all):

| Option | Meaning |
| --- | --- |
| `--seed N` | first loop seed |
| `--mood F` | 0 night, 1 dawn, 2 morning, 3 day, 4 golden, 5 dusk |
| `--weather NAME` | clear rain snow wind heat fog chill (omit: core picks) |
| `--overcast F`, `--fog F` | weather tint, 0..1 |
| `--night` | night mode on, night_dim 1 |
| `--lookout` | inside the sunset window |
| `--status N`, `--dog N` | other floor status, dog out |
| `--frames N` | frames to write |
| `--fps N` | render fps, ticks per frame = 25 / fps (default 25) |
| `--every N` | render one frame every N ticks (N >= 1); wins over `--fps` |
| `--skip N` | ticks to simulate before the first frame |
| `--log-beats` | print `loop=L seed=S start tick=T` and `tick=T loop=L seed=S beat=NAME` to stdout, `--skip` ticks included |
| `--out DIR` | output directory, created if missing |
| `--doorbell-at TICK` | post a doorbell at that tick (the tick counter includes `--skip`) |
| `--mood-sweep A B` | mood changes linearly A to B over the frames |

Cutting a clip: frame `f` (from 0) shows tick `SKIP + (f+1) * N`, N being ticks per
frame. A loop or beat that `--log-beats` reports at tick `T` therefore starts at frame
`(T - SKIP) / N - 1`. For `T=15000`, `--skip 0`, `--every 1`, 10 s of clip:
`ffmpeg -framerate 25 -start_number 14999 -i frame_%05d.ppm -frames:v 250 -pix_fmt yuv420p clip.mp4`

Unknown arguments print usage and exit 2. The final line reports wall time and
average ms per frame (simulation plus render, file writes excluded).

## SDL2 window

`ss_window` takes the same options (`--frames`, `--out`, `--mood-sweep` are ignored).

| Key | Action |
| --- | --- |
| SPACE | pause |
| RIGHT | step one tick while paused |
| F (hold) | fast-forward x8 |
| 1..6 | force mood 0..5 |
| M | release forced mood (back to `--mood`) |
| W | cycle weather code 0..6 |
| N | toggle night |
| D | doorbell |
| S | new random seed (restart) |
| R | restart with the same seed |
| P | write `shot_seed<seed>_tick<tick>.ppm` to the current directory |
| ESC | quit |

The HUD line is in the window title.

Unverified: `window_main.cpp` has never been compiled against real SDL2. The cloud
build container has no SDL2. It was only syntax-checked against a hand-written
stand-in for the SDL calls it uses.
