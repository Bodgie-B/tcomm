// Easing, sine table, PCG32 and Timeline tests. Plain executable.
#include "anim/ease.h"
#include "anim/rng.h"
#include "anim/scheduler.h"

#include <math.h>
#include <stdio.h>
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

// --- easing -----------------------------------------------------------------

typedef float (*EaseFn)(float);

static void testEasing() {
  struct { const char* name; EaseFn fn; } list[] = {
      {"easeInOut", easeInOut},           {"easeInCubic", easeInCubic},
      {"easeOutCubic", easeOutCubic},     {"easeInOutCubic", easeInOutCubic},
      {"easeOutBack", easeOutBack},       {"easeOutElastic", easeOutElastic},
      {"easeOutBounce", easeOutBounce},
  };
  for (auto& e : list) {
    CHECK(fabsf(e.fn(0.f)) < 1e-4f, "%s(0) = %g", e.name, (double)e.fn(0.f));
    CHECK(fabsf(e.fn(1.f) - 1.f) < 1e-4f, "%s(1) = %g", e.name, (double)e.fn(1.f));
  }
  float maxBack = 0.f, maxBounce = 0.f, minBounce = 1.f;
  for (int i = 0; i <= 1000; ++i) {
    const float t = (float)i / 1000.f;
    if (easeOutBack(t) > maxBack) maxBack = easeOutBack(t);
    const float b = easeOutBounce(t);
    if (b > maxBounce) maxBounce = b;
    if (b < minBounce) minBounce = b;
  }
  CHECK(maxBack > 1.f, "easeOutBack never exceeds 1 (max %g)", (double)maxBack);
  CHECK(maxBounce <= 1.001f && minBounce >= 0.f, "easeOutBounce range [%g, %g]", (double)minBounce, (double)maxBounce);
  float maxEl = 0.f;
  for (int i = 0; i <= 1000; ++i) maxEl = fmaxf(maxEl, easeOutElastic((float)i / 1000.f));
  CHECK(maxEl > 1.f, "easeOutElastic should overshoot (max %g)", (double)maxEl);
  CHECK(easeInOut(0.5f) > 0.499f && easeInOut(0.5f) < 0.501f, "smoothstep midpoint");
  CHECK(easeInOutCubic(0.5f) > 0.499f && easeInOutCubic(0.5f) < 0.501f, "inOutCubic midpoint");
  CHECK(easeOutCubic(0.5f) > easeInCubic(0.5f), "out should lead in at 0.5");
  // out-of-range input is clamped
  CHECK(easeInOut(-1.f) == 0.f && easeInOut(2.f) == 1.f, "easeInOut clamps");
  // window
  CHECK(window(0.5f, 0.25f, 0.75f) == 0.5f, "window midpoint");
  CHECK(window(0.f, 0.25f, 0.75f) == 0.f && window(1.f, 0.25f, 0.75f) == 1.f, "window clamps");
  CHECK(window(0.3f, 0.5f, 0.5f) == 0.f && window(0.7f, 0.5f, 0.5f) == 1.f, "window degenerate");
  CHECK(clamp01(-1.f) == 0.f && clamp01(2.f) == 1.f && lerp(2.f, 4.f, 0.5f) == 3.f, "clamp01/lerp");
}

static void testSine() {
  float maxErr = 0.f, maxCosErr = 0.f;
  const float twoPi = 6.2831855f;
  for (int i = 0; i < 4096; ++i) {
    const float ph = -2.f + 4.f * (float)i / 4096.f;   // spans negative phases and several turns
    const float e = fabsf(sinT(ph) - sinf(twoPi * ph));
    const float ec = fabsf(cosT(ph) - cosf(twoPi * ph));
    if (e > maxErr) maxErr = e;
    if (ec > maxCosErr) maxCosErr = ec;
  }
  printf("sinT max error %.2e, cosT max error %.2e (limit 2e-3)\n", (double)maxErr, (double)maxCosErr);
  CHECK(maxErr < 2e-3f, "sinT error %g", (double)maxErr);
  CHECK(maxCosErr < 2e-3f, "cosT error %g", (double)maxCosErr);
  CHECK(sinT(-1e-9f) < 1e-3f && sinT(-1e-9f) > -1e-3f, "sinT just below zero");

  float lo = 0.f, hi = 0.f;
  for (int oct = 1; oct <= 4; ++oct) {
    lo = hi = 0.f;
    for (int i = 0; i < 20000; ++i) {
      const float v = sumSines((float)i * 0.0137f, oct);
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    CHECK(lo >= -1.0001f && hi <= 1.0001f, "sumSines(%d octaves) range [%g, %g]", oct, (double)lo, (double)hi);
    CHECK(hi - lo > 0.8f, "sumSines(%d) barely moves: [%g, %g]", oct, (double)lo, (double)hi);
  }
  CHECK(sumSines(1.234f, 3) == sumSines(1.234f, 3), "sumSines deterministic");
  CHECK(sumSines(0.5f, 0) == sumSines(0.5f, 1) && sumSines(0.5f, 9) == sumSines(0.5f, 4), "octaves clamp");
}

// --- RNG --------------------------------------------------------------------

static void testRng() {
  Rng r;
  r.seed(42u, 54u);
  // Reference values from the PCG32 demo (pcg32-demo, seed 42, stream 54).
  const uint32_t ref[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
  CHECK(r.next() == ref[0], "first PCG32 output is not 0xa15c02b7");
  r.seed(42u, 54u);
  for (int i = 0; i < 6; ++i) {
    const uint32_t v = r.next();
    CHECK(v == ref[i], "PCG32 output %d = 0x%08x, expected 0x%08x", i, v, ref[i]);
  }

  Rng a, b, c;
  a.seed(12345u);
  b.seed(12345u);
  c.seed(12346u);
  bool same = true, differs = false;
  for (int i = 0; i < 1000; ++i) {
    const uint32_t x = a.next(), y = b.next(), z = c.next();
    same = same && x == y;
    differs = differs || x != z;
  }
  CHECK(same, "same seed must replay the same sequence");
  CHECK(differs, "different seeds must differ");

  bool inRange = true, sawLo = false, sawHi = false, rngOk = true;
  float mean = 0.f;
  for (int i = 0; i < 20000; ++i) {
    const float u = a.uniform();
    inRange = inRange && u >= 0.f && u < 1.f;
    mean += u;
    const float v = a.range(-2.f, 3.f);
    rngOk = rngOk && v >= -2.f && v < 3.f;
    const int k = a.rangeInt(-3, 4);
    sawLo = sawLo || k == -3;
    sawHi = sawHi || k == 4;
    rngOk = rngOk && k >= -3 && k <= 4;
  }
  mean /= 20000.f;
  CHECK(inRange, "uniform() left [0,1)");
  CHECK(mean > 0.48f && mean < 0.52f, "uniform mean %g", (double)mean);
  CHECK(rngOk && sawLo && sawHi, "range/rangeInt bounds (lo seen %d, hi seen %d)", sawLo, sawHi);
  CHECK(a.rangeInt(5, 5) == 5 && a.rangeInt(7, 2) == 7, "rangeInt degenerate");

  int hits = 0;
  for (int i = 0; i < 10000; ++i) hits += a.chance(0.25f) ? 1 : 0;
  CHECK(hits > 2300 && hits < 2700, "chance(0.25) hit %d / 10000", hits);
  CHECK(!a.chance(0.f) && a.chance(1.f), "chance extremes");

  const float w2[2] = {1.f, 3.f};
  int second = 0;
  for (int i = 0; i < 10000; ++i) second += a.weighted(w2, 2);
  const float frac = (float)second / 10000.f;
  printf("weighted {1,3}: second index %.1f%% (want 75 +/- 2)\n", (double)(frac * 100.f));
  CHECK(frac > 0.73f && frac < 0.77f, "weighted second-index fraction %g", (double)frac);

  const float zeros[3] = {0.f, 0.f, 0.f};
  CHECK(a.weighted(zeros, 3) == 0, "all-zero weights should give 0");
  CHECK(a.weighted(w2, 0) == 0, "n=0 gives 0");
  const float skip[3] = {0.f, 5.f, 0.f};
  bool onlyMid = true;
  for (int i = 0; i < 500; ++i) onlyMid = onlyMid && a.weighted(skip, 3) == 1;
  CHECK(onlyMid, "zero-weight entries must never be picked");
}

// --- Timeline ---------------------------------------------------------------
// The scheduler passes SceneCtx through untouched, so the test hands it a
// dummy object (not defining ss::SceneCtx, which the scene layer owns).

static char g_dummy;
static SceneCtx& ctx() { return *reinterpret_cast<SceneCtx*>(&g_dummy); }

struct HookEvent {
  char kind;   // 'B' begin, 'E' end
  int beat;
  uint32_t tick;
};
static HookEvent g_log[256];
static int g_logN = 0;
static int g_updates[8];
static float g_firstT[8], g_lastT[8];
static uint32_t* g_tickPtr = nullptr;   // points at Timeline::tick_count for log stamps

static void logHook(char kind, int beat) {
  if (g_logN < 256) g_log[g_logN++] = HookEvent{kind, beat, g_tickPtr ? *g_tickPtr : 0u};
}
static void resetLog(Timeline& tl) {
  g_logN = 0;
  memset(g_updates, 0, sizeof g_updates);
  for (int i = 0; i < 8; ++i) g_firstT[i] = g_lastT[i] = -1.f;
  g_tickPtr = &tl.tick_count;
}
static void doUpdate(int beat, float t, float dt) {
  CHECK(fabsf(dt - SS_TICK_DT) < 1e-9f, "dt %g", (double)dt);
  if (g_updates[beat] == 0) g_firstT[beat] = t;
  g_lastT[beat] = t;
  ++g_updates[beat];
}

#define BEAT_FUNCS(N)                                                   \
  static void begin##N(SceneCtx&) { logHook('B', N); }                  \
  static void end##N(SceneCtx&) { logHook('E', N); }                    \
  static void update##N(SceneCtx&, float t, float dt) { doUpdate(N, t, dt); }
BEAT_FUNCS(0)
BEAT_FUNCS(1)
BEAT_FUNCS(2)
BEAT_FUNCS(3)

static Beat makeBeat(int n, float dur) {
  static void (*const bg[4])(SceneCtx&) = {begin0, begin1, begin2, begin3};
  static void (*const en[4])(SceneCtx&) = {end0, end1, end2, end3};
  static void (*const up[4])(SceneCtx&, float, float) = {update0, update1, update2, update3};
  static const char* names[4] = {"A", "B", "C", "D"};
  return Beat{names[n], dur, bg[n], up[n], en[n]};
}

static int expectedTicksUntilEnd(const float* durs, int n) {
  // Boundary rule (see scheduler.cpp): leftover carries across beats, so beat k
  // ends on tick ceil(cumulative_seconds_k * SS_TICK_HZ).
  double cum = 0.0;
  for (int i = 0; i < n; ++i) cum += (double)durs[i];
  return (int)ceil(cum * SS_TICK_HZ - 1e-6);
}

static void testTimelineRun() {
  Timeline tl;
  tl.reset();
  resetLog(tl);
  const float durs[3] = {1.0f, 0.5f, 2.0f};
  for (int i = 0; i < 3; ++i) CHECK(tl.add(makeBeat(i, durs[i])), "add");
  CHECK(tl.progress() == 0.f && tl.currentBeat() == nullptr, "before start: no current beat");
  tl.start(ctx());
  CHECK(tl.current == 0 && tl.currentBeat() != nullptr && tl.progress() == 0.f, "start puts us in beat 0");
  CHECK(g_logN == 1 && g_log[0].kind == 'B' && g_log[0].beat == 0, "start calls begin(0)");

  // Expected from SS_TICK_HZ: beat ends at ceil(cum*HZ): 25, 38, 88 -> 25+13+50.
  const int e0 = expectedTicksUntilEnd(durs, 1);
  const int e1 = expectedTicksUntilEnd(durs, 2) - e0;
  const int e2 = expectedTicksUntilEnd(durs, 3) - e0 - e1;
  const int expectTotal = expectedTicksUntilEnd(durs, 3);
  printf("expected ticks per beat %d + %d + %d = %d (SS_TICK_HZ=%d)\n", e0, e1, e2, expectTotal, SS_TICK_HZ);
  CHECK(e0 == 25 && e1 == 13 && e2 == 50 && expectTotal == 88, "expected arithmetic changed with SS_TICK_HZ");

  int calls = 0, trues = 0;
  bool progressSane = true;
  float progressAt12 = -1.f;
  while (calls < 1000) {
    const bool more = tl.tick(ctx());
    ++calls;
    if (more) ++trues;
    if (calls == 12) progressAt12 = tl.progress();
    if (more) progressSane = progressSane && tl.progress() >= 0.f && tl.progress() < 1.f;
    if (!more) break;
  }
  printf("tick() returned false on call %d (%d true results); Timeline::tick_count=%u\n", calls, trues, (unsigned)tl.tick_count);
  CHECK(calls == expectTotal, "tick() returned false on call %d, expected %d", calls, expectTotal);
  CHECK(trues == expectTotal - 1, "true results %d", trues);
  CHECK(tl.tick_count == (uint32_t)calls, "ticks counter %u", (unsigned)tl.tick_count);
  CHECK(progressSane, "progress left [0,1) mid-run");
  CHECK(fabsf(progressAt12 - 0.48f) < 1e-3f, "progress after 12 ticks of a 1.0 s beat = %g", (double)progressAt12);
  CHECK(tl.current == -1 && tl.currentBeat() == nullptr && tl.progress() == 0.f, "after end: current -1, progress 0");

  CHECK(g_updates[0] == e0 && g_updates[1] == e1 && g_updates[2] == e2, "update counts %d %d %d, expected %d %d %d",
        g_updates[0], g_updates[1], g_updates[2], e0, e1, e2);
  CHECK(g_firstT[0] == 0.f && fabsf(g_firstT[1]) < 1e-5f, "first update of a beat has t~0 (carried leftover is 0 or <1 tick): %g, %g", (double)g_firstT[0],
        (double)g_firstT[1]);
  CHECK(g_lastT[0] < 1.f && g_lastT[2] < 1.f && g_lastT[0] > 0.9f, "last t values %g %g", (double)g_lastT[0], (double)g_lastT[2]);

  // Hooks: B0 (start), E0 B1, E1 B2, E2 -- each once, in order.
  const char kinds[6] = {'B', 'E', 'B', 'E', 'B', 'E'};
  const int ids[6] = {0, 0, 1, 1, 2, 2};
  CHECK(g_logN == 6, "hook count %d, expected 6", g_logN);
  // Order: B0 | E0 B1 | E1 B2 | E2  => reorder expected as sequence
  const char seqK[7] = {'B', 'E', 'B', 'E', 'B', 'E', 0};
  (void)kinds; (void)ids; (void)seqK;
  const struct { char k; int b; } want[6] = {{'B', 0}, {'E', 0}, {'B', 1}, {'E', 1}, {'B', 2}, {'E', 2}};
  for (int i = 0; i < 6 && i < g_logN; ++i)
    CHECK(g_log[i].kind == want[i].k && g_log[i].beat == want[i].b, "hook %d is %c%d, expected %c%d", i,
          g_log[i].kind, g_log[i].beat, want[i].k, want[i].b);
  if (g_logN == 6) {
    CHECK(g_log[1].tick == 25 && g_log[2].tick == 25, "beat 0 ends / beat 1 begins on tick 25 (got %u/%u)",
          (unsigned)g_log[1].tick, (unsigned)g_log[2].tick);
    CHECK(g_log[3].tick == 38 && g_log[4].tick == 38, "beat 1 ends on tick 38 (got %u/%u)", (unsigned)g_log[3].tick,
          (unsigned)g_log[4].tick);
    CHECK(g_log[5].tick == 88, "beat 2 ends on tick 88 (got %u)", (unsigned)g_log[5].tick);
  }

  // After the end: false again, counter keeps counting, no hooks fire.
  const int before = g_logN;
  CHECK(!tl.tick(ctx()) && !tl.tick(ctx()), "tick after end returns false");
  CHECK(tl.tick_count == (uint32_t)calls + 2u && g_logN == before, "tick counter increments every call, no hooks");

  // Empty timeline.
  Timeline e;
  e.reset();
  e.start(ctx());
  CHECK(e.current == -1 && !e.tick(ctx()) && e.tick_count == 1u, "empty timeline");
}

static void testEventsAndFull() {
  Timeline tl;
  tl.reset();
  for (int i = 0; i < SS_MAX_PENDING_EVENTS; ++i) CHECK(tl.post(Event{EV_DOG, i}), "post %d", i);
  CHECK(!tl.post(Event{EV_DOORBELL, 99}), "post into a full queue must fail");
  CHECK(tl.pending_count == SS_MAX_PENDING_EVENTS, "pending_count");
  Event out[SS_MAX_PENDING_EVENTS];
  int n = tl.takeEvents(out, 3);
  CHECK(n == 3 && out[0].arg == 0 && out[1].arg == 1 && out[2].arg == 2 && out[0].type == EV_DOG, "take 3 oldest first");
  CHECK(tl.pending_count == SS_MAX_PENDING_EVENTS - 3, "remaining pending");
  CHECK(tl.post(Event{EV_WEATHER, 7}), "post after draining makes room");
  n = tl.takeEvents(out, SS_MAX_PENDING_EVENTS);
  CHECK(n == SS_MAX_PENDING_EVENTS - 2, "drain rest (%d)", n);
  CHECK(out[0].arg == 3 && out[n - 1].type == EV_WEATHER && out[n - 1].arg == 7, "order preserved across shift");
  CHECK(tl.takeEvents(out, 4) == 0 && tl.pending_count == 0, "empty queue");
  CHECK(tl.takeEvents(out, 0) == 0, "max 0");

  // add() fails when full.
  tl.reset();
  bool allOk = true;
  for (int i = 0; i < SS_MAX_BEATS; ++i) allOk = allOk && tl.add(makeBeat(0, 1.f));
  CHECK(allOk && !tl.add(makeBeat(0, 1.f)) && tl.count == SS_MAX_BEATS, "add fails past SS_MAX_BEATS");
  tl.current = 0;
  tl.insertAfterCurrent(makeBeat(1, 1.f));
  CHECK(tl.count == SS_MAX_BEATS && tl.beats[1].name[0] == 'A', "insertAfterCurrent into full timeline is a no-op");
}

static void testInsertAndJump() {
  // insertAfterCurrent: A B C, insert D while A runs -> A D B C.
  Timeline tl;
  tl.reset();
  resetLog(tl);
  tl.add(makeBeat(0, 0.2f));
  tl.add(makeBeat(1, 0.2f));
  tl.add(makeBeat(2, 0.2f));
  tl.start(ctx());
  tl.tick(ctx());
  tl.insertAfterCurrent(makeBeat(3, 0.2f));
  CHECK(tl.count == 4 && tl.current == 0, "count/current after insert");
  CHECK(tl.beats[0].name[0] == 'A' && tl.beats[1].name[0] == 'D' && tl.beats[2].name[0] == 'B' &&
            tl.beats[3].name[0] == 'C',
        "order after insert: %s %s %s %s", tl.beats[0].name, tl.beats[1].name, tl.beats[2].name, tl.beats[3].name);
  int guard = 0;
  while (tl.tick(ctx()) && ++guard < 500) {}
  int begins[4], nb = 0;
  for (int i = 0; i < g_logN; ++i)
    if (g_log[i].kind == 'B' && nb < 4) begins[nb++] = g_log[i].beat;
  CHECK(nb == 4 && begins[0] == 0 && begins[1] == 3 && begins[2] == 1 && begins[3] == 2, "run order A D B C");

  // Insert from the middle (current = 1 of A B C -> A B X C).
  tl.reset();
  tl.add(makeBeat(0, 1.f));
  tl.add(makeBeat(1, 1.f));
  tl.add(makeBeat(2, 1.f));
  tl.start(ctx());
  tl.jumpTo(ctx(), 1);
  tl.insertAfterCurrent(makeBeat(3, 1.f));
  CHECK(tl.beats[2].name[0] == 'D' && tl.beats[3].name[0] == 'C', "insert in the middle");

  // No current beat (before start): appended.
  tl.reset();
  tl.add(makeBeat(0, 1.f));
  tl.insertAfterCurrent(makeBeat(1, 1.f));
  CHECK(tl.count == 2 && tl.beats[1].name[0] == 'B', "insert with no current beat appends");

  // jumpTo: A B C, tick a few, jump to C -> E(A) B(C) logged, B never begins.
  tl.reset();
  resetLog(tl);
  tl.add(makeBeat(0, 1.f));
  tl.add(makeBeat(1, 1.f));
  tl.add(makeBeat(2, 0.2f));
  tl.start(ctx());
  for (int i = 0; i < 3; ++i) tl.tick(ctx());
  CHECK(tl.elapsed > 0.1f, "elapsed advanced before jump");
  const int mark = g_logN;
  tl.jumpTo(ctx(), 2);
  CHECK(g_logN == mark + 2 && g_log[mark].kind == 'E' && g_log[mark].beat == 0 && g_log[mark + 1].kind == 'B' &&
            g_log[mark + 1].beat == 2,
        "jumpTo logs end(A) then begin(C)");
  CHECK(tl.current == 2 && tl.elapsed == 0.f && tl.progress() == 0.f, "jumpTo resets elapsed");
  tl.jumpTo(ctx(), 9);
  tl.jumpTo(ctx(), -1);
  CHECK(tl.current == 2 && g_logN == mark + 2, "invalid jump index is ignored");
  int guard2 = 0;
  while (tl.tick(ctx()) && ++guard2 < 500) {}
  bool bBegan = false;
  for (int i = 0; i < g_logN; ++i) bBegan = bBegan || (g_log[i].kind == 'B' && g_log[i].beat == 1);
  CHECK(!bBegan && tl.current == -1, "skipped beat never begins; timeline ends after C");

  // jumpTo after the end (current == -1) restarts at that beat without an end hook.
  const int mark2 = g_logN;
  tl.jumpTo(ctx(), 0);
  CHECK(tl.current == 0 && g_logN == mark2 + 1 && g_log[mark2].kind == 'B', "jumpTo from stopped state only begins");
}

int main() {
  testEasing();
  testSine();
  testRng();
  testTimelineRun();
  testEventsAndFull();
  testInsertAndJump();
  if (g_fail) {
    printf("test_anim: %d FAILURE(S)\n", g_fail);
    return 1;
  }
  printf("test_anim: OK\n");
  return 0;
}
