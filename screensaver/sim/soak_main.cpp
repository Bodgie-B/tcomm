// ss_soak: long-run soak test of the campsite screensaver.
//
// Drives the core through ss_app.h only, as the panel will: a simulated clock
// (starting 06:00) moves the mood, night mode, the lookout window, the weather
// and its tint, the other floor's status, the dogs and a doorbell, all
// generated from --seed; every tick is simulated and (every N ticks) rendered,
// and the invariants the unit tests check are checked on every tick and every
// loop. Violations are printed (one line each, first of a kind per loop) and
// counted; the run never stops on one. A hash of every frame of the first
// simulated hour is compared with a second run of that hour (determinism).
//
//   ss_soak [--hours H] [--seed S] [--every N] [--quiet]
//           [--max-lines N] [--slow-ms MS] [--no-det] [--help]
//
// Exit status: 0 clean, 1 violations or a determinism mismatch, 2 bad
// arguments, 3 hang (the watchdog thread saw no tick for 60 s).
#include "ss_app.h"
#include "scene/world.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

using namespace ss;
typedef std::chrono::steady_clock Clock;

// ---- constants (the tests' own) ---------------------------------------------------------

#define QUIET_X0 660   // the reserved corner (landscape.h LS_QUIET_X0 / _Y0), as the tests have it
#define QUIET_Y0 420
static const int kBuildTicks = 330;                                   // strip stats exclude the build-up, as the tests do
static const uint32_t kMaxLoopTicks = 15u * 60u * SS_TICK_HZ;         // a loop that runs past 15 min never ended
static const int64_t kStartSec = 6 * 3600;                            // the simulated clock starts at 06:00
static const uint64_t kHashHourTicks = 3600ull * SS_TICK_HZ;          // the determinism window
static const int kFamilies = SF_COUNT;
static const int kKinds = WX_COUNT;

// ---- small helpers --------------------------------------------------------------------------

static uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

// splitmix64: the host's own generator, one stream per input so changing one
// schedule never moves another. Not the core's.
struct HostRng {
  uint64_t s;
  HostRng(uint64_t seed = 0, uint64_t salt = 0) : s(mix64(seed * 0x9E3779B97F4A7C15ull + salt * 0xD1B54A32D192ED03ull + 1)) {}
  uint64_t next() {
    s += 0x9E3779B97F4A7C15ull;
    return mix64(s);
  }
  int range(int lo, int hi) { return lo + (int)(next() % (uint64_t)(hi - lo + 1)); }   // inclusive
  float unit() { return (float)(next() >> 40) * (1.0f / 16777216.0f); }
};

static const char* const kCodeNames[8] = {"clear", "rain", "snow", "wind", "heat", "fog", "chill", "storm"};   // Inputs::weather_code

static uint64_t hashFrame(uint64_t h, const Pixel* px, int strips) {
  // 8 bytes at a time, four independent lanes (about 4 GB/s).
  const size_t words = (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel) / 8;
  uint64_t l0 = h ^ 0x243F6A8885A308D3ull, l1 = h + 0x13198A2E03707344ull, l2 = h ^ 0xA4093822299F31D0ull, l3 = h + 0x082EFA98EC4E6C89ull;
  const unsigned char* p = (const unsigned char*)px;
  for (size_t i = 0; i + 4 <= words; i += 4) {
    uint64_t w0, w1, w2, w3;
    memcpy(&w0, p + (i + 0) * 8, 8);
    memcpy(&w1, p + (i + 1) * 8, 8);
    memcpy(&w2, p + (i + 2) * 8, 8);
    memcpy(&w3, p + (i + 3) * 8, 8);
    l0 = (l0 ^ w0) * 0x9E3779B97F4A7C15ull;
    l0 ^= l0 >> 32;
    l1 = (l1 ^ w1) * 0x9E3779B97F4A7C15ull;
    l1 ^= l1 >> 32;
    l2 = (l2 ^ w2) * 0x9E3779B97F4A7C15ull;
    l2 ^= l2 >> 32;
    l3 = (l3 ^ w3) * 0x9E3779B97F4A7C15ull;
    l3 ^= l3 >> 32;
  }
  return mix64(l0 ^ mix64(l1 ^ mix64(l2 ^ mix64(l3 ^ (uint64_t)strips))));
}

static bool isMajorBeat(const char* name) {
  for (int i = 0; i < MJ_COUNT; ++i)
    if (strcmp(name, majorName(i)) == 0) return true;
  return false;
}

// ---- the simulated host: clock, mood, weather, status, dogs, doorbell ---------------------

// Mood boundaries (minutes of the day) and the value after each: night 0 until
// 05:30, dawn 1, morning 2 (07:00), day 3 (10:00), golden 4 (16:00), dusk 5
// (18:30), night again (19:45, the value 6, which wraps to 0). Each step is
// spread over 20 to 30 minutes centred on its boundary, smoothstepped, the
// width drawn per day and boundary from the seed.
static const float kBoundMin[6] = {330.f, 420.f, 600.f, 960.f, 1110.f, 1185.f};

static float moodAt(uint64_t seed, int64_t abs_sec) {
  const int64_t day = abs_sec / 86400;
  const float m = (float)((double)(abs_sec % 86400) / 60.0);
  int period = 0;
  for (int i = 0; i < 6; ++i)
    if (m >= kBoundMin[i]) period = i + 1;
  float v = (float)period;
  for (int i = 0; i < 6; ++i) {
    HostRng r(seed, 1000 + (uint64_t)day * 8 + (uint64_t)i);
    const float w = 20.f + 10.f * r.unit();
    const float a = kBoundMin[i] - w * 0.5f;
    if (m >= a && m < a + w) {
      const float x = (m - a) / w;
      const float s = x * x * (3.f - 2.f * x);
      v = (float)i + s;
      break;
    }
  }
  return v >= 6.f ? v - 6.f : v;
}

struct Schedule {
  uint64_t seed;
  HostRng rw, rs, rd, rb, ro, rt;
  int64_t t_weather, t_status, t_dog, t_bell, t_outage;   // next change, simulated seconds
  bool outside = true;
  bool chip = true;   // Inputs::chip: the panel always draws the status chip and dog badges
  int code = 0, status = 0, dog = 0;
  float over = 0.f, fog = 0.f, over_t = 0.f, fog_t = 0.f;
  int bag[9];
  int bag_pos = 9;
  // Coverage, for the report.
  int64_t code_secs[8] = {}, outage_secs = 0, status_secs[6] = {}, dog_secs[4] = {};
  int n_weather = 0, n_outage = 0, n_status = 0, n_dog = 0, n_bell = 0;

  explicit Schedule(uint64_t sd)
      : seed(sd), rw(sd, 1), rs(sd, 2), rd(sd, 3), rb(sd, 4), ro(sd, 5), rt(sd, 6) {
    code = pickCode();
    status = rs.range(0, 5);
    setTarget();
    over = over_t;
    fog = fog_t;
    t_weather = rw.range(3600, 14400);
    t_status = rs.range(5 * 60, 90 * 60);
    t_dog = rd.range(10 * 60, 150 * 60);
    t_bell = rb.range(3600, 10800);
    t_outage = ro.range(20 * 60, 90 * 60);   // the first outage comes inside the first 90 minutes
  }

  // Every code once per bag of nine (clear twice), shuffled; never the same twice running.
  int pickCode() {
    static const int kBag[9] = {0, 1, 2, 3, 4, 5, 6, 7, 0};
    if (bag_pos >= 9) {
      const int last = n_weather > 0 ? code : -1;
      for (int i = 0; i < 9; ++i) bag[i] = kBag[i];
      for (int i = 8; i > 0; --i) {
        const int j = rw.range(0, i);
        const int t = bag[i];
        bag[i] = bag[j];
        bag[j] = t;
      }
      if (bag[0] == last) {
        const int t = bag[0];
        bag[0] = bag[8];
        bag[8] = t;
      }
      bag_pos = 0;
    }
    return bag[bag_pos++];
  }

  // What the code looks like: overcast and fog (WeatherGate would send these).
  void setTarget() {
    static const float kTint[8][2] = {{0.00f, 0.00f}, {0.75f, 0.15f}, {0.60f, 0.25f}, {0.25f, 0.00f},
                                      {0.00f, 0.05f}, {0.35f, 0.80f}, {0.45f, 0.10f}, {0.95f, 0.20f}};
    float o = kTint[code][0], f = kTint[code][1];
    if (o > 0.f) o += (rt.unit() - 0.5f) * 0.16f;
    if (f > 0.1f) f += (rt.unit() - 0.5f) * 0.10f;
    over_t = o < 0.f ? 0.f : (o > 1.f ? 1.f : o);
    fog_t = f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
  }

  static float toward(float v, float t, float step) { return v < t ? (t - v < step ? t : v + step) : (v - t < step ? t : v - step); }

  // Advance to simulated second `sec` (called once per second, in order). Fills
  // `ev` with the events to post at this tick; returns how many.
  int advance(int64_t sec, Event* ev) {
    int n = 0;
    while (sec >= t_weather) {
      code = pickCode();
      setTarget();
      ++n_weather;
      t_weather += rw.range(3600, 14400);   // every 1 to 4 hours
    }
    while (sec >= t_outage) {   // OUTSIDE goes quiet for 16 to 90 minutes (> the 15 the core waits), then returns
      outside = !outside;
      if (!outside) ++n_outage;
      t_outage = sec + (outside ? ro.range(90 * 60, 5 * 3600) : ro.range(16 * 60, 90 * 60));
    }
    while (sec >= t_status) {   // the other floor: every 5 to 90 minutes, any of the six, always a change
      int s = rs.range(0, 4);
      if (s >= status) ++s;
      status = s;
      ++n_status;
      t_status += rs.range(5 * 60, 90 * 60);
      ev[n].type = EV_STATUS;
      ev[n].arg = status;
      ++n;
    }
    while (sec >= t_dog) {   // dogs: out for 3 to 40 minutes (Lucy, Copper or both, maybe swapping), in for 10 to 150
      if (dog == 0) {
        dog = rd.range(1, 3);
        t_dog += rd.range(3 * 60, 40 * 60);
      } else if (rd.range(0, 3) == 0) {
        int d = rd.range(1, 2);
        if (d >= dog) ++d;   // another nonzero value than now: 1,2,3 minus the current
        dog = d;
        t_dog += rd.range(3 * 60, 40 * 60);
      } else {
        dog = 0;
        t_dog += rd.range(10 * 60, 150 * 60);
      }
      ++n_dog;
      ev[n].type = EV_DOG;
      ev[n].arg = dog;
      ++n;
    }
    while (sec >= t_bell) {   // a doorbell every 1 to 3 hours
      ev[n].type = EV_DOORBELL;
      ev[n].arg = 0;
      ++n;
      ++n_bell;
      t_bell += rb.range(3600, 10800);
    }
    // Tint: follows the code over about ten minutes; with no OUTSIDE reading there is none.
    const float ot = outside ? over_t : 0.f, ft = outside ? fog_t : 0.f;
    over = toward(over, ot, 1.f / 600.f);
    fog = toward(fog, ft, 1.f / 600.f);
    // Coverage.
    if (outside) code_secs[code]++;
    else outage_secs++;
    status_secs[status]++;
    dog_secs[dog]++;
    return n;
  }

  void fill(Inputs& in, int64_t sec) const {
    memset(&in, 0, sizeof in);   // a stray spine_force would force a family
    const int64_t abs_sec = kStartSec + sec;
    const int64_t tod = abs_sec % 86400;
    const bool night = tod >= 22 * 3600 || tod < 6 * 3600;
    in.mood = moodAt(seed, abs_sec);
    in.weather.overcast = over;
    in.weather.fog = fog;
    in.weather.night_dim = night ? 1.0f : 0.0f;
    in.weather_code = code;
    in.have_outside = outside;
    in.other_status = status;
    in.dog_out = dog;
    in.chip = chip;
    in.night_mode = night;
    in.lookout_window = tod >= 19 * 3600 && tod < 19 * 3600 + 45 * 60;
    in.spine_force = 0;
  }
};

// ---- the watchdog ---------------------------------------------------------------------------------

static std::atomic<uint64_t> g_progress(0);   // ticks completed, bumped every tick
static std::atomic<int> g_loop(0);
static std::atomic<uint64_t> g_loop_seed(0);
static std::atomic<int> g_family(0);

static void watchdog(double stall_s) {
  uint64_t last = ~0ull;
  Clock::time_point since = Clock::now();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const uint64_t p = g_progress.load(std::memory_order_relaxed);
    if (p != last) {
      last = p;
      since = Clock::now();
    } else if (std::chrono::duration<double>(Clock::now() - since).count() > stall_s) {
      fprintf(stdout, "HANG: no tick for %.0f s after tick=%llu loop=%d appLoopSeed=%llu family=%s\n", stall_s,
              (unsigned long long)p, g_loop.load(), (unsigned long long)g_loop_seed.load(), spineFamilyName(g_family.load()));
      fflush(stdout);
      _Exit(3);
    }
  }
}

// ---- violations ------------------------------------------------------------------------------------

enum VKind {
  V_STRIPS = 0,    // appRender outside 0..15
  V_FADE,          // appFade not finite or outside 0..1
  V_HIKER_RECT,    // appHikerRect true with a (non-empty) rect outside 0..800 x 0..480
  V_HIKER_ROWS,    // appHikerBounds not wholly inside 0..480 vertically
  V_HIKER_COLS,    // appHikerBounds out of frame sideways (the family's rule)
  V_HIKER_CORNER,  // appHikerBounds in the reserved corner
  V_CRIT_FRAME,    // a critter rect out of frame
  V_CRIT_CORNER,   // a critter rect in the reserved corner
  V_CRIT_OVER,     // a critter over the walking hiker (evening)
  V_OVERRUN,       // appSpineOverruns != 0 at the loop's end
  V_FORCED,        // appMinorForced != 0 at the loop's end
  V_LEN_RANGE,     // appLoopTicks outside the family's range
  V_LEN_MISMATCH,  // the rollover did not land on appLoopTicks
  V_LOOP_LONG,     // a loop running past 15 min
  V_SLOW,          // one tick plus render over --slow-ms
  V_DETERMINISM,   // the first hour's frame hash differs between two runs from the same seed
  V_COUNT
};
static const char* const kVName[V_COUNT] = {"strips_range", "fade_range",  "hiker_rect",     "hiker_rows",   "hiker_cols",
                                            "hiker_corner", "critter_frame", "critter_corner", "critter_over_hiker",
                                            "spine_overrun", "minor_forced", "loop_len_range", "loop_len_mismatch",
                                            "loop_over_15min", "slow_tick", "determinism"};

struct Cell {
  uint64_t frames = 0, sum = 0, pb_frames = 0, pb_sum = 0;
  int worst = 0, pb_worst = 0;
  uint32_t loops = 0;
};

// What one loop is, for the lines and the tables.
struct LoopInfo {
  int index = 0;
  uint64_t start_tick = 0;     // ticks completed when it started
  uint32_t expected = 0;       // appLoopTicks at its start
  uint64_t seed = 0;           // appLoopSeed
  int family = 0, kind = 0;    // spine family, weather kind the loop plays
  bool have_outside = true;
  int code = 0;
  float mood0 = 0, over0 = 0, fog0 = 0;
  bool night = false, lookout = false;
  char plan[320] = "";
  uint32_t printed = 0;        // violation kinds already printed for this loop
  bool long_flagged = false;
  uint32_t held = 0;           // ticks the story held still (the doorbell reaction): appTickCount did not advance
  // Sampled each tick; the values the loop ends with.
  int overruns = 0, forced = 0, rebuilds = 0;
  char last_beat[48] = "";
  uint64_t strips_sum = 0, strips_frames = 0;   // post-build
  float mood1 = 0, over1 = 0, fog1 = 0;
};

struct Stats {
  uint64_t vcount[V_COUNT] = {};
  uint32_t vloops[V_COUNT] = {};
  int vprinted[V_COUNT] = {};
  char vfirst[V_COUNT][700] = {};
  int loops_started = 0, loops_done = 0;
  int loops_fam[kFamilies] = {}, loops_kind[kKinds] = {}, loops_random_wx = 0;
  uint32_t len_min[kFamilies], len_max[kFamilies];
  Cell cell[kFamilies][kKinds];
  uint64_t frames = 0, ticks = 0;
  uint64_t strips_all = 0;
  int rebuild_max = 0;
  LoopInfo rebuild_loop;
  uint64_t rebuild_sum[kFamilies] = {}, rebuild_n[kFamilies] = {};
  int rebuild_fam_max[kFamilies] = {};
  uint64_t tick_ns_max = 0, tick_ns_sum = 0;
  // Tick plus render time by what the tick did: 0 an ordinary tick, 1 a palette rebuild (the cache is
  // redrawn), 2 a loop rollover (new geometry, cache rebuilt).
  uint64_t cls_n[3] = {}, cls_max_ns[3] = {}, cls_over10[3] = {}, cls_max_at[3] = {};
  uint64_t tick_max_at = 0;
  char tick_max_where[200] = "";
  uint64_t hist[26] = {};   // log2 of microseconds
  uint64_t held_ticks = 0, reactions = 0;
  uint32_t react_max = 0;
  uint64_t hiker_rect_empty = 0, hiker_rect_empty_out = 0, hiker_visible_ticks = 0, critter_ticks = 0;
  uint64_t events_posted = 0, events_failed = 0, ev_doorbell = 0, ev_status = 0, ev_dog = 0;
  Stats() {
    for (int f = 0; f < kFamilies; ++f) {
      len_min[f] = 0xFFFFFFFFu;
      len_max[f] = 0;
    }
  }
};

struct Config {
  double hours = 24.0;
  uint64_t seed = 1;
  int every = 1;
  bool quiet = false;
  int max_lines = 40;
  double slow_ms = 200.0;
  double hang_s = 60.0;   // the watchdog thread ends the run if no tick completes for this long (0: no watchdog)
  bool det = true;
  bool chip = true;             // Inputs::chip (--no-chip turns it off)
  bool det_same_fill = false;   // the rerun fills its memory as the first run did (tells nondeterminism from uninitialised reads)
};

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }

struct Soak {
  const Config& cfg;
  Stats& st;
  bool verbose;            // prints (violations, progress); false for the determinism pass
  uint64_t total_ticks;
  uint64_t hash_ticks;     // frames up to this tick are hashed
  // The app.
  HostAlloc alloc{};
  void* app_mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;
  Schedule sched;
  Inputs in{};
  uint64_t done = 0;       // ticks completed
  LoopInfo cur;
  // Determinism.
  uint64_t hash = 0x6A09E667F3BCC908ull;
  uint64_t hash_at_limit = 0;
  int loops_at_limit = 0;
  uint64_t strips_at_limit = 0;
  uint64_t strips_total_hashwin = 0;
  bool hash_done = false;
  // The tick being checked.
  const char* beat = "";
  uint32_t prev_tc = 0, held_run = 0;
  Clock::time_point wall0;

  unsigned char fill_byte;   // what the lent memory holds before the app starts (the rerun uses another)

  Soak(const Config& c, Stats& s, bool v, uint64_t total, uint64_t hticks, unsigned char fill)
      : cfg(c), st(s), verbose(v), total_ticks(total), hash_ticks(hticks), sched(c.seed), fill_byte(fill) {
    sched.chip = c.chip;
  }

  bool setup() {
    alloc.alloc_large = hostAlloc;
    alloc.free_large = hostFree;
    alloc.fast = malloc(SS_FAST_BYTES);   // lent as the panel lends LVGL's draw buffer
    app_mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)malloc((size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    if (!alloc.fast || !app_mem || !fb) return false;
    memset(alloc.fast, fill_byte, SS_FAST_BYTES);   // the board lends a buffer with old contents in it
    memset(app_mem, fill_byte, SS_APP_BYTES);
    memset(fb, fill_byte, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    sched.fill(in, 0);
    app = appStart(app_mem, alloc, cfg.seed, in);
    return app != nullptr;
  }

  void teardown() {
    if (app) appStop(app, alloc);
    app = nullptr;
    free(app_mem);
    free(fb);
    free(alloc.fast);
    app_mem = nullptr;
    fb = nullptr;
    alloc.fast = nullptr;
  }

  // ---- reporting ----
  void clockStr(uint64_t tick, char* buf, size_t n) const {
    const int64_t abs_sec = kStartSec + (int64_t)(tick / SS_TICK_HZ);
    snprintf(buf, n, "d%d %02d:%02d:%02d", (int)(abs_sec / 86400), (int)(abs_sec % 86400 / 3600), (int)(abs_sec % 3600 / 60),
             (int)(abs_sec % 60));
  }

  // One line: what failed, when, where, and the ss_preview flags that replay the loop's start.
  void violate(VKind k, const char* at_beat, const char* fmt, ...) {
    ++st.vcount[k];
    if (cur.printed & (1u << k)) return;
    cur.printed |= 1u << k;
    ++st.vloops[k];
    char detail[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    char clk[32];
    clockStr(done, clk, sizeof clk);
    const uint32_t looptick = (uint32_t)(done - cur.start_tick);
    char wx[48] = "";
    if (cur.have_outside) snprintf(wx, sizeof wx, " --weather %s", kCodeNames[cur.code]);
    char line[760];
    snprintf(line, sizeof line,
             "VIOLATION %s sim=%s tick=%llu loop=%d looptick=%u appLoopSeed=%llu family=%s beat=%s weather=%s%s: %s | "
             "preview: ss_preview --seed %llu --spine %s --mood %.3f%s --overcast %.2f --fog %.2f%s%s%s --skip %u --log-beats",
             kVName[k], clk, (unsigned long long)done, cur.index, looptick, (unsigned long long)cur.seed,
             spineFamilyName(cur.family), at_beat, weatherKindName(cur.kind), cur.have_outside ? "" : "(random)", detail,
             (unsigned long long)cur.seed, spineFamilyName(cur.family), (double)cur.mood0, wx, (double)cur.over0,
             (double)cur.fog0, cur.night ? " --night" : "", cur.lookout ? " --lookout" : "", cfg.chip ? " --chip" : "", looptick > 0 ? looptick - 1 : 0);
    if (st.vfirst[k][0] == 0) snprintf(st.vfirst[k], sizeof st.vfirst[k], "%s", line);
    if (verbose && st.vprinted[k] < cfg.max_lines) {
      ++st.vprinted[k];
      printf("%s\n", line);
      if (st.vprinted[k] == cfg.max_lines) printf("(further %s lines suppressed; counted in the summary)\n", kVName[k]);
      fflush(stdout);
    }
  }

  // ---- loop bookkeeping ----
  void startLoop(int index) {
    cur = LoopInfo{};
    cur.index = index;
    cur.start_tick = done;
    cur.expected = appLoopTicks(app);
    cur.seed = appLoopSeed(app);
    cur.family = appSpineFamily(app);
    cur.kind = appWorld(app)->weather.kind;
    cur.have_outside = in.have_outside;
    cur.code = in.weather_code;
    cur.mood0 = in.mood;
    cur.over0 = in.weather.overcast;
    cur.fog0 = in.weather.fog;
    cur.night = in.night_mode;
    cur.lookout = in.lookout_window;
    appLoopPlan(app, cur.plan, sizeof cur.plan);
    cur.overruns = appSpineOverruns(app);
    cur.forced = appMinorForced(app);
    cur.rebuilds = appPaletteRebuilds(app);
    snprintf(cur.last_beat, sizeof cur.last_beat, "%s", appBeatName(app));
    cur.mood1 = cur.mood0;
    cur.over1 = cur.over0;
    cur.fog1 = cur.fog0;
    ++st.loops_started;
    if (cur.family >= 0 && cur.family < kFamilies) ++st.loops_fam[cur.family];
    if (cur.kind >= 0 && cur.kind < kKinds) ++st.loops_kind[cur.kind];
    if (!cur.have_outside) ++st.loops_random_wx;
    if (cur.family >= 0 && cur.family < kFamilies && cur.kind >= 0 && cur.kind < kKinds) ++st.cell[cur.family][cur.kind].loops;
    g_loop.store(index);
    g_loop_seed.store(cur.seed);
    g_family.store(cur.family);
  }

  // The loop that just ended (its last sampled counters are in `cur`; `done` counts its last tick).
  void endLoop() {
    const uint64_t actual = done - cur.start_tick;
    const char* lb = cur.last_beat;
    if (cur.overruns != 0) violate(V_OVERRUN, lb, "%d spine beat(s) ran past their script | plan=%s", cur.overruns, cur.plan);
    if (cur.forced != 0) violate(V_FORCED, lb, "%d minor(s) had to be forced to end | plan=%s", cur.forced, cur.plan);
    const double secs = (double)cur.expected / SS_TICK_HZ;
    const bool look = cur.family == SF_LOOKOUT;
    const double lo = look ? 250.0 : 480.0, hi = look ? 320.0 : 660.0;   // test_lookout / the evening, morning, midday tests
    if (secs < lo || secs > hi) violate(V_LEN_RANGE, lb, "loop is %.1f s, want %.0f..%.0f", secs, lo, hi);
    // The doorbell reaction holds the story still (react.h), so a loop lasts appLoopTicks plus the ticks it held.
    if (actual != (uint64_t)cur.expected + cur.held)
      violate(V_LEN_MISMATCH, lb, "rolled over after %llu ticks, appLoopTicks said %u plus %u held ticks", (unsigned long long)actual,
              cur.expected, cur.held);
    // Stats.
    ++st.loops_done;
    if (cur.family >= 0 && cur.family < kFamilies) {
      if (actual < st.len_min[cur.family]) st.len_min[cur.family] = (uint32_t)actual;
      if (actual > st.len_max[cur.family]) st.len_max[cur.family] = (uint32_t)actual;
      st.rebuild_sum[cur.family] += (uint64_t)cur.rebuilds;
      ++st.rebuild_n[cur.family];
      if (cur.rebuilds > st.rebuild_fam_max[cur.family]) st.rebuild_fam_max[cur.family] = cur.rebuilds;
    }
    if (cur.rebuilds > st.rebuild_max) {
      st.rebuild_max = cur.rebuilds;
      st.rebuild_loop = cur;
    }
    if (verbose && !cfg.quiet) {
      char clk[32];
      clockStr(cur.start_tick, clk, sizeof clk);
      printf("loop %3d %s %-7s %-5s%s %5.0fs rebuilds=%2d strips(post-build) avg=%.2f mood %.2f->%.2f%s\n", cur.index, clk,
             spineFamilyName(cur.family), weatherKindName(cur.kind), cur.have_outside ? " " : "?",
             (double)actual / SS_TICK_HZ, cur.rebuilds,
             cur.strips_frames ? (double)cur.strips_sum / (double)cur.strips_frames : 0.0, (double)cur.mood0, (double)cur.mood1,
             cur.held ? " (doorbell reaction)" : "");
    }
  }

  // ---- per-tick checks ----
  static bool inCorner(const Rect& r) { return !r.empty() && r.x1 > QUIET_X0 && r.y1 > QUIET_Y0; }

  void checkHiker(const char* bn) {
    Rect rr;
    const bool vis = appHikerRect(app, &rr);
    if (vis) {
      ++st.hiker_visible_ticks;
      if (rr.empty()) {
        // ss_app.h: empty when he is out of frame; Rect::clipped leaves such a rect with
        // crossed corners, so only a non-empty rect has to lie inside the frame.
        ++st.hiker_rect_empty;
        if (rr.x0 < 0 || rr.y0 < 0 || rr.x1 > SS_WIDTH || rr.y1 > SS_HEIGHT) ++st.hiker_rect_empty_out;
      } else if (rr.x0 < 0 || rr.y0 < 0 || rr.x1 > SS_WIDTH || rr.y1 > SS_HEIGHT) {
        violate(V_HIKER_RECT, bn, "appHikerRect %d,%d,%d,%d is outside 0..%d x 0..%d", rr.x0, rr.y0, rr.x1, rr.y1, SS_WIDTH, SS_HEIGHT);
      }
    }
    Rect r;
    if (!appHikerBounds(app, &r)) return;
    // The tests' rules: wholly inside vertically; sideways wholly inside except where a
    // family lets him walk in or out (morning: leave, midday: return, lookout: approach) or
    // a major plays (partly in); the evening only needs him partly in. Never in the corner.
    bool strict = false;
    switch (cur.family) {
      case SF_MORNING: strict = strcmp(bn, "leave") != 0 && strcmp(bn, "day_hike") != 0 && !isMajorBeat(bn); break;   // both exits walk off by design
      case SF_MIDDAY: strict = strcmp(bn, "return") != 0 && !isMajorBeat(bn); break;
      case SF_LOOKOUT: strict = strcmp(bn, "approach") != 0; break;
      default: break;
    }
    const bool rows = r.y0 >= 0 && r.y1 <= SS_HEIGHT;
    const bool cols = strict ? (r.x0 >= 0 && r.x1 <= SS_WIDTH) : (r.x1 > 0 && r.x0 < SS_WIDTH);
    if (!rows) violate(V_HIKER_ROWS, bn, "hiker bounds %d,%d,%d,%d not inside 0..%d vertically", r.x0, r.y0, r.x1, r.y1, SS_HEIGHT);
    if (!cols)
      violate(V_HIKER_COLS, bn, "hiker bounds %d,%d,%d,%d out of frame sideways (%s rule)", r.x0, r.y0, r.x1, r.y1,
              strict ? "wholly in" : "partly in");
    if (inCorner(r)) violate(V_HIKER_CORNER, bn, "hiker bounds %d,%d,%d,%d in the reserved corner", r.x0, r.y0, r.x1, r.y1);
  }

  void checkCritters(const char* bn) {
    Rect hb;
    const bool hv = appHikerBounds(app, &hb);
    const bool walking = appHikerWalking(app);
    for (int i = 0; i < 8; ++i) {
      Rect r;
      int k = -1;
      if (!appCritterRect(app, i, &r, &k)) continue;
      ++st.critter_ticks;
      const bool in_frame = r.x1 > 0 && r.x0 < SS_WIDTH && r.y0 >= 0 && r.y1 <= SS_HEIGHT;
      if (!in_frame) violate(V_CRIT_FRAME, bn, "critter slot %d kind %d rect %d,%d,%d,%d out of frame", i, k, r.x0, r.y0, r.x1, r.y1);
      if (r.x1 > QUIET_X0 && r.y1 > QUIET_Y0)
        violate(V_CRIT_CORNER, bn, "critter slot %d kind %d rect %d,%d,%d,%d in the reserved corner", i, k, r.x0, r.y0, r.x1, r.y1);
      // test_scene's rule, for the evening spine it tests: never over the walking hiker.
      if (cur.family == SF_EVENING && hv && walking && r.x0 < hb.x1 && hb.x0 < r.x1 && r.y0 < hb.y1 && hb.y0 < r.y1)
        violate(V_CRIT_OVER, bn, "critter slot %d kind %d rect %d,%d,%d,%d over the walking hiker %d,%d,%d,%d", i, k, r.x0, r.y0,
                r.x1, r.y1, hb.x0, hb.y0, hb.x1, hb.y1);
    }
  }

  void recordTickTime(uint64_t ns, const char* bn, int cls) {
    st.tick_ns_sum += ns;
    ++st.cls_n[cls];
    if (ns > st.cls_max_ns[cls]) {
      st.cls_max_ns[cls] = ns;
      st.cls_max_at[cls] = done;
    }
    if (ns > 10000000ull) ++st.cls_over10[cls];
    const uint64_t us = ns / 1000;
    int b = 0;
    while (b < 25 && (us >> b) != 0) ++b;   // 0: under 1 us; b: [2^(b-1), 2^b) us
    ++st.hist[b];
    if (ns > st.tick_ns_max) {
      st.tick_ns_max = ns;
      st.tick_max_at = done;
      char clk[32];
      clockStr(done, clk, sizeof clk);
      snprintf(st.tick_max_where, sizeof st.tick_max_where, "tick %llu (%s) loop %d appLoopSeed %llu family %s beat %s",
               (unsigned long long)done, clk, cur.index, (unsigned long long)cur.seed, spineFamilyName(cur.family), bn);
    }
    if ((double)ns > cfg.slow_ms * 1e6)
      violate(V_SLOW, bn, "tick plus render took %.1f ms (limit %.0f ms)", (double)ns / 1e6, cfg.slow_ms);
  }

  // ---- the run ----
  bool run() {
    if (!setup()) {
      fprintf(stderr, "ss_soak: could not start the app (allocation failed)\n");
      return false;
    }
    wall0 = Clock::now();
    startLoop(0);
    Event ev[8];
    for (done = 0; done < total_ticks;) {
      // The host's second: inputs, then the events that go with them.
      if (done > 0 && done % SS_TICK_HZ == 0) {
        const int64_t sec = (int64_t)(done / SS_TICK_HZ);
        const int ne = sched.advance(sec, ev);
        sched.fill(in, sec);
        appSetInputs(app, in);
        cur.mood1 = in.mood;
        cur.over1 = in.weather.overcast;
        cur.fog1 = in.weather.fog;
        for (int i = 0; i < ne; ++i) {
          ++st.events_posted;
          if (ev[i].type == EV_DOORBELL) ++st.ev_doorbell;
          if (ev[i].type == EV_STATUS) ++st.ev_status;
          if (ev[i].type == EV_DOG) ++st.ev_dog;
          if (!appPostEvent(app, ev[i])) ++st.events_failed;
        }
      }

      const Clock::time_point t0 = Clock::now();
      const bool rolled = appTick(app);
      ++done;
      const bool due = (done % (uint64_t)cfg.every) == 0;
      int strips = 0;
      if (due) strips = appRender(app, surf);
      const Clock::time_point t1 = Clock::now();
      g_progress.store(done, std::memory_order_relaxed);
      const uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

      const int cls = rolled ? 2 : (appPaletteRebuilds(app) > cur.rebuilds ? 1 : 0);
      const uint32_t tc = appTickCount(app);
      const bool held = !rolled && tc == prev_tc;   // the reaction holds the story: the loop clock stands still
      prev_tc = tc;
      if (rolled) {
        endLoop();
        startLoop(cur.index + 1);
        held_run = 0;
      } else {
        if (held) {
          ++cur.held;
          ++st.held_ticks;
          if (++held_run == 1) ++st.reactions;
          if (held_run > st.react_max) st.react_max = held_run;
        } else {
          held_run = 0;
        }
        cur.overruns = appSpineOverruns(app);
        cur.forced = appMinorForced(app);
        cur.rebuilds = appPaletteRebuilds(app);
        snprintf(cur.last_beat, sizeof cur.last_beat, "%s", appBeatName(app));
      }
      beat = appBeatName(app);
      ++st.ticks;
      const uint32_t looptick = (uint32_t)(done - cur.start_tick);

      recordTickTime(ns, beat, cls);

      if (due) {
        ++st.frames;
        if (strips < 0 || strips > SS_STRIP_COUNT) violate(V_STRIPS, beat, "appRender returned %d, want 0..%d", strips, SS_STRIP_COUNT);
        st.strips_all += (uint64_t)(strips > 0 ? strips : 0);
        if (cur.family >= 0 && cur.family < kFamilies && cur.kind >= 0 && cur.kind < kKinds) {
          Cell& c = st.cell[cur.family][cur.kind];
          ++c.frames;
          c.sum += (uint64_t)strips;
          if (strips > c.worst) c.worst = strips;
          if (looptick >= (uint32_t)kBuildTicks) {
            ++c.pb_frames;
            c.pb_sum += (uint64_t)strips;
            if (strips > c.pb_worst) c.pb_worst = strips;
            cur.strips_sum += (uint64_t)strips;
            ++cur.strips_frames;
          }
        }
        if (done <= hash_ticks) {
          hash = hashFrame(hash, fb, strips);
          strips_total_hashwin += (uint64_t)strips;
        }
      }
      if (!hash_done && done >= hash_ticks) {
        hash_done = true;
        hash_at_limit = hash;
        loops_at_limit = st.loops_started;
        strips_at_limit = strips_total_hashwin;
      }

      const float fade = appFade(app);
      if (!(fade >= 0.f && fade <= 1.f) || !std::isfinite(fade)) violate(V_FADE, beat, "appFade is %g, want 0..1", (double)fade);
      checkHiker(beat);
      checkCritters(beat);
      if (looptick > kMaxLoopTicks && !cur.long_flagged) {
        cur.long_flagged = true;
        violate(V_LOOP_LONG, beat, "loop has run %u ticks (%.0f s) with no rollover; appLoopTicks says %u", looptick,
                (double)looptick / SS_TICK_HZ, cur.expected);
      }

      if (verbose && !cfg.quiet && done % kHashHourTicks == 0) {
        char clk[32];
        clockStr(done, clk, sizeof clk);
        const double wall = std::chrono::duration<double>(Clock::now() - wall0).count();
        uint64_t v = 0;
        for (int k = 0; k < V_COUNT; ++k) v += st.vcount[k];
        printf("[%s] %.1f h simulated, %d loops done, %llu violation ticks, wall %.1f s\n", clk, (double)done / (double)kHashHourTicks,
               st.loops_done, (unsigned long long)v, wall);
        fflush(stdout);
      }
    }
    if (!hash_done) {   // the run is shorter than the window
      hash_done = true;
      hash_at_limit = hash;
      loops_at_limit = st.loops_started;
      strips_at_limit = strips_total_hashwin;
    }
    teardown();
    return true;
  }
};

// ---- the summary ------------------------------------------------------------------------------------

static double percentileUs(const Stats& s, double q) {
  uint64_t total = 0;
  for (int i = 0; i < 26; ++i) total += s.hist[i];
  if (!total) return 0.0;
  const uint64_t want = (uint64_t)ceil(q * (double)total);
  uint64_t acc = 0;
  for (int i = 0; i < 26; ++i) {
    acc += s.hist[i];
    if (acc >= want) return i == 0 ? 1.0 : (double)(1ull << i);   // upper edge of the bucket
  }
  return 0.0;
}

static void usage(const char* prog, FILE* to) {
  fprintf(to,
          "usage: %s [options]\n"
          "  --hours H       simulated hours (default 24)\n"
          "  --seed S        first loop seed and the input schedules' seed (default 1)\n"
          "  --every N       render one frame every N ticks (default 1)\n"
          "  --quiet         no progress or per-loop lines (violations and the summary still print)\n"
          "  --max-lines N   violation lines printed per kind (default 40; the rest are only counted)\n"
          "  --slow-ms MS    a tick plus render over this many ms is a slow_tick violation (default 200)\n"
          "  --hang-s S      the watchdog ends the run (exit 3) if no tick completes for S seconds (default 60; 0 = off)\n"
          "  --no-chip       Inputs::chip off (default on, as the panel runs it)\n"
          "  --no-det        skip the second run of the first hour (determinism)\n"
          "  --help\n",
          prog);
}

[[noreturn]] static void bad(const char* prog, const char* msg, const char* arg) {
  fprintf(stderr, "%s: %s: %s\n", prog, msg, arg);
  usage(prog, stderr);
  exit(2);
}

static void printTables(const Stats& s) {
  printf("\nstrips written per frame, by spine family and the loop's weather kind\n");
  printf("(a) after the build-up (loop tick >= %d, as the tests count): avg/worst of 15\n", kBuildTicks);
  printf("%-9s", "");
  for (int k = 0; k < kKinds; ++k) printf("%10s", weatherKindName(k));
  printf("%10s\n", "all");
  uint64_t tsum = 0, tfr = 0, asum = 0, afr = 0;
  int tw = 0, aw = 0;
  for (int f = 0; f < kFamilies; ++f) {
    printf("%-9s", spineFamilyName(f));
    uint64_t fs = 0, ff = 0;
    int fw = 0;
    for (int k = 0; k < kKinds; ++k) {
      const Cell& c = s.cell[f][k];
      if (c.pb_frames) printf("%6.2f/%-3d", (double)c.pb_sum / (double)c.pb_frames, c.pb_worst);
      else printf("%10s", "-");
      fs += c.pb_sum;
      ff += c.pb_frames;
      if (c.pb_worst > fw) fw = c.pb_worst;
    }
    if (ff) printf("%6.2f/%-3d", (double)fs / (double)ff, fw);
    else printf("%10s", "-");
    printf("\n");
    tsum += fs;
    tfr += ff;
    if (fw > tw) tw = fw;
  }
  printf("%-9s", "all");
  for (int k = 0; k < kKinds; ++k) {
    uint64_t ks = 0, kf = 0;
    int kw = 0;
    for (int f = 0; f < kFamilies; ++f) {
      ks += s.cell[f][k].pb_sum;
      kf += s.cell[f][k].pb_frames;
      if (s.cell[f][k].pb_worst > kw) kw = s.cell[f][k].pb_worst;
    }
    if (kf) printf("%6.2f/%-3d", (double)ks / (double)kf, kw);
    else printf("%10s", "-");
  }
  if (tfr) printf("%6.2f/%-3d", (double)tsum / (double)tfr, tw);
  printf("\n");
  printf("(b) every frame, build-up included: avg/worst of 15\n");
  printf("%-9s", "");
  for (int k = 0; k < kKinds; ++k) printf("%10s", weatherKindName(k));
  printf("%10s\n", "all");
  for (int f = 0; f < kFamilies; ++f) {
    printf("%-9s", spineFamilyName(f));
    uint64_t fs = 0, ff = 0;
    int fw = 0;
    for (int k = 0; k < kKinds; ++k) {
      const Cell& c = s.cell[f][k];
      if (c.frames) printf("%6.2f/%-3d", (double)c.sum / (double)c.frames, c.worst);
      else printf("%10s", "-");
      fs += c.sum;
      ff += c.frames;
      if (c.worst > fw) fw = c.worst;
    }
    if (ff) printf("%6.2f/%-3d", (double)fs / (double)ff, fw);
    else printf("%10s", "-");
    printf("\n");
    asum += fs;
    afr += ff;
    if (fw > aw) aw = fw;
  }
  if (afr) printf("overall, build-up included: %.2f avg, %d worst; after the build-up: %.2f avg, %d worst (proposed budget: average under 8 of 15)\n",
                  (double)asum / (double)afr, aw, tfr ? (double)tsum / (double)tfr : 0.0, tw);
  printf("weather kind (Inputs::weather_code): clear(0) wind(3) rain(1) snow(2) fog(5) heat(4) chill(6) storm(7)\n");
  printf("loops per cell (family x weather kind)\n%-9s", "");
  for (int k = 0; k < kKinds; ++k) printf("%7s", weatherKindName(k));
  printf("\n");
  for (int f = 0; f < kFamilies; ++f) {
    printf("%-9s", spineFamilyName(f));
    for (int k = 0; k < kKinds; ++k) printf("%7u", s.cell[f][k].loops);
    printf("\n");
  }
}

int main(int argc, char** argv) {
  Config cfg;
  const char* prog = argc > 0 ? argv[0] : "ss_soak";
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto need = [&]() {
      if (i + 1 >= argc) bad(prog, "missing value for", a);
    };
    char* end = nullptr;
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
      usage(prog, stdout);
      return 0;
    } else if (!strcmp(a, "--hours")) {
      need();
      cfg.hours = strtod(argv[++i], &end);
      if (end == argv[i] || *end || !(cfg.hours > 0.0)) bad(prog, "bad number for", a);
    } else if (!strcmp(a, "--seed")) {
      need();
      cfg.seed = strtoull(argv[++i], &end, 10);
      if (end == argv[i] || *end) bad(prog, "bad integer for", a);
    } else if (!strcmp(a, "--every")) {
      need();
      cfg.every = (int)strtol(argv[++i], &end, 10);
      if (end == argv[i] || *end || cfg.every < 1) bad(prog, "must be an integer >= 1", a);
    } else if (!strcmp(a, "--max-lines")) {
      need();
      cfg.max_lines = (int)strtol(argv[++i], &end, 10);
      if (end == argv[i] || *end || cfg.max_lines < 0) bad(prog, "must be an integer >= 0", a);
    } else if (!strcmp(a, "--slow-ms")) {
      need();
      cfg.slow_ms = strtod(argv[++i], &end);
      if (end == argv[i] || *end || !(cfg.slow_ms > 0.0)) bad(prog, "bad number for", a);
    } else if (!strcmp(a, "--hang-s")) {
      need();
      cfg.hang_s = strtod(argv[++i], &end);
      if (end == argv[i] || *end || cfg.hang_s < 0.0) bad(prog, "bad number for", a);
    } else if (!strcmp(a, "--quiet")) {
      cfg.quiet = true;
    } else if (!strcmp(a, "--no-det")) {
      cfg.det = false;
    } else if (!strcmp(a, "--no-chip")) {
      cfg.chip = false;
    } else if (!strcmp(a, "--det-same-fill")) {
      cfg.det_same_fill = true;
    } else {
      bad(prog, "unknown argument", a);
    }
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);

  const uint64_t total = (uint64_t)(cfg.hours * 3600.0 * SS_TICK_HZ + 0.5);
  const uint64_t hash_ticks = total < kHashHourTicks ? total : kHashHourTicks;
  if (cfg.hang_s > 0.0) std::thread(watchdog, cfg.hang_s).detach();

  printf("ss_soak: %.2f h simulated from 06:00 (%llu ticks at %d Hz), seed %llu, render every %d tick(s)%s\n", cfg.hours,
         (unsigned long long)total, SS_TICK_HZ, (unsigned long long)cfg.seed, cfg.every, cfg.quiet ? ", quiet" : "");
  printf("inputs: mood by the clock (steps spread over 20-30 min), night_mode 22:00-06:00, lookout_window 19:00-19:45, weather code every 1-4 h with OUTSIDE outages, status every 5-90 min, dogs 0-3, doorbell every 1-3 h\n");

  Stats st;
  const Clock::time_point w0 = Clock::now();
  Soak main_run(cfg, st, true, total, hash_ticks, 0x00);
  if (!main_run.run()) return 2;
  const double wall_main = std::chrono::duration<double>(Clock::now() - w0).count();

  // Determinism: the first simulated hour again from the same seed. The rerun starts with
  // different bytes in the memory it is lent (the board lends a buffer with old contents),
  // so a read of uninitialised memory shows up as a mismatch too.
  bool det_ran = false, det_same = false;
  double wall_det = 0.0;
  uint64_t det_hash = 0, det_strips = 0;
  int det_loops = 0;
  Stats st2;
  if (cfg.det) {
    const Clock::time_point d0 = Clock::now();
    Soak again(cfg, st2, false, hash_ticks, hash_ticks, cfg.det_same_fill ? 0x00 : 0xA5);
    if (again.run()) {
      det_ran = true;
      det_hash = again.hash_at_limit;
      det_loops = again.loops_at_limit;
      det_strips = again.strips_at_limit;
      det_same = again.hash_at_limit == main_run.hash_at_limit && again.loops_at_limit == main_run.loops_at_limit &&
                 again.strips_at_limit == main_run.strips_at_limit;
    }
    wall_det = std::chrono::duration<double>(Clock::now() - d0).count();
    if (det_ran && !det_same) {
      ++st.vcount[V_DETERMINISM];
      st.vloops[V_DETERMINISM] = 1;
      snprintf(st.vfirst[V_DETERMINISM], sizeof st.vfirst[V_DETERMINISM],
               "VIOLATION determinism: first %.2f h from seed %llu: frame hash %016llx vs %016llx on the rerun | "
               "ss_soak --seed %llu --hours %.2f --det-same-fill separates uninitialised memory from real nondeterminism",
               (double)hash_ticks / (double)kHashHourTicks, (unsigned long long)cfg.seed,
               (unsigned long long)main_run.hash_at_limit, (unsigned long long)det_hash, (unsigned long long)cfg.seed, cfg.hours);
      printf("%s\n", st.vfirst[V_DETERMINISM]);
    }
  }

  // ---- summary ----
  printf("\n==== ss_soak summary ====\n");
  printf("simulated %.2f h (%llu ticks), %llu frames rendered, wall %.1f s (%.1f s with the determinism rerun of %.2f h: %.1f s)\n",
         (double)st.ticks / (double)kHashHourTicks, (unsigned long long)st.ticks, (unsigned long long)st.frames, wall_main,
         wall_main + wall_det, (double)hash_ticks / (double)kHashHourTicks, wall_det);
  printf("loops: %d completed (%d started)\n", st.loops_done, st.loops_started);
  printf("  by family:");
  for (int f = 0; f < kFamilies; ++f) printf(" %s=%d", spineFamilyName(f), st.loops_fam[f]);
  printf("\n  by the weather kind the loop played:");
  for (int k = 0; k < kKinds; ++k) printf(" %s=%d", weatherKindName(k), st.loops_kind[k]);
  printf("\n  %d loops started with have_outside false (the core picks the weather)\n", st.loops_random_wx);
  printf("  loop length, ticks to rollover:");
  for (int f = 0; f < kFamilies; ++f) {
    if (st.len_max[f]) printf(" %s=%.0f..%.0fs", spineFamilyName(f), (double)st.len_min[f] / SS_TICK_HZ, (double)st.len_max[f] / SS_TICK_HZ);
  }
  printf("\n");
  const Schedule& sc = main_run.sched;
  printf("inputs seen: weather changes %d, OUTSIDE outages %d (%.1f h without data), status changes %d, dog changes %d, doorbells %d\n",
         sc.n_weather, sc.n_outage, (double)sc.outage_secs / 3600.0, sc.n_status, sc.n_dog, sc.n_bell);
  printf("  hours per weather code (OUTSIDE up):");
  for (int c = 0; c < 8; ++c) printf(" %s=%.1f", kCodeNames[c], (double)sc.code_secs[c] / 3600.0);
  printf("\n  hours per other_status:");
  for (int c = 0; c < 6; ++c) printf(" %d=%.1f", c, (double)sc.status_secs[c] / 3600.0);
  printf("   hours per dog_out:");
  for (int c = 0; c < 4; ++c) printf(" %d=%.1f", c, (double)sc.dog_secs[c] / 3600.0);
  printf("\n  events posted %llu (doorbell %llu, status %llu, dog %llu), refused by a full queue %llu\n",
         (unsigned long long)st.events_posted, (unsigned long long)st.ev_doorbell, (unsigned long long)st.ev_status,
         (unsigned long long)st.ev_dog, (unsigned long long)st.events_failed);
  printf("  doorbell reactions (ticks the story held still, appTickCount not advancing): %llu runs, %llu ticks in all, longest %u ticks (%.2f s)\n",
         (unsigned long long)st.reactions, (unsigned long long)st.held_ticks, st.react_max, (double)st.react_max / SS_TICK_HZ);
  printf("  hiker visible on %llu ticks (%llu with an empty appHikerRect, %llu of those with coordinates outside the frame: not counted, ss_app.h says empty when out of frame); critter rect samples %llu\n",
         (unsigned long long)st.hiker_visible_ticks, (unsigned long long)st.hiker_rect_empty,
         (unsigned long long)st.hiker_rect_empty_out, (unsigned long long)st.critter_ticks);

  printf("\nviolations (ticks that broke the rule; loops that had at least one):\n");
  uint64_t vtotal = 0;
  for (int k = 0; k < V_COUNT; ++k) {
    vtotal += st.vcount[k];
    printf("  %-20s %10llu ticks %6u loops\n", kVName[k], (unsigned long long)st.vcount[k], st.vloops[k]);
  }
  if (!vtotal) printf("  none\n");
  else {
    printf("first line of each kind:\n");
    for (int k = 0; k < V_COUNT; ++k)
      if (st.vfirst[k][0]) printf("  %s\n", st.vfirst[k]);
  }

  printf("\ndeterminism: ");
  if (!cfg.det) printf("not run (--no-det)\n");
  else if (!det_ran) printf("rerun failed to start\n");
  else
    printf("%s: first %.2f h, %llu frames hashed, hash %016llx vs rerun %016llx; loops started %d vs %d; strips written %llu vs %llu; memory fill 0x00 vs 0x%02X\n",
           det_same ? "PASS" : "FAIL", (double)hash_ticks / (double)kHashHourTicks, (unsigned long long)(hash_ticks / (uint64_t)cfg.every),
           (unsigned long long)main_run.hash_at_limit, (unsigned long long)det_hash, main_run.loops_at_limit, det_loops,
           (unsigned long long)main_run.strips_at_limit, (unsigned long long)det_strips, cfg.det_same_fill ? 0x00u : 0xA5u);

  printf("\npalette rebuilds in one loop (the loop's own start counts): most %d", st.rebuild_max);
  if (st.rebuild_max) {
    const LoopInfo& L = st.rebuild_loop;
    char clk[32];
    main_run.clockStr(L.start_tick, clk, sizeof clk);
    printf(" in loop %d (%s, %s, weather %s%s, appLoopSeed %llu, mood %.2f->%.2f, overcast %.2f->%.2f, fog %.2f->%.2f)", L.index, clk,
           spineFamilyName(L.family), weatherKindName(L.kind), L.have_outside ? "" : " random", (unsigned long long)L.seed,
           (double)L.mood0, (double)L.mood1, (double)L.over0, (double)L.over1, (double)L.fog0, (double)L.fog1);
  }
  printf("\n  per family (mean/max):");
  for (int f = 0; f < kFamilies; ++f)
    if (st.rebuild_n[f]) printf(" %s=%.1f/%d", spineFamilyName(f), (double)st.rebuild_sum[f] / (double)st.rebuild_n[f], st.rebuild_fam_max[f]);
  printf("\n");

  printTables(st);

  printf("\ntick plus render wall time (appTick + appRender when due, hashing and checks excluded): mean %.1f us, p50 <= %.0f us, p99 <= %.0f us, p99.9 <= %.0f us, p99.99 <= %.0f us\n",
         st.ticks ? (double)st.tick_ns_sum / (double)st.ticks / 1000.0 : 0.0, percentileUs(st, 0.5), percentileUs(st, 0.99),
         percentileUs(st, 0.999), percentileUs(st, 0.9999));
  printf("  worst single tick: %.3f ms at %s\n", (double)st.tick_ns_max / 1e6, st.tick_max_where);
  static const char* const kCls[3] = {"ordinary ticks", "palette-rebuild ticks", "loop-rollover ticks"};
  for (int c = 0; c < 3; ++c) {
    char clk[32];
    main_run.clockStr(st.cls_max_at[c], clk, sizeof clk);
    printf("  %-22s %9llu ticks, worst %.3f ms (tick %llu, %s), %llu over 10 ms\n", kCls[c], (unsigned long long)st.cls_n[c],
           (double)st.cls_max_ns[c] / 1e6, (unsigned long long)st.cls_max_at[c], clk, (unsigned long long)st.cls_over10[c]);
  }

  const bool ok = vtotal == 0 && (!cfg.det || (det_ran && det_same));
  printf("\nRESULT: %s (violation ticks %llu, determinism %s)\n", ok ? "CLEAN" : "NOT CLEAN", (unsigned long long)vtotal,
         !cfg.det ? "not run" : (det_same ? "PASS" : "FAIL"));
  return ok ? 0 : 1;
}
