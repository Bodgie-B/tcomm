// Stills of the mail bird, fire and ice, and the hiker's wave, for Mike's review (stage 14).
//
// It runs the scene with the inputs each tile needs (mail waiting, the tenant's arrows, a passer-by)
// and plays on until the tile's moment (the bird landing, a douse, the ice at its smallest), then
// writes that frame. Night tiles use the panel's night gain (1.6), as the earlier stills did.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I screensaver/core screensaver/tools/fireice_stills.cpp BUILD/libss_core.a
//     -o BUILD/fireice_stills
//   BUILD/fireice_stills OUT_DIR    the stills, as OUT_DIR/NN_name.ppm, and OUT_DIR/tiles.txt
//   BUILD/fireice_stills --cost     what each costs a frame: the same runs with and without it
//   python -I screensaver/tools/dog_sheet.py OUT_DIR docs/screensaver/reviews/stage14-mail-fire-ice
//
// tiles.txt has one line per tile: "file|label", or "file|label|x0,y0,x1,y1" for a close-up crop of
// that file (dog_sheet.py enlarges it).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <functional>
#include <vector>

#include "ss_app.h"
#include "scene/fireice.h"
#include "scene/mailbird.h"
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

typedef std::function<bool(const App*, const World&, int)> Pred;   // (app, world, ticks since the inputs changed)

struct Tile {
  const char* name;
  const char* label;
  Look look;
  int dog;
  bool mail_at_start;   // mail waiting as the screensaver starts
  int mail_on;          // tick the mail is marked (-1 never)
  int mail_off;         // tick it is collected (-1 never)
  uint8_t ask;          // the tenant's arrow (Ask), and a second one
  uint8_t ask2;
  int ask_on, ask_off;  // ticks they are set and cleared (-1 never)
  int passer_from;      // a passer-by from this tick on, every 10 s (-1 none)
  Pred want;
  int crop[4];          // a close-up of this box too (all 0: none)
  const char* crop_label;
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

bool stageIs(const World& w, const char* s) { return strcmp(fireIceStageName(w), s) == 0; }
int fires(const World& w) {
  int n = 0;
  for (const FiFlame& q : w.fi.f) n += q.k > 0.3f ? 1 : 0;
  return n;
}
bool dogsIn(const World& w, int want) {
  for (int k = 0; k < HD_COUNT; ++k) {
    if (!((want >> k) & 1)) continue;
    const int s = CRITTER_STORY + k;
    if (!w.crit[s].in_use || !w.critters[s].visible || w.dogs.d[k].step != 2) return false;
  }
  return true;
}

void hikerBox(const World& w, int pad, int out[4]);

// Plays seeds until the tile's moment; writes it.
bool shoot(const Tile& t, const char* out_dir, FILE* list, int index) {
  for (uint64_t seed = 1; seed <= 6; ++seed) {
    Inputs in = inputsFor(t.look, t.dog);
    in.mail = t.mail_at_start;
    void* mem = malloc(SS_APP_BYTES);
    std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
    Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    App* app = appStart(mem, kAlloc, seed, in);
    bool done = false;
    int last_change = 0;
    for (int tick = 0; tick < 25 * 900 && !done; ++tick) {
      bool changed = false, passer = false;
      if (tick == t.mail_on) { in.mail = true; changed = true; }
      if (tick == t.mail_off) { in.mail = false; changed = true; }
      if (tick == t.ask_on) { in.temp_ask[0] = t.ask; in.temp_ask[1] = t.ask2; changed = true; }
      if (tick == t.ask_off) { in.temp_ask[0] = in.temp_ask[1] = 0; changed = true; }
      if (t.passer_from >= 0 && tick >= t.passer_from) {
        const bool p = ((tick - t.passer_from) % 250) < 75;
        if (p != in.passer_by) { in.passer_by = p; passer = true; }   // someone walks up for 3 s every 10 s
      }
      if (changed) last_change = tick;
      if (changed || passer) appSetInputs(app, in);
      appTick(app);
      appRender(app, surf);
      if (t.want(app, *appWorld(app), tick - last_change)) {
        char path[512];
        snprintf(path, sizeof path, "%s/%02d_%s.ppm", out_dir, index, t.name);
        writePPM(path, fb.data());
        fprintf(list, "%02d_%s.ppm|%s\n", index, t.name, t.label);
        int box[4] = {t.crop[0], t.crop[1], t.crop[2], t.crop[3]};
        if (t.crop[2] < 0) hikerBox(*appWorld(app), 34, box);   // round the hiker
        if (box[2] > 0) fprintf(list, "%02d_%s.ppm|%s|%d,%d,%d,%d\n", index, t.name, t.crop_label, box[0], box[1], box[2], box[3]);
        printf("  %02d %-22s seed %llu tick %d\n", index, t.name, (unsigned long long)seed, tick);
        done = true;
      }
    }
    appStop(app, kAlloc);
    free(mem);
    if (done) return true;
  }
  printf("  %02d %-22s NOT FOUND\n", index, t.name);
  return false;
}

// A box round the hiker (for the close-ups), about 5:3 like the tiles.
void hikerBox(const World& w, int pad, int out[4]) {
  const Rect r = w.hiker_full;
  const int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
  out[0] = cx - 5 * pad / 3 - pad;
  out[1] = cy - pad - 10;
  out[2] = cx + 5 * pad / 3 + pad;
  out[3] = cy + pad + 10;
}

uint32_t nowUs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int runCost() {
  // The same seeds with and without each feature: pixels composited and draw time a frame.
  struct Case {
    const char* name;
    Look look;
    bool mail;
    uint8_t ask;
  };
  const Case cases[] = {{"none (day)", DAY, false, 0},     {"mail bird", DAY, true, 0},     {"too hot, mild", DAY, false, 3},
                        {"too hot, urgent", DAY, false, 4}, {"too cold, mild", DAY, false, 2}, {"too cold, urgent", DAY, false, 1}};
  for (const Case& c : cases) {
    double us = 0.0, strips = 0.0, worst = 0.0, px = 0.0, stat = 0.0;
    long frames = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
      Inputs in = inputsFor(c.look, 0);
      void* mem = malloc(SS_APP_BYTES);
      std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
      Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
      App* app = appStart(mem, kAlloc, seed, in);
      appSetClock(app, nowUs);
      for (int tick = 0; tick < 25 * 300; ++tick) {
        if (tick == 600) {
          in.mail = c.mail;
          in.temp_ask[0] = c.ask;
          appSetInputs(app, in);
        }
        appTick(app);
        const auto t0 = std::chrono::steady_clock::now();
        const int n = appRender(app, surf);
        const double d = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        const RenderProfile p = appProfile(app);
        if (tick < 25 * 60) continue;   // a minute in: the spell under way
        px += p.px;
        stat += p.static_us;
        us += d;
        worst = d > worst ? d : worst;
        strips += n;
        frames++;
      }
      appStop(app, kAlloc);
      free(mem);
    }
    printf("%-18s %7.1f us a frame (worst %7.1f), %.2f strips, %.0f px composited, %.1f us cache repaint a frame\n", c.name,
           us / (double)frames, worst, strips / (double)frames, px / (double)frames, stat / (double)frames);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "--cost") == 0) return runCost();
  if (argc < 2) {
    fprintf(stderr, "usage: fireice_stills OUT_DIR | --cost\n");
    return 2;
  }
  const char* out = argv[1];
  char path[512];
  snprintf(path, sizeof path, "%s/tiles.txt", out);
  FILE* list = fopen(path, "w");
  if (!list) return 1;

  const int T0 = 25 * 40;   // the inputs change a while into the loop, once the camp is up
  auto mailStage = [](int s, float t) {
    return [s, t](const App*, const World& w, int) { return w.mail.stage == s && w.mail.t >= t; };
  };
  const Tile tiles[] = {
      // The mail bird.
      {"bird_arriving", "mail: the bird flies in with a letter", DAY, 0, false, T0, -1, 0, 0, -1, -1, -1,
       mailStage(MB_IN, 2.0f), {0, 0, 0, 0}, nullptr},
      {"bird_letter_falls", "lands on the tent, lets the letter go", DAY, 0, false, T0, -1, 0, 0, -1, -1, -1,
       [](const App*, const World& w, int) { return w.mail.stage == MB_PERCH && w.mail.letter && w.mail.lt > 0.3f && w.mail.lt < 0.5f; },
       {120, 240, 320, 340}, "the letter falling (close-up)"},
      {"bird_perched_day", "perched, letter by the tent (day)", DAY, 0, false, T0, -1, 0, 0, -1, -1, -1,
       [](const App*, const World& w, int since) { return w.mail.stage == MB_PERCH && w.mail.on_tent && since > 25 * 12 && w.mail.t > 2.f; },
       {120, 240, 320, 340}, "perched by day (close-up)"},
      {"bird_perched_night", "perched, letter by the tent (night)", NIGHT, 0, true, -1, -1, 0, 0, -1, -1, -1,
       [](const App*, const World& w, int) { return w.mail.stage == MB_PERCH && w.mail.on_tent && w.mail.t > 2.f && w.tent.lit; },
       {120, 240, 320, 340}, "perched at night (close-up)"},
      {"bird_on_branch", "no tent yet: on the pine branch", GOLDEN, 0, true, -1, -1, 0, 0, -1, -1, -1,
       [](const App*, const World& w, int) { return w.mail.stage == MB_PERCH && !w.mail.on_tent && w.mail.t > 2.f && w.hiker.visible; },
       {0, 0, 0, 0}, nullptr},
      {"bird_glance", "the hiker glances up at it", GOLDEN, 0, true, -1, -1, 0, 0, -1, -1, -1,
       [](const App*, const World& w, int) { return w.mail.stage == MB_PERCH && w.mail.glance > 0.6f && w.mail.glance < 1.0f && w.hiker.visible &&
                                                   w.hiker.target == POSE_STAND && !w.hiker.walking; },
       {0, 0, -1, 0}, "the glance (close-up)"},
      {"bird_leaving", "collected: letter gone, bird off", GOLDEN, 0, false, T0, T0 + 25 * 40, 0, 0, -1, -1, -1,
       [](const App*, const World& w, int) { return w.mail.stage == MB_OFF && w.mail.t > 1.0f; }, {0, 0, 0, 0}, nullptr},
      // Too hot.
      {"hot_mild_run", "too hot, mild: to the stream", DAY, 0, false, -1, -1, 3, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "to_water") && fires(w) >= 1 && w.hiker.walking && w.fi.st > 0.4f; },
       {0, 0, 0, 0}, nullptr},
      {"hot_fill", "fills the bucket at the stream", DAY, 0, false, -1, -1, 3, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "fill") && w.fi.st > 0.55f; },
       {0, 0, 0, 0}, nullptr},
      {"hot_carry", "runs back with it full", DAY, 0, false, -1, -1, 3, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "to_fire") && w.fi.st > 0.5f && w.fi.water > 0.9f; },
       {0, 0, 0, 0}, nullptr},
      {"hot_douse", "the pour", DAY, 0, false, -1, -1, 3, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "douse") && w.fi.st > 0.5f && w.fi.st < 0.56f; },
       {0, 0, -1, 0}, "the pour (close-up)"},
      {"hot_steam", "out, with a puff of steam", DAY, 0, false, -1, -1, 3, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "douse") && w.fi.st > 0.85f; }, {0, 0, 0, 0}, nullptr},
      {"hot_urgent", "too hot, urgent: warmer light", DAY, 0, false, -1, -1, 4, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return fires(w) >= 4 && w.fi.warm_q == 3 && w.hiker.walking; }, {0, 0, 0, 0}, nullptr},
      {"hot_urgent_two", "two urgent arrows", GOLDEN, 0, false, -1, -1, 4, 4, T0, -1, -1,
       [](const App*, const World& w, int) { return fires(w) >= 5 && w.hiker.walking; }, {0, 0, 0, 0}, nullptr},
      {"hot_urgent_night", "too hot, urgent, night", NIGHT, 0, false, -1, -1, 4, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return fires(w) >= 4 && w.hiker.walking; }, {0, 0, 0, 0}, nullptr},
      {"hot_dogs", "dogs lie clear of the fires", DAY, 3, false, -1, -1, 4, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return fires(w) >= 3 && dogsIn(w, 3); }, {0, 0, 0, 0}, nullptr},
      {"hot_wipe", "cleared: bucket down, wipes brow", DAY, 0, false, -1, -1, 4, 0, T0, T0 + 25 * 45, -1,
       [](const App*, const World& w, int) { return stageIs(w, "wipe") && w.fi.st > 0.9f; }, {0, 0, 0, 0}, nullptr},
      {"hot_after", "afterwards: nothing left", DAY, 0, false, -1, -1, 4, 0, T0, T0 + 25 * 45, -1,
       [](const App*, const World& w, int since) { return !w.fi.on && since > 25 * 40 && w.hiker.visible; }, {0, 0, 0, 0}, nullptr},
      // Too cold.
      {"cold_mild", "too cold, mild: circle holds camp", DAY, 0, false, -1, -1, 2, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "warm") && w.fi.ice_q > 0 && w.fi.ice < 0.95f; }, {0, 0, 0, 0}, nullptr},
      {"cold_wood", "hauls wood for the blaze", DAY, 0, false, -1, -1, 2, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "carry_wood") && w.fi.ice < 1.0f && w.fi.st > 0.6f; }, {0, 0, 0, 0}, nullptr},
      {"cold_urgent", "too cold, urgent: small circle", DAY, 0, false, -1, -1, 1, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return stageIs(w, "warm") && w.fi.ice < 0.42f && w.fi.breath_t > 0.25f && w.fi.breath_t < 0.5f; },
       {0, 0, -1, 0}, "huddled, breath showing (close-up)"},
      {"cold_urgent_night", "too cold, urgent, night", NIGHT, 0, false, -1, -1, 1, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return w.fi.on && w.fi.ice < 0.42f && stageIs(w, "warm"); }, {0, 0, 0, 0}, nullptr},
      {"cold_dogs", "the dogs lie in the warm circle", DAY, 3, false, -1, -1, 1, 0, T0, -1, -1,
       [](const App*, const World& w, int) { return w.fi.ice < 0.45f && dogsIn(w, 3) && stageIs(w, "warm"); }, {0, 0, 0, 0}, nullptr},
      {"cold_melting", "cleared: the ice melts back", DAY, 0, false, -1, -1, 1, 0, T0, T0 + 25 * 50, -1,
       [](const App*, const World& w, int) { return w.fi.on && w.fi.ending && w.fi.ice > 0.95f && w.fi.ice < 1.1f; }, {0, 0, 0, 0}, nullptr},
      {"cold_after", "afterwards: no ice, fire as was", DAY, 0, false, -1, -1, 1, 0, T0, T0 + 25 * 50, -1,
       [](const App*, const World& w, int since) { return !w.fi.on && since > 25 * 40 && w.hiker.visible; }, {0, 0, 0, 0}, nullptr},
      // The wave.
      {"wave", "a passer-by: he stops and waves", GOLDEN, 0, false, -1, -1, 0, 0, -1, -1, T0,
       [](const App*, const World& w, int) { return w.react.active && w.react.wave && w.react.ticks == 38; }, {0, 0, -1, 0},
       "the wave (close-up)"},
  };
  int i = 1, missing = 0;
  for (const Tile& t : tiles) {
    if (!shoot(t, out, list, i)) ++missing;
    ++i;
  }
  fclose(list);
  return missing ? 1 : 0;
}
