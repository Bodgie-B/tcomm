# Morning Tai Chi: design and build plan

Written 2026-11-18 from Mike's requests of 2026-11-17 and 2026-11-18. Design only: nothing here is
built. Every number marked PROPOSED is a starting value to tune with the stills. Everything about how
it looks is UNVERIFIED until Mike has seen it, and on the panel. References like `majors.cpp:3915` are
lines on 2026-11-18 in `screensaver/core/scene/`; they drift, search for the name.

What I read to write this: HOW_TO_ADD_SCENES.md (all), PLINK_SUN_PLAN.md (sections 0 to 4), hiker.h and
hiker.cpp (all), day.h and day.cpp (all), majors.h, majors.cpp 3655 to 4054 (the pool, the beat, the
plan), memory.h, world.h 60 to 354, world.cpp stepHiker, spine.h 60 to 256, spine.cpp spineLayout and
spineFitLength, spine_morning.cpp (header, durations, plan, layout, timeline, tick, the bird and
chipmunk helpers), morning_stay.cpp (header, plan), stream.h, stream.cpp 659 to 760 and 900 to 1320,
housedogs.h, housedogs.cpp 80 to 100 and 740 to 858, react.h, react.cpp 20 to 100, critters.h and the
kind table, landscape.cpp generateStream, tierGeom, drawPine. What I did not read is marked UNVERIFIED
where it matters.

---

## 1. Mike's words

- 2026-11-17: "Morning Tai Chi."
- 2026-11-18: "I would like the Tai Chi to be a slow progression of many exercises and not be in a
  hurry to finish."
- For all five new pieces: "Consider other interactions with animals and these from other vignettes."
- Standing taste: natural, classic, unhurried. "When something feels rushed, add stillness, not more
  motion." Nuance over stiffness. No ambient sound.

What that means for this piece: many different forms, each one slow, real stillness between them, and
no attempt to squeeze the whole form into one loop. The form is split into four chapters. Each time he
practises he does the next chapter (Memory keeps the place), so over a morning, or over several
mornings, the whole sequence goes by. Nothing is cut short to fit.

---

## 2. Proposed answers to the open choices (ask Mike at review, build these meanwhile)

| Choice | Proposed default | Alternative (one constant or flag) |
|---|---|---|
| How long | one chapter a session: 106 to 115 s of form inside a beat of 143 to 153 s. Four chapters, 7.3 minutes of form in all | `kTaiPace` 1.15 stretches every move and hold (beats 159 to 170 s) |
| How it fits the loop | a morning major that counts as two majors in the plan, is never dropped by the length fit, and may be the loop's only major | weight only, no special rules (it would be dropped often) |
| When | first thing: the slot after the wake, in the fog, before coffee | also the slot after coffee (slot bits 1 and 2) |
| How often | always in the first morning loop after a non-morning loop (the day's first morning), then by weight 1.6 with the usual ring rule (about one morning loop in four) | weight only (`kTaiFirstMorning = false`) |
| Order of chapters | in order, 1 2 3 4 1 ..., kept in Memory; a fresh Memory starts at a chapter taken from the plan's variant draw | always start at chapter 1 |
| Where | the camp-side bank just east of the cairn, 9 px above the water's edge, facing upstream (right) | facing left (`kTaiFace = -1`): the same tables mirrored |
| Hat and pack | hat off, laid on the grass behind the line; no pack (he has none on in the morning); boots stay on (the rig has no bare feet) | hat stays on (`kTaiHatOff = false`): then the turns flip the brim in one frame |
| How he is posed | a new pure module drives the whole pose every tick: feet planted by leg IK, hands on paths by arm IK, no new PoseIds | none considered workable: lerped joint angles slide the feet |
| Rig change | one float, `Hiker::foot_turn`, so the feet foreshorten through a turn | no rig change: the turn flips on a 2 px dip, as the doorbell turn does |
| Companions | one calm guest a chapter: the heron (ch 1), a bird on his hand (ch 2), a chipmunk that copies (ch 3), a deer at the tree line (ch 4); a butterfly in the warm months | `kTaiGuests = false`: none |
| House dogs | no wild guests while a dog is in camp; Lucy lies near him and watches; Copper, when out, may bump him once | dogs settle by the tent as in every other major |
| Humour | 60 % none, 15 % a wobble on the balance form, 15 % an itch, and Copper's bump when Copper is out | `kTaiGags = false` |
| Doorbell | he holds the form, only his head lifts (the look-only reaction) | the normal hop and wave |

---

## 3. Where everything is (frame pixels, 800 x 480)

From the loop's layout. Examples use F = (250, 398) and camp_x = 560; F.x is 238 to 262, F.y 393 to
403, camp_x 550 to 570 (`landscape.cpp:605`).

- **The stream** (`generateStream`, `landscape.cpp:63`): control points (640, 386), (470, 420),
  (300, 448) with 5 px jitter, half width about 25 px here. The camp is on the upper bank. The water's
  upper edge is `bankY(x) = cy - half` from `landscapeStreamSpan`: about 398 at x 450, 392 at x 485,
  386 at x 520.
- **The cairn** (`spineLayout`): `camp.cairn = (F.x + 150, bankY - 6)`, about (400, 400).
- **The camp pine**: trunk at camp_x, base y 340. Its lowest tier is a triangle with its base at y 318,
  half width about 56 px there (`tierGeom`: 0.5 x width_pct x 170), apex about y 256. At his shoulder
  height (y about 310) the foliage starts at camp_x - 49; at his raised hand (y about 285) at
  camp_x - 26.
- **The food bag**: hangs at x = camp_x - 82 from y 218 to about 240. His highest hand is at y 280 or
  lower: 40 px clear.
- **The practice line**: `x0 = camp.cairn.x + 18` (about 418; 406 to 430). He works along
  `x0 - 8 .. x0 + 66` px (feet), hips never east of `x0 + 0.56 H` = x0 + 50. His forward hand reaches
  27 px past his hips: at most x0 + 77, which is F.x + 245. The pine's foliage at that height starts at
  camp_x - 49, so the hand is clear whenever camp_x - F.x >= 294 (the range is 288 to 332). A safety
  scale `g = min(1, (camp_x - 73 - x0) / 50.4)` (0.93 to 1) multiplies every along-the-line distance,
  so the worst seeds shorten his steps by 7 % instead of putting a hand on the foliage.
- **The ground line**: `y0 = min(bankY(x0 - 8), bankY(x0 + 66)) - 9`, clamped to 366..392 (about 381).
  `h.feet.y = y0` for the whole form (a flat line; the bank falls 12 px along it, so his feet are 9 px
  from the water at the east end and about 20 px at the west end).
- **His extent**: feet y 381, hat-less head top about y 293, raised hand about y 281. He covers rows
  281..384: strips 8 (its last rows, only with a hand overhead), 9, 10 and 11.
- **The hat's place** (AT_M1): `(x0 - 30, y0 - 16)`: up the bank, behind and left of the cairn line,
  where no step of the form goes. He stands at AT_M2 = `(x0 - 16, y0 - 16)` to put it down and pick it
  up.
- **The line's start** (AT_M0): `(x0, y0)`.
- **The heron's spot** (ch 1): the stream's own rule (`visitPlan`, `stream.cpp:1183`):
  x = clamp(x0 + 30, 400, 480), y = cy + 0.68 x half / 1.06 (the near side's shallows), about (448, 440).
  It is about 60 px below and ahead of him. Left of the chip box (x 538).
- **The chipmunk's spot** (ch 3): `(x0 + 78, y0 + 5)`: on the bank ahead of him, nearer the viewer, so
  it sorts in front and never behind his legs. Its 19 px sit-up reaches y0 - 14.
- **The deer's spot** (ch 4): `(clamp(camp_x + 74, 620, 650), 338)`: on the grass beyond the pine,
  about 200 px from him. A 48 px deer there covers y 290..338: strips 9 and 10, which he dirties anyway.
- **Lucy's watching spot**: `(x0 - 52, y0 - 4)`, left of the cairn, facing him.
- **Chip box** x 538..796, y 412..476 and the quiet corner: nothing of this piece goes there. The heron
  flies in high (`heronPose`: "over the chip's corner it is still well up") and lands at x <= 480. The
  deer and the chipmunk stay at y <= 386.

---

## 4. How he moves: the tai chi module

### 4.1 Why a module and not poses

The rig (`hiker.h`, `hiker.cpp`) is forward kinematics: thigh, knee, shoulder and elbow angles from the
hips, with `dx` shifting the hips off `feet.x`. It grounds itself on the lowest contact every frame
(`build`, `hiker.cpp:406`). It has no foot placement: a pose blend lerps joint angles (`mix`), so when
the hips move the feet slide. At walking speed nobody sees that. At tai chi speed a sliding foot is the
first thing you see. PoseIds and `pose()` steps also ease in and out of every pose (`blendCurve`), which
is exactly the stop-and-go look to avoid.

So the form is driven by a pure module, `taichi.h/.cpp`, called from `dayPose` (right after
`hikerTick`, `world.cpp:222`), the way `bikeApplyRider` and `ropesPose` already take over his pose. It
writes the whole `HikerPose`, `h.feet.x` and `h.facing` every tick from one clock. No new PoseIds, no
change to `kPoses`, no blend through `hikerSetPose` (so `directorAvoidHiker` never sees him "stirring").

### 4.2 What a key holds

A chapter is a table of keys (15 to 21 a chapter). A key is where the body is at one instant:

| field | meaning | units in the table |
|---|---|---|
| `a`, `b` | where foot A and foot B stand along the line, east of x0 (rig leg 0 and 1) | 0.01 H |
| `la`, `lb` | how high each foot is held (0 planted) | 0.004 H |
| `hip` | where the hips are along the line | 0.01 H |
| `sink` | hips lowered from standing height | 0.005 H |
| `lean`, `head` | torso lean and head tilt (rig convention, + forward and down) | 0.01 rad |
| `hands` | a hand posture id (section 4.4); bit 7 swaps the two hands | id |
| `face` | +1 east, -1 west | |
| `move` | seconds to arrive here from the key before | 0.1 s |
| `hold` | seconds held here | 0.1 s |
| `cue` | an event raised on arrival or at the hold's end | bits |

Feet are in line coordinates, not relative to the hips, so a planted foot has the same number in every
key it stays down for. 16 bytes a key, about 75 keys: 1.2 KB of const data.

### 4.3 From keys to a pose, every tick

1. **Time**: `t` is the module's own clock, advanced by `taichiStep(state, dt)` from the major's tick
   (so the doorbell freezes it for free). The current segment runs from key k-1's hold end to key k's
   arrival; `u` is 0..1 along it.
2. **Layered timing** (the main nuance): the waist leads and the hands follow. Four channels read the
   same segment at different times: hips and legs at `t`, the lean at `t - 0.10 s`, the hands at
   `t - 0.25 s`, the head at `t - 0.40 s`. Every hold is at least 0.3 s, and where it is shorter than
   the lag the hands simply arrive as the hips are already leaving: that overlap is what makes it flow.
3. **Interpolation**: Catmull-Rom through the key values (hip, sink, lean, head, hand positions), with
   the tangent zeroed at a key whose hold is 1.0 s or more. Short holds (0 to 0.6 s) keep their speed
   through the key: the movement passes through and does not stop. Segment time is eased with
   `easeInOut` only next to the long holds.
4. **Steps**: when a foot's place differs between two keys it travels during the middle of the segment
   only (`u` 0.25 to 0.85), after the hips have gone over the other foot, on an arc of lift
   `0.035 H x sin^2` added to the keyed lifts. A foot whose place is the same in both keys does not
   move at all.
5. **Legs by IK**: for each leg, the ankle target relative to the hips is
   `(face x (foot - hip) x g x H, hh - lift)`, where `hh` is the hip height: `0.47 H - sink`, lowered
   as far as needed so that no planted leg is longer than `0.990 x 0.47 H` (no knee ever locks: the
   smallest knee bend is about 0.28 rad). Two-bone solve with equal bones (0.235 H each): knee point =
   midpoint + perpendicular x sqrt(L^2 - (d/2)^2) on the forward side; `thigh = atan2(knee)`,
   `knee = thigh - atan2(ankle - knee)`. A second pass raises each ankle by the amount its toe would
   dip below it (the rig pitches the foot with the shin, `kFootFollow`), so both planted feet sit on
   the ground line together.
6. **Arms by IK**: hand targets relative to the shoulder joint, in the world's orientation (the rig's
   angles are absolute), upper arm 0.16 H, forearm 0.15 H, reach capped at 0.305 H and floored at
   0.06 H. The elbow takes the low or back side (rig elbow bend >= 0): the elbows hang, as they should.
   Hand paths are curves through the key positions, so a hand that goes from behind the shoulder to a
   push passes by the ear without any angle bookkeeping.
7. **Never a dead frame**: on top of the keys, two slow table sines: the hips drift +-0.004 H at
   0.11 Hz and each hand +-0.006 H at 0.07 and 0.09 Hz. At a hold the sink eases 0.006 H deeper and
   releases as the next move starts. His breathing is slowed and deepened for the form
   (`breath_rate 0.6`, `breath_scale 1.5`: a 6 s breath, about 1.5 px; the Lookout already uses these
   two fields) and put back to 1 and 1 at the end.
8. **Out**: `h.pose` = the solved pose (dx 0, lift 0), `h.feet.x = x0 + face-independent hip x g x H`,
   `h.feet.y = y0`, `h.facing = face`, `h.foot_turn` (section 4.5). Coming in and going out, the
   module's pose is mixed with the rig's own POSE_STAND over 1.5 s by weight `w` (the first key, wuji,
   is a stand, so nothing jumps).
9. **Determinism**: single precision, `sinT/cosT` for the sines, `sqrtf` (exactly rounded), and the
   module's own polynomial `atan2` (below), so no libm call whose last bit can differ. No RNG at all:
   small per-loop variation (sink +-0.005 H, hand heights +-0.01 H) comes from a hash of the layout and
   `Memory::loop_count`, never from timing, so the beat's seconds are fixed.

```
// |z| <= 1: z x (0.9998660 + z2 x (-0.3302995 + z2 x (0.1801410 + z2 x (-0.0851330 + z2 x 0.0208351))))
// other octants by the usual reflections; max error about 1e-5 rad
float taiAtan2(float y, float x);
```

### 4.4 The hand postures (hand positions from the shoulder, units of H, x forward, y down)

Hand 0 first. A `~` after a name in the chapter tables means the two hands are swapped.

| id | hand 0 | hand 1 | what it is |
|---|---|---|---|
| HANG | (0.06, 0.30) | (0.00, 0.305) | arms at his sides |
| DOWN | (0.00, 0.305) | (0.00, 0.305) | both straight down inside the torso's silhouette (the turns) |
| RAISE | (0.29, 0.02) | (0.28, 0.04) | both arms floated up to shoulder height |
| LOWER | (0.20, 0.22) | (0.18, 0.23) | both pressed down in front of the hips |
| BALL | (0.19, -0.02) | (0.19, 0.20) | holding the ball: one hand over the other in front of the chest |
| MANE | (0.30, -0.04) | (-0.02, 0.30) | one hand forward at eye level, the other pressed down by the hip |
| CRANE | (0.11, -0.28) | (0.10, 0.28) | one hand above the head, the other low |
| WIND | (0.22, 0.03) | (-0.21, -0.04) | one hand across the chest, the other drawn back and up behind |
| BRUSH | (0.30, 0.00) | (0.11, 0.28) | a push at shoulder height, the other hand brushed down past the knee |
| LUTE | (0.30, -0.04) | (0.21, 0.09) | both forward, one high and long, one by its elbow |
| MONK_OPEN | (0.30, 0.00) | (-0.21, -0.04) | repulse monkey: one arm out ahead, one back and up |
| MONK_PASS | (0.16, 0.24) | (0.10, -0.08) | the front hand drawn to the hip, the back hand by the ear |
| WARD | (0.28, 0.05) | (0.07, 0.29) | ward off: a level forearm ahead of the chest |
| ROLL | (0.22, 0.15) | (0.16, 0.21) | roll back: both drawn down toward the back hip |
| PRESS | (0.30, 0.03) | (0.28, 0.04) | press: both forward together |
| WITHDRAW | (0.19, 0.07) | (0.18, 0.09) | both drawn back to the chest, elbows down |
| PUSH | (0.30, 0.01) | (0.29, 0.03) | both palms pushed out |
| WHIP_GATHER | (0.22, 0.03) | (-0.22, 0.02) | the hook hand going out behind |
| WHIP | (0.30, -0.01) | (-0.29, -0.07) | single whip: a palm ahead, the hook hand straight out behind, a little high |
| CLOUD | (0.21, -0.04) | (0.19, 0.21) | one hand passing the face, one passing the belly |
| SNAKE | (0.24, 0.18) | (-0.26, -0.12) | the front hand down along the front leg, the hook high behind |
| RISE | (0.28, 0.06) | (-0.10, 0.27) | coming up out of the snake |
| ROOSTER | (0.17, -0.05) | (0.06, 0.29) | a forearm upright over the raised knee, the other hand pressed down |
| HIGHPAT | (0.30, -0.06) | (0.15, 0.16) | one palm out at eye level, one palm up at the waist |
| CROSS | (0.19, 0.02) | (0.17, 0.04) | wrists crossed in front of the chest |
| KICK | (0.30, -0.02) | (-0.29, -0.04) | both arms out level, ahead and behind |
| NEEDLE_UP | (0.12, -0.07) | (0.05, 0.28) | a hand lifted by the ear before it goes down |
| NEEDLE | (0.14, 0.27) | (-0.04, 0.29) | the hand pointing at the ground ahead |
| FAN | (0.30, 0.00) | (0.11, -0.26) | a push ahead and the other arm arched over the head |

One-colour silhouette, side on: a hand shows only when it is outside the torso (half width 0.075 H).
Every posture above but DOWN keeps at least one hand 0.10 H or more clear of it. Left and right
versions of a form look the same side on, so "mane 1, mane 2" differ only in which leg steps.

### 4.5 Turning round (the one proposed rig change)

Three turns in the whole form (two in chapter 2, one in chapter 4). A turn key gathers him into a
shape that is the same from both sides: feet together, upright (lean 0), arms straight down (DOWN),
hat off. What is left that is not symmetric is the feet (heel 0.02 H, toe 0.07 H) and a 1 px head
offset.

PROPOSED: `float foot_turn` in `Hiker` (0 normal, 1 end-on). In `hiker.cpp` the three places that
compute `heel` and `toe` (`build`, `hikerHandPos`, `hikerHeadPos`) use
`lerp(kHeel, 0.035f, foot_turn)` and `lerp(kToe, 0.035f, foot_turn)`. `hikerInit` sets 0. At 0 the
lerp returns kHeel and kToe exactly, so every existing frame is bit for bit what it was. The module
ramps it 0 to 1 over the last 1.2 s into the turn key (the feet shorten, as feet do when they swing
toward you), flips `face` in the middle of the key's 1.0 s hold when everything is symmetric, and
ramps it back over the first 1.2 s out. Cost: 4 bytes in each Hiker.

Alternative with no rig change: flip at the same instant on a 0.02 H dip over 3 ticks, which is what
the doorbell turn does (`react.cpp:74`). The feet then pop 4 px in one frame.

---

## 5. Choreography, beat by beat

One script for all four chapters; the chapter, the gag and the guests come from the variant byte and
the module. Walks are 28 px/s (slower than the 34 stroll: he is not going anywhere).

### 5.1 Coming to it (about 14.5 s)

1. `pose(POSE_STAND, 0.8, 0.3)`. (1.1 s)
2. `go(AT_M2, 28)`: from wherever the wake left him (the door, or the fire) to beside the hat's place.
   About 140 to 200 px. (5 to 7 s)
3. `face(LEFT)`, `skipUnless(hatOn, 4)`, `act(actHatOff, 2.2)` (the near hand up to the brim, a small
   bow of the head, the hat comes off at u 0.45 and is held: ITEM_HAT, the way `actWring` takes it,
   `majors.cpp:2666`), `pose(POSE_CROUCH, 0.7, 0.2)`, `doit(doHatDown)` (ITEM_HAT down at AT_M1),
   `pose(POSE_STAND, 0.8, 0.4)`. (4.3 s)
4. `walk(AT_M0, 22, 0, 0)` (16 px down the bank to the line), `face(RIGHT)`, `still(1.2)`: he looks
   upstream. (2.0 s)
5. `doit(doFormBegin)`: the module comes on (weight 0 to 1 over 1.5 s into wuji), `doit(cueGuests)`.

### 5.2 The form: `until(formDone)`

Every chapter opens the same way (stillness, then the commencing) and ends the same way (the hands
come down, he stands, three breaths), so each one reads as whole. Columns: where foot A and foot B
stand and where the hips are (H east of x0; `^` a held lift: ^1 = 0.012 H, a toe or heel touch;
^3 = 0.03; other values written out), the sink (H), lean and head (rad), the hands, facing, seconds to
arrive and seconds held. The legs' angles are not in the table: the IK finds them. Checked on paper
with a small script: in every segment at most one foot changes place, every key is within reach, and
the sums are as given.

**Chapter 1, the opening (105.8 s). Guest: the heron.**

| # | form | A | B | hip | sink | lean | head | hands | face | move | hold |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | stillness (wuji) | 0.02 | -0.02 | 0.00 | 0 | 0.02 | 0 | HANG | E | 0 | 5.0 |
| 1 | commencing: the arms float up | 0.02 | -0.02 | 0.00 | 0 | 0.00 | -0.03 | RAISE | E | 5.0 | 1.2 |
| 2 | ... and press down as he sinks | 0.02 | -0.02 | 0.00 | 0.03 | 0.02 | 0.03 | LOWER | E | 5.0 | 2.0 |
| 3 | hold the ball | 0.04^1 | -0.02 | -0.02 | 0.05 | 0.00 | 0.05 | BALL | E | 3.5 | 0.4 |
| 4 | part the wild horse's mane, 1 | 0.27 | -0.02 | 0.18 | 0.05 | 0.04 | -0.03 | MANE | E | 5.5 | 2.4 |
| 5 | sit back, the ball | 0.27 | -0.02 | 0.10 | 0.06 | 0.00 | 0.05 | BALL~ | E | 3.0 | 0.3 |
| 6 | part the wild horse's mane, 2 | 0.27 | 0.54 | 0.45 | 0.05 | 0.04 | -0.03 | MANE~ | E | 5.5 | 2.4 |
| 7 | half step up, the ball | 0.44 | 0.54 | 0.44 | 0.05 | 0.00 | 0.05 | BALL | E | 3.5 | 0.3 |
| 8 | white crane spreads its wings | 0.44 | 0.66^1 | 0.41 | 0.06 | -0.02 | -0.10 | CRANE | E | 5.5 | 5.0 |
| 9 | wind up | 0.44 | 0.66^1 | 0.41 | 0.06 | 0.00 | 0.00 | WIND | E | 3.5 | 0.3 |
| 10 | brush knee and push | 0.44 | 0.66 | 0.56 | 0.05 | 0.04 | 0.00 | BRUSH~ | E | 5.5 | 2.4 |
| 11 | playing the lute | 0.44 | 0.66^1 | 0.43 | 0.06 | -0.02 | 0.00 | LUTE~ | E | 4.5 | 2.4 |
| 12 | repulse monkey: open | 0.44 | 0.66^1 | 0.43 | 0.06 | 0.00 | 0.00 | MONK_OPEN~ | E | 3.5 | 0.3 |
| 13 | ... the foot passes back | 0.44 | 0.43^3 | 0.40 | 0.05 | 0.00 | 0.00 | MONK_PASS~ | E | 2.5 | 0 |
| 14 | repulse monkey, 1 | 0.44^1 | 0.20 | 0.23 | 0.06 | -0.02 | 0.00 | MONK_OPEN | E | 3.0 | 1.2 |
| 15 | ... the foot passes back | 0.21^3 | 0.20 | 0.19 | 0.05 | 0.00 | 0.00 | MONK_PASS | E | 2.5 | 0 |
| 16 | repulse monkey, 2 | -0.04 | 0.20^1 | -0.01 | 0.06 | -0.02 | 0.00 | MONK_OPEN~ | E | 3.0 | 1.2 |
| 17 | closing: feet together, hands down | -0.04 | 0.00 | -0.02 | 0.03 | 0.00 | 0.03 | LOWER | E | 4.5 | 0.5 |
| 18 | ... he stands; three breaths | -0.04 | 0.00 | -0.02 | 0 | 0.02 | 0 | HANG | E | 4.5 | 5.0 |

Two steps out, two steps back: he ends within 2 px of where he began. Cues: CRANE on arriving at 8,
CRANE_END at the end of its hold; CLOSING on arriving at 17; DONE at the end of 18.

**Chapter 2, the sparrow's tail (115.3 s). Guest: the bird on his hand.**

| # | form | A | B | hip | sink | lean | head | hands | face | move | hold |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | stillness | 0.02 | -0.02 | 0.00 | 0 | 0.02 | 0 | HANG | E | 0 | 4.0 |
| 1 | commencing: up | 0.02 | -0.02 | 0.00 | 0 | 0.00 | -0.03 | RAISE | E | 5.0 | 1.0 |
| 2 | ... and down | 0.02 | -0.02 | 0.00 | 0.03 | 0.02 | 0.03 | LOWER | E | 5.0 | 1.5 |
| 3 | wind up | 0.04^1 | -0.02 | -0.02 | 0.05 | 0.00 | 0.00 | WIND | E | 3.5 | 0.3 |
| 4 | brush knee and push, 1 | 0.27 | -0.02 | 0.18 | 0.05 | 0.04 | 0.00 | BRUSH~ | E | 5.5 | 2.4 |
| 5 | sit back, wind up | 0.27 | -0.02 | 0.12 | 0.06 | 0.00 | 0.00 | WIND~ | E | 3.0 | 0.3 |
| 6 | brush knee and push, 2 | 0.27 | 0.54 | 0.45 | 0.05 | 0.04 | 0.00 | BRUSH | E | 5.5 | 2.4 |
| 7 | gather and turn round | 0.50 | 0.54 | 0.52 | 0.03 | 0.00 | 0.00 | DOWN | E to W | 4.5 | 1.0 |
| 8 | grasp the sparrow's tail: ward off | 0.27 | 0.54 | 0.36 | 0.05 | 0.04 | 0.00 | WARD | W | 5.5 | 1.5 |
| 9 | ... roll back | 0.27 | 0.54 | 0.49 | 0.06 | -0.04 | 0.03 | ROLL | W | 4.5 | 0.6 |
| 10 | ... press | 0.27 | 0.54 | 0.36 | 0.05 | 0.05 | 0.00 | PRESS | W | 4.5 | 1.0 |
| 11 | ... withdraw | 0.27 | 0.54 | 0.49 | 0.06 | -0.03 | 0.00 | WITHDRAW | W | 4.0 | 0.5 |
| 12 | ... push | 0.27 | 0.54 | 0.36 | 0.05 | 0.05 | 0.00 | PUSH | W | 4.5 | 2.4 |
| 13 | the hook goes out | 0.27 | 0.54 | 0.47 | 0.06 | 0.00 | 0.00 | WHIP_GATHER | W | 4.0 | 0.3 |
| 14 | single whip | 0.10 | 0.54 | 0.28 | 0.06 | 0.02 | 0.00 | WHIP | W | 6.0 | 7.0 |
| 15 | gather and turn back | 0.10 | 0.14 | 0.12 | 0.03 | 0.00 | 0.00 | DOWN | W to E | 5.0 | 1.0 |
| 16 | cross hands | 0.10 | 0.14 | 0.12 | 0.03 | 0.00 | 0.00 | CROSS | E | 3.5 | 0.8 |
| 17 | closing: hands down | 0.10 | 0.14 | 0.12 | 0.03 | 0.00 | 0.03 | LOWER | E | 4.0 | 0.3 |
| 18 | ... he stands; three breaths | 0.10 | 0.14 | 0.12 | 0 | 0.02 | 0 | HANG | E | 4.5 | 5.0 |

Keys 8 to 12 are the weight-shift showcase: the feet never move for 27 s while the hips go forward,
back, forward, back, forward over them. Cues: WHIP 1.5 s into key 14's hold, WHIP_END at its end.

**Chapter 3, clouds and the rooster (107.7 s). Guest: the chipmunk.**

| # | form | A | B | hip | sink | lean | head | hands | face | move | hold |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | stillness | 0.02 | -0.02 | 0.00 | 0 | 0.02 | 0 | HANG | E | 0 | 4.0 |
| 1 | commencing: up | 0.02 | -0.02 | 0.00 | 0 | 0.00 | -0.03 | RAISE | E | 5.0 | 1.0 |
| 2 | ... and down | 0.02 | -0.02 | 0.00 | 0.03 | 0.02 | 0.03 | LOWER | E | 5.0 | 1.5 |
| 3, 5, 7 | wave hands like clouds | 0.02 | -0.02^1.5 | 0.02 | 0.04 | 0.00 | -0.03 | CLOUD | E | 3.6 | 0 |
| 4, 6, 8 | ... the other hand | 0.02^1.5 | -0.02 | -0.02 | 0.04 | 0.00 | 0.03 | CLOUD~ | E | 3.6 | 0 |
| 9 | single whip | 0.40 | -0.02 | 0.20 | 0.06 | 0.02 | 0.00 | WHIP | E | 5.5 | 2.5 |
| 10 | snake creeps down | 0.40 | -0.02 | 0.03 | 0.25 | 0.30 | -0.12 | SNAKE | E | 6.0 | 2.0 |
| 11 | ... and up | 0.40 | -0.02 | 0.24 | 0.10 | 0.10 | 0.00 | RISE | E | 4.5 | 0 |
| 12 | golden rooster stands on one leg | 0.40 | 0.50^(0.24 H) | 0.40 | 0.02 | 0.00 | 0.00 | ROOSTER~ | E | 5.0 | 5.0 |
| 13 | high pat on horse | 0.40 | 0.62^1 | 0.39 | 0.06 | 0.00 | -0.03 | HIGHPAT | E | 5.0 | 2.4 |
| 14 | repulse monkey: open | 0.40 | 0.62^1 | 0.39 | 0.06 | 0.00 | 0.00 | MONK_OPEN | E | 3.5 | 0.3 |
| 15 | ... the foot passes back | 0.40 | 0.40^3 | 0.36 | 0.05 | 0.00 | 0.00 | MONK_PASS | E | 2.5 | 0 |
| 16 | repulse monkey, 1 | 0.40^1 | 0.16 | 0.19 | 0.06 | -0.02 | 0.00 | MONK_OPEN~ | E | 3.0 | 1.2 |
| 17 | ... the foot passes back | 0.17^3 | 0.16 | 0.15 | 0.05 | 0.00 | 0.00 | MONK_PASS~ | E | 2.5 | 0 |
| 18 | repulse monkey, 2 | -0.08 | 0.16^1 | -0.05 | 0.06 | -0.02 | 0.00 | MONK_OPEN | E | 3.0 | 1.2 |
| 19 | closing | -0.08 | -0.04 | -0.06 | 0.03 | 0.00 | 0.03 | LOWER | E | 4.5 | 0.5 |
| 20 | ... he stands; three breaths | -0.08 | -0.04 | -0.06 | 0 | 0.02 | 0 | HANG | E | 4.5 | 5.0 |

The clouds are the side-on version: he stays where he is, the hands wheel past his face and his belly
in turn for 21.6 s without one stop, the weight rocks 4 px, one heel then the other comes off the
ground. Key 10 is the lowest shape in the whole piece (hips 22 px down, one leg out along the ground)
and key 12 the tallest and narrowest, 14 s apart. Cues: CLOUDS at key 3, SNAKE on arriving at 10,
ROOSTER on arriving at 12, ROOSTER_END at its hold's end.

**Chapter 4, the kick and the close (107.6 s). Guest: the deer at the tree line.**

| # | form | A | B | hip | sink | lean | head | hands | face | move | hold |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | stillness | 0.02 | -0.02 | 0.00 | 0 | 0.02 | 0 | HANG | E | 0 | 4.0 |
| 1 | commencing: up | 0.02 | -0.02 | 0.00 | 0 | 0.00 | -0.03 | RAISE | E | 5.0 | 1.0 |
| 2 | ... and down | 0.02 | -0.02 | 0.00 | 0.03 | 0.02 | 0.03 | LOWER | E | 5.0 | 1.5 |
| 3 | high pat on horse | 0.22^1 | -0.02 | -0.03 | 0.06 | 0.00 | -0.03 | HIGHPAT | E | 5.0 | 2.0 |
| 4 | the wrists cross, the foot draws in | 0.06^(0.04 H) | -0.02 | -0.02 | 0.05 | 0.00 | 0.00 | CROSS | E | 4.0 | 0.4 |
| 5 | heel kick | 0.34^(0.30 H) | -0.02 | -0.04 | 0.02 | -0.08 | -0.03 | KICK | E | 4.5 | 3.0 |
| 6 | the knee folds, wind up | 0.12^(0.10 H) | -0.02 | -0.02 | 0.05 | 0.00 | 0.00 | WIND | E | 3.5 | 0 |
| 7 | brush knee and push | 0.27 | -0.02 | 0.18 | 0.05 | 0.04 | 0.00 | BRUSH~ | E | 5.0 | 2.4 |
| 8 | half step up, the hand by the ear | 0.27^1 | 0.05 | 0.07 | 0.06 | 0.00 | 0.00 | NEEDLE_UP | E | 4.0 | 0.3 |
| 9 | needle at sea bottom | 0.27^1 | 0.05 | 0.08 | 0.15 | 0.55 | 0.10 | NEEDLE | E | 5.0 | 2.5 |
| 10 | fan through the back | 0.37 | 0.05 | 0.26 | 0.05 | 0.03 | -0.03 | FAN | E | 5.5 | 2.6 |
| 11 | gather and turn round | 0.37 | 0.33 | 0.35 | 0.03 | 0.00 | 0.00 | DOWN | E to W | 4.5 | 1.0 |
| 12 | apparent close-up: withdraw | 0.37 | 0.09 | 0.30 | 0.06 | -0.03 | 0.00 | WITHDRAW | W | 4.5 | 0.5 |
| 13 | ... push | 0.37 | 0.09 | 0.18 | 0.05 | 0.05 | 0.00 | PUSH | W | 4.5 | 2.4 |
| 14 | cross hands | 0.13 | 0.09 | 0.11 | 0.03 | 0.00 | 0.00 | CROSS | W | 5.0 | 1.5 |
| 15 | closing: hands down | 0.13 | 0.09 | 0.11 | 0.03 | 0.00 | 0.03 | LOWER | W | 5.0 | 0.5 |
| 16 | ... he stands; the long close | 0.13 | 0.09 | 0.11 | 0 | 0.02 | 0 | HANG | W | 5.0 | 7.0 |

The last hold is the end of the whole form, so it is the longest stillness of the four: 7 s, with a
small bow of the head (+0.14 rad from 2.0 to 3.4 s into it), to the stream. Cues: KICK on arriving at
5, KICK_END at its hold's end, NEEDLE on arriving at 9.

The order follows the 24-form (simplified Yang style) where the 75 px of bank allows; repeats are cut
from three to two, and chapters 3 and 4 borrow a repulse monkey and a brush knee to walk him back to
his start. A practitioner will see the liberties. PROPOSED: Mike's call whether that matters.

### 5.3 Going back (about 15.5 s)

6. `until(formDone)` ends with the module's weight eased out over 1.5 s into POSE_STAND, `foot_turn`
   0, his breathing back to 1 and 1. `still(2.0)`.
7. `go(AT_M2, 24)`, `face(LEFT)`, `pose(POSE_CROUCH, 0.7, 0.2)`, `doit(doHatUp)`,
   `pose(POSE_STAND, 0.7, 0.2)`, `act(actHatOn, 2.0)` (`actHatOnM`'s motion, `majors.cpp:2693`),
   `still(0.8)`. (about 6 s)
8. `until(guestsGone)` (instant in practice: every guest has left by its own cue, section 6).
9. `go(AT_SIT, 28)`, `face(TO_FIRE)`, `pose(POSE_SIT, 0.9, 0.6)`. (about 6.5 s)

The fire is cold ash at that hour (`morningLayout`: `fire.ash = 1`); he sits by it and the coffee beat
finds him there, as after any morning major.

Beat seconds (script plus 7 s of slack for the longer walks): chapter 1 **143**, chapter 2 **153**,
chapters 3 and 4 **145**. With a gag: wobble +8, itch +6, Copper's bump +12. PROPOSED; tune with
SS_SPINE_TRACE.

### 5.4 What keeps it from looking stiff (the list a reviewer should check on the stills)

- The feet do not slide. Ever.
- The hips move first, the hands arrive a quarter second later, the head last.
- The weight is over the standing foot before the other one leaves the ground, and the stepping foot
  travels only in the middle of the move.
- No knee locks. No arm is ever dead straight (reach cap 0.305 of 0.31).
- Short holds are passed through, not stopped at. Only the named forms hold, 1.2 to 7 s.
- At a hold he is still breathing (6 s breaths), sinking a hair, drifting a hair.
- His head goes with the leading hand: up a little for the crane and the high pat, down along the leg
  for the snake and the needle.
- Nothing in the form is fast. The only fast thing in the piece is the slap in the itch gag, which is
  why it is funny.

---

## 6. Variants: the guests, the gags, the dogs, the weather

The variant byte (`taichiPlanVar`): bits 0-1 the chapter, bit 3 DAY_NOSHOW (set by the begin hook),
bits 4-5 the gag (0 none, 1 wobble, 2 itch, 3 Copper's bump), bit 6 guests on. All from the plan's one
`r_var` draw and Memory; no extra RNG.

### 6.1 The guests (one a chapter; none when a house dog is in camp or `guests` is off)

Guests come because he is slow and calm, do one clear thing in answer to one form, and are gone before
he bends for his hat. Bit 6 is set 3 times in 4 (`(r_var >> 16) & 3 != 0`).

- **Chapter 1, the heron and the white crane.** On the cue at key 3 (22 s in) the day tick calls
  `streamHeronGuest(w, x0 + 30)`: the stream's own heron (`stream.cpp` StreamVisit) flies in (its
  5 s flight) and stands in the near shallows, facing the way he faces. It does its own small things
  (idle, alert, a look back). At CRANE + 1.0 s it lifts one leg and half opens its wings
  (`streamHeronMirror(w, true)`, eased over 2.5 s), a beat behind him, and holds as long as he does.
  At CRANE_END + 0.8 s it folds (1.5 s). At CLOSING the day tick calls `streamHeronRelease`: it is the
  stream's own again and stays out its planned time or leaves by its own rules (it stays through majors
  that do not use the water, `kWaterMajors`, and tai chi is not one). If the stream's visitor this loop
  is the deer, or its heron has already come and gone, there is no heron: one visitor a loop stays the
  rule. If the stream's own heron is already standing there, it is simply taken as the guest.
- **Chapter 2, the bird on the hook hand.** At WHIP (1.5 s into the hold) a small bird (CR_BIRD,
  `directorSpawn`, CT_MAJOR, CF_BRAVE, `MajorState::c[0]`) comes down from the upper left
  (`(-24, 150)`) on a curve (the `bez`/`flyAlong` helpers of `spine_morning.cpp:79`, copied) and lands
  on his back hand at 3.6 s. Each tick while it sits its feet are put at that hand
  (`hikerHandPos(h, hook)`, y - 1), `sort_y = y0 + 2` so it draws in front of him. It looks about
  (CA_LOOK once, 1.2 s). His head lifts 0.06 rad: he knows. At WHIP_END, as the arm starts to come in,
  it hops once (CA_HOP) and flies off up and to the right (`(824, 120)`), gone in 2.5 s. This is why
  the single whip holds 7 s.
- **Chapter 3, the chipmunk that copies.** At CLOUDS a chipmunk (CR_CHIPMUNK, CT_MAJOR, CF_BRAVE,
  the loop's persona) runs in from the right edge along the bank at y0 + 5 to its spot
  (`x0 + 78, y0 + 5`), 70 px/s, and sits up (CA_SIT) facing him. At SNAKE it drops to all fours
  (CA_IDLE) as he goes down. At ROOSTER + 0.6 s it sits up tall again and holds stiller than he does
  (CA_SIT, then CA_LOOK at him). At CLOSING it runs back off to the right (`directorExit(+1)`).
- **Chapter 4, the deer at the tree line.** At key 3 a deer (CR_DEER, CT_MAJOR, CF_BRAVE) walks in
  from the right edge at y 338, 22 px/s, to its spot beyond the pine and stands watching him (CA_LOOK,
  then CA_HEADTURN now and then from its stage clock). At KICK its head goes up (CA_LOOK, 0.3 s blend);
  at NEEDLE, when he folds down out of its sight, it takes three steps nearer (to spot.x - 26). At
  CLOSING it turns and walks out the way it came. Skipped when the loop's plan holds the new deer-visit
  major (section 7.6), and when the stream's deer is in the scene.
- **The butterfly** (any chapter, months 4 to 9 or month 0, `w.stream.month`; and only with guests
  on): the module's own two-triangle flier (ACTOR_LIGHT wings, 2 primitives, about 5 px), not a
  critter and not a World bit. From the commencing's top it wanders round a point 14 px above his
  leading hand on two slow table sines, lagging the hand by 2 s; it settles on his shoulder for the
  closing's stand and leaves upward as he walks off. Drawn as the day piece's part 1 (in front of him).
- **The fish**: the stream's fish jump on their own (`StreamFish`). If one jumps within 160 px of him
  during the form, the day tick (read-only on the stream's state) lays a head turn over the form:
  `fx.head` +0.10 for 1.4 s, eased. The form does not break. He notices things.

### 6.2 The gags (gentle, at most one a session)

`gag = (r_var >> 20) % 20`: 0 to 11 none, 12 to 14 wobble, 15 to 17 itch, 18 to 19 none. When Copper
is out at loop start (`in.dog_out & 2`), 12 to 19 become Copper's bump.

- **The wobble (+8 s)**: on the chapter's balance key (crane's toe stance in 1, the rooster in 3, the
  kick in 4; chapter 2 has none and plays the itch instead), 1.5 s into the hold. 0 to 1.2 s: a slow
  drift, the lean goes +0.10 too far and the hips 0.03 H with it. 1.2 to 2.0 s: both arms go out
  (hands to (0.26, -0.06) and (-0.26, -0.04)), the lifted foot comes down ahead (planted at
  hip + 0.18 H), one small hop (lift 0.02 H). 2.0 to 3.4 s: he stands, the arms drop, the shoulders
  fall with a long breath out, head down 0.15. 3.4 to 4.8 s: a hand to the back of his head. 4.8 to
  8.0 s: hands down, he sinks, and goes back into the same key slowly; the hold then runs its last
  1.5 s. Meanwhile the guest does it better: the heron keeps its leg up 2 s longer than he did; the
  chipmunk starts (CA_STARTLE), then sits bolt upright and still through his whole recovery; the deer
  flicks an ear (CA_HEADTURN).
- **The itch (+6 s)**: in the longest hold of the chapter (crane, single whip, rooster, the needle).
  1.0 s of perfect stillness; then the near hand breaks form and slaps his neck in 0.25 s (hand to
  (0.03, -0.06)), stays there 0.6 s while his head tips toward it; then, guiltily, the hand goes back
  along the exact path it came, over 3.0 s, slower than anything else he does; the hold resumes. If
  the bird is on the other hand it stays (it is on the hook hand; the near hand slaps).
- **Copper's bump (+12 s)**: section 6.3.

### 6.3 Lucy and Copper

- **No gate**: tai chi plays with dogs out. The wild guests do not come (they would not, with a dog
  about; and the dogs chase the stream's heron, `HA_CHASE`): `guests` is cleared at begin when
  `w.dogs.want != 0`, and the day tick releases the guests calmly if a dog trots in mid-form
  (the bird and the chipmunk leave at once, the deer walks off, the heron is handed back to the stream,
  whose own rules send it up when the dog comes near).
- **Lucy lies down to watch.** A major sets `w.busy`, and the dogs then settle beside the tent
  (`HA_SETTLE`, `housedogs.cpp:222`). During tai chi Lucy's settle spot is
  `taichiWatchSpot`: `(x0 - 52, y0 - 4)`, facing him, lying with her head up (the existing CA_LIE and
  her slow wag). When he finishes and walks past for his hat she is there; the dogs' own `pat`
  activity may follow once he sits.
- **Copper** settles by the tent as usual (he would not lie still that near). With gag 3, at the
  chapter's bump key (brush knee in 1, the push in 2, the rooster in 3, the needle in 4), 0.8 s into
  the hold: the day tick borrows him (`houseDogsLend`, only if Copper is the dog it returns; else it
  gives the dog straight back and the gag is skipped, the 12 s become stillness at the end). He trots
  along the line from behind (`directorWalk` to `(hip x - 12 x face, y0 + 3)`, 60 px/s) and shoulders
  past his legs. On arrival: the wobble's first 3.4 s, with the stumble going the way Copper pushed.
  Copper sits (CA_SIT) and looks up at him. He looks down, one hand goes down to Copper's head for
  2.0 s (the pat: hand to (0.10, 0.30) with the lean +0.25), Copper's wag speeds up (the dogs' own wag
  clock runs on). He straightens, breathes, goes back into the key over 3.2 s. `houseDogsGiveBack`:
  Copper settles again. If Copper goes back indoors before the cue, the bump is skipped.

### 6.4 Weather and light

- **Fog**: the morning opens in fog that lifts in 0.05 steps from the end of the build to the end of
  the coffee's pause (`morningTick`, `fog_t0`, `fog_t1`). Tai chi sits in the slot after the wake, so
  it is inside that lift, and its seconds stretch it: the same 11 to 16 steps now spread over about
  300 s instead of about 130, so the mist thins slowly through the whole form, about one step every
  20 to 27 s, 5 to 8 of them during the form. No new palette changes: the count is what it was.
- **Rain, snow, storm**: the plan gives weight 0 when the OUTSIDE data says so; a random wet weather
  that turns up after the plan makes the begin hook play `dayNoShowScript` (he holds a hand out,
  shrugs, sits), exactly as the daytime majors do. The no-show does not advance the chapter.
- **Wind, chill, heat, a WX_FOG day**: he practises. The weather's pose overlays do not run while a
  script holds him (`weather.h`), so no shiver lands on a form.
- **First light**: the family is the morning's (mood 0.5 to 2.5), so the sky is whatever the real hour
  gives. Which side the real sun rises on screen is UNVERIFIED (not read); he faces upstream either
  way.

### 6.5 The doorbell and the passer-by

- **Doorbell**: can start on any tick. The story freezes: the major's tick does not run, so the
  module's clock stops and `dayPose` writes the same pose every tick (it also runs frozen,
  `stepHiker(w, true)`). `reactStart` gets `look_only` when `taichiInForm(w)`: his head lifts toward
  the sound (the reaction's own -0.22 rad on `fx.head`, added after `dayPose`), and that is all: no
  hop, no turn, no wave. He holds the single whip, or the rooster on one leg, for the 3.4 s, and goes
  on from the same instant. That is the joke: a chipmunk can unbalance him, the doorbell cannot.
  The bird on his hand, the chipmunk and the deer freeze with the story (the director does not step);
  the heron stands (the stream's `visitStep` is skipped while the reaction is active,
  `stream.cpp:1517`); the butterfly, being ambient decoration stepped from the frozen-safe side,
  keeps circling (its clock is `World::tick`; it reads the hand, it never moves him).
  Outside the form (the walks, the hat) the normal reaction plays; with the hat in his hand it is an
  Item, not a `carry`, so `reactStart` also sets `hands_full` when `taichiHatInHand(w)`.
- **Passer-by wave**: refused during any major (`w.busy`, `reactWaveOk`).
- **Fire and ice, the visiting couple, the mail glance, the minors**: none start during a major (they
  test `w.busy` or "no major"). `mailPose` runs after `dayPose` in `stepHiker`; that it stays quiet
  while a major is on is UNVERIFIED (mailbird.cpp not read): the builder checks, and gates it on
  `!taichiInForm(w)` if not.

---

## 7. How it meets the other four new pieces (and the old ones)

1. **Long-range shooting** (a daytime play): never in the same beat, and tai chi is the morning's
   alone. They share files only (section 10): `majors.h`, `majors.cpp`, `day.h`, `day.cpp`,
   `react.cpp`. Both add a MajorId and a DayKind: the integrator fixes the order of the new ids once
   for all five pieces. Majors today: 25 of 32.
2. **Reeds and cattails** (passive, all day, by the stream). Three requests to that piece:
   - keep the camp-side bank from `cairn.x + 6` to `cairn.x + 100` free of clumps taller than 20 px
     (his shins), or his feet and the planted-foot work vanish behind stems. Clumps on the near bank
     and in the shallows in front of him are welcome: he then practises behind a fringe of cattails
     with the water between, which is the picture.
   - the heron's spot (x 400 to 480 on the near side) should stay open water or low reeds, so the
     heron reads beside them and not inside them.
   - if that piece gives the hiker a "pick a reed and chew it" act, the tai chi outro is a good place
     for it: after the hat goes back on, on the way to the fire. Not built here; a one-line hook
     (`skipUnless(reedHere, n)` before step 9) when both exist.
   And for free: their seed fluff in autumn will drift through the closing's stillness, and their sway
   and his slow hands answer the same wind.
3. **Mushrooms and the trip**: if that piece leaves a Memory flag for "he ate them last loop", the
   next tai chi session forces the wobble gag (his balance is off). One line in `taichiPlanVar`. The
   trip itself could borrow the clouds (chapter 3, keys 3 to 8) for him to do under the psychedelic
   sky: the module is a pure function of (chapter, time), so another piece can run a sub-range of it.
   Suggestion only; not built here.
4. **The deer that visit while he is away**: chapter 4's watcher is one deer from the same kind
   table. When the loop's plan holds the deer-visit major, tai chi's own deer is skipped (one deer
   story a loop). If that piece adds a fawn, chapter 4's guest can later become a doe and a fawn, the
   fawn taking a step each time he does. Not built here.
5. **The visiting couple** (existing): they do not come during a major. A later idea, cheap because
   the module is pure: Ann stands behind him and follows the same chapter 1.2 s late. Parked.
6. **The stream's own life** (existing): floaters and fish go on as always; the fish glance (6.1).
   The stream's own visiting deer leaves when a major begins (its rule); the heron stays.
7. **The mail bird** (existing): keeps its place on the tent ridge; it is not a guest.
8. **The sunrise overlook** (existing set piece): a different loop. With the first-morning rule
   (section 9) the loop after a sunrise overlook is the one that opens with tai chi in the mist.

---

## 8. Data added

Everything the form needs lives in the daytime union (`day.h` DayState), so World and WorldDirty do
not grow.

```
struct TaiKey {               // 16 bytes, const tables in flash
  int8_t a, b, hip;           // 0.01 H east of x0
  uint8_t la, lb;             // 0.004 H
  uint8_t sink;               // 0.005 H
  int8_t lean, head;          // 0.01 rad
  uint8_t hands;              // TaiHands | 0x80 swapped
  int8_t face;                // +1 east, -1 west; a turn key lists the facing it arrives with and sets TK_TURN
  uint8_t move, hold;         // 0.1 s
  uint8_t cue, flags;         // TaiCue bits; TK_TURN, TK_BALANCE, TK_LONGEST, TK_BUMP
  uint8_t pad[2];
};
struct TaiHandsDef { int8_t x0, y0, x1, y1; };   // 0.005 H from the shoulder; 29 rows

struct TaichiState {          // about 132 bytes
  uint8_t chapter, gag, phase, key;       // phase: off, in, form, gag, out, done
  uint8_t n_keys, events, guests, turned;
  int8_t face; uint8_t gag_step, bump_side, pad;
  float t, key_t;             // seconds in the form, seconds in the segment
  float w;                    // the module's weight over the rig's pose
  float x0, y0, g;            // the line
  float gag_t;                // seconds in the gag
  float foot_turn;
  float hip_x;                // frame x of the hips this tick
  Vec2 hand[2];               // the hands this tick, frame space (the bird, the butterfly)
  HikerPose out;              // 48: this tick's solved pose
  Vec2 fly; float fly_ph; uint8_t fly_on, fly_mode, pad2[2];   // the butterfly, 16
};
struct DayTaichi { TaichiState t; };
static_assert(sizeof(DayTaichi) <= sizeof(DayBike), "the tai chi must fit in the bike's place");
```

- `DayKind`: `DAY_TAICHI` on the end.
- `MajorState`: used as the others use it. `p[0]` the line's start, `p[1]` the hat's place, `p[2]`
  where he stands for the hat, `p[3]` the guest's spot; `c[0]` the chapter's guest; `stage[0]`, `st[0]`
  its stage clock; `count` 1 + the borrowed dog's slot; cue bits kGo and kE1 (the form is done).
- `Memory`: one byte on the end, `uint8_t taichi_next;` (the chapter the next session plays). Not
  printed by `memoryFormat` (as `visit_wait` is not), so no pinned memory string changes. Whether it
  costs 0 or 4 bytes of `sizeof(App)` depends on the struct's tail padding: measure.
- `Hiker`: `float foot_turn;` (section 4.5), 4 bytes in World's hiker and in each of the visitors'
  two rigs (they live in a union asserted against LookoutLand: headroom UNVERIFIED, measure).
- `StreamVisit`: `uint8_t guest; float mirror;` (the tai chi's heron), about 8 bytes inside StreamLife,
  which shares the Lookout's running state (`static_assert` in `world.h:311`: headroom UNVERIFIED).
- No new palette slot, PoseId, CarryId, ItemId, critter kind, glyph shape, bit kind or anchor.

---

## 9. Gating, families, slots, weight, loop-length fit

- **Id and name**: `MJ_TAICHI`, appended to `MajorId`; name `"taichi"` (no other major's name contains
  it or is contained in it). `static_assert(MJ_COUNT == 26)` if it lands first of the five.
- **Pool row**: `{"taichi", {143, 143}, kTaichiW, 1, kMorn, taichiMajorBegin, taichiMajorTick,
  {nullptr, nullptr}, {0, 0}}`, `kTaichiW = 1.6f` PROPOSED. Slot bits `1`: after the wake only (in
  both mornings: `kMorningSlots` and `kStaySlots` both put slot 0 after the wake). Family `kMorn`
  only, so the evening's and the midday's plans draw exactly what they drew (`Rng::weighted` skips
  zero weights, HOW_TO_ADD_SCENES gotcha 4): only the morning pins move.
- **`scriptOf`**: `taichiScript(var, n)`. **`majorSeconds`**: `taichiSeconds(var)` (143, 153, 145,
  145 by chapter, plus the gag's seconds; the no-show 46 like the others). **`majorEnd`**:
  `taichiMajorEnd(w)`.
- **Gates in `majorsPlan`** (`majors.cpp:3933` block): weight 0 at night, and when the OUTSIDE
  reading is rain, snow or storm (add `MJ_TAICHI` to the line that zeroes the bike and its fellows).
  Dogs do not gate it.
- **The variant line**: `if (id == MJ_TAICHI) v = taichiPlanVar(r_var, mem, in.dog_out);`:
  chapter = `mem.taichi_next & 3`, or `(r_var >> 9) & 3` when `mem.loop_count <= 1` (a fresh Memory
  starts anywhere, so chapter 1 is not seen far more than the rest; whether Memory survives the
  screensaver stopping and starting is UNVERIFIED); the gag and guests bits as in section 6.
- **It counts as two majors**: `want` becomes a non-const local; when tai chi is picked,
  `if (want > 2) --want;`. The draws of every round are taken as always (rule 1.5.5), only the count
  of accepted picks changes. So a tai chi morning has tai chi and one or two others, not three.
- **The first morning of the day** (`kTaiFirstMorning`, PROPOSED true): in round k = 0, when
  `slots.family == SF_MORNING`, tai chi's gates pass, slot bit 1 is free and
  `mem.loop_count <= 1 || mem.last_family != SF_MORNING`, the pick becomes tai chi, the same way a
  house dog forces the fetch (`majors.cpp:3997`: "the draws above are taken as always; only the pick
  moves"). That `last_family` still holds the previous loop's family when `majorsPlan` runs is
  UNVERIFIED (memory.cpp not read): the builder checks where it is written.
- **Never dropped, and it may stand alone**: `majorKept` returns true for `MJ_TAICHI`; in
  `spineFitLength` (`spine.cpp:2758`) the floor `p.n_majors > 2` becomes `p.n_majors > floor_n`, with
  `floor_n` 1 when the plan holds tai chi, 2 otherwise. Without this the worst camp-stays morning
  cannot be fitted: its beats are up to 357 s (wake 30, coffee 74, pee 56, breakfast 43, wash 32,
  tidy 60, day hike 62), the build 13, six pauses squeezed to 8 s are 48: 418 s. Tai chi's longest
  (153 + 12 + two 4 s stills = 173) makes 591; a second major of up to 76 s would pass the 650 s aim,
  and the fit may not drop below two today. With the floor at 1 it drops that one and the loop is
  591 s. The break-camp morning is 301 s at its shortest plus tai chi 151 to 173: the fit stretches
  pauses and keeps fillers as it does now.
- **What it costs the rest of the morning**: in a camp-stays morning, tai chi leaves room for about
  one filler and one short major. That is the price of "not in a hurry"; `kTaiPace` 1.15 would leave
  room for a filler or a major, not both.
- **Memory**: `taichiMajorEnd` sets `taichi_next = (chapter + 1) & 3` when the form reached DONE (not
  on a no-show, not on an overrun). The ring and the recent sets treat it like any major.
- **Timeline cap**: unchanged (it is one major in an existing slot).

---

## 10. File plan

Two builders on disjoint files, then one integration pass. All new files LF and ASCII; existing files
keep their line endings (`majors.h` is CRLF).

### 10.1 Builder A: the module (pure, no World)

New:
- `screensaver/core/scene/taichi.h`, `taichi.cpp`: the key tables (four chapters, the hands table),
  `taichiInit(TaichiState&, chapter, gag, x0, y0, g)`, `taichiStep(TaichiState&, float dt)`,
  `taichiGag(TaichiState&, kind, side)`, `taichiPose(TaichiState&, Hiker&)` (writes pose, feet.x,
  feet.y, facing, foot_turn, breath_rate, breath_scale; mixes by `w`), `taichiFinish` (puts
  foot_turn 0 and the breath back), `taichiEvents` (and clears them), `taichiInForm`,
  `taichiFormSeconds(chapter)` and `taichiGagSeconds(gag)` (constexpr sums of the tables),
  `taichiFootFrame(const TaichiState&, int)` (tests), the butterfly: `taichiFlyStep(TaichiState&,
  uint32_t tick)`, `taichiFlyBounds`, `taichiFlyDraw`, `taichiDirty(prev, cur, Rect* out)`
  (`TAICHI_DIRTY_MAX 2`).
- `screensaver/tests/test_taichi.cpp` (section 11.1).
- `screensaver/tools/taichi_stills.cpp`, `screensaver/tools/taichi_sheet.py` (section 12).

Edits (and no others):
- `core/scene/hiker.h`: `float foot_turn;` after `breath_scale`, with its comment.
- `core/scene/hiker.cpp`: `hikerInit` (`h.foot_turn = 0.f`), and the heel and toe lines in `build`
  (`hiker.cpp:399`), `hikerHandPos` (798) and `hikerHeadPos` (832).
- `tests/test_hiker.cpp`: one check that `foot_turn` 0 draws the same pixels as before for every
  pose, and that 1 keeps the draw inside `hikerBounds`.

### 10.2 Builder B: the major and the shared edits

New:
- `screensaver/core/scene/day_taichi.h`, `day_taichi.cpp` (the `day_archery.*` pattern):
  `taichiPlanVar`, `taichiSeconds`, `taichiScript`, `taichiMajorBegin/Tick/End`, the acts (hat off,
  hat down, hat up, hat on), the guests' stage machines, the fish glance, Copper's bump,
  `taichiInForm(const World&)`, `taichiHatInHand(const World&)`, `taichiWatchSpot(const World&,
  Vec2&)`.
- `screensaver/tests/test_day_taichi.cpp` (section 11.2).

Edits (exact places):
- `core/scene/day.h`: `#include "taichi.h"`; `DAY_TAICHI` on the end of `DayKind`; `struct DayTaichi`;
  the member in the union; the `static_assert`; the header comment's list.
- `core/scene/day.cpp`: the `DAY_TAICHI` case in `sizeOf`, `dayBounds` (the butterfly's bounds),
  `dayPose` (`taichiPose`), `dayHoldsFacing` (true while `taichiInForm`), `dayStep` (the butterfly:
  `taichiFlyStep`, ambient, also while frozen), `dayTrack` (`taichiDirty`), `dayPartY` (part 1 at
  `feet.y + 2` while the butterfly shows), `dayPartDraw`.
- `core/scene/majors.h` (CRLF): `MJ_TAICHI` and its comment.
- `core/scene/majors.cpp`: the include; `kTaichiW`; the pool row; the `static_assert` count;
  `scriptOf`; `majorSeconds`; `majorKept`; `majorEnd` (`taichiMajorEnd`); in `majorsPlan`: the weather
  gate line, `want` non-const and the counts-as-two line, the first-morning line beside `house_dog`,
  the variant line.
- `core/scene/spine.cpp`: `spineFitLength`: `floor_n` (3 lines).
- `core/scene/memory.h`: `uint8_t taichi_next;` on the end of `Memory`, and a row in the header table.
  (`memory.cpp`: nothing, unless `memoryBeginLoop` rebuilds the struct field by field: UNVERIFIED.)
- `core/scene/react.cpp`: `reactStart`: `look_only` also when `taichiInForm(w)`; `hands_full` also
  when `taichiHatInHand(w)`.
- `core/scene/stream.h`, `stream.cpp`: `StreamVisit::guest`, `mirror`; `streamHeronGuest`,
  `streamHeronMirror`, `streamHeronRelease`; in `heronStep` GS_STAY the hiker does not spook a guest
  (dogs still do) and a guest's `stay` is not counted down until it is released; `HPose` gets
  `float spread`, set from `mirror` in `heronPose` with `lift[0] = mirror` and `ext >= 0.8 x mirror`;
  `heronStand` draws the wings half open by `spread`. How `heronStand` builds the folded wing is
  UNVERIFIED (not read): about 2 more primitives. **The reeds and cattails piece will very likely
  edit `stream.cpp` too: do this edit in the integration pass, after that piece lands.**
- `core/scene/housedogs.cpp`: `HA_SETTLE`'s spot (`housedogs.cpp:222`): Lucy's is `taichiWatchSpot`
  when it returns true.
- `tests/test_spines.cpp`, `tests/test_morning_stay.cpp`: `"taichi"` in `isMajor`; the morning pins
  re-captured (say so in the notes). `tests/test_midday.cpp`: its morning pins (108-131) re-captured;
  its evening and midday pins must NOT change. `tests/test_scene.cpp`: nothing (`kMajorNames` is the
  evening's).
- `tools/review_clips.py`: nothing for a plain major (it cuts every name in `majors=`); a
  `--family morning` run shows it.
- `docs/screensaver/HOW_TO_ADD_SCENES.md`: a short section on the module-driven pose and `foot_turn`.

---

## 11. Tests

### 11.1 `tests/test_taichi.cpp` (the module)

1. Table rules, every chapter: at most one foot changes place per segment; a foot that changes place
   is not the one the hips are over at the segment's start; every key within reach with the knee cap;
   every turn key has feet within 0.06 H, lean 0, hands DOWN.
2. Planted feet: stepping the module tick by tick through each chapter, a foot that is down in both
   keys of a segment stays within 0.6 px in x and 1.0 px in y (`taichiFootFrame`) for the whole
   segment, holds included (the drift is on the hips, not the feet).
3. Continuity: no joint angle changes by more than 0.06 rad a tick and the hips by more than 0.5 px a
   tick, anywhere, the turns included, the gags excluded (the slap is checked to be the only faster
   move: at most 0.5 rad a tick, for at most 7 ticks).
4. Timing: each chapter reaches DONE at exactly `taichiFormSeconds(chapter)` (+ the gag's seconds);
   the values are 105.8, 115.3, 107.7, 107.6 s at pace 1.
5. The signature shapes (so a table typo cannot flatten one): crane: a hand at least 8 px above the
   head's top; single whip: hands at least 48 px apart in x, on opposite sides of the body; rooster:
   one knee within 4 px of hip height and that foot at least 18 px up; snake: hips at least 18 px
   lower than standing and one leg within 0.35 rad of the ground; kick: a foot at least 24 px up and
   26 px ahead; needle: lean at least 0.5.
6. Frozen: `taichiStep(s, 0)` any number of times, then `taichiPose`: the same pose, bit for bit.
7. Determinism: two runs give identical poses every tick; no call into libm's `atan2f`, `acosf`,
   `sinf` (a grep in the test's comment; the builder checks the object's imports).
8. Draw: for every key of every chapter, both facings, `hikerDraw` stays inside `hikerBounds`, at
   most 22 primitives, and the hat-less head is clear of both hands except in CRANE and FAN.
9. `foot_turn`: at 0 the drawn pixels equal a build without the field for all 20 PoseIds; at the
   flip tick the frame before and after differ by at most 40 pixels (the whole point of it).

### 11.2 `tests/test_day_taichi.cpp` (the major; model `tests/test_day_plink.cpp`)

1. Plan: over 60 morning seeds of 10 loops (both mornings turn up), tai chi plays in the first
   morning loop of each run and in 20 to 45 % of the rest; always after the wake; never at night,
   never with OUTSIDE rain, snow or storm; every loop is 480 to 660 s; a loop with tai chi has 1 to 3
   majors; `--loops 10` for seeds 1 and 7 at mood 4 (evening) and `--spine midday` are unchanged.
2. Chapters: with `Memory::taichi_next` preset to 0..3, that chapter plays and Memory holds the next
   one after; a no-show (forced rain after the plan) leaves it alone.
3. Whole-beat runs, each chapter with each gag, each with guests on and off, dog_out 0, 1, 2, 3:
   render every tick; zero overruns; he is in frame; nothing of his, the hat's, a guest's or the
   butterfly's is inside x 538..796, y 412..476; strips average 10 or fewer; incremental equals full
   at 5 ticks; two runs draw the same frames.
4. End state: sitting at `camp.sit` facing the fire, hat on, ITEM_HAT neither held nor down, hands
   empty, `foot_turn` 0, breathing 1 and 1, the day union zero, no CT_MAJOR critter, the heron not a
   guest, no dog lent, `appCritterCount` 0 at the loop's end.
5. Doorbell at five ticks (mid-step, on the rooster's leg, during a turn's ramp, inside each gag,
   with the bird on his hand): for the 85 ticks his joints do not move except the head; his facing
   does not change; the bird stays on the hand; afterwards the beat ends with zero overruns and the
   same end state.
6. Guests: the bird's feet are within 2 px of the hook hand every tick it sits; the chipmunk is up
   when he is up and down within 1.5 s of the snake; the deer never comes within 150 px; with a dog
   out no guest spawns; a dog going out mid-form sends them off within 4 s.
7. Lucy: with dog_out 1 she is lying within 6 px of the watch spot from 12 s into the beat to its
   end. Copper: with dog_out 2 or 3 and gag 3 the bump plays and he is given back; if `dog_out` drops
   to 0 before the cue, it does not, and the beat still ends clean.
8. Numbers printed: `sizeof(DayTaichi)`, `sizeof(DayBike)`, `sizeof(App)` (budget 32,768; PC wire
   30,000), ms a frame, strips a frame.

---

## 12. Stills to render for review

`tools/taichi_stills.cpp` (the `plink_stills` pattern) into
`docs/screensaver/reviews/stage22-taichi/`, with `taichi_sheet.py` for the contact sheets:

- **S1, the posture sheet**: every named form's key (27 of them) on a flat pale ground, 2x, labelled,
  hat off: this is where Mike says which shapes read and which do not.
- **S2, four filmstrips**: one chapter each, a frame every 2.0 s (about 55 frames, 8 columns), in the
  scene, in the lifting fog.
- **S3, the guests**: the crane with the heron mirroring; the single whip with the bird on the hook
  hand; the rooster with the chipmunk up, and the snake with it down; the kick with the deer beyond
  the pine; the closing with the butterfly on his shoulder.
- **S4, the turn**: 16 consecutive ticks round a flip, 3x zoom, with and without `foot_turn`.
- **S5, the tight seed**: the seed with the smallest camp_x - F.x: brush knee at the east end against
  the pine's skirt, and the stillness beside the cairn.
- **S6, feet**: chapter 2 keys 8 to 12 overlaid in one frame (five poses, one pair of feet).
- **S7, the gags**: the wobble on the rooster in 8 frames; the itch in 6; Copper's bump in 10.
- **S8, Lucy watching**, and both dogs.
- **S9, the doorbell** mid-rooster: bell, +1 s, +2 s, +3.4 s.
- **S10, light**: the same form frame in thick fog (first step), thin fog, clear; a WX_FOG day; a
  no-show in rain.
- **S11**, when the reeds piece exists: him behind the cattails.

---

## 13. Cost estimate (all UNVERIFIED until measured; nothing on the panel)

- **Strips a frame**: he covers strips 9 to 11 (8 for the frames with a hand overhead), which he
  dirties in any standing beat (he is marked every tick). Guests: the heron 2 strips (13, 14) while
  it moves, which the stream's glints mostly dirty already; the chipmunk strip 11; the deer strips 9
  and 10; the bird and the butterfly inside his. Expect the major to average 8 to 9, under the 10 the
  tests allow.
- **Primitives**: he is 12 (no pack, no hat: 4 fewer than usual); bird 7, or chipmunk 7, or deer 7;
  butterfly 2; the heron's wings 2 more than it has. Under 30 on top of the scene.
- **Per tick**: two leg solves done twice, two arm solves, six `taiAtan2`, a dozen `sqrtf`, a
  Catmull-Rom on about 10 channels at 4 lags: a few microseconds on the PC. Nothing per pixel.
- **Code**: taichi.cpp about 450 lines, day_taichi.cpp about 600, the heron and dog edits about 80:
  12 to 18 KB of code, 1.4 KB of tables. All of it lands in PSRAM on the panel (646 KB free on
  2026-11-18). The builder reports `size -A` of the two objects as the proxy.
- **sizeof(App)**: the union does not grow (about 132 bytes against the bike's 1,700). +4 for
  `foot_turn`, +0 or +4 for `taichi_next`, +0 or +8 inside the stream's and the visitors' unions.
  Expect at most +8 on about 28.6 KB of 32 KB (30,000 on the PC wire: check, that one is closer).
- **Palette rebuilds**: none added. The fog's steps are the morning's own and their count does not
  change; they are spread over a longer time.
- **Loop RNG**: no new draws. The morning's plans change (a new pool entry, the counts-as-two rule,
  the first-morning rule); the evening's and the midday's do not.

---

## 14. Risks

1. **It may read as stretching, not tai chi.** At 90 px the hands are 5 px capsules and half the
   forms are told apart by the hands. The posture sheet (S1) is the gate: build the module and S1
   first, show Mike, cut or swap forms before the major is written. The strongest shapes (crane,
   single whip, snake, rooster, kick, needle) carry it; the weakest (ward off against press, the
   ball) lean on the weight shift.
2. **Foot contact.** The rig pitches each foot with its shin; the second IK pass is meant to keep both
   feet on the line. If a planted foot still swims by a pixel, the fallback is a rig flag that keeps a
   planted foot flat (one more small rig change). Test 11.1.2 is the tripwire.
3. **Shared files.** `majors.cpp`, `majors.h`, `day.h`, `day.cpp`, `react.cpp`, `memory.h`,
   `spine.cpp` (the fit) and above all `stream.cpp` and `hiker.cpp` are wanted by more than one of the
   five pieces. One integrator, one order of ids, the stream edit last.
4. **The fit change.** `floor_n` and counts-as-two touch the loop-length policy for mornings that
   hold tai chi. Other mornings are untouched by construction, but the morning pins move and must be
   re-captured on purpose; if any morning test asserts "2 to 4 majors" (not found in test_spines by
   grep, UNVERIFIED for test_morning_stay), it needs "1 to 4 with tai chi".
5. **The first-morning rule** depends on `Memory::last_family` being last loop's at plan time
   (UNVERIFIED). If it is not, use `loop_count <= 1` alone and the weight.
6. **The pine.** In the tightest seeds his push at the east end comes within a few pixels of the
   camp pine's lowest foliage; dark on dark does not read. `g` handles it; S5 checks it.
7. **The heron's wings** depend on drawing code not read. If half-open wings standing look wrong, the
   mirror is the lifted leg and the long neck alone, which the heron can already do.
8. **The morning gets crowded.** A camp-stays morning with tai chi has room for little else. If Mike
   misses the fillers, shorten nothing: lower `kTaichiW`, or keep the first-morning rule alone.
9. **Frame rate on the panel**: no new cost in kind (he is redrawn every tick in every beat), but 150 s
   of him plus a guest is a long steady load; measure draw time on the next flash, as always.
