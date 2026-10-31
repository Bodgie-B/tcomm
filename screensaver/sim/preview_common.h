// Shared driver for the PC preview front ends (headless ss_preview, SDL2 ss_window).
// Both drive the core only through ss_app.h. Standard C++17 plus malloc/stdio;
// the POSIX bits (mkdir) live in preview_main.cpp, not here.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "ss_app.h"

struct PreviewOptions {
  uint64_t seed = 1;
  float mood = 3.0f;
  int weather_code = 0;
  bool weather_given = false;   // false: have_outside = false (core picks random weather)
  float overcast = 0.0f;
  float fog = 0.0f;
  bool night = false;
  bool lookout = false;
  int status = 0;
  int dog = 0;
  int frames = 100;
  int fps = 25;                 // render fps; ticks per frame = SS_TICK_HZ / fps
  int every = 0;                // >0: ticks per frame, overrides fps
  bool log_beats = false;       // print beat changes and loop starts to stdout
  int loops = 0;                // >0: simulate this many loops without rendering, print one line each, exit
  int skip = 0;                 // ticks simulated before the first frame is written
  const char* out_dir = "frames";
  long long doorbell_at = -1;   // tick to post a doorbell at; -1 = never
  bool sweep = false;
  float sweep_a = 0.0f, sweep_b = 0.0f;
};

struct PreviewState {
  ss::HostAlloc alloc;
  void* app_mem;            // SS_APP_BYTES, malloc'd
  ss::App* app;
  ss::Inputs inputs;
  uint16_t* fb;             // SS_WIDTH * SS_HEIGHT RGB565
  ss::Surface surface;      // view of fb
  uint64_t seed;
  uint32_t tick;            // ticks since the last (re)start
  int speed;                // tick multiplier used by front ends (1 normal, 8 fast-forward)
  long long doorbell_at;    // fire a doorbell when tick reaches this; -1 = never
  bool log_beats;           // see previewEnableBeatLog
  int loop_index;           // loops started so far (the first loop is 0)
  char last_beat[48];       // beat name last logged; empty after a loop start
  int minors_seen;          // this loop's minor count last logged (0 after a loop start)
};

// Inputs from parsed options (mood, weather, night, status, dog, lookout).
ss::Inputs previewInputsFromOptions(const PreviewOptions& o);

// Allocate storage, framebuffer and start the app. Returns false on allocation failure.
bool previewInit(PreviewState& st, uint64_t seed, const ss::Inputs& in);
void previewShutdown(PreviewState& st);

// Simulate n ticks without rendering (used for --skip).
// Returns the number of ticks on which a new loop started.
int previewTicks(PreviewState& st, int n);
// Simulate n ticks, then render one frame. Returns new-loop count as above.
int previewStep(PreviewState& st, int n);

// Ticks to simulate for output frame number `frame` at the options' fps
// (integer-exact, so 50 fps alternates 0 and 1 tick per frame).
int previewTicksForFrame(int fps, int frame);
// Same, but honouring o.every (a fixed tick count per frame) when it is set.
int previewTicksForFrameOpts(const PreviewOptions& o, int frame);

// --loops N: simulate N whole loops with no rendering and print, per loop,
//   "loop=L seed=S length=Ts plan=P memory=M"
// (P is appLoopPlan, M is appMemory as the loop leaves it for the next).
// Returns the number of loops run.
int previewRunLoops(PreviewState& st, int n, FILE* out);

// Log to stdout from now on, in previewTicks (so --skip ticks are included):
//   "loop=L seed=S start tick=T plan=P"     a loop started (L from 0); P is appLoopPlan
//   "tick=T loop=L seed=S beat=NAME"        the beat name changed (content compare)
// T is the ticks simulated since the last (re)start, counted after the tick that
// caused the event. The current loop and beat are logged immediately.
void previewEnableBeatLog(PreviewState& st);

void previewSetMood(PreviewState& st, float mood);
void previewSetWeatherCode(PreviewState& st, int code);   // also marks have_outside
void previewSetNight(PreviewState& st, bool night);
void previewSetSeed(PreviewState& st, uint64_t seed);     // restart with a new seed
void previewRestart(PreviewState& st);                    // restart, same seed
void previewDoorbell(PreviewState& st);

// Write the framebuffer as binary P6 PPM, 800x480. Returns false on I/O error.
bool previewWritePPM(const PreviewState& st, const char* path);

// "seed=7 tick=120 beat=stub t=0.42 mood=3.50 weather=clear"
void previewHudLine(const PreviewState& st, char* buf, size_t n);

const char* previewWeatherName(int code);   // "clear".."chill", or "?"

// Parse command line into `o`. Prints usage and exits 2 on an unknown or malformed
// argument; --help prints usage and exits 0.
void previewParseArgs(int argc, char** argv, PreviewOptions& o);
void previewUsage(const char* prog, FILE* to);
