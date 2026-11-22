// Stills for Mike's stage 22 review: the tai chi major in the real scene (day_taichi.cpp; TAICHI_PLAN
// section 12, step C2). The module alone on flat ground is taichi_stills.cpp (S1, S2, S4, S6).
//   S3   the guests: the crane with the heron mirroring, the single whip with the bird on the hook hand,
//        the rooster with the chipmunk up and the snake with it down, the kick with the deer beyond the
//        pine, the closing with the butterfly on his shoulder (and the butterfly rising out of the grass)
//   S5   the tight seed (smallest camp_x - F.x of the seeds searched): the stillness beside the cairn and the
//        brush knee at the east end against the pine's skirt
//   S7   the gags in the scene: the wobble on the rooster (the chipmunk does it better), the itch in the
//        single whip, Copper's bump
//   S8   Lucy watching; both dogs
//   S9   the doorbell mid-rooster: the bell, +1 s, +2 s, +3.4 s
//   S10  light: thick fog at the start, thinner at the close, a clear morning, a WX_FOG day, a no-show in rain
//   S11  him behind the cattails (a seed with the RS_NEAR clump), and the hat on the grass
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (SS_S22 on):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I core tools/taichi_scene_stills.cpp BUILD/libss_core.a
//     -o BUILD/taichi_scene_stills
//   BUILD/taichi_scene_stills OUT_DIR    writes OUT_DIR/*.ppm (cropped tiles) and OUT_DIR/manifest.txt
//
// taichi_sheet.py turns them into PNG sheets. Manifest lines: group|file|label|x0|y0|x1|y1|scale.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "gfx/raster.h"
#include "palette.h"
#include "scene/day.h"
#include "scene/day_taichi.h"
#include "scene/housedogs.h"
#include "scene/majors.h"
#include "scene/reeds.h"
#include "scene/stream.h"
#include "scene/taichi.h"
#include "scene/weather.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

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

Inputs morning(int chapter, int dog, int code) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 1.5f;
  in.spine_force = (uint8_t)(1 + SF_MORNING);
  in.have_outside = code >= 0;
  in.weather_code = code >= 0 ? code : 0;
  in.dog_out = dog;
  in.month = 6;
  in.carry = (uint16_t)((0x80u | (unsigned)chapter) << 8);
  return in;
}

int taiIndex(const LoopPlan& p) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == MJ_TAICHI) return i;
  return -1;
}

// What a seed must offer besides the chapter, gag and guests.
enum Want { W_ANY = 0, W_TIGHT, W_REEDS };

uint64_t findSeed(int chapter, int gag, int guests, int dog, int code, Want want) {
  uint64_t best = 0;
  float best_v = 1.0e9f;
  for (uint64_t seed = 1; seed <= 400; ++seed) {
    Rig r;
    if (!r.start(seed, morning(chapter, dog, code))) return 0;
    const World* w = appWorld(r.app);
    const int i = taiIndex(w->plan);
    const uint8_t v = i >= 0 ? w->plan.major_var[i] : 0;
    bool hit = i >= 0 && ((v >> 4) & 3) == gag && (guests < 0 || ((v & 64) != 0) == (guests != 0));
    float score = 0.f;
    if (hit && want == W_TIGHT) score = (float)w->land.camp_x - w->fire.pos.x;
    if (hit && want == W_REEDS) {
      ReedClumpInfo ci;
      hit = reedsClump(*w, RS_NEAR, ci) && ci.present;
    }
    r.stop();
    if (!hit) continue;
    if (want != W_TIGHT) return seed;
    if (score < best_v) {
      best_v = score;
      best = seed;
    }
  }
  return best;
}

enum Kind { K_FORM, K_GAG, K_BEAT, K_BELL, K_AFTER };
struct Shot {
  const char* group;
  const char* label;
  Kind kind;
  float at;             // seconds: form time, into the gag, into the beat, after the bell, after the form
  int dx0, dy0, dx1, dy1;   // the crop, relative to the line's start (x0, y0)
  int scale;
  bool done;
};

FILE* g_manifest = nullptr;
int g_n = 0;
const char* g_dir = ".";

void writeTile(const Rig& r, const Shot& s, Vec2 origin, const char* extra) {
  int x0 = (int)origin.x + s.dx0, y0 = (int)origin.y + s.dy0, x1 = (int)origin.x + s.dx1, y1 = (int)origin.y + s.dy1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SS_WIDTH) x1 = SS_WIDTH;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  char name[64], path[600];
  snprintf(name, sizeof name, "sc_%03d.ppm", g_n++);
  snprintf(path, sizeof path, "%s/%s", g_dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  const int w = x1 - x0, h = y1 - y0;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row((size_t)w * 3);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const Pixel p = r.fb[(size_t)y * SS_WIDTH + x];
      row[(size_t)(x - x0) * 3] = red8(p);
      row[(size_t)(x - x0) * 3 + 1] = green8(p);
      row[(size_t)(x - x0) * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  fclose(f);
  fprintf(g_manifest, "%s|%s|%s%s|0|0|%d|%d|%d\n", s.group, name, s.label, extra, w, h, s.scale);
}

struct Cfg {
  int chapter, gag, guests, dog, code;
  Want want;
  int poke_weather;     // a rain the plan could not know of
  bool clear;           // no morning fog at all
  float bell_form_t;    // a doorbell at this form time (< 0 none)
  int bell_key;         // ... counted from this key's arrival (-1: from the form's start)
};

void run(const Cfg& c, std::vector<Shot> shots) {
  const uint64_t seed = findSeed(c.chapter, c.gag, c.guests, c.dog, c.code, c.want);
  if (!seed) {
    printf("no seed for chapter %d gag %d guests %d dog %d\n", c.chapter + 1, c.gag, c.guests, c.dog);
    return;
  }
  Rig r;
  if (!r.start(seed, morning(c.chapter, c.dog, c.code))) return;
  World* w = const_cast<World*>(appWorld(r.app));
  if (c.poke_weather) w->weather.kind = (uint8_t)c.poke_weather;
  if (c.clear) w->plan.fog_steps = 0;
  const uint32_t len = appLoopTicks(r.app);
  bool in_beat = false, belled = false;
  int n_in = 0, bell_ticks = -1, after = -1;
  const float bell_at = c.bell_form_t < 0.f ? -1.f : (c.bell_key >= 0 ? taichiKeyArrive(c.chapter, c.bell_key) : 0.f) + c.bell_form_t;
  char extra[48];
  int left = (int)shots.size();
  for (uint32_t t = 0; t + 1 < len && left > 0; ++t) {
    const TaichiState& s = w->day.taichi.t;
    if (in_beat && !belled && bell_at >= 0.f && taichiInForm(*w) && s.phase == TP_FORM && s.t >= bell_at) {
      Event ev;
      ev.type = EV_DOORBELL;
      ev.arg = 0;
      appPostEvent(r.app, ev);
      belled = true;
      bell_ticks = 0;
    }
    appTick(r.app);
    if (bell_ticks >= 0) ++bell_ticks;
    const bool now = strcmp(appBeatName(r.app), "taichi") == 0;
    if (!now) {
      if (in_beat) break;
      continue;
    }
    in_beat = true;
    ++n_in;
    const bool form = taichiInForm(*w);
    if (w->day.taichi.finished && after < 0) after = 0;
    else if (after >= 0) ++after;
    for (Shot& sh : shots) {
      if (sh.done) continue;
      bool go = false;
      switch (sh.kind) {
        case K_FORM: go = form && s.phase != TP_OFF && s.t >= sh.at && (s.phase == TP_FORM || s.phase == TP_WAIT || s.phase == TP_OUT); break;
        case K_GAG: go = form && s.phase == TP_GAG && s.gag_t >= sh.at; break;
        case K_BEAT: go = (float)n_in >= sh.at * 25.f; break;
        case K_BELL: go = bell_ticks >= 0 && (float)bell_ticks >= sh.at * 25.f + 1.f; break;
        case K_AFTER: go = after >= 0 && (float)after >= sh.at * 25.f; break;
      }
      if (!go) continue;
      sh.done = true;
      --left;
      appInvalidate(r.app);
      appRender(r.app, r.surf);
      snprintf(extra, sizeof extra, " (seed %d)", (int)seed);
      writeTile(r, sh, w->major.p[0], sh.kind == K_BEAT && sh.at < 1.f ? extra : "");
    }
  }
  for (const Shot& sh : shots)
    if (!sh.done) printf("missed: %s %s (seed %d)\n", sh.group, sh.label, (int)seed);
  r.stop();
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
  const auto A = [](int ch, int k) { return taichiKeyArrive(ch, k); };
  // The usual crop round him (the line's start at 0, 0): the bank, the water in front, a little sky.
  const int X0 = -70, Y0 = -130, X1 = 150, Y1 = 80;

  // ---- S3: the guests
  run(Cfg{0, 0, 1, 0, 0, W_ANY, 0, false, -1.f, -1},
      {{"s3", "the heron comes in (ball)", K_FORM, A(0, 3) + 3.6f, X0, Y0, X1, Y1, 2, false},
       {"s3", "crane: the heron lifts a leg and a wing", K_FORM, A(0, 8) + 4.2f, X0, Y0, X1, Y1, 2, false},
       {"s3", "the butterfly rises out of the grass", K_FORM, A(0, 1) + 0.5f, X0, Y0, X1, Y1, 2, false},
       {"s3", "... and follows his hand (mane)", K_FORM, A(0, 4) + 1.0f, X0, Y0, X1, Y1, 2, false},
       {"s3", "closing: the butterfly on his shoulder", K_FORM, A(0, 18) + 3.0f, X0, Y0, X1, Y1, 2, false}});
  run(Cfg{1, 0, 1, 0, 0, W_ANY, 0, false, -1.f, -1},
      {{"s3", "single whip: the bird comes down", K_FORM, A(1, 14) + 2.9f, X0, Y0, X1, Y1, 2, false},
       {"s3", "single whip: the bird on the hook hand", K_FORM, A(1, 14) + 5.2f, X0, Y0, X1, Y1, 2, false}});
  run(Cfg{2, 0, 1, 0, 0, W_ANY, 0, false, -1.f, -1},
      {{"s3", "clouds: the chipmunk sits up", K_FORM, A(2, 9) + 1.0f, X0, Y0, X1, Y1, 2, false},
       {"s3", "snake: the chipmunk drops down", K_FORM, A(2, 15) + 1.8f, X0, Y0, X1, Y1, 2, false},
       {"s3", "rooster: the chipmunk up again", K_FORM, A(2, 17) + 3.0f, X0, Y0, X1, Y1, 2, false}});
  run(Cfg{3, 0, 1, 0, 0, W_ANY, 0, false, -1.f, -1},
      {{"s3w", "kick: the deer beyond the pine, head up", K_FORM, A(3, 5) + 1.6f, -60, -140, 300, 40, 2, false},
       {"s3w", "needle: it comes three steps nearer", K_FORM, A(3, 9) + 4.0f, -60, -140, 300, 40, 2, false},
       {"s3w", "the deer grazes between looks", K_FORM, A(3, 7) + 0.5f, -60, -140, 300, 40, 2, false}});

  // ---- S5: the tight seed
  run(Cfg{0, 0, -1, 0, 0, W_TIGHT, 0, false, -1.f, -1},
      {{"s5", "tight seed: stillness beside the cairn", K_FORM, 3.0f, -90, -150, 190, 60, 2, false},
       {"s5", "tight seed: brush knee by the pine's skirt", K_FORM, A(0, 10) + 1.2f, -90, -150, 190, 60, 2, false},
       {"s5", "tight seed: the crane under the boughs", K_FORM, A(0, 8) + 2.0f, -90, -150, 190, 60, 2, false}});

  // ---- S7: the gags in the scene
  {
    std::vector<Shot> v;
    static const float kT[8] = {0.3f, 1.1f, 1.7f, 2.1f, 2.9f, 4.2f, 5.6f, 7.4f};
    static const char* const kL[8] = {"wobble 0.3 s", "1.1 s too far", "1.7 s arms out", "2.1 s caught", "2.9 s breath out",
                                      "4.2 s hand to his head", "5.6 s sinks", "7.4 s back in the rooster"};
    for (int i = 0; i < 8; ++i) v.push_back(Shot{"s7w", kL[i], K_GAG, kT[i], X0, Y0, X1, Y1, 2, false});
    run(Cfg{2, TG_WOBBLE, 1, 0, 0, W_ANY, 0, false, -1.f, -1}, v);
  }
  {
    std::vector<Shot> v;
    static const float kT[6] = {0.02f, 0.28f, 0.7f, 1.6f, 2.8f, 4.6f};
    static const char* const kL[6] = {"itch: still", "0.28 s slap", "0.7 s held", "1.6 s guilty", "2.8 s back along the path", "4.6 s whip again"};
    for (int i = 0; i < 6; ++i) v.push_back(Shot{"s7i", kL[i], K_GAG, kT[i], X0, Y0, X1, Y1, 2, false});
    run(Cfg{1, TG_ITCH, 1, 0, 0, W_ANY, 0, false, -1.f, -1}, v);
  }
  {
    std::vector<Shot> v;
    v.push_back(Shot{"s7b", "bump: Copper on his way", K_FORM, A(0, 10) - 1.2f, -120, Y0, X1, Y1, 2, false});
    static const float kT[9] = {0.08f, 0.5f, 1.0f, 1.8f, 2.6f, 3.6f, 4.6f, 6.0f, 8.0f};
    static const char* const kL[9] = {"0.1 s shouldered", "0.5 s stumble", "1.0 s", "1.8 s catches it", "2.6 s looks down", "3.6 s the pat",
                                      "4.6 s", "6.0 s straightens", "8.0 s back into the form"};
    for (int i = 0; i < 9; ++i) v.push_back(Shot{"s7b", kL[i], K_GAG, kT[i], -120, Y0, X1, Y1, 2, false});
    run(Cfg{0, TG_BUMP, -1, 2, 0, W_ANY, 0, false, -1.f, -1}, v);
  }

  // ---- S8: Lucy watching; both dogs
  run(Cfg{0, 0, -1, 1, 0, W_ANY, 0, false, -1.f, -1},
      {{"s8", "Lucy trots over as he lays his hat down", K_BEAT, 9.0f, -160, Y0, X1, Y1, 2, false},
       {"s8", "Lucy lies and watches (mane)", K_FORM, A(0, 4) + 1.0f, -160, Y0, X1, Y1, 2, false},
       {"s8", "Lucy watches the crane", K_FORM, A(0, 8) + 2.5f, -160, Y0, X1, Y1, 2, false}});
  run(Cfg{2, 0, -1, 3, 0, W_ANY, 0, false, -1.f, -1},
      {{"s8", "both dogs out: Lucy watches, Copper by the tent", K_FORM, A(2, 17) + 2.5f, -330, Y0, X1, Y1, 2, false}});

  // ---- S9: the doorbell mid-rooster
  run(Cfg{2, 0, 1, 0, 0, W_ANY, 0, false, 1.0f, 17},
      {{"s9", "rooster: the bell", K_BELL, 0.0f, X0, Y0, X1, Y1, 2, false},
       {"s9", "+1 s: only his head lifts", K_BELL, 1.0f, X0, Y0, X1, Y1, 2, false},
       {"s9", "+2 s", K_BELL, 2.0f, X0, Y0, X1, Y1, 2, false},
       {"s9", "+3.4 s: he goes on", K_BELL, 3.4f, X0, Y0, X1, Y1, 2, false}});

  // ---- S10: light
  run(Cfg{0, 0, 1, 0, 0, W_ANY, 0, false, -1.f, -1},
      {{"s10", "thick fog: commencing", K_FORM, A(0, 1) + 0.6f, -200, -200, 260, 90, 1, false},
       {"s10", "thinner: the closing", K_FORM, A(0, 18) + 2.0f, -200, -200, 260, 90, 1, false}});
  run(Cfg{0, 0, 1, 0, 0, W_ANY, 0, true, -1.f, -1}, {{"s10", "a clear morning: the crane", K_FORM, A(0, 8) + 2.0f, -200, -200, 260, 90, 1, false}});
  run(Cfg{0, 0, 1, 0, 5, W_ANY, 0, false, -1.f, -1}, {{"s10", "a fog day (WX_FOG): the crane", K_FORM, A(0, 8) + 2.0f, -200, -200, 260, 90, 1, false}});
  run(Cfg{0, 0, -1, 0, -1, W_ANY, WX_RAIN, false, -1.f, -1},
      {{"s10", "no-show in rain: a hand out", K_BEAT, 3.2f, -200, -200, 260, 90, 1, false},
       {"s10", "no-show: he sits it out", K_BEAT, 40.0f, -200, -200, 260, 90, 1, false}});

  // ---- S11: him behind the cattails; the hat on the grass
  run(Cfg{1, 0, 1, 0, 0, W_REEDS, 0, false, -1.f, -1},
      {{"s11", "the hat comes off", K_BEAT, 9.4f, -110, Y0, 170, 100, 2, false},
       {"s11", "behind the cattails: brush knee", K_FORM, A(1, 4) + 1.0f, -110, Y0, 170, 100, 2, false},
       {"s11", "behind the cattails: the push (facing west)", K_FORM, A(1, 12) + 1.0f, -110, Y0, 170, 100, 2, false},
       {"s11", "the hat again", K_AFTER, 6.4f, -110, Y0, 170, 100, 2, false}});

  fclose(g_manifest);
  printf("wrote %d tiles to %s\n", g_n, g_dir);
  return 0;
}
