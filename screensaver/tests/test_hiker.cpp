// Tests for core/scene/hiker: bounds cover every drawn pixel, mirroring,
// exact pose landing, walk arrival, primitive budget. Plain executable; returns
// non-zero on failure. Writes inspection sheets to the current directory
// (build/ under ctest): hiker_poses_day.ppm, hiker_poses_night.ppm,
// hiker_walk.ppm, hiker_carry.ppm.
#include "scene/hiker.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

using namespace ss;

static int g_fail = 0;
static int g_checks = 0;
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

static const int kBudget = 22;

struct Frame {
  std::vector<Pixel> px;
  Surface s;
  Frame(int w, int h, Pixel fill) : px((size_t)w * h, fill) { s = Surface{px.data(), w, h, w, 0}; }
  Pixel at(int x, int y) const { return s.row(y)[x]; }
  Rect all() const { return Rect{0, 0, s.width, s.height}; }
};

static bool writePPM(const char* path, const Frame& f) {
  FILE* fp = fopen(path, "wb");
  if (!fp) return false;
  fprintf(fp, "P6\n%d %d\n255\n", f.s.width, f.s.height);
  std::vector<unsigned char> row((size_t)f.s.width * 3);
  for (int y = 0; y < f.s.height; ++y) {
    for (int x = 0; x < f.s.width; ++x) {
      Pixel p = f.at(x, y);
      row[(size_t)x * 3 + 0] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), fp);
  }
  return fclose(fp) == 0;
}

static void posed(Hiker& h, PoseId p) {
  hikerSetPose(h, p, 0.f);
  hikerTick(h, 0.f);
}

// Put the hiker mid-walk at a given phase, full walk weight, without moving.
static void walking(Hiker& h, float phase) {
  hikerWalkTo(h, h.feet.x + 10000.f * (float)h.facing, 36.f);
  h.blending = false;
  h.walk_w = 1.f;
  h.walk_phase = phase;
  hikerTick(h, 0.f);
}

static bool samePose(const HikerPose& a, const HikerPose& b) { return memcmp(&a, &b, sizeof a) == 0; }

// The rod's line out to a float on the water ahead and below the hands.
static void rodOut(Hiker& h, float bend, bool stick) {
  hikerCarry(h, CARRY_ROD);
  h.rod_line = true;
  h.rod_float = true;
  h.rod_bend = bend;
  h.rod_stick = stick;
  h.rod_end = Vec2{h.feet.x + (float)h.facing * 70.f, h.feet.y + 12.f};
}

// ---------------------------------------------------------------------------

struct Coverage {
  int bad = 0;          // drawn pixels outside the bounds
  int drawn = 0;
  int max_prims = 0;
  int worst_prims_pose = -1, worst_prims_carry = -1;
};

// Render one state into a clean frame; check pixels against hikerBounds.
static void coverOne(Coverage& cv, const Hiker& h, const Palette& pal, const char* what) {
  const Pixel bg = pal[GROUND];
  Frame f(400, 300, bg);
  hikerDraw(h, f.s, pal, f.all());
  const int prims = hikerLastDrawPrimitives();
  if (prims > cv.max_prims) {
    cv.max_prims = prims;
    cv.worst_prims_pose = h.target;
    cv.worst_prims_carry = h.carry;
  }
  const Rect b = hikerBounds(h);
  int bad = 0;
  for (int y = 0; y < 300; ++y)
    for (int x = 0; x < 400; ++x) {
      if (f.at(x, y) == bg) continue;
      ++cv.drawn;
      if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) ++bad;
    }
  if (bad && cv.bad == 0) printf("  first escape: %s (%d px outside)\n", what, bad);
  cv.bad += bad;
}

static void testBoundsAndBudget() {
  printf("[1] bounds cover every drawn pixel; primitive budget\n");
  const Palette& pal = moodKey(MOOD_DAY);
  Coverage cv;
  char what[96];
  for (int facing = -1; facing <= 1; facing += 2)
    for (int lit = -1; lit <= 1; ++lit)
      for (int c = 0; c < CARRY_COUNT; ++c) {
        for (int p = 0; p < POSE_COUNT; ++p) {
          for (int pack = 0; pack < 2; ++pack) {
            Hiker h;
            hikerInit(h, Vec2{200.f, 220.f}, facing);
            h.lit_from = lit;
            h.pack_on = pack == 0;
            hikerCarry(h, (CarryId)c);
            posed(h, (PoseId)p);
            snprintf(what, sizeof what, "pose %s carry %d facing %d lit %d pack %d", hikerPoseName((PoseId)p), c,
                     facing, lit, h.pack_on);
            coverOne(cv, h, pal, what);
          }
        }
        for (int k = 0; k < 12; ++k) {
          Hiker h;
          hikerInit(h, Vec2{200.f, 220.f}, facing);
          h.lit_from = lit;
          hikerCarry(h, (CarryId)c);
          if (c != CARRY_NONE) posed(h, POSE_CARRY);
          walking(h, (float)k / 12.f);
          snprintf(what, sizeof what, "walk phase %d/12 carry %d facing %d lit %d", k, c, facing, lit);
          coverOne(cv, h, pal, what);
        }
      }
  // The rod: line and float out, bowed, a stick in its place; the hat off.
  for (int facing = -1; facing <= 1; facing += 2)
    for (int p = 0; p < POSE_COUNT; ++p)
      for (int v = 0; v < 4; ++v) {
        Hiker h;
        hikerInit(h, Vec2{200.f, 220.f}, facing);
        h.lit_from = facing;
        rodOut(h, v == 1 ? 1.f : (v == 3 ? 0.4f : 0.f), v == 2);
        h.hat_on = v != 3;
        h.rod_tilt = v == 3 ? 0.4f : 0.f;
        posed(h, (PoseId)p);
        snprintf(what, sizeof what, "pose %s rod variant %d facing %d", hikerPoseName((PoseId)p), v, facing);
        coverOne(cv, h, pal, what);
        const Vec2 tip = hikerRodTip(h);
        CHECK(tip.x == tip.x && fabsf(tip.x - h.feet.x) < 120.f, "rod tip at %.1f for feet %.1f", (double)tip.x,
              (double)h.feet.x);
      }
  // Mid-blend frames (anticipation and overshoot reach past both ends) and idle motion.
  for (int p = 0; p < POSE_COUNT; ++p) {
    Hiker h;
    hikerInit(h, Vec2{200.f, 220.f}, 1);
    hikerCarry(h, CARRY_WOOD);
    hikerSetPose(h, (PoseId)p, 1.f);
    for (int k = 0; k < 25 + 50; ++k) {
      hikerTick(h, SS_TICK_DT);
      snprintf(what, sizeof what, "blend to %s tick %d", hikerPoseName((PoseId)p), k);
      coverOne(cv, h, pal, what);
    }
  }
  printf("  %d drawn pixels checked, %d outside bounds; max primitives per draw %d (pose %s, carry %d)\n", cv.drawn,
         cv.bad, cv.max_prims, hikerPoseName((PoseId)cv.worst_prims_pose), cv.worst_prims_carry);
  CHECK(cv.bad == 0, "%d drawn pixels fell outside hikerBounds", cv.bad);
  CHECK(cv.max_prims <= kBudget, "%d primitives in one draw (budget %d)", cv.max_prims, kBudget);
  CHECK(cv.drawn > 0, "nothing drawn");

  // Height: standing at 90 px spans feet to hat top in about 90 px.
  Hiker h;
  hikerInit(h, Vec2{200.f, 220.f}, 1);
  Frame f(400, 300, pal[GROUND]);
  hikerDraw(h, f.s, pal, f.all());
  int top = 300, bottom = -1;
  for (int y = 0; y < 300; ++y)
    for (int x = 0; x < 400; ++x)
      if (f.at(x, y) != pal[GROUND]) {
        if (y < top) top = y;
        bottom = y;
      }
  printf("  standing: hat top at feet-%d, lowest pixel row feet%+d\n", 220 - top, bottom - 220);
  CHECK(220 - top >= 85 && 220 - top <= 95, "standing height %d px, want about 90", 220 - top);
  CHECK(bottom <= 220 && bottom >= 218, "feet not on the ground line: lowest row %d", bottom);

  // Strip drawing matches a full-frame draw (the renderer draws per strip).
  Frame whole(400, 300, pal[GROUND]), strips(400, 300, pal[GROUND]);
  hikerCarry(h, CARRY_BAG);
  h.lit_from = 1;
  posed(h, POSE_WAVE);
  hikerDraw(h, whole.s, pal, whole.all());
  for (int y0 = 0; y0 < 300; y0 += 32) hikerDraw(h, strips.s, pal, Rect{0, y0, 400, y0 + 32 < 300 ? y0 + 32 : 300});
  CHECK(whole.px == strips.px, "strip-by-strip draw differs from a full-frame draw");
}

static void testMirror() {
  printf("[2] facing -1 is the mirror of facing +1\n");
  const Palette& pal = moodKey(MOOD_DUSK);
  int worst = 0, diffs = 0, inked = 0;
  for (int p = 0; p < POSE_COUNT; ++p)
    for (int c = 0; c < CARRY_COUNT; c += 3)
      for (int lit = -1; lit <= 1; ++lit) {
        Frame a(400, 300, pal[GROUND]), b(400, 300, pal[GROUND]);
        Hiker h;
        hikerInit(h, Vec2{200.f, 220.f}, 1);
        hikerCarry(h, (CarryId)c);
        h.lit_from = lit;
        posed(h, (PoseId)p);
        hikerDraw(h, a.s, pal, a.all());
        hikerInit(h, Vec2{200.f, 220.f}, -1);
        hikerCarry(h, (CarryId)c);
        h.lit_from = -lit;   // the light flips with the picture
        posed(h, (PoseId)p);
        hikerDraw(h, b.s, pal, b.all());
        for (int y = 0; y < 300; ++y)
          for (int x = 0; x < 400; ++x) {
            const Pixel pa = a.at(x, y), pb = b.at(399 - x, y);
            if (pa != pal[GROUND]) ++inked;
            if (pa == pb) continue;
            ++diffs;
            int d = abs((pa >> 11) - (pb >> 11));
            int dg = abs(((pa >> 5) & 63) - ((pb >> 5) & 63));
            int db = abs((pa & 31) - (pb & 31));
            if (dg > d) d = dg;
            if (db > d) d = db;
            if (d > worst) worst = d;
          }
      }
  printf("  %d of %d inked pixels differ from the flip, worst channel step %d\n", diffs, inked, worst);
  // The geometry is mirrored exactly; the rasterizer accumulates coverage left
  // to right in float, so a mirrored edge may round one coverage step apart.
  CHECK(worst <= 1, "mirror differs by %d channel steps", worst);
  CHECK(diffs * 200 <= inked, "too many mirror differences: %d of %d", diffs, inked);
}

static void testBlend() {
  printf("[3] pose blends land exactly at the end\n");
  const float dur = 0.8f;
  const int want = (int)(dur / SS_TICK_DT + 0.5f);
  for (int p = 0; p < POSE_COUNT; ++p) {
    Hiker h;
    hikerInit(h, Vec2{200.f, 220.f}, 1);
    posed(h, p == POSE_SIT ? POSE_STAND : POSE_SIT);
    hikerSetPose(h, (PoseId)p, dur);
    int ticks = 0;
    while (h.blending && ticks < 1000) {
      hikerTick(h, SS_TICK_DT);
      ++ticks;
    }
    CHECK(ticks == want, "%s: blend took %d ticks, want %d", hikerPoseName((PoseId)p), ticks, want);
    CHECK(samePose(h.pose, hikerPoseDef((PoseId)p)), "%s: not exactly on the pose at the end",
          hikerPoseName((PoseId)p));
  }

  // Anticipation: reaching up first dips the arm; landing overshoots the seat.
  {
    Hiker h;
    hikerInit(h, Vec2{200.f, 220.f}, 1);
    const float start = h.pose.shoulder[0];
    hikerSetPose(h, POSE_REACH_UP, 1.f);
    float lowest = start;
    for (int k = 0; k < 25; ++k) {
      hikerTick(h, SS_TICK_DT);
      if (h.pose.shoulder[0] < lowest) lowest = h.pose.shoulder[0];
    }
    printf("  reach_up wind-up: shoulder dips %.3f rad before rising\n", (double)(start - lowest));
    CHECK(lowest < start - 0.05f, "no anticipation before reach_up");
  }
  {
    Hiker h;
    hikerInit(h, Vec2{200.f, 220.f}, 1);
    hikerSetPose(h, POSE_SIT, 1.f);
    const float goal = hikerPoseDef(POSE_SIT).thigh[0];
    float most = h.pose.thigh[0];
    for (int k = 0; k < 25; ++k) {
      hikerTick(h, SS_TICK_DT);
      if (h.pose.thigh[0] > most) most = h.pose.thigh[0];
    }
    printf("  sit overshoot: thigh passes the pose by %.3f rad and settles\n", (double)(most - goal));
    CHECK(most > goal + 0.02f, "no overshoot on landing in sit");
  }

  // Retargeting mid-blend starts from where the figure is, with no jump.
  {
    Hiker h;
    hikerInit(h, Vec2{200.f, 220.f}, 1);
    hikerSetPose(h, POSE_CROUCH, 1.f);
    for (int k = 0; k < 11; ++k) hikerTick(h, SS_TICK_DT);
    const HikerPose before = h.pose;
    hikerSetPose(h, POSE_WAVE, 1.f);
    hikerTick(h, 0.f);
    CHECK(samePose(before, h.pose), "retarget mid-blend jumped");
  }

  // Idle motion (the wave) only starts once the pose has landed.
  {
    Hiker h;
    hikerInit(h, Vec2{200.f, 220.f}, 1);
    hikerSetPose(h, POSE_WAVE, 0.4f);
    for (int k = 0; k < 10; ++k) hikerTick(h, SS_TICK_DT);
    CHECK(!h.blending && samePose(h.pose, hikerPoseDef(POSE_WAVE)), "wave not exact on landing");
    for (int k = 0; k < 20; ++k) hikerTick(h, SS_TICK_DT);
    CHECK(!samePose(h.pose, hikerPoseDef(POSE_WAVE)), "wave hand never moves after landing");
  }
}

static void testWalk() {
  printf("[4] walk cycle and arrival\n");
  Hiker h;
  hikerInit(h, Vec2{100.f, 220.f}, -1);
  posed(h, POSE_SIT);
  hikerWalkTo(h, 300.f, 40.f);
  CHECK(h.facing == 1, "did not turn toward the target");
  CHECK(!hikerArrived(h), "arrived before walking");
  int ticks = 0;
  int top_min = 1000, top_max = -1000;
  while (!hikerArrived(h) && ticks < 2000) {
    hikerTick(h, SS_TICK_DT);
    ++ticks;
    if (ticks > 25 && h.walking) {   // after the ramp, measure the hat bob
      const int t = hikerBounds(h).y0;
      if (t < top_min) top_min = t;
      if (t > top_max) top_max = t;
    }
  }
  printf("  arrived after %d ticks (%.2f s for 200 px at 40 px/s); hat bob %d px\n", ticks,
         (double)ticks * (double)SS_TICK_DT, top_max - top_min);
  CHECK(hikerArrived(h), "never arrived");
  CHECK(h.feet.x == 300.f, "stopped at %.3f, not on the target", (double)h.feet.x);
  CHECK(h.target == POSE_STAND, "arrival target is %s, not stand", hikerPoseName(h.target));
  CHECK(top_max - top_min >= 1 && top_max - top_min <= 4, "walk bob %d px, want about 2", top_max - top_min);
  for (int k = 0; k < 13; ++k) hikerTick(h, SS_TICK_DT);
  CHECK(!h.blending && samePose(h.pose, hikerPoseDef(POSE_STAND)), "not exactly standing after arrival");

  hikerWalkTo(h, 150.f, 30.f);
  CHECK(h.facing == -1, "did not turn back");
  for (int k = 0; k < 2000 && !hikerArrived(h); ++k) hikerTick(h, SS_TICK_DT);
  CHECK(hikerArrived(h) && h.feet.x == 150.f, "second walk did not arrive");

  // Carrying: arms keep the carry pose, and arrival keeps it too.
  hikerCarry(h, CARRY_WOOD);
  posed(h, POSE_CARRY);
  hikerWalkTo(h, 250.f, 40.f);
  for (int k = 0; k < 20; ++k) hikerTick(h, SS_TICK_DT);
  CHECK(h.pose.elbow[0] == hikerPoseDef(POSE_CARRY).elbow[0], "arms swing while carrying");
  for (int k = 0; k < 2000 && !hikerArrived(h); ++k) hikerTick(h, SS_TICK_DT);
  CHECK(h.target == POSE_CARRY, "arrival with wood dropped the carry pose (%s)", hikerPoseName(h.target));
}

// ---------------------------------------------------------------------------
// Inspection sheets.

static void sheetPoses(const char* path, Mood mood, int lit) {
  const Palette& pal = moodKey(mood);
  const int cw = 150, ch = 135, cols = 5, rows = 4;
  Frame f(cw * cols, ch * rows * 2, pal[GROUND]);
  for (int side = 0; side < 2; ++side)
    for (int p = 0; p < POSE_COUNT; ++p) {
      const int cx = (p % cols) * cw, cy = (side * rows + p / cols) * ch;
      Hiker h;
      hikerInit(h, Vec2{(float)(cx + cw / 2), (float)(cy + ch - 18)}, side == 0 ? 1 : -1);
      h.lit_from = lit;
      if (p == POSE_CARRY) hikerCarry(h, CARRY_WOOD);
      if (p == POSE_HOLD_MUG) hikerCarry(h, CARRY_MUG);
      if (p == POSE_READ) hikerCarry(h, CARRY_BOOK);
      if (p == POSE_FISH_STAND || p == POSE_FISH_SIT || p == POSE_CAST) {
        rodOut(h, 0.f, false);
        h.rod_end = Vec2{h.feet.x + (float)h.facing * 66.f, h.feet.y - 4.f};
        h.rod_line = p != POSE_CAST;
        h.hat_on = p != POSE_FISH_SIT;   // bareheaded, once
      }
      posed(h, (PoseId)p);
      hikerDraw(h, f.s, pal, Rect{cx, cy, cx + cw, cy + ch});
    }
  CHECK(writePPM(path, f), "could not write %s", path);
}

static void sheetWalk(const char* path) {
  const Palette& pal = moodKey(MOOD_DAY);
  const int cw = 72;
  Frame f(cw * 12, 120 + 85, pal[GROUND]);
  for (int row = 0; row < 2; ++row)
    for (int k = 0; k < 12; ++k) {
      const int cx = k * cw, cy = row == 0 ? 0 : 120, chh = row == 0 ? 120 : 85;
      Hiker h;
      hikerInit(h, Vec2{(float)(cx + cw / 2), (float)(cy + chh - 12)}, 1);
      h.height = row == 0 ? 90.f : 60.f;
      walking(h, (float)k / 12.f);
      hikerDraw(h, f.s, pal, Rect{cx, cy, cx + cw, cy + chh});
    }
  CHECK(writePPM(path, f), "could not write %s", path);
}

static void sheetCarry(const char* path) {
  // Row 1: POSE_CARRY with each item. Row 2: the item in the pose it is for.
  // Row 3: the same as row 1 at 60 px.
  static const PoseId kUse[CARRY_COUNT] = {POSE_STAND, POSE_CARRY, POSE_HOLD_MUG, POSE_READ,
                                           POSE_STAND, POSE_CARRY, POSE_CROUCH, POSE_HOLD_MUG, POSE_FISH_STAND};
  const Palette& pal = moodKey(MOOD_GOLDEN);
  const int cw = 100, ch = 130;
  Frame f(cw * CARRY_COUNT, ch * 2 + 95, pal[GROUND]);
  for (int row = 0; row < 3; ++row)
    for (int c = 0; c < CARRY_COUNT; ++c) {
      const int cx = c * cw, cy = row * ch, chh = row == 2 ? 95 : ch;
      Hiker h;
      hikerInit(h, Vec2{(float)(cx + cw / 2 - 5), (float)(cy + chh - 15)}, 1);
      if (row == 2) h.height = 60.f;
      hikerCarry(h, (CarryId)c);
      posed(h, row == 1 ? kUse[c] : POSE_CARRY);
      hikerDraw(h, f.s, pal, Rect{cx, cy, cx + cw, cy + chh});
    }
  CHECK(writePPM(path, f), "could not write %s", path);
}

int main() {
  testBoundsAndBudget();
  testMirror();
  testBlend();
  testWalk();
  sheetPoses("hiker_poses_day.ppm", MOOD_DAY, 0);
  sheetPoses("hiker_poses_night.ppm", MOOD_NIGHT, 1);
  sheetWalk("hiker_walk.ppm");
  sheetCarry("hiker_carry.ppm");
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
