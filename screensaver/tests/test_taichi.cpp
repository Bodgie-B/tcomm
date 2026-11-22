// Tests for core/scene/taichi: the tai chi module alone (TAICHI_PLAN 11.1, INTEGRATION_PLAN step B2).
//  1 table rules: one foot moves a segment; the turn keys; every key in reach; the hips are over the
//    standing foot before the other one leaves the ground
//  2 planted feet stay put (0.6 px in x, 1.0 px in y) for the whole time they are down
//  3 continuity: the body (lean, head, thighs) under 0.06 rad a tick, the hips 0.5 px a tick, the limbs
//    by screen speed (elbows 1 px, knees 1.5 px, hands and feet 1.4 px a tick), the turns included
//    (a facing flip is compared mirrored); the gags apart (the itch's slap is the only fast move)
//  4 timing: each chapter's form seconds, the gags' seconds, the bump that never comes
//  5 the signature shapes: crane, single whip, rooster, snake, kick, needle
//  6 frozen: taichiStep(s, 0) changes nothing, bit for bit
//  7 determinism: two runs, the same poses every tick (no libm trig: taichi.cpp uses sinT/cosT, sqrtf and
//    its own taiAtan2; checked by reading the object's imports, not here)
//  8 draw: every key both ways inside hikerBounds, the primitive budget, the head clear of the hands, the
//    elbow never above the shoulder (REVIEW_FIXES F20) but for the overhead forms
//  9 the turn: the frame before and after the flip differ by 60 pixels or fewer, all of them edge shades only
//    (plan: 40, see the test)
//  plus the module's copy of the rig's kinematics against hikerHandPos, the butterfly (bounds, every changed
//  pixel inside the rows marked, it comes, it settles clear of his head) and the numbers (sizes, time a pose).
// Plain executable; returns non-zero on failure.
#include "anim/ease.h"
#include "gfx/raster.h"
#include "palette.h"
#include "scene/hiker.h"
#include "scene/taichi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>
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

static const float kX0 = 140.f, kY0 = 170.f, kH = 90.f, kDt = SS_TICK_DT;
static const float kPi = 3.14159265f;

struct Run {
  TaichiState s;
  Hiker h;
  uint32_t tick;
  int bump_wait;     // the test plays Copper: he arrives 30 ticks after the call (-1: no answer)
  bool answer;
  uint16_t seen;     // every event so far
  int done_tick;
};

static void start(Run& r, int ch, uint8_t gag, int dir = 1, bool answer = true) {
  memset(&r, 0, sizeof r);
  const float x0 = dir > 0 ? kX0 : kX0 + 60.f;
  hikerInit(r.h, Vec2{x0, kY0}, dir);
  r.h.hat_on = false;
  r.h.pack_on = false;
  taichiInit(r.s, (uint8_t)ch, gag, x0, kY0, 1.f, dir, 0);
  r.bump_wait = -1;
  r.answer = answer;
  r.done_tick = -1;
  hikerTick(r.h, 0.f);
  taichiPose(r.s, r.h);
}

static void step(Run& r) {
  taichiStep(r.s, kDt);
  const uint16_t ev = taichiEvents(r.s);
  r.seen = (uint16_t)(r.seen | ev);
  if ((ev & TE_DONE) && r.done_tick < 0) r.done_tick = (int)r.tick + 1;
  if ((ev & TE_BUMP_CALL) && r.answer) r.bump_wait = 0;
  if (r.bump_wait >= 0 && ++r.bump_wait == 30) {
    taichiGag(r.s, TG_BUMP, 1);
    r.bump_wait = -1;
  }
  hikerTick(r.h, kDt);
  taichiPose(r.s, r.h);
  ++r.tick;
  taichiFlyStep(r.s, r.tick);
}

static void runTo(Run& r, float t) {
  const uint32_t n = (uint32_t)lroundf(t / kDt);
  while (r.tick < n) step(r);
}

// The rig's kinematics again (hiker.cpp), for the knees and the hips the rig does not expose.
struct Body {
  Vec2 hip, head, knee[2], ankle[2], hand[2], elbow[2], shoulder;
  float head_r, foot_low[2];
};
static Vec2 dirA(float a) { return Vec2{sinf(a), cosf(a)}; }
static Body body(const Hiker& h) {
  const float H = h.height;
  const HikerPose& P = h.pose;
  const float breath = (1.f - h.walk_w) * 0.011f * H * h.breath_scale * (0.5f + 0.5f * sinT(h.breath_phase));
  Vec2 hip{P.dx * H, 0.f};
  const Vec2 up{sinf(P.lean), -cosf(P.lean)};
  const Vec2 chest{hip.x + up.x * (0.235f * H + breath), hip.y + up.y * (0.235f * H + breath)};
  const Vec2 sh{hip.x + up.x * (0.25f * H + breath), hip.y + up.y * (0.25f * H + breath)};
  const Vec2 neck{hip.x + up.x * (0.29f * H + breath), hip.y + up.y * (0.29f * H + breath)};
  const float ht = P.lean + P.head;
  const Vec2 hu{sinf(ht), -cosf(ht)}, hf{cosf(ht), sinf(ht)};
  const Vec2 head{neck.x + hu.x * 0.06f * H + hf.x * 0.012f * H, neck.y + hu.y * 0.06f * H + hf.y * 0.012f * H};
  Body b;
  float low = fmaxf(hip.y, chest.y) + 0.075f * H;
  const float heel = 0.02f + 0.015f * h.foot_turn, toe = 0.07f - 0.035f * h.foot_turn;
  for (int i = 0; i < 2; ++i) {
    b.knee[i] = Vec2{hip.x + dirA(P.thigh[i]).x * 0.235f * H, hip.y + dirA(P.thigh[i]).y * 0.235f * H};
    const float shin = P.thigh[i] - P.knee[i];
    b.ankle[i] = Vec2{b.knee[i].x + dirA(shin).x * 0.235f * H, b.knee[i].y + dirA(shin).y * 0.235f * H};
    const Vec2 fd = dirA(0.5f * kPi + 0.75f * shin);
    b.foot_low[i] = fmaxf(b.ankle[i].y - fd.y * heel * H, fmaxf(b.ankle[i].y + fd.y * toe * H, b.ankle[i].y)) + 0.029f * H;
    low = fmaxf(low, b.knee[i].y + 0.0375f * H);
    low = fmaxf(low, b.foot_low[i]);
    b.elbow[i] = Vec2{sh.x + dirA(P.shoulder[i]).x * 0.16f * H, sh.y + dirA(P.shoulder[i]).y * 0.16f * H};
    const Vec2 fo = dirA(P.shoulder[i] + P.elbow[i]);
    b.hand[i] = Vec2{b.elbow[i].x + fo.x * 0.15f * H, b.elbow[i].y + fo.y * 0.15f * H};
  }
  low = fmaxf(low, head.y + 0.062f * H);
  const float oy = h.feet.y - low - P.lift * H, sx = h.facing < 0 ? -1.f : 1.f;
  auto fr = [&](Vec2 p) { return Vec2{h.feet.x + sx * p.x, oy + p.y}; };
  b.hip = fr(hip);
  b.head = fr(head);
  b.shoulder = fr(sh);
  b.head_r = 0.062f * H;
  for (int i = 0; i < 2; ++i) {
    b.knee[i] = fr(b.knee[i]);
    b.ankle[i] = fr(b.ankle[i]);
    b.hand[i] = fr(b.hand[i]);
    b.elbow[i] = fr(b.elbow[i]);
    b.foot_low[i] = oy + b.foot_low[i];
  }
  return b;
}

static float holdS(int ch, int k) { return 0.1f * taichiPace() * (float)taichiKey(ch, k).hold; }
static bool stopKey(int ch, int k) { return taichiKey(ch, k).hold >= 10; }
static float showTime(int ch, int k) {
  const float a = taichiKeyArrive(ch, k), hold = holdS(ch, k);
  if (taichiKey(ch, k).hold >= 10) return a + fmaxf(0.6f, 0.5f * hold);
  return a + 0.5f * hold + 0.25f;
}
static int footX(const TaiKey& k, int i) { return i ? k.b : k.a; }
static int footL(const TaiKey& k, int i) { return i ? k.lb : k.la; }
static float wrap(float a) {
  while (a > kPi) a -= 2.f * kPi;
  while (a < -kPi) a += 2.f * kPi;
  return a;
}

// ---- 1 ----
static void testTables() {
  printf("table rules\n");
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    const int n = taichiKeyCount(ch);
    for (int k = 1; k < n; ++k) {
      const TaiKey &p = taichiKey(ch, k - 1), &q = taichiKey(ch, k);
      CHECK(!(p.a != q.a && p.b != q.b), "ch %d key %d moves both feet", ch, k);
      if (q.flags & TK_TURN) {
        CHECK(abs(q.a - q.b) <= 6 && q.lean == 0 && (q.hands & 0x7F) == TH_DOWN, "ch %d turn key %d not gathered", ch, k);
        const TaiKey& nx = taichiKey(ch, k + 1);
        CHECK(nx.face == -q.face, "ch %d key after the turn %d does not face the other way", ch, k);
      }
    }
    // Every key in reach: in the middle of its hold (when the feet, which have no lag, are on the key) each
    // foot is where the table puts it.
    for (int k = 0; k < n; ++k) {
      Run r;
      start(r, ch, TG_NONE);
      runTo(r, taichiKeyArrive(ch, k) + 0.5f * holdS(ch, k));
      const TaiKey& q = taichiKey(ch, k);
      for (int i = 0; i < 2; ++i) {
        const float want = kX0 + 0.01f * (float)footX(q, i) * kH;
        const Vec2 f = taichiFootFrame(r.s, i);
        CHECK(fabsf(f.x - want) <= 1.0f, "ch %d key %d foot %d at %.2f, table %.2f", ch, k, i, (double)f.x, (double)want);
        const float up = kY0 - f.y, want_up = 0.004f * (float)footL(q, i) * kH;
        CHECK(fabsf(up - want_up) <= 1.2f, "ch %d key %d foot %d %.2f px up, table %.2f", ch, k, i, (double)up, (double)want_up);
      }
    }
    // The hips are nearer the standing foot than the stepping one when a foot sets off.
    Run r;
    start(r, ch, TG_NONE);
    Vec2 prev[2] = {taichiFootFrame(r.s, 0), taichiFootFrame(r.s, 1)};
    int still[2] = {0, 0};
    int starts = 0;
    while (!taichiDone(r.s)) {
      step(r);
      for (int i = 0; i < 2; ++i) {
        const Vec2 f = taichiFootFrame(r.s, i);
        const bool moved = fabsf(f.x - prev[i].x) > 0.02f;
        const float sup = taichiFootFrame(r.s, 1 - i).x;
        // From a gathered stance (the turns: feet 3.6 px apart) the hips are over both feet at once.
        if (moved && still[i] >= 10 && fabsf(sup - prev[i].x) > 6.f) {
          ++starts;
          CHECK(fabsf(r.s.hip_x - sup) < fabsf(r.s.hip_x - prev[i].x), "ch %d t %.2f foot %d leaves with the hips %.1f from it, %.1f from the other",
                ch, (double)r.s.t, i, (double)fabsf(r.s.hip_x - prev[i].x), (double)fabsf(r.s.hip_x - sup));
        }
        still[i] = moved ? 0 : still[i] + 1;
        prev[i] = f;
      }
    }
    printf("  chapter %d: %d keys, %d steps checked\n", ch + 1, n, starts);
  }
}

// ---- 2 ----
struct Span {
  float t0, t1;
};
static int plantedSpans(int ch, int foot, Span* out) {
  const int n = taichiKeyCount(ch);
  int m = 0;
  int k = 0;
  while (k < n) {
    const TaiKey& a = taichiKey(ch, k);
    if (footL(a, foot) != 0) {
      ++k;
      continue;
    }
    int e = k;
    while (e + 1 < n && footL(taichiKey(ch, e + 1), foot) == 0 && footX(taichiKey(ch, e + 1), foot) == footX(a, foot)) ++e;
    const float ak = taichiKeyArrive(ch, k), ee = taichiKeyArrive(ch, e);
    const float t0 = (k == 0 || stopKey(ch, k)) ? ak : ak + 0.5f * holdS(ch, k);
    const float t1 = (e == n - 1) ? taichiFormSeconds(ch) : (stopKey(ch, e) ? ee + holdS(ch, e) : ee + 0.5f * holdS(ch, e));
    out[m++] = Span{t0, t1};
    k = e + 1;
  }
  return m;
}

static void testPlanted() {
  printf("planted feet\n");
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    Span sp[2][24];
    int ns[2] = {plantedSpans(ch, 0, sp[0]), plantedSpans(ch, 1, sp[1])};
    float worst_x = 0.f, worst_y = 0.f;
    Run r;
    start(r, ch, TG_NONE);
    float anchor[2][24];
    bool reported[2][24];
    for (int i = 0; i < 2; ++i)
      for (int j = 0; j < 24; ++j) {
        anchor[i][j] = -1.f;
        reported[i][j] = false;
      }
    while (!taichiDone(r.s)) {
      step(r);
      const float t = r.s.t;
      if (r.s.phase != TP_FORM && r.s.phase != TP_OUT) continue;   // the ease in and out are checked too
      for (int i = 0; i < 2; ++i)
        for (int j = 0; j < ns[i]; ++j) {
          if (t < sp[i][j].t0 || t > sp[i][j].t1) continue;
          const Vec2 f = taichiFootFrame(r.s, i);
          if (anchor[i][j] < 0.f) anchor[i][j] = f.x;
          worst_x = fmaxf(worst_x, fabsf(f.x - anchor[i][j]));
          worst_y = fmaxf(worst_y, fabsf(f.y - kY0));
          if ((fabsf(f.x - anchor[i][j]) > 0.6f || fabsf(f.y - kY0) > 1.0f) && !reported[i][j]) {
            reported[i][j] = true;   // once a span
            CHECK(false, "ch %d t %.2f planted foot %d moved: dx %.2f, dy %.2f", ch, (double)t, i,
                  (double)(f.x - anchor[i][j]), (double)(f.y - kY0));
          }
        }
    }
    printf("  chapter %d: %d + %d planted spans, worst slide %.3f px, worst lift %.3f px\n", ch + 1, ns[0], ns[1], (double)worst_x,
           (double)worst_y);
  }
}

// ---- 3 ----
static void poseArr(const Hiker& h, float* a) {
  const HikerPose& P = h.pose;
  a[0] = P.lean;
  a[1] = P.head;
  for (int i = 0; i < 2; ++i) {
    a[2 + i] = P.thigh[i];
    a[4 + i] = P.knee[i];
    a[6 + i] = P.shoulder[i];
    a[8 + i] = P.elbow[i];
  }
}
static const char* const kJoint[10] = {"lean", "head", "thigh0", "thigh1", "knee0", "knee1", "shoulder0", "shoulder1", "elbow0", "elbow1"};

// The body's angles (lean, head, thighs) at the plan's 0.06 rad a tick. The elbows, hands, knees and feet by
// how far they move on screen in a tick: near the shoulder a small hand move needs a large joint turn (the
// fold of a hand passing the ear), and a stepping knee bends quickly while it moves only a few pixels, so
// the angles overstate what the eye sees (changed from TAICHI_PLAN 11.1.3 by B2, 2026-11-18).
// A stepping knee may go 1.5 px a tick (the plan's 0.27 to 0.56 H steps take about 2 s).
static const float kBodyMax = 0.06f, kHipMax = 0.5f, kElbowMax = 1.0f, kHandMax = 1.4f, kKneeMax = 1.5f, kFootMax = 1.4f;

static void testContinuity() {
  printf("continuity\n");
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    for (int gag = 0; gag <= 2; ++gag) {
      if (gag == 1 && ch == 1) continue;   // chapter 2's wobble is the itch
      Run r;
      start(r, ch, (uint8_t)gag);
      float a[10], b[10];
      poseArr(r.h, a);
      Body pb = body(r.h);
      float hip = r.h.feet.x;
      int face = r.h.facing;
      float worst_body = 0.f, worst_hip = 0.f, worst_el = 0.f, worst_hand = 0.f, worst_knee = 0.f, worst_foot = 0.f;
      float t_body = 0.f, t_hip = 0.f, t_el = 0.f, t_hand = 0.f, t_knee = 0.f, t_foot = 0.f;
      float worst_head_flip = 0.f;   // how far the head's disc moves on a flip tick
      int fast_gag = 0;              // ticks the near hand moves faster than the form's hands may (the slap)
      float worst_gag = 0.f;         // the fastest body angle in a gag
      float slap = 0.f, back = 0.f;  // the itch: the near hand's fastest in the slap, and on its way back
      float y_form = 1e9f, y_slap = 1e9f, y_top = 1e9f;   // the itch: the near hand's y on the form, at the neck, highest
      while (!taichiDone(r.s)) {
        step(r);
        poseArr(r.h, b);
        const Body nb = body(r.h);
        const bool flip = r.h.facing != face;
        const bool in_gag = r.s.phase == TP_GAG;
        float tick_max = 0.f;
        for (int j = 0; j < 10; ++j) tick_max = fmaxf(tick_max, fabsf(wrap(b[j] - (flip ? -a[j] : a[j]))));
        if (in_gag) {
          float body_max = 0.f;
          for (int j = 0; j < 4; ++j) body_max = fmaxf(body_max, fabsf(wrap(b[j] - (flip ? -a[j] : a[j]))));
          worst_gag = fmaxf(worst_gag, body_max);
          const float hv = hypotf(nb.hand[0].x - pb.hand[0].x, nb.hand[0].y - pb.hand[0].y);
          if (hv > kHandMax) ++fast_gag;
          if (y_form > 1e8f) y_form = nb.hand[0].y;              // the first gag tick: the hand still on the form
          if (r.s.gag_t >= 0.7f && r.s.gag_t < 0.8f) y_slap = nb.hand[0].y;   // in the slap's hold (after the lag)
          y_top = fminf(y_top, nb.hand[0].y);
          if (r.s.gag_t < 0.65f) slap = fmaxf(slap, hv);   // 0.3 s slap, the hands' 0.25 s lag
          else if (r.s.gag_t > 1.2f) back = fmaxf(back, hv);
        } else {
          const float t = r.s.t;
          for (int j = 0; j < 4; ++j) {   // lean, head, thighs
            // At the flip the bare head is judged by where its disc is, not by its tilt: through a turn it tips
            // back to sit straight over the neck, which is the same disc from both sides (review, 2026-11-18).
            if (flip && j == 1) continue;
            const float d = fabsf(wrap(b[j] - (flip ? -a[j] : a[j])));
            if (d > worst_body) worst_body = d, t_body = t;
          }
          if (flip) worst_head_flip = fmaxf(worst_head_flip, hypotf(nb.head.x - pb.head.x, nb.head.y - pb.head.y));
          auto mv = [](Vec2 p, Vec2 q) { return hypotf(p.x - q.x, p.y - q.y); };
          for (int i = 0; i < 2; ++i) {
            if (mv(nb.elbow[i], pb.elbow[i]) > worst_el) worst_el = mv(nb.elbow[i], pb.elbow[i]), t_el = t;
            if (mv(nb.hand[i], pb.hand[i]) > worst_hand) worst_hand = mv(nb.hand[i], pb.hand[i]), t_hand = t;
            if (mv(nb.knee[i], pb.knee[i]) > worst_knee) worst_knee = mv(nb.knee[i], pb.knee[i]), t_knee = t;
            if (mv(nb.ankle[i], pb.ankle[i]) > worst_foot) worst_foot = mv(nb.ankle[i], pb.ankle[i]), t_foot = t;
          }
          const float dh = fabsf(r.h.feet.x - hip);
          if (dh > worst_hip) worst_hip = dh, t_hip = t;
        }
        memcpy(a, b, sizeof a);
        pb = nb;
        hip = r.h.feet.x;
        face = r.h.facing;
      }
      printf("  chapter %d gag %d: body %.3f rad/tick (t %.1f), px/tick: hips %.2f (t %.1f), elbows %.2f (t %.1f), hands %.2f (t %.1f), "
             "knees %.2f (t %.1f), feet %.2f (t %.1f)",
             ch + 1, gag, (double)worst_body, (double)t_body, (double)worst_hip, (double)t_hip, (double)worst_el, (double)t_el,
             (double)worst_hand, (double)t_hand, (double)worst_knee, (double)t_knee, (double)worst_foot, (double)t_foot);
      if (gag == 1) printf("; the wobble's body at most %.3f rad/tick", (double)worst_gag);
      if (gag == 2) printf("; the slap %.2f px/tick (%d fast ticks), its way back %.2f", (double)slap, fast_gag, (double)back);
      printf("\n");
      CHECK(worst_body <= kBodyMax && worst_hip <= kHipMax && worst_el <= kElbowMax && worst_hand <= kHandMax &&
                worst_knee <= kKneeMax && worst_foot <= kFootMax,
            "chapter %d gag %d moves too fast somewhere (see the line above)", ch + 1, gag);
      CHECK(worst_head_flip <= 0.5f, "chapter %d gag %d: the head jumps %.2f px at a flip", ch + 1, gag, (double)worst_head_flip);
      // The itch (TAICHI_PLAN 6.2 and 11.1.3): the slap is the one fast move and the hand goes back slower than
      // anything else he does; measured on the hand, not the joints (the neck is next to the shoulder, where
      // the angles race while the hand barely moves). The wobble's catch is a startle: the body under 0.5.
      if (gag == 2)
        CHECK(slap >= 2.f && fast_gag <= 10 && back < worst_hand, "the itch: slap %.2f, %d fast ticks, back %.2f (form's hands %.2f)",
              (double)slap, fast_gag, (double)back, (double)worst_hand);
      if (gag == 1) CHECK(worst_gag <= 0.5f, "the wobble moves too fast: %.3f rad/tick", (double)worst_gag);
      // Added in review, 2026-11-18: out of the single whip the slap's way back rose 7 px above both ends of
      // its path, up past his head and out, which read as a wave. The near hand may rise at most 1.5 px above
      // the higher of where it starts (on the form) and where it slaps (his neck), both ways.
      if (gag == 2) {
        const float rise = fminf(y_form, y_slap) - y_top;
        printf("    the itch's hand rises %.1f px above the ends of its path\n", (double)rise);
        CHECK(rise <= 1.5f, "chapter %d: the itch's hand rises %.1f px over its path", ch + 1, (double)rise);
      }
    }
  }
}

// ---- 4 ----
static void testTiming() {
  printf("timing\n");
  const float want[4] = {105.8f, 115.3f, 107.7f, 107.6f};
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    CHECK(fabsf(taichiFormSeconds(ch) - want[ch] * taichiPace()) < 0.01f, "ch %d form %.2f s", ch, (double)taichiFormSeconds(ch));
    for (int gag = 0; gag <= 3; ++gag) {
      for (int answer = 1; answer >= (gag == 3 ? 0 : 1); --answer) {
        Run r;
        start(r, ch, (uint8_t)gag, 1, answer != 0);
        while (!taichiDone(r.s) && r.tick < 10000) step(r);
        const float T = taichiFormSeconds(ch) + taichiGagSeconds(ch, (uint8_t)gag);
        const int want_tick = (int)ceilf(T / kDt - 1e-3f);
        CHECK(abs(r.done_tick - want_tick) <= 1, "ch %d gag %d answer %d: done at tick %d, want %d", ch, gag, answer, r.done_tick, want_tick);
        const int out = (int)lroundf(taichiOutSeconds() / kDt);
        CHECK(abs((int)r.tick - r.done_tick - out) <= 1, "ch %d the ease out took %d ticks", ch, (int)r.tick - r.done_tick);
        CHECK(r.seen & TE_CLOSING, "ch %d no closing cue", ch);
        if (gag) CHECK((r.seen & TE_GAG) || (gag == 3 && !answer), "ch %d gag %d never played", ch, gag);
        if (gag == 3) CHECK(r.seen & TE_BUMP_CALL, "ch %d no bump call", ch);
      }
    }
    printf("  chapter %d: %.1f s, wobble +%.0f, itch +%.0f, bump +%.0f\n", ch + 1, (double)taichiFormSeconds(ch),
           (double)taichiGagSeconds(ch, TG_WOBBLE), (double)taichiGagSeconds(ch, TG_ITCH), (double)taichiGagSeconds(ch, TG_BUMP));
  }
}

// ---- 5 ----
static Run at(int ch, int k) {
  Run r;
  start(r, ch, TG_NONE);
  runTo(r, showTime(ch, k));
  return r;
}
static void testShapes() {
  printf("signature shapes\n");
  Run r0;
  start(r0, 0, TG_NONE);
  runTo(r0, 3.f);
  const float stand_hip = body(r0.h).hip.y;
  {
    const Run r = at(0, 8);
    const Body b = body(r.h);
    const float top = b.head.y - b.head_r, hand = fminf(b.hand[0].y, b.hand[1].y);
    printf("  crane: hand %.1f px above the head\n", (double)(top - hand));
    CHECK(top - hand >= 8.f, "crane hand only %.1f px above the head", (double)(top - hand));
  }
  {
    const Run r = at(1, 14);
    const Body b = body(r.h);
    const float dx = fabsf(b.hand[0].x - b.hand[1].x);
    const bool sides = (b.hand[0].x - b.hip.x) * (b.hand[1].x - b.hip.x) < 0.f;
    printf("  single whip: hands %.1f px apart\n", (double)dx);
    CHECK(dx >= 48.f && sides, "whip hands %.1f apart, opposite sides %d", (double)dx, sides);
  }
  {
    const Run r = at(2, 17);
    const Body b = body(r.h);
    int ok = 0;
    for (int i = 0; i < 2; ++i)
      if (fabsf(b.knee[i].y - b.hip.y) <= 4.f && kY0 - b.foot_low[i] >= 18.f) ok = 1;
    printf("  rooster: knees %.1f / %.1f px below the hip, feet %.1f / %.1f px up\n", (double)(b.knee[0].y - b.hip.y),
           (double)(b.knee[1].y - b.hip.y), (double)(kY0 - b.foot_low[0]), (double)(kY0 - b.foot_low[1]));
    CHECK(ok, "rooster knee not at hip height with its foot up");
  }
  {
    // Changed from the plan (B2): "one leg within 0.35 rad of the ground" is the hip-to-ankle line within
    // 0.5 rad: deeper than that his seat almost touched the ground and it read as sitting (posture sheet).
    const Run r = at(2, 15);
    const Body b = body(r.h);
    float best = 9.f;
    for (int i = 0; i < 2; ++i) {
      const float dx = fabsf(b.ankle[i].x - b.hip.x), dy = b.ankle[i].y - b.hip.y;
      best = fminf(best, atan2f(dy, dx));
    }
    printf("  snake: hips %.1f px below standing, the long leg %.2f rad off the ground\n", (double)(b.hip.y - stand_hip), (double)best);
    CHECK(b.hip.y - stand_hip >= 18.f && best <= 0.5f, "snake: hips %.1f lower, leg %.2f rad", (double)(b.hip.y - stand_hip), (double)best);
  }
  {
    const Run r = at(3, 5);
    const Body b = body(r.h);
    int ok = 0;
    for (int i = 0; i < 2; ++i)
      if (kY0 - b.foot_low[i] >= 24.f && (b.ankle[i].x - b.hip.x) * (float)r.h.facing >= 26.f) ok = 1;
    printf("  kick: feet %.1f / %.1f px up\n", (double)(kY0 - b.foot_low[0]), (double)(kY0 - b.foot_low[1]));
    CHECK(ok, "kick foot not up and ahead");
  }
  {
    const Run r = at(3, 9);
    printf("  needle: lean %.2f\n", (double)r.h.pose.lean);
    CHECK(r.h.pose.lean >= 0.5f, "needle lean %.2f", (double)r.h.pose.lean);
  }
}

// ---- 6, 7 ----
static void testFrozenAndDeterminism() {
  printf("frozen and determinism\n");
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    Run r;
    start(r, ch, TG_WOBBLE);
    const float times[4] = {12.3f, 40.04f, 63.5f, 90.2f};
    for (float t : times) {
      runTo(r, t);
      const HikerPose before = r.h.pose;
      const Vec2 feet = r.h.feet;
      const TaichiState s0 = r.s;
      for (int i = 0; i < 85; ++i) {   // a doorbell: the story stands, the rig is recomposed with dt 0
        taichiStep(r.s, 0.f);
        hikerTick(r.h, 0.f);
        taichiPose(r.s, r.h);
      }
      CHECK(memcmp(&before, &r.h.pose, sizeof before) == 0 && feet.x == r.h.feet.x, "ch %d t %.1f the pose moved while frozen", ch,
            (double)t);
      CHECK(memcmp(&s0, &r.s, sizeof s0) == 0, "ch %d t %.1f the state changed while frozen", ch, (double)t);
    }
    Run a, b;
    start(a, ch, TG_BUMP);
    start(b, ch, TG_BUMP);
    int diff = 0;
    while (!taichiDone(a.s)) {
      step(a);
      step(b);
      if (memcmp(&a.h.pose, &b.h.pose, sizeof a.h.pose) != 0 || a.h.feet.x != b.h.feet.x) ++diff;
    }
    CHECK(diff == 0 && taichiDone(b.s), "ch %d two runs differ on %d ticks", ch, diff);
  }
  // taichiHolding (REVIEW_FIXES F8): the stillness and the single whip hold, the commencing move does not.
  {
    Run r;
    start(r, 0, TG_NONE);
    runTo(r, 2.f);
    CHECK(taichiHolding(r.s, 2.f), "not holding in the opening stillness");
    runTo(r, 7.f);
    CHECK(!taichiHolding(r.s, 2.f), "holding in the commencing move");
    Run w;
    start(w, 1, TG_NONE);
    runTo(w, taichiKeyArrive(1, 14) + 3.f);
    CHECK(taichiHolding(w.s, 2.f), "not holding in the single whip");
    CHECK(!taichiHolding(w.s, 8.f), "the single whip held longer than its 7 s");
  }
}

// ---- 8 ----
struct Frame {
  std::vector<Pixel> px;
  Surface s;
  Frame(int w, int h, Pixel fill) : px((size_t)w * h, fill) { s = Surface{px.data(), w, h, w, 0}; }
};

static bool overhead(const TaiKey& k) {
  const int hp = k.hands & 0x7F;
  // Hands overhead, and the high pat's palm at eye level: a near-straight arm lifts its elbow a pixel or so
  // above the shoulder there, as an arm does; F20 is about the chicken wing of a hand behind him.
  return hp == TH_CRANE || hp == TH_FAN || hp == TH_NEEDLE_UP || hp == TH_HIGHPAT;
}

static void testDraw(const Palette& pal) {
  printf("draw\n");
  const Pixel bg = rgb565(250, 250, 250);
  int max_prims = 0, keys = 0;
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    for (int dir = 1; dir >= -1; dir -= 2) {
      for (int k = 0; k < taichiKeyCount(ch); ++k) {
        Run r;
        start(r, ch, TG_NONE, dir);
        runTo(r, showTime(ch, k));
        Frame f(320, 220, bg);
        hikerDraw(r.h, f.s, pal, Rect{0, 0, 320, 220});
        const int prims = hikerLastDrawPrimitives();
        max_prims = prims > max_prims ? prims : max_prims;
        CHECK(prims <= 22, "ch %d key %d draws %d primitives", ch, k, prims);
        const Rect bb = hikerBounds(r.h);
        int outside = 0;
        for (int y = 0; y < 220; ++y)
          for (int x = 0; x < 320; ++x)
            if (f.px[(size_t)y * 320 + x] != bg && (x < bb.x0 || x >= bb.x1 || y < bb.y0 || y >= bb.y1)) ++outside;
        CHECK(outside == 0, "ch %d key %d dir %d: %d pixels outside the bounds", ch, k, dir, outside);
        const TaiKey& q = taichiKey(ch, k);
        const Body b = body(r.h);
        const int hp = q.hands & 0x7F;   // the plan: clear but for the crane and the fan (and DOWN, inside the body)
        if (hp != TH_CRANE && hp != TH_FAN && hp != TH_DOWN) {
          for (int i = 0; i < 2; ++i) {
            const float d = hypotf(b.hand[i].x - b.head.x, b.hand[i].y - b.head.y);
            CHECK(d >= b.head_r + 2.5f, "ch %d key %d hand %d %.1f px from the head's centre", ch, k, i, (double)d);
          }
        }
        ++keys;
      }
    }
  }
  printf("  %d keys drawn both ways, at most %d primitives\n", keys / 2, max_prims);
  // The elbow is never above the shoulder joint (REVIEW_FIXES F20), every tick, but in the overhead forms
  // and the moves into and out of them.
  float worst_fk = 0.f;
  bool fk_reported = false;
  for (int ch = 0; ch < TAICHI_CHAPTERS; ++ch) {
    Run r;
    start(r, ch, TG_NONE);
    float worst = 0.f;
    while (!taichiDone(r.s)) {
      step(r);
      // Excused: the overhead form the hands are moving into or holding, and the move out of it. The hands
      // run 0.25 s behind the hips, so the key is taken at their time, not at r.s.key (review, 2026-11-18:
      // the builder's version excused the whole key before every overhead form too).
      const float th = r.s.t - 0.25f;
      int k = 0;
      while (k + 1 < taichiKeyCount(ch) && th > taichiKeyHoldEnd(ch, k)) ++k;
      const bool excused = overhead(taichiKey(ch, k)) || (k > 0 && overhead(taichiKey(ch, k - 1)));
      // The module's copy of the rig's kinematics (taichi.cpp fk, which the guests and the planted-feet
      // test read) against the rig itself, every tick (added in review, 2026-11-18: taichi.cpp said this was
      // checked and it was not). hikerHandPos leaves out breath_scale, which hikerDraw uses (hiker.cpp build),
      // so the drawn hand sits higher by the extra breath along the spine while the form deepens his breath;
      // that much is added back here. The module follows the drawing; the guests must use TaichiState::hand,
      // not hikerHandPos, to sit on the drawn hand.
      const float br = (1.f - r.h.walk_w) * 0.011f * r.h.height * (r.h.breath_scale - 1.f) * (0.5f + 0.5f * sinT(r.h.breath_phase));
      const float sxf = r.h.facing < 0 ? -1.f : 1.f;
      for (int i = 0; i < 2; ++i) {
        Vec2 rig = hikerHandPos(r.h, i);
        rig.x += sxf * sinf(r.h.pose.lean) * br;
        rig.y -= cosf(r.h.pose.lean) * br;
        const float e = hypotf(rig.x - r.s.hand[i].x, rig.y - r.s.hand[i].y);
        worst_fk = fmaxf(worst_fk, e);
        if (e > 0.05f && !fk_reported) {
          fk_reported = true;
          CHECK(false, "ch %d t %.2f hand %d: the module's kinematics %.3f px from hikerHandPos", ch, (double)r.s.t, i, (double)e);
        }
      }
      if (excused) continue;
      const Body b = body(r.h);
      for (int i = 0; i < 2; ++i) {
        const float up = b.shoulder.y - b.elbow[i].y;
        worst = fmaxf(worst, up);
        if (up > 0.5f) {
          CHECK(false, "ch %d t %.2f (key %d) elbow %d is %.1f px above the shoulder", ch, (double)r.s.t, k, i, (double)up);
          break;
        }
      }
    }
    printf("  chapter %d: the elbow at most %.2f px above the shoulder outside the overhead forms\n", ch + 1, (double)worst);
  }
  printf("  the module's hands at most %.4f px from hikerHandPos\n", (double)worst_fk);
}

// ---- 9 ----
// Pixels that differ, and of those how many change by more than a quarter of the way from his colour to the
// ground's (a pixel that moved, not an edge's shade).
static int frameDiff(const Run& a, const Run& b, const Palette& pal, int* strong = nullptr) {
  Frame fa(320, 220, rgb565(250, 250, 250)), fb(320, 220, rgb565(250, 250, 250));
  hikerDraw(a.h, fa.s, pal, Rect{0, 0, 320, 220});
  hikerDraw(b.h, fb.s, pal, Rect{0, 0, 320, 220});
  int n = 0, big = 0;
  for (size_t i = 0; i < fa.px.size(); ++i) {
    if (fa.px[i] == fb.px[i]) continue;
    ++n;
    if (abs((int)green8(fa.px[i]) - (int)green8(fb.px[i])) > 60) ++big;
  }
  if (strong) *strong = big;
  return n;
}

static void testTurn(const Palette& pal) {
  printf("the turn\n");
  const int turns[3][2] = {{1, 7}, {1, 15}, {3, 11}};
  for (const auto& tk : turns) {
    int aided = 0;
    for (int aid = 1; aid >= 0; --aid) {
      Run r, prev;
      start(r, tk[0], TG_NONE);
      r.s.turn_aid_off = (uint8_t)(aid ? 0 : 1);
      int face = r.h.facing, flips = 0, worst = 0, worst_strong = 0;
      while (r.s.t < taichiKeyHoldEnd(tk[0], tk[1]) + 0.1f) {
        prev = r;
        step(r);
        if (r.s.t < taichiKeyArrive(tk[0], tk[1]) - 2.f) {   // before this turn's window: start its facing here
          face = r.h.facing;
          continue;
        }
        if (r.h.facing != face) {
          ++flips;
          int st = 0;
          const int d = frameDiff(prev, r, pal, &st);
          worst = d > worst ? d : worst;
          worst_strong = st > worst_strong ? st : worst_strong;
          face = r.h.facing;
        }
      }
      printf("  chapter %d key %d %s: %d pixels change at the flip, %d of them more than an edge's shade\n", tk[0] + 1, tk[1],
             aid ? "aided" : "plain", worst, worst_strong);
      CHECK(flips == 1, "chapter %d key %d flipped %d times", tk[0] + 1, tk[1], flips);
      // TAICHI_PLAN 11.1.9 asks for 40 or fewer. B2 had 123 to 133 and allowed 160: the rig sets the head
      // 0.012 H ahead of the neck (it jumped 2 px) and the hips' drift sat 0.3 px off the middle of the gathered
      // feet. Review, 2026-11-18: the bare head now tips back to sit over the neck and the drift fades through
      // the turn, so the figure is the same from both sides; what is left (18 to 44 pixels) is edge shading
      // only, the rasterizer's coverage not being exactly mirror symmetric. So: no pixel may change by more
      // than an edge's shade, and at most 60 may change at all. Plain, about 1,090 change.
      if (aid) {
        CHECK(worst <= 60 && worst_strong == 0, "chapter %d key %d: %d pixels change at the flip, %d strongly", tk[0] + 1, tk[1], worst,
              worst_strong);
        aided = worst;
      } else {
        CHECK(aided * 5 < worst, "chapter %d key %d: the aids save too little (%d against %d)", tk[0] + 1, tk[1], aided, worst);
      }
    }
  }
  Run r;
  start(r, 1, TG_NONE);
  while (!taichiDone(r.s)) step(r);
  CHECK(r.h.foot_turn == 0.f && r.h.breath_rate == 1.f && r.h.breath_scale == 1.f, "after the form: foot_turn %.2f, breath %.2f %.2f",
        (double)r.h.foot_turn, (double)r.h.breath_rate, (double)r.h.breath_scale);
  taichiFinish(r.s, r.h);
  CHECK(r.h.foot_turn == 0.f && r.h.breath_rate == 1.f, "taichiFinish left the rig changed");
}

// ---- the butterfly ----
static void testFly(const Palette& pal) {
  printf("the butterfly\n");
  Run r;
  start(r, 0, TG_NONE);
  taichiFlyEnable(r.s, true);
  int shown = 0, outside = 0, marks = 0, sat = 0, unmarked = 0, on_head = 0;
  TaichiState prev = r.s;
  Frame fp(400, 260, rgb565(0, 0, 0)), fc(400, 260, rgb565(0, 0, 0));
  while (!taichiDone(r.s) || r.s.fly_mode != 6) {
    step(r);
    if (r.tick > 6000) break;
    if (taichiFlyShown(r.s)) {
      ++shown;
      Frame f(320, 220, rgb565(0, 0, 0));
      const Rect bb = taichiFlyBounds(r.s);
      const int ox = (int)r.s.fly.x - 160, oy = (int)r.s.fly.y - 110;
      TaichiState s = r.s;   // drawn moved into the small frame
      s.fly.x -= (float)ox;
      s.fly.y -= (float)oy;
      taichiFlyDraw(s, f.s, Rect{0, 0, 320, 220}, pal);
      for (int y = 0; y < 220; ++y)
        for (int x = 0; x < 320; ++x)
          if (f.px[(size_t)y * 320 + x] != 0 && (x + ox < bb.x0 || x + ox >= bb.x1 || y + oy < bb.y0 || y + oy >= bb.y1)) ++outside;
      if (r.s.fly_mode == 4) ++sat;
    }
    Rect rr[TAICHI_DIRTY_MAX];
    const int nd = taichiDirty(prev, r.s, rr);
    marks += nd > 0;
    // Incremental equals full (added in review, 2026-11-18): every pixel that differs between last tick's
    // drawing and this tick's lies in a rectangle taichiDirty marked, drawn at frame coordinates.
    std::fill(fp.px.begin(), fp.px.end(), (Pixel)0);
    std::fill(fc.px.begin(), fc.px.end(), (Pixel)0);
    taichiFlyDraw(prev, fp.s, Rect{0, 0, 400, 260}, pal);
    taichiFlyDraw(r.s, fc.s, Rect{0, 0, 400, 260}, pal);
    for (int y = 0; y < 260; ++y)
      for (int x = 0; x < 400; ++x) {
        if (fp.px[(size_t)y * 400 + x] == fc.px[(size_t)y * 400 + x]) continue;
        bool in = false;
        for (int j = 0; j < nd; ++j) in = in || (x >= rr[j].x0 && x < rr[j].x1 && y >= rr[j].y0 && y < rr[j].y1);
        unmarked += in ? 0 : 1;
      }
    // Sitting, none of it lies on his head: a pale speck there reads as an eye (review, 2026-11-18).
    if (r.s.fly_mode == 4) {
      float rad = 0.f;
      const Vec2 hc = hikerHeadPos(r.h, &rad);
      for (int y = 0; y < 260; ++y)
        for (int x = 0; x < 400; ++x)
          if (fc.px[(size_t)y * 400 + x] != 0 && hypotf((float)x + 0.5f - hc.x, (float)y + 0.5f - hc.y) < rad - 0.5f) ++on_head;
    }
    prev = r.s;
  }
  printf("  shown %d ticks, sat on his back %d ticks, marked %d ticks, gone %d, unmarked pixels %d, pixels on his head %d\n", shown,
         sat, marks, r.s.fly_mode == 6, unmarked, on_head);
  CHECK(shown > 1500 && sat > 50 && outside == 0 && r.s.fly_mode == 6, "butterfly: shown %d, sat %d, outside %d, mode %d", shown, sat,
        outside, r.s.fly_mode);
  CHECK(unmarked == 0, "butterfly: %d changed pixels outside the marked rows", unmarked);
  CHECK(on_head == 0, "butterfly: %d of its pixels on his head while it sits", on_head);
}

// ---- numbers ----
static void numbers() {
  printf("numbers\n");
  printf("  sizeof(TaichiState) %d, sizeof(TaiKey) %d\n", (int)sizeof(TaichiState), (int)sizeof(TaiKey));
  Run r;
  start(r, 2, TG_NONE);
  const auto t0 = std::chrono::steady_clock::now();
  int n = 0;
  while (!taichiDone(r.s)) {
    step(r);
    ++n;
  }
  const auto t1 = std::chrono::steady_clock::now();
  printf("  a tick (module step, rig tick, pose) %.2f us on this PC over %d ticks\n",
         (double)std::chrono::duration<double, std::micro>(t1 - t0).count() / n, n);
}

int main() {
  void* scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(scratch);
  Palette pal;
  WeatherTint tint;
  memset(&tint, 0, sizeof tint);
  computePalette(2.0f, tint, pal);
  testTables();
  testPlanted();
  testContinuity();
  testTiming();
  testShapes();
  testFrozenAndDeterminism();
  testDraw(pal);
  testTurn(pal);
  testFly(pal);
  numbers();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  free(scratch);
  return g_fail ? 1 : 0;
}
