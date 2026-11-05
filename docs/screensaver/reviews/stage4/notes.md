# Stage 4 notes, first half: story spine beats 1 to 5

Written 2026-10-30. Not committed; the chief reviews and commits. This covers the
first half of stage 4: the hiker placed in the world, the camp props, the
`LoopPlan`, and the arrival, setup, firewood and fire beats with three variants
each. Dinner, food bag, bear and close are 8 s placeholders, and the calm
fillers are not built yet. The second agent replaces both.

## Files

New in `screensaver/core/scene/`:

- `props.h/.cpp`: `Tent`, `PackProp`, `FoodBag`, `Cairn`, `Item` (`ItemId`: mug, book,
  pot, stick, shoe, wood). Each has bounds, draw and equality functions. Also
  `hikerHandsMid` (see "Fragile") and the oversized armload.
- `spine.h/.cpp`: `LoopPlan`, `CampLayout`, the step interpreter, the scripts,
  `spinePlan`, `spineLayout`, `spineBuildTimeline` and `spineFormatPlan`.

Changed:

- `world.h/.cpp`: `World` gains `hiker`, `fx` (`HikerFx`), `hiker_rect`, the props,
  `plan` and `spine`. `ActorKind` gains HIKER, TENT, PACK, BAG, CAIRN, ITEM and
  FGPINE. `WORLD_ACTOR_MAX` goes from 24 to 40. `WorldDirty` gains the previous
  rect and state for each prop, and a `stepHiker` step is added to `worldTick`.
- `ambient.h/.cpp`: adds `FIRE_KINDLING` and `FirePit::kindle`, and the smoke
  pull fields `smoke_pull`, `smoke_pull_to` and `smoke_to_x`.
- `buildup.h/.cpp`: the fire phase now lays the stones only.
- `landscape.h/.cpp`: adds `landscapeStreamSpan`, `landscapeFgPineBounds` and
  `landscapeDrawFgPine`.
- `app.cpp` and `ss_app.h`: add `appLoopPlan`, `appHikerRect` and `appSpineOverruns`.
- `sim/preview_common.*`: the `loop=... start tick=T` line now ends in ` plan=<appLoopPlan>`.
- `tests/test_scene.cpp`.

`hiker.cpp` is untouched. So are the `critters.*` files and `tests/test_critters.cpp`.

## Loop timeline

Every loop runs: build (13 s), then arrival, setup, firewood, fire, dinner,
foodbag, bear and close. Each of these except close is followed by a `pause` of
8 to 20 s, drawn from the loop RNG. A final `ambient` beat takes whatever is left
of the 600 s. All durations are whole seconds, so the loop is exactly 15000 ticks
(tested).

Beat durations are fixed per variant. Each is a few seconds longer than its
script, so every beat ends with the hiker at rest. The table below gives each
script's finish point as a fraction of its beat, measured over 40 seeds.

| Beat | Variant | Seconds | Script ends at | What happens |
|---|---|---|---|---|
| arrival | a | 25 | 0.88 to 0.90 | Walks in from the left, out from behind the foreground pines, to the spot. Stops, looks around both ways, hands on hips, nods twice, faces the fire pit. |
| arrival | b | 29 | 0.87 to 0.89 | Walks in from the left past the spot, stops, turns, looks back, walks back to the spot, hands on hips, nods. |
| arrival | c | 33 | 0.89 to 0.92 | Enters from the right on the near bank and wades across (each step is lifted while his feet are in the water). Walks to the far-bank rock nearest the ford, sits beside it for 3 s, gets up, looks around, walks to the spot. |
| setup | a | 39 | 0.92 | Shared start (below). The tent goes half up, a peg bends and it slumps sideways. He crouches to look, fetches a rock (CARRY_ROCK) from beside the camp, hammers twice (the tent jumps on each hit), and the tent springs up with an overshoot. Hands on hips. |
| setup | b | 39 | 0.88 | Shared start. The tent goes fully up with an overshoot, then folds down into a heap. Stillness, then SCRATCH_HEAD. He crouches, tries again with REACH_UP, and the tent springs up and overshoots. Hands on hips. |
| setup | c | 35 | 0.90 | Shared start. Up first try. Hands on hips while he steps back to admire it, and his heel catches the guy line: arms fling up (YELP), he tips back, hops, overcorrects forward and recovers, and the tent shakes. He crouches to look at the line, then hands on hips. |
| firewood | a | 34 | 0.73 to 0.86 | Looks to the trees, walks off left behind the pines and out of frame. Away for `away_s` (6 to 10 s). Returns with an armload (CARRY_WOOD), crouches, and sets a wood pile down behind the pit on its left. |
| firewood | b | 62 | 0.88 to 0.94 | As a, then SCRATCH_HEAD at the small pile and a second trip (4 s away). He comes back slowly, staggering under an armload about twice the size (4 extra logs drawn over the carried wood). The pile becomes a heap (size 1.65). Hands on hips. |
| firewood | c | 35 | 0.77 to 0.90 | As a, but a 62 px stick drags on the ground behind him from his hands. He drops it beside the pile as `ITEM_STICK`. |
| fire | a | 31 | 0.87 | Goes round the back of the pit to its right and crouches to lay kindling (`FIRE_KINDLING`). BLOW: three puffs, each flaring the glow higher with a `sumSines` flicker and puffing smoke. It catches (`FIRE_LIT`) and he leans back to watch. He reaches in to push a log, flinches, YELPs, and jumps back 14 px. Runs to the stream bank, kneels forward with his hand in the water for 3 s, gets up, shakes the hand, walks back and sits by the fire. |
| fire | b | 43 | about 0.90 | Two blowing tries flare and die back, with a slumped sigh after the first. The third catches, then as a. |
| fire | c | 37 | 0.92 | As a. Once he sits, the smoke turns and bends toward him (`smoke_pull`). He leans away with his head turned, and after about 5 s it lets go. |
| dinner, foodbag, bear, close | — | 8 each | — | Placeholders. Dinner: keeps sitting. Foodbag: stands, the bag appears hung from the branch tip (so the prop is exercised), looks around. Bear: stands, hands on hips. Close: sits. |

Shared start for setup: shrugs the pack off (it stands at his heels), crouches
to rummage, picks the pack up and carries it to the tent's left side, and sets
it down. Then he goes to the tent's front corner, crouches and spreads the
fabric. All three variants then stand, REACH_UP while the back pole and then the
front pole go up, and CROUCH for the pegs. Anticipation and overshoot appear
throughout: the pose blends (REACH_UP and YELP wind up first; SIT and CROUCH
overshoot), the tent's `bump` overshoot on raising, the armload stagger, a
settle on every arrival, and a hold before each reaction.

`LoopPlan` also holds `pause_s[8]` and `away_s`, so the RNG fixes a loop's whole
timeline at loop start. Inputs are passed to `spinePlan` but not used yet.

## Camp layout (`CampLayout`, from the fire pit and the landscape)

Fire pit at (250 ± 12, 398 ± 5), from stage 2.

- Tent: centered at fire.x − 44 with its base at y 352. That is behind the pit
  on its left, so the fire, the bag tree and the stream stay clear. The door
  faces the fire.
- Spot: fire + (48, −10).
- Wood pile: fire + (−30, −8).
- Blowing: fire + (31, −2).
- Sitting: fire + (40, −6), facing the fire.
- Cooling: on the bank at fire.x + 95, with his boots at the waterline.
- Ford for arrival c: x 732 to 652. The near bank's y is clamped to ≤ 410 so he
  never enters the reserved corner.
- Rock for arrival c: the far-bank rock nearest the ford with x 440 to 640.
  Without one he sits on the bank at x 590.
- Cairn: the bank at fire.x + 150. It starts with 0 stones; stage 6 memory sets the count.
- Food bag: hangs from the branch tip at (camp_x − 82, 294).

## Props

| Prop | Drawn as | Primitives |
|---|---|---|
| Tent | 5-point body in TENT (TENT_LIT when lit), darker door triangle (`mixRGB` toward ACTOR), 1.2 px guy line once pegged. `progress` drives spread, back pole, front pole and pegs; `sag` slumps it; `bump` overshoots; `wobble` leans the apexes. With `lit` and `shadow` set, a 12-point ACTOR head-and-shoulders shadow sits inside. | 3 (4 with the shadow) |
| PackProp | The hiker's own 8-point pack outline plus the bedroll disc, standing upright. `held`: World puts it at his hands each tick. | 2 |
| FoodBag | HUNG: line plus sack polygon, rotated by `swing`, with `line` length adjustable for hoisting. DROPPED: lying sack. CARRIED and NONE: not drawn. | 2 |
| Cairn | 0 to 7 ROCK ellipses, shrinking upward. `pop` drops the newest one in with an overshoot. | n |
| Items | Mug (2), book (2), pot (3), stick (1, TRUNK, lying or dragged), shoe (1), wood pile (3, TRUNK, scaled by `size`). | ≤ 3 |
| Armload | 4 extra logs (ACTOR) above the hands while `fx.armload` > 0. | 4 |

Every prop's bounds come from the same geometry as its drawing, and
`props_sheet.ppm` (written by `test_scene`) checks that no pixel falls outside
them. The sheet shows the tent going up, slumped, collapsed, and lit with the
shadow; the pack both ways; the bag still, swinging and dropped; cairns 1 to 7;
every item both ways, including a dragged stick; and the armload.

## Hiker in the world

`stepHiker` runs in `worldTick`:

- `hikerTick`.
- `feet.y` follows the walk's path segment (the walk API only moves x).
- Wading lift: up to 0.045 × height on each step while his feet are inside the
  water polygon.
- `HikerFx` offsets (lean, head, lift, reach toward `reach_to`) are added to the
  composed pose. They are not accumulated, because `hikerTick` recomposes the pose.
- Armload stagger.
- `lit_from` from the fire's side when it is LIT.
- Held pack and items follow `hikerHandsMid`.
- Visibility: `visible = fx.on && bounds overlap the frame horizontally`. Off-frame
  walks hide him automatically, and held props hide with him.

He is drawn in the actor sort at base y = `feet.y`. He is marked dirty every
tick he is visible, because he breathes.

**Foreground pines:** they are in the cache, so a hiker walking in from the
left was drawn over them. When his rect (including anything he holds) overlaps
a foreground pine, an `ACT_FGPINE` actor at the pine's base y redraws that pine
over him, clipped to his rect. He now emerges from behind the trees and
disappears behind them on the firewood trips. The redraw blends the pine's
antialiased edges a second time, inside his rect only, so the edges darken
slightly. It is deterministic (the full-versus-incremental tests pass).

## Numbers

- `sizeof(App)` = 59,696 bytes (budget 98,304).
- Spine, arrival through fire, rendering every tick (`test_scene`, 3 seeds,
  mood 3): average 7.40 / 7.45 / 7.69 strips per frame, max 12, and 0.096 to
  0.100 ms/frame.
- Pauses only (ambient plus a breathing hiker): 7.37 / 7.38 / 7.88 strips per frame.
- The old ambient window (ticks 400 to 900, which now includes the hiker walking
  in): 7.06 / 7.22 / 7.64 strips per frame.
- `ss_preview --seed 11 --mood 4 --every 2`: 0.123 ms/frame average.
- Worst primitives in a frame: hiker 21 + armload 4 + pine redraw + props, still
  well under the ~150 aim.

## Tests added (`test_scene`)

- Spine determinism at ticks 500, 1500, 2500 and 3500. One App renders
  incrementally, the other does a full rebuild at each checkpoint.
- Reserved corner every 250 ticks over a full loop, for seeds 1 to 3.
- Every variant: seeds are tried until all 12 have run. That takes 6 seeds, under
  the cap of 40. While visible, the hiker's rect must stay inside the frame
  vertically, overlap it horizontally, and stay out of the reserved corner. Each
  of the four beats must end with him fully in frame, and `appSpineOverruns` must be 0.
- Spine strip and ms numbers. The pause budget is checked at ≤ 9.
- `hikerHandsMid` against the rig, for every pose and both facings.
- Props bounds and the props sheet.

`sizeof(App)` was already printed and checked.

## What I changed after looking at the frames

1. The hiker walked over the foreground pines when he entered from the left. I
   added the `ACT_FGPINE` redraw, so he now comes out from behind them.
2. The pack he set down beside the tent was drawn behind it. Its base y came
   from his crouched hands (349), which is behind the tent's base (352). On
   setting it down the pack is now put at his feet + 3.
3. The collapsed tent (`sag` 1) drooped its ridge below the ground line, which
   showed as an inverted V. The ridge middle is now clamped above the base, so a
   collapsed tent reads as a low heap.
4. The dragged stick showed in front of the foreground pine while he was behind
   it. It also stayed on screen after he left the frame. His rect now includes
   everything he holds (so the pine covers it too), and held props are not drawn
   or marked while he is out of frame.
5. Cooling his hand: a plain CROUCH left his hands at his knees, a hand's height
   above the water. I added `fx.reach_to` and an `actCool` that kneels forward
   (lean 0.5) with the near arm straight down, swishing. I also lowered the spot
   so his boots are at the waterline. The hand now touches the water.
6. Durations: setup b and firewood b overran on some seeds, and fire a and c had
   10 s or more of dead tail. I measured every variant's finish point over 40
   seeds and set the durations to leave 1 to 4 s of rest. That gives the table above.
7. Carrying wood: walking in from a STAND pose kept STAND as the rest pose, so
   the wood hung at his thighs. The scripts now set CARRY before the walk back.
   This is the rig's documented behavior: arriving keeps the carry pose only when
   the pose is CARRY.

## Decisions beyond the brief

- **Scripts are data.** Each variant is a `Step` table (walk, via, pose and hold,
  still, face, carry, act(fn, seconds), do(fn)) with positions given as
  `CampLayout` anchors plus offsets. The interpreter is about 60 lines. Beats keep
  the scheduler's `t`, but the scripts run on ticks, because a walk's length
  depends on the layout.
- **The pack is carried with a proxy.** The rig has no pack carry. The scripts
  set CARRY_BAG (which keeps the arms in CARRY while walking) and draw the pack
  over the sack at his hands.
- **The big armload is drawn outside the rig.** It is 4 logs on top of the
  carried wood, drawn after the hiker by World, so `hiker.cpp` did not need a
  scale flag.
- **REACH has no pose of its own.** It is CROUCH plus `fx.reach`, which swings the
  near arm toward `fx.reach_to`. Hammering, the burn, shaking the hand and dipping
  it in the water all use the same field.
- **The fire beat's blowing** uses a new `FIRE_KINDLING` state. Intensity follows
  `kindle` in 0.35 s rising and 1 s falling, so each puff flares and a failed try
  dies back. The build-up no longer lights the fire. `worldBeginLoop` already set
  it UNLIT.
- **The smoke pull** eases on the 12.5 Hz sub-tick with the puffs, so the
  existing smoke dirty marks cover it.
- **Fillers:** the `FillerId` enum and `LoopPlan::filler` exist, but
  `n_fillers` is 0 and the plan prints `fillers=none`. I did not want the log to
  name fillers that do not play.
- **The hiker's idle sub-rate** (ticking him every 2nd tick while still) was not
  needed. Pauses come in at 7.4 to 7.9 strips per frame.
- **The `--log-beats` plan field** is ` plan=` followed by the `appLoopPlan`
  string verbatim, as asked. The string contains spaces, so treat it as the rest
  of the line.

## For the second agent (dinner, food bag, bear, close, fillers)

**Adding a variant**

1. Write a `Step` table in `spine.cpp`.
2. Put it in the beat's row of `kScripts`.
3. Put a duration in `kDurations` that is a few seconds longer than the script.
4. Run `test_scene`. It fails if any seed overruns, and also if any of arrival,
   setup, firewood or fire is unseen in 40 seeds (extend that list for your beats).

`spineVariantCount` counts the non-null entries, so `spinePlan` picks up new
variants automatically.

**Adding a beat**

1. Add a `SpineBeatId`, a name in `kBeatNames`, a row in both tables, and a
   field in `LoopPlan` with its line in `spineVariant`.
2. `kBegin` is a template list, so extend it too.
3. `spineFormatPlan` prints the fields explicitly, so add the new one there.

**Replacing a placeholder:** replace `kDinnerHold` and its siblings, and change
the 8 s in `kDurations`. Delete `doBagHung` when the real food bag beat hangs
the bag.

**State at the start of your beats:** the fire is LIT. He sits at `camp.sit`
facing the fire. The tent is UP at `camp.tent`, the pack stands at its left, and
the wood pile is down. In firewood c the stick is down by the pile. The bag is
NONE until the foodbag placeholder runs.

**Fillers** need `n_fillers` and `filler[]` set in `spinePlan`, with beats
spliced into `spineBuildTimeline`, for example by replacing a pause with
`pause / filler / pause`. The final ambient beat absorbs the time.

**Actions** are `void fn(World&, float u)` functions. They must leave the
world at rest at u = 1. Ease every `fx` field back to 0, or the next
`hikerSetPose` starts its blend from the offset pose and jumps. `spine.u0` is a
scratch slot for a tween's start value.

**Things that are fragile**

- `hikerHandsMid` (in `props.cpp`) duplicates the rig's forward kinematics from
  `hiker.cpp` `build()`. If the rig's constants or grounding change, it drifts.
  `testHandsMid` catches that. A public `hikerHandPos` in `hiker.cpp` would be
  cleaner (open question below).
- Walks only move x. `feet.y` follows a straight segment from where the walk
  starts, so a walk that is mostly in y slides. Keep each segment's Δx at least
  as large as its Δy, and use `via` waypoints to route around the pit.
- `OP_VIA` finishes within 4 px of its x. The next walk must continue roughly in
  the same direction, or he turns on the spot.
- Do not walk at y > 416 with x ≥ 640. The quiet-corner test checks the rect.

## Open questions

1. **Done-when item 2 is not reachable as written.**
   `--frames 2000 --every 2` covers 4000 ticks (160 s). The spine with the brief's
   beat lengths and 8 to 20 s pauses reaches dinner at 210 to 240 s; seed 11
   reaches it at tick 5650. That command shows the build-up through setup and into
   firewood. `--frames 2000 --every 4` (320 s) shows everything through the
   placeholders. Should the pauses be shorter, or is the longer clip fine?
2. **May I add a `hikerHandPos` accessor to `hiker.cpp`?** It would replace the
   mirrored kinematics in `hikerHandsMid`.
3. **Rim light.** ACTOR_LIGHT is still barely different from ACTOR at golden,
   dusk and night, so `lit_from` hardly shows. That is still the palette question
   left open in stage 3.
4. **The tent is TENT orange in every mood.** At dusk the unlit tent may want a
   darker key. That is a palette decision.

## Pause state (second half of stage 4, 2026-10-30)

Stopped on request before the machine was powered off. The tree builds clean
and all 6 ctest suites pass. Nothing committed.

### Finished

- **Dinner** (3 variants, `spine.cpp` kDinnerA/B/C, 38/41/33 s). a: mug from
  where it lies or the pack (`AT_MUG`), sits, sips every 6 s via `fx.arm_*`,
  leaves the mug by the fire. b: pot carried (CARRY_MUG proxy, pot drawn over
  it), set on the fire (`Item::on_fire`, sorted in front of the flames), steam
  burst (`Steam`, 3..5 puffs, `plan.steam_n`), half-yelp, hooks it off with
  CARRY_STICK, eats from it. c: rummages, holds up `ITEM_FORK` at arm's length,
  shrugs (lift 2 px + forearms out), quick bites, sets it down. Stage 5 hook:
  `doit(hookDinnerCritters)` (empty) before the eating in each variant.
- **Foodbag** (3 variants, 38/48/47 s; longer than the brief's ~30 s because
  the pack, the bag pine and the tent door are ~600 px of walking). He stands
  left of the branch tip facing the tree. a: sack on the rope, REACH_UP, line
  eases up with easeOutBack. b: two wind-up swings per toss, `BAG_FLYING` with
  the rope to his hand, two short tosses fall back into his hand, the third
  goes over and hangs; damped 3-swing settle. c: strain (barely lifts,
  trembling lean), head scratch, crouch and heave with a 35% overshoot. All end
  at the tent door: hidden (`fx.on = false`), `tent.lit`, shadow, breathing
  (`Tent::shadow_y`, whole px, from `w.tick` in worldTick).
- **Bear** placeholder: 40 s, lit tent with shadow; `bearUpdate` burns
  `fire.fuel` (new LIT target) from 1 to 0.45 over the beat.
- **Close** (3 variants, 22/32/24 s): light out, `FIRE_EMBERS`, smoke stops
  (`Ambient::smoke_k`, eased on the sub-tick), fade over the last 2.5 s
  (`closeUpdate`, from the beat's t, so it is 1 on the last tick). b: emerges
  (`doEmerge`), prods the fire twice (fuel flares), looks up 6 s, back in. c:
  light flickers out/on twice then out. Stage 5 hook: `doit(hookTheft)`.
- **Fade**: `World::fade`, blendSpan of ACTOR over every strip row last in
  `worldDrawDynamic`; any change marks all strips. `worldBeginLoop` sets 1; the
  build beat clears it over 1.1 s (0 by tick 30). `appFade()` added.
- **Fillers** (6, `kFill*`, each starts by standing up wherever he is and ends
  sitting at `camp.sit` facing the fire): mug_sunset 47 s, read 50 s (new
  reading rock appended to `ls.rocks` in `spineLayout`, behind the fire on its
  right), cairn 20 + 7 s per stone (4..6, `OP_REPEAT` loop), skip_stone 36 s
  (`Skip` rings + stone), cloud_watch 58 s (LIE ~40 s, one shift), stream_listen
  46 s (`Leaf`, ACTOR, rests on top of the knee, slides off when he stands and
  stays on the ground). `spinePlan`: 2..4 fillers, weighted, no repeats
  (mug_sunset x3 at mood >= 3.5); slots after fire and dinner, a 3rd after
  dinner, with 4 the first after setup; each filler has its own pause.
- **Loop length policy**: no final ambient beat; the loop ends with the close.
  Pauses (8..20 s) are nudged round robin until the loop is 490..650 s.
  `spineLoopTicks`, `appLoopTicks()`. Measured 495..650 s over 20 seeds.
- **Interpreter**: `OP_GO` (auto route: behind the fire pit when crossing it,
  a sideways waypoint when a move is steeper than wide), `OP_REPEAT`
  (`repeatWhile(pred, back)`), `faceTo(anchor)`, new anchors, `SpineState::v0`
  and `target`, `ScriptRun::beat_ticks/done_at`. `-DSS_SPINE_TRACE` prints where
  each script ended in its beat (used to set the durations).
- **Rig**: `hikerHandPos(h, i)` added to hiker.h/.cpp (approved); `hikerHandsMid`
  in props.cpp now averages it (the mirrored kinematics are gone).
  `HikerFx::arm_w/arm_sh/arm_el` blend each arm to absolute angles.
- **Tests** (`test_scene`): rollover at the plan length; spine determinism at
  500..12000; quiet corner every 250 ticks over two loops x 3 seeds (fade
  samples skipped, <= 6); all 22 variants and 6 fillers in frame while visible
  (seeds to 60; found in 6); arrival..dinner and fillers end in frame,
  foodbag/close end in the tent; no overruns for 60 seeds (close counts an
  unfinished script when its fade starts); loop length 480..660 s for 20 seeds;
  fade 1 at the end, 0 by tick 30; whole-loop strips budget; props sheet has the
  fork, flying bag, steam, rings and leaf. The stage 2 corner check's tick-0
  sample moved to tick 30 (tick 0 is black by design).

### Numbers

`sizeof(App)` 60,232 bytes. Whole loop, render every tick, build excluded:
seed 11 avg 7.16 strips (pauses 6.84), 0.103 ms/frame; seed 7 7.38 (7.26),
0.107 ms; seed 42 7.26 (6.96), 0.756 ms (one noisy timing run; not
re-measured). Max 15 strips during the fades.

### Changed after viewing frames

1. The leaf landed inside the knee silhouette (ACTOR on ACTOR) and vanished:
   it now rests 3.5 px higher, tip up, so it shows on top of the knee.
2. Standing right of the bag-branch tip he hid the branch: he now stands left
   of the tip facing the tree, and the tosses were re-aimed.
3. Tosses barely rose above his hand: apexes raised (44..58 px above the tip),
   underarm sweep ends at 2.0 rad instead of 2.5.
4. Foodbag overran every seed (walking): long walks at 36 px/s, durations set
   from the trace (a 38, b 48, c 47).

### Stubbed / not done

- **`--loops N` in `ss_preview` is not started** (nor its README row). Plan:
  in `preview_main.cpp`, when `--loops N` is given, loop `appTick` with no
  render; on each tick that returns true print
  `loop=L seed=S length=Ts plan=...` using the tick count before rollover
  (or `appLoopTicks` read at loop start), stop after N; add the option to
  `previewParseArgs`/`previewUsage` and the README table.
- Not re-checked by eye: close a, the bear placeholder's burn-down, dinner b's
  steam puffs at full frame rate (only coarse sheets), the loop fade-in.
- Bear stays a placeholder for stage 5.

### Open items

1. The bag branch tip (y 294) is at the hiker's chest; the hung bag hangs at
   his hip. Reads as "hung from a branch" but too low for the bear beat. A
   higher branch is a landscape.cpp change (not made).
2. The food bag beat runs 38..48 s, not ~30 s (walking distances).
3. The shrug uses `fx.lift` (whole body up 2 px): the rig has no shoulder raise.
4. The page turn is ~0.3 s, not 2 frames (2 ticks would not show at --every 4).
5. Items left out (mug, pot, fork, book, leaf) stay for the rest of the loop,
   ready for the stage 5 theft hook.
