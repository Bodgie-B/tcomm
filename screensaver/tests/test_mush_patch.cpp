// The mushroom patch after rain (stage 22 step B4; MUSHROOM_PLAN.md sections 3.1, 4, 8.1 and 11.1):
//   - the growth rule on the packed byte: wet, wet, then six dry loops give 1, 1, 2 .. 6, 6; a picked-over
//     patch counts down to none; rain on it starts a new one; the host's minutes agree with the loops; the
//     size table; mushWillPlay only with the switch on and only for a ripe, dry, midday day;
//   - the layout over 1,000 landscapes and every stage: dry ground 9 px clear of the water, at y 474 or above,
//     x 612 or less, pickables at x 530 or less, Nx in 420..501, none closer than 6 px, nothing in the
//     status chip's capsule; pure (twice the same);
//   - the draw: every mushroom at every size, drooping, tipped and toned inside its bounds; the layer
//     inside patchBounds; a strip drawn alone equals the same rows of a whole frame; lifted pickables
//     left out; nothing at all at stage 0, before and during the build-up;
//   - the app: with carry 0 the landscape has no patch; with carry 6 the patch is there from the memory.
// Plain executable; returns non-zero on failure.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>

#include "mush_patch.h"
#include "scene/landscape.h"
#include "scene/patch.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

static int g_fail = 0;
static long g_checks = 0;
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

static const Pixel kBlank = 0x1234;

static void testRule() {
  // wet, wet, dry x 6: 1, 1, 2, 3, 4, 5, 6, 6
  uint8_t b = 0;
  const bool wet[8] = {true, true, false, false, false, false, false, false};
  const int want[8] = {1, 1, 2, 3, 4, 5, 6, 6};
  for (int k = 0; k < 8; ++k) {
    b = mushAfterLoop(b, wet[k]);
    CHECK(mushStage(b) == want[k], "loop %d stage %d want %d", k, mushStage(b), want[k]);
  }
  CHECK(mushSoak(b), "a grown patch is soaked");
  // A ripe patch waits through dry loops and rain alike.
  CHECK(mushStage(mushAfterLoop(b, false)) == MUSH_RIPE, "ripe stays ripe");
  CHECK(mushStage(mushAfterLoop(b, true)) == MUSH_RIPE, "rain on a ripe patch changes nothing");
  // Nothing grows while it rains.
  const uint8_t three = mushPack(3, 0, true);
  CHECK(mushAfterLoop(three, true) == three, "rain holds a growing patch");
  // Picked: six dry loops count it down to none, soak and all.
  uint8_t p = mushAfterPick(b);
  CHECK(mushStage(p) == MUSH_PICKED && mushLeft(p) == MUSH_PICKED_LOOPS, "picked: stage 7, left 6");
  int loops = 0;
  while (mushStage(p) != 0 && loops < 20) {
    CHECK(mushStage(p) == MUSH_PICKED, "still picked over at loop %d", loops);
    CHECK(mushWilting(p) == (mushLeft(p) <= MUSH_WILT_LOOPS), "droops only in the last two (left %d)", mushLeft(p));
    p = mushAfterLoop(p, false);
    ++loops;
  }
  CHECK(loops == MUSH_PICKED_LOOPS && p == 0, "picked counts %d down to none (%d loops, byte %u)", MUSH_PICKED_LOOPS, loops,
        (unsigned)p);
  // Rain on a picked-over patch starts a new one.
  const uint8_t np = mushAfterLoop(mushAfterPick(b), true);
  CHECK(mushStage(np) == 1 && mushLeft(np) == 0 && mushSoak(np), "rain on a picked patch: new buttons (byte %u)", (unsigned)np);
  // Dry loops on a bare bank do nothing; a stage without soak does not grow.
  CHECK(mushAfterLoop(0, false) == 0, "a bare bank stays bare");
  CHECK(mushAfterLoop(mushPack(2, 0, false), false) == mushPack(2, 0, false), "no soak, no growth");
  // Every byte the rule can see maps to a well-formed byte (bit 7 clear, left only when picked).
  for (int v = 0; v < 128; ++v) {
    for (int w = 0; w < 2; ++w) {
      const uint8_t o = mushAfterLoop((uint8_t)v, w != 0);
      CHECK((o & 0x80u) == 0, "bit 7 stays clear (%d -> %u)", v, (unsigned)o);
      if (mushStage(o) != MUSH_PICKED && mushStage((uint8_t)v) != mushStage(o))
        CHECK(mushLeft(o) == 0, "left only counts on a picked patch (%d -> %u)", v, (unsigned)o);
    }
    // The host's minutes: k steps of 10 dry minutes are k dry loops; rain is one wet loop.
    for (int k = 0; k <= 9; ++k) {
      uint8_t a = (uint8_t)v;
      for (int i = 0; i < k; ++i) a = mushAfterLoop(a, false);
      CHECK(mushAfterMinutes((uint8_t)v, (uint32_t)(10 * k + 7), false) == a, "minutes %d agree with %d loops (byte %d)",
            10 * k + 7, k, v);
    }
    CHECK(mushAfterMinutes((uint8_t)v, 600, true) == mushAfterLoop((uint8_t)v, true), "raining minutes are one wet loop (%d)", v);
    CHECK(mushAfterMinutes((uint8_t)v, 100000, false) == mushAfterMinutes((uint8_t)v, 80, false), "a long night settles (%d)", v);
  }
  // The size table: two buttons at stage 1, all six full at ripe, the pickables gone once picked.
  int s1 = 0;
  for (int i = 0; i < 6; ++i) s1 += mushStep(1, i) > 0 ? 1 : 0;
  CHECK(s1 == 2, "stage 1: two buttons (%d)", s1);
  for (int i = 0; i < 6; ++i) {
    CHECK(mushStep(MUSH_RIPE, i) == 4, "ripe: mushroom %d full grown", i);
    CHECK(mushStep(mushAfterPick(MUSH_RIPE), i) == ((i == 0 || i == 1 || i == 5) ? 4 : 0), "picked: role %d", i);
    for (int st = 1; st < MUSH_RIPE; ++st) CHECK(mushStep((uint8_t)(st + 1), i) >= mushStep((uint8_t)st, i), "grows, never shrinks");
  }
  // The plan's question: never with the switch off; with it on only ripe + midday + day + dry.
  for (int st = 0; st < 8; ++st)
    for (int m = 0; m < 16; ++m) {
      const bool mid = (m & 1) != 0, night = (m & 2) != 0, dw = (m & 4) != 0;
      const bool want_play = ssS22Switch() != 0 && st == MUSH_RIPE && mid && !night && !dw;
      CHECK(mushWillPlay(mushPack((uint8_t)st, 0, true), mid, night, dw) == want_play, "mushWillPlay st %d m %d", st, m);
    }
}

static void makeLand(uint64_t seed, Landscape& ls) {
  memset(&ls, 0, sizeof ls);
  Rng rng;
  rng.seed(seed);
  landscapeGenerate(ls, rng);
}

static void testLayout() {
  int dropped_pick = 0, layouts = 0, worst_gap = 1000;
  float worst_cap_clear = 1000.f;
  for (uint64_t seed = 1; seed <= 1000; ++seed) {
    Landscape ls;
    makeLand(seed, ls);
    for (int st = 1; st <= 8; ++st) {
      // 8: the picked-over patch's drooping last loops (the whole byte in patch_stage)
      ls.patch_stage = st == 8 ? mushPack(MUSH_PICKED, 2, true) : (uint8_t)st;
      LsPatch p, q;
      patchLayout(ls, p);
      patchLayout(ls, q);
      CHECK(memcmp(&p, &q, sizeof p) == 0, "pure (seed %llu st %d)", (unsigned long long)seed, st);
      ++layouts;
      CHECK(p.nx >= 420 && p.nx <= 501, "Nx %d in 420..501 (seed %llu)", p.nx, (unsigned long long)seed);
      CHECK(p.wilt == (st == 8 ? 1 : 0), "wilt only in the last loops (seed %llu st %d)", (unsigned long long)seed, st);
      int n = 0;
      for (int i = 0; i < MUSH_PATCH; ++i) {
        const LsShroom& m = p.m[i];
        if (st == MUSH_RIPE && i >= MUSH_PICK0 && i < MUSH_PICK0 + 3 && m.step == 0) ++dropped_pick;
        if (!m.step) continue;
        ++n;
        float cy, half;
        if (landscapeStreamSpan(ls, (float)m.x, cy, half)) {
          CHECK((float)m.y >= cy + half + 8.5f, "dry ground (seed %llu st %d i %d: foot %d water to %.1f)", (unsigned long long)seed,
                st, i, m.y, (double)(cy + half));
          const Rect b = patchShroomBounds(patchLook(p, i));
          worst_cap_clear = fminf(worst_cap_clear, (float)b.y0 - (cy + half));
        }
        CHECK(m.y <= 474, "y %d <= 474", m.y);
        CHECK(m.x <= 612, "x %d <= 612", m.x);
        if (i >= MUSH_PICK0 && i < MUSH_PICK0 + 3) CHECK(m.x <= 530, "pickable x %d <= 530", m.x);
        const Rect b = patchShroomBounds(patchLook(p, i));
        CHECK(b.x1 <= 624 || b.y1 <= 424 || b.y0 >= 464, "not in the chip's capsule (seed %llu i %d: %d,%d..%d,%d)",
              (unsigned long long)seed, i, b.x0, b.y0, b.x1, b.y1);
        CHECK(!(b.x1 > LS_QUIET_X0 && b.y1 > LS_QUIET_Y0), "out of the quiet corner");
        for (int j = i + 1; j < MUSH_PATCH; ++j) {
          if (!p.m[j].step) continue;
          const int dx = m.x - p.m[j].x, dy = m.y - p.m[j].y;
          const int d2 = dx * dx + dy * dy;
          if (d2 < worst_gap) worst_gap = d2;
          CHECK(d2 >= 36, "no two closer than 6 px (seed %llu st %d: %d and %d)", (unsigned long long)seed, st, i, j);
        }
        // and the mother
        const int mx = m.x - ls.mush_x, my = m.y - ls.mush_y;
        CHECK(mx * mx + my * my >= 100, "10 px from the mother (seed %llu i %d)", (unsigned long long)seed, i);
      }
      CHECK(n == p.n, "n counts the standing ones");
      if (st == MUSH_RIPE) {
        CHECK(p.ny >= p.m[MUSH_PICK0].y + 2 && p.ny >= p.m[MUSH_PICK0 + 2].y + 2, "he crouches in front of the pickables");
        CHECK(patchNear(ls, (float)p.m[0].x, (float)p.m[0].y, 1.f) == (p.m[0].step != 0), "patchNear sees a standing one");
      }
    }
  }
  printf("  layout: %d layouts; ripe pickables left out by a rock: %d of 3000; closest pair %.1f px; worst cap to water %.1f px\n",
         layouts, dropped_pick, (double)sqrtf((float)worst_gap), (double)worst_cap_clear);
  CHECK(dropped_pick <= 150, "a rock takes a pickable in at most 5 %% of seeds (%d)", dropped_pick);
}

// Pixels changed outside `r` on a blank surface.
static int outside(const Pixel* px, const Rect& r) {
  int n = 0;
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x)
      if (px[(size_t)y * SS_WIDTH + x] != kBlank && (x < r.x0 || x >= r.x1 || y < r.y0 || y >= r.y1)) ++n;
  return n;
}

static void blank(Pixel* px) {
  for (size_t i = 0; i < (size_t)SS_WIDTH * SS_HEIGHT; ++i) px[i] = kBlank;
}

static void testDraw(Pixel* px) {
  Palette pal;
  WeatherTint wt;
  memset(&wt, 0, sizeof wt);
  computePalette(3.f, wt, pal);
  Surface s{px, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  // Every size, drooping or not, tipped, leaning, toned.
  const float tilts[5] = {0.f, 0.04f, -0.12f, 0.25f, -0.25f};
  for (int st = 1; st <= 4; ++st)
    for (int wl = 0; wl < 2; ++wl)
      for (float tilt : tilts)
        for (float sc : {0.2f, 0.5f, 0.8f, 1.f, 1.6f}) {
          ShroomLook m{300.5f, 300.f, sc, (uint8_t)st, (uint8_t)wl, wl ? -3.f : 2.f, tilt, wl ? -1.f : 1.f};
          blank(px);
          patchShroomDraw(m, s, pal, s.bounds());
          const Rect b = patchShroomBounds(m);
          CHECK(outside(px, b) == 0, "inside its bounds (step %d wilt %d tilt %.2f scale %.1f: %d px out)", st, wl, (double)tilt,
                (double)sc, outside(px, b));
          bool any = false;
          for (size_t i = 0; i < (size_t)SS_WIDTH * SS_HEIGHT && !any; ++i) any = px[i] != kBlank;
          CHECK(any, "draws something (step %d scale %.1f)", st, (double)sc);
        }
  // The layer: inside patchBounds, at each stage and through the build-up, for a few landscapes.
  for (uint64_t seed = 1; seed <= 12; ++seed) {
    Landscape ls;
    makeLand(seed * 7, ls);
    for (int st = 0; st <= 8; ++st) {
      ls.patch_stage = st == 8 ? mushPack(MUSH_PICKED, 1, true) : (uint8_t)st;
      for (float t : {0.f, 0.3f, 0.55f, 0.8f, 1.f}) {
        blank(px);
        patchDrawStatic(ls, s, pal, s.bounds(), t);
        const Rect b = patchBounds(ls);
        CHECK(outside(px, b) == 0, "the layer inside patchBounds (seed %llu st %d t %.2f)", (unsigned long long)seed, st, (double)t);
        if (st == 0) CHECK(outside(px, Rect{0, 0, 0, 0}) == 0, "stage 0 draws nothing");
      }
      CHECK(st != 0 || (patchBounds(ls).empty() && !patchNear(ls, (float)ls.mush_x, (float)ls.mush_y, 400.f)),
            "stage 0: no bounds, nothing near");
    }
    // A strip drawn alone equals the same rows of the whole frame (the cache draws strip by strip).
    ls.patch_stage = MUSH_RIPE;
    blank(px);
    patchDrawStatic(ls, s, pal, s.bounds(), 1.f);
    static Pixel strip[SS_WIDTH * 32];
    for (int band = 12; band < 15; ++band) {
      for (int i = 0; i < SS_WIDTH * 32; ++i) strip[i] = kBlank;
      Surface ss{strip, SS_WIDTH, 32, SS_WIDTH, band * 32};
      patchDrawStatic(ls, ss, pal, ss.bounds(), 1.f);
      CHECK(memcmp(strip, px + (size_t)band * 32 * SS_WIDTH, sizeof strip) == 0, "strip %d equals the frame (seed %llu)", band,
            (unsigned long long)seed);
    }
    // Lifted pickables are the major's: the cache leaves them out, the rest is unchanged.
    LsPatch p;
    patchLayout(ls, p);
    ls.patch_lifted = 7;
    blank(px);
    patchDrawStatic(ls, s, pal, s.bounds(), 1.f);
    const Rect kept = patchBounds(ls, false);
    CHECK(outside(px, kept) == 0, "lifted ones not drawn (seed %llu)", (unsigned long long)seed);
    ls.patch_lifted = 0;
    const Vec2 c = patchSpot(ls);
    CHECK(c.x > 400.f && c.x < 640.f && c.y > 430.f && c.y < 478.f, "the spot is on the patch (%.0f, %.0f)", (double)c.x, (double)c.y);
  }
}

static void* hostAlloc(size_t n) { return calloc(1, n); }
static void hostFree(void* p) { free(p); }

static void testApp() {
  const HostAlloc al = {hostAlloc, hostFree, nullptr};
  for (int carry = 0; carry <= 6; carry += 6) {
    Inputs in;
    memset(&in, 0, sizeof in);
    in.mood = 3.f;
    in.carry = (uint16_t)carry;
    void* mem = calloc(1, SS_APP_BYTES);
    App* app = appStart(mem, al, 39, in);
    const World* w = appWorld(app);
    LsPatch p;
    patchLayout(w->land, p);
    if (carry == 0) {
      CHECK(w->land.patch_stage == 0 && p.n == 0, "carry 0: no patch");
      CHECK(appCarry(app) == 0, "carry 0 comes back 0");
    } else {
      CHECK(w->land.patch_stage == MUSH_RIPE && p.n >= 5, "carry 6: a ripe patch (%d standing)", p.n);
      CHECK((appCarry(app) & 0xFF) == MUSH_RIPE, "the byte is carried on");
    }
    appStop(app, al);
    free(mem);
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  Pixel* px = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  testRule();
  testLayout();
  testDraw(px);
  testApp();
  printf("  sizeof(LsShroom) %u, sizeof(LsPatch) %u (computed, not stored)\n", (unsigned)sizeof(LsShroom), (unsigned)sizeof(LsPatch));
  free(px);
  if (g_fail) {
    printf("test_mush_patch: %d FAILED of %ld checks\n", g_fail, g_checks);
    return 1;
  }
  printf("test_mush_patch: ok (%ld checks)\n", g_checks);
  return 0;
}
