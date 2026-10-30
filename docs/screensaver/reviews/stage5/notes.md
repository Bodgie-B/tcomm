# Stage 5 notes

## Critter rigs

Module: `screensaver/core/scene/critters.h` and `critters.cpp`. Test: `screensaver/tests/test_critters.cpp`.
The module is built but not placed in the scene; the next task wires it into `World`, the
minors scheduler and the spine.

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
  Facing -1 is the pixel-exact mirror of facing +1 (0 differing pixels). Strip-by-strip drawing
  matches a full-frame draw.
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
