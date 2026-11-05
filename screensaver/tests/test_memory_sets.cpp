// Memory's sets of majors: every major id has a bit, rain_shelter's (16) too.
//
// The loop memory keeps the last MEMORY_SETS loops' majors as bit sets
// (Memory::sets, one bit per MajorId) and majorsPlan makes any pick that would
// rebuild one of them (as a prefix) a ten-thousandth as likely. A 16-bit set
// loses the bit of rain_shelter (id 16), so a set holding it matched the set
// without it and the rule never saw it. Checks:
//   - the API: a set holding id 16 is told apart from the same set without it;
//     the ring and the set list roll over for id 16 as they do for id 3;
//   - majorsPlan: a storm's pair that was played in a recent loop (rain_shelter
//     and echo) all but disappears, and so does a pair of ids below 16 (echo and
//     pinecone, in fair weather), the same way;
//   - the whole app: each loop's plan leaves its majors in the memory's newest
//     set, rain_shelter's bit included.
// Plain executable; returns non-zero on failure.
#include "ss_app.h"
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

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};   // no fast block lent: core takes one

static MajorSet bit(int id) { return (MajorSet)((MajorSet)1u << id); }

static Memory freshMemory() {
  Memory m;
  memset(&m, 0, sizeof m);
  return m;
}

// ---- the API -------------------------------------------------------------------------------

static void testApi() {
  static_assert(MJ_RAIN_SHELTER == 16, "this test is about the id past 15");
  CHECK(MJ_COUNT <= 8 * (int)sizeof(MajorSet), "%d majors do not fit a %d-bit MajorSet", (int)MJ_COUNT,
        8 * (int)sizeof(MajorSet));

  // A set with rain_shelter is recent, the same set without it is not, and
  // neither is a set with another member in its place.
  const MajorSet with_rs = (MajorSet)(bit(MJ_ECHO) | bit(MJ_RAIN_SHELTER));
  const MajorSet without = bit(MJ_ECHO);
  Memory m = freshMemory();
  memoryPushSet(m, with_rs);
  CHECK(memoryRecentSet(m, with_rs), "the set that was pushed is not recent");
  CHECK(!memoryRecentSet(m, without), "echo alone matches echo + rain_shelter: the bit of id 16 was lost");
  CHECK(!memoryRecentSet(m, (MajorSet)(bit(MJ_ECHO) | bit(MJ_BATHE))), "echo + bathe matches echo + rain_shelter");
  Memory n = freshMemory();
  memoryPushSet(n, without);
  CHECK(!memoryRecentSet(n, with_rs), "echo + rain_shelter matches echo alone: the bit of id 16 was lost");

  // The list holds the last MEMORY_SETS sets: the oldest drops out on the next push.
  // The same for a set with id 16 and a set of ids below 16.
  const MajorSet firsts[2] = {with_rs, (MajorSet)(bit(MJ_ECHO) | bit(MJ_PINECONE))};
  for (MajorSet first : firsts) {
    Memory r = freshMemory();
    memoryPushSet(r, first);
    for (int i = 0; i < MEMORY_SETS - 1; ++i) memoryPushSet(r, (MajorSet)(bit(MJ_BATHE) | bit(i)));
    CHECK(r.sets_n == MEMORY_SETS, "sets_n %d", (int)r.sets_n);
    CHECK(memoryRecentSet(r, first), "set %08x fell out of the list early", (unsigned)first);
    memoryPushSet(r, bit(MJ_MAP_CHASE));
    CHECK(!memoryRecentSet(r, first), "set %08x is still in the list MEMORY_SETS + 1 pushes on", (unsigned)first);
    CHECK(r.sets_n == MEMORY_SETS, "sets_n grew to %d", (int)r.sets_n);
  }

  // The ring: age 0 for the newest, counting up, gone after MEMORY_RING more.
  const uint8_t ids[2] = {(uint8_t)MJ_RAIN_SHELTER, (uint8_t)MJ_MAP_CHASE};
  for (uint8_t id : ids) {
    Memory r = freshMemory();
    CHECK(memoryMajorAge(r, id) == -1, "id %d in an empty ring", (int)id);
    memoryPushMajor(r, id);
    CHECK(memoryMajorAge(r, id) == 0, "id %d newest has age %d", (int)id, memoryMajorAge(r, id));
    for (int i = 0; i < MEMORY_RING - 1; ++i) memoryPushMajor(r, (uint8_t)MJ_ECHO);
    CHECK(memoryMajorAge(r, id) == MEMORY_RING - 1, "id %d oldest has age %d", (int)id, memoryMajorAge(r, id));
    memoryPushMajor(r, (uint8_t)MJ_ECHO);
    CHECK(memoryMajorAge(r, id) == -1, "id %d is still in the ring after MEMORY_RING more picks", (int)id);
  }
}

// ---- majorsPlan ----------------------------------------------------------------------------

static Inputs planInputs(bool storm) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.have_outside = storm;
  in.weather_code = storm ? 7 : 0;
  return in;
}

// How many of `trials` plans (rng seeds 1..trials) pick exactly the two majors
// in `pair`. Two slots are open, so a plan has two majors and the pair is the
// whole set. The memory is `mem` as it stands; majorsPlan only reads it.
static int pairPlans(const Memory& mem, const Inputs& in, MajorSet pair, int trials) {
  static const MajorSlots kSlots = {SF_MIDDAY, {SB_FIRE, SB_FIRE, SB_DINNER, SB_DINNER}, 2 | 4};
  int hits = 0;
  for (int s = 1; s <= trials; ++s) {
    LoopPlan p;
    memset(&p, 0, sizeof p);
    p.family = SF_MIDDAY;
    Memory m = mem;
    Rng rng;
    rng.seed((uint64_t)s);
    majorsPlan(p, rng, in, m, kSlots);
    MajorSet got = 0;
    for (int i = 0; i < p.n_majors; ++i) got = (MajorSet)(got | bit(p.major[i]));
    if (p.n_majors == 2 && got == pair) ++hits;
  }
  return hits;
}

// `pair` was a recent loop's set: its plans (fresh memory: `free_hits`) must nearly
// vanish with it in the memory (`rule_hits`); one in twenty is far more than
// the rule's one in ten thousand leaves, and far less than a rule that sees
// one of the two ids in the pair and not the other (about half).
static void checkPairRule(const char* what, MajorSet pair, bool storm, int trials) {
  const Inputs in = planInputs(storm);
  CHECK(weatherPlannedStorm(in) == storm, "the inputs %s a storm", storm ? "do not plan" : "plan");
  const Memory fresh = freshMemory();
  Memory seen = freshMemory();
  memoryPushSet(seen, pair);
  const int free_hits = pairPlans(fresh, in, pair, trials);
  const int rule_hits = pairPlans(seen, in, pair, trials);
  printf("%s: %d of %d plans make the pair with no memory, %d with it a recent set\n", what, free_hits, trials,
         rule_hits);
  CHECK(free_hits >= 100, "%s: only %d plans make the pair with no memory (too few to tell)", what, free_hits);
  CHECK(rule_hits * 20 <= free_hits, "%s: %d plans still make the recent pair, %d without the memory", what, rule_hits,
        free_hits);
}

static void testPlan() {
  // rain_shelter (id 16) and echo: a storm's pair. The rule used to leave the
  // plans that picked echo first and rain_shelter second (about 40% of them).
  checkPairRule("storm, rain_shelter + echo", (MajorSet)(bit(MJ_RAIN_SHELTER) | bit(MJ_ECHO)), true, 6000);
  // Ids below 16 behave as before: echo and pinecone in fair weather. (25000 trials: the midday pool has 24 majors now, so
  // the pair comes up about half as often as it did with 16 and 10000 trials were enough.)
  checkPairRule("fair, echo + pinecone", (MajorSet)(bit(MJ_ECHO) | bit(MJ_PINECONE)), false, 25000);
  // And a storm does not change that for them.
  checkPairRule("storm, echo + pinecone", (MajorSet)(bit(MJ_ECHO) | bit(MJ_PINECONE)), true, 20000);

  // Fair weather never plans rain_shelter, with or without the memory of it.
  Memory seen = freshMemory();
  memoryPushSet(seen, (MajorSet)(bit(MJ_RAIN_SHELTER) | bit(MJ_ECHO)));
  const int none = pairPlans(seen, planInputs(false), (MajorSet)(bit(MJ_RAIN_SHELTER) | bit(MJ_ECHO)), 2000);
  CHECK(none == 0, "%d fair-weather plans picked rain_shelter", none);
}

// ---- the whole app -------------------------------------------------------------------------

// Each loop's plan leaves its majors in the memory's newest set: all the bits
// (rain_shelter's among them), none extra. A storm brings rain_shelter in
// several of the loops.
static void testApp(int family) {
  Inputs in = planInputs(true);
  in.spine_force = (uint8_t)(1 + family);
  void* mem = malloc(SS_APP_BYTES);
  App* app = mem ? appStart(mem, kAlloc, 7, in) : nullptr;
  CHECK(app != nullptr, "appStart failed");
  if (!app) {
    free(mem);
    return;
  }
  const int loops = 8;
  int with_rs = 0, wrong = 0;
  for (int l = 0; l < loops; ++l) {
    const World& w = *appWorld(app);
    MajorSet want = 0;
    bool has_rs = false;
    for (int i = 0; i < w.plan.n_majors; ++i) {
      want = (MajorSet)(want | bit(w.plan.major[i]));
      has_rs = has_rs || w.plan.major[i] == MJ_RAIN_SHELTER;
    }
    const MajorSet got = w.memory.sets_n > 0 ? w.memory.sets[w.memory.sets_n - 1] : 0;
    if (got != want) {
      ++wrong;
      printf("loop %d: plan set %08x, memory's newest set %08x\n", l, (unsigned)want, (unsigned)got);
    }
    if (has_rs) {
      ++with_rs;
      CHECK((got & bit(MJ_RAIN_SHELTER)) != 0, "loop %d plays rain_shelter but its bit is not in the memory's set", l);
      CHECK(memoryMajorAge(w.memory, MJ_RAIN_SHELTER) >= 0, "loop %d: rain_shelter is not in the ring", l);
    }
    if (l + 1 < loops) {
      uint32_t guard = 0;
      while (!appTick(app) && guard++ < 40000u) {}
      CHECK(guard < 40000u, "loop %d never rolled over", l);
    }
  }
  CHECK(wrong == 0, "%d loops left a set other than their plan's", wrong);
  CHECK(with_rs >= 2, "family %d: only %d of %d storm loops played rain_shelter", family, with_rs, loops);
  printf("app, family %d: %d of %d storm loops played rain_shelter; the memory kept its bit each time\n", family,
         with_rs, loops);
  appStop(app, kAlloc);
  free(mem);
}

int main() {
  testApi();
  testPlan();
  testApp(SF_EVENING);
  testApp(SF_MIDDAY);
  if (g_fail) {
    printf("test_memory_sets: %d FAILED\n", g_fail);
    return 1;
  }
  printf("test_memory_sets: ok\n");
  return 0;
}
