// Tests for the camp dog (core/scene/critters.cpp CR_DOG) and the fetch major
// (core/scene/fetch.cpp, MORNING_DAYTIME_PLAN.md section 1):
//
//   [1] the rig at every pose: every pixel inside critterBounds, the primitive
//       budget, both facings, both browns, both sizes, every carry, the wag
//   [2] drawing clipped to a 32-row strip equals drawing the whole frame
//   [3] each fetch variant plays to the end: no overrun, the script done with
//       slack, no critter left, him seated at rest with his hands empty, the
//       dog and the stick out of the chip's corner, and each variant's own
//       events seen (the catch, the wading and the spray, the branch)
//   [4] dog_out forces a fetch into every midday loop (and never into the
//       other families); the plan reads it at loop start only
//   [5] with both dogs out a second, smaller dog in a darker brown joins; with
//       one out, none
//   [6] the doorbell freeze: the dog, the stick and the beat's clock stand still
//       for the whole reaction and then carry on
//   [7] drawn tick by tick the beat equals a full render; the strips it costs
//
// It also writes dog_poses_day.ppm and dog_poses_night.ppm (the rig in every
// pose at 2x) to the current directory. Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/critters.h"
#include "scene/director.h"
#include "scene/fetch.h"
#include "scene/majors.h"
#include "scene/spine_family.h"
#include "scene/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utility>
#include <vector>

using namespace ss;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      ++g_fail;                                                   \
      printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
      printf(__VA_ARGS__);                                        \
      printf("\n");                                               \
    }                                                             \
  } while (0)

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

// ---- drawing helpers ---------------------------------------------------------------------

struct Frame {
  std::vector<Pixel> px;
  Surface s;
  Frame(int w, int h, Pixel fill) : px((size_t)w * (size_t)h, fill) { s = Surface{px.data(), w, h, w, 0}; }
  Pixel at(int x, int y) const { return s.row(y)[x]; }
  Rect all() const { return Rect{0, 0, s.width, s.height}; }
};

static bool writePPM(const char* path, const Frame& f) {
  FILE* fp = fopen(path, "wb");
  if (!fp) return false;
  fprintf(fp, "P6\n%d %d\n255\n", f.s.width, f.s.height);
  std::vector<unsigned char> row((size_t)f.s.width * 3);
  for (int y = 0; y < f.s.height; ++y) {
    for (int x = 0; x < f.s.width; ++x) {
      const Pixel p = f.at(x, y);
      row[(size_t)x * 3 + 0] = red8(p);
      row[(size_t)x * 3 + 1] = green8(p);
      row[(size_t)x * 3 + 2] = blue8(p);
    }
    fwrite(row.data(), 1, row.size(), fp);
  }
  return fclose(fp) == 0;
}

static void held(Critter& c, int a, float phase) {
  critterSetAction(c, (CritterAction)a, 0.f);
  c.loop_w = 1.f;
  c.phase = phase;
  critterTick(c, 0.f);
}

// A dog in action `a` at gait/loop phase `ph`: a held pose, or a gait at full weight.
static Critter dogIn(int a, float ph, int facing, float scale, uint8_t tone, uint8_t carry, float tail) {
  Critter c;
  critterInit(c, CR_DOG, Vec2{140.f, 120.f}, facing);
  c.scale = scale;
  c.tone = tone;
  c.carry = carry;
  c.tail_add = tail;
  if (a == CA_MOVE || a == CA_RUN) {
    critterSetAction(c, (CritterAction)a, 0.f);
    c.moving = true;
    c.gait_w = 1.f;
    c.phase = ph;
    c.move_from = c.move_to = c.feet.x;
    critterTick(c, 0.f);
  } else if (a == CA_HOP) {
    critterSetAction(c, CA_HOP, 1.f);
    for (int t = 0; t < (int)(ph * 25.f); ++t) critterTick(c, SS_TICK_DT);
  } else {
    held(c, a, ph);
  }
  return c;
}

// The colour most inked pixels have (the fur; the nose, ear and legs are fewer).
static Pixel dominantInk(const Frame& f, Pixel bg) {
  std::vector<std::pair<Pixel, int>> hist;
  for (Pixel p : f.px) {
    if (p == bg) continue;
    bool found = false;
    for (auto& h : hist)
      if (h.first == p) {
        ++h.second;
        found = true;
        break;
      }
    if (!found) hist.push_back({p, 1});
  }
  Pixel best = bg;
  int n = 0;
  for (auto& h : hist)
    if (h.second > n) {
      n = h.second;
      best = h.first;
    }
  return best;
}

static int inked(const Frame& f, Pixel bg) {
  int n = 0;
  for (Pixel p : f.px) n += p != bg;
  return n;
}

// ---- [1] and [2] the rig ------------------------------------------------------------------------

static void testRig() {
  printf("[1] the dog's rig: every pose inside its bounds, the budget, the wag\n");
  const Palette& pal = moodKey(MOOD_DAY);
  const int W = 330, H = 200;
  int states = 0, outside = 0, drawn = 0, max_prims = 0;
  static const int kActs[] = {CA_IDLE, CA_MOVE, CA_RUN, CA_SIT, CA_LOOK, CA_SNIFF, CA_SHAKE, CA_HOP, CA_CARRY, CA_LIE, CA_BOW};
  static const uint8_t kCarries[3] = {CC_NONE, CC_STICK, CC_BRANCH};
  static const float kTails[3] = {-0.8f, 0.f, 0.8f};
  for (int a : kActs) {
    CHECK(critterSupports(CR_DOG, (CritterAction)a), "the dog does not support %s", critterActionName((CritterAction)a));
    for (int facing = -1; facing <= 1; facing += 2)
      for (int variant = 0; variant < 3; ++variant)
        for (int ph = 0; ph < 4; ++ph) {
          const float scale = variant == 2 ? 0.8f : 1.f;
          const uint8_t tone = variant == 1 ? 1 : 0;
          Critter c = dogIn(a, 0.1f + 0.25f * (float)ph, facing, scale, tone, kCarries[(ph + variant) % 3], kTails[(ph + a) % 3]);
          c.feet = Vec2{200.f, 130.f};
          critterTick(c, 0.f);
          Frame f(W, H, pal[GROUND]);
          critterDraw(c, f.s, pal, f.all());
          max_prims = critterLastDrawPrimitives() > max_prims ? critterLastDrawPrimitives() : max_prims;
          const Rect b = critterBounds(c);
          for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
              if (f.at(x, y) == pal[GROUND]) continue;
              ++drawn;
              if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) ++outside;
            }
          ++states;
        }
  }
  printf("  %d poses (both facings, two browns, two sizes, three carries, three wags), %d drawn pixels, %d outside bounds, max %d primitives (budget %d)\n",
         states, drawn, outside, max_prims, critterBudget(CR_DOG));
  CHECK(outside == 0, "%d drawn pixels fell outside critterBounds", outside);
  CHECK(max_prims <= critterBudget(CR_DOG), "the dog draws %d primitives", max_prims);
  CHECK(drawn > 0, "the dog drew nothing");

  // The wag moves only the tail (it is the caller's offset on the drawn pose).
  {
    const Critter a = dogIn(CA_SIT, 0.f, 1, 1.f, 0, CC_NONE, -0.6f), b = dogIn(CA_SIT, 0.f, 1, 1.f, 0, CC_NONE, 0.6f);
    Frame fa(W, H, pal[GROUND]), fb(W, H, pal[GROUND]);
    critterDraw(a, fa.s, pal, fa.all());
    critterDraw(b, fb.s, pal, fb.all());
    int diff = 0;
    for (size_t i = 0; i < fa.px.size(); ++i) diff += fa.px[i] != fb.px[i];
    printf("  a held sit with the tail swung each way differs in %d pixels\n", diff);
    CHECK(diff > 20, "the wag does not show in a held sit");
    CHECK(diff * 4 < inked(fa, pal[GROUND]), "the wag moved more than the tail: %d of %d", diff, inked(fa, pal[GROUND]));
  }
  // The browns differ, and each stays clear of the ground colour.
  {
    const Critter a = dogIn(CA_IDLE, 0.f, 1, 1.f, 0, CC_NONE, 0.f), b = dogIn(CA_IDLE, 0.f, 1, 1.f, 1, CC_NONE, 0.f);
    Frame fa(W, H, pal[GROUND]), fb(W, H, pal[GROUND]);
    critterDraw(a, fa.s, pal, fa.all());
    critterDraw(b, fb.s, pal, fb.all());
    const Pixel ca = dominantInk(fa, pal[GROUND]), cb = dominantInk(fb, pal[GROUND]);   // the fur: most of what is drawn
    printf("  fur colours by day: tone 0 %04x, tone 1 %04x, ground %04x\n", ca, cb, pal[GROUND]);
    CHECK(ca != cb, "the two browns are the same");
    CHECK(ca != pal[GROUND] && cb != pal[GROUND], "the fur is the ground colour");
  }
}

static void testStrips() {
  printf("[2] clipped to a 32-row strip the dog draws what a whole-frame draw does\n");
  const Palette& pal = moodKey(MOOD_DAY);
  const int W = 330, H = 192;
  int bad = 0, n = 0;
  static const int kActs[] = {CA_IDLE, CA_MOVE, CA_RUN, CA_SIT, CA_SNIFF, CA_HOP, CA_CARRY, CA_LIE, CA_BOW};
  for (int a : kActs)
    for (int carry = 0; carry < 3; ++carry)
      for (int row = 0; row < 3; ++row) {   // feet at several heights, so strip edges cut it at several places
        Critter c = dogIn(a, 0.3f, row == 1 ? -1 : 1, 1.f, (uint8_t)(row & 1), (uint8_t)(carry == 0 ? CC_NONE : (carry == 1 ? CC_STICK : CC_BRANCH)), 0.4f);
        c.feet = Vec2{200.f, 90.f + 17.f * (float)row + 1.f * (float)a};
        critterTick(c, 0.f);
        Frame whole(W, H, pal[GROUND]), strips(W, H, pal[GROUND]);
        critterDraw(c, whole.s, pal, whole.all());
        for (int y0 = 0; y0 < H; y0 += 32) critterDraw(c, strips.s, pal, Rect{0, y0, W, y0 + 32 < H ? y0 + 32 : H});
        ++n;
        if (whole.px != strips.px) ++bad;
      }
  printf("  %d poses drawn both ways, %d differ\n", n, bad);
  CHECK(bad == 0, "%d poses draw differently in strips", bad);
  // The stick glyph too.
  {
    int sbad = 0;
    for (int r = 0; r < 8; ++r) {
      Glyph g{};
      g.kind = GL_SHAPE;
      g.shape = SH_STICK;
      g.pos = Vec2{150.f + 3.f * (float)r, 60.f + 11.f * (float)r};
      g.rot = 0.125f * (float)r;
      g.scale = 1.f;
      Frame whole(W, H, pal[GROUND]), strips(W, H, pal[GROUND]);
      glyphDraw(g, whole.s, whole.all(), pal);
      for (int y0 = 0; y0 < H; y0 += 32) glyphDraw(g, strips.s, Rect{0, y0, W, y0 + 32 < H ? y0 + 32 : H}, pal);
      const Rect b = glyphBounds(g);
      int outside = 0;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
          if (whole.at(x, y) != pal[GROUND] && (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1)) ++outside;
      if (whole.px != strips.px || outside || inked(whole, pal[GROUND]) == 0) ++sbad;
    }
    CHECK(sbad == 0, "%d stick glyph angles draw outside their bounds, differently in strips, or not at all", sbad);
  }
}

// ---- the poses sheet ------------------------------------------------------------------------------

// Every pose of the rig at 2x on the ground colour: the dog's actions, a trot and a gallop at six phases,
// the carries and the second dog. One cell is 120 x 100 px.
static void sheetPoses(const char* path, Mood mood) {
  const Palette& pal = moodKey(mood);
  const float S = 2.f;
  const int cw = 120, ch = 100, cols = 6;
  std::vector<Critter> cells;
  static const int kActs[] = {CA_IDLE, CA_LOOK, CA_SNIFF, CA_SIT, CA_LIE, CA_BOW, CA_SHAKE, CA_HOP, CA_CARRY};
  for (int a : kActs) cells.push_back(dogIn(a, a == CA_HOP ? 0.5f : 0.3f, 1, S, 0, a == CA_CARRY ? CC_STICK : CC_NONE, 0.f));
  for (int g = 0; g < 2; ++g)
    for (int i = 0; i < 6; ++i) cells.push_back(dogIn(g == 0 ? CA_MOVE : CA_RUN, (float)i / 6.f, 1, S, 0, CC_NONE, 0.f));
  // the second dog: smaller, darker: standing, sitting (wagging), bowing
  cells.push_back(dogIn(CA_IDLE, 0.f, -1, 0.8f * S, 1, CC_NONE, 0.f));
  cells.push_back(dogIn(CA_SIT, 0.f, -1, 0.8f * S, 1, CC_NONE, 0.5f));
  cells.push_back(dogIn(CA_BOW, 0.f, 1, 0.8f * S, 1, CC_NONE, 0.5f));
  const int rows = ((int)cells.size() + cols - 1) / cols + 1;   // + a row for the branch, which is long
  Frame f(cw * cols, ch * rows, pal[GROUND]);
  for (size_t i = 0; i < cells.size(); ++i) {
    Critter c = cells[i];
    c.feet = Vec2{(float)((int)(i % (size_t)cols) * cw + cw / 2), (float)((int)(i / (size_t)cols) * ch + ch - 14)};
    critterTick(c, 0.f);
    critterDraw(c, f.s, pal, Rect{0, 0, f.s.width, f.s.height});
  }
  {   // the branch: carried by one end, the rest dragging behind
    Critter c = dogIn(CA_MOVE, 0.2f, 1, S, 0, CC_BRANCH, 0.f);
    c.feet = Vec2{(float)(cw * 5 - 20), (float)((rows - 1) * ch + ch - 14)};
    critterTick(c, 0.f);
    critterDraw(c, f.s, pal, Rect{0, 0, f.s.width, f.s.height});
  }
  CHECK(writePPM(path, f), "could not write %s", path);
}

// ---- fetch: the plan ------------------------------------------------------------------------------

static Inputs makeInputs(float mood, int force, int dog) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.spine_force = (uint8_t)(force >= 0 ? 1 + force : 0);
  in.have_outside = true;   // clear weather from "data"
  in.dog_out = dog;
  return in;
}

struct Rig {
  void* mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;
  bool start(uint64_t seed, const Inputs& in) {
    mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = (mem && fb) ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    free(fb);
    app = nullptr;
  }
};

// The plan's fetch: its index, or -1.
static int fetchIndex(const LoopPlan& p) {
  for (int i = 0; i < p.n_majors; ++i)
    if (p.major[i] == MJ_FETCH) return i;
  return -1;
}

static void testPlan() {
  printf("[4] dog_out forces a fetch into every midday loop; the other families never play it\n");
  int natural = 0, forced_n = 0, second_with_3 = 0, second_other = 0, bad_count = 0, plans = 0;
  for (int dog = 0; dog <= 3; ++dog)
    for (uint64_t seed = 1; seed <= 150; ++seed) {
      Rig r;
      CHECK(r.start(seed, makeInputs(3.f, SF_MIDDAY, dog)), "start");
      const LoopPlan& p = appWorld(r.app)->plan;
      const int i = fetchIndex(p);
      ++plans;
      if (p.n_majors < 2 || p.n_majors > 4) ++bad_count;
      if (dog != 0) {
        CHECK(i >= 0, "dog_out %d seed %d: the midday plan has no fetch", dog, (int)seed);
        if (i >= 0) {
          CHECK((p.major_var[i] & FETCH_FORCED) != 0, "dog_out %d seed %d: the fetch is not marked forced", dog, (int)seed);
          ++forced_n;
          const bool second = (p.major_var[i] & FETCH_SECOND) != 0;
          if (dog == 3) second_with_3 += second;
          else second_other += second;
        }
      } else if (i >= 0) {
        ++natural;
        CHECK((p.major_var[i] & (FETCH_FORCED | FETCH_SECOND)) == 0, "seed %d: a natural fetch carries the house's flags", (int)seed);
      }
      r.stop();
    }
  printf("  %d plans: forced in all %d with a dog out; without one it came up %d times in 150 (weight 1.5); a second dog in %d of 150 with both out, %d with one\n",
         plans, forced_n, natural, second_with_3, second_other);
  CHECK(bad_count == 0, "%d plans with fewer than 2 or more than 4 majors", bad_count);
  CHECK(natural >= 35 && natural <= 75, "a fetch came up in %d of 150 midday loops with no dog out (PROPOSED: about one in three)", natural);
  CHECK(second_with_3 == 150, "with both dogs out the second dog is planned in %d of 150", second_with_3);
  CHECK(second_other == 0, "a second dog planned with one dog out (%d)", second_other);

  // The other families: a dog out does not put a fetch there.
  int stray = 0;
  for (int fam : {(int)SF_EVENING, (int)SF_MORNING})
    for (uint64_t seed = 1; seed <= 60; ++seed) {
      Rig r;
      CHECK(r.start(seed, makeInputs(fam == SF_MORNING ? 1.5f : 4.f, fam, 3)), "start");
      stray += fetchIndex(appWorld(r.app)->plan) >= 0;
      r.stop();
    }
  CHECK(stray == 0, "%d evening or morning plans hold a fetch", stray);

  // Read at loop start only: the badge going on mid-loop changes the next loop, not this one.
  {
    Rig r;
    uint64_t seed = 1;   // a seed whose loop 0 has no fetch with no dog out
    for (; seed < 200; ++seed) {
      Rig q;
      q.start(seed, makeInputs(3.f, SF_MIDDAY, 0));
      const bool has = fetchIndex(appWorld(q.app)->plan) >= 0;
      q.stop();
      if (!has) break;
    }
    CHECK(r.start(seed, makeInputs(3.f, SF_MIDDAY, 0)), "start");
    const LoopPlan before = appWorld(r.app)->plan;
    appSetInputs(r.app, makeInputs(3.f, SF_MIDDAY, 1));
    for (int t = 0; t < 300; ++t) appTick(r.app);
    CHECK(memcmp(&before, &appWorld(r.app)->plan, sizeof before) == 0, "the plan moved when dog_out changed mid-loop");
    for (int guard = 0; guard < 40 * 60 * SS_TICK_HZ && appWorld(r.app)->memory.loop_count < 2; ++guard) appTick(r.app);
    CHECK(fetchIndex(appWorld(r.app)->plan) >= 0, "the next loop after the badge came on has no fetch");
    r.stop();
  }
  // Loop after loop with a dog out: every midday loop has its fetch (the ring and the recent sets do not hold it back).
  {
    Rig r;
    CHECK(r.start(7, makeInputs(3.f, SF_MIDDAY, 1)), "start");
    int loops = 0, missing = 0;
    uint32_t seen = 0;
    for (int guard = 0; guard < 12 * 700 * SS_TICK_HZ && loops < 8; ++guard) {
      const World* w = appWorld(r.app);
      if (loops == 0 || w->memory.loop_count != seen) {
        seen = w->memory.loop_count;
        ++loops;
        if (fetchIndex(w->plan) < 0) ++missing;
      }
      appTick(r.app);
    }
    printf("  %d midday loops in a row with a dog out: %d without a fetch\n", loops, missing);
    CHECK(loops >= 8 && missing == 0, "%d of %d loops had no fetch", missing, loops);
    r.stop();
  }
}

// ---- fetch: the beat ------------------------------------------------------------------------------

// The status chip's box (HOW_TO_ADD_SCENES.md 8.1): x 538..796, y 412..476.
static bool inChip(const Rect& r) { return r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }

struct Play {
  int overruns = -1, critters_end = -1, in_chip = 0, off_frame = 0, ticks = 0;
  float slack_s = 0.f;
  bool carried_stick = false, carried_branch = false, wading = false, spray = false, shook = false;
  bool two_dogs = false, empty_gap = false, hiker_ok = false, sat = false, lie = false, bow = false, hop = false;
  uint8_t tones[2] = {0, 0};
  float scales[2] = {0.f, 0.f};
  float max_dogs_apart = 0.f;
};

// Play the loop through the fetch beat. Returns false when the loop has none.
static bool playFetch(uint64_t seed, int dog, Play& pl, int* var_out) {
  Rig r;
  if (!r.start(seed, makeInputs(3.f, SF_MIDDAY, dog))) return false;
  const World* w = appWorld(r.app);
  const int idx = fetchIndex(w->plan);
  if (idx < 0) {
    r.stop();
    return false;
  }
  if (var_out) *var_out = w->plan.major_var[idx];
  const uint32_t len = appLoopTicks(r.app);
  bool in_beat = false, seen_dog_once = false, gap_open = false;
  for (uint32_t t = 0; t < len; ++t) {
    const bool f = strcmp(appBeatName(r.app), "fetch") == 0;
    if (f) {
      in_beat = true;
      ++pl.ticks;
      int dogs = 0;
      Vec2 pos[2] = {};
      for (int i = 0; i < CRITTER_MAX; ++i) {
        const Critter& c = w->critters[i];
        if (!(w->crit[i].in_use && w->crit[i].tag == CT_MAJOR && c.kind == CR_DOG)) continue;
        Rect cr;
        int k = -1;
        const bool on = appCritterRect(r.app, i, &cr, &k);
        if (dogs < 2) {
          pos[dogs] = c.feet;
          pl.tones[dogs] = c.tone;
          pl.scales[dogs] = c.scale;
        }
        ++dogs;
        seen_dog_once = true;
        if (c.carry == CC_STICK) pl.carried_stick = true;
        if (c.carry == CC_BRANCH) pl.carried_branch = true;
        if (c.action == CA_SHAKE) pl.shook = true;
        if (c.action == CA_SIT) pl.sat = true;
        if (c.action == CA_LIE) pl.lie = true;
        if (c.action == CA_BOW) pl.bow = true;
        if (c.action == CA_HOP) pl.hop = true;
        if (on && inChip(cr)) ++pl.in_chip;
        if (on && (cr.y0 < 0 || cr.y1 > SS_HEIGHT)) ++pl.off_frame;
      }
      if (dogs == 2) {
        pl.two_dogs = true;
        const float d = pos[0].x > pos[1].x ? pos[0].x - pos[1].x : pos[1].x - pos[0].x;
        pl.max_dogs_apart = d > pl.max_dogs_apart ? d : pl.max_dogs_apart;
      }
      if (seen_dog_once && dogs == 0) gap_open = true;
      if (gap_open && dogs > 0) pl.empty_gap = true;   // gone, then back (the wrong stick)
      for (const Glyph& g : w->major.g) {
        if (g.kind == GL_NONE) continue;
        if (g.kind == GL_WATER) pl.wading = true;
        if (g.kind == GL_DOT && g.slot == WATER_LIGHT) pl.spray = true;
        const Rect gb = glyphBounds(g);
        if (g.kind != GL_WATER && !gb.empty() && inChip(gb)) ++pl.in_chip;
      }
      Rect hr;
      if (appHikerBounds(r.app, &hr) && inChip(hr)) ++pl.in_chip;
    } else if (in_beat) {   // the beat has just ended
      pl.overruns = appSpineOverruns(r.app);
      pl.critters_end = appCritterCount(r.app);
      const SpineState& sp = w->spine;
      pl.slack_s = sp.run.done_at ? (float)(sp.run.beat_ticks - sp.run.done_at) / (float)SS_TICK_HZ : -1.f;
      Rect hr;
      pl.hiker_ok = appHikerBounds(r.app, &hr) && hr.x0 >= 0 && hr.x1 <= SS_WIDTH && w->hiker.carry == CARRY_NONE && w->hiker.hat_on &&
                    !w->hiker.walking && w->hiker.target == POSE_SIT;
      break;
    }
    appTick(r.app);
  }
  r.stop();
  return in_beat;
}

struct Kind {
  int var_mask, var_val;
  const char* name;
};

static void testPlays() {
  printf("[3] each fetch variant plays to the end\n");
  static const Kind kKinds[4] = {{3 | 16, 0, "good fetch, two throws"}, {3 | 16, 16, "good fetch, three throws"},
                                 {3, 1, "into the stream"}, {3, 2, "the wrong stick"}};
  for (int k = 0; k < 4; ++k) {
    int played = 0;
    float worst_slack = 1e9f;
    Play merged;
    for (uint64_t seed = 1; seed <= 400 && played < 8; ++seed) {
      const int dog = (seed & 1u) ? 3 : 1;   // with one dog out and with both
      Rig q;
      q.start(seed, makeInputs(3.f, SF_MIDDAY, dog));
      const int i = fetchIndex(appWorld(q.app)->plan);
      const int var = i >= 0 ? appWorld(q.app)->plan.major_var[i] : -1;
      q.stop();
      if (i < 0 || (var & kKinds[k].var_mask) != kKinds[k].var_val) continue;
      Play pl;
      CHECK(playFetch(seed, dog, pl, nullptr), "seed %d: the fetch beat never played", (int)seed);
      ++played;
      CHECK(pl.overruns == 0, "%s, seed %d: %d overruns", kKinds[k].name, (int)seed, pl.overruns);
      CHECK(pl.critters_end == 0, "%s, seed %d: %d critters left at the end of the beat", kKinds[k].name, (int)seed, pl.critters_end);
      CHECK(pl.slack_s >= 3.f, "%s, seed %d: the script finished %.1f s before the beat's end (want 3 s or more; -1 is unfinished)", kKinds[k].name,
            (int)seed, (double)pl.slack_s);
      CHECK(pl.hiker_ok, "%s, seed %d: he is not in frame, seated at rest, hat on, hands empty at the end", kKinds[k].name, (int)seed);
      CHECK(pl.in_chip == 0, "%s, seed %d: %d samples of the dog, the hiker or a glyph in the chip's box", kKinds[k].name, (int)seed, pl.in_chip);
      CHECK(pl.off_frame == 0, "%s, seed %d: the dog is out of the frame vertically %d times", kKinds[k].name, (int)seed, pl.off_frame);
      if (pl.slack_s < worst_slack) worst_slack = pl.slack_s;
      merged.carried_stick |= pl.carried_stick;
      merged.carried_branch |= pl.carried_branch;
      merged.wading |= pl.wading;
      merged.spray |= pl.spray;
      merged.shook |= pl.shook;
      merged.empty_gap |= pl.empty_gap;
      merged.lie |= pl.lie;
      merged.bow |= pl.bow;
      merged.hop |= pl.hop;
      merged.sat |= pl.sat;
      printf("  %-26s seed %3d: %.1f s of the beat, script done %.1f s early\n", kKinds[k].name, (int)seed, (double)pl.ticks / SS_TICK_HZ,
             (double)pl.slack_s);
    }
    CHECK(played >= 1, "%s never came up in 400 seeds", kKinds[k].name);
    printf("  %-26s worst slack %.1f s over %d plays\n", kKinds[k].name, (double)worst_slack, played);
    CHECK(merged.carried_stick, "%s: the dog never carried the stick", kKinds[k].name);
    CHECK(merged.bow, "%s: the dog never bowed", kKinds[k].name);
    if (k <= 1) {
      CHECK(merged.hop, "%s: the dog never hopped (the catch on the bounce)", kKinds[k].name);
      CHECK(merged.lie, "%s: the dog did not end lying down beside him", kKinds[k].name);
    }
    if (k == 2) {
      CHECK(merged.wading, "%s: nothing waded (no water patch over its legs)", kKinds[k].name);
      CHECK(merged.spray, "%s: no spray", kKinds[k].name);
      CHECK(merged.shook, "%s: it never shook", kKinds[k].name);
      CHECK(merged.sat, "%s: it never sat", kKinds[k].name);
    }
    if (k == 3) {
      CHECK(merged.carried_branch, "%s: no branch", kKinds[k].name);
      CHECK(merged.empty_gap, "%s: the scene was never empty of dogs and then not", kKinds[k].name);
    }
  }
}

static void testSecondDog() {
  printf("[5] both dogs out: a second, smaller dog in another brown; one out: none\n");
  int two = 0, one_two = 0, plays = 0;
  float apart = 0.f;
  for (uint64_t seed = 1; seed <= 12; ++seed) {
    Play a, b;
    if (!playFetch(seed, 3, a, nullptr)) continue;
    ++plays;
    two += a.two_dogs;
    CHECK(a.two_dogs, "seed %d: dog_out 3 and never two dogs at once", (int)seed);
    if (a.two_dogs) {
      CHECK(a.tones[0] != a.tones[1], "seed %d: the two dogs are the same brown", (int)seed);
      CHECK((a.scales[0] < a.scales[1] ? a.scales[0] : a.scales[1]) < 0.9f, "seed %d: the second dog is not smaller (%.2f, %.2f)", (int)seed,
            (double)a.scales[0], (double)a.scales[1]);
    }
    apart = a.max_dogs_apart > apart ? a.max_dogs_apart : apart;
    CHECK(a.overruns == 0 && a.critters_end == 0 && a.in_chip == 0, "seed %d, two dogs: overruns %d, left %d, chip %d", (int)seed, a.overruns,
          a.critters_end, a.in_chip);
    CHECK(a.slack_s >= 3.f, "seed %d, two dogs: slack %.1f s", (int)seed, (double)a.slack_s);
    if (playFetch(seed, 1, b, nullptr)) one_two += b.two_dogs;
  }
  printf("  %d fetches with both out, two dogs in %d; with one out, two dogs in %d; the dogs get up to %.0f px apart\n", plays, two, one_two,
         (double)apart);
  CHECK(plays >= 6, "only %d fetch plays", plays);
  CHECK(one_two == 0, "two dogs with one out (%d plays)", one_two);
}

// ---- the doorbell ---------------------------------------------------------------------------------

struct Snap {
  float fx[2], fy[2], at[2], ph[2];
  float t, st0;
  int glyphs;
  float gsum;
};

static Snap snapOf(const World& w) {
  Snap s;
  memset(&s, 0, sizeof s);
  int n = 0;
  for (int i = 0; i < CRITTER_MAX; ++i)
    if (w.crit[i].in_use && w.critters[i].kind == CR_DOG && n < 2) {
      s.fx[n] = w.critters[i].feet.x;
      s.fy[n] = w.critters[i].feet.y;
      s.at[n] = w.critters[i].action_t;
      s.ph[n] = w.critters[i].phase;
      ++n;
    }
  s.t = w.major.t;
  s.st0 = w.major.st[0];
  for (const Glyph& g : w.major.g)
    if (g.kind != GL_NONE) {
      ++s.glyphs;
      s.gsum += g.pos.x + 3.f * g.pos.y + 7.f * g.rot + 11.f * g.t;
    }
  return s;
}
static bool sameSnap(const Snap& a, const Snap& b) { return memcmp(&a, &b, sizeof a) == 0; }

// One of the beat's dogs is in the scene and in sight.
static bool dogOut(const World& w) {
  for (int i = 0; i < CRITTER_MAX; ++i)
    if (w.crit[i].in_use && w.crit[i].tag == CT_MAJOR && w.critters[i].kind == CR_DOG && w.critters[i].visible) return true;
  return false;
}

static void testFreeze() {
  printf("[6] the doorbell freezes the dog, the stick and the beat\n");
  int tried = 0, froze = 0;
  for (int kind = 0; kind < 3; ++kind) {   // good fetch, stream, wrong stick, each rung at a few moments
    for (int at = 0; at < 3; ++at) {
      uint64_t seed = 1;   // a two-throw seed of this kind, both dogs out
      for (; seed < 400; ++seed) {
        Rig q;
        q.start(seed, makeInputs(3.f, SF_MIDDAY, 3));
        const int i = fetchIndex(appWorld(q.app)->plan);
        const int var = i >= 0 ? appWorld(q.app)->plan.major_var[i] : -1;
        q.stop();
        if (i >= 0 && (var & 3) == kind && !(var & FETCH_THREE)) break;
      }
      Rig r;
      CHECK(r.start(seed, makeInputs(3.f, SF_MIDDAY, 3)), "start");
      const World* w = appWorld(r.app);
      uint32_t t = 0;
      const uint32_t len = appLoopTicks(r.app);
      for (; t < len && strcmp(appBeatName(r.app), "fetch") != 0; ++t) appTick(r.app);
      // into the beat by a different amount each time, then until a dog is out and he can react
      const uint32_t start = t;
      const uint32_t skip = 150u + 200u * (uint32_t)at + 100u * (uint32_t)kind;
      for (; t < start + skip && t < len; ++t) appTick(r.app);
      // (a dog of the beat's own on screen; the beat must still be running)
      for (int guard = 0; guard < 1500 && strcmp(appBeatName(r.app), "fetch") == 0 && !dogOut(*w); ++guard) {
        appTick(r.app);
        ++t;
      }
      if (strcmp(appBeatName(r.app), "fetch") != 0 || !dogOut(*w)) {
        r.stop();
        continue;
      }
      ++tried;
      Event e;
      e.type = EV_DOORBELL;
      e.arg = 0;
      appPostEvent(r.app, e);
      appTick(r.app);   // the bell starts the reaction
      ++t;
      if (!w->react.active) {   // he could not react just then (out of frame): nothing to freeze
        r.stop();
        continue;
      }
      const Snap s0 = snapOf(*w);
      bool still = true;
      int n = 0;
      while (w->react.active && n < 200) {
        appTick(r.app);
        ++t;
        ++n;
        if (w->react.active && !sameSnap(s0, snapOf(*w))) still = false;
      }
      CHECK(n >= 80 && n <= 90, "kind %d: the reaction lasted %d ticks", kind, n);
      CHECK(still, "kind %d seed %d: the dog, the stick or the beat's clock moved during the doorbell reaction", kind, (int)seed);
      froze += still;
      for (int k = 0; k < 40; ++k) {   // and then it carries on
        appTick(r.app);
        ++t;
      }
      CHECK(!sameSnap(s0, snapOf(*w)), "kind %d: nothing moved after the reaction", kind);
      while (strcmp(appBeatName(r.app), "fetch") == 0 && t < len + 4000) {   // to the end of the beat
        appTick(r.app);
        ++t;
      }
      CHECK(appSpineOverruns(r.app) == 0 && appCritterCount(r.app) == 0, "kind %d seed %d: after the bell the beat overran (%d) or left %d critters",
            kind, (int)seed, appSpineOverruns(r.app), appCritterCount(r.app));
      r.stop();
    }
  }
  printf("  %d bells rung into the beat, the scene still through the whole reaction in %d\n", tried, froze);
  CHECK(froze >= 6, "only %d bells found him able to react", froze);
}

// ---- drawing: incremental equals full, and the strips the beat costs -------------------------------

static void testIncremental() {
  printf("[7] drawn tick by tick the beat equals a full render; strips per frame\n");
  for (int kind = 0; kind < 3; ++kind) {
    uint64_t seed = 1;   // both dogs out, the worst case: two dogs, spray, a water patch
    for (; seed < 400; ++seed) {
      Rig q;
      q.start(seed, makeInputs(3.f, SF_MIDDAY, 3));
      const int i = fetchIndex(appWorld(q.app)->plan);
      const int var = i >= 0 ? appWorld(q.app)->plan.major_var[i] : -1;
      q.stop();
      if (i >= 0 && (var & 3) == kind && !(var & FETCH_THREE)) break;
    }
    Rig a, b;
    CHECK(a.start(seed, makeInputs(3.f, SF_MIDDAY, 3)) && b.start(seed, makeInputs(3.f, SF_MIDDAY, 3)), "start");
    const uint32_t len = appLoopTicks(a.app);
    uint32_t t = 0;
    for (; t < len && strcmp(appBeatName(a.app), "fetch") != 0; ++t) {
      appTick(a.app);
      appTick(b.app);
      appRender(a.app, a.surf);   // (every tick, so the dirty marks are the real ones)
    }
    long sum = 0, frames = 0;
    int worst = 0, diffs = 0, checks = 0;
    for (int n = 0; strcmp(appBeatName(a.app), "fetch") == 0 && t < len; ++t, ++n) {
      appTick(a.app);
      appTick(b.app);
      const int strips = appRender(a.app, a.surf);
      sum += strips;
      ++frames;
      worst = strips > worst ? strips : worst;
      if (n % 29 == 0) {
        appInvalidate(b.app);
        appRender(b.app, b.surf);
        ++checks;
        if (memcmp(a.fb, b.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)) != 0) {
          long k = 0;
          while (a.fb[k] == b.fb[k]) ++k;
          if (diffs < 3) printf("  kind %d seed %d beat tick %d: incremental differs from full at (%ld,%ld)\n", kind, (int)seed, n, k % SS_WIDTH, k / SS_WIDTH);
          ++diffs;
        }
      }
    }
    const double avg = frames ? (double)sum / (double)frames : 0.0;
    printf("  variant %d seed %d: %ld frames, strips avg %.2f max %d, %d full-render checks, %d differ\n", kind, (int)seed, frames, avg, worst, checks, diffs);
    CHECK(diffs == 0, "variant %d: %d checkpoints differ", kind, diffs);
    CHECK(avg <= 10.0, "variant %d: %.2f strips a frame (majors may average 10)", kind, avg);
    a.stop();
    b.stop();
  }
}

int main() {
  rasterSetScratch(calloc(1, rasterScratchBytes()));   // zeroed; the rig tests draw without appStart
  testRig();
  testStrips();
  testPlan();
  testPlays();
  testSecondDog();
  testFreeze();
  testIncremental();
  sheetPoses("dog_poses_day.ppm", MOOD_DAY);
  sheetPoses("dog_poses_night.ppm", MOOD_NIGHT);
  printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
