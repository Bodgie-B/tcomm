// Stage 22 baseline probe (scratch tool, not in the tree). Prints sizes, frame hashes, strips and ms a
// frame over whole loops, the sway hash, the plinking's state hash and deer action hashes.
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ss_app.h"
#include "gfx/raster.h"
#include "scene/world.h"

using namespace ss;

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }
static const HostAlloc kAlloc = {hostAlloc, hostFree, nullptr};

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}
static const uint64_t kFnv0 = 1469598103934665603ull;

static Inputs makeInputs(float mood, int force, bool night) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = mood;
  in.weather.night_dim = night ? 1.f : 0.f;
  in.night_mode = night;
  in.spine_force = (uint8_t)force;
  in.have_outside = false;   // as ss_preview without --weather
  return in;
}

struct Fam { const char* name; float mood; int force; bool night; };
static const Fam kFams[] = {{"evening", 4.f, 0, false}, {"night", 0.f, 0, true}, {"morning", 1.5f, 1 + SF_MORNING, false},
                            {"midday", 3.f, 1 + SF_MIDDAY, false}};

// Loop 0 rendered every tick; a hash of every 20th frame (and of all of them); strips and ms a frame.
static void frames(const Fam& f, uint64_t seed) {
  void* mem = malloc(SS_APP_BYTES);
  Pixel* fb = (Pixel*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(Pixel));
  Surface s{fb, SS_WIDTH, SS_HEIGHT, SS_WIDTH, 0};
  App* a = appStart(mem, kAlloc, seed, makeInputs(f.mood, f.force, f.night));
  uint64_t h20 = kFnv0;
  uint64_t first20 = 0;
  long long strips = 0;
  int n = 0;
  double us = 0.0;
  uint32_t t = 0;
  for (;; ++t) {
    if (t > 0 && appTick(a)) break;
    const auto t0 = std::chrono::steady_clock::now();
    strips += appRender(a, s);
    const auto t1 = std::chrono::steady_clock::now();
    us += (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000.0;
    ++n;
    if (t % 20 == 0) {
      h20 = fnv(h20, fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
      if (t == 20) first20 = fnv(kFnv0, fb, (size_t)SS_WIDTH * SS_HEIGHT * sizeof(Pixel));
    }
  }
  printf("frames %-7s seed %2llu ticks %u frame20 %016llx every20 %016llx strips/frame %.3f ms/frame %.4f\n", f.name,
         (unsigned long long)seed, t, (unsigned long long)first20, (unsigned long long)h20, (double)strips / n,
         us / n / 1000.0);
  appStop(a, kAlloc);
  free(mem);
  free(fb);
}

static void sway() {
  void* mem = malloc(SS_APP_BYTES);
  App* a = appStart(mem, kAlloc, 1, makeInputs(4.f, 0, false));
  uint64_t h = kFnv0;
  for (int t = 0; t < 2000; ++t) {
    appTick(a);
    const World* w = appWorld(a);
    h = fnv(h, w->amb.sway, sizeof w->amb.sway);
  }
  printf("sway seed 1 evening 2000 ticks %016llx\n", (unsigned long long)h);
  appStop(a, kAlloc);
  free(mem);
}

// The first midday seeds whose loop 0 plays the plinking: PlinkState hashed every tick it is live.
static void plink() {
  int found = 0;
  for (uint64_t seed = 1; seed < 200 && found < 2; ++seed) {
    void* mem = malloc(SS_APP_BYTES);
    App* a = appStart(mem, kAlloc, seed, makeInputs(3.f, 1 + SF_MIDDAY, false));
    char plan[320];
    appLoopPlan(a, plan, sizeof plan);
    if (!strstr(plan, "plink")) {
      appStop(a, kAlloc);
      free(mem);
      continue;
    }
    ++found;
    uint64_t h = kFnv0;
    int live = 0;
    for (uint32_t t = 0;; ++t) {
      if (t > 0 && appTick(a)) break;
      const World* w = appWorld(a);
      if (w->day.kind == DAY_PLINK) {
        h = fnv(h, &w->day.plink.p, sizeof(PlinkState));
        ++live;
      }
    }
    printf("plink seed %llu live ticks %d state %016llx\n", (unsigned long long)seed, live, (unsigned long long)h);
    appStop(a, kAlloc);
    free(mem);
  }
}

static void deer() {
  void* scratch = calloc(1, rasterScratchBytes());
  rasterSetScratch(scratch);
  Palette pal;
  WeatherTint wt;
  memset(&wt, 0, sizeof wt);
  computePalette(3.f, wt, pal);
  static Pixel px[240 * 160];
  Surface s{px, 240, 160, 240, 0};
  const Rect clip{0, 0, 240, 160};
  for (int act = 0; act <= CA_COUNT + 1; ++act) {
    Critter c;
    memset(&c, 0, sizeof c);
    critterInit(c, CR_DEER, Vec2{120.f, 130.f}, 1);
    if (act < CA_COUNT) critterSetAction(c, (CritterAction)act, 0.5f);
    else critterMoveTo(c, act == CA_COUNT ? 170.f : 200.f, act == CA_COUNT ? 20.f : 70.f);   // a walk, then a run
    uint64_t h = kFnv0;
    for (int t = 0; t < 60; ++t) {
      critterTick(c, SS_TICK_DT);
      if (t % 5 != 4) continue;
      memset(px, 0, sizeof px);
      critterDraw(c, s, pal, clip);
      h = fnv(h, px, sizeof px);
    }
    printf("deer action %2d %016llx\n", act, (unsigned long long)h);
  }
  rasterSetScratch(nullptr);
  free(scratch);
}

int main(int argc, char** argv) {
  const bool fast = argc > 1 && strcmp(argv[1], "fast") == 0;
  printf("sizeof App %u World %u StreamLife %u Landscape %u LookoutState %u", appAppBytes(), (unsigned)sizeof(World),
         (unsigned)sizeof(StreamLife), (unsigned)sizeof(Landscape), (unsigned)sizeof(LookoutState));
#if SS_XMAS
  printf(" XmasState %u", (unsigned)sizeof(XmasState));
#endif
  printf(" Memory %u LoopPlan %u Inputs %u DayState %u DayBike %u DayPlink %u Hiker %u Visitors %u LookoutLand %u\n",
         (unsigned)sizeof(Memory), (unsigned)sizeof(LoopPlan), (unsigned)sizeof(Inputs), (unsigned)sizeof(DayState),
         (unsigned)sizeof(DayBike), (unsigned)sizeof(DayPlink), (unsigned)sizeof(Hiker), (unsigned)sizeof(Visitors),
         (unsigned)sizeof(LookoutLand));
  sway();
  plink();
  deer();
  if (fast) return 0;
  const uint64_t seeds[3] = {1, 7, 12};
  for (const Fam& f : kFams)
    for (uint64_t sd : seeds) frames(f, sd);
  return 0;
}
