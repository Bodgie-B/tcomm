Stage 22 baseline (INTEGRATION_PLAN.md section 6.3, step A0), recorded 2026-11-18 from the tree as it
was before the foundation step: HEAD 3260e2c with screensaver/ clean, built into
screensaver/build-s22-base (Release, Ninja, WinLibs g++, SS_XMAS ON). All 36 suites passed there.

Files
  loops.txt    ss_preview --loops 10 for seeds 1 and 7 in each family: evening (--mood 4), night
               (--mood 0 --night), morning (--spine morning --mood 1.5), midday (--spine midday --mood 3).
               No --weather, so the weather is random per loop as in the pinned tests' source runs.
  beats.txt    ss_preview --log-beats --skip 20000 --frames 0 for seeds 1 to 12, moods in turn by seed
               mod 3 (1: --mood 0 --night, 2: --mood 5, 0: --mood 3), as testMinors does. The "done:" lines
               hold wall times and a temp folder: leave them out of any comparison.
  minors.txt   the minor lines (tick=T minor=NAME) of beats.txt, per seed.
  probe.txt    s22probe.exe: sizeof App, World, StreamLife, Landscape, LookoutState, XmasState and more;
               a hash of Ambient::sway[] over 2,000 ticks of seed 1 (evening); the plinking's PlinkState
               hashed every live tick in the first two midday seeds whose loop 0 plays it (6 and 26); a
               deer drawn in every CritterAction (0..17), a walk (18) and a run (19); and per family and
               seed (1, 7, 12) loop 0 rendered every tick: the frame at tick 20, a hash of every 20th
               frame, strips a frame and host ms a frame (ms is timing: compare it only on a quiet
               machine, never as a pass or fail).
  s22probe.cpp, baseline.sh   the probe and the script that made these files.

Comparing a later build
  cd to a scratch folder, then:
    c++ -std=c++17 -O2 -DSS_XMAS=1 -DSS_S22=<0|1> -ffp-contract=off -I<screensaver>/core s22probe.cpp \
        <screensaver>/build-NAME/libss_core.a -o probe.exe
    bash baseline.sh <screensaver>/build-NAME ./probe.exe OUT_DIR
    diff --strip-trailing-cr loops.txt OUT_DIR/loops.txt
    diff --strip-trailing-cr minors.txt OUT_DIR/minors.txt
    diff --strip-trailing-cr <(grep -v '^done:' beats.txt) <(grep -v '^done:' OUT_DIR/beats.txt)
    diff --strip-trailing-cr <(sed 's/ strips.*//' probe.txt | tail -n +2) <(sed 's/ strips.*//' OUT_DIR/probe.txt | tail -n +2)
  The probe's first line (sizes) is expected to differ after the foundation (sizeof App 29,856 -> 29,928).
  The PlinkState hash is of the raw struct: step B5 adds fields to PlinkState, so after B5 compare the
  plinking's frames (the midday frame hashes, and test_plink / test_day_plink) instead.

After step A1 (the foundation), both build-s22-off and build-s22-on reproduced every line here but the
sizes line and the timing columns (see docs/screensaver/stage22/FOUNDATION_NOTES.md section 7).
