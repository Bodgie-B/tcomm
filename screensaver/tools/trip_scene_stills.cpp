// Stills for Mike's stage 22 review: the mushroom major in the real scene (day_trip.cpp; MUSHROOM_PLAN
// section 12 items 5 to 8, step C5c). The patch and the sky alone are trip_stills.cpp (items 1 to 4).
//   st    the story, one frame per numbered step of plan 5.1 to 5.3 and 5.7, mother on the left
//   str   the same crossing and picking with the mother on the right
//   v0    set 0's two visions at their key moments (the big chipmunk, the deer with the tree)
//   v1    set 1's (the slow fish, the smoke rings)
//   sky   whole frames through the sky with him lying under it
//   dog   Lucy beside him; Copper beside him
//   bell  the doorbell mid-sky: the bell, +1.7 s, +3.4 s
//   pk    the pocket ending
//   c0, c1  a contact sheet per set: a half-size frame every 8 s of the beat
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (SS_S22 on):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I core tools/trip_scene_stills.cpp BUILD/libss_core.a
//     -o BUILD/trip_scene_stills
//   BUILD/trip_scene_stills OUT_DIR    writes OUT_DIR/*.ppm and OUT_DIR/manifest.txt
//
// trip_scene_sheet.py turns them into PNG sheets. Manifest lines: group|file|label|x0|y0|x1|y1|scale.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "gfx/raster.h"
#include "mush_patch.h"
#include "palette.h"
#include "scene/day.h"
#include "scene/day_trip.h"
#include "scene/majors.h"
#include "scene/trip.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

// day_trip.cpp's vision stages and cue bits (file-local there; keep in step).
enum { V_WAIT = 0, VC_IN, VC_LOOK, VC_NEAR, VC_SNIFF, VC_HOLD, VC_HELLO, VC_OFF, VC_GONE, V_GAP, VD_IN, VD_LOOK,
       VD_GROW, VD_BIRD, VD_STILL, VD_SHED, VD_BACK, VD_OUT, V_DONE, VW_FISH, VW_GAP, VW_RINGS };
const uint16_t kOnset = 1, kSkyOver = 256;

struct Rig {
  void* mem = nullptr;
  std::vector<Pixel> fb;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = mem ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
  }
};

Inputs midday(int dog) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = true;
  in.weather_code = 0;   // clear, from the data
  in.dog_out = dog;
  in.month = 6;
  in.carry = mushPack(MUSH_RIPE, 0, true);
  return in;
}

int tripIndex(const LoopPlan& p) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == MJ_MUSHROOM) return i;
  return -1;
}

// side: -1 the mother at x <= 520, +1 at x >= 575, 0 any.
uint64_t findSeed(int set, int pocket, int dog, int side) {
  for (uint64_t seed = 1; seed <= 600; ++seed) {
    Rig r;
    if (!r.start(seed, midday(dog))) return 0;
    const World* w = appWorld(r.app);
    const int i = tripIndex(w->plan);
    const uint8_t v = i >= 0 ? w->plan.major_var[i] : 0;
    bool hit = i >= 0 && (v & 3) == set && ((v & 4) != 0) == (pocket != 0);
    if (side < 0) hit = hit && w->land.mush_x <= 520;
    if (side > 0) hit = hit && w->land.mush_x >= 575;
    r.stop();
    if (hit) return seed;
  }
  return 0;
}

struct Ctx {
  const World* w;
  const TripState* s;
  float bt;        // seconds into the beat
  float bell;      // seconds since the bell (-1 none)
  bool last;       // the beat's last tick
};
struct Shot {
  const char* group;
  const char* label;
  std::function<bool(const Ctx&)> when;
  int x0, y0, x1, y1;   // the crop in frame px (x relative to rx when rel)
  int scale;
  bool rel;             // x0, x1 are offsets from the patch crouch (M1) or his lie spot
  bool done = false;
};

FILE* g_manifest = nullptr;
int g_n = 0;
const char* g_dir = ".";

void writeTile(const Rig& r, const char* group, const char* label, int x0, int y0, int x1, int y1, int scale, bool half) {
  if (x0 < 0) { x1 -= x0; x0 = 0; }
  if (x1 > SS_WIDTH) { x0 -= x1 - SS_WIDTH; x1 = SS_WIDTH; }
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  char name[64], path[600];
  snprintf(name, sizeof name, "ts_%03d.ppm", g_n++);
  snprintf(path, sizeof path, "%s/%s", g_dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  const int st = half ? 2 : 1;
  const int w = (x1 - x0) / st, h = (y1 - y0) / st;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row((size_t)w * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int rr = 0, gg = 0, bb = 0;
      for (int dy = 0; dy < st; ++dy)
        for (int dx = 0; dx < st; ++dx) {
          const Pixel p = r.fb[(size_t)(y0 + y * st + dy) * SS_WIDTH + (size_t)(x0 + x * st + dx)];
          rr += red8(p);
          gg += green8(p);
          bb += blue8(p);
        }
      row[(size_t)x * 3] = (unsigned char)(rr / (st * st));
      row[(size_t)x * 3 + 1] = (unsigned char)(gg / (st * st));
      row[(size_t)x * 3 + 2] = (unsigned char)(bb / (st * st));
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  fclose(f);
  fprintf(g_manifest, "%s|%s|%s|0|0|%d|%d|%d\n", group, name, label, w, h, scale);
}

struct Cfg {
  int set, pocket, dog, side;
  float bell_T;             // a doorbell at this many seconds into the sky (< 0 none)
  const char* contact;      // a contact sheet group, or null
};

void run(const Cfg& c, std::vector<Shot> shots) {
  const uint64_t seed = findSeed(c.set, c.pocket, c.dog, c.side);
  if (!seed) {
    printf("no seed for set %d pocket %d dog %d side %d\n", c.set, c.pocket, c.dog, c.side);
    return;
  }
  Rig r;
  if (!r.start(seed, midday(c.dog))) return;
  const World* w = appWorld(r.app);
  const uint32_t len = appLoopTicks(r.app);
  bool in_beat = false, belled = false;
  int n_in = 0, bell_ticks = -1, next_contact = 0;
  printf("set %d pocket %d dog %d side %d: seed %d, mother at x %d\n", c.set, c.pocket, c.dog, c.side, (int)seed, (int)w->land.mush_x);
  float sky_begin = -1.f, sky_end = -1.f, script_end = -1.f;
  for (uint32_t t = 0; t + 1 < len; ++t) {
    const TripState& s = w->day.trip.t;
    if (in_beat && !belled && c.bell_T >= 0.f && s.sky_on && s.T >= c.bell_T) {
      Event ev;
      ev.type = EV_DOORBELL;
      ev.arg = 0;
      appPostEvent(r.app, ev);
      belled = true;
      bell_ticks = 0;
    }
    appTick(r.app);
    if (bell_ticks >= 0) ++bell_ticks;
    const bool now = strcmp(appBeatName(r.app), "mushroom") == 0;
    if (!now) {
      if (in_beat) break;
      continue;
    }
    in_beat = true;
    ++n_in;
    Ctx cx{w, &s, (float)n_in / 25.f, bell_ticks >= 0 ? (float)bell_ticks / 25.f : -1.f, false};
    if (s.sky_on && sky_begin < 0.f) sky_begin = cx.bt;
    if ((w->major.cue & kSkyOver) && sky_end < 0.f) sky_end = cx.bt;
    if (scriptDone(*w) && script_end < 0.f) script_end = cx.bt;
    if (getenv("TRIP_TRACE") && n_in % 100 == 0)
      printf("    bt %.0f vis %d vt %.1f sky %.1f slow %d T %.0f dog %d/%d c0 %d c1 %d feet %.0f,%.0f pose %d\n", (double)cx.bt, s.vis, (double)s.vt,
             (double)s.sky_t, s.slow, (double)s.T, s.dog, s.dog_stage, w->major.c[0], w->major.c[1], (double)w->hiker.feet.x,
             (double)w->hiker.feet.y, (int)w->hiker.target);
    const int ax = w->day.kind == DAY_TRIP && (s.sky_on || (w->major.cue & kOnset)) ? (int)w->spine.camp.lie.x : (int)w->major.p[1].x;
    // Rendered every tick, as the panel does: the sky's rolling repaint goes through the renderer, and a
    // band waits when repaints pile up unrendered (REVIEW_FIXES F28), so a run that only drew now and then
    // would show a sky the panel never does. The stills are the incremental frames themselves.
    appRender(r.app, r.surf);
    auto draw = [&]() {};
    for (Shot& sh : shots) {
      if (sh.done || !sh.when(cx)) continue;
      sh.done = true;
      draw();
      const int ox = sh.rel ? ax : 0;
      writeTile(r, sh.group, sh.label, ox + sh.x0, sh.y0, ox + sh.x1, sh.y1, sh.scale, false);
    }
    if (c.contact && n_in >= next_contact) {
      next_contact += 8 * 25;
      draw();
      char lab[64];
      snprintf(lab, sizeof lab, "%.0f s  sky %.0f", (double)cx.bt, (double)(s.sky_on ? s.sky_t : 0.f));
      writeTile(r, c.contact, lab, 0, 0, SS_WIDTH, SS_HEIGHT, 1, true);
    }
  }
  printf("  beat %.1f s; the sky from %.1f to %.1f s (%.1f s); the script done at %.1f s\n", (double)n_in / 25., (double)sky_begin,
         (double)sky_end, (double)(sky_end - sky_begin), (double)script_end);
  for (const Shot& sh : shots)
    if (!sh.done) printf("  missed: %s %s (seed %d)\n", sh.group, sh.label, (int)seed);
  r.stop();
}

bool settled(const World& w, PoseId p) { return w.hiker.target == p && !w.hiker.blending && !w.hiker.walking; }
bool over(const Ctx& c) { return (c.w->major.cue & kSkyOver) != 0; }
bool onset(const Ctx& c) { return (c.w->major.cue & kOnset) != 0; }
int caps(const TripState& s, int mode) {
  int n = 0;
  for (int i = 0; i < TRIP_PROPS; ++i)
    if (s.prop[i].kind == TP_CAP && (mode < 0 || s.prop[i].mode == mode)) ++n;
  return n;
}

std::vector<Shot> story(const char* g) {
  // The crop: from the camp bank where he steps in to past the patch, the water between.
  const int X0 = -150, X1 = 110, Y0 = 290, Y1 = 480;
  const int LX0 = -130, LX1 = 130, LY0 = 250, LY1 = 440;   // round where he lies
  return {
      {g, "1 he stands", [](const Ctx& c) { return c.bt >= 1.0f; }, 190, Y0 - 40, 450, Y1 - 40, 2, false},
      {g, "2 he shades his eyes: something", [](const Ctx& c) { return c.w->fx.arm_w[0] > 0.97f && c.w->fx.head > 0.08f && c.bt < 8.f; }, 190, Y0 - 40, 450, Y1 - 40, 2, false},
      {g, "3 a look at the water", [](const Ctx& c) { return c.w->fx.head > 0.22f && !c.s->lifted && !c.w->fx.wade && c.bt > 5.f; }, X0, Y0, X1, Y1, 2, true},
      {g, "4 wading over", [](const Ctx& c) { return c.w->fx.wade && !c.s->lifted && fabsf(c.w->hiker.feet.x - c.w->major.p[2].x) < 6.f; }, X0, Y0, X1, Y1, 2, true},
      {g, "4 the stones are slick", [](const Ctx& c) { return c.w->fx.wade && !c.s->lifted && fabsf(c.w->fx.lean) > 0.06f; }, X0, Y0, X1, Y1, 2, true},
      {g, "5 he peers; a finger; it rocks", [](const Ctx& c) { return fabsf(c.s->rock) > 0.004f || c.w->fx.reach > 0.9f; }, X0, Y0, X1, Y1, 2, true},
      {g, "6 he picks the first", [](const Ctx& c) { return c.s->lifted && c.s->picked == 0 && c.s->pick_u > 0.45f; }, X0, Y0, X1, Y1, 2, true},
      {g, "6 the third", [](const Ctx& c) { return c.s->lifted && c.s->picked == 2 && c.s->pick_u > 0.58f; }, X0, Y0, X1, Y1, 2, true},
      {g, "7 the bunch in his far hand", [](const Ctx& c) { return c.s->picked > 0 && c.s->picked == c.s->n_pick && settled(*c.w, POSE_STAND) && !c.s->in_near; }, X0, Y0, X1, Y1, 2, true},
      {g, "8 he sniffs one", [](const Ctx& c) { return c.s->in_near && c.s->bite == 0 && c.w->fx.head < -0.06f; }, X0, Y0, X1, Y1, 2, true},
      {g, "8 and looks at it", [](const Ctx& c) { return c.s->in_near && c.s->bite == 0 && c.w->fx.head > 0.055f; }, X0, Y0, X1, Y1, 2, true},
      {g, "9 a look round. Nobody", [](const Ctx& c) { return c.s->in_near && c.s->bite == 0 && c.w->hiker.facing < 0 && c.s->picked > 0 && !c.w->fx.wade; }, X0, Y0, X1, Y1, 2, true},
      {g, "10 he eats it", [](const Ctx& c) { return c.s->in_near && c.s->bite == 2; }, X0, Y0, X1, Y1, 2, true},
      {g, "11 he waits. Nothing happens", [](const Ctx& c) { return c.s->picked > 0 && !c.s->in_near && c.s->bite >= 3 && settled(*c.w, POSE_HANDS_ON_HIPS) && !onset(c); }, X0, Y0, X1, Y1, 2, true},
      {g, "11 a shrug", [](const Ctx& c) { return c.s->bite >= 3 && c.w->fx.lift > 0.011f && !onset(c); }, X0, Y0, X1, Y1, 2, true},
      {g, "12 wading back", [](const Ctx& c) { return c.w->fx.wade && c.s->bite >= 3 && fabsf(c.w->hiker.feet.x - c.w->major.p[2].x) < 6.f; }, X0, Y0, X1, Y1, 2, true},
      {g, "13 it comes on: the grass leans", [](const Ctx& c) { return onset(c) && c.s->onset_t > 3.0f && c.w->hiker.walking; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "14 his hand", [](const Ctx& c) { return onset(c) && !c.s->sky_on && !over(c) && c.w->fx.arm_w[0] > 0.97f; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "15 the two in the grass; he sits", [](const Ctx& c) { return c.s->bunch_down && !c.s->sky_on && !over(c) && settled(*c.w, POSE_SIT); }, LX0, LY0, LX1, LY1, 2, true},
      {g, "15 he lies down", [](const Ctx& c) { return c.s->sky_on && c.s->T >= 2.f; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "16 he stirs", [](const Ctx& c) { return over(c) && c.w->fx.arm_w[0] > 0.9f && c.s->bunch_down && c.w->hiker.target == POSE_LIE; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "16 he rubs his eyes", [](const Ctx& c) { return over(c) && c.w->fx.arm_w[1] > 0.97f && c.s->bunch_down; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "17 the two beside him", [](const Ctx& c) { return over(c) && c.s->bunch_down && c.w->fx.head > 0.19f && c.w->fx.arm_w[1] < 0.01f; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "17 the plain sky", [](const Ctx& c) { return over(c) && c.s->bunch_down && c.w->fx.head < -0.2f; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "18 he has them", [](const Ctx& c) { return over(c) && !c.s->bunch_down && settled(*c.w, POSE_STAND) && c.s->in_far > 0; }, LX0, LY0, LX1, LY1, 2, true},
      {g, "20 an easy underhand lob", [](const Ctx& c) { return caps(*c.s, 0) > 0; }, LX0 + 20, LY0 + 20, LX1 + 20, LY1 + 20, 2, true},
      {g, "20 it rides the current away", [](const Ctx& c) { return caps(*c.s, 1) >= 2; }, LX0 - 40, LY0 + 20, LX1 - 40, LY1 + 20, 2, true},
      {g, "21 he watches them go", [](const Ctx& c) { return caps(*c.s, 1) >= 1 && settled(*c.w, POSE_HANDS_ON_HIPS); }, LX0 - 40, LY0 + 20, LX1 - 40, LY1 + 20, 2, true},
      {g, "22 back by the fire", [](const Ctx& c) { return over(c) && settled(*c.w, POSE_SIT) && !c.s->bunch_down && caps(*c.s, -1) == 0 && c.s->in_far == 0; }, 150, 250, 470, 440, 2, false},
  };
}

std::vector<Shot> skyShots() {
  auto at = [](float t) { return [t](const Ctx& c) { return c.s->sky_on && c.s->sky_t >= t; }; };
  return {
      {"sky", "sky 6 s: the edges begin to wave", at(6.f), 0, 0, 800, 480, 1, false},
      {"sky", "sky 16 s: colour coming", at(16.f), 0, 0, 800, 480, 1, false},
      {"sky", "sky 31 s: all drifting, stars out", at(31.f), 0, 0, 800, 480, 1, false},
      {"sky", "sky 91 s: his arm traces it", at(91.5f), 0, 0, 800, 480, 1, false},
      {"st", "the arm that traces the sky", at(92.5f), -130, 250, 130, 440, 2, true},
      {"sky", "sky 99 s: letting go", at(99.f), 0, 0, 800, 480, 1, false},
      {"sky", "sky 103 s: natural again", at(103.f), 0, 0, 800, 480, 1, false},
  };
}

std::vector<Shot> vis0() {
  const int X0 = -170, X1 = 190, Y0 = 220, Y1 = 400;
  auto st = [](int v, float t) { return [v, t](const Ctx& c) { return c.s->vis == v && c.s->vt >= t; }; };
  return {
      {"v0", "chipmunk: in, at its own size", st(VC_IN, 2.0f), X0 - 60, Y0, X1 - 60, Y1, 2, true},
      {"v0", "a look", st(VC_LOOK, 1.0f), X0, Y0, X1, Y1, 2, true},
      {"v0", "nearer, and bigger", st(VC_NEAR, 1.5f), X0, Y0, X1, Y1, 2, true},
      {"v0", "it sits up and sniffs", st(VC_SNIFF, 2.4f), X0, Y0, X1, Y1, 2, true},
      {"v0", "the hold", st(VC_HOLD, 2.0f), X0, Y0, X1, Y1, 2, true},
      {"v0", "a look; his hello", st(VC_HELLO, 1.6f), X0, Y0, X1, Y1, 2, true},
      {"v0", "it walks off, shrinking", st(VC_OFF, 1.6f), X0 - 30, Y0, X1 - 30, Y1, 2, true},
      {"v0", "deer: in along the back", st(VD_IN, 5.0f), X0 + 110, Y0, X1 + 110, Y1, 2, true},
      {"v0", "side on; a look", st(VD_LOOK, 1.0f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "the tree grows", st(VD_GROW, 5.0f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "leaves at the tips", st(VD_GROW, 10.6f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "a bird comes down", st(VD_BIRD, 0.9f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "the stillness", st(VD_STILL, 3.4f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "the bird leaves; leaves let go", st(VD_SHED, 1.3f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "the tree draws back in", st(VD_BACK, 2.0f), X0 + 60, Y0, X1 + 60, Y1, 2, true},
      {"v0", "it walks out", st(VD_OUT, 2.5f), X0 + 110, Y0, X1 + 110, Y1, 2, true},
  };
}

std::vector<Shot> vis1() {
  auto st = [](int v, float t) { return [v, t](const Ctx& c) { return c.s->vis == v && c.s->vt >= t; }; };
  return {
      {"v1", "the first fish leaves the water", st(VW_FISH, 0.9f), 240, 280, 600, 460, 2, false},
      {"v1", "at the top of its arc", st(VW_FISH, 1.8f), 240, 280, 600, 460, 2, false},
      {"v1", "the second, another colour", st(VW_FISH, 7.9f), 240, 280, 600, 460, 2, false},
      {"v1", "the third goes back in: one ring", st(VW_FISH, 15.3f), 240, 280, 600, 460, 2, false},
      {"v1", "a smoke ring from the cold pit", st(VW_RINGS, 3.0f), 120, 280, 480, 460, 2, false},
      {"v1", "three, widening as they climb", st(VW_RINGS, 9.4f), 120, 280, 480, 460, 2, false},
      {"v1", "the small one passes through", st(VW_RINGS, 12.6f), 120, 280, 480, 460, 2, false},
      {"v1", "the last thins away", st(VW_RINGS, 23.0f), 120, 280, 480, 460, 2, false},
  };
}

}  // namespace

int main(int argc, char** argv) {
  g_dir = argc > 1 ? argv[1] : ".";
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  char path[600];
  snprintf(path, sizeof path, "%s/manifest.txt", g_dir);
  g_manifest = fopen(path, "w");
  if (!g_manifest) {
    printf("cannot write %s\n", path);
    return 1;
  }
  const int LX0 = -170, LX1 = 190, LY0 = 220, LY1 = 400;
  {   // set 0, the mother on the left: the story, the visions, the sky, the contact sheet
    std::vector<Shot> a = story("st"), b = vis0(), c = skyShots();
    a.insert(a.end(), b.begin(), b.end());
    a.insert(a.end(), c.begin(), c.end());
    run(Cfg{0, 0, 0, -1, -1.f, "c0"}, a);
  }
  {   // the mother on the right: the crossing and the picking again
    std::vector<Shot> a = story("str");
    a.resize(12);
    run(Cfg{0, 0, 0, 1, -1.f, nullptr}, a);
  }
  run(Cfg{1, 0, 0, 0, -1.f, "c1"}, vis1());
  auto dog = [](int st, float t) { return [st, t](const Ctx& c) { return c.s->dog_stage == st && c.s->dog_t >= t; }; };
  run(Cfg{0, 0, 1, 0, -1.f, nullptr},
      {{"dog", "Lucy comes over", dog(1, 1.5f), LX0, LY0, LX1, LY1, 2, true},
       {"dog", "Lucy lies by his legs", dog(2, 2.5f), LX0, LY0, LX1, LY1, 2, true},
       {"dog", "her chin down", dog(3, 6.0f), LX0, LY0, LX1, LY1, 2, true},
       {"dog", "she looks up at his arm", [](const Ctx& c) { return c.s->dog_stage == 3 && c.s->sky_t >= 90.f; }, LX0, LY0, LX1, LY1, 2, true}});
  run(Cfg{0, 0, 2, 0, -1.f, nullptr},
      {{"dog", "Copper alone: he lies by his legs", dog(3, 6.0f), LX0, LY0, LX1, LY1, 2, true}});
  auto bell = [](float t) { return [t](const Ctx& c) { return c.bell >= t; }; };
  run(Cfg{0, 0, 0, 0, 26.f, nullptr},
      {{"bell", "the doorbell mid-sky (0.4 s)", bell(0.4f), 0, 0, 800, 480, 1, false},
       {"bell", "+1.7 s", bell(1.7f), 0, 0, 800, 480, 1, false},
       {"bell", "+3.4 s: the sky goes on", bell(3.6f), 0, 0, 800, 480, 1, false}});
  run(Cfg{0, 1, 0, 0, -1.f, nullptr},
      {{"pk", "he holds one up", [](const Ctx& c) { return over(c) && c.s->pocket == 2 && c.w->fx.arm_w[0] > 0.95f; }, LX0 + 20, LY0 + 40, LX1 + 20, LY1 + 40, 2, true},
       {"pk", "a look left", [](const Ctx& c) { return over(c) && c.s->pocket == 2 && c.w->hiker.facing != (c.w->major.count ? 1 : -1); }, LX0 + 20, LY0 + 40, LX1 + 20, LY1 + 40, 2, true},
       {"pk", "into his pocket, a pat", [](const Ctx& c) { return over(c) && c.s->pocket == 3 && c.w->fx.arm_w[0] > 0.9f; }, LX0 + 20, LY0 + 40, LX1 + 20, LY1 + 40, 2, true},
       {"pk", "one toss, not two", [](const Ctx& c) { return over(c) && c.s->pocket == 3 && caps(*c.s, 1) >= 1; }, LX0 - 20, LY0 + 40, LX1 - 20, LY1 + 40, 2, true}});
  fclose(g_manifest);
  printf("%d tiles\n", g_n);
  return 0;
}
