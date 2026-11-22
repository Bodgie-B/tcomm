# Plinking cans and a higher, whiter sun: design and build plan

Written 2026-11-17 from Mike's request of the same day. Design only: nothing here is built yet. Two
builders work from it in parallel on disjoint files (section 3). Every number marked PROPOSED is a
starting value to tune with the stills; everything about how it looks is UNVERIFIED until Mike has
seen it, and on the panel.

Mike's words (2026-11-17), in short:
- A new daytime vignette: the hiker sets up cans of different sizes along the full length of the
  branch the food bag hangs from (maybe with a stick), gets his .22 long rifle out of his backpack,
  walks to the foreground at the end of the water bank in the left corner, loads, shoulders, aims and
  shoots the cans off one by one. Have fun with how the cans pop up and spin. Variants: he cannot hit
  the last one however often he shoots; or he hits the food bag and all the food dumps to the ground.
  He cleans up all the cans and the food mess.
- The sun rises in a higher arc during the day and gets whiter the higher it goes. The moon stays on
  its low track as it is now.

References like `day_archery.cpp:161` are lines on 2026-11-17 in `screensaver/core/scene/`; they drift,
search for the name.

---

## 0. Proposed answers to the open choices (ask Mike at review, build these meanwhile)

| Choice | Proposed default | Alternative (one constant or flag) |
|---|---|---|
| What lifts the cans | a long stick from the wood pile, one can at a time balanced on its forked end | a trekking pole from the pack (saves about 6 s) |
| How many cans | 6, five sizes, tip to trunk | 5 (saves about 5 s if the loop fit drops the major too often) |
| Firing position | KNEELING on the camp-side bank just right of the big foreground pine, support elbow on the front knee | standing at the same spot (`kPlinkKneel = false`) |
| The rifle | a takedown survival .22 (the kind whose barrel and action stow in the stock), 8-round magazine, semi-automatic | none |
| Can colour | silhouettes (ACTOR) with a pale 1 px rim, like everything else on the scene | pale metal bodies |
| Weight in the midday pool | 0.7 (it is the longest major) | 0.8 like the ropes course |
| Sun at noon | about 30 px higher in November, 15 px higher in June (table in section 5) | a knot table, easy to raise or lower |
| Sun colour high up | the day's warm SUN colour blended to a warm white (STAR halfway to white), never pure white | STAR alone (creamier) |

Why kneeling: from the left corner every line to the cans crosses the screen area of the tent (x about
168 to 244, y 302 to 352). In depth the tent stands within 12 px of the camp pine, so the shot would
pass it high up, but a viewer reads the screen. Standing, the muzzle sits at about y 355, right on the
tent's front base, and the flash would look like it hits the tent. Kneeling puts the muzzle at about y
370 and the line passes under the tent's base in screen space until past its right corner. It is also
the steady position a careful .22 shooter uses, and the elbow on the knee reads well in silhouette.

---

## 1. The plinking major

### 1.1 Where everything is (frame pixels, 800 x 480)

From the loop's layout (all already in the World; examples use camp_x = 560 and the fire at F = (250,
398); camp_x is 550 to 570, F.x 238 to 262, F.y 393 to 403):

- **Camp pine** (`landscape.cpp:581`): trunk at x = camp_x, base y 340, 170 tall, five tiers. Its lowest
  foliage tier ends at y 318; at y 190 to 210 the foliage is within about 7 px of the trunk.
- **The bag branch** (`landscape.h` LS_BRANCH_*; drawn `landscape.cpp:267`): a 5 px TRUNK capsule from
  the root (camp_x, 212) to the tip (camp_x - 82, 202). Its centre line is
  `yc(x) = 212 - 10 * (camp_x - x) / 82`; its top surface is `yc - 2.5`. It shows from the tip to about
  camp_x - 7, so 75 px of branch.
- **The food bag** (`props.h` FoodBag, `bagInit` at `world.cpp:375`): tip (camp_x - 82, 202), line 16, so
  the sack's neck hangs at (camp_x - 82, 218) and the sack reaches about y 240. HUNG at midday from the
  build on (`spine_midday.cpp` campPop and campFinal).
- **Under the branch**: the ground lane y 356 (CampLayout::bag_work is (camp_x - 102, 356)).
- **The pack** (CampLayout::pack = (F.x - 74, 359)); **pack_work** (pack.x + 16, 356), where he stands
  facing it (left). At midday the pack stands there all loop (`middayLayout`), he wears none.
- **The wood pile** (F.x - 30, F.y - 8); **wood_work** (F.x - 48, F.y - 10).
- **The tent** (F.x - 44, 352): x from F.x - 82 to F.x - 6, ridge 44 to 50 px up.
- **The foreground pines** (`landscape.cpp:583`): fg[0] at x 14..26, fg[1] at x 64..76 with its base
  lifted to the bank (about y 450). fg[1]'s lowest tier reaches x 136 to 157 at y 378.
- **The stream at the lower left** (`generateStream`): control points (300, 448) and (120, 476) with
  5 px jitter, half width about 31 px there. At x 165 the water's upper edge is at y 437 to 441.
- **The firing spot (AT_M0)**: `x = clamp(landscapeFgPineBounds(ls, 1).x1 + 14, 150, 180)` (about 165,
  just clear of the big pine's skirt so the pine is never redrawn over him), `y = cy - half - 6` from
  `landscapeStreamSpan` at that x, clamped to 412..440 (about 432): on the bank, 6 px above the water.
  Kneeling, his hat is at about y 375, the rifle's grip at about (171, 388), the muzzle at about
  (203, 372). Facing right (downrange, up toward the pine).
- **Chip box** x 538..796, y 412..476 and the quiet corner x >= 660, y >= 420: nothing of the plinking
  goes there (cans land at y <= 356, all within x <= camp_x + 60).

### 1.2 The cans

Six cans, centres at `x = camp_x + dx`, base on the branch top (`yc(x) - 2.0`, half a pixel into the
bark). Index 0 is at the tip, 5 at the trunk. Sizes in px (width x height), PROPOSED:

| can | dx | look | w x h | base y (camp_x 560) | role |
|---|---|---|---|---|---|
| 0 | -76 | small paste tin | 5 x 7 | 200.7 | last shot; B: the one that will not fall |
| 1 | -64 | soda can | 7 x 11 | 202.2 | |
| 2 | -51 | tuna tin, squat | 9 x 6 | 203.8 | spins flat like a frisbee |
| 3 | -39 | soup can, tall | 8 x 12 | 205.2 | A: hops back onto the branch, teeters, falls by itself |
| 4 | -27 | bean can | 7 x 9 | 206.7 | |
| 5 | -15 | coffee can, big | 10 x 13 | 208.2 | first shot; tumbles slowly end over end |

Neighbours are 12 to 13 px apart and at least 3.5 px clear of each other; can 5's right edge is 3 px
clear of the foliage at its height. Drawn (plinkDraw): the body a rounded quad in ACTOR turned by `rot`
(and squeezed in x by `sq` for the frisbee spin), a 1 px rim across its top in STAR mixed 40 % toward
CLOUD_LIT (reads as tin), 2 primitives a can. They stand against the sky (y 190 to 210 is sky band 3).

### 1.3 Choreography, beat by beat (variant A, the clean run)

The major starts wherever the last beat left him (normally sitting at camp.sit facing the fire, as
archery assumes) and ends sitting there, hat on, hands empty, every can and food item gone, the bag
hung, the union zero. Walk speeds: 36 px/s short, 42 px/s for the long crossings (a purposeful walk;
the stroll is 34). Seconds are PROPOSED script seconds; the beat adds slack.

**Set-up (about 31 s)**
1. `pose(STAND 0.8, 0.3)`. (1.1 s)
2. `go(AT_WOOD_WORK, 36)`, `face(RIGHT)`, `act(actTakeStick, 1.6)`: a crouch, and at u 0.5 the stick
   (64 px, TRUNK, a small fork at one end) is in his near hand, fork up; `pose(STAND 0.6, 0.2)`. (5.4 s)
3. `go(AT_PACK_WORK, 36)`, `face(LEFT)`, `act(actCansOut, 1.8)`: a crouch at the pack, the can sack (a
   small mesh bag, ACTOR, two can rims showing) is in his far hand as he comes up; `pose(STAND 0.6,
   0.2)`. (4.0 s)
4. `doit(nextPlace)` sets AT_M3 to (can 0's x - 24, 356); `go(AT_M3, 42)`: along the lane under the
   branch (about 270 px). (6.4 s)
5. Six times: `face(RIGHT)`, `act(actPlaceCan, 1.8)`: u 0 to 0.2 the stick comes down level in front of
   him and the far hand sets the next can on the fork (the can leaves the sack); u 0.2 to 0.7 the near
   arm goes up (scriptSetArm near arm toward shoulder 2.7, elbow 0.2, a small lean back) and the fork
   rises to the branch top above the can's spot; u 0.7 to 0.8 the can is set down (CAN_ON, a 0.4 s
   settle wobble of 0.06 rad); u 0.8 to 1 the stick comes away and down. Then `doit(nextPlace)` and
   `walk(AT_M3, 30)` 12 to 13 px to the right, and `repeatWhile(morePlaces, 4)`. (6 x 1.8 + 5 x 0.45
   = 13.1 s). The stick's fork is about 54 px from his hand: standing 24 px left of the can, with the
   arm up, that reaches the branch top (he cannot reach it by hand: the branch was set high enough to
   clear his hat and a reared bear, `landscape.h:42`).
6. He keeps the stick and the empty sack in his hands for the walk back.

**The rifle (about 17 s)**
7. `go(AT_PACK_WORK, 42)` (about 320 px, 7.7 s).
8. `face(LEFT)`, `act(actRifleOut, 2.0)`: a crouch; at u 0.3 the stick and sack are laid down beside the
   pack (they lie there, PL_STICK_DOWN, PL_SACK_DOWN), at u 0.6 the rifle's two halves are in his
   hands (barrel and action in the near hand, stock in the far hand), both pointing at the ground;
   `pose(STAND 0.6, 0.2)`.
9. `act(actAssemble, 2.0)`: the hands come together in front of his belt, the barrel goes into the
   stock with a small twist of the far wrist (two little rocks of the far forearm), then it is one
   rifle in the low carry: the near hand at its balance point, the muzzle forward and 50 degrees
   below level, at the ground. (2.0 s)
10. `go(AT_M0, 34)`: down to the firing spot (two legs; steeper than wide). (2.6 s)
11. `face(RIGHT)`, `until(dogsClear)` (instant unless a house dog came out, section 1.11),
    `act(actKneel, 0.8)` (the kneel ramps in through the module's bend), `act(actLoad, 2.2)`: the far
    hand to his hip pocket and back with the magazine (2 x 5 px ACTOR in the hand), up under the
    action, the magazine shows in place; the far hand to the action's side and back once (the
    charging handle; no bolt drawn, the hand's tug reads). From here the rifle is LOADED. (3.0 s)

**Shooting (A: about 14 s)**
12. `doit(doString)`: queues the variant's string in the module (section 1.4); `until(stringDone)`.
    The module raises the rifle to the shoulder (0.8 s), then each shot: swing to the can (0.4 s), hold
    (0.9 to 1.3 s from the module's Rng; the first shot 1.6 s), fire, follow (0.8 s). A: six shots,
    cans 5, 4, 3, 2, 1, 0 (trunk end first, the tip can last). 0.8 + 6 x 2.3 = 14.6 s.
13. The rifle comes down to low ready (butt still at the shoulder, muzzle 30 degrees below level,
    downrange). (0.6 s)

**Unload and stow (about 8 s)**
14. `act(actUnload, 2.2)`: the far hand takes the magazine down to his pocket (it leaves the rifle),
    the far hand tugs the charging handle back and holds it, he tips the rifle a little toward himself
    (muzzle still downrange and low) and bends his head to look into the action (0.6 s): shown clear.
    UNLOADED. `act(actStand, 0.8)` back up to the low carry.
15. `go(AT_PACK_WORK, 34)` (2.6 s), `face(LEFT)`, `act(actStow, 2.2)`: a crouch, the barrel out of the
    stock (the reverse twist), both halves into the pack (they vanish at u 0.6), the stick and the sack
    back into his hands at u 0.8; `pose(STAND 0.6, 0.2)`.

**Clean-up (A: about 31 s)**
16. `go(AT_M3, 42)` to the first pair of fallen cans (section 1.5: they land in three pairs), and three
    times: `face(RIGHT)`, `act(actSweep, 1.3)` (a crouch; the two cans within 22 px of his reach go
    into the sack, which looks a little fuller), `doit(nextPick)`, `walk(AT_M3, 34)`,
    `repeatWhile(morePicks, 4)`. (about 7 s)
17. `go(AT_WOOD_WORK, 42, 20, -6)` (passing the wood pile, about 300 px), `face(RIGHT)`,
    `act(actTossStick, 0.9)`: an underhand lob, the stick arcs onto the pile and is gone. (8.1 s)
18. `go(AT_PACK_WORK, 36)`, `face(LEFT)`, `act(actSackIn, 1.4)`: a crouch, the sack into the pack.
    `pose(STAND 0.6, 0.2)`. (3.2 s)
19. `go(AT_SIT, 30)`, `face(TO_FIRE)`, `pose(SIT 0.9, 0.6)`. (4.1 s)

**Total A**: about 99 s of script. PROPOSED beat seconds **103**.

### 1.4 The shot string: hit and miss rules per variant

The module plays a queue (`PLINK_QUEUE` 12 entries) of entries `{kind, can}`. The kinds:

| kind | what happens |
|---|---|
| SK_HIT | swing, hold, fire; 3 ticks later (the bullet's flight, 0.12 s) the can pops (section 1.5) |
| SK_MISS | as a hit, but the can stays: 3 bark chips and a tiny dust puff jump off the branch 3 to 8 px beside or under it, and the can rocks (0.06 rad, 3 Hz, gone in 0.6 s) |
| SK_BAG | as a hit aimed at can 0, but the bag's line parts just under the tip: the bag falls (section 1.6) |
| SK_CHECK | rifle down from the shoulder to eye level, muzzle level and downrange; head bent to the rear sight; two small turns of the wrist (1.6 s); back to the shoulder |
| SK_SHRUG | low ready; the far hand opens palm up, the shoulders lift, head tilts (1.6 s) |
| SK_SCRATCH | low ready; the far hand up to the hat and back (head scratch), head shake (1.8 s) |

The strings (the plan's variant byte, `plinkPlanVar(r) = (r >> 13) % 3`):

- **A, the clean run**: HIT 5, HIT 4, HIT 3, HIT 2, HIT 1, HIT 0. Six rounds. Can 3's hit is the small
  gag: a weak pop (vy -90, vx 0) that lands it back on the branch; it teeters (rot swings to 0.18 rad
  and back twice over 0.9 s) and topples off on its own, landing with its pair. He holds his aim and
  lifts his cheek off the stock to look (the module's head lift) while it teeters.
- **B, the one that will not fall**: HIT 5, HIT 4, HIT 3, HIT 2, HIT 1, MISS 0 (chips under it), MISS 0
  (chips left of it, it rocks hard), CHECK, MISS 0 (chips right of it), SHRUG. Eight rounds: the
  magazine is empty, so the unload finds it empty (the same unload). In the clean-up, after the pairs,
  he walks under can 0 (`walk(AT_M2)`: can 0's x - 20, 356), raises the stick and pokes it
  (`act(actPoke, 2.4)`): it tips off, falls, bounces off his hat (his head dips 0.15, the hat stays
  on), and lands at his feet; `act(actPickOne, 1.0)`. B's script is about 109 s; beat **113**.
- **C, the food bag**: HIT 5, HIT 4, HIT 3, HIT 2, HIT 1, BAG, SCRATCH, HIT 0. Seven rounds. In the
  clean-up the food comes first (section 1.6), then the bag goes back up, then the cans. About 111 s;
  beat **115**.

Shots are always taken kneeling, facing right; the rifle never leaves the shoulder between shots of
one string except for CHECK, SHRUG and SCRATCH, which bring it down to low ready or eye level with the
muzzle downrange.

C needs the bag hung: if `w.bag.state != BAG_HUNG` at begin (never at midday after the build, but
cheap to check), the begin hook plays A instead (`m.var` low bits set to 0; no RNG).

### 1.5 Can physics (a few floats per can)

Per can: `x, y` (base centre), `vx, vy` (px/s), `rot` (rad, in plane), `spin` (rad/s), `sq` (frisbee
phase, turns), `land_y`, `t` (seconds in this mode), `wob` (settle wobble amplitude), plus a mode byte
(CAN_OFF, CAN_SACK, CAN_TIP on the stick, CAN_ON the branch, CAN_FLY, CAN_DOWN on the ground, CAN_HELD).

On a hit (the module's own Rng, two values, whatever the can): the can jumps up and away from him.
`vy = -(150 + 60 r1)`, `vx` chosen so it lands at its pair's spot (below), `spin = +-(2.5 + 2 r2)` turns
a second as rad/s (sign alternates by can index), gravity 650 px/s^2 (cartoon quick, the cone uses 900).
Styles by can: 2 also spins in `sq` (width scaled by |cos|, a flat spin); 5 tumbles slowly (spin 1.2
turns/s) and lands upright with a small hop. Flight from y 200 to 350 takes about 1.0 s, 20 px of rise.

Landing: at `land_y` it bounces once (`vy *= -0.3`, `vx *= 0.5`, `spin *= 0.4`), then comes to rest
when it next touches: `rot` eases to the nearest quarter turn (on its side, tall cans) or to 0 (upright,
squat cans) over 0.15 s. Then CAN_DOWN, still: no more dirty marks.

Where they land (so the clean-up is three stops, not six): pairs at `camp_x - 40` (cans 0 and 1),
`camp_x + 6` (2 and 3), `camp_x + 44` (4 and 5), each +-7 px apart, `land_y` 348 to 354 (always above
his walking lane y 356, so every can sorts behind him). Use `scriptGroundAt(w, x, y)` so none lands at
the water (the bank is at y 362 or lower out to x 640). All landing points are at x <= camp_x + 51 and
y <= 356: out of the chip box.

Bark chips (MISS): 3 TRUNK flecks r 1.1 px with `vy -(60..110)`, `vx +-40`, gravity 500, gone after
0.6 s or 40 px of fall; a dust puff (mix of SMOKE and CLOUD_LIT, r 1.5 to 3.5, 0.5 s). They use the
shared bit slots of the module (PlinkBit), never World::bits.

### 1.6 The food dump (variant C)

Reuse the existing bag: no new prop states and no change to `props.cpp`.

1. On SK_BAG's bullet tick the day tick (`plinkMajorTick`) cuts the line: `w.bag.state = BAG_DROPPED`,
   `w.bag.pos` = the sack's centre under the tip, a fall speed in the module (`bag_vy`). A 6 px frayed
   rope end stays at the tip (the module draws it, 1.2 px ACTOR, while `rope_cut`).
2. The bag falls (gravity 650; BAG_DROPPED draws the sack on its side, which reads as a dropped sack)
   to `bag_work.y - 4` (about 352) at the tip's x, and lands with a 1 px squash (its pos only).
3. On landing five food items squirt out of its neck to the right (PlinkBit kinds FOOD_LOAF, FOOD_APPLE,
   FOOD_PACKET, FOOD_JAR, FOOD_BAR), `vx 40..110`, `vy -60..-120`, one bounce, and come to rest in two
   clumps about camp_x - 72 and camp_x - 50, y 350 to 355. Colours from slots: loaf TRUNK mixed toward
   TENT_LIT, apple MUSHROOM, packet TENT_LIT, jar ACTOR with a STAR lid, bar TENT_LIT mixed toward
   MUSHROOM. One or two primitives each.
4. He has gone to low ready; SCRATCH; back to the shoulder; HIT 0.

Clean-up in C, before the cans (after the rifle is stowed and the stick and sack are in his hands):
`go(AT_M2, 42)` (left of the food, camp_x - 92, 356), two gathers (`act(actGather, 1.2)` each takes the
items within 22 px into his far hand's crook; they vanish), the stick and sack laid down beside the
bag, `act(actStuff, 1.2)` (crouch at the bag: the food goes in), `doit(doBagUp)` (`BAG_CARRIED`,
`hikerCarry(CARRY_BAG)`, `rope_cut` off), `walk(AT_BAG_WORK, 30)`, `face(RIGHT)`,
`act(actRehang, 2.0)`: one underhand toss that goes over the branch and drops to hang, the same motion
as the evening's foodbag c toss that catches (`spine.cpp` `toss(..., catches=true)`, copied into
day_plink.cpp as its own act: BAG_FLYING, World keeps the rope at his hand, `world.cpp:284`, then
BAG_HUNG with line 16 and swing 0). Then he picks up the stick and sack again and does the can pairs.

`plinkMajorEnd` (and `majorEnd` on an overrun) puts the bag back to HUNG, line 16, swing 0, and
`hikerCarry(CARRY_NONE)` if he still holds it, so a loop can never keep a dropped or carried bag.

### 1.7 The rifle: how it is held and drawn

A takedown survival .22: one-piece look when assembled, about 0.53 of his height long (48 px at 90).
Drawn by the module along an axis from the butt to the muzzle, in his hands like the bow (the bow hangs
from the near hand, `archery.h`):

- the stock: TRUNK capsule 3.2 px wide, from the butt to the wrist (0.24 of its length), so it shows on
  his black silhouette;
- the action: ACTOR capsule 3.0 px, 0.18 of its length;
- the barrel: ACTOR capsule 1.8 px to the muzzle;
- the magazine when in: ACTOR capsule 1.6 px, 5 px down from the action's middle;
- 3 to 4 primitives. When in two halves: the stock-and-action piece (0.30 H) in the far hand, the
  barrel (0.26 H) in the near hand, both muzzle or open end down.

Holds (RifleHold), each a bend of his composed pose in `plinkBend` (dayPose, like `archeryBend`), with
the rifle's axis placed from his hands:

| hold | where the rifle is | arms (rig angles: from straight down, + toward his facing) |
|---|---|---|
| RH_PARTS | two halves, muzzle ends at the ground | both forearms forward-down |
| RH_LOW_CARRY | grip at the near hand's balance point, muzzle 50 degrees below level ahead | near arm as it hangs (walks swing the far arm only, hands full) |
| RH_LOW_READY | butt at the shoulder, muzzle 30 degrees below level, downrange | as RH_SHOULDER with e = -0.52 |
| RH_SHOULDER | butt in the shoulder, the axis from the far hand (the grip) exactly at the target point | near (support) arm: shoulder `0.95 + 0.8 e`, elbow `(1.52 + e) - shoulder`; far (trigger) arm: shoulder `-0.25`, elbow `2.2 - 0.3 e`; head `+0.30 - 0.6 e` (cheek down on the stock) |
| RH_SIGHTS | as RH_SHOULDER at e = 0.05, a little forward of the face | head `+0.35` |

`e` is the aim's elevation above level: about 0.42 rad for can 5, 0.50 rad for can 0 from the firing
spot. The module computes it each step from last tick's far hand to the target (one tick behind, as the
archery does) and eases it over 0.4 s when the target changes. The rifle's axis is drawn exactly at the
target from the far hand; the support hand sits on the forend within a few px (tested, section 4).

Kneeling (`kPlinkKneel`, PROPOSED true): the bend's legs: front thigh 1.45, knee 1.45 (shin upright,
knee up), back thigh 0.25, knee 1.95 (shin back along the ground), lean +0.10; the rig grounds itself
on the lowest contact, so no hip height is tuned by hand. The support upper arm lies along the front
thigh (elbow on the knee). Getting down and up: 0.8 s each, through the bend's `kneel` weight.

Recoil: at the shot the muzzle kicks up 0.14 rad in 0.04 s and settles over 0.35 s; his shoulders rock
back (lean -0.035) and his head bobs 0.05.

Muzzle flash and puff: 2 ticks of flash (a short FIRE_CORE spoke 7 px forward along the axis, two 3 px
side spokes, a FIRE dot r 2), then a puff (SMOKE mixed 60 % toward CLOUD_LIT, r 2 to 4.5, drifting up
10 px, gone in 0.7 s). No sound anywhere (Mike: no ambient sound).

Safe handling the animation shows (and the tests check, section 4):
1. The halves come out and go together pointing at the ground.
2. Walking, the rifle hangs in the near hand, muzzle ahead and down at the ground.
3. It is loaded only at the firing spot, facing downrange (right, up into the pine); nothing else of
   the camp is in front of the muzzle in depth.
4. Between shots and when lowered, the muzzle stays downrange and below the line of the cans.
5. He unloads (magazine out, action open, a look into it) before he stands up or turns.
6. It goes back into the pack taken down. The dogs are never in the scene while it is out
   (section 1.11).

### 1.8 Wildlife startle

At the first shot only (the module raises PEV_FIRST_SHOT; the day tick acts on it):
- `directorScatter(w, muzzle, 900.f)`: every non-brave critter in use runs. (During a major the minors
  have quit and the stream's heron or deer has left at the major's start, so this is a safety net.)
- Two birds (CR_BIRD, `directorSpawn`, tag CT_MAJOR, `MajorState::c[1..2]`) burst from the camp pine's
  canopy edge at (camp_x - 26, 236) and (camp_x + 22, 228) and fly off the frame, one up-left and one
  up-right (CA_MOVE: the bird's flight arc).
- A squirrel (CR_SQUIRREL, `c[0]`) dashes out from behind the trunk at (camp_x + 8, 344) and runs off to
  the right (`directorExit(+1, 120 px/s, CA_RUN)`).
- A spawn that gets slot -1 is skipped. All three are out of sight within about 3 s; the script's
  last steps run long after, and `until(crittersGone)` before the final sit makes sure.
- The mail bird (CF_BRAVE, on the tent's ridge at midday) stays: it is the mail badge's bird and waits
  for the letter; Lucy and Copper are not there (next section).

### 1.9 The house dogs

- **Plan gate**: `majorsPlan` gives plink weight 0 when `in.dog_out != 0` (a midday loop with a dog
  out plays the fetch first anyway).
- **A dog that goes out mid-loop** while the plinking is on: it stays in the house until the major
  ends. In `housedogs.cpp` `houseDogsStep`, the flag that keeps the dogs out during the fetch
  (`fetchOn(w)`) becomes `fetchOn(w) || plinkHoldsDogs(w)`; a dog already in camp runs off to the left
  (the fetch's HP_STEPPED_OUT path) and comes back 2 to 4 s after the major. `plinkHoldsDogs` is true
  while `w.major.on && w.major.id == MJ_PLINK && w.day.kind == DAY_PLINK`.
- The script's `until(dogsClear)` (`houseDogsClear`) before the kneel means no round goes in while a
  dog is still running off past the firing spot.

### 1.10 Interruptions

- **Doorbell** (`react.cpp`): may start at any tick as now; the story freezes. While the rifle is in his
  hands in any form (`plinkRifleInHands`), the reaction is look-only exactly as with the bow
  (`r.look_only`): his head lifts, no hop, no turn, no wave, so the muzzle never swings. With the stick,
  the sack, a can or the food in his hands it is `hands_full` (a hop, no wave). The module steps with
  dt 0 while frozen (dayStep frozen), so a can in the air hangs there and carries on after; a muzzle
  flash that is showing when the bell comes is switched off on the frozen step (so a frozen flash never
  sits on screen for 3.4 s).
- **Passer-by wave**: refused during any major (`w.busy`); `reactWaveOk` also returns false while
  `plinkHandsFull` (belt and braces, as the archery does).
- **Fire and ice, the visiting couple, weather overlays, the mail glance, the minors**: none start
  during a major (they test `w.busy` or "no major").
- **Rain**: as the archery: the plan gives weight 0 to a rain, snow or storm in the data, and a random
  wet weather that turns up after the plan makes the begin hook play `dayNoShowScript` (DAY_NOSHOW).
- **The loop's end**: the beat's seconds leave 3 s of slack after the script (the tests read it).

### 1.11 Plan, pool and end

- `MJ_PLINK` after `MJ_ROPES` in `MajorId` (`majors.h`), name `"plink"` (no other major's name
  contains it or is contained in it). `static_assert(MJ_COUNT == 25)`.
- Pool row: `{"plink", {103, 103}, kPlinkW, kOut, kMid, plinkMajorBegin, plinkMajorTick, {nullptr,
  nullptr}, {0, 0}}` with `kPlinkW = 0.7f` PROPOSED.
- `scriptOf`: `if (id == MJ_PLINK) { s = plinkScript((uint8_t)var, n); return; }`; `majorSeconds`:
  `plinkSeconds(var)` (A 103, B 113, C 115, no-show 46); `majorEnd`: `plinkMajorEnd(w)`.
- `majorsPlan` gates: weight 0 at night, in a wet reading (the same line as the bike, archery,
  slackline and ropes), and when `in.dog_out != 0`; the variant line `if (id == MJ_PLINK) v =
  plinkPlanVar(r_var);`. The plan's draws stay fixed in number (the rule in HOW_TO_ADD_SCENES 1.5).
- Begin hook (`plinkMajorBegin`): MajorState points: `p[0]` the firing spot, `p[1]` beside the pack
  where the stick and sack are laid (pack.x + 4, 358), `p[2]` B: under can 0 / C: left of the food,
  `p[3]` the moving placement and pick-up point. `memset` the DayState, `kind = DAY_PLINK`, dayWet to
  NOSHOW, the C-needs-a-hung-bag check, `plinkInit` with the branch geometry, the six cans in CAN_SACK
  (not drawn), the module Rng seeded from the layout (`seedOf` as `day_archery.cpp`, xor a constant
  of its own).
- `plinkMajorTick`: reads last step's events: PEV_FIRST_SHOT (startle), PEV_BAG (cut the line, start
  the fall), steps the bag's fall and the spill (frozen for free during the doorbell: the beat does not
  run then).
- `plinkMajorEnd`: the bag hung, the carry off, the union zeroed.

---

## 2. Data added to World

Everything lives in the daytime union (`day.h` DayState), so World and WorldDirty do not grow.

```
#define PLINK_CANS 6
#define PLINK_BITS 9          // 5 food items + 3 bark chips + 1 dust puff (C uses the food, B the chips)
#define PLINK_QUEUE 12
#define PLINK_DIRTY_MAX (PLINK_CANS + PLINK_BITS + 3)   // cans, bits, held things, tree things, flash

struct PlinkCan {             // 40 bytes
  uint8_t mode, w, h, style;  // CanMode; size px; pop style
  float x, y, vx, vy, rot, spin, sq, land_y, t;
};
struct PlinkBit {             // 32 bytes
  uint8_t kind, mode, bounces, pad;
  float x, y, vx, vy, rot, land_y, t;
};
struct PlinkState {           // about 700 bytes
  PlinkCan can[PLINK_CANS];                 // 240
  PlinkBit bit[PLINK_BITS];                 // 288
  uint8_t queue_kind[PLINK_QUEUE], queue_can[PLINK_QUEUE];   // 24
  uint8_t n_queue, cur, phase, hold;        // shot machine; RifleHold
  uint8_t rifle, mag, rounds, chambered;    // RifleMode; magazine none/hand/in; 0..8
  uint8_t stick, sack, sack_n, rope_cut;    // stick none/hand/down; sack none/hand/down; cans in it
  uint8_t events, first_done, kneel_on, pad;
  int8_t dir; uint8_t pad2[3];
  float t, hold_len, aim, elev, elev_to, recoil, flash, kneel;   // 32
  float puff_t; Vec2 puff;                  // 12
  float stick_raise; Vec2 stick_tip;        // 12
  float bag_vy;                             // the falling bag (C)
  Vec2 near_hand, far_hand, target;         // 24, as of the last step
  Vec2 branch_root, branch_tip;             // 16
  Vec2 down_at;                             // where the stick and sack lie
};
struct DayPlink { PlinkState p; Rng rng; };   // about 720 bytes with the Rng (16)
```

Budget check: the union's size is set by DayBike (BikeState plus a 1,632 byte SplashState plus an Rng,
`day.h:9`), well over 1,700 bytes. DayPlink at about 720 bytes adds **0 bytes** to DayState, World,
WorldDirty and App: App stays about 28,640 of SS_APP_BYTES 32,768 on the panel and 29,856 of the PC
test wire 30,000 (`test_day_majors2.cpp:662`). Add `static_assert(sizeof(DayPlink) <= sizeof(DayBike),
"the plinking must fit in the bike's place")` in `day.h` so it can never grow the union by accident.
`MajorState` is used as the other majors use it (p[0..3], c[0..2], cue bits): no change. DAY_PARTS
stays 2 and WORLD_ACTOR_MAX stays 130 (section 3, day.cpp hooks).

Panel memory: no RAM change, but the code is new. On the panel the screensaver's code is copied into
PSRAM while it runs, which is the tightest memory (memory audit 2026-11-12). Estimate: plink.cpp plus
day_plink.cpp about 1,300 lines, about 14 to 20 KB of code. The builder reports the host object sizes
(`size -A` on the two .obj files in the build folder, from the WinLibs bin) as the proxy, with the
note that the panel figure is UNVERIFIED until the next firmware build (no firmware builds in this job).

Draw cost: cans at rest mark nothing. A flying can marks 1 or 2 strips a tick (it is 13 px), the
flash 1 strip for 2 ticks, the rifle and his hands the strips he is in already (he is marked every
tick). Primitives at most: rifle 4, held stick and can 3, cans 12, bits 9, flash 4, puff 1, rope end 1:
under 35 on top of the scene. Majors must average 10 strips or fewer.

---

## 3. File plan: two builders, disjoint files

### 3.1 The plinking builder

New files (all LF, ASCII):
- `screensaver/core/scene/plink.h`, `plink.cpp`: the standalone module in the archery's style
  (`archery.h`): plain data, commands, `plinkStep(state, near_hand, far_hand, facing, dt)`, the bend
  (`plinkBend`, `plinkPose`), bounds (`plinkBounds`, `plinkTreeBounds`, `plinkHeldBounds`), `plinkSame`,
  `plinkDirty`, and two draws: `plinkTreeDraw` (everything that is not in his hands: cans on the branch,
  in the air and on the ground, bits, the rope end, the stick and sack lying down) and `plinkHeldDraw`
  (the rifle or its halves, the magazine, the stick and the can on its fork, the sack, the flash, the
  puff). No World, no platform code, no RNG except the Rng a command is handed.
- `screensaver/core/scene/day_plink.h`, `day_plink.cpp`: the major (the archery's `day_archery.*`
  pattern): `plinkPlanVar`, `plinkSeconds`, `plinkScript`, `plinkMajorBegin/Tick/End`, the scripts
  for A, B, C, the acts, the bag cut and fall, the rehang toss, the startle, `plinkHoldsDogs`,
  `plinkRifleInHands`, `plinkHandsFull`.
- `screensaver/tests/test_plink.cpp`, `screensaver/tests/test_day_plink.cpp` (section 4).
- `screensaver/tools/plink_stills.cpp`, `screensaver/tools/plink_sheet.py` (section 4).

Existing files this builder may edit (and no others):
- `core/scene/day.h` (LF): `DAY_PLINK` on the end of DayKind, `DayPlink` in the union, the
  static_assert, the include, the comment block's list.
- `core/scene/day.cpp` (LF): the DAY_PLINK case in `sizeOf`, `dayBounds`, `dayPose` (`plinkPose`),
  `dayHoldsFacing` (true while the rifle is in his hands), `dayStep` (`plinkStep`, dt 0 when frozen,
  and the frozen flash off), `dayTrack` (`plinkDirty`), `dayPartY` (part 0 at the camp pine's base
  y - 1, like the hung bag; part 1 at his feet y + 1, like the bow), `dayPartDraw`.
- `core/scene/majors.h` (CRLF: keep it): `MJ_PLINK` and its comment.
- `core/scene/majors.cpp` (LF): the include, the weight constant, the pool row, the static_assert,
  `scriptOf`, `majorSeconds`, `majorEnd`, the `majorsPlan` gates and variant line.
- `core/scene/react.cpp` (LF): `look_only` and `hands_full` in `reactStart`, the check in
  `reactWaveOk` (through the day_plink.h helpers).
- `core/scene/housedogs.cpp` (LF): the keep-away flag (section 1.9).
- `tests/test_spines.cpp` (LF) and `tests/test_midday.cpp` (CRLF: keep it): `"plink"` in each
  `isMajor` list; the pinned midday plan lines in test_midday.cpp re-captured only after checking that
  every difference is the new pool entry (HOW_TO_ADD_SCENES gotcha 4).
- `tests/test_day_majors2.cpp` only if a check there counts majors by id range and fails for that
  reason alone (expect none).
- `docs/screensaver/NOTES_FOR_MIKE.md` and `docs/screensaver/WORKLOG.md` (the plinking's entries).

### 3.2 The sun builder

New files:
- `screensaver/tools/sun_stills.cpp` (and a sheet: reuse `tools/sky_sheet.py` unchanged if its
  `tiles.txt` format fits, else a new `tools/sun_sheet.py`).

Existing files this builder may edit (and no others):
- `core/scene/landscape.cpp` (CRLF: byte-safe edits, keep CRLF): `sunPoint`, the knot table,
  `landscapeSky`'s sun, `landscapeSunColor`, `drawSkyReal`'s sun fill.
- `core/scene/landscape.h` (CRLF): the `landscapeSunColor` declaration and the SkyDraw comment.
- `tests/test_sky.cpp` (LF): section 5.5.
- `tools/sky_stills.cpp` only if a shared helper is cleaner than copying into sun_stills.cpp.

No hook between the two is needed: the plinking never reads the sun, and the sun code touches no
scene file. Neither builder edits `world.h`, `world.cpp`, `app.cpp`, `props.*`, `hiker.*`,
`landscape.*` (plink) or `day*`, `majors*`, tests other than test_sky (sun). WORKLOG and NOTES_FOR_MIKE
belong to the plinking builder; the sun builder reports its numbers to the lead, who adds them.

---

## 4. Plinking tests, stills and checks

### 4.1 `tests/test_plink.cpp` (the module, like `tests/test_archery.cpp`)

- Bounds hold every draw: each state (cans in every mode and style mid-flight, bits, every rifle hold,
  halves, magazine in and out, the stick up and down, sack, flash, puff, the rope end), both facings,
  drawn on a blank surface: no pixel outside `plinkBounds` / the part's bounds.
- Dirty covers change: a scripted run (place six cans, shoot each kind, the bag event) rendered
  incrementally from `plinkDirty` rects equals a full redraw every tick; `plinkDirty` returns 0 on a
  tick where nothing visible changes (cans at rest, rifle still).
- Physics: every HIT can leaves the branch, comes to rest within 2.5 s, rests at its pair's spot +-9 px
  in x and at `land_y`, inside the frame and outside the chip box; can 3's A gag returns to the branch
  then falls; a MISS never moves a can off the branch; chips are gone within 1 s; rounds never exceed 8
  and A, B, C use 6, 8, 7.
- Pose: for every shot of every string, the support hand is within 3 px of the rifle's axis and the
  axis passes within 2 px of the target can's centre at the shot tick; the kneel grounds him (lowest
  contact on feet.y); `plinkBend` is mirror-correct for both facings.
- Safety invariants every tick: `chambered || mag == in` implies hold is LOW_READY, SHOULDER or SIGHTS
  and the axis points to his facing side (dx * facing > 0) and is never more than 0.2 rad above the
  highest can's line; RH_LOW_CARRY and RH_PARTS only while unloaded.
- Determinism: two runs give byte-identical states; each shot draws exactly two Rng values.

### 4.2 `tests/test_day_plink.cpp` (the major, like `tests/test_day_majors2.cpp`)

- Plan: over many seeds, plink is planned only in the midday family, never at night, never with a wet
  reading, never with `dog_out != 0`; every variant comes up (find seeds in 1..1200 with fresh memory).
- Each variant played from its seed, rendering every tick: the hiker in frame and out of the quiet
  corner every tick; nothing of the piece in the chip box; script done at least 3 s before the beat ends;
  no overrun; end state (sitting at camp.sit, hat on, carry none, day.kind DAY_NONE, bag HUNG with line
  16, no critter in the scene, `appCritterCount == 0` at loop end); strips averaged over the major 10 or
  fewer; incremental render equals full render at ticks inside the set-up, the shooting, a can in the
  air, C's fall and spill, and the clean-up.
- Story checks: A all six cans down; B can 0 still on the branch after the string, and down after the
  poke; C the bag DROPPED after the bag shot and HUNG again before the can pick-up, five food items
  spilled and all gone at the end.
- Startle: on the first shot's tick a scatter happened; the birds and squirrel spawned (when slots
  allowed) and are released before the end.
- Dogs: a run with `dog_out` switched on during the set-up: no house dog enters the scene until the
  major has ended, and one does come in afterwards.
- Doorbell at key moments (during a can placement, the kneel, a hold, a can's flight, C's bag fall, the
  unload, the walk back with the rifle): the reaction is look-only while the rifle is in his hands, his
  facing never changes during it, and the story's bytes after the reaction equal a run without the
  bell, shifted by the frozen ticks (the `testFreeze` snapshot pattern, `test_day_majors2.cpp:571`).
- Determinism: two runs of each variant hash the same frames; SS_XMAS ON and OFF.

### 4.3 Stills and sheet

`tools/plink_stills.cpp` (pattern: `tools/clouds_stills.cpp`; not in CMake; built with g++ against the
build folder's `libss_core.a`, `-DSS_XMAS=1` as the host build): finds a seed per variant with
`--spine`-style forced midday inputs, runs the scene rendering every tick (as the panel does) and writes
PPMs at labelled moments; full frames plus 2x crops around the branch and around the shooter.
`tools/plink_sheet.py` (pattern: `tools/clouds_sheet.py`, zlib only, run with `python -I`) makes
`docs/screensaver/reviews/stage21-plink/contact_sheet.png` and one sheet per variant.

Moments: the stick raising a can (crop); all six cans up (full and crop); at the pack with the two
halves; assembling; walking with the rifle (low carry); kneeling and loading; the aim (crop); the flash
frame; a can at the top of its pop, a can spinning, can 2's flat spin; cans on the ground; A: can 3
teetering; B: chips under can 0, the sight check, the shrug, the poke and the bonk; C: the bag falling,
the spill, the rehang toss; the unload look; the final sit. Also one mirrored-layout seed (camp_x 550
and 570) to check the branch spacing.

### 4.4 Checks to run and report

- Build folders `screensaver/build-plink` (SS_XMAS ON) and `screensaver/build-plink-off` (OFF),
  configured like `build-wave-on` / `build-xmas-off` (CMakeCache: WinLibs `c++.exe` and `ninja.exe` from
  `$LOCALAPPDATA/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin`,
  `-G Ninja`, `-DSS_XMAS=ON|OFF`). All ctest suites pass in both.
- `ss_soak` from build-plink: CLEAN, determinism PASS.
- Known risks to watch, not to "fix" by editing tests: `tests/test_dog.cpp` wants the fetch in 35 of
  150 midday loops, and `test_memory_sets` has trials that a new midday major can shift; tune
  `kPlinkW` (0.6 to 0.8) if they fail for that reason, and say so.
- Report: sizeof(App) (unchanged), strips per frame over the major, ms a frame, overruns, object sizes,
  primitives, and "unverified on the panel".

---

## 5. The sun: a higher arc that whitens

### 5.1 What it does now

With the host's real sun (`Inputs::sky.have`, every panel frame) `landscapeSky` (`landscape.cpp:935`)
places both bodies with one mapping, `skyPoint(az, alt)` (`landscape.cpp:349`):

- x: due south is x 400, 125 degrees of azimuth either side maps to the frame's edges, clamped 40..760.
- y above the horizon: `y = 268 - 212 * sin(alt)^0.6`; below it, `268 - alt * 7` (it sinks behind the
  ridges).
- The sun's radius is 30, swelling to 36 low in the west; the sun is drawn only while it is above -6
  degrees and the night's colours have not begun. It is filled with the palette's SUN slot
  (`drawSkyReal`, `landscape.cpp:417`); by day SUN is warm yellow (0xE8C170 at the day key), and the
  palette already pulls it toward the sky under overcast.

Noon at the settings' placeholder latitude 38.9 N (the stills tools use it; the house's real place is
not in the repo, so these move a few px there):

| date | noon altitude | y now (centre) | disc top |
|---|---|---|---|
| 21 Dec | 27.7 deg | 134 | 104 |
| 17 Nov | 31.9 deg | 123 | 93 |
| 20 Mar | 51.1 deg | 86 | 56 |
| 21 Jun | 74.6 deg | 61 | 31 |

The parked sky (no host sky: tests, the preview's default) uses the mood path `kBody`
(`landscape.cpp:289`) and is not touched by this plan. The Lookout and the sunrise draw their own suns
(`skyNow` returns an empty SkyDraw for set pieces, `app.cpp:137`) and are not touched either.

### 5.2 The new arc (the sun only)

A new `sunPoint(az, alt)`: x exactly as `skyPoint` (same azimuth mapping, same clamp); below the
horizon the same line (`268 - alt * 7`), so rising and setting behind the ridges are unchanged; above
it, a knot table interpolated linearly in altitude (PROPOSED, tune with the stills):

| alt (deg) | 0 | 5 | 10 | 20 | 30 | 45 | 60 | 75 | 90 |
|---|---|---|---|---|---|---|---|---|---|
| y new | 268 | 213 | 180 | 132 | 98 | 70 | 52 | 45 | 42 |
| y now | 268 | 219 | 194 | 157 | 128 | 96 | 74 | 60 | 56 |

So near the ridges it is almost the same (5 degrees up: 6 px higher), and through the middle of the day
the arc stands higher: 17 Nov noon at y 94 (29 px higher, disc top 64), 21 Dec 106 (28 higher), 20 Mar
63 (23 higher), 21 Jun 45 (16 higher, disc top 15). Overhead (never at our latitudes) is y 42, disc top
12: still inside the frame and inside the test's 10 px margin. Slopes up to 60 degrees are more than 1
px a degree, so the existing "strictly higher every degree up to 60" test still holds after rounding.

It stays clear of the rest:
- the top edge: disc top >= 12 always;
- the ridges: 20 degrees up the disc's bottom is at 162, under 190 as the test asks;
- the cloud deck (cover 4 and up, underside y 94 to 158): the sun goes behind it as before, now more
  often in summer, which is right for an overcast day; the high clouds of cover 2-3 and the two
  drifting ones are drawn after the cache, in front of it, as now;
- the status chip and the quiet corner: the sun is never below y 268.

### 5.3 Whiter as it climbs

`landscapeSunColor(const SkyDraw& s, const Palette& pal)` (public, so the tests and stills use the same
colour):
- `k = smoothstep(clamp((y8 - s.sun_y) / (y8 - y55)))` where `y8` and `y55` are `sunPoint`'s y at 8 and
  55 degrees (193 and 58 with the table): 0 low down, 1 from 55 degrees up.
- the high colour `T = mixRGB(pal[STAR], white, 0.5)` (STAR is the day's pale cream, 0xFFF1C2, so T is
  a warm white near 0xFFF8E0; never pure white), and with high cloud (cover 2 or 3 eighths)
  `T = mixRGB(T, pal[SKY2], 0.3)` so a hazy day's sun is softer.
- colour = `mixRGB(pal[SUN], T, k)`.

Because it is a function of `sun_y` and `cover` only, both already in SkyDraw: no new field (App does
not grow), `landscapeSkySame` already notices every change, and the app's row repaint
(`app.cpp:332-363`) already repaints the sun when it moves; a cover change already rebuilds the cache.
At 17 Nov noon k is about 0.8, mid-morning about 0.4, an hour before sunset 0. Golden hour and dusk
keep their colours (the sun is low then). The build's sky fade-in fades STAR as it fades SUN; the 50 %
white part does not fade, which shows only during the first seconds of a loop's build (accepted;
UNVERIFIED by eye).

### 5.4 Code paths that change (landscape.cpp and landscape.h only)

1. Add `sunPoint` beside `skyPoint`, sharing the x mapping (a small `skyX(az)` both call, so the two can
   never drift apart in x).
2. `landscapeSky`: the sun uses `sunPoint`; the moon keeps `skyPoint` (its lines unchanged).
3. `drawSkyReal`: the sun's `fillCircle` uses `landscapeSunColor(sky, pal)`; the moon's colour line is
   unchanged.
4. `landscape.h`: declare `landscapeSunColor`, update the SkyDraw comment ("the sun on its own higher
   arc; the moon on the shared low one").

The moon stays exactly as it is: `skyPoint` and the moon's lines in `landscapeSky` and `drawSkyReal` are
not edited, and a pinned table in the test proves it (next section).

### 5.5 Sun tests (`tests/test_sky.cpp`)

- **Moon unchanged**: before any edit, print the moon's (x, y) from `landscapeSky` for about 15 inputs
  (azimuths 60 to 300, altitudes -3 to 70, a few phases, by day and by night) and pin them in a table;
  after the change they must be equal to the pixel. Also pin the moon's phase byte and `dark` for the
  same inputs.
- **The arc**: `sun_y` at each knot equals the table; the noon heights for 27.7, 31.9, 51.1 and 74.6
  degrees are 106, 94, 63 and 45 within 1 px; for every altitude from 6 to 90 the new sun is higher than
  the old formula (computed in the test); for -5 to 0 it is where it was; x is the moon's x for the same
  azimuth.
- **The colour**: `landscapeSunColor` equals `pal[SUN]` at and below 8 degrees; its luminance never
  falls as the altitude rises; at 55 degrees and up it equals T; it is never 0xFFFF; cover 2 makes it
  softer than cover 0.
- **Existing checks to update** (because the change makes them wrong, not to make them pass): the
  "moon east of the sun" check (`test_sky.cpp:96`, `moon_y == sun_y`) becomes `moon_y` equal to its
  pinned value and `sun_y < moon_y` at the same altitude; `testParkedSky`'s real-sun pixel check
  (`test_sky.cpp:230`) compares with `landscapeSunColor(d, pal)` instead of `pal[SUN]`.
- **Moving equals rebuilt**: add a `testMovingEqualsRebuilt` case where the sun climbs from 12 to 50
  degrees (the colour changes with the height) with the palette spread as the panel runs.
- All suites in `build-sun` (SS_XMAS ON) and `build-sun-off` (OFF); `ss_soak` CLEAN with determinism
  PASS. Other suites that feed a real sky (`test_sunrise.cpp`, `test_visitors.cpp`) must pass
  unchanged; if one pins a camp frame with the sun in it, find out why before re-capturing.

### 5.6 Sun stills (`docs/screensaver/reviews/stage21-sun/`)

`tools/sun_stills.cpp` (the `tools/sky_stills.cpp` pattern: the firmware's own sky maths for the
placeholder place, `firmware/tcomm/src/core/sky.cpp`, compiled into the tool; nothing about the house's
real place; labels generic, never clock times):
1. Arc charts: the day scene with the sun's track for one day drawn as dots every 20 minutes, the old
   mapping (computed in the tool from the old constants 268, 212, 0.6, dim dots) and the new (bright
   dots), for 21 Dec, 17 Nov, 20 Mar and 21 Jun. The moon's track for one night on 17 Nov in a third
   colour, labelled "unchanged".
2. Full scene stills on 17 Nov: an hour after sunrise, mid-morning, noon, mid-afternoon, an hour before
   sunset (the colour ramp); noon on 21 Dec and 21 Jun; noon with high cloud (cover 3).
3. A day moon still (a half moon up in the afternoon) before and after, to show it in the same place.
Sheet with `tools/sky_sheet.py` (or a copy if its tile format does not fit) into
`docs/screensaver/reviews/stage21-sun/contact_sheet.png`.

---

## 6. Rules both builders keep

- Never run `tools/fw.sh`, `tools/build_sim.sh`, arduino-cli, esptool or ssh; never read
  `firmware/tcomm/src/config/secrets.h`; do not commit.
- Keep each file's line endings (`file` tells; CRLF files above: landscape.cpp, landscape.h, majors.h,
  test_midday.cpp): byte-safe edits.
- No em dashes in comments or docs; plain English comments; decisions cite "Mike, 2026-11-17".
- The engine builds for the ESP32-S3: no host-only APIs in `core/`, no heap, no STL, no double, table
  sines where possible, one RNG call per statement, no RNG outside the module's own Rng, no `w.tick`
  in story timing (HOW_TO_ADD_SCENES 1.5).
- Draw cost is pixels redrawn: small moving things, no full-screen redraws, no new palette steps.
- Report with the work: sizeof(App), strips per frame, ms a frame, palette rebuilds, overruns, object
  sizes as the flash and PSRAM proxy, and "unverified on the panel".
