// Fixed-timestep scheduler: a timeline of beats plus an always-on ambient tick.
//
// Simulation advances in SS_TICK_DT steps. A beat receives normalized progress
// t in 0..1 over its duration and is told when it starts and ends. Ambient
// systems update every tick regardless of the current beat. External events
// (doorbell, status change) are queued with a tick stamp so a run is fully
// described by (seed, input log) and replays exactly.
#pragma once

#include "../ss_platform.h"

namespace ss {

struct SceneCtx;   // defined by the scene layer; passed through untouched

struct Beat {
  const char* name;                                        // for the preview HUD and logs
  float duration;                                          // seconds, > 0
  void (*begin)(SceneCtx& ctx);                            // may be null
  void (*update)(SceneCtx& ctx, float t, float dt);        // t 0..1 within beat
  void (*end)(SceneCtx& ctx);                              // may be null
};

#define SS_MAX_BEATS 64

enum EventType : uint8_t { EV_NONE = 0, EV_DOORBELL, EV_STATUS, EV_DOG, EV_WEATHER };

struct Event {
  EventType type;
  int32_t arg;      // status id, dog id, weather code; 0 if unused
};

#define SS_MAX_PENDING_EVENTS 8

struct Timeline {
  Beat beats[SS_MAX_BEATS];
  int count;
  int current;        // index of the running beat, or -1 before start / after end
  float elapsed;      // seconds into the current beat
  uint32_t tick;      // ticks since loop start

  Event pending[SS_MAX_PENDING_EVENTS];
  int pending_count;

  void reset();
  bool add(const Beat& b);                   // false when full
  void insertAfterCurrent(const Beat& b);    // for reactions (doorbell) spliced in
  void start(SceneCtx& ctx);                 // begins beat 0

  // Advance one logical tick. Returns false once the last beat has ended.
  bool tick(SceneCtx& ctx);

  // Queue an external event. Processed at the start of the next tick by the
  // scene's event handler (see SceneCtx in the scene layer).
  bool post(Event e);

  // Jump to a beat by index (preview "jump to beat"). Runs end/begin hooks.
  void jumpTo(SceneCtx& ctx, int index);

  const Beat* currentBeat() const { return (current >= 0 && current < count) ? &beats[current] : nullptr; }
  float progress() const;                    // 0..1 within the current beat
};

}  // namespace ss
