// Stills for Mike's stage 22 review of the deer visit (DEER_PLAN.md, section 10): for now the deer pose sheet.
//
// The sheet is the deer rig on a plain backdrop (sky, a tree line, meadow) in the day, golden hour and night
// palettes, every cell 120 x 80 panel pixels with the feet on one line: idle, graze, the stretch (and the
// blend into it), the shake, a head up with the white flag at 0, 0.25, 0.5 and 1, the stamp, the ear states,
// the canter and the walk at four phases, both facings, the fawn alone and beside a doe, a startle, a look
// and a head turn. The pictures that need the whole scene (the doe looking in, the settled picture, the way
// out) come with the World side of the herd (step C4) and are added to this tool then.
//
// Not part of the CMake build. From the project folder, with a built libss_core.a in BUILD (the same
// -DSS_XMAS as the library):
//
//   g++ -std=c++17 -O2 -Wall -Wextra -DSS_XMAS=1 -I screensaver/core screensaver/tools/herd_stills.cpp
//     BUILD/libss_core.a -o BUILD/herd_stills
//   BUILD/herd_stills OUT_DIR     writes OUT_DIR/poses_day.ppm, poses_golden.ppm, poses_night.ppm and
//                                 OUT_DIR/manifest.txt (file|label|x0|y0|x1|y1, the crop of each cell)
//   python -I screensaver/tools/herd_sheet.py OUT_DIR docs/screensaver/reviews/stage22-herd
//
// herd_sheet.py (zlib only, no Pillow) cuts the cells out of the PPMs and writes the 2x and 4x sheets.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "gfx/raster.h"
#include "palette.h"
#include "scene/critters.h"
#include "scene/herd.h"

using namespace ss;

namespace {

constexpr int kCellW = 120, kCellH = 80, kFeetY = 68, kCols = 8;

struct Spec {
  int x;                        // feet x in the cell
  float scale;
  uint8_t tone;
  int facing;
  CritterAction act;
  float dur;                    // 0: held (loops at `phase`); > 0: the blend, drawn `at` seconds in
  float at;
  CritterAction from = CA_IDLE; // the action held before the blend (a head coming up from grazing)
  float phase;
  float tail;                   // Critter::tail_add, the white flag
  float head_add, fore_add;
};

Spec deer(int x, CritterAction a) { return Spec{x, 1.f, 0, 1, a, 0.f, 0.f, CA_IDLE, 0.25f, 0.f, 0.f, 0.f}; }
Spec fawn(int x, CritterAction a) { return Spec{x, 0.62f, kDeerFawn, 1, a, 0.f, 0.f, CA_IDLE, 0.25f, 0.f, 0.f, 0.f}; }

Critter make(const Spec& s, int cx, int cy) {
  Critter c;
  critterInit(c, CR_DEER, Vec2{(float)(cx + s.x), (float)(cy + kFeetY)}, s.facing);
  c.scale = s.scale;
  c.tone = s.tone;
  c.tail_add = s.tail;
  c.head_add = s.head_add;
  c.fore_add = s.fore_add;
  if (s.act == CA_MOVE || s.act == CA_RUN) {   // the gait at full weight, in place
    critterSetAction(c, s.act, 0.f);
    c.moving = true;
    c.gait_w = 1.f;
    c.move_from = c.move_to = c.feet.x;
    c.phase = s.phase;
    critterTick(c, 0.f);
  } else if (s.dur > 0.f) {   // part way through the blend (or the one-shot)
    if (s.from != CA_IDLE) {
      critterSetAction(c, s.from, 0.f);
      c.loop_w = 1.f;
      c.phase = 0.3f;
      critterTick(c, 0.f);
    }
    critterSetAction(c, s.act, s.dur);
    const int n = (int)(s.at / SS_TICK_DT + 0.5f);
    for (int i = 0; i < n; ++i) critterTick(c, SS_TICK_DT);
  } else {
    critterSetAction(c, s.act, 0.f);
    c.loop_w = 1.f;
    c.phase = s.phase;
    critterTick(c, 0.f);
  }
  return c;
}

struct Cell {
  std::string label;
  std::vector<Spec> deer;
};

std::string fmt(const char* f, double v) {
  char b[96];
  snprintf(b, sizeof b, f, v);
  return b;
}

std::vector<Cell> cells() {
  std::vector<Cell> v;
  auto one = [&](const std::string& label, Spec s) { v.push_back(Cell{label, {s}}); };
  one("idle", deer(60, CA_IDLE));
  Spec s = deer(60, CA_EAT);
  s.phase = 0.f; one("graze, phase 0", s);
  s.phase = 0.3f; one("graze, phase 0.3", s);
  s.phase = 0.7f; one("graze, phase 0.7", s);
  s = deer(60, CA_EAT); s.facing = -1; s.phase = 0.3f; one("graze, facing left", s);
  s = deer(60, CA_LOOK); one("head up (look)", s);
  s = deer(60, CA_HEADTURN); s.dur = 0.5f; s.at = 0.5f; one("head turn", s);
  s = deer(60, CA_STARTLE); s.dur = 0.5f; s.at = 0.5f; one("startle", s);
  // The stretch and the blend into it from standing.
  for (float t : {0.2f, 0.5f, 0.9f, 1.8f}) {
    s = deer(50, CA_REACH); s.dur = 1.8f; s.at = t;
    one(fmt("stretch %.1f s of 1.8", (double)t), s);
  }
  s = deer(50, CA_REACH); s.head_add = 0.03f; one("stretch, sniff up", s);
  s = deer(50, CA_REACH); s.head_add = -0.03f; one("stretch, sniff down", s);
  s = deer(50, CA_REACH); s.facing = -1; one("stretch, facing left", s);
  // The shake.
  for (float p : {0.1f, 0.35f, 0.6f}) {
    s = deer(60, CA_SHAKE); s.phase = p;
    one(fmt("shake, phase %.2f", (double)p), s);
  }
  // The flag on a head-up deer, and on a grazing one.
  for (float k : {0.f, 0.25f, 0.5f, 1.f}) {
    s = deer(60, CA_LOOK); s.tail = k;
    one(fmt("look, flag %.2f", (double)k), s);
  }
  s = deer(60, CA_LOOK); s.tail = 1.f; s.facing = -1; one("look, flag 1, facing left", s);
  s = deer(60, CA_LOOK); s.tail = 1.f; s.tone = kDeerEarFwd; one("alert: ears forward, flag 1", s);
  s = deer(60, CA_EAT); s.tail = 0.5f; one("graze, flag 0.5 (going along)", s);
  s = deer(60, CA_IDLE); s.tail = 0.3f; one("a tail flick at 0.3", s);
  // The stamp: the near fore hoof up.
  for (float f : {0.3f, 0.55f}) {
    s = deer(60, CA_LOOK); s.fore_add = f; s.tail = 1.f;
    one(fmt("stamp, hoof up %.2f rad", (double)f), s);
  }
  // The ear states.
  const uint8_t ears[4] = {0, kDeerEarFwd, kDeerEarMid, kDeerEarBack};
  const char* ear_names[4] = {"ear up", "ear forward", "ear half back", "ear back"};
  for (int i = 0; i < 4; ++i) {
    s = deer(60, CA_LOOK); s.tone = ears[i]; one(ear_names[i], s);
  }
  for (int i = 0; i < 4; ++i) {
    s = deer(60, CA_EAT); s.tone = ears[i]; s.phase = 0.2f;
    one(std::string("graze, ") + ear_names[i], s);
  }
  // The canter and the walk at four phases, flag up.
  for (int i = 0; i < 4; ++i) {
    s = deer(60, CA_RUN); s.phase = (float)i / 4.f; s.tail = 1.f;
    one(fmt("canter, flag up, phase %.2f", (double)s.phase), s);
  }
  for (int i = 0; i < 4; ++i) {
    s = deer(60, CA_MOVE); s.phase = (float)i / 4.f;
    one(fmt("walk, phase %.2f", (double)s.phase), s);
  }
  for (int i = 0; i < 4; ++i) {
    s = deer(60, CA_RUN); s.phase = (float)i / 4.f; s.facing = -1; s.tail = 0.f;
    one(fmt("canter, no flag, left, %.2f", (double)s.phase), s);
  }
  // The fawn.
  Spec f = fawn(60, CA_IDLE); one("fawn, idle", f);
  f = fawn(60, CA_EAT); f.phase = 0.3f; one("fawn, graze", f);
  f = fawn(60, CA_LOOK); f.tail = 1.f; one("fawn, look, flag 1", f);
  f = fawn(60, CA_SHAKE); f.phase = 0.1f; one("fawn, shake", f);
  f = fawn(60, CA_STARTLE); f.dur = 0.5f; f.at = 0.5f; one("fawn, startle", f);
  f = fawn(60, CA_RUN); f.phase = 0.25f; f.tail = 1.f; one("fawn, canter", f);
  f = fawn(60, CA_RUN); f.phase = 0.75f; f.tail = 1.f; one("fawn, canter, phase 0.75", f);
  f = fawn(60, CA_MOVE); f.phase = 0.25f; one("fawn, walk", f);
  f = fawn(60, CA_REACH); one("fawn, stretch", f);
  f = fawn(60, CA_EAT); f.tone |= kDeerEarBack; one("fawn, graze, ear back", f);
  // Beside a doe.
  v.push_back(Cell{"doe and fawn, both grazing", {deer(78, CA_EAT), fawn(26, CA_EAT)}});
  Spec d = deer(80, CA_LOOK);
  d.tail = 1.f;
  Spec f2 = fawn(30, CA_LOOK);
  f2.tail = 1.f;
  v.push_back(Cell{"doe and fawn, both alert", {d, f2}});
  d = deer(80, CA_EAT);
  f2 = fawn(30, CA_LOOK);
  v.push_back(Cell{"doe grazing, fawn looks up", {d, f2}});
  d = deer(84, CA_MOVE);
  d.phase = 0.2f;
  f2 = fawn(32, CA_MOVE);
  f2.phase = 0.6f;
  v.push_back(Cell{"doe and fawn walking", {d, f2}});
  d = deer(84, CA_RUN);
  d.phase = 0.3f; d.tail = 1.f;
  f2 = fawn(34, CA_RUN);
  f2.phase = 0.9f; f2.tail = 1.f;
  v.push_back(Cell{"doe and fawn cantering", {d, f2}});
  d = deer(40, CA_EAT);
  Spec d2 = deer(96, CA_EAT);
  d2.facing = -1;
  v.push_back(Cell{"two adults, grazing", {d, d2}});
  return v;
}

// ---- strips: real frames in a row, to see how it moves ------------------------------------------------------
// Eight frames to a strip (one row of the sheet), each a real tick of the rig or the herd's own machine
// (herdTick: the ear flick, the flag, the stamp), so what is drawn is what the panel would draw.

// The herd's machine on one deer standing idle, for the values it lays over a Critter.
struct Probe {
  HerdState h;
  Rng rng;
  HerdIn in;
  HerdOut out;
  Probe() {
    rng.seed(1);
    HerdLayout L;
    L.fire = Vec2{250.f, 398.f};
    L.tent = Vec2{206.f, 352.f};
    L.door = Vec2{226.f, 356.f};
    L.splash = Vec2{440.f, 405.f};
    L.camp_x = 560.f;
    herdInit(h, rng, L, 1, false);
    memset(&in, 0, sizeof in);
    in.dt = SS_TICK_DT;
    for (int i = 0; i < HERD_MAX; ++i) {
      in.in_use[i] = true;
      in.arrived[i] = true;
      in.x[i] = 100.f;
    }
    for (int i = 0; i < h.n; ++i) {
      h.d[i].entered = 10;
      h.d[i].mode = HM_CUES;
      h.d[i].cue = 255;
      h.d[i].ear_next = h.d[i].flick_next = 1e9f;
    }
  }
  HerdView tick() {
    herdTick(h, rng, in, out);
    return herdView(h.d[0]);
  }
};

std::vector<Cell> stripCells() {
  std::vector<Cell> v;
  auto add = [&](const std::string& label, Spec s) { v.push_back(Cell{label, {s}}); };
  auto fmt2 = [](const char* f, int a, double b) {
    char t[96];
    snprintf(t, sizeof t, f, a, b);
    return std::string(t);
  };
  // 1 the graze loop over one cycle (1.8 s)
  for (int k = 0; k < 8; ++k) {
    Spec s = deer(60, CA_EAT);
    s.phase = (float)k / 8.f;
    add(fmt2("graze cycle %d of 8 (%.2f)", k + 1, (double)s.phase), s);
  }
  // 2 the doe's head, fast: 0.18 s from grazing, a frame a tick
  for (int k = 0; k < 8; ++k) {
    Spec s = deer(60, CA_LOOK);
    s.from = CA_EAT; s.dur = 0.18f; s.at = (float)k * SS_TICK_DT;
    add(fmt2("head up 0.18 s, tick %d (%.2f s)", k, (double)s.at), s);
  }
  // 3 an ordinary head: 0.55 s, every second tick
  for (int k = 1; k <= 8; ++k) {
    Spec s = deer(60, CA_LOOK);
    s.from = CA_EAT; s.dur = 0.55f; s.at = (float)(2 * k) * SS_TICK_DT;
    add(fmt2("head up 0.55 s, tick %d (%.2f s)", 2 * k, (double)s.at), s);
  }
  // 4 the stretch, from standing: 1.8 s, with its small pull-back first
  for (int k = 1; k <= 8; ++k) {
    Spec s = deer(50, CA_REACH);
    s.dur = 1.8f; s.at = 0.225f * (float)k;
    add(fmt2("stretch, step %d (%.2f s of 1.8)", k, (double)s.at), s);
  }
  // 5 the ear flick as herdTick makes it: the near ear half back, back, back, half back (four ticks)
  {
    Probe p;
    p.h.d[0].ear_next = 1e9f;
    for (int k = 0; k < 8; ++k) {
      if (k == 2) p.h.d[0].ear = 5;   // the flick starts (the tick counts it down to 4 before it is seen)
      const HerdView vw = p.tick();
      Spec s = deer(60, CA_LOOK);
      s.tone = vw.ear;
      char t[96];
      snprintf(t, sizeof t, "ear flick, tick %d: ear %s", k, vw.ear == 0 ? "up" : (vw.ear == kDeerEarMid ? "half back" : "back"));
      add(t, s);
    }
  }
  // 6 the stamp: lift in 0.2 s, hold, strike in 0.08 s (the machine's own curve)
  {
    Probe p;
    p.h.d[0].stamp = 0.f;
    p.h.d[0].stamp_kind = 0;
    int taken = 0;
    for (int k = 0; k < 14 && taken < 8; ++k) {
      const HerdView vw = p.tick();
      if (k == 0 || k == 2 || k == 4 || k == 6 || k == 8 || k == 9 || k == 10 || k == 11) {
        Spec s = deer(60, CA_LOOK);
        s.fore_add = vw.fore_add;
        s.tail = 1.f;
        char t[96];
        snprintf(t, sizeof t, "stamp, tick %d: hoof %.2f rad", k, (double)vw.fore_add);
        add(t, s);
        ++taken;
      }
    }
  }
  // 7 the shake: eight ticks in a row (7 cycles a second: 0.28 of a cycle a tick)
  for (int k = 0; k < 8; ++k) {
    Spec s = deer(60, CA_SHAKE);
    s.phase = 0.28f * (float)k - floorf(0.28f * (float)k);
    add(fmt2("shake, tick %d (phase %.2f)", k, (double)s.phase), s);
  }
  // 8 the flag rising in 0.25 s, on a deer with its head up (the doe)
  {
    Probe p;
    p.h.d[0].tail_to = 1.f;
    p.h.d[0].tail_rate = 4.f;
    for (int k = 0; k < 8; ++k) {
      const HerdView vw = p.tick();
      Spec s = deer(60, CA_LOOK);
      s.tail = vw.tail;
      s.tone = kDeerEarFwd;
      add(fmt2("flag up in 0.25 s, tick %d: %.2f", k, (double)vw.tail), s);
    }
  }
  // 9 the canter, flag up, eight phases of a stride (2.6 strides a second at the middle dial)
  for (int k = 0; k < 8; ++k) {
    Spec s = deer(60, CA_RUN);
    s.phase = (float)k / 8.f;
    s.tail = 1.f;
    add(fmt2("canter %d of 8 (%.2f), flag up", k + 1, (double)s.phase), s);
  }
  // 10 the walk, eight phases
  for (int k = 0; k < 8; ++k) {
    Spec s = deer(60, CA_MOVE);
    s.phase = (float)k / 8.f;
    add(fmt2("walk %d of 8 (%.2f)", k + 1, (double)s.phase), s);
  }
  // 11 the fawn's head, fast (the fawn looks at its mother), the flag following
  for (int k = 0; k < 8; ++k) {
    Spec s = fawn(60, CA_LOOK);
    s.from = CA_EAT; s.dur = 0.3f; s.at = (float)(k + 1) * 0.04f;
    s.tail = k >= 4 ? (float)(k - 3) * 0.25f : 0.f;
    add(fmt2("fawn: head up 0.3 s, tick %d (%.2f s)", k + 1, (double)s.at), s);
  }
  // 12 the grazing doe's tail flick (the flag at 0.35 for four ticks, eased) and a graze with the head half up
  {
    Probe p;
    p.h.d[0].flick = 6;
    for (int k = 0; k < 8; ++k) {
      const HerdView vw = p.tick();
      Spec s = deer(60, CA_EAT);
      s.tail = vw.tail;
      s.phase = 0.2f;
      add(fmt2("tail flick while grazing, tick %d: flag %.2f", k, (double)vw.tail), s);
    }
  }
  return v;
}

struct Look {
  const char* name;
  float mood;
  bool night;
};

Palette paletteFor(const Look& l) {
  WeatherTint wt;
  memset(&wt, 0, sizeof wt);
  if (l.night) {
    wt.night_dim = 1.f;
    wt.night_gain = 1.6f;   // the panel's, as the stage 12 stills
  }
  Palette p;
  computePalette(l.mood, wt, p);
  return p;
}

void backdrop(Surface& s, const Palette& pal, int cx, int cy) {
  const Rect all{0, 0, s.width, s.height};
  fillRect(s, Rect{cx, cy, cx + kCellW, cy + 38}, pal[SKY4], all);
  fillRect(s, Rect{cx, cy + 30, cx + kCellW, cy + kFeetY - 8}, pal[TREES_MID], all);
  fillRect(s, Rect{cx, cy + kFeetY - 12, cx + kCellW, cy + kCellH}, pal[GROUND], all);
  // a few blades so the meadow is not a flat colour
  for (int x = 3; x < kCellW; x += 9) fillRect(s, Rect{cx + x, cy + kFeetY - 2, cx + x + 1, cy + kFeetY + 1}, pal[GRASS], all);
}

bool writePPM(const char* path, const Surface& s) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", s.width, s.height);
  std::vector<unsigned char> row((size_t)s.width * 3);
  for (int y = 0; y < s.height; ++y) {
    for (int x = 0; x < s.width; ++x) {
      const Pixel p = s.pixels[(size_t)y * (size_t)s.stride + (size_t)x];
      row[(size_t)x * 3] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  return fclose(f) == 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: herd_stills OUT_DIR\n");
    return 2;
  }
  const char* out = argv[1];
  rasterSetScratch(calloc(1, rasterScratchBytes()));
  struct Group {
    const char* name;
    std::vector<Cell> cells;
  };
  const Group groups[2] = {{"poses", cells()}, {"strips", stripCells()}};
  static const Look kLooks[3] = {{"day", 3.f, false}, {"golden", 4.f, false}, {"night", 0.f, true}};
  char path[1024];
  snprintf(path, sizeof path, "%s/manifest.txt", out);
  FILE* man = fopen(path, "wb");
  if (!man) {
    printf("cannot write %s\n", path);
    return 1;
  }
  int worst_prims = 0, ncells = 0;
  for (const Group& g : groups) {
    const std::vector<Cell>& cs = g.cells;
    const int rows = ((int)cs.size() + kCols - 1) / kCols;
    const int W = kCols * kCellW, H = rows * kCellH;
    for (const Look& lk : kLooks) {
      if (std::string(g.name) == "strips" && std::string(lk.name) == "golden") continue;   // the strips: day and night
      const Palette pal = paletteFor(lk);
      std::vector<Pixel> px((size_t)W * (size_t)H, 0);
      Surface s{px.data(), W, H, W, 0};
      for (size_t i = 0; i < cs.size(); ++i) {
        const int cx = (int)(i % kCols) * kCellW, cy = (int)(i / kCols) * kCellH;
        backdrop(s, pal, cx, cy);
        const Rect clip{cx, cy, cx + kCellW, cy + kCellH};
        for (const Spec& sp : cs[i].deer) {
          const Critter c = make(sp, cx, cy);
          critterDraw(c, s, pal, clip);
          if (critterLastDrawPrimitives() > worst_prims) worst_prims = critterLastDrawPrimitives();
          const Rect b = critterBounds(c);
          if (b.x0 < clip.x0 || b.x1 > clip.x1 || b.y0 < clip.y0 || b.y1 > clip.y1)
            printf("note: %s (%s) reaches out of its cell\n", cs[i].label.c_str(), lk.name);
        }
        fprintf(man, "%s_%s.ppm|%s|%d|%d|%d|%d\n", g.name, lk.name, cs[i].label.c_str(), cx, cy, cx + kCellW, cy + kCellH);
        ++ncells;
      }
      snprintf(path, sizeof path, "%s/%s_%s.ppm", out, g.name, lk.name);
      if (!writePPM(path, s)) {
        printf("cannot write %s\n", path);
        return 1;
      }
    }
  }
  fclose(man);
  printf("%d cells in all, most primitives in one deer %d\n", ncells, worst_prims);
  return 0;
}
