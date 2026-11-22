// Stage 22's integration checks (INTEGRATION_PLAN.md section 8, step C6; Mike, 2026-11-18). Each piece has its
// own suite (test_reeds, test_taichi, test_day_taichi, test_herd, test_day_herd, test_mush_patch, test_skytrip,
// test_day_trip, test_plink, test_longshot, test_plan_block); this one checks what crosses them:
//
//   [1] layout: sizeof(App) <= 29,992 (the test wire), Memory 88, LoopPlan 88, Reeds <= 224, each day member
//       in DayBike's place, MJ_COUNT 28; printed with the Lookout's shared block
//   [2] the evening untouched: whole loops 0..9 of seeds 1 and 7 at mood 4 and at night, printed as
//       ss_preview --loops prints them, hash to the lines in reviews/stage22-baseline/loops.txt
//   [3] never together: 400 midday seeds of 6 loops (memory kept), with and without a dog, with the data's
//       weather clear, wind, fog, rain (and a ripe patch in some): no pair of INTEGRATION 4.8 in one loop, a herd
//       loop has at most two majors, every loop 480 to 660 s
//   [4] the ripe loop: carry ripe, midday, clear: the mushroom alone (with a dog: the fetch, then the mushroom),
//       no herd; the next loop's memory shows mush=7 and so does the carry word
//   [5] tai chi through restarts: runs started in the morning with the carry word handed on: forced in the first
//       run's first loop only, the chapters follow on, a midday loop clears "done this morning", and the next
//       morning start forces it again with the chapter kept
//   [6] site rules, seeds 1 to 300: RS_NEAR absent or 36 px left of Nx and 40 px right of the bather's seat;
//       RS_UPSTREAM absent or at F.x + 254 or beyond; no clump's leaves within 12 px of the tai chi line, the
//       mushroom's M0, M1, M3, the long shot's M0 to M2, or a deer's standing spot other than N4
//   [7] the doe and the cattail: the head gone after 41 s, her nose within 3 px of the tip while she holds it;
//       without the fireside clump she stands at G4; the settled picture keeps 2 px in both
//   [8] the fawn and the reeds: the clump's spring moves between 66.6 and 69.5 s and is at rest by 72 s; the
//       fawn never passes the clump's base
//   [9] one wind: in the data's wind the grass's field at a clump, the reeds' lean, the flag's ribbon and a
//       puff's drift all go downwind (left); during a trip the reeds follow tripSway
//  [10] the stem: put in his mouth before each new major: gone by tai chi's first tick and by the pick (doLift),
//       still there at the long shot's last tick; the mouth point on the face's edge while prone
//  [11] guests and clumps: tai chi's heron stands 30 px or more right of RS_NEAR's base
//  [12] load: a herd of six in snow (caps on all five clumps), and the trip's antlers at their peak with a dog:
//       primitives printed, strips averaged over the beat at most 10, incremental equals full at five ticks
//  [13] determinism: two runs of a 10-loop mixed-family day with the data's weather clear, rain, clear hash the
//       same
//   and the dog rules with every piece live: with a dog out the midday plays the fetch first, never the long
//   shot or the deer, the mushroom after the fetch; tai chi still opens the morning, with no wild guests; each
//   loop's majors (the new ids included) are the memory's newest set.
//
// With SS_S22 off only [1], [2] and the rule that no new piece ever plans run.
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "mush_patch.h"
#include "scene/ambient.h"
#include "scene/day.h"
#include "scene/day_herd.h"
#include "scene/day_longshot.h"
#include "scene/day_taichi.h"
#include "scene/day_trip.h"
#include "scene/fetch.h"
#include "scene/herd.h"
#include "scene/hiker.h"
#include "scene/majors.h"
#include "scene/plink.h"
#include "scene/memory.h"
#include "scene/reeds.h"
#include "scene/script.h"
#include "scene/spine.h"
#include "scene/spine_family.h"
#include "scene/stage22_switch.h"
#include "scene/stream.h"
#include "scene/trip.h"
#include "scene/weather.h"
#include "scene/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

static const uint16_t kRipe = 0x46;   // mushPack(MUSH_RIPE, 0, soak): the carry word's low byte

struct Rig {
  void* mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = (mem && fb) ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    free(fb);
    app = nullptr;
    mem = nullptr;
    fb = nullptr;
  }
  World& w() { return const_cast<World&>(*appWorld(app)); }
};

// family: SF_MORNING, SF_MIDDAY or SF_EVENING. code < 0: no OUTSIDE reading.
static Inputs inputs(int family, int code = 0, int dog = 0, uint16_t carry = 0, int month = 8) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = family == SF_MORNING ? 1.5f : (family == SF_MIDDAY ? 3.f : 4.f);
  in.spine_force = (uint8_t)(1 + family);
  in.have_outside = code >= 0;
  in.weather_code = code >= 0 ? code : 0;
  in.dog_out = dog;
  in.month = (uint8_t)month;
  in.carry = carry;
  return in;
}

static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }
static bool has(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return true;
  return false;
}
static int indexOf(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
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
static float loopSeconds(const LoopPlan& p) { return (float)spineLoopTicks(p) / (float)SS_TICK_HZ; }
static bool lengthOk(const LoopPlan& p) {
  const float s = loopSeconds(p);
  return s >= (float)SS_LOOP_MIN_SECONDS - 0.01f && s <= (float)SS_LOOP_MAX_SECONDS + 0.01f;
}
static float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
static int gap(const Rect& a, const Rect& b) {
  const int gx = a.x0 > b.x1 ? a.x0 - b.x1 : (b.x0 > a.x1 ? b.x0 - a.x1 : -1);
  const int gy = a.y0 > b.y1 ? a.y0 - b.y1 : (b.y0 > a.y1 ? b.y0 - a.y1 : -1);
  return gx > gy ? gx : gy;
}
static bool sameFrame(const Pixel* a, const Pixel* b) { return memcmp(a, b, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) == 0; }
static uint32_t fnv32(uint32_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}
// Runs to the first tick of the named beat (true), or the end of the loop (false).
static bool toBeat(App* a, const char* name) {
  const uint32_t len = appLoopTicks(a);
  for (uint32_t t = 0; t < len + 50; ++t) {
    if (beatIs(a, name)) return true;
    if (appTick(a)) return false;
  }
  return false;
}
// Runs to the end of the current loop: true when it rolled over.
static bool toLoopEnd(App* a) {
  for (int t = 0; t < 40 * 60 * SS_TICK_HZ; ++t)
    if (appTick(a)) return true;
  return false;
}

// ---- [1] layout ------------------------------------------------------------------------------------------------

static void testLayout() {
  printf("[1] layout\n");
  printf("  sizeof(App) %u (wire 29992, %d spare), World %u, Memory %u, LoopPlan %u, Inputs %u, Reeds %u, Landscape %u\n",
         (unsigned)appAppBytes(), 29992 - (int)appAppBytes(), (unsigned)sizeof(World), (unsigned)sizeof(Memory),
         (unsigned)sizeof(LoopPlan), (unsigned)sizeof(Inputs), (unsigned)sizeof(Reeds), (unsigned)sizeof(Landscape));
#if SS_XMAS
  printf("  the Lookout's shared block %u: StreamLife %u + Reeds %u + XmasState %u = %u\n", (unsigned)sizeof(LookoutState),
         (unsigned)sizeof(StreamLife), (unsigned)sizeof(Reeds), (unsigned)sizeof(XmasState),
         (unsigned)(sizeof(StreamLife) + sizeof(Reeds) + sizeof(XmasState)));
#else
  printf("  the Lookout's shared block %u: StreamLife %u + Reeds %u\n", (unsigned)sizeof(LookoutState),
         (unsigned)sizeof(StreamLife), (unsigned)sizeof(Reeds));
#endif
  printf("  the day union (DayBike) %u: DayPlink %u, DayTaichi %u, DayHerd %u, DayTrip %u\n", (unsigned)sizeof(DayBike),
         (unsigned)sizeof(DayPlink), (unsigned)sizeof(DayTaichi), (unsigned)sizeof(DayHerd), (unsigned)sizeof(DayTrip));
  CHECK(appAppBytes() <= 29992u, "App is %u bytes", (unsigned)appAppBytes());
  CHECK(sizeof(Memory) == 88, "Memory %u", (unsigned)sizeof(Memory));
  CHECK(sizeof(LoopPlan) == 88, "LoopPlan %u", (unsigned)sizeof(LoopPlan));
  CHECK(sizeof(Reeds) <= 224, "Reeds %u", (unsigned)sizeof(Reeds));
  CHECK(sizeof(DayPlink) <= sizeof(DayBike) && sizeof(DayTaichi) <= sizeof(DayBike) && sizeof(DayHerd) <= sizeof(DayBike) &&
            sizeof(DayTrip) <= sizeof(DayBike),
        "a day member is bigger than the bike's place");
  CHECK(MJ_COUNT == 28, "MJ_COUNT %d", (int)MJ_COUNT);
}

// ---- [2] the evening untouched ---------------------------------------------------------------------------------

// FNV-1a of each line of reviews/stage22-baseline/loops.txt (made before the foundation step, HEAD 3260e2c):
// evening seed 1, night seed 1, evening seed 7, night seed 7.
static const uint32_t kEveningLines[4][10] = {
    {0x44A0975Du, 0x85A9D089u, 0xC3B4C9CBu, 0xB5E46435u, 0x7A6B28A4u, 0x797FE6D6u, 0xB50873B6u, 0xB4A46714u, 0x6EF5164Cu, 0x1860A55Fu},
    {0x4FE14573u, 0x50BD0905u, 0x1276C886u, 0x3177C37Bu, 0x9031E759u, 0xB5B40A6Cu, 0x314CBAA8u, 0x6E58AF98u, 0x241AA48Bu, 0x21A836D6u},
    {0x59DB3764u, 0x1F00D193u, 0x3C40CB9Au, 0xDDB10905u, 0xEA682E8Eu, 0xF111FFE8u, 0x78832571u, 0x789843FEu, 0x69A2F2B3u, 0xAE65B495u},
    {0xA2E0DCAAu, 0x112C651Au, 0xFAABED88u, 0x666B57E8u, 0xDFA0F7F2u, 0x561C9FBDu, 0xAD67C165u, 0xF6797FEEu, 0x2502B40Au, 0x7E4B94CCu}};

static void testEvening() {
  printf("[2] the evening untouched\n");
  int same = 0;
  for (int run = 0; run < 4; ++run) {
    const uint64_t seed = run < 2 ? 1 : 7;
    const bool night = (run & 1) != 0;
    Inputs in;   // as ss_preview --mood 4 / --mood 0 --night, no other option
    memset(&in, 0, sizeof in);
    in.mood = night ? 0.f : 4.f;
    in.night_mode = night;
    in.weather.night_dim = night ? 1.f : 0.f;
    void* mem = malloc(SS_APP_BYTES);
    HostAlloc al = {hostAlloc, hostFree, nullptr};
    al.fast = malloc(SS_FAST_BYTES);   // lent as ss_preview lends it
    App* a = appStart(mem, al, seed, in);
    CHECK(a != nullptr, "start");
    if (!a) continue;
    for (int loop = 0; loop < 10; ++loop) {
      char plan[320], memtxt[320], line[1024];
      appLoopPlan(a, plan, sizeof plan);
      const unsigned long long ls = (unsigned long long)appLoopSeed(a);
      const char* wx = weatherKindName(appWorld(a)->weather.kind);
      const uint32_t len = appLoopTicks(a);
      uint32_t ticks = 0;
      for (; ticks + 1 < len; ++ticks) appTick(a);
      appMemory(a, memtxt, sizeof memtxt);
      for (int guard = 0; guard < 40 * 60 * SS_TICK_HZ; ++guard) {
        ++ticks;
        if (appTick(a)) break;
      }
      snprintf(line, sizeof line, "loop=%d seed=%llu length=%.0fs plan=%s memory=%s weather=%s", loop, ls,
               (double)ticks / SS_TICK_HZ, plan, memtxt, wx);
      const uint32_t h = fnv32(2166136261u, line, strlen(line));
      CHECK(h == kEveningLines[run][loop], "%s seed %llu loop %d moved: %s", night ? "night" : "evening",
            (unsigned long long)seed, loop, line);
      same += h == kEveningLines[run][loop];
    }
    appStop(a, al);
    free(al.fast);
    free(mem);
  }
  printf("  %d of 40 evening and night loop lines equal the baseline's\n", same);
}

// ---- [3] never together ----------------------------------------------------------------------------------------

static uint64_t loopSeed(uint64_t seed, int loop) { return seed * 1000003ull + (uint64_t)loop * 7919ull + 1ull; }
static void planLoop(LoopPlan& p, uint8_t family, uint64_t seed, const Inputs& in, Memory& m) {
  memset(&p, 0, sizeof p);
  p.family = family;
  Rng r;
  r.seed(seed);
  spinePlan(p, r, in, m);
}

static int checkPairs(const LoopPlan& p, const Inputs& in, const char* what, uint64_t seed, int loop) {
  char b[200];
  int bad = 0;
  auto fail = [&](const char* why) {
    if (bad++ == 0) printf("  FAIL %s seed %llu loop %d: %s (%s herd=%u)\n", what, (unsigned long long)seed, loop, why,
                           majorsOf(p, b, sizeof b), (unsigned)p.herd);
  };
  const int hill = (has(p, MJ_PLINK) ? 1 : 0) + (has(p, MJ_LONGSHOT) ? 1 : 0) + (has(p, MJ_RANGER_RIDGE) ? 1 : 0);
  if (has(p, MJ_LONGSHOT) && hill > 1) fail("the long shot with plink or ranger_ridge");
  if (p.herd) {
    if (p.n_majors > 2) fail("a herd loop with more than two majors");
    if (has(p, MJ_DEER_STICK) || has(p, MJ_PLINK) || has(p, MJ_LONGSHOT)) fail("the herd with deer_stick, plink or the long shot");
    if (has(p, MJ_MUSHROOM)) fail("the herd with the mushroom");
    if (in.dog_out) fail("the herd with a dog out");
    const uint8_t wk = in.have_outside ? weatherKindForCode(in.weather_code) : (uint8_t)WX_CLEAR;
    if (wk == WX_RAIN || wk == WX_STORM) fail("the herd in the data's rain or storm");
  }
  if (has(p, MJ_LONGSHOT) && in.dog_out) fail("the long shot with a dog out");
  if (has(p, MJ_MUSHROOM) && !(p.n_majors == 1 || (p.n_majors == 2 && has(p, MJ_FETCH)))) fail("the mushroom with another major");
  if (has(p, MJ_TAICHI) && p.n_majors > 3) fail("tai chi with more than two others");
  if (!lengthOk(p)) fail("loop length outside 480..660 s");
  return bad;
}

static void testNeverTogether() {
  printf("[3] never together\n");
  static const int kCodes[4] = {0, 3, 5, 1};   // clear, wind, fog, rain (scene codes)
  int loops = 0, bad = 0, herd = 0, ls = 0, mush = 0, herd2 = 0;
  float lo = 1e9f, hi = 0.f;
  for (int dog = 0; dog <= 1; ++dog)
    for (int c = 0; c < 4; ++c)
      for (uint64_t s = 1; s <= 400; ++s) {
        Memory m;
        memset(&m, 0, sizeof m);
        if (s % 5 == 0) m.mush = (uint8_t)kRipe;   // a ripe patch now and then
        const Inputs in = inputs(SF_MIDDAY, kCodes[c], dog);
        bool prev = false;
        for (int loop = 0; loop < 6; ++loop) {
          LoopPlan p;
          planLoop(p, SF_MIDDAY, loopSeed(s * 8 + (uint64_t)(dog * 4 + c), loop), in, m);
          ++loops;
          bad += checkPairs(p, in, "midday", s, loop) ? 1 : 0;
          herd += p.herd ? 1 : 0;
          herd2 += (p.herd && prev) ? 1 : 0;
          prev = p.herd != 0;
          ls += has(p, MJ_LONGSHOT) ? 1 : 0;
          mush += has(p, MJ_MUSHROOM) ? 1 : 0;
          const float sec = loopSeconds(p);
          if (sec < lo) lo = sec;
          if (sec > hi) hi = sec;
        }
      }
  printf("  %d midday plans: herd %d, long shot %d, mushroom %d, %.0f to %.0f s, %d bad, herd twice running %d\n", loops,
         herd, ls, mush, (double)lo, (double)hi, bad, herd2);
  CHECK(bad == 0, "%d plans broke a rule", bad);
  CHECK(herd2 == 0, "the herd in two midday loops running %d times", herd2);
  if (ssS22Switch() != 0) CHECK(herd > 0 && ls > 0 && mush > 0, "a piece never planned (herd %d, long shot %d, mushroom %d)", herd, ls, mush);
  else CHECK(herd + ls + mush == 0, "SS_S22 off: a piece planned (herd %d, long shot %d, mushroom %d)", herd, ls, mush);
}

// ---- [4] the ripe loop -----------------------------------------------------------------------------------------

static void testRipe() {
  printf("[4] the ripe loop\n");
  int alone = 0, after_fetch = 0, seven = 0;
  for (int dog = 0; dog <= 1; ++dog)
    for (uint64_t seed = 1; seed <= 3; ++seed) {
      Rig r;
      CHECK(r.start(seed, inputs(SF_MIDDAY, 0, dog, kRipe)), "start");
      const LoopPlan& p = r.w().plan;
      char b[200];
      if (dog == 0) {
        CHECK(p.n_majors == 1 && p.major[0] == MJ_MUSHROOM, "seed %llu: a ripe loop plans %s", (unsigned long long)seed, majorsOf(p, b, sizeof b));
        alone += p.n_majors == 1 && p.major[0] == MJ_MUSHROOM;
      } else {
        CHECK(p.n_majors == 2 && p.major[0] == MJ_FETCH && p.major[1] == MJ_MUSHROOM, "seed %llu: a ripe loop with a dog plans %s",
              (unsigned long long)seed, majorsOf(p, b, sizeof b));
        after_fetch += p.n_majors == 2 && p.major[0] == MJ_FETCH && p.major[1] == MJ_MUSHROOM;
      }
      char line[400];
      appLoopPlan(r.app, line, sizeof line);
      CHECK(p.herd == 0 && strstr(line, " herd=") == nullptr, "seed %llu: the herd in a ripe loop: %s", (unsigned long long)seed, line);
      if (seed == 1 || (dog == 1 && seed == 2)) {   // the whole loop: the memory it leaves
        CHECK(toLoopEnd(r.app), "seed %llu: the loop never ended", (unsigned long long)seed);
        char memtxt[320];
        appMemory(r.app, memtxt, sizeof memtxt);
        const uint16_t carry = appCarry(r.app);
        const bool ok = strstr(memtxt, " mush=7") != nullptr && mushStage((uint8_t)(carry & 0xFF)) == MUSH_PICKED;
        CHECK(ok, "seed %llu dog %d: after the ripe loop the memory is \"%s\", carry %04x", (unsigned long long)seed, dog, memtxt,
              (unsigned)carry);
        seven += ok;
        CHECK(!has(r.w().plan, MJ_MUSHROOM), "seed %llu: the mushroom again in the next loop", (unsigned long long)seed);
      }
      r.stop();
    }
  printf("  the mushroom alone in %d of 3 ripe loops, after the fetch in %d of 3 with a dog; mush=7 after %d of 3 whole loops\n",
         alone, after_fetch, seven);
}

// ---- [5] tai chi through restarts ------------------------------------------------------------------------------

// One run from a carry word: the first loop's plan and, after `loops` whole loops, the carry word it hands on.
struct RunOut {
  bool tai_first;      // tai chi in the first loop
  int tai_chapter;     // the chapter of the first tai chi played (-1 none)
  uint16_t carry;      // appCarry at the end
};
static RunOut runFrom(uint64_t seed, int family, uint16_t carry, int loops, bool stop_after_taichi) {
  RunOut o{false, -1, 0};
  Rig r;
  if (!r.start(seed, inputs(family, 0, 0, carry))) return o;
  for (int l = 0; l < loops; ++l) {
    const LoopPlan& p = r.w().plan;
    const int i = indexOf(p, MJ_TAICHI);
    if (l == 0) o.tai_first = i >= 0;
    const bool here = i >= 0 && o.tai_chapter < 0;
    if (here) o.tai_chapter = p.major_var[i] & 3;
    if (!toLoopEnd(r.app)) break;
    if (here && stop_after_taichi) break;
  }
  o.carry = appCarry(r.app);
  r.stop();
  return o;
}

static void testRestarts() {
  printf("[5] tai chi through restarts\n");
  int second_first = 0, n_second = 0;
  for (uint64_t seed = 1; seed <= 3; ++seed) {
    // Run 1, the day's first start (carry 0): tai chi is forced; a fresh memory takes its chapter from the plan's
    // own draw (INTEGRATION 3.1 point 7); after the form the word holds the next chapter and "done this morning".
    const RunOut a = runFrom(seed, SF_MORNING, 0, 1, false);
    CHECK(a.tai_first && a.tai_chapter >= 0, "seed %llu run 1: tai chi %d, chapter %d", (unsigned long long)seed, (int)a.tai_first,
          a.tai_chapter);
    const unsigned want1 = 0x84u | (unsigned)((a.tai_chapter + 1) & 3);
    CHECK((a.carry >> 8) == want1, "seed %llu run 1: chapter %d, carry %04x, want %02x..", (unsigned long long)seed, a.tai_chapter,
          (unsigned)a.carry, want1);
    // Run 2, a later start the same morning: not forced, drawn by weight; when it plays it is the next chapter.
    const RunOut b = runFrom(seed + 100, SF_MORNING, a.carry, 6, true);
    ++n_second;
    second_first += b.tai_first ? 1 : 0;
    if (b.tai_chapter >= 0) {
      CHECK(b.tai_chapter == (int)(want1 & 3), "seed %llu run 2: chapter %d, want %u", (unsigned long long)seed, b.tai_chapter, want1 & 3);
      const unsigned want2 = 0x84u | ((want1 + 1) & 3);
      CHECK((b.carry >> 8) == want2, "seed %llu run 2: carry %04x, want %02x..", (unsigned long long)seed, (unsigned)b.carry, want2);
    } else {
      CHECK((b.carry >> 8) == want1, "seed %llu run 2: no tai chi, carry %04x", (unsigned long long)seed, (unsigned)b.carry);
    }
    // Run 3, at midday: the first plan clears "done this morning", the chapter stays.
    Rig m;
    CHECK(m.start(seed + 200, inputs(SF_MIDDAY, 0, 0, b.carry)), "start");
    const uint16_t c3 = appCarry(m.app);
    m.stop();
    CHECK((c3 >> 8) == ((b.carry >> 8) & ~4u), "seed %llu run 3: carry %04x after %04x", (unsigned long long)seed, (unsigned)c3, (unsigned)b.carry);
    // Run 4, the next morning: forced again, the next chapter.
    Rig n;
    CHECK(n.start(seed + 300, inputs(SF_MORNING, 0, 0, c3)), "start");
    const int i = indexOf(n.w().plan, MJ_TAICHI);
    CHECK(i >= 0 && (n.w().plan.major_var[i] & 3) == (int)((c3 >> 8) & 3), "seed %llu run 4: tai chi at %d, chapter %d, word %04x",
          (unsigned long long)seed, i, i >= 0 ? (n.w().plan.major_var[i] & 3) : -1, (unsigned)c3);
    n.stop();
  }
  // Not forced in a later start: over more seeds the first loop only sometimes has it.
  int later = 0;
  const int kLater = 60;
  for (uint64_t seed = 1; seed <= (uint64_t)kLater; ++seed) {
    Rig r;
    CHECK(r.start(seed + 1000, inputs(SF_MORNING, 0, 0, (uint16_t)(0x8500 | 0))), "start");
    later += has(r.w().plan, MJ_TAICHI) ? 1 : 0;
    r.stop();
  }
  printf("  run 2's first loop had tai chi in %d of %d; with the done bit carried in, %d of %d first loops (not forced)\n",
         second_first, n_second, later, kLater);
  CHECK(later < kLater * 6 / 10, "tai chi in %d of %d first loops with the done bit carried in", later, kLater);
}

// ---- [6] site rules --------------------------------------------------------------------------------------------

// Every clump as drawn at rest (no wind), one bit per clump in a frame-sized mask: the measure for the site rules
// is the clump's own pixels, leaves and stalks both (stricter than the leaves alone, and exact where a box of
// the leaves is not: the fans spread wider than ReedClumpInfo::half_w and the stalks lean out of them).
static uint8_t g_mask[SS_WIDTH * SS_HEIGHT];
static void buildClumpMask(World& w) {
  static Pixel px[SS_WIDTH * SS_HEIGHT];
  memset(g_mask, 0, sizeof g_mask);
  Palette pal;
  WeatherTint t;
  memset(&t, 0, sizeof t);
  computePalette(3.f, t, pal);
  int8_t keep[REED_CLUMPS][2];
  memcpy(keep, w.reeds.sway_q, sizeof keep);
  memset(w.reeds.sway_q, 0, sizeof w.reeds.sway_q);
  void* scratch = calloc(1, rasterScratchBytes());
  rasterResetScratch(scratch);
  void* was = rasterSetScratch(scratch);
  Surface sf{px, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  for (int c = 0; c < REED_CLUMPS; ++c) {
    if (!(w.reeds.site[c].flags & 1u)) continue;
    for (Pixel& q : px) q = (Pixel)0x0841;
    reedsDraw(w, c, sf, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, pal);
    for (int i = 0; i < SS_WIDTH * SS_HEIGHT; ++i)
      if (px[i] != (Pixel)0x0841) g_mask[i] = (uint8_t)(g_mask[i] | (1u << c));
  }
  rasterSetScratch(was);
  free(scratch);
  memcpy(w.reeds.sway_q, keep, sizeof keep);
}
static char g_detail[96];
static int g_spot_hist[HS_COUNT][REED_CLUMPS];
// The nearest clump pixel to p within 16 px (16 when none), and which clump (-1 none).
static float nearestLeaves(const World&, Vec2 p, int& which) {
  float best = 16.f;
  which = -1;
  const int x0 = (int)floorf(p.x) - 16, y0 = (int)floorf(p.y) - 16;
  for (int y = y0; y <= y0 + 32; ++y)
    for (int x = x0; x <= x0 + 32; ++x) {
      if (x < 0 || y < 0 || x >= SS_WIDTH || y >= SS_HEIGHT || !g_mask[y * SS_WIDTH + x]) continue;
      const float dx = (float)x + 0.5f - p.x, dy = (float)y + 0.5f - p.y;
      const float d = sqrtf(dx * dx + dy * dy);
      if (d < best) {
        best = d;
        const uint8_t m = g_mask[y * SS_WIDTH + x];
        which = 0;
        while (!((m >> which) & 1u)) ++which;
        snprintf(g_detail, sizeof g_detail, "at %.0f,%.0f; its pixel at %d,%d", (double)p.x, (double)p.y, x, y);
      }
    }
  return best;
}
// Starts the plan's major `id` with variant `var` on the live world now, as majorBegin does (for its places).
static void beginNow(World& w, int id, uint8_t var, void (*begin)(World&)) {
  MajorState& m = w.major;
  m.on = true;
  m.id = (uint8_t)id;
  m.var = var;
  for (Vec2& p : m.p) p = w.hiker.feet;
  for (int k = 0; k < 3; ++k) m.c[k] = -1;
  begin(w);
}

static void testSites() {
  printf("[6] site rules\n");
  int near_n = 0, up_n = 0, tai = 0, mush = 0, lsn = 0, herds = 0, worst_c[4] = {-1, -1, -1, -1};
  float worst[4] = {1e9f, 1e9f, 1e9f, 1e9f};   // tai chi, mushroom, long shot, deer
  int bad[4] = {0, 0, 0, 0};
  const char* kWhat[4] = {"the tai chi line", "a mushroom place", "a long shot place", "a deer's spot"};
  auto note = [&](int k, float d, int c, uint64_t seed, const char* where) {
    if (d < worst[k]) {
      worst[k] = d;
      worst_c[k] = c;
    }
    if (d < 12.f) {
      if (bad[k] < 4)
        printf("  %s (%s) seed %llu: %.1f px from clump %d (%s)\n", kWhat[k], where, (unsigned long long)seed, (double)d, c,
               g_detail);
      ++bad[k];
    }
  };
  for (uint64_t seed = 1; seed <= 300; ++seed) {
    // The midday camp: the reed rules, the mushroom's and the long shot's places.
    {
      Rig r;
      CHECK(r.start(seed, inputs(SF_MIDDAY, 0, 0, kRipe)), "start");
      for (int t = 0; t < 25 * 14; ++t) appTick(r.app);   // past the build
      World& w = r.w();
      const Vec2 F = w.fire.pos;
      if (w.reeds.site[RS_NEAR].flags & 1u) {
        ++near_n;
        const float nx = fminf((float)w.land.mush_x - 44.f, 501.f);
        CHECK((float)w.reeds.site[RS_NEAR].x <= nx - 36.f + 0.5f, "seed %llu: RS_NEAR at %d, Nx %.0f", (unsigned long long)seed,
              w.reeds.site[RS_NEAR].x, (double)nx);
        CHECK((float)w.reeds.site[RS_NEAR].x >= F.x + 126.f + 40.f - 1.f, "seed %llu: RS_NEAR at %d, the bather at %.0f",
              (unsigned long long)seed, w.reeds.site[RS_NEAR].x, (double)(F.x + 126.f));
      }
      if (w.reeds.site[RS_UPSTREAM].flags & 1u) {
        ++up_n;
        CHECK((float)w.reeds.site[RS_UPSTREAM].x >= F.x + 254.f - 1.f, "seed %llu: RS_UPSTREAM at %d, F.x %.0f", (unsigned long long)seed,
              w.reeds.site[RS_UPSTREAM].x, (double)F.x);
      }
      // The mushroom (the patch is ripe in this run): M0, M1, M3.
      {
        buildClumpMask(w);
        beginNow(w, MJ_MUSHROOM, 0, tripMajorBegin);
        if (w.day.kind == DAY_TRIP && w.day.trip.t.n_pick > 0) {
          ++mush;
          static const int kM[3] = {0, 1, 3};
          for (int k : kM) {
            int c;
            const float d = nearestLeaves(w, w.major.p[k], c);
            char where[8];
            snprintf(where, sizeof where, "M%d", k);
            note(1, d, c, seed, where);
          }
        }
      }
      r.stop();
    }
    {
      Rig r;
      CHECK(r.start(seed, inputs(SF_MIDDAY, 0, 0, 0)), "start");
      for (int t = 0; t < 25 * 14; ++t) appTick(r.app);
      World& w = r.w();
      buildClumpMask(w);
      beginNow(w, MJ_LONGSHOT, 0, longshotMajorBegin);
      if (w.day.kind == DAY_PLINK) {
        ++lsn;
        for (int k = 0; k < 3; ++k) {
          int c;
          const float d = nearestLeaves(w, w.major.p[k], c);
          char where[8];
          snprintf(where, sizeof where, "M%d", k);
          note(2, d, c, seed, where);
        }
      }
      r.stop();
    }
    // The morning camp: the tai chi line (the first morning loop always plays it; its places at its begin).
    {
      Rig r;
      CHECK(r.start(seed, inputs(SF_MORNING, 0, 0, 0)), "start");
      World& w = r.w();
      if (indexOf(w.plan, MJ_TAICHI) >= 0 && toBeat(r.app, "taichi")) {
        appTick(r.app);
        if (w.major.on && w.major.id == MJ_TAICHI) {
          ++tai;
          buildClumpMask(w);
          const Vec2 p0 = w.major.p[0];
          for (float dx = -8.f; dx <= 66.f; dx += 2.f) {
            int c;
            const float d = nearestLeaves(w, Vec2{p0.x + dx, p0.y}, c);
            note(0, d, c, seed, "line");
          }
        }
      }
      r.stop();
    }
    // The deer's standing spots (a midday seed whose first loop has the herd).
    {
      Rig r;
      CHECK(r.start(seed, inputs(SF_MIDDAY, 0, 0, 0)), "start");
      World& w = r.w();
      if (w.plan.herd && toBeat(r.app, "herd")) {
        appTick(r.app);
        if (w.day.kind == DAY_HERD) {
          ++herds;
          buildClumpMask(w);
          const HerdState& h = w.day.herd.h;
          static const uint8_t kSpots[] = {HS_WAY2, HS_FSTOP, HS_G0, HS_GF, HS_G4, HS_G4F, HS_G5, HS_T2WAIT, HS_T2HALF, HS_T2, HS_T2BACK,
                                           HS_D2OUT, HS_T3GRAZE, HS_T3, HS_FLOOK, HS_FTENT};
          for (uint8_t s : kSpots)
            for (int i = 0; i < h.n; ++i) {
              const Vec2 at = herdSpot(h, s, h.d[i].role);
              if (at.x < 0.f || at.x > (float)SS_WIDTH) continue;
              int c;
              const float d = nearestLeaves(w, at, c);
              char where[12];
              snprintf(where, sizeof where, "spot %d", (int)s);
              note(3, d, c, seed, where);
              if (d < 12.f && c >= 0) ++g_spot_hist[s][c];
            }
        }
      }
      r.stop();
    }
  }
  printf("  300 seeds: RS_NEAR %d, RS_UPSTREAM %d; tai chi lines %d, mushroom places %d, long shot places %d, herds %d\n", near_n, up_n,
         tai, mush, lsn, herds);
  for (int k = 0; k < 4; ++k)
    printf("  %-18s nearest clump pixel %.1f px (clump %d; 16 means none within 16), %d closer than 12 px\n", kWhat[k], (double)worst[k],
           worst_c[k], bad[k]);
  for (int s = 0; s < HS_COUNT; ++s)
    for (int c = 0; c < REED_CLUMPS; ++c)
      if (g_spot_hist[s][c]) printf("    deer spot %d within 12 px of clump %d: %d deer-places\n", s, c, g_spot_hist[s][c]);
  for (int k = 0; k < 4; ++k) CHECK(bad[k] == 0, "%s: %d places within 12 px of a clump's leaves", kWhat[k], bad[k]);
  if (ssS22Switch() != 0) CHECK(tai > 250 && herds > 30, "too few runs (tai chi %d, herds %d)", tai, herds);
}

// ---- [7] [8] [12a] the herd with the reeds -----------------------------------------------------------------------

struct HerdRun {
  bool ran;
  int nib_on, nib_gone, nib_gone_by41, reed_on, reed_moving, reed_past;
  float nib_err, d4_from_g4;
  int reed_rest72, reed_ambient, pic_min_gap, pic_samples;
  long strips;
  int ticks, diffs, checks, prims_max, deer_max;
};
// One herd beat drawn every tick. full_at: beat ticks at which a full render is compared (-1 ends the list).
static HerdRun playHerd(uint64_t seed, const Inputs& in, const int* full_at) {
  HerdRun e;
  memset(&e, 0, sizeof e);
  e.pic_min_gap = 9999;
  e.d4_from_g4 = -1.f;
  e.reed_rest72 = -1;
  Rig a, b;
  if (!a.start(seed, in) || !b.start(seed, in)) return e;
  World& w = a.w();
  if (!w.plan.herd || !toBeat(a.app, "herd") || !toBeat(b.app, "herd")) {
    a.stop();
    b.stop();
    return e;
  }
  e.ran = true;
  appInvalidate(a.app);
  appRender(a.app, a.surf);
  const int variant = herdVariant(w.plan.herd) - 1;
  const float pic_from = variant == 0 ? 73.5f : (variant == 1 ? 72.f : 70.f);
  float rest = 0.f;
  bool rest_set = false;
  int fi = 0;
  for (int bt = 0; beatIs(a.app, "herd"); ++bt) {
    appTick(a.app);
    appTick(b.app);
    e.strips += appRender(a.app, a.surf);
    ++e.ticks;
    if (full_at && full_at[fi] >= 0 && bt == full_at[fi]) {
      ++fi;
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++e.checks;
      e.diffs += sameFrame(a.fb, b.fb) ? 0 : 1;
    }
    if (w.day.kind != DAY_HERD) continue;
    const float bs = (float)(bt + 1) * SS_TICK_DT;
    const HerdState& h = w.day.herd.h;
    const HerdDrive& v = w.day.herd.v;
    int8_t slots[HERD_MAX];
    herdDeerSlots(w, slots);
    Rect rc[HERD_MAX];
    int vis = 0, prims = 0;
    for (int i = 0; i < HERD_MAX; ++i) {
      rc[i] = Rect{0, 0, 0, 0};
      if (slots[i] < 0 || !w.critters[slots[i]].visible) continue;
      rc[i] = critterBounds(w.critters[slots[i]]);
      ++vis;
    }
    for (int c = 0; c < REED_CLUMPS; ++c) prims += reedsPrimitives(w, c);
    if (vis > e.deer_max) e.deer_max = vis;
    if (prims > e.prims_max) e.prims_max = prims;
    if (!h.alarm && bs >= pic_from && bt % 5 == 0) {
      ++e.pic_samples;
      for (int i = 0; i < h.n; ++i)
        for (int j = i + 1; j < h.n; ++j) {
          if (rc[i].empty() || rc[j].empty()) continue;
          const int g = gap(rc[i], rc[j]);
          if (g < e.pic_min_gap) e.pic_min_gap = g;
        }
    }
    e.nib_on = h.nibble_on;
    if (h.nibble_on && v.nib_clump >= 0) {
      ReedStalkInfo si;
      reedsStalk(w, v.nib_clump, v.nib_stalk, si);
      if (v.nib == 1 && v.nib_t >= 1.25f) {   // from her lips on it (39.4 s on the plan's clock) to the take
        const int gi = herdIndexOf(h, HR_GRAZER);
        if (gi >= 0 && slots[gi] >= 0) {
          const float d = dist(critterDeerNose(w.critters[slots[gi]]), si.tip);
          if (d > e.nib_err) e.nib_err = d;
        }
      }
      if (v.nib == 2 && !si.present) {
        e.nib_gone = 1;
        if (bs <= 46.f) e.nib_gone_by41 = 1;   // the plan's 41 s, plus the walk's seed spread
      }
    }
    if (bt + 1 == 60 * SS_TICK_HZ) {
      const int gi = herdIndexOf(h, HR_GRAZER);
      if (gi >= 0 && slots[gi] >= 0) e.d4_from_g4 = fabsf(w.critters[slots[gi]].feet.x - herdSpot(h, HS_G4, HR_GRAZER).x);
    }
    e.reed_on = h.fawn_reed.x > 0.f;
    if (e.reed_on && v.reed_clump >= 0) {
      const int16_t push = w.reeds.push[v.reed_clump];
      if (v.fawn_reed == 0) {
        rest = (float)push;
        rest_set = true;
      }
      if (v.fawn_reed == 2 && rest_set && (fabsf((float)push - rest) > 256.f || abs((int)w.reeds.push_v[v.reed_clump]) > 200)) e.reed_moving = 1;
      if (bt + 1 == 72 * SS_TICK_HZ) e.reed_rest72 = abs((int)w.reeds.push_v[v.reed_clump]);
      // the wind's own swing of the spring in the 20 s before the fawn's dash
      if (bs >= 44.f && bs < 64.f && abs((int)w.reeds.push_v[v.reed_clump]) > e.reed_ambient)
        e.reed_ambient = abs((int)w.reeds.push_v[v.reed_clump]);
      ReedClumpInfo ci;
      const int fawn = herdIndexOf(h, HR_FAWN);
      if (reedsClump(w, v.reed_clump, ci) && fawn >= 0 && !rc[fawn].empty() && v.fawn_reed >= 1 && h.alarm == 0 &&
          rc[fawn].x1 > (int)ci.base.x + 1)
        ++e.reed_past;
    }
  }
  a.stop();
  b.stop();
  return e;
}

static uint64_t findHerd(int fireside, int upstream, int size, int all5, const Inputs& in, uint64_t from) {
  for (uint64_t seed = from; seed < from + 3000; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return 0;
    World& w = r.w();
    bool hit = w.plan.herd != 0 && (size < 0 || herdSize(w.plan.herd) == size);
    if (hit && all5) {
      for (int c = 0; c < REED_CLUMPS; ++c) hit = hit && (w.reeds.site[c].flags & 1u) != 0;
    }
    if (hit && (fireside >= 0 || upstream >= 0)) {
      hit = toBeat(r.app, "herd");
      if (hit) {
        appTick(r.app);
        // (a herd of four has no second doe: the fireside clump is then simply there)
        hit = w.day.kind == DAY_HERD && (fireside < 0 || (herdIndexOf(w.day.herd.h, HR_GRAZER) >= 0 && (w.day.herd.h.nibble_on != 0) == (fireside != 0))) &&
              (upstream < 0 || (w.day.herd.h.fawn_reed.x > 0.f) == (upstream != 0));
      }
    }
    r.stop();
    if (hit) return seed;
  }
  return 0;
}

static void testHerdReeds() {
  printf("[7] the doe and the cattail, [8] the fawn and the reeds\n");
  const Inputs in = inputs(SF_MIDDAY, 0, 0, 0, 8);
  int cases = 0;
  for (int k = 0; k < 2; ++k) {
    const uint64_t s = findHerd(1, -1, -1, 0, in, 1 + 400 * (uint64_t)k);
    CHECK(s != 0, "no herd seed with the fireside clump");
    if (!s) continue;
    const HerdRun e = playHerd(s, in, nullptr);
    ++cases;
    printf("  fireside seed %llu: nose within %.2f px of the tip, head gone %d (by 46 s %d), settled gap %d px (%d samples)\n",
           (unsigned long long)s, (double)e.nib_err, e.nib_gone, e.nib_gone_by41, e.pic_min_gap, e.pic_samples);
    CHECK(e.ran && e.nib_on, "seed %llu: the doe never went for the cattail", (unsigned long long)s);
    CHECK(e.nib_gone && e.nib_gone_by41, "seed %llu: the head was not bitten off in time (%d, %d)", (unsigned long long)s, e.nib_gone, e.nib_gone_by41);
    CHECK(e.nib_err <= 3.f, "seed %llu: her nose %.2f px from the tip while she held it", (unsigned long long)s, (double)e.nib_err);
    CHECK(e.pic_samples > 0 && e.pic_min_gap >= 2, "seed %llu: the settled picture's gap %d px", (unsigned long long)s, e.pic_min_gap);
  }
  {
    const uint64_t s = findHerd(0, -1, -1, 0, in, 1);
    CHECK(s != 0, "no herd seed without the fireside clump");
    if (s) {
      const HerdRun e = playHerd(s, in, nullptr);
      printf("  no fireside clump, seed %llu: the second doe %.1f px from G4 at 60 s, settled gap %d px\n", (unsigned long long)s,
             (double)e.d4_from_g4, e.pic_min_gap);
      CHECK(e.ran && !e.nib_on, "seed %llu: a nibble with no clump", (unsigned long long)s);
      CHECK(e.d4_from_g4 >= 0.f && e.d4_from_g4 <= 24.f, "seed %llu: the second doe %.1f px from G4", (unsigned long long)s, (double)e.d4_from_g4);
      CHECK(e.pic_samples > 0 && e.pic_min_gap >= 2, "seed %llu: the settled picture's gap %d px", (unsigned long long)s, e.pic_min_gap);
    }
  }
  for (int k = 0; k < 2; ++k) {
    const uint64_t s = findHerd(-1, 1, -1, 0, in, 1 + 500 * (uint64_t)k);
    CHECK(s != 0, "no herd seed with the upstream clump in the fawn's reach");
    if (!s) continue;
    const HerdRun e = playHerd(s, in, nullptr);
    ++cases;
    printf("  upstream seed %llu: the spring moved %d, at rest by 72 s (speed %d), the fawn past the base on %d ticks\n",
           (unsigned long long)s, e.reed_moving, e.reed_rest72, e.reed_past);
    printf("    (the wind alone swung that spring up to %d in the 20 s before the dash)\n", e.reed_ambient);
    CHECK(e.ran && e.reed_on && e.reed_moving, "seed %llu: the clump never moved for the fawn", (unsigned long long)s);
    // At rest: no faster than test_day_herd's 200, or than the wind alone swung it before the dash.
    const int rest_ok = e.reed_ambient > 200 ? e.reed_ambient : 200;
    CHECK(e.reed_rest72 >= 0 && e.reed_rest72 <= rest_ok, "seed %llu: the clump's spring still moving at 72 s (%d, the wind's %d)",
          (unsigned long long)s, e.reed_rest72, e.reed_ambient);
    CHECK(e.reed_past == 0, "seed %llu: the fawn passed the clump's base on %d ticks", (unsigned long long)s, e.reed_past);
  }
  (void)cases;
}

// ---- [9] one wind ----------------------------------------------------------------------------------------------

// Draws the flag glyph alone and returns the ribbon's mean x against the pole (negative: it streams left).
static float flagLean(const Glyph& g0) {
  static Pixel px[160 * 120];
  for (Pixel& p : px) p = (Pixel)0x0841;
  Surface s{px, 160, 120, 160, 0};
  Glyph g = g0;
  g.pos = Vec2{80.f, 90.f};
  g.base_y = 90;
  Palette pal;
  WeatherTint t;
  memset(&t, 0, sizeof t);
  computePalette(3.f, t, pal);
  // The rasterizer's scratch: the apps lend theirs only inside appRender.
  void* scratch = calloc(1, rasterScratchBytes());
  rasterResetScratch(scratch);
  void* was = rasterSetScratch(scratch);
  longshotShapeDraw(g, s, Rect{0, 0, 160, 120}, pal);
  rasterSetScratch(was);
  free(scratch);
  double sx = 0.0;
  long n = 0;
  for (int y = 0; y < 120; ++y)
    for (int x = 0; x < 160; ++x)
      if (px[y * 160 + x] != (Pixel)0x0841 && x != 80 && x != 81 && x != 79) {
        sx += x - 80;
        ++n;
      }
  return n ? (float)(sx / (double)n) : 0.f;
}

static void testOneWind() {
  printf("[9] one wind\n");
  // The data's wind (code 3), a midday with the long shot: the grass's field, the reeds' leaves, the ribbon, a puff.
  int samples = 0, left = 0, agree = 0, agree_n = 0, puffs = 0, puff_left = 0, flags = 0, flag_left = 0;
  double mean = 0.0;
  for (uint64_t seed = 1; seed <= 400 && flags < 2; ++seed) {
    Rig r;
    CHECK(r.start(seed, inputs(SF_MIDDAY, 3, 0, 0)), "start");
    World& w = r.w();
    if (!has(w.plan, MJ_LONGSHOT)) {
      r.stop();
      continue;
    }
    // From 50 ticks, every 5th tick for 2,000: the field at each clump and the leaves' quantized lean. The grass's field
    // (ambient.cpp, approved and pinned by the sway hash) swings right for moments by design, so a single tick cannot
    // be asked to agree with the always-left ribbon and puffs: the field must lean left most of the time and on
    // average, and the leaves must follow it.
    for (int t = 0; t < 2050; ++t) {
      appTick(r.app);
      if (t < 50 || t % 5 != 0) continue;
      for (int c = 0; c < REED_CLUMPS; ++c) {
        if (!(w.reeds.site[c].flags & 1u)) continue;
        const float f = ambientWindAt(w, (float)w.reeds.site[c].x);
        ++samples;
        left += f < 0.f ? 1 : 0;
        mean += f;
        if (fabsf(f) > 0.75f) {
          ++agree_n;
          agree += ((float)w.reeds.sway_q[c][0] < 0.f) == (f < 0.f) ? 1 : 0;
        }
      }
    }
    if (!toBeat(r.app, "longshot")) {
      r.stop();
      continue;
    }
    float last_puff_x = 0.f;
    bool had_puff = false;
    bool flag_seen = false;
    while (beatIs(r.app, "longshot")) {
      appTick(r.app);
      if (w.day.kind != DAY_PLINK) continue;
      const FarState& f = w.day.plink.far;
      if (f.g_flag && !flag_seen && !f.flag_held) {
        const Glyph& g = w.major.g[f.g_flag - 1];
        const float lean = flagLean(g);
        ++flags;
        flag_left += lean < -1.f ? 1 : 0;
        flag_seen = true;
        printf("  seed %llu: the planted flag's ribbon at %.1f px from the pole (breeze %.2f)\n", (unsigned long long)seed, (double)lean,
               (double)g.k);
      }
      if (f.g_puff && f.puff_t > 0.2f) {
        const float x = w.major.g[f.g_puff - 1].pos.x;
        if (had_puff) {
          ++puffs;
          puff_left += x <= last_puff_x ? 1 : 0;
        }
        last_puff_x = x;
        had_puff = true;
      } else {
        had_puff = false;
      }
    }
    r.stop();
  }
  printf("  the field at the clumps leans left in %d of %d samples (mean %.2f px); the leaves agree with it in %d of %d; flags %d"
         " (left %d); puff ticks %d (drifting left %d)\n",
         left, samples, samples ? mean / samples : 0.0, agree, agree_n, flags, flag_left, puffs, puff_left);
  CHECK(samples > 0 && left * 10 >= samples * 6 && mean < 0.0, "the wind leans left at only %d of %d samples (mean %.2f)", left, samples,
        samples ? mean / samples : 0.0);
  CHECK(agree_n > 0 && agree * 10 >= agree_n * 9, "the leaves agree with the field at %d of %d", agree, agree_n);
  CHECK(flags > 0 && flag_left == flags, "the flag's ribbon went left %d of %d times", flag_left, flags);
  CHECK(puffs == 0 || puff_left == puffs, "a puff drifted right on %d of %d ticks", puffs - puff_left, puffs);

  // During a trip: the reeds' leaves follow the trip's field (tripSway), not the plain one.
  {
    uint64_t seed = 0;
    for (uint64_t s = 1; s <= 40 && !seed; ++s) {
      Rig r;
      r.start(s, inputs(SF_MIDDAY, 0, 0, kRipe));
      if (has(r.w().plan, MJ_MUSHROOM)) seed = s;
      r.stop();
    }
    CHECK(seed != 0, "no ripe seed");
    if (seed) {
      Rig r;
      r.start(seed, inputs(SF_MIDDAY, 0, 0, kRipe));
      World& w = r.w();
      toBeat(r.app, "mushroom");
      World* plain = (World*)malloc(sizeof(World));
      double err_trip = 0.0, err_plain = 0.0;
      int n = 0;
      for (int t = 0; beatIs(r.app, "mushroom"); ++t) {
        appTick(r.app);
        appRender(r.app, r.surf);
        if (w.day.kind != DAY_TRIP || w.day.trip.t.ks < 0.6f || t % 10 != 0) continue;
        memcpy((void*)plain, (const void*)&w, sizeof(World));
        plain->day.kind = DAY_NONE;
        for (int c = 0; c < REED_CLUMPS; ++c) {
          if (!(w.reeds.site[c].flags & 1u)) continue;
          const float x = (float)w.reeds.site[c].x;
          const float leaves = 0.25f * (float)w.reeds.sway_q[c][0];
          err_trip += fabs(leaves - ambientWindAt(w, x));
          err_plain += fabs(leaves - ambientWindAt(*plain, x));
          ++n;
        }
      }
      free(plain);
      printf("  trip seed %llu: the leaves' mean distance from the trip's field %.2f px, from the plain field %.2f px (%d samples)\n",
             (unsigned long long)seed, n ? err_trip / n : 0.0, n ? err_plain / n : 0.0, n);
      CHECK(n > 50 && err_trip < err_plain, "during the trip the leaves follow the plain wind (%.2f against %.2f)", n ? err_trip / n : 0.0,
            n ? err_plain / n : 0.0);
      r.stop();
    }
  }
}

// ---- [10] the stem -------------------------------------------------------------------------------------------------

// The loop tick at which the named beat begins (or -1), in a run from seed and inputs.
static int beatStart(uint64_t seed, const Inputs& in, const char* name) {
  Rig r;
  if (!r.start(seed, in)) return -1;
  int at = -1;
  const uint32_t len = appLoopTicks(r.app);
  for (uint32_t t = 0; t < len; ++t) {
    if (beatIs(r.app, name)) {
      at = (int)t;
      break;
    }
    if (appTick(r.app)) break;
  }
  r.stop();
  return at;
}

static void testStem() {
  printf("[10] the stem\n");
  struct Case {
    const char* beat;
    int family, code;
    uint16_t carry;
  };
  const Case kCases[3] = {{"taichi", SF_MORNING, 0, 0}, {"mushroom", SF_MIDDAY, 0, kRipe}, {"longshot", SF_MIDDAY, 0, 0}};
  for (int k = 0; k < 3; ++k) {
    const Case& cs = kCases[k];
    int runs = 0;
    for (uint64_t seed = 1; seed <= 300 && runs < 2; ++seed) {
      const Inputs in = inputs(cs.family, cs.code, 0, cs.carry);
      {
        Rig q;
        q.start(seed, in);
        const bool planned = indexOf(q.w().plan, k == 0 ? MJ_TAICHI : (k == 1 ? MJ_MUSHROOM : MJ_LONGSHOT)) >= 0;
        q.stop();
        if (!planned) continue;
      }
      const int at = beatStart(seed, in, cs.beat);
      if (at < 0) continue;
      ++runs;
      Rig r;
      r.start(seed, in);
      World& w = r.w();
      for (int t = 0; t + 3 < at; ++t) appTick(r.app);
      reedsGiveChew(w, 300.f);
      const bool given = reedsChewing(w);
      while (!beatIs(r.app, cs.beat)) appTick(r.app);
      bool first_tick_gone = !reedsChewing(w), at_lift = true, last_tick = false, lifted_seen = false;
      float mouth_err = -1.f;
      int prone = 0;
      bool prev = true;
      while (beatIs(r.app, cs.beat)) {
        prev = reedsChewing(w);
        appTick(r.app);
        if (k == 1 && w.day.kind == DAY_TRIP && w.day.trip.t.lifted && !lifted_seen) {
          lifted_seen = true;
          at_lift = !reedsChewing(w);
        }
        if (k == 2 && w.day.kind == DAY_PLINK && w.day.plink.p.rest == REST_PRONE && w.day.plink.p.kneel > 0.99f && reedsChewing(w)) {
          float rad = 0.f;
          const Vec2 c = hikerHeadPos(w.hiker, &rad);
          const float e = fabsf(dist(hikerMouthPos(w.hiker), c) - rad);
          if (e > mouth_err) mouth_err = e;
          ++prone;
        }
      }
      last_tick = prev;
      printf("  %s seed %llu: given %d; gone at the first tick %d; at the pick %d; there at the last tick %d; prone mouth off the face's"
             " edge by %.2f px (%d ticks)\n",
             cs.beat, (unsigned long long)seed, (int)given, (int)first_tick_gone, (int)(lifted_seen && at_lift), (int)last_tick,
             (double)mouth_err, prone);
      CHECK(given, "%s seed %llu: the stem was not given", cs.beat, (unsigned long long)seed);
      if (k == 0) CHECK(first_tick_gone, "%s seed %llu: the stem is still in his mouth at the first tick", cs.beat, (unsigned long long)seed);
      if (k == 1) CHECK(lifted_seen && at_lift, "%s seed %llu: the stem still in at the pick (%d)", cs.beat, (unsigned long long)seed, (int)lifted_seen);
      if (k == 2) {
        CHECK(last_tick, "%s seed %llu: the stem was gone by the last tick", cs.beat, (unsigned long long)seed);
        CHECK(prone > 0 && mouth_err <= 2.f, "%s seed %llu: prone mouth %.2f px off the face's edge (%d ticks)", cs.beat,
              (unsigned long long)seed, (double)mouth_err, prone);
      }
      r.stop();
    }
    CHECK(runs > 0, "no seed plays %s", cs.beat);
  }
}

// ---- [11] the heron and RS_NEAR --------------------------------------------------------------------------------------

static void testHeron() {
  printf("[11] guests and clumps\n");
  int stood = 0;
  float closest = 1e9f;
  for (uint64_t seed = 1; seed <= 300 && stood < 6; ++seed) {
    const Inputs in = inputs(SF_MORNING, 0, 0, (uint16_t)(0x80 << 8), 6);   // chapter 1 (the crane)
    Rig r;
    r.start(seed, in);
    World& w = r.w();
    const int i = indexOf(w.plan, MJ_TAICHI);
    if (i < 0 || (w.plan.major_var[i] & 64) == 0 || !(w.reeds.site[RS_NEAR].flags & 1u) || !toBeat(r.app, "taichi")) {
      r.stop();
      continue;
    }
    bool seen = false;
    float worst = 1e9f;
    while (beatIs(r.app, "taichi")) {
      appTick(r.app);
      if (streamHeronIsGuest(w) && w.stream.visit.stage == GS_STAY) {
        seen = true;
        const float d = w.stream.visit.spot.x - (float)w.reeds.site[RS_NEAR].x;
        if (d < worst) worst = d;
      }
    }
    if (seen) {
      ++stood;
      if (worst < closest) closest = worst;
      CHECK(worst >= 30.f, "seed %llu: the heron stood %.1f px right of RS_NEAR's base", (unsigned long long)seed, (double)worst);
    }
    r.stop();
  }
  printf("  the heron stood as his guest in %d runs with RS_NEAR; nearest %.1f px right of its base\n", stood, (double)closest);
  CHECK(stood > 0, "the heron never stood beside RS_NEAR");
}

// ---- [12] load ---------------------------------------------------------------------------------------------------------

static void testLoad() {
  printf("[12] load\n");
  // A herd of six in snow, every clump here (snow caps on all five).
  {
    const Inputs in = inputs(SF_MIDDAY, 2, 0, 0, 1);
    const uint64_t s = findHerd(-1, -1, 1, 1, in, 1);
    CHECK(s != 0, "no seed with a herd of six and all five clumps");
    if (s) {
      static const int kAt[6] = {500, 1100, 1700, 2000, 2400, -1};
      const HerdRun e = playHerd(s, in, kAt);
      printf("  herd of six in snow, seed %llu: %d deer at once, reed primitives up to %d, strips %.2f a frame over %d ticks, %d of %d"
             " full renders differ\n",
             (unsigned long long)s, e.deer_max, e.prims_max, e.ticks ? (double)e.strips / e.ticks : 0.0, e.ticks, e.diffs, e.checks);
      CHECK(e.ran && e.deer_max == 6, "seed %llu: %d deer", (unsigned long long)s, e.deer_max);
      CHECK(e.ticks > 0 && e.strips <= 10L * e.ticks, "seed %llu: %.2f strips a frame", (unsigned long long)s, (double)e.strips / e.ticks);
      CHECK(e.checks == 5 && e.diffs == 0, "seed %llu: %d of %d incremental frames differ", (unsigned long long)s, e.diffs, e.checks);
    }
  }
  // The trip's antlers at their peak, Lucy beside him.
  {
    uint64_t seed = 0;
    for (uint64_t s = 1; s <= 200 && !seed; ++s) {
      Rig r;
      r.start(s, inputs(SF_MIDDAY, 0, 1, kRipe));
      if (has(r.w().plan, MJ_MUSHROOM) && toBeat(r.app, "mushroom")) {
        appTick(r.app);
        if (r.w().day.kind == DAY_TRIP && r.w().day.trip.t.set == 0) seed = s;
      }
      r.stop();
    }
    CHECK(seed != 0, "no set-0 trip with a dog");
    if (seed) {
      const Inputs in = inputs(SF_MIDDAY, 0, 1, kRipe);
      Rig a, b;
      a.start(seed, in);
      b.start(seed, in);
      World& w = a.w();
      toBeat(a.app, "mushroom");
      toBeat(b.app, "mushroom");
      appInvalidate(a.app);
      appRender(a.app, a.surf);
      long strips = 0;
      int ticks = 0, checks = 0, diffs = 0, peak_tick = -1, dog_seen = 0, prims = 0;
      int next_check = 600, periodic = 0;
      while (beatIs(a.app, "mushroom")) {
        appTick(a.app);
        appTick(b.app);
        strips += appRender(a.app, a.surf);
        ++ticks;
        if (w.day.kind == DAY_TRIP && w.day.trip.t.dog) ++dog_seen;
        // The twin renders every tick as well (its sky bands latch per render, REVIEW_FIXES F28); at a check it is
        // invalidated first, so that frame is drawn whole.
        const bool peak_now = w.day.kind == DAY_TRIP && w.day.trip.t.antler_q == 64 && peak_tick < 0;
        if (!(peak_now || (ticks == next_check && periodic < 4))) appRender(b.app, b.surf);
        const bool peak = w.day.kind == DAY_TRIP && w.day.trip.t.antler_q == 64 && peak_tick < 0;
        if (peak) {
          peak_tick = ticks;
          for (int c = 0; c < REED_CLUMPS; ++c) prims += reedsPrimitives(w, c);
        }
        if (peak || (ticks == next_check && periodic < 4)) {
          if (ticks == next_check) {
            next_check += 1100;
            ++periodic;
          }
          appInvalidate(b.app);
          appRender(b.app, b.surf);
          ++checks;
          diffs += sameFrame(a.fb, b.fb) ? 0 : 1;
        }
      }
      int lent = 0;
      for (int s = 0; s < CRITTER_MAX; ++s) lent += w.critters[s].visible ? 1 : 0;
      printf("  trip set 0 with Lucy, seed %llu: antlers at their peak at beat tick %d (reed primitives %d), the dog by him on %d ticks,"
             " strips %.2f a frame over %d ticks, %d of %d full renders differ\n",
             (unsigned long long)seed, peak_tick, prims, dog_seen, ticks ? (double)strips / ticks : 0.0, ticks, diffs, checks);
      (void)lent;
      CHECK(peak_tick > 0, "seed %llu: the antlers never peaked", (unsigned long long)seed);
      CHECK(dog_seen > 0, "seed %llu: no dog lay by him", (unsigned long long)seed);
      CHECK(ticks > 0 && strips <= 10L * ticks, "seed %llu: %.2f strips a frame", (unsigned long long)seed, (double)strips / ticks);
      CHECK(checks == 5 && diffs == 0, "seed %llu: %d of %d incremental frames differ", (unsigned long long)seed, diffs, checks);
      a.stop();
      b.stop();
    }
  }
}

// ---- [13] determinism --------------------------------------------------------------------------------------------------

static uint32_t dayHash(uint64_t seed) {
  Rig r;
  Inputs in = inputs(SF_MORNING, 0, 0, 0, 5);
  in.spine_force = 0;   // the family follows the mood
  if (!r.start(seed, in)) return 0;
  uint32_t h = 2166136261u;
  for (int loop = 0; loop < 10; ++loop) {
    // morning, morning, midday x 4, evening x 4; the data's weather clear, then rain for two loops, then clear
    in.mood = loop < 2 ? 1.5f : (loop < 6 ? 3.f : 4.2f);
    in.weather_code = (loop == 3 || loop == 4) ? 1 : 0;
    appSetInputs(r.app, in);
    char plan[320], memtxt[320];
    appLoopPlan(r.app, plan, sizeof plan);
    h = fnv32(h, plan, strlen(plan));
    for (int t = 0; t < 40 * 60 * SS_TICK_HZ; ++t) {
      const bool end = appTick(r.app);
      if (t % 997 == 0) {
        appRender(r.app, r.surf);
        h = fnv32(h, r.fb + (size_t)SS_WIDTH * 200, (size_t)SS_WIDTH * 200 * sizeof(Pixel));
      }
      if (end) break;
    }
    appMemory(r.app, memtxt, sizeof memtxt);
    h = fnv32(h, memtxt, strlen(memtxt));
    const uint16_t c = appCarry(r.app);
    h = fnv32(h, &c, sizeof c);
  }
  r.stop();
  return h;
}

static void testDeterminism() {
  printf("[13] determinism\n");
  const uint32_t a = dayHash(5), b = dayHash(5), c = dayHash(6);
  printf("  a 10-loop mixed day twice: %08x %08x (another seed %08x)\n", (unsigned)a, (unsigned)b, (unsigned)c);
  CHECK(a == b && a != 0, "two runs of the same day differ");
  CHECK(a != c, "two seeds give the same day");
}

// ---- the dog rules and the memory's sets with every piece live -----------------------------------------------------------

static void testDogsAndSets() {
  printf("[dogs] the dog rules and the memory's sets with every piece live\n");
  int mid = 0, fetch_first = 0, bad = 0, tai = 0, tai_guests = 0, ripe_ok = 0, mush_loops = 0;
  for (int dog = 1; dog <= 3; ++dog)
    for (uint64_t s = 1; s <= 200; ++s) {
      Memory m;
      memset(&m, 0, sizeof m);
      for (int loop = 0; loop < 4; ++loop) {
        LoopPlan p;
        const Inputs in = inputs(SF_MIDDAY, s % 2 ? 0 : 3, dog);
        if (loop == 2) m.mush = (uint8_t)kRipe;
        planLoop(p, SF_MIDDAY, loopSeed(s, loop) ^ (uint64_t)dog, in, m);
        ++mid;
        fetch_first += has(p, MJ_FETCH) ? 1 : 0;
        if (has(p, MJ_LONGSHOT) || p.herd) ++bad;
        mush_loops += has(p, MJ_MUSHROOM) ? 1 : 0;
        if (has(p, MJ_MUSHROOM)) ripe_ok += (p.n_majors == 2 && p.major[0] == MJ_FETCH && p.major[1] == MJ_MUSHROOM) ? 1 : 0;
        if (loop == 2 && ssS22Switch() != 0) CHECK(has(p, MJ_MUSHROOM), "dog %d seed %llu: the ripe loop has no mushroom", dog, (unsigned long long)s);
      }
      Memory mm;
      memset(&mm, 0, sizeof mm);
      LoopPlan p;
      planLoop(p, SF_MORNING, loopSeed(s, 9) ^ (uint64_t)dog, inputs(SF_MORNING, 0, dog), mm);
      const int i = indexOf(p, MJ_TAICHI);
      tai += i >= 0 ? 1 : 0;
      if (i >= 0) tai_guests += (p.major_var[i] & 64) != 0 ? 1 : 0;
    }
  printf("  %d midday plans with a dog out: the fetch in %d, the long shot or the deer in %d, the ripe mushroom after the fetch %d;"
         " tai chi in %d of 600 first mornings (the plan's guest bit in %d: the begin hook keeps them off)\n",
         mid, fetch_first, bad, ripe_ok, tai, tai_guests);
  CHECK(fetch_first == mid, "the fetch in %d of %d", fetch_first, mid);
  CHECK(bad == 0, "the long shot or the deer with a dog out %d times", bad);
  if (ssS22Switch() != 0) {
    CHECK(mush_loops > 0 && ripe_ok == mush_loops, "the mushroom after the fetch %d of %d", ripe_ok, mush_loops);
    CHECK(tai == 600, "tai chi %d of 600 with a dog out", tai);
    // In a real run with a dog out: the plan's guest bit is set, the begin hook keeps every wild guest away.
    int runs = 0;
    for (uint64_t seed = 1; seed <= 60 && runs < 3; ++seed) {
      Rig r;
      r.start(seed, inputs(SF_MORNING, 0, 1 + (int)(seed % 3)));
      const int i = indexOf(r.w().plan, MJ_TAICHI);
      if (i >= 0 && (r.w().plan.major_var[i] & 64) != 0 && toBeat(r.app, "taichi")) {
        appTick(r.app);
        ++runs;
        CHECK(r.w().day.kind == DAY_TAICHI && r.w().day.taichi.guests == 0, "seed %llu: wild guests may come with a dog out",
              (unsigned long long)seed);
      }
      r.stop();
    }
    CHECK(runs == 3, "only %d tai chi runs with a dog and the guest bit", runs);
  }
  // The whole app: each loop leaves its majors, the new ids included, as the memory's newest set.
  int loops = 0, wrong = 0, new_ids = 0;
  for (int fam = 0; fam < 2; ++fam)
    for (uint64_t seed = 1; seed <= 4; ++seed) {
      Rig r;
      const Inputs in = inputs(fam == 0 ? SF_MORNING : SF_MIDDAY, 0, 0, fam == 1 && seed == 1 ? kRipe : 0);
      if (!r.start(seed, in)) continue;
      World& w = r.w();
      for (int l = 0; l < 4; ++l) {
        MajorSet want = 0;
        for (int i = 0; i < w.plan.n_majors; ++i) {
          want |= (MajorSet)1u << w.plan.major[i];
          new_ids += w.plan.major[i] >= MJ_TAICHI ? 1 : 0;
        }
        const MajorSet got = w.memory.sets_n ? w.memory.sets[w.memory.sets_n - 1] : 0;
        // spinePlan pushes the picks before the fit trims: the newest set holds the plan's majors
        wrong += (got & want) != want ? 1 : 0;
        ++loops;
        if (!toLoopEnd(r.app)) break;
      }
      r.stop();
    }
  printf("  %d loops: %d left a set without their majors; %d new-id picks among them\n", loops, wrong, new_ids);
  CHECK(wrong == 0, "%d loops' majors are not in the memory's newest set", wrong);
  if (ssS22Switch() != 0) CHECK(new_ids > 0, "no new id played");
}

int main() {
  testLayout();
  testEvening();
  testNeverTogether();
  testDogsAndSets();
  if (ssS22Switch() != 0) {
    testRipe();
    testRestarts();
    testSites();
    testHerdReeds();
    testOneWind();
    testStem();
    testHeron();
    testLoad();
  } else {
    printf("SS_S22 off: [4] to [12] need the pieces live (build-s22-on)\n");
  }
  testDeterminism();
  printf("test_stage22: %d checks\n", g_checks);
  if (g_fail) {
    printf("test_stage22: %d FAILED\n", g_fail);
    return 1;
  }
  printf("test_stage22: all passed\n");
  return 0;
}
