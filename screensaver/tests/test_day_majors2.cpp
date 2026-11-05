// Tests for the bike, the archery, the slackline and the ropes course as surprise majors (core/scene/day_bike.cpp,
// day_archery.cpp, day_slackline.cpp, day_ropes.cpp, day.cpp; MORNING_DAYTIME_PLAN.md sections 5 to 8), wired into the
// midday family beside the kite and the boomerang (test_day_majors.cpp):
//
//   [1] the plan: none at night, in the other families or with the night rule on; none in a rain, snow or storm the
//       OUTSIDE data says is there (and in wind, heat, fog, chill and clear they play); how many midday loops play each
//   [2] each variant plays to its end in the real scene: no overrun, the script done with 3 s to spare, no critter left,
//       him seated at rest with his hands empty, the hat on and his pack as it was, the daytime union zero again; him
//       in frame (the bike's walk off and ride in and out excepted: rows only), everything the piece draws out of the
//       chip's box (x 538..796, y 412..476) and him out of the quiet corner, the geometry the header promises (the
//       archer's 150 px, the slackline's 180 to 260, the ropes' ground line under 385, the bike's lane above 412 past
//       x 538), each variant's own events seen, and the script never walking or posing him while a module drives him
//   [3] a random rain after the plan sends each of them to the no-show: nothing drawn, no overrun, him seated
//   [4] the doorbell freeze: the bike, the water, the walker, the line, the critter, the bow's story, the ropes' props and
//       the beat's clock stand still for the whole reaction, and the beat carries on and ends clean
//   [5] drawn tick by tick each variant equals a full render; strips per frame
//   [6] sizeof(App) stays under 30,000 bytes
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/day.h"
#include "scene/majors.h"
#include "scene/memory.h"
#include "scene/spine_family.h"
#include "scene/weather.h"
#include "scene/world.h"

#include <initializer_list>
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

static Inputs makeInputs(float mood, int family, bool night = false, bool outside = false, int code = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.spine_force = (uint8_t)(1 + family);
  in.night_mode = night;
  in.have_outside = outside;
  in.weather_code = code;
  return in;
}
static bool inChipBox(const Rect& r) { return !r.empty() && r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }
static bool inCorner(const Rect& r) { return !r.empty() && r.x1 > LS_QUIET_X0 && r.y1 > LS_QUIET_Y0; }
static bool inRows(const Rect& r) { return r.y0 >= 0 && r.y1 <= SS_HEIGHT; }
static bool inFrame(const Rect& r) { return r.x0 >= 0 && r.x1 <= SS_WIDTH && inRows(r); }
static bool beatIs(const App* a, const char* name) { return strcmp(appBeatName(a), name) == 0; }

static int indexOf(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
}
static bool wetWeather(const World& w) { return w.weather.kind == WX_RAIN || w.weather.kind == WX_SNOW || w.weather.kind == WX_STORM; }

struct Major {
  int id;
  const char* name;
  DayKind kind;
  int variants;
};
static const Major kMajorsNew[4] = {{MJ_BIKE, "bike", DAY_BIKE, 3}, {MJ_ARCHERY, "archery", DAY_ARCHERY, 3},
                                    {MJ_SLACKLINE, "slackline", DAY_SLACK, 2}, {MJ_ROPES, "ropes", DAY_ROPES, 3}};

// The first `n` seeds (from `from`) whose loop-0 plan has major `id` with variant `var` (the low two bits) and whose
// random weather is not a rain, snow or storm (a no-show: [3] has its own seeds).
static int findSeeds(int id, int var, const Inputs& in, uint64_t* out, int n, uint64_t from = 1, uint64_t to = 1200,
                     bool want_wet = false) {
  int k = 0;
  for (uint64_t seed = from; seed <= to && k < n; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return k;
    const World* w = appWorld(r.app);
    const int i = indexOf(w->plan, id);
    if (i >= 0 && (w->plan.major_var[i] & 3) == var && wetWeather(*w) == want_wet) out[k++] = seed;
    r.stop();
  }
  return k;
}

// ---- [1] the plan ----------------------------------------------------------------------------------------------

static MajorSlots middaySlots() {
  MajorSlots s;
  memset(&s, 0, sizeof s);
  s.family = SF_MIDDAY;
  s.after[0] = 0;
  s.after[1] = 1;
  s.after[2] = 2;
  s.open = 7;
  return s;
}

static int plansWith(int id, int n, const Inputs& in, const MajorSlots& sl) {
  int hits = 0;
  for (int seed = 1; seed <= n; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    LoopPlan p;
    memset(&p, 0, sizeof p);
    Memory mem;
    memset(&mem, 0, sizeof mem);
    majorsPlan(p, rng, in, mem, sl);
    hits += indexOf(p, id) >= 0;
  }
  return hits;
}

static void testPlan() {
  printf("[1] the plan: gating and how often\n");
  const MajorSlots mid = middaySlots();
  const int N = 600;
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  for (const Major& m : kMajorsNew) {
    const int hits = plansWith(m.id, N, day, mid);
    printf("  midday, %d fresh plans: %-9s %5.1f %%\n", N, m.name, 100.0 * hits / N);
    CHECK(hits > N / 12 && hits < N / 3, "%s in %d of %d midday plans", m.name, hits, N);
  }
  const Inputs night1 = makeInputs(3.f, SF_MIDDAY, true), night2 = makeInputs(0.2f, SF_MIDDAY), night3 = makeInputs(5.9f, SF_MIDDAY);
  for (const Inputs* in : {&night1, &night2, &night3})
    for (const Major& m : kMajorsNew)
      CHECK(plansWith(m.id, N, *in, mid) == 0, "%s at night (mood %.1f, night mode %d)", m.name, (double)in->mood, (int)in->night_mode);
  CHECK(plansWith(MJ_FETCH, N, night1, mid) > 0, "(the night test does not empty the pool)");
  static const char* const kName[8] = {"clear", "rain", "snow", "wind", "heat", "fog", "chill", "storm"};
  for (int code = 0; code < 8; ++code) {
    const Inputs in = makeInputs(3.f, SF_MIDDAY, false, true, code);
    const bool wet = code == 1 || code == 2 || code == 7;
    for (const Major& m : kMajorsNew) {
      const int k = plansWith(m.id, N, in, mid);
      if (wet) CHECK(k == 0, "%s planned in %s", m.name, kName[code]);
      else CHECK(k > N / 12, "only %d %s planned in %s", k, m.name, kName[code]);
    }
  }
  // The other families never play them (their masks).
  int other = 0;
  for (int fam = 0; fam < SF_COUNT; ++fam) {
    if (fam == SF_MIDDAY || fam == SF_LOOKOUT) continue;
    MajorSlots sl = mid;
    sl.family = (uint8_t)fam;
    sl.open = fam == SF_EVENING ? 15 : 7;
    for (float mood : {1.5f, 3.f, 4.f})
      for (const Major& m : kMajorsNew) other += plansWith(m.id, 200, makeInputs(mood, fam), sl);
  }
  CHECK(other == 0, "%d bike, archery, slackline or ropes plans in the evening or the morning", other);
  // Fetch stays forced when a dog is out: first among the picks, every loop.
  Inputs dog = makeInputs(3.f, SF_MIDDAY);
  dog.dog_out = 1;
  int forced = 0;
  for (int seed = 1; seed <= 200; ++seed) {
    Rng rng;
    rng.seed((uint64_t)seed);
    LoopPlan p;
    memset(&p, 0, sizeof p);
    Memory m;
    memset(&m, 0, sizeof m);
    majorsPlan(p, rng, dog, m, mid);
    forced += indexOf(p, MJ_FETCH) >= 0;
  }
  CHECK(forced == 200, "fetch forced in %d of 200 loops with a dog out", forced);
}

// ---- [2] each variant plays ----------------------------------------------------------------------------------------

struct Ev {
  bool bike_mode[BIKE_MODES], bike_rider[RIDER_MODES], bike_dir[2], bike_wet, bike_rock, splash_seen;
  bool arrow_end[5], arrow_ring0, arrow_split, bow_seen, target_seen, shot_seen;
  int shot_dist_n;
  float shot_dist_min;
  bool walker_mode[SW_MODES], hard_fall;
  int falls, crit_ticks, line_ticks, crit_off_line;
  float span;
  bool seg[SG_COUNT];
  float ropes_gy;
  float lane_max_y_right;   // the bike's lane, x >= 538
  int beat_ticks, done_at, overruns, critters, hidden_ticks;
  bool end_ok, back_in;
  char why[96];
  long strips_sum;
  int strips_n, strips_max;
  int diffs, checks;
  int driven_walk;   // ticks a module drove him while the script walked him or posed him
};

static bool packOn(const World& w) { return w.hiker.pack_on; }

// What a beat must leave behind (checked on the tick it ended).
static bool atRest(const World& w, bool pack_was, char* why, size_t n) {
  const Hiker& h = w.hiker;
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (h.walking) bad = "walking";
  else if (h.pack_on != pack_was) bad = "his pack is not as it was";
  else if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || w.fx.head != 0.f || w.fx.lift != 0.f) bad = "arms or lean still set";
  else if (w.fx.wade) bad = "still wading";
  else if (w.day.kind != DAY_NONE) bad = "the daytime union is not zero";
  else if (h.target != POSE_SIT) bad = "not sitting";
  else if (!inFrame(w.hiker_rect) || w.hiker_rect.empty()) bad = "not in frame";
  if (bad) snprintf(why, n, "%s", bad);
  return bad == nullptr;
}

// The piece this beat draws, as a rectangle.
static Rect pieceBounds(const World& w) { return dayBounds(w.day); }

static void observe(const World& w, const Major& m, Ev& ev, int n_in) {
  const DayState& d = w.day;
  if (d.kind != m.kind) return;
  switch (d.kind) {
    case DAY_BIKE: {
      const BikeState& b = d.bike.bike;
      ev.bike_mode[b.mode] = true;
      ev.bike_rider[b.rider] = true;
      if (b.mode == BIKE_RIDE || b.mode == BIKE_WHEELIE) ev.bike_dir[b.dir > 0 ? 1 : 0] = true;
      if ((b.flags & 3u) != 0) ev.bike_wet = true;
      if (b.flags & 4u) ev.bike_rock = true;
      if (b.flags & 16u) ev.splash_seen = true;
      if (n_in == 1) {
        float mx = -1e9f;
        for (int x = 538; x <= 900; x += 4) mx = fmaxf(mx, bikeLaneY(b.course, (float)x));
        ev.lane_max_y_right = mx;
      }
      break;
    }
    case DAY_ARCHERY: {
      const ArcheryState& a = d.archery.a;
      if (a.bow_on) ev.bow_seen = true;
      if (a.target_on) ev.target_seen = true;
      for (int i = 0; i < ARCHERY_ARROWS; ++i) {
        const ArcheryArrow& r = a.arrows[i];
        if (r.mode == ARROW_STUCK || r.mode == ARROW_SPLIT) {
          ev.arrow_end[r.end] = true;
          if (r.end == AEND_TARGET && r.ring == 0) ev.arrow_ring0 = true;
        }
        if (r.mode == ARROW_SPLIT) ev.arrow_split = true;
      }
      if (a.events & AEV_RELEASED) {   // each shot, once, as the arrow leaves: how far the target is from him
        ++ev.shot_dist_n;
        const float dist = fabsf(a.target.x - w.hiker.feet.x);
        if (dist < ev.shot_dist_min) ev.shot_dist_min = dist;
        ev.shot_seen = true;
      }
      break;
    }
    case DAY_SLACK: {
      const DaySlack& s = d.slack;
      ev.walker_mode[s.walker.mode] = true;
      if (s.walker.mode == SW_FALL && s.walker.hard && s.walker.t < 0.05f) ++ev.falls;
      if (s.line_on) {
        ++ev.line_ticks;
        ev.span = s.line.b.x - s.line.a.x;
      }
      if (s.crit_on) {
        ++ev.crit_ticks;
        // On the line: its feet at the line's top edge there, to a couple of px (the module's own placement).
        if (fabsf(s.cr.feet.y - slacklineTopY(s.line, s.cr.feet.x)) > 2.5f) ++ev.crit_off_line;
      }
      break;
    }
    case DAY_ROPES: {
      const RopesState& r = d.ropes.r;
      const int sg = ropesSegment(r);
      if (sg >= 0) ev.seg[sg] = true;
      ev.ropes_gy = r.gy;
      break;
    }
    default: break;
  }
}

// Play loop 0 of `seed` (midday) through the beat `m.name`, checking every tick; `b` shadows it for the incremental-versus-full checks.
static Ev playBeat(uint64_t seed, const Inputs& in, const Major& m, int var, bool check_draw, bool quiet) {
  Ev ev;
  memset(&ev, 0, sizeof ev);
  ev.shot_dist_min = 1e9f;
  ev.lane_max_y_right = -1e9f;
  Rig a, b;
  const bool ok = a.start(seed, in) && b.start(seed, in);
  CHECK(ok, "start seed %d", (int)seed);
  if (!ok) return ev;
  const World* w = appWorld(a.app);
  const uint32_t len = appLoopTicks(a.app);
  char tag[48];
  snprintf(tag, sizeof tag, "%s %c seed %d", m.name, 'A' + var, (int)seed);
  CHECK(indexOf(w->plan, m.id) >= 0, "%s: not in the plan", tag);
  uint32_t t = 0;
  bool in_beat = false, ended = false, pack_was = true;
  int bad_hiker = 0, bad_piece = 0, bad_box = 0, bad_corner = 0, n_in = 0;
  for (; t + 1 < len && !ended; ++t) {
    if (!in_beat) pack_was = packOn(*w);
    appTick(a.app);
    appTick(b.app);
    const bool now = beatIs(a.app, m.name);
    if (!now && !in_beat) {   // lead-up: marks accumulate in the dirty mask, so render now and then
      if (t % 250 == 0 || (t + 60 < len && t % 5 == 0)) appRender(a.app, a.surf);
      continue;
    }
    if (!now && in_beat) {   // the tick it ended: majorEnd has run
      ended = true;
      ev.beat_ticks = (int)w->spine.run.beat_ticks;
      ev.done_at = (int)w->spine.run.done_at;
      ev.overruns = appSpineOverruns(a.app);
      ev.critters = appCritterCount(a.app);
      ev.end_ok = atRest(*w, pack_was, ev.why, sizeof ev.why);
      break;
    }
    in_beat = true;
    ++n_in;
    const int strips = appRender(a.app, a.surf);
    ev.strips_sum += strips;
    ++ev.strips_n;
    ev.strips_max = strips > ev.strips_max ? strips : ev.strips_max;
    if (check_draw && n_in % 23 == 0) {
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++ev.checks;
      if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
        if (ev.diffs < 3) {
          long k = 0;
          while (a.fb[k] == b.fb[k]) ++k;
          printf("  %s beat tick %d: incremental differs from full at (%ld,%ld)\n", tag, n_in, k % SS_WIDTH, k / SS_WIDTH);
        }
        ++ev.diffs;
      }
    }
    // The hiker: wholly in frame, except the bike's walk off and ride in and out (rows only); never in the chip's box or the corner.
    Rect hr;
    const bool vis = appHikerBounds(a.app, &hr);
    if (vis) {
      if (m.kind == DAY_BIKE ? !inRows(hr) : !inFrame(hr)) ++bad_hiker;
      if (inChipBox(hr)) ++bad_box;
      if (inCorner(hr)) ++bad_corner;
      if (ev.hidden_ticks > 0) ev.back_in = true;
    } else {
      ++ev.hidden_ticks;
    }
    // The piece: in frame (the bike's ride runs in from and out through the edges: rows only), out of the chip's box.
    const Rect pb = pieceBounds(*w);
    if (!pb.empty()) {
      if (inChipBox(pb)) {
        if (bad_box < 3) printf("  %s tick %d: piece bounds %d,%d,%d,%d in the chip's box\n", tag, n_in, pb.x0, pb.y0, pb.x1, pb.y1);
        ++bad_box;
      }
      if (m.kind == DAY_BIKE ? !inRows(pb) : !inFrame(pb)) {
        if (bad_piece < 3) printf("  %s tick %d: piece bounds %d,%d,%d,%d out of frame\n", tag, n_in, pb.x0, pb.y0, pb.x1, pb.y1);
        ++bad_piece;
      }
    }
    // The script never walks or poses him while a module drives him (the bike's rider, the walker on the line).
    if (w->day.kind == DAY_BIKE && bikeDriving(w->day.bike.bike) && (w->hiker.walking || w->fx.path_on)) ++ev.driven_walk;
    if (w->day.kind == DAY_SLACK && w->day.slack.line_on && w->day.slack.walker.mode != SW_OFF && w->hiker.walking) ++ev.driven_walk;
    observe(*w, m, ev, n_in);
  }
  CHECK(ended, "%s: the beat never ended", tag);
  CHECK(bad_hiker == 0, "%s: %d ticks with him out of frame", tag, bad_hiker);
  CHECK(bad_piece == 0, "%s: %d ticks with the piece out of frame", tag, bad_piece);
  CHECK(bad_box == 0, "%s: %d samples in the chip's box (x 538..796, y 412..476)", tag, bad_box);
  CHECK(bad_corner == 0, "%s: %d ticks with him in the quiet corner", tag, bad_corner);
  CHECK(ev.driven_walk == 0, "%s: the script walked him on %d ticks while a module drove him", tag, ev.driven_walk);
  CHECK(ev.overruns == 0, "%s: %d overruns", tag, ev.overruns);
  CHECK(ev.critters == 0, "%s: %d critters left", tag, ev.critters);
  CHECK(ev.done_at > 0 && ev.beat_ticks - ev.done_at >= 75, "%s: script done at %d of %d ticks (wants 3 s spare)", tag, ev.done_at, ev.beat_ticks);
  CHECK(ev.end_ok, "%s: ends with %s", tag, ev.why);
  CHECK(ev.diffs == 0, "%s: %d checkpoints where incremental and full renders differ", tag, ev.diffs);
  const bool precip = wetWeather(*w);
  const double avg = ev.strips_n ? (double)ev.strips_sum / (double)ev.strips_n : 0.0;
  if (!quiet)
    printf("  %-18s %4d beat ticks, script done at %d (%.1f s spare), strips avg %.2f max %d, %d full-render checks\n", tag, ev.beat_ticks,
           ev.done_at, (double)(ev.beat_ticks - ev.done_at) / 25.0, avg, ev.strips_max, ev.checks);
  // (majors may average 10 strips a frame, 11 in the heat's shimmer; rain and snow dirty every strip on their own)
  const double limit = w->weather.kind == WX_HEAT ? 11.0 : 10.0;
  CHECK(precip || ev.strips_n == 0 || avg <= limit, "%s: %.2f strips a frame (weather %s)", tag, avg, weatherKindName(w->weather.kind));
  a.stop();
  b.stop();
  return ev;
}

static uint64_t g_seed[4][3];

static void checkVariant(const Major& m, int var, const Ev& e) {
  switch (m.kind) {
    case DAY_BIKE:
      CHECK(e.bike_mode[BIKE_RIDE], "bike %c: it rode", 'A' + var);
      CHECK(e.hidden_ticks >= 25 * 3 && e.back_in, "bike %c: a beat of empty scene (%d ticks) while he fetches it, then he is back (%d)", 'A' + var, e.hidden_ticks,
            (int)e.back_in);
      CHECK(e.lane_max_y_right > 0.f && e.lane_max_y_right <= 405.f, "bike %c: the lane is at y %.0f past x 538 (wants 405 or less: the chip's box starts at 412)", 'A' + var,
            (double)e.lane_max_y_right);
      if (var == 0) {
        CHECK(e.bike_mode[BIKE_AWAY] && e.bike_wet && e.bike_dir[0] && e.bike_dir[1], "bike A: through the stream and back (away %d wet %d both ways %d %d)", (int)e.bike_mode[BIKE_AWAY],
              (int)e.bike_wet, (int)e.bike_dir[0], (int)e.bike_dir[1]);
        CHECK(!e.bike_rider[RIDER_FLY] && !e.bike_rider[RIDER_FALL], "bike A: he stayed on");
      } else if (var == 1) {
        CHECK(e.bike_rock && e.bike_rider[RIDER_FLY] && e.bike_rider[RIDER_SPLASH] && e.bike_mode[BIKE_DOWN], "bike B: the rock, over the bars, into the water, down");
      } else {
        CHECK(e.bike_mode[BIKE_WHEELIE] && e.bike_mode[BIKE_TIP] && e.bike_rider[RIDER_FALL] && e.bike_mode[BIKE_DOWN], "bike C: the wheelie, the tip, the fall, down");
      }
      break;
    case DAY_ARCHERY:
      CHECK(e.bow_seen && e.target_seen && e.shot_seen, "archery %c: bow %d target %d shot %d", 'A' + var, (int)e.bow_seen, (int)e.target_seen, (int)e.shot_seen);
      CHECK(e.shot_dist_min >= 150.f, "archery %c: the target was %.0f px from him at a shot (wants 150 or more)", 'A' + var, (double)e.shot_dist_min);
      if (var == 0) {
        CHECK(e.arrow_end[AEND_GROUND] && e.arrow_end[AEND_PAST] && e.arrow_ring0 && e.shot_dist_n == 3, "archery A: two misses, then a bullseye (ground %d past %d bullseye %d shots %d)",
              (int)e.arrow_end[AEND_GROUND], (int)e.arrow_end[AEND_PAST], (int)e.arrow_ring0, e.shot_dist_n);
      } else if (var == 1) {
        CHECK(e.arrow_end[AEND_TREE] && e.shot_dist_n == 1, "archery B: into the tree (%d, shots %d)", (int)e.arrow_end[AEND_TREE], e.shot_dist_n);
      } else {
        CHECK(e.arrow_ring0 && e.arrow_split && e.shot_dist_n == 2, "archery C: the split arrow (bullseye %d split %d shots %d)", (int)e.arrow_ring0, (int)e.arrow_split, e.shot_dist_n);
      }
      break;
    case DAY_SLACK:
      CHECK(e.span >= 180.f && e.span <= 260.f, "slackline %c: the span is %.0f px (wants 180 to 260)", 'A' + var, (double)e.span);
      CHECK(e.walker_mode[SW_MOUNT] && e.walker_mode[SW_WALK], "slackline %c: he mounted and walked", 'A' + var);
      CHECK(e.crit_ticks >= 25 * 8 && e.crit_off_line == 0, "slackline %c: the critter on the line %d ticks, %d of them off it", 'A' + var, e.crit_ticks, e.crit_off_line);
      if (var == 0) CHECK(e.falls == 2, "slackline A: %d falls (wants 2)", e.falls);
      else CHECK(e.walker_mode[SW_FLOURISH] && e.falls == 0, "slackline B: flourish %d, falls %d", (int)e.walker_mode[SW_FLOURISH], e.falls);
      break;
    case DAY_ROPES:
      CHECK(e.ropes_gy > 0.f && e.ropes_gy <= 385.f, "ropes %c: the ground line is y %.0f (wants 385 or less)", 'A' + var, (double)e.ropes_gy);
      CHECK(e.seg[SG_BUILD] && e.seg[SG_TEAR] && e.seg[SG_HOLD], "ropes %c: built, held, torn down", 'A' + var);
      if (var == 0) CHECK(e.seg[SG_B_CROSS1] && e.seg[SG_S_SPIN] && e.seg[SG_N_HANG], "ropes A: the bridge, the swing's spin, the net's hang");
      else if (var == 1) CHECK(e.seg[SG_B_CROSS1] && e.seg[SG_S_SPIN] && !e.seg[SG_N_HANG], "ropes B: the bridge and the swing, no net");
      else CHECK(!e.seg[SG_B_CROSS1] && e.seg[SG_S_SPIN] && e.seg[SG_N_HANG], "ropes C: the swing and the net, no bridge");
      break;
    default: break;
  }
}

static void testPlays() {
  printf("[2] each variant plays to the end in the real scene; [5] drawn tick by tick it equals a full render\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  for (int k = 0; k < 4; ++k)
    for (int var = 0; var < kMajorsNew[k].variants; ++var) {
      uint64_t s[1] = {0};
      const int n = findSeeds(kMajorsNew[k].id, var, day, s, 1);
      g_seed[k][var] = n ? s[0] : 0;
      CHECK(n == 1, "no seed plays %s %c", kMajorsNew[k].name, 'A' + var);
    }
  for (int k = 0; k < 4; ++k)
    for (int var = 0; var < kMajorsNew[k].variants; ++var) {
      if (!g_seed[k][var]) continue;
      const Ev e = playBeat(g_seed[k][var], day, kMajorsNew[k], var, true, false);
      checkVariant(kMajorsNew[k], var, e);
    }
}

// Every variant over more seeds (the layouts differ: the fire, the stream, the pine): the same checks, no full renders ([2] did those).
static void testSweep() {
  printf("[2b] every variant over more seeds\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  int plays = 0;
  for (int k = 0; k < 4; ++k)
    for (int var = 0; var < kMajorsNew[k].variants; ++var) {
      uint64_t seeds[6];
      const int n = findSeeds(kMajorsNew[k].id, var, day, seeds, 6, 40);
      CHECK(n >= 5, "only %d seeds for %s %c", n, kMajorsNew[k].name, 'A' + var);
      for (int i = 0; i < n; ++i) {
        const Ev e = playBeat(seeds[i], day, kMajorsNew[k], var, false, true);
        checkVariant(kMajorsNew[k], var, e);
        ++plays;
      }
    }
  printf("  %d beats played\n", plays);
}

// ---- [3] a random rain ------------------------------------------------------------------------------------------------

static void testNoShow() {
  printf("[3] a rain the plan could not know of sends each to the no-show\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  static const int kKinds[3] = {WX_RAIN, WX_SNOW, WX_STORM};
  for (int k = 0; k < 4; ++k) {
    const Major& m = kMajorsNew[k];
    const uint64_t seed = g_seed[k][0] ? g_seed[k][0] : 1;
    for (int j = 0; j < 3; ++j) {
      Rig a;
      CHECK(a.start(seed, day), "start");
      World* w = const_cast<World*>(appWorld(a.app));
      w->weather.kind = (uint8_t)kKinds[j];
      int shown = 0;
      uint32_t t = 0;
      bool in_beat = false, pack_was = w->hiker.pack_on;
      for (; t + 1 < appLoopTicks(a.app); ++t) {
        if (!in_beat) pack_was = w->hiker.pack_on;
        appTick(a.app);
        if (beatIs(a.app, m.name)) {
          in_beat = true;
          shown += !pieceBounds(*w).empty();
        } else if (in_beat) {
          break;
        }
      }
      CHECK(in_beat, "the %s beat played", m.name);
      CHECK(shown == 0, "%s in %s: something was drawn on %d ticks", m.name, weatherKindName(kKinds[j]), shown);
      CHECK(appSpineOverruns(a.app) == 0 && appCritterCount(a.app) == 0, "%s in %s: overruns %d, critters %d", m.name, weatherKindName(kKinds[j]), appSpineOverruns(a.app),
            appCritterCount(a.app));
      char why[96];
      CHECK(atRest(*w, pack_was, why, sizeof why), "%s in %s: ends with %s", m.name, weatherKindName(kKinds[j]), why);
      a.stop();
    }
  }
}

// ---- [4] the doorbell -------------------------------------------------------------------------------------------------

// What the story keeps, as bytes: the live member of the union (the archery's hand positions follow his waving arm, so they are left out).
struct Snap {
  unsigned char bytes[sizeof(DayState)];
  size_t n;
  float mt;
};
static Snap snapOf(const World& w) {
  Snap s;
  memset(&s, 0, sizeof s);
  const DayState& d = w.day;
  switch (d.kind) {
    case DAY_BIKE: s.n = sizeof(DayBike); break;
    case DAY_ARCHERY: s.n = sizeof(DayArchery); break;
    case DAY_SLACK: s.n = sizeof(DaySlack); break;
    case DAY_ROPES: s.n = sizeof(DayRopes); break;
    default: s.n = 0; break;
  }
  if (s.n) memcpy(s.bytes, static_cast<const void*>(&d.kite), s.n);
  if (d.kind == DAY_ARCHERY) {   // the bow and string hands are his waving arm's, not the story's
    ArcheryState* a = reinterpret_cast<ArcheryState*>(s.bytes);
    a->bow_hand = a->string_hand = Vec2{0.f, 0.f};
  }
  s.mt = w.major.t;
  return s;
}

static void testFreeze() {
  printf("[4] the doorbell freezes the pieces and the beat\n");
  const Inputs day = makeInputs(3.f, SF_MIDDAY);
  int tried = 0, froze = 0;
  for (int k = 0; k < 4; ++k) {
    const Major& m = kMajorsNew[k];
    for (int var = 0; var < m.variants; ++var) {
      const uint64_t seed = g_seed[k][var];
      if (!seed) continue;
      int ok_here = 0;
      for (int at = 0; at < 8; ++at) {   // eight moments across the beat
        Rig r;
        CHECK(r.start(seed, day), "start");
        const World* w = appWorld(r.app);
        const uint32_t len = appLoopTicks(r.app);
        for (uint32_t t = 0; t < len && !beatIs(r.app, m.name); ++t) appTick(r.app);
        const uint32_t skip = 150u + 150u * (uint32_t)at + (uint32_t)(var * 37);
        for (uint32_t i = 0; i < skip && beatIs(r.app, m.name); ++i) appTick(r.app);
        if (!beatIs(r.app, m.name)) {
          r.stop();
          continue;
        }
        Event e;
        e.type = EV_DOORBELL;
        e.arg = 0;
        appPostEvent(r.app, e);
        appTick(r.app);
        if (!w->react.active) {   // he could not react just then (out of frame, or in a fade): nothing to freeze
          r.stop();
          continue;
        }
        ++tried;
        ++ok_here;
        const Snap s0 = snapOf(*w);
        bool still = true;
        int n = 0;
        while (w->react.active && n < 200) {
          appTick(r.app);
          ++n;
          if (w->react.active) {
            const Snap s1 = snapOf(*w);
            if (s0.n != s1.n || memcmp(s0.bytes, s1.bytes, s0.n) != 0 || s0.mt != s1.mt) still = false;
          }
        }
        CHECK(n >= 80 && n <= 90, "%s %c: the reaction lasted %d ticks", m.name, 'A' + var, n);
        CHECK(still, "%s %c seed %d moment %d: the piece or the beat's clock moved during the reaction", m.name, 'A' + var, (int)seed, at);
        froze += still;
        // Carried on: the clocks run again, and the beat ends clean.
        for (int i = 0; i < 40; ++i) appTick(r.app);
        const Snap s2 = snapOf(*w);
        CHECK(s0.n != s2.n || memcmp(s0.bytes, s2.bytes, s0.n) != 0 || s0.mt != s2.mt, "%s %c: nothing moved after the reaction", m.name, 'A' + var);
        for (int guard = 0; beatIs(r.app, m.name) && guard < 4000; ++guard) appTick(r.app);
        CHECK(appSpineOverruns(r.app) == 0 && appCritterCount(r.app) == 0, "%s %c: after the bell %d overruns, %d critters", m.name, 'A' + var,
              appSpineOverruns(r.app), appCritterCount(r.app));
        r.stop();
      }
      CHECK(ok_here >= 2, "%s %c: only %d bells found him able to react", m.name, 'A' + var, ok_here);
    }
  }
  printf("  %d bells rung into the beats, the pieces still through the whole reaction in %d\n", tried, froze);
  CHECK(froze >= 30, "only %d bells found him able to react", froze);
}

// ---- [1b] night, in real loops --------------------------------------------------------------------------------------------

static void testNight() {
  printf("[1b] night, in real loops\n");
  int seen = 0, loops = 0;
  const Inputs ins[3] = {makeInputs(3.f, SF_MIDDAY, true), makeInputs(0.2f, SF_MIDDAY), makeInputs(5.8f, SF_MIDDAY, false, true, 0)};
  for (const Inputs& in : ins)
    for (uint64_t seed = 1; seed <= 60; ++seed) {
      Rig r;
      r.start(seed, in);
      const LoopPlan& p = appWorld(r.app)->plan;
      for (const Major& m : kMajorsNew) seen += indexOf(p, m.id) >= 0;
      ++loops;
      r.stop();
    }
  printf("  %d night loops, %d with a bike, archery, slackline or ropes\n", loops, seen);
  CHECK(seen == 0, "%d night loops planned one", seen);
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed
  testPlan();
  testNight();
  testPlays();
  testSweep();
  testNoShow();
  testFreeze();
  printf("sizeof(App) = %u bytes (budget %d, wire limit 30000)\n", (unsigned)appAppBytes(), SS_APP_BYTES);
  CHECK(appAppBytes() <= 30000u, "App is %u bytes", (unsigned)appAppBytes());
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
