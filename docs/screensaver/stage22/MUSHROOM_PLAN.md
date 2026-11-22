# Mushrooms after rain and the sky that lets go: design and build plan

Written 2026-11-18 from Mike's request of the same day. Design only: nothing here is built. Every
number marked PROPOSED is a starting value to tune with the stills; everything about how it looks is
UNVERIFIED until Mike has seen it, and on the panel. References like `landscape.cpp:192` are lines on
2026-11-18 in `screensaver/core/scene/`; they drift, search for the name.

## 0. Mike's words

"having mushrooms grow near the one mushroom after a rain storm, having hiker pick one, eat one, lay
down and have the sky go psychodelic for a couple minutes"

And for all five new pieces: "Consider other interactions with animals and these from other
vignettes." Offered earlier and not rejected: he wakes and tosses the rest in the stream.

Mike's standing rules that shape this piece: natural, classic, unhurried; "when something feels
rushed, add stillness, not more motion"; no ambient sound; the evening spine scripts do not change;
he is fine with a play being skipped by a rule (the plinking and the dogs).

## 1. What was found in the code (verified by reading)

- **The one mushroom.** `generateMushroom` (`landscape.cpp:192`) puts it on the viewer's side of the
  stream: x 470..630, y 450..470 (base of the stem), clear of the water by 10 px and of rocks by 24 px.
  It is drawn by `drawMushroom` (`landscape.cpp:867`): about 14 px wide and 14 px tall, a pale stem
  (WATER_LIGHT), a cap in the MUSHROOM slot with two pale spots. It is the last build layer
  (`BL_MUSHROOM`) and sits in the static cache, so it costs no strips. The swaying tufts keep 18 px
  away from it (`ambient.cpp:85`). The bee_visit minor flies to it (`minors.cpp:981`).
- **Where that is.** The stream runs from the right edge (y about 386 at x 640) to the lower left
  (y about 448 at x 300), half width about 25 px (`generateStream`, `landscape.cpp:63`). The camp is on
  the far bank. The mushroom's bank is the strip between the water and the bottom of the frame. The
  hiker never stands there today. The only crossing is the ford at x 652..732 (`spineLayout`), and
  the way from the ford to the mushroom runs through the status chip's box.
- **Part of the mushroom's range is under the chip.** x up to 630 with a 7 px cap reaches the chip's
  capsule (x 624..784, y 424..464) and the dog badges' place (x 538..624). That is how it is today;
  the chip is drawn over it. This plan adds nothing that moves in that box.
- **Rain.** The weather is one kind per loop (`weather.h`). With a reading from the house it is the
  forecast's kind (`Weather::from_data`, `weather.cpp:504`); with none it is a random draw from the
  weather's own stream. Nothing remembers that it rained: `Memory` (`memory.h:43`) has no such field.
- **Memory does not outlive a screensaver run.** `appStart` builds a new `App` each time the
  screensaver comes on (`ss_host.cpp:753`), so `Memory` starts at zero after every touch. Growth
  over an hour therefore needs the host to carry one byte between runs (section 8.3).
- **The sky** is five flat bands SKY0..SKY4 with edges at y 58, 118, 178, 228 (`drawSky`,
  `landscape.cpp:813`), then the stars, the sun and moon, then the cloud deck when the cover is 4
  eighths or more (its underside at y 94 to 158). All of it is in the static cache. The ridges
  (far crest y 214..262), the pines and the trees are drawn over it in the same cache.
- **Three ways the cache changes today.** A palette step repaints all of it (about 110 ms on the
  panel; spread one band a frame it is about 16 ms more a frame for 15 frames, `ss_host.cpp:121`).
  A build boundary does the same. And `Renderer::repaintRows(y0, y1)` (`renderer.cpp:26`) repaints
  only those rows with the same palette: the real sun and moon use it (`app.cpp:358`), fire and ice
  uses it for the ice (`fireice.cpp:983`). The static layers clip to the rows.
- **Measured on the panel** (WORKLOG rows 14, 15, the -O2 note): a full-width strip is about 5 ms;
  settled scenes run 21.6 to 25 fps (25 is the ceiling); layers are 60 to 70 % of the draw time.
- **Free layers.** The stream glints are marked dirty every tick whatever happens
  (`world.cpp` worldTick, `track(..., true)`), the swaying tufts every second tick, the hiker every
  tick. Anything that only changes how those are drawn costs no new strips.
- **Sizes.** `sizeof(App)` is 29,856 of 32,768 bytes (printed by `build-plink-on/test_scene.exe`).
  25 major ids of 32. The day union (`day.h`) is as big as the bike's state (over 1,600 bytes).
- **Loop fit.** A midday loop without majors or fillers is 286 to 325 s (build 13, beats 233 to 272,
  five pauses of at least 8). The fit aims at 490 to 650 s (`spineFitLength`, `spine.cpp:2732`).
- **Critters can be drawn at any size**: `Critter::scale` (`critters.h`), used by Lucy (1.3) and
  Copper (0.62); the director's clamp multiplies by it (`director.cpp:170`).
- **A dog can be borrowed**: `houseDogsLend` / `houseDogsGiveBack` (`housedogs.cpp:830`), made for the
  visitors' frisbee. It takes Copper first.
- **Fire and ice and the visiting couple never start during a major** (`fireice.cpp:779`). The
  doorbell can (`react.cpp:26`).

UNVERIFIED (not read or not run): how the doorbell reaction looks on a lying hiker; which way his
body extends in POSE_LIE; whether the chipmunk supports CA_SIT; whether an opaque polygon with a
level edge gives exactly the pixels of `fillRect`; every panel timing for the new work.

## 2. Proposed answers to the open choices (ask Mike at review, build these meanwhile)

| Choice | Proposed default | Alternative (one constant or flag) |
|---|---|---|
| What "after a rain storm" means | rain or a storm the house's forecast says is there (never a random one, so the pinned plans stay as they are) | also a random rain (`kMushRandomRain`; moves the pins) |
| How fast they grow | one step per dry loop after the rain, six steps to ripe (about an hour); the host adds a step per 10 dry minutes while the screensaver is off | faster: 3 steps (`MUSH_RIPE`) |
| When he eats one | the first midday loop with a ripe patch, by day, in dry weather | also the golden hour slot of the evening (`kTripEvening`; needs Mike's yes: it touches the evening's pool) |
| How often | once per rain: after it the patch is picked over and wilts; a new rain starts a new one | none |
| Where he lies | the cloud-watching spot in the grass on the camp side (`CampLayout::lie`) | on the mushroom's own bank (smaller sky, close to the chip) |
| How long the sky lasts | 104 s, of a 196 s beat | 80 s (`kSkyS`) |
| How the sky is done | the sky's own five bands start to wave and take pastel colours; a few day stars wheel; the sun breathes; repainted one band a tick in the cache (section 6) | the slow wash (same picture, a new pose every 12 s) |
| What else is in the loop | nothing but the fetch when a dog is out: the trip is the loop's only surprise | one short major more (`kTripAlone = false`) |
| The ending | he tosses the two that are left into the stream | one time in four he pockets one first (the 4 in the variant byte) |
| The dogs | Lucy (or Copper alone) comes and lies by his legs for the whole of it | the dog only settles by the tent as in any major |
| The far ridge breathes | yes, 2 px, 9 s | off (`kTripRidgeBreath = false`) |
| Name in the plan line | `mushroom` | none |

## 3. Where everything is (frame pixels, 800 x 480)

Examples use the fire at F = (250, 398), camp_x 560, the mushroom at (mush_x, mush_y).

### 3.1 The patch (static, in the cache)

Six small mushrooms, `MUSH_PATCH 6`, placed by `patchLayout` from the loop's own mushroom with no
random draw. Index, role, place:

| i | role | x | y before the bank rule | full size (of the mother's 14 px) |
|---|---|---|---|---|
| 0 | companion | mush_x + 13 if mush_x <= 545, else mush_x - 13 | mush_y + 2 | 0.70 |
| 1 | companion | mush_x + 24 if mush_x <= 545, else mush_x - 25 | mush_y - 5 | 0.55 |
| 2 | pickable | Nx + 12 | mush_y - 4 | 0.80 |
| 3 | pickable | Nx + 21 | mush_y + 2 | 0.65 |
| 4 | pickable | Nx + 29 | mush_y - 3 | 0.75 |
| 5 | straggler | halfway between pickable 4 and the mother, only when that gap is over 50 px | mush_y + 1 | 0.50 |

`Nx = min(mush_x - 44, 501)` is where he will crouch (his feet), facing right. So with the mother at
x <= 545 the pickables stand between him and the mother, 15 to 32 px left of it; with the mother
further right they stop at x 530 and the companions and the straggler carry the patch on to it.

The bank rule, per mushroom: `y = clamp(y, water_bottom(x) + 9, 474)` where `water_bottom(x)` is
`cy + 1.06 * half` from `landscapeStreamSpan`; a companion whose x would pass 616 is left out. No
mushroom is under the chip's capsule; companions may stand where a dog badge can be (x 538..616),
as the mother can today. Nothing that moves ever goes there.

How one looks: `drawMushroom`'s shape scaled by size step s (table in 8.1), the cap in MUSHROOM, the
stem WATER_LIGHT, one spot from 0.75 up. A button (step 1) is a 3 px cap on a 2 px stem. Each leans
1 to 2 px away from its neighbour (a table, PROPOSED) so the clump does not look stamped. At most 4
primitives each, drawn once into the cache.

In the build-up they pop after the mother, each 0.08 of `buildMushroomT` later than the one before,
with the mother's own peek, hold and overshoot at their size.

### 3.2 His places

- **AT_M0, the far bank where he steps in**: x = Nx - 50, y = far edge of the water there minus 4
  (about (400..451, 392..404)). If the cairn stands within 26 px of that x, x = cairn.x - 30.
- **AT_M2, midstream**: halfway between M0 and M1.
- **AT_M1 = N, the crouch at the patch**: (Nx, max of the pickables' y + 2), so he sorts in front of
  them. Nx is 426..501; his bounds reach at most x 536 (the reaching hand at Nx + 31).
- **AT_LIE** (`CampLayout::lie`, (F.x + 140, F.y - 44), about (390, 354)): where he lies, as the
  cloud_watch filler and the stargaze major do. Strips 10 and 11.
- **AT_M3, the toss spot**: (lie.x + 30, far edge of the water there minus 3), about (420, 393).
- **The bunch on the ground**: 14 px from his hip on the side away from the stream, at lie.y - 3.
- **Chip box** x 538..796, y 412..476: his nearest approach is the reaching hand at x 532 on the
  patch bank; the wade goes from the left, down and right, and ends at Nx <= 501. The fish, the
  floating caps and every critter stay at x <= 530 or y <= 400. The test in 10.3 checks it.

### 3.3 The sky

- Bands: the sky's own, rows 0..58, 58..118, 118..178, 178..228, 228..318.
- The repainted part of the cache: rows 0..319, as ten bands of 32 rows (strips 0..9).
- Day stars: the landscape's nine (`Landscape::stars`, x 10..790, y 10..195).
- The sun: where the sky has it (`SkyDraw` or the parked body), when it is drawn and not behind a
  deck.

## 4. Growth: what the viewer sees over a day

1. It rains at the house (the forecast's rain or storm). In the first wet loop's end the patch gets
   its first two buttons; the next loop shows them from its build-up on. Nothing more while it rains.
2. Each dry camp loop after that adds a step: more buttons come up, the older ones grow. Six steps
   (about an hour of loops) and all are full grown: ripe.
3. They stand there through the evening, the night (the MUSHROOM slot is dim at night already) and
   the morning, in every camp family. The bee still visits. Nothing else happens until a midday loop.
4. The first midday loop with a ripe patch, in dry weather by day, plays the major (section 5).
5. From the moment he picks them the pickables are gone. The companions and the straggler stand for
   6 more loops (PROPOSED), then the patch is empty until the next rain.

Stages are a single byte (8.1). The patch a loop shows is fixed at loop start, so it is static
content: no strips, no per-frame cost, all day and all night.

## 5. The major, beat by beat

It starts wherever the last beat left him (normally sitting at camp.sit) and ends sitting there, hat
on, hands empty, the day union zero, the sky natural, every critter gone, a borrowed dog given back.
Seconds are PROPOSED script seconds. Walks: the stroll is 34 px/s; here 30 where he is in no hurry.

### 5.1 He notices and crosses (about 21 s)

1. `pose(STAND 0.8, 0.3)`. (1.1)
2. `faceTo(AT_M1)`, `act(actSpot, 2.8)`: the near hand comes up to shade his eyes (near arm toward
   shoulder 2.3, elbow 1.7), a small lean forward (0.08), and after 1.2 s his head tips a little to
   one side: he has seen something. `still(0.7)`. (3.5)
3. `go(AT_M0, 34)`. (3 to 5)
4. `faceTo(AT_M1)`, `still(0.9)` with his head down: a look at the water. `doit(doWadeOn)`,
   `walk(AT_M2, 12)`, `act(actBalance, 1.2)` (both arms out a little, one slow sway: the stones are
   slick), `walk(AT_M1, 12)`, `doit(doWadeOff)`. The waterline on him is the World's (`fx.wade`,
   `world.cpp:261`). He wades in his boots, as the bathe does. (about 6.5)
5. `face(RIGHT)`, `still(0.5)`, `pose(CROUCH 0.9, 0.6)`, `act(actPeer, 2.2)`: head to one side, then
   the other; at u 0.6 a finger touches pickable 0 and it rocks (0.10 rad, settles in 0.5 s). (4.2)

### 5.2 He picks three, sniffs one, eats one (about 23 s)

6. `doit(doLift)`: the three pickables leave the cache and become the module's own (8.4); nothing
   changes on screen. Three times `act(actPick, 1.6)` with `still(0.3)` between
   (`repeatWhile(morePicks, 2)`): the near arm reaches (`fx.reach`), the mushroom tilts toward the
   hand, comes free with a 1 px hop, and joins the bunch in the far hand. The first pick sets the
   memory to picked (8.1). (5.4)
7. `pose(STAND 0.8, 0.3)`. The bunch (three small caps on stems, fanned) is in the far hand. (1.1)
8. `act(actSniff, 2.6)`: one from the bunch into the near hand, up under his nose; his head goes back
   a touch; two short sniffs (the head bobs 0.04 twice, 2 Hz); he holds it away at arm's length and
   looks at it. (2.6)
9. `act(actLookRound, 2.0)`: a look over the left shoulder, a look over the right. Nobody. (2.0)
10. `act(actNibble, 3.4)`: hand to mouth; three small bites 0.8 s apart, the cap a third smaller with
    each (the module's `bite` 0..3); a slow chew (0.03 nod, 1.5 Hz); the stem flicked over his
    shoulder into the water (one small ring, BIT_RING). (3.4)
11. `pose(HANDS_ON_HIPS 0.6, 3.2)`: he waits. Nothing happens. This stillness is the joke; do not
    fill it. Then `act(actShrug, 1.4)`. (5.2)
12. `faceTo(AT_M0)`, `doit(doWadeOn)`, `walk(AT_M2, 12)`, `walk(AT_M0, 12)`, `doit(doWadeOff)`. (5.0)

### 5.3 It comes on, and he lies down (about 15 s)

13. `doit(cueOnset)`: the free layers start (6.4): the grass begins to lean in one slow wave.
    `go(AT_LIE, 26)`: a slower walk than he came with. (about 4)
14. He stops. `act(actHand, 4.0)`: the near hand comes up in front of his face; he turns it over
    slowly, once; the fingers' end of the forearm sways (elbow +-0.15 at 0.8 Hz); his head follows
    it. (4.0)
15. `pose(CROUCH 0.7, 0.2)`, `doit(doBunchDown)` (the two that are left lie beside him),
    `pose(SIT 0.9, 1.5)` with his head back (a look at the sky: `fx.head` -0.25 over the sit),
    `face(RIGHT)` as the stargaze does, `pose(LIE 1.6, 1.0)`. `doit(cueSky)`. (about 6.5)

### 5.4 The sky (104 s): `act(actTrip, 104)`

He lies still for all of it. His breathing slows and deepens (`breath_rate` 0.6, `breath_scale`
1.6, eased over 10 s; both put back to 1 at the end). The sky's own clock t runs from 0 to 104 s on
the beat's clock, so it stands still during a doorbell reaction.

| t (s) | Sky (section 6) | Camp | Him and the animals |
|---|---|---|---|
| 0..14 | the four band edges begin to wave, to full height by 14; colour to a third | grass in one wave | still |
| 14..30 | colour to full; the day stars come out one every 2 s from 16 and start to wheel; the sun's halo breathes | glints turn pastel one by one | Lucy arrives and lies down (5.6) |
| 30..34 | everything drifting | | nothing else moves: 4 s of only the sky |
| 34..58 | drifting | | vision 1 (5.5) |
| 58..62 | drifting | | only the sky |
| 62..88 | drifting; from 84 the stars go out one every 1.3 s | | vision 2 (5.5) |
| 88..98 | colour fading from 86, gone by 100; waves flattening from 90, flat by 102 | glints and grass back by 98 | he lifts the near arm straight up and slowly traces one wave with it (3 s up, 4 s of a slow side-to-side, 2 s down); Lucy lifts her head, looks up, puts her chin down |
| 98..104 | natural from 102 | natural | still |

Nuance that keeps it from looking stiff: nothing in the sky starts or stops at once (every ramp is
an ease of 12 s or more); the four edges have different wavelengths and drift opposite ways, so
they never repeat a pose; the stars come and go one by one, never together; between the two
visions there are 4 s where only the sky moves.

### 5.5 What he sees: three sets (the variant's low bits)

Only what a lying man could believe he saw, one thing at a time, each arriving and leaving at a
walk. Never a face, never teeth, never anything that comes at the viewer.

**Set 0, the visitors.**
- Vision 1, the big chipmunk (24 s). A chipmunk (CR_CHIPMUNK, CT_MAJOR, CF_BRAVE, `m.c[0]`) walks in
  from the tent's side at its own size, stops 70 px from his head, looks (CA_LOOK, 1.5 s). It walks
  the last 40 px, and as it comes its `scale` eases from 1 to 3.2 (PROPOSED, about 45 px tall: half
  of him). It stands behind him (feet at lie.y - 6, so he lies in front of it), sits up, sniffs
  toward his face (CA_SNIFF, 3 s), holds (4 s of nothing), turns its head once (CA_HEADTURN). He
  slowly raises his far forearm and lowers it: a hello. It turns and walks off the way it came,
  shrinking back to 1 over the first 3 s, and leaves by that edge.
- Vision 2, the deer with the tree (26 s). A deer (CR_DEER, `m.c[1]`) walks in from the right along
  the back of the clearing (y 340) to lie.x + 120, stops side on, looks at him. Its antlers grow
  (the module's TP_ANTLER, anchored at `critterHead` every tick): two stems that fork three times
  (each fork 0.68 of the last, +-0.11 turns), 14 capsules at full, 56 px tall and 70 px across, in
  TRUNK mixed 30 % toward ACTOR, 2.4 px thinning to 1.2. Growth 10 s, eased, tips last. From 0.7
  grown, eight small leaves open at the tips (FOLIAGE dots r 2.2, one every 0.4 s). At full a small
  bird (2 primitives, the SH_PERCH look) flutters down and lands on a tip. The deer turns its head
  slowly; the tree turns with it. 5 s of stillness. The bird flies off, the leaves let go and drift
  down (3 of them, slowly, then gone), the antlers draw back in over 4 s as it lowers its head, and
  it walks out the way it came.

**Set 1, the water.**
- Vision 1, the slow fish (24 s). Three fish leave the stream between x 330 and 500, one every 6 s,
  each in a slow high arc (3.2 s, 60 px up, 110 px along the stream, downstream), each a different
  pastel (6.2), each trailing four dots that fade. Each goes back in with one ring (BIT_RING) and no
  splash. All come down at x <= 520.
- Vision 2, smoke rings from the cold fire pit (26 s). Five rings, one every 4 s, rise from the
  ash, each a flat ellipse outline (SMOKE mixed toward CLOUD_LIT, 8 capsules) that widens from 8 to
  26 px as it climbs 90 px in 9 s and thins away. The third one has a smaller ring pass up through
  it. No fire: the pit stays cold.

**Set 2, the sky comes down.**
- Vision 1, the clouds look (24 s). Both drifting clouds grow the ear the cloud_animal minor uses
  (`Cloud::ear` eased 0 to 1 over 6 s), and the nearer one gets two small eyes (dots in the band's
  colour) that look down at him. It blinks once, slowly (eyes shut for 0.5 s). The ears go back
  over 6 s before the end.
- Vision 2, the stars settle (26 s). Five of the day stars leave their circles one by one and float
  down (12 px/s, a slow side-to-side of 6 px), land on his chest, his hat, his knee, the bunch and
  the dog if she is there (else the grass), rest there glowing for 6 s, and fade one by one.

### 5.6 The house dogs

Never skipped for the dogs. When he lies down and a house dog is in camp, the major borrows one
with `houseDogsLend(w, HD_LUCY)` (Lucy first; Copper when only he is out; tried again once a second
until t = 60 if none is free yet). She walks (30 px/s) to the ground beside his legs, on the side
away from his head, at lie.y + 2, lies down (CA_LIE) facing him, and after 4 s puts her chin down
(`head_add` eased, as her nap). Her wag is her own (it runs while lent). At t = 88 she lifts her
head and looks up for 3 s, then down again. When he sits up she is given back
(`houseDogsGiveBack`) and goes on with her day. If she is called in meanwhile, housedogs.cpp gives
her back and she trots off: nothing waits on her. Copper, when both are out, stays settled by the
tent as in any major. The visions never touch or change the dogs: Lucy and Copper stay real.

With a dog out the midday loop also plays the fetch (it is forced first, as now); the mushroom major
is forced second. With no dog there is no dog in it.

### 5.7 After (about 28 s)

16. `still(2.0)`. `act(actStir, 2.2)`: his head rolls to one side and back, the near hand to his
    forehead. `pose(SIT 1.4, 1.2)`. `act(actRubEyes, 2.0)`. (about 9)
17. He looks at the two beside him (head down toward the bunch), `still(1.5)`; up at the plain sky;
    back at the bunch; `act(actHeadShake, 1.4)`: slow, twice. (about 4.5)
18. `pose(CROUCH 0.6, 0.2)`, `doit(doBunchUp)`, `pose(STAND 0.9, 0.3)`. (2.0)
19. `go(AT_M3, 30)`, `faceTo(AT_M2)`. (about 3)
20. Twice `act(actToss, 1.5)` (`repeatWhile(moreToss, 1)`): an easy underhand lob; the cap arcs
    30 px out and drops in with a ring, bobs up and rides the current away to the left and down
    (the stream's path, `landscapeStreamAt`, at the glints' speed), turning slowly, until it is off
    the frame. (3.0)
21. `pose(HANDS_ON_HIPS 0.6, 2.6)`: he watches them go. Under the second one a fish rises (one
    ring; the cap bobs). (3.2)
22. `until(crittersGone)`, `go(AT_SIT, 30)`, `face(TO_FIRE)`, `pose(SIT 0.9, 0.6)`. (about 6)

**The pocket ending** (the 4 in the variant byte): before step 20 he holds one cap up, looks left, looks right,
and puts it in his pocket with a pat (`act(actPocket, 2.6)`); then one toss, not two. Same beat
length.

**Totals.** Script about 190 s. PROPOSED beat seconds **196** for every set and ending.

### 5.8 If it rains after all (the no-show)

The plan cannot know a random rain (no reading from the house). The begin hook then sets
`DAY_NOSHOW` as the other daytime majors do and plays `dayNoShowScript`: a look at the sky, a hand
out, a shrug, he sits. The patch is not picked and waits for another loop. The beat keeps its 196 s,
so he sits out a long quiet rain; this needs both a ripe patch and a lost reading, so it is rare.

## 6. The sky: options, costs, the recommendation

### 6.1 The options compared

| Option | What it gives | Cost on the panel (from the measured figures) | Verdict |
|---|---|---|---|
| A. Palette cycling of the sky slots | colour only: the five straight bands change colour | each step repaints the whole cache: 110 ms at once (a visible hitch), or spread one band a frame: about 16 ms more a frame for 15 frames. 40 steps over 100 s is 4.4 s of cache work, and 40 palette rebuilds where the tests cap them at 1 to 27. It also moves SKY4, which the fog bands and the star colours borrow | reads as a colour filter, not a living sky; needs the "ask first" palette steps. Kept as a comparison only |
| B. Overlays drawn every tick in the sky strips | shapes and motion | 9 or 10 more full-width strips a frame at about 5 ms each: 45 to 50 ms more, 10 to 11 fps. They would also paint over the ridges and pines unless those are drawn again | rejected |
| C. The rolling repaint (recommended) | the sky's own bands wave and colour, stars wheel, the sun breathes, all behind the ridges and pines as the cache has them | one 32-row band of the cache repainted a tick (about 5 to 8 ms, UNVERIFIED) and its strip composited full width (about 5 ms): about 10 to 13 ms more a frame. No palette change | build this |
| D. Only the free layers and the visions | the stream, the grass, the animals | nothing | not "the sky goes psychodelic"; it is part of C anyway |

### 6.2 The recommended design: the sky lets go

**The picture.** At rest it is exactly the sky as it is. Then its own four band edges start to
wave, slowly, like the edge of a tide, each with its own wavelength and its own direction, and the
five bands take soft sherbet colours that climb the sky one band every 16 s. The ridges, the pines
and the camp stay the dark natural silhouettes they are, so it looks like the flat paper sky came
alive behind a still world. Nine day stars come out and each turns a small slow circle with a short
tail. The sun grows a soft double halo that breathes. Under a cloud deck the deck takes the top
bands' colours.

**The edges.** Edge j (0..3) at rest y 58, 118, 178, 228. With the wave weight kw:

`y_j(x) = rest_j + A_j * kw * (sinT(x / L_j + d_j * v_j * t / L_j + o_j) + 0.35 * sinT(x / (0.43 * L_j) - 1.3 * d_j * v_j * t / L_j + 2 * o_j)) / 1.35`

| j | A (px) | L (px) | v (px/s) | d | o (turns) |
|---|---|---|---|---|---|
| 0 | 12 | 340 | 2.4 | +1 | 0.00 |
| 1 | 14 | 270 | 3.0 | -1 | 0.31 |
| 2 | 13 | 310 | 2.0 | +1 | 0.57 |
| 3 | 9 | 240 | 2.6 | -1 | 0.83 |

All PROPOSED. Drawn painter's style, as the ridges are: band 0 as a rectangle down to row 72, then
for each edge j a closed polygon whose top is the edge sampled every 16 px (52 points, x -16..816)
and whose flat bottom is 16 px under the next edge's rest (318 for the last), in band j + 1's
colour. Opaque fills, no blending, at most two layers over any pixel.

**The colours.** Five pastels, day values (PROPOSED): lilac 0xB9A6E8, rose 0xF2A7C3, peach
0xFFC9A0, butter 0xFFE9A8, mint 0xBFEBD0. Band b's colour is
`mixRGB(pal[SKY0 + b], lit(P[(b + n) % 5] .. P[(b + n + 1) % 5] by frac), 0.8 * kc)` with
`n + frac = t / 16`. `lit(hex)` scales the pastel by how bright the palette's SKY2 is now against the
day key's SKY2 (the method of `critterLitAs`, `critters.cpp:1837`, copied into skytrip.cpp because
that one is inside `#if SS_XMAS`), capped at 1. So the pastels dim with the mood, the overcast and
the night rule like the sky they sit on, and can never be brighter than the day's own sky allows.

**The ramps** (functions of the sky clock t only, e = easeInOut):
- kw: e(t / 14) up to 14; 1; from 90 down, 0 at 102.
- kc: 0.35 e(t / 14) up to 14; to 1 by 30; 1 until 86; down to 0 at 100.
- star i (0..8): in at 16 + 2.0 i over 1.5 s; out at 84 + 1.3 i over 1.5 s. Its circle: radius 5, 8
  or 11 px (i mod 3), period 14 + 2 (i mod 4) s but at least 20 s for the 11 px ones (so no star
  moves faster than 3.5 px/s). Drawn as three dots: itself (its own r + 0.8) and two behind it on
  the circle at -0.06 and -0.12 turns, 0.7 and 0.45 of its size, in STAR mixed 90 % over the band
  colour. A star within 12 px of the sun's halo is left out.
- the sun's halo: two discs behind the sun, radius r + 7 + 3 b and r + 16 + 5 b with
  b = 0.5 + 0.5 sinT(t / 7), in the band colour mixed 45 % and 22 % toward the sun's colour, weight
  kc. No sun drawn (down, or a deck of cover 4 and up): no halo.
- the deck (cover 4 and up): CLOUD_SHADE and CLOUD_LIT mixed 0.5 kc toward bands 0 and 1.
- the far ridge (PROPOSED on): drawn up to 2 px higher, `lift = 2 * kw * (0.5 + 0.5 sinT(t / 9))`.

**How it reaches the screen.** The cache holds the sky. Every tick of the trip (on the beat's
clock) the module takes the next band i of ten (0, 1, .. 9, 0, ..), writes the sky clock into
`Landscape::trip.tq[i]` (ticks, 16 bits; 0 means natural), and `dayTrack` calls
`repaintRows(32 i, 32 i + 32)` and `markDirtyRows` for the same rows. The static sky draws each band
of rows from that band's own latched clock: `drawSky` loops over the ten bands that meet its clip
and draws each with `clip` cut to the band. So:
- every pixel is a pure function of state: a full rebuild (a palette step, a test's full render)
  gives the same frame as the incremental one;
- neighbouring bands are one tick apart (0.04 s), so the join between them is under 0.15 px at
  these speeds; each band is redrawn every 0.4 s, so nothing may move more than about 1.4 px per
  redraw: that is why every speed above is 3.5 px/s or less;
- a band whose latched clock is 0 takes the old `fillRect` path, so before and after the trip the
  sky is bit for bit the old sky and switching on and off repaints nothing;
- the ridges, trees and pines in those rows are redrawn with the band, as in any cache repaint, so
  the sky stays behind them for free.

**No flashing.** Nothing switches: the fastest brightness change anywhere is the 12 s colour ramp
and the 16 s colour climb between pastels whose luminance lies within about 0.70 to 0.88 of each
other, far under one cycle in 3 seconds, let alone 3 a second. No strobing, no inverted colours, no
high-contrast patterns: the bands are wide (50 px and more) and the edges are single soft curves.
The test in 10.2 measures it.

**The night rule.** The major is planned by day only. If the night rule comes on while it runs,
the palette is recomputed (at once, as today) and every pastel dims with SKY2, so nothing is
brighter than the night sky it lies on; the stars and the halo use STAR and SUN, the two slots the
rule already lets be small highlights. If the mood moves on toward dusk under a long loop, the
pastels follow the palette the same way.

### 6.3 Frame cost of the recommendation (estimates; all UNVERIFIED on the panel)

- Per tick during the 104 s: one band of the cache (the old cost of a band is 110 / 15 = 7.3 ms on
  average; the sky bands hold less than the ground bands; the waves add one or two polygon fills and
  a handful of dots): 5 to 8 ms. Its strip, full width: about 5 ms. So 10 to 13 ms more a frame.
- What else is dirty meanwhile is little: he lies still (strips 10 and 11), the stream and grass as
  always, one vision at a time. Expect about 8 to 9 strips a frame in all against the usual 7 to 8,
  and 17 to 20 fps against 22 to 25. Because he lies still and everything drifts, a lower frame rate
  shows less here than anywhere else in the loop.
- If frames run past 40 ms the host runs two ticks before a render, which means two bands in that
  frame. With a settled frame of 42 ms and 11 ms a band that settles at about 58 ms (17 fps). The
  knob below bounds it.
- Primitives: the trip sky adds 5 fills and up to 9 dots to a band; the visions at most 24 (the
  full antlers with leaves and the bird) on top of a deer's 7.
- Palette rebuilds added: 0. Cache rebuilds added: 0. Row repaints: 2,600 bands of 32 rows.

### 6.4 The knobs and the fallback

- `kTripBandEvery` (PROPOSED 1): 2 repaints a band every second tick: half the cost, each band
  every 0.8 s; halve the speeds v and the stars' with it.
- **Fallback, the slow wash** (`kTripWashS`, PROPOSED 0 = off; 12 = on): the same picture, but the
  bands are only repainted during the first 10 ticks of every 12 s, top to bottom. The sky then
  holds a pose and every 12 s a new pose washes down it in 0.4 s. Average cost under 0.5 ms a
  frame. Same code, a different schedule. The stars then step rather than wheel, so in this mode
  they do not orbit (radius 0) and only come and go.
- If even the first band repaint shows a problem on the panel (a seam, a mismatch), option D alone
  still plays: the free layers and the visions, with the sky left alone (`kTripSky = false`).

### 6.5 The free layers (no new strips)

- **The stream's glints** (`ambientDrawGlints`): glint i's colour is
  `mixRGB(pal[WATER_LIGHT], lit(P[(i + n) % 5]), 0.85 * kg)`, n stepping every 3 s with a cross-fade;
  kg comes on one glint at a time from t = 14 and goes by t = 98. They are redrawn every tick anyway.
- **The grass** (`updateSway`): the tufts' own sway is blended by ks (on from the onset cue over
  8 s, off by t = 98) toward one shared slow wave, `2.4 * sinT(seconds / 5 + x / 800)` px, so the
  whole field leans together and a wave crosses it left to right every 5 s. Tufts are redrawn every
  second tick anyway.
- **His breathing** (5.4).

## 7. Interactions

### 7.1 Animals

- The chipmunk and the deer of set 0 are director critters tagged CT_MAJOR and CF_BRAVE, in
  `m.c[0]` and `m.c[1]`; a spawn that returns -1 skips that vision (the sky goes on). Both leave by
  an edge and `until(crittersGone)` holds the last walk until they have.
- The stream's heron or deer has left when the major's first still began (stream.h). The minors
  have quit. The mail bird stays on the tent's ridge (it is brave) and is left alone.
- The bee_visit minor is unchanged: it goes to the mother, in any loop.
- `directorAvoidHiker` scatters nothing while he lies still (`director.cpp:147`).

### 7.2 Weather

- Planned only in dry weather from the data; a random rain, snow or storm gives the no-show (5.8).
- Wind: the grass's own gusts are blended into the shared wave like any sway. Fog: its bands (SKY4)
  drift over the mid pines as always, in front of the sky. Heat: the shimmer shifts the cached
  far-ridge rows as always (it reads the strip, so it sees the trip's rows). Chill: no overlay runs
  during a script.
- Overcast (likely after rain): cover 4 and up puts the deck over the top of the sky; the waves and
  colours show below it and the deck takes the tint (6.2).

### 7.3 The doorbell, the passer-by, fire and ice, the visitors

- The doorbell freezes the story for 3.4 s (`react.h`): the script, the major's clock and so the
  sky clock stand still, and no band is repainted. The sky holds its pose, the clouds and the stream
  go on, he answers the bell as he does from a nap. It reads as the bell sobering the sky for a
  moment. `dayHoldsFacing` is true while he lies, so the reaction does not turn him.
- While the bunch or one mushroom is in his hands (steps 6 to 15 and 18 to 20) `tripHandsFull(w)`
  is true: add it beside `plinkHandsFullNow` in `react.cpp` (both places), so he does not wave with
  them and a passer-by's wave waits.
- Fire and ice and the visiting couple cannot start while a major runs. Nothing to do.
- The touch reveal freezes everything; nothing to do.

### 7.4 The status chip

Section 3.2. The chip's rows (strips 12 to 14) are dirty for 10 s after a bell; the trip's bands are
strips 0 to 9, so the two never add up in one strip.

### 7.5 The other four new pieces

- **Long-range shooting.** Never in a loop with the mushrooms: he does not handle a rifle the day
  he eats one. The trip loop has no other major but the fetch, so this holds by itself; if
  `kTripAlone` is turned off, `majorsPlan` zeroes the plinking's and the long-range major's weights
  in a ripe loop.
- **Reeds and cattails.** (1) They should lean with the grass in the shared wave: one call,
  `tripSway(w, x, sway)` (8.5), from wherever they compute their sway. (2) Ask of that plan: no reed
  clump on the mushroom's bank between Nx - 24 and the mother's x + 30, and none at AT_M0 +- 20 on
  the far bank, so he neither wades through reeds nor has them drawn over the patch. If they must
  stand there, the patch is drawn after them (it is the last layer). (3) If he has a cattail in his
  mouth when this major begins, their module drops it first (their hook, UNVERIFIED); he ends with
  an empty mouth and empty hands either way.
- **The deer herd.** The herd comes while he is away and the trip loop has no other major, so they
  never share a loop. The herd's grazing spots are on the camp side; the patch is across the water
  and is not touched. Offer to that plan: `patchSpot(ls)` (the patch's middle) if a fawn should
  sniff toward it across the stream. The vision deer of set 0 is the same rig; if the herd plan adds
  a fawn or an antlered buck look, set 0 uses the buck.
- **Morning Tai Chi.** A morning piece; never in the same loop. No shared state. The slow arm trace
  at t = 88 is deliberately one plain movement and borrows nothing from it.
- **Shared files** all five will want: `majors.h` / `majors.cpp` (ids and pool rows), `day.h` /
  `day.cpp` (the union), `landscape.h` / `landscape.cpp`, `ambient.cpp`, `memory.h`, `ss_app.h`.
  One builder at a time in each, or one integrator who takes every plan's edits to those files.
  Fix the id order before anyone builds (ids are stored by number).

## 8. Data added

### 8.1 Memory (`memory.h`, at the end of the struct; 3 bytes)

```
uint8_t mush_stage;   // 0 none; 1..6 growth steps (6 = MUSH_RIPE); 7 = MUSH_PICKED
uint8_t mush_left;    // MUSH_PICKED: loops the picked-over patch still stands (6 down to 0)
bool mush_soak;       // it has rained (from the data) since the patch last began: it may grow
```

The rule, in a new header `core/mush_patch.h` (plain C++, header only, shared with the host and the
tests, like `set_piece_day.h`):

- `mushAfterLoop(stage, left, soak, wet)`, called from `memoryEndLoop` for camp loops, `wet` being
  `w.weather.from_data && (kind == WX_RAIN || kind == WX_STORM)`:
  - wet: `soak = true`; a stage of 0 or MUSH_PICKED becomes 1 (new buttons; `left = 0`); else no
    change.
  - dry, stage 1..5 and soak: stage + 1.
  - dry, stage 6: stays (ripe until picked).
  - dry, MUSH_PICKED: `left - 1`; at 0 the stage is 0 and `soak = false`.
- `mushAfterMinutes(stage, left, soak, dry_minutes, raining)`: the same steps for the host while
  the screensaver is off, one step per 10 dry minutes (PROPOSED).
- Size step of mushroom i at stage S (0 not there, 1 button .. 4 full):
  `clamp(S - first[i], 0, 4)` with `first = {0, 1, 0, 1, 2, 2}`, and at MUSH_PICKED: 4 for i 0, 1, 5
  and 0 for the pickables; in the last two loops of `left` the three that remain droop one step.
  Scale of a step: 0.25, 0.5, 0.75, 1.0 of the mushroom's full size.
- `memoryFormat` prints ` mush=S` only when S is not 0, so every pinned memory string stays as it is.

### 8.2 Landscape (`landscape.h`; about 62 bytes)

```
struct LsShroom { int16_t x, y; uint8_t step; uint8_t lifted; };   // 6 bytes; lifted: the module draws it
struct LsPatch { uint8_t n; uint8_t pad; int16_t nx, ny; LsShroom m[MUSH_PATCH]; };   // 42 bytes
struct LsTrip { uint16_t tq[SKYTRIP_BANDS]; };   // 20 bytes; the sky clock each band was last painted at, 0 natural
```

`Landscape::patch` and `Landscape::trip`. Both are zero after `worldBeginLoop`'s clear;
`patchLayout(w.land, w.memory)` fills the patch right after `landscapeGenerate` and before
`ambientInit` (so `pickTufts` can keep the swaying tufts 12 px away from every patch mushroom).

### 8.3 Inputs and the host (`ss_app.h`; 1 byte)

```
uint8_t mush;   // the patch as the host remembers it: bits 0-2 stage, bits 3-5 left, bit 6 soak. Zeroed: none, as before.
uint8_t appMushroom(const App* app);   // the same byte from the memory, for the host to keep
```

`appStart` copies `in.mush` into the new memory before the first `beginLoop`. The host
(`firmware/tcomm/src/ui/ss_host.cpp`) keeps one byte `g_mush` in RAM: read from `appMushroom` when a
run ends and after each new loop, advanced by `mushAfterMinutes` from its minute tick while no run
is on, given to `screensaver_start`. Lost at a restart of the panel (fine: mushrooms after the
next rain). Serial: `ss mush N` to set it for testing. The preview gets `--mush N`.

Without the host part everything still works inside one run; the patch then only ripens when the
screensaver is left alone for an hour after rain. Build the core first, the host part as its own
small step.

### 8.4 The day union (`day.h`; nothing added to the World)

```
#define TRIP_PROPS 8
struct TripProp {            // 28 bytes
  uint8_t kind;              // TP_NONE, TP_CAP (a picked mushroom), TP_FISH, TP_RING, TP_SPARK, TP_LEAF, TP_BIRD, TP_EYES
  uint8_t mode, tint, pad;
  Vec2 pos, vel;
  float t, rot, k;
};
struct TripState {           // about 300 bytes
  uint8_t set, pocket, sky_on, lifted;      // the variant; the sky is running; the pickables are the module's
  uint8_t held, bite, bunch_n, bunch_down;  // what is in which hand (TH_NONE, TH_ONE, TH_BUNCH); bites taken; caps left
  uint8_t band, repaint, dog, vis;          // next band; 1 + the band to repaint this tick; 1 + the lent dog's slot; the vision's stage
  float sky_t;                              // the sky clock, s (the beat's time since cueSky)
  float ks, kg;                             // the grass's and the glints' weights
  float antler, ear;                        // 0..1
  float vt;                                 // seconds in the vision's stage
  Vec2 bunch_at, antler_at;
  LsShroom pick[3];                         // the lifted pickables
  TripProp prop[TRIP_PROPS];
};
struct DayTrip { TripState t; Rng rng; };   // rng: the fish's and the leaves' small spreads, seeded from the layout
static_assert(sizeof(DayTrip) <= sizeof(DayBike), "the trip must fit in the bike's place");
```

`DayKind` gets `DAY_TRIP`. `DAY_PARTS` goes from 2 to 3 (and `WORLD_ACTOR_MAX` up by 1, it is
exactly the worst case today): part 0 the ground things behind him (lifted pickables, the bunch on
the ground, floating caps), sorted at their own base y; part 1 what is in his hands, sorted at
`held_y`; part 2 the vision's own drawing (antlers at the deer's feet y + 1, fish, rings, sparks,
the cloud's eyes), sorted at that vision's y.

### 8.5 What `sizeof(App)` does

Memory + 3, Landscape + about 62, Inputs + 1 (twice: App keeps a copy), padding: about 80 bytes.
From 29,856 to about 29,940 of 32,768. Measure and report.

## 9. Gating, family, slots, weight, loop fit

- **Id** `MJ_MUSHROOM`, added before `MJ_COUNT` (26 of 32; the order among the five new pieces is
  the integrator's). Name `mushroom` (no other major's name contains it).
- **Pool row**: `{"mushroom", {196, 196}, 0.f, 2 | 4, kMid, tripMajorBegin, tripMajorTick,
  {nullptr, nullptr}, {0, 0}}`. Weight 0: it is never drawn by chance, so `rng.weighted` skips it
  and every existing plan and pin is unchanged. Slots after lunch or after the wade, never straight
  after his return.
- **Forced when ripe**, in `majorsPlan`, the way the fetch is forced for a dog:
  `ripe = mem.mush_stage == MUSH_RIPE && slots.family == SF_MIDDAY && !night && wk is not rain, snow or storm`.
  The draws are taken as always; at `k == (house_dog ? 1 : 0)` the pick becomes `MJ_MUSHROOM` when
  ripe (and the `sum <= 0` skip lets it through as it does the fetch). `want` becomes
  `house_dog ? 2 : 1` in a ripe loop (PROPOSED `kTripAlone`). Variant byte:
  `tripPlanVar(r_var) = (r_var >> 13) % 3 | (((r_var >> 5) % 4 == 0) ? 4 : 0)`; the value 8 is `DAY_NOSHOW`.
- **Kept by the fit**: `majorKept` returns true for `MJ_MUSHROOM`.
- **Loop length**: 286 to 325 s of spine, plus 196, plus stills of 8 to 16: 490 to 537 s before
  fillers; with the fetch (about 50 + stills) up to about 600. The fit keeps what fillers fit under
  650. Always inside 480 to 660. The loop never needs its pauses squeezed to fit this major alone.
- **Families**: the patch shows in evening, morning and midday (it is landscape). The major is
  midday only (the day union's plays are; the evening's pool is not touched).
- **Night and dusk**: never planned (`!night`, and midday is mood 2.5 to 3.5).
- **The ring and the recent sets**: pushed like any major; they only scale weights, and its weight
  is 0, so they do not matter to it.
- **Timeline**: one more beat; midday uses far fewer than 64.

## 10. File plan

### 10.1 New files

| File | What |
|---|---|
| `core/mush_patch.h` | the growth rule and the size table (8.1); header only |
| `core/scene/patch.h`, `patch.cpp` | `patchLayout`, `patchDrawStatic(ls, s, pal, clip, p)` (the layer's draw, skipping `lifted` ones), `patchShroomDraw` and `patchShroomBounds` (one mushroom at a size, tilt and place: the module uses them for picked ones), `patchRows(ls, y0, y1)`, `patchSpot(ls)` |
| `core/scene/skytrip.h`, `skytrip.cpp` | `LsTrip`, the ramps, `skyTripOn(trip)`, `skyTripBand(...)` (one band's wavy fill, stars, halo, from that band's clock), `skyTripDeckPal`, `skyTripRidgeLift`, the pastel table and `lit` |
| `core/scene/trip.h`, `trip.cpp` | `TripState` and its props: step, bounds, dirty list, draws for the three parts; `tripGlintColor`, `tripSway`; no World knowledge beyond what `plink.h` has |
| `core/scene/day_trip.h`, `day_trip.cpp` | the major: `tripPlanVar`, `tripSeconds`, `tripScript`, `tripMajorBegin`, `tripMajorTick` (the sky schedule, the visions' stage machines, the dog), `tripMajorEnd`, `tripHandsFull`; modelled on `day_plink.cpp` |
| `tests/test_mush_patch.cpp`, `tests/test_skytrip.cpp`, `tests/test_day_trip.cpp` | section 11 |
| `tools/trip_stills.cpp`, `tools/trip_sheet.py` | section 12, modelled on `plink_stills.cpp` / `plink_sheet.py` |

### 10.2 Existing files and the exact places

| File | Function | Edit |
|---|---|---|
| `ss_app.h` | `Inputs`; the introspection block | `mush` at the end of the struct; `appMushroom` |
| `scene/app.cpp` | `appStart`; new `appMushroom` | seed the memory from `in.mush` before `beginLoop` |
| `scene/memory.h`, `memory.cpp` | `Memory`; `memoryEndLoop`; `memoryFormat` | 8.1 |
| `scene/landscape.h` | `Landscape` | `LsPatch patch; LsTrip trip;` |
| `scene/landscape.cpp` | `drawSky` | when `skyTripOn`: loop the ten bands that meet `clip`; a band with clock 0 draws as now, else `skyTripBand`; the deck with `skyTripDeckPal` |
| | `drawRidge`, the far ridge's call in `drawLayer` | a `lift` argument (0 by default); the far ridge drawn band by band with `skyTripRidgeLift` when on |
| | `drawLayer`, `case BL_MUSHROOM` | `patchDrawStatic(ls, s, pal, clip, buildMushroomT(b))` after the mother |
| `scene/world.cpp` | `worldBeginLoop` | `patchLayout` after `landscapeGenerate`, before `ambientInit` |
| `scene/world.h` | `WORLD_ACTOR_MAX` | + 1 |
| `scene/ambient.cpp` | `pickTufts`; `updateSway`; `ambientDrawGlints` | keep 12 px from patch mushrooms; `tripSway`; `tripGlintColor` |
| `scene/day.h`, `day.cpp` | `DayKind`, the union, `DAY_PARTS`; `sizeOf`, `dayBounds`, `dayHoldsFacing`, `dayStep`, `dayTrack`, `dayPartY`, `dayPartDraw` | a `DAY_TRIP` case in each. `dayTrack`: the band repaint (`repaintRows` + `markDirtyRows`); on a change of kind away from `DAY_TRIP` with the sky on, rows 0..320 repainted and marked; with pickables lifted and not picked, `patchRows` repainted and marked |
| `scene/majors.h` | `MajorId` | `MJ_MUSHROOM` |
| `scene/majors.cpp` | `kMajors` and its `static_assert`; `scriptOf`; `majorSeconds`; `majorKept`; `majorEnd`; `majorsPlan` | section 9; `tripMajorEnd` in the chain in `majorEnd` |
| `scene/housedogs.h`, `housedogs.cpp` | `houseDogsLend` | a second argument `int first = HD_COPPER` (the visitors' call is unchanged) |
| `scene/react.cpp` | `reactWaveOk` and the hands-full test in `reactStart` | `tripHandsFull` beside `plinkHandsFullNow` |
| `sim/preview_common.cpp` | options | `--mush N` |
| `tests/test_spines.cpp`, `tests/test_midday.cpp` | the `isMajor` lists | add `mushroom` |
| `tools/review_clips.py` | a cut of its own like `major_rain_shelter` | `--spine midday --mood 3 --weather clear --mush 6` |
| `firmware/tcomm/src/ui/ss_host.cpp`, `tcomm.ino` | 8.3 | the carry byte and `ss mush N`; a separate, later step |

Not touched: `palette.h` / `palette.cpp` (no slot, no key), `render/` (the renderer already has
`repaintRows`), `stream.*`, `spine*.cpp`, `minors.*`, `weather.*`, `critters.*`, `director.*`.

### 10.3 Build order (each stage ships by itself)

1. **The patch**: `mush_patch.h`, `patch.*`, memory, landscape's mushroom layer, `--mush`. Passive
   mushrooms that grow after rain, in every family. No major yet.
2. **The sky by itself**: `skytrip.*` and `drawSky`, driven by a test hook and the stills tool.
   Measure it on the panel before going on (`ss stats` during a forced run).
3. **The major with set 0** and the dog.
4. **Sets 1 and 2**, the pocket ending.
5. **The host's carry byte.**

## 11. Tests

### 11.1 `tests/test_mush_patch.cpp`

- The rule: wet, wet, dry x 6 gives 1, 1, 2, 3, 4, 5, 6, 6; picked counts 6 down to none; rain on a
  picked patch starts a new one; `mushAfterMinutes` agrees with `mushAfterLoop` at 10 minutes a step.
- Layout over seeds 1..200 and stages 1..7: every mushroom on dry ground (9 px from the water),
  y <= 474, x <= 616; pickables at x <= 530; Nx in 420..501; no two closer than 6 px.
- Draw: each mushroom at each size inside its bounds (the `propsOutside` method, `test_scene.cpp:764`).
- Stage 0 and `Inputs::mush` 0: frames and `--loops 10` lines for seeds 1 and 7 in every family
  byte for byte what they were (compare against a build without the files, as `build-plink-off`).
- A run with data rain for 2 loops then clear for 7 midday loops: `mush=` climbs in the memory
  line, the eighth loop's plan has `majors=mushroom` alone, the ninth has `mush=7`.

### 11.2 `tests/test_skytrip.cpp`

- Every band clock 0: the cache is byte for byte the old sky (moods 1, 3, 4.4; covers 0, 3, 6).
- Purity: for clocks set to a ramp of values per band, drawing the ten bands one by one into a
  frame equals one full `landscapeDrawStatic`.
- Edges stay inside rest +- A, and the lowest stays above row 240.
- No flashing: step the sky clock through 0..104 s a tick at a time with every band current; the
  mean luminance of the sky and of each band changes by less than 0.4 % per tick and reverses
  direction fewer than 3 times in any second.
- Night rule: with `night_dim` 1 no trip colour exceeds `NIGHT_LUM_MAX`, the stars and halo stay
  under `NIGHT_HIGHLIGHT_MAX`.
- Speeds: no star or edge moves more than 1.5 px between two repaints of its band.

### 11.3 `tests/test_day_trip.cpp` (like `tests/test_day_plink.cpp`)

- With `mush` ripe, midday, mood 3, clear from data: `mushroom` is planned in loop 0 for seeds
  1..40, alone (with `fetch` and `--dog 1`: both); the loop is 480 to 660 s; never at night, in the
  evening or morning, or with data rain.
- Each set and the pocket ending plays through over those seeds: no overrun, he ends sitting, hat
  on, hands empty; `appCritterCount` 0; the day union zero; `Landscape::trip` all zero; the sky's
  rows equal a full redraw after the beat; memory `mush=7`.
- Incremental equals full render at 12 checkpoints inside the beat (three of them inside the sky).
- The chip box: his bounds, the module's bounds and every critter's stay out of x 538..796,
  y 412..476 for the whole beat, seeds 1..40.
- Doorbell at five ticks (wading, sniffing, the hand, mid-sky, the toss): the sky clock and the band
  counter do not move while the reaction runs; frames stay equal to a full redraw; the beat ends
  clean.
- Strips: the beat's average under 10 (the limit for majors); report the sky's 104 s by itself.
- Determinism: two runs, same frames, at 3 ticks inside it; `--loops 10` unchanged after it.
- The dog: with `--dog 1` Lucy is lying within 60 px of him from t = 30 to t = 98 and is not lent at
  the beat's end; with the dog called in at t = 50 she leaves and the beat ends clean.
- The no-show: random rain with a ripe patch leaves `mush=6`.

### 11.4 Existing tests to run and watch

`test_scene`, `test_spines`, `test_midday`, `test_weather` (it may switch weather kinds between
loops of one run with data: if so a patch grows there; check its strip and rebuild limits still
hold, and do not edit it before knowing why a line moved), `test_house_dogs`, `test_visitors` (the
lend's new argument), `test_react`, `test_spread`, `test_sky`, `test_stream`, `test_memory_sets`.
The pins in `test_spines.cpp:77-100` and `test_midday.cpp:79-131` must not change.

## 12. Stills to render for review (`docs/screensaver/reviews/stage22-mushroom/`)

1. The patch at stages 1, 3, 6 and 7, mother at x 475, 540 and 625, by day; stage 6 at dusk, at
   night and in rain. 2x crops of the patch beside whole frames. With `--chip --dog 3` for the
   mother at 625.
2. The build-up pop: 6 frames.
3. The sky alone, whole frames at t = 0, 8, 14, 22, 30, 50, 70, 86, 94, 102, clear sky; the same at
   t = 50 under cover 3 and cover 6, at mood 2.6 and 3.4, with the real sun high and with it low.
4. A strip of 10 frames 0.4 s apart at t = 50 (does the drift read as smooth at one band a tick),
   and the same for the slow wash.
5. The story: one frame per numbered step of 5.1 to 5.3 and 5.7 (22 frames), seed with the mother
   left and one with it right.
6. Each vision at its four key moments (arrive, peak, the still hold, leave): 24 frames.
7. Lucy beside him; Copper beside him; the doorbell mid-sky (3 frames).
8. One contact sheet per set (`trip_sheet.py`, the method of HOW_TO_ADD_SCENES section 9).

## 13. Checks to run and report with the build

- All tests; zero overruns; `sizeof(App)`; strips a frame (beat, sky part); PC ms a frame;
  palette rebuilds (must be unchanged); primitives at the antlers' peak.
- Build with the files out (or stage 0 forced) against the tree before: frames and plan lines equal.
- On the panel (Mike flashes; `bash tools/fw.sh` only from bash): `ss mush 6`, a midday scene, then
  `ss stats` through the sky: fps, strips, the cache time per frame, worst frame; free flash and
  PSRAM against the 745 KB and 646 KB measured on 2026-11-18. Try `kTripBandEvery` 1 and 2 and the
  slow wash, and let Mike choose by eye.

## 14. Cost estimate

| Item | Estimate | Basis |
|---|---|---|
| Passive patch | 0 strips, 0 per frame; about 20 primitives once per cache build | static layer |
| The story parts (5.1 to 5.3, 5.7) | as any major: 7 to 9 strips | he walks, wades, crouches |
| The sky (104 s) | + 1 full-width strip and 1 cache band a tick: + 10 to 13 ms a frame, 17 to 20 fps; UNVERIFIED | 6.3 |
| The slow wash fallback | under 0.5 ms a frame on average | 6.4 |
| Primitives | sky band + 5 fills and up to 9 dots; visions up to 24 | 6.3 |
| Code | 35 to 45 KB of flash, the same again in PSRAM while the screensaver runs (trip.cpp and day_trip.cpp are plinking sized, skytrip and patch are small) | plinking was about 30 KB |
| `sizeof(App)` | + about 80 bytes, to about 29,940 of 32,768 | 8.5 |
| Palette rebuilds | 0 added | no palette change |
| Major ids | 26 of 32 | |

## 15. Risks

1. **Frame rate during the sky on the panel.** The estimate rests on a band of sky costing less
   than an average band; if the ridges and pines in rows 192..319 make those bands dear, the frame
   rate dips most there. Stage 2 of the build order measures it before the story is written. The
   knob and the slow wash are the answers.
2. **Purity.** The sky must draw each band from its own latched clock everywhere it is drawn (the
   build-up path, a spread palette step, `repaintRows` from the real sun moving in the same frame).
   A miss shows as a mismatch between incremental and full renders; the tests in 11.2 and 11.3 are
   aimed at it. `repaintRows` keeps two patches and joins a third to the second, which is correct
   but can make one frame dear when the sun moves in the same tick.
3. **Stepping.** At one repaint per band every 0.4 s the drift may read as steps, not flow. The
   speeds are set for about 1 px a step; the strip of stills (12, item 4) and the panel decide.
4. **The wade and the thin bank.** With the mother at the left of its range he crouches on a strip
   of bank about 20 px deep, his feet 6 to 10 px from the frame's bottom. Check seeds with mush_x
   470..490 and a low stream (the +5 jitter).
5. **The look of pastels on a real rainy-day palette.** Overcast grey under 80 % pastel may read
   chalky. The mix weight and the table are one place each.
6. **Taste.** It is Mike's own idea, and the piece is kept gentle: a shrug, a nap in the grass, a
   friendly chipmunk, and he throws the rest away. The stills go to him before the major is wired
   into the plan.
7. **Shared files with four other new pieces** (7.5): ids, the day union, landscape, ambient.
8. **`test_weather` and any test that feeds rain then clear** may now see buttons in the cache or a
   forced major. Understand each moved line before touching a test.
9. **Lying pose and the bell.** How the doorbell reaction looks on a lying hiker is unverified; it
   is what the midday nap already does.
10. **The host's byte** is lost at a panel restart and is advanced by the forecast's words, not by
    a rain gauge: mushrooms follow the forecast's rain.
