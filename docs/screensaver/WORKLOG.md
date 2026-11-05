# Screensaver work log (taken over from Mark, 1 Nov 2026)

Mike's instructions for the overnight run (1 Nov, evening): no panel work, no SD-card spines. Work the
missing items: fast internal RAM, the touch reveal, house reactions (and suggestions for more), the
soak test, Mark's open notes explained. Then go on to the morning and daytime ideas (slackline, kite,
camp dog and the rest). Delegate to Sonnet and Haiku where it fits. No commits, no pushes, no flashing.

Baseline: Mark's last commit 08b10d5, untouched copy at sim/out/tcomm-screensaver/. Build folders:
screensaver/build-lead (lead), build-chip, build-soak, build-fix, build-react (agents).

## Queue

| # | Item | Who | State |
|---|---|---|---|
| 1 | Fast block lent by the host (strip buffer + raster scratch), App 72 KB -> 21 KB, SS_APP_BYTES 32 KB | lead | done, 10 tests pass, 12 Lookout stills pixel-identical |
| 2 | Touch reveal: core/scene/reveal.*, appRevealBegin/Step, preview --reveal-at, tests/test_reveal.cpp | lead | done, 191 checks; sheets in docs/screensaver/reviews/house/reveal_*.png |
| 3 | Status chip + dog badges + doorbell pulse: core/scene/chip.*, chip_font.h, test_chip | Sonnet | done (160x40, Jost 800 23 px, 6.46 M checks, corner never entered); now matching the panel's theme.cpp colors (day/night rows) |
| 4 | Wire the chip into app.cpp (draw last, dirty marks, chipDoorbell on EV_DOORBELL) | lead | done: Inputs.chip flag (zeroed = off, keeps Mark's renders), preview --chip, dog_out 3 = both |
| 5 | Doorbell reaction: core/scene/react.*, story freeze | lead | done: test_react passes (85 ticks exact, story equivalence, determinism); fixed after Sonnet's test: weather gags wait during the freeze, wave applied after weatherApplyPose; sheets doorbell_*.png |
| 6 | Repeat-rule fix (16-bit major sets) + house_weather (OUTSIDE -> scene weather) | Sonnet | done: sets were already 32-bit; removed the rain_shelter exemption in majors.cpp recent-set check, MajorSet typedef + static_assert; storm loops' plans change. house_weather.* + tests (55 cases). danger_heat/cold carried but unused |
| 7 | Soak tool sim/soak_main.cpp, 24 h runs | Sonnet | done: 3 x 24 h, 142 loops each, all families and weathers, deterministic, 0 overruns/hangs; only critter_over_hiker (pre-existing, 3 causes, see 11f); strips/frame ~8 avg, rain ~10 |
| 8 | Independent review of the whole diff vs 08b10d5 | Sonnet | done: no memory-safety or determinism bugs; fixed: story clock (storyTick) so a bell delays minors/weather/fog instead of using up their gaps (test_react equivalence now 0 minor diffs), coverage row zeroed every appRender, reveal hands the fast block back (documented), chip's first frame, chip pulse only when shown, react uses table sines. Left: ring AA fringe slightly bold; bird spawn draws loop RNG during a freeze (next loop's seed can differ after a bell) |
| 9 | Full ctest + pixel check after everything | Haiku | after 8 |
| 10 | Write-up for Mike: fast RAM why, soak test, Mark's notes explained, house reaction ideas | lead | last of the first batch |
| 11 | Morning and daytime ideas: design in MORNING_DAYTIME_PLAN.md (lead, done); guide HOW_TO_ADD_SCENES.md (Sonnet, done, 432 lines) | lead + agents | building |
| 11a | Camp dog + fetch major (midday) + dogs-out forced fetch | Sonnet (build-dog) | done: CR_DOG, fetch.cpp, MJ_FETCH (w 1.5, ~37% of midday loops), forced with dog_out, second dog with 3; 18 tests pass; sizeof(App) 23,224; evening/morning pins re-pinned (only the brave= label changed: dog joins the roll) |
| 11b | Kite module standalone (kite.*), then wire as a midday major | Sonnet | done: MJ_KITE w 1.0, ~20% of midday loops, no rain/snow/storm or night; random rain after the plan gives a no-show (palm out, shrug, sit); sheets major_kite_a/b/c |
| 11c | Boomerang + eagle module standalone (boomerang.*), then wire | Sonnet | done: MJ_BOOMERANG w 1.0, ~20%; stuck boomerang kept in memory (boom_stuck) for this and the next loop; test_day_majors 846 checks; 19 tests pass; sizeof(App) 23,680; sheets major_boomerang_a/b/c |
| 11d | Morning "camp stays up" variant | Sonnet (build-morn) | done: morning_stay.cpp, 61% of mornings (private stream, Mark's break-camp plans byte-identical), 7 beats, new items pan/cake/brush/sack + rustle; sizeof(App) 24,480; kMorning1 re-pinned; soak 10 h clean once day_hike was exempted like "leave" (lead edited soak_main.cpp); sheets morning_*.png |
| 11e | Bike and archery as standalone modules (bike.*, archery.*); wire after the morning lands | Sonnet (build-bike, build-arch) | archery done (19,243 checks; 440 B state; A 23 s, B 9 s, C 16 s, so the major needs a walk-in, target set-up and pauses to reach 40-60 s; sheets archery_a/b/c/zoom); bike done (25,424 checks; 77 px bike; BikeState 240 B + SplashState 1,632 B: the wiring should put the day majors' states in one union, only one major runs at a time; the test lane crossed the chip corner, so the wired lane must stay above y 412; sheets bike_a/b/c/zoom) |
| 11g | Slackline standalone module (slackline.*) | Sonnet (build-slack) | done (1.19 M checks; 44+92+48 B; feet within 0.8 px of the line; ~4 strips with hiker and critter; sheets slackline_a/b/critter/zoom). While the walker is on the line the scene must not call hikerWalkTo/hikerSetPose |
| 11h | Ropes course standalone module (ropes.*) | Sonnet (build-ropes) | done (379,983 checks; 136 B; A 85 s, B 62 s, C 76 s; head pokes above the net's beam on the climb, kept; sheets ropes_a/b/c/zoom/moods) |
| 11i | Wire bike, archery, slackline, ropes as midday majors (one agent, union of day-major states) | Sonnet, finished by lead | done 5 Nov: MJ_BIKE/ARCHERY/SLACKLINE/ROPES in day_*.cpp, one DayState union (day.h) with the kite; lead fixed the bike lane (the stream rises to camp height on the right: water only in the mid-frame dip, bank lane above it, spray capped at x 400, B's rock early, BIKE_LANE_MAX 8), the archery reaction (bow up: he only looks round), two test bugs; 25/25 tests, 12 h soak clean. Contact sheets for these four not made yet |
| 12 | Commit and push (Mike asked 5 Nov): local commit, merge Mark's PR #2 into main in the clone, copy on top, scan, Mike's OK on the file list, push | lead | in progress |
| 13 | Contact sheets for bike/archery/slackline/ropes in the scene; then stage 7, the panel test | lead (Sonnet weekly limit until 8 Nov) | queued |
| 11f | Critter overlap fixes from the soak: A mouse_tent exit vs his firewood return; B chipmunk_cross second leg clearance along the segment; C squirrel_nut route recheck (directorAvoidHiker for CM_FREE near his walking bounds) | Sonnet (build-crit) | done: hikerComingIn turns any leaving critter round (minors.cpp), directorAvoidHiker scatters CM_FREE non-brave critters within 12 px while he stands (director.cpp); critter_over_hiker 0 on seeds 1-5, 7; pins unchanged; 19 tests pass; ~40% of evening loops differ slightly in critter motion |

## Notes
- Fixed: the lent raster scratch must start zeroed (Sonnet caught it); rasterSetScratch now clears a new block.
- house_weather: the gateway's heat warning plays heat on a dry sky (lead).
- Scratch ownership changed (2 Nov): appRender sets its scratch and puts the caller's back; appStart zeroes it. The restore-on-appStop design crashed test_scene (non-LIFO stops), found by the react test agent.
- Usage limit hit ~1 Nov late evening, reset 1:30 am; soak and guide agents resumed with SendMessage.
- All 15 tests pass after the scratch change (tests' own scratch must be calloc'd).
- Integration rule: only one agent at a time adds majors/critter kinds (pinned --loops lines in test_spines/test_midday change with each addition).
- 2 Nov ~6:30 am: second usage limit; resumed dog, kite, boomerang and soak agents. test_critters/test_midday/test_spines were failing mid-way through the dog agent's edits (expected until it re-pins).
- 2 Nov afternoon: screensaver paused for the second panel (flashed, mounted, 29/29 checks). Evening: Mike chose to finish the extras before committing and the panel test. Resumed 11d, 11e, 11f; started 11g; hourly check-in cron recreated (:23).
- NOT COMMITTED: all screensaver work lives only in this session's folder. Commit + merge PR #2 + push is the step after the extras (Mike's order).
- 3-5 Nov: Sonnet hit its WEEKLY limit (resets 8 Nov, 12 pm); lead works alone until then. Hourly cron deleted.
