// Headless preview: simulate, render, write DIR/frame_%05d.ppm.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define SS_MKDIR(p) _mkdir(p)
#define SS_PATH_WIN 1
#else
#include <sys/stat.h>
#define SS_MKDIR(p) mkdir(p, 0755)
#endif
#include <chrono>

#include "preview_common.h"

// Windows accepts both '/' and '\\' as separators and has drive prefixes ("C:", "C:\\");
// elsewhere only '/' separates, so a backslash stays an ordinary filename character.
static bool isSep(char c) {
#ifdef SS_PATH_WIN
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

// A bare drive ("C:" or "C:\") cannot be created; it is the root of what follows.
static bool isDriveRoot(const char* p, size_t len) {
#ifdef SS_PATH_WIN
  return (len == 2 && p[1] == ':') || (len == 3 && p[1] == ':' && isSep(p[2]));
#else
  (void)p;
  (void)len;
  return false;
#endif
}

static bool mkdirP(const char* path) {
  char tmp[1024];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof tmp) return false;
  memcpy(tmp, path, len + 1);
  for (size_t i = 1; i < len; i++) {
    if (isSep(tmp[i])) {
      char sep = tmp[i];
      tmp[i] = 0;
      if (!isDriveRoot(tmp, i) && SS_MKDIR(tmp) != 0 && errno != EEXIST) return false;
      tmp[i] = sep;
    }
  }
  if (!isDriveRoot(tmp, len) && SS_MKDIR(tmp) != 0 && errno != EEXIST) return false;
  return true;
}

typedef std::chrono::steady_clock Clock;
static double msSince(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

int main(int argc, char** argv) {
  PreviewOptions o;
  previewParseArgs(argc, argv, o);
  if (o.sweep) o.mood = o.sweep_a;

  if (!mkdirP(o.out_dir)) {
    fprintf(stderr, "cannot create output directory %s: %s\n", o.out_dir, strerror(errno));
    return 1;
  }

  PreviewState st;
  if (!previewInit(st, o.seed, previewInputsFromOptions(o))) {
    fprintf(stderr, "init failed\n");
    return 1;
  }
  st.doorbell_at = o.doorbell_at;
  if (o.log_beats) previewEnableBeatLog(st);

  Clock::time_point wall0 = Clock::now();
  double sim_ms = 0.0;
  char hud[256];

  Clock::time_point t = Clock::now();
  previewTicks(st, o.skip);
  double skip_ms = msSince(t);

  int loops = 0;
  for (int f = 0; f < o.frames; f++) {
    t = Clock::now();
    if (o.sweep) {
      float u = o.frames > 1 ? (float)f / (float)(o.frames - 1) : 0.0f;
      previewSetMood(st, o.sweep_a + (o.sweep_b - o.sweep_a) * u);
    }
    loops += previewStep(st, previewTicksForFrameOpts(o, f));
    sim_ms += msSince(t);

    char path[1200];
    snprintf(path, sizeof path, "%s/frame_%05d.ppm", o.out_dir, f);
    if (!previewWritePPM(st, path)) {
      fprintf(stderr, "cannot write %s\n", path);
      previewShutdown(st);
      return 1;
    }
    if (f % 25 == 0) {
      previewHudLine(st, hud, sizeof hud);
      printf("frame %d: %s\n", f, hud);
    }
  }

  double wall_ms = msSince(wall0);
  printf("done: %d frames in %s, wall %.0f ms, skip %.1f ms, sim+render %.1f ms, avg %.3f ms/frame, new loops %d\n",
         o.frames, o.out_dir, wall_ms, skip_ms, sim_ms, o.frames ? sim_ms / o.frames : 0.0, loops);
  previewShutdown(st);
  return 0;
}
