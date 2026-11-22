// Stills for Mike's stage 22 review: the tai chi module alone (taichi.h), on a flat pale ground, before the
// major exists (TAICHI_PLAN section 12; INTEGRATION_PLAN step B2).
//   S1  the posture sheet: every named form, hat off, no pack (taichi_sheet.py shows it at 2x)
//   S2  a filmstrip of each chapter, a frame every 2 s from the first stillness to the weight back with the
//       rig, at panel size, so the weight shifts can be followed from frame to frame (chapter 1 with the
//       butterfly)
//   S4  the turn: 16 ticks round the flip, with the end-on feet and straight knees, and without
//   S6  the feet: chapter 2's grasp the sparrow's tail (ward off, roll back, press, withdraw, push) laid
//       over each other: five bodies, one pair of feet
//   S7  the gags: the wobble on the rooster, the itch in the single whip, Copper's bump at the brush knee
//       (no dog is drawn: the module alone)
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (the same
// -DSS_XMAS and -DSS_S22 as the library):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I core tools/taichi_stills.cpp BUILD/libss_core.a
//     -o BUILD/taichi_stills
//   BUILD/taichi_stills OUT_DIR    writes OUT_DIR/*.ppm (small tiles) and OUT_DIR/manifest.txt
//
// taichi_sheet.py turns them into PNG sheets (zlib only). Manifest lines: group|file|label|x0|y0|x1|y1|scale.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gfx/raster.h"
#include "palette.h"
#include "scene/hiker.h"
#include "scene/taichi.h"

using namespace ss;

namespace {

constexpr int kTileW = 200, kTileH = 128;
constexpr float kX0 = 52.f, kY0 = 112.f;   // where the line starts in a tile, and the ground

const char* const kNames0[] = {"stillness (wuji)", "commencing: arms float up", "... press down, he sinks", "hold the ball",
                               "part the horse's mane 1", "sit back, the ball", "part the horse's mane 2", "half step up, the ball",
                               "white crane spreads its wings", "wind up", "brush knee and push", "playing the lute",
                               "repulse monkey: open", "... the foot passes back", "repulse monkey 1", "... the foot passes back",
                               "repulse monkey 2", "closing: hands down", "he stands; three breaths"};
const char* const kNames1[] = {"stillness", "commencing: up", "... and down", "wind up", "brush knee and push 1", "sit back, wind up",
                               "brush knee and push 2", "gather and turn round", "sparrow's tail: ward off", "... roll back",
                               "... press", "... withdraw", "... push", "the hook goes out", "single whip", "gather and turn back",
                               "cross hands", "closing: hands down", "he stands; three breaths"};
const char* const kNames2[] = {"stillness", "commencing: up", "... and down", "wave hands like clouds", "... back and down, out and up",
                               "... the other hand", "... round again", "wave hands like clouds", "... back and down, out and up",
                               "... the other hand", "... round again", "wave hands like clouds", "... back and down, out and up",
                               "... the other hand",
                               "single whip", "snake creeps down", "... and up", "golden rooster on one leg", "high pat on horse",
                               "repulse monkey: open", "... the foot passes back", "repulse monkey 1", "... the foot passes back",
                               "repulse monkey 2", "closing", "he stands; three breaths"};
const char* const kNames3[] = {"stillness", "commencing: up", "... and down", "high pat on horse", "wrists cross, foot draws in",
                               "heel kick", "the knee folds, wind up", "brush knee and push", "half step up, hand by the ear",
                               "needle at sea bottom", "fan through the back", "gather and turn round", "apparent close-up: withdraw",
                               "... push", "cross hands", "closing: hands down", "he stands; the long close"};
const char* keyName(int ch, int k) {
  const char* const* t = ch == 0 ? kNames0 : ch == 1 ? kNames1 : ch == 2 ? kNames2 : kNames3;
  const int n = ch == 0 ? 19 : ch == 1 ? 19 : ch == 2 ? 26 : 17;
  return (k >= 0 && k < n) ? t[k] : "?";
}

struct Tile {
  std::vector<Pixel> px;
  Surface s;
  Tile() : px((size_t)kTileW * kTileH) { s = Surface{px.data(), kTileW, kTileH, kTileW, 0}; }
  Rect all() const { return Rect{0, 0, kTileW, kTileH}; }
};

const Pixel kSky = rgb565(226, 230, 220), kGround = rgb565(190, 196, 172), kLine = rgb565(160, 168, 140);

void background(Tile& t, float ground) {
  const int gy = (int)ground;
  for (int y = 0; y < kTileH; ++y)
    for (int x = 0; x < kTileW; ++x) t.px[(size_t)y * kTileW + x] = y < gy ? kSky : (y == gy ? kLine : kGround);
  // A tick every 0.1 H along the line, so a sliding foot would show.
  for (int i = -2; i <= 9; ++i) {
    const int x = (int)lroundf(kX0 + (float)i * 9.f);
    if (x >= 0 && x < kTileW)
      for (int y = gy + 1; y < gy + (i == 0 ? 6 : 3) && y < kTileH; ++y) t.px[(size_t)y * kTileW + x] = kLine;
  }
}

bool writePPM(const char* path, const Pixel* px, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row((size_t)w * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const Pixel p = px[(size_t)y * w + x];
      row[(size_t)x * 3] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  return fclose(f) == 0;
}

struct Run {
  TaichiState s;
  Hiker h;
  uint32_t tick;
  int bump_wait;   // ticks since the bump call (the tool plays Copper: he arrives after 1.2 s)
};

void start(Run& r, int ch, uint8_t gag, bool fly) {
  memset(&r, 0, sizeof r);
  hikerInit(r.h, Vec2{kX0, kY0}, 1);
  r.h.hat_on = false;
  r.h.pack_on = false;
  taichiInit(r.s, (uint8_t)ch, gag, kX0, kY0, 1.f, 1, 0);
  if (fly) taichiFlyEnable(r.s, true);
  r.bump_wait = -1;
  hikerTick(r.h, 0.f);
  taichiPose(r.s, r.h);
}

// One tick as the World runs it: the major's tick (the module's clock), then the rig, then the day pose,
// then the day step (the butterfly).
void step(Run& r) {
  taichiStep(r.s, SS_TICK_DT);
  const uint16_t ev = taichiEvents(r.s);
  if (ev & TE_BUMP_CALL) r.bump_wait = 0;
  if (r.bump_wait >= 0 && ++r.bump_wait == 30) {
    taichiGag(r.s, TG_BUMP, 1);
    r.bump_wait = -1;
  }
  hikerTick(r.h, SS_TICK_DT);
  taichiPose(r.s, r.h);
  ++r.tick;
  taichiFlyStep(r.s, r.tick);
}

void runTo(Run& r, float t) {
  const uint32_t n = (uint32_t)lroundf(t / SS_TICK_DT);
  while (r.tick < n) step(r);
}

void drawRun(Tile& t, const Run& r, const Palette& pal) {
  background(t, kY0);
  hikerDraw(r.h, t.s, pal, t.all());
  taichiFlyDraw(r.s, t.s, t.all(), pal);
}

const char* g_out = nullptr;
FILE* g_man = nullptr;
int g_n = 0;

void emit(const char* group, const Tile& t, const char* label, int x0, int y0, int x1, int y1, int scale) {
  char name[64], path[700];
  snprintf(name, sizeof name, "%s_%03d.ppm", group, g_n++);
  snprintf(path, sizeof path, "%s/%s", g_out, name);
  writePPM(path, t.px.data(), kTileW, kTileH);
  fprintf(g_man, "%s|%s|%s|%d|%d|%d|%d|%d\n", group, name, label, x0, y0, x1, y1, scale);
}

// A moment that shows a key's shape: well into a real stop, or as the hands pass a short one.
float showTime(int ch, int k) {
  const TaiKey& key = taichiKey(ch, k);
  const float a = taichiKeyArrive(ch, k), hold = 0.1f * taichiPace() * (float)key.hold;
  if (key.flags & TK_BOW) return a + 2.9f;
  if (key.hold >= 10) return a + (hold * 0.5f > 0.6f ? hold * 0.5f : 0.6f);
  return a + 0.5f * hold + 0.25f;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: taichi_stills OUT_DIR\n");
    return 2;
  }
  g_out = argv[1];
  char mp[600];
  snprintf(mp, sizeof mp, "%s/manifest.txt", g_out);
  g_man = fopen(mp, "wb");
  if (!g_man) return 2;
  void* scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(scratch);
  Palette pal;
  WeatherTint tint;
  memset(&tint, 0, sizeof tint);
  computePalette(2.0f, tint, pal);   // the morning's colours for him

  Tile tile;
  Run r;
  char label[160];

  // S1: the posture sheet.
  static const int kForms[][2] = {
      {0, 0},  {0, 1},  {0, 2},  {0, 3},  {0, 4},  {0, 8},  {0, 9},  {0, 10}, {0, 11}, {0, 12}, {0, 13},
      {1, 7},  {1, 8},  {1, 9},  {1, 10}, {1, 11}, {1, 12}, {1, 13}, {1, 14}, {1, 16}, {2, 3},  {2, 4},
      {2, 10}, {2, 11}, {2, 12}, {2, 13}, {3, 4},  {3, 5},  {3, 6},  {3, 8},  {3, 9},  {3, 10}, {3, 16}};
  for (const auto& f : kForms) {
    start(r, f[0], TG_NONE, false);
    runTo(r, showTime(f[0], f[1]));
    drawRun(tile, r, pal);
    snprintf(label, sizeof label, "%d.%d %s", f[0] + 1, f[1], keyName(f[0], f[1]));
    emit("s1", tile, label, 0, 0, kTileW, kTileH, 2);
  }

  // S2: the filmstrips, a frame every 2 s.
  for (int ch = 0; ch < 4; ++ch) {
    start(r, ch, TG_NONE, ch == 0);
    char group[8];
    snprintf(group, sizeof group, "s2c%d", ch + 1);
    for (int f = 0;; ++f) {
      runTo(r, 2.f * (float)f);
      drawRun(tile, r, pal);
      const int k = r.s.key;
      snprintf(label, sizeof label, "%3ds %s", 2 * f, taichiDone(r.s) ? "back with the rig" : keyName(ch, k));
      emit(group, tile, label, 0, 0, kTileW, kTileH, 1);
      if (taichiDone(r.s)) break;
    }
  }

  // S4: the turn (chapter 2, key 7), 16 ticks round the flip, with the turn's aids and without.
  const float flip = taichiKeyArrive(1, 7) + 0.5f * 0.1f * taichiPace() * (float)taichiKey(1, 7).hold;
  const int fk = (int)lroundf(flip / SS_TICK_DT);
  for (int aid = 1; aid >= 0; --aid) {
    start(r, 1, TG_NONE, false);
    r.s.turn_aid_off = (uint8_t)(aid ? 0 : 1);
    for (int k = fk - 8; k < fk + 8; ++k) {
      runTo(r, (float)k * SS_TICK_DT);
      drawRun(tile, r, pal);
      snprintf(label, sizeof label, "%s %+d", aid ? "aided" : "plain", k - fk);
      const int cx = (int)lroundf(r.s.hip_x);
      emit(aid ? "s4a" : "s4b", tile, label, cx - 22, 18, cx + 22, 118, 3);
    }
  }

  // S6: the sparrow's tail laid over itself.
  {
    static const Pixel kTint[5] = {rgb565(200, 40, 40), rgb565(230, 150, 20), rgb565(40, 150, 60), rgb565(40, 90, 210),
                                   rgb565(140, 50, 170)};
    std::vector<Pixel> acc((size_t)kTileW * kTileH);
    background(tile, kY0);
    acc = tile.px;
    for (int i = 0; i < 5; ++i) {
      start(r, 1, TG_NONE, false);
      runTo(r, showTime(1, 8 + i));
      Tile one;
      background(one, kY0);
      const std::vector<Pixel> bg = one.px;
      hikerDraw(r.h, one.s, pal, one.all());
      for (size_t p = 0; p < acc.size(); ++p)
        if (one.px[p] != bg[p]) acc[p] = blend565(acc[p], kTint[i], 120);
    }
    tile.px = acc;
    tile.s.pixels = tile.px.data();
    // The label fits the 3x tile (the full list ran off the sheet).
    emit("s6", tile, "2.8-12 sparrow's tail, red to purple", 20, 10, 180, 124, 3);
  }

  // S7: the gags.
  struct Gag {
    int ch;
    uint8_t gag;
    float from, every;
    int n;
    const char* name;
  };
  const Gag kGags[3] = {{2, TG_WOBBLE, -0.5f, 1.0f, 10, "wobble"}, {1, TG_ITCH, -0.5f, 0.5f, 14, "itch"},
                        {0, TG_BUMP, -0.5f, 1.0f, 15, "bump"}};
  for (const Gag& g : kGags) {
    start(r, g.ch, g.gag, false);
    float g0 = -1.f;
    for (int k = 0; k < 4000 && g0 < 0.f; ++k) {   // find when the gag (or the bump's call) starts
      step(r);
      if (r.s.phase == TP_GAG || r.s.phase == TP_WAIT) g0 = (float)r.tick * SS_TICK_DT;
    }
    start(r, g.ch, g.gag, false);
    char group[8];
    snprintf(group, sizeof group, "s7%c", g.name[0]);
    for (int i = 0; i < g.n; ++i) {
      const float t = g0 + g.from + g.every * (float)i;
      runTo(r, t);
      drawRun(tile, r, pal);
      snprintf(label, sizeof label, "%s %+.1fs", g.name, (double)(t - g0));
      emit(group, tile, label, 0, 0, kTileW, kTileH, 2);
    }
  }

  fclose(g_man);
  printf("wrote %d tiles to %s\n", g_n, g_out);
  return 0;
}
