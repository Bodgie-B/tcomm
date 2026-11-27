// The Christmas season's story (scene/xmas.h, xmas_story.cpp; Mike, 2026-11-26).
//
// (1) The season's dates: Nov 25 off, Nov 26 on, Dec 10 no star, Dec 11 the star, Dec 25 the full pile,
//     Jan 1 on, Jan 2 off; no date, no Christmas. Out of season the plan byte is 0 and no plan line
//     carries " xmas=".
// (2) Twice an evening: the first early (golden) loop decorates, the next early one only switches on, the
//     first night loop decorates again, the next one switches on; a morning or midday loop starts the
//     count over. The windows travel in the carry word (appCarry) to a new run.
// (3) The tree is up exactly while the tent is, in every camp family: the evening from the beat after
//     setup, the morning until break camp packs the tent (the lights go out first), midday all loop.
//     Nothing in the Lookout or the sunrise.
// (4) The decorating, rendered every tick: the generator in, the ladder, five wraps, the cardinals in
//     their order (flying, tangled, part, leaving, draped), seven ornaments, the star (from Dec 11), the
//     ladder down, the lights on; no overrun, no critter left, the hiker in frame and out of the chip's
//     box, nothing of the set in the chip's box, incremental renders equal full ones, the strips a frame.
// (5) Two runs draw the same frames (determinism); a doorbell during the decorating freezes it and it
//     goes on afterwards to the same end.
// (6) The raccoon: takes the low ornament to its bush, the bush's count grows over loops, capped, and is
//     reset outside the season; never in a loop with raccoon_pack.
#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/spine.h"
#include "scene/world.h"
#include "scene/xmas.h"

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
      printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void testDates() {
  CHECK(xmasSeasonDay(11, 25) < 0, "Nov 25 is out");
  CHECK(xmasSeasonDay(11, 26) == 0, "Nov 26 is day 0");
  CHECK(xmasSeasonDay(12, 10) == 14 && 14 < XMAS_STAR_DAY, "Dec 10: no star yet");
  CHECK(xmasSeasonDay(12, 11) == XMAS_STAR_DAY, "Dec 11: the star");
  CHECK(xmasSeasonDay(12, 25) == XMAS_DAY_CHRISTMAS, "Dec 25");
  CHECK(xmasSeasonDay(1, 1) == 36, "Jan 1 is in");
  CHECK(xmasSeasonDay(1, 2) < 0, "Jan 2 is out");
  CHECK(xmasSeasonDay(0, 0) < 0 && xmasSeasonDay(12, 0) < 0 && xmasSeasonDay(0, 12) < 0, "no clock, no Christmas");
  CHECK(xmasSeasonDay(7, 4) < 0 && xmasSeasonDay(11, 31) < 0, "summer");
  CHECK(ssXmasSwitch() == SS_XMAS, "switch");
}

#if SS_XMAS

static void* hostAlloc(size_t n) { return calloc(1, n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

struct Rig {
  void* mem = nullptr;
  std::vector<Pixel> fb;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = calloc(1, SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = appStart(mem, kAlloc, seed, in);
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
  }
  const World& w() const { return *appWorld(app); }
  World& wm() { return const_cast<World&>(*appWorld(app)); }
};

static Inputs inputs(float mood, int month, int mday, int family = -1, bool night = false) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.have_outside = true;
  in.weather_code = 0;
  in.night_mode = night;
  if (night) {
    in.weather.night_dim = 1.f;
    in.weather.night_gain = 1.6f;
  }
  in.month = (uint8_t)month;
  in.mday = (uint8_t)mday;
  if (family >= 0) in.spine_force = (uint8_t)(1 + family);
  return in;
}

// Ticks to the end of the loop that is running (appTick returns true on the first tick of the next).
static int toLoopEnd(Rig& r, int max = 20000) {
  for (int t = 0; t < max; ++t)
    if (appTick(r.app)) return t;
  return -1;
}

static bool planHas(const App* app, const char* s) {
  char buf[400];
  appLoopPlan(app, buf, sizeof buf);
  return strstr(buf, s) != nullptr;
}

// Tree pieces on show (anything of the decorations; the bush is not one).
static bool treeUp(const World& w) {
  const XmasMock& m = w.xmas.m;
  return m.gen || m.wraps || m.orns || m.garland != XG_NONE || (m.extras & (XE_STAR | XE_STOCKING)) || m.gifts;
}
static bool tentUp(const World& w) { return w.tent.state == TENT_UP && w.tent.progress >= 1.f; }

// ---- (1) out of season and the plan byte ----------------------------------------------------------------

static void testOutOfSeason() {
  const int dates[4][2] = {{11, 25}, {1, 2}, {0, 0}, {7, 4}};
  for (int d = 0; d < 4; ++d)
    for (int fam = 0; fam < 3; ++fam) {
      Rig r;
      CHECK(r.start(3, inputs(fam == 0 ? 4.f : (fam == 1 ? 1.5f : 3.f), dates[d][0], dates[d][1])), "start");
      bool ever = false;
      for (int loop = 0; loop < 2; ++loop) {
        CHECK(r.w().plan.xmas == 0, "out of season: plan byte %u", (unsigned)r.w().plan.xmas);
        CHECK(!planHas(r.app, " xmas="), "out of season: the plan line names Christmas");
        for (int t = 0; t < 20000; ++t) {
          if (xmasOn(r.w())) ever = true;
          if (appTick(r.app)) break;
        }
      }
      CHECK(!ever, "date %d-%d family %d: something of Christmas showed", dates[d][0], dates[d][1], fam);
      r.stop();
    }
}

// ---- (2) twice an evening -------------------------------------------------------------------------------

static void testTwiceAnEvening() {
  Rig r;
  CHECK(r.start(5, inputs(4.f, 12, 3)), "start");
  // Loop by loop: golden, golden, golden, night, night, golden (still the same evening).
  const float moods[6] = {4.f, 4.2f, 4.6f, 0.f, 0.2f, 4.f};
  const bool deck[6] = {true, false, false, true, false, false};
  for (int i = 0; i < 6; ++i) {
    if (i > 0) {
      appSetInputs(r.app, inputs(moods[i], 12, 3, -1, moods[i] < 0.5f));
      CHECK(toLoopEnd(r) >= 0, "loop %d ends", i - 1);
    }
    const World& w = r.w();
    CHECK(w.plan.family == SF_EVENING, "loop %d is an evening", i);
    CHECK(((w.plan.xmas & XMAS_PLAN_DECK) != 0) == deck[i], "loop %d (mood %.1f): decorate %d, wanted %d", i, (double)moods[i],
          (w.plan.xmas & XMAS_PLAN_DECK) != 0, deck[i]);
    CHECK(planHas(r.app, deck[i] ? " xmas=7,deck" : " xmas=7"), "loop %d plan line", i);
  }
  // A new run carries the windows: both played this evening, so no decorating.
  const uint16_t carry = appCarry(r.app);
  CHECK((carry >> 8 & (XMAS_DECK_EARLY | XMAS_DECK_LATE)) == (XMAS_DECK_EARLY | XMAS_DECK_LATE), "carry %04X", (unsigned)carry);
  r.stop();
  Inputs in = inputs(4.f, 12, 3);
  in.carry = carry;
  Rig r2;
  CHECK(r2.start(9, in), "start");
  CHECK(!(r2.w().plan.xmas & XMAS_PLAN_DECK), "a new run this evening: both windows played, lights_on only");
  // A midday loop starts the count over; the next golden loop decorates.
  appSetInputs(r2.app, inputs(3.f, 12, 4));
  CHECK(toLoopEnd(r2) >= 0, "loop end");
  CHECK(r2.w().plan.family == SF_MIDDAY, "midday");
  appSetInputs(r2.app, inputs(4.f, 12, 4));
  CHECK(toLoopEnd(r2) >= 0, "loop end");
  CHECK((r2.w().plan.xmas & XMAS_PLAN_DECK) != 0, "the next evening decorates again");
  r2.stop();
}

// ---- (3) the tree follows the tent ---------------------------------------------------------------------

struct FamilyRun {
  int ticks = 0, shown = 0, tent_no_tree = 0, tree_no_tent = 0, overruns = 0, critters_end = 0;
  bool off_beat = false, lights_off_seen = false, lit_after_off = false, ended_clear = true;
  int star = 0, gifts = -1;
};

static FamilyRun runFamily(uint64_t seed, const Inputs& in, int loops = 1) {
  FamilyRun f;
  Rig r;
  if (!r.start(seed, in)) return f;
  for (int loop = 0; loop < loops; ++loop) {
    bool decorated = false;   // the evening: the tree is up from the beat after setup
    for (int t = 0; t < 20000; ++t) {
      const bool next = appTick(r.app);
      const World& w = r.w();
      if (next) {
        f.overruns += 0;
        break;
      }
      ++f.ticks;
      const char* beat = appBeatName(r.app);
      if (!strcmp(beat, "decorate") || !strcmp(beat, "lights_on")) decorated = true;
      if (!strcmp(beat, "lights_off")) f.lights_off_seen = true;
      if (f.lights_off_seen && w.xmas.m.power != XP_OFF && !strcmp(beat, "break_camp")) f.lit_after_off = true;
      const bool tree = treeUp(w), tent = tentUp(w);
      if (tree) ++f.shown;
      if (tree && !tent) ++f.tree_no_tent;
      const bool should = w.plan.family == SF_EVENING ? decorated : true;
      if (tent && !tree && should && buildDone(w.build)) ++f.tent_no_tree;
      if (w.xmas.m.extras & XE_STAR) ++f.star;
      if (w.xmas.m.power == XP_ON && w.xmas.m.lift == 0) f.gifts = w.xmas.m.gifts;
      if (appLoopTicks(r.app) - 1 == appTickCount(r.app)) {
        f.overruns = appSpineOverruns(r.app);
        f.critters_end = appCritterCount(r.app);
        f.ended_clear = !(w.plan.family == SF_MORNING && w.plan.stay == 0) || !treeUp(w);
      }
    }
  }
  r.stop();
  return f;
}

static void testTreeFollowsTent() {
  // Evening (golden and night), midday, the morning (both kinds), Dec 3 and Dec 25.
  struct Case { uint64_t seed; float mood; int fam; int mday; bool night; };
  const Case cases[] = {{1, 4.f, SF_EVENING, 3, false}, {2, 0.f, SF_EVENING, 25, true}, {3, 3.f, SF_MIDDAY, 25, false},
                        {4, 1.5f, SF_MORNING, 3, false}, {6, 1.5f, SF_MORNING, 20, false}, {7, 1.5f, SF_MORNING, 25, false},
                        {8, 1.5f, SF_MORNING, 11, false}, {9, 1.5f, SF_MORNING, 12, false}};
  int marks = 0;
  for (const Case& c : cases) {
    const FamilyRun f = runFamily(c.seed, inputs(c.mood, 12, c.mday, c.fam, c.night));
    CHECK(f.ticks > 10000, "case seed %llu ran %d ticks", (unsigned long long)c.seed, f.ticks);
    CHECK(f.shown > 1000, "seed %llu fam %d: the tree showed %d ticks", (unsigned long long)c.seed, c.fam, f.shown);
    CHECK(f.tree_no_tent == 0, "seed %llu fam %d: tree without the tent on %d ticks", (unsigned long long)c.seed, c.fam, f.tree_no_tent);
    CHECK(f.tent_no_tree == 0, "seed %llu fam %d: the tent without the tree on %d ticks", (unsigned long long)c.seed, c.fam, f.tent_no_tree);
    CHECK(f.overruns == 0, "seed %llu fam %d: %d overruns", (unsigned long long)c.seed, c.fam, f.overruns);
    CHECK(f.critters_end == 0, "seed %llu fam %d: %d critters left", (unsigned long long)c.seed, c.fam, f.critters_end);
    CHECK(f.ended_clear, "seed %llu: Mark's morning ends with the tree packed", (unsigned long long)c.seed);
    CHECK(!f.lit_after_off, "seed %llu: lights on in break camp after lights_off", (unsigned long long)c.seed);
    CHECK((f.star > 0) == (c.mday >= 11), "seed %llu Dec %d: star ticks %d", (unsigned long long)c.seed, c.mday, f.star);
    if (c.mday == 25) CHECK(f.gifts == XMAS_GIFTS_ALL, "Dec 25: the full pile (%d)", f.gifts);
    if (c.mday == 3) CHECK(f.gifts == 3, "Dec 3, a week in: three presents (%d)", f.gifts);
    if (c.mday == 20) CHECK(f.gifts == 5, "Dec 20: five (%d)", f.gifts);
    if (c.fam == SF_MORNING && f.lights_off_seen) ++marks;
  }
  CHECK(marks > 0, "no break-camp morning among the cases: lights_off never tested");
  // Nov 26: two presents; the Lookout and the sunrise: nothing.
  {
    const FamilyRun f = runFamily(11, inputs(3.f, 11, 26, SF_MIDDAY));
    CHECK(f.gifts == 2 && f.star == 0, "Nov 26: gifts %d star %d", f.gifts, f.star);
  }
  for (int fam = SF_LOOKOUT; fam <= SF_SUNRISE; ++fam) {
    Rig r;
    CHECK(r.start(4, inputs(fam == SF_LOOKOUT ? 4.5f : 1.f, 12, 24, fam)), "start");
    CHECK(r.w().plan.xmas == 0, "set piece %d: no Christmas in the plan", fam);
    r.stop();
  }
}

// ---- (4), (5) the decorating rendered ------------------------------------------------------------------

struct DeckLog {
  int ticks = 0, strips = 0, worst = 0, loop_strips = 0, loop_frames = 0;
  int bad_full = 0, full_checks = 0, chip_hits = 0, out_of_frame = 0;
  uint64_t hash = 0;
  int overruns = 0, critters_end = 0, rebuilds = 0;
  uint8_t last_garland = 0;
  int garland_steps = 0;   // the cardinals' states in order
  bool star = false, lit = false, ornaments = false, wraps = false, ladder_down = false;
  int frozen_moves = 0, bells = 0;
};

static bool inChipBox(const Rect& r) { return !r.empty() && r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }

static DeckLog runDeck(uint64_t seed, const Inputs& in, int bell_every = 0) {
  DeckLog L;
  Rig a, full;
  if (!a.start(seed, in) || !full.start(seed, in)) return L;
  static const uint8_t kOrder[5] = {XG_FLYING, XG_TANGLED, XG_PART, XG_LEAVING, XG_DRAPED};
  bool in_deck = false;
  int deck_t = 0;
  uint64_t h = 0xCBF29CE484222325ull;
  uint8_t run_i = 0;
  uint16_t run_ticks = 0;
  bool prev_active = false;
  for (int t = 0; t < 20000; ++t) {
    const bool next = appTick(a.app);
    appTick(full.app);
    if (next) break;
    const World& w = a.w();
    const bool deck = !strcmp(appBeatName(a.app), "decorate");
    if (bell_every > 0 && deck && deck_t > 0 && deck_t % bell_every == 0) {
      appPostEvent(a.app, Event{EV_DOORBELL, 0});
      appPostEvent(full.app, Event{EV_DOORBELL, 0});
    }
    if (deck && w.react.active && !prev_active) ++L.bells;   // a bell he answered
    prev_active = w.react.active;
    if (deck && w.react.active) {   // the story holds still: the script's step and clock do not move
      if (w.spine.run.i != run_i || w.spine.run.ticks != run_ticks) ++L.frozen_moves;
    }
    run_i = w.spine.run.i;
    run_ticks = w.spine.run.ticks;
    const int s = appRender(a.app, a.surf);
    L.loop_strips += s;
    ++L.loop_frames;
    if (deck) {
      in_deck = true;
      ++deck_t;
      ++L.ticks;
      L.strips += s;
      if (s > L.worst) L.worst = s;
      const XmasMock& m = w.xmas.m;
      if (m.garland != L.last_garland && m.garland != XG_NONE) {
        if (L.garland_steps < 5 && m.garland == kOrder[L.garland_steps]) ++L.garland_steps;
        else L.garland_steps = 100;   // out of order
        L.last_garland = m.garland;
      }
      if (m.extras & XE_STAR) L.star = true;
      if (m.wraps == XMAS_WRAPS) L.wraps = true;
      if (m.orns == 0x7F) L.ornaments = true;
      if (m.power == XP_ON && m.lift == 0) L.lit = true;
      if ((m.extras & XE_LADDER) && m.ladder == 0 && L.wraps) L.ladder_down = true;
      Rect hb;
      if (appHikerBounds(a.app, &hb)) {
        // (He goes out at the left edge for the generator on purpose: only the right edge and the top count.)
        if (hb.x1 > SS_WIDTH + 2 || hb.y0 < 0) ++L.out_of_frame;
        if (inChipBox(hb)) ++L.chip_hits;
      }
      if (inChipBox(xmasTreeBounds(w)) || inChipBox(xmasGenBounds(w)) || inChipBox(xmasCritBounds(w))) ++L.chip_hits;
      if (deck_t % 3 == 0) h = (h ^ (uint64_t)s) * 0x100000001B3ull;
      if (deck_t % 41 == 0) {
        for (size_t i = 0; i < a.fb.size(); i += 7) h = (h ^ a.fb[i]) * 0x100000001B3ull;
      }
    }
    if (in_deck && ((deck && (deck_t % 29 == 7 || deck_t < 3)) || (!deck && t % 97 == 0))) {
      appInvalidate(full.app);
      appRender(full.app, full.surf);
      ++L.full_checks;
      if (memcmp(a.fb.data(), full.fb.data(), a.fb.size() * sizeof(Pixel)) != 0) {
        if (++L.bad_full <= 2)
          for (size_t i = 0; i < a.fb.size(); ++i)
            if (a.fb[i] != full.fb[i]) {
              printf("  seed %llu tick %d: incremental differs at x %d y %d\n", (unsigned long long)seed, t, (int)(i % SS_WIDTH),
                     (int)(i / SS_WIDTH));
              break;
            }
      }
    } else {
      appRender(full.app, full.surf);
    }
    if (appLoopTicks(a.app) - 1 == appTickCount(a.app)) {
      L.overruns = appSpineOverruns(a.app);
      L.critters_end = appCritterCount(a.app);
      L.rebuilds = appPaletteRebuilds(a.app);
    }
  }
  L.hash = h;
  a.stop();
  full.stop();
  return L;
}

static void testDecorating() {
  struct Case { uint64_t seed; float mood; int mday; bool night; };
  const Case cases[] = {{1, 4.f, 12, false}, {2, 4.5f, 10, false}, {3, 0.f, 24, true}, {4, 5.f, 26, false}, {5, 3.8f, 1, false}};
  double sum = 0.;
  int frames = 0, worst = 0;
  for (const Case& c : cases) {
    const int month = c.mday >= 26 ? 11 : 12;
    const DeckLog L = runDeck(c.seed, inputs(c.mood, month, c.mday, SF_EVENING, c.night));
    const bool star = xmasSeasonDay((uint8_t)month, (uint8_t)c.mday) >= XMAS_STAR_DAY;
    CHECK(L.ticks > 2500, "seed %llu: the decorate beat ran %d ticks", (unsigned long long)c.seed, L.ticks);
    CHECK(L.wraps && L.ornaments && L.lit && L.ladder_down, "seed %llu: wraps %d ornaments %d lit %d ladder down %d",
          (unsigned long long)c.seed, L.wraps, L.ornaments, L.lit, L.ladder_down);
    CHECK(L.garland_steps == 5, "seed %llu: the cardinals' states out of order (%d)", (unsigned long long)c.seed, L.garland_steps);
    CHECK(L.star == star, "seed %llu Dec %d: star %d", (unsigned long long)c.seed, c.mday, L.star);
    CHECK(L.overruns == 0, "seed %llu: %d overruns", (unsigned long long)c.seed, L.overruns);
    CHECK(L.critters_end == 0, "seed %llu: %d critters at the end", (unsigned long long)c.seed, L.critters_end);
    CHECK(L.bad_full == 0, "seed %llu: %d of %d incremental frames differ from a full render", (unsigned long long)c.seed,
          L.bad_full, L.full_checks);
    CHECK(L.full_checks > 100, "seed %llu: %d full checks", (unsigned long long)c.seed, L.full_checks);
    CHECK(L.chip_hits == 0, "seed %llu: %d ticks in the chip's box", (unsigned long long)c.seed, L.chip_hits);
    CHECK(L.out_of_frame == 0, "seed %llu: he left the frame %d ticks", (unsigned long long)c.seed, L.out_of_frame);
    CHECK(L.rebuilds <= 1, "seed %llu: %d palette rebuilds", (unsigned long long)c.seed, L.rebuilds);
    const double avg = L.ticks ? (double)L.strips / L.ticks : 0.;
    const double loop_avg = L.loop_frames ? (double)L.loop_strips / L.loop_frames : 0.;
    CHECK(avg <= 10.0, "seed %llu: %.2f strips a frame in the decorating", (unsigned long long)c.seed, avg);
    CHECK(loop_avg <= 9.0, "seed %llu: %.2f strips a frame over the loop", (unsigned long long)c.seed, loop_avg);
    printf("  decorate seed %llu Dec %d mood %.1f: %d ticks, strips %.2f a frame (worst %d), loop %.2f, %d full checks\n",
           (unsigned long long)c.seed, c.mday, (double)c.mood, L.ticks, avg, L.worst, loop_avg, L.full_checks);
    sum += (double)L.strips;
    frames += L.ticks;
    if (L.worst > worst) worst = L.worst;
  }
  printf("  decorate: %.2f strips a frame over %d frames, worst %d\n", frames ? sum / frames : 0., frames, worst);
  // Determinism: the same frames twice.
  const DeckLog a = runDeck(1, inputs(4.f, 12, 12, SF_EVENING));
  const DeckLog b = runDeck(1, inputs(4.f, 12, 12, SF_EVENING));
  CHECK(a.hash == b.hash && a.ticks == b.ticks, "two runs differ (%016llX %016llX)", (unsigned long long)a.hash,
        (unsigned long long)b.hash);
  // The doorbell: every 600 ticks of the beat (carrying, on the ladder, the birds, the ornaments ...).
  const DeckLog d = runDeck(1, inputs(4.f, 12, 12, SF_EVENING), 600);
  CHECK(d.bells >= 4, "only %d bells", d.bells);
  CHECK(d.frozen_moves == 0, "the decorating moved on %d ticks during a doorbell", d.frozen_moves);
  CHECK(d.overruns == 0 && d.critters_end == 0, "after the bells: %d overruns, %d critters", d.overruns, d.critters_end);
  CHECK(d.ticks == a.ticks + 85 * d.bells, "the beat ran %d ticks with %d bells, %d without", d.ticks, d.bells, a.ticks);
  CHECK(d.bad_full == 0, "with the bells: %d incremental frames differ", d.bad_full);
}

// The quick switch-on: off and raised before, on and turning after; the strips over a normal loop with the tree.
static void testLightsOn() {
  Rig r;
  CHECK(r.start(5, inputs(4.f, 12, 3)), "start");
  CHECK(toLoopEnd(r) >= 0, "first loop (decorates)");
  CHECK(r.w().plan.xmas == XMAS_PLAN_ON + 7, "second loop: lights_on (%02X)", (unsigned)r.w().plan.xmas);
  bool seen = false, off_before = false, on_after = false, turned = false;
  uint8_t wheel0 = 0;
  long strips = 0, frames = 0;
  for (int t = 0; t < 20000; ++t) {
    if (appTick(r.app)) break;
    const World& w = r.w();
    strips += appRender(r.app, r.surf);
    ++frames;
    const bool on = !strcmp(appBeatName(r.app), "lights_on");
    if (on && !seen) {
      seen = true;
      off_before = w.xmas.m.power == XP_OFF && w.xmas.m.lift > 0 && w.xmas.m.wraps == XMAS_WRAPS && w.xmas.m.orns == 0x7F;
    }
    if (seen && !on && !on_after) {
      on_after = w.xmas.m.power == XP_ON && w.xmas.m.lift == 0;
      wheel0 = w.xmas.m.wheel;
    }
    if (on_after && w.xmas.m.wheel != wheel0) turned = true;
    if (appLoopTicks(r.app) - 1 == appTickCount(r.app)) CHECK(appSpineOverruns(r.app) == 0, "lights_on loop overruns");
  }
  CHECK(seen && off_before && on_after && turned, "lights_on: seen %d off before %d on after %d turning %d", seen, off_before,
        on_after, turned);
  printf("  a lights_on loop with the tree: %.2f strips a frame\n", frames ? (double)strips / (double)frames : 0.);
  CHECK(frames && (double)strips / (double)frames <= 9.0, "the loop with the tree: %.2f strips a frame", (double)strips / (double)frames);
  r.stop();
}

// ---- (6) the raccoon -------------------------------------------------------------------------------------

static void testTheft() {
  int played = 0, grew = 0;
  for (uint64_t seed = 1; seed <= 60 && played < 3; ++seed) {
    Rig r;
    CHECK(r.start(seed, inputs(0.f, 12, 2, SF_EVENING, true)), "start");
    for (int loop = 0; loop < 3; ++loop) {
      const World& w = r.w();
      const bool theft = planHas(r.app, "bauble_thief");
      CHECK(!(theft && planHas(r.app, "raccoon_pack")), "seed %llu: both raccoons in one loop", (unsigned long long)seed);
      const uint8_t bush0 = w.memory.xmas_bush;
      bool gap = false, held = false;
      int left = -1, over = -1;
      for (int t = 0; t < 20000; ++t) {
        if (appTick(r.app)) break;
        if (r.w().xmas.m.coon == XR_HELD) held = true;
        if (r.w().xmas.m.extras & XE_GAP) gap = true;
        if (appLoopTicks(r.app) - 1 == appTickCount(r.app)) {
          left = appCritterCount(r.app);
          over = appSpineOverruns(r.app);
        }
      }
      if (theft) {
        ++played;
        CHECK(held && gap, "seed %llu: the raccoon held %d, the hook empty %d", (unsigned long long)seed, held, gap);
        CHECK(over == 0 && left == 0, "seed %llu: %d overruns, %d critters", (unsigned long long)seed, over, left);
        if (r.w().memory.xmas_bush == bush0 + 1 || r.w().memory.xmas_bush == XMAS_BUSH_MAX) ++grew;
      }
    }
    r.stop();
  }
  CHECK(played >= 2, "the theft played in only %d loops", played);
  CHECK(grew == played, "the bush grew in %d of %d thefts", grew, played);
  // The count over the season: the floor by the date, capped, and none outside the season.
  Rig r;
  CHECK(r.start(2, inputs(0.f, 12, 30, SF_EVENING, true)), "start");
  CHECK(r.w().memory.xmas_bush == 3 && r.w().xmas.m.bush == 4, "Dec 30: the bush starts with 3 (%u)", (unsigned)r.w().memory.xmas_bush);
  r.wm().memory.xmas_bush = 200;
  appSetInputs(r.app, inputs(0.f, 12, 30, SF_EVENING, true));
  CHECK(toLoopEnd(r) >= 0, "loop");
  CHECK(r.w().memory.xmas_bush == XMAS_BUSH_MAX, "capped at %d (%u)", XMAS_BUSH_MAX, (unsigned)r.w().memory.xmas_bush);
  appSetInputs(r.app, inputs(0.f, 1, 2, SF_EVENING, true));
  CHECK(toLoopEnd(r) >= 0, "loop");
  CHECK(r.w().memory.xmas_bush == 0 && r.w().xmas.m.bush == 0, "Jan 2: the bush is bare and gone (%u)", (unsigned)r.w().memory.xmas_bush);
  r.stop();
}

#endif  // SS_XMAS

int main() {
  testDates();
#if SS_XMAS
  void* scratch = calloc(1, rasterScratchBytes());
  void* old = rasterSetScratch(scratch);
  testOutOfSeason();
  testTwiceAnEvening();
  testTreeFollowsTent();
  testLightsOn();
  testDecorating();
  testTheft();
  rasterSetScratch(old);
  free(scratch);
#endif
  printf("test_xmas_story: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
