# The deer visit while the hiker is away: design and build plan

Written 2026-11-18 from Mike's request of the same day. Design only: nothing here is built. Every
number marked PROPOSED is a starting value to tune with the stills; everything about how it looks is
UNVERIFIED until Mike has seen it, and on the panel. References like `spine_midday.cpp:2078` are lines
on 2026-11-18 in `screensaver/core/scene/`; they drift, search for the name. A claim about the code
that I did not read myself is marked UNVERIFIED.

## Mike's words

2026-11-18: "when the hiker is away, a group of deer walk into the site, some stop to munch on grass,
a couple investigate the tent, carefully, one fawn splashes in the stream, the momma deer lifts her
head and tail alerts which the other deer alert to, and the Mom trots off the side of the screen with
other semi-alert deer, a little later the hiker walks back in."

And for all five new pieces: "Consider other interactions with animals and these from other
vignettes."

Offered earlier and not rejected (used below): the alarm triggered by real events (someone at the
panel, the doorbell, a house dog going out); hoof prints or a nibbled tent flap the hiker finds when
he is back.

---

## 0. Proposed answers to the open choices (ask Mike at review, build these meanwhile)

| Choice | Proposed default | Alternative (one constant or flag) |
|---|---|---|
| When it plays | midday loops only, as a new beat `herd` between the build and his return from the day hike | none that fits: see section 1 |
| How often | 30 % of midday loops that pass the gates, never two midday loops running (`kHerdShare = 0.30f`) | 0.20 or 0.40 |
| How long | 104 s for every variant, of which the first 5 s and the last 8 s are an empty camp (`kHerdS = 104`) | 92 s (drop the settled picture to 3 s and the look-in's second stop) |
| The herd | 5: the doe (the Mom), her fawn, two yearlings, a second doe. 6 in a quarter of visits, 4 in a sixth | always 5 |
| Where they come from | the left edge, out from behind the two big foreground pines, in ones and twos | the right edge along the camp-side bank |
| Where they go | left (back to cover) when the hiker is coming; right when a house dog comes out (the dogs come in from the left) | always left |
| What sets the doe off | by default the clock: at 80 s she hears him coming. Earlier: the real doorbell, a house dog going out, a lightning strike | the clock only (`kHerdRealAlarms = false`) |
| Someone walks up to the upstairs panel | not an alarm: three of them lift their heads and hold still for 3 s, then go back to grazing (they noticed you) | a full alarm |
| The white tail | a small ACTOR_LIGHT flag at the rump, drawn only while a tail is up | no flag, the alert is heads only |
| The fawn | the same rig at 0.62 with fawn proportions and three pale spots (`Critter::tone` bit) | a plain deer at 0.62 |
| Signs he finds | the tent door a hand's width open (the yearling tugged the flap), wet hoof prints at the bank, prints at the door. He looks, zips the flap, scratches his head | prints only, no flap |
| A young buck | no antlers anywhere | September to December one yearling has two short spikes (one more primitive) |
| deer_stick and the plinking in a herd loop | neither is planned in a loop that has the herd | allow deer_stick in the last slot only (reads as one coming back) |

---

## 1. When he is away, and why this is a beat and not a major

What I checked:

- **Midday** (`spine_midday.cpp`): `middayLayout` (line 2050) sets `w.fx.on = false`, "He is out on a
  day hike: no pack on him, out of the scene until the return". The timeline (`middayBuildTimeline`,
  2078) is `build` (13 s, the camp pops in: tent up, pack, bag hung, wood, cairn, the pit cold under
  ash), then `return` at once: `doEnter` (185) puts him at `camp.edge_r` (848, the ford's near bank),
  and he walks in over the ford (`RETURN_IN`, 1393). So the camp is already standing and empty, he is
  already away, and he already "walks back in". The visit goes between `build` and `return`.
- **Evening firewood** (`spine.cpp:1679`, `waitAway`, `plan.away_s` 6 to 10 s) and the **morning
  stay's pee** (`morning_stay.cpp:970`, 12 to 18 s): far too short, and the evening scripts are
  approved and must not change.
- **Evening arrival**: the camp is not pitched yet, so there is no tent to investigate.
- **The morning stay's day hike** (`morning_stay.cpp` creepTick, 859): he leaves, a chipmunk and a
  bird creep back before the fade. He does not come back in that loop. Left alone (a possible later
  teaser, section 6.6).
- **A major cannot do it.** `majorBegin` (`majors.cpp:3796`) points `m.p[]` at `w.hiker.feet`, sets
  `w.busy` and starts a hiker script; the midday slots are after return, lunch and wade
  (`kMiddaySlots`, `spine_midday.cpp:2005`), all with him in camp; HOW_TO_ADD_SCENES section 2 says a
  major "must leave him in frame, at rest", and the tests check a major's hiker rectangle
  (`test_midday.cpp:326`). There is no slot before the return.

So: **a new midday timeline beat named `herd`**, with its own begin, update and end hooks, added by
`middayBuildTimeline` right after `kMiddayBuild` when the plan says so. It is not a `MiddayBeatId`
(no variants table, no pause after it, `MD_COUNT` stays 6), not a filler and not a major. What allows
it: a `Beat` is just `{name, seconds, begin, update, end}` (`anim/scheduler.h`), the timeline has room
(64 beats), and nothing in `test_midday.cpp` fixes the beat after `build` (it checks at tick 320 that
he is not in the scene, and from then on only when he is visible).

What cannot run while he is away (verified): the doorbell reaction (`reactCanStart`, `react.cpp:26`,
needs `w.fx.on` and a visible hiker), fire and ice (`fireIceCalm`, `fireice.cpp:769`, the same), the
visiting couple (`visitorsCanStart` ends in `fireIceCalm`), the weather's hiker gags (`weather.cpp:173,
240`), the minors (the beat's name is not in the calm list, `minors.cpp:1343`), the stream's heron and
lone deer (they start only in a pause or a calm filler, `stream.cpp:680, 743`). So nothing freezes the
story during the visit, and nothing else puts a critter in the scene. The house dogs and the mail bird
can be there: section 6.

---

## 2. Where everything is (frame pixels, 800 x 480)

Examples use F = fire (250, 398), camp_x = 560, so the tent is at (206, 352). F.x is 238 to 262, F.y
393 to 403, camp_x 550 to 570. All spots are formulas from the layout, never constants.

Existing things:
- **Tent** `camp.tent` = (F.x - 44, 352), about 76 px wide (tent.x - 38 to tent.x + 38), ridge 44 to
  50 px up, door toward +x; `camp.door` (tent.x + 20, 356) is where he stands at it. The flap's middle
  is about (tent.x + 30, 338) (UNVERIFIED to the pixel: take it from `tentGeom`, `props.cpp:99`).
- **Pack** (tent.x - 30, 359). **Reading rock** (F.x + 18, 348), in the cache. **Wood pile**
  (F.x - 30, F.y - 8). **His seat** `camp.sit` (F.x + 40, F.y - 6). **Cairn** (F.x + 150, bank - 6).
- **Camp pine**: trunk at camp_x, base y 340, lowest foliage ends y 318. It is in the static cache and
  critters are drawn over the cache, so **a deer's feet must be at y >= 343 wherever its bounds cross
  x camp_x - 70 to camp_x + 70** (it is then in front of the pine, which is what the draw shows).
- **Foreground pines** fg[0] x 14 to 26 and fg[1] x 64 to 76, bases about y 440 and 450 to 470; fg[1]'s
  skirt reaches x 136 to 157 at y 378. They are redrawn over any critter behind them (`addPines`,
  `world.cpp:62`), so a deer coming in at the left edge is half hidden by them for free.
- **Stream** (`landscape.cpp:63`): centre line through (800, 372), (640, 386 +-5), (470, 420 +-5),
  (300, 448 +-5); half width 20 px at the right to 27 px at x 300. Camp-side water edge
  `bankTop(x) = cy - half` from `landscapeStreamSpan`: about y 350 at x 800, 363 at x 640, 401 at
  x 440, 420 at x 300.
- **Ford**: `camp.ford0` (732, near bank, y <= 410), `camp.ford1` (652, bankTop - 8).
- **Chip box** x 538..796, y 412..476 and the quiet corner x >= 660, y >= 420: nothing of the visit
  goes there. Standing deer are all at x <= 512, y <= 380; the fawn in the water is at x <= 470,
  y <= 410; a herd leaving to the right runs along y 346.

The herd's spots (PROPOSED; `feet` points; a standing adult is about 64 px long and 48 px tall):

| who | role | scale | spot | example | faces |
|---|---|---|---|---|---|
| D0 | the doe | 1.00 | G0 = (max(camp_x - 98, F.x + 214), 358) | (464, 358) | right |
| D1 | the fawn | 0.62 | Gf = (G0.x - 54, 368) | (410, 368) | right |
| D2 | yearling at the door | 0.90 | T2 = (tent.x + 76, 361); waits first at T2 + (24, 0) | (282, 361) | left |
| D3 | yearling at the wall | 0.86 | T3 = (tent.x - 8, 370); grazes first at T3 - (20, 0) | (198, 370) | right |
| D4 | the second doe | 0.96 | G4 = (F.x + 104, 374) | (354, 374) | left |
| D5 | a third grazer (herd of 6) | 0.92 | G5 = (camp_x - 44, 372), in front of the camp pine | (516, 372) | left |

Other points:
- Way in: spawned at x -44 (out of frame) on lanes y 372 (doe), 376 (fawn), 369 (D4), 362 (D2),
  371 (D3), 366 (D5). The doe's first stop is x 58 (head and neck clear of the frame edge, body behind
  the pines), her second x 100.
- The fawn's water: S = (F.x + 190, bankTop + 4), example (440, 405): 4 px into the shallows, 30 px
  right of the cairn, 170 px left of the boulder. Its dashes go S.x + 26, S.x - 8, S.x + 10.
- Way out, left: lanes y 378 (doe), 368 (D4), 364 (D2), 372 (D3), 374 (fawn) to x -80. Way out,
  right (a dog): every deer moves to y 346 by x 520 and runs to x 880.

Rule the test checks: in the settled picture (section 3, 72 to 80 s) no two deer's bounds overlap,
and every pair is at least 2 px apart. Flat black silhouettes merge when they touch.

---

## 3. Choreography, beat by beat (variant A)

Seconds are from the beat's first tick, PROPOSED. Walks are given as true px/s: the builder divides
by the critter's `(0.8 + 0.4 * speed_dial)` factor (`critterMoveTo`, `critters.cpp:1618`) so a speed
here is the speed on screen whatever the loop's dials are. Steps that depend on a walk wait for the
arrival (section 7.2), so a slow seed delays a deer's own next step and nothing else. The alarm time
is absolute.

Deer actions used: `CA_MOVE` (the four-beat walk), `CA_RUN` (the bounding canter), `CA_LOOK` (head
up), `CA_HEADTURN` (head high and back), `CA_STARTLE`, `CA_IDLE` exist for the deer
(`critters.cpp:69`). New for the deer only (section 7.1): `CA_EAT` (graze), `CA_REACH` (the cautious
stretch), `CA_SHAKE`; the raised tail (`Critter::tail_add`), a stamp (`Critter::fore_add`), an ear
flick and the fawn (bits of `Critter::tone`).

**The empty camp (0 to 5 s).** Nothing. The camp has just popped in. Grass, water, a cloud.

**The doe looks in (5 to 20 s).**
- 5.0: the doe walks in at the left edge, 18 px/s, lane y 372. She shows from about 6.5 s, behind
  the pines' skirts.
- 10.7: she stops at x 58, head up (`CA_LOOK`, 0.5 s blend) and holds 2.4 s. One ear flick at 11.8.
- 13.1: she looks back over her shoulder at the others, who are not in sight yet (`CA_HEADTURN`
  1.2 s), then forward again 0.8 s.
- 15.1: three slow steps to x 100 (14 px/s). 18.1: nose to the ground for 1.6 s (`CA_EAT`), head up
  0.6 s. Nothing has happened to her, so she commits.
- 20.3: she walks to G0 at 26 px/s, in front of the tent and past the cold fire, arriving about 34 s.

**The others follow, in ones and twos (16 to 43 s).**
- 16.0: the fawn, 30 px/s, lane y 376, keeping 50 px behind her. When she stops it takes two more
  steps, stops, and looks up at her. It follows her across and arrives at Gf about 35.5.
- 19.0: the second doe D4, alone, 24 px/s. She pauses 1.0 s at x 60, then goes straight to G4,
  arriving about 36.6, and puts her head down at once. She is the one who eats.
- 26.0 and 27.2: the two yearlings, a pair, 24 and 22 px/s. D2 stops at x 72 to look for 1.5 s while
  D3 walks past it, so they swap order. D3 stops 20 px short of the tent at 37.3 and grazes there,
  as if the tent were nothing. D2 arrives at its waiting spot, 24 px short of the door, at 42.1.
- (Herd of 6: D5 comes in with D4 at 20.2, half a length behind her, and goes on to G5.)

**Grazing (from each one's arrival to the alarm).** Each grazer runs its own cycle from the herd's own
random stream: head down (`CA_EAT`, 0.7 s blend) for 3.5 to 7 s; head up, chewing, for 1.2 to 3 s
(`CA_IDLE`, sometimes `CA_LOOK`); then either one step forward (6 to 10 px at 9 px/s) or straight
back down. An ear flick (3 ticks) every 2 to 6 s, each deer on its own timer. A tail flick (the flag
shows for 4 ticks at 0.35) once every 8 to 15 s. Two rules keep it from looking like a machine:
- no two deer change in the same 0.3 s (a change that would is put back 0.3 s);
- the doe never has her head down when everyone else's is: if she is the last one up she stays up
  until another head rises. She is the watch.

**The tent, carefully (43 to 61 s).**
- 43.0: D2 stands looking at the tent, 1.5 s, dead still. 43.4: D3 lifts her head from the grass.
- 44.5: D2 takes the 24 px to T2 at 9 px/s (2.7 s), stopping once half way for 0.4 s. 45.5: D3 takes
  her 20 px to T3 at 8 px/s.
- 47.6: D2 stretches (`CA_REACH`, 1.8 s): neck out level, weight back on the hind legs, so the nose
  goes forward while the body leans away. The blend's own small pull-back first (BLEND_ACTION) is the
  hesitation. The nose ends 6 px from the flap. 49.4 to 50.6: two small sniffs (head_add +-0.03).
- 49.0: D3 stretches to the tent's front corner from the other side, slower (2.0 s). The two noses
  end about 8 px apart at the door corner.
- 51.6: **the fabric moves.** `tent.wobble` goes +2 px for 4 ticks, -1 for 4, 0 (whole pixels, three
  redraws of the tent). In a wind the next gust is used instead if one comes between 50 and 53 s
  (`weather.gust > 0.5`); with the mail bird on the ridge, its next idle move in that window
  (section 6.3).
- 51.68 (2 ticks later): D2 flinches: `CA_STARTLE` in 0.25 s and two steps back, 14 px at 60 px/s,
  still facing the tent (`directorWalk(..., back = true)`). 51.84: D3 flinches in place: startle,
  legs braced, no step.
- 51.9 to 54.2: **both hold dead still for 2.3 s.** One ear flick from D2 at 52.9. The doe's head
  comes up at 51.9 and she looks their way for 1.6 s, then goes back to the grass. D4 never looks up.
- 54.2: D3, the bolder one, stretches again, slower (2.4 s), and touches the wall: `tent.wobble` +1
  for 3 ticks. Nobody flinches this time; D2's ears go. 57.4: D3 loses interest and grazes where she
  stands.
- 55.0: D2 comes forward its 14 px again (9 px/s), stretches (57.0 to 58.6), takes the flap in its
  lips and tugs twice (59.0 and 59.7): `tent.door` 0 -> 0.12 -> 0.05 -> 0.14 -> 0.10. It lets go at
  60.4, looks at the flap for 1 s, ambles 26 px toward the meadow and grazes. The door stays a hand's
  width open: the first sign.

**The fawn in the stream (57 to 72 s).** It starts as the tent business winds down, so the two never
compete for the eye.
- 57.0: the fawn leaves its mother and walks to the bank (26 px/s, about 2 s).
- 59.0: head down to the water (`CA_EAT` pose) for 1.5 s, one ring at its muzzle (a `BIT_RING` at
  spin 0.7). Its head jerks up: cold. 0.5 s still.
- 61.0: it paws the water twice with a fore hoof (`fore_add` 0.5 rad, 0.3 s each), a ring each time.
- 62.4: it bounds in: 26 px along the shallows at 70 px/s (`CA_RUN`), three drops thrown up at the
  landing (`BIT_DROP`, which fall back and turn into rings by themselves, `props.h:248`). It spins
  round and bounds back 34 px, drops again.
- 64.2: it stops braced, legs wide (the startle pose held 0.4 s), and shakes (`CA_SHAKE`, 0.9 s),
  two drops off it.
- 63.5 to 65.5: the doe lifts her head and watches it (`CA_HEADTURN` if it is behind her), then
  grazes again.
- 66.0: a third, shorter dash (18 px); it stumbles at the end (crouch 0.5 for 0.3 s), gets up, and
  stands in the water with two tail flicks.
- 68.6: it climbs out and walks back to Gf (34 px/s), leaving five wet prints on the bank (section
  3.2). 70.8 to 72.3: the doe lowers her head to it and they touch noses.

**The settled picture (72.3 to 80 s).** Nothing new for 7.7 s: five deer, heads going down and coming
up out of step, an ear here and there. This is the still Mike should judge first.

**The alarm (80 to 86 s).**
- 80.0: the doe's head comes up fast (`CA_LOOK` in 0.18 s, not the usual 0.5). If she faces left she
  turns first. She looks right, toward the ford, and does not move for 1.4 s. Nobody else has noticed.
- 81.4: her tail goes up (flag 0 to 1 in 0.25 s).
- 82.0: she stamps: the near fore hoof lifts (0.55 rad over 0.2 s), holds 0.15 s, strikes down in
  0.08 s, with a 1 px dip of the body. 82.9: again, with a small dust puff at the hoof (`BIT_DUST`).
- The alarm passes through the herd, nearest first, each in its own way:
  - 82.2 the fawn: head up, but it looks at its mother first, and only 0.5 s later where she looks.
  - 82.7 D4: caught mid-mouthful, her head comes half way up and stops, then the rest 0.4 s later.
  - 83.3 D2: head up cleanly, ears forward.
  - 83.9 D3: the last. She looks left first (wrong), then right.
  - Each one's tail follows its head by 0.4 to 0.8 s. The others' flags stop at half (0.5): they are
    going along with it, they have not heard anything themselves. The fawn's goes all the way up as
    soon as it sees hers.
- 84.0 to 86.0: **five heads up, all looking right, 2.0 s of complete stillness** but one small bob
  of the doe's head at 85.0.

**Off (86 to 95 s).**
- 86.0: the doe wheels left and goes, a springy canter (`CA_RUN` at 70 px/s, flag up), lane y 378.
  Not a panic: the director's flee speed for a deer is 140.
- She passes the others on her way out and each wheels and falls in behind her as she goes by: D4 at
  about 87.7 (66 px/s), D2 at 88.7 (68 px/s), D3 at 89.8 (66 px/s). They leave as a loose line, not a
  burst.
- The fawn stays frozen, watching them all go. 89.0: it starts the wrong way, 10 px toward where she
  stood, slips (crouch 0.4 for 0.2 s), turns and runs after them at 96 px/s, legs everywhere. At
  x 120 it stops dead for 0.6 s and looks back at the camp, then goes behind the pines. Last out,
  about 94.3.
- At each deer's start a hoof print or two is left where it stood (section 3.2).

**Stillness (95 to 104 s).** The empty camp. The flap hangs open; the wet prints by the water are
drying. Then the beat ends and `return` begins: he comes into sight at the right edge about 1 s later.

### 3.1 When something real sets her off (the hard alarm)

Before 80 s, any of these starts the alarm at once, from wherever each deer is (a deer not yet in
the frame turns back and is released):

| trigger | how the code sees it | which way they go |
|---|---|---|
| the doorbell | `appTick` calls `herdBell(world)` on `EV_DOORBELL` (the reaction itself cannot start, he is away) | left |
| a house dog goes out | `w.dogs.want` becomes non-zero (the app copies `Inputs::dog_out` every tick) | right: the dogs trot in from the left 1 to 3.5 s later (`housedogs.cpp:747`) |
| lightning | `w.weather.flash > 0.5` | left |

The hard alarm is the same sequence compressed: the doe's head up in 0.12 s, her flag at +0.3 s, no
stamp, the wave's delays times 0.4 (all heads up within 0.8 s), every flag all the way up, off at
+1.1 s, all at once and fast (`CA_RUN` at 115 px/s), the fawn 0.3 s behind and never stopping to look
back. Everyone is out of sight within 8 s. With a dog: section 6.1.

**A second look** (`kHerdSecondLook = true`, PROPOSED): if a bell or lightning emptied the camp before
45 s, and no dog is in the scene, then 14 s after the last deer left the doe and the fawn look in
again, walk to G4 and the spot beside it, and graze there until the 80 s alarm sends them off as
above. Without it the rest of the beat is an empty camp. After 80 s nothing triggers anything.

### 3.2 Hoof prints

Small cloven marks drawn flat on the ground under everything (two 2 px dashes side by side, 3 px
long, with `blendSpan`, so they darken whatever ground is under them and need no palette slot and no
rasterizer scratch). At most 14:
- 5 wet ones where the fawn came out of the water, alpha 150 of 255 when made, drying to 70 over
  40 s (so they are half gone when he arrives);
- 2 scuffs where the doe stamped, 4 at the door where D2 stood, 3 along the first steps of the way
  out; alpha 90.
They stay until he has looked at them (section 3.3), then fade to nothing over 25 s in 8 steps and
the herd's state is cleared. If a daytime major begins first it clears the shared state and the
prints go at once (cannot happen in practice: return plus its pause is at least 43 s).

### 3.3 He walks back in and finds the signs

The three `return` scripts get two optional pieces, skipped in one tick when there are no signs
(`skipUnless(herdSigns, n)`), so a loop without the herd runs them exactly as now:

- `HERD_LOOK` (all three variants, right after `RETURN_IN`, on the camp bank at the ford, 4 s): he
  stops, `POSE_LOOK_AROUND` for 2.4 s over the camp (the open flap), then his head tips down to the
  ground in front of him for 1 s. Then on as before.
- `HERD_DOOR` (variants a and c, after `STICK_DOWN`, 12 s): `go(AT_DOOR, kStroll)`, `face(LEFT)`,
  crouch, `act(herdActZip, 1.0)` (`tent.door` back to 0), stand, `act(herdActPrints, 3.0)`: he looks
  down at the prints at his feet, follows them with his head to the left where they went, and
  scratches his head (`POSE_SCRATCH_HEAD` arms through `scriptSetArm`). `doit(herdDoSeen)` starts the
  prints' fade. Then `go(AT_SIT)` as before.
- Variant b (the limp) gets only `HERD_LOOK`; `herdDoSeen` runs from it. The flap then stays ajar
  for the loop: nothing at midday uses the tent door (he naps under the pine), and the next loop
  re-inits the tent.

The `return` beat is 4 s longer (b) or 16 s longer (a, c) in a herd loop: `beatSeconds` adds
`herdReturnExtra(p, variant)`.

---

## 4. Variants

One byte in the plan, `LoopPlan::herd`: 0 none; otherwise bits 0-1 the variant (1 A, 2 B, 3 C) and
bits 2-3 the herd's size (0 five, 1 six, 2 four: no D4). All are 104 s. They are the same blocks in a
different order, so the builder writes the blocks once (look-in, follow, graze, tent, splash,
picture, alarm, off) and a small table per variant.

- **A, 50 %**: as section 3.
- **B, 30 %, the false alarm.** At the tent the second tug pulls the zip: the flap falls open
  (`tent.door` to 0.35) and D2 leaps back 30 px in two bounds (`CA_RUN`). That sets off a small wave:
  heads come up one by one, the doe's first, no tails, no stamp. 3 s of five deer staring at a tent.
  Then they relax in the reverse order, D4 first (she wants her grass), the doe last. The splash
  follows, shortened to two dashes. The real alarm at 80 s then reads as the second, serious one.
- **C, 20 %, the fawn copies.** The splash comes first (43 to 58 s), the tent second (58 to 76 s),
  and the fawn, back from the water, trots over and stretches its own short neck beside D2: three
  noses. The fabric moves and the three flinch one after another, 0.12 s apart, the fawn's the
  biggest (it sits back on its haunches: crouch 0.6). The picture is 4 s.

Smaller variation, no plan bytes: the deer's brave or shy loop (`Memory::brave`, already rolled per
kind) is read at begin. A shy herd holds every look 30 % longer and the soft alarm comes at 76 s; a
bold one skips the doe's second stop.

---

## 5. Gating, weight, loop-length fit

- **Family**: midday only (`SF_MIDDAY`). It is always day there.
- **The pick** is made at the top of `middayPlan` from a private stream, as the morning stay's is
  (`spine_morning.cpp:1663`): `Rng pick; pick.seed(rng.state, kHerdStream);` then three draws, one
  per statement, always all three: `chance(kHerdShare)`, the variant (`weighted` over 0.5, 0.3, 0.2),
  the size (`weighted` over 0.60, 0.25, 0.15). The loop RNG is not touched, so a midday loop without
  the herd plans exactly as it does today.
- **Gates** (the pick is made anyway, then dropped): `in.dog_out != 0` (deer do not walk into a camp
  with a dog in it); the OUTSIDE data says rain or storm (`weatherKindForCode`, as `majors.cpp:3935`;
  snow is fine); `mem.herd_wait > 0`.
- **Memory**: `Memory::herd_wait` (new, 1 byte): set to 2 when a herd is planned, counted down by one
  at the top of every `middayPlan`. So never in two midday loops running.
- **Other pieces in a herd loop**: `majorsPlan` gets `if (p.herd) base[MJ_DEER_STICK] =
  base[MJ_PLINK] = 0.f;` beside the dog rule (`majors.cpp:3942`), and the long-range shooting's id
  joins that line when it exists. No shooting half a minute after the deer left, and no second deer
  story in the loop. (`majorsPlan` still draws the same number of values.)
- **Length**: `busySeconds` (`spine_midday.cpp:1926`) adds `kHerdS` and the return's extra when
  `p.herd`. Midday loops are 600 to 650 s today (seeds 1 to 3 checked with `ss_preview --loops`), so
  a herd loop makes `spineFitLength` squeeze the pauses and drop a filler or a major. Worst case I can
  build on paper (herd 104 + 16, the six beats 280, ropes 90 and slackline 80 with 16 s of stills,
  no filler, pauses at 8 s): 13 + 120 + 280 + 186 + 40 = 639 s. It fits, but the test must prove it
  over 200 seeds (section 9). If a seed does not land in 480 to 660, cap a herd loop at two majors
  before the fit, and say so.
- **Plan line**: `middayFormatPlan` prints ` herd=N` after `afternoon=` only when N is not 0, so
  every other loop's line is byte for byte what it was. `tools/review_clips.py` `parse_plan` reads any
  `key=int` token (line 173).
- **A no-show**: if a house dog is in the scene when the beat begins (it went out during the 13 s
  build), no deer come. The beat is then 104 s of the dog having the camp to itself (its own
  activities run: `w.busy` is not set). Rare, and accepted.

---

## 6. Interactions

### 6.1 Lucy and Copper
- Out at loop start: no herd (the gate).
- Going out during the visit: the hard alarm, to the right, the moment the input changes (the doe
  hears the house door). The dog trots in from the left 1 to 3.5 s later, by which time every deer is
  moving. `maybeChase` (`housedogs.cpp:485`) gets one more source: `if (!streamGuestAt(w, at) &&
  !herdGuestAt(w, at)) return;` where `herdGuestAt` gives the feet of the last deer still in the
  frame. So the existing chase plays as it does for the stream's deer: the dog sees them, head up,
  runs at them, stops well short, looks after them, and goes back to its own life. `runChase` already
  handles a deer on the camp bank (`housedogs.cpp:513`). The beat does not set `w.busy`, because that
  would send the dog to lie by the tent (`HA_SETTLE`) and block the chase.
- Deer never run at or through a dog: with the dog coming from the left and the herd going right
  there is always the camp between them. The test checks no deer bounds touch a dog's.

### 6.2 The doorbell, a passer-by, the status chip, the touch reveal
- Doorbell: hard alarm (3.1). The chip's pulse (rings out to x 526) dirties strips 12 to 14 for
  10 s; the deer are not under it. No freeze: he is not there to react.
- A passer-by at the upstairs panel (`Inputs::passer_by`, read in the beat's update from
  `ctx.in`, rising edge, at most twice a visit, not after the alarm): the doe and the two deer
  nearest the middle lift their heads (`CA_HEADTURN`, 0.35 s), ears up, and hold 3 s; then the usual
  cycle. With nobody for the hiker's wave to come from, the deer notice the visitor instead.
- Chip box: section 2. The reveal freezes `appTick` and everything with it.
- The doorbell reaction or fire and ice can start during `return`, when only the prints are left:
  `dayStep(w, frozen)` does not age the prints while frozen, and is not called at all while fire and
  ice or the visitors hold the story (`world.cpp:554-572`), so the fade just waits.

### 6.3 The mail bird
It sits on the tent's ridge at midday when mail waits (`mailbird.h`; brave, in its own slot). If it
is perched there (`w.mail.stage == MB_PERCH && w.mail.on_tent`), then "the fabric moves" is the
bird's next idle move in the 50 to 53 s window (`w.mail.idle` changing, read only), with a 1 px
wobble; if none comes, the plain wobble at 51.6. After the flinch D2 and D3 look up at the bird for
their 2.3 s of stillness (`head_add` +0.25). No edit to `mailbird.cpp`. The bird stays through the
alarm.

### 6.4 Weather
- Wind: the flinch waits for a real gust (3). Ear flicks come twice as often.
- A random rain (drawn after the plan): the visit plays; grazing bouts are 30 % shorter and heads
  are up more. Rings on the water are the weather's own as well.
- Snow, fog, heat, chill: nothing special. In fog the deer coming out from behind the pines get the
  fog bands behind them for free.
- A storm: no herd when the data says so; a random one's first strike is a hard alarm.

### 6.5 The stream's own life and the other deer
- Fish: a jump (`w.stream.fish.kind == FISH_JUMP`, `t < 0.2`) within 120 px of the fawn while it
  stands at or in the water makes its head snap toward the splash and its ears go. Read only.
- The heron and the stream's lone deer cannot come during the beat (section 1). Later in a herd
  loop the lone deer may still come to drink (about 1 loop in 100): it reads as one of them coming
  back, so it is left alone.
- PROPOSED small extra, ask Mike: the stream's deer, when something spooks it (`deerLeave(hurry)`,
  `stream.cpp:1091`), also raises its flag (`c->tail_add = 1`). One line; changes how an approved
  vignette looks, so it is off until he says yes.
- The evening's dinner deer and `deer_stick`: untouched. They draw no flag (their `tail_add` is 0)
  and the deer's idle, look, sniff, startle and gaits are not changed.

### 6.6 The other four new pieces
- **Reeds and cattails** (passive, by the stream): the fawn's water (x F.x + 170 to F.x + 216 on the
  camp bank) is where a clump would be brushed. What the herd needs from that module, if it exists:
  `void reedsNudge(World&, float x, float px)` (bend the clump nearest x for about a second). The
  herd calls it at each of the fawn's landings and when it shakes, and when the doe's way out passes
  a clump. Until the reeds exist the calls are behind `#if SS_REEDS` and compile to nothing. The
  reeds planner should keep a 50 px gap in the clumps there, or put a clump at one end of it, so the
  fawn is not hidden.
- **Morning Tai Chi**: deer watching him from the meadow's far left would suit it (he is slow, and
  `directorAvoidHiker` only scatters animals within 12 px of a standing hiker or 80 px of a walking
  one, `director.cpp:143`). `day_herd.h` exports `herdSpawnDeer(World&, role, feet, facing, tag)`
  (scale, fawn tone, persona) and `herdGrazeStep(World&, slot, HerdGraze&, Rng&)` (the out-of-step
  graze cycle with ear and tail flicks) so the Tai Chi builder can stand a doe and a fawn there
  without copying anything. Their choreography is that plan's.
- **Long-range shooting**: never in a herd loop (section 5). Nothing shared.
- **Mushrooms after rain**: the one mushroom is on the near bank (x 470 to 630, y 450 to 470,
  `landscape.cpp:192`), across the water and partly under the chip. The herd stays on the camp side.
  Nothing shared; the trip's sky is that plan's, and it can draw a `CR_DEER` at any `Critter::scale`
  if it wants one.
- Possible later, not in this build: at the end of the morning stay's day hike the doe's head looks
  in at the left edge for the last seconds before the fade, which would tie that morning to the
  midday visit.

---

## 7. What is added to the code

### 7.1 The deer rig (`critters.h`, `critters.cpp`), deer only

Today a deer is 6 primitives (4 legs, body, neck and head in one outline, `buildDeer`,
`critters.cpp:951`), budget 7, drawn at `Critter::scale` like every kind (`build`, line 1463), so a
smaller deer already works.

- `kKinds` deer row: add `bit(CA_EAT) | bit(CA_REACH) | bit(CA_SHAKE)`; budget 7 -> 10.
- `poseFor`, `CR_DEER` (PROPOSED values):
  - `CA_EAT` (graze): head -1.70, look -0.45, crouch 0.10, fore[0] 0.26, fore[1] 0.14.
  - `CA_REACH` (the stretch): head -0.85, look 0.35, crouch 0.05, pitch -0.03, hind[0] -0.14,
    hind[1] -0.04, fore[0] 0.12, fore[1] 0.02.
  - `CA_SHAKE`: head -0.20, crouch 0.08.
- `loopRate`: `CA_EAT` for a deer 0.55 cycles/s. `addLoop` `CA_EAT`, deer branch: head -0.05 *
  sin, look +0.10 * sin(2 ph + 0.2) (the jaw working, a small tug), no fore-leg motion.
- `buildDeer`:
  - the tail: when `k = clamp01(P.tail + c.tail_add) > 0.08`, one ACTOR_LIGHT leaf-shaped polygon
    (5 points) rooted at body point (-4.4, -9.6), length 5 + 4k px, width 4.5 px, pointing at
    `-(0.5 + 2.3 k)` rad from straight down (k 1: straight up and a little back). Nothing is drawn
    below 0.08, so every existing deer scene is pixel for pixel what it was.
  - `tone` bits (`constexpr uint8_t kDeerFawn = 1, kDeerEarBack = 2;` in `critters.h`). Fawn: legs
    1.12 x, body length 0.9 x, the head outline 1.25 x with the muzzle (points 6 to 8) pulled in
    15 %, and three ACTOR_LIGHT spots (ellipses 1.6 x 1.1) along the back. Ear back: head point 3
    (the near ear's tip) moved to (-4.4, -6.6) for as long as the bit is set.
- `Vec2 critterDeerNose(const Critter&)`: the muzzle tip in frame space (head frame (8.2, 2.4)).
  A new function, because `critterMouth` returns the top of the bounds for a deer and the stream's
  drinking rings are placed from that (`stream.cpp:1150`); changing it would move an approved
  vignette's rings.
- Primitives: adult 6, 7 with the flag up; fawn 9, 10 with the flag.
- Note for the builder, not a change: `addLoop`'s `CA_SNIFF` adds `1.0 * sin` to `look`, and for a
  deer `look` is radians of nose lift (`critters.cpp:535`, `critters.h:96`). That looks like a very
  large nod on the stream's drinking deer. I did not see it move (UNVERIFIED). The herd does not use
  `CA_SNIFF`.

No new `CritterKind` (so no `brave=` pins change), no new field in `Critter` (so `World` does not
grow).

### 7.2 State (in the daytime union, so `World` does not grow)

`DayState` (`day.h`) is zero and unused at the start of a midday loop: the daytime majors only fill
it while they run. The herd takes it as `DAY_HERD` from its beat's begin until the prints have faded.

```
#define HERD_MAX 6
#define HERD_PRINTS 14

struct HerdDeer {              // 40 bytes
  int8_t slot;                 // director slot; -1 not in yet, or gone
  uint8_t role;                // HR_DOE, HR_FAWN, HR_DOOR, HR_WALL, HR_GRAZER, HR_GRAZER2
  uint8_t mode;                // HM_OFF, HM_CUES, HM_GRAZE, HM_ALERT, HM_OUT, HM_GONE
  uint8_t cue;                 // index of its next cue
  uint8_t alert;               // 0 none, 1 head up, 2 tail up, 3 leaving
  uint8_t ear;                 // ticks left of an ear flick
  uint8_t head_down;           // grazing now (the watch rule reads it)
  uint8_t pad;
  float t;                     // seconds in the mode, or since its last cue
  float next;                  // graze cycle: seconds to the next change
  float ear_next, flick_next;  // seconds to the next ear flick, tail flick
  float tail, tail_to;         // the flag now and where it is going (0..1)
  float stamp;                 // seconds into a stamp, < 0 none
  Vec2 spot;                   // its spot
};
struct HerdPrint { int16_t x, y; uint8_t a0, a1; };   // 6 bytes: alpha when made, alpha once dry
struct HerdState {             // about 400 bytes
  HerdDeer d[HERD_MAX];        // 240
  HerdPrint print[HERD_PRINTS];   // 84
  uint8_t n, variant, n_prints, block;
  uint8_t alarm;               // 0 none, 1 soft, 2 hard
  uint8_t trigger;             // HT_TIME, HT_BELL, HT_DOG, HT_FLASH
  int8_t flee;                 // -1 left, +1 right
  uint8_t looks;               // passer-by looks so far
  uint8_t events;              // set from outside: bit 0 the bell
  uint8_t prev_in;             // last tick: bit 0 passer_by, bit 1 a dog wanted out
  uint8_t signs;               // bit 0 the flap is ajar, bit 1 prints lie, bit 2 he has seen them
  uint8_t door_step, print_q, second, noshow, shy;
  float t;                     // seconds into the beat
  float alarm_t, gone_t;       // when the alarm fired; when the last deer left the frame (< 0 not yet)
  float seen_t;                // seconds since he looked at the prints (the fade), < 0 not yet
  float wobble_t;              // seconds into the tent's twitch, < 0 none
  Vec2 tent, door, splash;     // layout, set at begin
};
struct DayHerd { HerdState h; Rng rng; };   // about 416 bytes
static_assert(sizeof(DayHerd) <= sizeof(DayBike), "the herd must fit in the bike's place");
```

Cues: each role has a `const HerdCue[]` per variant, `struct HerdCue { float at; uint8_t cmd; uint8_t
wait; int16_t a, b; }` (`at` the earliest time, `wait` 1 when the deer must have arrived first; `cmd`
one of ENTER, WALK_TO, STEP_BACK, ACT (a `CritterAction` and a blend in a, b), GRAZE_ON, GRAZE_OFF,
TAIL, STAMP, EAR, RING, DROPS, PRINT, TENT_WOBBLE, DOOR). A deer fires its next cue when `h.t >= at`
and (not `wait` or it has arrived). The alarm stops all cue lists and runs the wave from one small
table of per-role delays (soft and hard). Random numbers (graze lengths, flick timers, the drops'
speeds) come only from `DayHerd::rng`, seeded at begin from the layout as the archery's is
(`day_archery.cpp:38`, xor a constant of its own), one call per statement.

Other additions:
- `LoopPlan::herd` (`spine.h`, 1 byte) and `Memory::herd_wait` (`memory.h`, 1 byte, not printed by
  `memoryFormat`).
- `CritterTag` gets `CT_HERD` at the end (`world.h:113`).
- Water and dust use `World::bits` through `worldTakeBit` (`BIT_RING`, `BIT_DROP`, `BIT_DUST`): at
  most 4 at a time; a -1 is skipped. Nothing else uses bits in this beat.

### 7.3 Hooks

- The beat (`Beat{"herd", kHerdS, herdBeatBegin, herdBeatUpdate, herdBeatEnd}`): begin zeroes the
  union, sets `DAY_HERD`, the layout points, the Rng, the shy flag, the no-show; update advances
  `h.t`, reads `ctx.in->passer_by`, `w.dogs.want`, `w.weather.flash`, `h.events`, fires cues, runs
  graze cycles, the alarm and the exits, eases every `tail_add`, `fore_add`, `head_add` and ear bit,
  and steps the tent's twitch and the flap. It runs inside `Timeline::tick`, so it stops with the
  story. End: any deer still in the scene counts a spine overrun and is sent off by the nearer edge
  at 140 px/s (`directorExit`); `tent.wobble = 0`.
- A deer leaves through `directorExit` (mode `CM_FLEE`), so the director frees its slot once it is
  out of sight (`director.cpp:253`); the herd sees `!in_use` and marks it gone.
- `dayStep` (`DAY_HERD`): not frozen: age the wet prints and the fade; when the fade is done, or the
  beat is over with no signs left, zero the union. `dayTrack`: `herdDirty` (the prints' bounds when a
  print is added or the quantized alpha steps). `dayPartY` part 0 at y 319 (flat on the ground, under
  every ground actor) while prints lie; `dayPartDraw`: `herdPrintsDraw`. `dayBounds`, `sizeOf`.
  `dayPose` and `dayHoldsFacing`: nothing.

---

## 8. File plan

New files (LF, ASCII):
- `screensaver/core/scene/herd.h`, `herd.cpp`: the standalone part, no World: the state structs, the
  cue tables and the alarm tables, `herdInit`, the graze cycle (`herdGrazeNext`), the watch rule, the
  wave's order and delays, the prints (`herdPrintAdd`, `herdPrintsBounds`, `herdPrintsDraw`,
  `herdDirty`, `herdSame`).
- `screensaver/core/scene/day_herd.h`, `day_herd.cpp`: the World side: `herdPlanPick` (the private
  stream and the gates), `herdSeconds`, `herdReturnExtra`, the beat's three hooks, the cue commands
  on the director and the critters, the triggers, `herdBell`, `herdGuestAt`, `herdSigns`,
  `herdActZip`, `herdActPrints`, `herdDoSeen`, `herdSpawnDeer`, `herdGrazeStep`, `herdStep` (for
  `dayStep`).
- `screensaver/tests/test_herd.cpp`, `screensaver/tests/test_day_herd.cpp` (section 9).
- `screensaver/tools/herd_stills.cpp`, `screensaver/tools/herd_sheet.py` (section 10).

Existing files to edit, and nothing else:

| file | what |
|---|---|
| `core/scene/critters.h`, `critters.cpp` | section 7.1 |
| `core/scene/day.h` | `DAY_HERD` at the end of `DayKind`, `DayHerd` in the union, the static_assert, the include, the comment |
| `core/scene/day.cpp` | the `DAY_HERD` cases in `sizeOf`, `dayBounds`, `dayStep`, `dayTrack`, `dayPartY`, `dayPartDraw` |
| `core/scene/spine.h` | `LoopPlan::herd` |
| `core/scene/memory.h` | `Memory::herd_wait` |
| `core/scene/world.h` | `CT_HERD` |
| `core/scene/spine_midday.cpp` | `middayPlan` (the pick first, before any loop RNG draw; `herd_wait`), `busySeconds`, `beatSeconds` (the return's extra), `middayBuildTimeline` (the beat after `kMiddayBuild`), `middayFormatPlan`, the `HERD_LOOK` and `HERD_DOOR` macros in `kReturnA`, `kReturnB`, `kReturnC` |
| `core/scene/majors.cpp` | one line in `majorsPlan` (section 5) |
| `core/scene/app.cpp` | in the `EV_DOORBELL` loop of `appTick` (line 269): `herdBell(a->world);` |
| `core/scene/housedogs.cpp` | `maybeChase`: the second source (section 6.1) |
| `core/scene/stream.cpp` | only if Mike wants the flag on the stream's spooked deer (6.5) |
| `tests/test_midday.cpp` (CRLF, keep it) | the pinned midday lines, re-captured only after checking each difference is a herd loop or a loop after one; nothing else |
| `tests/test_critters.cpp` | only where a hard-coded deer number fails for the new actions (expect none: it loops over `CA_COUNT`) |
| `tools/review_clips.py` | only if an unknown beat name breaks the cutter (UNVERIFIED): add `herd` to the midday beats without variants |
| `sim/soak_main.cpp` | only if a rule there needs the new beat's name (UNVERIFIED; the lead did this for `day_hike`) |
| `docs/screensaver/NOTES_FOR_MIKE.md`, `WORKLOG.md` | the entries |

The evening and morning spines, `spine.cpp`, `props.*`, `hiker.*`, `landscape.*`, `director.*`,
`minors.*`, `mailbird.*`, `weather.*`, `react.*` are not edited. Two builders can split it: one takes
the rig (`critters.*`, its test and a deer pose sheet), the other everything else; the second needs
the first's `CA_EAT`, `CA_REACH`, the tone bits and `critterDeerNose`, so the rig goes first.

---

## 9. Tests

`tests/test_herd.cpp` (the standalone part):
- Cue tables: for each variant and size, times never go backwards per deer; every deer that enters
  has an exit path; the tables' last cue is before 80 s.
- Out of step: a 60 s graze run of 5 from 20 seeds: no two changes within 0.3 s; never all heads
  down; the doe's head is up whenever all the others' are down; each deer's bouts are within 3.5 to
  7 s and 1.2 to 3 s.
- The wave: soft: head-up times strictly increase doe, fawn, D4, D2, D3, each gap 0.3 to 0.7 s;
  tails follow heads by 0.4 to 0.8 s; only the doe's and the fawn's reach 1.0. Hard: all heads up
  within 0.8 s, all tails 1.0, off at +1.1 s.
- Prints: bounds hold every pixel the draw touches (drawn on a blank surface); `herdDirty` is empty
  on a tick where nothing changed; the fade ends at zero alpha and reports empty bounds once.
- Determinism: two runs give identical bytes.

`tests/test_critters.cpp` (existing loops) must pass with the new deer actions; add: the flag draws
nothing at `tail_add` 0 (a deer in every existing action hashes the same as before the change:
capture the hashes first), the fawn's bounds hold, `critterDeerNose` is inside the bounds and mirrors.

`tests/test_day_herd.cpp` (whole loops, like `test_day_plink.cpp`):
- Plan: over 400 seeds at midday: a herd in 25 to 35 %; none with `dog_out`, with rain or storm
  data, in the evening, morning or Lookout; never in two midday loops running (6 loops a seed); every
  variant and size comes up; a loop without the herd has the plan line it has today (compare with a
  build where `kHerdShare` is 0); every herd loop is 480 to 660 s; no herd loop has `deer_stick` or
  `plink`.
- Each variant from its seed, rendering every tick: the hiker is not in the scene before `return`;
  5 (or 4, 6) deer were in the frame; critters only of kind deer; every deer rectangle inside the
  frame rows and out of the chip box and the quiet corner; feet y >= 343 near the camp pine; the
  2 px rule in the settled picture; `tent.door` is 0.10 after the visit and 0 after return a and c;
  `tent.wobble` is 0 at the beat's end; no critter in the scene on the beat's last 8 s; zero
  overruns; `appCritterCount == 0` at loop end; the union is `DAY_NONE` before the first major;
  strips averaged over the beat <= 9; incremental render equals a full render at ticks inside the
  look-in, the flinch, a splash, the frozen herd, the way out and the prints' fade.
- Real alarms: a doorbell at 30 s, 50 s and 70 s: every deer out of sight within 9 s; the second
  look happens for the 30 s bell and not for the 50 s one. `dog_out` switched on at 40 s: the deer go
  right, no deer rectangle ever touches a dog's, the dog chases and stops, and its activities go on.
  A passer-by at 60 s: three heads up within 0.6 s, nobody leaves, grazing again by 65 s.
- The mail bird on the ridge, a wind loop and a rain loop: run clean, same checks.
- A no-show (dog out at tick 300): no deer, the loop is clean.
- Determinism: two runs of each variant hash the same frames; SS_XMAS ON and OFF. `sizeof(App)`
  printed and <= 30,000 (the PC wire limit, `test_day_majors2.cpp:664`).

All ctest suites in `screensaver/build-herd-on` and `build-herd-off`; `ss_soak` CLEAN, determinism
PASS. The evening and morning pins must not move at all. Do not edit a failing test until the reason
is known.

---

## 10. Stills to render for review (`docs/screensaver/reviews/stage22-deer/`)

`tools/herd_stills.cpp` (the `tools/plink_stills.cpp` pattern, not in CMake) finds a seed per variant
with midday forced, renders every tick as the panel does, and writes full frames plus 2x crops;
`tools/herd_sheet.py` (zlib only, `python -I`) makes the contact sheets.

1. A deer pose sheet: idle, graze, look, the stretch, startle, the canter at 4 phases, the stamp up
   and down, flag at 0, 0.5 and 1, an ear flick, the fawn in each, both facings, at day, golden hour
   and night palettes.
2. The doe looking in from behind the pines (full and crop).
3. The pair of yearlings passing each other at the left edge.
4. Two noses at the tent door; the flinch one tick and 0.5 s after; the flap ajar.
5. The fawn: drinking, pawing, in the air mid-dash with drops, braced, shaking; the wet prints.
6. The settled picture (the one to judge the spacing by), herds of 4, 5 and 6.
7. The alarm: doe alone head up; flag and stamp; the wave half way; the frozen five.
8. The way out: the line going left; the fawn's look back; the herd going right with Lucy coming in.
9. The empty camp after; him at the ford looking; at the door, zipped, scratching his head.
10. Variant B's five deer staring at the tent; variant C's three noses.
11. With the mail bird on the ridge; in fog; in rain; layouts with F.x 238 and 262.

---

## 11. Cost estimate (all UNVERIFIED on the panel)

- **Strips**: standing deer cover rows 310 to 380, so strips 9, 10 and 11 are redrawn every tick
  while any deer is in frame (critters are marked every tick), strip 12 during the splash. Those
  strips are mostly busy already with grass and water. Estimate 6 to 8 of 15 over the beat, against
  a loop average of 7.6; the test limit is 9. The upper sky strips are not touched. The hiker is not
  being drawn, which pays for about one deer.
- **Primitives**: 5 deer x 6 to 7, the fawn 9 to 10: at most 39 (46 for six), plus up to 4 bits and
  28 two-pixel spans for the prints. Whole frame well under 150.
- **Tent redraws**: the wobble 3 + 1 changes, the flap 5, the zip about 20 ticks: each the tent's
  two strips.
- **Palette rebuilds**: none. **Static cache rebuilds**: none.
- **`sizeof(App)`**: the union does not grow (about 416 of the bike's 1,904 bytes). `LoopPlan::herd`
  and `Memory::herd_wait` are a byte each and may land in padding: 0 to 8 bytes. The PC build with
  the Christmas set is at 29,856 of its 30,000 limit, the panel at about 28,640 of 32,768. Measure.
  If the PC limit is hit, keep the byte in `LoopPlan::fvar[7]` (unused at midday) instead.
- **Code**: about 950 lines new (herd 350, day_herd 600) and 130 in `critters.cpp`: about 18 to 24 KB
  at -Os on the host, going by the plinking's 34 KB for 1,780 lines. The same lands in PSRAM while
  the screensaver runs (646 KB free) and in flash (745 KB free). Report the host object sizes.
- **Critter slots**: 4 to 6 of the 8 story slots, for at most 95 s, with no other story critter
  possible then.

---

## 12. Risks

1. **Merged silhouettes.** Five flat black animals in one band of the screen. The spots are spaced
   and the test checks it, but walks cross (the doe passes the others on the way out). If it reads
   as a blob, drop to four, or spread the lanes further in y.
2. **The deer's look at this size.** 48 px adults and a 30 px fawn: the stretch, the ear and the
   flag are a few pixels each. The pose sheet (still 1) comes first; if the flag does not read, widen
   it before building the rest.
3. **Loop length.** 104 to 120 s more in a family whose loops are already near the top of the range.
   The fit should absorb it by dropping a filler or a major; the 400-seed test decides. Fallbacks:
   two majors at most in a herd loop, or the 92 s version.
4. **Pins.** Midday pinned lines change for herd loops and for the loops after them (the memory's
   ring differs). Loops with no herd before them must not change: that is the check that the private
   stream works.
5. **The shared union.** The herd keeps `DAY_HERD` into the return beat for the prints. Anything
   that assumes the union is empty outside a daytime major would trip: I found none (`dayPose`,
   `dayHoldsFacing` and `react.cpp` test for their own kinds), but the builder should grep `day.kind`.
6. **The dog chase** reuses a path written for one deer at the stream. With five deer leaving it may
   pick a deer that is already off screen: `herdGuestAt` must return false once the last one is past
   x 760 or below x 40.
7. **Speed dials.** Cues wait on arrivals, so timing drifts by seed. The alarm is absolute; if a
   slow seed has the fawn still in the water at 80 s the alarm must still read (the fawn just runs
   from the water). Test with the slowest and fastest dials.
8. **A long empty camp** after an early real alarm when the second look is off or a dog is about.
   Accepted: stillness is in keeping, and the dog is its own show.
9. **`BIT_DROP` is drawn dark** (it was made for a crumb; UNVERIFIED). If the drops read as dirt,
   the herd draws its own pale ones in part 0 (6 more small structs in the union).
10. **Panel speed** is a guess everywhere above. Report strips per frame and ms per frame from the
    host, and say "unverified on the panel".
