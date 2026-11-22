// Tests for the morning Tai Chi as a surprise major (core/scene/day_taichi.cpp over taichi.cpp; TAICHI_PLAN.md
// section 11.2 with INTEGRATION_PLAN.md and REVIEW_FIXES F8, F27; Mike, 2026-11-18). The module itself is
// test_taichi.cpp; the plan block is test_plan_block.cpp.
//
//   [1] the plan in real runs: tai chi in the first morning loop of a run, after the wake, 1 to 3 majors in
//       its loop, every loop 480 to 660 s; the variant byte (chapter from the memory, gags, guests); the seconds
//   [2] chapters: with the carry word naming chapter 0..3 that chapter plays and the memory holds the next
//       one after, with "done this morning" set; a no-show (a rain the plan could not know of) leaves the
//       chapter alone, still counts as the morning's tai chi, and ends clean
//   [3] whole beats, each chapter with no gag, the wobble and the itch, guests on and off, and with Lucy,
//       Copper and both out: no overrun, the script done with 3 s to spare, him in frame, nothing of his, the
//       hat's, a guest's, the heron's or the butterfly's in the chip's box (x 538..796, y 412..476), strips,
//       drawn tick by tick it equals a full render; the fog's steps only in a hold of 2 s or more (F8)
//   [4] the end state: seated at camp.sit, hat on and not also on the grass, hands empty, foot_turn 0, the
//       breath 1 and 1, the day union zero, no guest left, the heron not a guest, no dog lent
//   [5] the doorbell mid-step, on the rooster's leg, in a turn's ramp, inside each gag and with the bird on
//       his hand: for the whole reaction no joint moves but the head, his facing and feet stay, the bird
//       stays on the hand; then the beat ends clean
//   [6] the guests: the heron mirrors the crane; the bird's feet within 2 px of the hook hand while it sits;
//       the chipmunk up, down within 1.5 s of the snake, up again for the rooster; the deer never near;
//       with a dog out none comes; a dog going out mid-form sends them off
//   [7] Lucy lies at her watch spot; Copper's bump plays and he is given back; a Copper who goes in before
//       the cue: no bump, a clean end
//   [8] two runs draw the same frames; the numbers
//
// With SS_S22 off tai chi never plans: only that is checked, and the pure functions.
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/day.h"
#include "scene/day_taichi.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/memory.h"
#include "scene/spine_family.h"
#include "scene/stage22_switch.h"
#include "scene/stream.h"
#include "scene/taichi.h"
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

// A dry morning the OUTSIDE data vouches for (code 0), tai chi's chapter in the carry word, a month for the butterfly.
static Inputs morning(int chapter, int dog = 0, bool outside = true) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 1.5f;
  in.spine_force = (uint8_t)(1 + SF_MORNING);
  in.have_outside = outside;
  in.weather_code = 0;
  in.dog_out = dog;
  in.month = 6;
  if (chapter >= 0) in.carry = (uint16_t)((0x80u | (unsigned)chapter) << 8);
  return in;
}
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
  for (int i = 0; i < SS_WIDTH * SS_HEIGHT; i += 7) h = (h ^ fb[i]) * 16777619u;
  return h;
}
static float dist(Vec2 a, Vec2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

// A seed whose first morning loop plays this chapter with this gag (TaiGag, as planned) and guests bit; 0: none found.
static uint64_t findSeed(int chapter, int gag, int guests, int dog, uint64_t from = 1) {
  const Inputs in = morning(chapter, dog);
  for (uint64_t seed = from; seed <= from + 900; ++seed) {
    Rig r;
    if (!r.start(seed, in)) return 0;
    const World* w = appWorld(r.app);
    const int i = indexOf(w->plan, MJ_TAICHI);
    const uint8_t v = i >= 0 ? w->plan.major_var[i] : 0;
    const bool hit = i >= 0 && (v & 3) == chapter && ((v >> 4) & 3) == gag && (guests < 0 || ((v & 64) != 0) == (guests != 0));
    r.stop();
    if (hit) return seed;
  }
  return 0;
}

// ---- one beat, watched ---------------------------------------------------------------------------------------

struct Opt {
  int bell_at = -1;      // beat tick of a doorbell
  int dog_at = -1;       // beat tick at which Inputs::dog_out becomes dog_val
  int dog_val = 0;
  int rain = 0;          // poke this weather kind before the beat (a rain the plan could not know of)
  bool check_draw = true;
  bool quiet = false;
};

struct Out {
  bool ran, ended, end_ok, noshow;
  char why[96];
  int beat_ticks, done_at, overruns, critters;
  long strips_sum;
  int strips_n, strips_max, diffs, checks;
  int bad_hiker, bad_box, hidden;
  uint32_t hash;
  uint8_t var, chapter;
  uint16_t carry_after;
  // the form
  int form_ticks, gag_ticks, gag_kind, bump_ticks, fog_steps_in_form, fog_bad, fly_ticks;
  // the guests
  int guest_ticks, heron_ticks, bird_sit, chip_up_early, chip_down_ok, chip_up_rooster, deer_ticks, leaving_by;
  float mirror_max, bird_off, deer_min;
  // the dogs
  int lucy_first, lucy_bad, lent_end, copper_sat;
  // moments for the doorbell runs (beat ticks; -1 never)
  int t_step, t_rooster, t_turn, t_gag, t_bird;
  // the doorbell
  int bell_ticks, bell_moved, bell_faced, bell_bird;
  float bell_head;
};

static bool atRest(const World& w, char* why, size_t n) {
  const Hiker& h = w.hiker;
  const Item& hat = w.items[ITEM_HAT];
  const char* bad = nullptr;
  if (!h.hat_on) bad = "hat off";
  else if (hat.held || hat.down) bad = "the hat is also held or on the grass";
  else if (h.carry != CARRY_NONE) bad = "carrying";
  else if (h.walking) bad = "walking";
  else if (w.fx.arm_w[0] != 0.f || w.fx.arm_w[1] != 0.f || w.fx.lean != 0.f || fabsf(w.fx.head) > 1.0e-6f || w.fx.lift != 0.f) bad = "arms or lean still set";
  else if (w.day.kind != DAY_NONE) bad = "the daytime union is not zero";
  else if (h.target != POSE_SIT) bad = "not sitting";
  else if (dist(h.feet, w.spine.camp.sit) > 3.f) bad = "not at camp.sit";
  else if (h.foot_turn != 0.f) bad = "foot_turn not 0";
  else if (h.breath_rate != 1.f || h.breath_scale != 1.f) bad = "his breathing is not back to 1 and 1";
  else if (streamHeronIsGuest(w)) bad = "the heron is still his guest";
  else if (w.dogs.lent != 0) bad = "a dog is still lent";
  else if (!inFrame(w.hiker_rect) || w.hiker_rect.empty()) bad = "not in frame";
  if (bad) snprintf(why, n, "%s", bad);
  return bad == nullptr;
}

static Out play(uint64_t seed, Inputs in, const Opt& o, const char* tag) {
  Out e;
  memset(&e, 0, sizeof e);
  e.hash = 2166136261u;
  e.deer_min = 1.0e9f;
  e.lucy_first = e.t_step = e.t_rooster = e.t_turn = e.t_gag = e.t_bird = e.leaving_by = -1;
  Rig a, b;
  const bool ok = a.start(seed, in) && (!o.check_draw || b.start(seed, in));
  CHECK(ok, "%s: start", tag);
  if (!ok) return e;
  World* w = const_cast<World*>(appWorld(a.app));
  const uint32_t len = appLoopTicks(a.app);
  const int pi = indexOf(w->plan, MJ_TAICHI);
  CHECK(pi >= 0, "%s: tai chi is not in the plan", tag);
  if (pi < 0) {
    a.stop();
    if (o.check_draw) b.stop();
    return e;
  }
  e.ran = true;
  e.var = w->plan.major_var[pi];
  e.chapter = (uint8_t)(e.var & 3);
  if (o.rain) w->weather.kind = (uint8_t)o.rain;
  bool in_beat = false;
  int n_in = 0;
  float fog_prev = w->fog_extra;
  HikerPose bell_pose{};
  Vec2 bell_feet{}, bell_bird{};
  int bell_face = 0;
  bool bell_on = false, bell_has_bird = false;
  int dog_tick = -1;
  for (uint32_t t = 0; t + 1 < len; ++t) {
    if (in_beat && o.bell_at >= 0 && n_in == o.bell_at) {
      Event ev;
      ev.type = EV_DOORBELL;
      ev.arg = 0;
      appPostEvent(a.app, ev);
      if (o.check_draw) appPostEvent(b.app, ev);
    }
    if (in_beat && o.dog_at >= 0 && n_in == o.dog_at) {
      in.dog_out = o.dog_val;
      appSetInputs(a.app, in);
      if (o.check_draw) appSetInputs(b.app, in);
      dog_tick = n_in;
    }
    appTick(a.app);
    if (o.check_draw) appTick(b.app);
    const bool now = beatIs(a.app, "taichi");
    if (!now && !in_beat) {
      if (t % 250 == 0) appRender(a.app, a.surf);
      fog_prev = w->fog_extra;
      continue;
    }
    if (!now && in_beat) {
      e.ended = true;
      e.beat_ticks = (int)w->spine.run.beat_ticks;
      e.done_at = (int)w->spine.run.done_at;
      e.overruns = appSpineOverruns(a.app);
      e.critters = appCritterCount(a.app) - ((in.dog_out & 1) ? 0 : 0);
      e.end_ok = atRest(*w, e.why, sizeof e.why);
      e.lent_end = w->dogs.lent;
      e.carry_after = appCarry(a.app);
      break;
    }
    in_beat = true;
    ++n_in;
    const MajorState& m = w->major;
    e.noshow = (m.var & DAY_NOSHOW) != 0;
    const int strips = appRender(a.app, a.surf);
    e.strips_sum += strips;
    ++e.strips_n;
    e.strips_max = strips > e.strips_max ? strips : e.strips_max;
    if (n_in % 9 == 0) e.hash = (e.hash ^ hashFrame(a.fb)) * 16777619u;
    if (o.check_draw && n_in % 23 == 0) {
      appInvalidate(b.app);
      appRender(b.app, b.surf);
      ++e.checks;
      if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
        if (e.diffs < 3) {
          long k = 0;
          while (a.fb[k] == b.fb[k]) ++k;
          printf("  %s beat tick %d: incremental differs from full at (%ld,%ld)\n", tag, n_in, k % SS_WIDTH, k / SS_WIDTH);
        }
        ++e.diffs;
      }
    }
    Rect hr;
    if (appHikerBounds(a.app, &hr)) {
      if (!inFrame(hr)) ++e.bad_hiker;
      if (inChipBox(hr)) ++e.bad_box;
    } else {
      ++e.hidden;
    }
    const Item& hat = w->items[ITEM_HAT];
    if ((hat.held || hat.down) && inChipBox(itemBounds(ITEM_HAT, hat))) ++e.bad_box;
    if (inChipBox(streamVisitBounds(*w))) ++e.bad_box;
    if (inChipBox(dayBounds(w->day))) ++e.bad_box;
    const bool tai = w->day.kind == DAY_TAICHI;
    const DayTaichi& d = w->day.taichi;
    const TaichiState& s = d.t;
    const bool form = taichiInForm(*w);
    // the guest's critter
    const int gs = m.c[0];
    const bool guest = gs >= 0 && w->crit[gs].in_use && w->crit[gs].tag == CT_MAJOR;
    if (guest) {
      ++e.guest_ticks;
      const Critter& c = w->critters[gs];
      if (c.visible && inChipBox(critterBounds(c))) ++e.bad_box;
      if (tai && s.chapter == 1 && m.stage[0] == 2) {   // the bird sits: its feet on the hook hand
        ++e.bird_sit;
        const float f = s.facing < 0 ? -1.f : 1.f;
        const Vec2 hk = (s.hand[0].x - s.hand[1].x) * f <= 0.f ? s.hand[0] : s.hand[1];
        const float off = dist(c.feet, Vec2{hk.x, hk.y - 2.f});
        if (off > e.bird_off) e.bird_off = off;
        if (e.t_bird < 0 && m.st[0] > 1.2f) e.t_bird = n_in;
      }
      if (tai && s.chapter == 2 && form) {
        const float snake = taichiKeyArrive(2, 15), rooster = taichiKeyArrive(2, 17);
        if (s.phase == TP_FORM && s.t > snake - 6.f && s.t < snake - 5.f && c.action == CA_SIT) ++e.chip_up_early;
        if (s.phase == TP_FORM && s.t > snake + 1.5f && s.t < snake + 2.5f && c.action == CA_IDLE) ++e.chip_down_ok;
        if (s.phase == TP_FORM && s.t > rooster + 2.f && s.t < rooster + 3.f && (c.action == CA_SIT || c.action == CA_LOOK)) ++e.chip_up_rooster;
      }
      if (tai && s.chapter == 3) {
        ++e.deer_ticks;
        const float dd = dist(c.feet, w->hiker.feet);
        if (dd < e.deer_min) e.deer_min = dd;
      }
    }
    if (streamHeronIsGuest(*w)) {
      ++e.heron_ticks;
      const float mm = streamHeronMirrorNow(*w);
      if (mm > e.mirror_max) e.mirror_max = mm;
    }
    if (dog_tick >= 0 && e.leaving_by < 0) {   // a dog went out: every guest gone or going
      const bool heron_gone = !streamHeronIsGuest(*w);
      const bool going = !guest || (s.chapter == 1 && m.stage[0] >= 3) || (s.chapter == 2 && m.stage[0] >= 5) || (s.chapter == 3 && m.stage[0] >= 6);
      if (heron_gone && going) e.leaving_by = n_in - dog_tick;
    }
    if (form) {
      ++e.form_ticks;
      if (s.phase == TP_GAG) {
        ++e.gag_ticks;
        e.gag_kind = s.gag_on;
        if (s.gag_on == TG_BUMP) ++e.bump_ticks;
        if (e.t_gag < 0 && s.gag_t > 1.0f) e.t_gag = n_in;
      }
      if (w->fog_extra != fog_prev) {
        ++e.fog_steps_in_form;
        if (!taichiHolding(s, 2.0f)) ++e.fog_bad;
      }
      if (taichiFlyShown(s)) ++e.fly_ticks;
      if (e.t_turn < 0 && s.foot_turn > 0.3f && s.foot_turn < 0.7f) e.t_turn = n_in;
      if (e.t_step < 0 && s.chapter == 0 && s.phase == TP_FORM && s.t > taichiKeyHoldEnd(0, 3) + 2.6f) e.t_step = n_in;
      if (e.t_rooster < 0 && s.chapter == 2 && s.phase == TP_FORM && s.t > taichiKeyArrive(2, 17) + 1.0f) e.t_rooster = n_in;
    }
    fog_prev = w->fog_extra;
    // the dogs
    const int ls = CRITTER_STORY + HD_LUCY, cs = CRITTER_STORY + HD_COPPER;
    Vec2 ws;
    if (taichiWatchSpot(*w, ws) && w->crit[ls].in_use) {
      const Critter& lc = w->critters[ls];
      const bool there = dist(lc.feet, ws) <= 6.f && lc.action == CA_LIE;
      if (there && e.lucy_first < 0) e.lucy_first = n_in;
      if (!there && e.lucy_first >= 0) ++e.lucy_bad;
    }
    if (tai && d.sat && w->crit[cs].in_use && w->critters[cs].action == CA_SIT) ++e.copper_sat;
    // the doorbell: nothing of him moves but his head
    if (w->react.active) {
      const Hiker& h = w->hiker;
      if (!bell_on) {
        bell_on = true;
        bell_pose = h.pose;
        bell_feet = h.feet;
        bell_face = h.facing;
        bell_has_bird = guest && s.chapter == 1 && m.stage[0] == 2;
        if (bell_has_bird) bell_bird = w->critters[gs].feet;
      } else {
        ++e.bell_ticks;
        bool moved = h.feet.x != bell_feet.x || h.feet.y != bell_feet.y;
        for (int i = 0; i < 2; ++i)
          moved = moved || h.pose.thigh[i] != bell_pose.thigh[i] || h.pose.knee[i] != bell_pose.knee[i] ||
                  h.pose.shoulder[i] != bell_pose.shoulder[i] || h.pose.elbow[i] != bell_pose.elbow[i];
        if (moved) ++e.bell_moved;
        if (h.facing != bell_face) ++e.bell_faced;
        const float dh = fabsf(h.pose.head - bell_pose.head);
        if (dh > e.bell_head) e.bell_head = dh;
        if (bell_has_bird && (!guest || dist(w->critters[gs].feet, bell_bird) > 0.01f)) ++e.bell_bird;
      }
    }
  }
  CHECK(e.ended, "%s: the beat never ended", tag);
  CHECK(e.bad_hiker == 0, "%s: %d ticks with him out of frame", tag, e.bad_hiker);
  CHECK(e.hidden == 0, "%s: %d ticks with him out of the scene", tag, e.hidden);
  CHECK(e.bad_box == 0, "%s: %d samples in the chip's box (x 538..796, y 412..476)", tag, e.bad_box);
  CHECK(e.overruns == 0, "%s: %d overruns", tag, e.overruns);
  CHECK(e.critters == 0 || in.dog_out != 0 || o.dog_at >= 0, "%s: %d critters left", tag, e.critters);
  CHECK(e.done_at > 0 && e.beat_ticks - e.done_at >= 75, "%s: script done at %d of %d ticks (wants 3 s spare)", tag, e.done_at, e.beat_ticks);
  CHECK(e.end_ok, "%s: ends with %s", tag, e.why);
  CHECK(e.diffs == 0, "%s: %d checkpoints where incremental and full renders differ", tag, e.diffs);
  CHECK(e.fog_bad == 0, "%s: %d fog steps during the form outside a hold of 2 s (F8)", tag, e.fog_bad);
  const double avg = e.strips_n ? (double)e.strips_sum / (double)e.strips_n : 0.0;
  CHECK(e.strips_n == 0 || avg <= 10.0, "%s: %.2f strips a frame", tag, avg);
  if (!o.quiet)
    printf("  %-30s %4d ticks, done at %4d (%4.1f s spare), strips %.2f max %d, form %d gag %d(%d) fog steps %d, guest %d heron %d fly %d\n", tag,
           e.beat_ticks, e.done_at, (double)(e.beat_ticks - e.done_at) / 25.0, avg, e.strips_max, e.form_ticks, e.gag_ticks, e.gag_kind,
           e.fog_steps_in_form, e.guest_ticks, e.heron_ticks, e.fly_ticks);
  a.stop();
  if (o.check_draw) b.stop();
  return e;
}

// ---- [1] ----------------------------------------------------------------------------------------------------------

static void testPure() {
  printf("[1a] the variant byte and the seconds\n");
  Memory mem;
  memset(&mem, 0, sizeof mem);
  int gag_n[4] = {0, 0, 0, 0}, guests = 0, chap[4] = {0, 0, 0, 0};
  const int N = 4000;
  uint32_t r = 12345u;
  for (int i = 0; i < N; ++i) {
    r = r * 1664525u + 1013904223u;
    const uint8_t v = taichiPlanVar(r, mem, 0);
    ++gag_n[(v >> 4) & 3];
    ++chap[v & 3];
    guests += (v & 64) ? 1 : 0;
    CHECK((v & DAY_NOSHOW) == 0, "the plan set the no-show bit");
    const uint8_t vc = taichiPlanVar(r, mem, 2);
    CHECK(((vc >> 4) & 3) == TG_NONE || ((vc >> 4) & 3) == TG_BUMP, "Copper out: gag %d", (vc >> 4) & 3);
    for (int c = 0; c < 4; ++c) {
      Memory m2 = mem;
      m2.taichi_next = (uint8_t)(0x80 | c);
      CHECK((taichiPlanVar(r, m2, 0) & 3) == c, "the memory's chapter %d not played", c);
    }
  }
  printf("  %d draws: gag none %d wobble %d itch %d; guests %d; chapters from a fresh memory %d %d %d %d\n", N, gag_n[0], gag_n[1], gag_n[2],
         guests, chap[0], chap[1], chap[2], chap[3]);
  CHECK(gag_n[0] > N / 2 && gag_n[1] > N / 10 && gag_n[2] > N / 10 && gag_n[3] == 0, "gag shares");
  CHECK(guests > N * 6 / 10 && guests < N * 9 / 10, "guests in %d of %d", guests, N);
  for (int c = 0; c < 4; ++c) CHECK(chap[c] > N / 6, "chapter %d only %d of %d", c, chap[c], N);
  for (int c = 0; c < 4; ++c) {
    const int base = taichiSeconds((uint8_t)c);
    CHECK((float)base >= taichiFormSeconds(c) + 30.f, "chapter %d: %d s for a %.1f s form", c, base, (double)taichiFormSeconds(c));
    CHECK(taichiSeconds((uint8_t)(c | (TG_BUMP << 4))) == base + 12, "the bump's seconds");
    CHECK(taichiSeconds((uint8_t)(c | (TG_ITCH << 4))) == base + 6, "the itch's seconds");
    CHECK(taichiSeconds((uint8_t)(c | (TG_WOBBLE << 4))) == base + (c == 1 ? 6 : 8), "the wobble's seconds (chapter 2 plays the itch)");
  }
  CHECK(strcmp(majorName(MJ_TAICHI), "taichi") == 0, "name");
}

static void testPlanRuns(bool live) {
  printf("[1b] the plan in real runs\n");
  int first = 0, later = 0, later_n = 0, bad_len = 0, max_majors = 0;
  const int kSeedsN = 10, kLoops = 5;
  for (int seed = 1; seed <= kSeedsN; ++seed) {
    Rig r;
    Inputs in = morning(-1, 0, false);
    if (!r.start((uint64_t)seed, in)) break;
    const World* w = appWorld(r.app);
    for (int loop = 0; loop < kLoops; ++loop) {
      const LoopPlan p = w->plan;
      const int i = indexOf(p, MJ_TAICHI);
      const uint32_t len = appLoopTicks(r.app);
      if (len < 480u * 25u || len > 660u * 25u) ++bad_len;
      if (i >= 0) {
        CHECK(live, "seed %d loop %d: tai chi planned with the switch off", seed, loop);
        CHECK(p.major_after[i] == 0, "seed %d loop %d: tai chi not after the wake", seed, loop);
        CHECK(p.n_majors >= 1 && p.n_majors <= 3, "seed %d loop %d: %d majors with tai chi", seed, loop, (int)p.n_majors);
        if (p.n_majors > max_majors) max_majors = p.n_majors;
      }
      if (loop == 0) first += i >= 0;
      else {
        later += i >= 0;
        ++later_n;
      }
      if (loop + 1 == kLoops) break;
      for (uint32_t t = 0; t < len; ++t) appTick(r.app);
      CHECK(appSpineOverruns(r.app) == 0, "seed %d loop %d: %d overruns", seed, loop, appSpineOverruns(r.app));
    }
    r.stop();
  }
  printf("  %d runs of %d morning loops: tai chi in %d first loops, %d of %d later; %d loops outside 480..660 s; at most %d majors with it\n",
         kSeedsN, kLoops, first, later, later_n, bad_len, max_majors);
  CHECK(bad_len == 0, "%d loops outside 480..660 s", bad_len);
  if (live) CHECK(first == kSeedsN, "tai chi in %d of %d first morning loops", first, kSeedsN);
  else CHECK(first == 0 && later == 0, "tai chi planned with the switch off");
  if (live) CHECK(later * 100 <= later_n * 55, "tai chi in %d of %d later loops (once a morning, then by weight)", later, later_n);
}

// ---- [2] ----------------------------------------------------------------------------------------------------------

static void testChapters() {
  printf("[2] chapters follow the memory; a no-show leaves the chapter alone\n");
  for (int c = 0; c < 4; ++c) {
    Opt o;
    o.check_draw = false;
    o.quiet = true;
    char tag[40];
    snprintf(tag, sizeof tag, "chapter %d from the carry", c + 1);
    const Out e = play(3 + (uint64_t)c, morning(c), o, tag);
    CHECK(e.ran && e.chapter == c, "%s: played chapter %d", tag, e.chapter + 1);
    const unsigned hi = e.carry_after >> 8;
    CHECK(hi == (0x80u | 4u | (unsigned)((c + 1) & 3)), "%s: the memory holds %02x after (wants next chapter %d, done this morning)", tag, hi, ((c + 1) & 3) + 1);
  }
  static const int kKinds[3] = {WX_RAIN, WX_SNOW, WX_STORM};
  for (int j = 0; j < 3; ++j) {
    Opt o;
    o.check_draw = false;
    o.quiet = true;
    o.rain = kKinds[j];
    char tag[40];
    snprintf(tag, sizeof tag, "no-show in %s", weatherKindName(kKinds[j]));
    const Out e = play(5, morning(2, 0, false), o, tag);
    CHECK(e.noshow, "%s: the no-show did not play", tag);
    CHECK(e.form_ticks == 0 && e.guest_ticks == 0 && e.fly_ticks == 0, "%s: the form ran (%d ticks) or a guest came", tag, e.form_ticks);
    const unsigned hi = e.carry_after >> 8;
    CHECK(hi == (0x80u | 4u | 2u), "%s: the memory holds %02x after (wants chapter 3 again, done this morning)", tag, hi);
    printf("  %s: script done at %d of %d ticks (the beat keeps its planned seconds, F27)\n", tag, e.done_at, e.beat_ticks);
  }
}

// ---- [3], [4], [6] ---------------------------------------------------------------------------------------------------

static int g_bell[8];        // beat ticks for the doorbell runs
static uint64_t g_bseed[8];
static int g_bch[8], g_bdog[8];
static const char* const kBellName[8] = {"mid-step", "on the rooster's leg", "in a turn's ramp", "inside the wobble", "inside the itch",
                                         "inside Copper's bump", "the bird on his hand", ""};

static void testBeats() {
  printf("[3] whole beats: each chapter, no gag / wobble / itch, guests on and off\n");
  static const char* const kGag[4] = {"plain", "wobble", "itch", "bump"};
  for (int i = 0; i < 8; ++i) g_bell[i] = -1;
  uint32_t hash0 = 0;
  for (int c = 0; c < 4; ++c) {
    for (int gag = 0; gag < 3; ++gag) {
      for (int guests = 0; guests < 2; ++guests) {
        const uint64_t seed = findSeed(c, gag, guests, 0);
        char tag[48];
        snprintf(tag, sizeof tag, "ch %d %s %s seed %d", c + 1, kGag[gag], guests ? "guests" : "alone", (int)seed);
        CHECK(seed != 0, "%s: no seed found", tag);
        if (!seed) continue;
        Opt o;
        const Out e = play(seed, morning(c), o, tag);
        if (!e.ran) continue;
        const int played = gag == TG_WOBBLE && c == 1 ? TG_ITCH : gag;   // chapter 2 has no balance form
        if (gag) CHECK(e.gag_ticks > 100 && e.gag_kind == played, "%s: gag %d played %d ticks", tag, e.gag_kind, e.gag_ticks);
        else CHECK(e.gag_ticks == 0, "%s: a gag played", tag);
        const int form_want = (int)((taichiFormSeconds(c) + taichiGagSeconds(c, (uint8_t)gag) + taichiOutSeconds()) * 25.f);
        CHECK(abs(e.form_ticks - form_want) <= 3, "%s: the form ran %d ticks, wants %d (nothing trimmed, nothing rushed)", tag, e.form_ticks, form_want);
        CHECK((e.carry_after >> 8) == (0x84u | (unsigned)((c + 1) & 3)), "%s: memory %02x after", tag, e.carry_after >> 8);
        if (!guests) {
          CHECK(e.guest_ticks == 0 && e.heron_ticks == 0 && e.fly_ticks == 0, "%s: a guest came (critter %d, heron %d, butterfly %d ticks)", tag, e.guest_ticks,
                e.heron_ticks, e.fly_ticks);
        } else {
          CHECK(e.fly_ticks > 1000, "%s: the butterfly showed %d ticks", tag, e.fly_ticks);
          if (c == 0) CHECK(e.heron_ticks > 1200 && e.mirror_max > 0.95f, "%s: the heron stood %d ticks, mirrored %.2f", tag, e.heron_ticks, (double)e.mirror_max);
          if (c == 1) CHECK(e.bird_sit > 50 && e.bird_off <= 2.f, "%s: the bird sat %d ticks, at most %.2f px off the hook hand", tag, e.bird_sit, (double)e.bird_off);
          if (c == 2)
            CHECK(e.chip_up_early > 10 && e.chip_down_ok > 20 && e.chip_up_rooster > 20, "%s: the chipmunk up %d, down after the snake %d, up for the rooster %d", tag,
                  e.chip_up_early, e.chip_down_ok, e.chip_up_rooster);
          if (c == 3) CHECK(e.deer_ticks > 1500 && e.deer_min >= 120.f, "%s: the deer %d ticks, nearest %.0f px", tag, e.deer_ticks, (double)e.deer_min);
          if (c == 3 && gag == 0) printf("    the deer came no nearer than %.0f px\n", (double)e.deer_min);
          if (c == 1 && gag == 0) printf("    the bird sat %d ticks, at most %.2f px from the hook hand\n", e.bird_sit, (double)e.bird_off);
        }
        // moments for [5]
        auto keep = [&](int k, int t) {
          if (g_bell[k] < 0 && t >= 0) {
            g_bell[k] = t;
            g_bseed[k] = seed;
            g_bch[k] = c;
            g_bdog[k] = 0;
          }
        };
        if (c == 0 && gag == 0) keep(0, e.t_step);
        if (c == 2 && gag == 0) keep(1, e.t_rooster);
        if (c == 1 && gag == 0) keep(2, e.t_turn);
        if (c == 2 && gag == TG_WOBBLE) keep(3, e.t_gag);
        if (c == 1 && gag == TG_ITCH) keep(4, e.t_gag);
        if (c == 1 && gag == 0 && guests) keep(6, e.t_bird);
        if (c == 0 && gag == 0 && guests) hash0 = e.hash;
        if (c == 0 && gag == 0 && guests) {   // [8] the same again draws the same frames
          Opt q;
          q.check_draw = false;
          q.quiet = true;
          const Out e2 = play(seed, morning(c), q, tag);
          CHECK(e2.hash == hash0 && e2.beat_ticks == e.beat_ticks, "%s: two runs differ (%08x %08x)", tag, e2.hash, hash0);
        }
      }
    }
  }
}

static void testDogs() {
  printf("[7] Lucy watches; Copper's bump\n");
  for (int c = 0; c < 4; ++c) {   // Lucy alone
    const uint64_t seed = findSeed(c, 0, -1, 1);
    char tag[48];
    snprintf(tag, sizeof tag, "ch %d Lucy out seed %d", c + 1, (int)seed);
    CHECK(seed != 0, "%s: no seed", tag);
    if (!seed) continue;
    Opt o;
    const Out e = play(seed, morning(c, 1), o, tag);
    CHECK(e.guest_ticks == 0 && e.heron_ticks == 0, "%s: a wild guest came with a dog out", tag);
    CHECK(e.lucy_first >= 0 && e.lucy_first <= 20 * 25 && e.lucy_bad == 0, "%s: Lucy lying at her watch spot from tick %d, %d ticks away after", tag, e.lucy_first,
          e.lucy_bad);
    printf("    Lucy lying at her watch spot %.1f s into the beat\n", (double)e.lucy_first / 25.0);
  }
  for (int dog = 2; dog <= 3; ++dog) {   // Copper, and both
    for (int c = 0; c < 4; ++c) {
      const uint64_t seed = findSeed(c, TG_BUMP, -1, dog);
      char tag[48];
      snprintf(tag, sizeof tag, "ch %d bump dog_out %d seed %d", c + 1, dog, (int)seed);
      CHECK(seed != 0, "%s: no seed", tag);
      if (!seed) continue;
      Opt o;
      const Out e = play(seed, morning(c, dog), o, tag);
      CHECK(e.bump_ticks > 150 && e.copper_sat > 25, "%s: the bump played %d ticks, Copper sat %d", tag, e.bump_ticks, e.copper_sat);
      CHECK(e.lent_end == 0, "%s: Copper not given back", tag);
      CHECK(e.guest_ticks == 0 && e.heron_ticks == 0, "%s: a wild guest came with a dog out", tag);
      if (g_bell[5] < 0 && e.t_gag >= 0) {
        g_bell[5] = e.t_gag;
        g_bseed[5] = seed;
        g_bch[5] = c;
        g_bdog[5] = dog;
      }
      if (dog == 2 && c == 0) {   // he goes back indoors before the cue: no bump, a clean end
        Opt q;
        q.dog_at = 400;
        q.dog_val = 0;
        snprintf(tag, sizeof tag, "ch %d Copper goes in early", c + 1);
        const Out e2 = play(seed, morning(c, dog), q, tag);
        CHECK(e2.bump_ticks == 0 && e2.lent_end == 0, "%s: bump %d ticks, lent %d", tag, e2.bump_ticks, e2.lent_end);
      }
    }
  }
  printf("[6b] a dog going out mid-form sends the guests off\n");
  for (int c = 0; c < 4; ++c) {
    const uint64_t seed = findSeed(c, 0, 1, 0);
    if (!seed) continue;
    // while the guest is there: the crane's hold (1), the whip's (2), the snake (3), the kick (4)
    static const int kKey[4] = {8, 14, 15, 5};
    Opt o;
    o.dog_at = (int)((16.f + taichiKeyArrive(c, kKey[c]) + (c == 1 ? 4.5f : 1.f)) * 25.f);
    o.dog_val = 1;
    char tag[48];
    snprintf(tag, sizeof tag, "ch %d Lucy comes out mid-form", c + 1);
    const Out e = play(seed, morning(c), o, tag);
    CHECK(e.leaving_by >= 0 && e.leaving_by <= 100, "%s: the guests going %d ticks after (wants 4 s)", tag, e.leaving_by);
  }
}

// ---- [5] ----------------------------------------------------------------------------------------------------------

static void testBell() {
  printf("[5] the doorbell: he holds the form, only his head lifts\n");
  for (int k = 0; k < 7; ++k) {
    CHECK(g_bell[k] >= 0, "no moment found for the doorbell %s", kBellName[k]);
    if (g_bell[k] < 0) continue;
    Opt o;
    o.bell_at = g_bell[k];
    o.quiet = true;
    char tag[64];
    snprintf(tag, sizeof tag, "bell %s (ch %d, tick %d)", kBellName[k], g_bch[k] + 1, g_bell[k]);
    const Out e = play(g_bseed[k], morning(g_bch[k], g_bdog[k]), o, tag);
    CHECK(e.bell_ticks >= 80, "%s: the reaction ran %d ticks", tag, e.bell_ticks);
    CHECK(e.bell_moved == 0, "%s: a joint or his feet moved on %d ticks", tag, e.bell_moved);
    CHECK(e.bell_faced == 0, "%s: his facing changed", tag);
    CHECK(e.bell_head > 0.1f, "%s: his head lifted only %.3f rad", tag, (double)e.bell_head);
    CHECK(e.bell_bird == 0, "%s: the bird left his hand (%d ticks)", tag, e.bell_bird);
    printf("  %-44s %d frozen ticks, head %.2f rad, script done %.1f s before the end\n", tag, e.bell_ticks, (double)e.bell_head,
           (double)(e.beat_ticks - e.done_at) / 25.0);
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  const bool live = ssS22Switch() != 0;
  testPure();
  testPlanRuns(live);
  if (live) {
    testChapters();
    testBeats();
    testDogs();
    testBell();
  } else {
    printf("SS_S22 off: tai chi does not plan; the beat tests are skipped\n");
  }
  printf("[8] numbers: sizeof(DayTaichi) %d, sizeof(TaichiState) %d, sizeof(DayBike) %d, sizeof(StreamLife) %d, sizeof(App) budget %d\n",
         (int)sizeof(DayTaichi), (int)sizeof(TaichiState), (int)sizeof(DayBike), (int)sizeof(StreamLife), (int)SS_APP_BYTES);
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
