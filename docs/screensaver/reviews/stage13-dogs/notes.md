# Lucy and Copper in camp, and the arrows upstairs (stage 13)

For review on the PC before anything goes to the panels. Nothing is flashed, nothing is committed.
Look at `contact_sheet.png` first (28 tiles); the full frames are here as `NN_name.png` (800 x 480).

## What changed

1. **Lucy and Copper are in camp while they are out.** When a dog goes out it trots in from the left
   within about 4 seconds (at a loop's start, about 4 seconds after the camp has built up), in the
   evening, the night, the morning and midday. It stays until it comes in, then trots off to the
   left (gone within about 13 seconds). Both can be there together.
2. **They behave like dogs**, one thing at a time, picked at random from what the camp offers right
   then: sniff about, sniff along the tent (once it is pitched), lie by the fire (mostly at night,
   when it is lit), drink at the stream (rings on the water), sit and watch the hiker, chase a leaf
   that skitters off (a play-bow, a run, a pounce), go to him for a pat while he sits still (Lucy
   also while he stands), nap with the chin down, and Copper's play-bow at Lucy and a dash round
   her. Nothing follows itself or the one before it; holds last a random while. Lucy is calm (long
   lies, a slow heavy wag), Copper busy (short holds, a quick wag, more sniffing and chasing). While a
   beat needs the camp (a major, dinner, the bear, the close) they lie out of the way by the tent;
   if the bear comes they sit up and watch it.
3. **They look like the real dogs.** Lucy: black, white muzzle, throat, chest and paws, a narrow
   blaze, broad head, short square muzzle, folded rose ears, deep chest, strong tapering tail, a
   heavy steady trot; about half the hiker's height. Copper: golden copper, small (16 px tall to
   Lucy's 37, about 0.43 of her height, as asked), domed head, long darker auburn ears that swing as
   he trots, a feathered pale-gold plume of a tail, feathering under the belly, a quick springy trot.
   Her white goes cream at golden hour and moonlit grey-blue at night; his gold goes dim brown at
   night. See the close-up pairs (tiles 25-28: the same frame, the same scale, day and night).
4. **The fetch.** The story's fetch is played by the house dogs when they are out: Lucy plays it
   (Copper if only he is out) and Copper is the second dog when both are out. As it begins they run
   off left, the fetch's dog trots back in with the stick (it waits until they are out of sight),
   and afterwards they come back in. A midday loop with a dog out still always has a fetch. With no
   dog out the fetch keeps its brown camp dog, as before.
5. **The Lookout:** no dogs (the cheapest and safest: nothing to place on the ledge). They trot
   into the next camp loop.
6. **Upstairs, the tenant's arrows sit beside the dog badges** (tiles 19-24), drawn as the UPSTAIRS
   tile draws them: mild heat light red, mild cooling light blue, urgent red or blue with a doubled
   head, flashing lit and dim half a second each; newest on the right, nearest the badges. On the
   scene each gets the badges' thin outline, and at night the colours step a quarter of the way
   toward the night badge (no bright discs at night). This is the real core drawing, not a mock.
7. **The downstairs mail badge no longer flickers.** It is now drawn into each strip as the
   renderer composites it, before the strip reaches the frame buffer, so the scene under it never
   shows (it used to be stamped after the whole render). Taking it away redraws only its square.

## The tiles

| # | Tile |
|---|---|
| 1-3 | Lucy alone, Copper alone, both: day |
| 4-6 | the same at golden hour |
| 7-9 | the same at night (panel night gain 1.6), the fire lit |
| 10-16 | behaviours: a pat for Lucy, a pat for Copper, a drink, a leaf chase, sniffing the tent, Copper's play-bow at Lucy, a nap |
| 17 | both in the morning camp |
| 18 | the fetch, Lucy playing, Copper shadowing |
| 19-24 | the upstairs badge row with mild and urgent arrows, by day and at night, each close-up lit and half a second later dim |
| 25-28 | close-ups: Lucy and Copper by day and at night, x4 |

## Choices for you (all in `screensaver/core/scene/`)

| What | Where | Now |
|---|---|---|
| Lucy's size | `kLucyScale`, `critters.h` | 1.3 (half the hiker) |
| Copper's size | `kCopperScale`, `critters.h` | 0.62 (0.43 of Lucy). A real 15 lb spaniel beside a 65 lb pit/boxer would be nearer 0.6 of her height; you asked for well under half |
| How soon a dog comes in | `housedogs.cpp` stepDog, `rnd(d, 1.f, 3.5f)` | 1 to 3.5 s, plus about a second to walk into sight |
| Each dog's speed, calm and wag | `kNature`, `housedogs.cpp` | Lucy calm and slow, Copper busy |
| What they choose and how often | `pickAct`, `housedogs.cpp` | weights, night and day |
| The arrows at night | `drawArrow`, `chip.cpp` | a quarter of the way to the night badge |

## Cost

- Memory: the scene block grows by 712 bytes of PSRAM (29,056 of 32,768): two more critter slots,
  the dogs' state (116 bytes) and the chip's arrows. No new buffers, no new static RAM. The panel
  host keeps no more than before (the mail badge sprite is the same).
- Stack: a critter drawing's shape list grows from 12 to 16 shapes (about 560 bytes more stack in
  the LVGL task, which has 16 KB).
- Flash: about 40 KB more PC object code (x86-64, -O3: the dogs' behaviour 18 KB, the two rigs 16
  KB, the arrows 5 KB); on the panel probably 20 to 30 KB. Not built for the panel.
- Per frame with no dog out: nothing. The dogs' step returns at once and draws nothing.
- Per frame with both dogs out (PC, the same runs with and without them): about 10% more pixels
  composited (69,000 to 76,000 a frame by day) and 0.1 more strips a frame; draw time on the PC
  went from 123 to 190 microseconds a frame. On the panel I'd expect about 5 ms more a frame while
  both are about (the extra pixels at the panel's ~1.3 px/us); not measured there.
- The urgent arrows' flash redraws only the arrows' strip columns, twice a second.

## Tests

`screensaver/tests/test_house_dogs.cpp` (345 checks): no dog out never uses their slots; arrival
within 6 s and leaving within 25 s in three families, one dog and both, and from a loop's start;
they stay in the frame, out of the chip's corner and out of the water; the same seed and dog
changes replay tick for tick and frame for frame; five or more different activities each, never
one twice running; no dogs in the Lookout; every frame equals a full render; never two Lucys (the
fetch's and the house's) in sight at once; the arrows' place, flash and night colours; the host
overlay. `test_dog.cpp` now checks Lucy and Copper in the rig and as the fetch's dogs. All 28
screensaver tests pass; a 6-hour soak is clean (it changes dog_out on a schedule).

## To build these again

See the top of `screensaver/tools/dog_stills.cpp`, then `screensaver/tools/dog_sheet.py`.
`ss_preview --dog 3 --chip --req 2,3,1` shows the dogs and the arrows in the preview.

## Not yet judged

- The dogs, their colours and sizes on the panel itself, in a dark room at night.
- Whether Copper at 16 px tall reads as a spaniel from across the room.
- The panel host change in `ss_host.cpp` (the arrows from the view, the mail badge per strip) is
  written but not compiled: no firmware build was run tonight.
- The real draw time with both dogs out, and the mail badge's flicker gone, on the downstairs panel.
