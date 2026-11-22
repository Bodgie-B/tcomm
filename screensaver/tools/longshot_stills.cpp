// Stills for Mike's review of stage 22 step B5 (LONGRANGE_PLAN.md section 11, stills 1, 2, 4 and 9): the long
// shot's far targets, the aim line, the prone position and the kneeling fallback. The major itself (step C3)
// is not built yet, so the shooter is posed here by hand: a hiker rig and a PlinkState stepped the way the
// World steps them (hikerTick, plinkBend, plinkStep from his hands), drawn over a real midday frame of the
// seed with the scene's own hiker hidden. The targets are longshotTargets' for that seed, drawn as the glyphs
// longshotTargetGlyph makes. The only thing drawn here that the panel never draws is the aim line (a 1 px
// line from the muzzle to the target, to judge how it reads against the camp pine's boughs).
//
// Not part of the CMake build. From the screensaver folder, with a built libss_core.a in BUILD (the same
// -DSS_XMAS as the library):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -DSS_S22=1 -I core tools/longshot_stills.cpp BUILD/libss_core.a
//     -o BUILD/longshot_stills
//   BUILD/longshot_stills OUT_DIR    writes OUT_DIR/*.ppm (800 x 480) and OUT_DIR/manifest.txt
//
// longshot_sheet.py (zlib only) turns the PPMs and the manifest (file|label|x0|y0|x1|y1|scale|sheet|row: the
// crop, its enlargement, which sheet and which row of it) into PNG sheets.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "gfx/raster.h"
#include "scene/day_longshot.h"
#include "scene/hiker.h"
#include "scene/landscape.h"
#include "scene/plink.h"
#include "scene/script.h"
#include "scene/world.h"
#include "ss_app.h"

using namespace ss;

namespace {

void* hostAlloc(size_t n) { return malloc(n); }
void hostFree(void* p) { free(p); }
const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};
const float kDt = SS_TICK_DT;
const Rect kFrame{0, 0, SS_WIDTH, SS_HEIGHT};

typedef std::vector<Pixel> Frame;

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

Inputs inputsDay() {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = 3.f;
  in.spine_force = (uint8_t)(1 + SF_MIDDAY);
  in.have_outside = true;   // a clear day from the data: no random rain or fog in the stills
  in.weather_code = 0;
  return in;
}

const char* kindName(uint8_t k) {
  switch (k) {
    case FT_TREE: return "plate in a tree";
    case FT_ROCK: return "rock, mid ridge";
    case FT_CONE: return "tin in a pine";
    case FT_POST: return "post, far ridge";
    default: return "skyline gong";
  }
}

// The seed's midday scene after the build: the World (copied), the palette, and a frame with his figure hidden.
struct Scene {
  uint64_t seed;
  World w;
  Palette pal;
  Frame bg;
  FarTarget tg[4];
  int n;
};

void makeScene(uint64_t seed, Scene& sc) {
  void* mem = calloc(1, SS_APP_BYTES);
  sc.bg.assign((size_t)SS_WIDTH * SS_HEIGHT, 0);
  Surface surf{sc.bg.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* app = appStart(mem, kAlloc, seed, inputsDay());
  for (int t = 0; t < 420; ++t) appTick(app);
  const_cast<World*>(appWorld(app))->fx.on = false;   // (a stills tool only: his figure is drawn by hand below)
  appTick(app);
  appInvalidate(app);
  appRender(app, surf);
  sc.seed = seed;
  memcpy(static_cast<void*>(&sc.w), appWorld(app), sizeof(World));
  sc.pal = *appPalette(app);
  sc.n = longshotTargets(sc.w.land, sc.tg);
  appStop(app, kAlloc);
  free(mem);
}

void drawTargets(const Scene& sc, Frame& fb) {
  Surface s{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  for (int i = 0; i < sc.n; ++i) {
    Glyph g;
    longshotTargetGlyph(sc.tg[i], g);
    glyphDraw(g, s, kFrame, sc.pal);
  }
}

FILE* g_man = nullptr;
const char* g_out = nullptr;

void emit(const Frame& fb, const char* name, const char* label, int x0, int y0, int x1, int y1, int scale, const char* sheet,
          int row, bool write = true) {
  char path[700];
  snprintf(path, sizeof path, "%s/%s.ppm", g_out, name);
  if (write) writePPM(path, fb.data());
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SS_WIDTH) x1 = SS_WIDTH;
  if (y1 > SS_HEIGHT) y1 = SS_HEIGHT;
  fprintf(g_man, "%s.ppm|%s|%d|%d|%d|%d|%d|%s|%d\n", name, label, x0, y0, x1, y1, scale, sheet, row);
}

// ---- stills 1: the targets on the terrain ----------------------------------------------------------------------

float farRidgeTop(const Landscape& ls) {
  float x0, x1, top = 1e9f;
  longshotWindow(ls, x0, x1);
  for (float x = x0; x <= x1; x += 4.f) top = top < landscapeCrestAt(ls, 0, x) ? top : landscapeCrestAt(ls, 0, x);
  return top;
}

void targetSheetRow(const Scene& sc, int row, const char* why) {
  Frame fb = sc.bg;
  drawTargets(sc, fb);
  float x0, x1;
  longshotWindow(sc.w.land, x0, x1);
  char name[64], label[200];
  snprintf(name, sizeof name, "targets_seed%llu", (unsigned long long)sc.seed);
  snprintf(label, sizeof label, "seed %llu: %s (%d targets)", (unsigned long long)sc.seed, why, sc.n);
  emit(fb, name, label, (int)x0 - 150, 180, 800, 330, 2, "targets", row);
  for (int i = 0; i < sc.n; ++i) {
    const FarTarget& t = sc.tg[i];
    snprintf(label, sizeof label, "%d: %s, %.2f s", i + 1, kindName(t.kind), (double)t.flight);
    const int cx = (int)t.pos.x, cy = (int)t.pos.y - 3;
    emit(fb, name, label, cx - 18, cy - 13, cx + 18, cy + 13, 4, "targets", row, false);
  }
}

// ---- the shooter, posed by hand ------------------------------------------------------------------------------

struct Rig {
  Hiker h;
  PlinkState s;
  Rng rng;
  void step() {
    hikerTick(h, kDt);
    plinkBend(s, h.pose);
    plinkStep(s, hikerHandPos(h, 0), hikerHandPos(h, 1), h.facing, h.height, kDt, rng);
  }
};

struct Spots {
  Vec2 fp, bag, flag;
};

Spots spotsOf(const World& w) {
  Spots sp;
  const Vec2 F = w.fire.pos;
  sp.fp = Vec2{F.x + 86.f, scriptGroundAt(w, F.x + 86.f, F.y - 34.f)};
  sp.bag = Vec2{sp.fp.x + 46.f, scriptGroundAt(w, sp.fp.x + 46.f, sp.fp.y + 1.f)};
  sp.flag = Vec2{sp.fp.x + 112.f, scriptGroundAt(w, sp.fp.x + 112.f, sp.fp.y + 2.f)};
  return sp;
}

void rigInit(Rig& r, const World& w, const Spots& sp, uint8_t rest) {
  const float x = (float)w.land.camp_x;
  const float px[PLINK_PAIRS] = {x - 40.f, x + 6.f, x + 44.f}, py[PLINK_PAIRS] = {351.f, 352.f, 351.f};
  plinkInit(r.s, Vec2{x, (float)LS_BRANCH_ROOT_Y}, Vec2{x - (float)LS_BRANCH_LEN, (float)LS_BRANCH_TIP_Y}, 1, px, py);
  plinkRifle(r.s, true, 1.f);
  plinkScope(r.s, true);
  plinkRest(r.s, rest);
  if (rest == REST_PRONE) {
    plinkSackBig(r.s, true);
    plinkSack(r.s, PI_DOWN);
    plinkLay(r.s, sp.bag);
  }
  hikerInit(r.h, sp.fp, 1);
  r.h.pack_on = false;   // the pack stays at camp.pack at midday
  hikerSetPose(r.h, POSE_STAND, 0.1f);
  r.rng.seed(4242u);
  for (int i = 0; i < 20; ++i) r.step();
}

Glyph flagGlyph(Vec2 at, float breeze, float phase) {
  Glyph g;
  memset(&g, 0, sizeof g);
  g.kind = GL_SHAPE;
  g.shape = SH_FLAG;
  g.slot = MUSHROOM;
  g.facing = 1;
  g.pos = at;
  g.scale = 1.f;
  g.k = breeze;
  g.t = phase;
  g.base_y = (int16_t)at.y;
  return g;
}

// The frame: the scene, the targets, the bag (behind him, as dayPartY sorts it), him, the rifle, the flag.
Frame compose(const Scene& sc, const Rig& r, const Glyph* flag, const Vec2* line_to) {
  Frame fb = sc.bg;
  drawTargets(sc, fb);
  Surface s{fb.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  plinkTreeDraw(r.s, s, kFrame, sc.pal);
  hikerDraw(r.h, s, sc.pal, kFrame);
  plinkHeldDraw(r.s, s, kFrame, sc.pal);
  if (flag) glyphDraw(*flag, s, kFrame, sc.pal);
  if (line_to) strokeCapsule(s, plinkMuzzle(r.s), *line_to, 1.0f, rgb565(255, 40, 40), kFrame);
  return fb;
}

void shooterCrop(const Spots& sp, int& x0, int& y0, int& x1, int& y1) {
  x0 = (int)sp.fp.x - 70;
  y0 = (int)sp.fp.y - 78;
  x1 = (int)sp.fp.x + 150;
  y1 = (int)sp.fp.y + 14;
}

void shooterStill(const Scene& sc, const Rig& r, const Spots& sp, const Glyph* flag, const char* name, const char* label, int row,
                  int scale = 3) {
  Frame fb = compose(sc, r, flag, nullptr);
  int x0, y0, x1, y1;
  shooterCrop(sp, x0, y0, x1, y1);
  emit(fb, name, label, x0, y0, x1, y1, scale, "prone", row);
  printf("  %-22s kneel %.2f up %.2f elev %+.2f muzzle (%.0f, %.0f) feet (%.0f, %.0f) bag ry %s\n", name, (double)r.s.kneel,
         (double)r.s.up, (double)r.s.elev, (double)plinkMuzzle(r.s).x, (double)plinkMuzzle(r.s).y, (double)r.h.feet.x,
         (double)r.h.feet.y, r.s.sack == PI_DOWN ? "down" : "-");
}

// Aim at `t` and hold: the aim line still (full frame, the line, and a crop along the line at the pine).
void aimStill(const Scene& sc, Rig& r, const Spots& sp, const Glyph& flag, const FarTarget& t, const char* name, const char* label,
              int row) {
  plinkQueueClear(r.s);
  plinkFar(r.s, t.pos, t.flight, 3.0f);
  plinkStart(r.s);
  for (int i = 0; i < 400; ++i) {
    r.step();
    if (r.s.phase == PP_HOLD && r.s.t > 1.2f) break;
  }
  Frame plain = compose(sc, r, &flag, nullptr);
  Frame lined = compose(sc, r, &flag, &t.pos);
  char n2[80], l2[200];
  snprintf(n2, sizeof n2, "%s_line", name);
  int x0, y0, x1, y1;
  shooterCrop(sp, x0, y0, x1, y1);
  emit(plain, name, label, x0, y0, x1, y1, 3, "aim", row);
  snprintf(l2, sizeof l2, "%s, with the aim line (tool only)", label);
  emit(lined, n2, l2, 0, 0, 800, 480, 1, "aim", row);
  const Vec2 m = plinkMuzzle(r.s);
  snprintf(l2, sizeof l2, "the line past the camp pine, 3x");
  emit(lined, n2, l2, (int)m.x - 10, (int)t.pos.y - 24, (int)t.pos.x + 16, (int)m.y + 14, 3, "aim", row, false);
  printf("  %-22s target (%.0f, %.0f) elev %+.2f (%.0f deg) muzzle (%.0f, %.0f)\n", name, (double)t.pos.x, (double)t.pos.y,
         (double)r.s.elev, (double)(r.s.elev * 57.3f), (double)m.x, (double)m.y);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: longshot_stills OUT_DIR\n");
    return 2;
  }
  if (ssXmasSwitch() != SS_XMAS) {
    fprintf(stderr, "compile with -DSS_XMAS=%d to match the library\n", ssXmasSwitch());
    return 2;
  }
  g_out = argv[1];
  char mp[600];
  snprintf(mp, sizeof mp, "%s/manifest.txt", g_out);
  g_man = fopen(mp, "wb");
  if (!g_man) return 2;
  rasterSetScratch(calloc(1, rasterScratchBytes()));

  // Six seeds for the targets: a low, a middle and a high far ridge, one where the rock falls back to the
  // cone, one where the post is dropped, one where the skyline is a tooth (or another seed).
  uint64_t pick[6] = {0, 0, 0, 0, 0, 0};
  const char* why[6] = {"low far ridge", "middle far ridge", "high far ridge", "the rock falls back to a tin in a pine",
                        "the post dropped", "the skyline is a tooth of the tree line"};
  for (uint64_t seed = 1; seed <= 300; ++seed) {
    App* app = nullptr;
    void* mem = calloc(1, SS_APP_BYTES);
    app = appStart(mem, kAlloc, seed, inputsDay());
    const Landscape& ls = appWorld(app)->land;
    FarTarget tg[4];
    const int n = longshotTargets(ls, tg);
    bool cone = false, sky_tooth = false, post = false;
    for (int i = 0; i < n; ++i) {
      cone |= tg[i].kind == FT_CONE;
      sky_tooth |= tg[i].kind == FT_SKY && tg[i].layer == TREES_FAR;
      post |= tg[i].kind == FT_POST;
    }
    const float top = farRidgeTop(ls);
    int slot = -1;
    if (cone && !pick[3]) slot = 3;
    else if (!post && !pick[4]) slot = 4;
    else if (sky_tooth && !pick[5]) slot = 5;
    else if (n == 4 && top > 236.f && !pick[0]) slot = 0;
    else if (n == 4 && top > 226.f && top < 232.f && !pick[1]) slot = 1;
    else if (n == 4 && top < 220.f && !pick[2]) slot = 2;
    if (slot >= 0) pick[slot] = seed;
    appStop(app, kAlloc);
    free(mem);
  }
  int row = 0;
  for (int k = 0; k < 6; ++k) {
    if (!pick[k]) {
      printf("no seed found for: %s\n", why[k]);
      continue;
    }
    Scene* sc = new Scene;
    makeScene(pick[k], *sc);
    targetSheetRow(*sc, row++, why[k]);
    printf("seed %llu (%s): %d targets\n", (unsigned long long)pick[k], why[k], sc->n);
    for (int i = 0; i < sc->n; ++i)
      printf("    %d %-16s (%.1f, %.1f) flight %.2f\n", i + 1, kindName(sc->tg[i].kind), (double)sc->tg[i].pos.x,
             (double)sc->tg[i].pos.y, (double)sc->tg[i].flight);
    delete sc;
  }

  // Stills 4 and 2: going down, prone, the aim at the nearest and the farthest target; then 9, the kneeling
  // fallback. On the low-ridge seed (or the first picked).
  uint64_t seed = pick[0] ? pick[0] : (pick[1] ? pick[1] : 1);
  Scene* sc = new Scene;
  makeScene(seed, *sc);
  const Spots sp = spotsOf(sc->w);
  printf("pose seed %llu: FP (%.0f, %.0f) bag (%.0f, %.0f) flag (%.0f, %.0f)\n", (unsigned long long)seed, (double)sp.fp.x,
         (double)sp.fp.y, (double)sp.bag.x, (double)sp.bag.y, (double)sp.flag.x, (double)sp.flag.y);
  Rig* r = new Rig;
  rigInit(*r, sc->w, sp, REST_PRONE);
  float phase = 0.f;
  Glyph flag = flagGlyph(sp.flag, 0.18f, phase);
  shooterStill(*sc, *r, sp, &flag, "down_00_standing", "standing at the firing point, rifle low", 0);
  plinkKneel(r->s, true);
  const float stops[6] = {0.2f, 0.33f, 0.5f, 0.62f, 0.8f, 1.0f};
  int si = 0;
  for (int i = 0; i < 200 && si < 6; ++i) {
    r->step();
    phase += (1.2f + 2.5f * 0.18f) * kDt;
    if (r->s.kneel >= stops[si] - 1e-4f) {
      char name[64], label[120];
      snprintf(name, sizeof name, "down_%02d_k%03d", si + 1, (int)(stops[si] * 100.f + 0.5f));
      snprintf(label, sizeof label, "going down, k %.2f", (double)stops[si]);
      flag = flagGlyph(sp.flag, 0.18f, phase);
      shooterStill(*sc, *r, sp, &flag, name, label, si < 3 ? 0 : 1);
      ++si;
    }
  }
  for (int i = 0; i < 12; ++i) r->step();
  // The settle: the near lower leg comes down flat over 1.2 s.
  for (int i = 0; i <= 30; ++i) {
    plinkSettle(r->s, (float)i / 30.f);
    r->step();
    phase += (1.2f + 2.5f * 0.18f) * kDt;
  }
  flag = flagGlyph(sp.flag, 0.18f, phase);
  shooterStill(*sc, *r, sp, &flag, "prone_settled", "prone, settled: the near heel down", 2);
  plinkLoad(r->s);
  r->step();
  shooterStill(*sc, *r, sp, &flag, "prone_loaded", "prone, loaded, the bag and the flag out", 2);
  const Glyph gust = flagGlyph(sp.flag, 0.68f, phase + 0.3f);
  shooterStill(*sc, *r, sp, &gust, "prone_gust", "the same, the ribbon in a gust", 2);

  plinkStay(r->s, true);
  const FarTarget& nearest = sc->tg[0];
  const FarTarget& farthest = sc->tg[sc->n - 1];
  aimStill(*sc, *r, sp, flag, nearest, "aim_nearest", "on the scope: the nearest target", 0);
  plinkHeadOff(r->s, 1.f);
  for (int i = 0; i < 25; ++i) r->step();
  shooterStill(*sc, *r, sp, &flag, "prone_head_off", "cheek off the stock (head_off 1)", 3);
  plinkHeadOff(r->s, 0.f);
  for (int i = 0; i < 25; ++i) r->step();
  aimStill(*sc, *r, sp, flag, farthest, "aim_farthest", "on the scope: the farthest target", 1);
  // The shot (the flash) at the farthest.
  plinkQueueClear(r->s);
  plinkFar(r->s, farthest.pos, farthest.flight, 0.4f);
  plinkStart(r->s);
  for (int i = 0; i < 200; ++i) {
    r->step();
    if (r->s.flash > 0.04f) break;
  }
  shooterStill(*sc, *r, sp, &flag, "prone_flash", "the shot (2 ticks of flash)", 3);

  // Still 9: the kneeling fallback at the same spot, on the nearest target.
  Rig* k = new Rig;
  rigInit(*k, sc->w, sp, REST_KNEEL);
  plinkKneel(k->s, true);
  for (int i = 0; i < 40; ++i) k->step();
  plinkLoad(k->s);
  plinkStay(k->s, true);
  aimStill(*sc, *k, sp, flag, nearest, "kneel_nearest", "the kneeling fallback: the nearest target", 2);
  delete k;
  delete r;
  delete sc;
  fclose(g_man);
  return 0;
}
