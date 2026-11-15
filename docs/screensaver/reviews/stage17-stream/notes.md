# Stage 17: the stream (round 2)

Mike asked for a less dull stream: things floating by now and then, a fish popping out, a little rock
outcropping the water flows round. Passive and always on, in the evening, morning and midday camp
(never the Lookout or the sunrise). No story beats. After the first sheet he asked for bigger fish, the
heron and the deer for real, and leaves that follow the season. The duck and the dragonfly stay mockups.

Code: `screensaver/core/scene/stream.h` and `stream.cpp`. The boulder's place is in `landscape.h`
(`LS_BOULDER_*`, `landscapeBoulder()`), the month is `Inputs::month` in `ss_app.h`. Stills from
`screensaver/tools/stream_stills.cpp`, turned into these PNGs by `screensaver/tools/stream_sheet.py`.
Close-ups are scaled up with no smoothing, so each panel pixel shows as a block. Nothing here has been
checked on the panel yet.

## The tiles (numbers as on `contact_sheet.png`; each still is followed by its close-up)

1, 2. `01_day`: the camp by day. The boulder is right of the middle of the stream. Water parts round
   it: a pale pillow on its upstream (right) side, foam, a V of ripples trailing downstream, calmer
   water behind it and its reflection. Three small stones by the near bank.
3, 4. `02_dusk_firelight`: dusk with the fire lit.
5, 6. `03_night_moon`: night with the moon up; glints of moonlight come and go on the wake.
7, 8. `04_leaf`: a leaf with no month known (the year-round mix of colours).
9, 10. `05_stick_snagged`: a stick caught on the boulder's upstream face (3 to 7 s, then it swings free).
11, 12. `06_pine_cone`: a pine cone, and a stick behind it.
13, 14. `07_fish_jump`: the bigger fish at the top of its jump: about 12 px, a dark back, a pale belly, a
   forked tail, a glint along its back. The jump is now 30 px long and 22 px high.
15, 16. `08_fish_splash`: the splash as it goes back in (a crown of drops).
17, 18. `09_fish_rings`: two rings spreading where it went in.
19, 20. `10_rise_ring`: a rise with no fish.
21, 22. `11_rain`: rain: faster, a little browner, more debris.
23, 24. `12_heron_arriving`: the heron flying in, wings beating, legs trailing.
25, 26. `13_heron_standing`: standing in the shallows on the near side, neck drawn in.
27, 28. `14_heron_strike`: a strike, neck shot out down to the water.
29, 30. `15_heron_ring`: drawing back up, the ring where it struck.
31, 32. `16_deer_walking`: the deer walking in along the camp-side bank from the right.
33, 34. `17_deer_drinking`: head down at the water's edge, rings at its muzzle.
35, 36. `18_deer_looking`: head up, looking round, between drinks.
37, 38. `19_leaves_summer`: a summer month: green leaves, and few of them.
39, 40. `20_leaves_autumn`: mid autumn: orange, russet and gold, many of them.
41, 42. `21_leaves_winter`: winter: a very few brown leaves (here one, and a stick).
43 to 46. `22_duck_mockup`, `23_dragonfly_mockup`: MOCKUPS ONLY, posed for these stills, never in the scene.
47, 48. `24_dog_heron`: Lucy is out; she has seen the heron and charges at the bank as it lifts off.
49, 50. `25_dog_deer`: Copper runs at the deer; it bolts before he is anywhere near it.

## The visitors, as they behave

- They are rare, and never both in one loop. A loop's chance is about 0.8% for the heron and 1% for the
  deer by day, three times that at dawn and dusk (the heron also all through the morning family). None
  at full night, in rain or storms, or with a shower falling.
- They come only in a quiet moment: a pause or a calm filler (read, the mug at sunset, cloud watching).
  There must be no major, no minor running, no other story critter, nobody within about 220 px of the
  spot, and two and a half minutes of the loop left.
- They leave calmly when:
  - a beat begins that uses the water or the banks (the skipped stone, listening at the stream, the
    cairn, birds on the water, the wade, the ford, the douse, the wash, the bear, dinner, the close), or
  - fire and ice or the visiting couple start, or
  - the loop is 40 s from its end.
- The deer also leaves for any beat that owns the critters. The heron stays through a major that keeps
  away from the water, and leaves for those that use it (fishing, fish leap, bathe, the squirrel's
  splash, the fetch, the bike).
- They leave in a hurry if the hiker comes within about 140 px (the heron) or 120 px (the deer), or a
  house dog within 130 px.
- **The dogs scare them off** (Mike: dogs will scare away wildlife that comes on to the scene). When a
  visitor arrives or is there and Lucy or Copper is out in camp (and not settled for a beat, lent to the
  visitors' frisbee, in fire and ice, or being patted), one dog goes for it (housedogs.h, the `chase`
  activity, never picked at random). It goes for the heron once it is landing or standing, and for the
  deer once it is well in from the edge (so the deer has somewhere to bolt):
  - it notices: head up, a look, about 0.7 s;
  - it runs at it at its run pace; the moment it starts running the heron takes off in a hurry or the
    deer bolts, away from the dog (stream.cpp threatNear counts a charging dog at any distance);
  - it pulls up on the camp-side bank: across the water from the heron, about 46 px short of where the
    deer stood;
  - it looks after it (Lucy about 2.6 s, Copper about 1.8 s), then takes up again what it was doing.
  - No random draws but the dog's own stream's, so a seed replays exactly; with no dog out nothing changes.
- **The heron:** flies in over about 5 s. It stands 30 to 90 s on the near side's shallows at x 400 to
  480, clear of the raccoon bush's bank and the chip's corner. While it stands it rests, stretches its
  neck up, takes a slow step, looks back over its shoulder, and now and then strikes at the water. Then
  it crouches and flies off, rising before it crosses the chip's corner.
- **The deer:** it is the scene's own deer, in a story critter slot tagged for the stream.
  - It walks in from the right edge along the camp-side bank to x 668 to 740, which is right of the
    water wheel's bank (x 572 to 640).
  - It steps down to the water, drinks for 4 to 7 s, looks round for 2.5 to 4 s, drinks again for 3 to
    6 s, and walks back out the way it came. Rings spread at its muzzle a little over every second.
  - If it is spooked it runs off; the director takes any critter it scatters.

## What already happens at the water (so it isn't offered twice)

- The dogs drink.
- skip_stone.
- fishing_small, fishing_log and fishing_good.
- fish_leap.
- The midday wade: the fish at his feet, a slip, stones.
- bathe, with dead fish drifting off.
- birds_on_water: small birds drifting, close to a duck.
- squirrel_splash.
- frog_splash.
- bird_chip's crumb.
- He cools his hand, washes, and fills the bucket.
- stream_listen.
- The cairn.
- The midday bike through the water.
- The ford.
- Rain rings.

## What changes the look (all in `stream.cpp` unless noted)

| Constant | Now | What it does |
|---|---|---|
| `LS_BOULDER_X`, `LS_BOULDER_LANE`, `LS_BOULDER_R`, `LS_BOULDER_H` (landscape.h) | 612, -0.12, 18, 19 | The boulder's place and size. |
| `rockCols()`, the `kRock`/`kTop`/... tables | | Its colours and facets. |
| `kFloatGapMin`, `kFloatGapMax`, `kRainGap` | 18 to 42 s, 0.45 | Time between floaters, and in rain. |
| `kSeasons` | | By month: the share of floaters that are leaves, their colours, and how often anything comes. |
| `leafCol()` | | The leaf colours: orange, russet, gold, green, winter brown. |
| `kFishLen`, `kJumpLen`, `kJumpH`, `kJumpS` | 12, 30, 22 px, 0.95 s | The fish's size, and its jump's reach, height and time. |
| `kFishBody`, `kFishTail`, `kFishBelly` | | The fish's shape. |
| `kSplashS` | 0.5 s | The splash going back in. |
| `kFishGapMin`, `kFishGapMax`, `kJumpChance`, `kTwilightGap` | 20 to 60 s, 0.22, 0.5 | How often fish show, and how many of them jump. |
| `kHeronChance`, `kDeerChance`, `kTwilightVisit` | 0.008, 0.010, x3 | How often the visitors come. |
| `kHeronS` | 1.15 | The heron's size. |
| `kHeronStayMin`, `kHeronStayMax` | 30 to 90 s | How long the heron stays. |
| `heronNextAct()` | | The heron's small actions and how often each comes. |
| `kHeronX0`, `kHeronX1`, `kDeerX0`, `kDeerX1` | | Where the heron stands and where the deer drinks. |
| `kHeronSpook`, `kDeerSpook`, `kDogSpook` | 140, 120, 130 px | How close is too close. |
| `kWaterBeats`, `kWaterMajors`, `kStartBeats` | | Which beats they come in and which send them off. |
| palette.cpp, under the rain gloom | 0.30 | How much browner the water goes in rain. |

## Notes

- **Month:** `Inputs::month` is 1 to 12 from the panel's clock, 0 when the clock isn't valid. Month 0
  keeps the first round's mix exactly.
- **Freeze:** there is no outside temperature input, so there is no freeze variant.
- **Water wheel:** a later water wheel can stand on the camp-side bank beside the boulder
  (`landscapeBoulder()`). Nothing of the stream draws there.
- **Unverified on the panel:** the look at real size, the draw cost and the timing.
