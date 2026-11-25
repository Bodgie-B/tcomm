// Tests for the frog he licks (frog_lick, MJ_FROG_LICK; core/scene/day_trip.cpp on the mushroom's DAY_TRIP and its
// sky; Mike, 2026-11-25: "the hiker goes over to the frog, picks it up, licks it, then sets the frog down, shakes his
// head due to the terrible taste, then goes to lay down. in a few seconds the psychedelic sky routine starts borrowed
// from the mushroom episode and runs."). Modelled on test_day_trip.cpp.
//
//   [1] the plan: midday only, by day, never in the data's rain, never in a loop the ripe patch plays (never two trips
//       in a loop), every loop that plans it 480 to 660 s; with SS_S22 off never
//   [2] whole beats, every tick drawn: the frog rides his hand while he holds it and draws in front of him, it is set
//       down on the ground line, nothing in the chip's box or the quiet corner, he stays in frame, strips under 10,
//       drawn tick by tick equals a full redraw (before the pick-up, held, the lick, lying, mid-sky, and every 211
//       ticks), no overrun, the script done 3 s before the end, no critter left, the sky 104 s and every band back
//   [3] the doorbell while he holds it out, during the lick, lying, mid-sky: a hop and a look with the frog in his
//       hand (no wave), the frog stays on his hand, the sky clock stands still, it ends clean
//   [4] with Lucy out: she comes and lies by him through the sky and is given back
//   [5] the no-show: a rain the plan could not know of
//   [6] two runs draw the same frames
//   [7] the ask through the carry word (0x0800, day_trip.h FROG_ASK): the next midday loop by day plays it, once
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "mush_patch.h"
#include "scene/day.h"
#include "scene/day_trip.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/script.h"
#include "scene/skytrip.h"
#include "scene/spine_family.h"
#include "scene/stage22_switch.h"
#include "scene/trip.h"
#include "scene/weather.h"
#include "scene/world.h"

#include <chrono>
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

// A midday the OUTSIDE data vouches for: code 0 clear, 1 rain.
static Inputs midday(int dog = 0, int code = 0, uint16_t carry = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = true;
  in.weather_code = code;
  in.dog_out = dog;
  in.month = 6;
  in.carry = carry;
  return in;
}
// (The frog's stages, as day_trip.cpp numbers them.)
enum { FR_IN = 0, FR_SIT, FR_HELD, FR_DOWN, FR_OFF, FR_GONE };

static bool inChipBox(const Rect& r) { return !r.empty() && r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }
static bool inCorner(const Rect& r) { return !r.empty() && r.x1 > LS_QUIET_X0 && r.y1 > LS_QUIET_Y0; }
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

// The n-th seed (from `from`) whose first midday loop plans frog_lick; 0: none in 800.
static uint64_t findSeed(int dog = 0, uint64_t from = 1) {
  for (uint64_t seed = from; seed <= from + 800; ++seed) {
    Rig r;
    if (!r.start(seed, midday(dog))) return 0;
    const bool hit = indexOf(appWorld(r.app)->plan, MJ_FROG_LICK) >= 0;
    r.stop();
    if (hit) return seed;
  }
  return 0;
}

// ---- [1] the plan ----------------------------------------------------------------------------------------------------

static void testPlan() {
  printf("[1] the plan\n");
  int planned = 0, alone = 0, bad_len = 0, loops = 0, with_mush = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    Rig r;
    if (!r.start(seed, midday())) continue;
    const World* w = appWorld(r.app);
    for (int loop = 0; loop < 5; ++loop) {   // memory kept: the ring and the recent sets have their say
      ++loops;
      const int i = indexOf(w->plan, MJ_FROG_LICK);
      if (i >= 0) {
        ++planned;
        if (w->plan.n_majors == 1) ++alone;
        if (indexOf(w->plan, MJ_MUSHROOM) >= 0) ++with_mush;
        const uint32_t s = appLoopTicks(r.app) / SS_TICK_HZ;
        if (s < 480 || s > 660) ++bad_len;
        CHECK(w->plan.major_var[i] == 0, "seed %d loop %d: the variant byte %u", (int)seed, loop, w->plan.major_var[i]);
      }
      const uint32_t at = appMemoryLoopCount(r.app);
      for (int guard = 0; guard < 30000 && appMemoryLoopCount(r.app) == at; ++guard) appTick(r.app);
    }
    r.stop();
  }
  if (!ssS22Switch()) {
    CHECK(planned == 0, "never planned with the switch off (%d)", planned);
    return;
  }
  printf("    planned in %d of %d midday loops (%.1f %%), alone in %d\n", planned, loops, 100.0 * planned / loops, alone);
  CHECK(planned >= 10 && planned <= 80, "planned in %d of %d loops (PROPOSED weight 1.0: about one in ten to one in five)", planned, loops);
  CHECK(bad_len == 0, "%d loops with the frog outside 480..660 s", bad_len);
  CHECK(with_mush == 0, "with the mushroom in %d loops", with_mush);
  // Never at night, in the evening, in the morning, in the data's rain, or with a ripe patch.
  int wrong = 0;
  const uint8_t ripe = mushPack(MUSH_RIPE, 0, true);
  for (uint64_t seed = 1; seed <= 120; ++seed) {
    Inputs v[5];
    v[0] = midday(); v[0].night_mode = true; v[0].mood = 0.f;
    v[1] = midday(); v[1].spine_force = (uint8_t)(1 + SF_EVENING); v[1].mood = 4.f;
    v[2] = midday(); v[2].spine_force = (uint8_t)(1 + SF_MORNING); v[2].mood = 1.5f;
    v[3] = midday(0, 1);
    v[4] = midday(0, 0, ripe);
    for (const Inputs& in : v) {
      Rig r;
      if (!r.start(seed, in)) continue;
      if (indexOf(appWorld(r.app)->plan, MJ_FROG_LICK) >= 0) ++wrong;
      r.stop();
    }
  }
  CHECK(wrong == 0, "planned %d times where it must not be", wrong);
}

// ---- [2] one beat, watched -----------------------------------------------------------------------------------------

enum Moment { M_NONE = 0, M_HELD, M_LICK, M_LYING, M_SKY };
struct Opt {
  int bell = M_NONE;     // a doorbell at this moment
  int rain = 0;          // poke this weather kind before the beat
  bool quiet = false;
};
struct Out {
  bool ran, end_ok, noshow;
  char why[96];
  int beat_ticks, done_at, overruns, critters, critters_loop_end;
  long strips_sum, sky_strips;
  int strips_n, strips_max, sky_n, diffs, checks, bad_box, bad_frame, bad_corner;
  uint32_t hash;
  float sky_beat_s, clock_back;
  int held_ticks, held_off, held_behind, set_down, set_down_off, frog_seen;
  int tongue_ticks, lie_before_sky, trip_left;
  int bell_ticks, bell_moved, bell_hands_full, bell_waved, bell_held_ticks;
  int lucy_ticks, lucy_far, lent_end, dog_slot_seen;
  double ms_sum;
};

static bool atRest(const World& w, char* why, size_t n) {
  const Hiker& h = w.hiker;
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (h.walking) bad = "walking";
  else if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || fabsf(w.fx.head) > 1.0e-6f || w.fx.lift != 0.f || w.fx.reach != 0.f) bad = "arms or lean still set";
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
  Rig a;
  if (!a.start(seed, in)) {
    CHECK(false, "%s: start", tag);
    return e;
  }
  Pixel* keep = (Pixel*)malloc(kFrame * sizeof(Pixel));
  World* w = const_cast<World*>(appWorld(a.app));
  const uint32_t len = appLoopTicks(a.app);
  CHECK(indexOf(w->plan, MJ_FROG_LICK) >= 0, "%s: frog_lick is not in the plan", tag);
  bool in_beat = false, belled = false, after = false;
  int n_in = 0, bell_left = 0;
  float bell_clock = 0.f, last_clock = 0.f, sky_T0 = -1.f;
  uint8_t bell_band = 0;
  bool seen[5] = {false, false, false, false, false};
  const int over0 = appSpineOverruns(a.app);
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
    const bool trip = in_beat && w->day.kind == DAY_TRIP;
    // The moments, each once: a full redraw there, and the doorbell when asked for.
    int moment = M_NONE;
    if (trip) {
      if (!seen[M_HELD] && s.held && s.grip_k >= 1.f && w->fx.arm_w[0] > 0.9f) moment = M_HELD;
      else if (!seen[M_LICK] && s.tongue >= 4) moment = M_LICK;
      else if (!seen[M_LYING] && w->hiker.target == POSE_LIE && !w->hiker.blending && !s.sky_on && (w->major.cue & 2) == 0) moment = M_LYING;
      else if (!seen[M_SKY] && s.sky_on && s.sky_t >= 52.f) moment = M_SKY;
      if (moment) seen[moment] = true;
    }
    if (moment && o.bell == moment && !belled) {
      Event ev;
      ev.type = EV_DOORBELL;
      ev.arg = 0;
      appPostEvent(a.app, ev);
      belled = true;
      bell_left = 86;
      bell_clock = s.sky_t;
      bell_band = s.band;
    }
    const auto t0 = std::chrono::steady_clock::now();
    appTick(a.app);
    const int strips = appRender(a.app, a.surf);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const bool now = beatIs(a.app, "frog_lick");
    if (after) continue;
    if (!now) {
      if (in_beat) {   // the beat's end: checked on the tick after it
        e.end_ok = atRest(*w, e.why, sizeof e.why);
        e.critters = appCritterCount(a.app);
        fullCheck("after the beat");
        after = true;
      }
      continue;
    }
    if (!in_beat) {
      in_beat = true;
      e.ran = true;
      e.noshow = (w->major.var & DAY_NOSHOW) != 0;
    }
    ++n_in;
    e.ms_sum += ms;
    if (scriptDone(*w) && e.done_at == 0) e.done_at = n_in;
    e.strips_sum += strips;
    ++e.strips_n;
    if (strips > e.strips_max) e.strips_max = strips;
    e.hash = (e.hash ^ hashFrame(a.fb)) * 16777619u;
    // Him: in frame, out of the chip's box and the quiet corner.
    if (!inFrame(w->hiker_rect) || w->hiker_rect.empty()) ++e.bad_frame;
    if (inChipBox(w->hiker_rect)) ++e.bad_box;
    if (inCorner(w->hiker_rect)) ++e.bad_corner;
    const int c = w->major.c[0];
    const bool frog = c >= 0 && w->crit[c].in_use && w->crit[c].tag == CT_MAJOR;
    if (frog) {
      ++e.frog_seen;
      if (inChipBox(w->critter_rect[c])) ++e.bad_box;
      if (inCorner(w->critter_rect[c])) ++e.bad_corner;
    }
    if (w->day.kind == DAY_TRIP) {
      if (inChipBox(tripBounds(s))) ++e.bad_box;
      if (s.tongue) ++e.tongue_ticks;
      // On his palm: its feet at his near hand (1 ahead, 3 below) once it has settled, unless he is setting it down.
      if (frog && s.held && s.grip_k >= 1.f && s.lower <= 0.f) {
        ++e.held_ticks;
        const Vec2 hand = hikerHandPos(w->hiker, 0);
        const Vec2 want{hand.x + (float)w->hiker.facing, hand.y + 3.f};
        const Vec2 f = w->critters[c].feet;
        const float gy = scriptGroundAt(*w, f.x, w->hiker.feet.y);
        if (dist(f, want) > 1.0f && fabsf(f.y - gy) > 0.01f) {
          if (e.held_off == 0 && !o.quiet)
            printf("    %s: the frog off his hand at beat tick %d: feet (%.1f, %.1f), hand (%.1f, %.1f)\n", tag, n_in, (double)f.x, (double)f.y, (double)hand.x, (double)hand.y);
          ++e.held_off;
        }
        if (w->crit[c].sort_y <= (int16_t)floorf(w->hiker.feet.y)) ++e.held_behind;
        if (w->react.active) ++e.bell_held_ticks;
      }
      // Set down: the first tick it is the director's again, its feet on the ground line.
      if (frog && s.frog_st == FR_DOWN && e.set_down == 0) {
        e.set_down = n_in;
        const Vec2 f = w->critters[c].feet;
        if (fabsf(f.y - scriptGroundAt(*w, f.x, w->hiker.feet.y)) > 0.01f || w->crit[c].sort_y != 0) ++e.set_down_off;
      }
      if (w->hiker.target == POSE_LIE && !s.sky_on && (w->major.cue & 2) == 0 && !w->hiker.blending) ++e.lie_before_sky;
      if (s.sky_on) {
        e.sky_strips += strips;
        ++e.sky_n;
        if (sky_T0 < 0.f) sky_T0 = (float)n_in;
        if (s.sky_t < last_clock) e.clock_back += last_clock - s.sky_t;
        last_clock = s.sky_t;
        if (s.T >= 45.f && s.sky_t < 98.f) {
          ++e.lucy_ticks;
          if (s.dog == 0 || s.dog_stage < 2 || dist(w->critters[s.dog - 1].feet, w->hiker.feet) > 110.f) ++e.lucy_far;
        }
      } else if (sky_T0 >= 0.f && e.sky_beat_s == 0.f) {
        e.sky_beat_s = ((float)n_in - sky_T0) / (float)SS_TICK_HZ;
      }
      if (s.dog) e.dog_slot_seen = s.dog;
    }
    if (bell_left > 0) {
      ++e.bell_ticks;
      if (w->react.active) {
        if (w->react.hands_full) ++e.bell_hands_full;
        if (w->hiker.pose.shoulder[0] > 2.0f) ++e.bell_waved;
        if (w->day.kind == DAY_TRIP && (s.sky_t != bell_clock || s.band != bell_band)) ++e.bell_moved;
      }
      if (bell_left % 20 == 0) fullCheck("inside the reaction");
      --bell_left;
    }
    if (moment) {
      static const char* const kName[5] = {"", "held", "the lick", "lying", "mid-sky"};
      fullCheck(kName[moment]);
    } else if (n_in % 211 == 0) {
      fullCheck("in the beat");
    }
  }
  e.critters_loop_end = appCritterCount(a.app);
  e.beat_ticks = n_in;
  e.overruns = appSpineOverruns(a.app) - over0;
  e.lent_end = w->dogs.lent;
  for (int i = 0; i < SKYTRIP_BANDS; ++i) e.trip_left += w->land.trip.tq[i] != 0;
  if (!o.quiet)
    printf("    %s: seed %d, beat %.0f s, script done %.1f s before the end, the sky %.1f s, strips %.2f (sky %.2f, max %d), %.3f ms a frame, %d of %d full redraws differ\n",
           tag, (int)seed, (double)n_in / SS_TICK_HZ, (double)(n_in - e.done_at) / SS_TICK_HZ, (double)e.sky_beat_s,
           e.strips_n ? (double)e.strips_sum / e.strips_n : 0., e.sky_n ? (double)e.sky_strips / e.sky_n : 0., e.strips_max,
           e.strips_n ? e.ms_sum / e.strips_n : 0., e.diffs, e.checks);
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
  CHECK(e.critters == 0 && e.critters_loop_end == 0, "%s: %d critters left after the beat, %d at the loop's end", tag, e.critters, e.critters_loop_end);
  CHECK(e.trip_left == 0, "%s: %d sky bands still away from natural", tag, e.trip_left);
  CHECK(e.diffs == 0, "%s: %d of %d full redraws differ from the frame drawn tick by tick", tag, e.diffs, e.checks);
  CHECK(e.checks >= 20, "%s: only %d full redraws compared", tag, e.checks);
  CHECK(e.bad_box == 0 && e.bad_corner == 0, "%s: %d ticks in the chip's box, %d in the quiet corner", tag, e.bad_box, e.bad_corner);
  CHECK(e.bad_frame == 0, "%s: out of frame on %d ticks", tag, e.bad_frame);
  CHECK(e.strips_n && (double)e.strips_sum / e.strips_n < 10., "%s: strips a frame %.2f", tag, (double)e.strips_sum / e.strips_n);
  CHECK(e.clock_back == 0.f, "%s: the sky clock never runs back", tag);
  CHECK(fabsf(e.sky_beat_s - (float)SKYTRIP_SECONDS) < 0.2f, "%s: the sky is 104 s (%.1f)", tag, (double)e.sky_beat_s);
  CHECK(e.lent_end == 0, "%s: no dog lent at the end", tag);
  CHECK(e.frog_seen > 0 && e.held_ticks > 8 * SS_TICK_HZ, "%s: the frog was in his hand for %.1f s", tag, (double)e.held_ticks / SS_TICK_HZ);
  CHECK(e.held_off == 0, "%s: the frog off his hand on %d of %d ticks", tag, e.held_off, e.held_ticks);
  CHECK(e.held_behind == 0, "%s: the frog behind him on %d ticks", tag, e.held_behind);
  CHECK(e.set_down > 0 && e.set_down_off == 0, "%s: set down on the ground line (at %d, off %d)", tag, e.set_down, e.set_down_off);
  CHECK(e.tongue_ticks >= 10, "%s: the tongue showed on %d ticks", tag, e.tongue_ticks);
  CHECK(e.lie_before_sky >= 3 * SS_TICK_HZ, "%s: lying %.1f s before the sky (want a few seconds)", tag, (double)e.lie_before_sky / SS_TICK_HZ);
}

static void testBeats(uint64_t seeds[4]) {
  printf("[2] whole beats\n");
  uint64_t from = 1;
  long strips = 0, sky = 0;
  int n = 0, nsky = 0, worst = 0;
  double ms = 0.;
  for (int k = 0; k < 4; ++k) {
    seeds[k] = findSeed(0, from);
    CHECK(seeds[k] != 0, "a seed (%d)", k);
    if (!seeds[k]) return;
    from = seeds[k] + 1;
    char tag[32];
    snprintf(tag, sizeof tag, "beat %d", k);
    const Out e = play(seeds[k], midday(), Opt{}, tag);
    checkClean(e, tag);
    strips += e.strips_sum;
    n += e.strips_n;
    sky += e.sky_strips;
    nsky += e.sky_n;
    ms += e.ms_sum;
    if (e.strips_max > worst) worst = e.strips_max;
  }
  printf("    over the four: strips %.2f a frame (sky %.2f, worst %d), %.3f ms a frame on this PC\n", n ? (double)strips / n : 0.,
         nsky ? (double)sky / nsky : 0., worst, n ? ms / n : 0.);
}

static void testBell(uint64_t seed) {
  printf("[3] the doorbell\n");
  const int kAt[4] = {M_HELD, M_LICK, M_LYING, M_SKY};
  const char* name[4] = {"bell holding it", "bell at the lick", "bell lying", "bell mid-sky"};
  for (int i = 0; i < 4; ++i) {
    Opt o;
    o.bell = kAt[i];
    const Out e = play(seed, midday(), o, name[i]);
    CHECK(e.ran && e.overruns == 0 && e.end_ok && e.critters == 0 && e.trip_left == 0, "%s: ends clean (%s)", name[i], e.why);
    CHECK(e.bell_ticks >= 80, "%s: the reaction ran (%d ticks)", name[i], e.bell_ticks);
    CHECK(e.bell_moved == 0, "%s: the sky clock or the band moved on %d ticks of the reaction", name[i], e.bell_moved);
    CHECK(e.diffs == 0, "%s: %d of %d full redraws differ", name[i], e.diffs, e.checks);
    CHECK(e.held_off == 0, "%s: the frog off his hand on %d ticks", name[i], e.held_off);
    CHECK(e.done_at > 0 && e.beat_ticks - e.done_at >= 3 * SS_TICK_HZ, "%s: script done %.1f s before the end", name[i], (double)(e.beat_ticks - e.done_at) / SS_TICK_HZ);
    if (kAt[i] == M_HELD || kAt[i] == M_LICK) {
      CHECK(e.bell_hands_full >= 80 && e.bell_waved == 0, "%s: the frog in his hand: no wave (hands full on %d ticks, waved on %d)", name[i], e.bell_hands_full, e.bell_waved);
      CHECK(e.bell_held_ticks >= 80, "%s: the frog stayed on his hand through the freeze (%d ticks)", name[i], e.bell_held_ticks);
    }
  }
}

static void testDog() {
  printf("[4] with Lucy out\n");
  const uint64_t seed = findSeed(1);
  CHECK(seed != 0, "a seed with a dog out");
  if (!seed) return;
  const Out e = play(seed, midday(1), Opt{}, "Lucy");
  checkClean(e, "Lucy");
  CHECK(e.dog_slot_seen == CRITTER_STORY + HD_LUCY + 1, "Lucy came (slot %d)", e.dog_slot_seen - 1);
  CHECK(e.lucy_ticks > 1000 && e.lucy_far == 0, "lying by him through the sky (%d of %d ticks not)", e.lucy_far, e.lucy_ticks);
}

static void testNoShow(uint64_t seed) {
  printf("[5] the no-show\n");
  // Poke the rain at the loop's start: the begin hook sees it.
  Rig r;
  if (!r.start(seed, midday())) return;
  World* w = const_cast<World*>(appWorld(r.app));
  w->weather.kind = WX_RAIN;
  const uint32_t len = appLoopTicks(r.app);
  bool ran = false, noshow = false, frog = false;
  const int over0 = appSpineOverruns(r.app);
  for (uint32_t t = 0; t + 1 < len; ++t) {
    w->weather.kind = WX_RAIN;
    appTick(r.app);
    if (beatIs(r.app, "frog_lick")) {
      if (!ran) noshow = (w->major.var & DAY_NOSHOW) != 0;
      ran = true;
      if (w->major.c[0] >= 0) frog = true;
    }
  }
  CHECK(ran && noshow, "a rain the plan could not know of is a no-show (ran %d, no-show %d)", ran, noshow);
  CHECK(!frog, "no frog came");
  CHECK(appSpineOverruns(r.app) - over0 == 0 && appCritterCount(r.app) == 0, "it ends clean");
  r.stop();
}

static void testDeterminism(uint64_t seed) {
  printf("[6] determinism\n");
  Opt o;
  o.quiet = true;
  const Out a = play(seed, midday(), o, "run a"), b = play(seed, midday(), o, "run b");
  CHECK(a.hash == b.hash && a.beat_ticks == b.beat_ticks && a.hash != 0, "two runs draw the same frames (%08x, %08x)", a.hash, b.hash);
}

static void testAsk() {
  printf("[7] the ask (carry 0x0800)\n");
  const uint16_t ask = (uint16_t)(FROG_ASK << 8);
  int first = 0, second = 0, night = 0, kept = 0, cleared = 0;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    {
      Rig r;
      if (!r.start(seed, midday(0, 0, ask))) continue;
      if (indexOf(appWorld(r.app)->plan, MJ_FROG_LICK) >= 0) ++first;
      if (seed <= 3) {   // once it has begun the word no longer asks
        const uint32_t at = appMemoryLoopCount(r.app);
        for (int guard = 0; guard < 30000 && appMemoryLoopCount(r.app) == at; ++guard) appTick(r.app);
        if ((appCarry(r.app) & ask) == 0) ++cleared;
      }
      r.stop();
    }
    {
      Rig r;
      if (!r.start(seed, midday(1, 0, ask))) continue;
      if (indexOf(appWorld(r.app)->plan, MJ_FROG_LICK) >= 0 && indexOf(appWorld(r.app)->plan, MJ_FETCH) >= 0) ++second;
      r.stop();
    }
    {
      Inputs in = midday(0, 0, ask);
      in.night_mode = true;
      in.mood = 0.f;
      Rig r;
      if (!r.start(seed, in)) continue;
      if (indexOf(appWorld(r.app)->plan, MJ_FROG_LICK) >= 0) ++night;
      if ((appCarry(r.app) & ask) != 0) ++kept;
      r.stop();
    }
  }
  if (!ssS22Switch()) {
    CHECK(first == 0 && second == 0, "the switch off: never (%d, %d)", first, second);
    return;
  }
  CHECK(first == 40, "asked for: planned in %d of 40 midday loops", first);
  CHECK(second == 40, "asked for with a dog out: the fetch and the frog in %d of 40", second);
  CHECK(cleared == 3, "the ask is spent once it has played (%d of 3)", cleared);
  CHECK(night == 0 && kept == 40, "not at night (%d), and the ask waits (%d of 40)", night, kept);
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  printf("test_frog_lick (SS_S22 %d)\n", ssS22Switch());
  printf("  sizeof(App) %u, DayTrip %u of DayBike %u, TripState %u\n", (unsigned)appAppBytes(), (unsigned)sizeof(DayTrip), (unsigned)sizeof(DayBike),
         (unsigned)sizeof(TripState));
  CHECK(appAppBytes() <= 29992u, "sizeof(App) %u is over the 29,992 wire", (unsigned)appAppBytes());
  CHECK(sizeof(DayTrip) <= sizeof(DayBike), "DayTrip fits the day union");
  CHECK(MJ_FROG_LICK == 28 && strcmp(majorName(MJ_FROG_LICK), "frog_lick") == 0, "the id and the name");
  CHECK(frogPlanVar(0) == 0 && frogPlanVar(0xFFFFFFFFu) == 0, "one variant");
  testPlan();
  if (ssS22Switch()) {
    uint64_t seeds[4] = {0, 0, 0, 0};
    testBeats(seeds);
    if (seeds[0]) {
      testBell(seeds[0]);
      testNoShow(seeds[0]);
      testDeterminism(seeds[1] ? seeds[1] : seeds[0]);
    }
    testDog();
  }
  testAsk();
  printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
