// Stage 22 step C0: the plan block (INTEGRATION_PLAN.md 3.1 points 7, 9, 10, 11 and section 4.8;
// REVIEW_FIXES F26; Mike, 2026-11-18).
//
// What it checks:
//   - the switch: with SS_S22 off no piece plans, whatever Memory::s22_preview says, and a plan with the
//     preview byte set is byte for byte the plan without it (the off folder plans as before stage 22);
//   - the draws: with the pieces previewed (bit 7 of s22_preview), the evening plans byte for byte as
//     before; elsewhere a plan that picks the same majors takes the same loop rng draws, and the same plan
//     twice is the same. (Rng::weighted takes no draw when every weight is 0, so a round with no free slot
//     left skips one: a plan that picks other majors can end with another loop rng state, as it already
//     could before stage 22.)
//   - the gates: tai chi by day and dry only, forced in the day's first morning loop (taichi_next bit 2
//     clear), counted as two; the long shot by day, dry, no fog, no house dog, never with the plinking or
//     ranger_ridge; the deer visit (herdPlanPick) never with a dog out, the data's rain or storm, a
//     visit in the last midday loop or a ripe patch, and a herd loop has at most two majors and none of
//     deer_stick, plink, longshot; the mushroom only when ripe, at midday by day in dry data weather,
//     alone or after a dog's fetch;
//   - the rates of INTEGRATION_PLAN 5.6 over 400 seeds per family (printed), and every loop 480 to 660 s;
//   - the fit (spineFitLength): the floor is one major with tai chi or the mushroom, and a loop whose
//     majors are all kept (a forced fetch and the mushroom, four fillers) drops neither.
// Until steps C2 to C5 land, no piece plans in a real run (the named constants in majors.cpp): this test
// previews them through Memory::s22_preview. With SS_S22 off only the switch checks run.
// Plain executable; returns non-zero on failure.
#include "ss_app.h"
#include "mush_patch.h"
#include "scene/day_herd.h"
#include "scene/fetch.h"
#include "scene/spine_family.h"
#include "scene/stage22_switch.h"
#include "scene/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static const uint8_t kPreviewAll = (uint8_t)(S22_PREVIEW | S22_ALL);

static Memory freshMemory(uint8_t preview) {
  Memory m;
  memset(&m, 0, sizeof m);
  m.s22_preview = preview;
  return m;
}

// code < 0: no OUTSIDE reading (the plan sees a clear sky).
static Inputs makeInputs(float mood, int dog_out, int code, bool night) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.dog_out = dog_out;
  in.have_outside = code >= 0;
  in.weather_code = code >= 0 ? code : 0;
  in.night_mode = night;
  in.weather.night_dim = night ? 1.f : 0.f;
  return in;
}

static uint64_t loopSeed(uint64_t seed, int loop) { return seed * 1000003ull + (uint64_t)loop * 7919ull + 1ull; }

// One loop's plan as beginLoop makes it (family first, then spinePlan on the loop rng). `rng_out`: the
// loop rng after the plan.
static void planLoop(LoopPlan& p, uint8_t family, uint64_t seed, const Inputs& in, Memory& m, Rng* rng_out = nullptr) {
  memset(&p, 0, sizeof p);
  p.family = family;
  Rng r;
  r.seed(seed);
  spinePlan(p, r, in, m);
  if (rng_out) *rng_out = r;
}

static bool has(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return true;
  return false;
}
static int count(const LoopPlan& p, int id) {
  int n = 0;
  for (int i = 0; i < p.n_majors; ++i) n += p.major[i] == id;
  return n;
}
static float loopSeconds(const LoopPlan& p) { return (float)spineLoopTicks(p) / (float)SS_TICK_HZ; }
static bool lengthOk(const LoopPlan& p) {
  const float s = loopSeconds(p);
  return s >= (float)SS_LOOP_MIN_SECONDS - 0.01f && s <= (float)SS_LOOP_MAX_SECONDS + 0.01f;
}
static const char* majorsOf(const LoopPlan& p, char* buf, size_t n) {
  buf[0] = 0;
  size_t k = 0;
  for (int i = 0; i < p.n_majors && k + 1 < n; ++i) {
    const int m = snprintf(buf + k, n - k, "%s%s", i ? "," : "", majorName(p.major[i]));
    if (m > 0) k += (size_t)m;
  }
  return buf;
}

// The pairs of INTEGRATION_PLAN 4.8 that must never share a loop, and the herd's cap.
static void checkNeverTogether(const LoopPlan& p, const char* what, uint64_t seed, int loop) {
  char b[160];
  int hill = 0;
  hill += has(p, MJ_PLINK) ? 1 : 0;
  hill += has(p, MJ_LONGSHOT) ? 1 : 0;
  hill += has(p, MJ_RANGER_RIDGE) ? 1 : 0;
  CHECK(hill <= 1 || !has(p, MJ_LONGSHOT), "%s seed %llu loop %d: the long shot with another hillside major (%s)",
        what, (unsigned long long)seed, loop, majorsOf(p, b, sizeof b));
  if (p.herd) {
    CHECK(p.n_majors <= 2, "%s seed %llu loop %d: a herd loop with %d majors (%s)", what, (unsigned long long)seed, loop,
          (int)p.n_majors, majorsOf(p, b, sizeof b));
    CHECK(!has(p, MJ_DEER_STICK) && !has(p, MJ_PLINK) && !has(p, MJ_LONGSHOT) && !has(p, MJ_MUSHROOM),
          "%s seed %llu loop %d: a herd loop with %s", what, (unsigned long long)seed, loop, majorsOf(p, b, sizeof b));
    CHECK(herdVariant(p.herd) >= 1 && herdVariant(p.herd) <= 3 && herdSize(p.herd) <= 2 && (p.herd >> 4) == 0,
          "%s seed %llu loop %d: herd byte %u", what, (unsigned long long)seed, loop, (unsigned)p.herd);
  }
  if (has(p, MJ_MUSHROOM)) {
    const bool alone = p.n_majors == 1;
    const bool after_fetch = p.n_majors == 2 && has(p, MJ_FETCH);
    CHECK(alone || after_fetch, "%s seed %llu loop %d: the mushroom with %s", what, (unsigned long long)seed, loop,
          majorsOf(p, b, sizeof b));
  }
  if (has(p, MJ_TAICHI))
    CHECK(p.n_majors <= 3 && count(p, MJ_TAICHI) == 1, "%s seed %llu loop %d: tai chi with %s", what,
          (unsigned long long)seed, loop, majorsOf(p, b, sizeof b));
  for (int i = 0; i < p.n_majors; ++i) {
    const int id = p.major[i];
    if (id == MJ_TAICHI) CHECK(p.family == SF_MORNING, "%s: tai chi in family %d", what, (int)p.family);
    if (id == MJ_LONGSHOT || id == MJ_MUSHROOM) CHECK(p.family == SF_MIDDAY, "%s: %s in family %d", what, majorName(id), (int)p.family);
  }
  if (p.family != SF_MIDDAY) CHECK(p.herd == 0, "%s: herd byte %u outside the midday", what, (unsigned)p.herd);
}

// ---- the switch ------------------------------------------------------------------------------

struct Fam {
  const char* name;
  uint8_t family;
  float mood;
  bool night;
};
static const Fam kFams[] = {{"evening", SF_EVENING, 4.f, false}, {"night", SF_EVENING, 0.f, true},
                            {"morning", SF_MORNING, 1.5f, false}, {"midday", SF_MIDDAY, 3.f, false}};

static void testSwitch() {
  const Memory none = freshMemory(0);
  const Memory all = freshMemory(kPreviewAll);
  const Memory off = freshMemory(S22_PREVIEW);
  printf("switch: SS_S22 %d; pieces that plan in a real run: %02x; with the preview byte %02x: %02x\n", ssS22Switch(),
         (unsigned)majorsS22Plans(none), (unsigned)kPreviewAll, (unsigned)majorsS22Plans(all));
  CHECK(majorsS22Plans(off) == 0, "the preview byte with no pieces named plans %02x", (unsigned)majorsS22Plans(off));
  if (ssS22Switch() == 0) {
    CHECK(majorsS22Plans(none) == 0 && majorsS22Plans(all) == 0, "SS_S22 off plans pieces %02x / %02x",
          (unsigned)majorsS22Plans(none), (unsigned)majorsS22Plans(all));
  } else {
    CHECK(majorsS22Plans(all) == S22_ALL, "the preview byte plans %02x", (unsigned)majorsS22Plans(all));
  }
  // The off build ignores the preview: the plans and the memories are the same byte for byte, for every
  // family and a ripe patch with a dog out and without (with SS_S22 on this only holds where nothing new
  // plays, so it is checked off only).
  if (ssS22Switch() != 0) return;
  int plans = 0;
  for (const Fam& f : kFams)
    for (int dog = 0; dog <= 1; ++dog)
      for (uint64_t s = 1; s <= 60; ++s) {
        const Inputs in = makeInputs(f.mood, dog, -1, f.night);
        Memory a = freshMemory(0), b = freshMemory(kPreviewAll);
        a.mush = b.mush = mushPack(MUSH_RIPE, 0, true);
        for (int loop = 0; loop < 4; ++loop) {
          LoopPlan pa, pb;
          Rng ra, rb;
          planLoop(pa, f.family, loopSeed(s, loop), in, a, &ra);
          planLoop(pb, f.family, loopSeed(s, loop), in, b, &rb);
          b.s22_preview = 0;
          CHECK(memcmp(&pa, &pb, sizeof pa) == 0 && memcmp(&a, &b, sizeof a) == 0 && ra.state == rb.state,
                "off: %s seed %llu loop %d plans differently with the preview byte", f.name, (unsigned long long)s, loop);
          b.s22_preview = kPreviewAll;
          ++plans;
        }
      }
  printf("switch: %d plans with SS_S22 off are the same with and without the preview byte\n", plans);
}

// ---- the draws ---------------------------------------------------------------------------------

static bool sameMajors(const LoopPlan& a, const LoopPlan& b) {
  if (a.n_majors != b.n_majors) return false;
  for (int i = 0; i < a.n_majors; ++i)
    if (a.major[i] != b.major[i] || a.major_after[i] != b.major_after[i]) return false;
  return true;
}

static void testDraws() {
  int plans = 0, evening_same = 0, same_picks = 0, moved = 0;
  static const int kCodes[4] = {-1, 1, 5, 7};   // none, rain, fog, storm
  for (const Fam& f : kFams)
    for (int dog = 0; dog <= 1; ++dog)
      for (int c = 0; c < 4; ++c)
        for (int ripe = 0; ripe <= 1; ++ripe)
          for (uint64_t s = 1; s <= 25; ++s) {
            const Inputs in = makeInputs(f.mood, dog, kCodes[c], f.night);
            Memory a = freshMemory(0), b = freshMemory(kPreviewAll);
            a.mush = b.mush = ripe ? mushPack(MUSH_RIPE, 0, true) : 0;
            for (int loop = 0; loop < 3; ++loop) {
              LoopPlan pa, pb, pc;
              Rng ra, rb, rc;
              Memory c2 = b;   // the same plan again from the same memory: deterministic
              planLoop(pa, f.family, loopSeed(s, loop), in, a, &ra);
              planLoop(pb, f.family, loopSeed(s, loop), in, b, &rb);
              planLoop(pc, f.family, loopSeed(s, loop), in, c2, &rc);
              CHECK(memcmp(&pb, &pc, sizeof pb) == 0 && memcmp(&b, &c2, sizeof b) == 0 && rb.state == rc.state,
                    "%s seed %llu loop %d: the same plan twice differs", f.name, (unsigned long long)s, loop);
              if (sameMajors(pa, pb)) {
                ++same_picks;
                CHECK(ra.state == rb.state, "%s seed %llu loop %d dog %d code %d ripe %d: the same picks moved the loop rng",
                      f.name, (unsigned long long)s, loop, dog, kCodes[c], ripe);
              } else {
                moved += ra.state != rb.state ? 1 : 0;
              }
              checkNeverTogether(pb, f.name, s, loop);
              CHECK(lengthOk(pb), "%s seed %llu loop %d: %.1f s", f.name, (unsigned long long)s, loop, (double)loopSeconds(pb));
              if (f.family == SF_EVENING) {
                Memory bb = b;
                bb.s22_preview = 0;
                const bool same = memcmp(&pa, &pb, sizeof pa) == 0 && memcmp(&a, &bb, sizeof a) == 0 && ra.state == rb.state;
                CHECK(same, "evening seed %llu loop %d: the evening plan moved with the pieces previewed", (unsigned long long)s, loop);
                evening_same += same ? 1 : 0;
              }
              ++plans;
            }
          }
  printf("draws: %d plans; %d pick the same majors with and without the pieces and take the same loop rng draws; of the"
         " rest %d end with another loop rng state (no free slot left in another round); %d evening plans byte for byte\n",
         plans, same_picks, moved, evening_same);
}

// ---- the gates ---------------------------------------------------------------------------------

static void testTaichiGates() {
  int forced = 0, n_first = 0, later = 0, n_later = 0, max_n = 0, wake_slot = 0;
  for (uint64_t s = 1; s <= 200; ++s) {
    // The day's first morning loop, by day and dry: tai chi, always, once, after the wake.
    {
      Memory m = freshMemory(kPreviewAll);
      LoopPlan p;
      planLoop(p, SF_MORNING, loopSeed(s, 0), makeInputs(1.5f, 0, -1, false), m);
      ++n_first;
      forced += has(p, MJ_TAICHI) ? 1 : 0;
      for (int i = 0; i < p.n_majors; ++i)
        if (p.major[i] == MJ_TAICHI) wake_slot += p.major_after[i] == 0 ? 1 : 0;   // MB_WAKE, SY_WAKE
      if (p.n_majors > max_n) max_n = p.n_majors;
      checkNeverTogether(p, "morning first", s, 0);
      CHECK(lengthOk(p), "morning first seed %llu: %.1f s", (unsigned long long)s, (double)loopSeconds(p));
    }
    // Done this morning (bit 2): drawn by weight only.
    {
      Memory m = freshMemory(kPreviewAll);
      m.taichi_next = 4;
      LoopPlan p;
      planLoop(p, SF_MORNING, loopSeed(s, 0), makeInputs(1.5f, 0, -1, false), m);
      ++n_later;
      later += has(p, MJ_TAICHI) ? 1 : 0;
      CHECK(m.taichi_next == 4, "a morning loop cleared the done bit (%02x)", (unsigned)m.taichi_next);
    }
    // Night, and the data's rain, snow or storm: never, forced or not.
    static const int kWet[3] = {1, 2, 7};
    for (int c = -1; c < 3; ++c) {
      Memory m = freshMemory(kPreviewAll);
      LoopPlan p;
      const bool night = c < 0;
      planLoop(p, SF_MORNING, loopSeed(s, 0), makeInputs(night ? 0.2f : 1.5f, 0, night ? -1 : kWet[c], night), m);
      CHECK(!has(p, MJ_TAICHI), "seed %llu: tai chi at %s", (unsigned long long)s, night ? "night" : "a wet morning");
    }
    // Not planned without the preview while step C2 has not landed (a stub major would be an empty beat).
    if ((majorsS22Plans(freshMemory(0)) & S22_TAICHI) == 0) {
      Memory m = freshMemory(0);
      LoopPlan p;
      planLoop(p, SF_MORNING, loopSeed(s, 0), makeInputs(1.5f, 0, -1, false), m);
      CHECK(!has(p, MJ_TAICHI), "seed %llu: the stub tai chi planned in a real run", (unsigned long long)s);
    }
  }
  printf("taichi: forced in %d of %d first morning loops (%d after the wake); %d of %d once done this morning; at most %d majors\n",
         forced, n_first, wake_slot, later, n_later, max_n);
  CHECK(forced == n_first, "tai chi forced in %d of %d first morning loops", forced, n_first);
  CHECK(wake_slot == forced, "tai chi after the wake in %d of %d", wake_slot, forced);
  CHECK(later > n_later / 20 && later < n_later * 6 / 10, "tai chi drawn in %d of %d later morning loops", later, n_later);

  // The done bit is cleared by the first evening or midday loop, never by a morning one.
  {
    Memory m = freshMemory(kPreviewAll);
    m.taichi_next = 0x84 | 2;
    LoopPlan p;
    planLoop(p, SF_MIDDAY, loopSeed(9, 0), makeInputs(3.f, 0, -1, false), m);
    CHECK(m.taichi_next == (0x80 | 2), "midday left taichi_next %02x", (unsigned)m.taichi_next);
    m.taichi_next = 0x84 | 1;
    planLoop(p, SF_EVENING, loopSeed(9, 1), makeInputs(4.f, 0, -1, false), m);
    CHECK(m.taichi_next == (0x80 | 1), "the evening left taichi_next %02x", (unsigned)m.taichi_next);
  }
}

static void testMiddayGates() {
  // The long shot: never at night, in the data's rain, snow, storm or fog, or with a house dog out.
  static const int kNo[5] = {1, 2, 7, 5, -2};   // -2: night
  int ls_seen = 0;
  for (uint64_t s = 1; s <= 300; ++s) {
    for (int c = 0; c < 5; ++c) {
      Memory m = freshMemory(kPreviewAll);
      LoopPlan p;
      const bool night = kNo[c] == -2;
      planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(night ? 0.2f : 3.f, 0, night ? -1 : kNo[c], night), m);
      CHECK(!has(p, MJ_LONGSHOT), "seed %llu code %d: the long shot", (unsigned long long)s, kNo[c]);
    }
    Memory m = freshMemory(kPreviewAll);
    LoopPlan p;
    planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, 1 + (int)(s % 3), -1, false), m);
    CHECK(!has(p, MJ_LONGSHOT) && p.herd == 0, "seed %llu: the long shot or the deer with a dog out", (unsigned long long)s);
    CHECK(has(p, MJ_FETCH), "seed %llu: no fetch with a dog out", (unsigned long long)s);
    // Wind and heat do not hold it back.
    Memory m2 = freshMemory(kPreviewAll);
    planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, 0, 3, false), m2);
    ls_seen += has(p, MJ_LONGSHOT) ? 1 : 0;
  }
  printf("longshot: none at night, in rain, snow, storm, fog or with a dog out; %d of 300 windy first loops\n", ls_seen);
  CHECK(ls_seen > 0, "the long shot never planned in the wind");

  // The deer: never in the data's rain or storm, fine in snow; never in two midday loops running.
  int herd_snow = 0;
  for (uint64_t s = 1; s <= 300; ++s) {
    static const int kWet[2] = {1, 7};
    for (int c = 0; c < 2; ++c) {
      Memory m = freshMemory(kPreviewAll);
      LoopPlan p;
      planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, 0, kWet[c], false), m);
      CHECK(p.herd == 0, "seed %llu code %d: the deer in the data's rain or storm", (unsigned long long)s, kWet[c]);
    }
    Memory m = freshMemory(kPreviewAll);
    LoopPlan p;
    planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, 0, 2, false), m);
    herd_snow += p.herd ? 1 : 0;
    // Not planned without the preview while step C4 has not landed.
    if ((majorsS22Plans(freshMemory(0)) & S22_HERD) == 0) {
      Memory r = freshMemory(0);
      planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, 0, -1, false), r);
      CHECK(p.herd == 0 && r.herd_wait == 0, "seed %llu: the deer planned in a real run", (unsigned long long)s);
    }
  }
  printf("herd: none in rain or storm; %d of 300 snowy first loops\n", herd_snow);
  CHECK(herd_snow > 30 && herd_snow < 150, "the deer in %d of 300 snowy loops (share 0.30)", herd_snow);
}

static void testMushroom() {
  const uint8_t ripe = mushPack(MUSH_RIPE, 0, true);
  int alone = 0, with_fetch = 0;
  for (uint64_t s = 1; s <= 300; ++s) {
    // Ripe, midday, by day, dry: the mushroom alone; with a dog out the fetch first, then the mushroom.
    for (int dog = 0; dog <= 1; ++dog) {
      Memory m = freshMemory(kPreviewAll);
      m.mush = ripe;
      LoopPlan p;
      planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, dog, s % 2 ? -1 : 3, false), m);
      char b[160];
      if (dog == 0) {
        CHECK(p.n_majors == 1 && p.major[0] == MJ_MUSHROOM, "seed %llu: a ripe loop plans %s", (unsigned long long)s,
              majorsOf(p, b, sizeof b));
        alone += p.n_majors == 1 && p.major[0] == MJ_MUSHROOM;
      } else {
        CHECK(p.n_majors == 2 && has(p, MJ_FETCH) && has(p, MJ_MUSHROOM), "seed %llu: a ripe loop with a dog plans %s",
              (unsigned long long)s, majorsOf(p, b, sizeof b));
        with_fetch += p.n_majors == 2 && has(p, MJ_FETCH) && has(p, MJ_MUSHROOM);
      }
      CHECK(p.herd == 0, "seed %llu: the deer in a ripe loop", (unsigned long long)s);
      CHECK(lengthOk(p), "ripe seed %llu dog %d: %.1f s", (unsigned long long)s, dog, (double)loopSeconds(p));
      for (int i = 0; i < p.n_majors; ++i)
        if (p.major[i] == MJ_MUSHROOM) CHECK(p.major_after[i] == MD_LUNCH || p.major_after[i] == MD_WADE, "slot %d", (int)p.major_after[i]);
    }
    // Not ripe, not midday, night, or the data's rain, snow or storm: never.
    struct No {
      uint8_t mush, family;
      float mood;
      int code;
      bool night;
    };
    const No kNo[] = {{mushPack(5, 0, true), SF_MIDDAY, 3.f, -1, false}, {mushPack(MUSH_PICKED, 4, true), SF_MIDDAY, 3.f, -1, false},
                      {ripe, SF_EVENING, 4.f, -1, false},                 {ripe, SF_MORNING, 1.5f, -1, false},
                      {ripe, SF_MIDDAY, 0.2f, -1, true},                  {ripe, SF_MIDDAY, 3.f, 1, false},
                      {ripe, SF_MIDDAY, 3.f, 2, false},                   {ripe, SF_MIDDAY, 3.f, 7, false}};
    for (const No& n : kNo) {
      Memory m = freshMemory(kPreviewAll);
      m.mush = n.mush;
      LoopPlan p;
      planLoop(p, n.family, loopSeed(s, 0), makeInputs(n.mood, 0, n.code, n.night), m);
      CHECK(!has(p, MJ_MUSHROOM), "seed %llu: the mushroom with mush %02x family %d code %d night %d", (unsigned long long)s,
            (unsigned)n.mush, (int)n.family, n.code, (int)n.night);
    }
    // Not planned without the preview while step C5c has not landed.
    if ((majorsS22Plans(freshMemory(0)) & S22_MUSHROOM) == 0) {
      Memory m = freshMemory(0);
      m.mush = ripe;
      LoopPlan p;
      planLoop(p, SF_MIDDAY, loopSeed(s, 0), makeInputs(3.f, 0, -1, false), m);
      CHECK(!has(p, MJ_MUSHROOM), "seed %llu: the stub mushroom planned in a real run", (unsigned long long)s);
    }
  }
  printf("mushroom: alone in %d of 300 ripe midday loops, after the fetch in %d of 300 with a dog out\n", alone, with_fetch);
}

// ---- the rates (INTEGRATION_PLAN 5.6), 400 seeds of 6 loops per family -----------------------------

static void testRates() {
  for (const Fam& f : kFams) {
    int loops = 0, bad_len = 0, tai_first = 0, n_first = 0, tai_later = 0, n_later = 0, ls = 0, herd = 0, herd_runs = 0;
    int herd_over = 0, plink = 0, n_majors = 0;
    float lo = 1e9f, hi = 0.f, herd_hi = 0.f;
    for (uint64_t s = 1; s <= 400; ++s) {
      Memory m = freshMemory(kPreviewAll);
      bool prev_herd = false;
      for (int loop = 0; loop < 6; ++loop) {
        // The OUTSIDE data as a day might bring it: mostly none, sometimes wind, now and then rain.
        const uint64_t k = (s * 31u + (uint64_t)loop * 7u) % 10u;
        const int code = k < 6 ? -1 : (k < 8 ? 3 : (k < 9 ? 0 : 1));
        LoopPlan p;
        planLoop(p, f.family, loopSeed(s, loop), makeInputs(f.mood, 0, code, f.night), m);
        ++loops;
        n_majors += p.n_majors;
        const float sec = loopSeconds(p);
        if (sec < lo) lo = sec;
        if (sec > hi) hi = sec;
        if (!lengthOk(p)) ++bad_len;
        checkNeverTogether(p, f.name, s, loop);
        if (f.family == SF_MORNING && code != 1) {   // (no tai chi in the data's rain)
          const bool first = (m.taichi_next & 4u) == 0;
          (first ? n_first : n_later)++;
          if (has(p, MJ_TAICHI)) {
            (first ? tai_first : tai_later)++;
            m.taichi_next = (uint8_t)(m.taichi_next | 4u);   // as taichiMajorEnd does when the form is done (step C2)
          }
        }
        ls += has(p, MJ_LONGSHOT) ? 1 : 0;
        plink += has(p, MJ_PLINK) ? 1 : 0;
        if (p.herd) {
          ++herd;
          // The fit does not count the herd's beat yet (step C4 adds it to busySeconds): what it will add.
          const float with = sec + 104.f + 16.f;
          if (with > herd_hi) herd_hi = with;
          herd_over += with > (float)SS_LOOP_MAX_SECONDS ? 1 : 0;
        }
        herd_runs += (p.herd && prev_herd) ? 1 : 0;
        if (f.family == SF_MIDDAY) prev_herd = p.herd != 0;
      }
    }
    printf("rates %-7s %d loops, %.2f majors a loop, %.1f to %.1f s, %d outside 480..660", f.name, loops,
           (double)n_majors / loops, (double)lo, (double)hi, bad_len);
    if (f.family == SF_MORNING)
      printf("; taichi %d of %d first loops, %d of %d later (%.1f %%)", tai_first, n_first, tai_later, n_later,
             n_later ? 100.0 * tai_later / n_later : 0.0);
    if (f.family == SF_MIDDAY)
      printf("; longshot %d (%.1f %%), plink %d (%.1f %%), herd %d (%.1f %%), herd twice running %d; herd loops with the"
             " beat's 120 s: up to %.1f s, %d over 660 until C4 fits them",
             ls, 100.0 * ls / loops, plink, 100.0 * plink / loops, herd, 100.0 * herd / loops, herd_runs, (double)herd_hi,
             herd_over);
    printf("\n");
    CHECK(bad_len == 0, "%s: %d loops outside 480..660 s", f.name, bad_len);
    CHECK(herd_runs == 0, "%s: the deer in two midday loops running %d times", f.name, herd_runs);
    if (f.family == SF_MORNING) {
      CHECK(tai_first == n_first, "%s: tai chi forced in %d of %d first loops", f.name, tai_first, n_first);
      CHECK(tai_later * 100 > n_later * 8 && tai_later * 100 < n_later * 45, "%s: tai chi in %d of %d later loops",
            f.name, tai_later, n_later);
    }
    if (f.family == SF_MIDDAY) {
      CHECK(ls * 100 > loops * 2 && ls * 100 < loops * 16, "%s: the long shot in %d of %d", f.name, ls, loops);
      CHECK(herd * 100 > loops * 15 && herd * 100 < loops * 30, "%s: the deer in %d of %d", f.name, herd, loops);
    }
    if (f.family == SF_EVENING) CHECK(tai_first + tai_later + ls + herd == 0, "%s: a stage 22 piece", f.name);
  }
}

// ---- the fit's floor (INTEGRATION_PLAN 3.1 point 11, REVIEW_FIXES F26) ------------------------------

// A busy function that keeps any plan too long: the fit drops all it may.
static int busyLong(const LoopPlan& p) { return 400 + 120 * p.n_majors + 50 * p.n_fillers; }
static void slotsNone(LoopPlan& p) {
  for (int i = 0; i < SPINE_FILLER_MAX; ++i) p.filler_after[i] = 0;
}

static void fitPlan(LoopPlan& p, const uint8_t* ids, const uint8_t* vars, int n, int fillers) {
  memset(&p, 0, sizeof p);
  p.family = SF_MIDDAY;
  p.n_majors = (uint8_t)n;
  for (int i = 0; i < n; ++i) {
    p.major[i] = ids[i];
    p.major_var[i] = vars[i];
    p.major_after[i] = (uint8_t)(i + 1);
    p.major_pre_s[i] = p.major_post_s[i] = 6;
  }
  p.n_fillers = (uint8_t)fillers;
  for (int i = 0; i < fillers; ++i) {
    p.filler[i] = (uint8_t)(FILL_READ + i);
    p.filler_pause_s[i] = 12;
  }
  for (uint8_t& s : p.pause_s) s = 12;
}

static void testFit() {
  const FitSpec spec = {busyLong, 4, slotsNone};
  // The dog-out ripe loop with four fillers: a forced fetch and the mushroom, both kept.
  {
    const uint8_t ids[2] = {MJ_FETCH, MJ_MUSHROOM}, vars[2] = {FETCH_FORCED, 0};
    LoopPlan p;
    fitPlan(p, ids, vars, 2, 4);
    Memory m = freshMemory(0);
    spineFitLength(p, m, spec);
    CHECK(p.n_majors == 2 && has(p, MJ_FETCH) && has(p, MJ_MUSHROOM), "the fit dropped a kept major (%d left)", (int)p.n_majors);
    CHECK(p.n_fillers == 0, "fillers %d", (int)p.n_fillers);
  }
  // Tai chi and two others: the floor is one, tai chi stays.
  {
    const uint8_t ids[3] = {MJ_TAICHI, MJ_ECHO, MJ_PINECONE}, vars[3] = {0, 0, 0};
    LoopPlan p;
    fitPlan(p, ids, vars, 3, 2);
    Memory m = freshMemory(0);
    spineFitLength(p, m, spec);
    CHECK(p.n_majors == 1 && p.major[0] == MJ_TAICHI, "tai chi's loop kept %d majors, first %s", (int)p.n_majors,
          majorName(p.major[0]));
  }
  // A forced fetch, the mushroom and a third: only the third goes.
  {
    const uint8_t ids[3] = {MJ_FETCH, MJ_MUSHROOM, MJ_ECHO}, vars[3] = {FETCH_FORCED, 0, 0};
    LoopPlan p;
    fitPlan(p, ids, vars, 3, 4);
    Memory m = freshMemory(0);
    spineFitLength(p, m, spec);
    CHECK(p.n_majors == 2 && has(p, MJ_FETCH) && has(p, MJ_MUSHROOM), "kept %d majors", (int)p.n_majors);
  }
  // As before stage 22: two plain majors are the floor.
  {
    const uint8_t ids[3] = {MJ_ECHO, MJ_PINECONE, MJ_MAP_CHASE}, vars[3] = {0, 0, 0};
    LoopPlan p;
    fitPlan(p, ids, vars, 3, 3);
    Memory m = freshMemory(0);
    spineFitLength(p, m, spec);
    CHECK(p.n_majors == 2, "the plain floor kept %d majors", (int)p.n_majors);
  }
  printf("fit: the floor is one with tai chi or the mushroom, two otherwise; kept majors are never dropped\n");
}

// ---- the carry bytes in a real run (C0 review, 2026-11-18) -----------------------------------------
// While no piece has landed, whatever the host carries in (Inputs::carry: taichi_next and mush) and
// whatever herd_wait holds must not reach a plan: every plan, the loop rng and the rest of the memory
// are what the same loops plan with those bytes at zero, and the bytes themselves stay as they were.
// Runs in both folders; skipped once a step has flipped its constant (a real run then plans that piece).
static void testCarryInRealRun() {
  if (majorsS22Plans(freshMemory(0)) != 0) {
    printf("carry: a piece plans in a real run now; the interim check is skipped\n");
    return;
  }
  static const int kCodes[6] = {-1, 0, 45, 61, 71, 95};   // none, clear, fog, rain, snow, storm (WMO codes)
  Rng meta;
  meta.seed(0xC0C0FFEEull);
  int plans = 0;
  for (const Fam& f : kFams)
    for (int code : kCodes)
      for (int dog = 0; dog <= 2; ++dog)
        for (uint64_t s = 1; s <= 12; ++s) {
          const Inputs in = makeInputs(f.mood, dog, code, f.night);
          Memory a = freshMemory(0), b = freshMemory(0);
          const uint32_t r1 = meta.next();
          const uint32_t r2 = meta.next();
          a.taichi_next = (uint8_t)r1;
          a.herd_wait = (uint8_t)(r1 >> 8);
          a.mush = (s % 3 == 0) ? mushPack(MUSH_RIPE, 0, (r2 & 1) != 0) : (uint8_t)(r2 >> 8);
          a.s22_preview = (uint8_t)((r2 >> 16) & 0x7F);   // bit 7 clear: not a preview
          const Memory a0 = a;
          for (int loop = 0; loop < 4; ++loop) {
            LoopPlan pa, pb;
            Rng ra, rb;
            planLoop(pa, f.family, loopSeed(s, loop), in, a, &ra);
            planLoop(pb, f.family, loopSeed(s, loop), in, b, &rb);
            CHECK(a.taichi_next == a0.taichi_next && a.herd_wait == a0.herd_wait && a.mush == a0.mush,
                  "carry: %s code %d dog %d seed %llu loop %d: a carry byte moved in a real run", f.name, code, dog,
                  (unsigned long long)s, loop);
            Memory a_cmp = a;
            a_cmp.taichi_next = 0;
            a_cmp.herd_wait = 0;
            a_cmp.mush = 0;
            a_cmp.s22_preview = 0;
            CHECK(memcmp(&pa, &pb, sizeof pa) == 0 && memcmp(&a_cmp, &b, sizeof b) == 0 && ra.state == rb.state,
                  "carry: %s code %d dog %d seed %llu loop %d plans differently with carry bytes %02x %02x %02x", f.name,
                  code, dog, (unsigned long long)s, loop, (unsigned)a0.taichi_next, (unsigned)a0.herd_wait,
                  (unsigned)a0.mush);
            ++plans;
          }
        }
  printf("carry: %d real-run plans are the same with any carry bytes as with none\n", plans);
}

int main() {
  testSwitch();
  testCarryInRealRun();
  if (ssS22Switch() != 0) {
    testDraws();
    testTaichiGates();
    testMiddayGates();
    testMushroom();
    testRates();
  } else {
    printf("SS_S22 off: the gate and rate checks need the switch on (build-s22-on)\n");
  }
  testFit();
  if (g_fail) {
    printf("test_plan_block: %d FAILED\n", g_fail);
    return 1;
  }
  printf("test_plan_block: all passed\n");
  return 0;
}
