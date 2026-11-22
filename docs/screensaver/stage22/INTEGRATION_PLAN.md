# Stage 22: integration plan for the five new pieces

Written 2026-11-18. Design only: nothing here is built. It sits on top of the five plans in this
folder (CATTAILS_PLAN.md, TAICHI_PLAN.md, DEER_PLAN.md, MUSHROOM_PLAN.md, LONGRANGE_PLAN.md) and
decides where they meet, where they disagree, and in what order they are built. Where this file and
one of the five disagree, this file wins; section 3 lists every change by plan.

Numbers marked PROPOSED are starting values. Everything about how it looks is UNVERIFIED until Mike
has seen it, and on the panel. Code references are lines on 2026-11-18 in `screensaver/core/scene/`
unless a path is given; they drift, search for the name.

What I read myself in the code for this file: `majors.h` (ids, glyphs), `majors.cpp` 3655 to 4050
(pool, `majorsPlan`), `day.h` (all), `memory.h` (the struct), `spine.h` (LoopPlan), `spine.cpp`
2740 to 2820 (`spineFitLength`, `spinePlan`), `world.h` 60 to 320, `anim/rng.cpp` (`weighted`),
`ambient.cpp` (`pickTufts`), `minors.cpp` (names), `ss_app.h` (grep), `CMakeLists.txt` (options),
`xmas_switch.h`, the sizeof tripwires in the tests, `ss_host.cpp` (`phaseMood`, `appStart` call).
Everything else about the code is taken from the five plans and marked "per the X plan" where it
matters. I ran nothing.

Second pass, same day (a re-read of this file against the code and the five plans; corrections are
in place below). Checked again by reading: `MJ_COUNT == 25` and the 25 pool rows with their weights
(`majors.cpp` `kMajors`; midday pool 18.7, morning 9.8); `majorsPlan` as quoted in 3.1 point 9;
`majorKept` (`majors.cpp:3904`); the fit's floor (`spine.cpp:2758`); `DayKind` (6 kinds) and
`DAY_PARTS 2` (`day.h`); `Memory` is 84 bytes by count; `LoopPlan` 86 padded to 88; `WORLD_ACTOR_MAX
130`, `CRITTER_MAX 11`, `ActorRef` 4 bytes (`world.h`); `Rng::weighted` skips zero weights and makes
one draw (`anim/rng.cpp:41`); `mem.last_family` is written at `spine.cpp:2815`; the 11 minor names
and weights (sum 12.3); the wires at `test_xmas.cpp:352` (29,992) and `test_day_majors2.cpp:664`,
`test_day_plink.cpp:614` (30,000); the day majors clear the union with a `memset` at begin and at end
(`day_plink.cpp:486, 525`); line endings by `file`. Run, read-only: `build-plink-on/test_xmas.exe`
prints `sizeof(App) 29856, World 24680, XmasState 92, LookoutState 624, StreamLife 288`; the last
test log in `build-plink-on` has `sizeof(DayPlink) = 848, sizeof(DayBike) = 1904`.

---

## 0. Mike's words

- "long range shooting with target out on the mountain sides and far trees"
- "adding reeds and cattails by the stream and having the hiker sometimes pick one and put in his
  mouth like huckleberry finn". 2026-11-18: "I would like the reeds and cattails to feature as a
  passive content that is in all daily modes from morning until night."
- "having mushrooms grow near the one mushroom after a rain storm, having hiker pick one, eat one,
  lay down and have the sky go psychodelic for a couple minutes"
- "when the hiker is away, a group of deer walk into the site, some stop to munch on grass, a couple
  investigate the tent, carefully, one fawn splashes in the stream, the momma deer lifts her head
  and tail alerts which the other deer alert to, and the Mom trots off the side of the screen with
  other semi-alert deer, a little later the hiker walks back in."
- Morning Tai Chi. 2026-11-18: "I would like the Tai Chi to be a slow progression of many exercises
  and not be in a hurry to finish."
- For all: "Consider other interactions with animals and these from other vignettes."
- Standing: natural, classic, unhurried; "when something feels rushed, add stillness, not more
  motion"; no ambient sound; the evening spine scripts do not change; a play may be skipped by a
  rule.

---

## 1. Final names and numbers (fixed before anyone builds)

| Thing | Final | Notes |
|---|---|---|
| `MJ_TAICHI` | 25, name `taichi` | morning only |
| `MJ_LONGSHOT` | 26, name `longshot` | midday only; runs on `DAY_PLINK` |
| `MJ_MUSHROOM` | 27, name `mushroom` | midday only; weight 0, forced when ripe |
| `MJ_COUNT` | 28 of 32 | 4 ids left |
| The deer visit | no major id; midday timeline beat `herd`; plan token ` herd=N` | |
| The reeds | no major id; minor `MN_REED_BIRD` (index 11, name `reed_bird`); `MN_COUNT` 12 | |
| `DayKind` | `DAY_TAICHI` 7, `DAY_HERD` 8, `DAY_TRIP` 9 | appended in that order |
| `DAY_PARTS` | 3 (was 2) | the trip needs three; tai chi uses part 1, the herd part 0 |
| `ActorKind` | `ACT_REED` after `ACT_STREAM`, before `ACT_XMAS` | |
| `WORLD_ACTOR_MAX` | 136 (was 130) | +5 reed clumps, +1 third day part |
| `CritterTag` | `CT_HERD` after `CT_STREAM` | |
| `ShapeId` | `SH_GONG`, `SH_FLAG` before `SH_COUNT` | longshot only |
| Memory string token | ` mush=S`, printed only when S is not 0 | no pinned memory string moves |

Name check (tests match with `strstr`, HOW_TO_ADD_SCENES gotcha 17): none of `taichi`, `longshot`,
`mushroom`, `herd`, `reed_bird` contains or is contained in an existing major, filler, beat or minor
name (checked against the 25 pool names in `majors.cpp:3687` and the 11 minor names in
`minors.cpp:16`; `reed_bird` and `bird_chip` do not contain each other; `longshot` was chosen over
"longrange" because of `ranger_ridge`). Beat and filler names were checked from HOW_TO_ADD_SCENES
1.2 and 7.6 only (UNVERIFIED against the three spine files).

---

## 2. Proposed answers to the open choices that cross pieces

Builders build the defaults. Section 9 has the same list as questions for Mike.

| Choice | Default | Alternative |
|---|---|---|
| Order the pieces go live | reeds, tai chi, longshot, herd, mushroom | any; the foundation step makes them independent |
| Stem in his mouth during tai chi | no: it falls as the major begins (it almost never arises, section 4.5) | he keeps it |
| Stem in his mouth prone at the rifle | yes | `kLsKeepStem = false` |
| Stem when he picks mushrooms | it falls as he crouches at the patch | none |
| A deer eats a cattail | yes: the second doe bites the head off the fireside cattail (section 4.1) | `kHerdNibble = false` |
| The fawn and the reeds | its third dash ends nose first in the upstream clump (section 4.2) | `kHerdFawnReeds = false` |
| Herd in a loop with a ripe mushroom patch | never | none |
| Majors in a herd loop | at most two, and never deer_stick, plink or longshot | three allowed |
| Longshot with plink or ranger_ridge in one loop | never | allowed |
| One wind for flag, reeds, fluff, puffs | yes: `weatherBreeze(w)` and `ambientWindAt(w, x)` (section 3.1) | none |
| Grass and reeds in the mushroom sky | one field: the trip's slow wave is inside `ambientWindAt` | reeds stay on the plain wind |
| What the host carries between runs | one 16 bit word: mushroom stage, tai chi chapter, "tai chi done this morning" | nothing carried (tai chi then opens every morning start, mushrooms ripen only in one long run) |
| The 29,992 and 30,000 byte test wires on `sizeof(App)` | kept; the pieces are trimmed to fit (section 5.3) | raise them with Mike's yes |
| Pins | re-captured once, in the last step, behind a build switch (section 6.1) | per step |

---

## 3. Where the five plans disagree, and what changes in each

### 3.1 Decisions that touch more than one plan

1. **One wind.** Three plans describe the wind three ways (`reedsWind`, `weatherBreeze`, `tripSway`).
   Final:
   - `float ambientWindAt(const World& w, float x)` in `ambient.cpp` (the cattails plan's refactor of
     `updateSway`): px of tip travel on an 18 px blade at x. When `w.day.kind == DAY_TRIP` it returns
     `tripSway(w, x, plain)` (the mushroom plan's blend toward the shared slow wave). So the grass
     and the reeds lean together in the mushroom sky with no call between the two modules.
   - `float weatherBreeze(const World& w)` in `weather.cpp`: `clamp01(0.18f + 0.42f *
     (weatherSwayMul(w) - 1.f))`, the longshot plan's formula. The ribbon, the dust puffs' drift and
     the reeds' fluff speed read it. Direction is always toward -x, as the clouds and smoke go.
   - `reedsWind` stays in `reeds.h` as a thin wrapper of `ambientWindAt`. `reedsDream` is removed
     (and `Reeds::dream_q` with it).
2. **The reed clumps move for the other pieces** (cattails plan section 2.2):
   - `RS_NEAR.x = clamp(min(F.x + 176, Nx - 36), 404, 440)` with `Nx = min(mush_x - 44, 501)` (the
     mushroom plan's crouch point), and the clump is **absent** when `Nx - 36 < F.x + 166` (it cannot
     clear both the bather and the place he crouches to pick). PROPOSED: this loses RS_NEAR in
     roughly 10 to 25 % of loops (mush_x below F.x + 246). It applies in every loop, ripe patch or
     not, so the layout does not change with the mushrooms' stage. The cattails plan's own rule (40 px
     left of `mush_x`) put the clump where he crouches when the mushroom is at the left of its range.
   - `RS_UPSTREAM`: the rock rule's left fallback has floor `F.x + 254` (was 492). Left of that is
     the tai chi line (feet to F.x + 234, forward hand to F.x + 245). If the rule cannot place it at
     or right of `F.x + 254`, the clump is absent that loop.
   - No other clump moves. Checked on paper: the tai chi line (F.x + 160 to F.x + 245, feet y about
     381) has RS_NEAR in front of it across the water (clump top at y 419 or lower: never over his
     feet) and RS_UPSTREAM 17 px or more east of his farthest hand; the mushroom's wade (AT_M0 at
     Nx - 50 on the camp bank, AT_M1 at Nx) passes 20 px or more right of RS_NEAR's leaves; the
     longshot's firing point, bag and flag are 14 px or more above the bank.
3. **Memory: four bytes for the stage, not nine.** `Memory` is exactly 84 bytes today with no tail
   padding (counted from `memory.h:50-76`), so the five loose bytes the plans ask for would cost 8.
   Final, appended in this order after `visit_wait`:
   ```
   uint8_t taichi_next;   // bits 0-1: the next chapter; bit 2: tai chi was done this morning; bit 7: valid
   uint8_t herd_wait;     // midday loops before a herd may come again (deer plan)
   uint8_t mush;          // packed as Inputs carry: bits 0-2 stage, bits 3-5 left, bit 6 soak
   uint8_t s22_spare;     // zero; keeps the struct at 88 bytes on purpose
   ```
   The mushroom plan's `mush_stage`, `mush_left`, `mush_soak` become accessors in `mush_patch.h`
   (`mushStage(b)`, `mushLeft(b)`, `mushSoak(b)`, `mushPack(stage, left, soak)`). None of the four is
   printed by `memoryFormat` except ` mush=S` when S is not 0.
4. **LoopPlan::herd goes at the very end of the struct**, after `fbirds`. LoopPlan is 86 bytes of
   fields padded to 88 (counted from `spine.h:76-117`), so the byte is free there.
5. **Landscape grows 24 bytes, not 62.** The mushroom plan stored the laid-out patch (`LsPatch`, 42
   bytes). The layout is a pure function of the landscape and the stage, so only two bytes are kept:
   `uint8_t patch_stage; uint8_t patch_lifted;` (bit i: pickable i is the module's). `patchLayout(const
   Landscape&, LsPatch& out)` is called where the patch is needed (the `BL_MUSHROOM` draw, which
   first rejects any clip that does not reach row 440; `pickTufts`; the major's begin). `LsTrip`
   (`uint16_t tq[10]`, 20 bytes) stays as planned: the per-band latched clock is what keeps the sky
   pure.
6. **One carry word between screensaver runs.** `appStart` builds a new App, so Memory starts at zero
   every time the screensaver comes on (`ss_host.cpp:753`, the mushroom plan's finding, confirmed).
   That breaks two plans, not one: the mushrooms cannot ripen across runs, and tai chi would be
   "the first morning loop" at every start and lose its chapter. Final: `uint16_t Inputs::carry` and
   `uint16_t appCarry(const App*)` replace the mushroom plan's `Inputs::mush` and `appMushroom`.
   Low byte: the `mush` byte. High byte: `taichi_next`. `appStart` copies both into the new Memory.
   The host keeps one word in RAM, reads it after each loop, advances the mushroom part by the minute
   while no run is on, and clears the "done this morning" bit when the mood leaves the morning
   window (PROPOSED: when 6 hours have passed since it was stored). The preview gets `--carry N`
   (and `--mush N` as a shorthand that sets the low byte).
7. **Tai chi's first-morning rule** becomes: forced in round 0 of a morning loop when its gates
   pass and bit 2 of `taichi_next` is clear. `taichiMajorEnd` sets bit 2 when the form reached DONE;
   `spinePlan` clears it in any evening or midday loop. The chapter is bits 0-1 when bit 7 is set,
   else `(r_var >> 9) & 3`. This replaces `mem.loop_count <= 1 || mem.last_family != SF_MORNING`.
   (`last_family` does hold the previous loop's family when `majorsPlan` runs: `spinePlan` writes it
   after the family's plan, `spine.cpp:2815`. The tai chi plan had that UNVERIFIED; it is true, but
   the carry bit is the better rule because of point 6.)
8. **`sizeof(Reeds) <= 224`** (the cattails plan said 232). The shared block is 624 bytes and holds
   92 + 288 on the host with the Christmas mockups (printed by `test_xmas.exe`; 96 + 288 once the
   92 is aligned); the tai chi plan adds about 8 to `StreamVisit` inside `StreamLife`.
   96 + 296 + 224 = 616 of 624. If `Reeds` comes out larger, drop `d_seed` (recompute from age)
   first.
9. **`majorsPlan` is edited by four plans.** One block, written once by the integrator (section 7,
   step C0), replaces the four separate descriptions:
   ```
   // gates (beside the bike's line; wk as there)
   if (night || wet(wk)) base[MJ_TAICHI] = 0;
   if (night || wet(wk) || wk == WX_FOG || in.dog_out != 0) base[MJ_LONGSHOT] = 0;
   if (p.herd) base[MJ_DEER_STICK] = base[MJ_PLINK] = base[MJ_LONGSHOT] = 0;
   const bool ripe = mushWillPlay(in, mem, slots.family, night);     // mush_patch.h, pure
   ...
   int want = 2 + rng.weighted(kHowMany, 3);                         // the draw is unchanged
   if (p.herd && want > 2) want = 2;
   if (ripe) want = (in.dog_out != 0) ? 2 : 1;
   // per round k, in the weight loop:
   //   isHillside(pick[j]) && isHillside(i): wt[i] = 0   (plink, longshot, ranger_ridge: at most one)
   // after the three draws of the round:
   const bool house_dog = ...;                                       // as now
   const bool tai_first = k == 0 && slots.family == SF_MORNING && base0_taichi > 0 && !(mem.taichi_next & 4);
   const bool ripe_pick = ripe && k == (house_dog_loop ? 1 : 0);
   if (house_dog) id = MJ_FETCH; else if (tai_first) id = MJ_TAICHI; else if (ripe_pick) id = MJ_MUSHROOM;
   const bool forced = house_dog || tai_first || ripe_pick;
   if (k >= want || (sum <= 0.f && !forced)) continue;
   if (k >= 2 && !forced && wt[id] < 1e-3f) continue;
   ... slot choice as now (taichi: slot bit 1; mushroom: 2 | 4) ...
   if (id == MJ_TAICHI && want > 2) --want;                          // it counts as two
   // variant lines: taichiPlanVar, longshotPlanVar, tripPlanVar
   ```
   `base0_taichi` is tai chi's weight after the gates and before the ring's penalty (a forced pick
   ignores the ring, as the fetch does). Every round still draws `id, r_slot, r_var`. Both copies of
   the slot rules are kept in step (gotcha 8). `mushWillPlay` is `mushStage(mem.mush) == MUSH_RIPE &&
   family == SF_MIDDAY && !night && the data's weather is not rain, snow or storm`.
10. **The herd's pick knows about the mushrooms.** `herdPlanPick` makes its three draws as planned,
    then drops the herd when `mushWillPlay(...)` is true. Reason: a herd loop plus the 196 s trip is
    610 to 649 s before a single pause is longer than 8 s, and two deer stories (the herd, the
    vision deer) in one loop is one too many.
11. **`majorKept`** returns true for a forced fetch (as now), `MJ_TAICHI` and `MJ_MUSHROOM`.
    `spineFitLength`'s floor `p.n_majors > 2` becomes `p.n_majors > floor_n`, `floor_n` 1 when the
    plan holds `MJ_TAICHI` or `MJ_MUSHROOM`, else 2.
12. **`housedogs.cpp` has three editors.** One step owns it: `houseDogsLend(World&, int first =
    HD_COPPER)` (mushroom; tai chi's bump asks for `HD_COPPER` and gives back anything else),
    Lucy's settle spot from `taichiWatchSpot` (tai chi), `maybeChase`'s second source `herdGuestAt`
    (deer).
13. **`react.cpp` has three editors.** One step owns it: `reactWaveOk` false while
    `reedsPickActive(w)` or `tripHandsFull(w)`; `reactStart`: `look_only` also when
    `taichiInForm(w)`, `hands_full` also when `taichiHatInHand(w)` or `tripHandsFull(w)`. The
    longshot needs nothing here (it is `DAY_PLINK`).
14. **`critters.h/.cpp` has two editors.** The deer rig goes first and owns the file through the
    parallel phase. The red-winged blackbird's shoulder patch (`kToneRedwing`, a `CR_BIRD` tone
    bit) is added afterwards in the reeds' wiring step. Tone bits are per kind, so the deer's
    `kDeerFawn = 1`, `kDeerEarBack = 2` and the bird's `kToneRedwing = 2` do not clash.
15. **`hiker.h/.cpp` has two editors.** Both edits are small and exact (`Hiker::foot_turn` and its
    three heel and toe lines; `hikerMouthPos`), so the foundation step does both and the file is
    frozen after it.
16. **`stream.h/.cpp` has two editors.** `streamAddStick` (reeds) in step C1; the heron guest hooks
    (tai chi) in step C2, after it. The deer plan's flag on the stream's own spooked deer stays off.

### 3.2 Changes by plan

**CATTAILS_PLAN.md**
- 2.2: the RS_NEAR and RS_UPSTREAM rules of 3.1 point 2.
- 6.1: `reedsDream` removed; `reedsWind` is `ambientWindAt`. No `reedsNudge` is added for the deer
  (see the deer plan's changes).
- 6.2: the tai chi "mat" beside RS_FIRESIDE is dropped; the tai chi plan's spot east of the cairn
  stands.
- 4.4: the drop list gains `taichi` (at the major's begin) and `mushroom` (at `doLift`, the call is
  the mushroom major's). It keeps the stem through `longshot` and `plink`.
- 7: `static_assert(sizeof(Reeds) <= 224)`; `WORLD_ACTOR_MAX` is 136 in the end, not 135.
- 9.2: the hooks in `world.*`, `landscape.cpp`, `ambient.*`, `hiker.*`, `weather.cpp`, `react.cpp`,
  `app.cpp` are made by the foundation step, not by the reeds' builder. The shoulder patch waits for
  the deer rig.

**TAICHI_PLAN.md**
- 9: id 25; the first-morning rule and the chapter come from `taichi_next` as in 3.1 points 6 and 7;
  the `want` and forced-pick lines are the block in 3.1 point 9.
- 8: `Memory::taichi_next` carries three things (3.1 point 3).
- 6.1 chapter 1: the heron's spot is `x = clamp(max(x0 + 30, RS_NEAR.base.x + 30), 400, 480)` when
  RS_NEAR is present, so the heron stands beside the cattails and not inside them.
- 6.1 chapter 4: the watching deer is skipped when the loop's plan holds `deer_stick` (the plan
  said "the deer-visit major", which is not a major and not in the morning). While it watches it
  grazes between looks: `CA_EAT` (the deer rig's new action) for 3 to 5 s, head up 2 s, from its
  stage clock; head up at KICK as planned.
- 7.3: the "wobble the morning after the mushrooms" link is dropped (a midday trip and the next
  morning are 18 hours and a screensaver restart apart).
- 10: `hiker.*` is the foundation's; `react.cpp`, `housedogs.cpp`, `majors.cpp`, `spine.cpp`,
  `memory.h` edits are in the serial steps of section 7. `taichiMajorBegin` calls
  `reedsDropChew(w)`.

**DEER_PLAN.md**
- 5: gate added: no herd when `mushWillPlay` (3.1 point 10). `want` capped at 2 in a herd loop now,
  not as a fallback. `MJ_LONGSHOT` is in the zeroed line from the start.
- 7.2: `LoopPlan::herd` at the end of the struct; `Memory::herd_wait` in the block of 3.1 point 3.
- 6.6: no `reedsNudge` and no `#if SS_REEDS`. The reeds land first. Deer are director critters, so
  every clump they walk through shakes by itself (the cattails plan's brush); the two staged meetings
  are sections 4.1 and 4.2 below and use `reedsPush`, `reedsBend`, `reedsTake`.
- 6.6: `herdSpawnDeer` and `herdGrazeStep` are not exported for tai chi (its one deer uses `CA_EAT`
  directly). They stay file-local until a later piece needs them.
- 8: `critters.*` belongs to the rig builder alone through the parallel phase.

**MUSHROOM_PLAN.md**
- 8.1: one packed byte in Memory (3.1 point 3). 8.2: `LsPatch` is computed, not stored (3.1 point 5).
  8.3: `Inputs::carry` and `appCarry` (3.1 point 6). 8.5: sizeof(App) is about +36 for this piece,
  not +80.
- 6.5 and 7.5: `tripSway` is called from `ambientWindAt` only. The reeds need no call.
- 7.5: the asks of the reeds plan are met by 3.1 point 2. `doLift` calls `reedsDropChew(w)`.
- 9: the forced pick and `want` are the block in 3.1 point 9. The herd never shares a ripe loop.
- 10.2: `houseDogsLend`'s second argument, the `react.cpp` lines, `WORLD_ACTOR_MAX` and
  `DAY_PARTS` are made in the shared steps of section 7.

**LONGRANGE_PLAN.md**
- 4.2: the shared breeze is `weatherBreeze(w)` in `weather.cpp`, made by the foundation step.
- 6: the rule "if a mushroom is within 20 px of AT_M1 or AT_M2, move 24 px right" is dropped: the
  mushrooms are across the stream at y 450 and below, the bag and flag at y 366. It can never apply.
- 6: the "never with the mushrooms" rule needs no code: a ripe loop has `want` 1 (2 with a dog, and
  a dog zeroes the longshot anyway).
- 6: the stem stays in his mouth (`kLsKeepStem = true`, PROPOSED). The reeds module draws it from
  `hikerMouthPos`, which must be right for the prone pose: add prone to the mouth-stem stills.
- 8: id 26. The exclusion rule is `isHillside` in 3.1 point 9.

---

## 4. Cross-piece interactions

Two kinds: the ones designed here and built now (4.1 to 4.7), and the ones left for later (4.8).
The test for "now" was: does it cost under about 60 lines, need no new art, and make one piece
better without the other piece's review depending on it.

### 4.1 The second doe and the cattail (build now)

Mike's words give the herd "some stop to munch on grass". One of them munches something else.

Where: RS_FIRESIDE (base about (F.x + 54, top + 1), example (304, 420)), the small clump behind his
empty seat. Who: D4, the second doe, "the one who eats" in the deer plan. Only when RS_FIRESIDE is
present this loop; otherwise D4 goes to G4 as the deer plan has it.

- Her first spot becomes `N4 = (clump.base.x + 40, top(clump.base.x + 40) - 5)` (example (344, 407)):
  dry ground, 5 px above the water, facing left, nose about 32 px ahead of her feet. Her walk in is
  about 30 px longer than to G4: she arrives about 37.9 s (PROPOSED), not 36.6.
- Target stalk: the clump's first cattail that still has a head; in spring (months 3 to 5, no heads
  on the new growth) the nearest reed.

| t (s, beat clock) | D4 | the stalk |
|---|---|---|
| 37.9 | arrives, stands. 0.3 s of nothing. | sways as it was |
| 38.2 to 39.4 | `CA_REACH` (1.2 s blend): neck out toward the head, weight back | |
| 39.0 to 39.4 | | bends to meet her: `reedsBend(clump, stalk, critterDeerNose(d) - reedsTip(...))`, eased in over 0.4 s, capped at 6 px |
| 39.4 to 40.1 | lips on it: a hold. One ear flick at 39.8. | held |
| 40.1 to 40.4 | a small tug (`head_add` -0.05 and back) | follows 3 px, comes back |
| 40.7 to 41.0 | a harder tug | stretches 5 px; at 41.0 `reedsTake(clump, stalk, true)`: the head is in her mouth, the stub springs and rings; `reedsPush(clump, +1, 0.3)`: the neighbours shiver |
| 41.0 to 43.4 | head up level, chewing (the graze loop's jaw at head height), looking at nothing | ringing down |
| 43.4 on | head down to the grass: she joins the graze cycle. Her "one step forward" moves are aimed at G4, so she drifts up to her place in the picture. | a headless stem for the rest of the loop |

Nuance: the stalk gives before it lets go, as it does for the hiker. She does not look round after.
It is over 0.4 s after the yearling starts staring at the tent (43.0), so it never competes with the
tent business. The hiker's seated pick later in the loop still has both reeds. No sign is added to
the return scripts for it: the bitten stem is simply there.

Checks: at N4 her bounds are x 312 to 376, y 359 to 407, clear of the fawn at Gf by 14 px or more;
the settled picture's 2 px rule is tested with her wherever her steps have taken her by 72 s (if a
seed fails it, her step toward G4 is lengthened, not the rule loosened). Strip 12 (rows 384 to 415)
is busy with the stream already.

### 4.2 The fawn pushes into the reeds (build now)

The deer plan's third dash (66.0 s, 18 px, ending in a stumble) is retargeted when RS_UPSTREAM is
present and its base is within 90 px east of the fawn's water point S:

- the dash ends at `x = clump.base.x - clump.half_w - 14` (the fawn's nose about 6 px into the
  leaves), at 70 px/s. From S.x - 8 that is about 42 to 50 px: about 0.65 s instead of 0.26 s.
- on arrival: `reedsPush(clump, +1, 0.8)` (the whole clump leans away and rings three or four
  times), the planned stumble (crouch 0.5 for 0.3 s), and its head comes up among the blades. Two
  tail flicks, as planned, with the cattail heads still swinging over it.
- at 68.6, as it turns to climb out: `reedsPush(clump, -1, 0.4)`.

Nothing else in the deer plan's timing moves (the stand in the water absorbs the 0.4 s). If the
clump is absent or too far, the plan's own 18 px dash plays.

For free, with no code: every deer that walks through a clump's footprint shakes it (the herd
leaving to the right for a dog runs through RS_FAR at x 770; the doe's way out to the left passes
RS_FIRESIDE's leaves when her lane is within 8 px of its base).

### 4.3 Tai chi beside the reeds, and the animals drawn to it (build now)

- The practice line stays where the tai chi plan put it. RS_NEAR stands across the water in front of
  him (when present): he practises behind a fringe of cattails with the stream between. Nothing of
  his is ever hidden by a stalk (3.1 point 2).
- The heron of chapter 1 stands 30 px or more right of RS_NEAR's base (3.2), so the pair reads as
  "heron beside the cattails, man beyond the water".
- The four guests are the tai chi plan's. One change: the chapter 4 deer grazes between looks
  (3.2), which makes it a deer at the tree line and not a statue.
- The reeds' own life goes on round him: the dragonfly by day in the warm months, fluff in autumn
  drifting through the closing's stillness. The pick overlay cannot start (it is not a pause).
- The blackbird minor cannot start during the major (minors wait); if one is on a stalk when the
  major's first still begins it leaves by its own quit rule.

### 4.4 Which plays keep the stem in his mouth

The cattails plan's list (4.4 there) stands for the existing beats. For the new pieces:

| Play | Stem | How |
|---|---|---|
| `taichi` | falls at begin | `taichiMajorBegin` calls `reedsDropChew`. Rare: the pick cannot start before 60 s of story and tai chi begins at about 55 to 70 s |
| `longshot` | stays, prone too | nothing; `kLsKeepStem`. Stills must show it does not cross the scope |
| `plink` | stays | nothing (not edited) |
| `mushroom` | falls as he crouches at the patch | `doLift` calls `reedsDropChew` |
| the herd's `return` | he has none (he was away; the stem is gone when he leaves the scene) | nothing |
| the doorbell wave, the passer-by wave | stays | nothing |

### 4.5 Mushrooms and deer (build now: only the rules)

- No herd in a loop that will play the mushroom major (3.1 point 10).
- The vision deer of set 0 is a plain `CR_DEER` (no antlers exist in the deer rig; the tree antlers
  are the trip's own). It uses none of the herd's code.
- The herd never crosses the stream; the patch is on the other bank and is not touched.

### 4.6 Long-range and far animals (build now: what the longshot plan has)

- The far birds that lift from the tree line at the first hit, and the camp startle on the first
  shot, are the longshot plan's and stay.
- No herd, `deer_stick` or longshot in one loop (3.1 point 9), so nobody shoots in a loop the deer
  walked through, and no deer comes back to a camp that was just a firing line.
- The ribbon, the reeds, the fluff and a wind miss's dust all lean and drift the same way and by
  the same strength (3.1 point 1).

### 4.7 House dogs, weather, the doorbell and the chip with each piece

| | Lucy and Copper | Weather | Doorbell | Status chip box |
|---|---|---|---|---|
| Reeds | brush a clump as they pass; the blackbird leaves when a dog runs near; no pick while he pats a dog | sway with the weather's wind and gusts; snow caps; frost tint; no pick in rain, snow, storm | plants and small life run on; the pick's and the chew's clocks stand still; he waves with the stem | no clump right of x 462 below y 412 |
| Tai chi | plays with dogs out; no wild guests then; Lucy lies at her watch spot; Copper may bump him | inside the morning fog lift; weight 0 in data rain, snow, storm; no-show for a random one | look-only: he holds the form, the head lifts | nothing below y 386 right of x 538 |
| Herd | no herd with a dog out at loop start; a dog going out is the hard alarm, they flee right, the dog's own chase plays | no herd in data rain or storm; a gust is the tent's twitch; lightning is a hard alarm | hard alarm, they flee left (he is away, so no freeze) | standing deer at x 512 or less |
| Mushroom | never skipped; Lucy (or Copper alone) lies by his legs; with a dog out the fetch plays first | data rain or storm grows the patch; the major only in dry data weather; no-show for a random rain | the sky clock and the band repaints stand still | he reaches x 532 at most; nothing moves in the box |
| Longshot | weight 0 with a dog out; a dog going out mid-major waits (`plinkHoldsDogs`) | weight 0 at night, in data rain, snow, storm, fog; wind picks variant B | look-only with the rifle out | nothing near it |

### 4.8 Pieces that must never share a loop (all enforced at plan time)

| Pair | Rule | Where |
|---|---|---|
| herd with deer_stick, plink, longshot | weights zeroed when `p.herd` | `majorsPlan` |
| herd with the mushroom major | herd dropped when `mushWillPlay` | `herdPlanPick` |
| herd with a house dog, data rain or storm, or the herd in the last midday loop | herd dropped | `herdPlanPick` |
| longshot, plink, ranger_ridge | at most one of the three | `majorsPlan` (`isHillside`) |
| longshot with a house dog | weight 0 | `majorsPlan` |
| mushroom with anything but a forced fetch | `want` is 1 (2 with a dog) | `majorsPlan` |
| tai chi with more than two other majors | it counts as two | `majorsPlan` |
| tai chi's deer with deer_stick | the guest is skipped | `taichiMajorBegin` |
| tai chi's heron with the stream's deer as this loop's visitor | no heron (one visitor a loop) | tai chi plan |

Families keep the rest apart by themselves: tai chi is the morning's; herd, longshot and mushroom
the midday's; the evening's pool gains nothing but the reeds.

### 4.9 Left for later (offered to Mike, not built in this stage)

1. He walks back from the day hike already chewing a stem (`reedsGiveChew` at the midday `return`,
   1 loop in 3). One line, but it skips the pick Mike asked to see.
2. A reed after tai chi: the outro detours by RS_UPSTREAM for a pick. The coffee beat comes straight
   after and drops it, so it would last under 20 s.
3. A doe and her fawn at the tree line for tai chi's chapter 4, the fawn stepping when he steps.
4. Two tiny deer walking back onto the far ridge during the longshot's last look at the hills (a new
   6 px shape; "the range is cold").
5. The fawn looking across the water at the mushroom patch; a deer leaving a nibbled companion.
6. A dog sniffing at a clump (`HA_ROAM` to `reedsStandPoint`): a new dog activity shifts each dog's
   random stream and its tests.
7. The doe's head at the left edge in the last seconds of the morning stay's day hike.
8. Ann following a tai chi chapter 1.2 s late (the visiting couple).
9. The cloud hands (tai chi chapter 3, keys 3 to 8) done under the mushroom sky.
10. `reedsBlast` on RS_FORE from the plinking's muzzle (the plinking is approved and not edited).
11. The white flag on the stream's own spooked deer (changes an approved vignette).
12. A real wind speed from the house (`Inputs::wind` from WeatherGate).

---

## 5. Budgets across the five

### 5.1 The day union

`DayState`'s union is as big as `DayBike`: 1,904 bytes (the last `build-plink-on` test log). Each
new member is asserted against it: `DayTaichi` about 132 bytes, `DayHerd` about 416, `DayTrip` about
300, `DayPlink` 848 today and about 1,010 with `FarState` (about 160; the assert decides). The union
does not grow. The herd holds `DAY_HERD` from its beat into the `return` beat (the hoof prints) and
zeroes the union itself when the prints have faded. If one of the day plays (kite, bike, archery,
slackline, ropes, and the new ones) begins first, its begin hook clears the union with a `memset`
(`day_plink.cpp:486` is the pattern) and `dayTrack` marks the old kind's bounds, so the prints then
go at once (cannot happen before 43 s after the herd, per the deer plan). A major that is not a day
play does not touch the union: the prints fade on their own clock under it. `herdStep` must
therefore never assume `w.major.on` is false.

### 5.2 Memory, LoopPlan, Inputs

Section 3.1 points 3, 4, 6. Memory 84 to 88 bytes. LoopPlan stays 88. Inputs gains a `uint16_t`
(0 to 4 bytes after padding; App keeps a copy, so 0 to 8 in all).

### 5.3 sizeof(App)

Measured today (both the cattails and mushroom plans read it from `build-plink-on/test_scene.exe`):
29,856 bytes on the host with the Christmas set; about 28,660 on the panel (UNVERIFIED, from the
plans). Limits: 32,768 (`SS_APP_BYTES`); the host test wires 29,992 (`test_xmas.cpp:352`) and 30,000
(`test_day_majors2.cpp:664`, `test_day_plink.cpp:614`).

| Source | Bytes | Why |
|---|---|---|
| `WORLD_ACTOR_MAX` 130 to 136 | +24 | `ActorRef` is 4 bytes (`world.h:85`) |
| `Hiker::foot_turn` | +4 | one Hiker in World (`world.h:245`); the visitors' rigs are inside a union |
| Memory | +4 | 3.1 point 3 |
| Landscape (`patch_stage`, `patch_lifted`, `LsTrip`) | +24 | 22 padded |
| Inputs carry (two copies) | +0 to +8 | padding decides |
| LoopPlan::herd | +0 | tail padding |
| Reeds, StreamVisit | +0 | inside the Lookout's shared block |
| Day union members | +0 | inside the bike's place |
| **Total** | **+56 to +64** | host: about 29,920 of 29,992; panel: about 28,720 of 32,768 |

As the five plans had it the total was about +108 to +124 (29,964 to 29,980), with under 30 bytes
left under the first wire. The trims in 3.1 buy back about 60. The wires are not raised. Every step
in section 7 prints `sizeof(App)`; if a step would cross 29,992 it stops and reports.

### 5.4 Pools

| Pool | Cap | Use after stage 22 |
|---|---|---|
| Major ids | 32 | 28 |
| Palette slots | 32 | 32 (unchanged: no piece adds a slot; all new colours are `mixRGB` of slots at draw time. The red ribbon and the blackbird's patch use `MUSHROOM`; the trip's pastels are scaled by the palette's own `SKY2`) |
| Critter slots | `CRITTER_MAX` 11 (`world.h:110`); the director's story slots are 8 per HOW_TO_ADD_SCENES (the split is UNVERIFIED) | herd 4 to 6 for at most 95 s with nothing else in the scene; tai chi 1 (+ Copper lent); trip 2 at most (+ a dog lent); reeds' blackbird 1 (minor); longshot's startle up to 3 |
| Glyphs | 28 | longshot 12 at its peak, 4 kept for the rest of a midday loop |
| Bits | 6 | herd at most 4; trip rings 1 at a time |
| Actors | 136 | worst case rises by exactly the 6 added |
| Timeline beats | 64 | midday +1 (`herd`) |
| Minors | none | 12 |
| Plan line | 320 bytes | + ` herd=N` (7) and longer names; worst case about 225 |

### 5.5 Loop-length fit when long plays meet

The fit aims at 490 to 650 s and allows 480 to 660 (`spineFitLength`). Majors cost their seconds
plus two stills of 4 to 8 s.

| Loop | Worst case on paper | Outcome |
|---|---|---|
| Morning, camp stays, tai chi (153 + 12 gag) + one other | 13 + 357 + 48 + 173 = 591; a second major of up to 76 passes 650 | floor 1: the other major is dropped, 591 s |
| Morning, break camp, tai chi | 301 + 151 to 173 | pauses stretch; fillers stay |
| Midday, herd (104 + 16) + two majors (ropes 90, slackline 80) | 13 + 120 + 272 + 40 + 98 + 88 = 631 | fits; the cap at two majors is what makes it fit |
| Midday, longshot 119 + ropes 90 + slackline 80 | three majors: 13 + 272 + 40 + 127 + 98 + 88 = 638 | fits with pauses at 8 s; otherwise the fit drops one as it does today |
| Midday, mushroom 196 alone | 13 + 233 to 272 + 40 + 204 to 212 = 490 to 537 | fits; fillers kept up to 650 |
| Midday, mushroom + forced fetch | about 600 | fits |
| Midday, herd + mushroom | 610 to 649 with no room | forbidden (3.1 point 10) |
| Midday, herd + longshot or plink | | forbidden |

Each of these is a test row (section 8).

### 5.6 Weights and how often each piece shows

Midday pool today, weights of the majors that may play there: 18.7 in all (eight at 1.0, fishing
0.6 + 0.6, bathe 1.5, fetch 1.5, kite, boomerang, bike, archery, slackline at 1.0, ropes 0.8, plink
0.7; counted from `majors.cpp:3687-3736`). Morning pool: 9.8.

| Piece | Setting (PROPOSED) | Expected rate |
|---|---|---|
| Reeds | always there, every camp family | every camp loop |
| The pick | 45 % of camp loops want it; needs a free pause | about 1 loop in 3 |
| `reed_bird` | minor weight 0.8 of 13.1, by day, March to October | about 1 minor in 16; roughly once every two or three daytime loops |
| `taichi` | forced once a morning (carry bit), then weight 1.6 with the ring's penalty | the day's first morning loop, then about 1 morning loop in 4 |
| `herd` | 30 % of midday loops that pass the gates, never two running | 0.30 / 1.30 = 23 % of midday loops |
| `longshot` | weight 0.6 (plink is 0.7) | about 7 to 9 % of dry, dog-free midday loops (2.9 picks x 0.6 / 19.3, less the fit's drops) |
| `mushroom` | forced in the first dry midday loop with a ripe patch | once per rain |

What the old majors lose (estimates; the 600-loop count in step C7 measures it):
- Evening: nothing. No new id has the evening's family bit, and `Rng::weighted` skips zero weights
  and draws the same single value (`rng.cpp:41-55`), so every evening plan is what it was.
- Morning: a tai chi loop has 1 or 2 other majors where it had 2 to 4. At 30 % of morning loops
  that is about 13 % fewer plays of the old morning majors.
- Midday: longshot takes about 3 % of the picks; a herd loop is capped at two majors (about one
  fewer in 23 % of loops, 7 to 8 %). About 10 % fewer plays of the old midday majors. `test_dog`
  (fetch in 35 of 150) and `test_memory_sets` are the tripwires; if either fails, lower
  `kLongshotW` to 0.5 first, then `kHerdShare` to 0.25.

Plays per day by family. ASSUMPTION, UNVERIFIED: a loop is about 9.5 minutes; the screensaver runs
the whole window; in mid November the morning window (moods 0.5 to 2.5: dawn and morning phases) is
about 3 hours and the midday window (the day phase) about 5 hours. The real lengths come from the
panel's sky phases, which I did not read.

| Family | Loops a day | New plays a day |
|---|---|---|
| Morning | about 19 | tai chi 5 or 6 sessions (all four chapters once, and one or two again); the pick 6 |
| Midday | about 32 | herd 7; longshot 2 or 3; mushroom 0, or 1 on the day after rain; the pick 10 |
| Evening and night | the rest | reeds only: the pick in about a third of loops, fireflies at dusk |

Someone glancing at the panel at a random midday moment sees deer in camp about 4 % of the time.

---

## 6. How the build is kept safe

### 6.1 One build switch for the plans, none for the layout

A new header `core/scene/stage22_switch.h` (the `xmas_switch.h` pattern): `#ifndef SS_S22` /
`#define SS_S22 0`, and `int ssS22Switch()`. CMake gets `option(SS_S22 ...)`. The switch gates only
what changes a plan:

- off: `kTaichiW`, `kLongshotW` are 0 and the tai chi's forced pick is off; `kHerdShare` is 0;
  `mushWillPlay` is false.
- on: the values of 5.6.

It does not gate any struct, enum, hook or the reeds (which never touch a plan), so the World's
layout is the same either way and a hand-built tool cannot mismatch. Two host folders through the
build: `build-s22-off` (every existing suite must pass with the old pins untouched: that is the
proof that nothing else moved) and `build-s22-on` (the pieces' own tests). The pinned-line checks in
`test_spines.cpp`, `test_midday.cpp` and `test_morning_stay.cpp` are wrapped in `if
(ss::ssS22Switch() == 0)` by the foundation step. In the last step the header's default becomes 1
(the panel compiles the core with no defines), the pins are re-captured once, the wrap is removed.

### 6.2 Hooks first

The foundation step makes every edit to a shared file that is only a declaration, a member, an
enum entry or a call into a module, with the modules' functions as empty stubs in skeleton files.
After it, a parallel builder fills in only files it owns. Edits to shared files that are real logic
(the plan block, the fit's floor, `drawSky`, the midday timeline, the heron, the dogs) are the serial
steps.

### 6.3 Baseline

Before the foundation, from the tree as it is, into `docs/screensaver/reviews/stage22-baseline/`:
`--loops 10` lines for seeds 1 and 7 in each family; frame hashes at 20 ticks for seeds 1, 7, 12 in
each family; strips per frame and ms per frame over whole loops; `sizeof(App)`, World, StreamLife,
Landscape, LookoutState, XmasState; a hash of `Ambient::sway[]` over 2,000 ticks of seed 1; the
plinking's module state hash (the longshot plan's guard); a deer drawn in every existing action
(hashes); the minors log of seeds 1 to 12. Every later "unchanged" claim is checked against these.

---

## 7. Build order

Models: Fable for the creative gates (reading stills, adjusting choreography, the review notes to
Mike). Opus for the foundation, the modules where geometry must match bounds, and every serial
step. Sonnet for well-briefed modules, tests and stills tools. Haiku for sheets scripts, list edits
and log entries. One integrator (Opus) holds the shared files from A1 to the end.

### Phase A, serial (integrator)

**A0. Baseline** (6.3). Files: none in the tree but the review folder. Done: the numbers are written
down.

**A1. Foundation.** Existing files and what goes in each:

| File (line endings kept) | Edit |
|---|---|
| `stage22_switch.h` (new), `CMakeLists.txt` | 6.1 |
| `majors.h` (CRLF) | `MJ_TAICHI`, `MJ_LONGSHOT`, `MJ_MUSHROOM`; `SH_GONG`, `SH_FLAG` |
| `majors.cpp` | three pool rows (weights by the switch; begin and tick are the skeletons' stubs); `static_assert(MJ_COUNT == 28)`; `scriptOf`, `majorSeconds`, `majorEnd`, `shapeReach`, `drawShape` lines calling the stubs. `majorsPlan` is not touched yet |
| `day.h`, `day.cpp` | `DAY_TAICHI`, `DAY_HERD`, `DAY_TRIP`; the three union members and asserts; `FarState` in `DayPlink`; `DAY_PARTS 3`; a case per kind in `sizeOf`, `dayBounds`, `dayPose`, `dayHoldsFacing`, `dayStep`, `dayTrack`, `dayPartY`, `dayPartDraw`, each calling a module function |
| `memory.h`, `memory.cpp` | the four bytes; `memoryFormat`'s ` mush=`; `memoryEndLoop` calls `mushAfterLoop` (stub returns its input) |
| `spine.h` | `LoopPlan::herd` at the end |
| `ss_app.h` (CRLF), `app.cpp` (CRLF) | `Inputs::carry`, `appCarry`; `appStart` seeds Memory from it; `herdBell(a->world)` in the doorbell loop; `reedsSetInputs` beside `streamSetInputs` |
| `world.h`, `world.cpp` | `ACT_REED`, `CT_HERD`, `WORLD_ACTOR_MAX 136`, `Reeds reeds` and the asserts; `reedsInit`, `reedsStep`, `reedsStoryStep`, `reedsTrack`, `reedsApplyPose`, the `ACT_REED` sort and draw, `reedsDrawMouth`, `reedsDrawAir`, `coverHiker`'s union; `patchLayout`'s stage copy after `landscapeGenerate` |
| `landscape.h` (CRLF), `landscape.cpp` (CRLF) | `patch_stage`, `patch_lifted`, `LsTrip`; `reedsDrawStatic` in `BL_TUFTS`; `patchDrawStatic` in `BL_MUSHROOM`. `drawSky` and `drawRidge` are not touched yet |
| `ambient.h`, `ambient.cpp` (CRLF) | `ambientWindAt` (with the `DAY_TRIP` branch calling `tripSway`), `updateSway` through it; `ambientDrawGlints` through `tripGlintColor`; `pickTufts` keeps 12 px from patch mushrooms. The sway hash must equal the baseline |
| `weather.cpp` (CRLF), `weather.h` | `weatherBreeze`; `hikerFree` gets `&& !reedsPickActive(w)` |
| `hiker.h`, `hiker.cpp` (CRLF) | `foot_turn` and its three heel and toe lines; `hikerMouthPos` |
| `react.cpp` | 3.1 point 13 |
| `housedogs.h`, `housedogs.cpp` | `houseDogsLend`'s second argument only |
| `sim/preview_common.cpp` | `--carry N`, `--mush N` |
| `tests/test_spines.cpp`, `test_midday.cpp` (CRLF), `test_morning_stay.cpp` | the three names in each `isMajor` list; the pin wrap of 6.1 |
| Skeleton files (new, stubs only) | `reeds.h/.cpp`, `taichi.h/.cpp`, `day_taichi.h/.cpp`, `herd.h/.cpp`, `day_herd.h/.cpp`, `mush_patch.h`, `patch.h/.cpp`, `skytrip.h/.cpp`, `trip.h/.cpp`, `day_trip.h/.cpp`, `day_longshot.h/.cpp`: the state structs from the plans (with this file's changes) and every function a shared file calls, returning nothing |

Done looks like: both build folders build with `-Werror`; in `build-s22-off` and `build-s22-on` every
existing suite passes; the `--loops` lines, frame hashes, sway hash, plinking hash and minors log
equal the baseline; `sizeof(App)` is about 29,920 on the host and is written in the WORKLOG;
`ss_soak` clean. Not committed until Mike asks.

### Phase B, parallel (each builder touches only the files listed)

| Step | Owner | Files | Done looks like | Suites |
|---|---|---|---|---|
| B1 Reeds, the plants | Opus | `reeds.h/.cpp`, `tests/test_reeds.cpp`, `tools/reeds_stills.cpp`, `tools/reeds_sheet.py` | cattails plan build step 1 with this file's site rules: sites, geometry, wind, seasons, snow, the actor, the fringe, push, bend, take, blast, the brush. Stills 1 to 5 and 8 of that plan | `test_reeds` (sites, bounds, dirty, interface, budgets); all existing suites in both folders; hashes differ from baseline only by the reeds' pixels |
| B2 Tai chi, the module | Opus | `taichi.h/.cpp`, `tests/test_taichi.cpp`, `tools/taichi_stills.cpp`, `tools/taichi_sheet.py` | tai chi plan 10.1: the four chapter tables, IK, lags, gags, the butterfly; posture sheet S1, filmstrips S2 on flat ground, S4, S6 | `test_taichi` (all nine checks), `test_hiker` |
| B3 Deer rig, then the herd's pure part | Sonnet (rig reviewed by Opus) | `critters.h/.cpp`, `tests/test_critters.cpp`; then `herd.h/.cpp`, `tests/test_herd.cpp`, `tools/herd_stills.cpp` (pose sheet only), `tools/herd_sheet.py` | deer plan 7.1 and the standalone part of 8: graze, stretch, shake, the flag, the fawn, `critterDeerNose`; cue tables, graze cycle, the wave, prints. Pose sheet (deer plan still 1) | `test_critters` (existing deer hashes equal the baseline), `test_herd` |
| B4 Mushroom, the patch and the sky | Sonnet (patch), Opus (sky) | `mush_patch.h`, `patch.h/.cpp`, `skytrip.h/.cpp`, `tests/test_mush_patch.cpp`, `tests/test_skytrip.cpp`, `tools/trip_stills.cpp` (patch and sky parts), `tools/trip_sheet.py` | the growth rule on the packed byte; `patchLayout` pure; the six mushrooms at every stage; `skyTripBand` drawn into a surface by the tool (not yet through `drawSky`). Mushroom plan stills 1, 2 (patch) and 3, 4 (sky, tool-driven) | `test_mush_patch` (rule, layout, draw), `test_skytrip` (edges, no flashing, night rule, speeds); with `carry` 0 every hash equals B1's |
| B5 Longshot, the rifle and the targets | Opus | `plink.h/.cpp`, `day_plink.h/.cpp`, `day_longshot.h/.cpp` (`longshotTargets`, the shapes), `tests/test_longshot.cpp` (targets and module parts), `tools/longshot_stills.cpp`, `tools/longshot_sheet.py` | longshot plan section 7 fields, `plinkFar`, stay, prone bend, scope, big sack; the five acts made public; targets over 1,200 seeds. Stills 1, 2 (the aim line), 4, 9 | `test_plink`, `test_day_plink` unchanged; the plinking hash equals the baseline; `test_longshot` targets and module checks |

Each builder reports: object sizes of its files, `sizeof` of its state, primitives, and what it
could not verify.

**Gate G1 (Fable, then Mike).** Five sheets go to Mike before any serial step past C1: the scene
with reeds (before and after, round the fire); the tai chi posture sheet; the deer pose sheet with
the flag and the fawn; the sky alone at ten moments and the 0.4 s strip; the targets with the aim
line and the prone pose. Fable reads them first against each plan's "what keeps it from looking
stiff" list and writes the changes; the builders of B1 to B5 apply them in their own files.

### Phase C, serial (integrator holds the shared files; one step at a time)

**C0. The plan block.** `majors.cpp` `majorsPlan` (3.1 point 9), `majorKept`; `spine.cpp`
`spineFitLength` (`floor_n`); `spine.cpp` `spinePlan` (clear the tai chi morning bit outside the
morning). Done: with the switch off, the baseline `--loops` lines; with it on, a 400-seed run per
family prints the rates of 5.6 and every loop is 480 to 660 s. Suites: all, both folders.

**C1. Reeds, the rest.** `reeds.cpp` (the pick, both forms; the stem; dragonfly, fireflies, fluff);
`minors.h/.cpp` (`MN_REED_BIRD`, gate, start, step, the `reedsPickActive` wait); `stream.h/.cpp`
(`streamAddStick`); `critters.h/.cpp` (the shoulder patch, after B3); `tests/test_scene.cpp`
(`kMinors[12]`, `fired_by[12]`); `test_reeds.cpp` (pick, stem, exclusion, small life, bird).
Done: cattails plan stills 6, 7, 9, 10, with the stem prone added once C3 exists. The minors' stream
shifts for every seed here, once: say so in the notes. Suites: all; `test_stream`, `test_react`,
`test_weather`, `test_house_dogs` watched.

**C2. Tai chi, the major.** `day_taichi.cpp` (script, acts, guests, gags, `reedsDropChew`, the
heron's shifted spot, the grazing deer); `stream.h/.cpp` (heron guest hooks); `housedogs.cpp`
(Lucy's watch spot); `tests/test_day_taichi.cpp`. Done: the tai chi plan's stills S3, S5, S7 to
S11; test 11.2 all eight; a morning's worth of loops with `--carry` shows chapters 1, 2, 3, 4 in
order and one forced session per morning. Suites: all; morning pins are off behind the switch.

**C3. Longshot, the major.** `day_longshot.cpp` (script, strings, results, no-shows);
`day_plink.cpp` (`plinkHoldsDogs`); `test_longshot.cpp` (plan, variants, story, safety, dogs,
doorbell, weather). Done: longshot stills 3, 5 to 8. Suites: all; `test_dog`, `test_memory_sets`,
`test_day_majors` watched.

**C4. The herd.** `day_herd.cpp`; `spine_midday.cpp` (CRLF: `middayPlan` pick, `busySeconds`,
`beatSeconds`, `middayBuildTimeline`, `middayFormatPlan`, the two return macros); `housedogs.cpp`
(`maybeChase`); `tests/test_day_herd.cpp`; `tools/herd_stills.cpp` (the rest). Sections 4.1 and 4.2
of this file are part of this step. `tools/review_clips.py` and `sim/soak_main.cpp` only if the new
beat name breaks them. Done: deer stills 2 to 11, plus the doe at the cattail (4 frames) and the
fawn in the reeds (3 frames). Suites: all; a midday loop without a herd has the baseline's line.

**C5. Mushroom.** In three sub-steps, each shippable:
- C5a the patch goes live: `memory.cpp` (`mushAfterLoop` real), nothing else. Done: data rain for 2
  loops then 7 clear loops shows `mush=` climb to 6.
- C5b the sky through the cache: `landscape.cpp` `drawSky`, `drawRidge`; `day.cpp` `dayTrack`'s band
  repaint; a test hook that runs the sky with no story. Done: `test_skytrip` purity (ten bands one by
  one equal one full static draw); strips and ms a frame for the sky alone. **Panel check here if
  Mike wants one before the story is written** (section 9, question 5).
- C5c the major: `trip.h/.cpp`, `day_trip.cpp`, `tests/test_day_trip.cpp`, the rest of the stills
  tool, `tools/review_clips.py`'s cut. Set 0 and the dog first, then sets 1 and 2 and the pocket
  ending.
Suites: all; `test_weather` watched (a patch may now grow in its runs: understand any moved line).

**C6. Cross-piece checks.** No new feature; one test file, `tests/test_stage22.cpp` (section 8).
Fixes go to the owning step's files.

**C7. Go live.** `stage22_switch.h` default 1; the pin wrap removed; pins re-captured once
(`test_spines.cpp` morning lines; `test_midday.cpp` morning and midday lines; `test_morning_stay.cpp`
if it pins), each difference read before it is accepted; the evening pins must be byte for byte
what they were. A 600-loop count per family (60 seeds of ten, memory kept) of every major, written
in the WORKLOG beside the last such count. `HOW_TO_ADD_SCENES.md` gets the module-driven pose, the
carry word, the switch and the new pools. `NOTES_FOR_MIKE.md` and `WORKLOG.md` entries (Haiku).

**C8. Firmware, only on Mike's go.** `firmware/tcomm/src/ui/ss_host.cpp` (the carry word, the minute
tick, `ss carry N` on serial), `tcomm.ino` if the command lives there. Built and flashed only by
`bash tools/fw.sh` from bash, after Mike taps Settings, Update over Wi-Fi, BOTH PANELS. Report flash,
PSRAM, RAM, draw time, storage. Until this step the pieces all work inside one run.

Parallel work possible inside phase C: stills tools and test files of the next step can be written
(new files only) while the integrator is in the current one.

---

## 8. Tests that belong to the integration (`tests/test_stage22.cpp`)

1. **Layout**: `sizeof(App) <= 29992`; `sizeof(Memory) == 88`; `sizeof(LoopPlan) == 88`;
   `sizeof(Reeds) <= 224`; each day member fits `DayBike`; `MJ_COUNT == 28`; printed, with the
   shared block's use.
2. **Evening untouched**: `--loops 10` for seeds 1 and 7 at mood 4 and at night equal the baseline
   lines, switch on.
3. **Never together**, 400 midday seeds of 6 loops, switch on, with and without `dog_out`, with
   data weather clear, wind, fog, rain: no loop holds a pair from the table in 4.8; every herd loop
   has at most two majors; every loop is 480 to 660 s.
4. **Ripe loop**: `--carry` ripe, midday, clear: `majors=mushroom` alone (with a dog: `fetch` and
   `mushroom`), no ` herd=`; the next loop's memory shows `mush=7`.
5. **Tai chi through restarts**: three runs started in the morning with the carry word passed from
   one to the next: tai chi is forced in the first run's first loop only; chapters follow on; a
   midday loop clears the bit.
6. **Site rules**, seeds 1 to 300: RS_NEAR is absent or at least 36 px left of Nx and 40 px right
   of the bather's seat; RS_UPSTREAM is absent or at `F.x + 254` or beyond; no clump's leaves are
   within 12 px of any point of the tai chi line, the mushroom's AT_M0, AT_M1, AT_M3, the
   longshot's AT_M0 to AT_M2, or the deer's spots other than N4.
7. **The doe and the cattail**: in a herd loop with RS_FIRESIDE, the cattail's head is gone after
   41 s and D4's nose was within 3 px of the stalk's tip from 39.4 to 41.0 s; without RS_FIRESIDE
   D4 is at G4; the settled picture's 2 px rule holds in both.
8. **The fawn and the reeds**: with RS_UPSTREAM within 90 px the clump's spring is moving between
   66.6 and 69.5 s and at rest by 72 s; the fawn's bounds never pass the clump's base.
9. **One wind**: with wind weather forced, at 50 ticks the sign of `ambientWindAt` at a clump, the
   reeds' quantized lean, the flag's ribbon angle and a puff's drift agree; during a trip the reeds'
   lean follows `tripSway`.
10. **The stem**: forced into his mouth before each new major: it is gone by the tai chi's first
    tick and by `doLift`; it is still there at the longshot's last tick; the mouth point is within
    2 px of the drawn face's edge in the prone pose.
11. **Guests and clumps**: the tai chi heron stands 30 px or more right of RS_NEAR's base.
12. **Load**: the loop with the most at once (a midday herd of six in wind with snow caps on five
   clumps; the trip's antlers at their peak with a dog beside him): primitives printed; strips
   averaged over the beat under the beat's limit; incremental equals full at five ticks.
13. **Determinism**: two runs of a 10-loop mixed-family day with data weather switching (clear, rain,
   clear) hash the same; SS_XMAS on and off; `ss_soak` clean.

Stills for this file's own content go with step C4 (4.1, 4.2) and C2 (the heron beside RS_NEAR).

---

## 9. Capacity forecast

Measured headroom (2026-11-18): flash about 745 KB free; PSRAM 646 KB free with the screensaver
running; `sizeof(App)` 29,856 host, about 28,660 panel, of 32,768. All forecasts are from the five
plans' own estimates (host object sizes at -Os as the proxy) and are UNVERIFIED until built, and
on the panel.

| | Reeds | Tai chi | Herd | Mushroom | Longshot | Shared edits | Total | After |
|---|---|---|---|---|---|---|---|---|
| Code, KB (flash, and again in PSRAM) | 20 to 24 | 12 to 18 | 18 to 24 | 35 to 45 | 13 to 16 | about 3 | 101 to 130 | flash 615 to 644 KB free; PSRAM 516 to 545 KB free |
| Const tables, KB | under 0.5 | 1.4 | about 1 | under 0.5 | under 0.5 | | about 3.5 | in the code figure |
| `sizeof(App)`, bytes | +20 | +4 (+2 Memory) | +4 (actor, Memory share) | +28 to +36 | 0 | | +56 to +64 | host about 29,920; panel about 28,720 |
| Strips a frame | +0.1 to +0.3, every camp loop | none new (he is marked every tick) | 6 to 8 over its beat (loop average is 7.6) | sky: +1 full strip and one cache band a tick for 104 s | +0.25 over its beat | | | loop averages stay under the test limits (9 whole loop, 10 a major) |
| Primitives a frame | +31 always, +1 stem | 12 for him (4 fewer than usual) + 7 to 9 guest | up to 46 with six deer | up to 24 + a deer's 7 | under 40 | | | steady scene about +32; aim stays under about 150 except the herd of six on a busy frame (measure) |
| Draw time on the panel | about +3 ms a frame, always | none new in kind | about that of the hiker times five, he is not drawn | +10 to 13 ms a frame for 104 s (17 to 20 fps) | small | | | the reeds' 3 ms is the only permanent cost |
| Palette rebuilds | 0 | 0 (the fog's own steps, spread longer) | 0 | 0 | 0 | | 0 | |
| Static cache | the fringe, once per rebuild | none | none | the patch once per rebuild; 2,600 band repaints over the sky | none | | | |

Reading it against Mike's standing ask (report flash, PSRAM, RAM, draw time, storage with every
feature): flash and PSRAM use about a sixth of the free space; RAM (the App) uses 64 of about 4,100
spare bytes on the panel; storage is not touched. The two costs to watch on the panel are the
reeds' permanent 31 primitives and the sky's 104 s. The cuts are ready in the plans: the reeds'
three (back fan, RS_FAR, RS_FIRESIDE's second reed), the sky's `kTripBandEvery` 2 and the slow wash.

---

## 10. Open questions for Mike (the builders build the defaults)

| # | Question | Default being built |
|---|---|---|
| 1 | Deer in 23 % of midday loops (about 7 visits on a full day): right, or rarer? | `kHerdShare` 0.30, never two running |
| 2 | Tai chi always in the day's first morning loop, then about 1 in 4? | yes |
| 3 | May the panel firmware carry one word between screensaver runs (mushroom growth, tai chi's chapter and "done this morning")? Without it tai chi opens every morning start and mushrooms only ripen in one unbroken hour | yes, in the last step, flashed when you say |
| 4 | The clump behind his seat by the fire (RS_FIRESIDE): keep, or keep the fire's bank clean? It carries the seated pick and the doe's cattail | keep; one mask bit removes it |
| 5 | A panel check of the sky alone (frame rate) before the mushroom story is built: wanted? It needs one extra flash | no extra flash; measured at the stage's flash, with the knob and the wash as fallbacks |
| 6 | The stem in his mouth while he shoots prone | keep it |
| 7 | The stem during tai chi | it falls as he begins |
| 8 | Longshot's aim line crossing the camp pine's low boughs (that plan's risk 1) | as planned; the still decides |
| 9 | Tai chi's order follows the 24-form with liberties (repeats cut, two borrowed forms) | as planned |
| 10 | Mushroom trip also at golden hour in the evening | no: midday only; the evening's pool is not touched |
| 11 | Raise the 29,992 byte test wire on the App instead of trimming | no; trimmed to about 29,920 |
| 12 | Which of the "later" list in 4.9 do you want next | none built now |
| 13 | One flash at the end, or reeds, tai chi and longshot first | one, at the end |

---

## 11. Risks of the integration itself

1. **The foundation step guesses every hook's signature.** If a builder needs a different one, the
   integrator changes the call site; builders do not edit shared files. Expect a few round trips.
2. **`sizeof(App)` has about 70 bytes of room under the wire** after the trims. A struct that comes
   out bigger than its plan (Reeds, Landscape's part) eats it. Every step prints the figure.
3. **The shared Lookout block has 8 bytes of room on the host build** (616 of 624, from the
   cattails plan's figures, which I did not measure). The asserts will say.
4. **The plan block is one function carrying four rules** (forced fetch, forced tai chi, forced
   mushroom, the herd's cap). A slip moves every pin. The switch-off folder is the guard: it must
   reproduce the baseline lines after every step.
5. **The computed patch** (3.1 point 5) is laid out in three places from one function; if a caller
   passes a different stage the cache and the major disagree. One accessor, one test (the drawn
   pixels sit where `patchLayout` says for stages 1 to 7).
6. **RS_NEAR missing in up to a quarter of loops** changes the reeds' look more than its plan
   expected (the dragonfly then has RS_FORE only). If Mike misses it, the alternative is to move the
   mushroom's crouch 20 px right in those seeds, which needs the mushroom plan's reach rechecked.
7. **Rates are estimates.** The 600-loop count in C7 is the truth; `test_dog` and
   `test_memory_sets` may ask for lower weights.
8. **Midday gets crowded**: herd, longshot, the mushroom day, on top of ten daytime majors. The
   rules of 4.8 keep any one loop calm, but a midday afternoon will show more set pieces and fewer
   plain fillers than today. Mike's "stillness" may want `kHerdShare` or `kLongshotW` down after he
   has lived with it.
9. **The first-morning rule without the firmware step** fires at every morning start. Until C8 that
   is what the panel would do; C8 is small but it is firmware and waits for Mike.
10. **Line endings** (checked with `file` on 2026-11-18). CRLF: `majors.h`, `spine_midday.cpp`,
    `app.cpp`, `ss_app.h`, `landscape.*`, `ambient.*`, `hiker.*`, `minors.*`, `weather.*`,
    `critters.*`, `memory.cpp`, `tests/test_midday.cpp`, `tests/test_critters.cpp`,
    `sim/preview_common.cpp`, `CMakeLists.txt`. LF: `majors.cpp`, `day.*`, `spine.h`, `spine.cpp`,
    `memory.h`, `world.*`, `react.cpp`, `housedogs.*`, `stream.*`, `plink.*`, `day_plink.cpp`,
    `tests/test_spines.cpp`, `test_morning_stay.cpp`, `test_scene.cpp`. New files are LF. Byte-safe
    edits; check with `file` after each step.
11. **Panel cost is unverified everywhere.** Nothing in this stage was measured on the panel.
