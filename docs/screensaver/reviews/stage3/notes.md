# Stage 3 notes: hiker rig

Module: `screensaver/core/scene/hiker.h` and `hiker.cpp`. Test: `screensaver/tests/test_hiker.cpp`.
The module is built but not placed in the scene; stage 4 places it.

Sheets written by `test_hiker` into `build/`: `hiker_poses_day.ppm` and `hiker_poses_night.ppm`
(every pose in 5 columns, three rows facing right and then three rows facing left; the night sheet
has the fire rim on, lit from the right), `hiker_walk.ppm` (12 walk phases at 90 px, then at 60 px),
and `hiker_carry.ppm` (row 1: POSE_CARRY with each CarryId in enum order; row 2: each item in the
pose it is for; row 3: row 1 at 60 px).

## Poses

| Pose | What it is for |
|---|---|
| STAND | Default and arrival pose. Weight even, arms loose. |
| LOOK_AROUND | Hand shading the eyes under the brim, head up. Arriving, scanning the valley, hearing a critter. |
| CROUCH | Deep squat with hands forward to the ground. Picking things up, tending the fire, looking at small animals. |
| SIT | On the ground with knees up and arms around them. By the fire, calm stretches. |
| REACH_UP | Both arms up, on tiptoe, head back. Hanging the food bag, reaching into the pine. |
| CARRY | Forearms level in front with the elbows bent. Holding wood (or any CarryId) while walking. |
| BLOW | Kneeling on one knee and leaning far forward to blow on the fire. A slow puff motion runs while held. |
| YELP | Startled jump: off the ground, arms flung up, recoiling. The bear, a sneaky critter. |
| LIE | Lying back with the hat tipped over his face, hands behind his head and one knee up. Napping. |
| WAVE | One arm up with the elbow out; the forearm waves while held. The ranger-lantern scene and the doorbell reaction. |
| HANDS_ON_HIPS | Elbows out, chest up. Admiring the tent or the cairn, mild exasperation at a thief. |
| SCRATCH_HEAD | Hand at the back of the head, head tilted. Puzzled, as when something has gone missing. A small scratch motion runs while held. |
| HOLD_MUG | Mug raised near the chin, other hand down. Morning coffee, looking at the view. |
| READ | Seated with a book held over the knees, head down. Evening by the fire or in the tent. |

READ is a seated pose, since that is how it will be used. HOLD_MUG is a standing one. Each of the
three idle motions (wave, scratch, blow puffs) starts from zero only after its blend has landed, so
every pose is exact on the tick its blend ends.

Blending: easeInOutCubic. Action poses (REACH_UP, YELP, WAVE) spend the first 15% winding up 12%
against the move. For reach_up that is a 0.35 rad dip of the arm. Landing poses (SIT, CROUCH, LIE)
use `easeOutBack(easeInOut(t))`, which overshoots by about 10% and settles. For sit that takes the
thigh 0.22 rad past the pose. A new `hikerSetPose` blends from the composed pose on screen, so
nothing jumps mid-blend. A blend lands on the table entry bit-exactly on its last tick (tested).

Walk: legs swing on a sine whose amplitude depends on speed (0.29 rad at 36 px/s for a 90 px
figure). The swing leg bends at the knee. Empty arms counter-swing. The walk phase advances by
distance covered, not time, so the stance foot keeps pace with the ground. The rig grounds itself
every frame (the lowest contact sits on feet.y), which gives the hip bob on its own: 2 px at
90 px. The pack bounces out of phase by about 1 px. Arriving settles into STAND in 0.3 s. When
carrying, the arms keep the carry pose, and arrival keeps it too.

Breathing: a 3.6 s cycle lifts the chest, shoulders and pack by up to 1 px at 90 px. It fades out
with the walk weight.

## Rig dimensions (units of `height`; px at 90)

- Thigh and shin 0.235 (21.2 px each). Hip height when standing is about 0.5.
- Torso: hip to shoulder 0.25 (22.5 px), hip to neck 0.29. Capsule width 0.15 (13.5 px).
- Head radius 0.062 (5.6 px), set 0.06 above the neck.
- Upper arm 0.16 × 0.065 wide; forearm 0.15 × 0.055 wide.
- Thigh width 0.095, shin 0.075, boot 0.058 × 0.09 long.
- Hat: brim 0.235 long (21 px) × 0.035 thick (3.2 px), set further forward than back. Crown is a
  dented trapezoid, 0.136 wide at the base, 0.112 at the top, 0.08 tall.
- Pack: an 8-point rounded outline spanning 0.035 to 0.19 behind the spine (14 px deep) and 0.035
  to 0.32 up the spine (26 px). It rises above the shoulders but stays under the brim. Bedroll: a
  disc of radius 0.05 at the back top corner (the roll is strapped across the pack, so it is seen
  end-on).
- Measured standing height at `height = 90`: 89 px from the ground line to the hat top.

## Primitive counts

- Body without the pack: 14 (2 arm capsules ×2, 3 leg capsules ×2, torso, head circle, brim
  capsule, crown polygon).
- Pack: +2 (outline polygon, bedroll disc).
- Fire rim (`lit_from != 0`): +2 (torso or pack edge, head chord).
- Carried item: wood 3; mug, bag and jar 2 each; book, stick and rock 1 each.
- Worst case is the pack, rim and wood together: 21, inside the budget of 22. The test checks
  every pose × carry × facing × lit × pack combination, 12 walk phases and every blend tick.
- Cost on the laptop: about 27 µs per tick plus a full-frame draw at 21 primitives, and 0.18 µs for
  `hikerBounds`. `sizeof(Hiker)` is 168 bytes. A draw builds its primitive list on the stack
  (about 1.8 KB) and does not cache it.

## What changed after looking at the sheets

1. The bedroll was a horizontal capsule across the top of the pack, level with the hat brim. At
   both 90 and 60 px the two merged into one bar and the hat disappeared. The pack top was lowered
   to 0.32 and the bedroll became an end-on disc at the back corner. Hat and pack are now two
   separate shapes in every pose and in the 60 px walk.
2. The limbs read as sticks. Torso, thigh, shin, arm and boot widths went up by 10 to 15%, and the
   brim and crown were made wider.
3. CARRY with a stick pointed it straight forward at waist height, which reads as a rifle. The
   stick is now held angled down and ahead (poking the fire, roasting).
4. Mug, rock, jar and book were the same small lump. Each is now 20 to 40% larger, and HOLD_MUG
   raises the mug to the chin, so the handle and the jar's lit lid read where they are used.
5. WAVE was hard to tell apart from REACH_UP. The elbow now juts forward with the forearm upright.
6. CROUCH looked like sitting on an invisible chair. It is now a deep squat with the knees above
   the hips.
7. HANDS_ON_HIPS: the back elbow is hidden by the pack, so the front elbow was pushed further out
   to carry the pose.

## API additions beyond the spec

- Public fields `pack_on` (default true; stage 4 clears it when the pack is set down as a prop) and
  `lit_from` (-1, 0 or +1).
- `HikerPose` is public, and so is the composed pose `h.pose`.
- `hikerPoseDef(PoseId)` returns the table entry and `hikerPoseName(PoseId)` returns a name for
  `--log-beats`.
- `hikerLastDrawPrimitives()` reports how many rasterizer calls the last `hikerDraw` made.
- `hikerTick(h, 0)` recomposes the pose without advancing time.
- `feet.x` anchors the hips: the body stands over it, sits on it, and lies with its hips on it.

## What I'd improve

- LIE with the pack on: the pack is left out of the grounding, so it is drawn just below the ground
  line under his back. On flat ground that reads as lying on the pack, but stage 4 should probably
  set the pack down before a nap.
- Rim light at night: ACTOR_LIGHT (0x0C121C) is barely different from ACTOR (0x05080F) in the night
  key. The rim only reads when the dusk or fire palette lifts ACTOR_LIGHT. That is a palette
  decision for stage 4, when the fire light pool exists.
- The far arm is mostly hidden behind the torso and pack. A slight 3/4 offset of the far shoulder
  would let it read in CARRY and HANDS_ON_HIPS.
- Turning is an instant flip of `facing`. A 2 to 3 frame squash (narrowing x) would sell the turn.
- In the walk, the swing knee and a fixed foot pitch give a slight skate at heel strike. Lifting
  the toe at heel strike would fix it, at the cost of one more angle.
- Ground contacts use the joint points only, not the hands, so crouch and blow hands can dip a
  pixel or two below the ground line.
- Small props are still one or two primitives. At 60 px, mug, rock and jar are only told apart by
  context (pose and scene).
