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
  [0.5, 2.5), midday in [2.5, 3.5), evening otherwise. (Written before the
  midday landed: the mood picks the midday since 9fef80d; `test_scene` forces
  the evening for its own checks. The Lookout's fallback goes when it lands.)
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

## Midday spine (section 3)

Written 2026-10-31 by the session that built `SPINES_WEATHER_LOOKOUT_SPEC.md`
section 3, alongside a parallel session building the weather (and review
fixes to the morning landing in the same tree). Not committed.

Files: `core/scene/spine_midday.cpp` (new), `tests/test_midday.cpp` (new);
`spine_family.h` (the midday's entry points, `MiddayBeatId`), `spine.h`
(`SpineState::fbit, fhold, fk`; comments), `spine.cpp` (dispatch only: plan,
`spineBeginLoop`, critter scenes, timeline, ticks, format; `spineFamilyFor`:
a forced midday now plays the midday, the mood's pick still falls back, see
below). `script.h`, `kFillerFamilies`, CampLayout
and LoopPlan untouched: no new anchors were needed (each beat sets
`AT_FAM0/AT_FAM1`).

Loop start (`middayLayout`): the camp as the morning's layout has it, popped in
by the build the same way (tent, pack, bag on its line, cairn one stone short),
the pit cold under its ash, the wood pile (0.45 of a load when the afternoon
will fetch wood, else a full one), him away: no pack on him, out of the scene
until the return. No fog, no palette steps of its own.

Timeline: build, return, lunch, wade, nap, map, afternoon, a pause (8 to 20 s)
after each but the afternoon; fillers (all but mug_sunset) after lunch (1st,
2nd/3rd) and nap; majors after return, lunch and wade (`MajorSlots {return,
lunch, wade}`, open 1|2|4); the afternoon's last 2.5 s fade to black. Length:
the shared `spineFitLength` plus the morning's "add a filler back" step.

| # | Beat | Variants | Script (s, 60 seeds) | Beat (s) |
|---|---|---|---|---|
| 1 | return | shared: in from the right over the ford, the walking stick in his hand (`CARRY_STICK`, no pack), the stick laid down behind his seat by the pit (`ITEM_STICK` lying, 0.75 size). a: drops onto his seat, hat off into his hand, six seconds of fanning his face chin up, hat back on. b: limps from the ford (a dip and a head drop on one step in two, a slower walk), sits, boot off (two tugs, the boot in his hand: `ITEM_SHOE`), held out and shaken, a stone falls out (`BIT_DIRT`), he peers into it, boot on. c: stands picking burrs: off his sleeve (twice), his trousers (bent over, twice), one off his hat brim, each flicked away (a fleck); then sits. | 31-32 / 36-37 / 32 | 35 / 40 / 36 |
| 2 | lunch | shared: rummages the pack, out come the pot (cold) and the fork, carried to his seat; sitting, the pot is held out over his knees in the far hand (`HOLD_POT`), the fork in the near hand; bites from pot to mouth; shared end: fork in the pot, back in the pack, back to his seat. a: a bee (`BIT_BEE`, its own flight) comes in from the right and hovers over the pot; a swat (the fork hand); it darts off, waits, comes back; a glare; he eats fast, hunched, six quick bites; it gives up. b: a chipmunk runs in from the left in front of the pit, sits, edges a hop nearer each bite (or every 2.6 s), dashes for the crumbs beyond his feet, takes one (`CC_CRUMB`) and runs; he watches it go, shrugs one-handed (the pot stays out), a last bite. | 45 / 44-46 | 49 / 52 |
| 3 | wade | shared: to the bank below the camp (`F.x + 102`), sits, both boots off (two tugs each, the boots go down beside him), wades in (`fx.wade`) to midstream; shared end: back to the bank, out, sits, boots on, back to the fire. a: a fish (the major's `SH_FISH` glyph, small and dark, just under the surface) circles his shins; head down, he turns round with it as it passes behind him, bends slowly, grabs: splash rings and drops, it darts off and is gone under; he shakes his hands, hands on hips. b: half way in he slips: arms up flailing, down he sits in the water (belly deep), a splash (rings, drops), sits there, a shrug, gets up. c: a stone off the bed, a throw: plop (one touch, `Skip` n 1); a sigh; another: it skips (`skip_n`, 3 or 4); hands on hips. | 37 / 28 / 39 | 41 / 32 / 43 |
| 4 | nap | shared: to the foot of the camp pine (`camp_x - 50`), sits, lies back (`POSE_LIE`, head toward the trunk, hands behind his head), the hat down over his face (`ITEM_HAT` placed on his face: `HOLD_HAT_FACE`); shared end: a stretch, the hat off his face and on, sits up, up, back to the fire. a: a chipmunk runs in to the pack by the tent, noses in, the pack shakes, out it comes with a bar (`CC_BAR`), a nibble, a look round, away; he sleeps through it. b: a leaf comes down off the pine onto his middle (tip up, above his outline) and stays; later his hand comes up over his chest and sweeps it off: it flicks up and flutters down to the grass in front of him, and stays there; he sleeps on. c: a fly (`BIT_MOTH`) comes and circles his head; a lazy swat in his sleep, it hops away and comes back; a bigger swat knocks the hat off his face onto the grass; he sits up with a start and looks round; the fly goes; he turns, picks the hat up and puts it on. | 45-49 / 44-45 / 41-43 | 52 / 49 / 47 |
| 5 | map | shared: sits against the reading rock looking out, the map (`SH_MAP` glyph, `CLOUD_LIT`: pale, so it reads in front of his silhouette) out of his pocket and opened, held up in both hands. a: studies it, turns it round (half a turn), again, scratches his head with one hand (the map in the other), a long look at the ridge with the map lowered, turns it again, a nod, folds it in quarters, pockets it. b: studies, turns it, a gust takes it up out of his hands, he lunges and has it back, folds it fast, lifts himself and sits on it (a sliver of it shows under his thighs), a sigh, up, crouches, pockets it. | 30 / 26-27 | 34 / 30 |
| 6 | afternoon | a (the pile is small): to the foot of the camp pine, gathers deadfall (`CARRY_WOOD`), carries it to the pile, the pile up to a full load. b (the pile will do): picks up the walking stick from where it lies, lays it on the pile (size +0.12), squares the pile with two pats. shared: the book back in the pack if the read filler left it out (+9 s when read is planned); the mug from the pack; sits by the pit facing out over the valley, two slow sips, the mug down beside him, still; the last 2.5 s fade. | 45-47 / 35 (+ the book) | 51 / 40 (+9) |

Numbers (from `tests/test_midday.cpp`; clang and GCC 16 give the same plans,
the pinned midday and morning lines pass under both):

- Loop length over 40 seeds: 552 to 650 s.
- Overruns: 0 over 40 midday loops (every variant of every beat seen) and over
  36 mixed loops (seeds 31 to 33, 12 loops each: midday, midday, evening,
  midday, morning, midday in turn).
- Strips per frame over 4 whole midday loops, rendered every tick, build
  excluded: avg 7.65 (7.66 GCC), max 15 (a full repaint: the fade, a palette
  change). Incremental equals a full render at 162 checkpoints.
- Palette rebuilds: the midday adds none of its own (no fog lift). The loops
  measured show up to 97 to 113 a loop, all from the weather layer the other
  session is building (flashes, showers); not the midday's.
- `sizeof(App)`: 68,072 to 68,080 bytes of 98,304 as the weather session's
  `World` moved under the runs; the midday's own share is 8 bytes
  (`SpineState::fbit, fhold, fk`).
- Every beat ends (checked on its last tick) with him visible, not walking,
  the hat on, nothing carried or held, the boots on, out of the water, no bit
  of the beat's still flying; the loop ends black with no critter, him sitting,
  the book not left out.

### Regression

`ss_preview --loops 10` for seed 1 and seed 7 at mood 4 and for seed 1 at mood
1.5 `--spine morning`, plus frame hashes (220 frames, `--every 97`, those three
and seed 5 at mood 0 `--night`, with their `--log-beats` logs): byte-identical
between the working tree and the same tree with the midday's changes taken out
(`spine.cpp/.h`, `spine_family.h` from HEAD, `spine_midday.cpp` removed). Against
the files saved before this work started they differ only by the weather
session's `weather=` column and (the morning) by its review fixes to
`spine_morning.cpp` (the leave's book pickup lengthens some loops). The evening
lines also match the strings `test_spines` holds. `test_midday` pins the
evening's 20 lines, the morning's 10 (captured from the tree with the review
fixes: re-capture if the morning changes again) and the midday's own 10.

### Judgment calls where the spec was silent

- fishing_good is dropped from the midday's plan after `majorsPlan` (it props
  a fish over the fire for the evening's dinner; at midday the pit is cold and
  lunch is over). The pool's midday mask still has it; removing `kMid` there
  would make the drop a no-op (see the asks).
- The return's walking stick stays at camp: laid behind his seat, put on the
  wood pile in afternoon b, left lying (camp gear by the fire) after afternoon a.
- The afternoon has two variants (the spec gives it one shape): "if the pile is
  small" is the variant, a fetches wood, b adds the stick and squares the pile.
  He sits facing out over the valley for the end, not the fire.
- The afternoon is 51 s (a), not ~25: the walk to the pine and back. Lunch,
  wade, nap run longer than the spec's seconds for the walks to the pack, the
  bank and the pine; the loop still lands in 480 to 660 s.
- Lunch: the pot is held out over his knees in his far hand. Set on the ground
  in front of him (as the evening's dinner does) his knees hide it.
- Nap a: the chipmunk takes a bar out of the pack (`CC_BAR`, as raccoon_pack
  does). `Memory::last_stolen` is not written: it holds an `ItemId` (shoe,
  mug, book) and `memoryFormat` prints its name; a bar is not an item (see
  the asks).
- Nap: he lies at the foot of the camp pine ("the shade"), not at `camp.lie`.
- The hat is never `held` through World: World puts a held item at the
  midpoint of his hands, which is wrong for a one-hand fan, a boot, a hat over
  his face. `SpineState::fhold` says what his near hand has and `middayTick`
  places it after the hiker's step (the item `down`, drawn where it is put).
- "Two shoes down" in the wade is one `ITEM_SHOE` (the props draw one boot;
  two tugs per boot are played). See the asks.
- The nap's leaf is marked `resting` while the midday drives it: the minors'
  `gustStep` takes every non-resting leaf that is not leaf_fall's own and
  would carry it off. See the asks.
- The return walks in from the right over the ford in every variant (the
  spec says "from the right"); the hiker is in the scene from the return's
  first tick, off the frame's right edge.
- Majors can follow return, lunch and wade; the variant memory is the
  morning's (a variant the last midday played is a quarter as likely).
- The fish in wade a is drawn over the water on both sides of him (sorted in
  front): behind him it would sit under his legs' water redraw and vanish.
- Minors run in the midday's pauses and fillers only (its beat names are not
  in the minors' calm list), as for the morning.

### The mood picks the midday (resolved by the chief at 9fef80d)

The fallback line is gone; `test_spines`, `test_midday` and `test_scene` were
updated with it (the last forces the evening). What follows is the history.

The brief asked for the fallback line in `spineFamilyFor` to go. Taken out
whole, two of `test_spines`' family-pick cases fail (they assert mood 2.5 and
3 play the evening, "midday: not built") and `test_scene` fails broadly: it
runs its evening checks (dinner visitors, the raccoon in the close, plan
parsing) at `makeInputs(3.f)`, which then plays the midday. Neither file is
this session's to edit, so the fallback now applies to the mood's pick only:
`spine_force` (`--spine midday`) plays the midday, mood [2.5, 3.5) still plays
the evening. The switch is one line in `spineFamilyFor` (`if (f ==
SF_MIDDAY) f = SF_EVENING;`, commented there), to be made with:
`test_spines.cpp testFamilyPick` cases `{2.5f, ...}` and `{3.f, ...}` wanting
`SF_MIDDAY`; `test_scene.cpp`'s evening checks moved off mood 3 (mood 4, or
`spine_force = 1 + SF_EVENING`); `test_midday.cpp testFamilyPick`'s mood cases
flipped to `SF_MIDDAY` (its comment says so). Checked: with the line removed,
everything else in `test_spines` passes; `test_scene` was not run to the end
that way.

### Asks for the chief

- The switch above (one line plus the three tests).
- `sim/preview_common.cpp`: `--spine midday` still prints "not built yet: the
  loop plays the evening" (`if (f >= 2)` should be `f >= 3`), and the help text
  says the same.
- `majors.cpp kMajors`: bathe "weighted up" for the midday (the spec) needs a
  per-family weight (a `midday_weight` or a family multiplier in `majorsPlan`);
  not done. `fishing_good`'s families: drop `kMid` (the midday drops it anyway).
- `minors.cpp gustStep`: skip a leaf the spine owns (a flag on `Bit`, or the
  spine's bit index), so the nap's leaf need not be marked `resting`.
- `props`: a pair of boots (`ITEM_SHOE` with `alt` drawing two side by side)
  for the wade's "two shoes down".
- `memory`: a way to remember a stolen bar (an `ItemId` for food, or a
  `last_stolen` that is not an item) if nap a should carry over.
- `minors.cpp kCalm`: whether lunch / map / afternoon should be calm beats for
  the minors (one line each); left as the morning's are.

### Not verified

- Anything on the board. Unverified.
- The weather layer was being built in the same tree while these runs were
  made; the palette mirror in `test_midday`'s corner check (`scenePalette`)
  copies `app.cpp sceneWeather` as it was at the time and will need to follow
  it if that changes.
- The limp in return b (a lean and head dip on one step in two; the rig has no
  leg offsets) was seen only in sampled frames at 2.5 fps; it reads as a heavy
  walk more than a limp at that rate.
- The sat-on map in map b shows as a few pale pixels under his thighs; it
  reads mostly as the map vanishing as he hops.
- Return c's pick off the hat and the head scratch in map a are small arm
  moves near the head; seen in the frames but faint at 1x.
- A dark speck sitting on the stream's near bank through seed 1's wade is not
  the midday's (no bit, critter or glyph of its own there); not traced.

Looked at in the frames (`ss_preview --spine midday --mood 3 --every 10`,
seeds 1, 2, 3, 7, 9, 14, sheets and 2x to 4x crops): the build, return a/b/c,
lunch a/b, wade a/b/c, nap a/b/c, map a/b, afternoon a/b (b with the book
pickup, seed 3), the fade into the next loop. Fixed on the way: the pot hidden
by his knees (now held out), the fish hidden under the water redraw (now just
under the surface, in front), the nap leaf hidden inside his outline and taken
by the minors' gust (now on top, marked resting), the brush arm buried in his
outline, the chipmunk's crumbs on the pit's stones (moved in front), the boot
held against his face (now held out), the read filler's book left out to the
fade (the afternoon picks it up).

## Lookout (section 5)

Written 2026-10-31 by the session that built `SPINES_WEATHER_LOOKOUT_SPEC.md`
section 5 on top of the landscape and tower already in `lookout_land.*`
(`lookout_land_notes.md`). Branch `screensaver`, not committed.

Files: `core/scene/lookout.h/.cpp` (new: plan, fit, beats, scripts, birds,
glyphs, the scene's static and dynamic draws, the palette tweak),
`tests/test_lookout.cpp` (new); `lookout_land.h/.cpp` (layered draw, build
parameters, the tower split for occlusion, `lookoutHeightAt`,
`lookoutCabBounds`), `world.h/.cpp` (`World::lk`, `look`, `mood_override`;
the Lookout's own tick, sort, static and dynamic paths), `app.cpp`
(`sceneMood`, the palette tweak, `appPalette`), `spine.h/.cpp` and
`spine_family.h` (dispatch; the fallback in `spineFamilyFor` is gone;
`LoopPlan::fsec[8], fsteps, fbirds`; `spineBeginLoop(World&, Rng&)`),
`memory.cpp` (a Lookout loop leaves memory alone), `hiker.h/.cpp`
(`POSE_CLIMB`, `POSE_PERCH`, `hikerApplyClimb`, `breath_rate/breath_scale`),
`ambient.h/.cpp` (the bird's size, colour, glide, a partner, `bird_manual`),
`majors.h/.cpp` (two shapes, `SH_PERCH`, `SH_CHIP`; the table and weights
untouched), `ss_app.h` (`appPalette`, a test hook), `sim/preview_common.cpp`
(the "not built" message gone), `tests/test_spines.cpp` (the pick case for the
window now wants `SF_LOOKOUT`; no pinned line touched).

### What plays

| # | Beat | s (20 seeds) | What |
|---|---|---|---|
| 0 | build | 13 | the frame up from black; the sky fades in from SKY4 (the hill there from the start, as the camp's ground is), the three valley layers rise from behind the hill far to near, the trail draws itself up from the foot, the pines and grass pop in (popcorn order, overshoot), a hold |
| 1 | approach | 38 to 43 | in from off the left edge at 90 px, up the switchback legs to the last left turn; his height is 90 at the trail's foot and 44 at the stair foot, linear in screen y and ratcheted (never grows); his pace 0.5 heights a second (a stroll), up to 0.75 on a long trail so the beat stays near 40 s |
| 2 | trail_pause | 8 | stops, turns uphill, out of breath (bent, chest heaving), hand to his brow looking up; the hawk (the ambient bird at 1.8x, gliding, 22 px/s) crosses the top right to left over this and the next beats |
| 3 | reveal | 12 | he watches, head up, then hands on hips; the tower rises from behind the ridge as a flat silhouette (6 s, ease out), then the frame, the cab and roof, the windows switch in (`lookout_land`'s staged reveal) |
| - | pause | 4 | |
| 4 | climb | 30 to 36 | the last leg to the stair foot; a bird glides down onto the deck rail's east end; three flights at 0.2 heights a second with the climbing gait (`hikerApplyClimb`: knees high in step, leaning in, near hand down on the handrail) and two landings at 0.4; on the first landing he turns and looks out over the valley for 6 s; when he moves on the bird flies off west |
| - | pause | 4 | |
| 5 | explore | 50 | peeks into the front-right and front-left panes (leaning to the glass), round the corner to the west pane, along the front to the east end and into the cab (behind its front wall: seen only through the far windows), a hand on the fire-finder's table, out, two taps on the compass plate on the front rail (a small ring of arcs each), a look down at the chipmunk; the chipmunk comes out from behind the left leg onto the middle girt at the start and watches him (turns his way, head up) |
| - | pause | 4 | |
| 6 | settle | 20 | the chipmunk goes; he walks to the west edge of the deck, sits on the edge facing west, legs over, forearms forward on the rail (`POSE_PERCH`, his feet let down below the seat as the pose blends) |
| 7 | sunset | 113 to 117 | nothing moves but his breath (4 s, 1 px) and the sky: `mood_override` steps 4.0 to 5.9 in 8 to 10 even steps (0.19 to 0.24 each; the review cut it from 10 to 12 so a step comes every 11 to 15 s as the brief says), step k at k / (n + 1) of the beat; birds cross every 15 to 25 s (5 or 6 a loop), at 44 to 168 px, 40 to 60 px/s, a third of them pairs; the second or third is a pair that comes in from the left, circles once over the far treeline and comes down into it, shrinking |
| 8 | fade | 10 | mood 0.0 at its start, 0.1 at 3 s; the star (GL_DOT in STAR, 1.6 px) comes out at 4 s; the last 2.5 s go to black |

Loop length: the sunset is stretched first (to 120 s at most), then the three
pauses (2 to 12 s), to land in 255 to 315 s; with these trails every loop
comes out at 315 s with 4 s pauses and a 113 to 117 s sunset. The approach is
longer than the spec's 30 s: the trails are 1300 to 2100 px of switchbacks,
and a stroll at the walking pace the rig was made for takes 38 to 43 s even
with the pace raised on the long ones. The climb is shorter than 40 s (30 to
36): the stairs are small. Explore ends with about 12 s of him standing at
the east rail.

### How it is built

- **Family and rng.** `spineFamilyFor` now plays the Lookout in the window
  (and when forced). `lookoutPlan` draws only the sunset's step count; the
  landscape, the clouds, the star, the landing place and the birds are drawn
  in `lookoutBeginLoop` (from `spineBeginLoop`, after `memoryBeginLoop`), so
  every other draw of the loop start happens first and in the same order as
  before. No rng is drawn during the loop (the birds are planned; the ambient
  bird is the scene's, `bird_manual`). The other families' paths draw nothing
  new: the evening's, morning's and midday's pinned lines pass.
- **World.** `World::lk` (1,916 bytes) and `World::look` (`LookoutState`, 248
  bytes) sit beside `land`. `worldTick` hands a Lookout loop to its own tick
  (clouds, `lookoutStep`, the hiker, a sort of him and the scene's glyphs, the
  dirty marks); `worldDrawStatic` / `worldDrawDynamic` to
  `lookoutDrawStatic` / `lookoutDrawDynamic`. The camp's stream glints, fire,
  smoke, tent, props, critters, minors and the weather's effects neither step
  nor draw nor mark. `stepHiker` calls `lookoutHikerPre` (height from feet y,
  pace from height) and `lookoutHikerPost` (the climbing gait) and skips the
  stream's wading lift.
- **Cache.** `lookoutLandDraw` draws any set of layers (sky, valley, hill,
  trail, plants, tower, cab, rails) with build parameters. The cache holds:
  in the build, the finished layers below the one animating (the camp's
  rule); then the landscape; through the reveal the landscape with the tower
  drawn dynamically as it rises; from the reveal's end the landscape and the
  tower without the cab's front and the rails, which the dynamic pass draws
  round him: the cab's front before him while he is outside it, after him
  while he is in it (with holes where the far windows show, so he is seen
  through them), the rails always after him. Nothing is drawn twice over
  itself, so no antialiased edge darkens round him. A change of cached layers
  invalidates the cache (5 a loop, not palette rebuilds).
- **Palette.** `World::mood_override` (-1: the host's mood) is what
  `computeAppPalette` and `appTick`'s threshold use. Through the sunset the
  clouds' two slots come from a palette 0.45 of mood behind (never before
  4.0), with the same weather and night rule (`lookoutPaletteTweak`), so they
  hold their colour last; in the fade they are the sky's.
- **Weather.** `weatherPlan` runs as for every loop (its own rng), so the
  kind's tint (overcast for rain, snow and storm, the fog screen) reaches the
  palette; nothing else of the weather runs: no `weatherStep`, so no
  lightning, gusts, rain, snow, fog bands, shimmer or hiker gags, and no
  rain_shelter (no majors at all). A storm sunset is a muted one.
- **Occlusion.** The tower's parts are split in `lookout_land.cpp` (the old
  `drawTower` order kept within each part). Visible change to the stills: the
  stair handrails are now drawn after the cab's front (they are nearer), and
  are 16 px above the treads (were 12) so his hand meets them at 44 px.
- **Memory.** `memoryBeginLoop` (after its one rng draw), `memoryEndLoop` and
  `spinePlan` leave memory alone in a Lookout loop; `loop_count` still moves
  (`worldBeginLoop`).

### Numbers (`tests/test_lookout.cpp`, clang)

- 20 seeds (forced, mood 4, random weather; every fourth with the night rule
  on): 0 overruns, every loop 315 s, his height never grew on the approach,
  in frame throughout (but for his walk in from the left), nothing in the
  reserved corner (the hiker, birds, clouds, glyphs and the tower's bounds;
  740 rendered checks of the corner's pixels, ground or grass only), World::busy
  every tick, exactly one landing pair a loop, 5 or 6 sunset birds.
- Sunset steps: 8 to 10 after the review (were 10 to 12), monotone, ending at 5.9.
- Palette rebuilds per loop: 11 to 13 after the review (loop start 1, the sunset's 8 to 10,
  the fade's 2), under the 16 the brief asked for.
- Night caps in the fade: with the night rule on, the highest slot is 0.560
  (cap 0.60); without it, the slots on screen stay at or under 0.340 (the
  palette's TENT_LIT is 0.66 at mood 0.1 without the dim, but no Lookout
  shape uses it).
- Strips per frame, 4 whole loops rendered every tick, build excluded: avg
  3.26, max 15 (a palette step or the fade). Incremental equals a full render
  at 453 checkpoints, every beat and the build.
- `sizeof(App)`: 70,320 of 98,304 (`LookoutLand` 1,916, `LookoutState` 248; 8 bytes of it from a concurrent weather change).
- Regression: `test_spines` (evening 20/20) and `test_midday` (evening 20/20,
  morning 10/10, midday 10/10) pinned lines identical, under clang and GCC 16.
- GCC 16: all 10 suites pass; the Lookout's `--loops 2` lines for seeds 1 to 3
  and 60 frames of seed 2 (`--every 131`) are byte-identical to clang's.

### Looked at

`ss_preview --spine lookout --mood 4 --every 25` for seeds 1 (clear and its
random storm), 2 (clear) and 3 (wind), sheets of every beat and through the
sunset, and 3x to 6x crops of the tower: the build, the walk up and the
shrinking, the hawk, the reveal's stages, the stairs (he reads at 44 px:
pack, hat, knees up, hand forward), the landing look, the rail bird (moved to
the rail's east end, against the sky: on the front rail it was lost against
the lit pane), the peeks, him in the cab seen through the far windows with
his hand on the finder, the taps, the chipmunk, the sitting on the edge with
the rail across him, the sunset's colours (gold, orange, pink, violet, deep
blue, the clouds pink to the last), the birds, the landing pair, the star
(made 1.6 px: at 1.3 it was a single pixel), the fade. Fixed on the way: the
build's first second showed an unpainted band under the sky (the lowest sky
band now runs to the frame's bottom); the rail bird's place; the star's size.

### Judgment calls

- Height follows screen y, not trail distance (the land notes' suggestion),
  ratcheted so it never grows; the stairs and deck are 44 px.
- The approach ends at the last left turn (the trail_pause is there, high on
  the hill, near the ridge); the last leg belongs to the climb.
- The Lookout plays at mood 4.0 from loop start whatever the host's mood (the
  sunset starts from 4.0; a host mood of, say, 4.6 would otherwise step back).
- The fade's mood goes to 0.0 then 0.1 as the spec says; the night key's sky
  is a touch lighter and greener than 5.9's, so the fade's first step reads as
  a small lift before the dark. 5.95 and 5.99 would keep it darker; not done.
- The birds are drawn over everything, the tower included (they are near; the review moved them after the cab front and rails): one crossing at a
  tower's height passes in front of it, dark on dark.
- "The wind sway rises a little" on the approach is not done: the Lookout's
  grass is static (no swaying tufts on the hill).
- `--loops` prints `family=lookout approach=.. trail_pause=.. reveal=..
  climb=.. explore=.. settle=.. sunset=.. fade=.. pauses=a,b,c steps=N birds=B`.

### Not verified

- Anything on the board: the cache rebuild cost per palette step (13 to 15 a
  loop, each a full static redraw of the Lookout's landscape and tower), the
  readability of a 44 px hiker, a 7 px chipmunk and a 1.6 px star at real size.
- The perched pose's feet offset (0.215 of his height) was set by hand and
  checked in crops only.
- `tools/review_clips.py` / `review_page.py` were not run on the Lookout (no
  clips cut); they read the family from the plan line and the beat names are
  the spec's.
- A camp loop after a Lookout loop was run (test_lookout's memory check: an
  evening plays to its end with no overrun); a morning or midday after one was not.

### Review fixes (chief, after the independent review of 060c7f3)

- Explore was 50 s with the script done at 32.7 s on every seed: 17 s of him
  standing at the compass. It is 36 s now; the fit gives the time to the
  sunset.
- The fade's second step (0.0 to 0.1) lightened and greened the sky just
  before the star; dropped. The clouds now keep their lagged colour through
  the fade's one step (mood 0 is treated as 6), so they no longer snap to
  night grey.
- Birds drew under the cab front and the rails; they draw after them now.
- `memoryBeginLoop` set the critters' persona on `loop_count <= 1`, so a
  Lookout as the first loop left it unset for two camp loops; it keys on
  `persona_age == 0` now (camp-only output unchanged: the pinned lines pass).
- He stepped through the cab wall (the inside flag flipped at +33, the wall
  ends at +30): the door is at +37.5 now.
- The reveal marked the whole tower dirty every tick for 12 s; it marks only
  while the tower rises and at its three detail stages.
- With no weather data a Lookout loop drew rain, snow, fog or storm a quarter
  of the time and tinted the sunset with nothing to show for it; those kinds
  become clear for the Lookout (its weather rng draw is unchanged, so the
  camp families are untouched).
- Not changed: the approach stays 38 to 43 s (the switchbacks are long), the
  climb 30 to 36 s (the stairs are small), no wind on the approach.
