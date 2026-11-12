# The sunrise overlook and the visiting couple (stage 15)

For review on the PC before anything goes to the panels. Nothing is flashed, nothing is committed.
Look at `contact_sheet.png` first (48 tiles). The full frames are here as `NN_name.png` (800 x 480).

## What changed

1. **The sunrise overlook.** A new set piece, the Lookout's morning twin, played once each morning
   around the real sunrise. Its own place: four long, smooth mountain ridgelines going paler and
   bluer into the haze, a wide valley with low fog banks, and in front a long pale slab of layered
   rock jutting out from the lower left over the valley, an undercut under its end and the support
   cliff well back, so there is air under the tip. Scrub pines and laurel stand on the slope behind.
   - He comes up the trail from behind the laurel in the blue before dawn with his pack and a lit
     headlamp, the stars going out one by one, walks out along the slab (smaller as he goes out),
     sets the pack down, switches the lamp off, sits at the very end with his legs over the edge,
     and pours from a flask into his cup (it steams).
   - The sky goes deep blue, then rose, then gold, in steps. The sun breaks over the far ridge where
     the real one rises (the panel's sun bearing; the scene looks east). The light then walks down
     the ridges one layer at a time, the valley fog glows and thins, a hawk glides past below him,
     and he raises his cup to the sun. He sits a while, packs up and walks back the way he came;
     the frame fades and the next loop is the morning camp.
   - A house dog that is out comes up behind him and sits beside him on the ledge (tile 22-23).
     Both can come. It was cheap: the house-dog drawings, driven by the sunrise itself.
   - A gray, dry morning plays it muted (paler sun, no glow, half the light on the ridges). On a
     rain, storm or snow reading the camp plays instead (snow too: there is no snow on the ledge).
2. **Once a day, fixed for both set pieces.** A sunrise or a Lookout now counts as the day's
   showing only once its key moment has played (the Lookout: its sunset beat, with the sun down; the
   sunrise: the sun's first pixel over the ridge). A touch in the first seconds no longer uses it
   up: the next start in the window plays it again from the beginning. One forced by hand never
   counts. The rule lives in `screensaver/core/set_piece_day.h` so the panel and the tests share it.
3. **The visiting couple.** Ann (slighter, a red knit beanie with a bobble, a ponytail, a small
   red daypack) and Ben (broad and tall, a ball cap, a big blue pack with a bedroll) walk in from
   the right along the trail, about one camp loop in seven, never two loops running.
   - Evening, fire lit: they wave from the trail, he waves them over, packs down, they sit by the
     fire, cups out, talk with small gestures, Ben tells a story with his hands, all three lean back
     laughing (shoulders shaking, heads back), a toast, a while longer; then up, a wave, and on.
   - Morning and midday: frisbee in a triangle in the meadow with real arcs, a fumbled catch (it
     drops and he picks it up), a long one Ann runs down, a dive by Ben, and, if a house dog is out,
     the dog takes the disc out of the air once, is chased and gives it back.
   - Night, clear or part cloud: two telescopes on tripods, turns at the eyepieces, Ann points up at
     the real moon (or the brightest star), the hiker has a look and steps back impressed (hands on
     hips, head back). On a moonless night a shooting star crosses once and all three follow it. An
     overcast night is a fireside visit.
   - **How it fits the story:** it takes his story over the way fire and ice does. It starts at a
     calm moment (him in frame with empty hands, no major, minor, story critter, weather gag,
     doorbell or fire and ice going, at least a minute of the loop left and not in its closing beat),
     the story waits while they are here and carries on exactly where it stopped. So no plan
     changes: **no test pins changed.** It needs `Inputs::visitors`, which is off when zeroed (as the
     chip was), so every existing test and render is what it was; the panel host turns it on.
   - Never in the Lookout or the sunrise, never on a rain, storm or snow loop, never while fire and
     ice or a doorbell reaction runs. A doorbell during a visit still gets its answer (the visit holds
     still meanwhile). Fire and ice asked for during a visit waits until they have gone.

## Choices for you

| What | Where | Now |
|---|---|---|
| How often the couple come | `kChance`, `screensaver/core/scene/visitors.cpp` | 0.17 a loop when allowed: about one camp loop in 6.8 (measured over 7,000 loops) |
| No two loops running | `visitorsMarkVisited`, same file | the next loop is skipped |
| Time left in a loop for them to start | `kLeftLoop`, same file | 60 s |
| Their sizes | `kAnnH`, `kBenH`, same file | 82 and 95 px (the hiker is 90) |
| Their builds and pack colours | `kLooks`, `screensaver/core/scene/hiker.cpp` | Ann 0.82 wide, red; Ben 1.28 wide, blue |
| Fireside length | `fireside()` and `ST_MAIN`'s 64 s, `visitors.cpp` | about 95 s with the walk-in and out |
| Frisbee throws | `kThrows`, `visitors.cpp` | 9 throws; the fumble, the run, the dive, the dog are fixed in that list |
| The sunrise window | `kSunriseBeforeMin`, `kSunriseAfterMin`, `firmware/tcomm/src/ui/ss_host.cpp` | 30 min before the real sunrise to 15 after |
| The sunrise's length | `sunrisePlan` (dawn 62-82 s, linger 18-28 s), `kSettleS`, `kPourS`, `kBreakS`, `kLightS`, `sunrise.cpp` | 266-283 s a loop |
| His pace on the slab, his sizes | `kPace`, `kNearH`, `kTipH`, `sunrise.cpp` | 0.42 heights a second; 84 px near, 58 px at the tip |
| The colours of dawn | `kMood0`, `kMoodRose`, and the rose and gold in `sunrisePaletteTweak`, `sunrise.cpp` | 0.32 to 1.0 to 1.52 |
| How much light the ridges catch | `kLitStep`, `sunrise_land.cpp`; the steps in `kLight`, `sunrise.cpp` | 0.09 a step, two steps a ridge |
| Gray morning threshold | `sunriseInputs`, `sunrise.cpp` | the reading's overcast 0.5 or more |

Serial on the panel: `ss scene 5` forces the sunrise; `ss lookout 3` plays the sunrise every loop,
`ss lookout 2` turns both set pieces off, `ss lookout 0` back to the clock. There is no serial
switch for the couple (it would need a new command in tcomm.ino, which this stage did not touch).

## Cost

- **Memory:** the scene block goes from 29,768 to 29,800 bytes (+32: the actor list grew from 120
  to 128 places), leaving 2,968. The sunrise (2,156 bytes) and the couple (952) live in a union with
  the Lookout's landscape (3,516), which no camp or sunrise loop used; `static_assert`s keep them
  inside it. `Inputs`, `Memory`, `Hiker` and `HouseDogs` kept their sizes (new fields went into
  padding). No new buffers. Static RAM in the core: none (the new files have no bss). The panel host
  keeps about 12 bytes more (the sunrise's flag and minute, a second day record).
- **Flash:** about 80 KB more PC object code (x86-64, -O3: sunrise 28.7 KB, its landscape 13.5 KB,
  the couple 37.6 KB, plus small growth in hiker, world, app). On the panel I'd expect 40-55 KB.
  Not built for the panel.
- **Per frame with neither on screen:** a few tests (the couple's step and dirty pass return at
  once; no visit planned is one flag).
- **Per frame (PC, three seeds each, `stage15_stills --cost`):**

  | Case | Pixels composited | Draw time |
  |---|---|---|
  | Camp, midday, no visit | 73,400 | 121 µs |
  | Camp, evening, no visit | 78,500 | 126 µs |
  | Sunrise, whole scene | 9,000 | 30 µs |
  | Couple: frisbee | 87,700 | 185 µs |
  | Couple: fireside | 77,700 | 211 µs |
  | Couple: telescopes (night) | 96,300 | 201 µs |

  At the panel's ~1.3 px/µs: the sunrise is light (about 7 ms a frame, 3.3 strips); frisbee about
  +11 ms and the telescopes about +14 ms over the plain camp; the fireside about the same pixels
  (three rigs drawn instead of one, so a little more drawing).
- **Rebuilds in a sunrise loop:** 10-11 palette changes (the dawn's steps, the sunbreak's two, the
  light's two, the loop start) and 14 row-band repaints for the light walking down (each repaints
  one ridge's rows, the fog's or the slab's), plus the build's layer changes. The Lookout has 11-13.

## Tests

- `screensaver/tests/test_sunrise.cpp` (108 checks): the family pick and the weather rule; six whole
  loops (no overruns, the leave's script done before the end, 240-320 s, the sun breaks exactly once
  in the sunbreak beat with the stars gone, the mood only climbs, he stays in frame and out of the
  chip's corner, it ends black); incremental frames equal full renders at 30+ checkpoints a seed
  including every light step and the break; the same seed replays; gray morning muted; Lucy sits
  beside him; the once-a-day rule played through the real app for the sunrise and the Lookout (a
  touch at 8 s does not count, played to the key moment it does, the loop after is the camp, forced
  never counts, the next day it is wanted again); memory within budget.
- `screensaver/tests/test_visitors.cpp` (52 checks): off unless asked (and a loop with them off is
  frame for frame the same); 7,000 loops: one in 6.8, never two running, never in a set piece or on
  a wet loop, the set pieces' memory untouched; fireside, frisbee with Copper and telescopes on a
  moonless night, two seeds each: they come and go, he is back where he was, nothing left (no packs,
  scopes, cup or lent dog), no overruns, every frame while they are here equals a full render, the
  same seed replays; exclusions (doorbell, fire and ice; fire and ice waits for them; none in the
  sunrise); memory.
- All 31 screensaver tests pass. **No pinned loop changed.**
- Soak (`ss_soak`, now with a 45-minute sunrise window each simulated morning and the couple on): 24 h, determinism
  passes; its loop-length rule now knows the sunrise's own length, as it knew the Lookout's.

## To build these again

See the top of `screensaver/tools/stage15_stills.cpp` (`--cost` prints the table above), then
`python -I screensaver/tools/dog_sheet.py STILLS_DIR docs/screensaver/reviews/stage15-sunrise-couple`.
Preview: `ss_preview --spine sunrise` or `--sunrise`; `--visitors` lets the couple come.

## Not yet judged

- Everything on the panel itself: the dawn colours on the real screen, the 58 px hiker at the tip
  across the room, Ann's beanie and the packs' colours, the telescopes at night in a dark room.
- The panel host lines in `ss_host.cpp` (the sunrise window from `sun_times`, the once-a-day records,
  `in.visitors = true`) are written but not compiled: no firmware build was run.
- The real cost of the light's row repaints on the panel (each up to about two thirds of the frame
  for the nearest ridge and the slab; expected 40-80 ms once each, a few times a loop).
- How often the couple find a calm moment in long evening beats (they can wait minutes, like fire
  and ice); and the night visit needs the evening family at night, which only plays while the
  panel's night mode or night mood is on.

## Revisions 2026-11-12

All 36 stills and the contact sheet are re-rendered. The single stills are in
`../stage16-revisions/` (`02` to `04b`, with `02c` in full light).
- **Shorter ledge.** The slab's tip now ends at x 392 to 418, the middle of the 800 px frame. It
  used to end at 556 to 592. The support cliff and the slope behind moved in with it. His seat, the
  pack stop and the dogs' places are fractions along the slab, so they moved with the tip. He still
  sits at the very end, Lucy sits just behind him on the rock (`02b`), and the walk in and out
  follows the slab as before. The tip range is `kTipX0` and `kTipX1` in `sunrise_land.cpp`. The
  test for Lucy's place now measures from the tip, not from a fixed x.
- **Mountains without outlines.** The pale 13 px haze band along each ridge's crest was what read
  as a thick white outline, and it is gone. The thin bright line along a lit crest is gone too. The
  ranges now read by atmospheric perspective only: flat fills, each farther ridge paler and closer
  to the sky colour, overlapping the one behind. The haze per ridge is `kHaze` in
  `sunrise_land.cpp`, now 0.50, 0.34, 0.19 and 0.06 from far to near. The light that walks down
  the ridges still warms each whole ridge a step at a time.
- **Ann's head.** The orange thing was her red knit beanie with a bobble: a 6 px dome in the
  mushroom's red-orange, with a 2 px black ponytail you could not see. She now has chestnut hair:
  a bob over the top and back of her head, with her face left in silhouette, and a thick ponytail
  hanging behind her neck. The ponytail has a darker shade down one side and swings when she
  walks. Ben keeps his black ball cap and the hiker his brimmed hat, so the three still read
  apart. The hair settings are `kHairR`, `kTailLen`, `kTailW` and `kHairMix` (the brown) in
  `hiker.cpp`.
- **The mountain's near ground (second pass).** The lower right used to be a flat field with one
  row of cloud puffs. It is now the mountain's own flank and the valley below it, painted near to
  far:
  - A rounded grey rock mass in the bottom-right corner, with a hollow and two cracks. It sits
    behind the OPEN pill.
  - Two bands of forest canopy that come out from behind the support cliff and fall away to the
    right. The crowns are rounded, with a few pointed conifers, in three overlapping rows.
  - Two wooded foothills over the valley, each lighter and hazier than the one in front.
  - Then the far ridges, as before.

  It is all flat fills with no outlines. Each nearer layer is darker and greener. As the light walks
  down, the foothills warm with the near ridge, and the canopy greens with sunlit crescents on the
  crowns' upper right (`../stage16-revisions/02c_sunrise_full_light.png`). The fog is now thin
  wisps lying in the hollows between the hills, and they still glow and thin as before. A hollow in
  the foothills under the ledge's tip keeps paler air under it, so the overhang stays crisp. The
  shapes come from a hash of the ledge and the ridges, so nothing new is stored and no random draws
  were added. The settings are in `sunrise_land.cpp`:
  - `kFlankTop` and `kFlankFall`: the upper canopy band's top and how fast it falls away
  - `kNearCanopy`: the nearer band's top
  - `kFoot1` and `kFoot2`: the foothills' crests
  - `kHollowW` and `kHollowD`: the hollow under the tip
  - `kCanopyLit` and `kCanopyGreen`: how much the canopy warms and greens with the light
- **Cost.** Memory is unchanged at 29,800 bytes. The sunrise frame is about 9,200 px composited,
  close to the old 9,000. All the new ground is in the static cache, so it costs nothing a frame.
  It only makes the cache repaints at the light steps heavier: on the PC the average cache repaint
  went from 1.7 to 8.3 µs a frame, about 2 ms more for each light-step repaint. I have not measured
  that on the panel.
- **Third pass: a real rock ledge.** Mike approved the near ground, so it is unchanged. The ledge
  is now weathered sandstone, not a ramp. `05_ledge_closeup_x2.png` shows it at 2x.
  - **The lip.** It is lumpy, with a step down partway out and two small notches. The rock's edge
    rounds over into the face as a lighter, wandering shoulder.
  - **The tip.** It is a thick, blunt block, raised a little at the nose. Its end face catches the
    sun.
  - **The overhang.** Its underside is dark and stepped where blocks fell out, with a deeper
    shadow right under the lip.
  - **The front face.** It is in blocks of slightly different tone, split by joints that open at
    the lip and close as they go down. Its foot is broken into steps.
  - **The support cliff.** It steps down and back in blocks, with a chipped corner, wedge-shaped
    joints of uneven length and a ledge standing off it.
  - **The ruled lines are gone.** The evenly spaced parallel lines on the face and the cliff are
    removed.
  - **On top.** There are clusters of darker and paler weathering, three shallow pans with the
    sky's colour lying in them, short kinked cracks, and a tuft of grass at the back.
  - **The figures.** They still stand on the straight lines between the edges. The edges only
    wander outward (the lip down, the back edge up), so nobody floats or sinks anywhere along the
    walk, the seat or the dogs' places.
  - **What is stored.** Everything is static and comes from a hash of the ledge and the ridges, so
    nothing is stored. The only random draw used differently is the tip's thickness, now twice
    what it was.
  - **Settings, in `sunrise_land.cpp`.**
    - `kLipLump` (1.3): the lip's lumps.
    - `kLipStep` (4): its step.
    - `kNoseRise` (1.6): the nose's lift.
    - The tip block's thickness is `sl.face_tip` in `sunriseLandGenerate`.
  - **Cost.** The sunrise is about 9,000 px composited a frame, as before. The average cache
    repaint on the PC is 7.7 µs a frame, where it was 8.3. The scene block is still 29,800 bytes.
