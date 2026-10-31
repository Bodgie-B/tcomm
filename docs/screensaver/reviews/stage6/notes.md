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
