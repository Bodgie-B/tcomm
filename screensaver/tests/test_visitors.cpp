// Tests for stage 15's visiting couple (core/scene/visitors.cpp):
//
//   [1] off unless asked: Inputs::visitors zeroed never plans them; no loop changes
//   [2] how often: about one camp loop in seven, never two loops running, never in a set piece or
//       on a wet loop
//   [3] each kind (fireside, frisbee with a dog, telescopes on a moonless night) comes, plays and goes:
//       he is back exactly where he was, the story carries on with no overrun, nothing is left
//   [4] every frame while they are here equals a full render
//   [5] the same seed replays frame for frame
//   [6] exclusions: not while fire and ice or a doorbell reaction runs; fire and ice waits for them
//   [7] memory: the visitors in the Lookout's place, the scene block within its budget
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "scene/fireice.h"
#include "scene/spine.h"
#include "scene/visitors.h"
#include "scene/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

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

enum Look { EVENING, MIDDAY, NIGHT_DARK };

static Inputs makeInputs(Look l, int dog = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  in.chip = true;
  in.dog_out = dog;
  in.visitors = true;
  if (l == EVENING) {
    in.mood = 4.6f;
    in.spine_force = 1 + SF_EVENING;
  } else if (l == MIDDAY) {
    in.mood = 3.f;
    in.spine_force = 1 + SF_MIDDAY;
  } else {
    in.mood = 0.f;
    in.spine_force = 1 + SF_EVENING;
    in.night_mode = true;
    in.weather.night_dim = 1.f;
    in.sky.have = true;
    in.sky.sun_alt = -40.f;
    in.sky.moon_alt = -25.f;
  }
  return in;
}

struct Rig {
  void* mem = nullptr;
  std::vector<Pixel> fb, full;
  Surface surf{}, fsurf{};
  App* app = nullptr;
  Inputs in{};
  bool start(uint64_t seed, const Inputs& i) {
    in = i;
    mem = malloc(SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    full.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    fsurf = Surface{full.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = mem ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  bool step() {
    const bool n = appTick(app);
    appRender(app, surf);
    return n;
  }
  int fullDiff() {
    appInvalidate(app);
    appRender(app, fsurf);
    int n = 0;
    for (size_t i = 0; i < fb.size(); ++i) n += fb[i] != full[i] ? 1 : 0;
    return n;
  }
  World& w() { return *const_cast<World*>(appWorld(app)); }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
    mem = nullptr;
  }
};

static uint32_t hashFb(const std::vector<Pixel>& fb) {
  uint32_t h = 2166136261u;
  for (Pixel p : fb) h = (h ^ p) * 16777619u;
  return h;
}

// The first seed from `from` whose first loop has them.
static uint64_t seedWithVisit(Look l, int dog, uint64_t from = 1) {
  for (uint64_t s = from; s < from + 500; ++s) {
    Rig r;
    r.start(s, makeInputs(l, dog));
    const bool p = r.w().vis.planned;
    r.stop();
    if (p) return s;
  }
  return 0;
}

static void testOff() {
  printf("[1] off unless asked\n");
  int planned = 0;
  for (uint64_t s = 1; s <= 60; ++s) {
    Rig r;
    Inputs in = makeInputs(s % 2 ? MIDDAY : EVENING);
    in.visitors = false;
    r.start(s, in);
    planned += r.w().vis.planned ? 1 : 0;
    r.stop();
  }
  CHECK(planned == 0, "planned %d with visitors off", planned);
  // A loop with them off plays exactly as one with them on but not planned: the same frames.
  const uint64_t s = 2;
  Rig a, b;
  Inputs ia = makeInputs(MIDDAY), ib = makeInputs(MIDDAY);
  ia.visitors = false;
  a.start(s, ia);
  b.start(s, ib);
  const bool planned_b = b.w().vis.planned;
  bool same = true;
  for (int t = 0; t < 4000 && same; ++t) {
    a.step();
    b.step();
    if (!planned_b && t % 50 == 0) same = hashFb(a.fb) == hashFb(b.fb);
  }
  CHECK(planned_b || same, "no visit: the same loop either way");
  a.stop();
  b.stop();
}

static void testFrequency() {
  printf("[2] how often\n");
  World* w = static_cast<World*>(calloc(1, sizeof(World)));
  Inputs in = makeInputs(MIDDAY);
  int visits = 0, loops = 0, running = 0;
  bool last = false;
  for (uint64_t s = 1; s <= 7000; ++s) {
    w->plan.family = SF_MIDDAY;
    w->weather.kind = WX_CLEAR;
    visitorsBeginLoop(*w, s * 0x2545F4914F6CDD1Dull, in);
    const bool v = w->vis.planned;   // planned and (as in the scene) begun
    if (v) visitorsMarkVisited(w->memory);
    if (v && last) ++running;
    last = v;
    visits += v ? 1 : 0;
    ++loops;
  }
  const float rate = (float)visits / (float)loops;
  printf("    %d visits in %d loops: one in %.1f\n", visits, loops, (double)(1.f / rate));
  CHECK(rate > 1.f / 8.5f && rate < 1.f / 5.5f, "one loop in %.1f", (double)(1.f / rate));
  CHECK(running == 0, "%d visits two loops running", running);
  // Never in a set piece (the union is theirs) or on a wet loop.
  int bad = 0;
  for (uint64_t s = 1; s <= 400; ++s) {
    w->memory.visit_wait = 0;
    w->plan.family = s % 2 ? SF_LOOKOUT : SF_SUNRISE;
    w->lk.tower_x = 1234;
    visitorsBeginLoop(*w, s, in);
    if (w->lk.tower_x != 1234) ++bad;
    w->plan.family = SF_EVENING;
    w->weather.kind = (uint8_t)(s % 3 == 0 ? WX_RAIN : (s % 3 == 1 ? WX_STORM : WX_SNOW));
    w->memory.visit_wait = 0;
    visitorsBeginLoop(*w, s, in);
    if (w->vis.planned) ++bad;
  }
  CHECK(bad == 0, "%d set-piece or wet loops touched", bad);
  free(w);
}

struct VisitRun {
  bool came = false, went = false, home = false, kind_ok = false, clean = false;
  int worst_diff = 0, checks = 0, overruns = 0;
  std::vector<uint32_t> hashes;
};

static VisitRun runVisit(Look l, int dog, uint64_t seed, int want_kind, bool check_full) {
  VisitRun v;
  Rig r;
  r.start(seed, makeInputs(l, dog));
  Vec2 home{0.f, 0.f};
  int8_t face = 0;
  bool walked = false;   // he was on his way somewhere: he sets off again at once (a step on by the check)
  for (int t = 0; t < 25 * 900; ++t) {
    const bool before = r.w().vis.on;
    const int over = appSpineOverruns(r.app);
    if (r.step()) {
      v.overruns = over;
      break;
    }
    World& w = r.w();
    if (!before && w.vis.on) {
      v.came = true;
      home = w.vis.home;
      face = w.vis.home_face;
      walked = w.vis.home_walk;
      v.kind_ok = w.vis.kind == want_kind;
    }
    if (w.vis.on && check_full && t % 40 == 0) {
      const int d = r.fullDiff();
      ++v.checks;
      if (d > v.worst_diff) v.worst_diff = d;
      if (d) printf("    tick %d %s: %d px differ\n", t, visitorsStepName(w), d);
    }
    if (w.vis.on && t % 61 == 0) v.hashes.push_back(hashFb(r.fb));
    if (before && !w.vis.on) {
      v.went = true;
      v.home = walked ? (w.hiker.walking && w.hiker.feet.x - home.x < 4.f && home.x - w.hiker.feet.x < 4.f)
                      : (w.hiker.feet.x == home.x && w.hiker.feet.y == home.y && w.hiker.facing == face);
      v.clean = !w.busy && w.hiker.carry == CARRY_NONE && w.dogs.lent == 0 && !w.vis.v[0].pack_down && !w.vis.v[1].pack_down &&
                w.vis.scope_k[0] == 0.f && w.vis.v[0].rect.empty() && w.vis.v[1].rect.empty();
    }
  }
  r.stop();
  return v;
}

static void testKinds() {
  printf("[3]-[5] each kind\n");
  struct K {
    const char* name;
    Look l;
    int dog, kind;
  } ks[] = {{"fireside", EVENING, 0, VK_FIRESIDE}, {"frisbee", MIDDAY, 2, VK_FRISBEE}, {"telescopes", NIGHT_DARK, 0, VK_SCOPES}};
  for (const K& k : ks) {
    for (uint64_t from : {1ull, 200ull}) {
      const uint64_t seed = seedWithVisit(k.l, k.dog, from);
      CHECK(seed != 0, "%s: a seed with a visit", k.name);
      if (!seed) continue;
      const VisitRun v = runVisit(k.l, k.dog, seed, k.kind, true);
      CHECK(v.came && v.went && v.kind_ok, "%s seed %d: came %d went %d kind %d", k.name, (int)seed, (int)v.came, (int)v.went, (int)v.kind_ok);
      CHECK(v.home, "%s seed %d: he is back where he was", k.name, (int)seed);
      CHECK(v.clean, "%s seed %d: nothing left behind", k.name, (int)seed);
      CHECK(v.overruns == 0, "%s seed %d: overruns %d", k.name, (int)seed, v.overruns);
      CHECK(v.worst_diff == 0 && v.checks > 20, "%s seed %d: %d checks, worst %d px", k.name, (int)seed, v.checks, v.worst_diff);
      const VisitRun again = runVisit(k.l, k.dog, seed, k.kind, false);
      CHECK(again.hashes == v.hashes && !v.hashes.empty(), "%s seed %d: replays", k.name, (int)seed);
    }
  }
}

static void testExclusions() {
  printf("[6] exclusions\n");
  const uint64_t seed = seedWithVisit(MIDDAY, 0);
  Rig r;
  r.start(seed, makeInputs(MIDDAY));
  // Hold them back with a doorbell reaction or fire and ice running: they don't start.
  bool blocked = true;
  for (int t = 0; t < 25 * 120; ++t) {
    World& w = r.w();
    if (visitorsCanStart(w)) {
      w.react.active = true;
      if (visitorsCanStart(w)) blocked = false;
      w.react.active = false;
      w.fi.on = true;
      if (visitorsCanStart(w)) blocked = false;
      w.fi.on = false;
      break;
    }
    r.step();
  }
  CHECK(blocked, "not with a doorbell reaction or fire and ice");
  // Fire and ice asked for while they are here waits until they go.
  bool on = false, fi_during = false, fi_after = false;
  for (int t = 0; t < 25 * 600; ++t) {
    r.step();
    World& w = r.w();
    if (w.vis.on && !on) {
      on = true;
      r.in.temp_ask[0] = 3;
      appSetInputs(r.app, r.in);
    }
    if (w.vis.on && w.fi.on) fi_during = true;
    if (on && !w.vis.on && w.fi.on) {
      fi_after = true;
      break;
    }
  }
  CHECK(on && !fi_during && fi_after, "fire and ice waits for them (on %d during %d after %d)", (int)on, (int)fi_during, (int)fi_after);
  r.stop();
  // Never in a set piece.
  Rig s;
  Inputs si = makeInputs(MIDDAY);
  si.spine_force = 1 + SF_SUNRISE;
  s.start(seed, si);
  bool any = false;
  for (int t = 0; t < 4000; ++t) {
    s.step();
    any = any || appVisitorsHere(s.app);
  }
  CHECK(!any, "never in the sunrise");
  s.stop();
}

static void testMemory() {
  printf("[7] memory\n");
  CHECK(sizeof(Visitors) <= sizeof(LookoutLand), "Visitors %u in the Lookout's %u", (unsigned)sizeof(Visitors), (unsigned)sizeof(LookoutLand));
  CHECK(appAppBytes() <= 29768u + 2400u && appAppBytes() <= SS_APP_BYTES, "App %u", appAppBytes());
}

int main() {
  testOff();
  testFrequency();
  testKinds();
  testExclusions();
  testMemory();
  printf("test_visitors: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
