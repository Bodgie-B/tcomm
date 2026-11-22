// Tests for the deer visit as a midday beat (core/scene/day_herd.cpp over herd.cpp; DEER_PLAN.md section 9
// with INTEGRATION_PLAN.md 4.1, 4.2, section 8 rows 7 and 8, and REVIEW_FIXES F12, F21, F25; Mike,
// 2026-11-18). The pure machine is test_herd.cpp; the plan block is test_plan_block.cpp.
//
//   [1] the plan: the deer in 25 to 35 % of midday loops that pass the gates, none with a dog out or in the
//       data's rain or storm, never in two midday loops running, every variant and size, every herd loop
//       480 to 660 s with at most two majors and no deer_stick, plink or longshot; the " herd=N" token only
//       in a herd loop; the beat's seconds are in the loop's length
//   [2] each variant and size, drawn every tick: he is away for the whole beat; 4, 5 or 6 deer came and no
//       other critter; every deer rectangle in the frame's rows, out of the chip's box (x 538..796,
//       y 412..476) and, near the camp pine, with its feet at y 343 or lower; the settled picture keeps every
//       pair 2 px apart; the leavers keep 2 px apart all the way out (F12); the tent's wobble is 0 at the end;
//       the camp is empty for the beat's last seconds; no overrun; strips; drawn tick by tick it equals a
//       full render; then the return: he finds the flap and the prints, zips the flap (returns a and c), the
//       prints fade and the union is free; the script ends with time to spare
//   [3] the doe and the cattail (4.1) and the fawn in the reeds (4.2)
//   [4] real alarms: the doorbell at 30, 50 and 70 s (the second look only after the first), a house dog let
//       out at 40 s (they go right, never touch the dog, the dog chases), a visitor at the panel at 60 s
//   [5] the no-show: a dog wanted out during the build, no deer at all (F21); wind and rain loops run clean
//   [6] fire and ice may start over the prints (F25); two runs draw the same frames; the numbers
//
// With SS_S22 off the deer never plan: only that is checked.
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/day.h"
#include "scene/day_herd.h"
#include "scene/fireice.h"
#include "scene/herd.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/memory.h"
#include "scene/reeds.h"
#include "scene/script.h"
#include "scene/spine.h"
#include "scene/spine_family.h"
#include "scene/stage22_switch.h"
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
  }
};

// A dry midday the OUTSIDE data vouches for (code 0), in August (cattails with heads, reeds up).
static Inputs midday(int dog = 0, int code = 0, bool outside = true) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = outside;
  in.weather_code = code;
  in.dog_out = dog;
  in.month = 8;
  return in;
}
static bool inChipBox(const Rect& r) { return !r.empty() && r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }
static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }
static uint32_t hashFrame(const Pixel* fb) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < SS_WIDTH * SS_HEIGHT; i += 7) h = (h ^ fb[i]) * 16777619u;
  return h;
}
// The gap between two rectangles (the larger of the x and y gaps; negative when they overlap).
static int gap(const Rect& a, const Rect& b) {
  const int gx = a.x0 > b.x1 ? a.x0 - b.x1 : (b.x0 > a.x1 ? b.x0 - a.x1 : -1);
  const int gy = a.y0 > b.y1 ? a.y0 - b.y1 : (b.y0 > a.y1 ? b.y0 - a.y1 : -1);
  return gx > gy ? gx : gy;
}

// A seed whose first midday loop is this herd byte, with this return variant (-1 any), and with (1), without
// (0) or regardless of (-1) the fireside clump and the upstream clump in the fawn's reach. 0: none found.
static uint64_t findSeed(int herd, int ret, int fireside, int upstream, uint64_t from = 1) {
  const Inputs in = midday();
  for (uint64_t seed = from; seed <= from + 4000; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return 0;
    const World* w = appWorld(r.app);
    bool hit = w->plan.herd == herd && (ret < 0 || w->plan.fvar[0] == ret);
    if (hit && (fireside >= 0 || upstream >= 0)) {
      while (!beatIs(r.app, "herd")) appTick(r.app);
      appTick(r.app);
      hit = w->day.kind == DAY_HERD && (fireside < 0 || (w->day.herd.h.nibble_on != 0) == (fireside != 0)) &&
            (upstream < 0 || (w->day.herd.h.fawn_reed.x > 0.f) == (upstream != 0));
    }
    r.stop();
    if (hit) return seed;
  }
  return 0;
}

// ---- [1] the plan ------------------------------------------------------------------------------------------------

static void planLoop(LoopPlan& p, uint64_t seed, const Inputs& in, Memory& m) {
  memset(&p, 0, sizeof p);
  p.family = SF_MIDDAY;
  Rng r;
  r.seed(seed);
  spinePlan(p, r, in, m);
}
static bool has(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return true;
  return false;
}

static void testPlan() {
  printf("[1] the plan\n");
  int loops = 0, herds = 0, seen_v[4] = {0, 0, 0, 0}, seen_s[3] = {0, 0, 0}, runs2 = 0, bad_len = 0, bad_mix = 0;
  float len_min = 1e9f, len_max = 0.f;
  for (uint64_t seed = 1; seed <= 400; ++seed) {
    Memory m;
    memset(&m, 0, sizeof m);
    const Inputs in = midday();
    bool last = false;
    for (int loop = 0; loop < 6; ++loop) {
      LoopPlan p;
      planLoop(p, seed * 1000003ull + (uint64_t)loop * 7919ull + 1ull, in, m);
      ++loops;
      const bool h = p.herd != 0;
      const float s = (float)spineLoopTicks(p) / (float)SS_TICK_HZ;
      if (h) {
        ++herds;
        ++seen_v[herdVariant(p.herd)];
        ++seen_s[herdSize(p.herd)];
        if (last) ++runs2;
        if (s < len_min) len_min = s;
        if (s > len_max) len_max = s;
        if (s < (float)SS_LOOP_MIN_SECONDS - 0.01f || s > (float)SS_LOOP_MAX_SECONDS + 0.01f) ++bad_len;
        if (p.n_majors > 2 || has(p, MJ_DEER_STICK) || has(p, MJ_PLINK) || has(p, MJ_LONGSHOT) || has(p, MJ_MUSHROOM)) ++bad_mix;
        char line[400];
        middayFormatPlan(p, line, sizeof line);
        CHECK(strstr(line, " herd=") != nullptr, "a herd loop's plan line has no herd token: %s", line);
      } else {
        char line[400];
        middayFormatPlan(p, line, sizeof line);
        CHECK(strstr(line, "herd") == nullptr, "a loop without the deer names them: %s", line);
        CHECK(s >= (float)SS_LOOP_MIN_SECONDS - 0.01f && s <= (float)SS_LOOP_MAX_SECONDS + 0.01f, "seed %d loop %d: %.0f s", (int)seed, loop, (double)s);
      }
      last = h;
    }
  }
  const float share = (float)herds / (float)loops;
  printf("  %d midday loops: the deer in %d (%.1f %%); variants A %d B %d C %d; five %d six %d four %d; herd loops %.0f to %.0f s\n",
         loops, herds, (double)(100.f * share), seen_v[1], seen_v[2], seen_v[3], seen_s[0], seen_s[1], seen_s[2], (double)len_min,
         (double)len_max);
  if (ssS22Switch() == 0) {
    CHECK(herds == 0, "SS_S22 off: the deer planned %d times", herds);
    return;
  }
  // 30 % of the loops that may have them, and never two running: 0.30 / 1.30 = 23 % of all (INTEGRATION 5.6)
  CHECK(share > 0.19f && share < 0.27f, "the deer came in %.1f %% of loops", (double)(100.f * share));
  CHECK(runs2 == 0, "%d herd loops straight after a herd loop", runs2);
  CHECK(bad_len == 0, "%d herd loops outside 480 to 660 s", bad_len);
  CHECK(bad_mix == 0, "%d herd loops with more than two majors or a forbidden one", bad_mix);
  CHECK(seen_v[1] > 0 && seen_v[2] > 0 && seen_v[3] > 0, "a variant never came");
  CHECK(seen_s[0] > 0 && seen_s[1] > 0 && seen_s[2] > 0, "a size never came");
  // the gates
  for (int c = 0; c < 3; ++c) {
    int n = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
      Memory m;
      memset(&m, 0, sizeof m);
      Inputs in = midday(c == 0 ? 1 : 0, 0);
      // Scene codes (ss_app.h: 1 rain, 7 storm), not WMO codes: 63 and 95 map to clear, which skipped
      // these two gates (found in C6, fixed in the final check).
      if (c == 1) in.weather_code = 1;   // rain
      if (c == 2) in.weather_code = 7;   // storm
      const bool wet = weatherKindForCode(in.weather_code) == WX_RAIN || weatherKindForCode(in.weather_code) == WX_STORM;
      CHECK(c == 0 || wet, "gate %d: code %d is not rain or storm", c, in.weather_code);
      if (c > 0 && !wet) continue;
      LoopPlan p;
      planLoop(p, seed * 31ull + 5ull, in, m);
      n += p.herd != 0;
    }
    CHECK(n == 0, "gate %d (dog, rain, storm): the deer planned %d times", c, n);
  }
  // the beat's seconds are in the length
  {
    LoopPlan p;
    Memory m;
    memset(&m, 0, sizeof m);
    planLoop(p, 8, midday(), m);
    const uint32_t with = spineLoopTicks(p);
    const uint8_t hb = p.herd;
    p.herd = hb ? 0 : 1;
    const uint32_t other = spineLoopTicks(p);
    const int want = (herdSeconds(1) + herdReturnExtra(1, p.fvar[0])) * SS_TICK_HZ;
    CHECK((hb ? with - other : other - with) == (uint32_t)want, "the herd's seconds: %d ticks, want %d", (int)(hb ? with - other : other - with), want);
  }
}

// ---- one visit, watched ------------------------------------------------------------------------------------------

struct Opt {
  int bell_at = -1;      // beat tick of a doorbell
  int dog_at = -1;       // beat tick at which Inputs::dog_out becomes 1 (Lucy)
  int dog_loop_tick = -1;   // loop tick (during the build) at which it does
  int passer_at = -1;    // beat tick a visitor walks up (held 3 s)
  int weather = -1;      // poke this weather kind when the beat begins
  bool check_draw = true;
  bool to_end = false;   // run the whole loop (else to the first beat after the return's pause)
};

struct Out {
  bool ran, herd_seen;
  int beat_ticks, max_deer, spawned, other_critters, hiker_seen, bad_rows, bad_box, bad_pine;
  int pic_samples, pic_min_gap, out_samples, out_min_gap, last_deer_tick, first_deer_tick;
  int overruns, strips_max, checks, diffs, wobble_end, crit_end, kind_after_beat;
  long strips_sum;
  float door_after_beat, door_after_return, alarm_t;
  int alarm, flee, second;
  int ret_done_at, ret_ticks, freed_at;   // return: the script's last step done (beat tick), the beat's ticks; loop ticks after the return began when the union was freed
  int prints_max, zipped, seen;
  uint32_t hash;
  // the reeds
  int nib_on, nib_gone, nib_stalk_present_before;
  float nib_err_max, d4_from_g4;
  int reed_on, reed_moving, reed_rest_72, reed_past_base;
  // the dog
  int dog_touch, dog_chased, right_exit;
  // the visitor
  int looks_06, alarm_66, down_66;
  int deer_after_gone;   // deer seen again after the camp emptied (the second look)
  int out_in_9;          // every deer out of sight within 9 s of a hard alarm
};

static Out play(uint64_t seed, Inputs in, const Opt& o, const char* tag) {
  Out e;
  memset(&e, 0, sizeof e);
  e.hash = 2166136261u;
  e.pic_min_gap = e.out_min_gap = 9999;
  e.last_deer_tick = e.first_deer_tick = e.ret_done_at = e.freed_at = -1;
  e.d4_from_g4 = -1.f;
  Rig a, b;
  const bool ok = a.start(seed, in) && (!o.check_draw || b.start(seed, in));
  CHECK(ok, "%s: start", tag);
  if (!ok) return e;
  World* w = const_cast<World*>(appWorld(a.app));
  World* wb = o.check_draw ? const_cast<World*>(appWorld(b.app)) : nullptr;
  const uint32_t len = appLoopTicks(a.app);
  e.ran = true;
  const int variant = herdVariant(w->plan.herd) - 1;
  const float pic_from = variant == 0 ? 73.5f : (variant == 1 ? 72.f : 70.f);
  int bt = 0, ret_bt = -1, since_ret = -1, phase = 0;   // phase 0 before, 1 the herd beat, 2 the return, 3 after
  int gone_tick = -1, hard_tick = -1;
  bool seen_any[CRITTER_MAX];
  memset(seen_any, 0, sizeof seen_any);
  float reed_rest = 0.f;
  bool reed_rest_set = false;
  for (uint32_t t = 0; t < len; ++t) {
    const bool herd = beatIs(a.app, "herd");
    if (phase == 0 && herd) {
      phase = 1;
      bt = 0;
      if (o.weather >= 0) {
        w->weather.kind = (uint8_t)o.weather;
        if (wb) wb->weather.kind = (uint8_t)o.weather;
      }
    } else if (phase == 1 && !herd) {
      phase = 2;
      ret_bt = 0;
      since_ret = 0;
      e.beat_ticks = bt;
      e.wobble_end = (int)w->tent.wobble;
      e.door_after_beat = w->tent.door;
      e.kind_after_beat = w->day.kind;
      CHECK(beatIs(a.app, "return"), "%s: after the herd comes %s", tag, appBeatName(a.app));
    } else if (phase == 2 && !beatIs(a.app, "return")) {
      phase = 3;
      e.ret_ticks = ret_bt;
      e.door_after_return = w->tent.door;
      e.overruns = appSpineOverruns(a.app);
    }
    if ((int)t == o.dog_loop_tick || (phase == 1 && bt == o.dog_at)) {
      in.dog_out = 1;
      appSetInputs(a.app, in);
      if (o.check_draw) appSetInputs(b.app, in);
    }
    if (phase == 1 && bt == o.bell_at) {
      appPostEvent(a.app, Event{EV_DOORBELL, 0});
      if (o.check_draw) appPostEvent(b.app, Event{EV_DOORBELL, 0});
    }
    if (phase == 1 && o.passer_at >= 0 && (bt == o.passer_at || bt == o.passer_at + 75)) {
      in.passer_by = bt == o.passer_at;
      appSetInputs(a.app, in);
      if (o.check_draw) appSetInputs(b.app, in);
    }
    appTick(a.app);
    if (o.check_draw) appTick(b.app);
    const int strips = appRender(a.app, a.surf);
    if (phase == 1) {
      e.herd_seen = true;
      e.strips_sum += strips;
      if (strips > e.strips_max) e.strips_max = strips;
      if (bt % 9 == 0) e.hash = (e.hash ^ hashFrame(a.fb)) * 16777619u;
    }
    const bool draw_now = o.check_draw && ((phase == 1 && bt % 61 == 0) || (phase == 2 && ret_bt % 97 == 0) || (phase == 3 && since_ret % 211 == 0 && w->day.kind == DAY_HERD));
    if (draw_now) {
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++e.checks;
      if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
        if (e.diffs < 3) {
          long k = 0;
          while (a.fb[k] == b.fb[k]) ++k;
          printf("  %s phase %d tick %d: incremental differs from full at (%ld,%ld)\n", tag, phase, phase == 1 ? bt : ret_bt, k % SS_WIDTH, k / SS_WIDTH);
        }
        ++e.diffs;
      }
    }
    if (phase == 1) {
      const float bs = (float)bt * SS_TICK_DT;
      if (w->hiker.visible || w->fx.on) ++e.hiker_seen;
      const bool live = w->day.kind == DAY_HERD;
      int8_t slots[HERD_MAX];
      const int n_in = herdDeerSlots(*w, slots);
      int vis = 0;
      Rect rc[HERD_MAX];
      for (int i = 0; i < HERD_MAX; ++i) {
        rc[i] = Rect{0, 0, 0, 0};
        if (slots[i] < 0) continue;
        if (!seen_any[slots[i]]) {
          seen_any[slots[i]] = true;
        }
        const Critter& c = w->critters[slots[i]];
        if (!c.visible) continue;
        rc[i] = critterBounds(c);
        ++vis;
        if (rc[i].y0 < 0 || rc[i].y1 > SS_HEIGHT) ++e.bad_rows;
        if (inChipBox(rc[i])) ++e.bad_box;
        const int cx = w->land.camp_x;
        if (rc[i].x1 > cx - 70 && rc[i].x0 < cx + 70 && c.feet.y < 343.f) ++e.bad_pine;
      }
      (void)n_in;
      for (int s = 0; s < CRITTER_STORY; ++s)
        if (w->crit[s].in_use && (w->crit[s].tag != CT_HERD || w->critters[s].kind != CR_DEER)) ++e.other_critters;
      if (vis > e.max_deer) e.max_deer = vis;
      if (vis > 0) {
        if (e.first_deer_tick < 0) e.first_deer_tick = bt;
        e.last_deer_tick = bt;
        if (gone_tick >= 0 && bt > gone_tick + 50) ++e.deer_after_gone;
      } else if (e.first_deer_tick >= 0 && gone_tick < 0 && live && w->day.herd.h.alarm != 0) {
        gone_tick = bt;
      }
      if (live) {
        const HerdState& h = w->day.herd.h;
        const HerdDrive& v = w->day.herd.v;
        if (h.alarm && e.alarm == 0) {
          e.alarm = h.alarm;
          e.alarm_t = h.alarm_t;
          e.flee = h.flee;
          if (h.alarm == 2) hard_tick = bt;
        }
        if (h.second_done) e.second = 1;
        if (h.n_prints > e.prints_max) e.prints_max = h.n_prints;
        // the settled picture: every pair 2 px apart
        if (!h.alarm && bs >= pic_from && bt % 5 == 0) {
          ++e.pic_samples;
          for (int i = 0; i < h.n; ++i)
            for (int j = i + 1; j < h.n; ++j) {
              if (rc[i].empty() || rc[j].empty()) continue;
              const int g = gap(rc[i], rc[j]);
              if (g < e.pic_min_gap) e.pic_min_gap = g;
            }
        }
        // the way out (F12): two deer both on their way out never merge
        if (h.alarm == 1 && bt % 5 == 0) {
          for (int i = 0; i < h.n; ++i)
            for (int j = i + 1; j < h.n; ++j) {
              if (rc[i].empty() || rc[j].empty() || h.d[i].mode != HM_OUT || h.d[j].mode != HM_OUT) continue;
              ++e.out_samples;
              const int g = gap(rc[i], rc[j]);
              if (g < e.out_min_gap) e.out_min_gap = g;
            }
        }
        if (h.alarm == 2 && h.flee > 0) {
          for (int i = 0; i < h.n; ++i)
            if (slots[i] >= 0 && w->critters[slots[i]].feet.x > 800.f) e.right_exit = 1;
          for (int dg = 0; dg < 2; ++dg) {
            Rect dr;
            int act = -1;
            if (!appHouseDog(a.app, dg, &dr, &act)) continue;
            if (act == HA_CHASE) e.dog_chased = 1;
            for (int i = 0; i < h.n; ++i)
              if (!rc[i].empty() && gap(rc[i], dr) < 0) ++e.dog_touch;
          }
        }
        // the doe and the cattail
        e.nib_on = h.nibble_on;
        if (h.nibble_on && v.nib_clump >= 0) {
          ReedStalkInfo si;
          reedsStalk(*w, v.nib_clump, v.nib_stalk, si);
          if (v.nib == 0) e.nib_stalk_present_before = si.present;
          if (v.nib == 1 && v.nib_t >= 1.25f) {
            const int gi = herdIndexOf(h, HR_GRAZER);
            if (gi >= 0 && slots[gi] >= 0) {
              const Vec2 nose = critterDeerNose(w->critters[slots[gi]]);
              const float d = sqrtf((nose.x - si.tip.x) * (nose.x - si.tip.x) + (nose.y - si.tip.y) * (nose.y - si.tip.y));
              if (d > e.nib_err_max) e.nib_err_max = d;
            }
          }
          if (v.nib == 2 && !si.present) e.nib_gone = 1;
        }
        if (bt == 60 * SS_TICK_HZ) {
          const int gi = herdIndexOf(h, HR_GRAZER);
          if (gi >= 0 && slots[gi] >= 0) e.d4_from_g4 = fabsf(w->critters[slots[gi]].feet.x - herdSpot(h, HS_G4, HR_GRAZER).x);
        }
        // the fawn in the reeds
        e.reed_on = h.fawn_reed.x > 0.f;
        if (e.reed_on && v.reed_clump >= 0) {
          const int16_t push = w->reeds.push[v.reed_clump];
          if (v.fawn_reed == 0) {
            reed_rest = (float)push;
            reed_rest_set = true;
          }
          if (v.fawn_reed == 2 && reed_rest_set && (fabsf((float)push - reed_rest) > 256.f || abs((int)w->reeds.push_v[v.reed_clump]) > 200)) e.reed_moving = 1;
          if (bt == 72 * SS_TICK_HZ) e.reed_rest_72 = abs((int)w->reeds.push_v[v.reed_clump]);
          ReedClumpInfo ci;
          const int fi = herdIndexOf(h, HR_FAWN);
          if (reedsClump(*w, v.reed_clump, ci) && fi >= 0 && !rc[fi].empty() && v.fawn_reed >= 1 && h.alarm == 0 && rc[fi].x1 > (int)ci.base.x + 1) ++e.reed_past_base;
        }
        // the visitor
        if (o.passer_at >= 0) {
          if (bt == o.passer_at + 15) {
            for (int i = 0; i < h.n; ++i)
              if (slots[i] >= 0 && w->critters[slots[i]].action == CA_HEADTURN && h.d[i].look_t > 0.f) ++e.looks_06;
          }
          if (bt == o.passer_at + 6 * SS_TICK_HZ) {
            e.alarm_66 = h.alarm;
            for (int i = 0; i < h.n; ++i) e.down_66 += h.d[i].head_down;
          }
        }
      }
      if (hard_tick >= 0 && bt == hard_tick + 9 * SS_TICK_HZ) e.out_in_9 = vis == 0;
      ++bt;
    } else if (phase == 2) {
      if (w->day.kind == DAY_HERD) {
        const HerdState& h = w->day.herd.h;
        if ((h.signs & 4) != 0) e.seen = 1;
        if (e.door_after_beat > 0.02f && w->tent.door == 0.f) e.zipped = 1;
      }
      if (e.ret_done_at < 0 && scriptDone(*w)) e.ret_done_at = ret_bt;
      ++ret_bt;
    }
    if (phase >= 2) {
      if (e.freed_at < 0 && w->day.kind == DAY_NONE) e.freed_at = since_ret;
      ++since_ret;
      if (phase == 3 && !o.to_end && (beatIs(a.app, "lunch") || since_ret > 120 * SS_TICK_HZ)) break;
    }
  }
  int spawned = 0;
  for (int s = 0; s < CRITTER_MAX; ++s) spawned += seen_any[s];
  e.spawned = spawned;
  if (phase < 3) e.overruns = appSpineOverruns(a.app);
  e.crit_end = appCritterCount(a.app);
  a.stop();
  if (o.check_draw) b.stop();
  return e;
}

// What every clean visit must show.
static void checkClean(const Out& e, const char* tag, int want_deer) {
  CHECK(e.ran && e.herd_seen, "%s: the beat did not play", tag);
  CHECK(e.beat_ticks == (int)(kHerdSeconds * (float)SS_TICK_HZ), "%s: the beat ran %d ticks", tag, e.beat_ticks);
  CHECK(e.hiker_seen == 0, "%s: he was in the scene on %d ticks of the visit", tag, e.hiker_seen);
  CHECK(e.other_critters == 0, "%s: another critter was in the scene (%d tick-slots)", tag, e.other_critters);
  if (want_deer >= 0) CHECK(e.max_deer == want_deer, "%s: %d deer in the frame at once, want %d", tag, e.max_deer, want_deer);
  CHECK(e.bad_rows == 0 && e.bad_box == 0 && e.bad_pine == 0, "%s: deer out of rows %d, in the chip's box %d, behind the pine %d", tag, e.bad_rows, e.bad_box, e.bad_pine);
  CHECK(e.wobble_end == 0, "%s: the tent's wobble is %d at the beat's end", tag, e.wobble_end);
  CHECK(e.overruns == 0, "%s: %d overruns", tag, e.overruns);
  CHECK(e.diffs == 0, "%s: %d of %d incremental frames differ from a full render", tag, e.diffs, e.checks);
  CHECK(e.strips_sum <= 9L * e.beat_ticks, "%s: %.2f strips a frame over the beat", tag, (double)e.strips_sum / (double)e.beat_ticks);
  CHECK(e.ret_done_at >= 0 && e.ret_ticks - e.ret_done_at >= 2 * SS_TICK_HZ, "%s: the return's script ended at tick %d of %d", tag, e.ret_done_at, e.ret_ticks);
}

static void testVisits() {
  printf("[2] each variant and size\n");
  static const char* kV[3] = {"A", "B", "C"};
  static const int kN[3] = {5, 6, 4};
  float empty_min = 1e9f;
  int pic_min = 9999, out_min = 9999;
  for (int size = 0; size < 3; ++size)
    for (int v = 0; v < 3; ++v) {
      const int herd = (v + 1) | (size << 2);
      // the plain five of A with each return; the others with whatever return their first seed has
      const int n_ret = (size == 0 && v == 0) ? 3 : 1;
      for (int r = 0; r < n_ret; ++r) {
        const uint64_t seed = findSeed(herd, n_ret == 3 ? r : -1, -1, -1);
        char tag[64];
        snprintf(tag, sizeof tag, "%s%d ret %d seed %d", kV[v], kN[size], r, (int)seed);
        CHECK(seed != 0, "%s: no seed", tag);
        if (!seed) continue;
        Opt o;
        const Out e = play(seed, midday(), o, tag);
        checkClean(e, tag, kN[size]);
        CHECK(e.alarm == 1 && fabsf(e.alarm_t - kHerdSoftAt) < 3.2f, "%s: alarm %d at %.1f s", tag, e.alarm, (double)e.alarm_t);
        const float empty = kHerdSeconds - (float)(e.last_deer_tick + 1) * SS_TICK_DT;
        if (empty < empty_min) empty_min = empty;
        CHECK(empty >= 5.f, "%s: the camp is empty only %.1f s before he walks in", tag, (double)empty);
        // 0.2 s apart: at least 3 s of it (a shy herd hears the alarm 3 s sooner), 6 s for the others
        CHECK(e.pic_samples >= (e.alarm_t < kHerdSoftAt - 1.f ? 15 : 30), "%s: the settled picture had %d samples", tag, e.pic_samples);
        CHECK(e.pic_min_gap >= 2, "%s: two deer %d px apart in the settled picture", tag, e.pic_min_gap);
        CHECK(e.out_samples > 0 && e.out_min_gap >= 2, "%s: two leavers %d px apart on the way out (%d samples)", tag, e.out_min_gap, e.out_samples);
        if (e.pic_min_gap < pic_min) pic_min = e.pic_min_gap;
        if (e.out_min_gap < out_min) out_min = e.out_min_gap;
        // the signs
        const float want_door = v == 1 ? 0.35f : 0.10f;
        CHECK(fabsf(e.door_after_beat - want_door) < 0.011f, "%s: the flap is %.2f after the visit, want %.2f", tag, (double)e.door_after_beat, (double)want_door);
        CHECK(e.kind_after_beat == DAY_HERD && e.prints_max >= 8, "%s: kind %d, %d prints", tag, e.kind_after_beat, e.prints_max);
        CHECK(e.seen == 1, "%s: he never looked at the signs", tag);
        // returns a and c zip the flap; the limping return (b) leaves it ajar
        // (the return's variant is in the tag; read it back from the result)
        if (e.zipped) CHECK(e.door_after_return == 0.f, "%s: zipped but the flap is %.2f", tag, (double)e.door_after_return);
        else CHECK(fabsf(e.door_after_return - want_door) < 0.011f, "%s: not zipped and the flap is %.2f", tag, (double)e.door_after_return);
        if (n_ret == 3) CHECK(e.zipped == (r != 1), "%s: zipped %d", tag, e.zipped);
        CHECK(e.freed_at >= 0 && e.freed_at <= 75 * SS_TICK_HZ, "%s: the union was freed %d ticks after the return began", tag, e.freed_at);
        printf("  %-22s deer %d, alarm %.1f s, empty %.1f s, picture gap %d px, leavers %d px, strips %.2f (max %d), return done %d of %d, freed at %.0f s\n",
               tag, e.max_deer, (double)e.alarm_t, (double)empty, e.pic_min_gap, e.out_min_gap, (double)e.strips_sum / (double)e.beat_ticks, e.strips_max,
               e.ret_done_at, e.ret_ticks, (double)e.freed_at * (double)SS_TICK_DT);
      }
    }
  printf("  least: %.1f s of empty camp, %d px in the settled picture, %d px between leavers\n", (double)empty_min, pic_min, out_min);
  // one whole loop: nothing left at its end
  {
    const uint64_t seed = findSeed(1, 0, -1, -1);
    Opt o;
    o.to_end = true;
    o.check_draw = false;
    const Out e = play(seed, midday(), o, "A5 whole loop");
    CHECK(e.crit_end == 0 && e.overruns == 0, "whole loop: %d critters left, %d overruns", e.crit_end, e.overruns);
  }
}

static void testReeds() {
  printf("[3] the doe and the cattail, the fawn in the reeds\n");
  {
    const uint64_t seed = findSeed(1, -1, 1, -1);
    CHECK(seed != 0, "no herd loop with the fireside clump");
    if (seed) {
      const Out e = play(seed, midday(), Opt{}, "cattail");
      checkClean(e, "cattail", 5);
      CHECK(e.nib_on == 1 && e.nib_stalk_present_before == 1 && e.nib_gone == 1, "the cattail: on %d, whole before %d, bitten %d", e.nib_on, e.nib_stalk_present_before, e.nib_gone);
      CHECK(e.nib_err_max <= 3.f, "her muzzle was %.1f px from the stalk's tip while she held it", (double)e.nib_err_max);
      CHECK(e.pic_min_gap >= 2, "with the cattail: %d px in the settled picture", e.pic_min_gap);
      printf("  seed %d: the head is bitten off; muzzle to tip at most %.1f px; she is %.0f px from her place at 60 s\n", (int)seed, (double)e.nib_err_max, (double)e.d4_from_g4);
      CHECK(e.d4_from_g4 >= 0.f && e.d4_from_g4 <= 32.f, "after the cattail she is %.0f px from her place", (double)e.d4_from_g4);
    }
  }
  {
    const uint64_t seed = findSeed(1, -1, 0, -1);
    CHECK(seed != 0, "no herd loop without the fireside clump");
    if (seed) {
      Opt o;
      o.check_draw = false;
      const Out e = play(seed, midday(), o, "no cattail");
      CHECK(e.nib_on == 0 && e.d4_from_g4 >= 0.f && e.d4_from_g4 <= 32.f, "without the clump: nibble %d, she is %.0f px from G4", e.nib_on, (double)e.d4_from_g4);
      CHECK(e.pic_min_gap >= 2, "without the cattail: %d px in the settled picture", e.pic_min_gap);
    }
  }
  {
    const uint64_t seed = findSeed(1, -1, -1, 1);
    CHECK(seed != 0, "no herd loop with the upstream clump in the fawn's reach");
    if (seed) {
      Opt o;
      o.check_draw = false;
      const Out e = play(seed, midday(), o, "fawn reeds");
      CHECK(e.reed_on == 1 && e.reed_moving == 1, "the fawn in the reeds: on %d, the clump moved %d", e.reed_on, e.reed_moving);
      CHECK(e.reed_rest_72 <= 200, "the clump still swings at 72 s (speed %d)", e.reed_rest_72);
      CHECK(e.reed_past_base == 0, "the fawn's bounds passed the clump's base on %d ticks", e.reed_past_base);
      CHECK(e.overruns == 0, "fawn reeds: %d overruns", e.overruns);
      printf("  seed %d: the clump leans and rings, speed %d at 72 s\n", (int)seed, e.reed_rest_72);
    }
  }
}

static void testAlarms() {
  printf("[4] real alarms\n");
  const uint64_t seed = findSeed(1, 0, -1, -1);
  static const int kBell[3] = {30, 50, 70};
  for (int k = 0; k < 3; ++k) {
    Opt o;
    o.bell_at = kBell[k] * SS_TICK_HZ;
    char tag[40];
    snprintf(tag, sizeof tag, "bell at %d s", kBell[k]);
    const Out e = play(seed, midday(), o, tag);
    CHECK(e.alarm == 2 && e.flee < 0, "%s: alarm %d flee %d", tag, e.alarm, e.flee);
    CHECK(e.out_in_9 == 1, "%s: a deer still in sight 9 s after", tag);
    CHECK((e.second == 1) == (k == 0), "%s: second look %d", tag, e.second);
    CHECK((e.deer_after_gone > 0) == (k == 0), "%s: deer seen again on %d ticks", tag, e.deer_after_gone);
    CHECK(e.overruns == 0 && e.diffs == 0 && e.bad_box == 0 && e.bad_rows == 0 && e.wobble_end == 0 && e.hiker_seen == 0, "%s: overruns %d diffs %d box %d rows %d wobble %d hiker %d", tag, e.overruns, e.diffs, e.bad_box, e.bad_rows, e.wobble_end, e.hiker_seen);
    printf("  %s: out of sight within 9 s, second look %d, flap %.2f after, freed %.0f s into the return\n", tag, e.second, (double)e.door_after_beat, (double)e.freed_at * (double)SS_TICK_DT);
  }
  {
    Opt o;
    o.dog_at = 40 * SS_TICK_HZ;
    const Out e = play(seed, midday(), o, "dog at 40 s");
    CHECK(e.alarm == 2 && e.flee > 0 && e.right_exit == 1, "dog: alarm %d flee %d right %d", e.alarm, e.flee, e.right_exit);
    CHECK(e.out_in_9 == 1, "dog: a deer still in sight 9 s after");
    CHECK(e.dog_touch == 0, "dog: a deer's bounds touched the dog's on %d ticks", e.dog_touch);
    CHECK(e.dog_chased == 1, "dog: no chase");
    CHECK(e.second == 0 && e.overruns == 0 && e.diffs == 0 && e.bad_box == 0 && e.bad_pine == 0, "dog: second %d overruns %d diffs %d box %d pine %d", e.second, e.overruns, e.diffs, e.bad_box, e.bad_pine);
    printf("  a dog let out at 40 s: they go right, the dog chases, touches %d\n", e.dog_touch);
  }
  {
    Opt o;
    o.passer_at = 60 * SS_TICK_HZ;
    const Out e = play(seed, midday(), o, "visitor at 60 s");
    CHECK(e.looks_06 >= 2 && e.looks_06 <= 3, "visitor: %d heads up 0.6 s after", e.looks_06);
    CHECK(e.alarm_66 == 0 && e.down_66 >= 1, "visitor: alarm %d, heads down again %d at 66 s", e.alarm_66, e.down_66);
    checkClean(e, "visitor", 5);
    printf("  a visitor at 60 s: %d heads up, grazing again by 66 s\n", e.looks_06);
  }
}

static void testNoShowAndWeather() {
  printf("[5] the no-show, wind, rain\n");
  const uint64_t seed = findSeed(1, 0, -1, -1);
  {
    Opt o;
    o.dog_loop_tick = 300;   // during the build: the dog cannot be in yet (F21)
    const Out e = play(seed, midday(), o, "no-show");
    CHECK(e.herd_seen && e.spawned == 0 && e.max_deer == 0, "no-show: %d deer spawned, %d seen", e.spawned, e.max_deer);
    CHECK(e.kind_after_beat == DAY_NONE && e.alarm == 0 && e.prints_max == 0, "no-show: kind %d alarm %d prints %d", e.kind_after_beat, e.alarm, e.prints_max);
    CHECK(e.overruns == 0 && e.diffs == 0 && e.hiker_seen == 0 && e.door_after_beat == 0.f, "no-show: overruns %d diffs %d hiker %d door %.2f", e.overruns, e.diffs, e.hiker_seen, (double)e.door_after_beat);
    CHECK(e.ret_done_at >= 0, "no-show: the return did not finish");
    printf("  a dog wanted out at tick 300: no deer, the return done at %d of %d\n", e.ret_done_at, e.ret_ticks);
  }
  for (int k = 0; k < 2; ++k) {
    Opt o;
    o.weather = k == 0 ? WX_WIND : WX_RAIN;
    const char* tag = k == 0 ? "wind" : "rain";
    const Out e = play(seed, midday(), o, tag);
    CHECK(e.herd_seen && e.max_deer == 5, "%s: %d deer", tag, e.max_deer);
    CHECK(e.overruns == 0 && e.diffs == 0 && e.bad_box == 0 && e.bad_rows == 0 && e.bad_pine == 0 && e.wobble_end == 0, "%s: overruns %d diffs %d box %d rows %d pine %d wobble %d", tag, e.overruns, e.diffs, e.bad_box, e.bad_rows, e.bad_pine, e.wobble_end);
    CHECK(e.pic_min_gap >= 2, "%s: %d px in the settled picture", tag, e.pic_min_gap);
    printf("  %s: clean, strips %.2f\n", tag, (double)e.strips_sum / (double)e.beat_ticks);
  }
}

static void testRest() {
  printf("[6] fire and ice over the prints, determinism, the numbers\n");
  const uint64_t seed = findSeed(1, 1, -1, -1);   // the limping return: the flap stays ajar, the look starts the fade
  // F25: with only the signs left the day union does not hold fire and ice off
  {
    Rig r;
    r.start(seed, midday());
    World* w = const_cast<World*>(appWorld(r.app));
    bool in_herd = false, after = false;
    int only_signs = 0, calm_over_signs = 0, calm_in_beat = 0;
    for (uint32_t t = 0; t < appLoopTicks(r.app); ++t) {
      const bool herd = beatIs(r.app, "herd");
      if (herd) in_herd = true;
      if (in_herd && !herd) after = true;
      appTick(r.app);
      if (herd && w->day.kind == DAY_HERD && fireIceCalm(*w)) ++calm_in_beat;
      if (after && herdOnlySigns(*w)) {
        ++only_signs;
        if (fireIceCalm(*w)) ++calm_over_signs;
      }
      if (after && beatIs(r.app, "lunch")) break;
    }
    CHECK(only_signs > 100 && calm_over_signs > 0, "F25: %d ticks with only the signs, calm on %d of them", only_signs, calm_over_signs);
    CHECK(calm_in_beat == 0, "F25: fire and ice could start on %d ticks of the visit itself", calm_in_beat);
    printf("  only the signs for %d ticks; fire and ice could start on %d of them, never during the visit\n", only_signs, calm_over_signs);
    r.stop();
  }
  {
    Opt o;
    o.check_draw = false;
    const Out a = play(seed, midday(), o, "det a"), b = play(seed, midday(), o, "det b");
    CHECK(a.hash == b.hash && a.last_deer_tick == b.last_deer_tick && a.freed_at == b.freed_at, "two runs differ: %08x %08x", a.hash, b.hash);
    printf("  two runs of seed %d: the same frames (%08x)\n", (int)seed, a.hash);
  }
  printf("  sizeof(App) %u, DayHerd %u of DayBike %u, HerdState %u, cues %d\n", (unsigned)appAppBytes(), (unsigned)sizeof(DayHerd), (unsigned)sizeof(DayBike),
         (unsigned)sizeof(HerdState), herdCueCount());
  CHECK(appAppBytes() <= 29992u, "sizeof(App) is %u, over the 29,992 wire", (unsigned)appAppBytes());
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  testPlan();
  if (ssS22Switch() != 0) {
    testVisits();
    testReeds();
    testAlarms();
    testNoShowAndWeather();
    testRest();
  } else {
    printf("SS_S22 off: the deer never plan; the visit's own tests are skipped\n");
  }
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
