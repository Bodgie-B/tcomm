# Spine families and the morning spine: notes

Written 2026-10-31 by the session that built `SPINES_WEATHER_LOOKOUT_SPEC.md`
sections 1 and 2 (the framework and the morning). Branch `screensaver`, not
committed by this session.

## Spine families (section 1)

- `spine.h`: `enum SpineFamily` (evening, morning, midday, lookout),
  `spineFamilyFor(Inputs)`, `spineFamilyName`, `LoopPlan::family / fvar[8] /
  n_fbeats` (plus the morning's `fog_steps, fog_t0, fog_t1`), `spineBeginLoop`.
  The evening fields of `LoopPlan` are unchanged.
- The pick is a function of the inputs only (no rng): `Inputs::spine_force`
  (new; 0 auto, 1 + family forces it, the preview's `--spine`), then the
  Lookout window, then night mode (the evening), then the mood: morning in
  [0.5, 2.5), midday in [2.5, 3.5), evening otherwise. **Midday and the Lookout
  are not built: the pick falls back to the evening for them** (and `--spine
  midday|lookout` says so on stderr). The midday implementer removes that
  fallback line in `spineFamilyFor`.
- `app.cpp beginLoop`: `worldBeginLoop`, family, `spinePlan` (dispatches),
  `memoryBeginLoop`, `spineBeginLoop` (the morning's camp state). The family
  costs the loop rng nothing.
- `spinePlan / spineBuildTimeline / spineLoopTicks / spineFormatPlan /
  spineCritterScenes` dispatch on `plan.family`. `spineLayout` is still called
  inside `worldBeginLoop` before the plan exists, so it stays the shared camp
  layout; the morning's plan-dependent set-up is `morningLayout`, called from
  `spineBeginLoop` (judgment call: the spec has spineLayout dispatch, but the
  family's layout needs the plan).
- The length policy is one function, `spineFitLength(plan, mem, FitSpec)`
  (`spine_family.h`): a family passes its busy seconds, how many beat pauses
  it has and its filler-slot rule. The evening's old inline code moved there
  unchanged (plus the memory push at the end).
- Fillers: `kFillerFamilies[FILL_COUNT]` in `spine.cpp` (evening all; morning
  read, cairn, cloud_watch, stream_listen, birds_on_water; midday all but
  mug_sunset), `spineFillerAllowed`, `spineFillerBeat`. The evening's plan
  does not consult the mask (all allowed), so its rng draws are untouched.
- Majors: `MajorDef::families` in `majors.cpp`; `majorsPlan` takes a
  `MajorSlots {family, after[4], open}`. The pool's slot bits are now slot
  indices (evening setup/fire/dinner/foodbag, morning wake/coffee/bag_down).
  Midday masks are in: the daylight majors, the four stream vignettes; bathe
  is not weighted up yet (the midday implementer adds that).
- New anchors at the end of `Anchor`: `AT_GUY_PEG, AT_FAM0, AT_FAM1`;
  `CampLayout::guy_peg`; `SpineState::fp[2], fcrit, fstage, ft` (the
  family's scratch).
- `Memory::last_family, last_fvar[8]` (not printed by `memoryFormat`, so the
  evening's `--loops` memory column is unchanged).
- `spineFormatPlan` prefixes `family=evening` / `family=morning`. The morning
  line is `family=morning wake=W coffee=C bag_down=B break_camp=K douse=D
  leave=L fillers=... majors=... fog=N`.
- Tooling: `tools/review_clips.py` gets `info["family"]` from the plan line
  (`plan_family`; lines with no family are the evening); its numeric plan
  dict was already safe (the family token is not numeric).
  `tools/review_page.py` keeps the family in its own `family` field so the
  page's plan columns are unchanged. The review page HTML is not touched.
  `test_scene.cpp parsePlan` skips the `family=evening ` prefix.
- `screensaver/CMakeLists.txt` needed no edit: it globs `core/*.cpp` and
  `tests/test_*.cpp` (so `spine_morning.cpp` and `test_spines.cpp` are in).

### Evening regression

Before touching anything: `ss_preview --loops 10 --mood 4` for seeds 1 and 7
(and `--mood 3 --seed 1`) saved; four frame runs (seeds 1, 7 at mood 4; 3 at
mood 3; 5 at mood 0 night; `--every 97`, 200 to 360 frames, two loops and
more) hashed with their `--log-beats` logs. After the work: the `--loops`
output is byte-identical once the new `family=evening ` prefix is taken out,
and all four frame hashes and logs are identical. `test_spines` hard-codes the
20 seed 1 / seed 7 lines and checks them on every run (clang and GCC 16).

## Morning spine (section 2)

Loop start (`morningLayout`): tent pitched (`TENT_UP`, not lit, no shadow,
the mouse hole if memory has one), the pit laid with last night's ash
(`FirePit::ash`, drawn as a low grey mound with a char log, `FIRE_UNLIT`:
there is no `FIRE_OUT` state, the ash is the "out" look), the wood pile (a
judgment call: he rekindles from it), the food bag `BAG_HUNG`, the pack by the
tent, the cairn one stone shorter than it was left (as the evening has it), the
hiker inside the tent (`fx.on` false). The build beat is wrapped
(`kMorningBuild`): during the plants phase the tent pitches itself with an
overshoot, the pack (`PackProp::pop`, new) and the wood pile scale in, the bag
drops down its line, the cairn stacks up stone by stone, each at its own hashed
moment as the rocks and tufts do. The ash appears with the fire pit's stones.

Fog: `World::fog_extra`, added to `in.weather.fog` (clamped) in
`computeAppPalette` (`sceneWeather`), and only when it is above 0, so a loop
without it computes exactly what it did. The plan draws 11 to 16 steps
(0.55 to 0.8), held through the build and lifted (eased) to 0 by the end of the
coffee's pause, in 0.05 steps.

| # | Beat | Variants | Script (s, 40 seeds) | Beat (s) |
|---|---|---|---|---|
| 1 | wake | a: stirs, unzips the door (`Tent::door`), crawls out pitched forward, stands, stretches (REACH_UP, on his toes), scratches his head, looks round. b: out, steps on what he left by the door (the shoe; the mug if the raccoon has the shoe), yelps, three hops holding the foot, picks it up (the shoe goes on; the mug goes in the pack). c: a bird lands on the tent's front pole top and pecks (the tent twitches); he bursts out (the flap thrown open), it flies off; he watches it go, scratches his head, stretches. | 24 / 23 / 22 | 28 / 30 / 27 |
| 2 | coffee | shared: pot from the pack onto the pit, three blows (the ash thins, `FIRE_LIT` at fuel 0.36), mug from the pack, poured, sits against the reading rock looking out. a: three slow sips, long still looks into the fog. b: the first sip burns: jerk back, the far hand fans his mouth; then he blows on it before each of two careful sips. c: a chipmunk runs in behind the tent and sits up on the reading rock; he glances back, holds the mug in close, hunched over it; it gives up and goes; he sits back and sips. Shared end: the mug left beside him by the rock, back to the fire. | 65 / 64 / 69 | 69 / 68 / 74 |
| 3 | bag_down | a: under the branch, hand over hand, the bag lowered to the ground, untied, carried to the pack. b: lowered part way, it sticks; two yanks; it drops onto his hat (he still has the rope), bounces off to the ground; he staggers, hands on hips, picks it up, as a. | 29 / 37 | 33 / 41 |
| 4 | break_camp | shared: crouched at the front peg, three tugs. a: one will not come: a heave, he goes over on his back, sits up, gets up, fetches a rock, levers it out. b: a gust takes the fly (`GL_SHAPE SH_MAP` in the TENT slot, tumbling across the clearing); he runs after it, stamps on it, carries it back. c: (after the tent is down) it will not go in its sack: he stuffs it, kneels on it, sits on it, then it goes. Shared end: the tent slumps (`TENT_COLLAPSED`, sag 0 to 1), gathered up, rolled (`TENT_PACKED`, a stuff sack in his hands), packed. | 42 / 39 / 37 | 46 / 43 / 41 |
| 5 | douse | a: the pot off the fire, three kicks of dirt (`BIT_DIRT` flecks, the flame sinks with each), a last puff of smoke (the steam prop, one puff), stirs the ash with a stick. b: lifts the pot and pours it on: a steam burst (`Steam`, 3 to 5 puffs), the fire out at once; waves the steam away, sets the pot down. Shared end: the pot to the pack. | 23 / 19 | 26 / 23 |
| 6 | leave | shared: the pack on, a long look back. a: picks up the mug by the rock, off right along the far bank and over the ford. b: off left, stops at the edge of the clearing (short of the foreground pines, which would hide him), comes back for the mug he left by the rock, a nod, goes. c: the mug, a stone on the cairn (if there is room), a pat, off right over the ford. The last 2.5 s fade to black. | 34 / 33 / 43 | 39 / 39 / 48 |

Pauses (8 to 20 s) after each beat but the leave; fillers after coffee (one or
two) and break_camp (one or two); majors after wake, coffee and bag_down, each
with its 4 to 8 s stills. Loop length over 40 seeds: 539 to 650 s (same policy
and code as the evening; if it is still short after stretching the pauses a
filler is added back; whether that fallback ever fires on the seeds tried
was not checked).

Numbers (from `tests/test_spines.cpp`, clang; GCC 16 gives identical plans and
frames, checked by hashing 300 morning frames and three 3-loop `--loops` runs
from both builds):

- Palette rebuilds per morning loop: 12 to 17 (the loop's own, plus one per
  fog step: 11 to 16), against the spec's "about 15".
- Strips per frame over 4 whole morning loops, rendered every tick, build
  excluded: avg 7.56, max 15 (the max is a full repaint: a fog step or the
  fade). The evening's whole-loop figures in `test_scene` are 7.57 and 7.69,
  max 15. The headless preview does not report strips; these come from the test.
- `sizeof(App)`: 67,152 bytes of 98,304 (was 67,024).
- Overruns: 0 over 40 morning seeds (loop 0 of each, every variant of every
  beat seen), 0 over 36 alternating evening/morning loops (seeds 21 to 23, 12
  loops each), 0 in the three wake b loops with the mug (an ad-hoc run after
  the raccoon took the shoe).
- Incremental rendering equals a full render at 163 checkpoints through 4
  morning loops (the camp popping in, the door, the ash, the fly, fog steps).

Looked at in the frames (`ss_preview --spine morning --mood 1.5 --every 25`,
seeds 1, 2, 3, 7, 14, contact sheets and 2x crops): the camp popping in during
the plants phase, the fog then clear sky, wake a/b/c (the open door reads as a
dark opening with the flap rolled back), coffee a/b/c (the chipmunk on the
rock's top behind him), bag_down a/b (the bag on his hat, then on the ground),
break_camp a/b/c, douse a/b, leave a/b/c, and wake b with the mug.

### Judgment calls where the spec was silent

- The morning's durations are longer than the spec's "~s" for coffee (68 to
  74 s, the spec says 55): the pot, the rekindling and the mug come before the
  sitting, and cutting them made it read as a jump. The loop still lands in
  480 to 660 s.
- fishing_good is not in the morning's pool (it leaves a fish roasting for the
  evening's dinner). The other three stream vignettes are.
- Variant memory: a variant the last morning played is a quarter as likely
  (one weighted draw, so still one rng call), rather than never.
- The thing by the door in wake b is there only in wake b; in a and c he
  steps out to nothing. The shoe, if he steps on it, goes on (vanishes); the
  mug goes into the pack and comes out again for the coffee.
- He sits against the reading rock for the coffee (`AT_READ`), not at `view`,
  so the chipmunk can sit on the rock behind him and the mug is "left by the
  rock" for leave b.
- The tent's flat heap is what he kneels and sits on in break_camp c: the
  tent's geometry cannot draw a tall bundle (the pole heights come from
  `progress`), and there is no stuff-sack prop on the ground. It reads as
  squashing the flat tent; a bundle prop would read better.
- The kicks in douse a are a lean and a lift (the rig has no leg offsets); the
  flecks and the sinking flame carry it.
- Minors run in the morning's pauses and fillers only: the minors' calm-beat
  list (`minors.cpp`) does not name the morning's beats, so a morning beat
  counts as owning the critters. That matches how the evening treats beats it
  does not list; adding coffee and bag_down to the list is a one-line change
  if wanted.
- The wake c bird sorts in front of the tent it stands on through a new
  `CritterSlot::sort_y` (0 for everything else).
- The fly in break_camp b uses the major's glyph slot 0 (no major runs then).

### Not verified

- Anything on the board (timing, flash size of the new tables, the fog
  rebuilds' cost on the real panel). Unverified.
- The kick flecks and the last smoke puff are too small to judge in the
  sampled frames; seen only as the flame sinking and one puff.
- `--spine midday|lookout` just fall back to the evening; the midday masks are
  in but no midday loop has run.
- A parallel session added `core/scene/lookout_land.*` and
  `tools/lookout_stills.cpp` while this work was going on; they are compiled
  into `ss_core` by the glob and were in the final test runs, but this session
  did not review them.
