# Mail by bird, fire and ice, and the wave (stage 14)

For review on the PC before anything goes to the panels. Nothing is flashed, nothing is committed.
Look at `contact_sheet.png` first (33 tiles). The full frames are here as `NN_name.png` (800 x 480).

## What changed

1. **Mail by bird (downstairs only).** When you mark mail, a bird flies in from the upper right
   with a letter in its beak. It lands on the tent's back pole (the morning's ridge birds use the
   front one), or on the camp pine's bag branch if no tent is up. It drops the letter by the tent
   (or under the branch) and stays there. Every few seconds it does something small: a quick turn,
   a look round, a hop or a preen. The hiker glances up at it now and then. When the mail is
   acknowledged the letter is gone (collected) and the bird flies off to the upper left. If the
   tent goes up or comes down while it waits, it flits across to the other perch. If mail is
   already waiting when the screensaver starts, or when a new loop begins, the bird and its letter
   are simply there once the camp has built up. The mail badge stays as it was.
2. **Fire and ice (both panels).** These follow the tenant's arrows. With both kinds showing, the
   newest arrow decides. Predicted arrows are left out.
   - **Too hot:** small fires start on the grass, by the wood pile and near the tent. He takes a
     bucket from his pack, runs to the stream, fills it, runs back and pours (a splash, then a puff
     of steam). When nothing is burning he waits with a full bucket, looking round. Mild: about one
     fire per bucket run, so he keeps even. Urgent: fires start faster and grow bigger than he can
     douse them. He runs flat out with his arms going, and the light moves three small steps toward
     the golden hour (not at night).
   - **Too cold:** frost creeps in from the edges over the ground, the grass and the rocks, and
     the stream freezes along its banks. Icicles hang from the camp pine and from the tent, with a
     frost cap on the ridge. He hauls wood and builds the campfire into a big blaze. A warm circle
     round it holds the ice back. Mild: the circle holds most of the camp and he warms his hands
     between armloads. Urgent: the circle is small and he huddles, arms hugged and shivering, with
     breath showing.
   - **Cleared:** any fires left die down and he douses them. Then he sets the bucket down, wipes
     his brow and puts the bucket back in his pack. Or the ice melts back to the edges, the fire
     settles to what it was, and he stretches. Then he walks back to where he was and the story
     carries on from that point. Nothing is left behind: no fire, no ice, no bucket. The tests
     check this.
   - It takes over his story the way the doorbell does: the story waits while it runs, so nothing
     is skipped. It begins at a calm moment: he is in frame, standing, sitting or walking (the walk
     carries on afterwards) with empty hands, and no major, story critter or weather gag is going.
     In the morning and midday families that comes within a second or two. In the evening it can
     take a few minutes when dinner, the bear or the close is under way. The fires and the ice
     only start once he does.
   - The dogs lie out of the way by the tent's back corner, and no fire starts near them. In the
     cold they lie in front of the blaze, inside the warm circle.
3. **The wave.** When someone walks up to the upstairs panel, the hiker stops, lifts his head
   toward the viewer and waves (the doorbell's wave without the jump or the point, 2.6 s). Then he
   carries on. He waves once per passer-by, and only with empty hands. He doesn't wave during a
   major's key moment or an urgent fire-and-ice spell. In the test he waved at 16 of 24
   passers-by; the rest came at busy moments.
4. **The Lookout:** neither the bird nor fire and ice appears there. It is the cheapest safe
   choice: the ledge has no tent, branch, stream or fire pit. Both come in with the next camp loop.

## Choices for you (all in `screensaver/core/scene/`)

| What | Where | Now |
|---|---|---|
| The bird's size | `kBirdScale`, `mailbird.cpp` | 1.5 × the story's small birds |
| Fire spawn rate, growth | `kSpawnMild`, `kSpawnUrgent`, `kGrow*`, `kBigUrgent`, `fireice.cpp` | mild 10 s, urgent 2.2 s |
| Running speed | `kRunMild`, `kRunUrgent` | 74 / 104 px/s |
| Warm light for urgent heat | `kWarmSteps`, `fireIcePaletteTweak` | 3 steps, up to 30% toward the golden hour |
| Warm circle sizes | `kIceMild`, `kIceUrgent` | 0.92 / 0.38 (1 = the frame's edges) |
| How fast the ice creeps and melts | `kCreepMild`, `kCreepUrgent`, `kMelt` | about 17 s in, 15-20 s to melt |
| The blaze | `kBlazeMild`, `kBlazeUrgent` | 1.25 / 1.6 × a good fire |
| The wave's length | `kWaveTicks`, `react.h` | 2.6 s |

## Cost

- **Memory:** the scene block goes from 29,056 to 29,768 of 32,768 bytes (+712, within the
  1,200 allowed), leaving 3,000 bytes. That covers the bird's critter slot (276), FireIce (300),
  MailBird (80), 10 more actor entries (40) and the inputs. No new buffers, no static RAM (both
  new files have 0 bytes of bss). The panel host adds no memory.
- **Flash:** about 41 KB more PC object code (x86-64, -O3: fireice 32 KB, mailbird 8 KB). On the
  panel I'd expect 20-30 KB. Not built for the panel.
- **Per frame with nothing active:** a few tests and nothing drawn or marked.
- **Per frame while active (PC, three seeds, a minute in):**

  | Case | Pixels composited | Draw time |
  |---|---|---|
  | Idle | 68,700 | 181 µs |
  | Mail bird | 69,000 | 195 µs |
  | Too hot, mild | 70,000 | 189 µs |
  | Too hot, urgent | 86,400 | 247 µs |
  | Too cold, mild | 71,200 | 233 µs |
  | Too cold, urgent | 71,500 | 203 µs |

  At the panel's ~1.3 px/µs, urgent heat is about +13 ms a frame; the rest are +1 to +2 ms.
- **The ice steps:** while the ice creeps in or melts (about 15-20 s each way), each step
  repaints the cache rows the warm circle covers and composites those strips, about once a second.
  At most this is the ground band (162 rows, a third of the frame), roughly 35-40 ms of cache
  repaint plus about 30 ms of compositing on the panel (an estimate, not measured). Once the
  circle settles it costs nothing more. The pine's icicles repaint once at each of their two
  steps.
- The warm-light steps for urgent heat are palette changes: 3 whole-cache rebuilds going in and 3
  coming out (they spread over frames if the host turns spreading on).

## Tests

`screensaver/tests/test_fireice.cpp` (278 checks):

- With no inputs, nothing runs.
- The arrows: the newest decides, predicted ones are left out, and the level and urgency are
  right.
- Too hot and too cold, mild and urgent, in all three camp families: it takes him over, the fires
  come and the ice comes in, and every frame equals a full render (ice steps and warm light
  included).
- Clearing: it lets go, he is back exactly where he was, no fire, bucket, ice, tent frost or warm
  light is left, the pit is as it was, and the cached background is drawn exactly as a clean
  World's. Then the story resumes.
- A change of kind mid-spell works; the Lookout never starts it.
- The same seed and the same input changes replay frame for frame.
- The bird: perched within 3.2 s with the letter down by 4.8 s, inside the frame and clear of the
  chip, gone 2.9 s after collection. It is simply there with mail at start and absent in the
  Lookout.
- The dogs: no fire starts near them, and they lie by the fire in the cold.
- The wave: once per passer-by, never longer than 2.6 s, never in an urgent spell.
- Memory stays within budget.

All 29 screensaver tests pass.

## To build these again

See the top of `screensaver/tools/fireice_stills.cpp` (`--cost` prints the table above), then
`screensaver/tools/dog_sheet.py STILLS_DIR docs/screensaver/reviews/stage14-mail-fire-ice`.

## Not yet judged

- Everything on the panel itself: the bird's size across the room, the frost colour and the warm
  light on the real screen, and the night versions in a dark room.
- The real cost of the ice steps on the panel (an estimate above).
- The panel host lines in `ss_host.cpp` (`temp_ask` on both panels, `mail` downstairs,
  `passer_by` from the View) are written but not compiled: no firmware build was run.
- How long the takeover waits for a calm moment in long evening beats. It can be minutes. If you
  want it sooner, the start rules are in `fireIceCanStart`.

## Revisions 2026-11-12

**The blaze against the ice is taller and wilder.** You asked for a fire that is taller and a little
out of control. Tiles 19 to 24 and the contact sheet are re-rendered. The single stills are in
`../stage16-revisions/`: `01`, `01b` and `01c` are a few ticks apart at the peak, and `01d` is at
night. Only the cold's big fire changed. A good campfire and the too-hot small fires and buckets
are as they were.
- **Height.** At the peak of an urgent spell the flames now reach about 100 to 115 px, taller than
  the hiker. The old blaze was about 30 px. A mild spell is about 45 to 75 px.
- **Wildness.** Five uneven tongues each jump on their own. The whole fire leans with slow gusts,
  mostly downwind, and sometimes stands up. Licks of flame tear off the top and leap up. About 16
  sparks fly up to 1.45 times the fire's height.
- **Colour and glow.** The flames have a deep red edge, then orange, then a yellow heart. A soft
  glow of three rings sits behind anyone standing by the fire, so he is never tinted. The smoke
  starts above the taller flames.
- **What you can change.** All of these are in `screensaver/core/scene/ambient.cpp`:
  - `kBlazeTall` (3.2): the height
  - `kBlazeWide` (16): the width
  - `kBlazeGust` (0.30): how far it leans
  - `kBlazeSparkUp` (1.45) and `kBlazeSparks` (16): how high and how many sparks
  - `kBlazeGlowW` (2.0) and `kBlazeGlowA` (30): the glow's width and strength

  `kBlazeMild` and `kBlazeUrgent` in `fireice.cpp` still set how big the fuel gets.
- **Cost.** The blaze's dirty box now comes from the same points it draws, so it is no bigger than
  what is drawn. Urgent cold goes from about 71,500 to 78,400 px composited a frame (+6,900, about
  +5 ms on the panel), plus the glow's blending. On the PC, draw time went from about 203 to
  356 µs a frame, so I expect roughly +5 to +10 ms a frame on the panel while the blaze is at its
  height. That has not been measured on the panel. Mild cold goes from 71,200 to 72,500 px. Memory is
  unchanged: the scene block is still 29,800 bytes. The glow is drawn as one more entry in the
  existing actor list, and nothing new is stored.
- **Third pass: he stands back, and the near trees ice up.** Mike approved the blaze and the
  sparks, so their look is unchanged. Tiles 19 to 25, the contact sheet and the `01` stills are
  re-rendered, and `06_ice_icicles.png` is new.
  - **Clear of the flames.** In the cold he now feeds the blaze a log from arm's length, a third of
    the way back. Then he steps back the rest of the way, turns to face it and warms himself:
    22 px farther out than before in a mild spell, 50 px in an urgent one. A good campfire and the
    too-hot scenario are unchanged.
  - **The dogs.** The dogs that lie by the blaze move out to the sides by the same amount.
  - **The path round the fire.** When his way crosses the pit he now goes round well behind the
    blaze.
  - **The tent.** I did not move the tent. The glow is drawn behind it, so it is not tinted. The
    tall flames still pass in front of its lower right corner, because the tent stands behind the
    fire.
  - **Icicles on the near trees.** The two big pines at the left edge now get icicles under every
    tier, with the camp pine's timing: short as the ice comes in, long at its fullest, gone as it
    melts. They are 1.35 and 1.6 times the camp pine's, the nearer tree's the largest. They are
    drawn into the cached background with the ice. They are drawn again wherever a pine is redrawn
    over the hiker or the fog.
  - **Settings.**
    - `kClearMild` and `kClearUrgent` in `fireice.cpp` (22 / 50 px): how far back he and the dogs
      stand.
    - `kFgIcicleFar` and `kFgIcicleNear` in the same file: the icicles' size.
  - **Cost.** Urgent cold is 79,100 px composited a frame, against 78,400 for the blaze alone. At
    each of the icicles' two steps, the cache repaints rows 170 to about 480: the camp pine's and
    both near pines' rows, once going in and once going out. The scene block is still 29,800 bytes.
    The dog-by-the-fire test now allows for the clearance, and also checks that the dogs are at
    least 24 px from the fire.
