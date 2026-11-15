# Stage 18: the Christmas set, mockup stills (round 2)

Pictures to choose from, not the animation. Every piece is drawn by the engine (`screensaver/core/scene/xmas.h/.cpp`), so depth, palette (day, dusk, night, snow) and clipping are real. Each still is posed by hand from a mock state (`XmasMock`). Nothing is scheduled: no date logic, no setting, no story script.

**Laptop only.** All of it is behind one compile-time switch, `SS_XMAS` (`scene/xmas_switch.h`), which is off by default. The panel firmware compiles `screensaver/core` as it is, with no defines (`tools/sync_ss_core.py`), so on the panel the Christmas code compiles to nothing and costs nothing. The host builds (`screensaver/CMakeLists.txt`, option `SS_XMAS`, default ON) turn it on for the tests, the stills and the soak. With the switch on and the mock at zero, every frame is the same as with it off (`tests/test_xmas.cpp`).

Rendered with `screensaver/tools/xmas_stills.cpp` (compile it with `-DSS_XMAS=1`), then `xmas_sheet.py` (PNGs, close-ups, `contact_sheet.png`). All looks are PROPOSED and unverified on the panel.

## Round 2: what changed from Mike's review

- **Water wheel:** lighter, fresh-cut boards, by day and at night. The wheel is a little bigger (rim 17 px, paddles 7 px) and stands higher, clear of the boulder. It is still on its reserved bank, x 572-640.
- **North star:** a bright four-point sparkle in the upper left, with two twinkle stills.
- **Raccoon:**
  - a lighter grey coat, a pale belly, a paler face;
  - broader pale tail rings and a dark tail tip;
  - a little bigger;
  - a warm rim of the tree lights' glow on its tree side at night.
- **Snow look:** snow on the ground, the boughs (camp pine, mid and far pines, foreground pines), caps on the rocks, the boulder, the generator box and the tent, drifts at the rocks, the tree's foot and the tent's corners, and frosted grass.
- **Sleigh:** 1.4 times bigger, sitting on the moon with the team crossing it.
- **Lights:** multi-colour is the look in every still (`XMAS_LOOK` in `xmas.h`; `XL_WARM` switches back to warm white). The old warm-white tile 07 is retired.
- **Morning after:** kept as it was.

## The stills (contact sheet tiles: odd = whole frame, even = its close-up)

| Still | What it shows |
|---|---|
| 01_generator_day | The water wheel by the boulder: two rims of bent sticks, 12 pale board paddles, crossed-stick spokes, lashings. The axle runs back to a plank box on the bank, and a wire runs to the camp pine's foot. Foam and drops where the paddles meet the water. |
| 02_wheel_turned | The same, about half a paddle on (motion check). |
| 03_thump_sparks_night | He crouches at the box and brings his fist down. Sparks fly off it and the lights dip. |
| 04_lights_one_spiral_dusk | On the ladder, one spiral done, the rest of the string coiled below. |
| 05_lights_half_dusk | Three spirals up. |
| 06_half_string_dead | Night: the lower half of the string is dark. |
| 08_full_night | Night: all the spirals, multi-colour. |
| 09_full_day | By day: beads on a dark cord. |
| 10-13 | Garland: flown in by two cardinals; one tangled; part draped; draped in swags. |
| 14_ornaments_day | Red and gold balls. |
| 15_raccoon_reach_night | The raccoon at real size, up on its hind legs, a paw to the low ornament. It is lit on its tree side. |
| 16_raccoon_carry_night | Off with it, ball in its mouth, the garland yanked down. |
| 17_puzzled_morning | Next morning: he scratches his head at the gap (unchanged). |
| 18_raccoon_bush_dusk | The raccoon's own bush by the stream with the stolen balls. |
| 19_hero_eve_night | Christmas Eve night, with the north star. |
| 20_snow_day | Snow by day. |
| 21_snow_dusk | Snow at dusk: the snow takes the evening light. |
| 22_hero_eve_snow | Christmas Eve night in snow, with the existing falling flakes. No north star: snow falls from a cloud deck. |
| 23_presents_morning | Presents. The two bone-shaped parcels are Lucy's (big, black with a white ribbon) and Copper's (small, golden copper). |
| 24_stocking_tent | The stocking on the tent's front pole. |
| 25_sleigh_moon | The bigger sleigh across the moon. |
| 26 / 27_north_star | The north star's twinkle, bright and dim. |

## The north star

- **Where:** x 100, y 47, upper left. It sits:
  - above the clouds' usual band (the clouds ride at y 96 and 158, and their puffs reach up to about y 70);
  - far from the old sky's moon (x 640);
  - nowhere near the status chip box (x 538-796, y 412-476).
- **Its sparkle** fits inside rows 32-63, so a twinkle repaints exactly one strip.
- **How it's drawn:** a soft halo (radius 6.5), two thin diamonds (arms 11 / 8.5 / 6 / 8 px by twinkle step), and a pale core in the STAR slot. It shows only when it is dark.
- **To animate it cheaply:** step `north` through 1-2-3-2 about every 0.3 s, driven by `storyTick`, not the loop RNG. That marks one strip every few ticks: about 0.1 strip per frame on average.
- **A real build should skip it:**
  - where the real moon (SkyDraw) comes within about 40 px;
  - under a cloud deck or falling snow.

## The snow look (XE_SNOW)

- **What it reuses:** fire and ice's frost technique (`fireice.cpp` `fireIceDrawStatic`). One pass over the cached frame recolours pixels that are exactly the ground's slots:
  - GROUND becomes snow, with faint drift streaks and a rare glint;
  - GROUND_DARK becomes the snow's shadow;
  - grass is frosted.
- **Bank edges:** near the stream's banks, antialiased blends between those colours are recoloured too, so the bank's edge doesn't show as a dotted line.
- **The snow colour** is the stream's light (WATER_LIGHT), whiter than the ice. So it's white by day, peach at dusk and moonlit blue at night, always under the night caps.
- **Drawn as shapes into the same cache:**
  - the shelf along each pine tier's lower edge and a cap on the top tier, using the landscape's own pine geometry;
  - white tips on the far tree line's teeth;
  - caps on the rocks, cut in uneven slices;
  - drifts at their feet and at the camp pine's foot.
- **Cost:** none per frame. Toggling it rebuilds the cache once.
- **Pieces drawn per frame anyway get their snow where they're drawn:**
  - the boulder: drawn again in snow, top slices only;
  - the tent: a cap on the ridge and the upper panels, drifts at its corners;
  - the generator's box: a cap on the lid;
  - a foreground pine redrawn over an actor;
  - the swaying tufts: frosted through a palette copy.
- **Not reused:**
  - The flakes are the weather's own snow (`weather_code 2`), shown in the snow hero still.
  - The tent's own weather snow still builds up in that still, under the mockup's cap.
- **Edited files:** `fireice.cpp`, `landscape.cpp` and `weather.cpp` were not edited.

## Constants that change the look (`xmas.cpp`, top of the file)

- **Wheel:**
  - `kWheelR` 17 and `kPaddle` 7 set its size; `kWheelDip` 5 (how far the paddles go under the waterline).
  - `kPaddles` 12; `kDepthX/Y` (the boards' width).
  - The board colour is in `drawWheel`.
- **Lights:** `XMAS_LOOK` (xmas.h), `kMulti[5]`, `kWarmCore/kWarmHalo`, `kBulbR` 1.5, `kHaloR1` 4.2, `kHaloA` 105, `kDimLevel`, `kGroundGlowA`.
- **Garland:** `kTinsel*`.
- **Ornaments:** `kOrnR` and the places in `kOrn[]`.
- **Star:** `kStarR`.
- **Sleigh:** `kSleighK` 1.4.
- **North star:** `kNorthX/Y`, `kNorthLen[]`, `kNorthLvl[]`.
- **Snow:** `snowCol` / `snowShade` / `frostCol`.
- **Raccoon:**
  - `kCoonScale` 1.4.
  - Its colours are in `critters.cpp` (`kToneMasked`, compiled only with `SS_XMAS`).
  - The rim light is in `rimOf` / `rimPal`.

## What the pieces would be in the way of (unchanged from round 1)

- **Wheel and box** (x 572-640): the bank by the boulder, which the midday bike's lane, fetched sticks landing and the walk to the rock seat cross.
- **The wire:** across that walk.
- **The ladder:** where the boomerang sticks.
- **The presents:** where the bear and food-bag business happens.
- **The raccoon's bush:** the near bank the wade crosses.
- **The stocking:** on the tent's front pole.

## Cost (PC)

- **Memory:**
  - App is 29,808 bytes in both builds.
  - With the switch on, `XmasState` (92 bytes, the 13-byte mock plus its last-tick copy and rects) shares the Lookout's 624-byte running state with the stream's life (288 bytes), so it adds nothing.
  - With it off there is no `XmasState` at all.
- **Draw cost:** the lit tree costs about 0.65 ms on a full-frame redraw on this PC, measured in round 1. The steady lights should go into the static cache in a real build; see round 1.
- **Snow:** no per-frame cost.

## Not done, by design

- Story scripts, the December build-up, dates and settings.
- Sound.
- Snow on the fire ring's stones and on the small stones beside the boulder.
- Snow on the far ridges (not asked for).
