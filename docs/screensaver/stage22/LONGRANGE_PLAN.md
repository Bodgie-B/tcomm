# Long-range shooting at steel on the mountainsides: design and build plan

Written 2026-11-18 from Mike's request of the same day. Design only: nothing here is built. Every
number marked PROPOSED is a starting value to tune with the stills. Everything about how it looks is
UNVERIFIED until Mike has seen it, and on the panel. References like `plink.cpp:413` are lines on
2026-11-18 in `screensaver/core/scene/`; they drift, search for the name.

The major's id is `MJ_LONGSHOT`, its plan name `"longshot"` (no other major's name contains it or is
contained in it; "longrange" was avoided because `ranger_ridge` shares "range" and tests match names
with strstr). Files are `day_longshot.h/.cpp`.

## 1. Mike's words

- 2026-11-18: "long range shooting with target out on the mountain sides and far trees".
- For all five new pieces: "Consider other interactions with animals and these from other vignettes."
- Standing taste: natural, classic, unhurried. "When something feels rushed, add stillness, not more
  motion." No ambient sound, ever. No shooting with Lucy or Copper about (2026-11-17, the plinking).
- Offered earlier and not rejected: misses show a dust puff and a visible flight delay, with a wind
  flag.

## 2. Proposed answers to the open choices (ask Mike at review, build these meanwhile)

| Choice | Proposed default | Alternative (one constant or flag) |
|---|---|---|
| How the targets get out there | They are his standing range: he set them on the day hike the midday loop begins with. They are not drawn until he shades his eyes and picks them out one by one (each shows with a small glint, nearest first). | He walks out of frame to set them (adds about 25 s of empty camp; not proposed) |
| What happens to them after | They stay out there, still and tiny, for the rest of the loop (`Glyph::keep`); a still glyph costs nothing | Cleared when the major ends (`kLsKeepSteel = false`) |
| Firing position | PRONE on the open grass at the valley view spot (where echo and ranger_ridge look out from), rifle rested on a stuffed shooting bag | Kneeling as in the plinking, no bag (`kLsProne = false`): no new pose at all |
| The rest | A shooting bag (the plinking's sack drawn 1.7 times bigger) carried from the pack | The pack itself carried over and laid down (adds about 18 s) |
| Spotting | Hand to the hat brim before he starts (POSE_LOOK_AROUND); after each shot he stays on the scope and watches | Binoculars (a new held prop; not proposed) |
| The rifle | The plinking's takedown .22 with a small scope on it (one extra capsule) | No scope (`scope = 0`) |
| Wind flag | A 20 px stick with a red ribbon (MUSHROOM slot) planted ahead of him, driven by the scene's own wind | No flag (`kLsFlag = false`; variant B then never plays) |
| Where he shoots | To the right, past the camp pine, at the ridges right of it (x 620 to 790) | See risk 1: the aim line crosses the pine's lowest boughs on screen |
| Echo | Small sound arcs (the existing GL_ARCS glyph, as echo and the thunder use) come back from the hill: once for the first report, and once for each gong that rings | No arcs (`kLsArcs = false`) |
| Weight in the midday pool | 0.6 (as long as the plinking) | 0.5 to 0.8, tuned against test_dog and test_memory_sets |
| Same loop as the plinking | Never both in one loop | Allowed |

## 3. Decision: share, do not copy

The rifle is the plinking's rifle. The long shot runs with `w.day.kind = DAY_PLINK` and uses
`w.day.plink.p` (PlinkState) for everything in his hands. So these work with NO edit:

- `day.cpp` `dayPose` (plinkBend), `dayStep` (plinkFace, plinkStep, plinkFreeze on a doorbell),
  `dayTrack` (plinkDirty), `dayPartY`, `dayPartDraw`, `dayHoldsFacing` (verified: all switch on
  `DAY_PLINK` only, `day.cpp:107-320`).
- `react.cpp:36,49,50`: `plinkRifleInHands(w)` and `plinkHandsFullNow(w)` test `w.day.kind ==
  DAY_PLINK` only (verified `day_plink.cpp:529-530`), so the doorbell is look-only with the rifle out
  and the passer-by wave is refused, for free.
- Reused as they are: `plinkRifle`, `plinkMag`, `plinkLoad`, `plinkUnload`, `plinkKneel`, `plinkSack`,
  `plinkLay`, `plinkQueue`, `plinkQueueClear`, `plinkStart`, `plinkStringDone`, `plinkTakeEvents`,
  `plinkMuzzle`, `plinkAxis`, `plinkStep`, `plinkBend`, the rifle draw (`buildRifle`), flash and puff
  (`buildFlash`), recoil, the two-values-a-shot Rng rule.
- Reused from `day_plink.cpp` by moving them out of its anonymous namespace and declaring them in
  `day_plink.h` (no change in behaviour): `actRifleOut` becomes `plinkActRifleOut`, `actAssemble`,
  `actLoad`, `actUnload`, `actStow`, and `startle` becomes `plinkStartle(World&)`. The stick and sack
  lines inside actRifleOut and actStow are already guarded by state (`p.stick != PI_DOWN`,
  `p.stick == PI_DOWN`), so with no stick they do nothing. UNVERIFIED: read each once more when
  moving; if one misbehaves without a stick, copy it instead.

Everything far away is a GLYPH (`MajorState::g`, `majors.h`): bounds, dirty marks, sorting and drawing
already exist (`majorsTrack`, `glyphBounds`, `glyphDraw`), and a glyph that does not change marks
nothing (verified `majors.cpp:4124-4137`). So there is no new draw, bounds or dirty code for the far
side except two small shapes.

New code is therefore: about 130 lines in `plink.cpp`, one new file of about 600 lines, about 40
lines in `majors.cpp`. That is about half of a plinking-sized play.

## 4. Where everything is (frame pixels, 800 x 480)

Examples use the fire F = (250, 398) and camp_x = 560 (F.x 238 to 262, F.y 393 to 403, camp_x 550 to
570).

- **Firing point FP (AT_M0)**, his hips: `(F.x + 86, F.y - 34)` = (336, 364), y through
  `scriptGroundAt`. This is `camp.view` moved 6 px left (`spine.cpp:2864`), the spot echo and
  ranger_ridge look out from. Facing right. Prone, his feet reach back to about x 294 and his head
  is at about x 372; the reading rock (x 256 to 280), the sit spot (290, 392) and the tent (to x 244)
  are clear of him.
- **Bag place (AT_M1)**: `(FP.x + 56, FP.y + 1)` = (392, 365). The bag is 20 x 13 px, its top at
  about FP.y - 14. He stands 16 px left of it to lay it down. The cairn (about (400, 395), top about
  y 375) is 10 px clear below.
- **Flag (AT_M2)**: `(FP.x + 112, FP.y + 2)` = (448, 366), ground through `scriptGroundAt`. Stick top
  at y 346. He stands 14 px left of it to plant it. The stream's upper edge at x 448 is about y 396.
- **The rifle, prone and shouldered**: grip at about (FP.x + 40, FP.y - 12), butt (FP.x + 30,
  FP.y - 10), muzzle about (FP.x + 78, FP.y - 23) = (414, 341) at 15 degrees up. The forend crosses
  the bag at FP.x + 56. The flag's top is 14 px under the line of the barrel.
- **Target window**: x from `camp_x + 62` to 790 (right of the camp pine's foliage, which is at most
  60 px half wide, `landscape.cpp:606`), the ridges and far trees, y 214 to 314. From the muzzle the
  shots are 11 to 20 degrees above level (clamp in `plinkStep` is -0.4 to 0.9 rad, no change).
- **Pack**: stays at `camp.pack` (F.x - 74, 359); he works at `camp.pack_work` facing left, as the
  plinking does.
- **Chip box** x 538..796, y 412..476 and the quiet corner: nothing of this piece goes below y 384
  near him, and the far things are all above y 316. Clear.

### 4.1 The targets: where they sit, from the real terrain

A pure function of the Landscape, no RNG, nothing stored in the landscape:
`int longshotTargets(const Landscape& ls, FarTarget out[4])`. Helpers:

- `toothTop(x)`: find spike i with `spikes[i].x <= x < spikes[i+1].x`, `xm` its middle;
  `landscapeCrestAt(ls, 2, x) - spikes[i].h * (1 - |x - xm| / (0.5 * width))` (the drawn tooth,
  `drawFarTrees`, `landscape.cpp:507`).
- `skyline(x) = min(crest0(x), crest1(x), toothTop(x))`.
- `pineOver(x, y)`: true when a mid pine covers the point: `|p.x - x| < 0.5 * 0.01 * width_pct * h + 6`
  and `p.base_y - p.h < y + 4` (the test `ridgeSpot` uses, `majors.cpp:524-528`).

Scan x in the window in 4 px steps. Targets are at least 28 px apart in x (PROPOSED).

| # | What | Where | Drawn as | Flight |
|---|---|---|---|---|
| T0 | a steel plate hung in a far tree | the tallest tooth (h >= 14) not under a mid pine: `(xm, crest2(xm) - 0.55 * h)` | GL_DOT, STAR, r 1.7 | 0.45 s |
| T1 | a painted rock on the middle ridge | the x where `g1 = toothTop(x) - crest1(x)` is largest, g1 >= 9: `(x, crest1(x) + min(6, g1 - 4))` | GL_DOT, STAR, r 1.5 | 0.65 s |
| T1 fallback | a pine cone at the top of a mid pine | when no x has g1 >= 9: the tallest mid pine in the window, `(p.x, p.base_y - p.h + 4)` | GL_DOT, TRUNK, r 1.6 | 0.55 s |
| T2 | a plate on a post on the far mountainside | the x where `g0 = min(crest1(x), toothTop(x)) - crest0(x)` is largest, g0 >= 8: `(x, crest0(x) + min(5, g0 - 3))` | SH_GONG, scale 0.7 | 0.9 s |
| T3 | the gong on the skyline | the x with the smallest `skyline(x)`, not `pineOver`: `(x, skyline(x))` | SH_GONG, scale 1 (if the skyline there is a tooth: GL_DOT r 1.7 at the tooth top + 2) | 1.2 s on the far ridge, 1.0 middle, 0.7 a tooth |

T0 and T3 always exist. T1 falls back as shown; if there is no mid pine either, it is the second
tallest tooth. T2 is dropped when the far ridge does not show (n = 3). If T2 and T3 want the same
place, T3 keeps it and T2 takes its next best spot 28 px away or is dropped. The shot tables name
targets as nearest, second, third, farthest and clamp to what exists. Flight seconds are PROPOSED.

Why this sits on the terrain for every seed: each position is computed from the same crest samples
and teeth the cache is drawn from, and the test (7.1) renders the static landscape for 1200 seeds and
reads the pixel under each target.

### 4.2 The two new shapes and the far effects

Shapes are drawn by `longshotShapeDraw` in `day_longshot.cpp`, called from `drawShape` the way
`SH_STICK` calls `fetchStickDraw` (`majors.cpp:397`).

- **SH_GONG** (pos: the foot on the ground; 7 px tall at scale 1; reach 7): two legs, ACTOR capsules
  0.9 px, (-2.5, 0) to (-1.5, -7) and (2.5, 0) to (1.5, -7); a top bar; the plate a STAR circle r 1.9
  at `(2.2 * sin(k), -3.6)`, `k` its swing in radians. While `t > 0` (the glint, 3 ticks) the plate
  is ACTOR_LIGHT, r 2.6. 4 primitives.
- **SH_FLAG** (pos: the stick's foot, or his far hand while carried; reach 26): a TRUNK capsule 1.6 px
  up 20 px; a ribbon of 3 capsules 1.4 px, 4.5 px each, in the glyph's slot (MUSHROOM: red). Segment
  i hangs at `a_i = 0.15 + 1.25 * s + (0.10 + 0.25 * s) * sinT(t + 0.22 * i)` rad from straight down
  toward -x (the way the clouds and smoke go, `weather.h:61`). `k` is the breeze s, `t` the flutter
  phase in turns, advanced by `(1.2 + 2.5 * s) * dt`. 4 primitives.
- **The breeze**: `s = clamp01(0.18 + 0.42 * (weatherSwayMul(w) - 1))` (`weather.cpp:779`: exactly 1
  when calm, up to 2.2 in a gust). Calm 0.18 (the ribbon hangs and stirs), a full gust 0.68 (it
  streams). There is no wind speed in the OUTSIDE reading (`house_weather.h:33`), so the scene's own
  wind is the real input: a wind from the data (code 3) or the loop's random weather and gusts. If
  the reeds and cattails piece adds a shared `weatherBreeze(w)`, both must call the same function so
  the ribbon and the cattails lean together; whoever builds first puts it in `weather.cpp`.
- **Far effects, all glyphs**: a hit on a dot target moves the dot +-1.2 px sideways (2.2 Hz, dying
  over 1.2 s) and shows a glint (a second GL_DOT, ACTOR_LIGHT, r 2.5, 3 ticks). A hit on a gong
  swings `k` +-0.6 rad at 2.2 Hz dying over 1.6 s (4 s in C's last shot), with the glint. A miss is a
  dust puff: GL_DOT in SMOKE, r 1.5 growing to 3.5 over 0.8 s, drifting downwind `8 * s` px/s. The
  cone fallback drops the dot 0.8 s down the pine (220 px/s^2) and removes it. Far birds: up to 3
  SH_PERCH glyphs (`k` 1, wings; scale 0.5 shrinking to 0.3; flap 3.5 Hz) rising from the tooth tops
  beside T0 at 26 px/s outward and 16 px/s up, gone after 3.2 s. The ring coming back: one GL_ARCS
  glyph leaving the target toward FP, scale 5 to 11, thickness 1.4 fading, 46 px of travel in 1.1 s
  (so it stays in one or two strips).

## 5. Choreography, beat by beat (variant A, the clean string)

Starts wherever the last beat left him (normally sitting at camp.sit) and ends sitting there, hat
on, hands empty, the day union zero. Speeds: 40 px/s for the long walks, 34 short. Seconds are
PROPOSED script seconds.

**Reading the range (5 s)**
1. `pose(POSE_STAND, 0.8, 0.3)`, `face(RIGHT)`.
2. `pose(POSE_LOOK_AROUND, 0.6, 3.4)` with `doit(cueGlass)`: the hand goes to the hat brim. The tick
   shows the targets nearest first at 0.9, 1.5, 2.1, 2.7 s, each with its 3-tick glint, and his head
   tips up a little more for each (`fx.head` -0.03 a target). Nuance: he holds on the farthest one a
   beat longer than the others before the hand comes down.

**The kit (about 12 s)**
3. `go(AT_PACK_WORK, 40)`, `face(LEFT)`, `act(plinkActRifleOut, 2.0)` (the two halves, pointing at
   the ground; the scope shows on the stock half), `pose(POSE_STAND, 0.6, 0.2)`,
   `act(plinkActAssemble, 2.0)`: one rifle in the low carry.
4. `act(actKitOut, 1.6)`: a second crouch; at u 0.4 the bag is in his far hand (`plinkSack(PI_HAND)`,
   `sack_big`), at u 0.6 the flag is in the same hand, upright, the ribbon hanging (the flag glyph
   follows `hikerHandPos(h, 1)` while `flag_held`). `pose(POSE_STAND, 0.6, 0.2)`.

**Out to the line (about 14 s)**
5. `go(AT_M2, 40, -14, 0)`: across the camp, about 255 px, rifle low in the near hand, muzzle ahead
   and down (6.4 s). The longest walk of the piece; nothing else moves.
6. `face(RIGHT)`, `act(actPlant, 1.4)`: a crouch, the stick goes in at u 0.5 (the glyph is fixed at
   AT_M2 and sorts at its own base). `pose(POSE_STAND, 0.6, 0.2)`, then `act(actWatchFlag, 1.6)`: he
   stands and watches the ribbon; his head follows it down. This is the first wind reading and it is
   pure stillness.
7. `walk(AT_M1, 34, -16, 0)` (back 40 px to the left), `face(RIGHT)`, `act(actBagDown, 1.2)`: a
   crouch, the bag is laid at AT_M1 (`plinkLay`, `plinkSack(PI_DOWN)`). `pose(POSE_STAND, 0.6, 0.2)`.
8. `walk(AT_M0, 34)` (40 px more to the left), `face(RIGHT)`, `until(dogsClear)`.

**Down and loaded (about 9 s)**
9. `act(actDown, 2.5)`: `plinkRest(P, REST_PRONE)` and `plinkKneel(P, true)`. The bend takes him
   down in three stages (section 5.2): onto one knee, forward onto his hands, the legs slid back. The
   rifle comes level onto the bag as he goes.
10. `act(plinkActLoad, 2.2)`: magazine from the hip pocket, in, one tug of the handle. LOADED only
    now: prone, facing downrange, the flag and the bag already out.
11. `act(actSettle, 2.6)`: elbows shuffle once (`fx.lean` +-0.02, one cycle), the near heel that was
    up drops flat (the bend's `settle` weight), cheek down on the stock. `still(1.0)`.

**The string (A: about 33 s)**: section 5.1.

**Unload and up (about 6 s)**
12. `still(1.2)`, `act(plinkActUnload, 2.2)`: magazine out and pocketed, the handle held back, a look
    into the action. UNLOADED before anything else moves.
13. `act(actUp, 2.3)`: `plinkKneel(P, false)`: back through all fours and one knee to standing, the
    rifle into the low carry.

**Clearing the line (about 16 s)**
14. `walk(AT_M1, 34, -16, 0)`, `act(actBagUp, 1.2)`, `pose(POSE_STAND, 0.5, 0.1)`.
15. `walk(AT_M2, 34, -14, 0)`, `act(actPull, 1.2)`: the flag back in his far hand with the bag.
    `pose(POSE_STAND, 0.5, 0.1)`. A last look at the hills: `act(actLookBack, 1.2)` (head up, no arm).
16. `go(AT_PACK_WORK, 40)` (6.4 s), `face(LEFT)`, `act(actStowAll, 2.8)`: a crouch; at u 0.2 the
    bag and the flag go into the pack (the glyph ends, `plinkSack(PI_NONE)`), then the rifle comes
    apart and both halves go in (the same arm work as `plinkActStow`). `pose(POSE_STAND, 0.6, 0.2)`.
17. `until(crittersGone)`, `go(AT_SIT, 34)`, `face(TO_FIRE)`, `pose(POSE_SIT, 0.9, 0.6)`.

**Totals** (script; beat seconds PROPOSED with 4 s of slack): A about 98 s, beat **102**. B about
110 s, beat **114**. C about 115 s, beat **119**. The fog no-show 30, the rain no-show 46.

### 5.1 One shot, and the strings

The script's loop (11 steps back from the repeat; the builder checks the count against the
plinking's `repeatWhile(morePlaces, 4)`):

```
doit(doShotBegin),                     // entry m.count of the variant's table into FarState
skipUnless(preFlag, 1), act(actReadFlag, 2.4),
skipUnless(preDial, 1), act(actDial, 2.2),
doit(doFire),                          // plinkFar(P, aim, flight, hold); plinkStart(P)
until(shotLanded),                     // the tick saw PEV_FAR and began the result (late: PEV_SHOT)
act(actWatch, 1.3),
act(actReact, 1.8),
doit(doShotEnd), still(0.9),
repeatWhile(moreShots, 11)
```

What one shot looks like:

1. **Swing** (0.4 s, the module's; 0.8 s raise for the first): the muzzle moves a few px to the next
   target.
2. **The breath** (the hold: 3.8 s the first shot, 2.8 to 3.3 s after, 4.6 s for C's last; plus up to
   0.5 s from the Rng). The tick sets `hiker.breath_scale` to 2.0 for the first 70 % (two slow deep
   breaths; his hands and so the muzzle rise and fall about 2 px, `hikerHandPos` includes breathing)
   and to 0 for the last 30 %: everything about him stops. This is the stillness the piece is built
   round.
3. **The shot**: the module's flash (2 ticks), puff and recoil. The camp startle on the first shot
   only (`plinkStartle`: two birds from the camp pine, a squirrel off to the right).
4. **The flight**: 0.45 to 1.2 s in which nothing at all happens. He does not move.
5. **Out there**: the hit or the miss (4.2), in an upper strip, small and short.
6. **Watching** (`actWatch`, 1.3 s): he stays on the scope.
7. **The reaction** (`actReact`, 1.8 s; all small):

| result | what he does |
|---|---|
| hit, not the last | a 1 px nod on the stock (`fx.head` +0.06 for 0.4 s), nothing else |
| hit, the last | the cheek comes off the stock, the head up, 1.0 s looking; then the quiet fist: the far hand leaves the grip, the forearm tips up 20 degrees and comes down 3 px once (0.5 s), and goes back |
| miss in the wind, first | the head lifts 0.1 and settles back |
| miss in the wind, second | a slow head shake, +-0.08, two cycles |
| miss over the top (no puff at all) | the head lifts, tilts, holds 1.2 s: where did that go |
| miss low | the shoulders drop a little (`fx.lean` +0.02): a breath out |

8. **Reading the flag** (`actReadFlag`, before a shot): the head lifts off the scope
   (`plinkHeadOff`), turns down toward the ribbon, holds 1.4 s, goes back.
9. **Dialing** (`actDial`): the far hand to the scope, two small turns of the wrist (the arm work of
   the plinking's SK_CHECK), back to the grip.

The strings (the plan's variant byte, `longshotPlanVar(r) = (r >> 13) % 3`):

- **A, walking them out** (4 rounds): nearest hit (the far birds lift from the tree line), second
  hit, third hit (the ring comes back), farthest hit (the ring comes back; the quiet fist). The echo
  of the first report: one larger set of arcs (scale 8 to 15, 1.3 s) from the middle of the far ridge
  1.6 s after the first shot.
- **B, the wind beats him** (5 rounds): nearest hit; second: a miss, the puff `4 + 9 * s` px
  downwind (left) of it; second again: a miss 60 % as far left (he held off, not enough), the head
  shake; he reads the flag, dials; second: hit; third: hit, with no pre-gesture: he has the wind now.
  B plays only when the scene is windy (weather kind WX_WIND, or a gust extra): the begin hook turns
  B into A when it is calm, and A into B when the weather kind is WX_WIND, with no RNG (the way the
  plinking turns C into A without a hung bag, `day_plink.cpp:490`). So the ribbon is never limp
  while the story says wind.
- **C, the far one** (5 rounds): nearest hit; third hit; farthest: a miss over the top, nothing to
  see, the puzzled tilt; he dials; farthest: a miss low, a puff 7 px under it; he reads the flag,
  the long breath (4.6 s); farthest: `shotLanded` is true at the shot for this one. He comes off the
  scope after 0.5 s sure he pulled it, shakes his head once and reaches for the handle. At 1.2 s the
  gong, tiny, swings and glints while he is looking down. 1.2 s later the arcs come back off the
  hill; when they fade the tick raises `heard`. Script: `skipUnless(isLate, 2), until(heard),
  act(actLookUp, 1.6)`: his head comes up fast (0.25 s), holds, and then the quiet fist. The gong is
  still swinging a little (4 s).

Rounds 4, 5, 5 of the magazine's 8.

### 5.2 The prone position (the bend, in `plinkBend`)

`rest == REST_PRONE` splits the existing kneel weight `k` (it then ramps over 2.4 s, not 0.8) into
three key poses; angles as `hiker.h` (from straight down, + forward; shin = thigh - knee):

| k | pose | lean | thigh near, far | knee near, far | head |
|---|---|---|---|---|---|
| 0.33 | one knee (the plinking's kneel) | 0.10 | 1.6, 0.12 | 1.6, 1.95 | as it was |
| 0.62 | all fours | 1.05 | 0.35, 0.20 | 1.92, 1.95 | -0.60 |
| 1.0 | prone | 1.38 | -1.20, -1.45 | 0.45, 0.05 | -0.95 |

The legs go back by way of the knees (the knee is the lowest contact all the way down), never by
swinging a straight leg through vertical, which would lift him 20 px (the rig grounds itself on the
lowest contact, `hiker.h:14-17`). The near leg stays a little cocked, heel up; `actSettle` lowers its
knee bend from 0.45 to 0.10. Prone and not shouldered, the arms are forearms forward on the ground
(shoulder 1.2, elbow 0.6) and the rifle's carry elevation is `lerp(kPlinkCarryElev, +0.10,
smoothstep(k, 0.45, 0.9))`: level on the bag, never muzzle into the ground. Shouldered, the arms are
solved as now from the shoulder to the grip and forend (`plink.cpp:1155-1169`; the offsets are in
world axes, so they hold for a lying torso). Head on the scope -0.80, off it -1.10 (`head_off`).
Getting up is the same in reverse.

If the stills show the rig cannot do this well, `kLsProne = false` uses the plinking's kneel with no
other change (the bag is then not carried).

### 5.3 Safe handling the animation shows (the tests check 1 to 4)

1. Halves out and together pointing at the ground; the walk in the low carry, muzzle ahead and down.
2. The flag is planted and the bag laid BEFORE the magazine goes in; they are fetched AFTER the
   unload. Nobody is ever ahead of a loaded muzzle.
3. Loaded only prone at FP, facing right; the axis always within 0.5 rad of level while loaded.
4. Unloaded, looked into, and only then up.
5. No dog in camp, no shooting in fog (he cannot see his targets), never at night.

## 6. Interactions

- **Camp animals**: the first shot runs `plinkStartle` (birds from the camp pine, the squirrel) and
  `directorScatter`. The stream's heron or deer has already left calmly when the major's still began
  (`stream.h`), about 40 s before the first shot. The mail bird is brave and stays.
- **Far animals**: the far birds of 4.2 lift at the first hit in the tree line. They are glyphs, not
  critter slots, so they cost no director slot and no brave/shy bit.
- **House dogs**: plan weight 0 when `in.dog_out != 0`. A dog that goes out mid-major waits:
  `plinkHoldsDogs` (`day_plink.cpp:528`) gets `|| w.major.id == MJ_LONGSHOT`; `housedogs.cpp:87`
  already calls it. `until(dogsClear)` before he goes down.
- **Weather**: weight 0 at night and in a rain, snow, storm or FOG from the data. A random rain, snow
  or storm found at begin plays `dayNoShowScript` (46 s); a random fog plays the fog no-show: stand,
  hand to the brim 3 s (no target shows), a slow head shake, sit (30 s). Wind picks variant B. Heat
  and chill: no change.
- **Doorbell**: the story freezes as for the plinking. Look-only while the rifle is out (prone too),
  hands_full with the bag or flag. A muzzle flash is switched off by `plinkFreeze`. A far glint or a
  bird that is showing when the bell rings stands still for 3.4 s (the glyphs are moved by the
  major's tick, which is frozen); they are 2 to 5 px, accepted. The ribbon also stands still for the
  3.4 s while the grass moves; accepted, noted for Mike.
- **Status chip and quiet corner**: nothing near them (section 4).
- **Plinking**: never both in one loop (in the pick loop of `majorsPlan`, beside the fishing rule,
  `majors.cpp:3984`: `if (isShooting(pick[j]) && isShooting(i)) wt[i] = 0`). No extra RNG draw.
- **ranger_ridge**: PROPOSED the same rule (nobody shoots at a hillside a ranger walked minutes
  ago). The steel left out there does not bother echo or ranger_ridge later in the loop.
- **The other four new pieces**:
  - Reeds and cattails: one shared breeze function (4.2), so ribbon, cattails and autumn fluff agree
    on direction and strength; a miss in the wind drifts the same way the fluff does. FP, bag and
    flag are 14 px or more above the bank, clear of the reeds. If he has a reed in his mouth when the
    major begins, he keeps it (its owner draws it from his head, so it follows him prone) unless that
    piece only supports upright poses, in which case the begin hook asks it to drop it.
  - Mushrooms: never the rifle and the mushroom trip in one loop (the same `isShooting` style rule,
    owned by whichever lands second). At begin, if `ls.mush_x` or a grown mushroom is within 20 px of
    AT_M1 or AT_M2, that point moves 24 px right.
  - Deer visit: it needs him away, so it never overlaps. PROPOSED for the deer plan: when longshot
    is in this loop's ring already, the deer come in more warily. Nothing to build here.
  - Tai Chi: morning family, no overlap. Both pieces use `hiker.breath_rate` and `breath_scale` for
    slow breathing; both must put them back to 1.

## 7. Data added

```
// plink.h, PlinkState: new fields (the bytes come out of pad8[2] and pad3[3], plus 20)
uint8_t stay;        // the rifle stays shouldered when the queue ends (PP_IDLE, up 1), not lowered
uint8_t rest;        // PlinkRest: REST_KNEEL 0 (as now), REST_PRONE 1
uint8_t scope;       // a scope on the stock half (1 capsule)
uint8_t sack_big;    // the sack drawn 1.7 times bigger: the shooting bag
Vec2 far_aim;        // SK_FAR: what he aims at
float far_flight;    // SK_FAR: the bullet's flight, s (instead of kBulletS)
float far_hold;      // SK_FAR: the hold, s (plus 0.5 * r1)
float head_off;      // 0..1: the head off the scope
// enums: SK_FAR after SK_SCRATCH (isGesture becomes k >= SK_CHECK && k <= SK_SCRATCH);
//        PEV_FAR = 16; enum PlinkRest.

// day.h
struct FarTarget {            // 24 bytes
  Vec2 pos;
  uint8_t kind, layer, glyph, shown;   // FT_TREE, FT_ROCK, FT_CONE, FT_POST, FT_SKY; glyph index + 1
  float flight, sw_t, sw_amp;          // seconds; the swing's clock and size
};
struct FarState {             // about 140 bytes
  FarTarget tg[4];
  uint8_t n, shot, result, pre;        // targets; the table entry; FarResult; FarPre bits
  uint8_t tstage, late, flag_held, first_done;
  float st, breeze, hit_t, echo_t;     // seconds in the stage; the ribbon's s; clocks (MajorState::t)
  uint8_t g_flag, g_puff, g_glint, g_arcs, g_drop, g_bird[3];   // glyph index + 1, 0 none
};
struct DayPlink { PlinkState p; Rng rng; FarState far; };   // far is all zero in the plinking
```

Budget: `static_assert(sizeof(DayPlink) <= sizeof(DayBike))` stays (`day.h:83`). The plan of
2026-11-17 put DayPlink at about 720 bytes against more than 1,700 for DayBike; with about 160 more
it still fits (the exact sizes are UNVERIFIED; the assert decides). DayState, World, WorldDirty and
App do not grow. MajorState is used as others use it: `p[0]` FP, `p[1]` the bag, `p[2]` the flag,
`p[3]` free, `c[0..2]` the startle's critters, `count` the shot, cue bits `kGo` (glass), `kE1` (the
result began), `kE2` (heard). Glyphs: 4 targets (kept), flag, puff, glint, arcs, drop, 3 birds: 12
of GLYPH_MAX 28. No new palette slot, no Memory field (no pinned memory string changes for that
reason), no new critter kind.

## 8. Gating, families, slots, weight, loop fit

- Id: the next free MajorId, before MJ_COUNT (25 are used, cap 32; the five new pieces must be given
  their ids in one order by the lead). Pool row: `{"longshot", {102, 102}, kLongshotW, kOut, kMid,
  longshotMajorBegin, longshotMajorTick, {nullptr, nullptr}, {0, 0}}`, `kLongshotW = 0.6f`.
- Midday only. `majorsPlan`: add `base[MJ_LONGSHOT] = 0` to the night and wet line
  (`majors.cpp:3939`), a fog line (`wk == WX_FOG`), the dog line (`majors.cpp:3942`), the pick-loop
  exclusion, and `if (id == MJ_LONGSHOT) v = longshotPlanVar(r_var);`. The number of draws does not
  change.
- `scriptOf`, `majorSeconds`, `majorEnd` get their `MJ_LONGSHOT` lines beside the plinking's.
- Loop fit: 102 to 119 s is the plinking's size; `spineFitLength` will drop it about as often. No
  change to the fit.

## 9. File plan

New files (LF, ASCII):
- `screensaver/core/scene/day_longshot.h`, `day_longshot.cpp`: `longshotTargets`, `longshotPlanVar`,
  `longshotSeconds`, `longshotScript`, `longshotMajorBegin/Tick/End`, the acts, the shot tables, the
  result and glyph work, `longshotShapeDraw`, `longshotShapeReach`, the fog no-show script.
- `screensaver/tests/test_longshot.cpp`.
- `screensaver/tools/longshot_stills.cpp`, `screensaver/tools/longshot_sheet.py` (copies of
  `plink_stills.cpp` and `plink_sheet.py`).

Existing files to edit (and no others):
- `core/scene/plink.h`, `plink.cpp` (LF): the fields and enums of section 7; `plinkFar(PlinkState&,
  Vec2 aim, float flight_s, float hold_s)` (sets the far fields and queues one SK_FAR),
  `plinkStay`, `plinkRest`, `plinkScope`, `plinkHeadOff`, `plinkSackBig`; `beginEntry` and
  `stepMachine` PP_RAISE (aim at `far_aim`, the hold from `far_hold`); `fire` (`bullet_t =
  far_flight`); `bulletArrives` (`case SK_FAR: events |= PEV_FAR`); `nextEntry` (with `stay`: PP_IDLE,
  `run = 0`, PEV_DONE, `up` kept); `plinkStep` (the kneel ramp 2.4 s and the carry elevation when
  prone); `plinkBend` (section 5.2); `buildRifle` (the scope); `buildHeld` and `buildLying` (the big
  sack); `isGesture`.
- `core/scene/day_plink.h`, `day_plink.cpp` (LF): the five acts and `startle` made public (section
  3); `plinkHoldsDogs` also true for MJ_LONGSHOT.
- `core/scene/day.h` (LF): FarTarget, FarState, DayPlink's new member, the comment block.
- `core/scene/majors.h` (CRLF: keep it): `MJ_LONGSHOT`; `SH_GONG`, `SH_FLAG` before SH_COUNT.
- `core/scene/majors.cpp` (LF): the include, `kLongshotW`, the pool row, the static_assert (count),
  `scriptOf`, `majorSeconds`, `majorEnd`, the `majorsPlan` lines of section 8, `shapeReach` and
  `drawShape` cases calling `longshotShapeReach` and `longshotShapeDraw`.
- `tests/test_spines.cpp` (LF), `tests/test_midday.cpp` (CRLF: keep it): `"longshot"` in each
  `isMajor` list; the pinned MIDDAY lines in test_midday.cpp re-captured only after checking every
  difference is the new pool entry (HOW_TO_ADD_SCENES gotcha 4). Evening and morning pins must not
  move (weight 0 there).
- `tests/test_plink.cpp`, `tests/test_day_plink.cpp`: no edits expected; they must pass unchanged.
- `docs/screensaver/NOTES_FOR_MIKE.md`, `docs/screensaver/WORKLOG.md`.

Not edited: `day.cpp`, `react.cpp`, `housedogs.cpp`, `world.*`, `app.cpp`, `landscape.*`, `hiker.*`,
`props.*`, `weather.*` (unless the shared breeze function goes there, 4.2), anything under
`firmware/`.

## 10. Tests

### 10.1 `tests/test_longshot.cpp`

- **Targets on the terrain** (1200 seeds, landscape only): 2 to 4 targets; each inside the window,
  y 200 to 316; 28 px apart; none inside `landscapeCampPineBounds`; rendering the static landscape,
  the pixel 2 px under each target's foot is the colour of the layer it claims (RIDGE_FAR, RIDGE_MID,
  TREES_FAR or TREES_MID); flights increase with the order. Print how often T1 falls back and T2 is
  dropped.
- **Module** (PlinkState alone): an SK_FAR shot aims within 2 px of `far_aim` at the shot tick; PEV_FAR
  comes `far_flight` after PEV_SHOT (+-1 tick); with `stay` the rifle is still shouldered after
  PEV_DONE; two Rng values a shot; the prone bend at k 0, 0.33, 0.62, 1 keeps the lowest contact on
  feet.y and the hip height never rises more than 6 px above its kneel height on the way down;
  bounds hold every draw (scope, big sack, both facings); `plinkDirty` is 0 on a still tick. The
  plinking's strings give byte-identical states to a build without these changes (compare a
  recorded hash).
- **Plan**: over many seeds longshot is planned only at midday, never at night, in a wet or fog
  reading, with `dog_out != 0`, or in a loop with plink (or ranger_ridge); every variant comes up.
- **Each variant from its seed, rendering every tick**: hiker in frame and out of the quiet corner;
  nothing of the piece in the chip box; the script done 3 s before the beat ends; no overrun; end
  state (sitting at camp.sit, hat on, carry none, `day.kind == DAY_NONE`, `breath_scale == 1`, no
  critter, only the target glyphs left and all of them still); strips averaged over the major 10 or
  fewer and never more than 12 in a frame; incremental render equals full render at ticks inside the
  glassing, the walk, going down, a hold, the flash, a flight, a gong swing, a puff, the birds, the
  arcs, getting up.
- **Story**: A: 4 results, all hits. B: only when windy; two puffs left of the target, the second
  nearer. C: no glyph change at all for the over shot; the gong's swing starts 1.2 s after the last
  shot and `heard` comes after it.
- **Safety, every tick**: loaded implies prone at FP, facing right, flag planted, bag down; unloaded
  before `kneel_on` goes false.
- **Dogs**: `dog_out` switched on during the walk out: no house dog in the scene until the major has
  ended.
- **Doorbell** at: the glassing, the flag plant, going down, a hold, a flight, a gong swing, the
  unload: look-only with the rifle out, facing unchanged, and the story's bytes afterwards equal a
  run without the bell shifted by the frozen ticks (`testFreeze` pattern,
  `test_day_majors2.cpp:571`).
- **Weather at begin**: forced rain gives the rain no-show, forced fog the fog no-show (no target
  glyph is made).
- **Determinism**: two runs hash the same frames; SS_XMAS on and off; `sizeof(App)` unchanged.

### 10.2 Checks to run and report

Build folders `screensaver/build-longshot-on` and `-off` (configured like `build-plink-on` and
`-off`); all ctest suites in both; `ss_soak` CLEAN, determinism PASS. Watch, do not "fix" by editing
tests: `test_dog` (fetch in 35 of 150 midday loops), `test_memory_sets`, `test_day_majors` (the kite
count's slack); tune `kLongshotW` and say so. Report sizeof(App), strips per frame over the major,
ms a frame, palette rebuilds (0 expected), overruns, object sizes of the changed files, primitives,
and "unverified on the panel".

## 11. Stills to render for review (`docs/screensaver/reviews/stage22-longshot/`)

Full frames plus 3x crops of the shooter and of the target window:
1. The glassing with all targets shown, for three seeds (a low, a middle and a high far ridge), and
   one seed where T1 is the cone and one where T2 is dropped.
2. **The aim line**: the prone hold on the nearest and on the farthest target with a 1 px line drawn
   by the tool only from the muzzle to the target, to judge how it reads against the camp pine.
3. The walk with rifle, bag and flag; the flag planted, calm and in a gust.
4. Going down at k 0.2, 0.33, 0.5, 0.62, 0.8, 1.0; prone loaded; the settle.
5. A hold at the top and bottom of a deep breath; the flash frame; the empty flight frame.
6. Out there, 3x: the tree plate swinging with its glint and the birds; the rock hit; the post gong
   mid swing; the skyline gong mid swing; a wind puff left of the target; a low puff; the arcs.
7. Reactions: the nod, the head shake, the puzzled tilt, reading the flag, dialing, the quiet fist;
   C's look away with the gong already swinging, then the look up.
8. Unload look; getting up; pulling the flag; the last look back; the final sit with the steel still
   out on the hills.
9. The kneeling fallback (`kLsProne = false`) on one target, for comparison.

## 12. Cost estimate (all UNVERIFIED until built and measured)

- **Strips**: near him everything is in strips 10 to 12, which he keeps dirty anyway. Far effects
  are in strips 6 to 9 (y 192 to 319), normally quiet: one strip for 1.2 to 1.6 s for a hit, one for
  0.8 s for a puff, one or two for 1.1 s for the arcs, two for 3 s for the birds (once). Over a 100 s
  major that is about 25 strip-seconds: about +0.25 strips a frame on average, at most +3 in a
  frame for a few ticks. Still targets and the planted bag mark nothing. Majors must average 10 or
  fewer.
- **Primitives on top of the scene**: rifle 5 (with the scope), bag 1, flag 4, targets up to 10,
  one effect at a time (glint 1, puff 1, arcs 3 polygons, birds 3 x 5): under 40 at the worst tick.
- **Code**: about 130 lines in plink.cpp, 600 in day_longshot.cpp, 40 in majors.cpp: an estimated 13
  to 16 KB of flash, and the same in PSRAM while the screensaver runs (745 KB and 646 KB were free
  on 2026-11-18). The builder reports the host object sizes as the proxy.
- **sizeof(App)**: unchanged (the union does not grow; the asserts decide).
- **Palette rebuilds**: none. No static cache change at all: the targets are dynamic glyphs.

## 13. Risks

1. **The aim line crosses the camp pine on screen.** From the muzzle at about (414, 341) to anything
   right of the pine, the line passes through the bottom 10 to 20 px of the pine's lowest tier (x 515
   to 605). In depth the pine is a few steps away and the hills are far beyond; there is no drawn
   bullet path, and the plinking already shoots into this pine. But Mike reads the screen. Still 2 is
   there to judge it. I found no firing point that avoids it without a steep (over 30 degree) shot or
   a line over the tent's ridge. If it reads badly: restrict the targets to the near tree line (the
   line then stays under the boughs) or move to the plinking's corner, kneeling.
2. **The prone pose is new for the rig** (section 5.2 is arithmetic, not a render). The kneel
   fallback costs one constant.
3. **Tiny targets may not read on the panel** at arm's length: 3 to 7 px. The glint at the reveal and
   the swing help; `kLsTargetScale` (PROPOSED 1.0, try 1.3) scales all four.
4. **Shared plink.cpp**: a slip changes the plinking. The recorded-hash test (10.1) and the unchanged
   test_plink and test_day_plink guard it.
5. **Length**: 102 to 119 s with long stillness in it. If it drags, cut the walk back for the flag
   (plant it beside the bag), not the holds.
6. **T1 and T2 depend on the seed** (the middle and far ridges do not always show in the window);
   the fallbacks keep the strings whole, and the test prints how often they are used.
7. **Midday pins and pool balance** move with a new midday major; re-capture deliberately.
8. **Kept glyphs** take 4 of 28 glyph slots for the rest of a midday loop. No midday major needs
   more than a few (UNVERIFIED for fetch's rings); `kLsKeepSteel = false` removes the risk.
