// Stills of Lucy and Copper in camp and the upstairs request arrows, for Mike's review (stage 13).
//
// It runs the scene with the dogs out (and the arrows set) and, for each tile, plays on until the
// moment the tile asks for (Lucy lying by the fire, Copper's play-bow, a pat), then writes that
// frame. Night tiles use the panel's night gain (1.6), as the stage 12 stills did.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I screensaver/core screensaver/tools/dog_stills.cpp BUILD/libss_core.a
//     -o BUILD/dog_stills
//   BUILD/dog_stills OUT_DIR       the stills, as OUT_DIR/NN_name.ppm, and OUT_DIR/tiles.txt
//   BUILD/dog_stills --cost        what the dogs cost a frame: the same runs with no dog out and both out
//   python -I screensaver/tools/dog_sheet.py OUT_DIR docs/screensaver/reviews/stage13-dogs
//
// tiles.txt has one line per tile: "file|label", or "file|label|x0,y0,x1,y1" for a close-up crop of
// that file (dog_sheet.py enlarges it).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <vector>

#include "ss_app.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/spine.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

enum Look { DAY, GOLDEN, NIGHT, MORNING };

Inputs inputsFor(Look look, int dog) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;   // clear weather
  in.chip = true;
  in.dog_out = dog;
  switch (look) {
    case DAY: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
    case GOLDEN: in.mood = 4.f; in.spine_force = 1 + SF_EVENING; break;
    case MORNING: in.mood = 2.f; in.spine_force = 1 + SF_MORNING; break;
    case NIGHT:
      in.mood = 0.f;
      in.spine_force = 1 + SF_EVENING;
      in.night_mode = true;
      in.weather.night_dim = 1.f;
      in.weather.night_gain = 1.6f;
      break;
  }
  return in;
}

// What a tile waits for. dog: 0 Lucy, 1 Copper, -1 either; act: HouseDogAct (HA_NONE: any hold).
struct Want {
  int dog;
  int act;
  float hold_s;      // seconds into the hold (step 2) at least
  int min_tick;
  bool pat;          // the pat itself, the hand out
  bool fetch;        // the fetch's two dogs in sight
  bool hiker;        // the hiker in sight too
  bool fire = false; // the fire lit
};

bool matches(const App* app, const Want& q) {
  const World& w = *appWorld(app);
  if ((int)appTickCount(app) < q.min_tick) return false;
  if (q.hiker && !w.hiker.visible) return false;
  if (q.fire && w.fire.state != FIRE_LIT) return false;
  if (q.fetch) {
    if (!(w.major.on && w.major.id == MJ_FETCH) || w.major.count < 1) return false;
    int n = 0;
    for (int i = 0; i < CRITTER_STORY; ++i)
      if (w.crit[i].in_use && w.crit[i].tag == CT_MAJOR && w.critters[i].kind == CR_DOG && w.critters[i].visible &&
          w.critters[i].feet.x > 40.f && w.critters[i].feet.x < 620.f)
        ++n;
    return n == 2;
  }
  if (q.pat) {
    const int k = w.dogs.pat - 1;
    return k >= 0 && (q.dog < 0 || q.dog == k) && w.dogs.pat_t > 1.3f && w.dogs.pat_t < 2.2f;
  }
  for (int k = 0; k < HD_COUNT; ++k) {
    if (q.dog >= 0 && q.dog != k) continue;
    const HouseDog& d = w.dogs.d[k];
    if (!appHouseDog(app, k, nullptr, nullptr) || d.phase != HP_HERE) continue;
    if (q.act == HA_LEAF) {
      if (d.act == HA_LEAF && d.step == 12 && d.t > 0.6f) return true;
      continue;
    }
    if (q.act != HA_NONE && d.act != q.act) continue;
    if (d.step == 2 && d.t >= q.hold_s) {
      // both dogs in sight when both are out (the "both" tiles), and neither half out of the frame
      bool ok = true;
      for (int j = 0; j < HD_COUNT; ++j) {
        Rect b;
        if (!((w.dogs.want >> j) & 1)) continue;
        if (!appHouseDog(app, j, &b, nullptr) || b.x0 < 4 || b.x1 > SS_WIDTH - 4) ok = false;
      }
      if (ok) return true;
    }
  }
  return false;
}

struct Tile {
  const char* name;
  const char* label;
  Look look;
  int dog;
  uint8_t req[3];
  Want want;
};

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

// Plays seeds until the tile's moment; writes it. The dogs' bounds then, for the crops.
bool shoot(const Tile& t, const char* dir, int index, Rect bounds[2], int* seed_used) {
  std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
  Surface s{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  void* mem = malloc(SS_APP_BYTES);
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Inputs in = inputsFor(t.look, t.dog);
    for (int i = 0; i < 3; ++i) in.req[i] = t.req[i];
    App* app = appStart(mem, kAlloc, seed, in);
    if (!app) break;
    const uint32_t len = appLoopTicks(app);
    for (uint32_t k = 0; k < len; ++k) {
      appTick(app);
      if (!matches(app, t.want)) continue;
      appInvalidate(app);
      appRender(app, s);
      if (t.req[0] || t.req[1] || t.req[2]) {
        // Urgent arrows flash: this frame and one half a second on; the lit one is the tile, the dim
        // one is kept beside it (NN_name_dim.ppm) for the close-up.
        std::vector<Pixel> a = fb;
        for (int j = 0; j < 13; ++j) appTick(app);
        appInvalidate(app);
        appRender(app, s);
        auto glow = [](const std::vector<Pixel>& f) {   // the arrows' row, summed
          long sum = 0;
          for (int y = 427; y < 461; ++y)
            for (int x = 400; x < 536; ++x) {
              const Pixel p = f[(size_t)y * SS_WIDTH + x];
              sum += red8(p) + green8(p) + blue8(p);
            }
          return sum;
        };
        std::vector<Pixel> dim = fb;
        if (glow(a) < glow(fb)) a.swap(dim);
        char p2[512];
        snprintf(p2, sizeof p2, "%s/%02d_%s_dim.ppm", dir, index, t.name);
        writePPM(p2, dim.data());
        fb.swap(a);
      }
      for (int j = 0; j < 2; ++j) {
        bounds[j] = Rect{0, 0, 0, 0};
        appHouseDog(app, j, &bounds[j], nullptr);
      }
      char path[512];
      snprintf(path, sizeof path, "%s/%02d_%s.ppm", dir, index, t.name);
      const bool ok = writePPM(path, fb.data());
      appStop(app, kAlloc);
      free(mem);
      *seed_used = (int)seed;
      return ok;
    }
    appStop(app, kAlloc);
  }
  free(mem);
  return false;
}

int cost() {
  // The same runs with no dog out and both out: strips and pixels composited a frame, and the PC's
  // draw time, from a settled camp (after the build-up) for 6000 ticks.
  const Look looks[3] = {DAY, GOLDEN, NIGHT};
  std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
  Surface s{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  void* mem = malloc(SS_APP_BYTES);
  printf("sizeof(App) %u of %u; sizeof(HouseDogs) %u\n", appAppBytes(), (unsigned)SS_APP_BYTES, (unsigned)sizeof(HouseDogs));
  for (Look l : looks) {
    for (int dog = 0; dog <= 3; dog += 3) {
      double strips = 0, px = 0, us = 0;
      int frames = 0;
      for (uint64_t seed = 1; seed <= 4; ++seed) {
        App* app = appStart(mem, kAlloc, seed, inputsFor(l, dog));
        for (int k = 0; k < 600; ++k) {
          appTick(app);
          appRender(app, s);
        }
        appProfile(app);
        for (int k = 0; k < 6000; ++k) {
          appTick(app);
          const auto t0 = std::chrono::steady_clock::now();
          strips += appRender(app, s);
          us += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count() / 1000.0;
          ++frames;
          if (k % 1000 == 999) px += appProfile(app).px;
        }
        appStop(app, kAlloc);
      }
      printf("%-6s dogs %d: %.2f strips a frame, %.0f px a frame, %.1f us a frame on this PC\n",
             l == DAY ? "day" : (l == GOLDEN ? "golden" : "night"), dog, strips / frames, px / frames, us / frames);
    }
  }
  free(mem);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: dog_stills OUT_DIR | --cost\n");
    return 2;
  }
  if (!strcmp(argv[1], "--cost")) return cost();
  const char* dir = argv[1];
  const Want any = {-1, HA_NONE, 1.5f, 1500, false, false, true};
  const Tile tiles[] = {
      {"lucy_day", "Lucy alone, day (watching him)", DAY, 1, {0, 0, 0}, {0, HA_WATCH, 2.f, 900, false, false, true}},
      {"copper_day", "Copper alone, day (sniffing the tent)", DAY, 2, {0, 0, 0}, {1, HA_TENT, 1.f, 900, false, false, true}},
      {"both_day", "Both, day", DAY, 3, {0, 0, 0}, any},
      {"lucy_golden", "Lucy alone, golden hour", GOLDEN, 1, {0, 0, 0}, {0, HA_NONE, 1.5f, 1500, false, false, false}},
      {"copper_golden", "Copper alone, golden hour", GOLDEN, 2, {0, 0, 0}, {1, HA_NONE, 1.5f, 1500, false, false, false}},
      {"both_golden", "Both, golden hour", GOLDEN, 3, {0, 0, 0}, {-1, HA_NONE, 1.5f, 1500, false, false, false}},
      {"lucy_night", "Lucy alone, night (by the fire)", NIGHT, 1, {0, 0, 0}, {0, HA_FIRE, 4.f, 1200, false, false, false, true}},
      {"copper_night", "Copper alone, night", NIGHT, 2, {0, 0, 0}, {1, HA_NONE, 2.f, 1200, false, false, false, true}},
      {"both_night", "Both, night", NIGHT, 3, {0, 0, 0}, {-1, HA_FIRE, 3.f, 1200, false, false, false, true}},
      {"pat_lucy", "Behaviour: a pat for Lucy", DAY, 1, {0, 0, 0}, {0, HA_PAT, 0.f, 600, true, false, true}},
      {"pat_copper", "Behaviour: a pat for Copper", DAY, 2, {0, 0, 0}, {1, HA_PAT, 0.f, 600, true, false, true}},
      {"drink", "Behaviour: a drink at the stream", DAY, 3, {0, 0, 0}, {-1, HA_DRINK, 1.5f, 600, false, false, false}},
      {"leaf", "Behaviour: chasing a leaf", DAY, 3, {0, 0, 0}, {-1, HA_LEAF, 0.f, 600, false, false, false}},
      {"tent", "Behaviour: Lucy sniffs the tent", DAY, 1, {0, 0, 0}, {0, HA_TENT, 1.5f, 600, false, false, false}},
      {"play", "Behaviour: Copper's play-bow at Lucy", DAY, 3, {0, 0, 0}, {1, HA_PLAY, 0.6f, 600, false, false, false}},
      {"nap", "Behaviour: a nap (chin down)", GOLDEN, 3, {0, 0, 0}, {-1, HA_NAP, 8.f, 600, false, false, false}},
      {"morning", "Both in the morning camp", MORNING, 3, {0, 0, 0}, {-1, HA_NONE, 2.f, 1200, false, false, false}},
      {"fetch", "The fetch: Lucy plays it, Copper shadows her", DAY, 3, {0, 0, 0}, {-1, HA_NONE, 0.f, 0, false, true, false}},
      {"badges_lit", "Upstairs badges: mild heat, mild cool, urgent heat (lit)", DAY, 3, {2, 3, 1}, {-1, HA_NONE, 2.f, 1500, false, false, false}},
      {"badges_night", "Upstairs badges at night: urgent cool, mild heat", NIGHT, 3, {4, 2, 0}, {-1, HA_NONE, 2.f, 1500, false, false, false, true}},
  };
  const int n = (int)(sizeof tiles / sizeof tiles[0]);
  char path[512];
  snprintf(path, sizeof path, "%s/tiles.txt", dir);
  FILE* list = fopen(path, "w");
  if (!list) return 1;
  Rect both_bounds[2] = {}, night_bounds[2] = {};
  char night_path[64] = "";
  int index = 1, failed = 0;
  for (int i = 0; i < n; ++i) {
    Rect b[2];
    int seed = 0;
    if (!shoot(tiles[i], dir, index, b, &seed)) {
      printf("no frame for %s\n", tiles[i].name);
      ++failed;
      continue;
    }
    printf("%02d_%s: seed %d\n", index, tiles[i].name, seed);
    fprintf(list, "%02d_%s.ppm|%s\n", index, tiles[i].name, tiles[i].label);
    if (!strcmp(tiles[i].name, "both_day")) {
      both_bounds[0] = b[0];
      both_bounds[1] = b[1];
      snprintf(path, sizeof path, "%02d_both_day.ppm", index);
    }
    if (!strcmp(tiles[i].name, "both_night")) {
      night_bounds[0] = b[0];
      night_bounds[1] = b[1];
      snprintf(night_path, sizeof night_path, "%02d_both_night.ppm", index);
    }
    if (tiles[i].req[0]) {
      fprintf(list, "%02d_%s.ppm|close-up: the badge row, urgent arrow lit|404,420,604,468\n", index, tiles[i].name);
      fprintf(list, "%02d_%s_dim.ppm|close-up: the same half a second on, dim|404,420,604,468\n", index, tiles[i].name);
    }
    ++index;
  }
  // The close-up pair: each dog from the "both, day" frame, its own box padded.
  for (int k = 0; k < 2; ++k) {
    const Rect& r = both_bounds[k];
    if (r.empty()) continue;
    const int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
    fprintf(list, "%s|close-up: %s, day (same frame, same scale)|%d,%d,%d,%d\n", path, k ? "Copper" : "Lucy", cx - 50, cy - 30, cx + 50, cy + 30);
  }
  for (int k = 0; k < 2; ++k) {   // and the same pair at night, from the "both, night" frame
    const Rect& r = night_bounds[k];
    if (r.empty() || !night_path[0]) continue;
    const int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
    fprintf(list, "%s|close-up: %s, night (same frame, same scale)|%d,%d,%d,%d\n", night_path, k ? "Copper" : "Lucy", cx - 50, cy - 30,
            cx + 50, cy + 30);
  }
  fclose(list);
  return failed ? 1 : 0;
}
