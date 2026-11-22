// The deer visit, the standalone part. See herd.h.
//
// Step B3 (2026-11-18; Mike's words in herd.h). In this file: the cue tables (built at compile time from a
// few blocks), the spots, herdInit, herdTick (the cue runner, the graze cycle, the alarm and the walk out),
// herdView, and the hoof prints. No World; every random number comes from the Rng the driver gives, one
// call per statement; nothing reads a clock but HerdState::t.
//
// How the choreography is kept natural (Mike: unhurried, nuanced; when something feels rushed add
// stillness, not more motion): every deer runs its own list and its own graze cycle, so no two heads
// rise or fall together (a change that would collide is put back 0.3 s); the doe is never the last head
// down; the alarm reaches the deer nearest her first and each in its own way (the fawn looks at its
// mother before it looks where she looks, the second doe is caught mid-mouthful and her head comes half
// way up, stops, and comes the rest 0.4 s later, the bolder yearling looks the wrong way first); and they
// leave as a loose line, each when the one ahead is a body length past (REVIEW_FIXES F12), not as a burst.
#include "herd.h"

#include <math.h>

#include "../anim/ease.h"
#include "critters.h"

namespace ss {

namespace {

inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float absf(float a) { return a < 0.f ? -a : a; }

// ---- the camp's rough lanes (frame px; feet y) --------------------------------------------------------------
// A deer's way in and way out each have a lane of its own so their bodies are 4 to 10 px apart, not on
// top of each other (the way out's are REVIEW_FIXES F12's: 380, 366, 372, 360).
constexpr int kLaneIn[HR_COUNT] = {372, 376, 362, 371, 369, 366};
constexpr int kLaneOut[HR_COUNT] = {380, 374, 372, 360, 366, 370};
// The first stop, px from the foreground pines' right edge (step C4: the plan's x 58 to 100 is wholly behind
// the pines' foliage, so nothing of the looks in was ever seen; now the head and neck come out past the edge
// and the body stays behind it). Never so far that the nose reaches the pack that stands just beyond.
constexpr float kLookInDx[HR_COUNT] = {-14.f, -30.f, 0.f, 0.f, -12.f, -10.f};
// True px/s on the way out (the doe's canter is a spring, not a flight; the fawn is the quick one).
constexpr float kExitSpeed[HR_COUNT] = {70.f, 96.f, 68.f, 66.f, 66.f, 68.f};
constexpr float kHardSpeed = 115.f;
constexpr float kBody = 72.f;   // a body length and a bit: how far ahead the deer in front must be

// ---- the tables -----------------------------------------------------------------------------------------------

constexpr int kPoolMax = 520;

struct CueList {
  uint16_t off;
  uint8_t n;
};
struct Pool {
  HerdCue c[kPoolMax];
  int used;
  CueList main[3][HR_COUNT];
  CueList second[HR_COUNT];
  CueList wave[2][HR_COUNT];
  CueList exit_soft[HR_COUNT];
  CueList exit_hard[2];       // left, right: a plain run out, whatever the role
  CueList nibble[3];          // the second doe's list when she goes to the fireside cattail first, by variant
};

// Builds one list: cues in any order, sorted (stably) by time when the list is done.
struct Bld {
  Pool& p;
  int start;
  constexpr explicit Bld(Pool& pp) : p(pp), start(pp.used) {}
  constexpr void c(float at, int cmd, int a = 0, int b = 0, bool wait = false) {
    HerdCue& q = p.c[p.used++];
    q.at = (uint16_t)(at * 100.f + 0.5f);
    q.cmd = (uint8_t)cmd;
    q.wait = wait ? 1 : 0;
    q.a = (int16_t)a;
    q.b = (int16_t)b;
  }
  constexpr CueList done() {
    for (int i = start + 1; i < p.used; ++i) {   // insertion sort: stable, so cues made at one time keep their order
      const HerdCue k = p.c[i];
      int j = i - 1;
      while (j >= start && p.c[j].at > k.at) {
        p.c[j + 1] = p.c[j];
        --j;
      }
      p.c[j + 1] = k;
    }
    return CueList{(uint16_t)start, (uint8_t)(p.used - start)};
  }
};

// Per variant (0 A, 1 B, 2 C): when the fawn's splash starts and whether it is the short one (two dashes),
// and when the tent business starts. C does the splash first. DEER_PLAN section 4; REVIEW_FIXES F12 moved
// the alarm to 77 s, so B's and C's blocks are fitted inside it (B's false alarm comes at the first tug
// and not the second; C's splash is the short one and starts as soon as the fawn is in place).
constexpr float kSplashT0[3] = {57.f, 57.f, 37.f};
constexpr bool kSplashShort[3] = {false, true, true};
constexpr float kTentT0[3] = {43.f, 43.f, 49.f};
constexpr float kNuzzleAt[3] = {70.8f, 68.0f, 48.0f};   // the doe lowers her head to the fawn (when it is back)

// The false alarm of B: the zip comes at the first tug, Z seconds into the beat; heads rise one by one from
// Z + the doe's 0.4, they stare 3 s, and relax in reverse order (D4 first, the doe last).
constexpr float kZip = 43.f + 7.8f;
constexpr float kHeadB[HR_COUNT] = {0.4f, 1.3f, 1.2f, 1.2f, 0.8f, 1.0f};         // doe, fawn, door, wall, grazer, grazer2
constexpr float kRelaxB[HR_COUNT] = {5.7f, 5.1f, 4.6f, 4.1f, 3.4f, 3.7f};

constexpr int CA(CritterAction a) { return (int)a; }

// The alarm waves, seconds from the alarm: head up (the first ACT), ears forward, the flag, the stamps. The
// soft one is DEER_PLAN section 3 (heads: the doe 0, her fawn 2.2, the second doe 2.7, a sixth 3.0, the
// door yearling 3.3, the wall yearling 3.9; flags a beat behind each head; only hers and the fawn's go all
// the way up). The hard one is the same order compressed (all heads up within 0.7 s, all flags all the way).
constexpr float kWaveHead[HR_COUNT] = {0.0f, 2.2f, 3.3f, 3.9f, 2.7f, 3.0f};
constexpr float kWaveTail[HR_COUNT] = {1.4f, 2.7f, 3.9f, 4.6f, 3.2f, 3.55f};
constexpr float kHardHead[HR_COUNT] = {0.0f, 0.15f, 0.5f, 0.7f, 0.3f, 0.4f};
constexpr float kHardTail[HR_COUNT] = {0.3f, 0.35f, 0.75f, 0.95f, 0.55f, 0.65f};
constexpr float kSoftOff = kHerdSoftOff;
constexpr float kHardOff = kHerdHardOff;

constexpr void introDoe(Bld& b) {
  b.c(2.0f, HC_ENTER, kLaneIn[HR_DOE]);           // (behind the pines until about 9 s: the camp is empty till then)
  b.c(2.0f, HC_WALK, HS_LOOKIN, 22);
  b.c(10.7f, HC_ACT, CA(CA_LOOK), 50, true);      // she stops and lifts her head, and holds
  b.c(11.8f, HC_EAR);
  b.c(13.1f, HC_ACT, CA(CA_HEADTURN), 120);       // looks back over her shoulder at the others, not in sight yet
  b.c(14.3f, HC_ACT, CA(CA_LOOK), 80);
  b.c(15.1f, HC_WALK, HS_WAY2, 14);               // three slow steps
  b.c(18.1f, HC_ACT, CA(CA_EAT), 70, true);       // nose to the ground: nothing has happened to her
  b.c(19.7f, HC_ACT, CA(CA_LOOK), 60);
  b.c(20.3f, HC_WALK, HS_G0, 26);                 // she commits
  b.c(30.0f, HC_GRAZE_ON, 0, 0, true);
}
constexpr void introFawn(Bld& b) {
  b.c(13.0f, HC_ENTER, kLaneIn[HR_FAWN]);
  b.c(13.0f, HC_WALK, HS_FSTOP, 30);
  b.c(19.0f, HC_ACT, CA(CA_LOOK), 50, true);      // two more steps, stops, looks up at her
  b.c(21.0f, HC_WALK, HS_GF, 27);                 // follows her across
  b.c(30.0f, HC_GRAZE_ON, 0, 0, true);
}
constexpr void introDoor(Bld& b) {
  b.c(21.5f, HC_ENTER, kLaneIn[HR_DOOR]);
  b.c(21.5f, HC_WALK, HS_LOOKIN, 24);
  b.c(30.0f, HC_ACT, CA(CA_LOOK), 50, true);      // stops to look for 1.5 s while the other walks past
  b.c(32.3f, HC_WALK, HS_T2WAIT, 22);
}
constexpr void introWall(Bld& b) {
  b.c(22.3f, HC_ENTER, kLaneIn[HR_WALL]);
  b.c(22.3f, HC_WALK, HS_T3GRAZE, 22);            // walks past the other, grazes as if the tent were nothing
  b.c(30.0f, HC_GRAZE_ON, 0, 0, true);
}
constexpr void introGrazer(Bld& b) {
  b.c(16.0f, HC_ENTER, kLaneIn[HR_GRAZER]);
  b.c(16.0f, HC_WALK, HS_LOOKIN, 24);
  b.c(20.0f, HC_HOLD, 100, 0, true);              // pauses 1.0 s at the edge
  b.c(20.0f, HC_WALK, HS_G4, 24);
  b.c(30.0f, HC_FACE, -1, 0, true);               // she turns to face the fire and the tent
  b.c(30.0f, HC_GRAZE_ON, 0, 0, true);            // she is the one who eats: head down at once
}
// The second doe when the fireside clump stands this loop (INTEGRATION_PLAN 4.1, step C4): she goes to the
// cattail behind his empty seat first. 0.3 s of nothing, the neck out toward the head, the stalk bends to meet
// her lips (the driver's part, from HC_NIBBLE), an ear flick, a small tug, a harder one and the head is hers;
// she chews it with her head level, looking at nothing, then ambles up to her place and joins the others.
// Every step waits for the one before, so a slow seed is late, never out of order.
constexpr void introGrazerNibble(Bld& b) {
  b.c(16.0f, HC_ENTER, kLaneIn[HR_GRAZER]);
  b.c(16.0f, HC_WALK, HS_LOOKIN, 24);
  b.c(20.0f, HC_HOLD, 100, 0, true);
  b.c(20.0f, HC_WALK, HS_N4VIA, 24);              // across the camp behind the cold pit, then down to the bank
  b.c(20.0f, HC_WALK, HS_N4, 16, true);
  b.c(30.0f, HC_FACE, -1, 0, true);               // the clump is behind her as she arrives: she turns to it
  b.c(30.0f, HC_HOLD, 110);                       // the turn, and 0.3 s of nothing
  b.c(30.0f, HC_ACT, CA(CA_REACH), 120);
  b.c(30.0f, HC_NIBBLE);
  b.c(30.0f, HC_HOLD, 160);
  b.c(30.0f, HC_EAR);                             // lips on it: one ear flick in the hold
  b.c(30.0f, HC_HOLD, 30);
  b.c(30.0f, HC_NUDGE, -5, 30);                   // a small tug
  b.c(30.0f, HC_HOLD, 60);
  b.c(30.0f, HC_NUDGE, -9, 30);                   // a harder one: the head comes off (the driver, 2.8 s in)
  b.c(30.0f, HC_HOLD, 40);
  b.c(30.0f, HC_ACT, CA(CA_IDLE), 40);            // head up level, chewing, looking at nothing
  b.c(30.0f, HC_HOLD, 240);
  b.c(30.0f, HC_WALK, HS_G4, 9);                  // and she drifts up to her place in the picture
  b.c(30.0f, HC_FACE, -1, 0, true);
  b.c(30.0f, HC_GRAZE_ON, 0, 0, true);
}
constexpr void introGrazer2(Bld& b) {
  b.c(20.2f, HC_ENTER, kLaneIn[HR_GRAZER2]);
  b.c(20.2f, HC_WALK, HS_G5, 24);
  b.c(30.0f, HC_FACE, -1, 0, true);
  b.c(30.0f, HC_GRAZE_ON, 0, 0, true);
}

// The yearling at the door: stands looking at the tent, takes the 24 px in two goes (stopping half way),
// stretches its neck out to the flap, sniffs twice, and then the variant's thing happens: A the fabric
// moves (it flinches, holds still, comes back, tugs the flap twice and lets go with it a hand's width
// open), B the first tug pulls the zip and the flap falls open (it leaps back), C as A with the fawn at
// its side.
constexpr void tentDoor(Bld& b, int v) {
  const float t0 = kTentT0[v];
  if (t0 > 46.f) {   // C: it grazes where it waited
    b.c(42.4f, HC_GRAZE_ON, 0, 0, true);
    b.c(t0 - 0.6f, HC_GRAZE_OFF);
  }
  b.c(t0, HC_FACE, -1);                           // it turns to look at the tent
  b.c(t0, HC_ACT, CA(CA_LOOK), 40);
  b.c(t0 + 1.5f, HC_WALK, HS_T2HALF, 9);
  b.c(t0 + 3.2f, HC_WALK, HS_T2, 9, true);
  b.c(t0 + 4.6f, HC_ACT, CA(CA_REACH), 180, true);
  b.c(t0 + 4.7f, HC_PRINT, HP_SCUFF, 0);
  b.c(t0 + 6.4f, HC_NUDGE, 3, 30);
  b.c(t0 + 7.0f, HC_NUDGE, -3, 30);
  if (v == 1) {   // B
    b.c(kZip, HC_DOOR, 35, 20);
    b.c(kZip, HC_WOBBLE, 3);
    b.c(kZip + 0.05f, HC_ACT, CA(CA_STARTLE), 15);
    b.c(kZip + 0.1f, HC_BACK, 22, 120);            // (22 px, not the plan's 30: the second doe grazes just behind it)
    b.c(kZip + 1.2f, HC_ALERT, 1, 0, true);
    b.c(kZip + 1.2f, HC_ACT, CA(CA_LOOK), 40, true);
    b.c(kZip + kRelaxB[HR_DOOR], HC_ALERT, 0);
    b.c(kZip + kRelaxB[HR_DOOR], HC_ACT, CA(CA_IDLE), 50);
    b.c(kZip + kRelaxB[HR_DOOR] + 0.6f, HC_WALK, HS_D2OUT, 9);   // a few steps back toward the tent, to its place
    b.c(kZip + kRelaxB[HR_DOOR] + 0.6f, HC_GRAZE_ON, 0, 0, true);
    return;
  }
  b.c(t0 + 8.6f, HC_WOBBLE, 1);                   // the fabric moves
  b.c(t0 + 8.7f, HC_ACT, CA(CA_STARTLE), 25);     // it flinches and takes two steps back, still facing the tent
  b.c(t0 + 8.7f, HC_BACK, 14, 60);
  b.c(t0 + 9.9f, HC_EAR);
  b.c(t0 + 12.0f, HC_WALK, HS_T2, 9, true);       // comes forward its 14 px again
  b.c(t0 + 14.0f, HC_ACT, CA(CA_REACH), 160, true);
  b.c(t0 + 14.1f, HC_PRINT, HP_SCUFF, 6);
  b.c(t0 + 16.0f, HC_WOBBLE, 4);                  // takes the flap in its lips and tugs twice
  b.c(t0 + 16.0f, HC_DOOR, 12, 30);
  b.c(t0 + 16.3f, HC_DOOR, 5, 30);
  b.c(t0 + 16.7f, HC_WOBBLE, 4);
  b.c(t0 + 16.7f, HC_DOOR, 14, 30);
  b.c(t0 + 17.2f, HC_DOOR, 10, 20);               // lets go: a hand's width open, the first sign
  b.c(t0 + 17.4f, HC_ACT, CA(CA_LOOK), 40);
  b.c(t0 + 17.5f, HC_PRINT, HP_SCUFF, -6);
  b.c(t0 + 18.4f, HC_WALK, HS_D2OUT, 9);
  b.c(t0 + 19.0f, HC_PRINT, HP_SCUFF, 3);
  b.c(t0 + 19.0f, HC_GRAZE_ON, 0, 0, true);
}

// The yearling at the wall: lifts her head, steps up, stretches to the tent's corner from the other
// side, slower; flinches in place; then touches the wall with her second stretch and loses interest.
constexpr void tentWall(Bld& b, int v) {
  const float t0 = kTentT0[v];
  b.c(t0 + 0.4f, HC_GRAZE_OFF);
  b.c(t0 + 0.4f, HC_ACT, CA(CA_LOOK), 40);
  b.c(t0 + 2.5f, HC_WALK, HS_T3, 8);
  b.c(t0 + 6.0f, HC_ACT, CA(CA_REACH), 200, true);
  if (v == 1) {   // B
    b.c(kZip + 0.1f, HC_ACT, CA(CA_STARTLE), 20);
    b.c(kZip + kHeadB[HR_WALL], HC_ALERT, 1);
    b.c(kZip + kHeadB[HR_WALL], HC_ACT, CA(CA_LOOK), 35);
    b.c(kZip + kRelaxB[HR_WALL], HC_ALERT, 0);
    b.c(kZip + kRelaxB[HR_WALL], HC_ACT, CA(CA_IDLE), 50);
    b.c(kZip + kRelaxB[HR_WALL] + 0.6f, HC_GRAZE_ON);
    return;
  }
  b.c(t0 + 8.84f, HC_ACT, CA(CA_STARTLE), 20);    // flinches in place, legs braced
  b.c(t0 + 11.2f, HC_ACT, CA(CA_REACH), 240);     // the bolder one stretches again, slower
  b.c(t0 + 13.6f, HC_WOBBLE, 2);                  // and touches the wall
  b.c(t0 + 14.4f, HC_ACT, CA(CA_IDLE), 40);       // loses interest and grazes where she stands
  b.c(t0 + 14.4f, HC_GRAZE_ON);
}

// The doe's part of the tent business: A and C, her head comes up when the fabric moves and she looks their
// way for 1.6 s; B, her head is the first of the five that come up at the zip.
constexpr void tentDoe(Bld& b, int v) {
  const float t0 = kTentT0[v];
  if (v == 1) {
    b.c(kZip + kHeadB[HR_DOE], HC_GRAZE_OFF);
    b.c(kZip + kHeadB[HR_DOE], HC_ACT, CA(CA_LOOK), 30);
    b.c(kZip + kHeadB[HR_DOE], HC_ALERT, 1);
    b.c(kZip + kRelaxB[HR_DOE], HC_ALERT, 0);
    b.c(kZip + kRelaxB[HR_DOE], HC_ACT, CA(CA_IDLE), 50);
    b.c(kZip + kRelaxB[HR_DOE] + 0.6f, HC_GRAZE_ON);
    return;
  }
  b.c(t0 + 8.9f, HC_GRAZE_OFF);
  b.c(t0 + 8.92f, HC_ACT, CA(CA_LOOK), 35);
  b.c(t0 + 8.92f, HC_ALERT, 1);
  b.c(t0 + 10.5f, HC_ALERT, 0);
  b.c(t0 + 10.5f, HC_GRAZE_ON);
}

// B's stare, for a deer that is not at the tent (the fawn, the second doe, a sixth): its head comes up and
// its ears go forward, and in reverse order they go back to the grass.
constexpr void stareB(Bld& b, int role) {
  b.c(kZip + kHeadB[role], HC_GRAZE_OFF);
  b.c(kZip + kHeadB[role], HC_ACT, CA(CA_LOOK), 30);
  b.c(kZip + kHeadB[role], HC_ALERT, 1);
  b.c(kZip + kRelaxB[role], HC_ALERT, 0);
  b.c(kZip + kRelaxB[role], HC_ACT, CA(CA_IDLE), 50);
  b.c(kZip + kRelaxB[role] + 0.6f, HC_GRAZE_ON);
}

// The fawn in the stream (DEER_PLAN section 3): to the bank, head down to the water and one ring, the jerk
// up (cold), two paws with a ring each, a bound along the shallows with three drops at the landing and
// back, braced, the shake, [a third shorter dash that ends in a stumble, and a stand in the water with
// two tail flicks], and out, leaving five wet prints. The short one stops after the shake.
constexpr void splashFawn(Bld& b, int v) {
  const float s = kSplashT0[v];
  b.c(s, HC_GRAZE_OFF);
  b.c(s, HC_WALK, HS_WATER, 26);
  b.c(s + 2.0f, HC_ACT, CA(CA_EAT), 70, true);
  b.c(s + 2.5f, HC_RING, 70);
  b.c(s + 3.5f, HC_ACT, CA(CA_LOOK), 15);
  b.c(s + 4.0f, HC_STAMP, 2);
  b.c(s + 4.6f, HC_STAMP, 2);
  b.c(s + 5.4f, HC_RUN, HS_SP26, 70);
  b.c(s + 5.55f, HC_DROPS, 3);
  b.c(s + 6.4f, HC_RUN, HS_SM8, 70);
  b.c(s + 6.9f, HC_DROPS, 3);
  b.c(s + 7.2f, HC_ACT, CA(CA_STARTLE), 30);
  b.c(s + 7.6f, HC_ACT, CA(CA_SHAKE), 90);
  b.c(s + 7.9f, HC_DROPS, 2);
  float out = s + 9.4f;
  if (!kSplashShort[v]) {
    b.c(s + 9.0f, HC_RUN, HS_SP10, 60);
    b.c(s + 9.9f, HC_ACT, CA(CA_STARTLE), 30);
    b.c(s + 10.5f, HC_ACT, CA(CA_IDLE), 40);
    b.c(s + 10.7f, HC_TAIL, 35, 10);
    b.c(s + 11.0f, HC_TAIL, 0, 20);
    b.c(s + 11.3f, HC_TAIL, 35, 10);
    b.c(s + 11.6f, HC_TAIL, 0, 20);
    out = s + 11.6f;
  } else {
    b.c(s + 8.6f, HC_ACT, CA(CA_IDLE), 40);
    b.c(s + 8.7f, HC_TAIL, 35, 10);
    b.c(s + 9.0f, HC_TAIL, 0, 20);
  }
  b.c(out, HC_WALK, HS_GF, 34);
  for (int i = 0; i < 5; ++i) b.c(out + 0.3f + 0.4f * (float)i, HC_PRINT, HP_WET, i * 3 - 6);
}

// The doe watches the splash (head high and back: it is behind her), and, when the fawn is back, lowers her
// head to it: she turns to face it and stretches her nose out to its nose.
constexpr void watchDoe(Bld& b, int v) {
  const float s = kSplashT0[v];
  b.c(s + 6.4f, HC_GRAZE_OFF);
  b.c(s + 6.45f, HC_ACT, CA(CA_HEADTURN), 35);
  b.c(s + 8.5f, HC_GRAZE_ON);
  const float n = kNuzzleAt[v];
  b.c(n - 0.4f, HC_GRAZE_OFF);
  b.c(n - 0.4f, HC_FACE, -1);
  b.c(n, HC_ACT, CA(CA_REACH), 80);
  b.c(n + 1.8f, HC_FACE, 1);                      // and turns back to face the ford: she is the watch
  b.c(n + 2.0f, HC_GRAZE_ON);
}
// The fawn walks back to its place from the water, which is to its right, so it arrives facing left, away
// from its mother. It turns to her first, so the two noses meet (and in the settled picture it stands
// facing her, not with its rump to her).
constexpr void nuzzleFawn(Bld& b, int v) {
  const float n = kNuzzleAt[v];
  b.c(n - 0.2f, HC_GRAZE_OFF, 0, 0);
  b.c(n - 0.2f, HC_FACE, 1, 0, true);
  b.c(n, HC_ACT, CA(CA_LOOK), 40, true);
  if (v != 2) b.c(n + 1.9f, HC_GRAZE_ON);   // (C: it stands by its mother until it goes over to the tent)
}

// C: the fawn comes over from the stream, stretches its own short neck beside the yearling's, flinches
// the biggest when the fabric moves (the three go one after another, 0.12 s apart), and goes home.
constexpr void tentFawnC(Bld& b) {
  const float t0 = kTentT0[2];
  b.c(t0 + 0.5f, HC_GRAZE_OFF);
  b.c(t0 + 0.5f, HC_WALK, HS_FTENT, 28);
  b.c(t0 + 5.0f, HC_ACT, CA(CA_REACH), 180, true);
  b.c(t0 + 8.92f, HC_ACT, CA(CA_STARTLE), 25);
  b.c(t0 + 8.95f, HC_BACK, 16, 60);
  b.c(t0 + 11.0f, HC_EAR);
  b.c(t0 + 12.0f, HC_WALK, HS_GF, 28, true);
  b.c(t0 + 12.1f, HC_GRAZE_ON, 0, 0, true);
}

// ---- the lists per role and variant ---------------------------------------------------------------------------

constexpr CueList mainList(Pool& p, int role, int v) {
  Bld b(p);
  switch (role) {
    case HR_DOE:
      introDoe(b);
      tentDoe(b, v);
      watchDoe(b, v);
      break;
    case HR_FAWN:
      introFawn(b);
      if (v == 1) stareB(b, HR_FAWN);
      splashFawn(b, v);
      nuzzleFawn(b, v);
      if (v == 2) tentFawnC(b);
      break;
    case HR_DOOR:
      introDoor(b);
      tentDoor(b, v);
      break;
    case HR_WALL:
      introWall(b);
      tentWall(b, v);
      break;
    case HR_GRAZER:
      introGrazer(b);
      if (v == 1) stareB(b, HR_GRAZER);
      break;
    default:
      introGrazer2(b);
      if (v == 1) stareB(b, HR_GRAZER2);
      break;
  }
  return b.done();
}

// A second look: the doe and the fawn come in again after a bell or a flash emptied the camp early, walk to
// the second doe's place and the spot beside it, and graze there until the alarm.
constexpr CueList secondList(Pool& p, int role) {
  Bld b(p);
  if (role == HR_DOE) {
    b.c(0.f, HC_ENTER, kLaneIn[HR_DOE]);
    b.c(0.f, HC_WALK, HS_G4, 24);
    b.c(0.f, HC_FACE, -1, 0, true);
    b.c(0.f, HC_GRAZE_ON, 0, 0, true);
  } else if (role == HR_FAWN) {
    b.c(2.f, HC_ENTER, kLaneIn[HR_FAWN]);
    b.c(2.f, HC_WALK, HS_G4F, 26);
    b.c(2.f, HC_GRAZE_ON, 0, 0, true);
  }
  return b.done();
}

constexpr CueList nibbleList(Pool& p, int v) {
  Bld b(p);
  introGrazerNibble(b);
  if (v == 1) stareB(b, HR_GRAZER);
  return b.done();
}

constexpr CueList waveList(Pool& p, int role, bool hard) {
  Bld b(p);
  const float head = hard ? kHardHead[role] : kWaveHead[role];
  const float tail = hard ? kHardTail[role] : kWaveTail[role];
  const int blend = hard ? 12 : (role == HR_DOE ? 18 : 30);
  b.c(head, HC_GRAZE_OFF);
  if (role == HR_DOE) b.c(head, HC_FACE, 1);                       // if she faces left she turns first
  if (role == HR_WALL && !hard) b.c(head, HC_FACE, -1);            // the bolder yearling looks the wrong way first
  // The second doe and a sixth stand facing the fire and the tent (left); the alarm is toward the ford
  // (right), so they look back over the shoulder (CA_HEADTURN) rather than straight ahead: the plan's
  // "five heads up, all looking right" (review of step B3). They are already facing the way they will go.
  if (role == HR_GRAZER && !hard) {                                 // caught mid-mouthful: half way up, stops,
    b.c(head, HC_ACT, CA(CA_IDLE), 35);                             // and the rest 0.4 s later
    b.c(head + 0.4f, HC_ACT, CA(CA_HEADTURN), 30);
  } else if (role == HR_GRAZER2 && !hard) {
    b.c(head, HC_ACT, CA(CA_HEADTURN), blend);
  } else if (role == HR_FAWN && !hard) {                            // it looks at its mother first (head level,
    b.c(head, HC_ACT, CA(CA_IDLE), 30);                             // toward her), and only 0.5 s later where
    b.c(head + 0.5f, HC_ACT, CA(CA_LOOK), 25);                      // she looks; its flag goes up with that
  } else {
    b.c(head, HC_ACT, CA(CA_LOOK), blend);
  }
  b.c(head, HC_ALERT, 1);
  if (role == HR_WALL && !hard) b.c(tail, HC_FACE, 1);
  const bool all_the_way = hard || role == HR_DOE || role == HR_FAWN;
  b.c(tail, HC_TAIL, all_the_way ? 100 : 50, role == HR_DOE ? 25 : (hard ? 12 : (role == HR_FAWN ? 30 : 40)));
  if (role == HR_DOE && !hard) {
    b.c(2.0f, HC_STAMP, 0);       // the near fore hoof lifts, holds, strikes
    b.c(2.9f, HC_STAMP, 1);       // again, with a little dust
    b.c(5.0f, HC_NUDGE, -12, 50); // one small bob of her head in the stillness
  }
  return b.done();
}

// The soft way out, to the left. The fawn's is the wrong way (10 px toward where its mother stood), a slip,
// a run to x 120, a stop and a look back at the camp, and out; everyone else just goes, the doe leaving
// three scuffs along her first steps.
constexpr CueList exitSoftList(Pool& p, int role) {
  Bld b(p);
  if (role == HR_FAWN) {
    b.c(0.f, HC_WALK, HS_FSLIP, 36);
    b.c(0.f, HC_ACT, CA(CA_STARTLE), 20, true);
    b.c(0.f, HC_HOLD, 40);
    b.c(0.f, HC_RUN, HS_FLOOK, 96);
    b.c(0.f, HC_ACT, CA(CA_HEADTURN), 35, true);
    b.c(0.f, HC_HOLD, 60);
    b.c(0.f, HC_RUN, HS_OUT_L, (int)kExitSpeed[HR_FAWN]);
  } else {
    b.c(0.f, HC_RUN, HS_OUT_L, (int)kExitSpeed[role]);
    if (role == HR_DOE) {
      b.c(0.0f, HC_PRINT, HP_SCUFF, 0);
      b.c(0.5f, HC_PRINT, HP_SCUFF, 0);
      b.c(1.0f, HC_PRINT, HP_SCUFF, 0);
    }
  }
  return b.done();
}

// A hard alarm's exit is a plain run at kHardSpeed.
constexpr CueList exitHardList(Pool& p, bool right) {
  Bld b(p);
  b.c(0.f, HC_RUN, right ? HS_OUT_R : HS_OUT_L, (int)kHardSpeed);
  return b.done();
}

constexpr Pool makePool() {
  Pool p{};
  for (int v = 0; v < 3; ++v)
    for (int r = 0; r < HR_COUNT; ++r) p.main[v][r] = mainList(p, r, v);
  for (int v = 0; v < 3; ++v) p.nibble[v] = nibbleList(p, v);
  for (int r = 0; r < HR_COUNT; ++r) p.second[r] = secondList(p, r);
  for (int r = 0; r < HR_COUNT; ++r) {
    p.wave[0][r] = waveList(p, r, false);
    p.wave[1][r] = waveList(p, r, true);
  }
  for (int r = 0; r < HR_COUNT; ++r) p.exit_soft[r] = exitSoftList(p, r);
  p.exit_hard[0] = exitHardList(p, false);
  p.exit_hard[1] = exitHardList(p, true);
  return p;
}
constexpr Pool kPool = makePool();
static_assert(kPool.used <= kPoolMax, "the cue pool is bigger than kPoolMax");

}  // namespace

const HerdCue* herdCues(uint8_t list, uint8_t role, int variant, int& n) {
  n = 0;
  if (role >= HR_COUNT) return nullptr;
  CueList l{0, 0};
  const HerdCue* base = kPool.c;
  switch (list) {
    case HL_MAIN: l = kPool.main[variant < 0 ? 0 : (variant > 2 ? 2 : variant)][role]; break;
    case HL_SECOND: l = kPool.second[role]; break;
    case HL_WAVE_SOFT: l = kPool.wave[0][role]; break;
    case HL_WAVE_HARD: l = kPool.wave[1][role]; break;
    case HL_EXIT_SOFT: l = kPool.exit_soft[role]; break;
    case HL_EXIT_HARD_L: l = kPool.exit_hard[0]; break;
    case HL_EXIT_HARD_R: l = kPool.exit_hard[1]; break;
    default: return nullptr;
  }
  n = l.n;
  return n > 0 ? base + l.off : nullptr;
}

int herdCueCount() { return kPool.used; }

int herdRoles(uint8_t size, uint8_t roles[HERD_MAX]) {
  int n = 0;
  roles[n++] = HR_DOE;
  roles[n++] = HR_FAWN;
  roles[n++] = HR_DOOR;
  roles[n++] = HR_WALL;
  if (size != 2) roles[n++] = HR_GRAZER;
  if (size == 1) roles[n++] = HR_GRAZER2;
  return n;
}

int herdIndexOf(const HerdState& h, uint8_t role) {
  for (int i = 0; i < h.n; ++i)
    if (h.d[i].role == role) return i;
  return -1;
}

Vec2 herdSpot(const HerdState& h, uint8_t spot, uint8_t role) {
  const float fx = h.fire.x, tx = h.tent.x, st_x = h.splash.x, st_y = h.splash.y;
  // Step C4 (the B3 reviewer's note: in the tight layout most graze steps were blocked by the 8 px clearance):
  // the doe and her fawn stand 18 px further right (10 with a sixth deer in front of the camp pine), the second
  // doe 14 px further right (4) and the door yearling settles 8 px nearer the tent, so the gaps are wide enough
  // for several of them to take a step, and no two bounds touch when a head or a tail comes up.
  const float g0_plain = maxf(h.camp_x - 98.f, fx + 214.f) + (h.size == 1 ? 10.f : 18.f);
  const float g0x = h.g0_max > 0.f ? minf(g0_plain, h.g0_max) : g0_plain;   // clear of the upstream clump (step C6)
  const int r = role < HR_COUNT ? role : 0;
  const float pack_x = tx - 30.f;                                  // the pack beside the tent (CampLayout::pack)
  const float edge = minf(maxf(h.edge_x, 100.f), pack_x - 8.f);    // the pines' foliage ends here
  switch (spot) {
    case HS_LOOKIN: return Vec2{minf(edge + kLookInDx[r], pack_x - 48.f), (float)kLaneIn[r]};
    case HS_WAY2: return Vec2{pack_x + 12.f, (float)kLaneIn[HR_DOE]};
    case HS_FSTOP: return Vec2{pack_x + 12.f - 46.f, (float)kLaneIn[HR_FAWN]};
    case HS_G0: return Vec2{g0x, 358.f};
    case HS_GF: return Vec2{g0x - 54.f, 368.f};
    // The plan's G4 (F.x + 104) and G5 (the camp pine - 44) put the second doe 46 px from the yearling at the
    // door and the sixth 52 px from the doe, and a deer is about 52 px long: they touched. 14 and 24 px
    // further apart keeps a hand's width between every pair in the settled picture.
    case HS_G4: return Vec2{fx + (h.size == 1 ? 122.f : 132.f), 374.f};
    case HS_G4F: return Vec2{fx + (h.size == 1 ? 122.f : 132.f) - 52.f, 372.f};
    // (step C4: and never nearer than 80 px to the doe: where the fire is far right and the pine far left the
    // two stood nose to nose, 1 px apart on the real rigs)
    case HS_G5: return Vec2{maxf(h.camp_x - 20.f, g0x + 80.f), 372.f};
    case HS_T2WAIT: return Vec2{tx + 100.f, 361.f};
    case HS_T2HALF: return Vec2{tx + 88.f, 361.f};
    case HS_T2: return Vec2{tx + 76.f, 361.f};
    case HS_T2BACK: return Vec2{tx + 90.f, 361.f};
    case HS_D2OUT: return Vec2{tx + 94.f, 362.f};
    case HS_T3GRAZE: return Vec2{tx - 28.f, 370.f};
    case HS_T3: return Vec2{tx - 8.f, 370.f};
    case HS_WATER: return Vec2{st_x, st_y};
    case HS_SP26: return Vec2{st_x + 26.f, st_y};
    case HS_SM8: return Vec2{st_x - 8.f, st_y};
    case HS_SP10: return h.fawn_reed.x > 0.f ? h.fawn_reed : Vec2{st_x + 10.f, st_y};   // nose first into the reeds (4.2)
    case HS_FSLIP: return Vec2{g0x - 54.f + 10.f, (float)kLaneOut[HR_FAWN]};
    case HS_FLOOK: return Vec2{pack_x + 4.f, (float)kLaneOut[HR_FAWN]};
    case HS_FTENT: return Vec2{tx + 86.f, 371.f};
    case HS_OUT_L: return Vec2{kHerdExitLX, (float)kLaneOut[r]};
    case HS_OUT_R: return Vec2{kHerdExitRX, 346.f};
    case HS_N4: return h.nibble;
    case HS_N4VIA: return Vec2{h.nibble.x - 44.f, minf(378.f, h.nibble.y)};
    default: return Vec2{0.f, 0.f};
  }
}

float herdScale(uint8_t role) {
  static const float kScale[HR_COUNT] = {1.00f, 0.62f, 0.90f, 0.86f, 0.96f, 0.92f};
  return role < HR_COUNT ? kScale[role] : 1.f;
}

void herdReach(uint8_t role, bool facing_right, float& left, float& right) {
  // an adult: 22 behind its feet and 34 before (the nose); a fawn's head is big and its body short: 15 and 22
  const float k = herdScale(role);
  const float back = role == HR_FAWN ? 15.f : 22.f * k, front = role == HR_FAWN ? 22.f : 34.f * k;
  left = facing_right ? back : front;
  right = facing_right ? front : back;
}

float herdSoftAt(const HerdState& h) { return h.shy ? kHerdSoftAt - kHerdShyEarlier : kHerdSoftAt; }

// ---- the graze cycle -----------------------------------------------------------------------------------------

float herdGrazeNext(Rng& rng, bool head_down_now, float scale) {
  const float lo = head_down_now ? 3.5f : 1.2f;
  const float hi = head_down_now ? 7.0f : 3.0f;
  const float u = rng.range(lo, hi);
  return u * scale;
}

bool herdMayLower(const HerdState& h, int i) {
  for (int j = 0; j < h.n; ++j) {
    if (j == i) continue;
    const HerdDeer& o = h.d[j];
    if (!o.entered || o.mode == HM_GONE || o.mode == HM_OFF) continue;
    if (!o.head_down) return true;
  }
  return false;
}

namespace {

void emit(HerdOut& out, int deer, int cmd, int a, int b) {
  if (out.n >= HERD_EV_MAX) return;
  HerdEv& e = out.ev[out.n++];
  e.deer = (uint8_t)deer;
  e.cmd = (uint8_t)cmd;
  e.a = (int16_t)a;
  e.b = (int16_t)b;
}

const HerdCue* listOf(const HerdState& h, const HerdDeer& d, int& n) {
  if (d.list == HL_MAIN && d.role == HR_GRAZER && h.nibble_on) {   // the cattail first (step C4)
    const CueList l = kPool.nibble[h.variant > 2 ? 2 : h.variant];
    n = l.n;
    return kPool.c + l.off;
  }
  return herdCues(d.list, d.role, h.variant, n);
}

float bout(const HerdState& h, uint8_t in_flags, bool down) {
  float s = 1.f;
  if (h.shy) s *= 1.3f;
  if (down && (in_flags & HI_RAIN)) s *= 0.7f;
  if (!down && (in_flags & HI_RAIN)) s *= 0.8f;
  return s;
}

void grazeStart(HerdDeer& d, Rng& rng) {
  d.grazing = 1;
  d.mode = HM_GRAZE;
  d.head_down = 0;
  d.stepping = 0;
  d.next = rng.range(0.5f, 2.0f);   // it arrives with its head level: a moment before the first mouthful
}

// Would deer `i`, if it stood at `nx`, come within 8 px of another deer's body (x only: every body is taller
// than the lanes are far apart)?
bool stepClear(const HerdState& h, const HerdIn& in, int i, float nx) {
  const HerdDeer& d = h.d[i];
  float left, right;
  herdReach(d.role, d.face != 0, left, right);
  const float a0 = nx - left - 8.f, a1 = nx + right + 8.f;
  for (int j = 0; j < h.n; ++j) {
    const HerdDeer& o = h.d[j];
    if (j == i || o.mode == HM_GONE) continue;
    float ol, orr;
    herdReach(o.role, o.face != 0, ol, orr);
    // Whichever way the other faces now, it may turn (the doe to her fawn and back, one still walking to its
    // place): its longer end counts on both sides (step C4: two that had each stepped while the other faced
    // away stood nose to nose, 1 px apart).
    const float om = maxf(ol, orr);
    // where it stands now, and where it settles (a deer on its way there is not in the way yet)
    if (o.entered && o.mode != HM_OFF && a1 > in.x[j] - om && a0 < in.x[j] + om) return false;
    if (a1 > o.spot.x - om && a0 < o.spot.x + om) return false;
  }
  return true;
}

void execCue(HerdState& h, Rng& rng, const HerdIn& in, HerdOut& out, int i, const HerdCue& c) {
  HerdDeer& d = h.d[i];
  switch (c.cmd) {
    case HC_ENTER:
      d.mode = HM_CUES;
      d.entered = 1;
      d.face = 1;
      d.walk_ticks = 255;
      emit(out, i, HC_ENTER, c.a, c.b);
      break;
    case HC_WALK:
    case HC_RUN: {
      const Vec2 to = herdSpot(h, (uint8_t)c.a, d.role);
      if (to.x > in.x[i] + 0.5f) d.face = 1;
      else if (to.x < in.x[i] - 0.5f) d.face = 0;
      d.walk_ticks = 0;
      d.stepping = 0;
      emit(out, i, c.cmd, c.a, c.b);
      break;
    }
    case HC_BACK:
      d.walk_ticks = 0;
      emit(out, i, HC_BACK, c.a, c.b);
      break;
    case HC_FACE:
      d.face = (uint8_t)(c.a > 0 ? 1 : 0);
      emit(out, i, HC_FACE, c.a, c.b);
      break;
    case HC_ACT:
      emit(out, i, HC_ACT, c.a, c.b);
      break;
    case HC_GRAZE_ON:
      grazeStart(d, rng);
      break;
    case HC_GRAZE_OFF:
      d.grazing = 0;
      d.head_down = 0;
      d.stepping = 0;
      if (d.mode == HM_GRAZE) d.mode = HM_CUES;
      break;
    case HC_ALERT:
      d.alert = (uint8_t)(c.a != 0 ? 1 : 0);
      break;
    case HC_TAIL: {
      d.tail_to = (float)c.a * 0.01f;
      const float secs = maxf(0.02f, (float)c.b * 0.01f);
      d.tail_rate = absf(d.tail_to - d.tail) / secs;
      break;
    }
    case HC_STAMP:
      d.stamp = 0.f;
      d.stamp_kind = (uint8_t)c.a;
      break;
    case HC_EAR:
      d.ear = 4;
      break;
    case HC_NUDGE:
      d.nudge = (float)c.a * 0.01f;
      d.nudge_t = 0.f;
      d.nudge_d = maxf(0.05f, (float)c.b * 0.01f);
      break;
    case HC_HOLD:
      d.hold_t = (float)c.a * 0.01f;
      break;
    case HC_DOOR:
      if (c.a > 0) h.signs |= 1;
      emit(out, i, HC_DOOR, c.a, c.b);
      break;
    case HC_RING:
    case HC_DROPS:
    case HC_PRINT:
    case HC_WOBBLE:
    case HC_NIBBLE:
      emit(out, i, c.cmd, c.a, c.b);
      break;
    default:
      break;
  }
}

// When each deer leaves, from the alarm. Hard: all at once at +1.1 s, the fawn 0.3 s later. Soft: the doe at
// +6.0 s; each follower when the nearest deer to its right that is already going (the doe, for the one at
// the right end) is a body length (72 px) past it, and the fawn after the last of them (REVIEW_FIXES F12).
void scheduleLeaving(HerdState& h, const HerdIn& in, bool soft) {
  if (!soft) {
    for (int i = 0; i < h.n; ++i) {
      HerdDeer& d = h.d[i];
      if (d.mode != HM_ALERT) continue;
      d.leave = h.alarm_t + kHardOff + (d.role == HR_FAWN ? 0.3f : 0.f);
    }
    return;
  }
  struct S {
    int i;
    float x, v, t;
  };
  S sched[HERD_MAX];
  int ns = 0;
  const int doe = herdIndexOf(h, HR_DOE);
  float t_last = h.alarm_t + kSoftOff;
  if (doe >= 0 && h.d[doe].mode == HM_ALERT) {
    h.d[doe].leave = h.alarm_t + kSoftOff;
    sched[ns++] = S{doe, in.x[doe], kExitSpeed[HR_DOE], h.alarm_t + kSoftOff};
  }
  static const uint8_t kChain[4] = {HR_GRAZER2, HR_GRAZER, HR_DOOR, HR_WALL};
  for (int k = 0; k < 4; ++k) {
    const int j = herdIndexOf(h, kChain[k]);
    if (j < 0 || h.d[j].mode != HM_ALERT) continue;
    const float xj = in.x[j];
    float t = h.alarm_t + kSoftOff + 0.6f;
    bool any = false;
    for (int s = 0; s < ns; ++s) {   // after every deer already going that is at or to the right of this one
      const float dx = sched[s].x - xj;
      if (dx < -8.f) continue;
      const float vj = kExitSpeed[kChain[k]];
      const float closing = maxf(0.f, vj - sched[s].v) * xj / vj;   // a faster follower gains on the way to the edge
      const float run = maxf(0.f, dx + kBody + closing);
      const float ts = sched[s].t + maxf(0.6f, run / sched[s].v);
      if (!any || ts > t) t = ts;
      any = true;
    }
    if (!any && ns > 0) {   // none to the right (the sixth, at the right end): right behind the doe
      const float run = maxf(0.f, sched[0].x - xj + kBody);
      t = sched[0].t + maxf(0.6f, run / sched[0].v);
    }
    h.d[j].leave = t;
    if (ns < HERD_MAX) sched[ns++] = S{j, xj, kExitSpeed[kChain[k]], t};
    if (t > t_last) t_last = t;
  }
  const int fawn = herdIndexOf(h, HR_FAWN);
  if (fawn >= 0 && h.d[fawn].mode == HM_ALERT) h.d[fawn].leave = t_last + 1.1f;
  if (ns == 0) {   // no doe and nobody else on the chain: whoever is left goes after a beat
    for (int i = 0; i < h.n; ++i)
      if (h.d[i].mode == HM_ALERT && h.d[i].leave < 0.f) h.d[i].leave = h.alarm_t + kSoftOff;
  }
}

void startSecondLook(HerdState& h) {
  h.second_done = 1;
  h.alarm = 0;
  h.trigger = 0;
  h.gone_t = -1.f;
  for (int i = 0; i < h.n; ++i) {
    HerdDeer& d = h.d[i];
    if (d.role != HR_DOE && d.role != HR_FAWN) continue;
    const float ear = d.ear_next, fl = d.flick_next;
    d.mode = HM_OFF;
    d.list = HL_SECOND;
    d.cue = 0;
    d.base = h.second_t;
    d.leave = -1.f;
    d.alert = 0;
    d.ear = 0;
    d.head_down = 0;
    d.grazing = 0;
    d.stamp = -1.f;
    d.lift = 0.f;
    d.tail = d.tail_to = 0.f;
    d.nudge_d = 0.f;
    d.look_t = 0.f;
    d.hold_t = 0.f;
    d.entered = 0;
    d.walk_ticks = 255;
    d.hard = 0;
    d.ear_next = ear;
    d.flick_next = fl;
  }
}

}  // namespace

void herdInit(HerdState& h, Rng& rng, const HerdLayout& L, uint8_t plan, bool shy) {
  h = HerdState{};
  h.variant = (uint8_t)(herdVariantOf(plan) < 0 ? 0 : herdVariantOf(plan));
  h.size = (uint8_t)herdSizeOf(plan);
  h.shy = shy ? 1 : 0;
  h.tent = L.tent;
  h.door = L.door;
  h.splash = L.splash;
  h.fire = L.fire;
  h.camp_x = L.camp_x;
  h.edge_x = L.edge_x > 0.f ? L.edge_x : 150.f;
  h.nibble_on = L.nibble_on ? 1 : 0;
  h.nibble = L.nibble;
  h.fawn_reed = L.fawn_reed;
  h.g0_max = L.g0_max;
  h.alarm_t = -1.f;
  h.gone_t = -1.f;
  h.seen_t = -1.f;
  h.wobble_t = -1.f;
  h.second_t = -1.f;
  h.last_change = -10.f;
  uint8_t roles[HERD_MAX];
  h.n = (uint8_t)herdRoles(h.size, roles);
  for (int i = 0; i < h.n; ++i) {
    HerdDeer& d = h.d[i];
    d.slot = -1;
    d.role = roles[i];
    d.mode = HM_OFF;
    d.list = HL_MAIN;
    d.leave = -1.f;
    d.stamp = -1.f;
    d.walk_ticks = 255;
    d.face = 1;
    d.ear_next = rng.range(1.5f, 6.f);
    d.flick_next = rng.range(5.f, 12.f);
  }
  auto set = [&](int role, uint8_t spot) {
    const int i = herdIndexOf(h, (uint8_t)role);
    if (i >= 0) h.d[i].spot = herdSpot(h, spot, (uint8_t)role);
  };
  set(HR_DOE, HS_G0);
  set(HR_FAWN, HS_GF);
  set(HR_DOOR, HS_D2OUT);
  set(HR_WALL, HS_T3);
  set(HR_GRAZER, HS_G4);
  set(HR_GRAZER2, HS_G5);
}

void herdAlarm(HerdState& h, int kind, const HerdIn& in) {
  if (h.alarm != 0) return;
  const bool soft = kind == HT_TIME;
  h.alarm = soft ? 1 : 2;
  h.trigger = (uint8_t)kind;
  h.flee = kind == HT_DOG ? 1 : -1;
  h.alarm_t = h.t;
  for (int i = 0; i < h.n; ++i) {
    HerdDeer& d = h.d[i];
    if (d.mode == HM_GONE) continue;
    if (d.entered == 0) {   // not in yet: it turns back and is let go
      d.mode = HM_GONE;
      continue;
    }
    d.grazing = 0;
    d.head_down = 0;
    d.stepping = 0;
    d.look_t = 0.f;
    d.hold_t = 0.f;
    d.leave = -1.f;
    d.hard = soft ? 0 : 1;
    d.cue = 0;
    d.base = h.t;
    if (in.x[i] < -2.f) {   // still off the left edge: turns back at once, at a canter
      d.list = HL_EXIT_HARD_L;
      d.mode = HM_OUT;
      d.hard = 1;
      continue;
    }
    d.list = soft ? HL_WAVE_SOFT : HL_WAVE_HARD;
    d.mode = HM_ALERT;
    // Four deer (no second doe between the fawn and the yearlings): theirs come up half a second sooner so
    // the gaps between heads stay 0.3 to 0.7 s.
    if (soft && h.size == 2 && (d.role == HR_DOOR || d.role == HR_WALL)) d.base = h.t - 0.5f;
  }
  // A bell or a flash before the doe has stepped in leaves the camp as empty as one that sent them all off:
  // the clock for the second look starts now, or the whole beat would pass with no deer at all.
  if (!soft && h.alive == 0) h.gone_t = h.t;
  scheduleLeaving(h, in, soft);
}

void herdTick(HerdState& h, Rng& rng, const HerdIn& in, HerdOut& out) {
  out.n = 0;
  const float dt = in.dt;
  h.t += dt;

  // ---- what sets the alarm off
  const bool dog = (in.flags & HI_DOG) != 0, passer = (in.flags & HI_PASSER) != 0;
  const bool dog_edge = dog && (h.prev_in & 2) == 0;
  const bool passer_edge = passer && (h.prev_in & 1) == 0;
  if (h.alarm == 0) {
    if ((in.flags & HI_BELL) != 0) herdAlarm(h, HT_BELL, in);
    else if (dog_edge) herdAlarm(h, HT_DOG, in);
    else if ((in.flags & HI_FLASH) != 0) herdAlarm(h, HT_FLASH, in);
    else if (h.t >= herdSoftAt(h)) herdAlarm(h, HT_TIME, in);
  }
  // A second look: a bell or a flash before 45 s emptied the camp and no dog is about: 14 s after the last
  // deer left the doe and the fawn come in again.
  if (h.alarm != 0 && h.trigger != HT_TIME && h.trigger != HT_DOG && !h.second_done && h.second_t < 0.f &&
      h.alive == 0 && h.gone_t >= 0.f && h.alarm_t < 45.f && !dog)
    h.second_t = h.gone_t + 14.f;
  if (h.second_t >= 0.f && !h.second_done && h.t >= h.second_t && !dog) startSecondLook(h);

  // ---- a visitor at the panel: the doe and the two deer nearest the middle lift their heads and hold 3 s
  if (passer_edge && h.alarm == 0 && h.looks < 2) {
    int idx[HERD_MAX], m = 0;
    float mean = 0.f;
    for (int i = 0; i < h.n; ++i)
      if (h.d[i].mode == HM_GRAZE && h.d[i].look_t <= 0.f) {
        idx[m++] = i;
        mean += in.x[i];
      }
    if (m > 0) {
      mean /= (float)m;
      ++h.looks;
      int chosen = 0;
      bool used[HERD_MAX] = {false, false, false, false, false, false};
      for (int pass = 0; pass < 3 && chosen < m; ++pass) {
        int pick = -1;
        float best = 1e9f;
        for (int k = 0; k < m; ++k) {
          const int i = idx[k];
          if (used[k]) continue;
          float score = absf(in.x[i] - mean);
          if (pass == 0 && h.d[i].role != HR_DOE) score += 1000.f;   // the doe first, if she is grazing
          if (score < best) {
            best = score;
            pick = k;
          }
        }
        if (pick < 0) break;
        used[pick] = true;
        ++chosen;
        HerdDeer& d = h.d[idx[pick]];
        d.look_t = 3.f * (h.shy ? 1.3f : 1.f);
        d.head_down = 0;
        emit(out, idx[pick], HC_ACT, (int)CA_HEADTURN, 35);
      }
    }
  }
  h.prev_in = (uint8_t)((passer ? 1 : 0) | (dog ? 2 : 0));

  // ---- the deer
  for (int i = 0; i < h.n; ++i) {
    HerdDeer& d = h.d[i];
    if (d.mode == HM_GONE) continue;
    if (d.ear > 0) --d.ear;       // a flick set last tick or this one is seen at 4, 3, 2, 1 and then gone
    if (d.flick > 0) --d.flick;
    if (d.entered > 0 && d.entered < 255) ++d.entered;
    if (d.walk_ticks < 255) ++d.walk_ticks;
    if (d.hold_t > 0.f) d.hold_t -= dt;
    if (d.entered > 3 && !in.in_use[i]) {   // out of sight and let go by the director
      d.mode = HM_GONE;
      d.grazing = 0;
      emit(out, i, HE_GONE, 0, 0);
      continue;
    }

    // the walk out starts when the wave is over and its time has come
    if (d.mode == HM_ALERT && d.leave >= 0.f && h.t >= d.leave) {
      int wn = 0;
      herdCues(d.list, d.role, h.variant, wn);
      if (d.cue >= wn) {
        d.list = d.hard ? (h.flee > 0 ? HL_EXIT_HARD_R : HL_EXIT_HARD_L) : HL_EXIT_SOFT;
        d.cue = 0;
        d.base = d.leave;
        d.mode = HM_OUT;
        d.alert = 3;
      }
    }

    // the cue list
    {
      int n = 0;
      const HerdCue* L = listOf(h, d, n);
      int guard = 0;
      while (L && d.cue < n && d.hold_t <= 0.f && guard++ < 24) {
        const HerdCue& c = L[d.cue];
        if (h.t < d.base + (float)c.at * 0.01f) break;
        if (c.wait && !(in.arrived[i] && d.walk_ticks >= 2)) break;
        execCue(h, rng, in, out, i, c);
        ++d.cue;
      }
    }
    if (d.mode == HM_OFF) continue;   // not in the scene yet

    // the graze cycle
    if (d.grazing && d.mode == HM_GRAZE) {
      if (d.look_t > 0.f) {
        d.look_t -= dt;
        if (d.look_t <= 0.f) {
          d.look_t = 0.f;
          d.next = rng.range(0.4f, 1.2f);
        }
      } else {
        d.next -= dt;
        if (d.next <= 0.f) {
          if (h.t - h.last_change < 0.3f) {
            d.next = 0.3f;   // a change that would land with another's is put back
          } else if (d.head_down) {   // head up, chewing, a look round now and then
            const bool look = rng.chance(0.3f);
            const float up_s = rng.range(0.8f, 1.1f);   // a relaxed head comes up in about a second, not a snap
            emit(out, i, HC_ACT, look ? (int)CA_LOOK : (int)CA_IDLE, (int)(up_s * 100.f + 0.5f));
            d.head_down = 0;
            d.next = herdGrazeNext(rng, false, bout(h, in.flags, false));
            h.last_change = h.t;
            d.stepping = 0;
          } else if (d.stepping) {   // the step is done: back down
            if (!in.arrived[i] || d.walk_ticks < 2) {
              d.next = 0.2f;
            } else if (herdMayLower(h, i)) {
              const float down_s = rng.range(0.9f, 1.2f);
              emit(out, i, HC_ACT, (int)CA_EAT, (int)(down_s * 100.f + 0.5f));
              d.head_down = 1;
              d.stepping = 0;
              d.next = herdGrazeNext(rng, true, bout(h, in.flags, true));
              h.last_change = h.t;
            } else {
              d.next = 0.3f;
            }
          } else if (!herdMayLower(h, i)) {   // the watch rule: never all heads down; she is the last one up
            d.next = 0.3f;
          } else {
            const float px = rng.range(6.f, 10.f);
            const bool want_step = rng.chance(0.45f);   // (0.3 until step C4: too few steps were ever taken)
            const float dirf = d.face ? 1.f : -1.f;
            const float nx = in.x[i] + dirf * px;
            if (want_step && absf(nx - d.spot.x) <= 30.f && stepClear(h, in, i, nx)) {   // one step forward, kept near its place and off its neighbours
              emit(out, i, HE_STEP, (int)(px + 0.5f), 9);
              d.walk_ticks = 0;
              d.stepping = 1;
              d.next = px / 9.f + 0.4f;
            } else {
              const float down_s = rng.range(0.9f, 1.2f);
              emit(out, i, HC_ACT, (int)CA_EAT, (int)(down_s * 100.f + 0.5f));
              d.head_down = 1;
              d.next = herdGrazeNext(rng, true, bout(h, in.flags, true));
              h.last_change = h.t;
            }
          }
        }
      }
    }

    // the ear and the flag's own small life
    if (d.mode != HM_OUT) {
      d.ear_next -= dt;
      if (d.ear_next <= 0.f) {
        d.ear = 4;
        const float gap = rng.range(2.f, 6.f);
        d.ear_next = (in.flags & HI_WIND) != 0 ? 0.5f * gap : gap;
      }
      if (d.tail_to <= 0.f && d.tail < 0.05f && d.alert == 0) {
        d.flick_next -= dt;
        if (d.flick_next <= 0.f) {
          d.flick = 4;
          d.flick_next = rng.range(8.f, 15.f);
        }
      }
    }
    if (d.tail != d.tail_to) {
      const float step = d.tail_rate * dt;
      if (absf(d.tail_to - d.tail) <= step) d.tail = d.tail_to;
      else d.tail += d.tail_to > d.tail ? step : -step;
    }
    if (d.stamp >= 0.f) {
      d.stamp += dt;
      const float u = d.stamp;
      if (d.stamp_kind == 2) {   // pawing the water: the hoof up and down in 0.3 s
        if (u < 0.15f) d.lift = 0.5f * easeInOut(u / 0.15f);
        else if (u < 0.3f) d.lift = 0.5f * (1.f - easeInOut((u - 0.15f) / 0.15f));
        else {
          d.lift = 0.f;
          d.stamp = -1.f;
          emit(out, i, HE_HIT, 2, 0);
        }
      } else {   // a stamp: lift in 0.2 s, hold 0.15, strike down in 0.08
        if (u < 0.2f) d.lift = 0.55f * easeInOut(u / 0.2f);
        else if (u < 0.35f) d.lift = 0.55f;
        else if (u < 0.43f) d.lift = 0.55f * (1.f - (u - 0.35f) / 0.08f);
        else {
          d.lift = 0.f;
          d.stamp = -1.f;
          emit(out, i, HE_HIT, d.stamp_kind, 0);
        }
      }
    }
    if (d.nudge_d > 0.f) {
      d.nudge_t += dt;
      if (d.nudge_t >= d.nudge_d) d.nudge_d = 0.f;
    }
  }

  // ---- who is left
  int alive = 0;
  for (int i = 0; i < h.n; ++i)
    if (h.d[i].entered > 0 && h.d[i].mode != HM_GONE && h.d[i].mode != HM_OFF) ++alive;
  if (h.alive > 0 && alive == 0) h.gone_t = h.t;
  h.alive = (uint8_t)alive;
}

HerdView herdView(const HerdDeer& d) {
  HerdView v;
  v.ear = 0;
  if (d.ear > 0) {   // a flick: half back, back, back, half back
    static const uint8_t kSeq[5] = {0, kDeerEarMid, kDeerEarBack, kDeerEarBack, kDeerEarMid};
    v.ear = kSeq[d.ear > 4 ? 4 : d.ear];
  } else if (d.alert == 1 || d.alert == 3 || d.look_t > 0.f) {
    v.ear = kDeerEarFwd;
  }
  float tail = d.tail;
  if (d.flick > 0) {   // a tail flick: the flag shows at 0.35 for four ticks, eased in and out
    static const float kRamp[5] = {0.f, 0.2f, 0.35f, 0.35f, 0.2f};
    tail = maxf(tail, kRamp[d.flick > 4 ? 4 : d.flick]);
  }
  v.tail = tail;
  v.head_add = 0.f;
  if (d.nudge_d > 0.f) v.head_add = d.nudge * sinT(0.5f * clamp01(d.nudge_t / d.nudge_d));
  v.fore_add = d.lift;
  return v;
}

// ---- the hoof prints ------------------------------------------------------------------------------------------

bool herdPrintAdd(HerdState& h, int x, int y, bool wet) {
  if (h.n_prints >= HERD_PRINTS) return false;
  HerdPrint& p = h.print[h.n_prints++];
  p.x = (int16_t)x;
  p.y = (int16_t)y;
  p.a0 = wet ? 152 : 88;
  p.a1 = wet ? 72 : 88;
  const float ds = h.t * 10.f;
  p.made = (uint16_t)(ds < 0.f ? 0.f : (ds > 65535.f ? 65535.f : ds));
  h.signs |= 2;
  return true;
}

void herdPrintsSeen(HerdState& h) {
  if (h.seen_t < 0.f) h.seen_t = 0.f;
  h.signs |= 4;
}

void herdPrintsStep(HerdState& h, float dt) {
  h.t += dt;
  if (h.seen_t >= 0.f) h.seen_t += dt;
}

uint8_t herdPrintAlpha(const HerdState& h, int i) {
  if (i < 0 || i >= h.n_prints) return 0;
  const HerdPrint& p = h.print[i];
  float a = (float)p.a0;
  if (p.a1 != p.a0) {   // wet: dries over 40 s
    const float age = h.t - (float)p.made * 0.1f;
    a = (float)p.a0 + ((float)p.a1 - (float)p.a0) * clamp01(age / 40.f);
  }
  if (h.seen_t >= 0.f) {   // seen: gone in 8 steps over 25 s
    int k = (int)(h.seen_t * 8.f / 25.f);
    if (k > 8) k = 8;
    a *= (float)(8 - k) / 8.f;
  }
  int q = ((int)(a + 4.f) / 8) * 8;
  if (q > 248) q = 248;
  return (uint8_t)q;
}

bool herdPrintsLive(const HerdState& h) {
  for (int i = 0; i < h.n_prints; ++i)
    if (herdPrintAlpha(h, i) > 0) return true;
  return false;
}

bool herdPrintsDone(const HerdState& h) { return h.seen_t >= 0.f && !herdPrintsLive(h); }

static Rect printRect(const HerdPrint& p) { return Rect{p.x - 1, p.y - 1, p.x + 6, p.y + 4}; }

static Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
              a.y1 > b.y1 ? a.y1 : b.y1};
}

Rect herdPrintsBounds(const HerdState& h) {
  Rect r{0, 0, 0, 0};
  for (int i = 0; i < h.n_prints; ++i)
    if (herdPrintAlpha(h, i) > 0) r = unite(r, printRect(h.print[i]));
  return r;
}

void herdPrintsDraw(const HerdState& h, Surface& s, const Palette& pal, const Rect& clip) {
  const Rect c = clip.clipped(s.bounds());
  for (int i = 0; i < h.n_prints; ++i) {
    const uint8_t a = herdPrintAlpha(h, i);
    if (a == 0) continue;
    const HerdPrint& p = h.print[i];
    for (int dy = 0; dy < 3; ++dy) {
      const int y = p.y + dy;
      if (y < c.y0 || y >= c.y1) continue;
      for (int k = 0; k < 2; ++k) {   // two dashes side by side: a cloven print
        int x0 = p.x + k * 3, x1 = x0 + 2;
        if (x0 < c.x0) x0 = c.x0;
        if (x1 > c.x1) x1 = c.x1;
        if (x1 > x0) blendSpan(s, y, x0, x1, pal[ACTOR], a);
      }
    }
  }
}

bool herdDirty(const HerdState& prev, const HerdState& cur, Rect& out) {
  out = Rect{0, 0, 0, 0};
  const int n = prev.n_prints > cur.n_prints ? prev.n_prints : cur.n_prints;
  for (int i = 0; i < n; ++i) {
    const uint8_t pa = herdPrintAlpha(prev, i), ca = herdPrintAlpha(cur, i);
    const bool same_pos = i < prev.n_prints && i < cur.n_prints && prev.print[i].x == cur.print[i].x &&
                          prev.print[i].y == cur.print[i].y;
    if (pa == ca && (same_pos || (pa == 0 && ca == 0))) continue;
    if (pa > 0 && i < prev.n_prints) out = unite(out, printRect(prev.print[i]));
    if (ca > 0 && i < cur.n_prints) out = unite(out, printRect(cur.print[i]));
  }
  return !out.empty();
}

bool herdSame(const HerdState& a, const HerdState& b) {
  Rect r;
  return !herdDirty(a, b, r);
}

}  // namespace ss
