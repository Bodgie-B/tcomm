// Stills for Mike's review of stage 22 step C3, the long shot as a major (LONGRANGE_PLAN.md section 11, stills 3
// and 5 to 8, plus the reed stem against the scope in the prone pose). Unlike longshot_stills.cpp (step B5, the
// shooter posed by hand) these are real midday loops: the app runs the seed and every frame is the scene's own,
// taken the first time a moment's condition holds. Nothing is drawn by the tool.
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (SS_S22 on):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I core tools/longshot_scene_stills.cpp
//     BUILD/libss_core.a -o BUILD/longshot_scene_stills
//   BUILD/longshot_scene_stills OUT_DIR      writes OUT_DIR/*.ppm and OUT_DIR/manifest.txt
//   python -I tools/longshot_sheet.py OUT_DIR SHEET_DIR
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gfx/raster.h"
#include "scene/day.h"
#include "scene/day_longshot.h"
#include "scene/plink.h"
#include "scene/reeds.h"
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

Inputs midday(int code) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = true;
  in.weather_code = (uint8_t)code;
  in.month = 6;
  return in;
}

int planIndex(const LoopPlan& p, int id) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == id) return i;
  return -1;
}

// A moment: the first tick its condition holds is taken. `kind`: 0 the shooter's crop, 1 a crop round the
// target being shot at, 2 the whole frame, 3 the shooter and the target side by side.
struct Moment {
  const char* sheet;
  int row;
  const char* label;
  int kind;
  bool (*when)(const World& w, bool after);
  bool taken;
};

const FarState& Fs(const World& w) { return w.day.plink.far; }
const PlinkState& Ps(const World& w) { return w.day.plink.p; }
bool on(const World& w) { return w.day.kind == DAY_PLINK; }
const FarTarget* cur(const World& w) {
  const FarState& f = Fs(w);
  return f.target < f.n ? &f.tg[f.target] : nullptr;
}

// ---- the moments ----
bool mGlass(const World& w, bool) { return on(w) && Fs(w).n && (Fs(w).tg[Fs(w).n - 1].shown & 1) && w.major.count == 0 && w.major.t < 8.f; }
bool mWalkOut(const World& w, bool) {
  return on(w) && Fs(w).flag_held && Ps(w).sack == PI_HAND && w.hiker.walking && w.major.count == 0 && w.hiker.feet.x > w.major.p[0].x - 40.f;
}
bool mWatchFlag(const World& w, bool) { return on(w) && Fs(w).g_flag && !Fs(w).flag_held && w.fx.head > 0.12f && w.major.count == 0; }
bool mDown33(const World& w, bool) { return on(w) && Ps(w).kneel_on && Ps(w).kneel > 0.33f; }
bool mDown62(const World& w, bool) { return on(w) && Ps(w).kneel_on && Ps(w).kneel > 0.62f; }
bool mLoad(const World& w, bool) { return on(w) && Ps(w).mag == MAG_HAND && Ps(w).kneel_on && w.major.count == 0; }
bool mSettled(const World& w, bool) { return on(w) && Ps(w).settle > 0.99f && Ps(w).mag == MAG_IN && Ps(w).up == 0.f; }
bool mHoldTop(const World& w, bool) {
  return on(w) && Ps(w).phase == PP_HOLD && w.major.count == 0 && fabsf(w.hiker.breath_phase - 0.25f) < 0.03f;
}
bool mHoldStill(const World& w, bool) { return on(w) && Ps(w).phase == PP_HOLD && w.major.count == 0 && Ps(w).t > 0.85f * Ps(w).hold_len; }
bool mFlash(const World& w, bool) { return on(w) && Ps(w).flash > 0.f; }
bool mFlight(const World& w, bool) { return on(w) && Ps(w).bullet_t > 0.f && Ps(w).flash <= 0.f && Ps(w).bullet_t < Ps(w).far_flight - 0.15f; }
bool mFirstHit(const World& w, bool) { return on(w) && Fs(w).glint_t > 0.f && Fs(w).bird_t >= 0.f && Fs(w).bird_t < 0.06f; }
bool mBirds(const World& w, bool) { return on(w) && Fs(w).bird_t > 1.2f; }
bool mEcho(const World& w, bool) { return on(w) && Fs(w).echo_t > 2.1f; }
bool mRock(const World& w, bool) { return on(w) && cur(w) && cur(w)->kind == FT_ROCK && Fs(w).glint_t > 0.f && cur(w)->sw_t >= 0.f; }
bool mTin(const World& w, bool) { return on(w) && Fs(w).drop_t > 0.25f; }
bool mPost(const World& w, bool) { return on(w) && cur(w) && cur(w)->kind == FT_POST && cur(w)->sw_t > 0.1f && cur(w)->sw_t < 0.2f; }
bool mSky(const World& w, bool) {
  return on(w) && cur(w) && cur(w)->kind == FT_SKY && cur(w)->layer != TREES_FAR && cur(w)->sw_t > 0.1f && cur(w)->sw_t < 0.2f;
}
bool mArcs(const World& w, bool) { return on(w) && Fs(w).arcs_t > 0.45f; }
bool mWindPuff(const World& w, bool) { return on(w) && Fs(w).result == FR_WIND && Fs(w).puff_t > 0.3f; }
bool mWindPuff2(const World& w, bool) { return on(w) && Fs(w).result == FR_WIND && Fs(w).wind_miss == 2 && Fs(w).puff_t > 0.3f; }
bool mLowPuff(const World& w, bool) { return on(w) && Fs(w).result == FR_LOW && Fs(w).puff_t > 0.3f; }
bool mNod(const World& w, bool) { return on(w) && Fs(w).result == FR_HIT && (w.major.cue & 256) && w.fx.head > 0.05f; }
bool mShake(const World& w, bool) { return on(w) && Fs(w).result == FR_WIND && Fs(w).wind_miss == 2 && w.fx.head > 0.07f; }
bool mTilt(const World& w, bool) { return on(w) && Fs(w).result == FR_OVER && Ps(w).head_off > 0.75f && w.fx.head < -0.06f; }
bool mReadFlag(const World& w, bool) { return on(w) && (Fs(w).pre & 1) && !(w.major.cue & 256) && w.fx.head > 0.17f; }
bool mDial(const World& w, bool) { return on(w) && (Fs(w).pre & 2) && !(w.major.cue & 256) && w.fx.arm_w[0] > 0.95f; }
bool mFistUp(const World& w, bool) { return on(w) && Fs(w).result == FR_LAST && w.fx.arm_w[0] > 0.95f; }
bool mLookAway(const World& w, bool) {
  return on(w) && Fs(w).result == FR_LATE && cur(w) && cur(w)->sw_t > 0.15f && w.fx.head > 0.1f;
}
bool mLookUp(const World& w, bool) { return on(w) && Fs(w).result == FR_LATE && (w.major.cue & 512) && w.fx.head < -0.06f; }
bool mLateFist(const World& w, bool) { return on(w) && Fs(w).result == FR_LATE && (w.major.cue & 512) && w.fx.arm_w[0] > 0.95f; }
bool mUnloadLook(const World& w, bool) { return on(w) && Ps(w).mag == MAG_NONE && Ps(w).kneel_on && w.fx.head > 0.28f; }
bool mUp(const World& w, bool) { return on(w) && !Ps(w).kneel_on && Ps(w).kneel > 0.2f && Ps(w).kneel < 0.5f; }
bool mPull(const World& w, bool) {
  return on(w) && w.major.count > 0 && Fs(w).flag_held && !w.hiker.walking && w.hiker.target == POSE_CROUCH;
}
bool mLookBack(const World& w, bool) { return on(w) && w.major.count > 0 && Fs(w).flag_held && w.fx.head < -0.15f; }
bool mSitAfter(const World&, bool after) { return after; }

Moment g_m[] = {
    {"c3_walk", 0, "the glassing: all the targets picked out", 3, mGlass, false},
    {"c3_walk", 1, "the walk out: rifle low, the bag and the flag", 0, mWalkOut, false},
    {"c3_walk", 1, "the flag planted, he watches the ribbon", 0, mWatchFlag, false},
    {"c3_walk", 2, "going down, k 0.33", 0, mDown33, false},
    {"c3_walk", 2, "going down, k 0.62", 0, mDown62, false},
    {"c3_walk", 3, "loading, prone (the far hand)", 0, mLoad, false},
    {"c3_walk", 3, "settled, the near heel down, loaded", 0, mSettled, false},
    {"c3_hold", 0, "the hold: the top of the deep breath", 0, mHoldTop, false},
    {"c3_hold", 0, "the hold: the last 30 pc, nothing moves", 0, mHoldStill, false},
    {"c3_hold", 1, "the shot (2 ticks of flash)", 0, mFlash, false},
    {"c3_hold", 1, "the flight: nothing happens", 3, mFlight, false},
    {"c3_far", 0, "the first hit: the glint, the birds lift", 1, mFirstHit, false},
    {"c3_far", 0, "the far birds, 1.2 s on", 1, mBirds, false},
    {"c3_far", 0, "the first report's echo off the far ridge", 2, mEcho, false},
    {"c3_far", 1, "the rock hit", 1, mRock, false},
    {"c3_far", 1, "the tin falling out of its pine", 1, mTin, false},
    {"c3_far", 1, "the post gong mid swing", 1, mPost, false},
    {"c3_far", 2, "the skyline gong mid swing", 1, mSky, false},
    {"c3_far", 2, "the ring coming back", 1, mArcs, false},
    {"c3_far", 3, "b: a miss in the wind, the puff downwind", 1, mWindPuff, false},
    {"c3_far", 3, "b: the second miss, nearer", 1, mWindPuff2, false},
    {"c3_far", 3, "c: the low miss, the puff under it", 1, mLowPuff, false},
    {"c3_react", 0, "a hit: the nod on the stock", 0, mNod, false},
    {"c3_react", 0, "b: the slow head shake", 0, mShake, false},
    {"c3_react", 0, "c: over the top, the puzzled tilt", 0, mTilt, false},
    {"c3_react", 1, "reading the flag", 0, mReadFlag, false},
    {"c3_react", 1, "dialing (the near hand at the scope)", 0, mDial, false},
    {"c3_react", 1, "the last hit: the quiet fist", 0, mFistUp, false},
    {"c3_react", 2, "c: he looks away, the gong already swinging", 3, mLookAway, false},
    {"c3_react", 3, "c: the ring is back, he looks up", 0, mLookUp, false},
    {"c3_react", 3, "c: the quiet fist", 0, mLateFist, false},
    {"c3_end", 0, "unloaded: the look into the action", 0, mUnloadLook, false},
    {"c3_end", 0, "getting up", 0, mUp, false},
    {"c3_end", 1, "pulling the flag", 0, mPull, false},
    {"c3_end", 1, "the last look back at the hills", 0, mLookBack, false},
    {"c3_end", 2, "the final sit, the steel still out on the hills", 2, mSitAfter, false},
};
const int kMoments = (int)(sizeof g_m / sizeof g_m[0]);

void emitCrop(const char* name, const char* label, int x0, int y0, int x1, int y1, int scale, const char* sheet, int row) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SS_WIDTH) x1 = SS_WIDTH;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  fprintf(g_man, "%s.ppm|%s|%d|%d|%d|%d|%d|%s|%d\n", name, label, x0, y0, x1, y1, scale, sheet, row);
}

void take(App* app, Pixel* fb, const World& w, Moment& m, uint64_t seed, const char* why) {
  Surface s{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  appInvalidate(app);
  appRender(app, s);
  char name[64], path[700], label[200];
  snprintf(name, sizeof name, "c3_%02d", g_n++);
  snprintf(path, sizeof path, "%s/%s.ppm", g_out, name);
  writePPM(path, fb);
  snprintf(label, sizeof label, "%s (seed %llu%s)", m.label, (unsigned long long)seed, why);
  const Vec2 fp = w.major.p[0];
  const int sx0 = (int)fp.x - 90, sy0 = (int)fp.y - 78;
  const FarTarget* t = w.day.kind == DAY_PLINK ? cur(w) : nullptr;
  Vec2 tp = t ? t->pos : Vec2{700.f, 260.f};
  if (m.kind == 1 && w.day.plink.far.echo_t >= 0.f) tp = w.day.plink.far.echo_at;
  switch (m.kind) {
    case 0: emitCrop(name, label, sx0, sy0, sx0 + 250, sy0 + 98, 3, m.sheet, m.row); break;
    case 1: emitCrop(name, label, (int)tp.x - 45, (int)tp.y - 30, (int)tp.x + 45, (int)tp.y + 18, 5, m.sheet, m.row); break;
    case 2: emitCrop(name, label, 0, 0, SS_WIDTH, SS_HEIGHT, 1, m.sheet, m.row); break;
    default:
      emitCrop(name, label, sx0, sy0, sx0 + 250, sy0 + 98, 3, m.sheet, m.row);
      emitCrop(name, "the target side, 2x", 520, 180, 800, 330, 2, m.sheet, m.row);
      break;
  }
}

// One seed's loop until its long shot has ended; `stem` puts a reed stem in his mouth as the beat begins.
void runSeed(uint64_t seed, const Inputs& in, bool stem, const char* why) {
  void* mem = calloc(1, SS_APP_BYTES);
  Pixel* fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  App* app = appStart(mem, kAlloc, seed, in);
  World* w = const_cast<World*>(appWorld(app));
  if (planIndex(w->plan, MJ_LONGSHOT) < 0) {
    printf("seed %llu: no long shot\n", (unsigned long long)seed);
  } else {
    const uint32_t len = appLoopTicks(app);
    bool in_beat = false;
    int after = 0;
    for (uint32_t t = 0; t + 1 < len; ++t) {
      appTick(app);
      const bool now = strcmp(appBeatName(app), "longshot") == 0;
      if (now && !in_beat && stem) reedsGiveChew(*w, 260.f);
      if (now) in_beat = true;
      if (!in_beat) continue;
      const bool done = !now;
      if (done && ++after < 40) continue;   // (1.6 s into the stillness after)
      for (int i = 0; i < kMoments; ++i) {
        Moment& m = g_m[i];
        if (m.taken || !m.when(*w, done)) continue;
        m.taken = true;
        take(app, fb, *w, m, seed, why);
      }
      if (done) break;
    }
  }
  appStop(app, kAlloc);
  free(mem);
  free(fb);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: longshot_scene_stills OUT_DIR\n");
    return 2;
  }
  g_out = argv[1];
  char path[700];
  snprintf(path, sizeof path, "%s/manifest.txt", g_out);
  g_man = fopen(path, "w");
  if (!g_man) return 1;
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  // A (seed 52, clear), C (seed 49, clear), B (seed 6, wind from the data), then more seeds for the targets the
  // first ones lack (a rock, a tin, a post).
  runSeed(52, midday(0), false, "");
  runSeed(49, midday(0), false, "");
  runSeed(6, midday(3), false, ", wind");
  const uint64_t more[] = {151, 160, 248, 200, 205, 313, 316, 26, 79, 90, 99};
  for (uint64_t s : more) runSeed(s, midday(s % 2 ? 3 : 0), false, s % 2 ? ", wind" : "");
  int missing = 0;
  for (int i = 0; i < kMoments; ++i)
    if (!g_m[i].taken) {
      ++missing;
      printf("not seen: %s\n", g_m[i].label);
    }
  // The stem against the scope, prone (INTEGRATION_PLAN 4.4: it stays while he shoots): the same table with
  // only these four moments left to take.
  for (int i = 0; i < kMoments; ++i) g_m[i].taken = true;
  static const Moment stem[4] = {
      {"c3_stem", 0, "the stem: prone, settled", 0, mSettled, false},
      {"c3_stem", 0, "the stem: on the scope, the hold", 0, mHoldStill, false},
      {"c3_stem", 1, "the stem: the cheek off (reading the flag)", 0, mReadFlag, false},
      {"c3_stem", 1, "the stem: the quiet fist", 0, mFistUp, false},
  };
  for (int i = 0; i < 4; ++i) g_m[i] = stem[i];
  runSeed(52, midday(0), true, ", a stem");
  runSeed(26, midday(3), true, ", a stem, wind");
  fclose(g_man);
  printf("wrote %d frames to %s (%d moments not seen)\n", g_n, g_out, missing);
  return 0;
}
