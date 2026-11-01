# Stage 5 notes

## Critter rigs

Module: `screensaver/core/scene/critters.h` and `critters.cpp`. Test: `screensaver/tests/test_critters.cpp`.
Placed in the scene since: see "In the scene" below; the minor effects are in "Minors" at the end.

Sheets written by `test_critters` into `build/`:

- `critters_day.ppm`, `critters_night.ppm`: one row per kind in enum order (chipmunk, squirrel,
  frog, deer, raccoon, owl, bird, bear, mouse), one 112 px column per action in enum order (idle,
  move, run, sit, look, sniff, reach, climb, shake, hop, blink, headturn, startle, eat, carry,
  rear). Unsupported cells are empty. The same rows follow at scale 0.7. Climb cells draw a
  TRUNK-colored strip at the trunk line. Loops are shown at loop phase 0.3, hops near the top of
  the arc, the owl's head turn mid-way (face turned away), blinks with the eyes shut, startles
  landed.
- `critters_gaits.ppm`: 8 phases per row of every MOVE / HOP / RUN gait each kind supports, then
  the bear's REAR blend (all fours to standing, anticipation dip visible in frames 1 to 2), then
  the climbing gait for squirrel, raccoon and bear.

### Kinds, sizes, budget

Size is the larger of the idle silhouette's height and length (the mouse without its tail),
measured from the drawing by the test. `critterHeight` returns the idle height, feet to top.

| Kind | Nominal | Idle h x len (px) | Silhouette feature | Max prims / budget |
|---|---|---|---|---|
| chipmunk | 20 | 19 x 20 | thin tail straight up | 6 / 7 |
| squirrel | 28 | 29 x 29 | plume tail curled over the back (12-point ellipse) | 7 / 7 |
| frog | 18 | 14 x 20 | Z-folded hind leg, eye bump, front leg propping the chest | 6 / 7 |
| deer | 50 | 48 x 42 | jointed legs with hocks, tapered neck, two long ears | 6 / 7 |
| raccoon | 30 | 18 x 35 | arched back, pointed snout and ear, thick tail with 2 ACTOR_LIGHT rings | 7 / 7 |
| owl | 24 | 26 x 16 | front view: egg body, ear tufts, two ACTOR_LIGHT eye discs with pupils, beak | 7 / 7 |
| bird | 14 | 10 x 16 | head, beak, tail; V wings in flight | 6 / 7 |
| bear | 60 | 59 x 70 | shoulder hump, round ear, long snout, thick paws; 101 px reared | 7 / 9 |
| mouse | 8 | 8 x 9 | round ear, thin two-segment tail | 7 / 7 |

### Actions supported

Y = supported; anything else given to `critterSetAction` becomes CA_IDLE (tested for all 71
unsupported pairs). Loops (idle, move, run, blink, eat, sniff, shake) repeat; the rest hold
their end pose.

| Kind | idle | move | run | sit | look | sniff | reach | climb | shake | hop | blink | headturn | startle | eat | carry | rear |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| chipmunk | Y | Y | Y | Y | Y | Y | | | | | | | Y | Y | Y | |
| squirrel | Y | | Y | Y | Y | | | Y | Y | Y | | | Y | Y | Y | |
| frog | Y | | | | Y | | | | | Y | Y | | Y | | | |
| deer | Y | Y | Y | | Y | Y | | | | | | Y | Y | | | |
| raccoon | Y | Y | Y | Y | Y | Y | Y | Y | | | | | Y | Y | Y | |
| owl | Y | | | | Y | | | | | | Y | Y | Y | | | |
| bird | Y | Y | | | Y | | | | | Y | | | Y | Y | Y | |
| bear | Y | Y | Y | Y | Y | Y | Y | Y | Y | | | | Y | | | Y |
| mouse | Y | Y | Y | Y | Y | Y | | | | | | | Y | Y | | |

What the less obvious ones are:

- Deer SNIFF is grazing (head to the ground). Deer HEADTURN is the "head high, offended" pose.
  Deer STARTLE is a flinch then a head-up freeze; the caller then sends it off with CA_RUN.
- Owl HEADTURN turns the head a full turn in three snaps over `seconds` (eyes slide off the face,
  the back of the head shows, they come back from the other side). Owl STARTLE goes tall and thin.
- Bird CA_MOVE is flight: call `critterSetAction(CA_MOVE)` then `critterMoveTo`, and the move flies a
  low arc (up to 14 px) with V wings flapping. In place, CA_MOVE hovers 6 px up.
- Bear STARTLE drops to a crouch (from a rear if reared) and turns round 45% of the way in; the
  caller then runs it off. Bear SHAKE stands up with arms forward round the trunk and shivers.
- Squirrel SHAKE is the shake-off after the stream splash (shiver, tail flick).
- `carry`: nut, crumb, shoe, granola bar drawn at the mouth, or at the paws while sitting up and
  eating. The bird draws a shoe as a crumb. Deer, bear, frog and owl ignore `carry`.

### Motion

- `seconds` in `critterSetAction` is how long the action takes to land: the pose blend, or the
  whole one-shot (in-place hop, owl head turn, startle). Pose blends land on the table entry
  bit-exactly on the last tick (tested for all 73 supported pairs, 20 ticks for 0.8 s), and loop
  motion fades in over 0.3 s only after that. Startle, rear and reach wind up 15% against the
  move first (bear rear dips to rear = -0.15); sit overshoots and settles; hops crouch first and
  land with a squash (squirrel crouches 4 px and rises 9; frog 3 and 20; bird 2 and 3).
- Gaits: deer and bear walk a four-beat cycle; the raccoon waddles; chipmunk, mouse and every RUN
  bound (fore pair and hind pair half a cycle apart, a short flight, legs stretched). The deer
  bound lifts 10 px. The phase advances with distance covered, so feet keep pace with the ground.
  Hops move only while in the air. The bear's walk sinks the body on each step.
- `critterMoveTo` turns the critter round first when the target is behind: the figure squeezes
  to 15% width and flips facing at the narrowest point (0.24 s). Arrival lands in CA_IDLE with a
  0.22 s settle.
- Climbing: the body turns nose-up with its belly on the line x = feet.x and hind feet at
  feet.y; `critterClimbTo` climbs with diagonal limb pairs and holds CA_CLIMB on arrival.
- Personality: `speed_dial` scales move speed 0.8x to 1.2x (chipmunk over 100 px at 40 px/s: 81
  ticks at dial 0, 55 at dial 1) and in-place gait rate; `jitter_dial` scales idle fidget size
  (0.5x to 1.5x), idle rate and blink rate.

### Numbers

- `sizeof(Critter)` = 184 bytes.
- Test: 519 checks, all pass. 18,144 rendered states (every kind x supported action x facing x
  scale 1 and 0.7, at 3 held phases plus every tick of a 1 s blend and 1.2 s of loop, plus every
  tick of moves, runs, flights and climbs), 7.4 M drawn pixels, 0 outside `critterBounds`.
  Facing -1 mirrors facing +1 to within one channel step: the test allows a difference of 1 and
  measures 1 differing pixel in 137,869 inked pixels (an anti-aliased edge rounding the other way).
  It is not pixel-exact. Strip-by-strip drawing matches a full-frame draw.
- PC cost (tick + draw + bounds, laptop, -O2): bird 1.9 us, mouse 2.2, frog 3.3, owl 3.5,
  squirrel 3.9, chipmunk 4.3, raccoon 4.6, deer 6.1, bear 11.5. Not measured on the board.

### What changed after looking at the sheets

- First pass: the squirrel's and raccoon's tails swung under the body when sitting (a sign error
  in the tail angle); the frog was an unreadable blob; deer and bear legs were straight capsules
  that read as table legs; the owl read as a cat; the mouse's ear was buried in its body.
- Deer and bear legs became one tapered two-segment polygon each (hock behind on the deer, paw
  toe on the bear), same primitive count. The deer's neck and head became one outline, so the
  neck tapers and the deer dropped to 6 primitives.
- The frog was redrawn as a low body sloping up to the head with the hind leg as a Z under it
  (knee forward, long foot back past the rump), the front leg propping the chest, the eye bump
  above the head line. Scaled 1.25x to reach its nominal 18 px.
- The owl became one egg-and-shoulders outline with a separate tuft outline, two eye discs with
  pupils and a beak. Seen from behind (head turn), the tufts stay at the sides.
- The reared bear first read as a blob on a stick: the body now pivots about the middle of the
  rump instead of the bottom, the hind legs thicken and lengthen as it stands, and the far leg
  steps back so both show. The bear is drawn at 1.09x its rig units to stand 59 px on all fours
  and 101 px reared.
- Squirrel sitting: the plume first sat on the back as a heart-shaped blob, then lay flat on the
  ground; it now stands back at an angle. Raccoon sitting: the tail lies on the ground behind.
- The bird's flight V was narrow and tall; the wings now keep their width and flap in depth.
  The peck tilts the body rather than burying the head.

At 0.7 scale every kind is identifiable from its silhouette on the day sheet: chipmunk (upright
thin tail), squirrel (plume), frog (squat with the leg Z, weakest at 10 px), deer, raccoon
(ringed tail), owl, bird, bear, mouse (dot with a long tail line).

### What I would improve

- ACTOR_LIGHT is very close to ACTOR in the day and night palettes, so the owl's eyes and the
  raccoon's rings are subtle. A palette question, not a rig one; check on the panel.
- Foot lift, eye, shiver and the owl's head turn are layered motion, not part of the blended
  pose, so they snap when an action changes mid-loop (at most 3 px, at most once per change).
- The bear's near and far legs overlap when standing on all fours, so it can read as two-legged
  in some walk frames; offsetting the far pair further would help.
- The frog at 0.7 scale is about 10 px tall and reads mainly by its squat shape and hop.
- Climbing down still faces up (fine for the bear backing down, less so for the squirrel, which
  goes head first in life).
- The chipmunk's head is small; stripes are out of reach in a flat silhouette.
- Open question: the raccoon's nominal 30 px is its length with the tail (35 px measured); if the
  caller expected 30 px of body, scale it up.

## In the scene

Written 2026-10-30. Not committed. The critter rigs are now in `World`, the
bear beat replaces the stage 4 placeholder, and the two spine hooks (dinner
visitors, the night theft) are filled. The minor-effect scheduler ("Minors", below) came next
task; it drives critters through the director below.

### Files

- New: `core/scene/director.h/.cpp` (the critter director).
- Changed: `world.h/.cpp` (critter slots, `Memory`, bits, actor list), `spine.h/.cpp` (bear beat, hooks,
  scenes, fixes), `props.h/.cpp` (tent shadow jitter, items held by critters, cones and dust),
  `critters.h/.cpp` (pose offsets, anchors, phase fix), `landscape.h/.cpp` (the bag branch), `app.cpp`,
  `ss_app.h`, `tests/test_scene.cpp`.

### World

- `Critter critters[CRITTER_MAX]` (8) with `CritterSlot crit[8]`: `in_use`, `tag` (`CT_DINNER`,
  `CT_BEAR`, `CT_THIEF`, `CT_MINOR`), `mode` (free, startle, flee), `flags` (`CF_BRAVE` never scattered,
  `CF_SHY` scatters at any move of the hiker), this loop's `speed_dial`/`jitter_dial` (two loop-RNG calls
  per slot in `worldBeginLoop`, applied at spawn), and a walk path (critters move only in x, so `feet.y`
  follows a line, as the hiker's does).
- `ACT_CRITTER` in the base-y sort (a climbing critter sorts at the trunk's foot), `ACT_BIT` for cones
  and dust. `ACT_FGPINE` now redraws a foreground pine over any actor that walks behind it (idx bit 0 the
  pine, the rest the owner: 0 the hiker, 1 + slot a critter), so a raccoon from the left edge comes out
  from behind the pines like the hiker. Every actor push goes through one guarded `add`;
  `WORLD_ACTOR_MAX` is 64.
- Each in-use critter is ticked, its previous and current bounds dirtied every tick it is visible
  (`visible` = its bounds overlap the frame horizontally).
- Items held by a critter: `Item::critter` (1 + slot) and `Item::angle`. The director keeps the item at
  the critter's paw (reared) or mouth; a held stick then points from there at `angle` turns from up.
- `Bit bits[4]`: pine cones (TRUNK ovals: fall at 900 px/s², bounce off a head to the ground, lie on
  their side) and dust puffs (SMOKE, swell and shrink over 0.9 s).
- `Memory` (`shoe_taken`, `cairn_n`, `mouse_hole`, `loop_count`, `last_stolen`): `worldBeginLoop` copies
  it out, clears the whole `World` with `memset` and copies it back, then counts the loop. Only
  `shoe_taken` and `loop_count` are set; stage 6 fills the rest. `appMemoryShoeTaken`,
  `appMemoryLoopCount` expose them.

### Director API (`director.h`)

| Call | What it does |
|---|---|
| `directorSpawn(w, kind, feet, facing)` | A free slot (or -1) with the slot's dials, tag `CT_NONE`, mode free, no flags. Callers set `w.crit[slot].tag` and `flags`. |
| `directorRelease(w, slot)` | Frees the slot; anything it held drops where it is. |
| `directorScatter(w, from, radius)` | Every free, non-brave critter within `radius` startles (0.3 s) and runs (or hops, or flies) away from `from` out of the frame; released once out of sight. |
| `directorAvoidHiker(w)` | Each tick: while he walks, critters whose bounds come within 80 px (40 px in y) of his scatter; `CF_SHY` ones scatter at any move (walking, or a pose blend away from sit, lie, read, mug). |
| `directorClamp(w, slot, to, exiting)` | The one clamp: the whole figure stays in frame (unless exiting) and a move with either end in the corner band (feet y > 417) stays left of x 660 by the critter's half length; an exit there turns left. |
| `directorWalk(w, slot, to, speed, gait)` | Clamped move along a straight ground line. |
| `directorExit(w, slot, side, speed, gait)` | Off the frame; the slot goes to the director and is released out of sight. |
| `directorOwns(w, slot, tag)`, `directorCount(w)` | Owner checks (a scattered critter is no longer its owner's); count for `appCritterCount`. |
| `directorStep(w)` | From `worldTick` after `spineCritterScenes`: hiker rule, modes, rig ticks, paths, visibility, release, held items. |

`ss_app.h` adds `appCritterCount`, `appCritterRect` (slot, unclipped bounds, kind), `appHikerBounds`
(unclipped, deferred fix 1), `appHikerWalking` and the two memory accessors.

Rig additions in `critters.*` (on the drawn pose only, so a new action never blends from them):
`fore_add` (radians on the near fore limb) and `head_add`, and the anchors `critterPaw`,
`critterHead`, `critterMouth` (from the same primitive build as the drawing).

### The bag branch

`LS_BRANCH_ROOT_Y` 212 to `LS_BRANCH_TIP_Y` 202, 82 px to the left (was 302 to 294). The hung bag's
bottom is at y 233: 33 px above a standing hiker's hat at the bag spot (y 266) and about 10 px above
the reared bear's raised paw at its spot under the bag (feet 348, paw 105 px up: y 243). `bagInit`,
`bag_work` and the toss apexes all follow the tip. Viewed: the three food bag variants still read (the
tosses arc higher; the third still goes over and hangs). The branch is now a long bare limb near the top
third of the pine.

### Bear beat (3 variants, `fire.fuel` 1 to 0.45 over the beat)

Shared start: the bear walks in from the right edge at 22 px/s (its speed dial is squeezed to
0.96..1.04x so the beat length holds), stops right of the bag pine and sniffs the air toward the tent with
its nose up for 5 s. The shadow in the tent freezes (stops breathing) as the sniff starts and trembles
from half way (up to 2 px in x, hunched down up to 3 px). The whole beat, the shadow's x follows the
bear's x scaled into the tent (-14..+9 px) with a 0.9 s lag, in whole px. When the bear is out of the frame
the shadow relaxes to its rest and starts breathing again.

| Variant | Beat | Script ends | What happens |
|---|---|---|---|
| a climb | 58 s | 51.8..55.8 s | Walks to the trunk, a 2 s look up at the bag, climbs (13 px/s) to just under the branch, two strained reaches toward the bag along the branch (paw out past the trunk; the bag is 82 px away), slides down (0.55 s), lands with a thump and two dust puffs, sits and looks up 5 s, walks off right. |
| b shake | 42 s | 37.0..39.1 s | At the trunk: a look up, sniffs the bark, rears, shakes 5.5 s (each jolt pumps the bag's damped swing along its motion; a cone drops to the ground at 1.6 s, a second falls on its head at 4.4 s and bounces off), startles (drops and turns round), a 1.2 s frozen beat, runs off right at 72 px/s. |
| c stick | 59 s (76 s) | 50.9..55.0 s (68.2..72.9 s) | Fetches the stick in its mouth (trailing back along its side), carries it to beside the bag, rears with it raised (CA_REACH), two swats (wind back, fast sweep through the bag, which swings), drops it, stands reared looking up 5.5 s, down on all fours, walks off. The stick is the firewood c stick when it lies by the fire (the bear walks behind the fire to the tent's door side to get it, which is the longest and best shadow moment: 76 s); otherwise a fallen branch laid right of the bag pine when the arrival beat starts. |

The bag's swing after a hit is a damped oscillator (0.95 Hz, ζ 0.14) in `spineCritterScenes`; it stops
itself when settled.

### Hooks

- **Dinner** (`hookDinnerCritters`, both `CF_SHY`, two critters at most): a deer walks in from the right
  edge to the right edge of camp (44 px right of the bag pine), freezes head up (CA_LOOK) and takes one
  14 px step toward him on each bite or sip (`noteBite` in the eating actions counts them on the way up;
  at most 7 steps, never nearer than 150 px). A chipmunk runs in from the left, sits up in front of the
  tent for a look, runs on behind the fire and sits by the wood pile eating a crumb, looking up at him
  every few seconds. They scatter when he next moves (standing up for the next beat). Dinner b: they
  come while the pot heats (the deer edges in every 3.2 s instead of per bite; that still is now 11 s,
  the beat 48 s), and bolt when it boils over (`doDinnerScatter` as he jumps back).
- **Theft** (`hookTheft`): the food bag beat ends with one crouch at the tent door and a shoe left
  beside it (`doShoeOff`, +0.5 s per variant). In the close a raccoon waddles in from the left (38 px/s),
  sniffs the shoe 1.8 s, picks it up (CA_CARRY, `CC_SHOE`, `memory.shoe_taken`), turns toward the tent
  and looks at it 1.9 s, waddles off left. It starts after the light goes out (a, c) or once he is back in
  the tent (b). Close durations are now 34 / 50 / 37 s; the close counts an overrun if a critter is still in
  the scene when the fade starts.

### Loop length

The bear and close beats are longer, so the plan now drops fillers (down to two) when even 8 s pauses
cannot land the loop under 650 s. Measured 514..650 s over 20 seeds.

### Deferred review fixes (all 7 applied)

1. `appHikerBounds` (unclipped) added; the variant test asserts on it.
2. The shrugged pack no longer jumps: `actPackLift` tweens it from behind his heels round to his hands
   (0.6 s, a small lift on the way). Viewed.
3. `setPose` in `spine.cpp` takes the fx lean, head and lift out of the blend's start before any pose
   change (the stumble, the yelp, the leaf stand, every `OP_POSE`).
4. Walk arrival snaps `feet.y` to the path's end.
5. `critterSetAction` and `critterMoveTo` keep the gait phase on a mid-move gait change.
6. Every actor push is guarded (`ActorList::add`).
7. The mirror statement above is corrected (1 differing pixel in 137,869, one channel step).

### Numbers

- `sizeof(App)` = 62,960 bytes (was 60,232; budget 98,304).
- Bear beat, rendering every tick: seed 11 (a) 8.58 strips per frame, seed 7 (c) 8.46, seed 42 (a)
  8.83, seed 1 (b) 9.06; max 14. Budget 10.
- Whole loop: 7.37..7.57 strips per frame, pauses 6.86..7.24, 0.104..0.115 ms/frame.
- Tests: 24 spine variants and 6 fillers run in 9 seeds; critters in frame and out of the corner at
  every tick of those loops; 420 critter samples while he walked, none over him; every dinner had
  visitors and every close a raccoon; no critter left at the end of 60 loops; determinism at ticks 7000
  and 13000 added; memory survives the rollover.

### What I changed after viewing

1. The chipmunk ended behind the fire, right under the smoke column, and could not be seen; it now sits
   24 px left of the fire, by the wood pile, where it reads against the tent.
2. The deer's 9 px steps did not show at sheet scale; they are 14 px.
3. The shaking bear barely moved the bag: its jolts alternated at the shake rate and cancelled out. Each
   jolt now pushes along the bag's own swing, so the swing builds.
4. The shadow's 1.5 px tremble was hard to see; it is 2 px and the shadow also hunches down 3 px.
5. Durations: dinner b, all bear variants and the closes set from a 120-seed trace with 2..5 s of slack.

### Open items

- Bear c with the firewood stick runs 76 s (the walk to the fire and back), over the ~60 s aim. It is
  1 loop in 9. Faster walks would cost the "big and slow" read.
- At golden hour the camp pine is near black, so the climbing bear (ACTOR) shows mainly where it sticks
  out past the trunk against the sky. A palette question.
- The pine does not shake when the bear shakes it (it is in the static cache); the bag, the cones and
  the bear's shiver carry it.
- The shadow's mapping is "follows the bear's x scaled into the tent", so when the bear is just right of
  the tent the shadow is at the tent's left, away from it. It reads as cowering, but it is not a literal
  "turns toward the bear".
- The raccoon is never scattered by the hiker in practice (he is in the tent), but if a later beat
  moves him while it works, it would flee with the shoe.
- Not checked by eye: night palettes, dinner c's fast bites with the deer at full frame rate.

## Minors

Written 2026-10-30. Not committed. The minor-effect layer from brief section 4: every 10 to 30 s
a small reward for anyone glancing at the screen.

### Files

- New: `core/scene/minors.h/.cpp` (the scheduler and all 11 minors).
- Changed, small: `world.h` (`Minors minors;` and the include), `world.cpp` (`minorsInit` at loop
  start, `minorsStep` between `spineCritterScenes` and `directorStep`, and every live `Bit` kind now
  joins the actor sort, not only dust), `app.cpp` (`minorsSync` before `worldTick`; `appMinorName`,
  `appMinorCount`, `appMinorCritters`), `ss_app.h` (those three), `sim/preview_common.cpp`
  (`--log-beats` also prints `tick=T minor=NAME` when one starts), `tests/test_scene.cpp` (`testMinors`).
- Changed, for the effects: `props.h/.cpp` (`BITS_MAX` 4 to 6; new bit kinds DIRT, DROP, RING, LEAF,
  MOTH, BEE), `landscape.h/.cpp` (`Cloud::ear`, drawn and bounded), `hiker.h/.cpp`
  (`hikerHeadPos`, same kinematics as `hikerHandPos`), `director.h/.cpp` (`directorDryY`, below).

### The pool

Each runs under 25 s and holds at most one critter (tag `CT_MINOR`), released at the end. Weights are
the base weight times 0.6..1.4 drawn per loop.

| Name | What happens | Critters / effects | Length | Gating |
|---|---|---|---|---|
| chipmunk_cross | Runs in from the edge away from him along the bank in front of the fire, sits up half way and looks, runs on out the far side (or back the way it came if he is now in the way). Carries a crumb, then a nut, then a granola bar on later picks (`chip_n`). | chipmunk | 9 to 12 s | the run-in and the stop clear of him; base 1.4 |
| squirrel_nut | Runs in with a nut, digs (head down, paw going, flecks of earth), drops the nut in, two pats, sits up and looks round, then hops to three other spots and digs: nothing, nothing, a nut; runs off with it. | squirrel; DIRT bits | 17 to 20 s | a clearing spot 130 px from him, 50 from the fire, off the tent and the camp pine, with a dry, clear run from an edge; hops never go toward him; base 1.0 |
| frog_splash | Hops in along the far bank from the right edge to the water's edge, sits, looks, blinks, hops in: a ring on the water and it is gone. | frog; RING | 8 to 10 s | spot 130 px from him; base 1.0 |
| bird_chip | Swoops in from the side away from him and lands 44 px beside the fire pit, one hop, pecks, picks up a crumb, flies up over the stream and drops it (a DROP that falls and becomes a small ring), flies out. | bird (flight is driven along a curve; the rig flaps in place) | 9 to 11 s | fire stones laid, landing 90 px from him; base 1.0 |
| deer_edge | Walks out at the right edge of camp, freezes looking at camp, one 14 px step closer, freezes, turns its head, walks off right after about 20 s. Shy: any move of his and it bolts. | deer (`CF_SHY`) | 22 to 24 s | 160 px from him, clear path from the right edge; base 0.8 |
| owl_watch | Glides onto the camp pine's bag branch, a slow blink, a full head turn, a look, another blink, glides off. | owl | 14 to 16 s | dark only (mood ≥ 4.5, < 0.75, or night mode); not while the hung bag swings; base 1.2 |
| mouse_tent | Out from behind the pitched tent past its door side, scurries along the front of the base to the back corner, sits up, looks, sniffs, off left past the pack. | mouse | 6 to 7 s | tent up and taut, him 90 px clear of the tent; base 0.9 |
| leaf_fall | One ACTOR leaf drifts down from the top on a sine (30 px/s, 15 px sway) and lands on the clearing, or on his hat if he is sitting; it rides the hat until he moves (or 22 s), then slides off. It stays on the ground after (a later leaf may take its bit slot). | LEAF bit | 12 to 22 s | none; base 1.2 |
| moth_lantern | A moth flutters in to the lit tent and circles the upper wall on a wobbling loop for 15 s, then away up into the dark (early if the light goes out). | MOTH bit | 20 s | tent lit and dark mood; its window is the pause after the food bag, so when the gate opens and it has not run this loop it goes first, as soon as 10 s have passed since the last minor started; base 2.0 |
| bee_visit | Buzzes up from the bottom edge to the mushroom, hovers, lands on the cap; if he sits still in a pause with 6 s left, it goes to his face, buzzes, lands on his nose, he swats (hand to face, flick out) and it shoots off; otherwise it flies off. | BEE bit; his near arm (`fx.arm_*`) | 10 to 14 s | none; base 1.0 |
| cloud_animal | One of the clouds grows a pointed lobe on its leading puff that reads as an ear, holds 6 s, shrinks. If he is idle in a pause with time left, he looks up at it (`fx.head`, 3.2 s). | `Cloud::ear` | 11 s | a cloud whose centre is in x 200..700, sky built; base 0.8 |

### Firing policy

- A minor starts only when none is running, the tick is past `next_tick` (10 to 30 s after the last
  one started, drawn per minor) and `hold_tick` (6 s after any beat other than a pause ends: a spine
  beat, a filler or a major; 5 s after the last minor let go), the beat does not own the critters, no
  one else's critter is in the scene, the hiker is not walking, and the frame is not fading.
- "Owns the critters": `World::busy` (set by stage 6 for dinner, bear, close and majors), or the beat
  name is dinner, bear, close, build, or anything not in the calm list (pause, arrival, setup,
  firewood, fire, foodbag and the six fillers). Unknown names count as owning, so a major and the
  stillness beats round it hold the minors off even without `busy`.
- If such a beat starts while a minor runs, its critters leave at once (fast, by the side clear of
  him); the bird and owl fly out; a bee near him leaves. The leaf, moth and cloud carry on (no
  critters).
- Pick: weights times the scene gates; the last 6 picks (ring buffer) are out; if that leaves none,
  only the last pick is out. A start that the scene declines (him in the way) retries in 1 s.
- Every exit is a free walk to just inside the frame edge, then the director's exit for the last
  half body, so the hiker rule still scatters a leaving critter if he walks its way.
- Randomness: `Minors` has its own `Rng`, seeded at loop start from a copy of the loop RNG (not
  advanced), so the spine's picks and the ambient layer replay exactly as before for a seed.

### Numbers

- `sizeof(App)` = 63,344 bytes with `Minors` (was 62,960; measured on the stage 5 tree before the
  stage 6 changes landed in the shared files).
- Minors per loop: seed 1 (mood 3) 18 in 562 s, seed 2 (mood 0.3) 22 in 649 s, seed 3 (mood 5) 20 in
  592 s. Over 12 seeds 20.8 a loop: chipmunk_cross 31, squirrel_nut 12, frog_splash 30, bird_chip 23,
  deer_edge 24, owl_watch 19, mouse_tent 17, leaf_fall 34, moth_lantern 8 (8 of the 8 dark loops),
  bee_visit 29, cloud_animal 22.
- Whole loop, rendering every tick: seed 11 7.60 strips per frame (was 7.37..7.57 without minors),
  seed 7 7.70, seed 42 7.55, seed 1 7.51; pauses 7.08..7.40; 0.106..0.116 ms/frame. Bear beat
  unchanged (8.46..9.06).
- `testMinors`: counts 12..40 for seeds 1..3; no start in a beat outside the calm list; all 11 fire
  in 12 seeds; no minor ends with a `CT_MINOR` critter in the scene; same-seed frames identical at
  ticks 2000, 7000, 11900 of a dusk loop (11900 is mid-moth; the incremental render also matches a
  full one there). The existing critter checks (in frame, out of the corner, never over the walking
  hiker) now cover the minors' critters too.

### What I changed after viewing

Viewed contact sheets and 2x to 3x crops at mood 4 (seed 4), dusk (seed 3), night (seed 8) and for the
two hiker reactions (seed 9 bee, seed 4 leaf on the hat).

1. A scattered chipmunk fled straight along its own y and ran across the stream. Fleeing and leaving
   critters now aim for `directorDryY`: the end y that keeps the straight run 3 px above the water's
   far edge (the dinner critters' scatter benefits too).
2. The test caught a squirrel hopping to a dig spot right next to the crouched hiker (then overlapping
   him when he stood and walked), and chipmunks crossing his feet. Paths, spots and exits now keep
   clear of his bounds (`clearOfHiker`), the chipmunk comes from the side away from him and turns back
   if he has moved into its way, and exits walk free to the edge before the director takes them.
3. The mouse was `CF_SHY` and bolted at his first pose change in the cairn filler before anyone could
   see it; it now only avoids him walking.
4. The moth circled across the tent's right edge, where the smoke drifts over it, and vanished behind
   the puffs; its loop is now over the upper middle of the lit wall.
5. The leaf on his hat was the same black as the hat and read as a lump; it now sits with the stem on
   the crown and the blade sticking out past the edge, tip up.
6. The swat raised his hand over his head (a wave). The arm now goes out level with the forearm back
   up to his face, then flicks out; the bee sits under the brim, not on it.
7. Earth from the digs was GROUND_DARK on GROUND and could not be seen; it is ROCK now.
8. The look-up at the cloud never happened in 12 loops (it needed him idle at one exact moment of a
   pause); it can now start any time while the ear is up, once, if the pause has 3.7 s left. It fired
   in 3 of 12 loops.

### Open items

- The bee is 2 px as asked: on the panel it is a speck with two light wing dots. Its trip to his nose
  needs him sitting in a pause, which happened in 2 of 12 loops; the leaf on his hat in 5 of 12.
- The squirrel's spot rules (clearing, away from fire, tent, pine and him) decline it often: 12 runs
  in 12 loops against about 30 for the others.
- During setup and the fillers he moves a lot, so a fair share of minors end early with the critter
  scattered. It reads as natural (he walks over, it runs), but some never get to their payoff.
- The moth's only window is the pause after the food bag (before the bear); a loop whose pause there
  is short and starts within the 6 s hold misses it.
- The leaf falls from the top of the frame, not from a tree; a leaf from the camp pine's foliage would
  be better but the pine is a conifer.
- `--log-beats` keeps the last minor count in a static in `preview_common.cpp` (one Edit); a second
  `PreviewState` in the same process would share it. Fine for the CLI; noted.
- The bird drops the crumb from just above the near bank (46 px over the water's centre line, at the
  end of a short hop of a flight), so the crumb falls the last stretch onto the water and makes a
  small ring (viewed, seed 1 tick 4060). Flying further out over the water would read more as "over
  the stream".
- Not checked on the panel: any of it. Not viewed: squirrel and frog at night.

