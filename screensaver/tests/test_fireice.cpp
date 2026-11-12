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
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/fireice.h"
#include "scene/mailbird.h"
#include "scene/react.h"
#include "scene/spine.h"
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

  // [4] cleared.
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
        r.step();
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

int main() {
  testIdle();
  testArrows();
  testMemory();
  testBird();
  testDeterminism();
  testWave();
  testSpells();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
