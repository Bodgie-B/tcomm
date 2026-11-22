// Stills for Mike's stage 22 review: the reeds and cattails along the stream (scene/reeds.h, step B1, the
// plants). CATTAILS_PLAN.md section 11, stills 1 to 5 and 8:
//   S  the whole scene with the reeds: three layouts (seed 1, one with RS_FAR and RS_FIRESIDE, one without
//      them) in the morning, at midday, in the evening and at night;
//   C  each clump close up, by day and by night;
//   M  the seasons: the four left clumps for months 0, 4, 6, 8, 9, 10, 11, 1, and 1 in snow;
//   W  the wind: calm, the leading edge of a gust (leaves gone over, heads not yet), the full gust, the
//      swing back; and a filmstrip of a gust going through, 0.24 s a frame;
//   B  before and after, round the fire: the same frame with and without the piece;
//   N  neighbours: the heron by RS_NEAR, Lucy passing the clumps, the plinking shooter by RS_FORE, the
//      bather and the fisherman by RS_FIRESIDE (each only if a seed in the search plays it).
// Step C1 (stills 6, 7, 9 and 10 of the plan; build with -DSS_S22=1 against build-s22-on, as the pick and the
// blackbird wait for the switch):
//   P  the pick: seated, 8 frames; the stroll, 6 frames;
//   T  the stem in his mouth in each posture (posed by hand on a calm frame), prone as the long shot's bend lays
//      him (the rifle itself is step C3's), wilted, walking, the doorbell wave, the toss;
//   L  the small life: the dragonfly hovering and perched, fireflies at dusk, fluff on a November gust, the
//      red-winged blackbird landing, riding, leaving;
//   K  the chip's corner with both dog badges.
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (the same
// -DSS_XMAS and -DSS_S22 as the library):
//
//   c++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -ffp-contract=off -I core tools/reeds_stills.cpp
//       BUILD/libss_core.a -o BUILD/reeds_stills
//   BUILD/reeds_stills OUT_DIR    writes OUT_DIR/<sheet>_NN_name.ppm (800 x 480) and OUT_DIR/manifest.txt
//
// reeds_sheet.py turns the PPMs and the manifest (file|label|x0|y0|x1|y1|scale: the crop and its enlargement)
// into PNG sheets with zlib alone.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gfx/raster.h"
#include "ss_app.h"
#include "scene/hiker.h"
#include "scene/landscape.h"
#include "scene/plink.h"
#include "scene/world.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

const char* g_out = ".";
FILE* g_manifest = nullptr;

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

struct Look {
  int family;      // SpineFamily
  float mood;
  bool night;
  int month;
  int weather;     // Inputs::weather_code, with have_outside
  int dog;
  bool chip = false;   // the status chip drawn (Inputs::chip)
};

Inputs inputsFor(const Look& l) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = l.mood;
  in.have_outside = true;
  in.weather_code = l.weather;
  in.spine_force = (uint8_t)(1 + l.family);
  in.night_mode = l.night;
  in.weather.night_dim = l.night ? 1.f : 0.f;
  in.month = (uint8_t)l.month;
  in.dog_out = l.dog;
  in.chip = l.chip;
  return in;
}

struct Run {
  void* mem = nullptr;
  std::vector<Pixel> fb;
  Surface surf{};
  App* app = nullptr;
  int tick = 0;
  bool start(uint64_t seed, const Look& l) {
    mem = calloc(1, SS_APP_BYTES);
    fb.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
    surf = Surface{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = appStart(mem, kAlloc, seed, inputsFor(l));
    tick = 0;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    app = nullptr;
  }
  World& w() { return const_cast<World&>(*appWorld(app)); }
  void step(int n, bool render = false) {
    for (int i = 0; i < n; ++i) {
      appTick(app);
      if (render) appRender(app, surf);
      ++tick;
    }
  }
  void shot(const char* name, const char* label, int x0, int y0, int x1, int y1, int scale) {
    appInvalidate(app);
    appRender(app, surf);
    char path[700];
    snprintf(path, sizeof path, "%s/%s.ppm", g_out, name);
    writePPM(path, fb.data());
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SS_WIDTH) x1 = SS_WIDTH;
    if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
    fprintf(g_manifest, "%s.ppm|%s|%d|%d|%d|%d|%d\n", name, label, x0, y0, x1, y1, scale);
    printf("  %-28s %s\n", name, label);
  }
};

// The pieces of the scene with no reeds at all: every clump away and the fringe gone (for "before").
void hideReeds(World& w) {
  for (ReedSite& s : w.reeds.site) s.flags = 0;
  for (int i = 0; i + 1 < LS_REED_FRINGE; ++i) w.land.reed_fringe[i] = 0;
}

bool has(const World& w, int c) { return (w.reeds.site[c].flags & 1u) != 0; }

// Seeds whose first loop has the clumps asked for (bit c in want: present; in not: absent).
uint64_t findLayout(unsigned want, unsigned not_, uint64_t from) {
  const Look l{SF_MIDDAY, 3.f, false, 0, 0, 0};
  for (uint64_t seed = from; seed < from + 400; ++seed) {
    Run r;
    r.start(seed, l);
    bool ok = true;
    for (int c = 0; c < REED_CLUMPS; ++c) {
      if ((want >> c) & 1u) ok = ok && has(r.w(), c);
      if ((not_ >> c) & 1u) ok = ok && !has(r.w(), c);
    }
    r.stop();
    if (ok) return seed;
  }
  return 0;
}

// ---- S: the scene ----
void sheetScene() {
  const uint64_t seeds[3] = {1, findLayout(1u << RS_FAR | 1u << RS_FIRESIDE | 1u << RS_NEAR, 0, 2),
                             findLayout(0, 1u << RS_FAR | 1u << RS_FIRESIDE, 2)};
  const Look looks[4] = {{SF_MORNING, 1.6f, false, 0, 0, 0}, {SF_MIDDAY, 3.f, false, 0, 0, 0},
                         {SF_EVENING, 4.4f, false, 0, 0, 0}, {SF_EVENING, 0.f, true, 0, 0, 0}};
  const char* when[4] = {"morning", "midday", "evening", "night"};
  int n = 0;
  for (int i = 0; i < 3; ++i) {
    if (!seeds[i]) continue;
    for (int k = 0; k < 4; ++k) {
      Run r;
      r.start(seeds[i], looks[k]);
      r.step(25 * 50);
      char name[64], label[200];
      const World& w = r.w();
      snprintf(name, sizeof name, "S_%02d_seed%llu_%s", n++, (unsigned long long)seeds[i], when[k]);
      snprintf(label, sizeof label, "seed %llu, %s: clumps %s%s%s%s%s", (unsigned long long)seeds[i], when[k],
               has(w, 0) ? "F" : "-", has(w, 1) ? "N" : "-", has(w, 2) ? "U" : "-", has(w, 3) ? "S" : "-",
               has(w, 4) ? "R" : "-");
      r.shot(name, label, 0, 0, 800, 480, 1);
      r.stop();
    }
  }
}

Rect clumpCrop(const World& w, int c, int pad) {
  Rect b = reedsBounds(w, c);
  return Rect{b.x0 - pad, b.y0 - pad, b.x1 + pad, b.y1 + pad};
}

// ---- C: each clump close up, day and night ----
void sheetClumps() {
  const uint64_t seed = findLayout(0x1Fu, 0, 1);
  const Look looks[2] = {{SF_MIDDAY, 3.f, false, 0, 0, 0}, {SF_EVENING, 0.f, true, 0, 0, 0}};
  const char* nm[5] = {"fore", "near", "upstream", "fireside", "far"};
  int n = 0;
  for (int k = 0; k < 2; ++k) {
    Run r;
    r.start(seed, looks[k]);
    r.step(25 * 50);
    for (int c = 0; c < REED_CLUMPS; ++c) {
      if (!has(r.w(), c)) continue;
      const Rect b = clumpCrop(r.w(), c, 8);
      char name[64], label[160];
      snprintf(name, sizeof name, "C_%02d_%s_%s", n++, nm[c], k ? "night" : "day");
      snprintf(label, sizeof label, "%s, %s (seed %llu), 3x", nm[c], k ? "night" : "day", (unsigned long long)seed);
      r.shot(name, label, b.x0, b.y0, b.x1, b.y1, 3);
    }
    r.stop();
  }
}

// ---- M: the seasons ----
void sheetSeasons() {
  const uint64_t seed = findLayout(1u << RS_FORE | 1u << RS_NEAR | 1u << RS_UPSTREAM | 1u << RS_FIRESIDE, 0, 1);
  const int months[9] = {0, 4, 6, 8, 9, 10, 11, 1, 1};
  const char* names[9] = {"month 0 (no clock): late summer", "April: new growth, last year's stalk",
                          "June: slim green heads", "August", "September: turning",
                          "October: bursting heads, a bent blade", "November: fluff, straw",
                          "January: straw, ragged heads", "January in snow: caps"};
  for (int i = 0; i < 9; ++i) {
    const bool snow = i == 8;
    Run r;
    r.start(seed, Look{SF_MIDDAY, 3.f, false, months[i], snow ? 2 : 0, 0});
    r.step(snow ? 25 * 120 : 25 * 45);
    const World& w = r.w();
    const int x0 = w.reeds.site[RS_FORE].x - 40, x1 = w.reeds.site[RS_UPSTREAM].x + 40;
    char name[64], label[200];
    snprintf(name, sizeof name, "M_%02d_month%d%s", i, months[i], snow ? "_snow" : "");
    snprintf(label, sizeof label, "%s%s", names[i], snow ? "" : "");
    r.shot(name, label, x0, 340, x1, 480, 2);
    r.stop();
  }
}

// ---- W: the wind ----
void sheetWind() {
  const uint64_t seed = findLayout(1u << RS_FORE | 1u << RS_UPSTREAM | 1u << RS_NEAR, 0, 1);
  const Look l{SF_MIDDAY, 3.f, false, 8, 3, 0};
  // Pass 1: find the moments in a windy loop (the leaves' and heads' quantized values at RS_UPSTREAM).
  int t_calm = -1, t_lead = -1, t_full = -1, t_back = -1;
  {
    Run r;
    r.start(seed, l);
    int best_lead = 0, best_full = 0, best_back = 0;
    float calm = 1e9f;
    for (int t = 0; t < 25 * 400; ++t) {
      r.step(1);
      if (t < 25 * 20) continue;
      const World& w = r.w();
      const int q0 = w.reeds.sway_q[RS_UPSTREAM][0], q1 = w.reeds.sway_q[RS_UPSTREAM][1];
      const float g = w.weather.gust;
      // Nobody walking near it: a brush would pass for wind.
      const ReedSite& us = w.reeds.site[RS_UPSTREAM];
      const bool quiet = !w.fx.on || w.hiker.feet.x < (float)us.x - 70.f || w.hiker.feet.x > (float)us.x + 70.f;
      if (!quiet) continue;
      if (g < 0.03f && (float)(q0 * q0 + q1 * q1) < calm) { calm = (float)(q0 * q0 + q1 * q1); t_calm = t; }
      if (q1 - q0 > best_lead && g > 0.2f) { best_lead = q1 - q0; t_lead = t; }   // leaves far left, heads behind
      if (g > 0.6f && -q1 > best_full) { best_full = -q1; t_full = t; }
      if (t_full > 0 && t > t_full && t < t_full + 50 && q1 - q0 > best_back) { best_back = q1 - q0; t_back = t; }
    }
    r.stop();
  }
  const int ts[4] = {t_calm, t_lead, t_full, t_back};
  const char* nm[4] = {"calm", "gust_leading_edge", "full_gust", "swing_back"};
  const char* lb[4] = {"calm air", "a gust's leading edge: leaves over, heads not yet", "the full gust",
                       "the swing back after it"};
  int n = 0;
  for (int k = 0; k < 4; ++k) {
    if (ts[k] < 0) continue;
    Run r;
    r.start(seed, l);
    r.step(ts[k] + 1);
    const World& w = r.w();
    char name[64], label[200];
    const Rect b = clumpCrop(w, RS_UPSTREAM, 6);
    snprintf(name, sizeof name, "W_%02d_%s_up", n++, nm[k]);
    snprintf(label, sizeof label, "RS_UPSTREAM, %s (gust %.2f)", lb[k], (double)w.weather.gust);
    r.shot(name, label, b.x0, b.y0, b.x1, b.y1, 3);
    const Rect f = clumpCrop(w, RS_FORE, 4);
    snprintf(name, sizeof name, "W_%02d_%s_fore", n++, nm[k]);
    snprintf(label, sizeof label, "RS_FORE, %s", lb[k]);
    r.shot(name, label, f.x0, f.y0, f.x1, f.y1, 3);
    r.stop();
  }
  // The filmstrip: 12 frames 6 ticks apart from a little before the full gust.
  if (t_full > 60) {
    Run r;
    r.start(seed, l);
    r.step(t_full - 36);
    for (int i = 0; i < 12; ++i) {
      const World& w = r.w();
      const Rect b = Rect{w.reeds.site[RS_FORE].x - 50, 400, w.reeds.site[RS_UPSTREAM].x + 40, 480};
      char name[64], label[200];
      snprintf(name, sizeof name, "F_%02d_gust", i);
      snprintf(label, sizeof label, "gust filmstrip, +%.2f s", (double)(i * 6) * (double)SS_TICK_DT);
      r.shot(name, label, b.x0, 340, b.x1, b.y1, 2);
      r.step(6);
    }
    r.stop();
  }
  // And calm air, 12 frames 10 ticks apart: how the plain breeze moves them.
  {
    Run r;
    r.start(seed, Look{SF_MIDDAY, 3.f, false, 8, 0, 0});
    r.step(25 * 40);
    for (int i = 0; i < 12; ++i) {
      const World& w = r.w();
      const Rect b = Rect{w.reeds.site[RS_FORE].x - 50, 400, w.reeds.site[RS_UPSTREAM].x + 40, 480};
      char name[64], label[200];
      snprintf(name, sizeof name, "G_%02d_calm", i);
      snprintf(label, sizeof label, "calm filmstrip, +%.1f s", (double)(i * 10) * (double)SS_TICK_DT);
      r.shot(name, label, b.x0, 340, b.x1, b.y1, 2);
      r.step(10);
    }
    r.stop();
  }
}

// ---- B: before and after round the fire ----
void sheetBefore() {
  const uint64_t seeds[2] = {1, findLayout(1u << RS_FIRESIDE | 1u << RS_FORE | 1u << RS_NEAR, 0, 2)};
  int n = 0;
  for (uint64_t seed : seeds) {
    if (!seed) continue;
    const Look l{SF_EVENING, 4.4f, false, 0, 0, 0};
    Run r;
    r.start(seed, l);
    // He sits by the lit fire, settled: wait for it through the loop (or settle for 9 minutes in).
    bool seated = false;
    for (int t = 0; t < 25 * 560 && !seated; ++t) {
      r.step(1);
      const World& w = r.w();
      const Vec2 s = w.spine.camp.sit;
      const float dx = w.hiker.feet.x - s.x, dy = w.hiker.feet.y - s.y;
      seated = t > 25 * 60 && w.fire.state == FIRE_LIT && w.fx.on && w.hiker.visible && w.hiker.target == POSE_SIT &&
               !w.hiker.walking && !w.hiker.blending && dx * dx + dy * dy < 12.f * 12.f;
    }
    if (!seated) printf("  seed %llu: he never sat settled by the lit fire\n", (unsigned long long)seed);
    const World& w = r.w();
    const int fx = (int)w.fire.pos.x;
    char name[64], label[200];
    snprintf(name, sizeof name, "B_%02d_after_seed%llu", n++, (unsigned long long)seed);
    snprintf(label, sizeof label, "AFTER, seed %llu: round the fire, with the reeds", (unsigned long long)seed);
    r.shot(name, label, fx - 100, 330, fx + 300, 480, 2);
    hideReeds(r.w());
    snprintf(name, sizeof name, "B_%02d_before_seed%llu", n++, (unsigned long long)seed);
    snprintf(label, sizeof label, "BEFORE, seed %llu: the same frame without them", (unsigned long long)seed);
    r.shot(name, label, fx - 100, 330, fx + 300, 480, 2);
    r.stop();
  }
}

// ---- N: neighbours ----
uint64_t findMajor(int id, const Look& l) {
  for (uint64_t seed = 1; seed <= 600; ++seed) {
    Run r;
    r.start(seed, l);
    const World& w = r.w();
    bool ok = w.weather.kind == WX_CLEAR;
    bool hit = false;
    for (int i = 0; i < w.plan.n_majors; ++i) hit = hit || w.plan.major[i] == id;
    r.stop();
    if (ok && hit) return seed;
  }
  return 0;
}

void majorShots(int id, const Look& l, const char* tag, const char* what, int c) {
  const uint64_t seed = findMajor(id, l);
  if (!seed) {
    printf("  no seed for %s\n", tag);
    return;
  }
  Run r;
  r.start(seed, l);
  int start = -1, shots = 0;
  for (int t = 0; t < 25 * 700 && shots < 3; ++t) {
    r.step(1);
    const World& w = r.w();
    const bool on = w.major.on && w.major.id == id;
    if (on && start < 0) start = t;
    if (start >= 0 && !on) break;
    if (start < 0) continue;
    const int at[3] = {25 * 15, 25 * 35, 25 * 55};
    if (t - start == at[shots]) {
      char name[64], label[200];
      snprintf(name, sizeof name, "N_%s_%d", tag, shots);
      snprintf(label, sizeof label, "%s (seed %llu, %d s in)", what, (unsigned long long)seed, (t - start) / 25);
      const Rect b = has(w, c) ? clumpCrop(w, c, 60) : Rect{(int)w.fire.pos.x - 80, 330, (int)w.fire.pos.x + 220, 480};
      r.shot(name, label, b.x0, b.y0 - 40, b.x1, b.y1, 2);
      ++shots;
    }
  }
  r.stop();
}

void sheetNeighbours() {
  {   // the heron in the near shallows by RS_NEAR. A forced heron still waits for a quiet moment in its window
      // and may find none (test_stream says so), so try seeds with RS_NEAR until one stands.
    uint64_t seed = 0;
    for (int tries = 0; tries < 12; ++tries) {
      seed = findLayout(1u << RS_NEAR, 0, seed + 1);
      if (!seed) break;
      Run r;
      r.start(seed, Look{SF_MORNING, 1.6f, false, 0, 0, 0});
      r.w().stream.visit.force = SV_HERON;
      for (int t = 0; t < 25 * 400; ++t) {
        r.step(1);
        const StreamVisit& V = r.w().stream.visit;
        if ((V.stage == GS_STAY && V.t > 4.f) || V.stage == GS_DONE) break;
      }
      const StreamVisit& V = r.w().stream.visit;
      if (V.stage == GS_STAY) {
        const Rect b = clumpCrop(r.w(), RS_NEAR, 40);
        const int hx = (int)V.spot.x;
        char label[160];
        snprintf(label, sizeof label, "the heron standing in the shallows by RS_NEAR (seed %llu)", (unsigned long long)seed);
        r.shot("N_heron", label, (hx < b.x0 ? hx : b.x0) - 50, b.y0 - 70, (hx > b.x1 ? hx : b.x1) + 50, b.y1, 2);
        r.stop();
        break;
      }
      r.stop();
      if (tries == 11) printf("  no heron stood in 12 seeds\n");
    }
  }
  {   // Lucy out in camp, passing a clump
    const uint64_t seed = findLayout(1u << RS_FIRESIDE | 1u << RS_UPSTREAM, 0, 1);
    Run r;
    r.start(seed, Look{SF_EVENING, 4.f, false, 0, 0, 1});
    int got = 0;
    for (int t = 0; t < 25 * 600 && got < 2; ++t) {
      r.step(1);
      const World& w = r.w();
      for (int i = CRITTER_STORY; i < CRITTER_STORY + 2 && got < 2; ++i) {
        if (!w.crit[i].in_use || !w.critters[i].visible) continue;
        const int c = got == 0 ? RS_FIRESIDE : RS_UPSTREAM;
        const ReedSite& s = w.reeds.site[c];
        const float dx = w.critters[i].feet.x - (float)s.x, dy = w.critters[i].feet.y - (float)s.y;
        if (dx * dx + dy * dy < 22.f * 22.f) {
          const Rect b = clumpCrop(w, c, 50);
          char name[64];
          snprintf(name, sizeof name, "N_dog_%d", got);
          r.shot(name, got == 0 ? "Lucy by RS_FIRESIDE" : "Lucy by RS_UPSTREAM", b.x0, b.y0 - 30, b.x1, b.y1, 2);
          ++got;
          r.step(25 * 20);
        }
      }
    }
    if (!got) printf("  Lucy never passed a clump\n");
    r.stop();
  }
  majorShots(MJ_PLINK, Look{SF_MIDDAY, 3.f, false, 0, 0, 0}, "plink", "the plinking beside RS_FORE", RS_FORE);
  majorShots(MJ_BATHE, Look{SF_MIDDAY, 3.f, false, 0, 0, 0}, "bathe", "the bathe by RS_FIRESIDE", RS_FIRESIDE);
  majorShots(MJ_FISHING_GOOD, Look{SF_EVENING, 4.f, false, 0, 0, 0}, "fishing", "fishing_good beside RS_FIRESIDE",
             RS_FIRESIDE);
}

// ---- step C1: the pick, the stem, the small life, the blackbird, the chip's corner ----
// (built with -DSS_S22=1 against build-s22-on: the pick and the blackbird wait for the switch)

Rect around(const World& w, int c, int pad_x, int pad_up, int pad_down) {
  Rect h = w.hiker_rect;
  if (c >= 0 && has(w, c)) {
    const Rect b = reedsBounds(w, c);
    h = Rect{h.x0 < b.x0 ? h.x0 : b.x0, h.y0 < b.y0 ? h.y0 : b.y0, h.x1 > b.x1 ? h.x1 : b.x1, h.y1 > b.y1 ? h.y1 : b.y1};
  }
  return Rect{h.x0 - pad_x, h.y0 - pad_up, h.x1 + pad_x, h.y1 + pad_down};
}

// A loop where a forced pick of `form` starts (1 seated, 2 the stroll); the stroll by standing him 40 px from
// RS_UPSTREAM's stand point at a long pause (the scene rarely puts him there by itself).
bool startPick(Run& r, uint64_t& seed, int form) {
  for (int tries = 0; tries < 60; ++tries, ++seed) {
    r.start(seed, Look{SF_MIDDAY, 3.f, false, 7, 0, 0});
    World& w = r.w();
    if ((form == 1 && !has(w, RS_FIRESIDE)) || (form == 2 && !has(w, RS_UPSTREAM))) {
      r.stop();
      continue;
    }
    w.reeds.want_pick = 1;
    w.reeds.pick_after = 0;
    if (form == 2) w.reeds.site[RS_FIRESIDE].flags = 0;
    for (int t = 0; t < 25 * 600; ++t) {
      r.step(1);
      if (form == 2 && w.reeds.pick == 0 && strcmp(appBeatName(r.app), "pause") == 0 && w.minors.beat_left > 16.f &&
          w.fx.on && !w.hiker.walking && !w.hiker.blending) {
        Vec2 st;
        float cy, half;
        if (reedsStandPoint(w, RS_UPSTREAM, -1, st) && landscapeStreamSpan(w.land, st.x - 40.f, cy, half)) {
          w.hiker.feet = Vec2{st.x - 40.f, cy - half - 8.f};
          w.fx.path_on = false;
          w.reeds.want_pick = 1;
          w.reeds.pick_after = 0;
        }
      }
      if (w.reeds.pick == (form == 1 ? 1 : 3)) return true;
    }
    r.stop();
  }
  return false;
}

// ---- P: the pick, seated (8 frames) and the stroll (6 frames) ----
void sheetPick() {
  uint64_t seed = 1;
  Run r;
  if (startPick(r, seed, 1)) {
    static const float at[8] = {0.6f, 1.7f, 2.3f, 2.85f, 3.15f, 4.35f, 5.0f, 6.0f};
    static const char* what[8] = {"the look: his head lifts", "the reach back behind his hip", "his hand closes on it",
                                  "the stretch", "it slips out: the stub rings", "he looks at it", "up to his mouth",
                                  "at rest with it"};
    const Rect crop = around(r.w(), RS_FIRESIDE, 22, 18, 6);
    for (int i = 0; i < 8; ++i) {
      while ((float)r.w().reeds.pick_t / 25.f < at[i] && r.w().reeds.pick != 0) r.step(1);
      char name[64], label[200];
      snprintf(name, sizeof name, "P_%02d_seated", i);
      snprintf(label, sizeof label, "seated pick %.1f s: %s (seed %llu), 3x", (double)at[i], what[i], (unsigned long long)seed);
      r.shot(name, label, crop.x0, crop.y0, crop.x1, crop.y1, 3);
    }
    r.stop();
  } else {
    printf("  no seated pick found\n");
  }
  seed = 1;
  Run q;
  if (startPick(q, seed, 2)) {
    struct Want { uint8_t stage; float t; const char* what; };
    static const Want want[6] = {{4, 0.8f, "he walks to the clump"},   {5, 0.4f, "faces it, looks"},
                                 {6, 1.3f, "crouched: the reach"},     {6, 2.1f, "the pull"},
                                 {7, 0.6f, "standing: it at his chest"}, {7, 2.2f, "in his mouth, looking out"}};
    for (int i = 0; i < 6; ++i) {
      for (int t = 0; t < 25 * 20; ++t) {
        const Reeds& R = q.w().reeds;
        if (R.pick == want[i].stage && (float)R.pick_t / 25.f >= want[i].t) break;
        if (R.pick == 0) break;
        q.step(1);
      }
      const Rect crop = around(q.w(), RS_UPSTREAM, 24, 18, 6);
      char name[64], label[200];
      snprintf(name, sizeof name, "P_%02d_stroll", 8 + i);
      snprintf(label, sizeof label, "stroll pick: %s (seed %llu), 3x", want[i].what, (unsigned long long)seed);
      q.shot(name, label, crop.x0, crop.y0, crop.x1, crop.y1, 3);
    }
    q.stop();
  } else {
    printf("  no stroll pick found\n");
  }
}

// ---- T: the stem in his mouth, in each posture ----
void stemShot(Run& r, const char* name, const char* label) {
  const Vec2 m = hikerMouthPos(r.w().hiker);
  r.shot(name, label, (int)m.x - 34, (int)m.y - 30, (int)m.x + 34, (int)m.y + 22, 4);
}

void sheetStem() {
  {   // by the fire at night: the stem as a silhouette
    Run n;
    n.start(1, Look{SF_EVENING, 0.f, true, 7, 0, 0});
    n.step(25 * 90);
    World& w = n.w();
    hikerSetPose(w.hiker, POSE_SIT, 0.f);
    hikerTick(w.hiker, 0.f);
    w.reeds.chew = 2;
    w.reeds.chew_until = storyTick(w) + 25u * 200u;
    stemShot(n, "T_00_night", "the stem: sitting by the fire at night, 4x");
    w.reeds.chew = 0;
    n.stop();
  }
  Run r;
  r.start(1, Look{SF_MIDDAY, 3.f, false, 7, 0, 0});
  r.step(25 * 90);
  World& w = r.w();
  // Posed by hand on a calm frame: each pose, both facings, the stem given (the scene's own turns, roll and chew).
  struct P { int pose; int facing; const char* what; };
  static const P poses[] = {{POSE_SIT, -1, "sitting, facing left"}, {POSE_SIT, 1, "sitting, facing right"},
                            {POSE_STAND, 1, "standing"},         {POSE_CROUCH, -1, "crouched"},
                            {POSE_LIE, 1, "the nap, on his back"}, {POSE_READ, -1, "reading"},
                            {POSE_FISH_SIT, 1, "fishing, sitting"}, {POSE_FISH_STAND, -1, "fishing, standing"},
                            {POSE_HOLD_MUG, 1, "holding the mug"}, {POSE_LOOK_AROUND, 1, "looking round"},
                            {POSE_HANDS_ON_HIPS, -1, "hands on hips"}, {POSE_PERCH, 1, "perched"}};
  const Hiker keep = w.hiker;
  int n = 1;
  for (const P& p : poses) {
    w.hiker = keep;
    w.hiker.facing = p.facing;
    hikerSetPose(w.hiker, (PoseId)p.pose, 0.f);
    hikerTick(w.hiker, 0.f);
    w.reeds.chew = 2;
    w.reeds.chew_until = storyTick(w) + 25u * 200u;
    char name[64], label[160];
    snprintf(name, sizeof name, "T_%02d_pose%d", n++, p.pose);
    snprintf(label, sizeof label, "the stem: %s, 4x", p.what);
    stemShot(r, name, label);
  }
  {   // prone, as the long shot will lay him (plinkBend's prone shouldered pose; the rifle is step C3's, not drawn)
    w.hiker = keep;
    w.hiker.facing = -1;
    hikerSetPose(w.hiker, POSE_STAND, 0.f);
    hikerTick(w.hiker, 0.f);
    PlinkState ps;
    memset(static_cast<void*>(&ps), 0, sizeof ps);
    ps.rest = REST_PRONE;
    ps.kneel = 1.f;
    ps.up = 1.f;
    plinkBend(ps, w.hiker.pose);
    w.reeds.chew = 2;
    char name[64];
    snprintf(name, sizeof name, "T_%02d_prone", n++);
    stemShot(r, name, "the stem prone at the rifle (plinkBend's pose; rifle not drawn until C3), 4x");
  }
  {   // wilted: its last seconds
    w.hiker = keep;
    hikerSetPose(w.hiker, POSE_SIT, 0.f);
    hikerTick(w.hiker, 0.f);
    w.reeds.chew = 2;
    w.reeds.chew_until = storyTick(w) + 25u * 2u;
    char name[64];
    snprintf(name, sizeof name, "T_%02d_wilted", n++);
    stemShot(r, name, "wilted: the outer third droops, the plume gone, 4x");
  }
  w.hiker = keep;
  w.reeds.chew = 0;
  r.stop();
  {   // walking with it, and the doorbell wave with it
    Run q;
    q.start(7, Look{SF_MIDDAY, 3.f, false, 7, 0, 0});
    for (int t = 0; t < 25 * 600; ++t) {   // a walk in a beat that leaves his mouth alone
      q.step(1);
      if (t < 25 * 40 || !q.w().hiker.walking || !q.w().fx.on || !q.w().hiker.visible) continue;
      reedsGiveChew(q.w(), 200.f);
      q.step(6);
      if (reedsChewing(q.w()) && q.w().hiker.walking) break;
    }
    char name[64];
    snprintf(name, sizeof name, "T_%02d_walking", n++);
    stemShot(q, name, "walking, the bob a little behind his steps, 4x");
    for (int t = 0; t < 25 * 120 && (q.w().hiker.walking || !q.w().hiker.visible); ++t) q.step(1);
    reedsGiveChew(q.w(), 200.f);
    Event e;
    e.type = EV_DOORBELL;
    e.arg = 0;
    appPostEvent(q.app, e);
    q.step(25);
    snprintf(name, sizeof name, "T_%02d_doorbell", n++);
    stemShot(q, name, q.w().react.active ? "the doorbell wave, the stem kept, 4x" : "after the bell (he could not wave), 4x");
    q.stop();
  }
  {   // the toss: in a pause after its time; the stem tumbles off low in front
    uint64_t seed = 1;
    Run q;
    if (startPick(q, seed, 1)) {
      World& v = q.w();
      while (v.reeds.pick != 0) q.step(1);
      v.reeds.chew_until = storyTick(v);
      int shots = 0;
      for (int t = 0; t < 25 * 900 && shots < 3; ++t) {
        q.step(1);
        if (shots == 0 && !reedsChewing(v) && v.reeds.pick == 0 && v.fx.on) {   // another ending came first: again
          reedsGiveChew(v, 1.f);
          v.reeds.chew_until = storyTick(v);
        }
        const float tt = (float)v.reeds.pick_t / 25.f;
        if (v.reeds.pick == 10 && ((shots == 0 && tt >= 0.5f) || (shots == 1 && tt >= 0.75f))) {
          char name[64];
          snprintf(name, sizeof name, "T_%02d_toss%d", n++, shots);
          const Vec2 m = hikerMouthPos(v.hiker);
          q.shot(name, shots == 0 ? "the toss: the hand to his mouth, 3x" : "the toss: flicked away low in front, 3x",
                 (int)m.x - 60, (int)m.y - 30, (int)m.x + 60, (int)v.hiker.feet.y + 12, 3);
          ++shots;
        }
        if (shots == 2 && v.reeds.fall_kind && v.reeds.fall_t >= 8) {
          char name[64];
          snprintf(name, sizeof name, "T_%02d_toss2", n++);
          q.shot(name, "the toss: tumbling down (on the water it would ride away as a stick), 3x", (int)v.reeds.fall_x - 60,
                 (int)v.reeds.fall_y - 30, (int)v.reeds.fall_x + 60, (int)v.reeds.home_y + 14, 3);
          ++shots;
        }
      }
      if (shots < 3) printf("  the toss: %d of 3 shots\n", shots);
      q.stop();
    }
  }
}

// ---- L: the small life ----
void sheetLife() {
  {   // the dragonfly, hovering and perched
    const uint64_t seed = findLayout(1u << RS_NEAR | 1u << RS_FORE, 0, 1);
    Run r;
    r.start(seed, Look{SF_MIDDAY, 3.f, false, 7, 0, 0});
    int got = 0;
    for (int t = 0; t < 25 * 900 && got < 2; ++t) {
      r.step(1);
      const Reeds& R = r.w().reeds;
      const int st = R.fly & 15;
      if ((got == 0 && st == 2 && R.fly_t == 10) || (got == 1 && st == 3 && R.fly_t == 30)) {
        const int x = R.d_fly[0] != -32768 ? R.d_fly[0] : 300, y = R.d_fly[1] != -32768 ? R.d_fly[1] : 420;
        char name[64];
        snprintf(name, sizeof name, "L_%02d_dragonfly", got);
        r.shot(name, got == 0 ? "the dragonfly hovering by the heads, 3x" : "the dragonfly perched on a head, 3x", x - 50, y - 34,
               x + 50, y + 26, 3);
        ++got;
      }
    }
    if (got < 2) printf("  dragonfly: %d of 2 shots\n", got);
    r.stop();
  }
  {   // fireflies at dusk
    Run r;
    r.start(findLayout(1u << RS_NEAR | 1u << RS_UPSTREAM | 1u << RS_FIRESIDE, 0, 1), Look{SF_EVENING, 4.8f, false, 7, 0, 0});
    r.step(25 * 30);
    for (int t = 0; t < 25 * 60; ++t) {   // a moment with at least two lit
      r.step(1);
      int lit = 0;
      // the dots draw on the frame: count them through the air layer's output
      std::vector<Pixel> px((size_t)SS_WIDTH * SS_HEIGHT, (Pixel)0x0841);
      Surface s{px.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
      static void* scratch = calloc(1, rasterScratchBytes());
      void* prev = rasterSetScratch(scratch);
      Palette pal;
      WeatherTint wt;
      memset(&wt, 0, sizeof wt);
      computePalette(4.8f, wt, pal);
      World& w = r.w();
      const uint8_t fly = w.reeds.fly;
      w.reeds.fly = 0;
      reedsDrawAir(w, s, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, pal);
      w.reeds.fly = fly;
      rasterSetScratch(prev);
      int blobs = 0, last_x = -100;
      for (int x = 0; x < SS_WIDTH; ++x)
        for (int y = 340; y < 440; ++y)
          if (px[(size_t)y * SS_WIDTH + x] != (Pixel)0x0841) {
            if (x > last_x + 6) ++blobs;
            last_x = x;
            break;
          }
      lit = blobs;
      if (lit >= 2) break;
    }
    r.shot("L_02_fireflies", "fireflies at dusk over the reeds, 2x", 200, 300, 600, 470, 2);
    r.stop();
  }
  {   // fluff on a November gust
    Run r;
    r.start(findLayout(1u << RS_FORE, 0, 1), Look{SF_MIDDAY, 3.f, false, 11, 3, 0});
    int got = 0;
    for (int t = 0; t < 25 * 400 && !got; ++t) {
      r.step(1);
      int on = 0, x = 0, y = 0;
      for (const ReedSeed& s : r.w().reeds.seed)
        if (s.on == 1 && s.age > 20) {
          ++on;
          x = s.x0 - (int)((float)s.speed * (float)s.age / 25.f);
          y = s.y0;
        }
      if (on) {
        r.shot("L_03_fluff", "a fluff seed on a November gust, 3x", x - 60, y - 40, x + 60, y + 30, 3);
        got = 1;
      }
    }
    if (!got) printf("  no fluff seed\n");
    r.stop();
  }
  {   // the red-winged blackbird: landing (the dip), riding, leaving (the whip)
    int got = 0;
    for (uint64_t seed = 1; seed <= 12 && got < 3; ++seed) {
      Run r;
      r.start(seed, Look{SF_MIDDAY, 3.f, false, 7, 0, 0});
      int shot_stage = 0;
      for (int t = 0; t < 25 * 700 && got < 3; ++t) {
        r.step(1);
        const World& w = r.w();
        if (strcmp(minorsName(w), "reed_bird") != 0) continue;
        const int st = w.minors.stage;
        const bool want = (shot_stage == 0 && st == 1 && w.minors.t >= 0.2f) || (shot_stage == 1 && st == 2 && w.minors.t >= 4.f) ||
                          (shot_stage == 2 && st == 4 && w.minors.t >= 0.12f);
        if (!want) continue;
        const Rect b = clumpCrop(w, w.minors.n, 30);
        static const char* what[3] = {"the blackbird lands: the stalk dips", "riding the head", "springing off: the stalk whips up"};
        char name[64], label[160];
        snprintf(name, sizeof name, "L_%02d_bird%d", 4 + got, shot_stage);
        snprintf(label, sizeof label, "%s (seed %llu), 3x", what[shot_stage], (unsigned long long)seed);
        r.shot(name, label, b.x0, b.y0 - 26, b.x1, b.y1, 3);
        ++got;
        ++shot_stage;
      }
      r.stop();
    }
    if (got < 3) printf("  blackbird: %d of 3 shots\n", got);
  }
}

// ---- K: the chip's corner with both dog badges: nothing of the reeds meets it ----
void sheetChip() {
  Look l{SF_MIDDAY, 3.f, false, 10, 3, 3};
  l.chip = true;
  Run r;
  r.start(findLayout(1u << RS_NEAR | 1u << RS_FORE | 1u << RS_UPSTREAM, 0, 1), l);
  r.step(25 * 120);
  r.shot("K_00_chip", "the bottom right with both dog badges (October wind), 2x", 400, 330, 800, 480, 2);
  r.stop();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: reeds_stills OUT_DIR [sheets: SCMWBNPTLK]\n");
    return 2;
  }
  g_out = argv[1];
  const char* which = argc > 2 ? argv[2] : "SCMWBN";
  if (ssXmasSwitch() != SS_XMAS) {
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  char mp[700];
  snprintf(mp, sizeof mp, "%s/manifest.txt", g_out);
  g_manifest = fopen(mp, strchr(which, '+') ? "ab" : "wb");
  if (!g_manifest) return 2;
  if (strchr(which, 'S')) sheetScene();
  if (strchr(which, 'C')) sheetClumps();
  if (strchr(which, 'M')) sheetSeasons();
  if (strchr(which, 'W')) sheetWind();
  if (strchr(which, 'B')) sheetBefore();
  if (strchr(which, 'N')) sheetNeighbours();
  if (strchr(which, 'P')) sheetPick();
  if (strchr(which, 'T')) sheetStem();
  if (strchr(which, 'L')) sheetLife();
  if (strchr(which, 'K')) sheetChip();
  fclose(g_manifest);
  return 0;
}
