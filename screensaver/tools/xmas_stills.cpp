// Stills for Mike's stage 18 review: the Christmas set as MOCKUPS (scene/xmas.h), to pick from. Nothing
// here is scheduled; each still runs the scene to a calm moment, then poses the mock state, the hiker
// and the light by hand.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -I screensaver/core screensaver/tools/xmas_stills.cpp BUILD/libss_core.a
//     -o BUILD/xmas_stills
//   BUILD/xmas_stills OUT_DIR [NAME_PART]   writes OUT_DIR/NN_name.ppm (800 x 480) and OUT_DIR/stills.txt
//
// stills.txt has one line per still: name, the close-up's box (x0 y0 x1 y1), its scale, the label;
// tools/xmas_sheet.py turns them into PNGs, close-ups and the contact sheet.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <vector>

#include "ss_app.h"
#include "scene/spine.h"
#include "scene/world.h"
#include "scene/xmas.h"

#if !SS_XMAS
#error "build with -DSS_XMAS=1 (the Christmas set is off by default: scene/xmas_switch.h)"
#endif

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

enum Look { DAY, DUSK, NIGHT, MORNING, SNOW_NIGHT };

Inputs inputsFor(Look look) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  switch (look) {
    case DAY: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
    case MORNING: in.mood = 1.9f; in.spine_force = 1 + SF_MORNING; break;
    case DUSK: in.mood = 4.85f; in.spine_force = 1 + SF_EVENING; break;
    case NIGHT:
    case SNOW_NIGHT:
      in.mood = 0.f;
      in.spine_force = 1 + SF_EVENING;
      in.night_mode = true;
      in.weather.night_dim = 1.f;
      in.weather.night_gain = 1.6f;
      if (look == SNOW_NIGHT) in.weather_code = 2;
      break;
  }
  return in;
}

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

typedef std::function<bool(const App*, const World&)> Pred;
typedef std::function<void(World&)> Pose;
// The close-up's box, from the World (the tree and the wheel move with the seed's layout).
typedef std::function<void(const World&, int box[4])> Crop;

struct Still {
  const char* name;
  const char* label;
  Look look;
  uint64_t seed0;
  Pred want;
  Pose pose;
  Crop crop;
  int scale;
  int after;   // ticks run after the pose before the frame
};

void hush(World& w) {
  w.stream.next_float = w.tick + 25u * 600u;
  w.stream.next_fish = w.tick + 25u * 600u;
  for (Floater& f : w.stream.f) f.kind = FK_NONE;
  w.stream.fish.kind = FISH_NONE;
}

// He stands (or sits) where the still wants him, pack off, hat on, hands empty.
void placeHiker(World& w, Vec2 feet, int facing, PoseId p) {
  HikerFx& fx = w.fx;
  fx.on = true;
  fx.path_on = false;
  fx.lean = fx.head = fx.lift = fx.reach = fx.armload = fx.stomp = 0.f;
  for (int i = 0; i < 2; ++i) fx.arm_w[i] = fx.arm_sh[i] = fx.arm_el[i] = 0.f;
  fx.wade = false;
  hikerInit(w.hiker, feet, facing);
  w.hiker.pack_on = false;
  hikerSetPose(w.hiker, p, 0.04f);
}
void arm(World& w, int i, float sh, float el) {
  w.fx.arm_w[i] = 1.f;
  w.fx.arm_sh[i] = sh;
  w.fx.arm_el[i] = el;
}
void hideHiker(World& w) {
  w.fx.on = false;
  w.hiker.visible = false;
}

bool shoot(const Still& s, const char* out, FILE* log) {
  for (uint64_t seed = s.seed0; seed < s.seed0 + 300; ++seed) {
    Inputs in = inputsFor(s.look);
    void* mem = calloc(1, SS_APP_BYTES);
    std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
    Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    App* app = appStart(mem, kAlloc, seed, in);
    bool hit = false;
    for (int tick = 0; tick < 25 * 900 && !hit; ++tick) {
      if (appTick(app)) break;
      if (tick % 8 == 0) appRender(app, surf);
      if (tick > 400 && s.want(app, *appWorld(app))) {
        World& w = const_cast<World&>(*appWorld(app));
        hush(w);
        s.pose(w);
        for (int k = 0; k < s.after; ++k) {
          appTick(app);
          appRender(app, surf);
        }
        appInvalidate(app);
        appRender(app, surf);
        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", out, s.name);
        writePPM(path, fb.data());
        int b[4];
        s.crop(*appWorld(app), b);
        fprintf(log, "%s %d %d %d %d %d %s\n", s.name, b[0], b[1], b[2], b[3], s.scale, s.label);
        printf("  %-26s seed %llu tick %d  tree x %d\n", s.name, (unsigned long long)seed, tick, (int)appWorld(app)->land.camp_x);
        hit = true;
      }
    }
    appStop(app, kAlloc);
    free(mem);
    if (hit) return true;
  }
  printf("  %-26s NOT FOUND\n", s.name);
  return false;
}

bool beatIs(const App* a, const char* b) { return strcmp(appBeatName(a), b) == 0; }
bool calm(const App* a, const World& w) {
  return (beatIs(a, "pause") || beatIs(a, "nap")) && !w.hiker.walking && appCritterCount(a) == 0 && !w.busy &&
         w.tent.state == TENT_UP;
}

// ---- the mock states -------------------------------------------------------------------------------

XmasMock genOnly(uint8_t wheel) {
  XmasMock m{};
  m.gen = 1;
  m.wheel = wheel;
  return m;
}
XmasMock lights(uint8_t wraps, uint8_t look, uint8_t power) {
  XmasMock m = genOnly(9);
  m.wraps = wraps;
  m.look = look;
  m.power = power;
  return m;
}
XmasMock finished(uint8_t look) {
  XmasMock m = lights(XMAS_WRAPS, look, XP_ON);
  m.garland = XG_DRAPED;
  m.orns = 0x7F;
  m.extras = XE_STAR;
  return m;
}

// Crop boxes.
void cropAround(int b[4], float cx, float cy, float hw, float hh) {
  b[0] = (int)(cx - hw);
  b[1] = (int)(cy - hh);
  b[2] = (int)(cx + hw);
  b[3] = (int)(cy + hh);
  if (b[0] < 0) { b[2] -= b[0]; b[0] = 0; }
  if (b[2] > SS_WIDTH) { b[0] -= b[2] - SS_WIDTH; b[2] = SS_WIDTH; }
  if (b[1] < 0) { b[3] -= b[1]; b[1] = 0; }
  if (b[3] > SS_HEIGHT) { b[1] -= b[3] - SS_HEIGHT; b[3] = SS_HEIGHT; }
}
const Crop kWheelCrop = [](const World& w, int b[4]) {
  const Vec2 h = xmasWheelHub(w.land);
  cropAround(b, h.x - 14.f, h.y, 50.f, 32.f);
};
const Crop kTreeCrop = [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x + 6.f, 262.f, 100.f, 100.f); };
const Crop kTreeLowCrop = [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x - 10.f, 320.f, 80.f, 48.f); };

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: xmas_stills OUT_DIR [NAME_PART]\n");
    return 2;
  }
  const char* out = argv[1];
  if (ssXmasSwitch() != SS_XMAS) {   // the World's layout depends on it (scene/xmas_switch.h)
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  char lp[512];
  snprintf(lp, sizeof lp, "%s/stills.txt", out);
  FILE* log = fopen(lp, "w");
  if (!log) return 2;
  const Pred calmAny = [](const App* a, const World& w) { return calm(a, w); };
  const Pred fireLit = [](const App* a, const World& w) { return calm(a, w) && w.fire.state == FIRE_LIT && w.fire.intensity > 0.9f; };
  const Still stills[] = {
      {"01_generator_day", "The water wheel generator by the boulder (day)", DAY, 1, calmAny,
       [](World& w) { w.xmas.m = genOnly(0); }, kWheelCrop, 4, 3},
      {"02_wheel_turned", "The wheel a half paddle on (motion check)", DAY, 1, calmAny,
       [](World& w) { w.xmas.m = genOnly(3); }, kWheelCrop, 4, 3},
      {"03_thump_sparks_night", "Night: he thumps the box, it sparks, the lights dip", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_DIM);
         w.xmas.m.spark = 4;
         // He crouches behind the box on the bank, his fist come down on its lid.
         const Vec2 h = xmasWheelHub(w.land);
         const float x = h.x - 40.f;
         placeHiker(w, Vec2{x, xmasBankY(w.land, h.x - 28.f) - 11.f}, 1, POSE_CROUCH);
         arm(w, 0, 0.62f, 0.08f);
         w.fx.lean = 0.12f;
       },
       kWheelCrop, 3, 3},
      {"04_lights_one_spiral_dusk", "Dusk: on the ladder, one spiral done", DUSK, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(1, XMAS_LOOK, XP_ON);
         w.xmas.m.extras = XE_LADDER | XE_COIL;
         placeHiker(w, xmasLadderRung(w.land, 4), -1, POSE_REACH_UP);
         arm(w, 0, 2.3f, 0.5f);
         arm(w, 1, 2.0f, 0.6f);
       },
       kTreeCrop, 2, 3},
      {"05_lights_half_dusk", "Dusk: half the spirals up", DUSK, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(3, XMAS_LOOK, XP_ON);
         w.xmas.m.extras = XE_LADDER | XE_COIL;
         placeHiker(w, xmasLadderRung(w.land, 2), -1, POSE_REACH_UP);
         arm(w, 0, 1.9f, 0.4f);
         arm(w, 1, 1.6f, 0.7f);
       },
       kTreeCrop, 2, 3},
      {"06_half_string_dead", "Night: half the string dead", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_HALF_DEAD);
         placeHiker(w, Vec2{(float)w.land.camp_x - 70.f, (float)w.land.camp.base_y + 22.f}, 1, POSE_HANDS_ON_HIPS);
       },
       kTreeCrop, 2, 3},
      {"08_full_night", "Night: all the spirals (multi-colour, Mike's pick)", NIGHT, 1, fireLit,
       [](World& w) { w.xmas.m = lights(XMAS_WRAPS, XL_MULTI, XP_ON); }, kTreeCrop, 2, 3},
      {"09_full_day", "Day: all the spirals", DAY, 1, calmAny,
       [](World& w) { w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON); }, kTreeCrop, 2, 3},
      {"10_garland_flying", "Two cardinals fly the garland in", DAY, 1, calmAny,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_FLYING;
         w.xmas.m.fly = 200;
       },
       [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x + 70.f, 200.f, 110.f, 70.f); }, 2, 3},
      {"11_garland_tangled", "One cardinal tangled in it", DAY, 1, calmAny,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_TANGLED;
       },
       kTreeCrop, 2, 3},
      {"12_garland_part", "Part draped, the end pulled round", DAY, 1, calmAny,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_PART;
       },
       kTreeCrop, 2, 3},
      {"13_garland_draped", "The garland draped in swags", DAY, 1, calmAny,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_DRAPED;
       },
       kTreeCrop, 2, 3},
      {"14_ornaments_day", "Ornaments on (red and gold)", DAY, 1, calmAny,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_DRAPED;
         w.xmas.m.orns = 0x7F;
       },
       kTreeCrop, 2, 3},
      {"15_raccoon_reach_night", "Night: the raccoon reaches for the low one", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_DRAPED;
         w.xmas.m.orns = 0x7F;
         w.xmas.m.coon = XR_REACH;
         hideHiker(w);
       },
       kTreeLowCrop, 3, 3},
      {"16_raccoon_carry_night", "Night: off with it", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_DRAPED;
         w.xmas.m.orns = 0x3F;
         w.xmas.m.extras = XE_GAP;
         w.xmas.m.coon = XR_CARRY;
         hideHiker(w);
       },
       [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x - 70.f, 335.f, 70.f, 42.f); }, 3, 3},
      {"17_puzzled_morning", "Next morning: he finds the gap", MORNING, 1, calmAny,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_DRAPED;
         w.xmas.m.orns = 0x3F;
         w.xmas.m.extras = XE_GAP;
         const Vec2 o = xmasOrnament(w.land, XMAS_LOW_ORN);
         placeHiker(w, Vec2{o.x - 26.f, (float)w.land.camp.base_y + 14.f}, 1, POSE_SCRATCH_HEAD);
         w.fx.head = -0.35f;
       },
       [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x - 40.f, 300.f, 84.f, 60.f); }, 3, 3},
      {"18_raccoon_bush_dusk", "Dusk: the raccoon's own bush by the stream", DUSK, 1, fireLit,
       [](World& w) {
         w.xmas.m = lights(XMAS_WRAPS, XMAS_LOOK, XP_ON);
         w.xmas.m.garland = XG_DRAPED;
         w.xmas.m.orns = 0x3F;
         w.xmas.m.extras = XE_GAP;
         w.xmas.m.bush = 3;
         w.xmas.m.coon = XR_PROUD;
       },
       [](const World& w, int b[4]) {
         const Vec2 p = xmasBushAt(w.land);
         cropAround(b, p.x + 10.f, p.y - 14.f, 52.f, 30.f);
       },
       4, 3},
      {"19_hero_eve_night", "Christmas Eve night: star, lights, garland, ornaments, north star", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.north = 1;
       },
       kTreeCrop, 2, 3},
      {"20_snow_day", "Snow by day", DAY, 1, calmAny,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.extras |= XE_SNOW;
       },
       [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x - 40.f, 330.f, 130.f, 80.f); }, 2, 3},
      {"21_snow_dusk", "Snow at dusk", DUSK, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.extras |= XE_SNOW;
       },
       [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x - 40.f, 330.f, 130.f, 80.f); }, 2, 3},
      {"22_hero_eve_snow", "Christmas Eve night in snow, flakes falling", SNOW_NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.extras |= XE_SNOW;   // no north star: snow falls from a cloud deck
       },
       kTreeCrop, 2, 25 * 4},
      {"23_presents_morning", "Christmas morning: presents (the bones are Lucy's and Copper's)", MORNING, 1, calmAny,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.extras = XE_STAR | XE_PRESENTS | XE_STOCKING;
       },
       [](const World& w, int b[4]) { cropAround(b, (float)w.land.camp_x, 340.f, 64.f, 30.f); }, 4, 3},
      {"24_stocking_tent", "A stocking on the tent's front pole", DUSK, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.extras = XE_STAR | XE_STOCKING;
       },
       [](const World& w, int b[4]) { cropAround(b, w.tent.pos.x + 8.f, w.tent.pos.y - 38.f, 44.f, 30.f); }, 4, 3},
      {"25_sleigh_moon", "Christmas Eve: the sleigh across the moon", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.extras = XE_STAR | XE_SLEIGH;
       },
       [](const World&, int b[4]) { cropAround(b, 640.f, 108.f, 48.f, 28.f); }, 4, 3},
      {"26_north_star_bright", "The north star, twinkle bright", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.north = 1;
       },
       [](const World&, int b[4]) { cropAround(b, xmasNorthStar().x, xmasNorthStar().y, 36.f, 22.f); }, 5, 3},
      {"27_north_star_dim", "The north star, twinkle dim", NIGHT, 1, fireLit,
       [](World& w) {
         w.xmas.m = finished(XMAS_LOOK);
         w.xmas.m.north = 3;
       },
       [](const World&, int b[4]) { cropAround(b, xmasNorthStar().x, xmasNorthStar().y, 36.f, 22.f); }, 5, 3},
  };
  int missing = 0;
  for (const Still& s : stills)   // a second argument: only the stills whose name contains it
    if ((argc < 3 || strstr(s.name, argv[2])) && !shoot(s, out, log)) ++missing;
  fclose(log);
  return missing ? 1 : 0;
}
