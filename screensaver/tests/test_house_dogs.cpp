// Tests for Lucy and Copper in camp (core/scene/housedogs.cpp, stage 13) and the
// upstairs request arrows on the chip (core/scene/chip.cpp), and the host overlay:
//
//   [1] no dog out: their slots are never used, in every family, and nothing of
//       theirs is ever in the scene
//   [2] a dog goes out: it trots in within a few seconds, in the evening, the
//       morning and midday, stays (in the frame, out of the chip's corner, out
//       of the water) while it is out, and trots off once it comes in; both
//       together; at a loop's start once the camp is built
//   [3] the same seed and the same dog_out changes replay exactly, tick for tick
//       and frame for frame; another seed does other things
//   [4] variety: over long stays each dog does at least five different things
//       and never the same thing twice running (but for settling out of a
//       beat's way)
//   [5] the Lookout: no dogs
//   [6] drawn tick by tick with the dogs about, every frame equals a full render
//   [7] the fetch: while its dogs (Lucy, Copper) are in, the house dogs are not
//   [8] the chip's request arrows: where they sit, the flash, what the flash redraws,
//       the night colours
//   [9] the host overlay: in every composited strip, kept by strips not redrawn,
//       gone after appInvalidateRect
//
// Plain executable; non-zero on failure.
#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/chip.h"
#include "scene/critters.h"
#include "scene/director.h"
#include "scene/housedogs.h"
#include "scene/landscape.h"
#include "scene/majors.h"
#include "scene/spine.h"
#include "scene/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static Inputs makeInputs(int family, int dog) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = family == SF_EVENING ? 0.f : (family == SF_MORNING ? 1.6f : (family == SF_LOOKOUT ? 4.f : 3.f));
  in.night_mode = family == SF_EVENING;
  in.weather.night_dim = family == SF_EVENING ? 1.f : 0.f;
  in.spine_force = (uint8_t)(1 + family);
  in.have_outside = true;   // clear weather from "data"
  in.dog_out = dog;
  in.chip = true;
  return in;
}

struct Rig {
  void* mem = nullptr;
  Pixel* fb = nullptr;
  Surface surf{};
  App* app = nullptr;
  Inputs in{};
  bool start(uint64_t seed, const Inputs& i) {
    in = i;
    mem = malloc(SS_APP_BYTES);
    fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
    surf = Surface{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
    app = (mem && fb) ? appStart(mem, kAlloc, seed, in) : nullptr;
    return app != nullptr;
  }
  void dogs(int d) {
    in.dog_out = d;
    appSetInputs(app, in);
  }
  const World& w() const { return *appWorld(app); }
  void stop() {
    if (app) appStop(app, kAlloc);
    free(mem);
    free(fb);
    app = nullptr;
  }
};

static const char* famName(int f) { return f == SF_EVENING ? "evening" : (f == SF_MORNING ? "morning" : (f == SF_MIDDAY ? "midday" : "lookout")); }

static bool fetchOn(const World& w) { return w.major.on && w.major.id == MJ_FETCH; }
static bool inSceneSlot(const World& w, int k) {
  const int s = CRITTER_STORY + k;
  return w.crit[s].in_use && w.crit[s].tag == CT_HOUSEDOG;
}
static bool readyScene(const World& w) {
  const char* b = w.minors.beat;
  return w.build.phase == BP_DONE && w.fade <= 0.f && !(b && strcmp(b, "close") == 0);
}
static bool inChip(const Rect& r) { return r.x1 > 538 && r.x0 < 796 && r.y1 > 412 && r.y0 < 476; }

// ---- [1] ------------------------------------------------------------------------------------------

static void testNoDog() {
  printf("[1] no dog out: their slots never used\n");
  int ticks = 0, used = 0;
  const int fams[3] = {SF_EVENING, SF_MORNING, SF_MIDDAY};
  for (int f : fams)
    for (uint64_t seed = 1; seed <= 3; ++seed) {
      Rig r;
      CHECK(r.start(seed, makeInputs(f, 0)), "start");
      for (int t = 0; t < 6000; ++t) {
        appTick(r.app);
        ++ticks;
        for (int k = 0; k < HD_COUNT; ++k) {
          used += r.w().crit[CRITTER_STORY + k].in_use ? 1 : 0;
          used += r.w().dogs.d[k].phase != HP_AWAY ? 1 : 0;
          used += appHouseDog(r.app, k, nullptr, nullptr) ? 1 : 0;
        }
      }
      r.stop();
    }
  printf("  %d ticks in three families, the dogs' slots or states in use %d times\n", ticks, used);
  CHECK(used == 0, "a house dog appeared with no dog out (%d)", used);
}

// ---- [2] ------------------------------------------------------------------------------------------

struct Stay {
  int arrive_ticks[2] = {-1, -1};   // ticks from going out to in sight
  int leave_ticks[2] = {-1, -1};    // ticks from coming in to gone
  int missing = 0;                  // ticks out, scene ready, no fetch, and not in camp after its grace
  int off_frame = 0, in_chip = 0, wet = 0, ticks_here = 0;
};

// Out at `on`, in at `off`; watches each wanted dog.
static void playStay(uint64_t seed, int family, int dog, int on, int off, int end, Stay& s) {
  Rig r;
  CHECK(r.start(seed, makeInputs(family, 0)), "start");
  int out_since[2] = {-1, -1};   // ticks the dog has been out while it could have been here
  int here_for[2] = {0, 0};
  for (int t = 0; t < end; ++t) {
    if (t == on) r.dogs(dog);
    if (t == off) r.dogs(0);
    appTick(r.app);
    const World& w = r.w();
    for (int k = 0; k < HD_COUNT; ++k) {
      const bool want = t >= on && t < off && ((dog >> k) & 1);
      Rect b;
      int act = -1;
      const bool vis = appHouseDog(r.app, k, &b, &act);
      if (want && vis && s.arrive_ticks[k] < 0) s.arrive_ticks[k] = t - on;
      if (!want && t >= off && ((dog >> k) & 1) && !inSceneSlot(w, k) && s.leave_ticks[k] < 0) s.leave_ticks[k] = t - off;
      // Missing: out, the scene could have it, no fetch, and not here for more than 7 s.
      if (want && readyScene(w) && !fetchOn(w) && !inSceneSlot(w, k)) {
        if (out_since[k] < 0) out_since[k] = t;
        if (t - out_since[k] > 7 * SS_TICK_HZ) ++s.missing;
      } else {
        out_since[k] = -1;
      }
      // While here (after its walk in), in the frame, clear of the chip, on dry ground.
      const HouseDog& d = w.dogs.d[k];
      if (vis && d.phase == HP_HERE) ++here_for[k];
      else here_for[k] = 0;
      if (here_for[k] > 12 * SS_TICK_HZ) {
        ++s.ticks_here;
        if (b.x0 < -2 || b.x1 > SS_WIDTH + 2 || b.y0 < 0 || b.y1 > SS_HEIGHT) ++s.off_frame;
        if (inChip(b)) ++s.in_chip;
        const Vec2 f = w.critters[d.slot].feet;
        float cy, half;
        if (landscapeStreamSpan(w.land, f.x, cy, half) && f.y > cy - half + 1.f) ++s.wet;
      }
    }
  }
  r.stop();
}

static void testStay() {
  printf("[2] out: in within a few seconds, stays, in the frame and dry; in: gone\n");
  const int fams[3] = {SF_EVENING, SF_MORNING, SF_MIDDAY};
  int worst_in = 0, worst_out = 0, plays = 0, here = 0;
  for (int f : fams)
    for (int dog = 1; dog <= 3; ++dog)
      for (uint64_t seed = 1; seed <= 3; ++seed) {
        Stay s;
        const int on = 700 + 37 * (int)seed, off = on + 3600;
        playStay(seed * 7 + (uint64_t)dog, f, dog, on, off, off + 900, s);
        ++plays;
        here += s.ticks_here;
        for (int k = 0; k < HD_COUNT; ++k) {
          if (!((dog >> k) & 1)) {
            CHECK(s.arrive_ticks[k] < 0, "%s dog %d: dog %d came in unasked", famName(f), dog, k);
            continue;
          }
          CHECK(s.arrive_ticks[k] >= 0 && s.arrive_ticks[k] <= 6 * SS_TICK_HZ, "%s dog %d seed %d: dog %d in sight after %d ticks",
                famName(f), dog, (int)seed, k, s.arrive_ticks[k]);
          CHECK(s.leave_ticks[k] >= 0 && s.leave_ticks[k] <= 25 * SS_TICK_HZ, "%s dog %d seed %d: dog %d gone after %d ticks",
                famName(f), dog, (int)seed, k, s.leave_ticks[k]);
          if (s.arrive_ticks[k] > worst_in) worst_in = s.arrive_ticks[k];
          if (s.leave_ticks[k] > worst_out) worst_out = s.leave_ticks[k];
        }
        CHECK(s.missing == 0, "%s dog %d seed %d: out but not in camp for %d ticks", famName(f), dog, (int)seed, s.missing);
        CHECK(s.off_frame == 0 && s.in_chip == 0 && s.wet == 0, "%s dog %d seed %d: off frame %d, in the chip %d, in the water %d",
              famName(f), dog, (int)seed, s.off_frame, s.in_chip, s.wet);
      }
  printf("  %d stays: in sight within %.1f s at worst, gone within %.1f s; %d dog-ticks checked for the frame, the chip, the water\n",
         plays, (double)worst_in / SS_TICK_HZ, (double)worst_out / SS_TICK_HZ, here);
  // Out from the start: in once the camp is built.
  int worst_start = 0;
  for (int f : fams) {
    Rig r;
    CHECK(r.start(11, makeInputs(f, 3)), "start");
    int built = -1, seen[2] = {-1, -1};
    for (int t = 0; t < 2000; ++t) {
      appTick(r.app);
      if (built < 0 && readyScene(r.w())) built = t;
      for (int k = 0; k < 2; ++k)
        if (seen[k] < 0 && appHouseDog(r.app, k, nullptr, nullptr)) seen[k] = t;
    }
    for (int k = 0; k < 2; ++k) {
      CHECK(built >= 0 && seen[k] >= built && seen[k] - built <= 6 * SS_TICK_HZ, "%s: out from the start, dog %d in at %d, built at %d",
            famName(f), k, seen[k], built);
      if (seen[k] - built > worst_start) worst_start = seen[k] - built;
    }
    r.stop();
  }
  printf("  out from a loop's start: in sight %.1f s after the camp is built at worst\n", (double)worst_start / SS_TICK_HZ);
}

// ---- [3] ------------------------------------------------------------------------------------------

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const unsigned char* b = (const unsigned char*)p;
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}

struct Trace {
  std::vector<uint64_t> tick_hash;
  std::vector<uint64_t> frame_hash;
  std::vector<int> acts;   // activity starts, both dogs, in order
};

static void playTrace(uint64_t seed, int family, Trace& tr) {
  Rig r;
  CHECK(r.start(seed, makeInputs(family, 0)), "start");
  uint8_t last_act[2] = {0, 0};
  for (int t = 0; t < 8000; ++t) {
    if (t == 500) r.dogs(3);
    if (t == 4000) r.dogs(1);
    if (t == 5200) r.dogs(3);
    appTick(r.app);
    const World& w = r.w();
    uint64_t h = 14695981039346656037ull;
    for (int k = 0; k < HD_COUNT; ++k) {
      const HouseDog& d = w.dogs.d[k];
      h = fnv(h, &d.phase, 1);
      h = fnv(h, &d.act, 1);
      h = fnv(h, &d.step, 1);
      if (d.slot >= 0) {
        const Critter& c = w.critters[d.slot];
        h = fnv(h, &c.feet, sizeof c.feet);
        h = fnv(h, &c.action, 1);
        h = fnv(h, &c.facing, sizeof c.facing);
      }
      if (d.act != last_act[k] && d.act != HA_NONE) tr.acts.push_back(k * 100 + d.act);
      last_act[k] = d.act;
    }
    h = fnv(h, &w.hiker.feet, sizeof w.hiker.feet);
    h = fnv(h, &w.fx.reach, sizeof w.fx.reach);
    tr.tick_hash.push_back(h);
    if (t % 1000 == 999) {
      appRender(r.app, r.surf);
      tr.frame_hash.push_back(fnv(14695981039346656037ull, r.fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel)));
    }
  }
  r.stop();
}

static void testDeterminism() {
  printf("[3] the same seed and dog changes replay exactly; another seed differs\n");
  const int fams[3] = {SF_EVENING, SF_MORNING, SF_MIDDAY};
  for (int f : fams) {
    Trace a, b, c;
    playTrace(21, f, a);
    playTrace(21, f, b);
    playTrace(22, f, c);
    int diff_ticks = 0;
    for (size_t i = 0; i < a.tick_hash.size(); ++i) diff_ticks += a.tick_hash[i] != b.tick_hash[i];
    CHECK(diff_ticks == 0, "%s: the replay differs in %d ticks", famName(f), diff_ticks);
    CHECK(a.frame_hash == b.frame_hash, "%s: the replay's frames differ", famName(f));
    CHECK(a.acts != c.acts, "%s: seeds 21 and 22 did the same things", famName(f));
    printf("  %s: %d activities replayed exactly over %d ticks and %d frames (seed 22: %d activities, another order)\n", famName(f),
           (int)a.acts.size(), (int)a.tick_hash.size(), (int)a.frame_hash.size(), (int)c.acts.size());
  }
}

// ---- [4] ------------------------------------------------------------------------------------------

static void testVariety() {
  printf("[4] variety: five or more things each, never one twice running\n");
  const int fams[2] = {SF_MIDDAY, SF_EVENING};
  for (int f : fams) {
    int counts[2][HA_COUNT] = {};
    int repeats = 0, starts = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
      Rig r;
      CHECK(r.start(seed + 40, makeInputs(f, 3)), "start");
      uint8_t last[2] = {0, 0}, prev_real[2] = {0, 0};
      for (int t = 0; t < 22000; ++t) {
        // A new loop is a new World: each dog's "nothing straight again" starts over (stage 22's reeds moved
        // where a loop boundary falls between two of Copper's visits to the tent in seed 41).
        if (appTick(r.app)) prev_real[0] = prev_real[1] = 0;
        for (int k = 0; k < 2; ++k) {
          const uint8_t a = r.w().dogs.d[k].act;
          if (a != last[k] && a != HA_NONE) {
            ++counts[k][a];
            ++starts;
            if (a == prev_real[k] && a != HA_SETTLE) ++repeats;
            prev_real[k] = a;
          }
          last[k] = a;
        }
      }
      r.stop();
    }
    for (int k = 0; k < 2; ++k) {
      int kinds = 0;
      char line[256];
      int n = 0;
      for (int a = 1; a < HA_COUNT; ++a) {
        if (counts[k][a]) ++kinds;
        n += snprintf(line + n, sizeof line - (size_t)n, " %s %d", houseDogActName(a), counts[k][a]);
      }
      printf("  %s, %s:%s\n", famName(f), k ? "Copper" : "Lucy", line);
      CHECK(kinds >= 5, "%s: %s did only %d different things", famName(f), k ? "Copper" : "Lucy", kinds);
    }
    CHECK(repeats == 0, "%s: an activity followed itself %d times in %d", famName(f), repeats, starts);
  }
}

// ---- [5] ------------------------------------------------------------------------------------------

static void testLookout() {
  printf("[5] the Lookout: no dogs\n");
  Rig r;
  CHECK(r.start(3, makeInputs(SF_LOOKOUT, 3)), "start");
  int seen = 0;
  for (int t = 0; t < 4000; ++t) {
    appTick(r.app);
    for (int k = 0; k < 2; ++k) seen += inSceneSlot(r.w(), k) ? 1 : 0;
  }
  r.stop();
  CHECK(seen == 0, "a house dog in the Lookout (%d ticks)", seen);
  printf("  4000 ticks of the Lookout with both out: dogs in it %d ticks\n", seen);
}

// ---- [6] ------------------------------------------------------------------------------------------

static void testFrames() {
  printf("[6] tick by tick with the dogs about, every frame equals a full render\n");
  int checked = 0, bad = 0, strips = 0, frames = 0;
  const int fams[2] = {SF_MIDDAY, SF_EVENING};
  for (int f : fams) {
    Rig r;
    CHECK(r.start(5, makeInputs(f, 3)), "start");
    std::vector<Pixel> full((size_t)SS_WIDTH * SS_HEIGHT);
    for (int t = 0; t < 5000; ++t) {
      appTick(r.app);
      strips += appRender(r.app, r.surf);
      ++frames;
      if (t > 600 && t % 250 == 0) {
        memcpy(full.data(), r.fb, full.size() * sizeof(Pixel));
        appInvalidate(r.app);
        appRender(r.app, r.surf);
        int d = 0;
        for (size_t i = 0; i < full.size(); ++i) d += full[i] != r.fb[i];
        ++checked;
        if (d) {
          ++bad;
          printf("  %s tick %d: %d pixels differ from a full render\n", famName(f), t, d);
        }
      }
    }
    r.stop();
  }
  printf("  %d checks, %d differ; %.2f strips a frame with both dogs out\n", checked, bad, (double)strips / frames);
  CHECK(bad == 0, "%d frames differ from a full render", bad);
}

// ---- [7] ------------------------------------------------------------------------------------------

static void testFetch() {
  printf("[7] the fetch: the house dogs are away while its dogs are in\n");
  int fetches = 0, overlap = 0, back = 0;
  for (uint64_t seed = 1; seed <= 6; ++seed) {
    Rig r;
    CHECK(r.start(seed, makeInputs(SF_MIDDAY, 3)), "start");
    bool was = false, in_fetch_dogs = false;
    int after = -1;
    const uint32_t len = appLoopTicks(r.app);
    for (uint32_t t = 0; t < len; ++t) {
      appTick(r.app);
      const World& w = r.w();
      const bool on = fetchOn(w);
      if (on && !was) ++fetches;
      if (!on && was) after = 0;
      was = on;
      bool major_dog = false;
      for (int i = 0; i < CRITTER_STORY; ++i)
        if (w.crit[i].in_use && w.crit[i].tag == CT_MAJOR && w.critters[i].kind == CR_DOG && w.critters[i].visible) major_dog = true;
      if (major_dog) in_fetch_dogs = true;
      if (major_dog && (appHouseDog(r.app, 0, nullptr, nullptr) || appHouseDog(r.app, 1, nullptr, nullptr))) ++overlap;
      if (after >= 0 && ++after == 15 * SS_TICK_HZ && appHouseDog(r.app, 0, nullptr, nullptr) && appHouseDog(r.app, 1, nullptr, nullptr)) ++back;
    }
    CHECK(in_fetch_dogs, "seed %d: the fetch's dogs never showed", (int)seed);
    r.stop();
  }
  printf("  %d fetches with both out: house dogs in sight with the fetch's %d ticks; both back 15 s after %d of them\n", fetches, overlap, back);
  CHECK(fetches >= 6, "only %d fetches", fetches);
  CHECK(overlap == 0, "two Lucys (or Coppers) in sight for %d ticks", overlap);
  CHECK(back >= fetches - 1, "the house dogs came back after only %d of %d fetches", back, fetches);
}

// ---- [8] ------------------------------------------------------------------------------------------

static float lum(Pixel p) { return (0.2126f * red8(p) + 0.7152f * green8(p) + 0.0722f * blue8(p)) / 255.f; }

static void testArrows() {
  printf("[8] the request arrows beside the dog badges\n");
  ChipState c;
  chipInit(c);
  const uint8_t req[3] = {2, 3, 1};   // mild heat, mild cooling, urgent heat (newest)
  chipRequests(c, req, 3);
  chipUpdate(c, 0, 3, false, 0.f);
  const int badges_x0 = kChipX0 - kChipBadgeGap - 2 * kChipBadgeD - kChipBadgeStep;
  const float newest = chipArrowX(c, 2), oldest = chipArrowX(c, 0);
  CHECK((int)(newest + 0.5f * kChipArrowD) <= badges_x0 - kChipArrowGap + 1, "the newest arrow (%.1f) runs into the badges (%d)", (double)newest,
        badges_x0);
  CHECK(oldest < chipArrowX(c, 1) && chipArrowX(c, 1) < newest, "the arrows are not oldest to newest, left to right");
  const Rect b = chipBounds(c);
  CHECK(b.x0 <= (int)(oldest - 0.5f * kChipArrowD), "the bounds (%d) miss the oldest arrow (%.1f)", b.x0, (double)oldest);
  printf("  centres %.0f %.0f %.0f, badges from %d, bounds from %d\n", (double)oldest, (double)chipArrowX(c, 1), (double)newest, badges_x0, b.x0);

  // The flash: the urgent one dims every half second; only the arrows are redrawn for it.
  int toggles = 0, small = 0;
  uint8_t dim = c.look.req_dim;
  for (int t = 0; t < 4 * SS_TICK_HZ; ++t) {
    const Rect d = chipUpdate(c, 0, 3, false, SS_TICK_DT);
    if (c.look.req_dim != dim) {
      ++toggles;
      dim = c.look.req_dim;
      if (!d.empty() && d.x0 >= b.x0 - 2 && d.x1 < badges_x0) ++small;
    } else {
      CHECK(d.empty(), "tick %d: the chip redrew without a change", t);
    }
  }
  printf("  4 s: %d flash changes, %d redrew only the arrows\n", toggles, small);
  CHECK(toggles == 8, "%d flash changes in 4 s (8 expected)", toggles);
  CHECK(small == toggles, "%d of %d flash changes redrew more than the arrows", toggles - small, toggles);
  {   // mild ones never flash
    ChipState m;
    chipInit(m);
    const uint8_t mild[2] = {2, 3};
    chipRequests(m, mild, 2);
    chipUpdate(m, 0, 0, false, 0.f);
    int dims = 0;
    for (int t = 0; t < 3 * SS_TICK_HZ; ++t) {
      chipUpdate(m, 0, 0, false, SS_TICK_DT);
      dims += m.look.req_dim;
    }
    CHECK(dims == 0, "mild arrows flashed (%d)", dims);
    CHECK(chipArrowX(m, 1) + 0.5f * kChipArrowD <= (float)(kChipX0 - kChipArrowGap + 1), "with no dogs out the arrows run into the chip");
  }
  // Drawn: lit and dim differ only inside the urgent arrow; at night nothing brighter than the chip's cream.
  std::vector<Pixel> lit((size_t)SS_WIDTH * SS_HEIGHT, 0), dark((size_t)SS_WIDTH * SS_HEIGHT, 0);
  Surface sl{lit.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0}, sd{dark.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  std::vector<uint8_t> scratch(SS_FAST_BYTES, 0);
  void* prev = rasterSetScratch(scratch.data());
  const Palette& pal = moodKey(MOOD_DAY);
  ChipState a = c;
  a.look.req_dim = 0;
  chipDraw(a, sl, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, pal);
  a.look.req_dim = 1;
  chipDraw(a, sd, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, pal);
  int diff = 0, outside = 0;
  const float ux = chipArrowX(c, 2);
  for (int y = 0; y < SS_HEIGHT; ++y)
    for (int x = 0; x < SS_WIDTH; ++x) {
      const size_t i = (size_t)y * SS_WIDTH + x;
      if (lit[i] == dark[i]) continue;
      ++diff;
      if (x < ux - 18.f || x > ux + 18.f) ++outside;
    }
  CHECK(diff > 40 && outside == 0, "the flash changed %d pixels, %d outside the urgent arrow", diff, outside);
  ChipState n;
  chipInit(n);
  const uint8_t all4[3] = {1, 4, 3 | SS_REQ_PREDICTED};
  chipRequests(n, all4, 3);
  chipUpdate(n, 0, 3, true, 0.f);
  std::vector<Pixel> night((size_t)SS_WIDTH * SS_HEIGHT, 0);
  Surface sn{night.data(), SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  chipDraw(n, sn, Rect{0, 0, SS_WIDTH, SS_HEIGHT}, moodKey(MOOD_NIGHT));
  float top = 0.f;
  const Rect ar{(int)(chipArrowX(n, 0) - 18.f), kChipY0, (int)(chipArrowX(n, 2) + 18.f), kChipY0 + kChipH};
  for (int y = ar.y0; y < ar.y1; ++y)
    for (int x = ar.x0; x < ar.x1; ++x) {
      const float l = lum(night[(size_t)y * SS_WIDTH + x]);
      if (l > top) top = l;
    }
  rasterSetScratch(prev);
  printf("  the flash changes %d pixels, all in the urgent arrow; brightest arrow pixel at night %.2f\n", diff, (double)top);
  CHECK(top <= 0.86f, "an arrow at night is brighter (%.2f) than the chip's own cream", (double)top);
}

// ---- [9] ------------------------------------------------------------------------------------------

static void overlayBox(void* user, Surface& strip, const Rect& clip) {
  int* calls = (int*)user;
  ++*calls;
  for (int y = clip.y0 > 20 ? clip.y0 : 20; y < (clip.y1 < 40 ? clip.y1 : 40); ++y)
    for (int x = 700; x < 720; ++x) strip.row(y)[x] = 0xF81F;
}

static void testOverlay() {
  printf("[9] the host overlay: drawn into each strip, kept, and gone when asked\n");
  Rig r;
  CHECK(r.start(3, makeInputs(SF_MIDDAY, 0)), "start");
  for (int t = 0; t < 400; ++t) appTick(r.app);
  int calls = 0;
  appSetOverlay(r.app, overlayBox, &calls);
  appInvalidateRect(r.app, 700, 20, 720, 40);
  appRender(r.app, r.surf);
  CHECK(r.fb[30 * SS_WIDTH + 710] == 0xF81F, "the overlay is not in the frame");
  int kept = 0;
  for (int t = 0; t < 300; ++t) {
    appTick(r.app);
    appRender(r.app, r.surf);
    kept += r.fb[25 * SS_WIDTH + 705] == 0xF81F && r.fb[39 * SS_WIDTH + 719] == 0xF81F;
  }
  CHECK(kept == 300, "the overlay was missing from %d of 300 frames", 300 - kept);
  appSetOverlay(r.app, nullptr, nullptr);
  appInvalidateRect(r.app, 700, 20, 720, 40);
  appRender(r.app, r.surf);
  const Pixel after = r.fb[30 * SS_WIDTH + 710];
  std::vector<Pixel> frame(r.fb, r.fb + (size_t)SS_WIDTH * SS_HEIGHT);
  appInvalidate(r.app);
  appRender(r.app, r.surf);
  CHECK(after != 0xF81F && memcmp(frame.data(), r.fb, frame.size() * sizeof(Pixel)) == 0, "the overlay's square was not redrawn as the scene");
  printf("  the overlay drawn %d times over 301 frames, in every frame; gone after the rectangle was redrawn\n", calls);
  r.stop();
}

int main() {
  testNoDog();
  testStay();
  testDeterminism();
  testVariety();
  testLookout();
  testFrames();
  testFetch();
  testArrows();
  testOverlay();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
