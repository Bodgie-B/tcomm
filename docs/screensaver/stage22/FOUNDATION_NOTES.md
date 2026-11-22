# Stage 22: the foundation as built (steps A0 and A1)

Written 2026-11-18 by the integrator after steps A0 and A1 of INTEGRATION_PLAN.md section 7. This is
what the five parallel builders code against: the real signatures, the files and who owns them, what
changed from the plans and why, and the commands that built. Nothing here is committed (Mike has not
asked). Nothing was looked at on the panel: everything below is host-only and **unverified on the
panel**.

## 1. Build folders and commands (Git Bash)

The WinLibs toolchain, as `build-plink-on` has it (Release, Ninja, SS_XMAS ON):

```
cd screensaver
TC=~/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH=$TC:$PATH
cmake -S . -B build-s22-off -G Ninja -DCMAKE_CXX_COMPILER=$TC/c++.exe -DCMAKE_MAKE_PROGRAM=$TC/ninja.exe -DSS_XMAS=ON -DSS_S22=OFF
cmake -S . -B build-s22-on  -G Ninja -DCMAKE_CXX_COMPILER=$TC/c++.exe -DCMAKE_MAKE_PROGRAM=$TC/ninja.exe -DSS_XMAS=ON -DSS_S22=ON
cmake --build build-s22-off
cmake --build build-s22-on
cd build-s22-off && ctest -j 6 --output-on-failure      # about 2.5 minutes for the 36 suites
```

CMake prints warnings about object paths over 250 characters (the workspace path is long); the build
works regardless. New `core/**/*.cpp` and `tests/test_*.cpp` files are picked up by the globs
(CONFIGURE_DEPENDS), so a plain `cmake --build` re-globs.

A tool built by hand against the library needs the same defines: `-DSS_XMAS=1` (it changes the
World) and `-DSS_S22=0/1` (it changes no struct, so a mismatch only lies about the switch).

The baseline folder `build-s22-base` is the tree before A1 (HEAD 3260e2c, clean) and must not be
rebuilt.

## 2. The switch

- `core/scene/stage22_switch.h`: `#ifndef SS_S22` / `#define SS_S22 0`; `int ssS22Switch();`
  (defined in `stage22_switch.cpp`). CMake: `option(SS_S22 ... OFF)`, PUBLIC define `SS_S22=1/0`.
- It gates only plans. It gates no struct, enum or hook: the World is the same either way (sizes
  below are identical in both folders).
- **Changed from the plan (6.1):** in A1 the new weights are 0 in BOTH folders. `majors.cpp` has
  `constexpr bool kS22PlanBlock = false;` and `kS22Live = SS_S22 != 0 && kS22PlanBlock`; `kTaichiW` and
  `kLongshotW` are 1.6 and 0.6 only when `kS22Live`. Why: without the plan block's gates (step C0) a
  live weight plans the empty stub majors with no gates at all (tai chi at night, the long shot with a
  dog out) and would push `test_dog` and `test_memory_sets` before any piece exists. Step C0 sets
  `kS22PlanBlock = true` together with the block.
- **Pin wrap, changed from the plan:** only the morning and midday pins are wrapped in
  `ssS22Switch() == 0` (`test_midday.cpp` testRegression's morning and midday lines,
  `test_morning_stay.cpp` testMarkUnchanged). The evening pins (`test_spines.cpp` testEveningRegression,
  `test_midday.cpp` evening lines) stay checked in both folders, because section 8.2 and step C7 say the
  evening must never move. `test_spines.cpp` has only evening pins, so it got the `isMajor` names only.

## 3. Ids, enums and sizes (as built)

| Thing | Value |
|---|---|
| `MJ_TAICHI`, `MJ_LONGSHOT`, `MJ_MUSHROOM` | 25 `taichi`, 26 `longshot`, 27 `mushroom`; `MJ_COUNT` 28 (`static_assert` in majors.cpp) |
| `SH_GONG`, `SH_FLAG` | 16, 17 (before `SH_COUNT`, now 18) |
| `DayKind` | `DAY_TAICHI` 7, `DAY_HERD` 8, `DAY_TRIP` 9 |
| `DAY_PARTS` | 3 |
| `ActorKind` | `ACT_REED` after `ACT_STREAM`, before `ACT_XMAS` (idx = clump) |
| `CritterTag` | `CT_HERD` after `CT_STREAM` |
| `WORLD_ACTOR_MAX` | 136 |
| `REED_CLUMPS`, `REED_STALKS` | 5, 5 |
| `LS_REED_FRINGE` | 16 |
| `SKYTRIP_BANDS` | 10 |
| `MUSH_PATCH` | 6 |
| `HERD_MAX`, `HERD_PRINTS`, `TRIP_PROPS` | 6, 14, 8 |

Sizes on the host with the Christmas set (both folders the same):

| | Before | After |
|---|---|---|
| `sizeof(App)` | 29,856 | **29,928** (wire 29,992: 64 bytes left) |
| World | 24,680 | 24,752 |
| Memory | 84 | 88 (`static_assert` in memory.h) |
| LoopPlan | 88 | 88 |
| Inputs | 84 | 88 |
| Landscape | 3,228 | 3,268 (+40: LsTrip 20, two bytes, the fringe 16, padding 2) |
| Hiker | 204 | 208 |
| Visitors (two Hiker rigs) | 952 | 960, inside LookoutLand 3,516 |
| DayPlink | 848 | 976 (FarState 128), inside DayBike 1,904 |
| DayTaichi / DayHerd / DayTrip | | 128 / 424 / 344, each inside DayBike |
| Reeds | | 208 (`static_assert <= 224` in reeds.h) |
| The Lookout's shared block | 92 + 288 = 380 | 92 + 288 + 208 = 588 of 624 (36 left; tai chi's StreamVisit +8 in C2) |

The App grew by 72 and not the plan's 56 to 64 because the fringe (16, REVIEW_FIXES F18) is new; the
Inputs' 4 bytes landed in padding. Every later step must print `sizeof(App)` and stop before 29,992.

## 4. Every hook as built

### 4.1 Shared files and what each now calls

| File | Edit |
|---|---|
| `CMakeLists.txt` (CRLF) | `option(SS_S22)` |
| `core/ss_app.h` (CRLF) | `uint16_t Inputs::carry` (last field); `uint16_t appCarry(const App* app);` |
| `scene/app.cpp` (CRLF) | `appStart`: `memory.mush = carry & 0xFF; memory.taichi_next = carry >> 8;` before the first `beginLoop`; `appCarry`; in the EV_DOORBELL loop `herdBell(a->world);` (before reactStart); beside streamSetInputs `reedsSetInputs(a->world, m, night \|\| a->in.night_mode, a->in.month);` (m is the wrapped mood) |
| `scene/memory.h` | `uint8_t taichi_next, herd_wait, mush, s22_spare;` after `visit_wait` |
| `scene/memory.cpp` (CRLF) | `memoryEndLoop`: `if (kMushLive) m.mush = mushAfterLoop(m.mush, wet);` with `wet = weather.from_data && (kind == WX_RAIN \|\| kind == WX_STORM)` and `constexpr bool kMushLive = false;` (step C5a flips it, in this file only); `memoryFormat`: ` mush=S` only when `mushStage(m.mush) != 0` |
| `scene/spine.h` | `uint8_t LoopPlan::herd;` last |
| `scene/world.h` | `#include "reeds.h"`; `ACT_REED`; `CT_HERD`; `WORLD_ACTOR_MAX 136`; `Reeds reeds;` after `StreamLife stream;` in the shared block; both asserts `+ sizeof(Reeds)` |
| `scene/world.cpp` | after `landscapeGenerate`: `w.land.patch_stage = mushStage(w.memory.mush);` (before ambientInit, so pickTufts sees it); after `streamInit`: `reedsInit(w, rng);`; sortActors (camp), after the stream's two: `if (reedsActorY(w, i, y)) L.add(ACT_REED, i, y)` for each clump; coverHiker: `hiker_rect` (not `hiker_full`) united with `reedsMouthBounds(w).clipped(frame)` when not empty; stepHiker: `reedsApplyPose(w, P);` after weatherApplyPose, before mailPose and reactPose; worldTick: `reedsStep(w);` after streamStep (ambient, before the react branch), `reedsStoryStep(w);` after weatherStep (every branch: it must return at once while react.active, fi.on or visitorsHolds), `reedsTrack(w, r);` after streamTrack; worldDrawDynamic: `case ACT_REED: reedsDraw(w, a.idx, strip, clip, pal)`, in ACT_HIKER `reedsDrawMouth(w, strip, clip, pal)` after armloadDraw, `reedsDrawAir(w, strip, clip, pal)` after the actor loop, before the smoke |
| `scene/landscape.h` (CRLF) | `#include "skytrip.h"`; `LS_REED_FRINGE 16`; at the end of Landscape: `LsTrip trip; uint8_t patch_stage, patch_lifted; uint8_t reed_fringe[LS_REED_FRINGE];` |
| `scene/landscape.cpp` (CRLF) | `BL_TUFTS`: `reedsDrawStatic(ls, s, pal, clip, b)` after the tuft loop; `BL_MUSHROOM`: `patchDrawStatic(ls, s, pal, clip, buildMushroomT(b))` after the mother. `drawSky` and `drawRidge` untouched (C5b) |
| `scene/ambient.h`, `ambient.cpp` (CRLF) | `float ambientWindAt(const World& w, float x);` the old tuft formula whole, then `w.day.kind == DAY_TRIP ? tripSway(w, x, plain) : plain`; `updateSway` is `sway[i] = ambientWindAt(w, x_i)` (sway hash equals the baseline); pickTufts adds `&& !patchNear(ls, x, y, 12.f)`; ambientDrawGlints draws in `tripGlintColor(w, i, pal)` |
| `scene/weather.h`, `weather.cpp` (CRLF) | `float weatherBreeze(const World& w)` = `clamp01(0.18f + 0.42f * (weatherSwayMul(w) - 1.f))` (real, not a stub); `hikerFree` returns false while `reedsPickActive(w)` |
| `scene/fireice.cpp` | `fireIceCalm` returns false while `reedsPickActive(w)` (REVIEW_FIXES F16; covers visitorsCanStart) |
| `scene/hiker.h`, `hiker.cpp` (CRLF) | `float Hiker::foot_turn;` (last field, 0 in hikerInit); heel and toe in `build`, `hikerHandPos`, `hikerHeadPos` use `heelLen(h)` / `toeLen(h)` = `kHeel + (0.035f - kHeel) * foot_turn` (exactly kHeel at 0); `Vec2 hikerMouthPos(const Hiker& h)` real: head centre + 0.85 r along the face - 0.35 r up the crown, turned with lean + head and mirrored with facing (`kMouthFwd`, `kMouthDown` PROPOSED). The file is frozen after A1: ask the integrator to change the mouth constants |
| `scene/react.cpp` | reactWaveOk: false while `reedsPickActive(w) \|\| tripHandsFull(w)`; reactStart: `look_only \|\|= taichiInForm(w)`, `hands_full` also `taichiHatInHand(w) \|\| tripHandsFull(w)` |
| `scene/housedogs.h`, `housedogs.cpp` | `int houseDogsLend(World& w, int first = HD_COPPER);` (HD_LUCY asks for Lucy first; anything else Copper first, as before) |
| `scene/majors.h` (CRLF) | the three ids; SH_GONG, SH_FLAG |
| `scene/majors.cpp` | includes; `kS22PlanBlock`, `kS22Live`, `kTaichiW`, `kLongshotW`; rows `{"taichi", {143,143}, kTaichiW, 1, kMorn, taichiMajorBegin, taichiMajorTick, ...}`, `{"longshot", {109,109}, kLongshotW, kOut, kMid, longshotMajorBegin, longshotMajorTick, ...}`, `{"mushroom", {201,201}, 0.f, 2 \| 4, kMid, tripMajorBegin, tripMajorTick, ...}`; `static_assert(MJ_COUNT == 28)`; scriptOf, majorSeconds, majorEnd dispatch to the three; shapeReach and drawShape send SH_GONG and SH_FLAG to `longshotShapeReach(sh)` / `longshotShapeDraw(g, s, clip, pal)`. `majorsPlan` and `majorKept` untouched (C0) |
| `scene/day.h` | includes herd.h, taichi.h, trip.h; DayKind; `FarTarget`, `FarState` (LONGRANGE 7) and `DayPlink::far`; `DayTaichi { TaichiState t; }`, `DayHerd { HerdState h; Rng rng; }`, `DayTrip { TripState t; Rng rng; }` in the union as `taichi`, `herd`, `trip`, each asserted `<= sizeof(DayBike)`; `DAY_PARTS 3` |
| `scene/day.cpp` | a case per new kind in sizeOf, dayBounds, dayPose, dayHoldsFacing, dayStep, dayTrack (same kind only; a change of kind marks old and new dayBounds as before), dayPartY, dayPartDraw, each calling the kind's `xDay*` hook (4.2) |
| `sim/preview_common.h`, `.cpp` (CRLF) | `uint16_t PreviewOptions::carry`; `--carry N` (0..65535) and `--mush N` (sets the low byte); `in.carry = o.carry` |
| `tests/test_spines.cpp`, `test_midday.cpp` (CRLF), `test_morning_stay.cpp` | the three names in `isMajor`; the pin wrap of section 2 |

### 4.2 The day hooks (one set per kind, in its day_*.h)

For X in `taichi` (DayTaichi), `herd` (DayHerd), `trip` (DayTrip):

```
Rect XDayBounds(const DayX& d);
void XDayPose(World& w);                         // after hikerTick
bool XDayHoldsFacing(const World& w);
void XDayStep(World& w, bool frozen);            // after stepHiker; frozen: the doorbell
void XDayTrack(const DayX& prev, const DayX& cur, const World& w, Renderer& r);   // mark own changes
bool XDayPartY(const World& w, int part, int16_t& y);   // part 0..2
void XDayPartDraw(const World& w, int part, Surface& s, const Rect& clip, const Palette& pal);
```

The long shot has none: it is DAY_PLINK and reuses the plinking's hooks.

### 4.3 The skeleton files and their signatures

All new files are LF and ASCII. Each has an OWNERSHIP comment at the top naming the step that fills it
in. Every function is a stub that draws, marks and changes nothing (scripts are one `still(0.5f)` step,
so a stub major planned by mistake is harmless).

**`core/mush_patch.h`** (header only, core/ so the panel host can share it like set_piece_day.h).
Owner: B4, then C5a wires it live in memory.cpp. Real now: `MUSH_RIPE 6`, `MUSH_PICKED 7`,
`mushStage(b)`, `mushLeft(b)`, `mushSoak(b)`, `mushPack(stage, left, soak)` (bits 0-2, 3-5, 6).
Stubs: `uint8_t mushAfterLoop(uint8_t b, bool wet)`, `uint8_t mushAfterMinutes(uint8_t b, uint32_t
dry_minutes, bool raining)` (both return b), `bool mushWillPlay(uint8_t mush, bool midday, bool night,
bool data_wet)` (false). B4 may fill in the rule freely: it is not live until C5a flips `kMushLive`.

**`scene/reeds.h/.cpp`**. Owner: B1 (plants), then C1 (pick, stem, small life). The Reeds struct (208
bytes) with F10's `sway_q[5][2]`, `d_sway[5][2]`, no `dream_q`, no `pad0`, no seed dirty words. The
6.1 interface (all stubs but `reedsWind`, which is `ambientWindAt(w, x)`):
```
bool reedsClump(const World&, int clump, ReedClumpInfo&);   bool reedsStalk(const World&, int clump, int stalk, ReedStalkInfo&);
Vec2 reedsTip(const World&, int clump, int stalk);          int reedsNearest(const World&, Vec2 p, float max_d, bool camp_only);
int reedsNearestStalk(const World&, int clump, Vec2 p, bool reeds_only);
bool reedsStandPoint(const World&, int clump, int side, Vec2& feet);   Rect reedsBounds(const World&, int clump);
void reedsPush(World&, int clump, float dir, float strength);   void reedsBlast(World&, Vec2 at, float radius, float strength);
void reedsBend(World&, int clump, int stalk, Vec2 tip_offset);  bool reedsTake(World&, int clump, int stalk, bool allow_cattail = false);
float reedsWind(const World&, float x);
bool reedsChewing(const World&);  void reedsGiveChew(World&, float seconds);  void reedsDropChew(World&);
bool reedsPickActive(const World&);
```
What World, the landscape and the app call:
```
void reedsInit(World&, const Rng& loop_rng);        void reedsSetInputs(World&, float mood, bool night, uint8_t month);
void reedsStep(World&);   void reedsStoryStep(World&);   void reedsApplyPose(const World&, HikerPose&);
void reedsTrack(World&, Renderer&);   bool reedsActorY(const World&, int clump, int16_t& y);
void reedsDraw(const World&, int clump, Surface&, const Rect& clip, const Palette&);
void reedsDrawStatic(const Landscape&, Surface&, const Palette&, const Rect& clip, const BuildState& b);
void reedsDrawAir(const World&, Surface&, const Rect&, const Palette&);
void reedsDrawMouth(const World&, Surface&, const Rect&, const Palette&);   Rect reedsMouthBounds(const World&);
```

**`scene/taichi.h/.cpp`**. Owner: B2. `TaiKey`, `TaiHandsDef`, `TaichiState` (128 bytes). No functions
yet: nothing shared calls the module; B2 adds its API (TAICHI_PLAN 10.1).

**`scene/day_taichi.h/.cpp`**. Owner: C2.
```
uint8_t taichiPlanVar(uint32_t r_var, const Memory& mem, int dog_out);   int taichiSeconds(uint8_t var);   // stub 143
const Step* taichiScript(uint8_t var, uint8_t& n);
void taichiMajorBegin(World&);  void taichiMajorTick(World&, float dt);  void taichiMajorEnd(World&);
bool taichiInForm(const World&);  bool taichiHatInHand(const World&);  bool taichiWatchSpot(const World&, Vec2& out);
+ the day hooks of 4.2
```

**`scene/herd.h/.cpp`**. Owner: B3 (after the deer rig in critters.*). `HerdRole`, `HerdMode`,
`HerdDeer`, `HerdPrint`, `HerdState` (408 bytes), `HerdCue`. No functions yet (B3 adds them).

**`scene/day_herd.h/.cpp`**. Owner: C4. `void herdBell(World&);` `bool herdGuestAt(const World&, Vec2&
out);` and the day hooks. herdPlanPick, herdSeconds, herdReturnExtra and the beat hooks are C4's to add
(their signatures depend on middayPlan and the Beat table).

**`scene/patch.h/.cpp`**. Owner: B4. `LsShroom`, `LsPatch` (42 bytes, not stored in the Landscape).
```
void patchLayout(const Landscape& ls, LsPatch& out);     // stub: n = 0
void patchDrawStatic(const Landscape& ls, Surface& s, const Palette& pal, const Rect& clip, float t);
bool patchNear(const Landscape& ls, float x, float y, float r);   // stub: false
```

**`scene/skytrip.h/.cpp`**. Owner: B4 (sky), C5b wires it. `SKYTRIP_BANDS`, `LsTrip`, and a real
`bool skyTripOn(const LsTrip&)`. skytrip.h is included by landscape.h: it must not include landscape.h.

**`scene/trip.h/.cpp`**. Owner: C5c. `TRIP_PROPS`, `TripProp`, `TripState` (328 bytes).
```
float tripSway(const World& w, float x, float plain);          // stub: plain
Pixel tripGlintColor(const World& w, int i, const Palette& pal);   // stub: pal[WATER_LIGHT]
```

**`scene/day_trip.h/.cpp`**. Owner: C5c.
```
uint8_t tripPlanVar(uint32_t r_var);   int tripSeconds(uint8_t var);   // stub 201
const Step* tripScript(uint8_t var, uint8_t& n);
void tripMajorBegin(World&);  void tripMajorTick(World&, float dt);  void tripMajorEnd(World&);
bool tripHandsFull(const World&);
+ the day hooks of 4.2
```

**`scene/day_longshot.h/.cpp`**. Owner: B5 (targets and shapes), then C3 (the major).
```
int longshotTargets(const Landscape& ls, FarTarget out[4]);    // stub: 0
float longshotShapeReach(uint8_t shape);                       // stub: 12
void longshotShapeDraw(const Glyph& g, Surface& s, const Rect& clip, const Palette& pal);
uint8_t longshotPlanVar(uint32_t r_var);   int longshotSeconds(uint8_t var);   // stub 109 (F13)
const Step* longshotScript(uint8_t var, uint8_t& n);
void longshotMajorBegin(World&);  void longshotMajorTick(World&, float dt);  void longshotMajorEnd(World&);
```
FarTarget and FarState live in day.h (the integrator's): ask before changing them.

**`scene/stage22_switch.h/.cpp`**. The integrator's.

## 5. What changed from the plans, and why

1. **The fringe in the Landscape (F18):** `uint8_t reed_fringe[16]`, one byte per spike cluster, 0 none;
   the encoding is the reeds module's (PROPOSED: bits 0-6 x / 8 + 1, bit 7 the near bank). +16 bytes of
   App. reedsInit writes it, reedsDrawStatic reads only it.
2. **mush_patch.h lives in `core/`** (the mushroom plan's place), not `core/scene/`, so the panel host
   can include it in C8. `mushWillPlay` takes plain values (mush byte, midday, night, data wet) instead
   of Inputs and Memory, so the header needs none of the scene's types; C0 passes `mem.mush`,
   `slots.family == SF_MIDDAY`, its `night`, and whether the data weather is rain, snow or storm.
3. **The patch is not live:** memory.cpp's `kMushLive = false` holds the call to `mushAfterLoop` back
   until C5a, so B4 can fill in the rule without the patch appearing in B-phase runs.
4. **Weights 0 in both folders until C0** (section 2).
5. **Evening pins not wrapped** (section 2).
6. **The long shot's beat and the trip's beat seconds** in the stubs are REVIEW_FIXES' figures (109,
   201), not the plans' (102, 196). The rows' `seconds` fields are unused (majorSeconds dispatches).
7. **F16 applied:** fireIceCalm asks reedsPickActive.
8. **F15 not applied here** (the deer flag from tail_add alone): it is the deer rig builder's (B3,
   critters.cpp).
9. **F25 not decided here:** fireIceCalm still returns false for any `day.kind != DAY_NONE`, so fire and
   ice and the visiting couple are held off while DAY_HERD holds the union for the prints. C4 decides.
10. **reedsInit runs before the family is known** (inside worldBeginLoop, before spineBeginLoop's family
    layout): story spots that move with the morning or midday layout must be read later (reedsSetInputs
    runs every camp tick). The fringe written at reedsInit sees only spineLayout's evening camp layout.
11. **One wind:** ambientWindAt already branches to tripSway for DAY_TRIP; reedsWind wraps it.
    `reedsDream` does not exist.
12. **`houseDogsLend`'s argument** is `int first` with HD_LUCY / anything else (Copper), rather than an
    order list, so the visitors' call is unchanged.
13. **`stage22_switch.cpp`** is a new file the plan did not list: `ssS22Switch()` needs a definition in
    the library (as ssXmasSwitch lives in xmas.cpp).
14. **`sim/preview_common.h`** was edited too (the plan named only the .cpp): the option needs a field.

## 6. Baseline and checks (A0 and A1's "done")

The baseline is in `docs/screensaver/reviews/stage22-baseline/` (see its README.txt: how it was made
and how to compare a later build against it). Results for A1: see section 7, filled in after the runs.

## 7. Results of the A1 runs

| Check | build-s22-off | build-s22-on |
|---|---|---|
| Build, core with -Werror | clean | clean |
| ctest (36 suites) | 36 of 36 | 36 of 36 |
| `--loops 10` lines, seeds 1 and 7, evening, night, morning, midday (80 lines) | equal to the baseline | equal |
| Beat logs and minors logs, seeds 1 to 12 | equal (but the "done:" timing lines) | equal |
| Frame hashes, loop 0 rendered every tick, every 20th frame, seeds 1, 7, 12, four families | equal | equal |
| Strips a frame (the same 12 runs) | equal (7.39 to 10.08) | equal |
| Host ms a frame, quiet machine | 0.119 to 0.161, as the baseline's 0.116 to 0.165 | not re-timed quietly |
| Sway hash (2,000 ticks, seed 1) | equal | equal |
| Plinking state hash (seeds 6 and 26) | equal | equal |
| Deer drawn in every action, a walk, a run | equal | equal |
| `ss_soak --hours 6` | CLEAN, determinism PASS | CLEAN, determinism PASS |
| sizeof(App) | 29,928 | 29,928 |

Also: every core .cpp passes `-fsyntax-only` with the full warning set as the panel builds it
(SS_XMAS 0, SS_S22 0). `ss_preview --mush 3 --loops 1` prints ` mush=3` in the memory (the byte is
carried; the patch draws nothing yet). The ON folder equals the baseline only because the weights are
held at 0 until C0 (section 2); from C0 on, ON is expected to differ in morning and midday plans.

Not done or not checked: nothing on the panel; no stills (the stubs draw nothing); the firmware host
(C8) does not pass Inputs::carry yet, so on the panel carry stays 0.

## 8. Step C0 as built (the plan block, 2026-11-18)

- `majors.cpp`: `kS22PlanBlock = true`. Each piece plans only with SS_S22 on AND once its own constant is
  true: `kTaichiLanded` (step C2 flips it), `kLongshotLanded` (C3), `kHerdLanded` (C4), `kMushroomLanded`
  (C5c). All four are false now, so no stub major can be planned into an empty beat in either folder,
  and both folders plan exactly as the baseline. The pool rows now carry the real weights (tai chi 1.6,
  long shot 0.6, mushroom 0); `majorsPlan` zeroes a weight whose piece does not plan.
- `uint8_t majorsS22Plans(const Memory&)` (majors.h): the S22Piece bits that plan (stage22_switch.h:
  `S22_TAICHI 1`, `S22_LONGSHOT 2`, `S22_MUSHROOM 4`, `S22_HERD 8`, `s22Plans`). `bool
  majorsMushroomPlays(in, mem, family)`: the ripe-loop question the herd's pick asks.
- `Memory::s22_spare` is renamed `s22_preview`: 0 on the panel and in every run. A test sets bit 7 and the
  piece bits to plan as if those steps had landed (tests/test_plan_block.cpp does). Ignored with SS_S22
  off. Never carried (Inputs::carry holds only mush and taichi_next) and never printed.
- `majorsPlan`: INTEGRATION_PLAN 3.1 point 9 as written (gates, herd zeroing, ripe, want caps, the
  hillside rule, tai chi forced in round 0 when `taichi_next` bit 2 is clear, the mushroom forced in round
  0 or 1, tai chi counts as two, `taichiPlanVar`, `longshotPlanVar`, `tripPlanVar`). The hillside rule
  (plink, longshot, ranger_ridge at most one) holds only while the long shot plans, so plink and
  ranger_ridge may still share a loop until C3.
- `majorKept`: also MJ_TAICHI and MJ_MUSHROOM. `spineFitLength`: floor 1 with either in the plan, and
  REVIEW_FIXES F26 (the drop loop stops when every major left is kept).
- `spinePlan`: `p.herd = 0` for every family; `taichi_next` bit 2 cleared in any camp loop that is not a
  morning (evening, midday), only while tai chi plans.
- `day_herd.h/.cpp`: `herdPlanPick(loop_rng, in, mem, mushroom_plays)` real (private stream "HERDDEER",
  three draws always, gates: dog out, data rain or storm, `herd_wait`, the ripe loop), `herdVariant`,
  `herdSize`, `kHerdShare 0.30`. `middayPlan` calls it first. C4 still owes: the herd's seconds in
  `busySeconds` (until then a previewed herd loop is fitted without its 104 + 16 s), the beat, the plan
  token.
- New suite `tests/test_plan_block.cpp`.
