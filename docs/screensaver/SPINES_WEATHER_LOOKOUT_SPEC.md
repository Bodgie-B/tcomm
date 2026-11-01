# Morning and midday spines, weather, the Lookout: architecture spec

Written 2026-10-31 by the chief session, from Mike's requests on the draft
pull request (after he approved the evening spine at 9312743) and brief
sections 6, 7, 8 and 11. Design intent stays in `CAMPSITE_SCREENSAVER_BRIEF.md`;
the rules of `STAGES_2_6_SPEC.md` (layering, cache split, dirty tracking,
budget, determinism) all still stand. Status lines at the bottom are updated
as each chunk lands.

Order of work: morning spine, midday spine, weather, Lookout (Mike's order).
Everything in the PC preview; nothing here needs the board.

## 1. Spine families

A loop plays one *family* of spine, picked at loop start from the inputs:

| Family | When (`Inputs`) | What |
|---|---|---|
| `SF_EVENING` | default: mood in [3.5, 6) and night, i.e. golden hour, dusk, night | the stage 4 to 6 spine as it stands (approved; do not change its scripts) |
| `SF_MORNING` | mood in [0.5, 2.5): dawn and morning | wakes in fog, coffee, breaks camp |
| `SF_MIDDAY` | mood in [2.5, 3.5): day | back from a day hike, lunch, wade, nap, map |
| `SF_LOOKOUT` | `in.lookout_window` (beats the mood) | the sunset set piece, its own landscape |

Preview: `--spine evening|morning|midday|lookout` overrides the pick (the
existing `--lookout` flag sets `lookout_window`). `--loops N` prints the
family per loop. The review tooling (`tools/review_clips.py`) gets the
family from the `--log-beats` plan line.

Code shape:

- `spine.h`: `enum SpineFamily`, `LoopPlan::family`, and for the new
  families `LoopPlan::fvar[8]` (variant per beat, family order) and
  `n_fbeats`. The evening fields stay as they are so nothing that reads
  `arrival=.. setup=..` changes. `spineFormatPlan` prefixes `family=`.
- `spinePlan` / `spineLayout` / `spineBuildTimeline` / `spineLoopTicks`
  dispatch on the family. The evening path is untouched; the morning and
  midday paths live in `spine_morning.cpp` and `spine_midday.cpp` (same
  anonymous-namespace pattern: action functions, `const Step` tables,
  durations, names, begin/update/end hooks), each exposing a small internal
  header (`spine_family.h`) with `xxxPlan`, `xxxLayout`, `xxxBuildTimeline`,
  `xxxBeatName`, `xxxVariantCount`.
- The interpreter, anchors (`script.h`), `CampLayout` and the fillers are
  shared. New anchors go on the end of `Anchor` and `CampLayout`.
- Fillers: each family has an allowed set (`kFillerFamilies[FILL_COUNT]`
  bit mask). Morning: read, cairn, cloud_watch, stream_listen, birds.
  Midday: all but mug_sunset. Evening: as now.
- Majors: `kMajors[]` gets a `families` mask. Evening keeps its full pool.
  Morning: squirrel_splash, deer_stick, pinecone, map_chase, fish_leap,
  ants_cookie, echo, ranger_ridge, the four stream vignettes. Midday: the
  same plus bathe weighted up. Stargaze, firefly_jar, shadow_theater,
  raccoon_pack: evening only. `majorsPlan` takes the family's slot list
  (which beat each slot follows and whether he is outside).
- Loop length policy: morning and midday use the same 480 to 660 s policy
  and the same squeeze / stretch code (factor it so a family passes its
  pause slots). The Lookout has its own length (section 4).
- `memory.h`: unchanged in shape. Morning and midday write the same fields
  (cairn, stolen item, rod_lost). `Memory::last_family` (new) so the plan
  can avoid the same variant twice running for a family.

Tests: `tests/test_spines.cpp` (new), same harness as `test_scene.cpp`:
every family's every variant plays with zero overruns over 40 seeds, the
hiker stays in frame, the quiet zone stays clean, `sizeof(App)` under the
limit, and the evening's `--loops 10` output for seed 1 is byte-identical
to what it was (regression: the evening must not move).

## 2. Morning spine (`SF_MORNING`)

Loop start: the camp is already built. `worldBeginLoop` for this family
leaves the tent `TENT_UP` (not lit, no shadow), the fire pit laid with ash
(`FIRE_OUT`, the morning's `fire.ash` look: a low grey mound; add a draw
state if the pit has none), the food bag `BAG_HUNG`, the pack by the tent,
the cairn from memory. The build beat runs as now; the camp props pop in
with the plants (scale 0 to 1 with an overshoot, like the mushroom) so the
build still reads as "the scene assembles".

Fog: the loop opens in fog whatever the inputs say and it lifts over the
loop. `World::fog_extra` (0..1) is added to `in.weather.fog` by
`computeAppPalette` (clamped). The morning plan sets it to 0.55..0.8 at
start and eases it to 0 by the end of the coffee beat's pause, in steps of
0.05 (each step is a palette change and a cache rebuild: about 15 a loop;
record the count in the notes).

Beats, in order, each with a pause after it as the evening has, fillers in
the pauses after coffee and break_camp, majors after wake, coffee and
bag_down (he is outside for all of them):

| # | Beat | Variants (a / b / c) | ~s |
|---|---|---|---|
| 1 | wake | a: the tent wobbles as he stirs, the door unzips (a tent `door` 0..1 draws the flap open), he crawls out on all fours, stands, stretches (arms up, POSE_REACH_UP), scratches his head. b: crawls out, stands on the mug he left by the door (memory: or the shoe), hops holding his foot (the stumble fx). c: a bird lands on the ridge of the tent and taps; he bursts out, it leaves. | 30 |
| 2 | coffee | pot from the pack, rekindles the fire (blow puffs, a small flame: `FIRE_LIT` at low fuel), mug from the pack, sits at `view` with the mug. a: sips, long still looks out into the fog as it lifts. b: burns his tongue, yelps, fans his mouth, blows on the mug before every sip. c: the chipmunk sits on the reading rock watching the mug; he holds it closer. | 55 |
| 3 | bag_down | walks under the bag branch, lowers it by the rope (line grows), unties. a: smooth. b: the knot sticks; he yanks twice; the bag drops on his head, he staggers. | 25 |
| 4 | break_camp | pegs out (crouch at `tent_work`, three tugs), the tent slumps (`TENT_COLLAPSED`, sag 0..1), he gathers it into the pack. a: one peg is stuck; he heaves, falls on his back, gets up, pulls it with a rock as a lever. b: a gust takes the fly (a `SH_MAP`-like tumbling shape, GL_SHAPE) across the clearing; he chases it, stamps on it, brings it back. c: the tent will not go in its bag; he stuffs, kneels on it, sits on it, it goes. | 50 |
| 5 | douse | a: kicks dirt on the embers (BIT_DIRT flecks), a last smoke puff, stirs with the stick. b: pours the pot on it, a steam burst (`Steam`), waves the smoke away. | 22 |
| 6 | leave | pack on (the pack prop goes, `pack_on`), looks back at the clearing. a: walks off right over the ford. b: walks off, stops at the edge, comes back for the mug he left by the rock, goes. c: adds a stone to the cairn, pats it, goes. The last 2.5 s fade to black as the close does. | 32 |

Memory from the morning: a stone added to the cairn in leave c; the mug
left (b) is "lost" only within the beat. `rod_lost` is read as now by the
fishing majors.

Mood: morning loops are expected at dawn/morning, so the fog palette and
the low sun are the look; the tent is never lit.

## 3. Midday spine (`SF_MIDDAY`)

Loop start: camp built as for the morning, fire out, bag hung, pack by the
tent, the hiker away (he is on a day hike).

| # | Beat | Variants | ~s |
|---|---|---|---|
| 1 | return | walks in from the right with a walking stick (CARRY_STICK). a: drops onto `sit`, takes the hat off, fans himself with it (hat as an item in his hand: ITEM_HAT held), puts it back. b: limps in, sits, boot off (the shoe item), shakes a stone out (a BIT_DIRT fleck), boot on. c: walks in picking burrs off his sleeves and trousers (small arm flicks), one off his hat. | 34 |
| 2 | lunch | rummages the pack, sits with the pot (cold) and the fork. a: eats, a bee (BIT_BEE) comes for it, he swats, it comes back, he eats faster. b: the chipmunk edges in for a crumb, gets it, runs (the dinner deer/chip machinery, chip only). | 40 |
| 3 | wade | boots off by the bank (two shoes down), wades in (`fx.wade`). a: stands midstream looking down at a fish that circles his feet (a GL_SHAPE fish from majors), tries to grab it, misses. b: slips, sits down in the water, a splash (rings), sits there a moment, shrugs. c: skips stones from midstream (the `Skip` prop). Out, boots on. | 45 |
| 4 | nap | lies in the shade at `lie` with the hat over his face (POSE_LIE, hat item on the head). a: the chipmunk raids the open pack and leaves with a bar (no memory: `last_stolen` holds an item the raccoon can bring back). b: a leaf lands on his belly, he brushes it off without waking. c: a fly circles (BIT_MOTH flutter), he swats in his sleep, the hat falls off, he sits up. | 60 |
| 5 | map | sits with the map (SH_MAP open), turns it round twice, scratches his head, looks at the ridge, turns it again. a: calm. b: a gust nearly takes it; he grabs it, folds it, sits on it. | 30 |
| 6 | afternoon | lays out the wood for the evening (if the pile is small), sits by the pit with the mug, looks out; the last 2.5 s fade. | 25 |

Fillers in the pauses after lunch and nap; majors after return, lunch and
wade. Bathe is weighted up (he is already wet).

## 4. Weather (`weather.h/.cpp`, `World::weather`)

State chosen at loop start by `weatherPlan(World&, Rng&, const Inputs&)`:

```cpp
enum WeatherKind : uint8_t { WX_CLEAR = 0, WX_WIND, WX_RAIN, WX_SNOW, WX_FOG, WX_HEAT, WX_CHILL, WX_STORM, WX_COUNT };
struct Weather {
  uint8_t kind;        // WeatherKind
  float k;             // intensity 0..1 (eases toward k_to so a shower can come and go)
  float k_to;
  float wind;          // -1..1, + blows right; gusts ride on it
  float gust;          // 0..1 current gust envelope
  uint32_t next_gust, next_flash;   // ticks
  float flash;         // 0..1 lightning envelope (storm)
  float snow_roof;     // 0..1 snow on the tent roof
  uint8_t extra;       // a random extra this loop: 0 none, 1 gust, 2 shower (ticks in extra_at)
  uint32_t extra_at;
};
```

Source: `in.weather_code` when `in.have_outside` (the mapping table lives
in one place and is easy to edit; codes as `ss_app.h` lists them); else a
random draw weighted by mood (fog likelier at dawn, heat at day, storm at
golden hour, chill at night) with clear at 50 %. Random extras on top
(brief section 11: a gust, a short shower) never contradict the base (no
shower in snow, no gust in fog).

Effects (each a small system drawn in the dynamic layer, bounds marked
dirty like everything else):

| Kind | What moves |
|---|---|
| wind | the ambient `wind_t` sway amplitude scales 1 to 2.5 with `wind`; the tent `wobble` follows the gusts; leaves blow across (BIT_LEAF, 2 to 3 at a time, from the left or right by `wind`); once a loop when he is standing still his hat comes off and tumbles a few metres (the leaf_fall minor's hat mechanics; he fetches it) |
| rain | diagonal streaks: no per-drop state; each strip draws a hashed pattern of 1 px streaks from (tick, strip, k) so it costs nothing to keep; drips off the tent ridge (BIT_DROP at the ridge ends); rings on the stream (BIT_RING at hashed points); the fire smokes more and burns lower (`smoke_k` 1.6, `fuel` capped 0.5) with a hiss glyph (GL_ARCS small); overcast tint 0.6 |
| snow | slow flakes, hashed as the rain but drifting on sines, 2 px dots; `snow_roof` grows on the tent (the tent draw whitens its ridge and the upper third of the near face by `snow_roof` with a highlight slot, never pure white); a shake-off once a loop when he passes the tent |
| fog | `fog_extra` as the morning uses it, plus 2 to 3 drifting bands: wide low ellipses in the FOG slot (new palette slot: the mood's sky-horizon colour) over the mid pines band, drawn after the static layers and before the ground actors; they drift at 3 px/s and breathe |
| heat | horizon shimmer: the far ridge band (24 rows above the horizon) is redrawn dynamically with per-row x offsets `sin`; the hiker fans himself with the hat in the pauses (a filler-like insert); a sweat drop (BIT_DROP from his head, once in a while) |
| chill | breath puffs from the head every 4 to 6 s while he stands or sits (the Steam prop shape, 1 puff, small); the arm-hug pose (new POSE_HUG: arms crossed, shoulders up) in pauses; a shiver fx (2 px x jitter, 0.4 s) now and then |
| storm | rain at k 1 plus lightning: `flash` rises in one tick and decays over 0.3 s; the palette is recomputed with a `flash` term that pulls sky and ridge slots toward a light blue-grey, capped by the night rule (`NIGHT_LUM_MAX`); a palette change rebuilds the cache, so a flash costs 3 to 4 rebuilds; the rain_shelter major becomes available (below) |

`rain_shelter` (MJ_RAIN_SHELTER, the thirteenth planned major): a flash
while he is outside; he looks up, a second flash and the thunder arcs, he
runs for the tent and dives in (the tent sags and rebounds), his shadow
appears if it is dusk or later; the rain eases; he looks out of the door.
Only when `weather.kind == WX_STORM`, slot after fire or dinner.

Night rule: nothing in this section draws large bright areas; the flash
is a palette shift within the night caps; snow and fog use dimmed slots.

Preview: `--weather clear|wind|rain|snow|fog|heat|chill|storm` sets
`weather_code` and `have_outside`; `--loops N` prints the kind per loop.

Tests: `tests/test_weather.cpp`: every kind runs a full loop on 10 seeds in
every family with zero overruns; the night palette caps hold at flash 1;
the hashed rain is identical across two runs; no weather draws into the
quiet zone.

## 5. The Lookout (`lookout.h/.cpp`, `lookout_land.h/.cpp`)

Plays in place of the camp spine when `in.lookout_window` (preview:
`--lookout` or `--spine lookout`). About 4 to 5 minutes; its own length
policy: `SS_LOOKOUT_MIN_SECONDS 250`, max 320, pauses stretched to land it.

Its own landscape, `LookoutLand`, a separate struct in `World` beside
`Landscape` (not a union: about 3 KB, within the budget). Geometry from
the loop rng: a hillside rising from the lower left to a ridge on the
right two thirds up the frame; a switchback trail (a polyline of 5 to 7
legs) up the hillside; thinning pines along it; the tower on the ridge;
the valley behind in three atmospheric layers (reuse the ridge crest
drawing and the mood palette's ridge slots); horizon clouds (the camp's
`Cloud`, 2, slow). The quiet zone stays clean (ground and grass only).

The tower (original; not Firewatch's): four timber legs with X bracing in
two tiers, a zigzag stair with two landings up the near side, a square
cab with a wrap-round catwalk and rail, a shallow hipped roof with a short
overhang, windows all round (the near two catch the sun: a highlight slot
that follows the mood, dark by dusk), a fire-finder table silhouette
inside, a flagpole stub. About 110 px tall on the ridge, drawn in TRUNK
with ACTOR for the cab's dark glass; silhouette first in the reveal beat.

The hiker rig scales: `height` goes from 90 at the trail foot to 44 at the
ridge (the brief's "getting smaller as he climbs"); walks along the trail
follow its polyline with `feet.y` set each tick by an `act` (the walk cycle
only moves x), legs in OP_GO style; the stairs the same at 44 px with a
climb pose blend (a new POSE_CLIMB: knees high, hand on the rail).

Beats (brief section 7), each a script as the spine's:

| # | Beat | s | Notes |
|---|---|---|---|
| 1 | approach | 30 | in from the left at 90 px, up the switchbacks, shrinking; the wind sway rises a little |
| 2 | trail_pause | 8 | stops, breathes, looks up; a hawk (the ambient bird, bigger, slower, a glide) crosses the top |
| 3 | reveal | 12 | the tower rises above the ridge: silhouette (ACTOR) first, then detail (TRUNK, windows) |
| 4 | climb | 40 | the zigzag stairs; pauses at a landing to look at the valley; a bird on the rail leaves when he moves |
| 5 | explore | 50 | round the catwalk, peeks in windows, into the cab, touches the finder table, taps the compass plate (a small arc glyph); a chipmunk on a beam watches |
| 6 | settle | 20 | sits at the west rail, legs over the edge, arms on the rail |
| 7 | sunset | 90 to 120 | he breathes (4 s cycle, 1 px) and nothing else; mood steps from 4.0 to 5.9 in 8 to 10 steps, one every 11 to 15 s (a palette change and cache rebuild each), the valley layers go dark far to near, horizon clouds hold colour last; birds every 15 to 25 s at a random height and speed, sometimes a pair, once a pair circles and lands in the far treeline |
| 8 | fade | 10 | deep blue (one step, mood 5.9 to 0 = night; the clouds keep their lagged colour through it), one star (a GL_DOT in STAR), then the frame fades to black as the close does |

No fillers, no majors, no minors (the minor scheduler sees `busy`).
Memory is untouched except `loop_count`. The Lookout never changes the
evening camp.

Preview: `--spine lookout --mood 4` is the reference run; the review page
gets a section with the whole scene at 8x and stills of each beat.

Tests: `tests/test_lookout.cpp`: zero overruns over 20 seeds, the hiker's
height is monotone non-increasing over the approach, the tower's bounds
never enter the quiet zone, the sunset's palette steps are monotone in
mood, the night caps hold through the fade.

## 5b. The sun and the stars (Mike, PR #2, 2026-11-01)

Two deliberate changes to brief section 7, asked for after the clips:
the sun sinks, and the stars come out one by one. Both are sky things
drawn in the dynamic pass over the cached landscape (the sky stays plain
in the cache), clipped to the skyline, with small dirty marks; the
sunset's palette steps and the rebuild count do not change.

**The sun.** One flat disc in `SUN` (which `lookoutPaletteTweak` sets to a
deep orange, 0xF28C3A, at the loop's mood 4.0: the key's pale cream vanished
against the low sky bands), radius 20 to 24 px (the sun's own stream, below), over
the valley on the west side. It moves linearly in loop time from the
loop's start to the moment he is down on the deck's edge: down, and a
little to the right (dx = 0.2 dy). Start: centre x 120 to 300, the disc
clear above the skyline by 60 to 84 px. Timing (the ask): when
`actSitEdge` reaches u = 1 the disc is 7/8 below the skyline at its
centre x, within 1/16 either way; the last sliver goes in the 8 s or so
after that, while he sits still (as built, seeds 1 to 20: fully set from
7.2 s before the sunset beat starts to 1.1 s after it, and 11 to 19 s
before its first palette step; the prose first said "in the first 10 s
of the sunset", which the numbers never gave). The sit moment is computed
at loop start from the plan: the build, the beats and pauses before the
settle, the walk from the compass to the seat at the deck pace, the sit's
1.8 s. The skyline at x is the lowest y (highest on screen) of the three
valley crests and the hill (`lookoutSkylineAt`); the disc is drawn only
where y < skyline(x), column by column, so it goes down behind the ridge
line. Drawn after the sky and before the clouds (a cloud can pass in
front of it); during the build's sky fade its colour mixes toward SKY4
with the sky, as the clouds do. Once fully set it is neither drawn nor
tracked. Dirty: the union of its last and current integer bounds when
they differ (about one strip every second or two). Kept out from behind
the clouds (added after the first frames): the start is drawn up to 16
times and each try scored by its worst cover by either cloud's puffs
(padded 4 px) at 8 even times over the descent plus the sit; the least
covered is kept. The sun and the stars draw from their own rng stream
(seeded from the loop rng's state, as the weather's is from the seed), so
the loop rng's sequence and every later loop are unchanged.

**The stars.** `LK_STARS = 40`, planned at loop start: positions over the
whole width, each at least 8 px above the skyline at its x, outside the
tower's bounds (`lookoutTowerBounds`: they would otherwise draw over the
tower, which is in the cache), outside the reserved corner, no two within
10 px (retry, one rng call per statement, bounded). The first eight are
the bright ones (radius as the camp's larger stars), the rest single
pixels. Appearance order is the array order (bright first). Schedule in
seconds from the sunset's start: 34 stars over 45 % to 100 % of the
sunset with gaps that shrink toward the end (`u_i = (i / 34)^0.6`, plus
±1.5 s jitter, gaps never under 0.4 s), the last six at 0.5 to 6 s into
the fade. Each star comes out in four steps of 0.5 s (colour mixed from
the sky band behind it toward `STAR`, as the camp's `drawSkyBody` does),
so a star marks dirty four times, not every tick. Drawn after the sky and
before the sun and the clouds (a cloud or a bird in front of a star is
right). They stay through the fade's night step and go to black with the
frame. The old single `G_STAR` glyph goes.

**Helpers for the tests and the notes:** `float lookoutSunBelow(const
World&)` (0 above the line .. 1 fully set), `int lookoutStarsOut(const
World&)` (stars at full), `uint32_t LookoutState::sat_tick` (the tick
he is down; 0 before).

**Tests (`test_lookout.cpp`):** over the 20 seeds the sun's fraction
below is non-decreasing, 0 at the loop's first tick with the whole disc
inside the frame and clear of the corner, 13/16 to 15/16 at `sat_tick`,
1 before 30 % of the sunset; the stars-out count is non-decreasing, 0
before 40 % of the sunset, at least 20 at the sunset's end, 40 by 7 s
into the fade; every star is at least 8 px above the skyline at its x,
outside the tower's bounds and the corner, no two within 10 px; the
incremental render still equals the full render at checkpoints that
include the sun's crossing and the stars' coming out; rebuilds still at
most 16; `sizeof(App)` still fits. The `seen.star` check becomes
`lookoutStarsOut(w) == LK_STARS` at the loop's end.

## 6. Budget and determinism (unchanged rules, restated)

- One `rng` call per statement; table sines; no doubles; `-Werror` under
  clang and GCC 16 (`cmake -S .. -B /tmp/gccb -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-16`).
- `sizeof(App) <= SS_APP_BYTES` (now 67,024 of 98,304). Scripts are const
  tables (flash); the new RAM is `Weather`, `LookoutLand` and a few fields.
- Palette changes rebuild the static cache; count them per loop in the
  notes (fog lift, flashes, the sunset).
- Nothing draws into x >= 660, y >= 420.
- Mark anything not verified as unverified in comments and notes.

## 7. Review and clips

Each chunk: an Opus review that did not write it (incremental vs full
render harness as before), fixes, then `tools/review_clips.py` cuts its
clips (new sections: morning, midday, weather, lookout) and
`tools/review_page.py` injects them into
`docs/screensaver/reviews/stages2-6/index.html` (renamed in place to
"stages 2 to 6 and the spines" rather than a new page, so Mike has one
link). Push after each chunk.

## Status (2026-10-31, chief)

All four chunks are built in the PC preview, independently reviewed, fixed
and pushed to `screensaver`:

- Spine families and the morning spine: 05ace3b (review fixes in 9fef80d:
  the leave c detour skipped whole when the cairn is full, the book goes in
  the pack, the fly flag, beat-end cleanup). The evening's `--loops` lines and
  frames are unchanged; `test_spines` pins seeds 1 and 7.
- Midday spine: 9fef80d; the mood picks it from 2.5 to 3.5 (`test_scene`
  forces the evening for its own checks). `test_midday` pins its lines.
- Weather: 9fef80d, with review fixes in 060c7f3 (night lightning a palette
  shift, smoke sampled on the sub-tick, shiver gated, gag abort restores
  him, sway capped). Storm loops cost 16 to 25 rebuilds (morning up to 37),
  every other kind none beyond the loop's own. `test_weather`.
- Lookout: 060c7f3, review fixes after it (explore 36 s so he does not stand
  17 s at the compass, 8 to 10 sunset steps, the fade's second step dropped
  and the clouds held through it, birds over the tower, the persona set on
  the first camp loop after a Lookout, the reveal marks dirty only when the
  tower changes, random rain/snow/fog/storm never tint a Lookout). 315 s a
  loop, 11 to 13 rebuilds, strips 3.3 a frame. `test_lookout`. Not done: the
  approach's wind rising (no swaying grass on the hill).

`sizeof(App)` 70,320 of 98,304. 10 test suites under clang and g++-16.
Notes: `reviews/spines/notes.md`, `weather_notes.md`,
`lookout_land_notes.md`. Clips: `reviews/stages2-6/index.html`.

Deviations from this spec, all recorded in the notes: coffee runs 68 to
74 s (not 55); `spineLayout` stays shared and each family's layout runs
from `spineBeginLoop`; the fog band uses SKY4 (all 32 slots are taken);
the wind always blows left; the nap's stolen bar leaves no memory; random
weather ignores the season (no date input yet: **unverified** against the
brief until stage 8 adds one); rain_shelter plays only for a storm from
the data (a random storm still rains and strikes); the Lookout's approach
is 38 to 43 s and its climb 30 to 36 s (trail and stair lengths).

Everything is unverified on the panel: rebuild cost of the palette steps
(the flash, the fog lift, the sunset), the rain's strip count (about 10 of
15), readability of the 44 px hiker and the tower rails at real size.
