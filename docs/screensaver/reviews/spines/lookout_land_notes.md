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
