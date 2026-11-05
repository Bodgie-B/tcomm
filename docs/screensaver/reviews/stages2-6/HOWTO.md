# Re-cutting the review clips and page (2026-10-31)

Commands run to produce the morning, midday and weather clips and the page, in the
order run. Re-run them after the Lookout lands (and add the Lookout clips, see the end).
Standard library Python and ffmpeg only; the same command gives byte-identical clips
(checked by re-cutting `morning_wake_c`, `morning_overview`, `morning_douse_a`).

```sh
# 0. A preview binary from a clean copy of the tree (so edits in progress cannot break it).
#    HEAD at the time: 9fef80d.
S=/private/tmp/claude-501/-Users-mark--claude-spares-spare-mjlbDH/8ba60bf2-9ee9-4fb0-b995-8ac90a7e3bcd/scratchpad
mkdir -p $S/src3 && git -C /Users/mark/Repos/TComm/tcomm archive 9fef80d | tar -x -C $S/src3
cmake -S $S/src3/screensaver -B $S/b_tools
cmake --build $S/b_tools -j8 --target ss_preview
P=$S/b_tools/ss_preview            # review_clips.py takes the executable path (not a build dir)

cd /Users/mark/Repos/TComm/tcomm
D=docs/screensaver/reviews/stages2-6
T=screensaver/tools

# 1. Morning spine: every beat x variant (wake, coffee, bag_down, break_camp, douse, leave),
#    clips/morning_<beat>_<letter>.mp4, mood 1.5, --spine morning --weather clear, the beat
#    window plus 1 s either side, 640 px, crf 28, real time (2 ticks a frame, 12.5 fps), and
#    clips/morning_overview.mp4 (the first loop at 8x). Seeds are tried in order until all 16
#    variants are collected (seeds 1 to 14 were enough).
python3 -I $T/review_clips.py --preview $P --out $D --family morning --seeds 1-40

# 2. Midday spine, the same: midday_<beat>_<letter>.mp4 (return, lunch, wade, nap, map,
#    afternoon: 15 variants), mood 3, and midday_overview.mp4 (seeds 1 to 14 were enough).
python3 -I $T/review_clips.py --preview $P --out $D --family midday --seeds 1-40

# 3. Weather set: weather_<kind>_day / _night for wind rain snow fog heat chill storm (14 clips,
#    seed 3, 25 s at real time from 1 s before the first pause of 8 s or more at tick 4000 or
#    later; day = evening, mood 4; night = --night --mood 0), weather_storm_flash (10 s centred
#    on the first strike, found by brightness probing frames 500 to 1800), weather_wind_hat
#    (15 s from 2 s before the first pause), major_rain_shelter (first of seeds 1-30 whose
#    storm loop 0 plans it: seed 2), morning_fog_lift (first 150 s of a clear morning at 8x).
python3 -I $T/review_clips.py --preview $P --out $D --weather-clips --seeds 1-30

# 4. The page: manifest + the three ten-loop tables (evening mood 4, morning mood 1.5 with
#    --spine morning, midday mood 3 with --spine midday, each seed 1, 10 loops, family and
#    weather per loop), and the missing-clip check. Exits 1 if a clip the page names is missing.
python3 -I $T/review_page.py --preview $P --dir $D
```

Last run of step 4 printed:

```
page: docs/screensaver/reviews/stages2-6/index.html; 112 clips in manifest; 112 clips checked; loops tables: 10 evening, 10 morning, 10 midday
missing clips: none (lookout placeholder, 0 clips so far)
clip bytes: 14.4 MB
```

Notes

- A name already in `manifest.json` is not cut again. To re-cut some clips, delete their
  `.mp4` (a manifest row whose file is gone is dropped on load) or pass `--force`, which
  re-cuts every name the run produces and leaves the other manifest rows alone.
- The weather windows rest on two facts about the weather code, both probed with a
  throwaway program against the 9fef80d core (not committed): the hat, fan and hug gags start
  on the first tick of a pause (wind seed 3 at tick 1050; heat and chill seed 3 at tick 4300,
  which is the first pause after tick 4000), and the first strike of seed 3's storm is at
  tick 899 (the brightness probe found 902). If the weather timings change, check those
  clips by eye (`weather_wind_hat`, `weather_heat_*`, `weather_chill_*`,
  `weather_storm_flash`) and change `--weather-seed` if a gag moves out of its window.
- `--list` prints the manifest table: `python3 -I $T/review_clips.py --out $D --list`.

## After the Lookout lands

Add a `--family lookout` entry to `FAMILY_BEATS` / `FAMILY_MOOD` in `review_clips.py` (the
beat names and variant counts of the Lookout, and its mood), then run

```sh
python3 -I $T/review_clips.py --preview $P --out $D --family lookout --seeds 1-40
python3 -I $T/review_page.py --preview $P --dir $D
```

Clips named `lookout_*.mp4` in the manifest fill the Lookout section on their own; the
section's placeholder note hides when there is one. Its prose and the clip descriptions
in the page's data block can be written then. Rebuild `$P` from the tree that has the Lookout
(step 0 with the new commit) first. `review_page.py` does not check the Lookout clips.

## The Lookout clips (added 2026-10-31, tree 060c7f3)

This supersedes "After the Lookout lands" above: the Lookout has no variants, so it is not a
`FAMILY_BEATS` entry; `review_clips.py --family lookout` has its own run (`run_lookout`, with
`FAMILY_MOOD["lookout"] = 4` and the beat list `LOOKOUT_BEATS`). `review_page.py` now checks the
Lookout's nine beat clips, the two overviews and the seven stills.

```sh
# 0. A preview binary from the clean copy of 060c7f3.
S=/private/tmp/claude-501/-Users-mark--claude-spares-spare-mjlbDH/8ba60bf2-9ee9-4fb0-b995-8ac90a7e3bcd/scratchpad
mkdir -p $S/src5 && git -C /Users/mark/Repos/TComm/tcomm archive 060c7f3 | tar -x -C $S/src5
cmake -S $S/src5/screensaver -B $S/b_tools5
cmake --build $S/b_tools5 -j8 --target ss_preview
P=$S/b_tools5/ss_preview

cd /Users/mark/Repos/TComm/tcomm
D=docs/screensaver/reviews/stages2-6
T=screensaver/tools

# 1. --spine lookout --weather clear, mood 4. Seed 1 (the first of --seeds) gives, in clips/:
#    lookout_build, _approach, _trail_pause, _reveal, _climb, _explore, _settle, _fade (the
#    beat window plus 1 s either side, 640 px, crf 28, real time), lookout_sunset (the same
#    window at 4x, about 30 s) and lookout_overview (the whole loop at 8x, about 39 s);
#    and in img/: lookout_sunset_1..6.png (800x480, six ticks evenly spaced from 1 s after the
#    sunset starts to 1 s before it ends: 4750, 5320, 5890, 6460, 7030, 7600 for seed 1) and
#    lookout_rail.png (tick 4775, 2 s into the sunset: the top right 400x240 of the frame, 2x,
#    nearest neighbour: fixed at x=400, y=0, so check it by eye if the tower's place changes).
#    Seed 2 gives only lookout_overview_seed2 (the landscape differs per seed).
python3 -I $T/review_clips.py --preview $P --out $D --family lookout --seeds 1,2

# 2. The page: its Lookout section needs nothing but those clips and stills.
python3 -I $T/review_page.py --preview $P --dir $D
```

Re-cutting `lookout_sunset`, `lookout_climb`, `lookout_overview` and `lookout_rail.png` into an
empty directory gave byte-identical files. A name already in `manifest.json` is not cut again
(`--force` re-cuts all of the family, stills included). `build` is cut as well as the eight beats
named in the brief's table; the two 4 s `pause` beats are not.

Last run of step 2 printed:

```
page: docs/screensaver/reviews/stages2-6/index.html; 123 clips in manifest; 130 clips checked; loops tables: 10 evening, 10 morning, 10 midday
missing clips: none (lookout: 11 clips in the manifest)
clip bytes: 15.2 MB
```

## The Lookout re-cut for the sun and the stars (2026-11-01, tree 9ee92dd)

Mike's two changes (spec section 5b: the sun sinks, the stars come out one by one) change
every Lookout clip and still, so the whole family was re-cut with `--force` from a clean copy
of 9ee92dd (eb679d6 plus the orange sun), the same commands as above with `src7` / `b_tools7`. `review_clips.py` now also
cuts five sun stills for seed 1: `img/lookout_sun_1..4.png` (tick 340 just after the build;
midway between that and the half-below tick; 16 s before the sit, about half the disc
below the far ridge; and the sit itself, 6.2 s into the settle beat, where the disc is 7/8
below) and `img/lookout_sun_zoom.png` (the sit tick, the left 400x240 from y 120 at 2x,
nearest neighbour: fixed at x=0, y=120, so check it by eye if the sun's place changes).
The constants are `LOOKOUT_SUN_*` and `LOOKOUT_SIT_AFTER` at the top of the script.

```sh
S=/private/tmp/claude-501/-Users-mark--claude-spares-spare-mjlbDH/8ba60bf2-9ee9-4fb0-b995-8ac90a7e3bcd/scratchpad
mkdir -p $S/src7 && git -C /Users/mark/Repos/TComm/tcomm archive 9ee92dd | tar -x -C $S/src7
cmake -S $S/src7/screensaver -B $S/b_tools7 -DCMAKE_BUILD_TYPE=Release
cmake --build $S/b_tools7 -j8
P=$S/b_tools7/ss_preview
cd /Users/mark/Repos/TComm/tcomm
D=docs/screensaver/reviews/stages2-6
T=screensaver/tools
python3 -I $T/review_clips.py --preview $P --out $D --family lookout --seeds 1,2 --force
python3 -I $T/review_page.py --preview $P --dir $D
```
