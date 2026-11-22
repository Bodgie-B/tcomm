// Stills for Mike's stage 21 review: the plinking (day_plink.h). One run of the scene per variant (A the clean run,
// B the can that will not fall, C the food bag), rendering every tick as the panel does, with a full frame taken the
// first time each moment comes up: setting the cans, the rifle in two halves, putting it together, walking to the
// corner, loading, aiming, the flash, a can in the air, a can spinning flat, cans on the ground, the gag can
// teetering (A), the chips and the sight check and the shrug and the poke (B), the bag falling and the spill and
// the rehang (C), the unload look, the clean-up. Also a mirrored layout (a seed with the camp pine at the other end
// of its range) and one full frame mid-shot.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD (the same -DSS_XMAS as the
// library):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -I screensaver/core screensaver/tools/plink_stills.cpp BUILD/libss_core.a
//     -o BUILD/plink_stills
//   BUILD/plink_stills OUT_DIR    writes OUT_DIR/<variant>_NN_name.ppm (800 x 480) and OUT_DIR/manifest.txt
//
// plink_sheet.py (Pillow) turns the PPMs and the manifest (file|label|x0|y0|x1|y1|scale, the crop and its
// enlargement) into the sheets.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "ss_app.h"
#include "scene/landscape.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

bool writePPM(const char* path, const Pixel* px) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT);
  std::vector<unsigned char> row((size_t)SS_WIDTH * 3);
  for (int y = 0; y < SS_HEIGHT; ++y) {
    for (int x = 0; x < SS_WIDTH; ++x) {
      const Pixel p = px[(size_t)y * SS_WIDTH + x];
      row[(size_t)x * 3] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  return fclose(f) == 0;
}

Inputs inputsDay() {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  return in;
}

bool wet(const World& w) { return w.weather.kind == WX_RAIN || w.weather.kind == WX_SNOW || w.weather.kind == WX_STORM; }

int indexOf(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
}

// A seed whose first loop plays plink variant `var`; `want_cx_low`: -1 any, 0 the camp pine at the left of its range, 1 right.
uint64_t findSeed(int var, int cx_side) {
  for (uint64_t seed = 1; seed <= 2400; ++seed) {
    void* mem = malloc(SS_APP_BYTES);
    App* app = appStart(mem, kAlloc, seed, inputsDay());
    const World* w = appWorld(app);
    const int i = indexOf(w->plan, MJ_PLINK);
    const bool ok = i >= 0 && (w->plan.major_var[i] & 3) == var && !wet(*w) &&
                    (cx_side < 0 || (cx_side == 0 && w->land.camp_x <= 553) || (cx_side == 1 && w->land.camp_x >= 567));
    appStop(app, kAlloc);
    free(mem);
    if (ok) return seed;
  }
  return 0;
}

enum Crop { CROP_FULL, CROP_BRANCH, CROP_SHOOTER, CROP_HIKER, CROP_BAG, CROP_GROUND, CROP_RUN };

struct Ctx {
  int var;
  float t;               // seconds into the major
  const World* w;
  const PlinkState* p;   // null when the piece is not up
  int hold_seen;
  int prev_down;
};

struct Moment {
  const char* name;
  const char* label;
  Crop crop;
  bool (*pred)(const Ctx&);
  bool var_a, var_b, var_c;
  bool done;
};

int countMode(const PlinkState& p, int mode) {
  int n = 0;
  for (const PlinkCan& c : p.can) n += c.mode == mode ? 1 : 0;
  return n;
}

// clang-format off
Moment kMoments[] = {
  {"stick_lifting_can", "a can on the stick's fork, lifted to the branch", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[2].mode == CAN_TIP && c.p->stick == PI_HAND; }, true, true, true, false},
  {"stick_set_down", "set down on the bark", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[3].mode == CAN_ON && c.p->can[3].t < 0.2f && c.p->can[3].wob > 0.f; }, true, true, true, false},
  {"all_cans_up", "six cans up, full length of the branch", CROP_BRANCH,
   [](const Ctx& c) { return c.p && countMode(*c.p, CAN_ON) == 6 && c.p->rifle == 0; }, true, true, true, false},
  {"halves_at_pack", "the two halves out of the pack", CROP_HIKER,
   [](const Ctx& c) { return c.p && c.p->rifle && c.p->join < 0.05f && c.p->stick == PI_DOWN && !c.p->first_done; }, true, true, true, false},
  {"assembling", "joining the halves", CROP_HIKER,
   [](const Ctx& c) { return c.p && c.p->rifle && c.p->join > 0.45f && c.p->join < 0.6f; }, true, true, true, false},
  {"walk_low_carry", "walking with the rifle, muzzle down", CROP_RUN,
   [](const Ctx& c) { return c.p && c.p->rifle && c.p->join >= 1.f && c.w->hiker.walking && c.w->hiker.feet.x < 330.f && c.w->hiker.feet.y > 380.f; }, true, true, true, false},
  {"kneel_load", "kneeling, the magazine in", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->mag == MAG_HAND && c.p->kneel > 0.95f; }, true, true, true, false},
  {"aim_first", "the first aim", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->phase == PP_HOLD && c.p->t > 1.0f && !c.p->first_done; }, true, true, true, false},
  {"aim_full", "the first aim, full frame", CROP_FULL,
   [](const Ctx& c) { return c.p && c.p->phase == PP_HOLD && c.p->t > 1.2f && !c.p->first_done; }, true, true, true, false},
  {"flash", "the shot", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->flash > 0.04f; }, true, true, true, false},
  {"can_pop", "the first can pops", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[5].mode == CAN_FLY && c.p->can[5].t > 0.12f; }, true, true, true, false},
  {"can_spin", "a can spinning", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[4].mode == CAN_FLY && c.p->can[4].t > 0.3f; }, true, true, true, false},
  {"can_flat_spin", "the squat tin spinning flat", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[2].mode == CAN_FLY && c.p->can[2].t > 0.25f; }, true, true, true, false},
  {"gag_teeter", "the third can hops back, teeters", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[3].mode == CAN_ON && c.p->can[3].aux == 2 && c.p->can[3].t > 0.3f; }, true, false, false, false},
  {"gag_full", "the teetering can, he lifts his cheek to look", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->can[3].mode == CAN_ON && c.p->can[3].aux == 2 && c.p->can[3].t > 0.3f; }, true, false, false, false},
  {"chips", "bark chips under the last can", CROP_BRANCH,
   [](const Ctx& c) { if (!c.p) return false; for (const PlinkBit& b : c.p->bit) if (b.kind == PB_CHIP && b.t > 0.1f) return true; return false; }, false, true, false, false},
  {"stubborn_rocks", "it rocks, still up", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.p->can[0].mode == CAN_ON && c.p->can[0].wob > 0.15f && c.p->can[0].t > 0.1f; }, false, true, false, false},
  {"sights_check", "checking the sights", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->phase == PP_GESTURE && c.p->gest == SK_CHECK && c.p->gw > 0.95f; }, false, true, false, false},
  {"shrug", "a shrug", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->phase == PP_GESTURE && c.p->gest == SK_SHRUG && c.p->gw > 0.95f; }, false, true, false, false},
  {"poke", "poking it off with the stick", CROP_BRANCH,
   [](const Ctx& c) { return c.p && c.var == 1 && c.p->can[0].mode == CAN_FLY; }, false, true, false, false},
  {"bag_falls", "the line is cut: the bag falls", CROP_BAG,
   [](const Ctx& c) { return c.p && c.p->bag_vy > 150.f; }, false, false, true, false},
  {"bag_falls_full", "the line is cut (full frame)", CROP_FULL,
   [](const Ctx& c) { return c.p && c.p->bag_vy > 150.f; }, false, false, true, false},
  {"food_spill", "the food spills", CROP_BAG,
   [](const Ctx& c) { if (!c.p) return false; for (const PlinkBit& b : c.p->bit) if (b.kind >= FOOD_LOAF && !b.rest && b.t > 0.08f) return true; return false; }, false, false, true, false},
  {"food_down", "the food on the ground", CROP_BAG,
   [](const Ctx& c) { return c.p && c.p->first_done && c.p->bag_vy == 0.f && c.p->rope_cut && c.p->bit[0].rest && c.p->bit[1].rest && c.p->bit[0].kind >= FOOD_LOAF; }, false, false, true, false},
  {"scratch", "a scratch of the head", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->phase == PP_GESTURE && c.p->gest == SK_SCRATCH && c.p->gw > 0.95f; }, false, false, true, false},
  {"cans_on_ground", "cans on the ground", CROP_GROUND,
   [](const Ctx& c) { return c.p && countMode(*c.p, CAN_DOWN) >= 4 && c.p->phase == PP_FOLLOW; }, true, true, true, false},
  {"unload_look", "unloaded, looking into the action", CROP_SHOOTER,
   [](const Ctx& c) { return c.p && c.p->kneel_on && !c.p->run && c.p->mag == MAG_NONE && c.p->rifle && c.w->fx.head > 0.25f; }, true, true, true, false},
  {"stow", "the rifle into the pack", CROP_HIKER,
   [](const Ctx& c) { return c.p && c.p->rifle && c.p->join < 0.6f && c.p->first_done && !c.p->kneel_on; }, true, true, true, false},
  {"gather_food", "gathering the food", CROP_GROUND,
   [](const Ctx& c) { return c.p && c.var == 2 && !c.p->rifle && plinkFoodLeft(*c.p) > 0 && c.w->hiker.feet.x > c.w->land.camp_x - 100 && c.w->hiker.feet.y > 354.f && c.w->hiker.pose.lean > 0.3f; }, false, false, true, false},
  {"rehang", "tossing the bag back over the branch", CROP_BAG,
   [](const Ctx& c) { return c.p && c.w->bag.state == BAG_FLYING; }, false, false, true, false},
  {"sweep_cans", "picking up the cans", CROP_GROUND,
   [](const Ctx& c) { return c.p && c.p->first_done && !c.p->rifle && c.p->sack == PI_HAND && c.p->sack_n >= 2 && c.p->sack_n < 6 && c.w->hiker.feet.x > c.w->land.camp_x - 60; }, true, true, true, false},
  {"stick_toss", "the stick onto the pile", CROP_HIKER,
   [](const Ctx& c) { return c.p && c.p->stick == PI_HAND && !c.p->rifle && c.p->first_done && c.p->sack_n >= 5 && c.w->hiker.feet.x < c.w->spine.camp.wood_work.x + 40.f && c.t > 70.f; }, true, true, true, false},
};
// clang-format on

void cropRect(Crop c, const World& w, int& x0, int& y0, int& x1, int& y1, int& scale) {
  const int cx = w.land.camp_x;
  switch (c) {
    case CROP_BRANCH: x0 = cx - 140; y0 = 170; x1 = cx + 90; y1 = 370; scale = 3; break;
    case CROP_SHOOTER: x0 = 100; y0 = 330; x1 = 330; y1 = 480; scale = 3; break;
    case CROP_HIKER: x0 = (int)w.hiker.feet.x - 90; y0 = (int)w.hiker.feet.y - 120; x1 = x0 + 200; y1 = y0 + 150; scale = 3; break;
    case CROP_RUN: x0 = 60; y0 = 330; x1 = 360; y1 = 480; scale = 2; break;
    case CROP_BAG: x0 = cx - 170; y0 = 190; x1 = cx + 30; y1 = 370; scale = 3; break;
    case CROP_GROUND: x0 = cx - 130; y0 = 280; x1 = cx + 90; y1 = 380; scale = 3; break;
    default: x0 = 0; y0 = 0; x1 = SS_WIDTH; y1 = SS_HEIGHT; scale = 1; break;
  }
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SS_WIDTH) x1 = SS_WIDTH;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
}

void runVariant(int var, uint64_t seed, const char* out, FILE* manifest, bool moments, const char* tag) {
  void* mem = calloc(1, SS_APP_BYTES);
  std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
  Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* app = appStart(mem, kAlloc, seed, inputsDay());
  const World& w = *appWorld(app);
  for (Moment& m : kMoments) m.done = false;
  bool seen = false;
  int n = 0, start_tick = 0;
  uint32_t done_at = 0, beat_ticks = 0;
  Ctx cx{var, 0.f, &w, nullptr, 0, 0};
  for (int tick = 0; tick < 40000; ++tick) {
    appTick(app);
    appRender(app, surf);
    const bool on = w.major.on && w.major.id == MJ_PLINK;
    if (on && !seen) {
      seen = true;
      start_tick = tick;
    }
    if (seen && !on) break;
    if (!on) continue;
    done_at = w.spine.run.done_at;
    beat_ticks = w.spine.run.beat_ticks;
    cx.t = (float)(tick - start_tick) * SS_TICK_DT;
    cx.p = w.day.kind == DAY_PLINK ? &w.day.plink.p : nullptr;
    if (!moments) {
      if (tick - start_tick == 1200 && cx.p) {   // mirrored layout: one full frame with all the cans up
        appInvalidate(app);
        appRender(app, surf);
        char path[600];
        snprintf(path, sizeof path, "%s/%s_cans_up_full.ppm", out, tag);
        writePPM(path, fb.data());
        fprintf(manifest, "%s_cans_up_full.ppm|%s: cans up (camp_x %d)|%d|170|%d|370|3\n", tag, tag, (int)w.land.camp_x, (int)w.land.camp_x - 140, (int)w.land.camp_x + 90);
        return appStop(app, kAlloc), free(mem);
      }
      continue;
    }
    for (Moment& m : kMoments) {
      if (m.done || !((var == 0 && m.var_a) || (var == 1 && m.var_b) || (var == 2 && m.var_c))) continue;
      if (!m.pred(cx)) continue;
      m.done = true;
      appInvalidate(app);
      appRender(app, surf);
      char name[600], path[700];
      snprintf(name, sizeof name, "%s_%02d_%s.ppm", tag, n++, m.name);
      snprintf(path, sizeof path, "%s/%s", out, name);
      writePPM(path, fb.data());
      int x0, y0, x1, y1, sc;
      cropRect(m.crop, w, x0, y0, x1, y1, sc);
      fprintf(manifest, "%s|%s (%.1f s)|%d|%d|%d|%d|%d\n", name, m.label, (double)cx.t, x0, y0, x1, y1, sc);
      printf("  %-10s %-18s at %6.1f s\n", tag, m.name, (double)cx.t);
    }
  }
  const World& wend = *appWorld(app);
  printf("%s seed %llu: major ran %.1f s, script done at %.1f s of a %.1f s beat; overruns %d\n", tag, (unsigned long long)seed,
         (double)(beat_ticks) * (double)SS_TICK_DT, (double)done_at * (double)SS_TICK_DT, (double)beat_ticks * (double)SS_TICK_DT,
         (int)wend.spine.overruns);
  for (const Moment& m : kMoments)
    if (!m.done && ((var == 0 && m.var_a) || (var == 1 && m.var_b) || (var == 2 && m.var_c))) printf("  MISSED %s\n", m.name);
  appStop(app, kAlloc);
  free(mem);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: plink_stills OUT_DIR\n");
    return 2;
  }
  const char* out = argv[1];
  if (ssXmasSwitch() != SS_XMAS) {
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  char mp[600];
  snprintf(mp, sizeof mp, "%s/manifest.txt", out);
  FILE* manifest = fopen(mp, "wb");
  if (!manifest) return 2;
  static const char* const kTag[3] = {"A", "B", "C"};
  for (int var = 0; var < 3; ++var) {
    const uint64_t seed = findSeed(var, -1);
    if (!seed) {
      fprintf(stderr, "no seed for variant %d\n", var);
      continue;
    }
    runVariant(var, seed, out, manifest, true, kTag[var]);
  }
  // Mirrored layouts: the camp pine at the left and the right of its range.
  for (int side = 0; side < 2; ++side) {
    const uint64_t seed = findSeed(0, side);
    if (!seed) continue;
    runVariant(0, seed, out, manifest, false, side == 0 ? "L" : "R");
  }
  fclose(manifest);
  return 0;
}
