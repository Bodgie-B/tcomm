# Morning and daytime ideas: design and build plan

Mike's ideas from his comment on PR #2 (31 October), which he held for after the handoff. Written
2 November by the lead session. Everything here is PROPOSED: Mike reviews each piece from contact
sheets before it's called done.

Where they go:

- The **morning** gets a second variant, "the camp stays up", beside Mark's "break camp and leave".
  Each morning loop picks one (proposed 60/40 in favour of staying, since that's what Mike asked for
  first).
- The **daytime** activities join the **midday** spine as a pool, picked two or three per loop like
  the surprise majors, replacing Mark's quiet "afternoon" beat when they're picked. Mark's return,
  lunch, wade, nap and map beats stay.

General rules for every piece (Mark's brief, section 4 and 9, and HOW_TO_ADD_SCENES.md):

- A beat of stillness before and after each activity. Never busy.
- Anticipation before each action (two or three frames of wind-up), overshoot and settle.
- Every new critter or prop is a few primitives with one unmistakable silhouette feature.
- Everything respects the doorbell freeze (`World::react.active`) and keeps out of the chip's corner
  (x 538..796, y 412..476).
- Each piece gets variants picked from the loop seed, a test in its own tests file, and a contact
  sheet in `docs/screensaver/reviews/morning_day/`.
- Budget: sizeof(App) stays under 32 KB; average strips per frame under 8 outside the big moments.

## Build order

| # | Piece | New things | Why this order |
|---|---|---|---|
| 1 | The camp dog, and fetch | dog critter rig, stick prop | A new character Mike asked for, and it ties to the house: see "Dogs out" below |
| 2 | Kite | kite prop with tail and line | Simple prop, three good variants, a callback to the rod-and-log gag |
| 3 | Boomerang | boomerang prop, eagle (a bigger bird) | Simple prop; the eagle is reusable |
| 4 | Morning "camp stays up" | pan and pancake, toothbrush, stuff sack | Five small beats on existing systems (steam, raccoon, squirrel, the shoe in memory) |
| 5 | Slackline | line between two trees | Balance poses; a squirrel crosses it with ease |
| 6 | Mountain bike | bike prop and a riding pose | Bigger rig work; splashes reuse the stream's |
| 7 | Archery | bow, arrow, target | |
| 8 | Ropes course | posts, rope bridge, swing, net | Largest set of props; last |

## 1. The camp dog and fetch

**The dog:** about 34 px long, 24 px tall: body ellipse, head circle with a muzzle, one floppy ear,
a tail that wags (a capsule swinging), four leg capsules with a trot cycle. Colour: a warm mid-brown
(ACTOR_LIGHT side lit like the hiker), so it reads against the grass by day and goes dark at night
like everyone else. Behaviours: trot, run, sit, lie down, sniff, wag, shake off water, pick up and
drop a stick, play-bow. Its personality dial (per loop): bouncy or lazy.

**Fetch beat (daytime pool), about 45 to 70 s:**

1. The dog trots in from the left with a stick, drops it at his feet, play-bows, tail going.
2. He picks it up, winds up (stick back over the shoulder, a beat of anticipation), throws.
3. Variant A, the good fetch: the stick arcs right, the dog races after it, catches it on the bounce,
   trots back proud, drops it. Two or three throws, the last one longer. Ends with the dog lying
   down beside him.
4. Variant B, into the stream: the stick lands in the water; the dog skids at the bank, looks back
   at him, wades in, comes out and shakes off right next to him (droplet bits fly, he flinches and
   wipes his face).
5. Variant C, the wrong stick: the dog comes back with a branch three times its size, dragging it;
   he looks at it, shrugs, and they both sit.

**Dogs out (house reaction):** when the panel's `dog_out` says Lucy or Copper is out, the camp dog
shows up in that loop for sure (fetch is forced into the pool, or for other families a short visit:
it trots through, sniffs the tent and lies by the fire). With both out, a second dog joins, a little
smaller and a different brown.

## 2. Kite (daytime pool), about 40 to 60 s

He brings out a diamond kite with a ribbon tail (a filled quad, a cross spar line, a tail of three
little bows on a wavy line). The line is a 1 px line from his hand.

- Variant A, serene: he walks backward, the kite lifts on the first try and climbs into the sky,
  swaying, the tail rippling. He stands and watches it for a long, calm stretch, then reels it in.
- Variant B, the kite wins: a gust (the weather's own if it's windy) yanks it; he leans back, digs
  his heels in, skids, then gets pulled off the right edge at a run. A beat of empty scene. He walks
  back in from the right, without the kite, line dangling. (The callback to the rod and the log.)
- Variant C, it won't fly: three launches. It nosedives, then spins and crashes, then lifts a little
  and drops. On the third he sits down with it in his lap, and a breeze lifts it out of his hands
  into the sky by itself, and he scrambles up after the line.

## 3. Boomerang (daytime pool), about 30 to 45 s

A small curved boomerang (two capsules at an angle), spinning as it flies (rotate per tick).

- Variant A: it flies a wide loop out over the stream and comes back; he catches it, a little
  surprised, and does a small proud pose.
- Variant B: an eagle (a bigger dark bird, 50 px wingspan, slow deep wingbeats) glides in, snatches
  it in mid-air and flies off with it. He stands, arm still out, watching it go.
- Variant C: it comes back, misses him, and thunks into the tree behind him, sticking there. He
  tugs, can't reach; it stays in the tree for the rest of the loop (loop memory could keep it there
  next loop too, like the shoe).

## 4. Morning: the camp stays up

Beats in order, replacing "bag down" and "break camp" in the stay variant: wake (Mark's), coffee
(Mark's, with the boil-over as one of its variants), the morning pee and raid, breakfast, wash,
tidy, then he heads off on a day hike with the camp left standing.

- **Boil-over** (a coffee variant): the pot rattles, steam bursts (Mark's Steam from dinner b),
  hissing; he jumps back, then pulls it off with his sleeve over his hand.
- **The morning pee:** he glances at the viewer ("just going over here"), walks behind the same
  big tree every time (the tree is fixed per camp layout), off camera. Bushes rustle (a small shake
  of the foliage). A squirrel startles off a branch. The animals learn: while he's gone (12 to 18 s),
  the raccoon slips in and walks off with his shoe (the existing shoe in loop memory; if the shoe is
  already gone, it takes a mug, the stolen-item memory). He comes back, looks down at one shoe.
- **Breakfast, pancake flip:** a pan on the fire, a pancake (a flat ellipse, golden). He flips it:
  - A: it flips once and lands back in the pan; he nods.
  - B: it flips too high and lands on his head, draped over his hat.
  - C: a bird swoops in and snatches it out of the air.
- **Wash at the stream:** he kneels at the bank, splashes his face (water bits), shudders with his
  teeth chattering (a fast small head jitter), brushes his teeth (a tiny stick at his mouth, arm
  scrubbing), and spits into the stream (a small arc of droplets, a ring on the water).
- **Tidy, the stuff sack:** he stuffs the sleeping bag into its sack, it bulges, he sits on it to
  push it down, he ties it, he stands up, and it springs open and the bag puffs out. Two tries, the
  second works (or not, by variant).
- **Off on a day hike:** he shoulders a day pack, checks the map, and walks off right. The camp
  stays: tent up, fire out, the pot by the fire. A few seconds of the empty camp with the animals
  creeping back before the fade.

## 5. Slackline (daytime pool), about 45 to 60 s

He ties a line between the near tree and a trunk at the edge (a 2 px line that sags, more where he
stands).

- He steps on, arms out, wobbling (sway in lean and arms), takes three steps, falls off sideways,
  lands on his feet, tries again.
- Variant B: he makes it all the way across, stops in the middle and does a little pose; the line
  bounces.
- A squirrel or the chipmunk then trots across the line effortlessly, both ways, and sits in the
  middle. He watches, hands on hips.

## 6. Mountain bike (daytime pool), about 30 to 45 s

A bike: two wheel circles with spokes suggested by a cross, a frame of three capsules, handlebars.
He rides in from the left edge, riding pose (bent forward, legs on a pedal cycle).

- A: he rides through the stream, a big splash fan each side, out the other bank, and off right.
  A moment later he rides back the other way, proud.
- B: the front wheel hits a rock in the stream; he goes over the handlebars into the water (a dive
  arc, a big splash), stands up dripping, the bike lying in the stream.
- C: he pops a wheelie, holds it, holds it, and slowly tips over backward, landing on his back with
  the bike on top of him, wheels spinning.

## 7. Archery (daytime pool), about 40 to 60 s

A straw target on a stand at the right of the clearing (rings: three circles), a bow (an arc capsule
and a string line), arrows (thin lines with a fletch).

- He draws (anticipation: a long hold), looses: the arrow flies, a 1 px streak.
- Variants: a bullseye on the third try after two misses (one into the ground, one past the target);
  an arrow that sails over and lands in the tree; a split arrow (the second hits the first).
- Night never plays it.

## 8. Ropes course (daytime pool), about 60 to 90 s

A few posts appear at the back of the clearing with a rope bridge between two of them, a tyre swing
and a cargo net (all thin lines and circles).

- He tries each: the bridge (wobbly, makes it), the swing (swings out, spins, gets dizzy, staggers
  off), the net (gets a foot caught, hangs upside down a moment, drops).
- Varying success by seed. It's the largest piece; built last.

## Open questions for Mike

- The 60/40 split between "camp stays up" and "break camp" mornings.
- How often the camp dog appears when no real dog is out (proposed: in one daytime loop in three).
- Whether the boomerang staying stuck in the tree should carry into the next loop (like the shoe).
