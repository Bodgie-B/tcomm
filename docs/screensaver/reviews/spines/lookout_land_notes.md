# Lookout landscape (`lookout_land.h/.cpp`): notes

Static landscape and tower for the Lookout scene (SPINES_WEATHER_LOOKOUT_SPEC.md section 5).
This covers no hiker, no beats and no sunset stepping. Not registered anywhere by hand,
but `CMakeLists.txt` globs `core/*.cpp`, so `core/scene/lookout_land.cpp` joins the core
library on the next CMake configure. All helpers are in an anonymous namespace.

Files:
- `screensaver/core/scene/lookout_land.h`, `lookout_land.cpp`
- `screensaver/tools/lookout_stills.cpp` (standalone stills and checks, not in CMake)

`sizeof(LookoutLand)` = **1916 bytes** (clang, arm64). The limit is 4 KB.

## Composition

- **Sky:** the camp's five flat bands (0/58/118/178/228). The lowest band runs on under the
  valley. The sky is left plain, with no sun, moon, stars or clouds; the dynamic layer adds
  the horizon clouds, birds and the one star.
- **Valley:** three ridge layers in RIDGE_FAR, RIDGE_MID and RIDGE_NEAR. They use the camp's
  generator (base plus three seeded table sines, sampled every 16 px) with bases at
  236/258/286, so the horizon sits around y 272 as in the camp. A serrated tree line in
  TREES_FAR runs along the near valley crest.
- **Hillside:** a skyline sampled on the same 16 px grid. It runs from y 372 to 384 at the left
  edge and climbs on a smoothstep, with a small seeded wobble mid-slope, to a flat ridge at
  `tower_gy` (180 to 194) from x of about 550 to 590. The ridge is held exactly flat for
  76 px either side of the tower, so the tower stands on a level line. The ridge top rolls
  gently elsewhere.
  - The hill is in **two tones** for depth. The farther upper slope is
    `mix(GROUND, RIDGE_NEAR, 0.3)`. The near shoulder is `GROUND`. Its edge leaves the
    skyline on the left and falls away to y 300 to 326 at the right edge.
  - A low foreground band in GROUND_DARK sits along the bottom (y about 452 to 468).
- **Trail:** 5 or 7 legs (odd, so the last turn is a left end and the last leg climbs right to
  the stair foot). It starts off the left edge at y 446 to 454. Turn heights are spaced so
  the lower (nearer) legs climb more on screen. Right ends drift from x of about 500 toward
  the tower. Left ends sit under the skyline with a margin that shrinks with height.
  - It is drawn 2.6 px wide at the foot, narrowing to 1.6 px, as a lighter ground colour:
    `mix(tone, GRASS, 0.55)`, where tone is the hill colour under it.
  - Each leg is split exactly where it crosses the shoulder edge (by sampling, then
    bisection), so the colour change sits on that edge.
  - It is visible but quiet in every mood, and very faint at 5.9 by design.
- **Pines:** 11 to 14 per seed. Half the placement tries cluster toward the foot. They thin up
  the slope (acceptance drops with height) and shrink with it (height × (1 − 0.62 d)).
  - Colours: body FOLIAGE_DARK, a shade wedge in `mix(FOLIAGE_DARK, ACTOR, 0.45)` on the
    east side (the sun is west), trunk TRUNK.
  - Rejected placements:
    - on the ridge top near the tower (`x + hw > tower_x − 100` and `y < gy + 50`);
    - in the quiet zone;
    - within 9 px of the trail;
    - any pine standing in front of a trail leg whose crown would cross that leg. The hiker
      is drawn over the cache, so a nearer crown would be wrong in depth.
  - Pines behind the trail are allowed.
- **Grass:** 18 to 28 tufts in GRASS between y 396 and 478, kept off the trail. As in the camp,
  they stay out of the quiet corner.
- **Quiet zone** (x ≥ 660, y ≥ 420): holds only the GROUND and GROUND_DARK fills. The stills
  tool checks every pixel there in every still: 0 failures.

## The tower (original design)

The view is a simple oblique from the south-west. The front (south) face is square to the
viewer and the west face shows to its left. Depth projects to (−18, +4) px, so receding
edges fall slightly and the deck and eave undersides show, as they should from below.
West is frame-left, where the valley and the sunset are.

The tower is about 113 px from the ridge to the roof apex, plus an 11 px flagpole. Parts:

| Part | Geometry | Colour (full reveal) |
|---|---|---|
| 4 legs | front legs splay 40 px to 30 px half-width over 58 px; back legs offset by depth | front: `lit`; back: `back` |
| X bracing, 2 tiers | girts at 3, 29, 55 px; X on the front and west faces; back girts seen between the legs | `front` (west face `front` too) |
| Zigzag stair, 2 landings | flight up-right, landing, flight up-left, landing, flight up-right to a gap in the front rail; sawtooth treads, handrail on posts | `lit` |
| Deck / catwalk | 9 px wide all round, front and west edges plus an underside sliver | `lit`, underside `back` |
| Rails | top 20 px and mid 10 px; front rail has a stair gap at x +12..+24; north and east rails drawn behind the cab | front `front`, west `lit`, behind `back` |
| Cab | 60 px square, 44 px deck to eaves; front and west walls | front `front`, west `lit` |
| Windows | three front panes (16 px each) and one west pane, band 15 to 40 px above the deck | glass ACTOR |
| Glint (the near two: west pane and front-left pane) | lit glass `mix(ACTOR, SKY4, 0.3 g)` and two slanted stripes `mix(ACTOR, SKY4, g)` | g = glint amount, below |
| See-through | middle pane shows the back-wall window, right pane the east-wall window, as SKY1 (the sky band behind the cab) | SKY1 |
| Fire-finder table | pedestal, disc, two sight vanes and the sighting wire, silhouetted in the middle pane | ACTOR |
| Hipped roof | shallow pyramid: front slope, west slope, apex over the cab centre, 5 px overhang, fascia, west soffit | front `roof_f`, west `lit`, soffit `back` |
| Flagpole stub | 11 px with a knob | `lit` |

Timber colours: `lit = mix(TRUNK, ACTOR, sil)`, `front = mix(TRUNK, ACTOR, 0.3 + 0.7 sil)`,
`back = mix(TRUNK, ACTOR, 0.55 + 0.45 sil)`, `roof_f = mix(TRUNK, ACTOR, 0.5 + 0.5 sil)`.
Each shape is one flat colour (a fixed mix of two slots, recomputed when the palette or mood
changes). There is no alpha.

- **`sil` (silhouette amount):** 0 up to mood 5.0, rising to 1 at 5.8, 1 through night, and
  falling back over 0.6 to 1.0 (dawn). This is needed because TRUNK and the sky bands converge
  near night: at 5.9, TRUNK has luminance 33 and SKY1 has 36, so plain TRUNK would vanish. At
  5.9 the tower is ACTOR (luminance 3) against a sky of 22 to 48.
- **`g` (glint amount):** `window(m, 3.2, 4.0) × (1 − window(m, 4.6, 5.4))`. That gives about
  0.4 at 3.5, full from 4.0 to 4.6, half at 5.0, and 0 from 5.4. The windows catch the sun at
  4.0 to 4.5 and are dark from 5.5. The see-through panes still show the sky at dusk; that is
  the sky, not a highlight, and it keeps the cab reading as a cab in the 5.9 silhouette.

**Reveal** (`tower_reveal`):
- 0 draws nothing.
- 0 to 0.5: everything in flat ACTOR, rising with easeOutCubic from fully below the ridge.
  The tower is clipped to y < `tower_gy`, so it emerges from behind the crest.
- 0.5 to 1: detail switches on in three flat steps:
  - k ≥ 0.2 (reveal 0.6): frame (legs, bracing, stair);
  - k ≥ 0.5 (0.75): deck, rails, cab walls, roof;
  - k ≥ 0.8 (0.9): windows (see-through, finder, glints).

## Accessor API (all read only the struct)

- `lookoutLandGenerate(lk, rng)`: one rng call per statement throughout.
- `lookoutLandDrawStatic(lk, frame, pal, mood, tower_reveal)`: full frame, origin_y 0.
- `lookoutHillAt(lk, x)`: skyline y, matching the drawn edge.
- `lookoutTrailAt(lk, s)`: feet position by arc length.
  - s = 0 is (−24, foot_y), off the left edge.
  - s = 1 is the stair foot, which equals `lookoutStairAt(lk, 0)`.
- `lookoutTrailScale(s)`: `lerp(1, 0.49, s)`, linear, so it is monotone (90 px to 44 px).
- `lookoutStairAt(lk, s)`: feet position by arc length over 6 knots: foot, top of flight 1,
  far end of landing 1, top of flight 2, far end of landing 2, deck at the rail gap.
  - On each landing he walks to the end and turns.
  - Flight rise is 19.3 px. The feet line runs through the tread middles.
- `lookoutStairKnots(lk, knots[6], s[6])`: for pausing on a landing. Landing 1 is
  `[s1, s2]` and landing 2 is `[s3, s4]`.
- `lookoutRailSeat(lk)`: the west deck edge at mid depth, (tower_x − 48, deck + 2). He faces
  frame-left with legs over the edge, and the top rail is 20 px above.
- `lookoutCabPoints(lk, out)`:
  - `peek_feet` and `peek_eye` for the front-left pane, the front-right pane and the west pane
    (the last from the west catwalk);
  - `finder`, the table top, about waist height on a 44 px hiker, and `finder_feet` inside the
    cab, behind the front wall;
  - `compass`, on the front top rail right of the stair gap, and `compass_feet`;
  - `stair_head`, `deck_y`, `rail_y`.
- `lookoutTowerBounds(lk)`: integer bounds at full reveal (pole to ground). They are about
  104 × 124 px and never in the quiet zone: the largest y1 is 194.

## Checks run (`tools/lookout_stills`)

- Quiet zone: every pixel in every still is GROUND, GROUND_DARK or a blend of the two.
  0 failures.
- Tower bounds never in the quiet zone (4 seeds).
- Night caps with night_dim = 1 at mood 5.9 and at 0.1 (the fade target): frame maximum
  luminance 0.135, against a cap of 0.60. The scene uses no slot above ordinary luminance
  except the glint, which is 0 at those moods.
- Without night_dim, the frame maximum at 5.9 is 0.25. The brightest pixel is the sky band.
- Both compilers are clean, and they produce bit-identical output (81 PPMs compared).

## Stills

All are in `/private/tmp/claude-501/-Users-mark--claude-spares-spare-mjlbDH/8ba60bf2-9ee9-4fb0-b995-8ac90a7e3bcd/scratchpad/lookout/`:

- `lk_s{1..4}_m{35,40,45,50,55,59}_r{00,05,10}.png`: the 72 required stills (r05 is reveal
  0.5, r10 is reveal 1).
- `lk_s1_m40_r{20,35,60,75,90}.png`: intermediate reveal stages.
- `lk_s{1..4}_debug.png`: mood 4 with the accessors marked:
  - red bars: hiker height along the trail at s = 0, 0.1, …, 1;
  - cyan: stair samples and a 44 px bar mid-stair;
  - yellow: rail seat, compass and a 44 px bar at the compass;
  - magenta: peek feet and eyes, and the quiet-zone corner;
  - white: finder top and tower bounds.
- Contact sheets:
  - `sheet_moods_s1.png`, `sheet_moods_s2.png`: six moods;
  - `sheet_b.png`: four seeds in mixed moods;
  - `sheet_dbg.png`: the four debug stills;
  - `zoom_tower_seeds.png`: tower at 4.5 and 5.9 for each seed, 2x;
  - `zoom_reveal.png`: reveal 0 to 1, 3x.

## Unverified and open

- **Unverified on the panel:** all colours (IPS and RGB565), and how readable the 1.1 to
  1.5 px rails, bracing and finder table are at real size.
- **Scale tension:** a 44 px hiker against a tower of about 113 px gives a short tower. The legs
  are 1.3 hiker heights, the cab walls are as tall as he is, and flight rise is 19 px. I kept
  the spec's numbers. A taller tower (about 140 px) would read more like a classic lookout but
  would crowd the top of the frame.
- **Occlusion is not handled here** (the scripts' job):
  - the front rail and stair handrails should be redrawn over the hiker when he is on the
    catwalk or stair;
  - inside the cab he is behind the lower front wall and visible only through the panes.
  The rail drawing is a self-contained block in `drawTower`, so it is easy to expose as
  `lookoutDrawFrontRail` if wanted.
- **Not tested in the app or under CMake:** `World` integration, the `tests/test_lookout.cpp`
  checks, and the static-cache rebuild cost per palette step. The `lookout_land.cpp` fill
  count is modest (about 40 polygons and 70 capsules), but it has not been timed.
- **Window glint slot:** SKY4 mixed toward ACTOR, keyed on mood inside the draw rather than on a
  dedicated palette slot.
- **`lookoutTrailScale` is linear in arc length,** not in screen y. It is monotone as the test
  requires, but on long, nearly flat legs he shrinks while barely climbing.

## Sun and stars (2026-11-01)

Spec section 5b. Code: `lookout.cpp` (`planSun`, `planStars`, `drawSun`, `drawStars`,
`lookoutMarkDirty` and the helpers), `lookout_land.cpp` (`lookoutSkylineAt`,
`lookoutSkyTop`, `lookoutSkyBand`, the `sky_top` table), one call in `world.cpp`
`lookoutTick`. Tests in `tests/test_lookout.cpp`.

**What it does.**

- The sun is one flat `SUN` disc, radius 20 to 24. Its start centre x is 120 to 300, and the
  disc clears the skyline over its width by 60 to 84 px.
- It moves in a straight line in loop ticks, dx = 0.2 dy. The tick he is planned to be down
  (`sun_tick`) it is 7/8 below the skyline at its centre x. The end point comes from a
  bisection on the skyline.
- `sun_tick` is the build (325 ticks), plus the whole-second beats and pauses before the
  settle, plus `walkSeconds` from the explore's last spot (`towerP` 25, where `doToCompass`
  walks) to the rail seat at the deck pace, plus the sit's 1.8 s.
- It is drawn after the stars and before the clouds. It is drawn only in rows above
  `LookoutLand::sky_top` (one `fillCircle` per run of columns with the same skyline).
- During the build's sky fade it mixes toward SKY4.
- It is drawn at a pixel-centred position, so its image changes only when its integer bounds
  do. It marks the union of its old and new bounds when they differ. Once no pixel of it is
  above the skyline, it marks its last bounds once and then nothing more.
- Stars: 40 places, at least 8 px between a star's lowest row and the skyline. They keep 2 px
  clear of `lookoutTowerBounds` and out of the corner, and no two are within 10 px. Each star
  gets 64 tries, with one rng call per statement.
- The first eight are bright: discs of r 1.6 to 2.0, about a pixel centre. The other 32 are
  single pixels.
- Each star comes out in four steps (0.25, 0.5, 0.75, 1 of the mix from its sky band toward
  STAR), 0.5 s apart. Each step marks its bounds once.
- They are drawn after the sky and before the sun and the clouds. They stay through the
  fade's night step.
- Their clock is `LookoutState::sunset_tick`, set when the sunset begins. The old single
  `G_STAR` glyph is gone (glyph slot 3 is now free).
- Helpers: `lookoutSunBelow`, `lookoutStarsOut`, `lookoutSunAt`, `lookoutSunShown`,
  `lookoutStarLevel` and `LookoutState::sat_tick`. `sat_tick` is set in `actSitEdge` at
  u = 1, as the tick the world is about to count.
- **rng (revised the same day):** the sun and the stars draw from their own stream, as the
  weather does, tagged "LOOKSUN!". The loop seed does not reach `lookoutBeginLoop` (that
  would need `spine.cpp` or `app.cpp`), so the stream is seeded from the loop rng's state at
  the end of the loop start. That state is a function of the loop seed alone, and it is only
  read, never drawn from.
  - The loop rng's draws are as before the sun and stars. The old star's two draws stay in
    place, unused. So the next loop's seed is too: `--loops 3` output for seeds 1 to 3 is
    byte-identical to the build before 5b, and seed 1's loop 1 is seed 387781948 again.
  - For seeds 1 and 2, frames every 200 ticks over loops 0 and 1 differ from the build before
    5b only in the sun's disc and the stars' pixels (a pixel diff of 40 frames each).
- **Kept out from behind the clouds (Mike, revised the same day).** The start (x and
  clearance) is drawn up to 16 times from the sun's stream (8 at first; the chief raised it
  when 8 left 4 of 20 seeds half hidden for 30 to 47 s). Each try is scored by its worst
  cover: the share of the disc's part above the skyline that either horizon cloud's puffs
  hide. The puffs are the lit and shade ellipses at the drawn position, each padded by
  4 px, sampled on a 2 px grid.
  - Cover is checked at 8 evenly spaced ticks from 0 to the try's fully-set tick, and at the
    planned sit tick. The clouds are advanced as `cloudStep` moves them.
  - The least-covered try is kept (the first on a tie), and a try with zero cover ends the
    search. `LookoutState::sun_tries` and `sun_cover` record it.
  - A cloud edge drifting over the disc for a while is accepted. What this avoids is the
    disc more than half hidden for long.

**Numbers** (clang and g++-16 give the same results; from `test_lookout`):

| seed | r | start centre | clear | down speed | sat tick (planned) | below at sat | fully set |
|---|---|---|---|---|---|---|---|
| 1 | 21.2 | 130.9, 137.1 | 68.7 | 0.740 px/s | 4028 (4031) | 0.873 | tick 4288, 3.5 s before the sunset |
| 2 | 23.3 | 286.0, 135.5 | 86.3 | 0.799 px/s | 4053 (4056) | 0.873 | tick 4337, 2.5 s before |
| 3 | 22.6 | 273.2, 131.3 | 80.1 | 0.742 px/s | 4103 (4106) | 0.873 | tick 4437, 0.5 s before |
| 4 | 23.1 | 125.6, 143.5 | 66.4 | 0.649 px/s | 4053 (4056) | 0.873 | tick 4296, 4.2 s before |

Tries and cover for the kept start (worst cover is the plan's score, puffs plus 4 px; most
hidden is what the test measured once a second on the bare puffs):

| seed | tries | worst cover (planned) | most hidden (measured) |
|---|---|---|---|
| 1 | 16 | 0.20 | 0.13 |
| 2 | 16 | 0.16 | 0.10 |
| 3 | 16 | 0.15 | 0.10 |
| 4 | 1 | 0 | 0 |

These values come from the final version: the sun's own stream and the best of 16 cloud
tries. Earlier versions in this session had different starts.

- The "clear" column is measured at the centre column; the plan clears the minimum over the
  disc's width. Over 20 seeds the fraction below at `sat_tick` is 0.873. He sits
  3 ticks earlier than planned in every seed: the real walk is 0.12 s shorter than the
  `walkSeconds` estimate. The window is 0.8125 to 0.9375.
- Seed 1's star schedule (sunset 120 s), times when each star is full, from the sunset's
  start: the first at 55.0 s (46 %; its first step at 53.5 s), the eighth at 78.2 s, the 34th
  at 117.9 s, the last at 126.0 s (6.0 s into the fade). Seeds 2 to 4 are within 2.5 s of
  these.
- Over 20 seeds no star is out before 40 % of the sunset. At least 20 are out at its end, and
  all 40 are out at 7 s into the fade.
- Strips per frame (4 whole loops, seeds 1, 2, 3 and 7, build excluded): average 3.19 before,
  3.24 after. The maximum is 15 both times.
- Incremental equals full at 854 checkpoints. 248 of them are in the sun's crossing (every
  second while it is partly below, and at `sat_tick`) and 160 are on ticks when a star
  reached full.
- Palette rebuilds per loop: 10 to 12 before and after (20 seeds). The "11 to 13" in
  `notes.md` dates from before the review dropped the fade's second step.
- `sizeof(App)` went from 70,320 to 72,296. `LookoutLand` went from 1,916 to 3,516 (the
  800-entry `sky_top`). `LookoutState` went from 248 to 624 (the 40 stars at 8 bytes each,
  plus the sun's fields).

**Review fixes (chief, after the Opus review of the diff).** No high findings; the review
rendered seeds 1 to 120 incrementally against full renders on every tick (0 differences,
also across a loop boundary and with the night rule on) and showed the test catches a
missing dirty mark (three mutated builds, 6 to 44 failures each). Fixed: `sun_done` latches
the sun off the first tick after the sit that no pixel shows (past a pine corner the
skyline steps down and a pixel could show again for 2 to 35 ticks in 47 of 2000 seeds,
drawing a real pixel in 3 of them); the two "monotone" comments in `planSun` corrected;
a bright star's five rows are kept in one sky band (its mix base is the band at its
centre row); the `sky_top` comment says what it leaves out (the trail's top end, one row
above it in a few columns inside the tower's bounds; and a row where a crest's coverage
rounds to 0); the early-star test checks any lit step, not only full; the set-time range
and the stale numbers above restated. Not changed: the sun past x 365 never happens, so
the trail rows do not matter; a 90 s sunset is never planned over 300 seeds (checked by
hand by the review: the schedule still fits).

**The sun's colour (chief, from the first stills).** The key's `SUN` at mood 4 (0xFFE3A3,
pale cream) all but vanished against SKY3 and SKY4 (0xF4C87B, 0xF7D58C) once the disc was
low, so the half-below and sliver stills showed nothing. `lookoutPaletteTweak` now sets
`SUN` to 0xF28C3A (deep orange, luminance 0.61) whenever the mood is the loop's own 4.0,
which is the only time the sun shows; nothing else in the Lookout draws with `SUN` and the
camp's palette is untouched. It reads at every stage in the seed 1 stills. Unverified on the
panel.

**Behind the clouds, over 20 seeds** (from `test_lookout`; printed, not failing checks):

- **Retries:** 18 seeds drew the start again. 2 found a cloud-free start on the first try.
- **Kept start's worst cover** (planned, puffs plus 4 px):

  | worst cover | seeds |
  |---|---|
  | 0 | 8 |
  | above 0, up to 0.25 | 9 |
  | above 0.25, up to 0.5 | 1 |
  | over 0.5 | 2 (most 0.83) |

- **Measured once a second on the bare puffs,** the disc is more than half hidden for a
  second or more in 1 seed: seed 16, for 40 s, at most 0.70 hidden. With 8 tries it was
  4 seeds (5, 14, 15 and 16, 30 to 47 s each); under the first rule (8 tries checked
  against cloud bounds, the last one kept) 9 seeds, seed 1 alone hidden for 33 s.
- **Why seed 16 remains:** none of its 16 tries found a path past the lower cloud. A scan of
  the whole of 5b's range finds a path with worst cover of 25 % or less for 17 of 20 seeds;
  the rest would need a wider x range. Accepted: the sun is clear at the sit and through its
  crossing in every seed (the crest is below the clouds), and a cloud in front of the sun
  during the approach reads as weather.

**Deviations from 5b, with reasons.**

- **`lookoutSkylineAt` covers more than 5b lists.** Besides the three valley crests and the
  hill, it includes the near valley's tree line and the hill pines' tiers. Both are in the
  cache and sometimes poke above the crests. In 7 of seeds 1 to 20 they rise 3 to 18 px
  above them (seed 5: 18.2 px at x 359, a pine on the upper slope; none in seeds 1 to 4). A
  sun or a star drawn over them would show on them. Every use goes through this one skyline: the clip, the fraction, the 7/8 aim and the
  stars' 8 px. So the spec's checks hold against a stricter line.
- **Schedule times are when a star reaches full.** The four steps come at t - 1.5, t - 1.0,
  t - 0.5 and t. With the times read as the first step, the last star (6 s into the fade)
  would not be full by 7 s, and the test asks for all 40 by then.
- **`u_i = (i / 34)^0.6` uses i from 0 to 33.** The first star is at 45 % of the sunset and
  the 34th near 99 %. The 0.4 s minimum gap is applied forward after the ±1.5 s jitter. The
  fade's six fall at 0.5 + 1.1 j s, with no jitter.
- **The stream is seeded from the loop rng's state, not the loop seed.** The loop seed does
  not reach `lookoutBeginLoop`, and passing it in would mean changing `spine.cpp` or
  `app.cpp`. The effect is the same: the stream is a function of the loop seed alone, and
  the loop rng is untouched.

**Not as 5b's prose expects (decided).** The spec's prose says the last sliver goes in the
first 10 s or so of the sunset. With its numbers, it doesn't: he is down about 6.5 s into
the 20 s settle, and the last 1/8 of the disc takes about 8 s more at 0.65 to 0.85 px/s. So
the sun is fully set from 7.2 s before the sunset beat begins to 1.1 s after it (seed 14;
seeds 1 to 20, measured by the review), 11 to 19 s before its first palette step, while he
sits still at the rail. The chief kept it: the ask was the sun in its last eighth as he sits down, which
holds in all 20 seeds, and the last sliver going while he sits still before the sky
starts to turn is the right order. (Alternatives not taken: aim 5/8 below at the sit,
slow the last stretch, sit later.)

**Looked at** (seed 1, mood 4, `ss_preview --spine lookout --weather clear`, PNGs in the
session scratchpad). These are from the first version, with the sun drawn from the loop rng;
the star and sit frames look the same now, apart from the star places.

- **Seed 1, final version, ticks 340, 825, 2500 and 3700:** the whole disc is plainly
  visible at every one, left of the lower cloud's end, sinking toward the crest by 3700.
  (In the first version, at ticks 340 and 825, most of it was behind the lower cloud.)
- **Seed 5, the worst remaining seed with 8 tries** (clear with 16):
  - tick 340: clear, just under the upper cloud's left end;
  - tick 825: the top half shows above the lower cloud;
  - ticks 1200 and 1700: a cap above it, then a sliver below it;
  - tick 2500: clear below it;
  - tick 3700: low over the crest.

  It was behind the lower cloud for about 47 s in the approach. With 16 tries the one seed
  left like this is 16 (40 s); its frames were not looked at.
- seeds 2 to 4 at tick 825 (first version): clear discs, partly behind a cloud in 2 and 3;
- tick 3700: about half below, clipped cleanly along the far crest;
- tick 4028 (`sat_tick`): a thin sliver at the ridge;
- tick 4278: gone;
- tick 6175 (60 %): six or so stars, the bright ones, none over the tower or the hill;
- tick 7225 (95 %): about 30, spread over the whole sky;
- tick 7550 (fade + 7 s): all 40, the tower clear.

**Unverified.**

- **On the panel:** all colours, and the contrast at mood 4. `SUN` is a pale cream on the
  pale SKY2 to SKY4 bands. In the PNGs it reads clearly but softly, as the camp's does.
  Whether single-pixel stars at 1/4 and 1/2 of their mix are visible at all is also
  unverified.
- **Cost:** the loop-start cost on the board is untimed. Two parts:
  - the `sky_top` table (800 columns x 3 samples x crests, 80 tree teeth and the pine tiers);
  - the sun's cloud scoring (up to 16 tries x 9 samples x about 120 grid points x up to 14
    ellipses).
- **The ridge edge:** where the disc meets the ridge, the first row with any landscape
  coverage is left as the cache drew it (sky blended with ridge). So the sun stops one
  partial row short of the line. This was not visible in the zoomed PNGs.
