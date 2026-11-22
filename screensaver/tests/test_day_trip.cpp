// Tests for the mushrooms after rain as a surprise major (core/scene/day_trip.cpp over trip.cpp, patch.cpp
// and skytrip.cpp; MUSHROOM_PLAN.md section 11.3 with INTEGRATION_PLAN.md and REVIEW_FIXES F2, F3, F5, F22,
// F27; Mike, 2026-11-18). The growth rule and the patch are test_mush_patch.cpp; the sky is test_skytrip.cpp;
// the plan block is test_plan_block.cpp.
//
//   [1] growth in real runs (step C5a): the data's rain for 2 loops, then clear loops: the memory's stage
//       climbs 1, 1, 2 .. 6; the first dry midday loop with a ripe patch plays the mushroom alone; after it
//       the patch is picked over for six more loops, then gone
//   [2] the plan: a ripe patch at midday in clear data weather plans the mushroom alone for seeds 1..40
//       (with a dog out: the fetch and the mushroom), every loop 480 to 660 s; never at night, in the
//       evening, in the morning or in the data's rain
//   [3] whole beats, both sets, the pocket ending, with Lucy, Copper and both: no overrun, the script done
//       with 3 s to spare, nothing of his, the module's or a vision's in the chip's box (x 538..796,
//       y 412..476), strips, drawn tick by tick it equals a full render at checkpoints, the wade's true
//       speed (F5), the stem gone at the pick, the end state
//   [4] the doorbell at five moments: the sky clock and the band counter stand still, frames stay equal
//       to a full redraw, the beat ends clean
//   [5] Lucy lies by him through the sky and is given back; called in mid-sky she leaves and it ends clean
//   [6] the no-show: a rain the plan could not know of leaves the patch ripe
//   [7] two runs draw the same frames; the numbers
//
// With SS_S22 off the mushroom never plans (the patch still grows): only that is checked.
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "mush_patch.h"
#include "scene/day.h"
#include "scene/day_trip.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/memory.h"
#include "scene/reeds.h"
#include "scene/skytrip.h"
#include "scene/spine_family.h"
#include "scene/stage22_switch.h"
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
static const size_t kFrame = (size_t)SS_WIDTH * SS_HEIGHT;

struct Rig {
  void* mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)calloc(kFrame, sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = (mem && fb) ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    free(fb);
    app = nullptr;
  }
};

// A midday the OUTSIDE data vouches for: code 0 clear, 1 rain. `mush`: the patch byte the run starts with.
static Inputs midday(uint8_t mush, int dog = 0, int code = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = true;
  in.weather_code = code;
  in.dog_out = dog;
  in.month = 6;
  in.carry = mush;
  return in;
}
static const uint8_t kRipe = mushPack(MUSH_RIPE, 0, true);
static bool inChipBox(const Rect& r) { return !r.empty() && r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }
static bool inFrame(const Rect& r) { return r.x0 >= 0 && r.x1 <= SS_WIDTH && r.y0 >= 0 && r.y1 <= SS_HEIGHT; }
static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }
static int indexOf(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
}
static uint32_t hashFrame(const Pixel* fb) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < kFrame; i += 7) h = (h ^ fb[i]) * 16777619u;
  return h;
}
static float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
static bool planHas(const App* a, const char* what) {
  char buf[400];
  appLoopPlan(a, buf, sizeof buf);
  return strstr(buf, what) != nullptr;
}

// A seed whose first midday loop plays the mushroom with this set and ending; 0: none found.
static uint64_t findSeed(int set, int pocket, int dog, uint64_t from = 1) {
  for (uint64_t seed = from; seed <= from + 600; ++seed) {
    Rig r;
    if (!r.start(seed, midday(kRipe, dog))) return 0;
    const World* w = appWorld(r.app);
    const int i = indexOf(w->plan, MJ_MUSHROOM);
    const uint8_t v = i >= 0 ? w->plan.major_var[i] : 0;
    const bool hit = i >= 0 && (v & 3) == set && ((v & 4) != 0) == (pocket != 0);
    r.stop();
    if (hit) return seed;
  }
  return 0;
}

// ---- [1] growth ------------------------------------------------------------------------------------------------

static void runLoop(Rig& r) {
  const uint32_t at = appMemoryLoopCount(r.app);
  for (int guard = 0; guard < 30000 && appMemoryLoopCount(r.app) == at; ++guard) appTick(r.app);
}

static void testGrowth() {
  printf("[1] growth after the data's rain\n");
  Rig r;
  Inputs in = midday(0, 0, 1);   // raining, from the data
  if (!r.start(3, in)) {
    CHECK(false, "start");
    return;
  }
  const World* w = appWorld(r.app);
  int stages[20];
  int n = 0;
  bool played = false;
  int played_at = -1, majors_then = 0;
  for (int loop = 0; loop < 18; ++loop) {
    if (loop == 1) {   // the rain stops during the second wet loop: the weather is clear from the third on
      in.weather_code = 0;
      appSetInputs(r.app, in);
    }
    if (indexOf(w->plan, MJ_MUSHROOM) >= 0 && !played) {
      played = true;
      played_at = loop;
      majors_then = w->plan.n_majors;
      CHECK(mushStage(w->land.patch_stage) == MUSH_RIPE, "the major is planned on a ripe patch (stage %d)", mushStage(w->land.patch_stage));
    }
    runLoop(r);
    stages[n++] = mushStage(w->memory.mush);
  }
  printf("    stage after each loop:");
  for (int i = 0; i < n; ++i) printf(" %d", stages[i]);
  printf("\n");
  static const int kGrow[7] = {1, 1, 2, 3, 4, 5, 6};
  for (int i = 0; i < 7; ++i) CHECK(stages[i] == kGrow[i], "loop %d: stage %d, want %d", i, stages[i], kGrow[i]);
  if (ssS22Switch()) {
    // Ripe after loop 6; loop 7 is the first dry midday loop with a ripe patch: he picks. Then six more loops.
    CHECK(played && played_at == 7 && majors_then == 1, "the mushroom alone in loop 7 (played %d at %d with %d majors)", played, played_at, majors_then);
    for (int i = 7; i <= 12; ++i) CHECK(stages[i] == MUSH_PICKED, "loop %d: picked over (stage %d)", i, stages[i]);
    CHECK(stages[13] == 0 && stages[17] == 0, "the bank is bare after six more loops (%d, %d)", stages[13], stages[17]);
    CHECK(appSpineOverruns(r.app) == 0, "no overruns (%d)", appSpineOverruns(r.app));
  } else {
    CHECK(!played, "never planned with the switch off");
    for (int i = 7; i < n; ++i) CHECK(stages[i] == MUSH_RIPE, "loop %d: ripe and waiting (stage %d)", i, stages[i]);
  }
  r.stop();
}

// ---- [2] the plan ------------------------------------------------------------------------------------------------

static void testPlan() {
  printf("[2] the plan\n");
  int alone = 0, with_fetch = 0, bad_len = 0, sets[4] = {0, 0, 0, 0}, pockets = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    for (int dog = 0; dog <= 1; ++dog) {
      Rig r;
      if (!r.start(seed, midday(kRipe, dog))) continue;
      const World* w = appWorld(r.app);
      const int i = indexOf(w->plan, MJ_MUSHROOM);
      if (dog == 0 && i >= 0 && w->plan.n_majors == 1) ++alone;
      if (dog == 1 && i >= 0 && w->plan.n_majors == 2 && indexOf(w->plan, MJ_FETCH) >= 0) ++with_fetch;
      if (i >= 0 && dog == 0) {
        ++sets[w->plan.major_var[i] & 3];
        if (w->plan.major_var[i] & 4) ++pockets;
      }
      const uint32_t s = appLoopTicks(r.app) / SS_TICK_HZ;
      if (s < 480 || s > 660) ++bad_len;
      CHECK(!planHas(r.app, " herd="), "seed %d: no herd in a ripe loop", (int)seed);
      r.stop();
    }
  }
  if (!ssS22Switch()) {
    CHECK(alone == 0 && with_fetch == 0, "never planned with the switch off (%d, %d)", alone, with_fetch);
    return;
  }
  CHECK(alone == 40, "the mushroom alone in %d of 40 ripe loops", alone);
  CHECK(with_fetch == 40, "the fetch and the mushroom in %d of 40 with a dog out", with_fetch);
  CHECK(bad_len == 0, "%d loops outside 480..660 s", bad_len);
  CHECK(sets[0] > 5 && sets[1] > 5 && sets[2] == 0 && sets[3] == 0, "both built sets are drawn (%d, %d, %d, %d)", sets[0], sets[1], sets[2], sets[3]);
  printf("    set 0 in %d, set 1 in %d of 40; the pocket ending in %d\n", sets[0], sets[1], pockets);
  // Never at night, in the evening, in the morning, or in the data's rain; never unless ripe.
  int wrong = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Inputs v[5];
    v[0] = midday(kRipe); v[0].night_mode = true; v[0].mood = 0.f;
    v[1] = midday(kRipe); v[1].spine_force = (uint8_t)(1 + SF_EVENING); v[1].mood = 4.f;
    v[2] = midday(kRipe); v[2].spine_force = (uint8_t)(1 + SF_MORNING); v[2].mood = 1.5f;
    v[3] = midday(kRipe, 0, 1);
    v[4] = midday(mushPack(5, 0, true));
    for (const Inputs& in : v) {
      Rig r;
      if (!r.start(seed, in)) continue;
      if (indexOf(appWorld(r.app)->plan, MJ_MUSHROOM) >= 0) ++wrong;
      r.stop();
    }
  }
  CHECK(wrong == 0, "planned %d times where it must not be", wrong);
}

// ---- [3] one beat, watched ---------------------------------------------------------------------------------------

struct Opt {
  int bell_at = -1;      // beat tick of a doorbell
  float bell_sky = -1.f; // ... or a doorbell at this sky clock
  float dog_in_T = -1.f; // the dog is called in at this many seconds into the sky
  int rain = 0;          // poke this weather kind before the beat
  bool chew = false;     // a stem in his mouth before the beat
  bool quiet = false;
};
struct Out {
  bool ran, end_ok, noshow;
  char why[96];
  int beat_ticks, done_at, overruns, critters;
  long strips_sum, sky_strips;
  int strips_n, strips_max, sky_n, diffs, checks, bad_box, slow_ticks;
  uint32_t hash;
  uint8_t var, mush_after, lifted_after;
  float sky_beat_s, wade_max, wade_min, wade_s;
  int wade_ticks;
  bool chew_before, chew_after;
  int lucy_ticks, lucy_far, lent_end, dog_slot_seen;
  int bell_ticks, bell_moved, trip_left;
  float clock_back;
};

static bool atRest(const World& w, char* why, size_t n) {
  const Hiker& h = w.hiker;
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (h.walking) bad = "walking";
  else if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || fabsf(w.fx.head) > 1.0e-6f || w.fx.lift != 0.f || w.fx.reach != 0.f) bad = "arms or lean still set";
  else if (w.fx.wade) bad = "still wading";
  else if (w.day.kind != DAY_NONE) bad = "the daytime union is not zero";
  else if (h.target != POSE_SIT) bad = "not sitting";
  else if (dist(h.feet, w.spine.camp.sit) > 3.f) bad = "not at camp.sit";
  else if (h.breath_rate != 1.f || h.breath_scale != 1.f) bad = "his breathing is not back to 1 and 1";
  else if (w.dogs.lent != 0) bad = "a dog is still lent";
  else if (skyTripOn(w.land.trip)) bad = "the sky is not natural";
  else if (!inFrame(w.hiker_rect) || w.hiker_rect.empty()) bad = "not in frame";
  if (bad) snprintf(why, n, "%s", bad);
  return bad == nullptr;
}

static Out play(uint64_t seed, Inputs in, const Opt& o, const char* tag) {
  Out e;
  memset(&e, 0, sizeof e);
  e.wade_min = 1.0e9f;
  Rig a;
  if (!a.start(seed, in)) {
    CHECK(false, "%s: start", tag);
    return e;
  }
  Pixel* keep = (Pixel*)malloc(kFrame * sizeof(Pixel));
  World* w = const_cast<World*>(appWorld(a.app));
  const uint32_t len = appLoopTicks(a.app);
  const int pi = indexOf(w->plan, MJ_MUSHROOM);
  CHECK(pi >= 0, "%s: the mushroom is not in the plan", tag);
  if (pi < 0) {
    a.stop();
    free(keep);
    return e;
  }
  e.var = w->plan.major_var[pi];
  if (o.rain) w->weather.kind = (uint8_t)o.rain;
  bool in_beat = false, belled = false, dog_called = false;
  int n_in = 0, bell_left = 0;
  float bell_clock = 0.f, last_clock = 0.f, sky_T0 = -1.f;
  uint8_t bell_band = 0;
  Vec2 prev_feet = w->hiker.feet;
  const int over0 = appSpineOverruns(a.app);
  // Compare the frame as drawn tick by tick with a full redraw of the same app.
  auto fullCheck = [&](const char* what) {
    memcpy(keep, a.fb, kFrame * sizeof(Pixel));
    appInvalidate(a.app);
    appRender(a.app, a.surf);
    ++e.checks;
    if (memcmp(keep, a.fb, kFrame * sizeof(Pixel)) != 0) {
      ++e.diffs;
      int first = -1, cnt = 0;
      for (size_t i = 0; i < kFrame; ++i)
        if (keep[i] != a.fb[i]) {
          if (first < 0) first = (int)i;
          ++cnt;
        }
      if (e.diffs <= 3) printf("    %s: %s at beat tick %d: %d px differ, first at (%d, %d)\n", tag, what, n_in, cnt, first % SS_WIDTH, first / SS_WIDTH);
    }
  };
  for (uint32_t t = 0; t + 1 < len; ++t) {
    const TripState& s = w->day.trip.t;
    const bool sky = w->day.kind == DAY_TRIP && s.sky_on;
    if (in_beat && !belled && ((o.bell_at >= 0 && n_in >= o.bell_at) || (o.bell_sky >= 0.f && sky && s.sky_t >= o.bell_sky))) {
      Event ev;
      ev.type = EV_DOORBELL;
      ev.arg = 0;
      appPostEvent(a.app, ev);
      belled = true;
      bell_left = 86;
      bell_clock = s.sky_t;
      bell_band = s.band;
    }
    if (in_beat && !dog_called && o.dog_in_T >= 0.f && sky && s.T >= o.dog_in_T) {
      in.dog_out = 0;
      appSetInputs(a.app, in);
      dog_called = true;
    }
    if (!in_beat && o.chew && beatIs(a.app, "still") && indexOf(w->plan, MJ_MUSHROOM) >= 0 && !reedsChewing(*w) && !w->major.on) reedsGiveChew(*w, 600.f);
    appTick(a.app);
    const int strips = appRender(a.app, a.surf);
    const bool now = beatIs(a.app, "mushroom");
    if (!now) {
      if (in_beat) {   // the beat's end: checked on the tick after it
        e.end_ok = atRest(*w, e.why, sizeof e.why);
        e.critters = appCritterCount(a.app);
        fullCheck("after the beat");
        break;
      }
      prev_feet = w->hiker.feet;
      continue;
    }
    if (!in_beat) {
      in_beat = true;
      e.ran = true;
      e.noshow = (w->major.var & DAY_NOSHOW) != 0;
      e.chew_before = reedsChewing(*w);
    }
    ++n_in;
    if (scriptDone(*w) && e.done_at == 0) e.done_at = n_in;
    e.strips_sum += strips;
    ++e.strips_n;
    if (strips > e.strips_max) e.strips_max = strips;
    e.hash = (e.hash ^ hashFrame(a.fb)) * 16777619u;
    if (w->day.kind == DAY_TRIP) {
      if (s.sky_on) {
        e.sky_strips += strips;
        ++e.sky_n;
        if (sky_T0 < 0.f) sky_T0 = (float)n_in;
        if (s.slow) ++e.slow_ticks;
        if (s.sky_t < last_clock) e.clock_back += last_clock - s.sky_t;
        last_clock = s.sky_t;
      } else if (sky_T0 >= 0.f && e.sky_beat_s == 0.f) {
        e.sky_beat_s = ((float)n_in - sky_T0) / (float)SS_TICK_HZ;
      }
      if (s.lifted && e.chew_after == false) e.chew_after = reedsChewing(*w);
      // The chip's box: him, the module's things, the visions.
      // (His things get 2 px on the box's left edge: the rightmost pickable stands at x 530 at most and the
      // fringe of its cap ends at x 538. It stood exactly there in the cache before he came; nothing moves.)
      Rect things = tripBounds(s);
      if (things.x1 <= 540) things = Rect{0, 0, 0, 0};
      if (inChipBox(w->hiker_rect) || inChipBox(things)) {
        if (e.bad_box == 0 && !o.quiet)
          printf("    %s: in the chip's box at beat tick %d: him x %d..%d y %d..%d (reach %.2f), his things x %d..%d, crouch x %.0f\n", tag, n_in,
                 w->hiker_rect.x0, w->hiker_rect.x1, w->hiker_rect.y0, w->hiker_rect.y1, (double)w->fx.reach, tripBounds(s).x0, tripBounds(s).x1,
                 (double)w->major.p[1].x);
        ++e.bad_box;
      }
      for (int k = 0; k < 2; ++k) {
        const int c = w->major.c[k];
        if (c >= 0 && w->crit[c].in_use && w->crit[c].tag == CT_MAJOR && inChipBox(w->critter_rect[c])) ++e.bad_box;
      }
      // The wade's true speed (F5).
      if (w->fx.wade && w->hiker.walking && bell_left == 0) {
        const float v = dist(w->hiker.feet, prev_feet) * (float)SS_TICK_HZ;
        ++e.wade_ticks;
        if (v > e.wade_max) e.wade_max = v;
        if (w->hiker.walk_w >= 1.f && v < e.wade_min) e.wade_min = v;
      }
      // Lucy by his legs through the sky.
      if (s.sky_on && s.T >= 45.f && s.sky_t < 98.f && !dog_called) {
        ++e.lucy_ticks;
        if (s.dog == 0 || s.dog_stage < 2 || dist(w->critters[s.dog - 1].feet, w->hiker.feet) > 110.f) ++e.lucy_far;
      }
      if (s.dog) e.dog_slot_seen = s.dog;
      if (bell_left > 0) {
        ++e.bell_ticks;
        if (w->react.active && (s.sky_t != bell_clock || s.band != bell_band)) ++e.bell_moved;
        if (bell_left % 20 == 0) fullCheck("inside the reaction");
        --bell_left;
      }
    }
    prev_feet = w->hiker.feet;
    if (n_in % 211 == 0) fullCheck("in the beat");
  }
  e.beat_ticks = n_in;
  e.overruns = appSpineOverruns(a.app) - over0;
  e.mush_after = w->memory.mush;
  e.lifted_after = w->land.patch_lifted;
  e.lent_end = w->dogs.lent;
  for (int i = 0; i < SKYTRIP_BANDS; ++i) e.trip_left += w->land.trip.tq[i] != 0;
  if (!o.quiet)
    printf("    %s: seed %d var %d, beat %.0f s, script done %.1f s before the end, the sky %.1f s (%d slow ticks), strips %.2f (sky %.2f, max %d), %d of %d full redraws differ\n",
           tag, (int)seed, e.var, (double)n_in / SS_TICK_HZ, (double)(n_in - e.done_at) / SS_TICK_HZ, (double)e.sky_beat_s, e.slow_ticks,
           e.strips_n ? (double)e.strips_sum / e.strips_n : 0., e.sky_n ? (double)e.sky_strips / e.sky_n : 0., e.strips_max, e.diffs, e.checks);
  a.stop();
  free(keep);
  return e;
}

static void checkClean(const Out& e, const char* tag) {
  CHECK(e.ran && !e.noshow, "%s: it played", tag);
  CHECK(e.overruns == 0, "%s: %d overruns", tag, e.overruns);
  CHECK(e.done_at > 0 && e.beat_ticks - e.done_at >= 3 * SS_TICK_HZ, "%s: the script done %.1f s before the end (want 3)", tag,
        (double)(e.beat_ticks - e.done_at) / SS_TICK_HZ);
  CHECK(e.end_ok, "%s: the end state: %s", tag, e.why);
  CHECK(e.critters == 0, "%s: %d critters left", tag, e.critters);
  CHECK(e.trip_left == 0, "%s: %d sky bands still away from natural", tag, e.trip_left);
  CHECK(e.diffs == 0, "%s: %d of %d full redraws differ from the frame drawn tick by tick", tag, e.diffs, e.checks);
  CHECK(e.bad_box == 0, "%s: %d ticks with something in the chip's box", tag, e.bad_box);
  CHECK(mushStage(e.mush_after) == MUSH_PICKED && mushLeft(e.mush_after) == MUSH_PICKED_LOOPS, "%s: the memory says picked over, six loops left (byte %u)", tag, e.mush_after);
  CHECK(e.lifted_after != 0, "%s: the pickables left the cache", tag);
  CHECK(e.strips_n && (double)e.strips_sum / e.strips_n < 10., "%s: strips a frame %.2f", tag, (double)e.strips_sum / e.strips_n);
  CHECK(e.clock_back == 0.f, "%s: the sky clock never runs back", tag);
  CHECK(e.lent_end == 0, "%s: no dog lent at the end", tag);
  CHECK(e.wade_ticks > 100 && e.wade_max <= 12.f * 1.08f && e.wade_min >= 12.f * 0.9f, "%s: the wade's true speed %.1f to %.1f px/s over %d ticks (want 12)", tag,
        (double)e.wade_min, (double)e.wade_max, e.wade_ticks);
}

static void testBeats() {
  printf("[3] whole beats\n");
  for (int set = 0; set < 2; ++set) {
    for (int pocket = 0; pocket < 2; ++pocket) {
      uint64_t from = 1;
      for (int k = 0; k < 3; ++k) {   // three seeds each
        const uint64_t seed = findSeed(set, pocket, 0, from);
        CHECK(seed != 0, "a seed for set %d pocket %d", set, pocket);
        if (!seed) break;
        from = seed + 1;
        char tag[48];
        snprintf(tag, sizeof tag, "set %d%s", set, pocket ? " pocket" : "");
        Opt o;
        o.chew = k == 0;
        const Out e = play(seed, midday(kRipe), o, tag);
        checkClean(e, tag);
        if (set == 1) CHECK(e.slow_ticks == 0 && fabsf(e.sky_beat_s - (float)SKYTRIP_SECONDS) < 0.2f, "%s: the sky is 104 s (%.1f)", tag, (double)e.sky_beat_s);
        if (set == 0) CHECK(e.slow_ticks > 500 && e.sky_beat_s > 115.f && e.sky_beat_s < 140.f, "%s: the sky runs slow under the tall visions (%.1f s, %d ticks)", tag, (double)e.sky_beat_s, e.slow_ticks);
        if (o.chew) CHECK(e.chew_before && !e.chew_after, "%s: the stem is in his mouth before (%d) and gone at the pick (%d)", tag, e.chew_before, e.chew_after);
      }
    }
  }
  for (int dog = 1; dog <= 3; ++dog) {
    const uint64_t seed = findSeed(0, 0, dog);
    CHECK(seed != 0, "a seed with dog %d", dog);
    if (!seed) continue;
    char tag[48];
    snprintf(tag, sizeof tag, "dog %d", dog);
    const Out e = play(seed, midday(kRipe, dog), Opt{}, tag);
    checkClean(e, tag);
    CHECK(e.dog_slot_seen == CRITTER_STORY + (dog == 2 ? HD_COPPER : HD_LUCY) + 1, "%s: the right dog came (slot %d)", tag, e.dog_slot_seen - 1);
    CHECK(e.lucy_ticks > 1000 && e.lucy_far == 0, "%s: lying by him through the sky (%d of %d ticks not)", tag, e.lucy_far, e.lucy_ticks);
  }
}

static void testBell() {
  printf("[4] the doorbell\n");
  const uint64_t seed = findSeed(0, 0, 0);
  if (!seed) return;
  Opt o[5];
  o[0].bell_at = 14 * SS_TICK_HZ;   // wading
  o[1].bell_at = 36 * SS_TICK_HZ;   // sniffing, the bunch in his hand
  o[2].bell_at = 58 * SS_TICK_HZ;   // the hand
  o[3].bell_sky = 40.f;             // mid-sky, the chipmunk there
  o[4].bell_sky = 103.5f;           // as it ends
  const char* name[5] = {"bell wading", "bell sniffing", "bell at his hand", "bell mid-sky", "bell at the sky's end"};
  for (int i = 0; i < 5; ++i) {
    const Out e = play(seed, midday(kRipe), o[i], name[i]);
    CHECK(e.ran && e.overruns == 0 && e.end_ok && e.critters == 0 && e.trip_left == 0, "%s: ends clean (%s)", name[i], e.why);
    CHECK(e.bell_ticks >= 80, "%s: the reaction ran (%d ticks)", name[i], e.bell_ticks);
    CHECK(e.bell_moved == 0, "%s: the sky clock or the band moved on %d ticks of the reaction", name[i], e.bell_moved);
    CHECK(e.diffs == 0, "%s: %d of %d full redraws differ", name[i], e.diffs, e.checks);
    CHECK(e.done_at > 0 && e.beat_ticks - e.done_at >= 3 * SS_TICK_HZ, "%s: script done %.1f s before the end", name[i], (double)(e.beat_ticks - e.done_at) / SS_TICK_HZ);
  }
}

static void testDogLeaves() {
  printf("[5] the dog called in mid-sky\n");
  const uint64_t seed = findSeed(0, 0, 1);
  if (!seed) return;
  Opt o;
  o.dog_in_T = 50.f;
  const Out e = play(seed, midday(kRipe, 1), o, "Lucy called in");
  CHECK(e.ran && e.overruns == 0 && e.end_ok && e.critters == 0 && e.lent_end == 0, "ends clean (%s)", e.why);
  CHECK(e.dog_slot_seen != 0, "she had come");
  CHECK(e.diffs == 0, "%d of %d full redraws differ", e.diffs, e.checks);
}

static void testNoShow() {
  printf("[6] the no-show\n");
  const uint64_t seed = findSeed(1, 0, 0);
  if (!seed) return;
  Opt o;
  o.rain = WX_RAIN;
  const Out e = play(seed, midday(kRipe), o, "rain after all");
  CHECK(e.ran && e.noshow, "a rain the plan could not know of is a no-show");
  CHECK(e.overruns == 0 && e.critters == 0, "it ends clean (%d overruns)", e.overruns);
  CHECK(mushStage(e.mush_after) == MUSH_RIPE && e.lifted_after == 0, "the patch is still ripe and whole (byte %u)", e.mush_after);
}

static void testDeterminism() {
  printf("[7] determinism\n");
  for (int set = 0; set < 2; ++set) {
    const uint64_t seed = findSeed(set, 0, 0);
    if (!seed) continue;
    Opt o;
    o.quiet = true;
    const Out a = play(seed, midday(kRipe), o, "run a"), b = play(seed, midday(kRipe), o, "run b");
    CHECK(a.hash == b.hash && a.beat_ticks == b.beat_ticks, "set %d: two runs draw the same frames (%08x, %08x)", set, a.hash, b.hash);
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  printf("test_day_trip (SS_S22 %d)\n", ssS22Switch());
  printf("  sizeof(App) %u, DayTrip %u of DayBike %u, TripState %u\n", (unsigned)appAppBytes(), (unsigned)sizeof(DayTrip), (unsigned)sizeof(DayBike),
         (unsigned)sizeof(TripState));
  CHECK(appAppBytes() <= 29992u, "sizeof(App) %u is over the 29,992 wire", (unsigned)appAppBytes());
  CHECK(tripPlanVar(0) == 4 && (tripPlanVar(1u << 13) & 3) == 1 && (tripPlanVar(2u << 13) & 3) == 0 && tripPlanVar(32u) == 0, "the variant byte");
  CHECK(tripSeconds(0) >= 201 && tripSeconds(1) >= 201 && tripSeconds(0) > tripSeconds(1), "the seconds (%d, %d)", tripSeconds(0), tripSeconds(1));
  testGrowth();
  testPlan();
  if (ssS22Switch()) {
    testBeats();
    testBell();
    testDogLeaves();
    testNoShow();
    testDeterminism();
  }
  printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
