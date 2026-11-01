# Weather: notes

Written 2026-10-31 by the session that built `SPINES_WEATHER_LOOKOUT_SPEC.md`
section 4 (weather). Branch `screensaver`, on top of 05ace3b, not committed.
A midday session was editing the spine files in the same tree at the same
time; this work did not touch `spine*.{h,cpp}`, `script.h`, `memory.*`,
`buildup.*`, `test_spines.cpp`, `test_scene.cpp` or `tools/`.

## What is where

- `core/scene/weather.h/.cpp` (new): `Weather` (in `World::weather`), the
  code table, the random draw, every effect's step, draw and dirty marks.
- `core/palette.h/.cpp`: `computePalette(mood, tint, flash, out)` (the
  3-argument form is the same with flash 0, bit for bit); overcast now also
  hides the stars (STAR toward SKY1).
- `core/scene/app.cpp`: `weatherPlan` at loop start (after the family and
  plan, so the shower can be placed in the loop); `sceneWeather` adds the
  weather's overcast/fog and returns its flash step; the palette is recomputed
  when the flash step changes (exact compare: it moves in fixed steps);
  `computeAppPalette(a, force)` skips `setPalette` (no rebuild, not counted)
  when the new palette is byte for byte the one in use, except at loop start
  (`force`: new geometry).
- `core/scene/world.h/.cpp`: `Weather weather`; `ACT_WXBIT` (the weather's own
  Bits sorted among the ground actors; `WORLD_ACTOR_MAX` 91 -> 95);
  `weatherApplyPose` in `stepHiker`; `weatherStep` / `weatherPlaceHeld` before
  the actor sort; `weatherTrack` last; in `worldDrawDynamic` the shimmer first,
  fog after the glints (before the ground actors), the near layer (breath,
  hiss, rain, snow) after the smoke, before the fade.
- `core/scene/ambient.cpp`: sway x `weatherSwayMul`, smoke x `weatherSmokeMul`,
  a lit fire capped by `weatherFireCap` (all exactly 1 in calm, dry weather).
- `core/scene/props.h/.cpp`: `Tent::sway` (wind lean added to `wobble`),
  `Tent::snow` (ridge and upper face), `tentRidgeEnd`.
- `core/scene/hiker.h/.cpp`: `POSE_HUG`.
- `core/scene/majors.h/.cpp`: `MJ_RAIN_SHELTER` (id 16), its script, tick and
  gating; `static_assert(MJ_COUNT <= 17)` (see the asks).
- `core/scene/minors.cpp`: one hook: no minor starts while the hat or the fan
  gag runs.
- `core/scene/landscape.h/.cpp`: `landscapeCampPineBounds`,
  `landscapeDrawNearPines` (the near pines over the fog bands; the shimmer
  skips their columns).
- `core/ss_app.h`: the `weather_code` comment lists 7 storm (no field changed).
- `sim/preview_common.*`: `--weather storm`; `--loops` appends ` weather=K`;
  `--log-beats` prints `loop=L weather=K` after each loop start line (a line
  `tools/review_clips.py`'s patterns ignore).
- `tests/test_weather.cpp` (new), `tests/test_palette.cpp` (`testFlash`).

## The mapping table (`weather.cpp kCodeKinds`, one place)

| `Inputs::weather_code` | kind |
|---|---|
| 0 | clear |
| 1 | rain |
| 2 | snow |
| 3 | wind |
| 4 | heat |
| 5 | fog |
| 6 | chill |
| 7 | storm (new; `--weather storm`) |
| anything else | clear |

**Unverified:** WeatherGate's real OUTSIDE codes (Zambretti) are not in the
repo; the host is assumed to map them to these codes, or the table is edited.

No data (`have_outside` false): clear half the time, else a draw weighted by
the mood key, interpolated between keys (`kMoodWeights`): fog likelier at dawn,
heat by day, storm at golden hour, chill at night, snow only at night, dawn
and dusk. Extras (30 % of loops, at 90 s .. length - 120 s): a gust (on clear,
heat, chill, rain) or a short shower (on clear, wind, chill), never a shower
in snow or fog, never a gust in fog. All of it from `Weather::rng`, seeded
from the loop seed (`seed ^ "WEATHER!"`), never from the loop rng.

## Effects: mechanism and cost

Strips are whole 800 x 32 rows (15 a frame). Numbers from `test_weather`
(GCC 16 and clang give the same plans; frames rendered every tick, build excluded).

| Kind | Mechanism | Strips/frame avg (clear 7.48) | Palette rebuilds per loop |
|---|---|---|---|
| wind | sway x (1 + 1.5 |wind| (0.35 + 0.65 gust)); gusts every 6-14 s; tent apex `sway` whole px with the gust; 2-3 leaves (weather Bits, BIT_LEAF) per gust from off the right edge; the hat gag (below) | 7.53 | 1 (evening); the morning's 12-17 are its own fog steps |
| rain | per strip, a hashed pattern of 1 px diagonal streaks from (step, strip, j), step = (tick + strip) / 3, so each strip redraws every 3rd tick, staggered (5 strips a tick); amount in 1/16 steps (a step repaints all); drips off both ridge ends (BIT_DROP -> a puddle ring), rings on the stream (BIT_RING, at most 2), fire capped at lerp(1, 0.5, rain), smoke x (1 + 0.6 rain), hiss (GL_ARCS, SMOKE) every 5-10 s; overcast 0.6 | 10.02 | 1 |
| snow | 150 global hashed flakes (2 px, or 3 px soft) falling 11-23 px/s on sines, each strip drawing them at its own step time (same stagger as rain); `Tent::snow` grows over ~140 s in 1/12 steps; shaken off once when he walks past the pitched tent (two puffs, the tent sways); overcast 0.7 | 9.99 | 1 |
| fog | palette fog 0.45 screened over the scene's (the morning's 0.05 lift steps each stay a step); 3 bands (rx 170-260, ry 13-19) at y 284-318, three nested spans each of SKY4 at alpha up to ~120, drifting left 3 px/s, breathing in 6-steps; the near pines drawn again over them; repaint only when a band's whole-px x or alpha step changes | 7.89 (morning) | 1 (+ the morning's own fog steps) |
| heat | the 24 rows above the horizon shifted in place (two sines, +-2 px, more near the horizon) on the 12.5 Hz sub-tick; the near pines' columns are skipped; strips 7-8 marked only when an offset changed; the fan gag; a sweat drop (BIT_DROP from his forehead) every 9-16 s | 8.28 | 1 |
| chill | breath puff (a small SMOKE ellipse drifting out ahead of his face, 1.3 s) every 4-6 s while he is still; POSE_HUG over his upper body in pauses (1 s in, 3-7 s held, 1 s out; lets go at 2.5/s if anything else wants him); a 0.45 s shiver (+-1.5 px hips) every 9-18 s | 7.54 (night) | 1 |
| storm | rain at k 1 (eased to 0.4 by rain_shelter for 40 s), wind -0.65 (sway, tent, no leaves, no hat), lightning: the first strike 20-40 s after the build, then every 100-140 s (rain_shelter adds its two and pushes the next one 45 s on); flash 1 -> 0 over 0.3 s in palette steps 1 / 0.5 / 0 (3 rebuilds a strike); overcast 0.8 | 10.04 (evening), 10.25 (night), 10.05 (morning) | see the table below |

The flash (palette.cpp): SKY0-4, SUN, STAR, the clouds, RIDGE_FAR/MID and
TREES_FAR toward 0xD6DEEA by 0.72 x flash (0.5 under the night dim), RIDGE_NEAR
0.6 of that, WATER 0.5, GROUND 0.15; then the night dim and its caps as before.
`test_palette testFlash`: at flash 1 and night_dim 1, at 25 moods x overcast 0
and 0.8, every slot holds `NIGHT_LUM_MAX` (highlights `NIGHT_HIGHLIGHT_MAX`), no
pure white; at night the sky still lifts (night storm SKY2 lum 0.070 -> 0.275);
flash 0 is the plain palette exactly.

Rain over budget: the rain and the storm average ~10 strips a frame against
the proposed 8 (the majors' test allows 10). The whole frame is under the
rain, so a strip redraw at 8.3 Hz is the floor short of drawing fewer rows of
rain. A stagger of 4 would save about 0.7 a frame. Left at 3; board cost
**unverified**.

### Cache rebuilds per loop (after the chief's budget note)

A first version struck every 14-40 s with 4 steps a strike (and a shower
ramped the overcast in 0.15 steps): 97-142 rebuilds a storm loop, which is
what the midday session measured. Now: 3 steps a strike, a strike every
100-140 s, the shower leaves the sky alone, fog and overcast are constant
for the loop, and an identical palette is not rebuilt. Measured whole loops,
6 seeds per cell, family forced, kind from the data (`random` = no data):

| Kind | evening (mood 4) | morning (mood 1.5) | midday (mood 3) |
|---|---|---|---|
| no data (random) | 1 | 10-14 | 1 |
| clear | 1 | 9-14 | 1 |
| rain | 1 | 10-14 | 1 |
| snow | 1 | 11-16 | 1 |
| wind | 1 | 9-14 | 1 |
| heat | 1 | 10-14 | 1 |
| fog | 1 | 12-14 | 1 |
| chill | 1 | 9-14 | 1 |
| storm | 16-25 (6.5 strikes) | 26-34 (6.0 strikes) | 16-22 (6.2 strikes) |

(The morning storm figure measured 26 to 37 across later runs; `test_weather`
asserts the bound.)

The 1 is the loop's own rebuild; the morning's 9-17 are its own fog lift
(the identical-palette skip drops the steps that land on the same 565
colours, so it is now often fewer than 1 + fog steps). `test_weather` checks
these bounds (weather adds 0 but in a storm, at most 26 there). Each rebuild
is a full static redraw plus 15 strips: cheap on the PC, **unverified on the
board**.

`sizeof(App)`: 68,096 of 98,304 at 9fef80d (`sizeof(Weather)` 888; the
Lookout adds its land after this).

## rain_shelter (`MJ_RAIN_SHELTER`)

Script (`kShelter`, 44 s beat): still 0.8 s; strike 1 (cue); he looks up at the
sky (2.4 s); strike 2 and three thunder arcs (GL_ARCS in the sky, bulging down);
up with a start; runs (`go(AT_DOOR, kRun)`, round the fire); faces the door,
lunges in pitched forward (the flap opens, he is gone at 42 %), the tent sags
(0.32) and rebounds (`bump` 0.08, a wobble); rain eased to 0.4; if it is dusk
or later (mood >= 4.5, < 0.5, or night) the lantern lights and his shadow sits
on the wall; 7 s; the flap opens; he is at the door crouched, head out,
looking up at the sky; crawls out; stands; lantern back as it was; the flap
back as it was (open in the morning, shut in the evening); `TO_SIT` like the
other outside majors. Natural strikes wait while it runs.

Slots: `2 | 4` (evening after fire or dinner); the morning limited to slot 1
(after coffee) in `majorsPlan`; midday gets `2 | 4` of its own slots until its
implementer says otherwise. Also in `majors.cpp` (the chief's asks): fishing_good
is the evening's only (kMid dropped), and midday weights bathe x 2.5. Weight 6 (a storm nearly always brings it: 7 of 10
evening and 5 of 10 morning storm loops in the test, the rest lose to the ring
or to the loop-length trim). No overruns over those 20 loops.

## The hiker overlays

- Free when: in a pause with the time left, standing or sitting (not
  blending, not walking), no script, major or minor holding him; the hat and
  fan also want no critter about. `minors.cpp` holds the next minor while the
  hat or fan runs.
- Hat (wind, once a loop): the wind takes it (its own gust), it flies left and
  lands with two hops 36-62 px away on the clearing (never toward the stream;
  test: always in frame and out of the corner); he reaches up, walks after it,
  crouches, puts it on, walks back to the exact spot, faces as he did and
  takes his pose again. 57 of 60 wind loops in the test (evening and morning).
- Fan (heat, twice a loop at most): near hand to the hat, hat off, fanned for
  3.2 s, back on. 20 in 10 loops.
- Hug (chill): above. ~30-45 s a loop.

## Regression

- `ss_preview --loops 10 --mood 4` seeds 1 and 7: identical to before this
  work but for the new trailing ` weather=K` (both seeds include random storm,
  wind, heat, rain and chill loops). `test_spines` (its hard-coded 20 lines)
  passes. `test_weather testRandomKeepsPlan`: random weather plans exactly what
  clear weather plans, and hands on the same next-loop seed, 6 loops x 2 seeds.
- `--seed 1 --mood 1.5 --spine morning --loops 10`: differs from the line
  saved before this work, because the midday session changed
  `spine_morning.cpp` (review fixes) meanwhile; on the current tree the random
  -weather output equals the `--weather clear` output line for line.
- Clear-weather frames (`--weather clear --every 97 --frames 160`, hashed
  against the pre-work build): seeds 1 and 7 at mood 4 identical. Seed 5 at
  night differs, by design: "clear" from the data still takes the random
  extras, and that seed's first loop has a gust and its second a shower.
  Seed 3 morning differs with the concurrent `spine_morning.cpp` edits.

## Judgment calls

- **rain_shelter needs a storm from the data.** A random storm (no data)
  rains and strikes but does not add the major: putting it in the pool changes
  the weighted picks, so the loop's plan, its length, the memory ring and the
  next loop's seed would all move, and the evening regression lines (and
  `test_spines`) with them. With this rule a seed plays the same story
  whatever the random weather is.
- **No palette transient from the random extras.** The shower draws rain
  without darkening the sky (a passing shower); with an overcast ramp it cost
  8 rebuilds and broke `test_spines`' exact morning rebuild count. Fog screens
  over the morning's fog for the same reason (each lift step stays a step).
- Wind always blows left (`wind` in -1..0): the clouds drift, the smoke
  trails and the grass leans that way already.
- The weather's leaves, drips, rings, sweat drops and snow puffs are Bits in
  the Weather's own 4 slots, not `World::bits`: they never take a slot the
  bear, a major or a minor wants, and the minors' rng sees nothing.
- No FOG palette slot: the 32 slots are full; the bands use SKY4 (the mood's
  horizon colour, which is what the spec asks the slot to be).
- The breath puff is its own small ellipse (SMOKE, out ahead of the face): the
  boil-over's Steam puff rises 34 px and read as steam, not breath.
- The hat gag also runs when he sits (he gets up and sits back down):
  standing still in a long enough pause came up in 1 of 60 loops.
- Lightning strikes during other majors too; only rain_shelter holds them.
- Overcast hides the stars (all overcast, the host's included).
- The heat shimmer skips the near pines' columns, and the near pines are drawn
  again over the fog bands; both through two small landscape exports.

## Asks for the chief

1. `Memory::sets` (memory.h) and the sets built in `spine.cpp` are 16-bit:
   rain_shelter is id 16, so it falls outside the recent-set rule (harmless:
   it is weather-gated). Widen to 32 bits when memory.h is next open; then
   `majors.h`'s assert can go back to "<= 32".
2. Should a random storm (no data) also plan rain_shelter? Then the evening
   regression lines in `test_spines` need re-capturing (storm loops change).
3. A spine-side "fan with the hat" filler for heat, and a slot for the snow's
   fox snowball (brief section 8), if wanted: both are spine work.
4. `test_spines`' morning test checks rebuilds == 1 + fog steps. With the
   identical-palette skip (asked for) a fog step that lands on the same 565
   colours is no longer a rebuild, so that check now fails on all 40 seeds
   (9-14 rebuilds for 11-16 steps); it wants `reb <= 1 + plan.fog_steps`. It also runs with random weather on: no
   random storm falls among seeds 1-40 at mood 1.5 today, but one would add
   strikes; consider `have_outside` true, code 0 there.
7. `test_midday`'s pinned midday `--loops` lines need re-capturing: dropping
   fishing_good from midday and bathe x 2.5 (the chief's asks) change the
   midday's major picks. Weather is not the cause: on the current tree the
   midday's random-weather lines equal its `--weather clear` lines.
6. `test_midday` copies `computeAppPalette`'s composition: the composition
   is unchanged (inputs' tint, + fog_extra, + `weatherTint`, flash as the
   separate `computePalette` argument), but the flash steps are now 1 / 0.5
   / 0 and a byte-identical palette is no longer counted as a rebuild.
5. Midday: `MJ_RAIN_SHELTER`'s families include midday with slots 2 | 4; the
   midday implementer should confirm which of their beats those slots follow.

## Not verified

- Anything on the board: rebuild cost of the flash steps (the main risk),
  the rain's strip count, flash size of the new tables.
- The mapping table against real OUTSIDE codes.
- Motion read in a video (only stills and 2x crops were looked at): rain at
  8.3 Hz per strip, the shimmer's speed, the leaves' path, the tent's sway.
- The fan's hat position at the hand is approximate (the hat item's base at
  the hand).

## Review fixes (chief, after the independent review of 9fef80d)

- Night lightning was still a flash: the sky mean jumped 0.071 to 0.273 in
  one tick (within the caps, but brief section 11 wants a palette shift).
  At night a strike is now a quarter the strength (flash 0.3, 0.15, 0) and
  decays over 1 s instead of 0.3 s, still three palette steps. Measured on
  seed 3, night storm, the first strike: sky mean 0.071, 0.128, 0.094, 0.071.
- The smoke puffs' rain swelling (`weatherSmokeMul`) is now sampled on the
  ambient sub-tick only, so a puff never changes size on a tick nothing
  marks dirty (the every-tick incremental check had found two such frames
  inside rain_shelter).
- The chill shiver runs only when no script holds him, as the header said.
- A gag cut short by a beat starting under it (never seen in 480 loops, but
  the path exists) now puts him back where the gag found him, in his pose
  and not walking, before the beat's first step reads him.
- The wind's sway multiplier is capped at 2.2 (the tuft bounds allow a 16 px
  tip swing; the uncapped 2.49 left 0.3 px).
- Not changed: random weather with no data ignores the season (there is no
  date in `Inputs`); snow can come on a summer night. **Unverified against
  the brief's "suits the season" until a date or season input exists**
  (stage 8). The midday nap's stolen bar writes no memory (`last_stolen`
  holds an `ItemId`); the spec is amended to say so.
