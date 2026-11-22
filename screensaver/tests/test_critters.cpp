// Tests for core/scene/critters: bounds cover every drawn pixel, mirroring,
// exact blend landing, move and climb arrival, unsupported actions, primitive
// budget, sizes. Plain executable; returns non-zero on failure. Writes
// inspection sheets to the current directory (build/ under ctest):
// critters_day.ppm, critters_night.ppm, critters_gaits.ppm.
//
// The herd's deer (stage 22): testDeerHerd, section [6].
//
// Sheet layout (day and night): one row per kind in enum order (chipmunk,
// squirrel, frog, deer, raccoon, owl, bird, bear, mouse, dog), one column per
// action in enum order (idle, move, run, sit, look, sniff, reach, climb,
// shake, hop, blink, headturn, startle, eat, carry, rear, lie, bow); unsupported cells
// stay empty. The same rows follow at scale 0.7.
#include "gfx/raster.h"
#include "scene/critters.h"

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

static const char* kn(int k) { return critterName((CritterKind)k); }
static const char* an(int a) { return critterActionName((CritterAction)a); }
static bool supports(int k, int a) { return critterSupports((CritterKind)k, (CritterAction)a); }

// A typical item for an action, so carrying states get covered and drawn.
static uint8_t itemFor(int k, int a) {
  if (a != CA_CARRY && a != CA_EAT && a != CA_SIT) return CC_NONE;
  switch (k) {
    case CR_CHIPMUNK: case CR_SQUIRREL: return CC_NUT;
    case CR_RACCOON: return a == CA_CARRY ? CC_SHOE : CC_BAR;
    case CR_BIRD: case CR_MOUSE: return CC_CRUMB;
    case CR_DOG: return a == CA_CARRY ? CC_STICK : (a == CA_SIT ? CC_BRANCH : CC_NONE);   // the stick; the dragged branch
    default: return CC_NONE;
  }
}

// Hold an action in place at a loop/gait phase, full loop weight, no blend.
static void held(Critter& c, int a, float phase) {
  critterSetAction(c, (CritterAction)a, 0.f);
  c.loop_w = 1.f;
  c.phase = phase;
  critterTick(c, 0.f);
}

// A one-shot or blend part way: start the action over `dur`, tick `ticks`.
static void partway(Critter& c, int a, float dur, int ticks) {
  critterSetAction(c, (CritterAction)a, dur);
  for (int i = 0; i < ticks; ++i) critterTick(c, SS_TICK_DT);
}

// The moment each action is shown on the sheet.
static void sheetState(Critter& c, int a) {
  switch (a) {
    case CA_HOP: partway(c, a, 1.f, 11); break;              // near the top of the arc
    case CA_HEADTURN:
      if (c.kind == CR_OWL) partway(c, a, 3.f, 32);         // second snap, face turned away
      else partway(c, a, 0.5f, 13);
      break;
    case CA_BLINK: held(c, a, c.kind == CR_OWL ? 0.17f : 0.045f); break;
    case CA_STARTLE: partway(c, a, 0.6f, 15); break;
    default:
      if (critterActionLoops((CritterAction)a)) held(c, a, a == CA_IDLE ? 0.f : 0.3f);
      else held(c, a, 0.f);
      break;
  }
}

// ---------------------------------------------------------------------------

struct Coverage {
  int bad = 0, drawn = 0, states = 0;
  int max_prims[CR_COUNT] = {};
  int worst_action[CR_COUNT] = {};
};

static const int kW = 280, kH = 220;
static const float kFeetX = 140.f, kFeetY = 180.f;

static void coverOne(Coverage& cv, const Critter& c, const Palette& pal, const char* what) {
  const Pixel bg = pal[GROUND];
  Frame f(kW, kH, bg);
  critterDraw(c, f.s, pal, f.all());
  const int prims = critterLastDrawPrimitives();
  if (prims > cv.max_prims[c.kind]) {
    cv.max_prims[c.kind] = prims;
    cv.worst_action[c.kind] = c.action;
  }
  const Rect b = critterBounds(c);
  int bad = 0;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      if (f.at(x, y) == bg) continue;
      ++cv.drawn;
      if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) ++bad;
    }
  if (bad && cv.bad == 0) printf("  first escape: %s (%d px outside)\n", what, bad);
  cv.bad += bad;
  ++cv.states;
}

static void testBoundsAndBudget() {
  printf("[1] bounds cover every drawn pixel; primitive budget\n");
  const Palette& pal = moodKey(MOOD_DAY);
  Coverage cv;
  char what[128];
  static const float kScales[2] = {1.f, 0.7f};
  for (int k = 0; k < CR_COUNT; ++k)
    for (int a = 0; a < CA_COUNT; ++a) {
      if (!supports(k, a)) continue;
      for (int facing = -1; facing <= 1; facing += 2)
        for (int si = 0; si < 2; ++si) {
          // Three phases of the held action (loop or gait motion at full weight).
          for (int ph = 0; ph < 3; ++ph) {
            Critter c;
            critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, facing);
            c.scale = kScales[si];
            c.carry = itemFor(k, a);
            held(c, a, (float)ph / 3.f + 0.1f);
            snprintf(what, sizeof what, "%s %s phase %d facing %d scale %.1f", kn(k), an(a), ph, facing,
                     (double)c.scale);
            coverOne(cv, c, pal, what);
          }
          // The whole blend in (anticipation, overshoot, one-shot timelines),
          // then the loop fading in, from the sitting or idle pose.
          Critter c;
          critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, facing);
          c.scale = kScales[si];
          c.carry = itemFor(k, a);
          c.jitter_dial = 1.f;
          if (supports(k, CA_SIT) && a != CA_SIT) held(c, CA_SIT, 0.f);
          critterSetAction(c, (CritterAction)a, 1.f);
          for (int t = 0; t < 25 + 30; ++t) {
            critterTick(c, SS_TICK_DT);
            snprintf(what, sizeof what, "%s blend to %s tick %d facing %d", kn(k), an(a), t, facing);
            coverOne(cv, c, pal, what);
          }
        }
    }
  // Moves (with the turn-around), runs, flights and climbs, every tick.
  for (int k = 0; k < CR_COUNT; ++k)
    for (int g = 0; g < 3; ++g) {
      const int gait = g == 0 ? -1 : (g == 1 ? CA_RUN : CA_MOVE);
      if (gait >= 0 && !supports(k, gait)) continue;
      Critter c;
      critterInit(c, (CritterKind)k, Vec2{kFeetX + 40.f, kFeetY}, 1);
      if (gait >= 0) critterSetAction(c, (CritterAction)gait, 0.f);
      critterMoveTo(c, kFeetX - 40.f, 40.f);
      for (int t = 0; t < 200 && !critterArrived(c); ++t) {
        critterTick(c, SS_TICK_DT);
        snprintf(what, sizeof what, "%s move gait %d tick %d", kn(k), gait, t);
        coverOne(cv, c, pal, what);
      }
    }
  for (int k = 0; k < CR_COUNT; ++k) {
    if (!supports(k, CA_CLIMB)) continue;
    for (int facing = -1; facing <= 1; facing += 2) {
      Critter c;
      critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, facing);
      critterClimbTo(c, kFeetY - 30.f, 30.f);
      for (int t = 0; t < 120 && !critterArrived(c); ++t) {
        critterTick(c, SS_TICK_DT);
        snprintf(what, sizeof what, "%s climb tick %d", kn(k), t);
        coverOne(cv, c, pal, what);
      }
    }
  }
  printf("  %d states, %d drawn pixels checked, %d outside bounds\n", cv.states, cv.drawn, cv.bad);
  for (int k = 0; k < CR_COUNT; ++k) {
    printf("  %-9s max primitives %d (budget %d, worst in %s)\n", kn(k), cv.max_prims[k],
           critterBudget((CritterKind)k), an(cv.worst_action[k]));
    CHECK(cv.max_prims[k] <= critterBudget((CritterKind)k), "%s draws %d primitives", kn(k), cv.max_prims[k]);
    CHECK(cv.max_prims[k] > 0, "%s drew nothing", kn(k));
  }
  CHECK(cv.bad == 0, "%d drawn pixels fell outside critterBounds", cv.bad);

  // Strip drawing matches a full-frame draw (the renderer draws per strip).
  for (int k = 0; k < CR_COUNT; ++k) {
    Critter c;
    critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, -1);
    c.carry = itemFor(k, CA_CARRY);
    held(c, supports(k, CA_CARRY) ? CA_CARRY : CA_IDLE, 0.2f);
    Frame whole(kW, kH, pal[GROUND]), strips(kW, kH, pal[GROUND]);
    critterDraw(c, whole.s, pal, whole.all());
    for (int y0 = 0; y0 < kH; y0 += 32) critterDraw(c, strips.s, pal, Rect{0, y0, kW, y0 + 32 < kH ? y0 + 32 : kH});
    CHECK(whole.px == strips.px, "%s: strip-by-strip draw differs from a full-frame draw", kn(k));
  }
}

static void testMirror() {
  printf("[2] facing -1 is the mirror of facing +1\n");
  const Palette& pal = moodKey(MOOD_DUSK);
  int worst = 0, diffs = 0, inked = 0;
  for (int k = 0; k < CR_COUNT; ++k)
    for (int a = 0; a < CA_COUNT; ++a) {
      if (!supports(k, a)) continue;
      for (int ph = 0; ph < 3; ++ph) {
        Frame fa(kW, kH, pal[GROUND]), fb(kW, kH, pal[GROUND]);
        for (int side = 0; side < 2; ++side) {
          Critter c;
          critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, side == 0 ? 1 : -1);
          c.carry = itemFor(k, a);
          if (ph == 2) partway(c, a, 1.f, 9);   // mid-blend / mid one-shot
          else held(c, a, 0.2f + 0.4f * (float)ph);
          Frame& f = side == 0 ? fa : fb;
          critterDraw(c, f.s, pal, f.all());
        }
        for (int y = 0; y < kH; ++y)
          for (int x = 0; x < kW; ++x) {
            const Pixel pa = fa.at(x, y), pb = fb.at(kW - 1 - x, y);
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
    }
  printf("  %d of %d inked pixels differ from the flip, worst channel step %d\n", diffs, inked, worst);
  CHECK(worst <= 1, "mirror differs by %d channel steps", worst);
  CHECK(diffs * 200 <= inked, "too many mirror differences: %d of %d", diffs, inked);
}

static bool samePose(const CritterPose& a, const CritterPose& b) { return memcmp(&a, &b, sizeof a) == 0; }

static bool sameDraw(const Critter& a, const Critter& b) {
  const Palette& pal = moodKey(MOOD_DAY);
  Frame fa(kW, kH, pal[GROUND]), fb(kW, kH, pal[GROUND]);
  critterDraw(a, fa.s, pal, fa.all());
  critterDraw(b, fb.s, pal, fb.all());
  return fa.px == fb.px;
}

static void testBlend() {
  printf("[3] blends land exactly; anticipation; unsupported actions fall back\n");
  const float dur = 0.8f;
  const int want = (int)(dur / SS_TICK_DT + 0.5f);
  int landed = 0, fallbacks = 0;
  for (int k = 0; k < CR_COUNT; ++k)
    for (int a = 0; a < CA_COUNT; ++a) {
      Critter c;
      critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, 1);
      if (!supports(k, a)) {
        critterSetAction(c, (CritterAction)a, 0.5f);
        CHECK(c.action == CA_IDLE, "%s: unsupported %s became %s, not idle", kn(k), an(a), an(c.action));
        ++fallbacks;
        continue;
      }
      // Start from a different pose so the blend has work to do.
      held(c, a == CA_IDLE ? (supports(k, CA_LOOK) ? CA_LOOK : CA_STARTLE) : CA_IDLE, 0.4f);
      critterSetAction(c, (CritterAction)a, dur);
      int ticks = 0;
      while (c.blending && ticks < 1000) {
        critterTick(c, SS_TICK_DT);
        ++ticks;
      }
      CHECK(ticks == want, "%s %s: blend took %d ticks, want %d", kn(k), an(a), ticks, want);
      CHECK(samePose(c.pose, critterPoseDef((CritterKind)k, (CritterAction)a)), "%s %s: not exactly on the pose",
            kn(k), an(a));
      // Landing frame shows exactly the pose (loop motion has not started).
      Critter ref;
      critterInit(ref, (CritterKind)k, Vec2{kFeetX, kFeetY}, 1);
      critterSetAction(ref, (CritterAction)a, 0.f);
      ref.facing = c.facing;   // the bear's startle turns it round
      ref.phase = c.phase;
      critterTick(ref, 0.f);
      if (a != CA_HOP && a != CA_HEADTURN) CHECK(sameDraw(c, ref), "%s %s: landing frame differs from the pose", kn(k), an(a));
      ++landed;
      // Loops start moving after landing; others hold still.
      Critter before = c;
      bool moved = false;
      for (int t = 0; t < 40; ++t) {
        critterTick(c, SS_TICK_DT);
        if (!moved && !sameDraw(before, c)) moved = true;
      }
      if (critterActionLoops((CritterAction)a))
        CHECK(moved, "%s %s: loop never moves after landing", kn(k), an(a));
      else
        CHECK(!moved, "%s %s: one-shot moves after it has landed", kn(k), an(a));
    }
  printf("  %d supported actions land on their pose in %d ticks; %d unsupported fall back to idle\n", landed, want,
         fallbacks);

  // Anticipation: rearing first dips, a startle flinches, a hop crouches.
  {
    Critter c;
    critterInit(c, CR_BEAR, Vec2{kFeetX, kFeetY}, 1);
    critterSetAction(c, CA_REAR, 1.f);
    float lowest = 0.f;
    for (int t = 0; t < 25; ++t) {
      critterTick(c, SS_TICK_DT);
      if (c.pose.rear < lowest) lowest = c.pose.rear;
    }
    printf("  bear rear wind-up: rear dips to %.3f before rising\n", (double)lowest);
    CHECK(lowest < -0.05f, "no anticipation before the bear rears");
  }
  {
    Critter c;
    critterInit(c, CR_DEER, Vec2{kFeetX, kFeetY}, 1);
    critterSetAction(c, CA_STARTLE, 0.5f);
    float lowest = 0.f;
    for (int t = 0; t < 13; ++t) {
      critterTick(c, SS_TICK_DT);
      if (c.pose.head < lowest) lowest = c.pose.head;
    }
    CHECK(lowest < -0.03f, "deer startle has no flinch before the head comes up");
  }
  for (int k = 0; k < CR_COUNT; ++k) {
    if (!supports(k, CA_HOP)) continue;
    Critter c;
    critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, 1);
    critterSetAction(c, CA_HOP, 1.f);
    const int top0 = critterBounds(c).y0;
    int lowest_top = top0, highest = top0;
    int ticks = 0;
    for (; ticks < 25; ++ticks) {
      critterTick(c, SS_TICK_DT);
      const int t = critterBounds(c).y0;
      if (ticks < 5 && t > lowest_top) lowest_top = t;
      if (t < highest) highest = t;
    }
    printf("  %s hop: crouches %d px, rises %d px\n", kn(k), lowest_top - top0, top0 - highest);
    CHECK(lowest_top > top0, "%s hop has no crouch before take-off", kn(k));
    CHECK(top0 - highest >= 2, "%s hop barely leaves the ground", kn(k));
  }

  // Retargeting mid-blend starts from the figure on screen.
  for (int k = 0; k < CR_COUNT; ++k) {
    Critter c;
    critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, 1);
    const int a1 = supports(k, CA_SIT) ? CA_SIT : CA_STARTLE, a2 = supports(k, CA_LOOK) ? CA_LOOK : CA_IDLE;
    critterSetAction(c, (CritterAction)a1, 1.f);
    for (int t = 0; t < 11; ++t) critterTick(c, SS_TICK_DT);
    Critter before = c;
    critterSetAction(c, (CritterAction)a2, 1.f);
    critterTick(c, 0.f);
    CHECK(sameDraw(before, c), "%s: retarget mid-blend jumped", kn(k));
  }
}

static void testMoves() {
  printf("[4] moves, turns, climbs\n");
  for (int k = 0; k < CR_COUNT; ++k) {
    Critter c;
    critterInit(c, (CritterKind)k, Vec2{100.f, kFeetY}, -1);
    critterMoveTo(c, 180.f, 30.f);
    if (k == CR_OWL) {
      CHECK(critterArrived(c) && c.feet.x == 100.f, "the owl moved");
      continue;
    }
    CHECK(!critterArrived(c), "%s arrived before moving", kn(k));
    int ticks = 0, top_min = 1000, top_max = -1000;
    bool turned_mid = false;
    while (!critterArrived(c) && ticks < 2000) {
      critterTick(c, SS_TICK_DT);
      ++ticks;
      if (c.facing == 1 && ticks <= 4) turned_mid = true;
      const int t = critterBounds(c).y0;
      if (t < top_min) top_min = t;
      if (t > top_max) top_max = t;
    }
    printf("  %-9s 80 px at 30 px/s: %d ticks, gait %s, top moves %d px\n", kn(k), ticks,
           an(k == CR_SQUIRREL || k == CR_FROG || k == CR_BIRD ? CA_HOP : CA_MOVE), top_max - top_min);
    CHECK(critterArrived(c), "%s never arrived", kn(k));
    CHECK(c.feet.x == 180.f, "%s stopped at %.3f", kn(k), (double)c.feet.x);
    CHECK(c.action == CA_IDLE, "%s arrival action %s, not idle", kn(k), an(c.action));
    CHECK(c.facing == 1, "%s did not turn toward the target", kn(k));
    CHECK(turned_mid, "%s turn-around took too long", kn(k));
    CHECK(top_max - top_min >= 1, "%s gait has no bob", kn(k));
    for (int t = 0; t < 10; ++t) critterTick(c, SS_TICK_DT);
    CHECK(!c.blending && samePose(c.pose, critterPoseDef((CritterKind)k, CA_IDLE)), "%s not idle after arrival",
          kn(k));
  }
  // Speed dial: a quick critter arrives sooner.
  {
    int t_slow = 0, t_fast = 0;
    for (int d = 0; d < 2; ++d) {
      Critter c;
      critterInit(c, CR_CHIPMUNK, Vec2{100.f, kFeetY}, 1);
      c.speed_dial = d == 0 ? 0.f : 1.f;
      critterMoveTo(c, 200.f, 40.f);
      int t = 0;
      while (!critterArrived(c) && t < 2000) {
        critterTick(c, SS_TICK_DT);
        ++t;
      }
      (d == 0 ? t_slow : t_fast) = t;
    }
    printf("  chipmunk 100 px at 40 px/s: speed dial 0 -> %d ticks, dial 1 -> %d ticks\n", t_slow, t_fast);
    CHECK(t_fast < t_slow, "speed dial has no effect");
  }
  // Bird flight: CA_MOVE first, then the move flies in an arc.
  {
    Critter c;
    critterInit(c, CR_BIRD, Vec2{100.f, kFeetY}, 1);
    critterSetAction(c, CA_MOVE, 0.2f);
    critterMoveTo(c, 160.f, 50.f);
    const int top0 = critterBounds(c).y0;
    int highest = top0;
    for (int t = 0; t < 400 && !critterArrived(c); ++t) {
      critterTick(c, SS_TICK_DT);
      const int y = critterBounds(c).y0;
      if (y < highest) highest = y;
    }
    printf("  bird flight over 60 px rises %d px\n", top0 - highest);
    CHECK(critterArrived(c) && c.feet.x == 160.f && c.action == CA_IDLE, "bird flight did not land at the target");
    CHECK(top0 - highest >= 8, "bird flight too low (%d px)", top0 - highest);
  }
  // Climbing: up the trunk and hold on.
  for (int k = 0; k < CR_COUNT; ++k) {
    if (!supports(k, CA_CLIMB)) continue;
    Critter c;
    critterInit(c, (CritterKind)k, Vec2{150.f, kFeetY}, 1);
    critterClimbTo(c, kFeetY - 50.f, 25.f);
    int ticks = 0;
    while (!critterArrived(c) && ticks < 2000) {
      critterTick(c, SS_TICK_DT);
      ++ticks;
    }
    for (int t = 0; t < 10; ++t) critterTick(c, SS_TICK_DT);
    const Rect b = critterBounds(c);
    printf("  %-9s climbs 50 px in %d ticks; on the trunk its bounds span x %d..%d (trunk line 150)\n", kn(k),
           ticks, b.x0, b.x1);
    CHECK(critterArrived(c) && c.feet.y == kFeetY - 50.f, "%s climb did not arrive", kn(k));
    CHECK(c.action == CA_CLIMB && !c.blending, "%s does not hold on after climbing", kn(k));
    CHECK(samePose(c.pose, critterPoseDef((CritterKind)k, CA_CLIMB)), "%s climb hold not exact", kn(k));
    // Paws reach onto the trunk; the bear hugs it, so its arms reach further.
    const int reach = k == CR_BEAR ? 24 : 10;
    CHECK(b.x1 >= 150 && b.x1 <= 150 + reach && b.x0 < 150 - 10, "%s is not against the trunk line", kn(k));
  }
  // Bear startle: drops and turns around.
  {
    Critter c;
    critterInit(c, CR_BEAR, Vec2{kFeetX, kFeetY}, 1);
    held(c, CA_REAR, 0.f);
    critterSetAction(c, CA_STARTLE, 0.8f);
    for (int t = 0; t < 30; ++t) critterTick(c, SS_TICK_DT);
    CHECK(c.facing == -1, "bear startle did not turn around");
    CHECK(c.turn_t < 0.f && !c.blending, "bear startle not finished");
  }
}

static void testSizes() {
  printf("[5] sizes and sizeof\n");
  const Palette& pal = moodKey(MOOD_DAY);
  printf("  sizeof(Critter) = %d bytes\n", (int)sizeof(Critter));
  CHECK(sizeof(Critter) <= 200, "Critter is %d bytes", (int)sizeof(Critter));
  for (int k = 0; k < CR_COUNT; ++k) {
    for (int rear = 0; rear < 2; ++rear) {
      if (rear && k != CR_BEAR) continue;
      Critter c;
      critterInit(c, (CritterKind)k, Vec2{kFeetX, kFeetY}, 1);
      if (rear) held(c, CA_REAR, 0.f);
      else critterTick(c, 0.f);
      Frame f(kW, kH, pal[GROUND]);
      critterDraw(c, f.s, pal, f.all());
      int top = kH, bottom = -1, left = kW, right = -1;
      // The mouse is measured without its tail: only the columns from the rump on.
      for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
          if (f.at(x, y) != pal[GROUND]) {
            if (k == CR_MOUSE && x < (int)kFeetX - 4) continue;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
            if (x < left) left = x;
            if (x > right) right = x;
          }
      const int h = (int)kFeetY - top, len = right - left + 1;
      if (rear) {
        printf("  bear reared: %d px tall\n", h);
        CHECK(h >= 92 && h <= 106, "reared bear %d px, want about 100", h);
        continue;
      }
      const int big = h > len ? h : len;
      const float nominal[CR_COUNT] = {20.f, 28.f, 18.f, 50.f, 30.f, 24.f, 14.f, 60.f, 8.f, 36.f};
      printf("  %-9s idle %2d px tall, %2d px long (nominal %2.0f, critterHeight %.0f), lowest row feet%+d\n", kn(k),
             h, len, (double)nominal[k], (double)critterHeight((CritterKind)k), bottom - (int)kFeetY);
      CHECK(h - critterHeight((CritterKind)k) <= 1.5f && critterHeight((CritterKind)k) - h <= 1.5f,
            "%s critterHeight %.1f but drawn %d", kn(k), (double)critterHeight((CritterKind)k), h);
      CHECK((float)big >= 0.8f * nominal[k] && (float)big <= 1.2f * nominal[k], "%s size %d, nominal %.0f", kn(k),
            big, (double)nominal[k]);
      // The owl's tail tip hangs over the front of the branch.
      const int below = k == CR_OWL ? 2 : 0;
      CHECK(bottom <= (int)kFeetY + below && bottom >= (int)kFeetY - 2, "%s not on the ground: lowest row %d", kn(k),
            bottom);
    }
  }
}

// ---------------------------------------------------------------------------
// The herd's deer (stage 22, Mike 2026-11-18): graze, stretch, shake, the white flag, the fawn, the ear
// states, critterDeerNose and critterDeerCrown. And the proof that nothing earlier moved.

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

// One frame of a deer: how many pixels differ from the backdrop, the lowest and highest inked rows, a hash.
struct Ink {
  int px = 0, top = 1 << 20, bottom = -1;
  uint64_t hash = 0;
};

static Ink inkOf(const Critter& c, const Palette& pal, Frame& f) {
  Ink k;
  critterDraw(c, f.s, pal, f.all());
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const Pixel p = f.at(x, y);
      h = fnv(h, &p, sizeof p);
      if (p == pal[GROUND]) continue;
      ++k.px;
      if (y < k.top) k.top = y;
      if (y > k.bottom) k.bottom = y;
    }
  k.hash = h;
  return k;
}

static Critter deerAt(uint8_t tone, float scale, int facing) {
  Critter c;
  critterInit(c, CR_DEER, Vec2{kFeetX, kFeetY}, facing);
  c.tone = tone;
  c.scale = scale;
  return c;
}

static void testDeerHerd() {
  printf("[6] the herd's deer: graze, stretch, shake, flag, fawn, ears, nose and crown\n");
  const Palette& pal = moodKey(MOOD_DAY);

  // (a) Nothing earlier moved. These are the hashes (FNV-1a over every 5th of 60 ticks of a 240 x 160
  // frame) that stage 22's baseline recorded before the rig changed, for a deer given each action in turn
  // over 0.5 s (0 to 17), then a walk (18) and a run (19). Actions 6, 8 and 13 (reach, shake, eat) were
  // not the deer's and drew as idle; they are the new ones and must now differ from idle.
  {
    static const uint64_t kBase[CA_COUNT + 2] = {
        0x995cfdafe95db881ull, 0x1ac214060a584c99ull, 0xd2cf8b8b6eacb271ull, 0x995cfdafe95db881ull,
        0xc601e3d21f870870ull, 0x15ffb64c8d14dfb1ull, 0x995cfdafe95db881ull, 0x995cfdafe95db881ull,
        0x995cfdafe95db881ull, 0x995cfdafe95db881ull, 0x995cfdafe95db881ull, 0x3abe0d7b56b3fb3bull,
        0x72190bcadbd6ef0bull, 0x995cfdafe95db881ull, 0x995cfdafe95db881ull, 0x995cfdafe95db881ull,
        0x995cfdafe95db881ull, 0x995cfdafe95db881ull, 0x2eca56a02ddd5959ull, 0x51456da677e10748ull};
    WeatherTint wt;
    memset(&wt, 0, sizeof wt);
    Palette day;
    computePalette(3.f, wt, day);
    static Pixel px[240 * 160];
    Surface s{px, 240, 160, 240, 0};
    const Rect clip{0, 0, 240, 160};
    int same = 0, changed = 0;
    for (int act = 0; act <= CA_COUNT + 1; ++act) {
      Critter c;
      memset(&c, 0, sizeof c);
      critterInit(c, CR_DEER, Vec2{120.f, 130.f}, 1);
      if (act < CA_COUNT) critterSetAction(c, (CritterAction)act, 0.5f);
      else critterMoveTo(c, act == CA_COUNT ? 170.f : 200.f, act == CA_COUNT ? 20.f : 70.f);
      uint64_t h = 1469598103934665603ull;
      for (int t = 0; t < 60; ++t) {
        critterTick(c, SS_TICK_DT);
        if (t % 5 != 4) continue;
        memset(px, 0, sizeof px);
        critterDraw(c, s, day, clip);
        h = fnv(h, px, sizeof px);
      }
      const bool isNew = act == CA_REACH || act == CA_SHAKE || act == CA_EAT;
      if (isNew) {
        CHECK(h != kBase[CA_IDLE], "deer %s still draws as idle", an(act));
        ++changed;
      } else {
        CHECK(h == kBase[act], "deer action %d no longer draws as the baseline (%016llx, was %016llx)", act,
              (unsigned long long)h, (unsigned long long)kBase[act]);
        ++same;
      }
    }
    printf("  %d existing deer scenes hash as the baseline, %d new actions differ from idle\n", same, changed);
  }

  // (b) The flag: nothing at or under 0.08, one primitive above it; the fawn's three spots are three more.
  {
    Frame f(kW, kH, pal[GROUND]);
    int adult_max = 0, fawn_max = 0;
    for (int fawn = 0; fawn < 2; ++fawn)
      for (int a = 0; a < CA_COUNT; ++a) {
        if (!supports(CR_DEER, a)) continue;
        Critter c = deerAt(fawn ? kDeerFawn : 0, fawn ? 0.62f : 1.f, 1);
        held(c, a, 0.2f);
        critterDraw(c, f.s, pal, f.all());
        const int base = critterLastDrawPrimitives();
        for (float k : {0.f, 0.05f, 0.08f}) {
          c.tail_add = k;
          critterDraw(c, f.s, pal, f.all());
          CHECK(critterLastDrawPrimitives() == base, "%s %s: a flag at %.2f drew a primitive", fawn ? "fawn" : "deer",
                an(a), (double)k);
        }
        for (float k : {0.1f, 0.5f, 1.f}) {
          c.tail_add = k;
          critterDraw(c, f.s, pal, f.all());
          const int n = critterLastDrawPrimitives();
          CHECK(n == base + 1, "%s %s: a flag at %.2f drew %d, want %d", fawn ? "fawn" : "deer", an(a), (double)k, n,
                base + 1);
          int& mx = fawn ? fawn_max : adult_max;
          if (n > mx) mx = n;
        }
      }
    printf("  flag: an adult draws %d primitives with it up, a fawn %d (budget %d)\n", adult_max, fawn_max,
           critterBudget(CR_DEER));
    CHECK(adult_max == 7, "adult deer draws %d primitives with the flag up, want 7", adult_max);
    CHECK(fawn_max == 10 && fawn_max <= critterBudget(CR_DEER), "fawn draws %d primitives, want 10", fawn_max);
    // The flag follows tail_add alone: the canter swings the pose's tail every stride and must show none.
    Critter c = deerAt(0, 1.f, 1);
    critterSetAction(c, CA_RUN, 0.f);
    c.moving = true;
    c.gait_w = 1.f;
    c.move_from = c.move_to = c.feet.x;
    for (int i = 0; i < 12; ++i) {
      c.phase = (float)i / 12.f;
      critterTick(c, 0.f);
      critterDraw(c, f.s, pal, f.all());
      CHECK(critterLastDrawPrimitives() == 6, "a cantering deer with no flag drew %d primitives",
            critterLastDrawPrimitives());
    }
  }

  // (c) Bounds hold every pixel, and every state fits the budget: every action, adult and fawn, each
  // ear state, the flag at 0, 0.5 and 1, both facings, and a near fore leg lifted for a stamp.
  {
    static const uint8_t kEars[5] = {0, kDeerEarBack, kDeerEarMid, kDeerEarFwd, kDeerEarBack | kDeerEarFwd};
    int bad = 0, states = 0, worst = 0;
    for (int fawn = 0; fawn < 2; ++fawn)
      for (int a = 0; a < CA_COUNT; ++a) {
        if (!supports(CR_DEER, a)) continue;
        for (int e = 0; e < 5; ++e)
          for (int ti = 0; ti < 3; ++ti)
            for (int facing = -1; facing <= 1; facing += 2)
              for (int ph = 0; ph < 3; ++ph) {
                Critter c = deerAt((uint8_t)((fawn ? kDeerFawn : 0) | kEars[e]), fawn ? 0.62f : 1.f, facing);
                c.tail_add = (float)ti * 0.5f;
                c.fore_add = ph == 2 ? 0.55f : 0.f;
                c.head_add = ph == 1 ? -0.1f : 0.f;
                if (a == CA_MOVE || a == CA_RUN) {
                  critterSetAction(c, (CritterAction)a, 0.f);
                  c.moving = true;
                  c.gait_w = 1.f;
                  c.move_from = c.move_to = c.feet.x;
                  c.phase = 0.1f + 0.3f * (float)ph;
                  critterTick(c, 0.f);
                } else {
                  held(c, a, 0.1f + 0.35f * (float)ph);
                }
                Frame f(kW, kH, pal[GROUND]);
                critterDraw(c, f.s, pal, f.all());
                const int prims = critterLastDrawPrimitives();
                if (prims > worst) worst = prims;
                const Rect b = critterBounds(c);
                int out = 0;
                for (int y = 0; y < kH; ++y)
                  for (int x = 0; x < kW; ++x)
                    if (f.at(x, y) != pal[GROUND] && (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1)) ++out;
                if (out && !bad)
                  printf("  first escape: %s %s ears %d flag %d facing %d phase %d (%d px)\n", fawn ? "fawn" : "deer",
                         an(a), e, ti, facing, ph, out);
                bad += out;
                ++states;
              }
      }
    printf("  %d deer states, %d pixels outside the bounds, most primitives %d\n", states, bad, worst);
    CHECK(bad == 0, "%d deer pixels fell outside critterBounds", bad);
    CHECK(worst <= critterBudget(CR_DEER), "a deer drew %d primitives", worst);
  }

  // (d) Grazing puts the nose to the grass with all four feet down; the fawn is a fawn.
  {
    int worst_gap = 0;
    for (float ph : {0.f, 0.25f, 0.5f, 0.75f}) {
      Critter c = deerAt(0, 1.f, 1);
      held(c, CA_EAT, ph);
      const Vec2 nose = critterDeerNose(c);
      const float gap = c.feet.y - nose.y;
      if ((int)(gap + 0.5f) > worst_gap) worst_gap = (int)(gap + 0.5f);
      Frame g(kW, kH, pal[GROUND]);
      const Ink ink = inkOf(c, pal, g);
      CHECK(gap >= -2.f && gap <= 4.f, "grazing nose %.1f px above the ground at phase %.2f", (double)gap, (double)ph);
      CHECK(ink.bottom >= (int)kFeetY - 2 && ink.bottom <= (int)kFeetY + 1, "grazing deer's lowest row %d, feet %d",
            ink.bottom, (int)kFeetY);
    }
    printf("  graze: the nose is at most %d px above the ground\n", worst_gap);
    Critter big = deerAt(0, 1.f, 1), small = deerAt(kDeerFawn, 0.62f, 1);
    critterTick(big, 0.f);
    critterTick(small, 0.f);
    Frame g1(kW, kH, pal[GROUND]), g2(kW, kH, pal[GROUND]);
    const Ink a = inkOf(big, pal, g1), b = inkOf(small, pal, g2);
    const int ha = (int)kFeetY - a.top, hb = (int)kFeetY - b.top;
    printf("  an adult stands %d px tall, a fawn %d px\n", ha, hb);
    CHECK(hb >= 25 && hb <= 36, "a fawn stands %d px tall", hb);
    CHECK(hb * 10 <= ha * 8, "a fawn (%d) is not clearly smaller than an adult (%d)", hb, ha);
    CHECK(b.bottom >= (int)kFeetY - 2 && b.bottom <= (int)kFeetY + 1, "the fawn's lowest row %d", b.bottom);
  }

  // (e) The ear states draw four different ears.
  {
    uint64_t h[4];
    const uint8_t ears[4] = {0, kDeerEarFwd, kDeerEarMid, kDeerEarBack};
    for (int i = 0; i < 4; ++i) {
      Critter c = deerAt(ears[i], 1.f, 1);
      held(c, CA_LOOK, 0.f);
      Frame f(kW, kH, pal[GROUND]);
      h[i] = inkOf(c, pal, f).hash;
    }
    for (int i = 0; i < 4; ++i)
      for (int j = i + 1; j < 4; ++j) CHECK(h[i] != h[j], "ear states %d and %d draw the same", i, j);
  }

  // (f) critterDeerNose and critterDeerCrown: inside the bounds, the mirror of each other for the two
  // facings, the nose in front of the crown and not above it standing; for a kind that is not a deer they
  // fall back to the mouth and the head.
  {
    for (int fawn = 0; fawn < 2; ++fawn)
      for (int a = 0; a < CA_COUNT; ++a) {
        if (!supports(CR_DEER, a)) continue;
        const uint8_t tone = fawn ? kDeerFawn : 0;
        const float sc = fawn ? 0.62f : 1.f;
        Critter r = deerAt(tone, sc, 1), l = deerAt(tone, sc, -1);
        held(r, a, 0.3f);
        held(l, a, 0.3f);
        const Vec2 nr = critterDeerNose(r), nl = critterDeerNose(l), cr = critterDeerCrown(r), cl = critterDeerCrown(l);
        const Rect b = critterBounds(r);
        CHECK(nr.x >= (float)b.x0 && nr.x <= (float)b.x1 && nr.y >= (float)b.y0 && nr.y <= (float)b.y1,
              "%s %s: the nose (%.1f, %.1f) is outside the bounds", fawn ? "fawn" : "deer", an(a), (double)nr.x,
              (double)nr.y);
        CHECK(cr.x >= (float)b.x0 && cr.x <= (float)b.x1 && cr.y >= (float)b.y0 && cr.y <= (float)b.y1,
              "%s %s: the crown is outside the bounds", fawn ? "fawn" : "deer", an(a));
        CHECK(fabsf((nr.x - kFeetX) + (nl.x - kFeetX)) < 0.01f && fabsf(nr.y - nl.y) < 0.01f,
              "%s %s: the nose does not mirror", fawn ? "fawn" : "deer", an(a));
        CHECK(fabsf((cr.x - kFeetX) + (cl.x - kFeetX)) < 0.01f && fabsf(cr.y - cl.y) < 0.01f,
              "%s %s: the crown does not mirror", fawn ? "fawn" : "deer", an(a));
      }
    Critter c = deerAt(0, 1.f, 1);
    critterTick(c, 0.f);
    const Vec2 n = critterDeerNose(c), cr = critterDeerCrown(c);
    CHECK(n.x > cr.x + 4.f && n.y >= cr.y,
          "idle: the nose (%.1f, %.1f) should be in front of and not above the crown (%.1f, %.1f)", (double)n.x,
          (double)n.y, (double)cr.x, (double)cr.y);
    Critter sq;
    critterInit(sq, CR_SQUIRREL, Vec2{kFeetX, kFeetY}, 1);
    critterTick(sq, 0.f);
    CHECK(critterDeerNose(sq).x == critterMouth(sq).x && critterDeerCrown(sq).y == critterHead(sq).y,
          "a squirrel's deer anchors are not its mouth and head");
    // The old anchor did not move: critterMouth of a deer is still the top of its bounds.
    const Vec2 m = critterMouth(c);
    const Rect b = critterBounds(c);
    CHECK(m.y == (float)b.y0 + 2.f, "critterMouth of a deer moved");
  }

  // (g) The stretch hesitates before it reaches: early in its blend the nose draws back before it goes
  // forward, and by the end it is well in front of where it started.
  {
    Critter c = deerAt(0, 1.f, 1);
    critterTick(c, 0.f);
    const float x0 = critterDeerNose(c).x;
    critterSetAction(c, CA_REACH, 1.8f);
    float lo = x0, last = x0;
    for (int t = 0; t < 45; ++t) {
      critterTick(c, SS_TICK_DT);
      last = critterDeerNose(c).x;
      if (last < lo) lo = last;
    }
    printf("  stretch: the nose draws back %.1f px, then ends %.1f px past its start\n", (double)(x0 - lo),
           (double)(last - x0));
    CHECK(x0 - lo >= 0.5f, "the stretch has no hesitation (the nose only draws back %.2f px)", (double)(x0 - lo));
    CHECK(last - x0 >= 4.f, "the stretch only reaches %.1f px", (double)(last - x0));
  }

  // (h) The shake moves the deer.
  {
    Critter c = deerAt(0, 1.f, 1);
    held(c, CA_SHAKE, 0.f);
    float x_lo = 1e9f, x_hi = -1e9f;
    for (int i = 0; i < 40; ++i) {
      c.phase = (float)i / 40.f;
      critterTick(c, 0.f);
      const Rect b = critterBounds(c);
      if ((float)b.x1 > x_hi) x_hi = (float)b.x1;
      if ((float)b.x1 < x_lo) x_lo = (float)b.x1;
    }
    CHECK(x_hi - x_lo >= 2.f, "the shake moves the deer only %.1f px", (double)(x_hi - x_lo));
  }
}

// ---------------------------------------------------------------------------
// Inspection sheets.

static int rowHeight(int k) {
  switch (k) {
    case CR_BEAR: return 122;
    case CR_DEER: return 70;
    case CR_SQUIRREL: return 46;
    case CR_FROG: return 46;
    case CR_OWL: case CR_RACCOON: return 40;
    default: return 34;
  }
}

static void sheetActions(const char* path, Mood mood) {
  const Palette& pal = moodKey(mood);
  const int cw = 112;
  int total = 0;
  for (int si = 0; si < 2; ++si)
    for (int k = 0; k < CR_COUNT; ++k) total += si == 0 ? rowHeight(k) : (int)(0.7f * (float)rowHeight(k)) + 6;
  Frame f(cw * CA_COUNT, total, pal[GROUND]);
  int y = 0;
  for (int si = 0; si < 2; ++si)
    for (int k = 0; k < CR_COUNT; ++k) {
      const int rh = si == 0 ? rowHeight(k) : (int)(0.7f * (float)rowHeight(k)) + 6;
      for (int a = 0; a < CA_COUNT; ++a) {
        if (!supports(k, a)) continue;
        const int cx = a * cw;
        Critter c;
        const bool climb = a == CA_CLIMB;
        critterInit(c, (CritterKind)k, Vec2{(float)(cx + cw / 2 + (climb ? 14 : 0)), (float)(y + rh - 5)}, 1);
        c.scale = si == 0 ? 1.f : 0.7f;
        c.carry = itemFor(k, a);
        if (climb) c.feet.y -= 2.f;
        sheetState(c, a);
        if (climb) fillRect(f.s, Rect{(int)c.feet.x, y, (int)c.feet.x + 6, y + rh}, pal[TRUNK], f.all());
        critterDraw(c, f.s, pal, Rect{cx, y, cx + cw, y + rh});
      }
      y += rh;
    }
  CHECK(writePPM(path, f), "could not write %s", path);
}

static void sheetGaits(const char* path) {
  const Palette& pal = moodKey(MOOD_DAY);
  struct Row { int k, a; };
  std::vector<Row> rows;
  for (int k = 0; k < CR_COUNT; ++k)
    for (int a : {CA_MOVE, CA_HOP, CA_RUN})
      if (supports(k, a)) rows.push_back(Row{k, a});
  rows.push_back(Row{CR_BEAR, CA_REAR});
  for (int k = 0; k < CR_COUNT; ++k)
    if (supports(k, CA_CLIMB)) rows.push_back(Row{k, CA_CLIMB});
  const int cw = 112;
  int total = 0;
  for (const Row& r : rows) total += r.a == CA_CLIMB ? rowHeight(r.k) + 10 : rowHeight(r.k);
  Frame f(cw * 8, total, pal[GROUND]);
  int y = 0;
  for (const Row& r : rows) {
    const int rh = r.a == CA_CLIMB ? rowHeight(r.k) + 10 : rowHeight(r.k);
    for (int i = 0; i < 8; ++i) {
      const int cx = i * cw;
      Critter c;
      critterInit(c, (CritterKind)r.k, Vec2{(float)(cx + cw / 2), (float)(y + rh - 5)}, 1);
      if (r.a == CA_REAR) {
        partway(c, CA_REAR, 1.f, (i * 25) / 7);   // the blend from all fours to standing
      } else if (r.a == CA_CLIMB) {
        c.feet.x += 14.f;
        held(c, CA_CLIMB, 0.f);
        c.climbing = true;   // the gait at full weight, without moving
        c.gait_w = 1.f;
        c.phase = (float)i / 8.f;
        critterTick(c, 0.f);
        fillRect(f.s, Rect{(int)c.feet.x, y, (int)c.feet.x + 6, y + rh}, pal[TRUNK], f.all());
      } else if (r.a == CA_HOP) {
        critterSetAction(c, CA_HOP, 1.f);
        c.phase = (float)i / 8.f;
        c.moving = true;   // the gait path, without moving
        c.gait_w = 1.f;
        c.move_from = c.move_to = c.feet.x;
      } else {
        held(c, r.a, (float)i / 8.f);
      }
      critterDraw(c, f.s, pal, Rect{cx, y, cx + cw, y + rh});
    }
    y += rh;
  }
  CHECK(writePPM(path, f), "could not write %s", path);
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed; these tests draw without appStart too
  testBoundsAndBudget();
  testMirror();
  testBlend();
  testMoves();
  testSizes();
  testDeerHerd();
  sheetActions("critters_day.ppm", MOOD_DAY);
  sheetActions("critters_night.ppm", MOOD_NIGHT);
  sheetGaits("critters_gaits.ppm");
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
