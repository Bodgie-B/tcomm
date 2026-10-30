// Public interface of the screensaver module.
//
// This is the whole surface the panel firmware (port_esp32/) and the PC preview
// (sim/) talk to. Integration into TComm means calling these functions and
// nothing else. The host owns the framebuffer and the clock; core owns the
// scene, the scheduler and the renderer.
#pragma once

#include "ss_platform.h"
#include "gfx/surface.h"
#include "palette.h"
#include "anim/scheduler.h"

namespace ss {

// Inputs the host keeps up to date. Core reads them at tick boundaries.
struct Inputs {
  float mood;             // solar-day position, see palette.h (0 night .. 5 dusk)
  WeatherTint weather;    // overcast, fog, night_dim in 0..1
  int weather_code;       // scene weather: 0 clear, 1 rain, 2 snow, 3 wind, 4 heat, 5 fog, 6 chill (expandable)
  bool have_outside;      // false: no OUTSIDE reading for 15 min, pick random weather
  int other_status;       // 0 Open, 1 Busy, 2 Private, 3 Sleeping, 4 Away, 5 No signal
  int dog_out;            // 0 none, 1 Lucy, 2 Copper
  bool night_mode;        // night rule in force (dim, no large bright areas)
  bool lookout_window;    // true inside the sunset window: play the Lookout spine
};

struct App;   // opaque to the host; defined in core/scene/app.cpp

// Size of the App object so the host can place it in internal SRAM or PSRAM
// without core allocating. Must be >= sizeof(App); checked at compile time in
// app.cpp.
#define SS_APP_BYTES (96 * 1024)

// Start the screensaver. `storage` is SS_APP_BYTES of host memory. `seed` picks
// the first loop; later loops reseed from the loop's own RNG, so a first seed
// fully determines the sequence of loops.
App* appStart(void* storage, const HostAlloc& alloc, uint64_t seed, const Inputs& in);
void appStop(App* app, const HostAlloc& alloc);

void appSetInputs(App* app, const Inputs& in);
bool appPostEvent(App* app, Event e);          // doorbell, status change, dog

// Advance one logical tick (SS_TICK_DT). The host calls this at SS_TICK_HZ.
// Returns true when a new loop started this tick (for the preview log).
bool appTick(App* app);

// Composite the current state into `fb` (full frame, origin_y 0). Only dirty
// strips are written. Returns the number of strips written.
int appRender(App* app, Surface& fb);

// Force every strip dirty (after the host touched the framebuffer, or on resume).
void appInvalidate(App* app);

// Introspection for the preview HUD and logs.
const char* appBeatName(const App* app);
float appBeatProgress(const App* app);
uint64_t appLoopSeed(const App* app);
uint32_t appTickCount(const App* app);
// Build-up phase: 0 sky, 1 ridges, 2 trees, 3 stream, 4 plants, 5 fire, 6 done.
int appBuildPhase(const App* app);
// sizeof(App), for budget logs (must stay <= SS_APP_BYTES).
uint32_t appAppBytes();
// This loop's story variant picks, e.g. "arrival=1 setup=0 firewood=2 fire=1
// dinner=0 foodbag=0 bear=0 close=0 fillers=none". Returns snprintf's count.
int appLoopPlan(const App* app, char* buf, size_t n);
// The frame area the hiker covers this tick (empty when out of frame);
// returns whether he is visible.
bool appHikerRect(const App* app, Rect* out);
// Spine beats this loop that ended before their script did (0 when the
// timing is right).
int appSpineOverruns(const App* app);

}  // namespace ss
