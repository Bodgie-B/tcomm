// Tests for stage 14: the mail bird (core/scene/mailbird.cpp), fire and ice (core/scene/fireice.cpp)
// and the hiker's wave at a passer-by (core/scene/react.cpp):
//
//   [1] none of it asked for: nothing of it runs, holds a slot or draws
//   [2] the arrows: the newest decides the kind; predicted ones are left out
//   [3] too hot and too cold take him over in each camp family, and never in the Lookout
//   [4] cleared: it winds down and lets go, he is back where he was, and nothing is left: no fire,
//       no ice in the cached background (drawn statics equal a clean World's), no bucket, the pit
//       as it was, the light as it was
//   [5] drawn tick by tick (fires, the ice steps, the warm light, the bird), every frame equals a
//       full render
//   [6] the same seed and the same input changes replay frame for frame
//   [7] the bird: flies in, lands, drops the letter, perches inside the frame; leaves when the mail is
//       collected; simply there when mail waits as the loop begins; none in the Lookout
//   [8] the dogs: no fire starts near them; in the cold they lie in the warm circle
//   [9] the wave: once per passer-by, short, the story waits for it; not in an urgent spell
//  [10] memory: the scene block within its budget
//  The episode (Mike, 2026-11-15: "about two minutes per loop"; fireice.h, the rule at the top):
//  [11] with the arrows left as they are, an episode lasts 110-130 s mild and 160-180 s urgent, hot
//       and cold, in each camp family; then nothing is left behind, he is back where he was, the
//       story stood still for it and carries on; incremental frames equal full renders through its
//       start and its end; mild to urgent under way runs it to the urgent length, no extra one after
//  [12] exactly one episode a loop over several loops while the arrow stays; never one running as
//       a loop ends; every planned major still plays
//  [13] a new request in the loop (mild to urgent, the other kind, one more arrow, an arrow back
//       after all cleared) earns one more episode in the same loop, and only one; one of two arrows
//       going does not
//  [14] two runs through episodes agree frame for frame
//  [15] majors per hour over many midday hours with no arrow, a mild and an urgent arrow showing
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/fireice.h"
#include "scene/mailbird.h"
#include "scene/react.h"
#include "scene/spine.h"
#include "scene/world.h"

#include <math.h>
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

static const char* famName(int f) { return f == SF_EVENING ? "evening" : (f == SF_MORNING ? "morning" : (f == SF_MIDDAY ? "midday" : "lookout")); }

static Inputs makeInputs(int family, int dog = 0) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = family == SF_EVENING ? 4.5f : (family == SF_MORNING ? 1.6f : (family == SF_LOOKOUT ? 4.f : 3.f));
  in.spine_force = (uint8_t)(1 + family);
  in.have_outside = true;
  in.dog_out = dog;
  in.chip = true;
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
  void set() { appSetInputs(app, in); }
  void ask(uint8_t a, uint8_t b = 0, uint8_t c = 0) {
    in.temp_ask[0] = a;
    in.temp_ask[1] = b;
    in.temp_ask[2] = c;
    set();
  }
  void step() {
    appTick(app);
    appRender(app, surf);
  }
  // A tick with no drawing (long runs); true when a loop ended. resync() before drawing again.
  bool tickOnly() { return appTick(app); }
  void resync() {
    appInvalidate(app);
    appRender(app, surf);
  }
  // A full render into the second buffer, compared with the frame built tick by tick.
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
  }
};

static uint32_t hashFb(const std::vector<Pixel>& fb) {
  uint32_t h = 2166136261u;
  for (Pixel p : fb) h = (h ^ p) * 16777619u;
  return h;
}

// Ticks until fire and ice holds him, at most `limit`; -1 if it never did.
static int waitOn(Rig& r, int limit) {
  for (int t = 0; t < limit; ++t) {
    r.step();
    if (r.w().fi.on) return t;
  }
  return -1;
}
static int waitOff(Rig& r, int limit) {
  for (int t = 0; t < limit; ++t) {
    r.step();
    if (!r.w().fi.on) return t;
  }
  return -1;
}

// The static layers as worldDrawStatic draws them for World `w`, rows [y0, SS_HEIGHT).
static std::vector<Pixel> drawStatics(const World& w, const Palette& pal) {
  std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT, 0);
  Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  std::vector<uint32_t> scratch(rasterScratchBytes() / 4 + 1);
  rasterResetScratch(scratch.data());
  void* prev = rasterSetScratch(scratch.data());
  worldDrawStatic(w, s, pal, 3.f, nullptr);
  rasterSetScratch(prev);
  return px;
}
static int staticDiffFromClean(Rig& r) {
  World* clean = (World*)malloc(sizeof(World));
  memcpy((void*)clean, &r.w(), sizeof(World));
  memset((void*)&clean->fi, 0, sizeof(FireIce));
  const std::vector<Pixel> a = drawStatics(r.w(), *appPalette(r.app));
  const std::vector<Pixel> b = drawStatics(*clean, *appPalette(r.app));
  free(clean);
  int n = 0;
  for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i] ? 1 : 0;
  return n;
}

// ---- [1] -------------------------------------------------------------------------------------------

static void testIdle() {
  printf("[1] nothing asked for: nothing runs\n");
  int bad = 0;
  for (int f : {SF_EVENING, SF_MORNING, SF_MIDDAY}) {
    Rig r;
    CHECK(r.start(7, makeInputs(f)), "start");
    for (int t = 0; t < 25 * 120; ++t) {
      r.step();
      const World& w = r.w();
      bad += w.fi.on || w.fi.mode || w.fi.ice_q || w.fi.pine_q || w.fi.warm_q ? 1 : 0;
      bad += w.crit[CRITTER_MAIL].in_use || w.mail.stage || w.mail.letter ? 1 : 0;
      bad += w.react.active && w.react.wave ? 1 : 0;
    }
    r.stop();
  }
  CHECK(bad == 0, "something of stage 14 ran with no inputs (%d ticks)", bad);
}

// ---- [2] -------------------------------------------------------------------------------------------

static void testArrows() {
  printf("[2] the arrows\n");
  Rig r;
  CHECK(r.start(1, makeInputs(SF_MIDDAY)), "start");
  struct Case { uint8_t a, b, c; uint8_t want, level; bool urgent; };
  const Case cases[] = {{3, 0, 0, FI_HOT, 1, false},  {4, 0, 0, FI_HOT, 2, true},     {2, 0, 0, FI_COLD, 1, false},
                        {1, 0, 0, FI_COLD, 2, true},  {1, 4, 0, FI_HOT, 2, true},     {4, 1, 0, FI_COLD, 2, true},
                        {3, 3, 4, FI_HOT, 4, true},   {2 | SS_REQ_PREDICTED, 0, 0, FI_NONE, 0, false},
                        {3, 2 | SS_REQ_PREDICTED, 0, FI_HOT, 1, false}, {0, 0, 0, FI_NONE, 0, false}};
  for (const Case& c : cases) {
    const uint8_t ask[3] = {c.a, c.b, c.c};
    fireIceSetInputs(r.w(), ask);
    CHECK(r.w().fi.want == c.want && r.w().fi.level == c.level && r.w().fi.urgent == c.urgent,
          "ask %d,%d,%d: want %d level %d urgent %d", c.a, c.b, c.c, r.w().fi.want, r.w().fi.level, (int)r.w().fi.urgent);
  }
  r.stop();
}

// ---- [3] [4] [5] -----------------------------------------------------------------------------------

static void testSpell(int fam, uint8_t ask, uint64_t seed) {
  Rig r;
  CHECK(r.start(seed, makeInputs(fam, 3)), "start");
  for (int t = 0; t < 25 * 30; ++t) r.step();
  r.ask(ask);
  const int on = waitOn(r, 25 * 600);
  CHECK(on >= 0, "%s ask %d seed %llu: never took him over", famName(fam), ask, (unsigned long long)seed);
  if (on < 0) {
    r.stop();
    return;
  }
  const Vec2 home = r.w().fi.home;
  const uint8_t pit = r.w().fi.pit_state;
  int diffs = 0, frames = 0, max_fires = 0, near_dog = 0, ice_min_q = 255, warm_max = 0, static_during = 0;
  for (int t = 0; t < 25 * 90; ++t) {
    r.step();
    const World& w = r.w();
    int n = 0;
    for (const FiFlame& q : w.fi.f) {
      if (q.k <= 0.f) continue;
      ++n;
      for (int k = 0; k < HD_COUNT; ++k) {   // [8] no fire near a dog (where it is when the fire starts, or after)
        const int s = CRITTER_STORY + k;
        if (!w.crit[s].in_use || !w.critters[s].visible) continue;
        const Vec2 p = w.critters[s].feet;
        if (q.k < 0.12f && abs((int)p.x - q.x) < 30 && abs((int)p.y - q.y) < 12) ++near_dog;
      }
    }
    if (n > max_fires) max_fires = n;
    if (w.fi.ice_q && w.fi.ice_q < ice_min_q) ice_min_q = w.fi.ice_q;
    if (w.fi.warm_q > warm_max) warm_max = w.fi.warm_q;
    if (t % 15 == 0) {
      diffs += r.fullDiff() ? 1 : 0;
      ++frames;
    }
    if (t == 25 * 80 && ask <= 2) static_during = staticDiffFromClean(r);
  }
  const bool hot = ask >= 3, urgent = ask == 1 || ask == 4;
  printf("  %s ask %d seed %llu: on after %.1f s, fires up to %d, ice step down to %d, warm %d, %d/%d frames differ\n",
         famName(fam), ask, (unsigned long long)seed, on / 25.0, max_fires, ice_min_q == 255 ? 0 : ice_min_q, warm_max, diffs,
         frames);
  CHECK(diffs == 0, "incremental frames differ from a full render (%d of %d)", diffs, frames);
  CHECK(near_dog == 0, "a fire started next to a dog (%d)", near_dog);
  if (hot) {
    CHECK(max_fires >= (urgent ? 3 : 1), "too few fires (%d)", max_fires);
    CHECK(warm_max == (urgent ? 3 : 0), "warm light %d", warm_max);
  } else {
    CHECK(ice_min_q != 255 && ice_min_q <= (urgent ? 11 : 24), "the ice did not come in (step %d)", ice_min_q);
    CHECK(static_during > 1000, "the ice is not in the cached background (%d px)", static_during);
    for (int k = 0; k < HD_COUNT; ++k) {   // [8] the dogs lie in the warm circle
      const int s = CRITTER_STORY + k;
      if (!r.w().crit[s].in_use) continue;
      const Vec2 p = r.w().critters[s].feet;
      // By the fire, but back from the big blaze by its clearance (Mike, 2026-11-12), and never closer
      // than 24 px across: clear of its flames.
      const int clear = (int)fireIceClearance(r.w());
      const int dx = abs((int)(p.x - r.w().fire.pos.x));
      CHECK(dx < 60 + clear && dx >= 24 && abs((int)(p.y - r.w().fire.pos.y)) < 30 + clear / 2, "dog %d at %.0f,%.0f, not by the fire at %.0f,%.0f",
            k, (double)p.x, (double)p.y, (double)r.w().fire.pos.x, (double)r.w().fire.pos.y);
    }
  }

  // [4] cleared, mid-episode (the episode's own time is not up yet).
  CHECK(r.w().fi.on && !r.w().fi.ending, "the episode was not under way when the arrows cleared");
  r.ask(0);
  int off = -1, diffs2 = 0;
  for (int t = 0; t < 25 * 150 && off < 0; ++t) {
    r.step();
    if (t % 10 == 0) diffs2 += r.fullDiff() ? 1 : 0;
    if (!r.w().fi.on) off = t;
  }
  CHECK(off >= 0, "never let go after the arrows cleared");
  CHECK(diffs2 == 0, "incremental frames differ while it winds down (%d)", diffs2);
  const World& w = r.w();
  printf("    let go %.1f s after clearing\n", off / 25.0);
  int left = 0;
  for (const FiFlame& q : w.fi.f) left += q.k > 0.f ? 1 : 0;
  CHECK(left == 0 && w.fi.bucket == 0 && w.fi.ice_q == 0 && w.fi.pine_q == 0 && w.fi.tent_q == 0 && w.fi.warm_q == 0,
        "left behind: fires %d bucket %d ice %d pine %d tent %d warm %d", left, w.fi.bucket, w.fi.ice_q, w.fi.pine_q, w.fi.tent_q,
        w.fi.warm_q);
  CHECK(w.tent.snow == 0.f, "frost left on the tent");
  CHECK(w.fire.state == pit, "the pit is %d, was %d", w.fire.state, pit);
  CHECK(!w.busy, "busy left set");
  CHECK(w.hiker.feet.x == home.x && w.hiker.feet.y == home.y, "he is at %.1f,%.1f, was at %.1f,%.1f", (double)w.hiker.feet.x,
        (double)w.hiker.feet.y, (double)home.x, (double)home.y);
  CHECK(staticDiffFromClean(r) == 0, "the cached background is not a clean one");
  // The story carries on: within a minute the beat moves on or he moves.
  const uint32_t tk = appTickCount(r.app);
  for (int t = 0; t < 25 * 60; ++t) r.step();
  CHECK(appTickCount(r.app) > tk + 25 * 59, "the story did not resume");
  r.stop();
}

static void testSpells() {
  printf("[3-5, 8] too hot and too cold, cleared\n");
  for (int f : {SF_MIDDAY, SF_EVENING, SF_MORNING})
    for (uint8_t a : {(uint8_t)3, (uint8_t)4, (uint8_t)2, (uint8_t)1}) testSpell(f, a, f == SF_EVENING ? 2 : 1);
  // The Lookout: never.
  Rig r;
  CHECK(r.start(3, makeInputs(SF_LOOKOUT)), "start");
  r.ask(4);
  int on = 0;
  for (int t = 0; t < 25 * 120; ++t) {
    r.step();
    on += r.w().fi.on ? 1 : 0;
  }
  CHECK(on == 0, "fire and ice ran in the Lookout");
  r.stop();
  // A change of kind mid-spell: hot, then cold (the newest arrow), then cleared.
  Rig q;
  CHECK(q.start(4, makeInputs(SF_MIDDAY)), "start");
  for (int t = 0; t < 25 * 30; ++t) q.step();
  q.ask(4);
  CHECK(waitOn(q, 25 * 600) >= 0, "never started");
  for (int t = 0; t < 25 * 40; ++t) q.step();
  q.ask(4, 1);
  bool cold = false;
  for (int t = 0; t < 25 * 120 && !cold; ++t) {
    q.step();
    cold = q.w().fi.mode == FI_COLD;
  }
  CHECK(cold, "the newest arrow (heat) did not turn it to ice");
  q.ask(0);
  CHECK(waitOff(q, 25 * 150) >= 0, "never let go after the change");
  CHECK(staticDiffFromClean(q) == 0, "ice left after the change");
  q.stop();
}

// ---- [6] -------------------------------------------------------------------------------------------

static void testDeterminism() {
  printf("[6] replay\n");
  uint32_t h[2] = {0, 0};
  for (int k = 0; k < 2; ++k) {
    Rig r;
    CHECK(r.start(11, makeInputs(SF_MIDDAY, 1)), "start");
    for (int t = 0; t < 25 * 200; ++t) {
      if (t == 25 * 20) { r.in.mail = true; r.set(); }
      if (t == 25 * 30) r.ask(4);
      if (t == 25 * 90) r.ask(0, 1);
      if (t == 25 * 150) { r.in.mail = false; r.ask(0); }
      if (t == 25 * 40 || t == 25 * 120) { r.in.passer_by = true; r.set(); }
      if (t == 25 * 43 || t == 25 * 123) { r.in.passer_by = false; r.set(); }
      r.step();
      if (t % 25 == 0) h[k] = (h[k] * 31u) ^ hashFb(r.fb);
    }
    r.stop();
  }
  CHECK(h[0] == h[1], "two runs differ (%08x %08x)", h[0], h[1]);
}

// ---- [7] -------------------------------------------------------------------------------------------

static void testBird() {
  printf("[7] the mail bird\n");
  for (int f : {SF_MIDDAY, SF_EVENING, SF_MORNING}) {
    Rig r;
    CHECK(r.start(5, makeInputs(f)), "start");
    for (int t = 0; t < 25 * 40; ++t) r.step();
    r.in.mail = true;
    r.set();
    int perched = -1, letter = -1, outside = 0, diffs = 0;
    for (int t = 0; t < 25 * 60; ++t) {
      r.step();
      const World& w = r.w();
      if (perched < 0 && w.mail.stage == MB_PERCH) perched = t;
      if (letter < 0 && w.mail.letter && w.mail.lt >= 0.8f) letter = t;
      if (w.mail.stage == MB_PERCH) {
        const Rect b = w.critter_rect[CRITTER_MAIL];
        if (b.x0 < 0 || b.x1 > SS_WIDTH || b.y0 < 0 || (b.x1 > 640 && b.y1 > 410)) ++outside;
      }
      if (t % 10 == 0) diffs += r.fullDiff() ? 1 : 0;
    }
    r.in.mail = false;
    r.set();
    int gone = -1;
    for (int t = 0; t < 25 * 10 && gone < 0; ++t) {
      r.step();
      if (t % 5 == 0) diffs += r.fullDiff() ? 1 : 0;
      if (!r.w().crit[CRITTER_MAIL].in_use) gone = t;
    }
    printf("  %s: perched after %.1f s, letter down %.1f s, gone %.1f s after collection\n", famName(f), perched / 25.0, letter / 25.0,
           gone / 25.0);
    CHECK(perched >= 0 && perched < 25 * 6, "never perched (or late: %d)", perched);
    CHECK(letter >= 0 && letter < 25 * 8, "no letter on the ground (%d)", letter);
    CHECK(outside == 0, "perched out of the frame or in the chip's corner (%d)", outside);
    CHECK(gone >= 0 && gone < 25 * 5, "it did not leave (%d)", gone);
    CHECK(!r.w().mail.letter, "the letter stayed after collection");
    CHECK(diffs == 0, "frames differ from a full render (%d)", diffs);
    r.stop();
  }
  // Waiting as the screensaver starts: simply there, no flight in.
  {
    Inputs in = makeInputs(SF_EVENING);
    in.mail = true;
    Rig r;
    CHECK(r.start(6, in), "start");
    int flew = 0, there = -1;
    for (int t = 0; t < 25 * 60; ++t) {
      r.step();
      flew += r.w().mail.stage == MB_IN ? 1 : 0;
      if (there < 0 && r.w().mail.stage == MB_PERCH && r.w().mail.letter) there = t;
    }
    CHECK(flew == 0 && there >= 0, "with mail waiting at the start: flew %d, there at %d", flew, there);
    CHECK(r.w().crit[CRITTER_MAIL].tag == CT_MAIL && (r.w().crit[CRITTER_MAIL].flags & CF_BRAVE), "slot tag");
    r.stop();
  }
  // The Lookout: none.
  {
    Inputs in = makeInputs(SF_LOOKOUT);
    in.mail = true;
    Rig r;
    CHECK(r.start(6, in), "start");
    int used = 0;
    for (int t = 0; t < 25 * 60; ++t) {
      r.step();
      used += r.w().crit[CRITTER_MAIL].in_use ? 1 : 0;
    }
    CHECK(used == 0, "a mail bird in the Lookout");
    r.stop();
  }
}

// ---- [9] -------------------------------------------------------------------------------------------

static void testWave() {
  printf("[9] the wave\n");
  int waves = 0, tries = 0, too_long = 0;
  for (uint64_t seed = 1; seed <= 4; ++seed) {
    Rig r;
    CHECK(r.start(seed, makeInputs(SF_MIDDAY)), "start");
    for (int t = 0; t < 25 * 40; ++t) r.step();
    for (int k = 0; k < 6; ++k) {   // someone walks up for 3 s, every 2 minutes
      r.in.passer_by = true;
      r.set();
      ++tries;
      int started = 0, run = 0;
      for (int t = 0; t < 75; ++t) {
        const bool before = r.w().react.active;
        r.step();
        if (!before && r.w().react.active && r.w().react.wave) ++started;
        run += r.w().react.active ? 1 : 0;
      }
      r.in.passer_by = false;
      r.set();
      for (int t = 0; t < 25 * 117; ++t) {
        const bool before = r.w().react.active;
        r.step();
        if (!before && r.w().react.active && r.w().react.wave) ++started;   // a late wave (he waits up to 8 s)
        run += r.w().react.active && r.w().react.wave ? 1 : 0;
      }
      CHECK(started <= 1, "waved %d times at one passer-by", started);
      waves += started;
      too_long += run > kWaveTicks + 2 ? 1 : 0;
    }
    r.stop();
  }
  printf("  waved at %d of %d passers-by (the rest came at a busy moment)\n", waves, tries);
  CHECK(waves * 3 >= tries, "too few waves (%d of %d)", waves, tries);
  CHECK(too_long == 0, "a wave ran long (%d)", too_long);
  // Not in an urgent spell.
  Rig r;
  CHECK(r.start(1, makeInputs(SF_MIDDAY)), "start");
  for (int t = 0; t < 25 * 30; ++t) r.step();
  r.ask(4);
  CHECK(waitOn(r, 25 * 600) >= 0, "never started");
  int during = 0;
  r.in.passer_by = true;
  r.set();
  for (int t = 0; t < 25 * 20; ++t) {
    r.step();
    during += r.w().react.active ? 1 : 0;
  }
  CHECK(during == 0, "he waved in an urgent spell");
  r.stop();
}

// ---- [10] ------------------------------------------------------------------------------------------

static void testMemory() {
  printf("[10] memory\n");
  printf("  sizeof(App) %u of %u (FireIce %u, MailBird %u)\n", appAppBytes(), (unsigned)SS_APP_BYTES, (unsigned)sizeof(FireIce),
         (unsigned)sizeof(MailBird));
  CHECK(appAppBytes() <= SS_APP_BYTES, "App too big");
  CHECK(appAppBytes() <= 29056u + 1200u, "stage 14 over its 1,200-byte budget (%u)", appAppBytes());
}

// ---- [11] ------------------------------------------------------------------------------------------

// Where the story stood as an episode began, to check it stood still and he is back.
struct Held {
  Vec2 home;
  uint8_t pit;
  uint32_t story;      // appTickCount
  const Step* steps;
  uint8_t i;
  uint16_t ticks;
  uint32_t loop;
};
static Held heldNow(Rig& r) {
  const World& w = r.w();
  return Held{w.fi.home, w.fi.pit_state, appTickCount(r.app), w.spine.run.steps, w.spine.run.i, w.spine.run.ticks, w.memory.loop_count};
}
// After an episode let go: nothing left, he is back, the story is where it stood.
static void checkClean(Rig& r, const Held& h, const char* tag) {
  const World& w = r.w();
  int left = 0;
  for (const FiFlame& q : w.fi.f) left += q.k > 0.f ? 1 : 0;
  CHECK(left == 0 && w.fi.bucket == 0 && w.fi.ice_q == 0 && w.fi.pine_q == 0 && w.fi.tent_q == 0 && w.fi.warm_q == 0,
        "%s: left behind: fires %d bucket %d ice %d pine %d tent %d warm %d", tag, left, w.fi.bucket, w.fi.ice_q, w.fi.pine_q,
        w.fi.tent_q, w.fi.warm_q);
  CHECK(w.fi.steam.t < 0.f && w.fi.splash_t < 0.f && w.fi.breath_t < 0.f, "%s: a puff left", tag);
  CHECK(w.tent.snow == 0.f || w.weather.kind == WX_SNOW, "%s: frost left on the tent", tag);
  CHECK(w.fire.state == h.pit, "%s: the pit is %d, was %d", tag, w.fire.state, h.pit);
  CHECK(!w.busy, "%s: busy left set", tag);
  CHECK(w.fx.lean == 0.f && w.fx.reach == 0.f && w.fx.stomp == 0.f && w.fx.arm_w[0] == 0.f && w.fx.arm_w[1] == 0.f,
        "%s: his offsets left set", tag);
  CHECK(w.hiker.carry == CARRY_NONE, "%s: something left in his hands", tag);
  // Put back where he was; a walk he was on sets off again in the same tick (one step's worth).
  const float tol = w.hiker.walking ? 4.f : 0.f;
  CHECK(fabsf(w.hiker.feet.x - h.home.x) <= tol && fabsf(w.hiker.feet.y - h.home.y) <= tol, "%s: he is at %.1f,%.1f, was at %.1f,%.1f", tag,
          (double)w.hiker.feet.x, (double)w.hiker.feet.y, (double)h.home.x, (double)h.home.y);
  CHECK(appTickCount(r.app) == h.story, "%s: the story moved during the episode (%u to %u)", tag, h.story, appTickCount(r.app));
  CHECK(w.spine.run.steps == h.steps && w.spine.run.i == h.i && w.spine.run.ticks == h.ticks, "%s: not back in his script where he was", tag);
  CHECK(w.memory.loop_count == h.loop, "%s: the loop changed under an episode", tag);
  CHECK(staticDiffFromClean(r) == 0, "%s: the cached background is not a clean one", tag);
}

static void testEpisodeLength() {
  printf("[11] an episode's length, and after it\n");
  for (int fam : {SF_MIDDAY, SF_EVENING, SF_MORNING})
    for (uint8_t ask : {(uint8_t)3, (uint8_t)4, (uint8_t)2, (uint8_t)1}) {
      const bool urgent = ask == 1 || ask == 4;
      Rig r;
      CHECK(r.start(fam == SF_EVENING ? 2 : 1, makeInputs(fam, fam == SF_MIDDAY ? 3 : 0)), "start");
      for (int t = 0; t < 25 * 30; ++t) r.step();
      r.ask(ask);
      const int on = waitOn(r, 25 * 600);
      CHECK(on >= 0, "%s ask %d: never started", famName(fam), ask);
      if (on < 0) {
        r.stop();
        continue;
      }
      const Held h = heldNow(r);
      int len = 1, ending_at = -1, diffs = 0, checked = 0;
      // Every frame through the start (its first 2 s), the start of the wind-down and the end.
      diffs += r.fullDiff() ? 1 : 0;
      ++checked;
      while (r.w().fi.on && len < 25 * 400) {
        r.step();
        ++len;
        if (ending_at < 0 && r.w().fi.ending) ending_at = len;
        const bool near = len < 50 || (ending_at >= 0 && len - ending_at < 50) || !r.w().fi.on || len % 25 == 0;
        if (near) {
          diffs += r.fullDiff() ? 1 : 0;
          ++checked;
        }
      }
      for (int t = 0; t < 50; ++t) {
        r.step();
        diffs += r.fullDiff() ? 1 : 0;
        ++checked;
      }
      const float s = (float)len / 25.f;
      printf("  %s ask %d: on after %.1f s, winds down from %.1f s, lets go after %.1f s; %d/%d frames differ\n", famName(fam), ask,
             on / 25.0, ending_at / 25.0, (double)s, diffs, checked);
      CHECK(urgent ? (s >= 160.f && s <= 180.f) : (s >= 110.f && s <= 130.f), "%s ask %d: an episode of %.1f s", famName(fam), ask,
            (double)s);
      CHECK(diffs == 0, "%s ask %d: incremental frames differ from a full render (%d of %d)", famName(fam), ask, diffs, checked);
      // The 50 ticks above: nothing new started, and the story moved on.
      CHECK(!r.w().fi.on, "%s ask %d: it started again straight away", famName(fam), ask);
      CHECK(appTickCount(r.app) == h.story + 50 || r.w().memory.loop_count != h.loop, "%s ask %d: the story did not carry on", famName(fam), ask);
      r.stop();
      // The same again, checked at the tick it lets go.
      Rig q;
      CHECK(q.start(fam == SF_EVENING ? 2 : 1, makeInputs(fam, fam == SF_MIDDAY ? 3 : 0)), "start");
      for (int t = 0; t < 25 * 30; ++t) q.tickOnly();
      q.ask(ask);
      while (!q.w().fi.on) q.tickOnly();
      const Held hq = heldNow(q);
      while (q.w().fi.on) q.tickOnly();
      q.resync();
      char tag[48];
      snprintf(tag, sizeof tag, "%s ask %d", famName(fam), ask);
      checkClean(q, hq, tag);
      q.stop();
    }
  // Mild to urgent while one is under way: it runs to the urgent length; no extra one after it.
  Rig r;
  CHECK(r.start(1, makeInputs(SF_MIDDAY)), "start");
  for (int t = 0; t < 25 * 30; ++t) r.tickOnly();
  r.ask(2);
  while (!r.w().fi.on) r.tickOnly();
  const uint32_t loop = r.w().memory.loop_count;
  int len = 0;
  for (; len < 25 * 60; ++len) r.tickOnly();
  r.ask(1);
  while (r.w().fi.on && len < 25 * 400) {
    r.tickOnly();
    ++len;
  }
  int again = 0;
  bool prev = false;
  while (r.w().memory.loop_count == loop) {
    r.tickOnly();
    again += r.w().fi.on && !prev ? 1 : 0;
    prev = r.w().fi.on;
  }
  printf("  mild to urgent at 60 s: let go after %.1f s, %d more in the loop\n", len / 25.0, again);
  CHECK(len >= 25 * 160 && len <= 25 * 180, "mild to urgent: an episode of %.1f s", len / 25.0);
  CHECK(again == 0, "mild to urgent under way earned another one (%d)", again);
  r.stop();
}

// ---- [12] ------------------------------------------------------------------------------------------

static void testOncePerLoop() {
  printf("[12] one episode a loop\n");
  struct Case { int fam; uint8_t ask; uint64_t seed; };
  for (const Case& c : {Case{SF_MIDDAY, 2, 2}, Case{SF_MIDDAY, 4, 2}, Case{SF_EVENING, 1, 3}, Case{SF_MORNING, 3, 5}}) {
    Inputs in = makeInputs(c.fam);
    in.temp_ask[0] = c.ask;
    Rig r;
    CHECK(r.start(c.seed, in), "start");
    const int kLoops = 5;
    int starts[kLoops] = {0}, played[kLoops] = {0}, planned[kLoops] = {0};
    int loop = 0, last = -1, ended_on = 0;
    long held = 0, ticks = 0;
    planned[0] = r.w().plan.n_majors;
    bool prev = false;
    while (loop < kLoops && ticks < 25L * 3600 * 2) {
      const bool before = r.w().fi.on;
      const bool ended = r.tickOnly();
      ++ticks;
      if (ended) {
        ended_on += before ? 1 : 0;
        if (++loop >= kLoops) break;
        planned[loop] = r.w().plan.n_majors;
        last = -1;
        prev = false;
      }
      const World& w = r.w();
      held += w.fi.on ? 1 : 0;
      starts[loop] += w.fi.on && !prev ? 1 : 0;
      prev = w.fi.on;
      const int id = w.major.on ? w.major.id : -1;
      if (id != last && id >= 0) {
        bool in_plan = false;
        for (int k = 0; k < w.plan.n_majors; ++k) in_plan = in_plan || w.plan.major[k] == id;
        played[loop] += in_plan ? 1 : 0;
      }
      last = id;
    }
    printf("  %s ask %d seed %llu: %d loops, episodes", famName(c.fam), c.ask, (unsigned long long)c.seed, loop);
    for (int k = 0; k < kLoops; ++k) printf(" %d", starts[k]);
    printf(", planned majors played");
    for (int k = 0; k < kLoops; ++k) printf(" %d/%d", played[k], planned[k]);
    printf(", story held %.0f%% of the time\n", 100.0 * (double)held / (double)ticks);
    CHECK(loop == kLoops, "only %d loops ran", loop);
    for (int k = 0; k < kLoops; ++k) {
      CHECK(starts[k] == 1, "%s ask %d: loop %d had %d episodes", famName(c.fam), c.ask, k, starts[k]);
      CHECK(played[k] == planned[k], "%s ask %d: loop %d played %d of %d planned majors", famName(c.fam), c.ask, k, played[k], planned[k]);
    }
    CHECK(ended_on == 0, "a loop ended with an episode running (%d)", ended_on);
    r.stop();
  }
}

// ---- [13] ------------------------------------------------------------------------------------------

// Runs until the loop's first episode has let go; false if the loop ended first.
static bool firstEpisode(Rig& r, uint32_t& loop) {
  while (!r.w().fi.on) r.tickOnly();
  loop = r.w().memory.loop_count;
  while (r.w().fi.on) r.tickOnly();
  return r.w().memory.loop_count == loop;
}
// Episodes started in loop `loop` from now to its end (or within `limit` ticks), and the kind of the first.
static int episodesUntilLoopEnd(Rig& r, uint32_t loop, int limit, uint8_t* first_mode, int* first_at) {
  int n = 0;
  bool prev = r.w().fi.on;
  for (int t = 0; t < limit && r.w().memory.loop_count == loop; ++t) {
    if (r.tickOnly()) break;
    if (r.w().fi.on && !prev) {
      if (n == 0 && first_mode) *first_mode = r.w().fi.mode;
      if (n == 0 && first_at) *first_at = t;
      ++n;
    }
    prev = r.w().fi.on;
  }
  return n;
}

static void testNewRequest() {
  printf("[13] a new request in the loop\n");
  struct Case { const char* name; uint8_t a0, a1; uint8_t b0, b1; bool clear_between; int want; uint8_t mode; };
  const Case cases[] = {
      {"mild to urgent", 2, 0, 1, 0, false, 1, FI_COLD},     {"hot to cold", 3, 0, 3, 2, false, 1, FI_COLD},
      {"one more arrow", 2, 0, 2, 2, false, 1, FI_COLD},     {"back after all cleared", 3, 0, 3, 0, true, 1, FI_HOT},
      {"one of two gone", 2, 2, 2, 0, false, 0, FI_NONE},    {"cleared", 4, 0, 0, 0, false, 0, FI_NONE},
      {"mild to urgent, hot", 3, 0, 4, 0, false, 1, FI_HOT},
  };
  for (const Case& c : cases) {
    Inputs in = makeInputs(SF_MIDDAY);
    in.temp_ask[0] = c.a0;
    in.temp_ask[1] = c.a1;
    Rig r;
    CHECK(r.start(4, in), "start");
    uint32_t loop = 0;
    const bool same = firstEpisode(r, loop);
    CHECK(same, "%s: the loop ended in its first episode", c.name);
    // No second one while the arrows stay.
    int quiet = 0;
    for (int t = 0; t < 25 * 30 && r.w().memory.loop_count == loop; ++t) {
      r.tickOnly();
      quiet += r.w().fi.on ? 1 : 0;
    }
    CHECK(quiet == 0, "%s: a second episode with the arrows unchanged", c.name);
    if (c.clear_between) {
      r.ask(0);
      for (int t = 0; t < 25 * 20; ++t) r.tickOnly();
    }
    r.ask(c.b0, c.b1);
    uint8_t mode = FI_NONE;
    int at = -1;
    const int n = episodesUntilLoopEnd(r, loop, 25 * 3600, &mode, &at);
    printf("  %s: %d more in the loop%s", c.name, n, n ? "" : "\n");
    if (n) printf(" (%s, %.1f s after the change)\n", mode == FI_HOT ? "hot" : "cold", at / 25.0);
    CHECK(n == c.want, "%s: %d more episodes in the loop, wanted %d", c.name, n, c.want);
    if (c.want) CHECK(mode == c.mode, "%s: the new one is mode %d", c.name, mode);
    r.stop();
  }
}

// ---- [14] ------------------------------------------------------------------------------------------

static void testEpisodeReplay() {
  printf("[14] replay through episodes\n");
  uint32_t h[2] = {0, 0};
  for (int k = 0; k < 2; ++k) {
    Inputs in = makeInputs(SF_MIDDAY, 2);
    in.temp_ask[0] = 4;
    Rig r;
    CHECK(r.start(6, in), "start");
    for (int t = 0; t < 25 * 400; ++t) {
      if (t == 25 * 250) r.ask(4, 1);   // the other kind after the first episode: one more
      r.step();
      if (t % 25 == 0) h[k] = (h[k] * 31u) ^ hashFb(r.fb);
    }
    r.stop();
  }
  CHECK(h[0] == h[1], "two runs differ (%08x %08x)", h[0], h[1]);
}

// ---- [15] ------------------------------------------------------------------------------------------

static void testMajorsPerHour() {
  printf("[15] majors per hour, midday\n");
  double per_hour[3] = {0, 0, 0}, held[3] = {0, 0, 0};
  const uint8_t asks[3] = {0, 2, 1};
  const int kSeeds = 12;
  for (int a = 0; a < 3; ++a) {
    int plays = 0;
    long hold = 0, total = 0;
    for (uint64_t seed = 1; seed <= (uint64_t)kSeeds; ++seed) {
      Inputs in;
      memset(&in, 0, sizeof in);
      in.have_outside = true;
      in.chip = true;
      in.mood = 3.f;
      in.visitors = true;
      in.temp_ask[0] = asks[a];
      Rig r;
      CHECK(r.start(seed, in), "start");
      int last = -1;
      for (int t = 0; t < 25 * 3600; ++t) {
        r.tickOnly();
        const World& w = r.w();
        ++total;
        hold += w.fi.on ? 1 : 0;
        const int id = w.major.on ? w.major.id : -1;
        plays += id != last && id >= 0 ? 1 : 0;
        last = id;
      }
      r.stop();
    }
    per_hour[a] = (double)plays / kSeeds;
    held[a] = 100.0 * (double)hold / (double)total;
  }
  printf("  no arrow %.1f an hour; mild too-cold %.1f (%.0f%%, story held %.0f%%); urgent too-cold %.1f (%.0f%%, held %.0f%%)\n",
         per_hour[0], per_hour[1], 100.0 * per_hour[1] / per_hour[0], held[1], per_hour[2], 100.0 * per_hour[2] / per_hour[0], held[2]);
  CHECK(held[0] == 0.0, "the story held with no arrow");
  CHECK(held[1] < 22.0 && held[2] < 30.0, "the story held too long (mild %.0f%%, urgent %.0f%%)", held[1], held[2]);
  // The episode costs only the time it holds the story: the loops are longer, the content the same.
  CHECK(per_hour[1] >= 0.72 * per_hour[0] && per_hour[2] >= 0.62 * per_hour[0], "majors too rare with an arrow (%.1f, %.1f of %.1f)",
        per_hour[1], per_hour[2], per_hour[0]);
}

int main() {
  testIdle();
  testArrows();
  testMemory();
  testBird();
  testDeterminism();
  testWave();
  testSpells();
  testEpisodeLength();
  testOncePerLoop();
  testNewRequest();
  testEpisodeReplay();
  testMajorsPerHour();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
