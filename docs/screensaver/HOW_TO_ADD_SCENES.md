# How to add or change scenes in the campsite screensaver

Written 2026-11-01 for Claude sessions (Opus, Sonnet) that will build new scenes. It describes Mark's scene
system as it is in the working tree, not as the specs say it should be. Where code and spec disagree this
guide follows the code and says so. Design intent stays in `CAMPSITE_SCREENSAVER_BRIEF.md`,
`STAGES_2_6_SPEC.md` and `SPINES_WEATHER_LOOKOUT_SPEC.md`.

**References.** `majors.cpp:3671` means that line on 2026-11-01. Bare names are in
`screensaver/core/scene/`. Other prefixes: `tests/`, `tools/`, `sim/` (all under `screensaver/`), `anim/`,
`render/` (under `core/`), `palette.h`, `ss_app.h`, `ss_platform.h` (in `core/`). `screensaver/` is untracked in
git, so there is no commit to pin the lines to, and other sessions are editing core files right now
(chip, react, reveal, house_weather, a memory fix). Lines drift by a few: search for the function name. `app.cpp` and `world.cpp` references were re-checked on
2026-11-02 after the chip, the doorbell reaction changes and the fast-block scratch landed (section 8.1).
Anything I did not run or read closely is marked **unverified**.

**Before you start.**
- Mike owns the look. The evening spine is "approved; do not change its scripts" (spec 1, `spine.cpp:2664`).
  Ask before changing its pool, scripts or pinned test lines.
- Never edit a test to make it pass until you know why it failed. Several tests are deliberate tripwires (section 10).
- Report numbers with every addition (section 8). Label anything not checked on the panel "unverified".
- Work in Git Bash. This PC has g++ 16.2.0, cmake 4.4.3 and ninja (WinLibs), no clang, no ffmpeg.

---

## 1. The mental model

### 1.1 Time: ticks, beats, the timeline

The simulation runs at a fixed 25 ticks a second (`ss_platform.h:24`). A loop is a `Timeline`
(`anim/scheduler.h:35`) of up to 64 `Beat`s (`SS_MAX_BEATS`, `scheduler.h:24`; `Timeline::add` returns false
when full and every caller ignores it). A beat is `{name, seconds, begin, update(t 0..1), end}`. It lasts
`ceil(seconds * 25)` whole ticks; `update` runs on each, `t` is exactly 0 on the first and exactly 1 on the last
(`scheduler.cpp:64-89`). The next beat's `begin` runs in the same tick as the previous `end`. Beat hooks drive
the World through `SceneCtx` (`world.h:219`). They never draw.

One tick, in order (`app.cpp:177-225`, `world.cpp:426-543`):
1. Events: a doorbell starts the status chip's pulse and, if he can, the reaction (`app.cpp:182-186`); the chip steps and marks its rows (187-190).
2. Palette follows the inputs and weather if they moved enough (`computeAppPalette`, `app.cpp:99`; the check at 192-202): a change rebuilds the static cache.
3. If the doorbell reaction is active only `worldTick` runs (the story freezes) and the tick ends (`app.cpp:206-209`).
4. `Timeline::tick`: the current beat's `update` (scripts run here). When the last beat ends: `memoryEndLoop`, new loop seed from the loop RNG, `beginLoop` (`app.cpp:213-220`).
5. `minorsSync`, then `worldTick`: clouds, ambient, `stepHiker` (rig plus `HikerFx`), `spineCritterScenes` (family critters, `spine.cpp:2895`), `minorsStep`, `directorStep`, bits, weather, `sortActors`, dirty marks.

Then the host calls `appRender` (`app.cpp:227-234`): it lends the rasterizer its scratch for the duration, redraws only the dirty strips, and the status chip is drawn last, over everything (`app.cpp:65-69`). Section 8.1 is about what runs over, or freezes, your scene.

### 1.2 The loop plan, families, fillers, majors, minors

Loop start (`beginLoop`, `app.cpp:118-133`): seed the loop RNG from the loop seed; `worldBeginLoop` (clears the
World except `Memory`, generates geometry, `spineLayout`); pick the family; `spinePlan` (every random pick of
the loop goes into `LoopPlan`, `spine.h:67-110`); `memoryBeginLoop`; `spineBeginLoop` (family setup); `weatherPlan`
(own RNG); `spineBuildTimeline`.

- **Family** (`SpineFamily`, `spine.h:45`): evening (default), morning, midday, lookout. Picked from the inputs
  alone by `spineFamilyFor` (`spine.cpp:2613`), no RNG. The lookout is a separate world (own landscape and tick
  path, `world.cpp:375`); this guide is about the three camp families.
- **Spine beats**: evening arrival, setup, firewood, fire, dinner, foodbag, bear, close (`SpineBeatId`, `spine.h:52`);
  morning wake, coffee, bag_down, break_camp, douse, leave (`spine_family.h:39`); midday return, lunch, wade, nap,
  map, afternoon (`spine_family.h:58`). Each has 2 or 3 variants; each variant is one `Step` table.
- **Timeline shape** (`spineBuildTimeline`, `spine.cpp:2919-2940`; the morning and midday copies are the same):
  `build (13 s)`, then for each spine beat: the beat, a `pause` (8 to 20 s, only ambient moves), each filler planned
  after it (filler, own pause), each major planned after it (`still` 4 to 8 s, major, `still` 4 to 8 s). The last
  beat has no pause and ends with a 2.5 s fade to black. Loop length is the sum, fitted to 480 to 660 s
  (aim 490 to 650) by `spineFitLength` (`spine.cpp:2732`): squeeze pauses, drop fillers and majors, shorten stills,
  then stretch pauses.
- **Fillers**: calm solo beats (30 to 60 s) in the pauses (`FillerId`, `spine.h:58`). A bit mask says which family may draw each (`kFillerFamilies`, `spine.cpp:2214`).
- **Majors**: 2 to 4 surprise set pieces a loop (`majors.h`, `majors.cpp`), one per slot, drawn by `majorsPlan` (`majors.cpp:3826`) from a weighted pool.
- **Minors**: 10 to 30 s small effects (a leaf, a chipmunk crossing) run by their own scheduler beside the story (`minors.h`, `minors.cpp`).
- **Memory** (`memory.h:43`): what survives between loops (cairn, shoe, mouse hole, rod, ring of recent majors, last family and variants, brave/shy split).

### 1.3 The step interpreter

A script is a `const Step[]` (`script.h:54`). `scriptStart` (`spine.cpp:2565`) loads it into `w.spine.run` (one
script at a time: a beat, a filler or a major). The beat's update calls `scriptRun` every tick (`spine.cpp:2444`,
`majors.cpp:3755`). Instant steps chain within one tick (cap 16, `spine.cpp:2381`); waiting steps hold the script.
A step is entered once (`enterStep`, `spine.cpp:2321`) then polled (`stepDone`, `spine.cpp:2347`).

| Constructor (`script.h:72-98`) | Op | What it does |
|---|---|---|
| `place(AT, face)` | OP_PLACE | `hikerInit` at the anchor: height 90, pack on, hat on, empty hands, fx on |
| `walk(AT, v, dx, dy)` | OP_WALK | walk at v px/s (x moves; feet.y slides on the path `walkTo` sets, `spine.cpp:2273`), wait for arrival |
| `via(...)` | OP_VIA | as walk, done within 4 px so the next walk keeps its stride |
| `go(...)` | OP_GO | walk by a route round the fire pit; two legs when the move is steeper than wide (`routeVia`, `spine.cpp:2289`) |
| `pose(P, blend, hold)` | OP_POSE | ease into P over `blend` s, hold until `blend + hold` from step start |
| `still(s)`, `waitAway()` | OP_WAIT | nothing for s seconds (waitAway: `plan.away_s`) |
| `face(LEFT/RIGHT/TO_FIRE)`, `faceTo(AT)` | OP_FACE | instant |
| `carry(CarryId)` | OP_CARRY | instant |
| `act(fn, s)` | OP_ACT | `fn(w, u)` every tick for s seconds; u 0..1, exactly 1 on the last tick |
| `doit(fn)` | OP_DO | `fn(w, 1)` once |
| `until(pred)` | OP_UNTIL | wait until `pred(w)`; **no timeout** |
| `skipUnless(pred, n)` | OP_SKIP | if pred is false skip the next n steps |
| `repeatWhile(pred, back)` | OP_REPEAT | if pred is true jump back `back` steps |

Speeds: `kStroll 34`, `kWade 22`, `kRun 78` px/s (`script.h:70`).

- **Anchors** (`Anchor`, `script.h:37-49`) name a point; `Step.x/y` is an offset. `anchorPos` (`spine.cpp:2226`) resolves
  them from `CampLayout` (`spine.h:124`, filled by `spineLayout`, `spine.cpp:2813`, all relative to the fire pit `F`),
  `w.fire.pos`, the cairn, `AT_M0..M3` (the running major's own `MajorState::p[]`) and `AT_FAM0/1`
  (`SpineState::fp[]`, a family beat's own points). An unknown anchor falls to `default`: his current feet (`spine.cpp:2269`). No error.
- **ActFn** `void(World&, float u)` sets everything it moves as a function of u (absolute values, never accumulated)
  and on `u >= 1.f` restores what it touched, normally `scriptFxRest(w)` (`spine.cpp:341`). **PredFn** `bool(const World&)`.
- Helpers scripts share (`script.h:113-126`): `scriptSetPose`, `scriptFxRest`, `scriptSetArm`, `scriptEnvelope`, `scriptGroundAt`, `scriptSetDown`.
- Majors split work between the script and a per-tick function, talking through `MajorState::cue` bits: `doit(cueGo)` sets a low bit, the tick sets `kE1..kE3`, `until(ev1)` waits for them (`majors.cpp:61-75`).

### 1.4 The World, dirty strips, static cache versus dynamic drawing

`World` (`world.h:165-217`) is everything dynamic, by value: landscape, clouds, ambient, fire, hiker and `HikerFx`,
props (tent, pack, bag, cairn, `Item[ITEM_COUNT]`, steam, skip, leaf), 8 critter slots, 6 bits, memory,
`busy`, `MajorState`, `Minors`, `Weather`, `LoopPlan`, `SpineState`, the actor list and `WorldDirty`.
`worldBeginLoop` zeroes it every loop (`world.cpp:265`) and keeps only `Memory`.
`busy` means a beat owns critters (dinner, bear, close, a major): the minors wait.

**Static cache.** The sky, sun/moon, stars, ridges, far trees, mid pines, ground, stream body, rocks, camp pine,
foreground pines and non-swaying tufts are drawn once into a full-frame cache and redrawn only on a palette change or a
build-phase boundary (`worldDrawStatic`, `world.cpp:563`; `Renderer::render`, `render/renderer.cpp:47`). Anything
that never changes during the loop belongs here (the reading rock is added to `Landscape::rocks` at loop start,
`spine.cpp:2859-2873`): it costs no strips.

**Dynamic.** Everything that moves is drawn per dirty strip in one place, `worldDrawDynamic` (`world.cpp:573-660`):
heat shimmer, sky, clouds and the distant bird, build-up layers, stream glints and the skipped stone's rings, fog
bands, then **ground actors sorted far to near by base y** (fire, tufts, hiker, tent, pack, bag, cairn, items, leaf,
critters, bits, weather bits, major glyphs, foreground pines redrawn over actors behind them; `sortActors`,
`world.cpp:61`), then smoke and steam, weather in front, then the loop fade over everything.

**Dirty strips.** The frame is 15 strips of 32 rows (`ss_platform.h:30-32`). `Renderer::markDirtyRect` uses only the
rect's rows (`renderer.h:58`, `renderer.cpp:35`): x is ignored. A dirty strip is a 51 KB copy from the cache plus every
dynamic draw clipped to it, so cost is about the number of dirty strips. Rules:
1. Every dynamic element has bounds computed from the same geometry its draw uses (`props.h:4-5`; `hikerBounds` and
   `critterBounds` are folded from the primitive list they draw), plus the antialiasing fringe.
2. `worldTick` remembers last tick's state and bounds per element and marks the old and the new bounds when the state
   changed or the bounds moved (`track`, `world.cpp:26-31`; calls at `world.cpp:496-542`). An element that did not change marks
   nothing, so each `xSame` function must compare **every field the draw reads** or the old pixels stay on screen.
3. The hiker and in-frame critters are marked every tick (they breathe). Major glyphs are tracked automatically
   (`majorsTrack`, `majors.cpp:4008`).
4. An element that disappears must report empty bounds once so its last position is marked.
5. A palette change or `invalidateCache()` marks every strip and redraws the whole cache. Budgeted, not free (section 8).
6. A draw must be a pure function of state, palette and clip: the same pixels whether the strip is drawn alone or as part of the frame. The tests render incrementally and in full and compare.

### 1.5 Determinism rules

The goal: (seed, inputs, events) gives the same frames on the PC and on the panel. Tests compare two runs and
incremental against full renders (`test_scene.cpp:113`, `test_spines.cpp:356`).

1. **The loop RNG (`App::rng`) is drawn only at loop start**: landscape, clouds, fire, critter dials, ambient (`worldBeginLoop`), `spinePlan`, `memoryBeginLoop` (exactly one call, used or not, `memory.cpp:13`), Lookout setup. The loop's last act is `loop_seed = rng.next()` (`app.cpp:217`), so one extra or missing draw in a loop changes the seed of every later loop. The only draws during a loop are the distant bird's, timed by tick and independent of the story (`ambient.cpp:292-298`).
2. **No RNG inside a beat, hook, script action or tick.** Variation comes from the plan (a byte chosen at plan time), from the layout, or from `hash01(a, b)` (`majors.cpp:51`).
3. **Random systems have their own stream, never the loop RNG**: minors (`Minors::rng`, seeded from a copy of the loop RNG, `minors.cpp:1298-1305`), weather (`weather.cpp:500`, from the loop seed), the Lookout's sun and stars (`lookout.cpp:1137`). Do the same for anything new.
4. **One RNG call per statement.** Argument evaluation order differs between compilers and the board must replay the PC.
5. **A plan draws a fixed number of values whatever it decides.** `majorsPlan` draws `id, r_slot, r_var` every round even when it skips (`majors.cpp:3888-3891`). Make conditional draws unconditional.
6. **Floats.** Single precision, `-ffp-contract=off` (`CMakeLists.txt:20`), table sines `sinT/cosT` (`anim/ease.h:25`). `expf`, `atan2f`, `powf`, `sinf` still appear (e.g. `majors.cpp:917, 1111, 2414`, `react.cpp:18`); their last bit can differ between libms. Prefer the table functions and round anything that becomes a pixel position. Bit-for-bit agreement with the panel is **unverified**.
7. **No clocks, no mutable statics, no uninitialized reads.** Do not time story things with `w.tick`: it keeps counting during the doorbell reaction while beats and scripts stand still (`app.cpp:204-209`, `world.cpp:434`). Use the beat's clock (`MajorState::t`, `SpineState::ft`, script steps).

Breaks byte-identical replays: an RNG draw in a beat; two draws in one expression; a draw that happens only sometimes; a `static` that keeps state; anything read from the clock or from `w.tick` in story logic; a dynamic draw that depends on which strip it is in.

---

## 2. Recipe: add a surprise major

A major = a hiker script + a per-tick function + up to 3 critters + a few glyphs, wrapped in two `still` beats.
It starts wherever he was left and must leave him in frame, at rest, hat on, hands empty, with every critter gone.

**Decide first:** which families (`kMajors[].families`, bit per `SpineFamily`); which slots (bits 1, 2, 4: after setup,
fire, dinner in the evening; wake, coffee, bag_down in the morning; return, lunch, wade at midday; bit 8 is the
evening's in-the-tent slot); day or night; how many variants (the `var` byte: 0 and 1 pick between two tables
through `scriptOf`; other values are free-form like the fireflies' count); seconds; critters; glyphs; any memory.

**Worked example: `deer_stick`** (id 1: a deer steps on a stick, it snaps, he laughs, the deer leaves, head high).

1. **The id.** `MajorId` in `majors.h:36-44`: add before `MJ_COUNT`; never reorder (ids are stored in `Memory::ring` and the sets, and tests index by number). deer_stick is the second entry (`majors.h:37`). `static_assert`s at `majors.h:48` and `memory.h:40` fail past 32 ids.
2. **The code**, in the anonymous namespace before "the pool" (`majors.cpp:3637`):
   - `beginDeer` (`majors.cpp:753-764`): nothing random. Sets `m.p[0]` (where the stick lies, from `camp.sit` and `scriptGroundAt`) and makes one glyph, `SH_TWIG` in the `ROCK` slot (a pale stick: `TRUNK` vanishes on dark ground, line 760).
   - `tickDeer` (`majors.cpp:766-815`): waits for `kGo`; spawns the deer once (`spawnC`, flags `CF_BRAVE`, walk to the stick); a stage machine on `m.stage[0]` with seconds in stage `m.st[0]` (case 0: on arrival snap the twig glyph (`g.k = 1`), take a dust bit with `worldTakeBit(w, BIT_DUST)`, startle, `fire(m, kE1)`; later cases look at him, head up, exit with `exitC`).
   - `kDeer` (`majors.cpp:817-820`): `SIT_HERE` (macro, `majors.cpp:739`: skipped if already seated, else walk to `camp.sit`, sit), `still(0.4)`, `doit(cueGo)`, `until(ev1)`, `still(0.5)`, `face(RIGHT)`, `still(0.6)`, `act(actLaugh, 2.8)` (`majors.cpp:551-560`), `until(gone)`, `still`, `face(TO_FIRE)`, `still`.
3. **The pool row** (`majors.cpp:3669-3697`, same index as the enum): `{"deer_stick", {42, 42}, 1.f, kOut, kDay, beginDeer, tickDeer, {kDeer, kDeer}, {N(kDeer), N(kDeer)}}` (line 3671). Fields: name, `seconds[2]` per variant, weight, slot bits, family bits, begin, tick, script tables, step counts. `kOut = 1|2|4`, `kTent = 8`, `kDay` = evening|morning|midday (`majors.cpp:3645-3655`). Then bump `static_assert(MJ_COUNT == 17)` (`majors.cpp:3698`). `N()` is `#define`d at 3639 and `#undef`ed at 3715: use it only inside the table.
4. **Gating and weights** in `majorsPlan` (`majors.cpp:3826-3945`): base weight from the family mask (3834); night and dusk gates (3835-3841: stargaze needs real night, firefly_jar dusk); per-id slot limits (3868-3877) **and again** for the chosen id (3895-3898): keep both copies in step; the variant byte per id (3911-3917). Deer_stick needs none. If you give a major a condition, put it here, not in the tick.
5. **Plan and memory.** Automatic: picks go into `LoopPlan::major/major_after/major_var/major_pre_s/major_post_s` and `spineFitLength` pushes them into `Memory::ring` (8) and the recent sets (9 loops) (`spine.cpp:2782-2788`). The repeat rule: a pick in the ring has its weight scaled by `0.5 * k * k` (`majors.cpp:3851-3856`); last loop's majors weigh `1e-4`; a set played in the last 9 loops weighs `1e-4` (3884); the third and fourth picks are optional and never take a penalised major (3894). Only touch `Memory` (`memory.h:43-64`, `memory.cpp:10-35`, `memoryFormat` at 85) if the major changes what the next loop looks like (raccoon_pack: `shoe_taken`; fishing: `rod_lost`).
6. **Begin and end hooks.** The per-id `begin` runs from `majorBegin<I>` (`majors.cpp:3724-3748`), which first clears glyphs (except `keep` ones), zeroes the cues and clocks, sets `m.id`, `m.var`, points `m.p[]` at his feet and sets `w.busy`. `majorUpdate` (3750) runs `scriptRun` then your tick each tick. There is **no per-major end hook**: `majorEnd` (3759-3795) counts an overrun if the script is not done or a `CT_MAJOR` critter is left, releases leftovers, turns resting cone glyphs into real bits (3776-3786, the model for "what a major leaves behind"), and resets `tent.wobble`, `fx.stomp`, `fx.wade`, `hat_on`, the rod carry, `busy`. Undo anything else in the script's last steps, or add it to `majorEnd` if it must be undone even on an overrun.
7. **Critter ownership.** `spawnC` (`majors.cpp:405-416`) takes a slot from the director, tags it `CT_MAJOR`, sets flags (`CF_BRAVE` so his laugh does not scatter it) and the loop's persona. Up to 3 in `m.c[]`. `own()` (417) is false once the director scattered it. `gone()` (78) is what `until(gone)` waits on. `exitC`/`walkC`/`stageTo` are the helpers (419-432). `w.busy` is set by `majorBegin` and cleared by `majorEnd`; the minors treat the beat name as "owns critters" because it is not in their calm list (`minors.cpp:1331`). Every critter must be out of the scene when the beat ends (section 4).
8. **Glyphs**, if you need a drawn thing that is not a critter, prop or bit. `glyphNew(w, GL_SHAPE, pos, base_y)` (`majors.cpp:86`); at most `GLYPH_MAX 28` shared with every major. To add a shape: `ShapeId` before `SH_COUNT` (`majors.h:63-77`), `shapeReach` (`majors.cpp:127`; the default is 12 px), `drawShape` case (`majors.cpp:246`; the default draws **nothing**, bounds and draw can disagree silently). `glyphBounds` (3947) uses the reach times scale. Dirty marking is automatic (`majorsTrack`, 4008) as long as the draw reads only Glyph fields. Palette slots only. Mark `keep` only for things that must outlive the major (the counted stars).
9. **The name.** `kMajors[].name` is the plan token (`majors=...` in `appLoopPlan`; written by `spineFormatPlan` `spine.cpp:2970-2975` and the morning and midday copies), the beat name `appBeatName` returns, and the key review tools use. Lowercase and underscores, no spaces, commas or `=`. Tests find names with `strstr`: do not pick a name that contains, or ends with, another major's name.
10. **review_clips.** Nothing to edit for a plain major: `tools/review_clips.py` reads `majors=` from the plan line and cuts every major it meets (`beat_kind`, line 364; the loop at 903-924). For a night-only or dusk-only major run it a second time with `--night`. A weather-gated one needs its own cut like `major_rain_shelter` (`review_clips.py:744-760`).
11. **Tests.** (a) `tests/test_scene.cpp:982-986`: `N_MAJORS` and `kMajorNames` (16 names today for 17 ids: rain_shelter is deliberately absent). Add your name and bump `N_MAJORS` if it can play in the evening: `testVariety` (1024) then requires it to fire at least once in 30 night loops from seed 1 (check 1136), and checks `busy` matches the beat name, the 4 to 8 s stills, no overrun, no critter left. (b) `isMajor` lists in `tests/test_spines.cpp:187-195` and `tests/test_midday.cpp:211-219`: add it, or the hiker-in-frame checks treat it as an ordinary beat. (c) A dedicated test modelled on `testStreamVignettes` (`test_scene.cpp:1349`): seeds until it plays, render every tick, hiker in frame and out of the corner, end state, no overruns, strips under 10, determinism at 3 ticks inside it. New `tests/test_*.cpp` files are picked up by CMake (`CMakeLists.txt:25`, re-run cmake if not). (d) Pinned lines change: a new pool entry changes the plan draws of every family it is eligible in, so the `--loops` pins in `test_spines.cpp:77-100` (evening) and `test_midday.cpp:79-131` (evening, morning, midday) will differ. See gotcha 4 and re-capture deliberately.
12. **Check.** `ss_preview --seed S --mood M [--night] --loops 1` for S in 1..60 and grep `majors=.*yourname` to find a seed (loop 0 has fresh memory, so `--seed S` replays exactly that plan). Then section 9.

---

## 3. Recipe: add a new prop

**Pick the right kind first.**
- Never changes in the loop (camp furniture): put it in the **Landscape** so it is in the cache and costs no strips (the reading rock, `spine.cpp:2859-2873`).
- Small thing he can pick up, put down or a critter can carry: an **`ItemId`** (`props.h:129`, `Item` at 144).
- A short-lived particle (leaf, dust, drop, ring, bee): a **`Bit`** (`props.h:227-246`), always taken with `worldTakeBit` (`world.cpp:239`).
- Something only one major needs: a **glyph** (section 2, step 8).
- A camp structure with its own state (tent, bag, cairn, pack): a new prop struct, below.

**Steps for a new prop struct** (template: `Leaf`, `props.h:203`, `props.cpp:731-748`; or `Steam`, `props.h:177`).
1. **State** in `props.h`: plain data, no pointers, no randomness, no clock of its own. Add `xBounds`, `xDraw`, `xSame` in `props.cpp`. Bounds come from the same geometry the draw uses, padded 1 to 2 px for the fringe, and are empty when it is not shown. `xSame` compares every field the draw reads.
2. **World** (`world.h:165`): a member by value. **WorldDirty** (`world.h:134-163`): a `Rect x;` and a copy `X x_s;`.
3. **Loop start** (`worldBeginLoop`, `world.cpp:262-367`): the World is zeroed, so only set non-zero defaults; add `d.x = kEmpty; d.x_s = w.x;` beside the others (341-360). Homes come from `spineLayout` (`spine.cpp:2813`) or the family layout.
4. **Actor sort.** If it should sort among ground actors: add to `ActorKind` (`world.h:52`), `L.add(ACT_X, idx, baseY)` in `sortActors` (`world.cpp:61-123`; base y is where it touches the ground; things in his hands use `held_y = feet.y + 1` to draw in front of him, lines 71 and 88), a case in the draw switch (`world.cpp:610-645`). Raise `WORLD_ACTOR_MAX` (`world.h:67`) by your maximum count: it is **exactly** the worst-case sum today (95) and `ActorList::add` silently drops what comes after it is full (glyphs and weather bits go first). Fixed-layer things (smoke, steam) are drawn at a fixed point in `worldDrawDynamic` instead.
5. **Dirty tracking** in `worldTick` (`world.cpp:517-543`): `track(r, d.x, xBounds(w.x), !xSame(w.x, d.x_s)); d.x_s = w.x;` every tick. The Lookout tick tracks only its own things (`world.cpp:375-422`), so a camp prop must not exist in a Lookout loop.
6. **Palette slots.** All 32 are taken (`palette.h:17-35`, `static_assert(SLOT_COUNT <= 32)`; the fog bands already borrow `SKY4`). Reuse: `ACTOR` for things he owns, `TRUNK` wood, `ROCK` stones and anything pale on dark ground, `ACTOR_LIGHT` small highlights only, `WATER_LIGHT`, `FIRE`/`FIRE_CORE`, `SMOKE`, `TENT_LIT`. Adding a slot means widening `Palette::c`, six key tables and their `static_assert`s (`palette.cpp:154-159`): ask first.
7. **Carrying it in his hands.** Two ways.
   - `CarryId` (`hiker.h:45-49`): add before `CARRY_COUNT`, add a `case` in `buildCarry` (`hiker.cpp:254-303`; anchored at the near hand, sizes in units of height `H` so it scales on the 44 px Lookout hiker). Its primitives join the rig's: the body is 16 of `kMaxPrims 24` and the test budget is 22 (`tests/test_hiker.cpp:31`), so a carry gets 6 at most (`CARRY_WOOD` uses 3). `hikerBounds` includes it automatically. With hands full, `hikerWalkTo` keeps the carrying pose instead of swinging the arms (`hiker.cpp:545-547`). Script: `carry(CARRY_X)`.
   - `Item.held` (the prop also lies on the ground, or a critter carries it): World puts it at his hands each tick (`world.cpp:197-207`), sorts it at `held_y`, hides its bounds while he is out of frame (`heldBounds`, `world.cpp:227`). New `Item` fields go at the **end** with a default initializer (like `alt`), and into `itemSame` (`props.cpp:599`); `Item` is built positionally at `world.cpp:312` and `test_scene.cpp:827`. A new `ItemId` also needs a case in `itemBounds` (`props.cpp:443`; the default is empty, so it would draw nothing and mark nothing), `itemDraw` (515) and `itemName` (606; a short initializer leaves a null name).
8. **Tests.** Add the new states and both facings to `testProps` (`test_scene.cpp:772`): it draws each prop on a blank surface and fails if a pixel lies outside the bounds (`propsOutside`, 764). Add the incremental-versus-full check through a scene that uses it. Check the quiet corner.

---

## 4. Recipe: add a new critter kind

A critter is a rig of at most 12 primitives (`kMaxPrims`, `critters.cpp:557`; budget 7 per kind, bear 9) drawn in `ACTOR` (`ACTOR_LIGHT` for small marks).

1. **Enum.** `CritterKind` before `CR_COUNT` (`critters.h:37`); never reorder (tests and tables index by number, e.g. `kind_seen[3]`, `k == 4` for the raccoon at `test_scene.cpp:513`).
2. **Kind table** `kKinds` (`critters.cpp:58-89`): name, nominal size, idle height (`testSizes` compares it with the drawing), the `CritterAction` mask it supports (an unsupported action silently becomes `CA_IDLE`, `critterSetAction`, `critters.cpp:1182`), default gait, primitive budget, strides, hop height.
3. **Per-kind arrays** sized by `CR_COUNT` that a short initializer leaves zeroed: `kRearAng` (`critters.cpp:96`), `kReach` in `director.cpp:22-32` (half length, height, flee speed and gait: used by `directorClamp` to keep the whole figure in frame and out of the corner).
4. **Poses**: `idleOf` (`critters.cpp:104`) and per-action overrides in `poseFor` (129-230), built at compile time into `kPoses`.
5. **The rig.** A small quadruped: a `QuadDims` with body and head polygons, a case in `quadDims` (691; the default is the chipmunk's) and a case in `buildQuad`'s tail switch (716). Anything else: a `buildX` and a case in `build` (1061-1067). Gait amplitudes per kind: `addGait` (417-425, four-legged walks 383-413), loop motion `addLoop` (456).
6. **Memory.** `Memory::brave` is a 16-bit mask of kinds (`CR_COUNT <= 16` asserted, `memory.h:41`); `memoryBeginLoop` masks the roll with `(1 << CR_COUNT) - 1` (`memory.cpp:18`). A new kind adds a bit, so **every pinned `memory=... brave=...` string changes in the loops where that bit is set** (gotcha 6).
7. **Tests.** `test_critters.cpp` loops over `CR_COUNT` for bounds, mirror, blend landing, moves, budget (`testBoundsAndBudget`, line 140). Hard-coded: `nominal[CR_COUNT]` (`test_critters.cpp:517`), `itemFor` (66), `kind_seen[9]` (`test_scene.cpp:448, 457`).

**How a beat, major or minor takes a critter** (`director.h`):
- `directorSpawn(w, kind, feet, facing)` returns a slot or **-1** when all 8 are taken: check it. Then tag it (`CT_DINNER`, `CT_BEAR`, `CT_THIEF`, `CT_MINOR`, `CT_MAJOR`, `world.h:73`), set flags (`CF_BRAVE` never scatters; `CF_SHY` scatters at any move of his), apply `memoryApplyPersona`.
- Drive it: `directorWalk`, `critterSetAction`, `directorExit(side)`; `critterClimbTo`, `critterMouth/Paw/Head` for anchors; `CritterSlot::sort_y` to perch it in the actor sort (the bird on the tent ridge); `Item.critter = slot + 1` to make it carry an item (`director.cpp:248-258` keeps it at the mouth or paw).
- `directorOwns(w, slot, tag)` (`director.cpp:200`) is false once he scattered it (`directorAvoidHiker`, 133: non-brave critters within ~80 px of a walking hiker flee). Check it before giving orders.
- Release: leaving through `directorExit` frees the slot once out of sight; `directorRelease` frees now.
- Who owns what: evening beats use `SpineState::deer/chip/bear/thief`; morning and midday keep one slot in `SpineState::fcrit` with stage `fstage` driven from `morningTick`/`middayTick` (`spine_morning.cpp:58-202`); majors use `spawnC` and `MajorState::c[3]`; minors use `spawn()` and `Minors::slot[2]` (`minors.cpp:116`).
- At a beat's end no owned critter may remain: majors count an overrun and release (`majors.cpp:3769-3774`), the morning sends it off by the nearer edge (`spine_morning.cpp:1342-1346`), `beatEndOwned` does the same for dinner, bear, close (`spine.cpp:2507`). Tests read `appCritterCount == 0` on the loop's last tick.

---

## 5. Recipe: a new hiker pose, an arm overlay, or a `HikerFx`

**Choosing.** A pose (`PoseId`) is something he holds for more than a second. A transient move inside a script is an `act` that sets `HikerFx`. Something that must run on top of whatever he is doing, in pauses only, is a pose overlay in the weather's style.

**A new pose** (`hiker.h:28-43`, `hiker.cpp`):
1. `PoseId` before `POSE_COUNT`.
2. A row in `kPoses` (`hiker.cpp:69-90`): **positional**, in enum order, with a comment per row: lean, head, lift, dx, thigh[2], knee[2], shoulder[2], elbow[2]. Angles are radians, absolute from straight down, + toward the facing; knees bend backward; elbows bend forward (`hiker.h:8-12`). Reference values: shoulder 3.0 is straight up (`POSE_REACH_UP`), about 1.6 level (`POSE_LOOK_AROUND`), 0.1 hanging.
3. A name in `kPoseNames` (92).
4. Optional: `blendKindFor` (96: action poses wind up, landing poses overshoot), `groundPose` (130: hips or knees on the ground, which changes the pose a walk settles into), `idleRate`/`addIdle` (135/144: a small loop on a held pose).
5. He must know the pose is "sitting-like": `hikerSitting` and `hikerIdle` (`minors.cpp:86, 93`) test SIT/READ (and STAND); `notSeated` (`majors.cpp:535`) tests `POSE_SIT`; `directorAvoidHiker` (`director.cpp:137`) treats any blend into a pose other than SIT, LIE, READ, HOLD_MUG as "stirring" and scatters animals. A new sit-like pose needs adding to those lists or animals bolt when he settles.
6. `tests/test_hiker.cpp` loops over `POSE_COUNT` (bounds, mirror, exact blend landing, walk); `sheetCarry` has a positional `kUse[CARRY_COUNT]` (line 410).

**`HikerFx`** (`world.h:102-121`) is added to the composed pose every tick after `hikerTick` (`stepHiker`, `world.cpp:149-191`): `lean`, `head`, `lift` add to the pose; `arm_w[i]/arm_sh[i]/arm_el[i]` blend arm i toward absolute angles; `reach`/`reach_to` swing the near arm straight out; `stomp`, `armload`, `wade`.
- Set them **every tick of the act, as functions of u**, and rest them on the last tick with `scriptFxRest` (`spine.cpp:341`). Arms: `scriptSetArm(w, i, k, sh, el)` (i 0 is the near arm, k the blend weight).
- Change pose in scripts through the interpreter's `setPose` (`spine.cpp:40`) or `scriptSetPose`, not `hikerSetPose`: the blend starts from the composed pose, which already contains the fx, so the helper strips `lean/head/lift` first or they count twice.
- A new field: add it to `HikerFx`, apply it in `stepHiker` (additive ones also in `setPose`), rest it in `fxRest`. The doorbell reaction saves the whole struct at the bell and puts it back every tick (`react.cpp:31, 42`), so a new field survives it for free.
- Overlays of the weather kind (hug, hat fan, shiver; `weatherApplyPose`, `weather.cpp:766`) run only while no script holds him (a pause with time left, standing or sitting, no major or minor running) and leave him exactly as found (`weather.h:41-45`). Copy that gating.
- The minors take `fx.head` and `arm_w[0]` only when he is idle in a pause and restore them (`minors.cpp:1226-1231`).

---

## 6. Recipe: a spine variant, a filler, or a whole new family

### 6.1 A variant of an existing beat

Variants are capped at **3** per beat (`wt[3]` in the plans; `kScripts[...][3]`; `LETTERS = "abcd"` in review_clips). A beat with a `NO_SCRIPT` slot has room: morning `bag_down` and `douse` (`spine_morning.cpp:1267, 1269`); midday `lunch`, `map`, `afternoon` (`spine_midday.cpp:1691, 1694, 1695`). Fill the slots in order: the variant count is "scripts up to the first null" (`morningVariantCount`, `spine_morning.cpp:1481`).
1. Write the `Step` table beside its siblings (macros like `FROM_REST`, `TO_SIT`, `PACK_CROUCH` give the shared start and end; `spine_morning.cpp:965-970`). End him at rest, in frame, the way the shared end leaves him; start from where the shared start expects.
2. `kScripts` (`spine_morning.cpp:1264`, `spine_midday.cpp:1689`) and the seconds in `kDurations` (1278 / 1703). Whole seconds; script plus slack for slower walks on some seeds. Tune with `-DSS_SPINE_TRACE` (below).
3. Tests: `kMVariants` (`test_spines.cpp:179`), `kDVariants` (`test_midday.cpp:203`); the variant tests then require every variant to run in 40 seeds (`test_spines.cpp:333-339`). `tools/review_clips.py` `FAMILY_BEATS` (line 72) must list the new count.
4. Pins: the morning and midday plans weight a variant the last loop played at a quarter (`spine_morning.cpp:1491-1502`), so the pinned lines (`test_midday.cpp:108-131`) change. Re-capture.
5. Do **not** add a fourth random variant to the evening: `spineVariantCount` stops at 3 (`spine.cpp:2579`; dinner's fourth is placed by the plan, never drawn) and changing the draw range moves every evening plan.

**Tuning durations.** Build a separate tree with `-DSS_SPINE_TRACE` (`cmake -S . -B build-trace -G Ninja -DCMAKE_CXX_FLAGS=-DSS_SPINE_TRACE`; unverified here). Each beat then prints `trace ... step=i/n ticks=T done_at=D frac=F` at its end (`spine.cpp:2492-2497`, `majors.cpp:3762-3767`, morning `1333`, midday `1832`). `done_at` must be before the beat's last tick with a little slack. `SS_MORNING_TUNE` / `SS_MIDDAY_TUNE` make every beat 120 s so a script can be measured whole (`spine_morning.cpp:1289`, `spine_midday.cpp:1722`).

### 6.2 A filler

`FillerId` before `FILL_COUNT` (`spine.h:58`); then in `spine.cpp`: `kFillerSeconds` (2202; cairn and birds are special-cased in `spineFillerSeconds`, 2591), `kFillerNames` (2207), `kFillerScripts` (2163; `NO_SCRIPT` plus your own hooks like `birdsFillerBegin`), `kFillerBegin` (2529) and the `static_assert` (2531), `kFillerFamilies` (2214: one bit per family). The evening's weights are an array initializer `{1,1,1.2,1,1,1,1}` (2684): **a short initializer gives the new filler weight 0 and it never plays**. If minors may run during it, add its name to the calm list (`minors.cpp:1332`) and the test copy (`test_scene.cpp:902`). Tests: `kFillers[7]` in `test_scene.cpp` (421, 481, 493, 570). Names are matched with `strstr`: avoid `read`-like substrings.

### 6.3 A new camp family

Morning and midday were added by keeping the evening untouched and sharing the interpreter, the anchors, the fillers, the majors and the length policy (spec 1; status: `05ace3b`, `9fef80d`). A new family follows `spine_morning.cpp` section by section.

**What `spineFamilyFor` does with the time of day** (`spine.cpp:2613-2625`): `Inputs::spine_force` (1 + family; the preview's `--spine`) wins; else `lookout_window` gives the Lookout; else, unless `night_mode`, the mood is wrapped into [0, 6) and **[0.5, 2.5) is the morning, [2.5, 3.5) midday**; everything else (golden 3.5 to 6, the night mood below 0.5, and all of night mode) is the evening. Add your window to that chain and a case to `testFamilyPick` (`test_spines.cpp:147-160`). It must stay RNG-free.

Touch, in order:
1. `SpineFamily` + `SF_COUNT` (`spine.h:45`), `spineFamilyName` (`spine.cpp:2609`), the `--spine` names array `kFam[4]` (`sim/preview_common.cpp:340-348`) and its usage text (261), `review_clips.py` `FAMILY_MOOD` (76) and `FAMILY_BEATS` (72).
2. `spine_family.h`: `XBeatId`, `xPlan`, `xLayout`, `xBuildTimeline`, `xLoopTicks`, `xFormatPlan`, `xBeatName`, `xVariantCount`, `xTick` (copy lines 37-74).
3. A new `core/spine_x.cpp` (CMake globs `core/*.cpp`, `CMakeLists.txt:14`; re-run cmake): helpers and critters, actions, `Step` tables, `kScripts[beats][3]`, `kDurations`, `beatBegin<B>/beatUpdate/beatEnd` (a beat that drove a critter must send it off, `spine_morning.cpp:1331-1356`), the last beat's fade (`leaveUpdate`, 1360-1368: the script must be done before the fade starts or it counts an overrun), the build beat with the camp popping in (`campPop`, 1377-1429), `busySeconds`, `fillerSlots` and a `FitSpec` (1436-1457), the plan (1488-1558), layout (1560-1596), timeline (1598-1616), ticks (1618), format (1624: `family=x beat=v ... fillers=... majors=...`; review_clips parses that shape, `parse_plan` line 153), tick (1654).
4. Dispatch in `spine.cpp`: `spinePlan` (2791), `spineBeginLoop` (2807), `spineBuildTimeline` (2919), `spineLoopTicks` (2942), `spineFormatPlan` (2952), `spineCritterScenes` (2895).
5. Which fillers (`kFillerFamilies`, 2214) and which majors (`kMajors[].families`, `majors.cpp:3654-3697`; weight tweaks like midday's bathe, 3841); a `MajorSlots` (`majors.h:128`; `kMorningSlots`, `spine_morning.cpp:1532`): the slot bits in `kMajors` mean *the family's* first, second, third beat slot.
6. New anchors go on the end of `Anchor` (`script.h:37`) with a case in `anchorPos` and a `CampLayout` field set by your layout (`spine.h:150`, `AT_GUY_PEG`).
7. Tests: copy `test_midday.cpp` (variants over 40 seeds, hiker in frame, quiet corner, incremental equals full, mixed families in turn, strips, pinned lines).

**Limits:** 8 beats (`LoopPlan::fvar[8]`, `pause_s[SB_COUNT]`, `Memory::last_fvar[8]`), 3 variants, 4 fillers, 4 majors, 4 major slots, 8 families (the masks are `uint8_t`), 64 timeline beats.

---

## 7. Recipe: add a minor effect

A minor is a small state machine over at most 2 critters or 2 bits, picked every 10 to 30 s when the stage is calm (`minors.h`).
1. `MinorId` before `MN_COUNT` (`minors.h:40-44`). Add the name to `kNames` (`minors.cpp:16`) and a weight to `kBaseWeight` (20): both are sized by `MN_COUNT` and a short initializer is a null name or weight 0 (never picked).
2. Write `startX(World&) -> bool` (return false to decline: tried again in a second) and `stepX(World&)` (one tick; end with `letGo(m)`). Register in `startMinor` (`minors.cpp:1164`), `stepMinor` (1181) and, for a condition, `gate` (1153, e.g. owl and moth only in the dark).
3. **Randomness: `w.minors.rng` only** (`m.rng.range(...)`). It is the minors' own stream, so it never moves the loop RNG. Adding a minor adds one `range()` call in `minorsInit` (`minors.cpp:1306-1309`), which shifts the minors' sequence for every seed (not the plans or loop seeds).
4. Critters: `spawn(w, kind, feet, facing, flags)` (tag `CT_MINOR`), `own(w, k)`, leave with `leave`/`leaveAt`/`exitAll` (`minors.cpp:181-203`). Bits: `takeBit` and `freeBit`. Hiker: `fx.head` and `fx.arm_w[0]` only when `hikerIdle(w, seconds)` (a pause with time left), and set `m.fx_head`/`m.fx_arm` so `finish` (`minors.cpp:1226`) restores them.
5. **Wrap-up.** When a beat that owns critters begins (the `still` before a major counts), `m.quit` is set: your step must head for the exit or `letGo` quickly (`stepChip`, `minors.cpp:290-318`, is the pattern). A minor still running 30 s after it started is forced to end and counted (`kMaxAge`, 1374): tests demand zero forced.
6. **When minors may run.** Only when `worldBeatOwnsCritters` is false (`minors.cpp:1331`): `w.busy` clear and the beat name in a hard-coded calm list (`pause`, arrival, setup, firewood, fire, foodbag, mug_sunset, read, cairn, skip_stone, cloud_watch, stream_listen). **The morning and midday beats (`wake`, `coffee`, `lunch`, ...) are not in that list, so minors never run inside them, only in their pauses and calm fillers.** That is current behaviour; I found no spec line either way. A copy of the list is in `test_scene.cpp:902`.
7. Tests: `kMinors[11]` and `fired_by[11]` in `testMinors` (`test_scene.cpp:899-957`): every minor must fire in 12 seeds (moods day, night, dusk in turn); no forced ends; started only in calm beats.
8. Clips: `review_clips.py` cuts every minor from the `tick=T minor=NAME` log lines as `minor_<name>` (lines 926-938); run `--night` too if it is gated dark.

---

## 8. Budgets and rules every addition must keep

- **`sizeof(App) <= SS_APP_BYTES`**, now 32 KB (`ss_app.h:39`; `static_assert` at `app.cpp:53`). Measured 21,128 bytes in the lead's build on 2026-11-01 before the chip went into `App` (log in `build-lead/Testing`; `ChipState` is about 50 bytes; the new figure is **unverified**); `test_scene` prints it. Everything lives in `World` by value; no heap. A prop costs its struct twice (`WorldDirty` keeps a copy) plus a `Rect`; each glyph slot about 110 bytes (three arrays of 28). Estimates are mine: measure.
- **Strips per frame** (`appRender` returns it). Today a quiet frame dirties about 7 of 15 (ambient 7.1 to 8.1, whole loop 7.6, majors 7.8 to 8.2, pauses 7.1 to 7.4: `build-lead` test log). Test limits: majors average at most 10, pauses 9, whole loop 9, each stream vignette 10 (`test_scene.cpp:720-731, 1459`). Grass, stream, fire and his breathing keep the lower strips busy anyway; effects in the upper strips (sky, a flash) cost real money. A thing 100 px tall touches 4 strips.
- **PC time**: about 0.13 ms a frame now (same log). Doubling it is a design problem (`START_HERE.md`, budget section). The panel is 30 to 80 times slower (a guess, **unverified**).
- **Primitives per frame**: each antialiased capsule, circle or polygon costs 4 to 10 microseconds on a laptop. Hiker at most 22, critters 7 (bear 9), a glyph shape about 8, whole frame aim under about 150.
- **Palette changes and cache rebuilds** (each redraws the whole static layer; unverified on the panel): tests cap them (morning `1 + fog_steps`, Lookout 16, weather 1 plus 17 in the morning plus 26 in a storm: `test_spines.cpp:328`, `test_lookout.cpp:450`, `test_weather.cpp:219`). Do not add mid-loop palette steps without asking.
- **No allocation after init; no exceptions, RTTI, STL containers or `double`** (`ss_platform.h:8-12`). Write `0.5f`. Cast for `printf` (`(double)`).
- **Compiler flags** (`CMakeLists.txt:17-21`): `-fno-exceptions -fno-rtti -Wall -Wextra -Wconversion -Wdouble-promotion -Wshadow -Werror -ffp-contract=off`, on `core/` only. Practical meaning: narrow with a cast (`(uint8_t)(a + 1)`), `(float)i` in float math, no shadowed names (lambda parameters too), and an unused function in an anonymous namespace is an error.
- **Quiet corner**: nothing in x >= 660 and y >= 420 (`LS_QUIET_X0/Y0`, `landscape.h:37-38`). That is what the tests check (`inCorner`, `test_scene.cpp:1347`) and what `directorClamp` enforces for critters (`director.cpp:155`). **The status chip needs more room, see 8.1: x 538..796, y 412..476.**
- **Pacing** (brief section 4, spec): a pause of 8 to 20 s before each major (`kPauseMin/Max`, `spine.cpp:2662`), a `still` of 4 to 8 s each side (`majors.cpp:3935-3944`; the fit may cut them to 4, `spine.cpp:2775-2778`), one major per slot, 2 to 4 a loop (`kHowMany`, `majors.cpp:3859`), the last loop's majors excluded. "When something feels rushed, add stillness, not more motion" (brief line 220). A script must finish with slack before its beat ends, and end with him at rest.
- **Night rule** (brief section 11): the night palette caps every slot at luminance 0.60 except `ACTOR_LIGHT`, `FIRE_CORE`, `STAR`, `SUN` (0.82) (`palette.h:53-60`). So: no large bright areas, no pure white, bright details small and only in those slots; lightning is a palette shift. Gating uses `night = night_mode || mood within 0.5 of night` and `dusk` (`majors.cpp:3827-3831`), minors use `dark()` (`minors.cpp:51`). The brief's "proposed" fewer majors and longer pauses at night is **not implemented** (I found no code). Test new content with `--night` (mood 0) and at dusk (mood 5).
- **Report with each addition**: `sizeof(App)`, strips per frame, ms a frame, palette rebuilds, overruns, and "unverified on the panel".

### 8.1 What runs over your scene, and what freezes it (added 2026-11-02)

**The doorbell reaction** (`react.h`, `react.cpp`; `app.cpp:182-186, 204-209`; `world.cpp:434-445`). On an `EV_DOORBELL` event, if `reactCanStart` (he is in the scene and in frame, not a Lookout loop, fade under 0.5; `react.cpp:22`), `World::react.active` is set for 85 ticks (3.4 s). While it is active:
- `appTick` skips `Timeline::tick` and `minorsSync` (`app.cpp:206-209`), and `worldTick` runs only the ambient side plus `reactStep` and a *frozen* `stepHiker(w, true)` (rig recomposed with dt 0). The `else` branch (`world.cpp:439-445`) does not run: `spineCritterScenes`, `minorsStep`, `directorStep`, `bitStep`. So every beat, script, `MajorState::t`, `SpineState::ft`, critter stage clock, minor and bit stands still and resumes exactly where it stopped. `w.tick`, clouds, grass, stream, fire, weather and the tent's breathing shadow keep going.
- `reactStep` rewrites `fx = r.fx` (the offsets he had at the bell) plus a hop, a head lift and a lean every tick, restores `facing` at the end, and the wave is laid over the composed pose after the weather overlay by `reactPose` (`world.cpp:182`).
- Rules for new work: (1) Anything that moves him or a critter belongs in a script action, a family or major tick, `spineCritterScenes` or a minor step; all of those sit inside the frozen part for free. A new per-tick system that moves him or critters must be called from inside that `else` branch, never from the always-on part of `worldTick`. Always-on code may only do ambient decoration. (2) Only `HikerFx` and `facing` are saved and restored: do not change his pose, `carry`, `pack_on` or `hat_on` from always-on code. (3) Do not time story things from `w.tick`. (4) `hands_full` is `hiker.carry != CARRY_NONE` (`react.cpp:33`): something held as an `Item` or a pack in his hands does not count, so he would wave with it. How that looks is **unverified**; prefer `CarryId` for what he carries through a scene where a bell is likely. (5) It can start on any tick of any script while he is in frame, so a script must not assume nothing else touches `fx` between two of its ticks.

**The status chip** (`chip.h`, `chip.cpp`; `app.cpp:65-69, 187-190`; `Inputs::chip`, `ss_app.h:24`). It is drawn last in every dirty strip, over everything, the loop's fade included, and marks its own rows. The capsule is 160 by 40 at x 624..784, y 424..464 (`kChipX0/Y0`, `chip.h`); up to two 34 px dog badges sit to its left (to x 538) and a doorbell pulse sends rings out to 12 px beyond (`chipBounds`, `chip.cpp:265`). **Keep x 538..796, y 412..476 clear of action.** That box is larger than the quiet corner the tests and `directorClamp` enforce (x >= 660, y >= 420), so nothing stops you from putting a critter, prop or glyph under a badge: test your own. Ground feet stay at y <= 412 in that range (`kLaneMaxY`, `minors.cpp:29`); keep thrown or falling things and their bounds out of y >= 412 there. Cost: the chip's rows are strips 12 to 14 (rows 384..479); a doorbell pulse lasts 10 s (`kChipPulseSeconds`) and dirties them, so do not count those strips as free then. The preview draws no chip unless you pass `--chip`, and `tools/review_clips.py` does not pass it (**unverified**), so clips do not show it. The chip's colours are final (brief section 11): do not touch them.

**The fast block and the rasterizer scratch.** The strip buffer (51,200 bytes) and the rasterizer's scratch (about 11 KB, `rasterScratchBytes()`) are one `SS_FAST_BYTES` block (62 KB, `ss_platform.h:41`) the host lends: on the panel, LVGL's idle draw buffer. `appStart` zeroes the scratch (`app.cpp:152`); `appRender` installs it for the duration of its own drawing and puts the previous one back (`app.cpp:229-233`). Consequences:
- **Only code reached from `appRender` may rasterize**, i.e. the draw callbacks (`worldDrawDynamic`, `chipDraw`). `fillCircle`, `fillEllipse`, `fillPolygon` and `strokeCapsule` called from a beat, a tick, an action or a plan **draw nothing, silently**, because no scratch is set (`raster.h:18-26`). Span and rectangle fills do not need it. So never "pre-render" or measure pixels in tick code; compute bounds from geometry, as every module does.
- The scratch must start zeroed and every fill leaves it zeroed: never write into the block, never hand it to anything else. Working memory for new features comes from `World` (inside the 32 KB `App`) or a few hundred bytes of stack, not from the fast block.
- Tests and tools that draw outside `appStart` must set a scratch themselves, as the test mains do (`rasterSetScratch(malloc(rasterScratchBytes()))`, `test_scene.cpp:1521`); it must be zeroed, so use `calloc` in new code (the existing mains use `malloc`). Several apps can run side by side because `appRender` restores the old scratch.

---

## 9. How to review a change

**Build and test** (each session in its own folder: `build`, `build-lead`, `build-fix`, `build-soak` are taken):
```
cd screensaver
cmake -S . -B build-NAME -G Ninja
cmake --build build-NAME
cd build-NAME && ctest --output-on-failure      # every tests/test_*.cpp; or ./test_scene.exe
```
`test_scene`, `test_weather`, `test_lookout` and `test_midday` run whole loops; give them minutes (not timed by me). `ss_soak` is a long-run invariant checker (`sim/soak_main.cpp`, not studied).

**`ss_preview` options** (`./ss_preview.exe --help`; `screensaver/README.md` has the table): `--seed N`, `--mood F` (0 night, 1 dawn, 2 morning, 3 day, 4 golden, 5 dusk), `--night`, `--spine evening|morning|midday|lookout`, `--weather clear|rain|snow|wind|heat|fog|chill|storm` (without it the weather is random per loop), `--lookout`, `--chip` (draw the status chip; off by default), `--every N`, `--skip N`, `--frames N`, `--out DIR`, `--doorbell-at TICK`, `--log-beats`, `--loops N`.
- `--loops N`: no frames, one line per loop: `loop=L seed=S length=Ts plan=<family beats fillers majors> memory=<...> weather=<kind>`. This is the randomness table and what the pinned tests compare.
- `--log-beats`: `loop=L seed=S start tick=T plan=...`, `loop=L weather=K`, `tick=T loop=L seed=S beat=NAME` per beat change, `tick=T minor=NAME` per minor. **Cheap beat log, no frames:** `./ss_preview.exe --seed 7 --mood 4 --skip 20000 --frames 0 --out probe --log-beats` (`--skip` ticks are simulated without rendering and logged; 20,000 ticks cover loop 0, the longest being 16,500).
- Force a family or weather: `--spine morning --mood 1.5`, `--weather rain`. There is **no flag to force a major**: find a seed with `for s in $(seq 1 60); do ./ss_preview.exe --seed $s --mood 4 --loops 1 | grep -q "majors=.*NAME" && echo $s; done`. Loop 0 starts with empty memory, so `--seed s` replays that plan. Replaying a later loop's printed seed gives its geometry and minors but not necessarily its plan (memory differs).
- Frame f shows tick `SKIP + (f + 1) * N`. To get frames at ticks T, T+N, ...: `--skip $((T - N)) --every N`. A frame is 1.15 MB: never render a whole loop at `--every 1` (about 18 GB).

**Clips.** `python -I tools/review_clips.py --preview build-NAME/ss_preview.exe --out DIR --seeds 1-12 [--night] [--family morning|midday|lookout] [--weather-clips]` cuts one clip per beat, variant, filler, major and minor, plus `manifest.json` (README lists the options; `--list` prints the table). It needs **ffmpeg, which this PC does not have**: it exits with code 2 at line 831. A real Python is at `C:\Users\Lodeb\AppData\Local\Python\bin\python.exe`; the `python` on PATH first is the Store stub.

**The fallback: PPM frames to a PNG contact sheet** (no Pillow, no ffmpeg; tested here on synthetic frames, same output either way). Save as `sheet.py` in your scratch folder:
```python
# sheet.py: PPM frames -> one PNG contact sheet (zlib only; no Pillow, no ffmpeg).
# usage: python -I sheet.py OUT.png COLS DIV frame1.ppm frame2.ppm ...   (DIV 2: 800x480 -> 400x240 tiles)
import struct, sys, zlib

def read_ppm(path):
    d = open(path, "rb").read()
    tok, pos = [], 0
    while len(tok) < 4:                       # P6, width, height, maxval
        while d[pos:pos + 1].isspace(): pos += 1
        s = pos
        while not d[pos:pos + 1].isspace(): pos += 1
        tok.append(d[s:pos])
    w, h = int(tok[1]), int(tok[2])
    px = d[pos + 1:pos + 1 + w * h * 3]
    if tok[0] != b"P6" or tok[3] != b"255" or len(px) != w * h * 3: sys.exit(path + ": not a whole P6 255 PPM")
    return w, h, px

def tile(w, h, px, div):                      # keep every div-th pixel
    return [b"".join(px[(y * w + x) * 3:(y * w + x) * 3 + 3] for x in range(0, w - div + 1, div))
            for y in range(0, h - div + 1, div)]

def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

out, cols, div = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
tiles = [tile(*read_ppm(p), div) for p in sys.argv[4:]]
tw, th, gap = len(tiles[0][0]) // 3, len(tiles[0]), 4
rows_n = (len(tiles) + cols - 1) // cols
W, H = cols * tw + (cols + 1) * gap, rows_n * th + (rows_n + 1) * gap
sheet = [bytearray(bytes([40, 40, 40]) * W) for _ in range(H)]
for i, t in enumerate(tiles):
    x0, y0 = gap + (i % cols) * (tw + gap), gap + (i // cols) * (th + gap)
    for y, r in enumerate(t): sheet[y0 + y][x0 * 3:(x0 + tw) * 3] = r
raw = b"".join(b"\x00" + bytes(r) for r in sheet)
open(out, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                      + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
print("wrote %s: %d tiles, %dx%d" % (out, len(tiles), W, H))
```
Use: beat starts at `T` in the log; 12 frames 2 s apart: `./ss_preview.exe --seed 7 --mood 4 --skip $((T-50)) --every 50 --frames 12 --out f` then `python -I sheet.py sheet.png 4 2 f/frame_*.ppm` (zero-padded names sort in order), then look at the PNG with the Read tool. Use `--every 25` for fast gags. Seeing it is the real test: Mike judges by eye.

**Checklist for any change:** all tests pass; `--loops 10` seeds 1 and 7 at mood 4 unchanged unless the change is meant to move them (the pins say so); incremental equals full render (the Rig tests do this at checkpoints, `test_scene.cpp:113`); zero overruns; `appCritterCount` 0 at loop end; day, `--night`, dusk and a rain run looked at; numbers from section 8 written down.

---

## 10. Gotchas

1. **Major-id masks.** The 16-bit problem is already fixed in the tree: `typedef uint32_t MajorSet` (`memory.h:39`), asserted twice (`memory.h:40`, `majors.h:48`), used by `majorsPlan` (`majors.cpp:3863, 3884, 3919`) and `spineFitLength` (`spine.cpp:2754-2787`, `uint32_t`), covered by `tests/test_memory_sets.cpp`. Room for 32 ids; 17 used. Still narrow, all deliberately: `MajorState::cue` is 16 bits (used: 1, 2, 4, 8, 16 `kSweep`, 32 `kFired`, 64, 128 as raw literals at `majors.cpp:1517-1522`, 256 to 1024 `kE1..kE3`; free from 2048); `Memory::brave` 16 bits (9 kinds, asserted); `kMajors[].slots` 4 bits; family and filler masks `uint8_t` (4 of 8 bits); `LoopPlan::major[]` and `Memory::ring[]` are `uint8_t`; `Renderer::dirty_mask`, `BuildState` and `LKL_` layer masks are `uint16_t`. `test_scene.cpp:1057` hard-codes bit 5 for raccoon_pack (`1u << 5`).
2. **Three copies of the major-name list in tests** (`test_scene.cpp:983`, `test_spines.cpp:188`, `test_midday.cpp:212`), and `N_MAJORS` is 16 while `MJ_COUNT` is 17 (rain_shelter missing; it only plays for a storm from the data). A major missing from `kMajorNames` that does play makes `testVariety` fail on the `busy` check.
3. **Silent defaults hide typos.** An unknown anchor means "where he stands" (`spine.cpp:2269`). A shape without a `drawShape` case draws nothing and has reach 12 (`majors.cpp:142, 399`). An `ItemId` without bounds marks nothing (`props.cpp:509`). An unsupported critter action becomes idle. Arrays sized by `*_COUNT` with a short initializer give null names and zero weights (`kNames`, `kBaseWeight`, `kRearAng`, `kReach`, `itemName`, filler weights). Check by running, not by compiling.
4. **Pinned regression lines.** `test_spines.cpp:77-100` pins the evening for seeds 1 and 7, `test_midday.cpp:79-131` pins evening, morning and midday. They include each loop's length and the plan, so these change the pins: any new or re-weighted pool entry (`rng.weighted` over `MJ_COUNT`), a new variant, a changed `seconds`, a new filler. Excluding a major from a family leaves that family's pin alone (zero weights are skipped by `Rng::weighted`, `anim/rng.cpp:41`). Re-capture from `ss_preview --seed S --mood M [--spine F] --loops 10` (the tests drop the `weather=` token) and say so in your notes. The evening needs Mike's yes.
5. **The loop RNG is a shared fuse** (section 1.5). Symptoms of a stray draw: every later loop differs and the pins fail from loop 1 on.
6. **A new critter kind changes the brave/shy bits**, so `brave=` in every pinned memory string changes in the loops where the new bit is set (`memory.cpp:18`).
7. **A new minor shifts the minors' random stream** for all seeds (`minors.cpp:1306-1314`), not the plans.
8. **Two copies of the slot rules in `majorsPlan`** (`majors.cpp:3868-3877` and `3895-3898`): edit both.
9. **Calm-beat list in two places** (`minors.cpp:1332`, `test_scene.cpp:902`), and morning and midday beats are not in it (minors only in their pauses and fillers).
10. **`until()` has no timeout.** A predicate that never turns true stalls the script to the end of the beat and counts an overrun. Always end it with a tick that sets the cue, and check `gone()` can become true (a scattered critter is no longer `directorOwns`, but `gone()` still sees it until released).
11. **`place()` re-initializes him**: pack on, hat on, empty hands, height 90 (`spine.cpp:2324`).
12. **Beat timing**: `Beat.seconds` is whole seconds in practice; the script must end earlier. Overruns are counted at the beat's end (`beatEnd`, `spine.cpp:2498`), and the last beat counts when its fade starts. Retuning a duration changes loop lengths in the pins.
13. **`w.tick` stops meaning story time during the doorbell reaction** (`app.cpp:204`). Use the beat clock.
14. **Tables that mirror geometry elsewhere**: the tent's ridge top in `spine_morning.cpp:88-90` ("keep in step" with `props.cpp` `tentGeom`); `worldDrawWater` against the landscape's `drawStream` (`world.cpp:547`); the morning's rock top at `F.x + 14, 333` against `spineLayout`'s rock at `F.x + 18` (`spine_morning.cpp:123`). Move one, move the other.
15. **`WORLD_ACTOR_MAX`, `GLYPH_MAX`, `CRITTER_MAX` (8), `BITS_MAX` (6) are shared pools.** Overflow is silent: later actors are dropped, `directorSpawn` returns -1, `worldTakeBit` returns -1 (it recycles resting cones and leaves first, `world.cpp:243-255`). Always check the return.
16. **`Item` is built positionally** (`world.cpp:313`, `test_scene.cpp:827`); `kPoses`, `kKinds` and `kReach` rows are positional too and must follow enum order.
17. **Names are matched with `strstr`** in tests (`test_scene.cpp:1008-1009, 1118-1119`, `test_weather.cpp:142`): a name that ends with another's, or contains it, gives false hits.
18. **Plan line length**: `ss_preview` formats into 320 bytes (`preview_common.cpp:109, 145`), tests use 200 to 400. A long plan truncates silently and `majors=`, at the end, goes first. Worst case today is about 210.
19. **The Lookout is a separate path.** `sortActors`, `worldTick`, `worldDrawStatic`, `worldDrawDynamic` branch on `SF_LOOKOUT` (`world.cpp:63, 427, 564, 574`); camp props, critters and minors do not exist there; `memoryBeginLoop`/`EndLoop` return early (`memory.cpp:16, 32`). `spineLayout` still runs first for every family (`world.cpp:296`) before the family is set, then the family's layout fixes the camp up.
20. **`SF_EVENING` is 0**: a World whose `plan.family` was never set plays the evening (the World is zeroed before `app.cpp:122` sets it).
21. **Timeline cap of 64** drops beats silently (`scheduler.cpp:38`). The evening uses at most 36.
22. **`-Werror` on core only.** A test file with warnings still builds (`CMakeLists.txt:30`); core with one does not. An unused anonymous-namespace function is an error.
23. **Weather overlays share him.** The hat gag and the fan run only when no script holds him; `majorEnd` forces `hat_on = true` (`majors.cpp:3791`) so an overrun cannot leave him hatless. Leave hands empty and the hat on at the end of every script.
24. **Labels in comments**: the code marks guesses `PROPOSED` and `UNVERIFIED` (`react.h`, `reveal.h`, `palette.h`). Do the same.

---

## Appendix: the other modules in the tree today (not part of Mark's scene system)

Mentioned so you do not trip over them; I read only their headers. Being edited by other sessions.
- `chip.h/.cpp`, `chip_font.h`, `tests/test_chip.cpp`: the status chip and dog badges, bottom right, drawn last in every dirty strip; wired into `app.cpp` as of 2026-11-02; `CHIP_OUTLINE` is a palette slot. Section 8.1.
- `react.h/.cpp`: the doorbell reaction. Freezes beats, critters, minors and the director while it runs (`app.cpp:204-209`, `world.cpp:434`); uses `HikerFx` and restores it. Section 8.1.
- `reveal.h/.cpp`: the touch reveal (`appRevealBegin/Step`); freezes `appTick` and `appRender` (`app.cpp:178, 228`).
- `house_weather.h/.cpp` (in `core/`): OUTSIDE reading to `Inputs::weather_code`. Host side; the scene's own mapping is `kCodeKinds` in `weather.cpp`.
- `memory.*`: the 32-bit `MajorSet` fix is in (gotcha 1).
- `WORKLOG.md` in this folder lists the open queue (morning and daytime ideas, slackline, kite, camp dog).
