#include "preview_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <new>

using namespace ss;

static const char* const kWeatherNames[] = {"clear", "rain", "snow", "wind", "heat", "fog", "chill"};
static const int kWeatherCount = 7;

static void* hostAlloc(size_t n) { return malloc(n); }
static void hostFree(void* p) { free(p); }

const char* previewWeatherName(int code) {
  return (code >= 0 && code < kWeatherCount) ? kWeatherNames[code] : "?";
}

Inputs previewInputsFromOptions(const PreviewOptions& o) {
  Inputs in;
  memset(&in, 0, sizeof in);
  in.mood = o.mood;
  in.weather.overcast = o.overcast;
  in.weather.fog = o.fog;
  in.weather.night_dim = o.night ? 1.0f : 0.0f;
  in.weather_code = o.weather_code;
  in.have_outside = o.weather_given;
  in.other_status = o.status;
  in.dog_out = o.dog;
  in.night_mode = o.night;
  in.lookout_window = o.lookout;
  return in;
}

static void startApp(PreviewState& st) {
  st.app = appStart(st.app_mem, st.alloc, st.seed, st.inputs);
  st.tick = 0;
}

bool previewInit(PreviewState& st, uint64_t seed, const Inputs& in) {
  memset(&st, 0, sizeof st);
  st.alloc.alloc_large = hostAlloc;
  st.alloc.free_large = hostFree;
  st.app_mem = malloc(SS_APP_BYTES);
  st.fb = (uint16_t*)calloc((size_t)SS_WIDTH * SS_HEIGHT, sizeof(uint16_t));
  if (!st.app_mem || !st.fb) {
    free(st.app_mem);
    free(st.fb);
    st.app_mem = nullptr;
    st.fb = nullptr;
    return false;
  }
  st.surface.pixels = st.fb;
  st.surface.width = SS_WIDTH;
  st.surface.height = SS_HEIGHT;
  st.surface.stride = SS_WIDTH;
  st.surface.origin_y = 0;
  st.inputs = in;
  st.seed = seed;
  st.speed = 1;
  st.doorbell_at = -1;
  startApp(st);
  return st.app != nullptr;
}

void previewShutdown(PreviewState& st) {
  if (st.app) appStop(st.app, st.alloc);
  st.app = nullptr;
  free(st.app_mem);
  free(st.fb);
  st.app_mem = nullptr;
  st.fb = nullptr;
}

static void logBeatChange(PreviewState& st) {
  // A minor effect started: its own line (the count goes up, or restarts at 1 on a new loop).
  static int minors_seen = 0;
  const int mc = appMinorCount(st.app);
  if (mc != minors_seen) {
    if (mc > minors_seen || mc == 1) printf("tick=%u minor=%s\n", (unsigned)st.tick, appMinorName(st.app));
    minors_seen = mc;
  }
  const char* name = appBeatName(st.app);
  if (strncmp(name, st.last_beat, sizeof st.last_beat - 1) == 0 &&
      strlen(name) == strlen(st.last_beat))
    return;
  strncpy(st.last_beat, name, sizeof st.last_beat - 1);
  st.last_beat[sizeof st.last_beat - 1] = 0;
  printf("tick=%u loop=%d seed=%llu beat=%s\n", (unsigned)st.tick, st.loop_index,
         (unsigned long long)appLoopSeed(st.app), name);
}

static void logLoopStart(PreviewState& st) {
  char plan[320];
  appLoopPlan(st.app, plan, sizeof plan);
  printf("loop=%d seed=%llu start tick=%u plan=%s\n", st.loop_index,
         (unsigned long long)appLoopSeed(st.app), (unsigned)st.tick, plan);
  st.last_beat[0] = 0;   // the new loop's first beat is logged even if the name repeats
}

void previewEnableBeatLog(PreviewState& st) {
  st.log_beats = true;
  logLoopStart(st);
  logBeatChange(st);
}

int previewTicks(PreviewState& st, int n) {
  int loops = 0;
  for (int i = 0; i < n; i++) {
    if (st.doorbell_at >= 0 && (long long)st.tick == st.doorbell_at) previewDoorbell(st);
    bool new_loop = appTick(st.app);
    if (new_loop) loops++;
    st.tick++;
    if (st.log_beats) {
      if (new_loop) {
        st.loop_index++;
        logLoopStart(st);
      }
      logBeatChange(st);
    }
  }
  return loops;
}

int previewRunLoops(PreviewState& st, int n, FILE* out) {
  int done = 0;
  while (done < n) {
    char plan[320], mem[320];
    appLoopPlan(st.app, plan, sizeof plan);
    const unsigned long long seed = (unsigned long long)appLoopSeed(st.app);
    // Up to the loop's last tick: the memory as this loop leaves it (the
    // rollover tick plans the next loop, which pushes its majors into the ring).
    const uint32_t len = appLoopTicks(st.app);
    uint32_t ticks = 0;
    for (; ticks + 1 < len; ++ticks, ++st.tick) appTick(st.app);
    appMemory(st.app, mem, sizeof mem);
    for (int guard = 0; guard < 40 * 60 * SS_TICK_HZ; ++guard) {   // the rollover (guards a broken plan)
      ++ticks;
      ++st.tick;
      if (appTick(st.app)) break;
    }
    fprintf(out, "loop=%d seed=%llu length=%.0fs plan=%s memory=%s\n", st.loop_index, seed,
            (double)ticks / SS_TICK_HZ, plan, mem);
    ++st.loop_index;
    ++done;
  }
  return done;
}

int previewStep(PreviewState& st, int n) {
  int loops = previewTicks(st, n);
  appRender(st.app, st.surface);
  return loops;
}

int previewTicksForFrame(int fps, int frame) {
  if (fps < 1) fps = 1;
  long long a = (long long)frame * SS_TICK_HZ / fps;
  long long b = (long long)(frame + 1) * SS_TICK_HZ / fps;
  return (int)(b - a);
}

int previewTicksForFrameOpts(const PreviewOptions& o, int frame) {
  return o.every > 0 ? o.every : previewTicksForFrame(o.fps, frame);
}

void previewSetMood(PreviewState& st, float mood) {
  st.inputs.mood = mood;
  appSetInputs(st.app, st.inputs);
}

void previewSetWeatherCode(PreviewState& st, int code) {
  st.inputs.weather_code = code;
  st.inputs.have_outside = true;
  appSetInputs(st.app, st.inputs);
}

void previewSetNight(PreviewState& st, bool night) {
  st.inputs.night_mode = night;
  st.inputs.weather.night_dim = night ? 1.0f : 0.0f;
  appSetInputs(st.app, st.inputs);
}

void previewRestart(PreviewState& st) {
  appStop(st.app, st.alloc);
  startApp(st);
}

void previewSetSeed(PreviewState& st, uint64_t seed) {
  st.seed = seed;
  previewRestart(st);
}

void previewDoorbell(PreviewState& st) {
  Event e;
  e.type = EV_DOORBELL;
  e.arg = 0;
  appPostEvent(st.app, e);
}

bool previewWritePPM(const PreviewState& st, const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  size_t n = (size_t)SS_WIDTH * SS_HEIGHT;
  uint8_t* rgb = (uint8_t*)malloc(n * 3);
  if (!rgb) {
    fclose(f);
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    Pixel p = st.fb[i];
    rgb[i * 3 + 0] = red8(p);
    rgb[i * 3 + 1] = green8(p);
    rgb[i * 3 + 2] = blue8(p);
  }
  bool ok = fprintf(f, "P6\n%d %d\n255\n", SS_WIDTH, SS_HEIGHT) > 0 &&
            fwrite(rgb, 1, n * 3, f) == n * 3;
  ok = (fclose(f) == 0) && ok;
  free(rgb);
  return ok;
}

void previewHudLine(const PreviewState& st, char* buf, size_t n) {
  const char* wname = st.inputs.have_outside ? previewWeatherName(st.inputs.weather_code) : "random";
  snprintf(buf, n, "seed=%llu tick=%u beat=%s t=%.2f mood=%.2f weather=%s%s",
           (unsigned long long)appLoopSeed(st.app), (unsigned)appTickCount(st.app),
           appBeatName(st.app), (double)appBeatProgress(st.app), (double)st.inputs.mood, wname,
           st.inputs.night_mode ? " night" : "");
}

void previewUsage(const char* prog, FILE* to) {
  fprintf(to,
          "usage: %s [options]\n"
          "  --seed N            first loop seed (default 1)\n"
          "  --mood F            mood 0..6 (0 night, 1 dawn, 2 morning, 3 day, 4 golden, 5 dusk; default 3)\n"
          "  --weather NAME      clear rain snow wind heat fog chill (default: not given, core picks)\n"
          "  --overcast F        0..1\n"
          "  --fog F             0..1\n"
          "  --night             night mode on, night_dim = 1\n"
          "  --lookout           inside the sunset window (Lookout spine)\n"
          "  --status N          other floor status 0..5\n"
          "  --dog N             0 none, 1 Lucy, 2 Copper\n"
          "  --frames N          frames to write (default 100)\n"
          "  --fps N             render fps; ticks per frame = %d / fps (default 25)\n"
          "  --every N           render one frame every N ticks (N >= 1; overrides --fps)\n"
          "  --skip N            ticks to simulate before the first frame\n"
          "  --log-beats         print loop starts and beat changes (with tick numbers) to stdout\n"
          "  --loops N           simulate N whole loops without rendering, print one line per loop\n"
          "                      (seed, length, plan with spine variants and majors, memory) and exit\n"
          "  --out DIR           output directory for frame_%%05d.ppm (default ./frames)\n"
          "  --doorbell-at TICK  post a doorbell when the tick counter reaches TICK\n"
          "  --mood-sweep A B    change mood linearly from A to B over the frames\n"
          "  --help              this text\n",
          prog, SS_TICK_HZ);
}

[[noreturn]] static void bad(const char* prog, const char* msg, const char* arg) {
  fprintf(stderr, "%s: %s: %s\n", prog, msg, arg);
  previewUsage(prog, stderr);
  exit(2);
}

static long long parseInt(const char* prog, const char* flag, const char* s) {
  char* end = nullptr;
  errno = 0;
  long long v = strtoll(s, &end, 10);
  if (errno || end == s || *end) bad(prog, "bad integer for", flag);
  return v;
}

static float parseFloat(const char* prog, const char* flag, const char* s) {
  char* end = nullptr;
  errno = 0;
  float v = strtof(s, &end);
  if (errno || end == s || *end) bad(prog, "bad number for", flag);
  return v;
}

void previewParseArgs(int argc, char** argv, PreviewOptions& o) {
  const char* prog = argc > 0 ? argv[0] : "preview";
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    auto need = [&](int count) {
      if (i + count >= argc) bad(prog, "missing value for", a);
    };
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
      previewUsage(prog, stdout);
      exit(0);
    } else if (!strcmp(a, "--seed")) {
      need(1);
      char* end = nullptr;
      o.seed = strtoull(argv[++i], &end, 10);
      if (end == argv[i] || *end) bad(prog, "bad integer for", a);
    } else if (!strcmp(a, "--mood")) {
      need(1);
      o.mood = parseFloat(prog, a, argv[++i]);
    } else if (!strcmp(a, "--weather")) {
      need(1);
      const char* name = argv[++i];
      int code = -1;
      for (int k = 0; k < kWeatherCount; k++)
        if (!strcmp(name, kWeatherNames[k])) code = k;
      if (code < 0) bad(prog, "unknown weather", name);
      o.weather_code = code;
      o.weather_given = true;
    } else if (!strcmp(a, "--overcast")) {
      need(1);
      o.overcast = parseFloat(prog, a, argv[++i]);
    } else if (!strcmp(a, "--fog")) {
      need(1);
      o.fog = parseFloat(prog, a, argv[++i]);
    } else if (!strcmp(a, "--night")) {
      o.night = true;
    } else if (!strcmp(a, "--lookout")) {
      o.lookout = true;
    } else if (!strcmp(a, "--status")) {
      need(1);
      o.status = (int)parseInt(prog, a, argv[++i]);
    } else if (!strcmp(a, "--dog")) {
      need(1);
      o.dog = (int)parseInt(prog, a, argv[++i]);
    } else if (!strcmp(a, "--frames")) {
      need(1);
      o.frames = (int)parseInt(prog, a, argv[++i]);
      if (o.frames < 0) bad(prog, "must be >= 0", a);
    } else if (!strcmp(a, "--fps")) {
      need(1);
      o.fps = (int)parseInt(prog, a, argv[++i]);
      if (o.fps < 1) bad(prog, "must be >= 1", a);
    } else if (!strcmp(a, "--every")) {
      need(1);
      o.every = (int)parseInt(prog, a, argv[++i]);
      if (o.every < 1) bad(prog, "must be >= 1", a);
    } else if (!strcmp(a, "--loops")) {
      need(1);
      o.loops = (int)parseInt(prog, a, argv[++i]);
      if (o.loops < 1) bad(prog, "must be >= 1", a);
    } else if (!strcmp(a, "--log-beats")) {
      o.log_beats = true;
    } else if (!strcmp(a, "--skip")) {
      need(1);
      o.skip = (int)parseInt(prog, a, argv[++i]);
      if (o.skip < 0) bad(prog, "must be >= 0", a);
    } else if (!strcmp(a, "--out")) {
      need(1);
      o.out_dir = argv[++i];
    } else if (!strcmp(a, "--doorbell-at")) {
      need(1);
      o.doorbell_at = parseInt(prog, a, argv[++i]);
      if (o.doorbell_at < 0) bad(prog, "must be >= 0", a);
    } else if (!strcmp(a, "--mood-sweep")) {
      need(2);
      o.sweep_a = parseFloat(prog, a, argv[++i]);
      o.sweep_b = parseFloat(prog, a, argv[++i]);
      o.sweep = true;
    } else {
      bad(prog, "unknown argument", a);
    }
  }
}
