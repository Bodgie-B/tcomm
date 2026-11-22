// Tests for core/scene/plink (the plinking module, Mike 2026-11-17), driven by a real hiker rig the way the World
// drives it (hikerTick, plinkBend, plinkStep from his hands): the three strings (A a clean run with the gag can, B the
// can that will not fall, C the bag's line) over a kneeling hiker, both facings.
//
//   [1] every pixel drawn is inside plinkBounds (noise background), the held and the tree layers inside their own
//   [2] plinkDirty covers every changed pixel: an incremental render from the dirty rects equals a full render each
//       tick, and a still tick marks nothing
//   [3] the physics: each hit can leaves the branch and rests at its pair's spot on its ground, the gag can returns
//       and topples, a miss never moves a can off the branch, chips are gone within a second, round counts 6/8/7,
//       the bag event comes once and only in C
//   [4] the pose: at every hold the axis passes within 2 px of the can's centre and the support hand is on it, the
//       kneel grounds him, and it is mirror-correct
//   [5] safety every tick: loaded means the axis points the way he faces and is never more than 0.2 rad above the
//       highest can's line; the low carry has the muzzle forward and down; the string ends in the low carry
//   [6] determinism: two runs are byte-identical, and each fired shot draws exactly two Rng values
//
// Plain executable; returns non-zero on failure.
#include "anim/ease.h"
#include "anim/rng.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/hiker.h"
#include "scene/plink.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace ss;

static int g_fail = 0, g_checks = 0, g_printed = 0;
#define CHECK(cond, ...)                                       \
  do {                                                         \
    ++g_checks;                                                \
    if (!(cond)) {                                             \
      ++g_fail;                                                \
      if (g_printed++ < 30) {                                  \
        printf("  FAIL line %d: %s -- ", __LINE__, #cond);     \
        printf(__VA_ARGS__);                                   \
        printf("\n");                                          \
      }                                                        \
    }                                                          \
  } while (0)

static const int kW = SS_WIDTH, kH = SS_HEIGHT;
static const float kDt = SS_TICK_DT;
static const Rect kFrame{0, 0, kW, kH};

static Pixel g_noise[SS_WIDTH * SS_HEIGHT], g_a[SS_WIDTH * SS_HEIGHT], g_b[SS_WIDTH * SS_HEIGHT], g_ref[SS_WIDTH * SS_HEIGHT];

static Surface surfOf(Pixel* fb) { return Surface{fb, kW, kH, kW, 0}; }
static bool inside(const Rect& o, const Rect& r) { return r.x0 >= o.x0 && r.y0 >= o.y0 && r.x1 <= o.x1 && r.y1 <= o.y1; }

static Palette dayPalette() {
  Palette p;
  WeatherTint t;
  memset(&t, 0, sizeof t);
  computePalette(3.f, t, p);
  return p;
}

// One run's scene: the rig, the module, the clock.
struct Run {
  Hiker h;
  PlinkState s;
  Rng rng;
  int var;
  int dir;
  int tick;
  Run(int variant, int facing) : var(variant), dir(facing), tick(0) {
    static const float px[3] = {520.f, 566.f, 604.f}, py[3] = {351.f, 352.f, 351.f};
    static const float mx[3] = {280.f, 234.f, 196.f};
    if (facing < 0) {   // mirrored: the branch reaches to the right of its root and he kneels at the lower right
      plinkInit(s, Vec2{240.f, 212.f}, Vec2{322.f, 202.f}, facing, mx, py);
      hikerInit(h, Vec2{635.f, 432.f}, facing);
    } else {
      plinkInit(s, Vec2{560.f, 212.f}, Vec2{478.f, 202.f}, facing, px, py);
      hikerInit(h, Vec2{165.f, 432.f}, facing);
    }
    rng.seed(777u + (uint64_t)variant);
    for (int i = 0; i < PLINK_CANS; ++i) plinkSetCan(s, i, CAN_ON);
    plinkRifle(s, true, 1.f);
    plinkLoad(s);
    plinkKneel(s, true);
    hikerSetPose(h, POSE_STAND, 0.1f);
    queue();
    plinkStart(s);
  }
  void queue() {
    plinkQueueClear(s);
    if (var == 1) {
      static const uint8_t k[10] = {SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_MISS_U, SK_MISS_L, SK_CHECK, SK_MISS_R, SK_SHRUG};
      static const uint8_t c[10] = {5, 4, 3, 2, 1, 0, 0, 0, 0, 0};
      for (int i = 0; i < 10; ++i) plinkQueue(s, k[i], c[i]);
    } else if (var == 2) {
      static const uint8_t k[8] = {SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_HIT, SK_BAG, SK_SCRATCH, SK_HIT};
      static const uint8_t c[8] = {5, 4, 3, 2, 1, 0, 0, 0};
      for (int i = 0; i < 8; ++i) plinkQueue(s, k[i], c[i]);
    } else {
      static const uint8_t k[6] = {SK_HIT, SK_HIT, SK_WEAK, SK_HIT, SK_HIT, SK_HIT};
      static const uint8_t c[6] = {5, 4, 3, 2, 1, 0};
      for (int i = 0; i < 6; ++i) plinkQueue(s, k[i], c[i]);
    }
  }
  // One tick in the World's order.
  void step() {
    hikerTick(h, kDt);
    plinkBend(s, h.pose);
    plinkStep(s, hikerHandPos(h, 0), hikerHandPos(h, 1), h.facing, h.height, kDt, rng);
    ++tick;
  }
};

static void drawAll(const PlinkState& s, Pixel* fb, const Rect& clip, const Palette& pal) {
  Surface sf = surfOf(fb);
  plinkTreeDraw(s, sf, clip, pal);
  plinkHeldDraw(s, sf, clip, pal);
}

static void fillNoise(Pixel* fb) {
  uint32_t x = 987654u;
  for (int i = 0; i < kW * kH; ++i) {
    x = x * 1664525u + 1013904223u;
    fb[i] = (Pixel)(x >> 16);
  }
}

static float distToAxis(Vec2 p, Vec2 b, Vec2 d) {
  const float rx = p.x - b.x, ry = p.y - b.y;
  return fabsf(rx * d.y - ry * d.x);
}

// A whole string, checking everything per tick. `max_ticks` guards a hang.
static void playString(int var, int facing, bool full_checks) {
  Run r(var, facing);
  const Palette pal = dayPalette();
  char tag[24];
  snprintf(tag, sizeof tag, "%c%s", 'A' + var, facing < 0 ? " mirrored" : "");
  fillNoise(g_noise);
  for (int i = 0; i < kW * kH; ++i) g_ref[i] = (Pixel)0x4208;   // the flat background
  memcpy(g_a, g_ref, sizeof g_a);   // the incremental frame
  PlinkState prev = r.s;
  if (full_checks) drawAll(r.s, g_a, kFrame, pal);   // (the frame starts with the first state drawn)
  int last_hold_cur = -1;
  int shots = 0, bag_events = 0, last_flash_tick = -999, hold_checks = 0, still_marks = 0, still_ticks = 0;
  bool gag_back = false, chips_seen = false, check_seen = false;
  float worst_axis = 0.f, worst_hand = 0.f, worst_ground = 0.f;
  float max_elev = -9.f;
  int chips_clear_tick = -1, last_miss_tick = -1;
  const float line_top = 202.f;   // (highest can's centre is about 200)
  bool done = false;
  for (int n = 0; n < 4000 && !done; ++n) {
    const PlinkState before = r.s;
    r.step();
    const PlinkState& s = r.s;
    // [3] bookkeeping
    if (s.flash > 0.f && before.flash <= 0.f) {
      ++shots;
      last_flash_tick = r.tick;
    }
    const uint8_t ev = s.events;
    if (ev & PEV_BAG) ++bag_events;
    r.s.events = 0;
    if (s.can[3].mode == CAN_ON && s.can[3].aux == 2) gag_back = true;
    for (const PlinkBit& b : s.bit) chips_seen = chips_seen || b.kind == PB_CHIP;
    if (s.phase == PP_GESTURE && s.gest == SK_CHECK) check_seen = true;
    if (var == 1 && s.bullet_kind >= SK_MISS_U && s.bullet_kind <= SK_MISS_R && before.bullet_t > 0.f && s.bullet_t == 0.f) last_miss_tick = r.tick;
    // [5] safety
    const Vec2 ax = plinkAxis(s);
    if (plinkLoaded(s)) {
      CHECK(ax.x * (float)facing > 0.f, "%s tick %d: loaded and the axis points away from his facing", tag, r.tick);
      CHECK(s.elev < 0.75f, "%s tick %d: elevation %.2f", tag, r.tick, (double)s.elev);
    }
    CHECK(s.rounds <= 8, "%s: rounds %d", tag, (int)s.rounds);
    if (s.up == 0.f && s.rifle && s.join >= 1.f) CHECK(fabsf(s.elev - kPlinkCarryElev) < 1.0e-4f, "%s: low carry elevation %.3f", tag, (double)s.elev);
    if (s.elev > max_elev) max_elev = s.elev;
    // [4] pose at the last tick of every hold
    if (s.phase == PP_HOLD && s.t + kDt >= s.hold_len - 0.001f && last_hold_cur != (int)s.cur) {
      last_hold_cur = (int)s.cur;
      const Vec2 tgt = plinkCanPos(s, s.queue_can[s.cur]);
      const Vec2 b = plinkButt(s);
      const float da = distToAxis(tgt, b, ax);
      const float dh = distToAxis(s.near_hand, b, ax);
      if (da > worst_axis) worst_axis = da;
      if (dh > worst_hand) worst_hand = dh;
      ++hold_checks;
      // facing mirror: the muzzle is on his facing side of the trigger hand
      CHECK((plinkMuzzle(s).x - s.far_hand.x) * (float)facing > 20.f, "%s: muzzle on the wrong side", tag);
      // kneeling grounds him: the rig's bottom is at his feet (a couple of px of boot)
      const Rect hb = hikerBounds(r.h);
      const float over = fabsf((float)hb.y1 - r.h.feet.y);
      if (over > worst_ground) worst_ground = over;
    }
    // [1] bounds with a noise background, every third tick (a full check of every layer)
    if (full_checks && r.tick % 3 == 0) {
      memcpy(g_b, g_noise, sizeof g_b);
      drawAll(s, g_b, kFrame, pal);
      const Rect bb = plinkBounds(s);
      const Rect tb = plinkTreeBounds(s), hb2 = plinkHeldBounds(s);
      CHECK(inside(bb, tb) && inside(bb, hb2), "%s tick %d: layer bounds outside the whole", tag, r.tick);
      int outside = 0;
      for (int y = 0; y < kH && outside < 3; ++y)
        for (int x = 0; x < kW; ++x) {
          if (g_b[y * kW + x] == g_noise[y * kW + x]) continue;
          if (x < bb.x0 || x >= bb.x1 || y < bb.y0 || y >= bb.y1) {
            if (outside++ < 2) printf("  %s tick %d: pixel %d,%d drawn outside bounds %d,%d,%d,%d\n", tag, r.tick, x, y, bb.x0, bb.y0, bb.x1, bb.y1);
          }
        }
      CHECK(outside == 0, "%s tick %d: pixels outside plinkBounds", tag, r.tick);
      // a layer's own bounds
      memcpy(g_b, g_noise, sizeof g_b);
      Surface sf = surfOf(g_b);
      plinkHeldDraw(s, sf, kFrame, pal);
      int bad = 0;
      for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
          if (g_b[y * kW + x] != g_noise[y * kW + x] && (x < hb2.x0 || x >= hb2.x1 || y < hb2.y0 || y >= hb2.y1)) ++bad;
      CHECK(bad == 0, "%s tick %d: held layer outside plinkHeldBounds (%d px)", tag, r.tick, bad);
    }
    // [2] the dirty marks: incremental from last tick's frame equals a full render
    if (full_checks) {
      Rect rs[PLINK_DIRTY_MAX];
      const int nr = plinkDirty(prev, s, rs);
      // The renderer redraws whole strips (full width, SS_STRIP_ROWS tall) under each mark: the background back, then the pieces.
      for (int i = 0; i < nr; ++i) {
        const Rect c = rs[i].clipped(kFrame);
        if (c.empty()) continue;
        const int ya = (c.y0 / SS_STRIP_ROWS) * SS_STRIP_ROWS, yb = ((c.y1 + SS_STRIP_ROWS - 1) / SS_STRIP_ROWS) * SS_STRIP_ROWS;
        for (int y = ya; y < yb && y < kH; ++y)
          for (int x = 0; x < kW; ++x) g_a[y * kW + x] = g_ref[y * kW + x];
        drawAll(s, g_a, Rect{0, ya, kW, yb < kH ? yb : kH}, pal);
      }
      memcpy(g_b, g_ref, sizeof g_b);
      drawAll(s, g_b, kFrame, pal);
      int diff = 0;
      for (int i = 0; i < kW * kH; ++i) diff += g_a[i] != g_b[i];
      CHECK(diff == 0, "%s tick %d: incremental differs from full in %d px", tag, r.tick, diff);
      if (plinkSame(prev, s)) {
        ++still_ticks;
        if (nr != 0) ++still_marks;
      }
    }
    prev = s;
    done = !s.run && s.phase == PP_IDLE && plinkSettled(s);
    (void)before;
    (void)line_top;
  }
  CHECK(done, "%s: the string never ended", tag);
  const PlinkState& s = r.s;
  // [3] the physics
  static const int kRounds[3] = {6, 8, 7};
  CHECK(shots == kRounds[var], "%s: %d shots, wanted %d", tag, shots, kRounds[var]);
  CHECK((int)(8 - s.rounds) == kRounds[var], "%s: %d rounds used", tag, 8 - (int)s.rounds);
  CHECK(bag_events == (var == 2 ? 1 : 0), "%s: %d bag events", tag, bag_events);
  for (int i = 0; i < PLINK_CANS; ++i) {
    const PlinkCan& c = s.can[i];
    const bool stays = var == 1 && i == 0;
    if (stays) {
      CHECK(c.mode == CAN_ON, "%s: the stubborn can left the branch (mode %d)", tag, (int)c.mode);
      CHECK(c.wob == 0.f && fabsf(c.rot) < 1.0e-4f, "%s: the stubborn can still rocks", tag);
      continue;
    }
    CHECK(c.mode == CAN_DOWN, "%s: can %d ended in mode %d", tag, i, (int)c.mode);
    if (c.mode != CAN_DOWN) continue;
    const int p = i / 2;
    const float lx = s.pair_x[p] + ((i & 1) ? 7.f : -7.f);
    CHECK(fabsf(c.x - lx) <= 9.f, "%s: can %d rests at x %.1f, its pair's spot is %.1f", tag, i, (double)c.x, (double)lx);
    const float ly = s.pair_y[p] + ((i & 1) ? 2.f : 0.f);
    const float ext = fabsf(cosT(c.rot / 6.2831853f)) * 0.5f * (float)c.h + fabsf(sinT(c.rot / 6.2831853f)) * 0.5f * (float)c.w;
    CHECK(fabsf(c.y + ext - ly) < 0.6f, "%s: can %d bottom %.1f on ground %.1f", tag, i, (double)(c.y + ext), (double)ly);
    CHECK(c.x > 0.f && c.x < (float)kW && c.y > 0.f && c.y < (float)kH, "%s: can %d out of the frame", tag, i);
    CHECK(!(c.x > 538.f && c.y > 412.f), "%s: can %d in the chip's box", tag, i);
  }
  if (var == 0) CHECK(gag_back, "%s: the gag can never teetered on the branch", tag);
  if (var == 1) {
    CHECK(chips_seen, "%s: no chips", tag);
    CHECK(check_seen, "%s: no sight check", tag);
    CHECK(last_miss_tick > 0, "%s: no miss seen", tag);
    (void)chips_clear_tick;
  }
  for (const PlinkBit& b : s.bit) CHECK(b.kind == PB_NONE || b.kind >= FOOD_LOAF, "%s: a chip or dust is left", tag);
  // [4] aim
  CHECK(hold_checks == kRounds[var], "%s: %d holds checked", tag, hold_checks);
  CHECK(worst_axis <= 2.0f, "%s: the axis misses the can's centre by %.2f px", tag, (double)worst_axis);
  CHECK(worst_hand <= 4.0f, "%s: the support hand is %.2f px off the axis", tag, (double)worst_hand);
  CHECK(worst_ground <= 4.5f, "%s: the kneeling rig is %.2f px off the ground", tag, (double)worst_ground);
  CHECK(still_marks == 0, "%s: %d still ticks marked something", tag, still_marks);
  // [5] the string ends in the low carry
  CHECK(s.up == 0.f && fabsf(s.elev - kPlinkCarryElev) < 1.0e-4f, "%s: ends with up %.2f elev %.2f", tag, (double)s.up, (double)s.elev);
  CHECK(max_elev < 0.75f, "%s: max elevation %.2f", tag, (double)max_elev);
  printf("  %-12s %d shots, %d holds: axis off by at most %.2f px, hand %.2f px, ground %.2f px; %d still ticks marked nothing; %d ticks\n", tag, shots,
         hold_checks, (double)worst_axis, (double)worst_hand, (double)worst_ground, still_ticks, r.tick);
  (void)last_flash_tick;
}

// [3] chips and a miss: a miss never moves a can off the branch, chips are gone within a second.
static void testChips() {
  printf("[3b] chips and dust\n");
  Run r(1, 1);
  int last_chip_tick = -1, last_shot_tick = -1;
  for (int n = 0; n < 4000; ++n) {
    r.step();
    r.s.events = 0;
    for (const PlinkBit& b : r.s.bit)
      if (b.kind == PB_CHIP || b.kind == PB_DUST) {
        last_chip_tick = r.tick;
        CHECK(b.t <= 0.62f, "a chip lives %.2f s", (double)b.t);
      }
    if (r.s.flash > 0.f) last_shot_tick = r.tick;
    if (!r.s.run && plinkSettled(r.s)) break;
  }
  CHECK(last_chip_tick > 0, "no chips");
  CHECK(r.s.can[0].mode == CAN_ON, "the can left the branch");
  // (the last shot of B is a miss: its chips are gone within a second of it)
  CHECK((last_chip_tick - last_shot_tick) * kDt < 1.05f, "chips still flying %.2f s after the last shot", (double)((last_chip_tick - last_shot_tick) * kDt));
}

// [6] two runs, byte for byte; two Rng values a shot.
static void testDeterminism() {
  printf("[6] determinism, and two Rng values a shot\n");
  static const int kDraws[3] = {12, 16, 14};   // (shots with a can to hit or miss, times two)
  for (int var = 0; var < 3; ++var) {
    Run a(var, 1), b(var, 1);
    bool same = true;
    for (int n = 0; n < 4000; ++n) {
      a.step();
      b.step();
      a.s.events = b.s.events = 0;
      if (memcmp(&a.s, &b.s, sizeof a.s) != 0) {
        same = false;
        break;
      }
      if (!a.s.run && plinkSettled(a.s) && a.s.phase == PP_IDLE) break;
    }
    CHECK(same, "variant %c: two runs differ", 'A' + var);
    Rng probe;
    probe.seed(777u + (uint64_t)var);
    for (int i = 0; i < kDraws[var]; ++i) probe.next();
    CHECK(probe.state == a.rng.state, "variant %c: the string drew a different number of values than %d", 'A' + var, kDraws[var]);
  }
}

// A frozen step (dt 0) draws nothing and moves nothing but his hands' follow.
static void testFrozen() {
  printf("[6b] a frozen step changes nothing\n");
  Run r(0, 1);
  for (int n = 0; n < 300; ++n) r.step();
  const PlinkState before = r.s;
  const uint64_t st = r.rng.state;
  for (int i = 0; i < 20; ++i) plinkStep(r.s, r.s.near_hand, r.s.far_hand, 1, 90.f, 0.f, r.rng);
  CHECK(memcmp(&before, &r.s, sizeof before) == 0, "a frozen step changed the state");
  CHECK(st == r.rng.state, "a frozen step drew random values");
  r.s.flash = 0.05f;
  plinkFreeze(r.s);
  CHECK(r.s.flash == 0.f, "plinkFreeze left the flash on");
}

int main(int argc, char**) {
  (void)argc;
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  printf("[1]-[5] the three strings, both facings\n");
  for (int var = 0; var < 3; ++var) playString(var, 1, true);
  for (int var = 0; var < 3; ++var) playString(var, -1, var == 0);
  testChips();
  testDeterminism();
  testFrozen();
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
