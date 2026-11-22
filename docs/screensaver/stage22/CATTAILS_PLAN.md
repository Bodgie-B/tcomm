# Reeds and cattails along the stream, and the stem in his mouth: design and build plan

Written 2026-11-18 from Mike's requests of 2026-11-17 and 2026-11-18. Design only: nothing here is
built. Every number marked PROPOSED is a starting value to tune with the stills. Everything about how
it looks is UNVERIFIED until Mike has seen it, and on the panel. References like `ambient.cpp:63` are
lines on 2026-11-18 in `screensaver/core/scene/`; they drift, search for the name. Facts about the code
were read in the tree unless marked UNVERIFIED.

This piece is one of five in stage 22 (long-range shooting, cattails, mushrooms, the deer visit, Tai
Chi). It owns the plants and the geometry interface the other four use (section 6).

---

## 0. Mike's words

- 2026-11-17: "adding reeds and cattails by the stream and having the hiker sometimes pick one and
  put in his mouth like huckleberry finn".
- 2026-11-18: "I would like the reeds and cattails to feature as a passive content that is in all
  daily modes from morning until night."
- For all five pieces: "Consider other interactions with animals and these from other vignettes."
- Standing rules: natural, classic, unhurried; "when something feels rushed, add stillness, not more
  motion"; no ambient sound; the evening spine scripts are approved and do not change.
- Offered earlier and not rejected: cattails swaying with the real wind, seeding fluff in autumn.

---

## 1. Proposed answers to the open choices

Build the defaults. Each alternative is one constant or one small switch.

| Choice | Proposed default | Alternative |
|---|---|---|
| Where the plants live | 5 clumps at fixed kinds of place (section 2): two on the near bank in front of the water, three on the camp-side bank | `kReedSites` mask: drop the fireside or the far-right clump |
| Static cache or drawn live | Drawn live, each clump one ground actor sorted at its base. A short sedge fringe at the waterline is the only part in the cache (section 2.4, why) | none: the cache cannot hold a thing that stands in front of moving things |
| How big | Waist high to the hiker on the camp bank (25 to 34 px), bigger in the foreground (54 px, 48 showing) | `kReedScale` per site |
| What he picks | A slender reed stem (not a cattail head): the upper stem slides out of its sheath, the stub stays | a cattail leaf blade |
| How the pick is staged | A pause overlay, like the weather's hat gag: no plan entry, no loop RNG, no pinned line moves. Two forms: a seated reach from his place by the fire, and a short stroll to a clump within 90 px | a morning and midday filler `reed_pick` (moves those families' pins; the evening weight stays 0) |
| How often | 45 % of camp loops want it; at most once a loop; from 60 s in | `kPickChance` |
| How long it stays in his mouth | 150 to 260 s of story time, through beats, fishing, naps and walks; it drops out by itself in beats that use his mouth | `kChewMin/Max` |
| Season | From `Inputs::month` (exists, `ss_app.h:60`): green in summer, straw in winter, fluff in October and November. Month 0 (no clock, the tests) is the late-summer look | fixed late-summer look (`kReedSeasons = false`) |
| Wind | The same wind field the grass uses (`weatherSwayMul` and the ambient gust wave). There is no wind number from the house: `house_weather.h:33` says an OUTSIDE reading has no wind | a later `Inputs::wind` (0..1) from WeatherGate, one line in `reedsWind` |
| Small life | A dragonfly by day, three fireflies at dusk, autumn fluff, and one new minor: a red-winged blackbird that lands on a head and rides it | plain black bird (no shoulder patch); or no new minor |
| Lookout and sunrise | None. Those loops have their own landscape with no stream (header grep only, UNVERIFIED beyond that) and share the bytes this state lives in | none |

---

## 2. Where everything is (frame pixels, 800 x 480)

### 2.1 The stream, as generated

`generateStream` (`landscape.cpp:63`): centre line through (800, 372), (640, 386), (470, 420),
(300, 448), (120, 476), interior y jittered by 5 px; half width 20 px at the right edge to 32 px at the
left. The normal points up the screen, to the camp. So:

- the **camp-side waterline** `top(x) = cy - half` (from `landscapeStreamSpan`, which returns the
  drawn half width): about y 353 at x 770, 388 at x 512, 419 at x 302;
- the **near waterline** `bot(x) = cy + half`: about y 434 at x 530, 455 at x 426, 470 at x 340, and
  below the frame's bottom edge left of about x 310.

Examples below use the fire at F = (250, 398) (F.x 238 to 262, F.y 393 to 403) and camp_x 560.

### 2.2 The five clumps

All places come from the landscape and the camp layout (`spineLayout`, `spine.cpp:2829`), with no RNG
draw, so a seed keeps its plans. `reedsInit` works them out once at loop start.

| id | name | base x | base y | cattails | reeds | tallest (px) | dirty box rows | notes |
|---|---|---|---|---|---|---|---|---|
| 0 | RS_FORE | clamp(F.x - 22, 212, 244) | min(bot(x) + 2, 488) | 3 | 1 | 54 (about 48 showing) | 430..480 | the foreground stand, rooted at the frame's bottom edge in the near shallows |
| 1 | RS_NEAR | clamp(min(F.x + 176, mush_x - 40), 404, 440) | bot(x) + 2 | 2 | 1 | min(38, base - 419) | 417..base+3 | near bank, in front of the water |
| 2 | RS_UPSTREAM | clamp(F.x + 262, 500, 528), then clear of rocks | top(x) + 1 | 2 | 2 | min(33, base - 354) | 352..base+3 | camp bank between the skip-stone spot and the boulder |
| 3 | RS_FIRESIDE | F.x + 54 | top(x) + 1 | 1 | 2 | set by his seat, 22..36 | base-38..base+3 | camp bank just behind where he sits by the fire; the seated pick |
| 4 | RS_FAR | 770 | top(x) + 1 | 1 | 1 | min(25, base - 322) | 321..base+3 | camp bank far right; the frog and the visiting deer pass through it |

All PROPOSED. Rules that go with the table:

- **Height caps keep each clump in two strips** (strips are 32 rows): RS_FORE and RS_NEAR in strips 13
  and 14 (rows 416 to 479), RS_UPSTREAM in 11 and 12 (rows 352 to 415), RS_FIRESIDE in 11 to 13,
  RS_FAR in 10 and 11.
- **RS_FIRESIDE** exists only when `base.y - camp.sit.y` is 20 to 36 px and no landscape rock is within
  12 px of it; otherwise the loop has four clumps. Its first reed is cut to length so its tip stands at
  `camp.sit.y - 4`, 14 px behind his hips when he sits facing the fire: within reach of a hand swung
  back (section 4.1).
- **RS_UPSTREAM rock rule**: if a landscape rock's x extent comes within 16 px of the base and its base
  line within 14 px of the waterline, move to `rock.x1 + 12` (right of it; he sits on the left of his
  rock, `pickRock` `spine.cpp:2554`), capped at 540; if that fails, `rock.x0 - 12`, floor 492.
- **RS_FAR** is present in about 70 % of loops (bit 3 of a hash of `land.camp_x` and `land.mush_x`), so
  the right side is not the same every loop.
- **Kept clear** (checked against the code): the chip box x 538..796, y 412..476 and the quiet corner
  (no clump is right of x 462 below y 412; RS_FAR ends at y 357); the boulder's bank x 592..632; the
  mushroom (RS_NEAR is at least 40 px left of `mush_x`, and the near bank right of RS_NEAR.x + 24 is
  left free for the mushroom piece); the bather's seat in the stream at F.x + 126 (`majors.cpp:3085`;
  RS_NEAR is 50 px right of it, so the cattails never screen the bath); the fishing spot F.x + 76, the
  cool spot F.x + 95, the wade F.x + 102, the listen seat F.x + 112, the cairn F.x + 150 and its stones,
  the skip spot F.x + 205, the fetch's bank point F.x + 163; the ford x 652..732; the plinking spot
  x 150..180 (RS_FORE begins 26 px right of the kneeling shooter and 60 px below his muzzle line).
- **Known neighbours, on purpose**: the heron stands at x 400..480 in the near shallows
  (`stream.cpp:669`), so it will sometimes stand just behind RS_NEAR with its legs in the stalks; the
  bather's hat lands at F.x + 48 on the bank (`majors.cpp:3064`), in front of RS_FIRESIDE's left
  leaves; the fisherman stands 22 px right of RS_FIRESIDE with his back to it. All three sort
  correctly because the clumps are actors. Look at them in the stills.

### 2.3 What a clump is made of

Everything is flat fills in mixes of existing slots (all 32 are taken; no new slot):

- **A leaf fan**: one polygon, a tussock with 5 blades (RS_FORE has a second, darker fan of 6 behind
  it). Blade j stands at `base.x + (j - 2) * 2.6 * s`, is 0.62 to 1.0 of the leaf height (a hash), leans
  outward (`(j - 2) / 2.5 * 0.42` plus a hash of 0.12, as dx over height) and arcs: its mid point at
  55 % of its height carries 45 % of its lean. Valleys between blades sit at 22 % of the height, so
  the bottom reads as one mass and the tops as blades. Leaf height is 0.8 of the tallest cattail.
  21 points (`SS_MAX_POLY_POINTS` is 64, `raster.h:35`; concave is allowed).
- **Cattails** (the head bearers): stems at `base.x + {-5, +3, +8} * s`, heights 1.0, 0.86 and 0.74 of
  the clump's tallest, a small standing lean of -0.05, +0.08, +0.14. Each is two primitives: the stem,
  a 6 point polygon 1.6 px wide at the foot and 1.1 px at the head, through three stations (50 %, 78 %,
  100 % of the height, displaced by 0.30, 0.72 and 1.0 of the tip offset d, so it bends like a rod
  and not like a hinge); and the head, a capsule from 78 % to 93 % along the top segment, 4.6 * s px
  wide. The stem's last 7 % above the head is the bare spike.
- **Reeds** (the slender ones he picks): stems at `base.x + {-9, +11} * s`, 1.12 of the tallest
  cattail, 1.2 px wide, more flexible (1.3 of the offset), ending in a small drooping plume 7 px long
  that hangs downwind. One polygon each (5 stem points, 3 plume points). The lower 55 % is the sheath:
  when the upper stem is picked or bitten off, only the sheath is drawn, with a cut top.

Colours (computed per draw from the palette, so they follow the mood, the night rule and the weather):

| part | summer (and month 0) | notes |
|---|---|---|
| leaves | `mixRGB(GRASS, FOLIAGE_DARK, 0.45)` | deeper than the grass tufts, so reeds read as reeds |
| back fan (RS_FORE) | leaves mixed 0.3 toward ACTOR | depth |
| stems | `mixRGB(GRASS, FOLIAGE, 0.30)` | |
| heads | `mixRGB(TRUNK, ACTOR, 0.35)` | dark brown; reads on water, on ground and at night as a silhouette |
| reed plume | `mixRGB(GRASS, CLOUD_SHADE, 0.5)` | pale, feathery |

Sizes at the hiker's 90 px: a camp-bank head is about 4.5 by 5 px, the foreground head 7 by 8 px.

### 2.4 Why not in the static cache

The brief for this piece suggested static bodies in the cache. Read against the code that does not
work: the cache is the back layer, and every dynamic thing is drawn over it (`worldDrawDynamic`,
`world.cpp:741`). The stream's glints are drawn every frame across the water (`ambientDrawGlints`,
step 4) and would paint over any cached stalk on the near bank; walkers on the camp ground would
paint over cached stalks on the camp bank. The boulder solves this by being drawn twice and
repainting itself over a union of everything that crossed it (`streamDrawNear`, `stream.cpp:1606`).
For five thin clumps that bookkeeping costs more code than it saves: the split would still redraw
every swaying stalk each frame and would save only the five leaf fans. So each clump is one ground
actor, drawn whole in each dirty strip it overlaps, like the swaying tufts (`ACT_TUFT`). Their strips
are dirty nearly every frame already (the glints mark their bounds every tick, `world.cpp:648`), so
the cost is the primitives, counted in section 9.

What does go in the cache: **the sedge fringe**. `reedsDrawStatic`, called from `drawLayer`'s
`BL_TUFTS` case, draws about 14 (PROPOSED) low spikes clusters 6 to 12 px tall along both waterlines
in the leaf colour, placed by a hash every 28 to 50 px, skipping 18 px either side of every story
spot listed in 2.2, the boulder's bank, the ford and anything right of x 530 below y 400. It costs
nothing per frame and ties the clumps into a stream that has reeds along it. It has the same
compromise the static tufts have today: a boot at the waterline draws over a spike it should be
behind.

---

## 3. How the plants move and change (all passive)

### 3.1 Wind

The reeds ride the grass's wind so the whole field moves together.

- New `ambientWindAt(const World& w, float x)` in `ambient.cpp`: the value `updateSway`
  (`ambient.cpp:63`) computes for a tuft at x, in px of tip travel on an 18 px blade (already scaled
  by `weatherSwayMul`: 1 calm, up to 2.2 in a gust). `updateSway` calls it, so the grass is unchanged.
- Per clump, on the ambient sub-tick (12.5 Hz, `ambientSubTick`):
  `d = ambientWindAt(w, base.x + 40) * (H / 18) * 0.55`. The 40 px upwind sample makes the heavy heads
  answer a gust a moment after the grass in front of them; 0.55 is their stiffness. Leaves use 1.0 of
  the field at `base.x`, reeds 1.3. Stalk k adds a phase offset by sampling at `x + 9 * k`.
- **Quantized to half pixels** (`int8_t sway_q`). The draw reads only the quantized value and the
  clump is marked dirty only when it changes: in calm air that is 3 to 4 marks a second per clump
  instead of 12.5, and a state compare is one byte.
- Calm air: heads nod about 1 px each way on a 20 s swell, leaves 2 px. A gust (the weather's
  `Weather::gust`): up to 6 px at a camp-bank head, 10 px at a reed plume, all leaning left (the wind
  blows toward -x). The dirty box pads 16 px each side.
- Nuance: a gust arrives right to left across the clumps (the field's wavelength is about 290 px), the
  leaves go first, the heads follow and overshoot back once.

### 3.2 Touch: brush, bend, take

- **Brush** (automatic). `reedsStep` looks at the hiker, every critter slot in use (the house dogs
  and the stream's deer are critters) and the heron (`streamGuestAt`): a mover whose feet are inside
  a clump's footprint (base.x +- 14, base.y - 8 to + 6) while `Critter::moving` or `Hiker::walking`
  gives the clump's spring a push in its direction of travel (40 px/s of tip speed a tick, capped).
  The spring (one per clump: tip offset and speed, 1.6 Hz, damping 0.25) rings three or four times
  over 1.5 s. The heron gives one push when it lands and one when it leaves. It runs on the ambient
  side: plants keep swinging through a doorbell freeze, which is right.
- **Bend** (asked for): one stalk held to an offset by a hand, a muzzle or a bird's weight. The holder
  calls `reedsBend` every tick; two ticks without a call and the stalk springs back. Two holds at
  most at once.
- **Take**: the upper 45 % of a reed (or, if the caller allows it, a cattail above 60 %) is gone for
  the rest of the loop. The stub springs. Next loop the World is new and it has grown back; nothing
  goes in `Memory`.

### 3.3 Season (`Inputs::month`, the same input the stream's leaves use)

| months | leaves and stems | heads | extra |
|---|---|---|---|
| 0 (no clock) | summer colours, full height | brown, full | dragonfly and fireflies allowed |
| 3 to 5 | new growth 60 %, 80 %, 100 % of full height in a fresher green (`GRASS` 0.2 toward `WATER_LIGHT`); one old straw stalk per clump with a ragged pale head | none on the new growth | blackbird from March |
| 6 | summer colours | slim (3.0 px), green-brown | dragonfly, fireflies |
| 7, 8 | summer colours | brown, full | dragonfly, fireflies |
| 9 | leaves 0.4 toward straw (`mixRGB(GRASS, TRUNK, 0.55)` lifted 0.25 toward `CLOUD_LIT`) | full | dragonfly; a rare fluff seed |
| 10, 11 | leaves 0.7 then 0.9 toward straw; one blade per fan bent over at 60 % of its height | bursting: a pale fluff patch on the upper half (`mixRGB(TRUNK, CLOUD_LIT, 0.6)`), bigger in November | fluff seeds on the wind |
| 12, 1, 2 | straw; two blades per fan bent over; reeds without plumes | ragged, pale, 0.7 of their size | snow caps when it snows |

Colours are mixes of slots at draw time: no palette rebuild, no new state. The month is read at loop
start only (`reedsInit` takes it from the first `reedsSetInputs`; a month change shows at the next
loop).

### 3.4 Weather and light

- **Snow** (`Weather::snow_roof`, the value that whitens the tent): from 0.15 up, each head and the
  bent blades carry a cap in `WATER_LIGHT` (the tent's snow colour, `props.cpp:381`; pale, never
  white). One more capsule per head, in snow only. Quantized to quarters; a step marks the clumps.
- **Frost**: in chill or snow weather heads and blade tips are mixed 0.25 toward `WATER_LIGHT`. The
  weather's kind is fixed for the loop, so no marks.
- **Rain and storm**: nothing of their own; a storm's wind bends them through `weatherSwayMul`.
- **Night**: the palette dims them like everything else; heads and stems read as silhouettes
  against the water, which stays the brightest thing at night. No highlights added.
- **Firelight**: while the fire's intensity is over 0.3, RS_FIRESIDE's colours are mixed 0.18 toward
  `FIRE` (a flag, flipped with a mark). Colour only, no extra primitive.
- **Build-up**: the clumps pop in with the plants (`buildPlantScale` at their base), the fringe with
  the cached tufts.

### 3.5 Small life on them (passive, cheap)

- **Dragonfly** (by day: mood 1.5 to 4.5; clear, wind-free or heat weather; months 5 to 9 or 0). One,
  from the reeds' own random stream. It hovers 1.5 to 4 s at a point 6 to 14 px beside a head of
  RS_NEAR or RS_FORE (a 1 px bob), darts in 0.35 s along a straight line to the next point (up to
  60 px, over the water or to the other clump), and after two to four darts lands on a head or a reed
  tip for 4 to 9 s, wings still, the stalk bent 1 px under it (`reedsBend`). Then it darts off the
  left edge low over the water and stays away 20 to 60 s. It keeps to rows 384 to 470 and x under 520.
  Drawn as the stream's mockup dragonfly (`drawMock`, `stream.cpp:592`) at 0.65 size, 9 px long: body,
  head, two wing pairs that flicker. Four primitives while it is there. The mockup stays as it is.
- **Fireflies** (dusk and night as the minors' `dark()`, `minors.cpp:51`; not in rain, snow, chill or
  storm; months 5 to 9 or 0). Three, one each over RS_NEAR, RS_UPSTREAM and RS_FIRESIDE. Each is a
  pure function of `World::tick`: a slow figure (14 px wide, 8 px tall, periods 7 to 11 s by a hash)
  round a point 10 px above the clump's leaves, lit for 0.5 s every 3.1 to 4.7 s. Drawn only while
  lit: one dot of radius 1.3 in `STAR` (a night highlight slot). Marked only while lit.
- **Fluff seeds** (months 10 and 11; a rare one in 9). Up to three at once, own stream. In a gust
  (`Weather::gust` over 0.5) one leaves a bursting head every 6 to 14 s; in calm air every 25 to 50 s.
  It drifts left at 30 to 60 px/s, rising 6 px and sinking on a 2.5 s sine, for 3 to 5 s, then is
  gone. One dot of radius 1.2 with a 2 px wisp, in the fluff colour. Released from clumps 0 to 3
  only and kept to rows 352 and below, so they never dirty the quiet upper strips. These are the
  module's own three slots, not `World::bits`: the bear writes bits 0 to 3 by index and
  `bitStep` stands still in a freeze (`world.cpp:582`).
- **The red-winged blackbird**, a new minor `reed_bird` (section 4.3).
- **Animals that are there already and now touch the reeds for free** (through the brush): the frog
  of `frog_splash` hops in along the camp bank from the right edge to x 680..740 (`minors.cpp:465`),
  straight through RS_FAR; the stream's visiting deer walks the same way to drink at x 668..740; the
  heron lands beside RS_NEAR; a house dog on its way to drink at the cool spot passes RS_FIRESIDE; a
  jumping fish and the floaters pass behind RS_FORE and RS_NEAR.

---

## 4. Choreography

### 4.1 The pick, seated (form A, about 6.4 s)

When: a pause, he sits at `camp.sit` facing the fire (so his back is to RS_FIRESIDE), nothing else has
him, and 7.5 s of the pause are left. Only an overlay on his arm, lean and head, laid over the pose
after the weather's (a new `reedsApplyPose` in `stepHiker`, as `weatherApplyPose`): his pose, facing,
feet, carry and `HikerFx` are never written, so he is exactly as found when the weight is back at 0.

| t (s) | what he does | what the reed does |
|---|---|---|
| 0.0 to 0.9 | nothing but his head: it lifts 0.08 and tips back a little. He has thought of it. | sways as it was |
| 0.9 to 2.1 | the near arm swings back and down behind his hip (shoulder -1.15, elbow 0.35, eased in and out); the torso leans back 0.10 | from 1.7 s the first reed bends to meet his hand: the module holds its tip at last tick's `hikerHandPos(h, 0)` (one tick behind, as the archery does), eased in over 0.4 s |
| 2.1 to 2.6 | his hand closes on it: a hold. One small tug (the arm 0.04 forward and back) | follows the hand 1 px; its neighbours shiver (a small push) |
| 2.6 to 3.3 | he draws it up and forward (shoulder to -0.4, elbow 0.9) | the stalk stretches after his hand, up to 7 px; at 3.0 s it slides out of its sheath (`reedsTake`): the upper stem, 16 px with its plume, is now in his hand; the stub springs back and rings twice |
| 3.3 to 4.1 | the arm comes round in front (shoulder 0.9, elbow 1.9) | in his hand, plume end out |
| 4.1 to 4.6 | a hold: he looks at it, head down 0.10 | |
| 4.6 to 5.3 | up to his mouth (shoulder 0.9, elbow 2.45: the mug sip's values, `spine.cpp:627`); head back level | at 5.1 s it is in his mouth (section 4.4), plume end out front |
| 5.3 to 6.4 | the hand lingers 0.2 s, then the arm goes back to rest; the lean comes off | at 6.0 s the first slow roll to the other corner of his mouth |

If the hand cannot come within 14 px of the reed's tip at 2.1 s (an odd layout), the arm goes back to
rest and nothing is taken: no error, no pick this pause.

### 4.2 The pick, a stroll (form B, 10 to 14 s)

When: a pause with enough left, he stands or sits still within 90 px of a camp-bank clump (2, 3 or 4)
that still has a reed, and form A does not apply. This is the weather's hat gag pattern
(`weather.cpp:234`): his place, facing, pose and walk path are saved, the pick runs, and he is put
back exactly.

1. If seated, he stands (`hikerSetPose(POSE_STAND, 0.8)`).
2. He walks at 30 px/s (slower than the stroll's 34) to the clump's stand point (`reedsStandPoint`:
   14 px to the side he comes from, 5 px above the waterline, never below y 412).
3. He faces it and stands 0.6 s, looking. Stillness before the reach.
4. `POSE_CROUCH` (0.45 s blend). The near arm reaches down and forward to the nearest reed (0.8 s);
   the reed bends to his hand as in form A; a 0.4 s hold; the pull (0.6 s) and the take.
5. He stands (0.5 s), holds the stem at his chest and looks at it (0.5 s), puts it in his mouth
   (0.7 s), and stays 0.8 s looking out over the water with it. The arm comes down.
6. He walks home at 30 px/s, turns as he was, and sits again if he sat (0.9 s).

Seconds: `8.05 + distance / 15` (60 px: 12.1 s; 90 px: 14.1 s), plus 1 s of margin asked of the pause.
If a beat begins under it anyway (it should not), he is put home at once as the hat gag does
(`gagHome`), with the stem in his mouth if he had it in his hand.

### 4.3 The red-winged blackbird (minor `reed_bird`, 14 to 22 s)

By day, not in rain, snow or storm, months 3 to 10 or 0. It picks RS_NEAR or RS_UPSTREAM, the one
farther from the hiker, and declines if he is within 130 px.

1. It flies in from the nearer frame edge on a shallow curve (the `bird_chip` flight, `flyAlong`,
   `minors.cpp:541`), 2.2 s, and lands on the tallest cattail's head.
2. The stalk dips under it: the head goes 5 px down and 3 px toward the bird's side in 0.25 s, comes
   back 2 px, and settles (`reedsBend` every tick; the bird's feet are set to `reedsTip` every tick,
   so the two can never part). `CritterSlot::sort_y` is the clump's base y + 1, so it draws in front
   of its stalk.
3. It rides the stalk for 8 to 14 s. In wind it swings with the head. Twice: a head turn
   (`CA_HEADTURN`), a pause, a look up (`CA_LOOK`) with the tail dipped. A slow shuffle of one px
   along the head between them. No sound marks.
4. It crouches (the stalk dips 1 px more), springs off (the stalk whips up 4 px past rest and rings
   down over 1.2 s through the clump's spring), and flies out by the far edge.

Told to quit (a beat that owns the critters begins): step 4 at once. If he walks near, the director
scatters it as any bird; the module sees the slot gone and lets the stalk go.

The shoulder patch: `Critter::tone = kToneRedwing (2)` on a `CR_BIRD` adds one small primitive at the
wing's shoulder, red (`mixRGB(MUSHROOM, FIRE, 0.5)`) over a 1 px yellow bar (`FIRE_CORE`), following
how the cardinal is done (`critters.cpp:1131, 1805`, inside `SS_XMAS`). UNVERIFIED how the tint table
takes a new entry outside the Christmas switch; if it takes the bird past its 7 primitives or needs
more than about 25 lines, ship the bird plain black and say so.

### 4.4 The stem in his mouth (carried through later beats)

State `chew` in the module: none, in hand, in mouth, falling. In the mouth it is one capsule 1.2 px
wide and 13 px long (9 px at the Lookout's size, but it never reaches a Lookout loop), stem colour,
with the 3 px plume at its end until it wilts, drawn right after `hikerDraw` in the `ACT_HIKER` case.
It needs one new helper in `hiker.cpp`, `hikerMouthPos(const Hiker&, Vec2* dir)`: the mouth point on
the face (head centre plus 0.85 of the head radius toward the facing, 0.3 of it below centre, turned
with the head as `hikerDraw` turns it) and the unit vector the face points along. So it is right
when he stands, sits, crouches, lies on his back for a nap (it points at the sky and rises and falls
with his breathing) and holds a fishing rod.

How it moves (all from the story clock and his walk phase; no state):

- rest angle 0.35 rad above the face's forward line;
- **the roll**: every 3.5 to 6 s (a hash of the roll's number) it travels to the other corner of his
  mouth: the angle goes from +0.35 to +0.10 or back over 0.4 s, eased. This is the Huckleberry Finn
  read: a slow, idle shifting of the stem;
- **the chew**: every 7 to 11 s two quick small nods (0.08 rad, 0.25 s each);
- **walking**: a bob of 0.06 rad at twice the walk phase, a little behind his steps;
- **wilting**: over its last 30 s the outer third droops, up to 0.5 rad, and the plume falls off.

How it ends:

- **A toss, when a pause allows** (from `chew_until`, he is free for 3 s as in 4.1): the near hand
  goes up to his mouth (0.6 s), takes it, and flicks it away low to his front (0.4 s). It tumbles for
  0.5 s and is gone; if it comes down on the water and the stream has a free floater slot it becomes
  a small drifting stick (`streamAddStick`, section 8) and rides away downstream.
- **It drops by itself** (0.5 s fall from the mouth, then gone) when: a beat that uses his mouth
  begins (by name: `fire`, `dinner`, `bear`, `close`, `coffee`, `breakfast`, `wash`, `lunch`, `echo`,
  `bathe`, `ants_cookie`, `rain_shelter`, `shadow_theater`, `raccoon_pack`, `leave`, `day_hike`); the
  target pose is `POSE_BLOW` or `POSE_YELP`; it starts to rain or snow; or 40 s have passed after
  `chew_until` with no pause to toss it in.
- **It is simply gone** when he leaves the scene (`fx.on` false) or the loop ends.

It stays through: every pause, the fillers (`read`, `cloud_watch`, `stream_listen`, `cairn`,
`skip_stone`, `mug_sunset` up to the first sip: add `mug_sunset` to the drop list if the stills show
the mug passing through it), the three fishing majors, `nap`, `map`, `afternoon`, `wade`, `foodbag`,
`setup`, `firewood`, the day majors and the doorbell wave. The four new pieces may ask for it or end
it (section 6).

### 4.5 What keeps it from looking stiff

- Every action starts with a short stillness (the head first, then the arm).
- The reed gives before it lets go: it stretches, then slips, and the stub rings.
- Nothing in a clump moves in step: leaves lead, heads lag, reeds overshoot, each stalk is a few
  hundredths of a second out of phase with its neighbour.
- The stem in his mouth has three slow, unrelated rhythms (roll, chew, walk bob), none periodic by eye.
- The bird makes the stalk dip, recover and settle, and leaves it ringing.

---

## 5. Variants

- **Layout**: four or five clumps, RS_FAR in about 70 % of loops, RS_FIRESIDE when the bank fits;
  blade heights and leans from a hash of the base position; RS_UPSTREAM moves for rocks.
- **Pick**: form A or B by where he is; which reed (the first present one of the clump); about 45 %
  of loops; not in rain, snow or storm; not while a house dog is being patted (`HouseDogs::pat`).
- **Season**: seven looks by month (3.3).
- **Life**: dragonfly by day in the warm months, fireflies at dusk, fluff in autumn, the blackbird
  from March to October; winter has only the plants, the wind and the snow caps.

---

## 6. Interactions

### 6.1 The interface the other pieces use (`reeds.h`)

```
#define REED_CLUMPS 5
#define REED_STALKS 5            // per clump: cattails first (0..n_cat-1), then reeds
enum ReedSiteId : uint8_t { RS_FORE = 0, RS_NEAR, RS_UPSTREAM, RS_FIRESIDE, RS_FAR };

struct ReedClumpInfo {
  bool present;                  // this loop has it
  bool camp_bank;                // false: the near bank (nobody on the camp ground can reach it)
  Vec2 base;                     // root at the waterline (also its sort y)
  float half_w;                  // half width of the leaves at rest, px
  float height;                  // tallest stalk, px
  uint8_t n_cat, n_reed;
};
struct ReedStalkInfo {
  bool present;                  // false: taken this loop (the sheath stub remains)
  bool cattail;                  // false: a slender reed
  Vec2 foot, tip;                // as drawn this tick (wind, brush and holds included)
  Vec2 head;                     // middle of the seed head (cattails; the plume for reeds)
};

// Enumerate.
bool reedsClump(const World& w, int clump, ReedClumpInfo& out);
bool reedsStalk(const World& w, int clump, int stalk, ReedStalkInfo& out);
Vec2 reedsTip(const World& w, int clump, int stalk);
// The nearest present clump to p within max_d px, or -1. camp_only: skip the near bank.
int reedsNearest(const World& w, Vec2 p, float max_d, bool camp_only);
// The nearest present stalk of a clump to p (reeds_only: skip the cattails), or -1.
int reedsNearestStalk(const World& w, int clump, Vec2 p, bool reeds_only);
// Dry ground beside a camp-bank clump for feet: side -1 left of it, +1 right. False for the near bank.
bool reedsStandPoint(const World& w, int clump, int side, Vec2& feet);
// Everything a clump can draw (its fixed dirty box).
Rect reedsBounds(const World& w, int clump);

// Touch. All safe to call from a script action, a major's tick or a minor's step.
void reedsPush(World& w, int clump, float dir, float strength);    // a body through it: dir -1/+1, strength 0..1
void reedsBlast(World& w, Vec2 at, float radius, float strength);  // every clump within radius leans away, then rings
void reedsBend(World& w, int clump, int stalk, Vec2 tip_offset);   // hold one stalk bent; call every tick, lets go by itself
bool reedsTake(World& w, int clump, int stalk, bool allow_cattail = false);   // bite or pick off its upper part

// The wind as the reeds show it now: signed px of lean at an 18 px blade (negative: leaning left).
float reedsWind(const World& w, float x);
// A slow, wide sway laid over the wind, 0..1 (the mushroom dream). Set every tick; falls back by itself.
void reedsDream(World& w, float k);

// The stem in his mouth.
bool reedsChewing(const World& w);
void reedsGiveChew(World& w, float seconds);    // straight into his mouth (a script that staged its own pick)
void reedsDropChew(World& w);                   // it falls from his mouth now
// The pick overlay has him this tick (others wait, as they wait for the hat gag).
bool reedsPickActive(const World& w);
```

### 6.2 The other four new pieces

- **The deer visit**: deer walking through any clump shake it for free (brush). To graze: walk a
  deer to `reedsStandPoint`, play `CA_EAT`, and each tick call `reedsBend(clump, stalk,
  critterMouth(deer) - reedsTip(...))` for a tug, then `reedsTake` for a bitten reed (the stub stays,
  which is the nibbled look the hiker can find on return; a taken cattail with `allow_cattail` leaves
  a headless stem). The fawn "pushes through": walk it across RS_UPSTREAM's footprint; add
  `reedsPush(clump, dir, 1)` on entry for a bigger part. The alarm: the mother's head-up beside
  RS_UPSTREAM reads well against the heads. RS_FAR stands where deer come in from the right.
- **Tai Chi**: the bank between RS_FIRESIDE and the cool spot is the natural mat (feet at about
  (F.x + 78, top - 12)), the clump on his left, the water in front. `reedsWind` gives the form a
  breeze to move with. A slow arm sweep within 20 px may call `reedsPush(clump, dir, 0.2)`. The form
  may end with `reedsGiveChew` only if Mike wants that; default no. The pick overlay never starts
  during it (it is not a pause).
- **Mushrooms**: the near bank from RS_NEAR.x + 24 to x 530 is left free for them. `reedsDream(k)`
  makes every clump sway wide and slow while the sky is strange (amplitude times 1 + 1.5 k, on a
  9 s wave), which costs nothing extra. If he tosses the rest into the stream from the near bank,
  aim right of RS_NEAR. A stem in his mouth drops when he eats (call `reedsDropChew`).
- **Long-range shooting**: `reedsWind(w, x)` is the wind the shooter can read (the heads lean the way
  the bullet will drift); a wind flag should use the same value so flag and reeds agree. A muzzle
  blast near a clump: `reedsBlast(muzzle, 70, 0.6)` (the plinking spot is 45 px from RS_FORE; the
  plinking itself is not edited). A stem in his mouth while he shoots prone is the classic picture:
  keep it (do not call `reedsDropChew`), unless the stills show it crossing the rifle.

### 6.3 Existing things

- **Animals**: see 3.5. The director is not changed. A clump is not an obstacle: nothing routes round it.
- **House dogs**: Lucy and Copper brush a clump when they pass (they are critters). No new dog
  activity in this piece (a new `HouseDogAct` would shift each dog's own random stream and its tests).
  Offered for later: `HA_ROAM` choosing `reedsStandPoint` of RS_UPSTREAM now and then, to sniff. The
  pick does not start while he pats a dog. The blackbird leaves when a dog runs near (the director).
- **Weather**: 3.1 and 3.4. The pick overlay and the weather's gags exclude each other: the weather's
  `hikerFree` (`weather.cpp:171`) gets `&& !reedsPickActive(w)`, and the pick asks that
  `Weather::gag` is none and `hug_w`, `ov_w` and `shiver` are 0 (the test fire and ice already uses,
  `fireice.cpp:791`).
- **Minors**: none starts during a pick (`minors.cpp:1425` gets `|| reedsPickActive(w)`), and the pick
  does not start while one runs or holds his head or arm.
- **Doorbell freeze**: the pick is stepped beside the weather's overlay, after the same early return
  (`if (w.react.active || w.fi.on) return;`, `weather.cpp:711`), and not while the visitors hold him.
  So in a freeze its clock stands still; in form B his walk stands still too (`hikerTick` with dt 0).
  He answers the bell with the stem in his mouth or his hand; the overlay arm is laid on before
  `reactPose`, so the wave wins. The plants, the dragonfly, fireflies and fluff run on (ambient). The
  chew's clocks use `storyTick`, so a bell delays the roll and the wilt instead of using them up. The
  blackbird is a minor: it holds still with the others, and the stalk under it holds its bend because
  the hold is refreshed only by the minor's step: so in a freeze the module keeps a held stalk held
  (holds do not time out while `react.active`).
- **Passer-by wave**: `reactWaveOk` (`react.cpp:31`) returns false while `reedsPickActive` (his hand is
  busy); it keeps trying, as now.
- **Fire and ice, the visiting couple**: both hold the story; the pick does not start or step while
  either is on. The plants keep moving. The ice overlay is in the cache, under the clumps.
- **Status chip**: nothing of this piece is inside x 538..796, y 412..476. The dragonfly and the fluff
  are held to x under 520 below row 400. A tossed stem is thrown to his front and its landing point
  is clamped out of the box.
- **Stream life**: floaters, fish, the wake and the glints are drawn before the actors, so they are
  behind every clump with no change. The heron sorts by its feet, behind RS_NEAR.
- **Plinking, bike, fetch, slackline, ropes, archery, kite**: none is edited. The bike's lane crosses
  x 500 at about y 370, 17 px above RS_UPSTREAM's base, so it rides behind the clump; where the lane
  comes within the footprint the brush shakes it.

---

## 7. Data added

All of it lives beside `StreamLife` in the block a camp loop shares with the Lookout's running state
(`world.h:232`): 624 bytes there, 384 used on the host build with the Christmas mockups (92 padded to
96, plus 288), 288 on the panel. So it adds nothing to `World`.

```
struct ReedSite {              // 10 bytes
  int16_t x, y;                // base
  uint8_t h_cat, h_reed;       // tallest cattail and the first reed, px
  uint8_t n_cat, n_reed;
  uint8_t flags;               // present, camp_bank, warm (firelight)
  uint8_t gone;                // bit per stalk: upper part taken
};
struct ReedHold {              // 6 bytes
  int8_t clump, stalk;         // -1 free
  int8_t dx, dy;               // half px
  uint8_t ttl, pad;
};
struct ReedSeed {              // 8 bytes: its place is a pure function of age
  int16_t x0, y0;
  uint16_t age;                // ticks
  uint8_t on, speed;
};
struct Reeds {                 // about 216 bytes; static_assert <= 232
  uint32_t rng;                // own stream (a 32 bit generator as housedogs.cpp uses), seeded from a copy of the loop RNG
  ReedSite site[REED_CLUMPS];                    // 50
  int16_t push[REED_CLUMPS], push_v[REED_CLUMPS];   // the brush spring, 1/64 px and 1/64 px per tick: 20
  int8_t sway_q[REED_CLUMPS];                    // half px, what the draw reads
  int8_t d_sway[REED_CLUMPS];                    // last drawn
  uint8_t d_look[REED_CLUMPS];                   // last drawn: gone, snow, warm, holds folded into a byte
  ReedHold hold[2];                              // 12
  uint8_t month, season, day, dusk, snow_q, dream_q, guest_near, pad0;   // inputs and small flags
  // the pick and the stem
  uint8_t want_pick, pick, pick_form, pick_clump, pick_stalk, chew;      // stages; ReedChew
  uint16_t pick_t;                               // ticks in the stage (story time)
  uint32_t pick_after, chew_until;               // storyTick deadlines
  int16_t fall_x, fall_y;                        // a dropped or tossed stem
  uint8_t fall_t, fall_kind;
  int16_t home_x, home_y;                        // form B: where and how he was
  int8_t home_face;
  uint8_t home_pose, home_path;
  // small life
  uint8_t fly, fly_at;                           // dragonfly state; the stalk it sits on
  uint16_t fly_t, fly_len;
  int16_t fly_from[2], fly_to[2];
  uint32_t fly_next, seed_next;
  ReedSeed seed[3];                              // 24
  // dirty tracking
  int16_t d_fly[2], d_seed[3][2], d_fall[2];
  uint8_t d_ff;                                  // fireflies lit last tick (3 bits)
};
```

Budget:

- `world.h`: `Reeds reeds;` after `StreamLife stream;` in the anonymous struct, and the two
  `static_assert`s at `world.h:307` grow by `+ sizeof(Reeds)`. Host build with the mockups:
  96 + 288 + 216 = 600 of 624. Panel: 504 of 624. If the struct comes out over 232, cut the seeds'
  dirty words (recompute from age) before anything else.
- `WORLD_ACTOR_MAX` 130 to 135 (five `ACT_REED` entries, 4 bytes each): **sizeof(App) + 20 bytes**,
  29,856 to 29,876 on the host (the tripwires are 29,992 in `test_xmas.cpp:352` and 30,000 in
  `test_day_majors2.cpp:664` and `test_day_plink.cpp:614`), about 28,660 of 32,768 on the panel. The
  other four pieces need their bytes out of the same 116 left under the first tripwire: the lead adds
  them up before anyone raises a wire.
- `Landscape`, `Memory`, `Hiker`, `HikerFx`, `DayState`, `MajorState`: unchanged. No major id is used
  (25 of 32 stay 25). One minor id (11 to 12 of the minors' own list).
- Stack: a clump's draw builds at most 21 + 12 points; no heap.

---

## 8. Gating, families, weight, loop length

- **Families**: evening, morning (both mornings), midday. Every hour and mood: nothing is gated but
  the small life (3.5). Not the Lookout, not the sunrise (`spineSetPiece`): `reedsStep`, the track
  and the draws are reached only from the camp branch of `worldTick` and `worldDrawDynamic`, and
  `reedsSetInputs` is called inside the same `if (!spineSetPiece(...))` as `streamSetInputs`
  (`app.cpp:282`).
- **Slots, plan, loop length**: none. The plants are scenery. The pick is an overlay inside pauses the
  plan already made; it adds no beat and no second to a loop, and the loop RNG is never drawn (the
  module's stream is seeded from a copy, as the stream's and the minors' are).
- **The pick's gate** (all must hold): the loop wants it (one draw of the module's stream at
  `reedsInit`, 45 %); story time past `pick_after` (60 to 200 s, one draw); none taken yet this loop;
  not raining, snowing or storming; a pause with the form's seconds left (`Minors::beat_left`); the
  weather's `hikerFree` conditions (in the scene, visible, not busy, no major, script done, standing or
  sitting still, no minor, `weather.cpp:171`); no weather gag; no fire and ice, visitors, doorbell;
  `carry == CARRY_NONE`, no item held, hat on; a reed left in a reachable clump.
- **reed_bird**: base weight 0.8 (PROPOSED) in `kBaseWeight`; gate: by day, dry, months 3 to 10 or 0,
  a present clump of the two with its tallest cattail not taken.
- **Draw counts are fixed**: `reedsInit` draws the same number of values whatever it decides
  (want, after, chew length, dragonfly first wait, seed first wait).

---

## 9. File plan

### 9.1 New files (LF, ASCII)

- `screensaver/core/scene/reeds.h`, `reeds.cpp`: the module. Section 6.1's interface, plus what World
  calls: `reedsInit(World&, const Rng& loop_rng)`, `reedsSetInputs(World&, float mood, bool night,
  uint8_t month)`, `reedsStep(World&)` (ambient: wind, springs, holds, dragonfly, fireflies, seeds,
  brush), `reedsStoryStep(World&)` (the pick overlay and the stem's ending; story side),
  `reedsApplyPose(const World&, HikerPose&)`, `reedsTrack(World&, Renderer&)`,
  `reedsActorY(const World&, int clump, int16_t& y)`, `reedsDraw(const World&, int clump, Surface&,
  const Rect&, const Palette&)`, `reedsDrawStatic(const Landscape&, Surface&, const Palette&, const
  Rect&, float scale_of(...))` (the fringe: takes only the landscape, so `landscape.cpp` can call it),
  `reedsDrawAir(...)` (dragonfly, fireflies, seeds, a falling stem), `reedsDrawMouth(...)` and
  `reedsMouthBounds(const World&)` (the stem in his mouth or hand). About 1,000 lines.
- `screensaver/tests/test_reeds.cpp` (section 10).
- `screensaver/tools/reeds_stills.cpp`, `screensaver/tools/reeds_sheet.py` (section 11).

### 9.2 Existing files and the exact edits

| file (line endings) | function | edit |
|---|---|---|
| `core/scene/world.h` (LF) | includes; `ActorKind`; `WORLD_ACTOR_MAX`; the shared struct; the asserts | `#include "reeds.h"`; `ACT_REED` after `ACT_STREAM` (before the `SS_XMAS` entry); 130 to 135 with a comment line; `Reeds reeds;`; `+ sizeof(Reeds)` in both asserts |
| `core/scene/world.cpp` (LF) | `worldBeginLoop` | `reedsInit(w, rng);` after `streamInit(w, rng);` |
| | `sortActors` (camp branch) | after the stream's two: for each clump, `if (reedsActorY(w, i, y)) L.add(ACT_REED, i, y);` |
| | `coverHiker` | `w.hiker_rect` only (not `hiker_full`, which the in-frame tests read) united with `reedsMouthBounds(w)` clipped to the frame |
| | `stepHiker` | `reedsApplyPose(w, P);` after `weatherApplyPose`, before `mailPose` and `reactPose` |
| | `worldTick` | `reedsStep(w);` after `streamStep(w);`; `reedsStoryStep(w);` after `weatherStep(w);` (it returns at once while `react.active`, `fi.on` or `visitorsHolds`); `reedsTrack(w, r);` after `streamTrack` |
| | `worldDrawDynamic` | `case ACT_REED: reedsDraw(...)`; in `case ACT_HIKER`, `reedsDrawMouth(...)` after `armloadDraw`; `reedsDrawAir(...)` after the actor loop, before the smoke |
| `core/scene/landscape.cpp` (CRLF, keep) | `drawLayer`, `case BL_TUFTS` | after the tuft loop: `reedsDrawStatic(ls, s, pal, clip, b)` (it applies `buildPlantScale` per spike itself) |
| `core/scene/ambient.h`, `ambient.cpp` (CRLF) | new `ambientWindAt`; `updateSway` | the tuft's formula moved into the new function and called from `updateSway`; grass output must be bit-identical (test) |
| `core/scene/hiker.h`, `hiker.cpp` (CRLF) | new `hikerMouthPos` | beside `hikerHeadPos`, from the same composed geometry; nothing else changes |
| `core/scene/weather.cpp` (CRLF) | `hikerFree` | `&& !reedsPickActive(w)` |
| `core/scene/minors.h`, `minors.cpp` (CRLF) | `MinorId`, `kNames`, `kBaseWeight`, `gate`, `startMinor`, `stepMinor`, the weather-gag wait at 1425 | `MN_REED_BIRD` before `MN_COUNT`, `"reed_bird"`, 0.8, the gate, `startReedBird` / `stepReedBird`, `|| reedsPickActive(w)` |
| `core/scene/critters.h`, `critters.cpp` (CRLF) | `kToneRedwing`; `buildBird` and its colours | the shoulder patch (4.3); optional, last |
| `core/scene/react.cpp` (LF) | `reactWaveOk` | false while `reedsPickActive(w)` |
| `core/scene/stream.h`, `stream.cpp` (LF) | new `streamAddStick(World&, Vec2 at)` | puts a small `FK_STICK` in a free floater slot at the stream position nearest `at` (false when both are afloat); draws nothing from the stream's random stream |
| `core/scene/app.cpp` (CRLF) | `appTick`, beside `streamSetInputs` | `reedsSetInputs(a->world, m, night || a->in.night_mode, a->in.month);` |
| `tests/test_scene.cpp` (LF) | `kMinors[11]`, `fired_by[11]` in `testMinors` | 12, with `"reed_bird"`; if it cannot fire in the 12 seeds the test runs, find out why before touching the seeds |
| `docs/screensaver/NOTES_FOR_MIKE.md`, `WORKLOG.md` | | this piece's entries and numbers |

Not edited: `spine*.cpp`, `majors.*`, `day*`, `plink.*`, `housedogs.*`, `director.*`, `props.*`,
`palette.*`, `memory.*`, `fireice.*`, `visitors.*`, `sim/`, the firmware. `CMakeLists.txt` globs
`core/*.cpp` and `tests/test_*.cpp` (re-run cmake).

Shared with the other four pieces (they will touch the same files): `world.h`, `world.cpp`,
`minors.*`, `weather.cpp`, `react.cpp`, `test_scene.cpp`. One builder at a time in those, or the lead
merges.

### 9.3 Build order (each step leaves every test passing)

1. The plants: `reeds.*` with the sites, geometry, wind, seasons, snow, the actor, the fringe, the
   interface of 6.1 (push, bend, take, blast, dream), the brush. `test_reeds.cpp`. Stills.
2. The stem and the pick: `hikerMouthPos`, the overlay, both forms, the endings, the guards in
   `weather.cpp`, `minors.cpp`, `react.cpp`, `streamAddStick`.
3. Small life: dragonfly, fireflies, fluff.
4. The blackbird minor, then its shoulder patch.

Steps 1 and 2 are Opus work (geometry that must match its bounds, an overlay that must leave him as
found). Steps 3 and 4 and the stills tool suit Sonnet with this plan in hand.

---

## 10. Tests (`tests/test_reeds.cpp`, the pattern of `tests/test_stream.cpp`)

- **Sites**: over seeds 1 to 300, every present clump's base is on its waterline (within 3 px), inside
  the frame's x, outside the chip box and the quiet corner, at least 40 px from the mushroom (RS_NEAR),
  40 px from F.x + 126 (RS_NEAR), outside the boulder's bank; RS_FIRESIDE's first reed tip is 12 to
  18 px behind `camp.sit` and within 6 px of its height; each dirty box fits its two (or three)
  strips; the same seed gives the same sites; the loop RNG's state after `worldBeginLoop` is what it
  was before this piece (compare the loop seeds of 10 loops with a build without it: the pinned plan
  lines of `test_spines.cpp` and `test_midday.cpp` must pass unchanged).
- **Bounds hold every draw**: each site, every month 0 to 12, snow 0 and 1, wind at calm and at a
  full gust, a full push each way, a hold at its largest offset, every stalk taken and not: drawn on
  a blank surface, no pixel outside `reedsBounds`. The same for the fringe (inside its rows), the
  dragonfly, a firefly, a seed, the mouth stem in every pose in `POSE_COUNT` and both facings (inside
  `reedsMouthBounds`).
- **Dirty covers change**: a camp loop of each family rendered incrementally equals a full redraw at
  20 checkpoints, with wind weather forced; a tick where no quantized value changed marks nothing for
  the clumps.
- **Grass unchanged**: `Ambient::sway[]` for 2,000 ticks of seed 1 is bit-identical before and after
  the `ambientWindAt` refactor (capture the values first, pin a hash).
- **Interface**: push rings and comes to rest within 3 s; a hold not refreshed lets go within 2 ticks
  and the stalk is at rest within 2 s; `reedsTake` twice returns true then false; a taken reed is not
  offered by `reedsNearestStalk`; `reedsStandPoint` is on dry ground at y 412 or less; `reedsBlast`
  moves only clumps inside the radius.
- **The pick, form A**: a forced run (a test hook sets `want_pick` and `pick_after = 0`): during it his
  `Hiker::pose` target, facing, feet, carry and every `HikerFx` field are bit-identical to a run
  without it at the same ticks (only the composed pose differs); afterwards the composed pose matches
  too; the hand is within 3 px of the reed's tip from 2.1 s to 3.0 s; the reed is taken; the stem is
  in his mouth.
- **Form B**: he ends at his saved place, facing and pose; a beat forced to begin under it puts him
  home on that tick and the beat's script runs with no overrun.
- **The stem**: it survives a filler, a fishing major and a nap (find seeds); it drops in the first
  tick of `dinner`; it is gone at loop end; its clocks stand still through a doorbell (the byte
  snapshot pattern of `test_day_majors2.cpp:571`); a doorbell during each pick stage leaves the story
  byte-identical after the reaction, shifted by the frozen ticks.
- **Exclusion**: with wind weather forced, the hat gag and the pick never overlap in 60 seeds; no
  minor starts during a pick.
- **Small life**: the dragonfly never by night, in rain, or in months 10 to 4; it stays in rows 384
  to 470; fireflies only when dark; seeds only in months 9 to 11 and never above row 352; none of
  them in a Lookout or sunrise loop (the module's bytes are untouched there: a canary).
- **reed_bird**: it fires in `testMinors`' seeds; the bird's feet equal `reedsTip` on every perched
  tick; zero forced ends; the stalk is at rest 3 s after it leaves.
- **Budgets**: `sizeof(Reeds) <= 232`; `sizeof(App)` printed and under both tripwires; strips per
  frame over whole loops of the three families no more than 0.3 above the same seeds without the
  piece (build the baseline first and write its numbers down); primitives per frame printed.
- **Determinism**: two runs hash the same frames; SS_XMAS on and off; `ss_soak` clean.

---

## 11. Stills to render for review (`docs/screensaver/reviews/stage22-cattails/`)

`tools/reeds_stills.cpp` (pattern: `tools/stream_stills.cpp`; built by hand against the build
folder's `libss_core.a`), `tools/reeds_sheet.py` (pattern: `tools/stream_sheet.py`, zlib only).

1. The whole scene with the reeds, seed 1 and two more layouts (one with RS_FAR, one without; one
   with RS_FIRESIDE absent), at moods night, dawn, morning, day, golden, dusk: 12 tiles.
2. A 3x crop of each clump at day and at night.
3. The season strip: RS_NEAR and RS_FORE cropped for months 0, 4, 6, 8, 9, 10, 11, 1, and month 1
   with snow caps.
4. Wind: calm, the leading edge of a gust (leaves gone, heads not yet), the full gust, the swing
   back: 4 crops of RS_UPSTREAM.
5. Before and after: the same frame with and without the piece, to judge clutter round the fire.
6. The pick, form A: 8 frames (the look, the reach back, the grasp, the stretch, the slip with the
   stub ringing, the look at it, to the mouth, at rest with it). Form B: 6 frames.
7. The stem in his mouth: sitting by the fire at night, walking, fishing (`fishing_good`), the nap
   on his back, reading, the doorbell wave, the wilted last seconds, the toss, the stick floating off.
8. Neighbours: the heron behind RS_NEAR; the bather with his hat by RS_FIRESIDE; the fisherman beside
   it; the frog in RS_FAR; Lucy passing RS_FIRESIDE; the plinking shooter beside RS_FORE.
9. Small life: the dragonfly hovering and perched (3x); three fireflies at dusk (two lit); fluff on
   a November gust; the blackbird landing (the dip), riding, leaving (the whip).
10. With `--chip`: the bottom right with both dog badges, to show nothing meets the chip.

---

## 12. Cost estimate (all UNVERIFIED until measured; the panel most of all)

- **Primitives per frame**, always: RS_FORE 9 (two fans, three cattails at two each, one reed),
  RS_NEAR 6, RS_UPSTREAM 7, RS_FIRESIDE 5, RS_FAR 4: **31** with all five clumps, plus 1 for the stem
  in his mouth. Now and then: dragonfly 4, fireflies up to 3, seeds up to 3, snow caps up to 9. The
  guide's aim is under about 150 for the frame; the 14 swaying tufts are about 35 today.
- **Pixels**: about 3,500 to 4,000 px of fill a frame if every strip were redrawn every frame; at the
  measured 1.3 px per microsecond that is about 3 ms on the panel, before per-primitive overhead.
  If that is too much, in order: drop RS_FORE's back fan (`kReedBackFan`), drop RS_FAR, drop
  RS_FIRESIDE's second reed.
- **Strips**: no new strip in most frames: RS_FORE and RS_NEAR live in strips 13 and 14, RS_UPSTREAM
  and RS_FIRESIDE in 11 to 13, where the glints, the grass and his breathing already mark every
  frame. RS_FAR reaches strip 10 (rows 321 to 351), which is quiet in a morning with no smoke: with
  half-pixel quantizing it is marked about 3 times a second in calm air, roughly +0.1 strip a frame.
  Fireflies and seeds stay in strips 11 to 14. Expect +0.1 to +0.3 strips a frame; the test allows 0.3.
- **Palette rebuilds and cache**: none added. The fringe is drawn with the cache whenever it is
  rebuilt (about 30 small triangles, once per rebuild).
- **Code**: about 1,000 lines in `reeds.cpp` plus about 150 elsewhere; by the plinking's measure
  (1,777 lines gave 34 KB at -Os on x86) about 20 to 24 KB of flash and the same of PSRAM while the
  screensaver runs (745 KB and 646 KB free on 2026-11-18). The builder reports the object size.
- **RAM**: sizeof(App) + 20 bytes (section 7). Per tick: 5 wind samples on the sub-tick, a scan of 11
  critter slots against 5 boxes.

---

## 13. Risks

1. **Clutter round the fire.** RS_FIRESIDE stands 14 px behind his seat, where the fishing, the bath
   and the dog's drink also happen. It is the smallest clump on purpose and it is one bit in
   `kReedSites`; the before-and-after still (section 11, item 5) is the first thing to look at. If Mike wants the fire's bank clean, the
   seated pick goes with it and only the stroll remains (fewer picks, since few pauses are long enough).
2. **Draw time on the panel.** 31 more primitives every frame is a real cost and only a panel run
   says how much. The three cuts in section 12 are ready.
3. **The tripwires on sizeof(App).** 116 bytes are left under 29,992 on the host for all five pieces;
   this one takes 20. A piece that needs more must use a union or raise the wire with Mike's yes.
4. **The shared block is nearly full on the host build** (600 of 624 with the Christmas mockups).
   `Reeds` must stay under 232 bytes; the assert will say.
5. **The overlay that walks him (form B)** has the hat gag's hazards: a beat starting under it, the
   weather or a minor taking him mid-way. The guards are listed; the exclusion tests must be real.
   If it proves fragile, ship form A alone first.
6. **The mouth point.** `hikerMouthPos` must follow the head exactly as `hikerDraw` turns it, in
   every pose; lying down and the fishing poses are the ones to check in the stills. A stem that
   floats beside his face is worse than none.
7. **The new minor shifts the minors' random stream for every seed** (not the plans). Loops will
   show different minors at different times than before; `testMinors` needs its lists updated and
   may need its seeds looked at.
8. **`ambientWindAt` must not change the grass** by a bit, or incremental and pinned frame checks in
   other suites move. Pin the sway hash before the refactor.
9. **The heron and RS_NEAR.** It may land with its body behind the stalks. If that reads badly, move
   its spot 16 px when it falls within 14 px of the clump (one line in `stream.cpp` `visitBegin`,
   no extra random draw); not done by default because `test_stream.cpp` may pin its spots (UNVERIFIED).
10. **Height.** Real cattails are head high; these are waist high so they stay in two strips and
    leave the stage open. If Mike wants them taller, the camp-bank clumps cost a third strip each.
11. **No wind number from the house.** "Swaying with the real wind" today means: when the OUTSIDE
    forecast makes the loop's weather windy or stormy, and in the weather's own gusts. A true wind
    speed needs a WeatherGate field and one `Inputs` member; it is not in this plan.
12. **Line endings.** Seven of the edited core files are CRLF (`ambient.*`, `hiker.*`, `minors.*`,
    `weather.cpp`, `app.cpp`, `critters.*`, `landscape.cpp`); `world.*`, `stream.*`, `react.cpp` are LF.
    Byte-safe edits, as the plinking plan asked.
