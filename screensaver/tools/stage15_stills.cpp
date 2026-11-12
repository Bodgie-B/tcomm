// Stills of the sunrise overlook and the visiting couple, for Mike's review (stage 15).
//
// It runs the scene with the inputs each tile needs and plays on until the tile's moment, then
// writes that frame. Night tiles use the panel's night gain (1.6), as the earlier stills did. The
// couple come about one camp loop in seven; for the stills it takes the first seeds whose first loop
// has them (the same scene, just found quickly).
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I screensaver/core screensaver/tools/stage15_stills.cpp BUILD/libss_core.a
//     -o BUILD/stage15_stills
//   BUILD/stage15_stills OUT_DIR    the stills, as OUT_DIR/NN_name.ppm, and OUT_DIR/tiles.txt
//   BUILD/stage15_stills --cost     what each costs a frame
//   python -I screensaver/tools/dog_sheet.py OUT_DIR docs/screensaver/reviews/stage15-sunrise-couple
//
// tiles.txt has one line per tile: "file|label", or "file|label|x0,y0,x1,y1" for a close-up crop.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <functional>
#include <vector>

#include "ss_app.h"
#include "scene/spine.h"
#include "scene/spine_family.h"
#include "scene/sunrise.h"
#include "scene/visitors.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

enum Look { SUNRISE, SUNRISE_GRAY, EVENING, MIDDAY, NIGHT_MOON, NIGHT_DARK };

Inputs inputsFor(Look look, int dog) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.have_outside = true;   // a clear sky
  in.chip = true;
  in.dog_out = dog;
  in.visitors = true;
  switch (look) {
    case SUNRISE:
    case SUNRISE_GRAY:
      in.mood = 1.f;
      in.sunrise_window = true;
      in.sky.have = true;
      in.sky.sun_az = 100.f;
      in.sky.sun_alt = -1.f;
      in.sky.moon_alt = -30.f;
      if (look == SUNRISE_GRAY) in.weather.overcast = 0.7f;   // "Unsettled, Rain Later": gray and dry
      break;
    case EVENING: in.mood = 4.6f; in.spine_force = 1 + SF_EVENING; break;
    case MIDDAY: in.mood = 3.f; in.spine_force = 1 + SF_MIDDAY; break;
    case NIGHT_MOON:
    case NIGHT_DARK:
      in.mood = 0.f;
      in.spine_force = 1 + SF_EVENING;
      in.night_mode = true;
      in.weather.night_dim = 1.f;
      in.weather.night_gain = 1.6f;
      in.sky.have = true;
      in.sky.sun_alt = -40.f;
      in.sky.moon_az = look == NIGHT_MOON ? 150.f : 40.f;
      in.sky.moon_alt = look == NIGHT_MOON ? 38.f : -25.f;
      in.sky.moon_phase = 0.42f;
      break;
  }
  return in;
}

typedef std::function<bool(const App*, const World&)> Pred;

struct Tile {
  const char* name;
  const char* label;
  Look look;
  int dog;
  bool couple;     // look for a seed whose first loop has the couple
  Pred want;
  int crop[4];     // a close-up box too (all 0: none; crop[2] < 0: round the hiker; crop[2] == -2: round all three)
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

const char* beat(const App* a) { return appBeatName(a); }
bool beatIs(const App* a, const char* b) { return strcmp(beat(a), b) == 0; }
bool stepIs(const World& w, const char* s) { return strcmp(visitorsStepName(w), s) == 0; }

void boxRound(const Rect& r, int pad, int out[4]) {
  const int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
  const int hw = (r.x1 - r.x0) / 2 + pad, hh = (r.y1 - r.y0) / 2 + pad;
  int w = hw > hh * 5 / 3 ? hw : hh * 5 / 3;
  int h = w * 3 / 5;
  out[0] = cx - w;
  out[1] = cy - h;
  out[2] = cx + w;
  out[3] = cy + h;
}

bool shoot(const Tile& t, const char* out_dir, FILE* list, int index) {
  int tried = 0;
  for (uint64_t seed = 1; seed <= 400 && tried < 6; ++seed) {
    Inputs in = inputsFor(t.look, t.dog);
    void* mem = malloc(SS_APP_BYTES);
    std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
    Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    App* app = appStart(mem, kAlloc, seed, in);
    if (t.couple && !appWorld(app)->vis.planned) {
      appStop(app, kAlloc);
      free(mem);
      continue;
    }
    ++tried;
    bool done = false;
    for (int tick = 0; tick < 25 * 720 && !done; ++tick) {
      const bool new_loop = appTick(app);
      appRender(app, surf);
      if (new_loop) break;   // the tile's moment belongs to the first loop
      if (t.want(app, *appWorld(app))) {
        char path[512];
        snprintf(path, sizeof path, "%s/%02d_%s.ppm", out_dir, index, t.name);
        writePPM(path, fb.data());
        fprintf(list, "%02d_%s.ppm|%s\n", index, t.name, t.label);
        int box[4] = {t.crop[0], t.crop[1], t.crop[2], t.crop[3]};
        const World& w = *appWorld(app);
        if (t.crop[2] == -1) boxRound(w.hiker_full, 26, box);
        if (t.crop[2] == -2) {
          Rect r = w.hiker_full;
          for (int k = 0; k < 2; ++k) {
            const Rect v = visitorsRect(w, k);
            if (v.empty()) continue;
            r = Rect{r.x0 < v.x0 ? r.x0 : v.x0, r.y0 < v.y0 ? r.y0 : v.y0, r.x1 > v.x1 ? r.x1 : v.x1, r.y1 > v.y1 ? r.y1 : v.y1};
          }
          boxRound(r, 14, box);
        }
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

uint32_t nowUs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// What a frame costs: pixels composited and draw time, over whole scenes or the visit's span.
int runCost() {
  struct Case {
    const char* name;
    Look look;
    bool couple;   // only the frames while they are here
    bool visitors;
  };
  const Case cases[] = {{"camp, midday, no visit", MIDDAY, false, false}, {"camp, evening, no visit", EVENING, false, false},
                        {"sunrise (whole scene)", SUNRISE, false, true},  {"couple: frisbee", MIDDAY, true, true},
                        {"couple: fireside", EVENING, true, true},       {"couple: telescopes", NIGHT_DARK, true, true}};
  for (const Case& c : cases) {
    double us = 0.0, strips = 0.0, worst = 0.0, px = 0.0, stat = 0.0;
    long frames = 0;
    int runs = 0;
    for (uint64_t seed = 1; seed <= 400 && runs < 3; ++seed) {
      Inputs in = inputsFor(c.look, 0);
      in.visitors = c.visitors;
      void* mem = malloc(SS_APP_BYTES);
      std::vector<Pixel> fb((size_t)SS_WIDTH * SS_HEIGHT);
      Surface surf{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
      App* app = appStart(mem, kAlloc, seed, in);
      if (c.couple && !appWorld(app)->vis.planned) {
        appStop(app, kAlloc);
        free(mem);
        continue;
      }
      ++runs;
      appSetClock(app, nowUs);
      for (int tick = 0; tick < 25 * 700; ++tick) {
        if (appTick(app)) break;
        const auto t0 = std::chrono::steady_clock::now();
        const int n = appRender(app, surf);
        const double d = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        const RenderProfile p = appProfile(app);
        if (c.couple ? !appVisitorsHere(app) : tick < 25 * 15) continue;
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
    if (!frames) {
      printf("%-26s no frames\n", c.name);
      continue;
    }
    printf("%-26s %7.1f us a frame (worst %7.1f), %.2f strips, %.0f px composited, %.1f us cache repaint a frame, %ld frames\n", c.name,
           us / (double)frames, worst, strips / (double)frames, px / (double)frames, stat / (double)frames, frames);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "--cost") == 0) return runCost();
  if (argc < 2) {
    fprintf(stderr, "usage: stage15_stills OUT_DIR | --cost\n");
    return 2;
  }
  const char* out = argv[1];
  char path[512];
  snprintf(path, sizeof path, "%s/tiles.txt", out);
  FILE* list = fopen(path, "w");
  if (!list) return 1;

  auto moodAt = [](float lo, float hi) { return [lo, hi](const App*, const World& w) { return w.mood_override >= lo && w.mood_override <= hi; }; };
  const Tile tiles[] = {
      // The sunrise overlook.
      {"sr_arrive", "before dawn: up the trail, headlamp lit", SUNRISE, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "arrive") && w.hiker.visible && w.hiker.feet.x > 150.f; }, {0, 0, 0, 0}, nullptr},
      {"sr_slab", "out along the slab", SUNRISE, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "arrive") && w.hiker.feet.x > w.sr.land.ft.x - 130.f; }, {0, 0, -1, 0}, "the lamp (close-up)"},
      {"sr_pack", "pack down, lamp off", SUNRISE, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "settle") && w.sr.pack_down && w.sr.lamp > 0.5f && w.hiker.target == POSE_STAND; },
       {0, 0, 0, 0}, nullptr},
      {"sr_sit", "sits at the very end, legs over", SUNRISE, 0, false,
       [](const App*, const World& w) { return w.sr.sat_tick != 0 && w.tick > w.sr.sat_tick + 20; }, {0, 0, -1, 0}, "on the ledge (close-up)"},
      {"sr_pour", "pours from the flask", SUNRISE, 0, false, [](const App*, const World& w) { return w.sr.flask > 1.5f && w.sr.steam_t > 0.6f; },
       {0, 0, -1, 0}, "the pour, steam (close-up)"},
      {"sr_blue", "deep blue, the last stars", SUNRISE, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "dawn") && w.mood_override < 0.5f && w.tick > 25 * 50; }, {0, 0, 0, 0}, nullptr},
      {"sr_rose", "rose", SUNRISE, 0, false, [](const App* a, const World& w) { return beatIs(a, "dawn") && w.mood_override >= 0.99f; },
       {0, 0, 0, 0}, nullptr},
      {"sr_break", "the sun breaks over the far ridge", SUNRISE, 0, false,
       [](const App*, const World& w) { return w.sr.broke_tick != 0 && w.tick == w.sr.broke_tick + 40; }, {520, 120, 800, 288}, "the sunbreak (close-up)"},
      {"sr_gold", "gold", SUNRISE, 0, false, [](const App* a, const World& w) { return beatIs(a, "sunbreak") && w.mood_override > 1.2f && w.sr.lit[0] == 2; },
       {0, 0, 0, 0}, nullptr},
      {"sr_light1", "light on the far ridges", SUNRISE, 0, false,
       [](const App*, const World& w) { return w.sr.lit[1] == 2 && w.sr.lit[2] == 0; }, {0, 0, 0, 0}, nullptr},
      {"sr_light2", "down to the near ridge; fog glows", SUNRISE, 0, false,
       [](const App*, const World& w) { return w.sr.fog_glow == 1 && w.sr.slab_lit == 0; }, {0, 0, 0, 0}, nullptr},
      {"sr_hawk", "a hawk below him", SUNRISE, 0, false,
       [](const App*, const World& w) { return w.sr.hawk == 1 && w.amb.bird.active && w.amb.bird.x < 690.f && w.amb.bird.x > 600.f; },
       {440, 250, 800, 466}, "the hawk (close-up)"},
      {"sr_toast", "the cup raised to the sun", SUNRISE, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "light") && w.fx.arm_w[0] > 0.f && w.fx.arm_sh[0] > 2.1f; }, {0, 0, -1, 0},
       "the toast (close-up)"},
      {"sr_thin", "the fog thins", SUNRISE, 0, false, [](const App* a, const World& w) { return w.sr.fog_thin == 3 && beatIs(a, "linger"); },
       {0, 0, 0, 0}, nullptr},
      {"sr_leave", "packs up and walks back", SUNRISE, 0, false,
       [](const App* a, const World& w) { return beatIs(a, "leave") && w.hiker.walking && w.hiker.facing < 0 && w.hiker.feet.x < 360.f; },
       {0, 0, 0, 0}, nullptr},
      {"sr_dog", "Lucy out: she sits with him", SUNRISE, 1, false,
       [](const App* a, const World& w) { return beatIs(a, "pour") && w.tick > w.sr.sat_tick + 200; }, {0, 0, -1, 0}, "with Lucy (close-up)"},
      {"sr_gray_break", "gray, dry morning: muted sunbreak", SUNRISE_GRAY, 0, false,
       [](const App*, const World& w) { return w.sr.broke_tick != 0 && w.tick == w.sr.broke_tick + 120; }, {0, 0, 0, 0}, nullptr},
      {"sr_gray_light", "gray, dry morning: later", SUNRISE_GRAY, 0, false, [](const App*, const World& w) { return w.sr.fog_glow == 2; },
       {0, 0, 0, 0}, nullptr},
      // The visiting couple.
      {"vc_arrive", "the couple come along the trail", MIDDAY, 0, true,
       [](const App*, const World& w) { return stepIs(w, "arrive") && w.vis.st > 3.f; }, {0, 0, 0, 0}, nullptr},
      {"vc_greet", "they wave; he waves them over", MIDDAY, 0, true, [](const App*, const World& w) { return stepIs(w, "greet") && w.vis.st > 1.6f; },
       {0, 0, 0, 0}, nullptr},
      {"vc_fire_talk", "fireside: talk, cups out", EVENING, 0, true, [](const App*, const World& w) { return stepIs(w, "talk") && w.vis.st > 6.f; },
       {0, 0, -2, 0}, "Ann, the hiker, Ben (close-up)"},
      {"vc_fire_story", "Ben tells a story with his hands", EVENING, 0, true,
       [](const App*, const World& w) { return stepIs(w, "story") && w.vis.st > 25.f; }, {0, 0, 0, 0}, nullptr},
      {"vc_fire_laugh", "all three laugh", EVENING, 0, true, [](const App*, const World& w) { return stepIs(w, "laugh") && w.vis.st > 34.5f; },
       {0, 0, -2, 0}, "the laugh (close-up)"},
      {"vc_fire_toast", "a toast", EVENING, 0, true, [](const App*, const World& w) { return stepIs(w, "toast") && w.vis.st > 48.5f; },
       {0, 0, 0, 0}, nullptr},
      {"vc_fris_throw", "frisbee: a throw in the air", MIDDAY, 0, true,
       [](const App*, const World& w) { return stepIs(w, "fly") && w.vis.disc_t > 0.45f * w.vis.disc_dur && w.vis.n == 1; }, {0, 0, 0, 0},
       nullptr},
      {"vc_fris_fumble", "a fumbled catch", MIDDAY, 0, true, [](const App*, const World& w) { return stepIs(w, "fumble") && w.vis.disc_t > 0.25f; },
       {0, 0, -1, 0}, "the fumble (close-up)"},
      {"vc_fris_run", "a long one: Ann runs it down", MIDDAY, 0, true,
       [](const App*, const World& w) { return stepIs(w, "fly") && w.vis.n == 4 && w.vis.disc_t > 0.6f * w.vis.disc_dur; }, {0, 0, 0, 0},
       nullptr},
      {"vc_fris_dive", "Ben dives for it", MIDDAY, 0, true,
       [](const App*, const World& w) { return stepIs(w, "fly") && w.vis.n == 6 && w.vis.disc_t > 0.8f * w.vis.disc_dur; }, {0, 0, 0, 0},
       nullptr},
      {"vc_fris_dog", "Copper takes the disc; the chase", MIDDAY, 2, true,
       [](const App*, const World& w) { return stepIs(w, "dog_off") && w.vis.ft > 0.5f; }, {0, 0, 0, 0}, nullptr},
      {"vc_scope_setup", "night: telescopes going up", NIGHT_MOON, 0, true,
       [](const App*, const World& w) { return stepIs(w, "settle") && w.vis.scope_k[0] > 0.5f; }, {0, 0, 0, 0}, nullptr},
      {"vc_scope_point", "Ann points at the moon", NIGHT_MOON, 0, true,
       [](const App*, const World& w) { return stepIs(w, "look") && w.vis.ft > 3.f; }, {0, 0, -2, 0}, "pointing (close-up)"},
      {"vc_scope_look", "the hiker has a look", NIGHT_MOON, 0, true,
       [](const App*, const World& w) { return stepIs(w, "hero_look") && w.vis.ft > 2.5f; }, {0, 0, 0, 0}, nullptr},
      {"vc_scope_wow", "steps back, impressed", NIGHT_MOON, 0, true,
       [](const App*, const World& w) { return stepIs(w, "impressed") && w.vis.ft > 1.5f; }, {0, 0, 0, 0}, nullptr},
      {"vc_meteor", "no moon: a shooting star", NIGHT_DARK, 0, true,
       [](const App*, const World& w) { return stepIs(w, "meteor") && w.vis.meteor_t > 0.6f; }, {0, 0, 0, 0}, nullptr},
      {"vc_leave", "they wave and walk on", EVENING, 0, true, [](const App*, const World& w) { return stepIs(w, "go") && w.vis.st > 4.f; },
       {0, 0, 0, 0}, nullptr},
      {"vc_closeup_day", "Ann, the hiker and Ben by day", MIDDAY, 0, true,
       [](const App*, const World& w) { return stepIs(w, "settle") && w.vis.st > 0.5f; }, {0, 0, -2, 0}, "the three (close-up)"},
  };
  (void)moodAt;
  int i = 1, missing = 0;
  for (const Tile& t : tiles) {
    if (!shoot(t, out, list, i)) ++missing;
    ++i;
  }
  fclose(list);
  return missing ? 1 : 0;
}
