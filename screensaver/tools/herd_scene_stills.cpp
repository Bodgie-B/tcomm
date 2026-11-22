// Stills for Mike's review of stage 22 step C4, the deer visit as a midday beat (DEER_PLAN.md section 10,
// stills 2 to 10, plus INTEGRATION_PLAN 4.1 the doe at the cattail and 4.2 the fawn in the reeds). Unlike
// herd_stills.cpp (step B3, the deer rig posed by hand) these are real midday loops: the app runs the seed
// and every frame is the scene's own, taken at a moment of the beat. Nothing is drawn by the tool.
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (SS_S22 on):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I core tools/herd_scene_stills.cpp
//     BUILD/libss_core.a -o BUILD/herd_scene_stills
//   BUILD/herd_scene_stills OUT_DIR      writes OUT_DIR/*.ppm and OUT_DIR/manifest.txt
//   python -I tools/herd_scene_sheet.py OUT_DIR SHEET_DIR
//
// manifest.txt: one line a still, "sheet|scale|label|file|x0|y0|x1|y1" (the crop of the frame to show).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gfx/raster.h"
#include "scene/day.h"
#include "scene/day_herd.h"
#include "scene/herd.h"
#include "scene/reeds.h"
#include "scene/spine_family.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

FILE* g_man = nullptr;
const char* g_out = nullptr;
int g_n = 0;

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

Inputs midday(int month) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = true;
  in.weather_code = 0;
  in.month = (uint8_t)month;
  return in;
}

// What a still shows: 0 the camp's band (x 0..680, y 270..450) at 1x; 1 the whole frame at 1x; 2 a 2x crop
// round a deer (`role`); 3 a 2x crop round the tent's door; 4 a 2x crop round him; 5 the left edge at 2x.
struct Shot {
  const char* sheet;
  int phase;          // 0 the herd beat (at `t` seconds of it), 1 the return (a condition, `t` its number)
  float t;
  int kind, role;
  const char* label;
  bool taken;
};

void save(const Pixel* fb, const World& w, const Shot& s) {
  char name[64], path[512];
  snprintf(name, sizeof name, "s%03d.ppm", g_n++);
  snprintf(path, sizeof path, "%s/%s", g_out, name);
  writePPM(path, fb);
  int x0 = 0, y0 = 270, x1 = 680, y1 = 450, k = 1;
  if (s.kind == 1) {
    y0 = 0;
    x1 = 800;
    y1 = 480;
  } else if (s.kind == 2 && w.day.kind == DAY_HERD) {
    const int i = herdIndexOf(w.day.herd.h, (uint8_t)s.role);
    const int sl = i >= 0 ? w.day.herd.h.d[i].slot : -1;
    const Vec2 f = sl >= 0 ? w.critters[sl].feet : Vec2{400.f, 370.f};
    x0 = (int)f.x - 80;
    x1 = x0 + 160;
    y0 = (int)f.y - 70;
    y1 = y0 + 96;
    k = 2;
  } else if (s.kind == 3) {
    x0 = (int)w.tent.pos.x - 50;
    x1 = x0 + 190;
    y0 = (int)w.tent.pos.y - 62;
    y1 = y0 + 96;
    k = 2;
  } else if (s.kind == 4) {
    x0 = (int)w.hiker.feet.x - 100;
    x1 = x0 + 190;
    y0 = (int)w.hiker.feet.y - 104;
    y1 = y0 + 116;
    k = 2;
  } else if (s.kind == 5) {
    x0 = 0;
    x1 = 200;
    y0 = 300;
    y1 = 400;
    k = 2;
  }
  if (x0 < 0) { x1 -= x0; x0 = 0; }
  if (x1 > SS_WIDTH) { x0 -= x1 - SS_WIDTH; x1 = SS_WIDTH; }
  if (y0 < 0) { y1 -= y0; y0 = 0; }
  if (y1 > SS_HEIGHT) { y0 -= y1 - SS_HEIGHT; y1 = SS_HEIGHT; }
  fprintf(g_man, "%s|%d|%s|%s|%d|%d|%d|%d\n", s.sheet, k, s.label, name, x0, y0, x1, y1);
}

// A seed whose first midday loop is this herd byte and return variant (-1 any), with (1) or regardless of
// (-1) both reed meetings.
uint64_t findSeed(int herd, int ret, int reeds, int month) {
  for (uint64_t seed = 1; seed < 6000; ++seed) {
    void* mem = malloc(SS_APP_BYTES);
    App* a = appStart(mem, kAlloc, seed, midday(month));
    const World* w = appWorld(a);
    bool hit = w->plan.herd == herd && (ret < 0 || w->plan.fvar[0] == ret);
    if (hit && reeds > 0) {
      for (int t = 0; t < 330; ++t) appTick(a);
      hit = w->day.kind == DAY_HERD && w->day.herd.h.nibble_on && w->day.herd.h.fawn_reed.x > 0.f;
    }
    appStop(a, kAlloc);
    free(mem);
    if (hit) return seed;
  }
  return 0;
}

// Runs loop 0 of `seed` through the herd beat and the return, taking the shots. `bell_s`, `dog_s`: a
// doorbell or Lucy let out at that second of the beat (< 0: none).
void run(uint64_t seed, int month, Shot* shots, int n, float bell_s, float dog_s) {
  Inputs in = midday(month);
  void* mem = malloc(SS_APP_BYTES);
  Pixel* fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  Surface surf{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* a = appStart(mem, kAlloc, seed, in);
  const World* w = appWorld(a);
  char plan[400];
  appLoopPlan(a, plan, sizeof plan);
  printf("seed %d: %s\n", (int)seed, plan);
  int phase = -1, bt = 0, look_t = -1, zip_t = -1, scratch_t = -1, rt = 0, still_n = 0;
  const uint32_t len = appLoopTicks(a);
  for (uint32_t t = 0; t < len; ++t) {
    const bool herd = strcmp(appBeatName(a), "herd") == 0;
    if (phase < 0 && herd) phase = 0;
    else if (phase == 0 && !herd) phase = 1;
    else if (phase == 1 && strcmp(appBeatName(a), "return") != 0) break;
    if (phase == 0 && bell_s >= 0.f && bt == (int)(bell_s * 25.f)) appPostEvent(a, Event{EV_DOORBELL, 0});
    if (phase == 0 && dog_s >= 0.f && bt == (int)(dog_s * 25.f)) {
      in.dog_out = 1;
      appSetInputs(a, in);
    }
    appTick(a);
    appRender(a, surf);
    if (phase == 0) {
      for (int i = 0; i < n; ++i) {
        Shot& s = shots[i];
        if (s.taken || s.phase != 0) continue;
        bool now = bt == (int)(s.t * 25.f + 0.5f);
        if (s.t >= 1000.f && w->day.kind == DAY_HERD) {   // the cattail: 1000 + seconds since she reached; 2000 + seconds since the bite
          const HerdDrive& v = w->day.herd.v;
          static int bite_tick = -1;
          if (v.nib == 2 && bite_tick < 0) bite_tick = bt;
          if (v.nib == 0) bite_tick = -1;
          now = s.t < 2000.f ? (v.nib == 1 && v.nib_t >= s.t - 1000.f) : (bite_tick >= 0 && bt - bite_tick >= (int)((s.t - 2000.f) * 25.f));
        }
        if (s.t >= 3000.f && s.t < 5000.f && w->day.kind == DAY_HERD)   // 3000 + seconds after the alarm
          now = w->day.herd.h.alarm != 0 && w->day.herd.h.t >= w->day.herd.h.alarm_t + (s.t - 3000.f);
        if (s.t >= 5000.f && w->day.kind == DAY_HERD) {   // the fawn has stopped to look back
          const int fi = herdIndexOf(w->day.herd.h, HR_FAWN);
          const int sl = fi >= 0 ? w->day.herd.h.d[fi].slot : -1;
          now = sl >= 0 && w->day.herd.h.d[fi].mode == HM_OUT && w->critters[sl].action == CA_HEADTURN && !w->critters[sl].blending;
        }
        if (now) {
          save(fb, *w, s);
          s.taken = true;
        }
      }
      ++bt;
    } else if (phase == 1) {
      // (the ford's look is a plain stand since the C4 review: the first stop of 10 ticks while the signs are unseen)
      still_n = w->hiker.visible && !appHikerWalking(a) ? still_n + 1 : 0;
      if (look_t < 0 && still_n == 10 && herdSigns(*w)) look_t = rt - 9;
      if (zip_t < 0 && w->day.kind == DAY_HERD && w->tent.door > 0.f && w->tent.door < w->day.herd.v.door * 0.6f) zip_t = rt;
      if (scratch_t < 0 && w->hiker.target == POSE_SCRATCH_HEAD) scratch_t = rt;
      for (int i = 0; i < n; ++i) {
        Shot& s = shots[i];
        if (s.taken || s.phase != 1) continue;
        const int c = (int)s.t;
        const bool now = (c == 0 && look_t >= 0 && rt == look_t + 30) || (c == 1 && look_t >= 0 && rt == look_t + 78) ||
                         (c == 2 && zip_t >= 0 && rt == zip_t) || (c == 3 && scratch_t >= 0 && rt == scratch_t + 26) ||
                         (c == 4 && zip_t >= 0 && rt > zip_t + 25 && w->fx.head < -0.3f) || (c == 5 && rt == 4 * 25);
        if (now) {
          save(fb, *w, s);
          s.taken = true;
        }
      }
      ++rt;
    }
  }
  for (int i = 0; i < n; ++i)
    if (!shots[i].taken) printf("  not taken: %s / %s\n", shots[i].sheet, shots[i].label);
  appStop(a, kAlloc);
  free(mem);
  free(fb);
}

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: herd_scene_stills OUT_DIR\n");
    return 2;
  }
  g_out = argv[1];
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  char mp[512];
  snprintf(mp, sizeof mp, "%s/manifest.txt", g_out);
  g_man = fopen(mp, "w");
  if (!g_man) return 1;

  // Variant A, five deer, both reed meetings, the return that zips the flap.
  {
    static Shot s[] = {
        {"story_A", 0, 3.f, 0, 0, "A 3 S THE EMPTY CAMP", false},
        {"story_A", 0, 12.f, 0, 0, "A 12 S THE DOE LOOKS IN", false},
        {"story_A", 0, 19.f, 0, 0, "A 19 S NOSE DOWN, THE FAWN BEHIND HER", false},
        {"story_A", 0, 31.f, 0, 0, "A 31 S THE YEARLINGS PASS EACH OTHER", false},
        {"story_A", 0, 1001.5f, 0, 0, "A THE SECOND DOE AT THE CATTAIL", false},
        {"story_A", 0, 51.f, 0, 0, "A 51 S TWO NOSES AT THE TENT", false},
        {"story_A", 0, 52.4f, 0, 0, "A 52.4 S THE FLINCH, HELD", false},
        {"story_A", 0, 59.6f, 0, 0, "A 59.6 S THE TUG; THE FAWN DRINKS", false},
        {"story_A", 0, 62.7f, 0, 0, "A 62.7 S THE FAWN BOUNDS IN", false},
        {"story_A", 0, 67.2f, 0, 0, "A 67.2 S NOSE IN THE REEDS", false},
        {"story_A", 0, 71.8f, 0, 0, "A 71.8 S NOSES TOUCH", false},
        {"story_A", 0, 75.5f, 0, 0, "A 75.5 S THE SETTLED PICTURE", false},
        {"story_A", 0, 3001.8f, 0, 0, "A ALARM + 1.8 S HER HEAD AND TAIL UP", false},
        {"story_A", 0, 3005.f, 0, 0, "A ALARM + 5 S FIVE HEADS UP, STILL", false},
        {"story_A", 0, 3007.2f, 0, 0, "A ALARM + 7.2 S THE DOE GOES", false},
        {"story_A", 0, 3010.f, 0, 0, "A ALARM + 10 S THE LINE GOING LEFT", false},
        {"story_A", 0, 3013.f, 0, 0, "A ALARM + 13 S", false},
        {"story_A", 0, 3016.5f, 0, 0, "A ALARM + 16.5 S THE FAWN LAST", false},
        {"story_A", 0, 106.f, 0, 0, "A 106 S EMPTY CAMP, FLAP AJAR, PRINTS", false},
        {"story_A", 1, 0.f, 0, 0, "RETURN: HE STOPS AT THE FORD AND LOOKS", false},
        {"whole", 0, 75.5f, 1, 0, "A 75.5 S THE SETTLED PICTURE, WHOLE FRAME", false},
        {"whole", 0, 3005.f, 1, 0, "A ALARM + 5 S, WHOLE FRAME", false},
        {"whole", 0, 3010.f, 1, 0, "A ALARM + 10 S THE WAY OUT, WHOLE FRAME", false},
        {"whole", 1, 3.f, 1, 0, "RETURN: AT THE DOOR, THE FLAP SHUT, WHOLE FRAME", false},
        {"edge_2x", 0, 9.6f, 5, 0, "9.6 S SHE COMES OUT PAST THE PINES", false},
        {"edge_2x", 0, 12.f, 5, 0, "12 S HEAD UP, HOLDS", false},
        {"edge_2x", 0, 13.9f, 5, 0, "13.9 S LOOKS BACK", false},
        {"edge_2x", 0, 19.f, 5, 0, "19 S NOSE DOWN; THE FAWN", false},
        {"edge_2x", 0, 25.f, 5, 0, "25 S THE SECOND DOE PAUSES", false},
        {"edge_2x", 0, 31.f, 5, 0, "31 S YEARLINGS PASS", false},
        {"edge_2x", 0, 32.6f, 5, 0, "32.6 S", false},
        {"edge_2x", 0, 5000.f, 5, 0, "THE FAWN LOOKS BACK", false},
        {"tent_2x", 0, 48.5f, 3, 0, "48.5 S THE STRETCH", false},
        {"tent_2x", 0, 51.f, 3, 0, "51 S TWO NOSES", false},
        {"tent_2x", 0, 51.72f, 3, 0, "51.72 S THE FABRIC MOVES", false},
        {"tent_2x", 0, 52.2f, 3, 0, "52.2 S FLINCHED, HELD", false},
        {"tent_2x", 0, 56.6f, 3, 0, "56.6 S SHE TOUCHES THE WALL", false},
        {"tent_2x", 0, 59.25f, 3, 0, "59.25 S THE FIRST TUG", false},
        {"tent_2x", 0, 59.95f, 3, 0, "59.95 S THE SECOND", false},
        {"tent_2x", 0, 63.f, 3, 0, "63 S THE FLAP AJAR", false},
        {"fawn_2x", 0, 59.6f, 2, HR_FAWN, "59.6 S DRINKING", false},
        {"fawn_2x", 0, 61.1f, 2, HR_FAWN, "61.1 S PAWING", false},
        {"fawn_2x", 0, 62.72f, 2, HR_FAWN, "62.72 S MID DASH, DROPS", false},
        {"fawn_2x", 0, 64.4f, 2, HR_FAWN, "64.4 S BRACED", false},
        {"fawn_2x", 0, 65.f, 2, HR_FAWN, "65 S THE SHAKE", false},
        {"fawn_2x", 0, 66.5f, 2, HR_FAWN, "66.5 S DASH AT THE REEDS", false},
        {"fawn_2x", 0, 67.f, 2, HR_FAWN, "67 S NOSE IN, THEY LEAN", false},
        {"fawn_2x", 0, 67.7f, 2, HR_FAWN, "67.7 S HEAD UP AMONG THEM", false},
        {"fawn_2x", 0, 70.4f, 2, HR_FAWN, "70.4 S OUT, WET PRINTS", false},
        {"fawn_2x", 0, 71.8f, 2, HR_FAWN, "71.8 S NOSES TOUCH", false},
        {"cattail_2x", 0, 1000.3f, 2, HR_GRAZER, "THE NECK GOES OUT", false},
        {"cattail_2x", 0, 1001.5f, 2, HR_GRAZER, "LIPS ON IT, IT BENDS TO HER", false},
        {"cattail_2x", 0, 1002.65f, 2, HR_GRAZER, "THE HARD TUG", false},
        {"cattail_2x", 0, 2000.1f, 2, HR_GRAZER, "THE HEAD IS HERS, THE STUB RINGS", false},
        {"cattail_2x", 0, 2001.5f, 2, HR_GRAZER, "CHEWING, HEAD LEVEL", false},
        {"alarm_2x", 0, 3000.6f, 2, HR_DOE, "ALARM + 0.6 S HEAD UP, ALONE", false},
        {"alarm_2x", 0, 3001.8f, 2, HR_DOE, "+ 1.8 S THE FLAG", false},
        {"alarm_2x", 0, 3002.3f, 2, HR_DOE, "+ 2.3 S THE HOOF UP", false},
        {"alarm_2x", 0, 3003.4f, 2, HR_DOE, "+ 3.4 S STAMP, DUST", false},
        {"alarm_2x", 0, 3005.f, 2, HR_GRAZER, "+ 5 S THE SECOND DOE LOOKS BACK", false},
        {"alarm_2x", 0, 3005.f, 2, HR_FAWN, "+ 5 S THE FAWN, FLAG UP", false},
        {"return_2x", 1, 0.f, 4, 0, "AT THE FORD, LOOKING OVER THE CAMP", false},
        {"return_2x", 1, 1.f, 4, 0, "HEAD DOWN TO THE GROUND", false},
        {"return_2x", 1, 2.f, 4, 0, "AT THE DOOR, ZIPPING", false},
        {"return_2x", 1, 4.f, 4, 0, "LOOKING DOWN AT THE PRINTS", false},
        {"return_2x", 1, 3.f, 4, 0, "SCRATCHES HIS HEAD", false},
    };
    const uint64_t seed = findSeed(1, 0, 1, 8);
    if (seed) run(seed, 8, s, N(s), -1.f, -1.f);
  }
  // The settled picture for four and six, and variants B and C.
  {
    static Shot s4[] = {{"herds", 0, 75.5f, 0, 0, "FOUR DEER, 75.5 S", false}};
    static Shot s6[] = {{"herds", 0, 75.5f, 0, 0, "SIX DEER, 75.5 S", false}, {"herds", 0, 3005.f, 0, 0, "SIX DEER, ALARM + 5 S", false}, {"herds", 0, 3011.5f, 0, 0, "SIX DEER, THE WAY OUT", false}};
    static Shot sb[] = {{"herds", 0, 50.6f, 0, 0, "B 50.6 S THE TUG", false},
                        {"herds", 0, 51.2f, 0, 0, "B 51.2 S THE ZIP GIVES, IT LEAPS BACK", false},
                        {"herds", 0, 53.5f, 0, 0, "B 53.5 S FIVE DEER STARE AT A TENT", false},
                        {"herds", 0, 75.5f, 0, 0, "B 75.5 S THE SETTLED PICTURE, THE FLAP OPEN", false}};
    static Shot sc[] = {{"herds", 0, 44.f, 0, 0, "C 44 S THE SPLASH COMES FIRST", false},
                        {"herds", 0, 57.5f, 0, 0, "C 57.5 S THREE NOSES", false},
                        {"herds", 0, 58.2f, 0, 0, "C 58.2 S THREE FLINCH", false},
                        {"herds", 0, 75.5f, 0, 0, "C 75.5 S THE SETTLED PICTURE", false}};
    uint64_t seed = findSeed(9, -1, -1, 8);
    if (seed) run(seed, 8, s4, N(s4), -1.f, -1.f);
    seed = findSeed(5, -1, -1, 8);
    if (seed) run(seed, 8, s6, N(s6), -1.f, -1.f);
    seed = findSeed(2, -1, -1, 8);
    if (seed) run(seed, 8, sb, N(sb), -1.f, -1.f);
    seed = findSeed(3, -1, -1, 8);
    if (seed) run(seed, 8, sc, N(sc), -1.f, -1.f);
  }
  // Real alarms: Lucy let out at 40 s (they go right), the doorbell at 50 s (left, fast).
  {
    static Shot sd[] = {{"alarms", 0, 40.6f, 0, 0, "DOG AT 40 S: 0.6 S AFTER", false},
                        {"alarms", 0, 42.2f, 0, 0, "DOG: THEY GO RIGHT", false},
                        {"alarms", 0, 44.f, 0, 0, "DOG: 4 S AFTER", false},
                        {"alarms", 0, 46.5f, 0, 0, "DOG: LUCY COMES IN", false},
                        {"alarms", 0, 49.f, 0, 0, "DOG: THE CHASE", false}};
    static Shot sb[] = {{"alarms", 0, 50.7f, 0, 0, "BELL AT 50 S: 0.7 S AFTER", false}, {"alarms", 0, 52.5f, 0, 0, "BELL: THEY GO LEFT, FAST", false}};
    const uint64_t seed = findSeed(1, 0, -1, 8);
    if (seed) {
      run(seed, 8, sd, N(sd), -1.f, 40.f);
      run(seed, 8, sb, N(sb), 50.f, -1.f);
    }
  }
  fclose(g_man);
  printf("%d stills in %s\n", g_n, g_out);
  return 0;
}
