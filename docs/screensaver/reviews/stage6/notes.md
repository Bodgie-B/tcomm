# Stage 6 notes: surprise majors, variety, world memory

Written 2026-10-30. Not committed. The minor-effect scheduler (`minors.*`) was
built at the same time by another agent; this stage touches it in one line
(below).

## Files

New in `screensaver/core/scene/`:

- `majors.h/.cpp`: the pool of 12 majors (begin, tick, script each), the planner
  (`majorsPlan`), the beat (`majorBeat`), and the glyphs (small drawn things only
  majors need: ripple rings, dots, shapes, sound arcs, an ant line) with their
  bounds, drawing and dirty tracking.
- `memory.h/.cpp`: `Memory` (moved out of `world.h`) and what carries it across loops.
- `script.h`: the step interpreter's vocabulary (`Step`, `StepOp`, `Anchor`, the step
  constructors in `ss::sc`) and the interpreter's public face (`scriptStart`,
  `scriptRun`, `scriptDone`, helpers). The interpreter itself stays in `spine.cpp`.

Changed:

- `spine.h/.cpp`: the vocabulary moved to `script.h`; anchors `AT_M0..AT_M3` (the
  running major's points); `OP_SKIP` (`skipUnless(pred, n)`: steps that only
  sometimes apply); `LoopPlan` gains the majors, `cairn_start` and `patch`;
  `spinePlan(p, rng, in, memory)` plans the majors and the new length policy;
  `spineBuildTimeline` adds `still / major / still`; `spineFormatPlan` ends in
  ` majors=...`; `busy` set by dinner, bear and close; the mouse-hole patch at the
  end of every setup variant; the cairn from memory at arrival; a bold deer at
  dinner takes up to 7 steps, a shy one 4.
- `world.h/.cpp` (small anchored edits): `#include "majors.h"`, `"memory.h"`;
  `CT_MAJOR`, `ACT_MAJOR`; `World::busy`, `World::major`; `WORLD_ACTOR_MAX` 64 to 80;
  glyph actors in `sortActors`, `majorsTrack` in `worldTick`, `glyphDraw` in the
  draw switch.
- `props.h/.cpp`: `Tent::bite` (the mouse hole: a notch in the base of the back panel).
- `app.cpp`, `ss_app.h`: `memoryBeginLoop` after the plan, `memoryEndLoop` at the
  rollover; `appMemory`, `appBusy`.
- `minors.cpp` (the other agent's file, one line): `startMouse` calls
  `memoryNoteMouseTent(w.memory)`.
- `sim/preview_common.*`, `sim/preview_main.cpp`: `--loops N`; the plan buffer is 320.
- `tests/test_scene.cpp`: `testVariety`, `testMemoryCarry`, the majors' strips in
  `testSpineBudget`.
- `screensaver/README.md`: the `--loops` row.

## The majors

Seconds are each beat's length; every script ends 0.85 to 0.95 of the way in
(measured over 240 loops at moods 3, 0 night and 5 with `-DSS_SPINE_TRACE`).
Slots: S1 after setup, S2 after fire, S3 after dinner (he is outside), S4 after
the food bag (he is in the lit tent).

| Major | What happens | Critters | Seconds | Gating |
|---|---|---|---|---|
| squirrel_splash | Two squirrels run in from the right and chase up the camp pine and out along the bag branch. The leader turns at bay at the far end; the other lunges; the leader spins off (falling, flipping facing every 3 ticks) into the stream: two ripple rings and four drops. Under for a second, it comes out at the nearest rock (or the far bank), hops up, shakes off (CA_SHAKE), sits and looks about as if nothing happened, runs off. The other peers down, then goes back down the trunk and off. He turns to watch and laughs. | 2 squirrels | 31 | S1 to S3 |
| deer_stick | A pale stick lies at the right edge of camp. A deer walks in from the right and steps on it: it snaps (two halves) with a dust puff; the deer startles, then glares at him (CA_LOOK); he turns and laughs; it lifts its head (CA_HEADTURN) and walks off with its nose held high (head_add). | deer | 42 | S1 to S3 |
| pinecone | A squirrel runs up the camp pine and out on the branch, sits eating a cone, and flicks it away: it lobs onto the tent's front pole (the tent shakes), bounces onto his head (he ducks), and off onto the ground, where it stays (a resting cone bit). He looks up at the pine (turning to it), rubs his head; the squirrel peers down at him and leaves. | squirrel | 30 | S1 to S3 |
| map_chase | He crouches at the reading rock and spreads a map on it (a flat sliver with fold lines). The wind lifts a corner; he grabs and misses; it tumbles off to the right in hops (turning, flipping); he yelps and runs after it off the right edge. Back from the right a few seconds later, folding it as he walks (a fold every 1.8 s), into his pocket, and he sits. | none | 43 | S1 to S3 |
| fish_leap | He sits on the bank. A fish arcs out of the water in front of him (rings out and in); he yelps (rocks back, arms up). Variant 1 (2 in 5): it lands in the fire pit and flops twice; he gets up, picks it out, carries it to the water, tosses it back (ring), nods. | none | 23 / 37 | S1 to S3 |
| raccoon_pack | He naps in the grass (var 0, S1 to S3) or is in the lit tent (var 1, S4). A raccoon comes in from behind the pines, noses into the pack (it jiggles), sits eating a granola bar; he sits up (or his shadow in the tent sits up); it freezes, looks up guiltily, waddles off. Memory: with last loop's shoe it first drops the shoe by the pack (shoe_taken cleared), and after the guilty look takes the mug or the book, whichever is down (last_stolen), and scurries off; he picks the shoe up. | raccoon | 58 / 26 | var 0 S1 to S3; var 1 S4 only without the shoe |
| ants_cookie | He sets a cookie down in front of his seat and watches: a line of 3 ants comes from the grass and carries off a crumb, then 5 a chunk, then 9 the whole cookie (it shrinks 4.0, 3.4, 2.5, gone). He shrugs. | none (an ant-line glyph) | 46 | S1 to S3 |
| shadow_theater | In the lit tent his head shadow moves over and his hands make a rabbit (ears flick, hops), a bird (flapping), a dog (three barks), then a bear (growling). A chipmunk and a squirrel come and sit watching. The real bear comes from behind the pines to behind the tent and rears, towering over it; the watchers scatter. His shadow bear roars; the real bear startles and runs. The hands come down; he never knew. | chipmunk, squirrel, bear | 44 | S4 only (tent lit) |
| ranger_ridge | He stands at the view, hands on hips. A tiny 8 px figure rises on the far skyline (the highest ridge point clear of mid pines and the camp pine); its lantern blinks three times; he does a double take and waves (POSE_WAVE); it blinks back twice; he nods; it walks on along the crest and drops from sight. | none | 32 | S1 to S3 |
| echo | A squirrel comes and sits on a rock (or the ground) ahead of him. He cups his hands and shouts at the valley: three nested arcs go out toward the ridge; a smaller set comes back from the ridge, then a tinier one; then a tiny one from the squirrel. He tilts his head, nods, looks down at the squirrel, scratches his head; it runs off. | squirrel | 34 | S1 to S3 |
| firefly_jar | 12 to 20 fireflies (FIRE_CORE dots) blink on one by one over the grass and drift. He takes out a jar (CARRY_JAR) and makes three sweeps; the dots near each sweep fly into the jar and glow there (the rest go in after the third). A long look (HOLD_MUG pose, 5.5 s), he opens the lid, they spread back out and blink off one by one. | none | 42 | mood >= 4.5 or night |
| stargaze | He lies in the grass. 5 to 8 counted stars (2 px STAR dots, popping in with an overshoot) come out one at a time, 2.2 s apart; he points at each (the near arm aimed at it); loses count (head shake); counts again quicker. The counted stars stay out for the rest of the loop. | none | 30 + 2.95 per star (45 to 54) | night (stars visible, or night mode) |

`rain_shelter` is not built: it needs weather (stage 9).

At most 3 critters per major (shadow_theater uses all 3). Major critters are tagged
`CT_MAJOR`, take this loop's brave/shy personality, and are `CF_BRAVE` (never
scattered by him) except the theater's watchers, which the bear scatters.

### Glyphs

`GL_RING` (water rings, as the skipped stone's), `GL_DOT` (fireflies, stars,
drops; a FIRE_CORE dot over 1.2 px is a lantern: a FIRE rim and a FIRE_CORE core),
`GL_SHAPE` (twig, map, fish, cookie, cone, the ranger, four hand shadows),
`GL_ARCS` (three nested thin arcs), `GL_ANTS` (the whole ant line in one glyph, so
it dirties one strip band). 24 slots, each tracked like a prop (previous bounds
and state; nothing that did not change marks anything). They sort with the ground
actors by `base_y`. A glyph marked `keep` (the counted stars) outlives its major;
every other glyph is cleared at the major's end, and a cone left lying becomes a
resting cone bit.

## Placement and the ring buffer

- **How many:** 2 to 4 drawn (30 / 45 / 25 %), then the length policy may drop some.
- **Where:** one per slot (S1 to S4). Each major lists the slots it can use; a
  pick takes a random free one of them. Play order follows the slots.
- **Gating:** firefly_jar from mood 4.5 (dusk) or at night; stargaze only at night
  (within 0.3 of mood 0 / 6, or night mode); shadow_theater only in S4 (the lit
  tent); raccoon_pack in S1 to S3 (nap) or S4 (asleep, only while the raccoon has
  no shoe to bring back).
- **Weights:** 1 each, shadow_theater 1.2, firefly_jar and stargaze 1.5.
- **Ring buffer (Memory::ring, the last 8 picks):** a major in the ring keeps
  `0.5 * ((age + 1) / 9)^2` of its weight (age 0 the newest); the last loop's own
  majors keep 1e-4 (out unless nothing else fits). A set of majors (or a prefix of
  one) that one of the last 9 loops played (Memory::sets) also keeps 1e-4, and the
  length trim drops whichever major avoids leaving such a set. Over 40 loops x 6
  seeds x 2 moods no set repeats within 10 loops.
- **Stillness:** each major sits between two `still` beats of 4 to 8 s (after the
  slot's pause and fillers). Majors never touch each other or dinner, bear or close.
- **Loop length (still 490 to 650 s):** squeeze the pauses to 8 s; then drop fillers
  down to one, majors down to two, the last filler; then shorten the still beats to
  4 s; then stretch the pauses up to 20 s. Fillers can now be 0 to 4 (the slot table
  gained a row for 1). Measured 589 to 650 s over 20 seeds.
- **Beat slots:** build 1 + spine 8 + pauses 7 + fillers 4 x 2 + majors 4 x 3 = 36 at
  most, under `SS_MAX_BEATS` 64 (unchanged).
- **busy:** `World::busy` is set by the begin hook of dinner, bear, close and every
  major and cleared by their end hooks (the test checks it every tick of 30 loops).
  The minors read it.
- **Overruns:** a major that ends with its script unfinished or one of its critters
  in the scene counts a spine overrun and releases the critter (`testSpineOverruns`
  and `testVariety` require none).

## Memory

| Field | Set | Effect |
|---|---|---|
| `cairn_n` | live as stones are placed; at arrival | Next loop the cairn stands from the end of the build-up, one stone shorter (`LoopPlan::cairn_start`); the cairn filler builds on it (its stone count is capped at the top). |
| `shoe_taken` | the close's raccoon takes the shoe; raccoon_pack returns it | raccoon_pack (nap) brings the shoe back first and drops it by the pack; he picks it up. |
| `last_stolen` | raccoon_pack | What it took instead: the mug or the book, whichever was down. |
| `mouse_hole`, `mouse_tent_runs` | `memoryNoteMouseTent` (the mouse_tent minor); the second run in a loop sets the hole | Next loop the tent has a notch in its base; setup ends with one more crouch at the tent and the hole is gone (`PATCH_HOLE`, +7 s on the setup beat). |
| `loop_count` | each loop start | Loops started. |
| `ring`, `ring_n`, `last_n`, `sets` | the plan | The majors' ring buffer and the recent sets (above). |
| `brave`, `persona_age` | one rng call every loop; drawn again every 2 loops | A bit per critter kind: bold (quick, steady: speed dial 0.7..1, jitter 0..0.3) or shy (slow, fidgety: speed 0..0.35, jitter 0.65..1), laid over the slot dials when a major spawns a critter; a bold dinner deer comes up to 7 steps, a shy one 4. |

`appMemory` prints `loop=N cairn=N shoe=0|1 stolen=mug|book|none hole=0|1
brave=kind,... ring=major,...`.

## `--loops 10`, seed 1, mood 3

`ss_preview --seed 1 --loops 10` (memory as the loop leaves it; `loop=` in it is
`loop_count`, 1-based). Variants are arrival, setup, firewood, fire, dinner,
foodbag, bear, close.

| Loop | Seed | Length | Variants | Fillers | Majors | Cairn | Shoe | Stolen | Hole | Brave |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 1 | 650 s | 21100010 | cloud_watch, stream_listen | deer_stick, map_chase | 0 | 1 | none | 1 | chipmunk, frog, deer, bear, mouse |
| 1 | 1027777925 | 636 s | 21110211 | mug_sunset | echo, squirrel_splash, shadow_theater | 0 | 1 | none | 0 | same |
| 2 | 2322246646 | 583 s | 12220011 | skip_stone | ants_cookie, raccoon_pack, fish_leap | 0 | 1 | none | 0 | chipmunk, frog, deer, raccoon, owl, bear, mouse |
| 3 | 2791936963 | 626 s | 01212121 | cairn | pinecone, ranger_ridge, deer_stick | 5 | 1 | none | 0 | same |
| 4 | 2368957256 | 622 s | 21020212 | cloud_watch | echo, raccoon_pack, ants_cookie | 4 | 1 | none | 1 | frog, deer, owl, bird, mouse |
| 5 | 2288296774 | 645 s | 01102011 | read | ranger_ridge, squirrel_splash, map_chase, shadow_theater | 3 | 1 | none | 0 | same |
| 6 | 207203734 | 634 s | 01211202 | stream_listen, skip_stone, cairn | fish_leap, pinecone | 7 | 1 | none | 0 | chipmunk, frog, raccoon, owl |
| 7 | 1674968920 | 598 s | 02020112 | cloud_watch | echo, raccoon_pack, deer_stick | 6 | 1 | none | 1 | same |
| 8 | 1081727665 | 599 s | 12102000 | cairn | map_chase, ranger_ridge, ants_cookie | 7 | 1 | none | 0 | all but deer |
| 9 | 4250151990 | 645 s | 12121121 | read, mug_sunset | squirrel_splash, shadow_theater | 6 | 1 | none | 0 | same |

No two loops share their variants, nor their set of majors, and no major repeats
in consecutive loops. The cairn shrinks a stone a loop and grows when the filler
runs (loop 3: 5; loop 6 back to 7). The hole appears after loops where the mouse
came twice and is patched in the next setup. `shoe` stays 1: the close's raccoon
takes the shoe again every loop, even after raccoon_pack brought it back.

## Numbers

- `sizeof(App)` = 66,048 bytes (budget 98,304; 63,344 with the minors before stage 6).
- Strips per frame, rendering every tick (`test_scene`, mood 3): majors 7.62 (seed 11:
  squirrel_splash, echo, fish_leap), 7.96 (seed 7), 7.79 (seed 42: ranger_ridge,
  shadow_theater), 7.83 (seed 1), max 14. Whole loop 7.52 to 7.71, 0.104 to 0.114
  ms/frame; pauses 7.06 to 7.55; bear beat 8.07 to 8.95.
- `testVariety`: 30 loops at night, 84 majors run, every one 5 to 8 times.
- `ss_preview --loops 10` runs in about 0.4 s.

## Tests added (`test_scene`)

- `testVariety`: 30 loops from seed 1 at night. The first 10 differ in (spine variants
  + majors) and in their set of majors; every major fires at least once; every major
  has a `still` beat of 4 to 8 s right before and right after it (so majors never
  touch each other or dinner, bear, close); `appBusy` matches the beat (dinner, bear,
  close or a major) on every tick; no overrun and no critter left at any loop's end.
  Then 12 day loops: no night major planned, 2 to 4 majors each.
- `testMemoryCarry`: the cairn a loop leaves stands one stone shorter in the next once
  the arrival starts (checked twice in 10 loops); the memory line parses.
- `testSpineBudget` prints and checks (<= 10) the majors' strips per frame.
- The existing spine tests now run with majors in: determinism at 9 checkpoints, the
  quiet corner over two loops x 3 seeds, critters in frame and clear of the walking
  hiker, no overruns for 60 seeds, loop length, the fade.

## What I changed after looking at the frames

Sheets were cut from `ss_preview --skip T --every N` at the logged major ticks
(moods 4 and 0 night), cropped and zoomed.

1. The deer's stick (TRUNK) vanished on the dark ground: it is ROCK (a pale dry stick)
   and 1.25x.
2. Map: crouched at the rock his knees covered the map (ACTOR on ACTOR). He crouches
   10 px further right, and the map lies flat on the rock top (squashed to a sliver)
   and opens as the wind lifts it.
3. The cookie at his boot merged with the boot: it sits in front of his seat, the
   nest moved to the front right.
4. The raccoon stood 22 px off the pack and did not seem to touch it: 16 px.
5. The theater bear came round the tent's left side, which is behind the left
   foreground pine, and was mostly hidden. It now walks in behind the tent and rears
   there, towering over it.
6. The ranger and the echo's ridge point were sometimes picked on the camp pine's top
   or beside its branch: that stretch is excluded.
7. The echo's arcs were small and thin: about 40 % larger and thicker.
8. At night the fireflies (r 0.62) were too dim: 0.8. The counted stars (r 1.05) were
   dimmer than the cached ones: 1.35.
9. The lantern (FIRE_CORE) disappeared against the pale golden sky: it is a FIRE rim
   round a FIRE_CORE core, r 1.5.
10. The laugh was hard to see: bigger rock, head thrown back, and the near hand
    slapping his knee on each bounce (it reads as pointing and laughing).
11. The mouse hole was on the front corner, where he stands at the end of setup: it is
    on the back panel's base, larger; he crouches there to patch it.
12. Echo (and others) came up in consecutive loops: the last loop's majors are out, and
    recent sets are avoided.
13. Durations set from the trace (several had 10+ s of dead tail; raccoon_pack and
    deer_stick overran).

## Open items

1. `rain_shelter` waits for weather (stage 9).
2. raccoon_pack's tent variant almost never runs after the first loop: the close's
   raccoon takes the shoe every loop, so the raccoon always has one to bring back, which
   only the nap variant does. Review fix 6 (no shoe laid at the tent door while the
   raccoon has it) would change that balance.
3. There is no stump in the landscape: the echo squirrel sits on a rock ahead of him, or
   the ground.
4. The ranger at 8 px reads as a figure mostly by context (hat brim and lantern); the
   brief asked for 8 px.
5. The brave/shy split reaches the majors' critters and the dinner deer; the minors do
   not read it yet (`memoryBrave` is there for them).
6. I added the `memoryNoteMouseTent` call to `minors.cpp` (`startMouse`), the other
   agent's file. If that file is rewritten, the call must stay or the hole never forms.
7. Not checked by eye: firefly_jar at mood 4.5 to 5 (bright ground), stargaze with a
   cloud over a counted star (stars are actors, drawn over clouds), the fish landing in
   a lit fire (no steam), the map at night.
8. Dark shapes that are not the major's (I take them for minor effects already under
   way) were on the stream and in the air during fish_leap at mood 4 (seed 8 tick
   10590). The minors defer to `busy` for new starts; whether one already running should
   leave when a major begins is the minors' call. Not investigated.
9. Most loops now land 590 to 650 s with 2 or 3 majors; 4 majors are rare (1 loop in
   10 in the table), since the length policy trims them before the pauses go under 8 s.

## Post-review fixes

Applied 2026-10-30 (not committed): round 2 of the deferred fixes (1 to 8) and
items 9 to 15 from the review of 9b8b1f7.

1. **Held items follow the critter.** `directorStep` sets each critter-held
   item's `facing` and `squeeze` (`critterTurnScale`, so a stick narrows with the
   turn-around instead of flipping 118 px) every tick. `Item` gains `grip`,
   `squeeze`, `placed`.
2. **Bear c's stick.** The fallen branch lies at trunk + 20, ahead of the sniff
   spot. `doBearToStick` puts the mouth over the stick's grip point (0.3 of it
   ahead of the mouth) and backs up without turning (`critterMoveTo(..., back)`,
   legs stepping backward) when that is under 40 px behind it. The pick-up eases
   the stick from the lying line into the mouth (u 0.45 to 0.75). Carried in the
   mouth on all fours, trailing low along its side (angle -0.29); the anchor
   slides to the paw (held by the end) as `pose.rear` rises. The drop is now
   `actBearDropStick` (0.5 s): it topples forward off the paw and lands as the
   lying stick on the same line.
3. **`sp.bear` after release.** `bearHeld` (slot in use and tagged `CT_BEAR`)
   guards `bearArrived`, `bearGone`, `bearWalk`, `doBearLeave`, `doBearRunOff`;
   `bearUpdate` sets `sp.bear = -1` once the slot is not the bear's.
4. **Beat ends.** dinner, bear and close end with `beatEndOwned<B>`: any of their
   critters still owned walks out by the nearer edge.
5. **Hiker rule test.** `testHikerScatter` walks him at a chipmunk, a deer and a
   raccoon on a bare World: each startles 77 to 79 px ahead and is never
   overlapped; brave ones stay. The rule works; it rarely fires in loops because
   the minors keep their paths clear of him.
6. **The shoe.** `doShoeOff` leaves no shoe out while the raccoon has it, nor on
   the night raccoon_pack brought it back (`Memory::shoe_back`, this loop only:
   he keeps it in the tent). The night thief then takes the mug or the book,
   whichever is out (in its mouth; `last_stolen` records it, and the shoe too),
   or noses about the door and leaves; it walks quicker when the loot is far in,
   so it is out before the fade. That alone left the tent variant impossible
   (shoe = 0 only in the loop right after a return, where the last-loop rule
   bars raccoon_pack), so the planner allows a **sequel**: with `shoe_back` set
   at plan time, raccoon_pack may run again, tent side (S4) only, unpenalised by
   the ring. Seed 1 day, 40 loops: 8 returns, 5 tent raids; night seeds 1 to 4:
   2 tent raids each. `--loops 10`, seed 1, shoe / stolen as each loop leaves
   it: 1 shoe, 1 mug, 0 mug (return), 1 shoe (tent raid, then theft), 1 mug,
   0 mug (return), 1 shoe, 1 mug, 1 mug, 0 mug (return).
7. **Pack set-down** tweens over 0.3 s (`actPackSetDown`).
8. **Minors clear the stage.** The "stray shapes" at seed 8 tick 10590 were not
   critters but two leaves leaf_fall had left lying, one slid off his hat onto
   the water's edge; across 40 loops 95 of 108 majors began with a leaf about,
   and one (night seed 12) with a minor's deer still strolling off. Now, when a
   beat that owns the critters begins (the still before a major counts):
   whatever of the minor's critters is already walking off goes at 120 px/s
   (`hurryOff`); a gust lifts every leaf and carries it off the nearest edge
   (`gustStep`, about 3 s); a leaf still falling, or landing after the wrap-up,
   goes with the gust; a leaf never lands on the water. The moth leaves too
   (item 11). `testMajorsStartClear` (20 seeds mood 4, 20 night): 108 majors, none
   with a minor critter or a leaf.
9. **One bit allocator.** `worldTakeBit`: a free slot, else the resting cone or
   leaf about longest. The bear's dust and cones, deer_stick's dust, majorEnd's
   resting cone and the minors all use it; no fixed indices remain (seed 11 mood
   4: the pinecone's cone outlives the bear's slide).
10. **Optional picks** (the third and fourth major) never take a penalised major
    (`wt < 1e-3`). `--loops 60` for seed 1 day and night, 7 mood 4, 20 night,
    3 dusk: no major repeats in consecutive loops except the raccoon sequel
    (4 to 7 per 60 loops); `testVariety` asserts it.
11. The moth leaves at once on the wrap-up (`m.quit`).
12. The minors' RNG seed no longer includes `loop_count`: `--seed <loop seed>`
    replays that loop's minors (they diverge later only where memory changes
    the scene: the cairn, the mouse hole, the brave/shy split).
13. `GLYPH_MAX` 28 (`WORLD_ACTOR_MAX` 88 to hold them).
14. `--log-beats`: the minor count lives in `PreviewState` and resets each loop.
15. Tests: `appMinorForced` counts minors forced to end; `testMinors` asserts 0
    (it found one: a mouse scattered across the frame was released in sight by
    the 12 s exit timeout; a critter still moving is now left to get out).
    `testMemoryCarry` also checks the mouse hole (made after two mouse_tent runs,
    patched in the next setup; 4 of 12 loops) and that the shoe comes back (3 in
    12 loops).

Viewed: bear c at seed 7 mood 4 (the branch) every 25 ticks and the pick-up,
raise, swats and drop every 1 to 4 ticks; seed 18 (the fire-poking stick, with a
turn while carrying). `sizeof(App)` 66,664.


## Palette fixes (Mike's review)

Written 2026-10-30. Not committed. PC preview only: every value is unverified on
the real panel (IPS + RGB565), final tuning is still to be done there.

Files: `screensaver/core/palette.h`, `core/palette.cpp`, `tests/test_palette.cpp`,
and the mushroom draw in `core/scene/landscape.cpp` (`drawMushroom`, the only
draw site; the build-up reaches it through `drawLayer`). Review image:
`screensaver/build/palette_review.png` (and `.ppm`): swatch strip, before/after
crops, every mood after.

### Plumbing

- New slot `MUSHROOM` (slot 31, so `SLOT_COUNT` is now 32, the limit). The cap is
  `MUSHROOM`; its two spots are `mixRGB(MUSHROOM, WATER_LIGHT, 0.65)` (toward the
  pale stem), so they dim with the mood too (they were `FIRE_CORE`).
- `night_dim` now does what its comment always said ("clamp bright slots"):
  after the scale, every slot is capped at `NIGHT_LUM_MAX` = 0.60 luminance and
  the small-highlight slots (`ACTOR_LIGHT`, `FIRE_CORE`, `STAR`, `SUN`;
  `isNightHighlight`) at `NIGHT_HIGHLIGHT_MAX` = 0.82 (no pure white). The cap
  scales the color down, hue kept. Both limits live in `palette.h`.
- `ACTOR_LIGHT` joins the lights that `night_dim` scales by 0.85 instead of 0.55,
  so the eyes, rings and rim keep reading under the night rule.

### Changed keys (before -> after)

| Mood | Slot | Before | After | Why |
|---|---|---|---|---|
| night | RIDGE_FAR, MID, NEAR | 1B2A40, 16253B, 132036 | 1E3046, 1C2C44, 1C2A40 | lifted ~6/255 so the near trees can sit 25+ over ACTOR and still under RIDGE_NEAR |
| night | TREES_FAR, TREES_MID | 142138, 121D2E | 1C2A48, 182434 | follow the ridges (order rules) |
| night | TRUNK, FOLIAGE, FOLIAGE_DARK | 0E1520, 0F1C2A, 0A131E | 262230, 1A2634, 1A2232 | bear on the pine |
| night | TENT, TENT_LIT | 8A4A2A, F0C070 | 4E2C2E, D8964C | dim rust unlit; amber glow lit, 0.54 under night_dim |
| night | ACTOR, ACTOR_LIGHT | 05080F, 0C121C | 020408, 6E4E34 | near-black body; small firelit amber highlight |
| night | MUSHROOM | (FIRE E07B39) | 5A2E2C | dim brick red |
| dawn | ACTOR, ACTOR_LIGHT | 1E2230, 303648 | 1A1E2C, 7E6E6C | trunk gap; highlight that reads |
| dawn | MUSHROOM | (FIRE) | C46A4A | |
| morning | TRUNK | 40322A | 4C3A2E | was 20/255 over ACTOR |
| morning | ACTOR, ACTOR_LIGHT | 15251E, 24392E | 101C17, 7E7A60 | |
| morning | MUSHROOM | (FIRE) | D06A3A | |
| day | TRUNK | 3A2E26 | 4A3A2E | was 18/255 over ACTOR |
| day | ACTOR, ACTOR_LIGHT | 14231C, 22382B | 0F1A15, 7E7C64 | |
| day | MUSHROOM | (FIRE) | D2642E | |
| golden | TRUNK, FOLIAGE, FOLIAGE_DARK | 3A2A2A, 4A3A3F, 2E2429 | 46282A, 5A3E44, 422838 | warm plum-brown, bear reads (was ~19 over ACTOR) |
| golden | TENT | C8662F | B65A30 | deeper sun-warmed rust under the bright sky |
| golden | ACTOR, ACTOR_LIGHT | 1A1318, 2C2024 | 120C10, A87A56 | |
| golden | MUSHROOM | (FIRE) | D85A2A | |
| dusk | TRUNK, FOLIAGE, FOLIAGE_DARK | 2A2038, 2A2E48, 1E2038 | 36243A, 3A2E48, 2C2440 | warm plum, bear reads (was ~20) |
| dusk | TENT, TENT_LIT | C8683C, F6C070 | 9A5448, EAA45C | dusky rust; warm glow |
| dusk | ACTOR, ACTOR_LIGHT | 0E0D1A, 1A1828 | 08070F, 8E6450 | |
| dusk | MUSHROOM | (FIRE) | 9C4A48 | |

Unchanged: dawn/morning/day TENT and TENT_LIT, golden TENT_LIT, all skies,
grounds, water, fire, stars.

### Contrast (test_palette prints this; luminance 0..255, Rec.709 on the 8-bit expansion)

Asserted: ACTOR_LIGHT over ACTOR >= 50 at dawn..dusk, >= 30 at night and under
night_dim at every mood; TRUNK/FOLIAGE/FOLIAGE_DARK over ACTOR >= 25 at every
integer mood; under night_dim >= 13.75 (25 x 0.55: night_dim is a uniform 0.55
scale, and `testWeather` requires SKY2 to drop >= 40%, so absolute gaps shrink
with it while the ratio holds); under night_dim every slot <= 0.60 except the
highlight slots <= 0.82; TENT off the day value at golden, dusk, night, and
day > dusk > night when unlit; night lit tent > 2x unlit; night MUSHROOM < half
of day and < half of FIRE.

```
                         ACTOR  LIGHT  gap  | TRUNK  gap  FOL  gap  FOLD  gap | TENT  LIT  MUSH
night                      3.4   81.4  77.9 |  33.4 30.0  34.4 30.9  31.5 28.1 |  50.2 159.8  53.6
dawn                      28.1  111.8  83.7 |  57.5 29.4  87.7 59.6  65.1 37.0 | 137.4 213.5 122.5
morning                   24.6  119.8  95.3 |  58.7 34.2  79.4 54.8  65.0 40.5 | 132.2 212.9 124.7
day                       20.0  122.7 102.7 |  58.7 38.7  74.3 54.3  56.5 36.4 | 125.9 209.4 120.7
golden                    13.1  129.2 116.1 |  45.6 32.5  66.8 53.7  46.8 33.6 | 105.7 202.0 113.8
dusk                       5.1  107.9 102.8 |  40.3 35.1  48.9 43.8  39.2 34.1 |  99.3 176.0  90.7
night   night_dim=1        0.0   68.6  68.6 |  16.6 16.6  17.7 17.7  14.9 14.9 |  27.0 137.3  28.7
dawn    night_dim=1       11.4   95.8  84.3 |  28.1 16.6  47.2 35.7  34.4 22.9 |  75.1 152.9  65.8
morning night_dim=1       10.9  100.9  90.0 |  29.9 19.0  42.4 31.5  33.7 22.9 |  73.3 152.9  66.2
day     night_dim=1        9.2  103.8  94.6 |  29.9 20.7  37.3 28.1  30.3 21.1 |  69.1 152.3  65.7
golden  night_dim=1        5.1  108.5 103.3 |  22.5 17.3  35.7 30.5  23.1 17.9 |  57.1 152.8  61.6
dusk    night_dim=1        0.0   90.0  90.0 |  21.1 21.1  25.2 25.2  20.1 20.1 |  51.9 150.7  49.0
```

Before, for comparison: ACTOR_LIGHT gaps were 9 (night), 21, 19, 22, 16, 13; the
near-tree minimum gaps 9 (night), 26, 20, 18, 19, 20.

### What I looked at and changed after looking

Stills from `ss_preview` seed 42: bear on the camp pine at mood 4 t13625, mood 5
t13825, `--night` t13175; owl_watch `--night` t4700; hiker by the fire at moods
4, 5 and `--night` (dinner); raccoon_pack at mood 4 t12360 and the raccoon at
the close `--night` t14925; the lit tent `--night` t12500 and mood 5 t12000;
the mushroom at moods 3, 4, 5 and `--night`; every mood at t7625.

- First pass lifted the night ridges to 22344A/1E3048: `testWeather`'s fog check
  failed (RIDGE_FAR too close to SKY4 for fog to move it measurably in 565), so
  RIDGE_FAR/MID came down to 1E3046/1C2C44 and TREES_FAR got a blue step to stay
  between MID and NEAR. Night FOLIAGE came down to 1A2634 so the camp pine sits
  under RIDGE_NEAR rather than level with it.
- Golden and dusk: the bear now reads clearly as a black shape on a plum pine;
  the pines still read as dark warm silhouettes against the sky. Kept.
- Owl eyes read as two amber discs under `--night`; raccoon rings read at golden
  (tan) and faintly at night (amber, 1.5 px marks). Kept.
- Tent: dusk unlit is a dusky rust that belongs to the violet scene; night unlit
  is a dim rust; the lit tent at night is an amber glow (0.54 under night_dim,
  was 0.67, over the limit). Kept.

### Still weak / outside the allowed files

- **Bear on the pine under `--night`.** The trees sit 15 to 18 over ACTOR (ACTOR
  is 0 after the dim). It reads, but only just. The night ridge stack (SKY4 54,
  RIDGE_NEAR 38) caps how light the near trees can be without breaking the
  atmospheric order; a bigger gap would need the night key lighter overall or a
  non-uniform night_dim, both bigger calls than this pass. Check on the panel.
- **Hiker rim (`hiker.cpp`, not touched).** The rim now has the contrast, but it
  draws as a broken, sub-pixel line inside the silhouette (partly overdrawn by the
  arms) and is easy to miss. Suggest for the scene agent: draw the rim after the
  arms, at the silhouette edge on the fire side, with a minimum width of about
  1.2 px (`rim` in the `lit_from` block, around line 346). Unverified which side
  it lands on in every pose: in the golden dinner frame the fire is left of him
  and the marks sit right of his centre line.
- Night unlit tent and mushroom are deliberately dark; if they vanish on the
  panel's night backlight, raise them there.

## Stream vignettes (Mike's idea)

Written 2026-10-30. Not committed. Mike watched the loop and wrote that the
stream, so big in the foreground, invites play with the water. These are his
five: three ways a fishing trip goes, a bath, and birds on the water. Tone: a
Pixar short; the gutting and the dead fish are implied and cartoonish.

### What was built

| Scene | Kind | Seconds | Gating | What happens |
|---|---|---|---|---|
| fishing_small | major | 60 | S1 to S3 | Rod out of the pack (a stick off the wood pile if the rod was lost last loop), down to the bank left of the clearing (fire.x + 76, by where he cools his hand), two pulls extend it, casts (POSE_CAST: rod back over the shoulder with the action wind-up, then whipped to POSE_FISH_STAND; the float lobs out from the tip and rings). Waits, the float bobs; it dips twice and goes under; he yanks: a tiny fish dangles under the tip; he lowers the rod and looks; flicks it back (ring). Three times, each yank bigger and quicker (tip higher, rock back, shoulders and a hop), a head shake after the second. The third: rod back over the shoulder and hurled across the stream; it tumbles end over end (2.25 turns, 1.4 s) and lands on the near-viewer bank with a dust puff, where it lies for the rest of the loop (ITEM_ROD). He storms off (fx.stomp: stiff arms swung high, head down, 50 px/s) into the tent; the tent shakes once; 2.6 s; out again, a sigh, sits by the fire. Without a tent up: to the pack, hands on hips. Memory: rod_lost. |
| fishing_log | major | 56 + 8..12 | S1 to S3 | Rod, bank, cast as above; he looks about at the sky. A log (SH_LOG, TRUNK, 40 px) comes down the stream from off the right edge along the same path the glints use (52 px/s); the audience sees it first. The line snags its near end; the rod bows; he is jerked forward, leans back and digs in (two stumbles), is pulled off the bank into the water (wading, the waterline on him), and towed off downstream behind the log, leaning back, heels in, splash rings trailing, out of the left edge (his feet kept at y <= 474 and the line's end at <= 472 so he stays in frame while visible). The scene sits empty for the variant's 8..12 s. He walks back in from the left, dripping (drops off him for 5 s), wrings out his hat (two drops), puts it on, sits by the fire. Memory: rod_lost (it went down the river). |
| fishing_good | major | 50 | S2 only | Rod, bank, cast, then POSE_FISH_SIT, patient. A bite; he jumps up and plays it (the rod bows, two capsules; the fish under the water comes in toward the bank with rings); a heave and the fish (the 14 px fish_leap glyph) arcs out onto the bank and flops; he drops the rod (left lying on the bank), pounces, holds it up at arm's length (REACH_UP, the near arm forward and up so the fish hangs clear of him against the sky), nods. Carries it to the flat reading rock by the fire: crouched, three quick strokes, one flick toward the stream (a 2 px bit arcs into the water, a ring). Onto a stick propped over the fire (ITEM_FISH on_fire). Sits. |
| fish_dinner | dinner variant 3 | 34 | only with fishing_good | Takes the stick off the fire, sits, eats in three bites (the fish shrinks 1, 0.68, 0.38, then the bones), tosses the skeleton into the fire (an underarm lob), pats his stomach. The dinner critter hook runs before the eating as in a to c (the deer edges in a step a bite). |
| bathe | major | 54 | S2 or S3 (tent up) | To the bank, hat off onto the nearest rock (or the bank), pack off if he wears it, wades in, sits down in the near half of the stream (he is taller than the stream is wide: in the middle, his torso stood against the bank and read as "sitting on the edge"). Scrubs (arm circles), splashes (rings either side, drops), leans back, hands behind his head, 9 s. Downstream two or three fish surface belly up one after another and drift off along the stream. He turns, looks at them, lifts an arm and sniffs his armpit, shrugs. Out, a dog-shake with drops flying, hat back on, sits by the fire. |
| birds_on_water | filler (pool of 7) | 38 | after fire or dinner, as the other fillers | He sits on the bank (camp.listen). Two or three birds swoop down onto the water to his right (rings), sit low (a GL_WATER patch redraws the water over their bottom 3 px), drift with the flow, dip their heads (CA_EAT), flick a wing (fore_add, a small ring); at 12 s the first chases the second a few px (rings); at 19 s they fly off together, upstream and up. His head and facing follow the nearest. |

### What it uses

- **Rig** (`hiker.h/.cpp`): `CARRY_ROD` (one capsule from the butt behind the near
  hand out along the near forearm + 0.55 rad + `rod_tilt`, `rod_len` units of
  height; bowed by `rod_bend` it is two capsules; a 1 px line from the tip to
  `rod_end` and a FIRE float when `rod_float`; `rod_stick` draws a kinked TRUNK
  stick with a bare string). The figure stays at 22 primitives at most: a bowed
  rod or a stick draws no float. `hikerRodTip`. Poses `POSE_FISH_STAND`,
  `POSE_FISH_SIT` (a landing pose), `POSE_CAST` (an action pose, so it dips before
  it goes back). `hat_on` (false: no brim or crown). The sheets in `test_hiker`
  have rows for the new poses with the rod and line out, one bareheaded.
- **Props**: `ITEM_ROD` (lying, `alt` the stick), `ITEM_HAT`, `ITEM_FISH` (on a
  roasting stick; `size` shrinks it; size 0 is the skeleton; `on_fire` sorts it
  in front of the flames). `Item::alt` added (default member initializer, so the
  existing brace inits stand). Held fish and fork go to the near hand.
- **World**: `HikerFx::stomp`, `wade`, `wet_y`. With `wade` on, World works out
  his waterline every tick from how far into the stream his feet are and, after
  drawing him, redraws the water body (`worldDrawWater`, the same polygon the
  landscape caches) over his rect below it. The same function draws the birds'
  GL_WATER patches. `WORLD_ACTOR_MAX` 88 to 91 for the three items.
- **Majors** (`majors.*`): ids 12..15 (the memory's 16-bit sets are now full:
  a static_assert guards it), glyph kinds `GL_WATER`, shapes `SH_ROD`, `SH_LOG`,
  `SH_DEADFISH`, the line's mode in `MajorState::mode/mt`, `v0`, `stick`. The
  log and the dead fish move along the stream by its point index, as the glints
  do (`streamOn`). Drops are glyph dots with a velocity that stop at the ground
  (not the minors' DROP bits, which turn into rings on the water).
- **Planner**: weight 0.6 each (so a stream scene plays about every other loop); fishing_good S2 only, bathe S2/S3, the other two
  S1..S3; at most one fishing major a loop (they share the rod). `spinePlan`
  sets `dinner = 3` when fishing_good is placed, and the length trim never drops
  fishing_good. `spineVariantCount` still draws dinner from 0..2.
- **Memory**: `rod_lost` (printed `rod=0|1`), set when the rod is thrown or
  towed away; the next fishing major reads it at its start (`MajorState::stick`)
  and clears it. fishing_log setting it as well is my call (the spec only said
  "the rod is gone this loop").
- **Filler**: `FILL_BIRDS_ON_WATER` with its own beat hooks
  (`birdsFillerBegin/Update/End` in majors.cpp; it borrows MajorState for the
  birds and glyphs). It does not set `busy`; its name is not a calm one, so the
  minors stay away while it runs (as they do for majors).
- `appWorld` (test hook in `ss_app.h`).

### Numbers

- `sizeof(App)` = 67,024 bytes (was 66,664; budget 98,304).
- Strips per frame (test_scene, rendering every tick): fishing_small 7.75 (max
  13), fishing_log 7.39 (11), fishing_good 7.39 (13), bathe 6.85 (10),
  birds_on_water 6.55 (12). All majors per seed 7.55..7.80.
- Script finish over 450 loops (`-DSS_SPINE_TRACE`, seeds 1..10, moods 4, 0, 2):
  fishing_small 52.2..57.2 s of 60, fishing_log 48.2..53.2 s plus its empty
  seconds of 56 plus them, fishing_good 43.4..46.8 of 50, bathe 46.0..50.9 of 54.
- `--loops 20`, seed 1, mood 4: a stream major in 14 loops; dinner=3 in loops
  1, 8, 12, 16 (each with fishing_good); birds_on_water in 2.
- testVariety (30 night loops, 83 majors): every major 3..9 times; the four new
  ones 6, 4, 5, 3.
- Builds clean with GCC 16 (`-Werror`) and clang; all 6 suites pass under both.

### Tests added

`testStreamVignettes`: seeds until all five and fish_dinner have run (4
seeds), rendering every tick: the hiker in frame and out of the corner while
visible; at the end of each of those beats in frame, dry (no waterline, wade
off, feet not in the stream) and with his hat; rod, hat and fish items, the
log, the dead fish and the thrown rod never in the quiet corner; dinner=3
exactly when fishing_good is planned; no overruns; strips per frame printed
and <= 10; determinism at 3 ticks inside fishing_log; 25 loops: the rod lost 8
times, the stick used 7 times, no flag mistakes. The existing tests take the
larger pools (16 majors, 7 fillers, a 4th dinner column).

### What I changed after looking at the frames

Contact sheets cut from `ss_preview --skip --every` at the logged ticks (mood
4, seeds 1027777925, 892470523, 2791936963, 1517378683), cropped and zoomed.

1. The fish never showed over the fire: an `on_fire` item still has to be
   `down` to have bounds. Fixed; the fish on its stick reads against the flames.
2. Held up by the tail with REACH_UP, the fish hung over his own arm (ACTOR on
   ACTOR) and vanished. The near arm now goes forward and up to arm's length
   and the fish hangs clear against the ridges; carried, it is held out ahead.
3. The bath: sat on the centreline, his torso stood against the bank and he
   looked perched on the edge. He sits in the near half of the stream now, so
   water shows behind him too; his knees break the surface (left in: it reads).
4. The third tiny fish stayed hanging in the air after he threw the rod. It
   now drops back into the water as the rod leaves his hands.
5. The towed hiker crossed the frame's bottom edge where the stream leaves it:
   his feet and the line's end are kept inside; lean back increased to read as
   heels-in water-skiing.
6. The tow never ended once the log left the frame (he stopped following it):
   he is towed on at the log's speed.
7. The rod's bow was an L-shaped hook: 1.05 to 0.75 rad.
8. The tiny fish at 0.38 scale was a speck: 0.5 (still tiny: that is the joke).
9. Durations set from the trace (fishing_small trimmed by 1.6 s of stills).

### Coordinator additions (same pass)

- **Rim light** (`hiker.cpp`): the rim was two capsules a sub-pixel inside the
  torso and head edge, drawn before the arms; in most poses the near arm, the
  knees or the brim form the fire-side edge, so it showed as a broken line
  inside the silhouette. It is now a pass after the whole figure (`rimPass`): on
  each row of the head (below the brim) and the torso, it walks from the body's
  axis toward the fire through the silhouette's pixels and lights the last one
  (and half the one inside it), only where that edge is within the torso's
  half width (or the pack's depth when his back is to the fire). Rows where an
  arm or knee stands out past the body stay dark. Mirror-exact and strip-exact
  (test_hiker passes: 0 pixels differ from the flip). Checked in stills by the
  fire at mood 4 (dinner, the fire on his left: the rim on his left edge) and
  at `--night` (it showed nothing at first: the night ground was within the
  "solid figure" tolerance of ACTOR; the tolerance is now one step a channel).
- **GCC 16**: `ambient.cpp` smoke out-params initialised, `critters.cpp` carry
  conditional cast to `uint8_t`, `spine.cpp` filler loop bounded by
  `SPINE_FILLER_MAX`. GCC 16 Release `-Werror` builds clean, ctest 6/6.

### Open items

1. Frequency: with four new majors of weight 1, a stream major plays in about
   two loops of three. If that is too much water, lower their weights.
2. "A bird may swoop for the gut bit" (fishing_good) is not built.
3. The dead fish's x eye is under a pixel at 1x; it reads as a belly-up fish
   by its pale belly line on top, not by the eye.
4. The waterline redraw duplicates the landscape's water polygon
   (`worldDrawWater` vs `landscape.cpp drawStream`); keep them in step. I did
   not touch landscape.cpp (the palette agent was editing it).
5. Not checked by eye: the stick variant's cast (no float, by design), the
   bath and the log at night, the tent-less storm-off branch (never runs: the
   tent is always up after setup), fish_dinner's skeleton toss at full frame rate.
6. The rod thrown across the stream lies on the near bank (toward the viewer)
   for the rest of the loop, and the one set down in fishing_good on the camp
   bank; nothing picks them up. With rod_lost the next loop has no rod lying.
