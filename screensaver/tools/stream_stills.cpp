// Single stills for Mike's stage 17 review: the stream's boulder, things drifting by, fish, the rain, the
// visiting heron and deer (brought on for sure: StreamVisit::force), the leaves by month, and the duck
// and dragonfly mockups (posed: StreamLife::mock, not scheduled).
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -I screensaver/core screensaver/tools/stream_stills.cpp BUILD/libss_core.a
//     (-DSS_XMAS=1 as the host build has it, CMakeLists.txt; with -DSS_XMAS=OFF builds, 0)
//     -o BUILD/stream_stills
//   BUILD/stream_stills OUT_DIR    writes OUT_DIR/NN_name.ppm (800 x 480) and OUT_DIR/boulder.txt
//
// Each still runs the scene with its inputs until its moment, then poses what it shows (a floater, a
// fish, a mockup) straight into the World and runs on a few ticks before the frame is taken (a full
// render). The poses use only the stream's own state (scene/stream.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <vector>

#include "ss_app.h"
#include "scene/spine.h"
#include "scene/stream.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

enum Look { DAY, DUSK, NIGHT, RAIN, MORNING };

Inputs inputsFor(Look look) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;
  switch (look) {
    case DAY: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
    case MORNING: in.mood = 1.9f; in.spine_force = 1 + SF_MORNING; break;
    case DUSK: in.mood = 4.85f; in.spine_force = 1 + SF_EVENING; break;
    case NIGHT:
      in.mood = 0.f;
      in.spine_force = 1 + SF_EVENING;
      in.night_mode = true;
      in.weather.night_dim = 1.f;
      in.weather.night_gain = 1.6f;
      break;
    case RAIN: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; in.weather_code = 1; break;
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

struct Still {
  const char* name;
  Look look;
  uint64_t seed0;
  Pred want;
  Pose pose;    // may be empty
  int after;    // ticks run after the pose before the frame
  uint8_t visit = SV_NONE;   // a visitor to bring this loop (StreamVisit::force), set before the build ends
  uint8_t month = 0;         // Inputs::month
  int dogs = 0;              // Inputs::dog_out: Lucy 1, Copper 2, both 3
};

// Quiet the stream's own schedule so only the posed things show.
void hush(World& w) {
  w.stream.next_float = w.tick + 25u * 600u;
  w.stream.next_fish = w.tick + 25u * 600u;
  for (Floater& f : w.stream.f) f.kind = FK_NONE;
  w.stream.fish.kind = FISH_NONE;
}

// A floater at `x` (frame px) in `lane`.
Floater floaterAt(const World& w, uint8_t kind, float x, float lane, float ang, uint8_t tint, float len) {
  Floater f{};
  f.kind = kind;
  f.phase = FP_DRIFT;
  f.tint = tint;
  f.side = 1;
  f.lane = lane;
  f.ang = ang;
  f.spin = 0.f;
  f.len = len;
  // Find s for x along the stream.
  float s = 0.f;
  for (int i = 0; i + 1 < LS_STREAM_PTS; ++i) {
    const LsStreamPt& a = w.land.stream[i];
    const LsStreamPt& b = w.land.stream[i + 1];
    if (x <= a.x && x >= b.x) {
      s = (float)i + (a.x - x) / (a.x - b.x);
      break;
    }
  }
  f.s = s;
  return f;
}

StreamFish fishAt(const World& w, uint8_t kind, float x, float lane, float t) {
  StreamFish f{};
  f.kind = kind;
  f.dir = 1;
  f.t = t;
  float cy = 420.f, half = 26.f;
  landscapeStreamSpan(w.land, x, cy, half);
  f.x = (int16_t)(x + 0.5f);
  f.y = (int16_t)(cy - lane * half / 1.06f + 0.5f);
  return f;
}

bool shoot(const Still& s, const char* out, FILE* log) {
  for (uint64_t seed = s.seed0; seed < s.seed0 + 300; ++seed) {
    Inputs in = inputsFor(s.look);
    in.month = s.month;
    in.dog_out = s.dogs;
    void* mem = malloc(SS_APP_BYTES);
    std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
    Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    App* app = appStart(mem, kAlloc, seed, in);
    const_cast<World&>(*appWorld(app)).stream.visit.force = s.visit;
    bool hit = false;
    for (int tick = 0; tick < 25 * 900 && !hit; ++tick) {
      if (appTick(app)) break;
      if (tick % 8 == 0) appRender(app, surf);
      if (tick > 400 && s.want(app, *appWorld(app))) {
        World& w = const_cast<World&>(*appWorld(app));
        if (s.pose) s.pose(w);
        for (int k = 0; k < s.after; ++k) {
          appTick(app);
          appRender(app, surf);
        }
        appInvalidate(app);
        appRender(app, surf);
        char path[512];
        snprintf(path, sizeof path, "%s/%s.ppm", out, s.name);
        writePPM(path, fb.data());
        const Vec2 b = landscapeBoulder(appWorld(app)->land);
        printf("  %-28s seed %llu tick %d  boulder %d %d\n", s.name, (unsigned long long)seed, tick, (int)b.x, (int)b.y);
        fprintf(log, "%s %d %d\n", s.name, (int)b.x, (int)b.y);
        hit = true;
      }
    }
    appStop(app, kAlloc);
    free(mem);
    if (hit) return true;
  }
  printf("  %-28s NOT FOUND\n", s.name);
  return false;
}

bool beatIs(const App* a, const char* b) { return strcmp(appBeatName(a), b) == 0; }
// He is out of the way: in frame and still, or out of the scene.
bool calm(const App* a, const World& w) {
  return (beatIs(a, "pause") || beatIs(a, "nap")) && !w.hiker.walking && appCritterCount(a) == 0 && !w.busy;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: stream_stills OUT_DIR\n");
    return 2;
  }
  const char* out = argv[1];
  if (ssXmasSwitch() != SS_XMAS) {   // the World's layout depends on it (scene/xmas_switch.h)
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  char lp[512];
  snprintf(lp, sizeof lp, "%s/boulder.txt", out);
  FILE* log = fopen(lp, "w");
  if (!log) return 2;
  const Pred calmDay = [](const App* a, const World& w) { return calm(a, w); };
  const Pred fireLit = [](const App* a, const World& w) {
    return calm(a, w) && w.fire.state == FIRE_LIT && w.fire.intensity > 0.9f;
  };
  const Pred dark = [](const App* a, const World& w) { return calm(a, w) && w.fire.state == FIRE_LIT; };
  const Still stills[] = {
      {"01_day", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_LEAF, 300.f, 0.25f, 0.1f, 0, 11.f);
       }, 12},
      {"02_dusk_firelight", DUSK, 1, fireLit, [](World& w) { hush(w); }, 8},
      {"03_night_moon", NIGHT, 1, dark, [](World& w) { hush(w); }, 8},
      {"04_leaf", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_LEAF, 520.f, 0.15f, 0.12f, 0, 11.5f);
         w.stream.f[1] = floaterAt(w, FK_LEAF, 330.f, 0.4f, 0.6f, 3, 10.5f);
       }, 6},
      {"05_stick_snagged", DAY, 1, calmDay, [](World& w) {
         hush(w);
         Floater f = floaterAt(w, FK_STICK, 650.f, LS_BOULDER_LANE - 0.18f, 0.05f, 1, 17.f);
         f.hold = 30.f;
         w.stream.f[0] = f;
       }, 25 * 5},
      {"06_pine_cone", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_CONE, 470.f, 0.3f, 0.2f, 0, 6.8f);
         w.stream.f[1] = floaterAt(w, FK_STICK, 250.f, 0.2f, 0.4f, 0, 15.f);
       }, 6},
      {"07_fish_jump", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.fish = fishAt(w, FISH_JUMP, 430.f, -0.2f, 0.f);
       }, 12},
      {"08_fish_splash", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.fish = fishAt(w, FISH_JUMP, 430.f, -0.2f, 0.f);
       }, 29},
      {"09_fish_rings", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.fish = fishAt(w, FISH_JUMP, 430.f, -0.2f, 0.f);
       }, 52},
      {"10_rise_ring", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.fish = fishAt(w, FISH_RISE, 380.f, -0.1f, 0.f);
       }, 22},
      {"11_rain", RAIN, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_STICK, 470.f, 0.35f, 0.07f, 1, 19.f);
         w.stream.f[1] = floaterAt(w, FK_LEAF, 260.f, 0.05f, 0.4f, 1, 11.f);
       }, 6},
      {"12_heron_arriving", MORNING, 1, [](const App*, const World& w) {
         return w.stream.visit.kind == SV_HERON && w.stream.visit.stage == GS_IN && w.stream.visit.t > 3.2f;
       }, [](World& w) { hush(w); }, 0, SV_HERON},
      {"13_heron_standing", MORNING, 1, [](const App*, const World& w) {
         return w.stream.visit.stage == GS_STAY && w.stream.visit.t > 6.f && w.stream.visit.act == 0 && w.stream.visit.act_t > 1.f;
       }, [](World& w) { hush(w); }, 0, SV_HERON},
      {"14_heron_strike", MORNING, 1, [](const App*, const World& w) {
         return w.stream.visit.stage == GS_STAY && w.stream.visit.t > 4.f;
       }, [](World& w) {
         hush(w);
         StreamVisit& V = w.stream.visit;
         V.act = 4;   // the strike
         V.act_t = 0.f;
         V.act_len = 1.4f;
       }, 14, SV_HERON},
      {"15_heron_ring", MORNING, 1, [](const App*, const World& w) {
         return w.stream.visit.stage == GS_STAY && w.stream.visit.t > 4.f;
       }, [](World& w) {
         hush(w);
         StreamVisit& V = w.stream.visit;
         V.act = 4;
         V.act_t = 0.f;
         V.act_len = 1.4f;
       }, 33, SV_HERON},
      {"16_deer_walking", MORNING, 1, [](const App*, const World& w) {
         const StreamVisit& V = w.stream.visit;
         return V.kind == SV_DEER && V.stage == GS_IN && V.slot >= 0 && w.critters[V.slot].visible &&
                w.critters[V.slot].feet.x < 770.f;
       }, [](World& w) { hush(w); }, 0, SV_DEER},
      {"17_deer_drinking", MORNING, 1, [](const App*, const World& w) {
         const StreamVisit& V = w.stream.visit;
         return V.kind == SV_DEER && V.stage == GS_STAY && V.act == 2 && V.act_t > 2.3f;
       }, [](World& w) { hush(w); }, 0, SV_DEER},
      {"18_deer_looking", MORNING, 1, [](const App*, const World& w) {
         const StreamVisit& V = w.stream.visit;
         return V.kind == SV_DEER && V.stage == GS_STAY && V.act == 3 && V.act_t > 1.2f;
       }, [](World& w) { hush(w); }, 0, SV_DEER},
      {"19_leaves_summer", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_LEAF, 500.f, 0.2f, 0.12f, 3, 11.5f);
         w.stream.f[1] = floaterAt(w, FK_LEAF, 420.f, -0.15f, 0.6f, 3, 10.5f);
       }, 6, SV_NONE, 7},
      {"20_leaves_autumn", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_LEAF, 500.f, 0.2f, 0.12f, 0, 11.5f);
         w.stream.f[1] = floaterAt(w, FK_LEAF, 420.f, -0.15f, 0.6f, 1, 10.5f);
       }, 6, SV_NONE, 10},
      {"21_leaves_winter", DAY, 1, calmDay, [](World& w) {
         hush(w);
         w.stream.f[0] = floaterAt(w, FK_LEAF, 500.f, 0.2f, 0.12f, 4, 11.f);
         w.stream.f[1] = floaterAt(w, FK_STICK, 420.f, -0.15f, 0.6f, 0, 15.f);
       }, 6, SV_NONE, 1},
      {"22_duck_mockup", DAY, 1, calmDay, [](World& w) { hush(w); w.stream.mock = SM_DUCK; }, 6},
      {"23_dragonfly_mockup", DAY, 1, calmDay, [](World& w) { hush(w); w.stream.mock = SM_DRAGONFLY; }, 6},
      // Round 3: a house dog goes for a visitor (housedogs.h HA_CHASE): Lucy charges, the heron lifts off;
      // Copper runs at the deer, it bolts.
      {"24_dog_heron", MORNING, 1, [](const App*, const World& w) {
         const StreamVisit& V = w.stream.visit;
         return V.kind == SV_HERON && V.stage == GS_OUT && V.t >= 0.5f && w.dogs.d[HD_LUCY].act == HA_CHASE &&
                w.dogs.d[HD_LUCY].step == 1;
       }, nullptr, 0, SV_HERON, 0, 1},
      {"25_dog_deer", MORNING, 1, [](const App*, const World& w) {
         const StreamVisit& V = w.stream.visit;
         // (The bolting deer is the director's once it runs: the visit is done, the critter still in sight.)
         return V.kind == SV_DEER && V.spooked && w.dogs.d[HD_COPPER].act == HA_CHASE && w.dogs.d[HD_COPPER].step == 1 &&
                w.dogs.d[HD_COPPER].t >= 0.6f;
       }, nullptr, 0, SV_DEER, 0, 2},
  };
  int missing = 0;
  for (const Still& s : stills)   // a second argument: only the stills whose name contains it
    if ((argc < 3 || strstr(s.name, argv[2])) && !shoot(s, out, log)) ++missing;
  fclose(log);
  return missing ? 1 : 0;
}
