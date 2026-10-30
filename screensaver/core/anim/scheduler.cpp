// Fixed-timestep timeline. See scheduler.h.
//
// Boundary rule: time is counted in whole ticks, never accumulated as float
// seconds. A beat lasts dur_ticks = max(1, ceil(duration * SS_TICK_HZ - 1e-3))
// ticks (the 1e-3 keeps float noise in duration * HZ from adding a tick). Its
// update runs on each of those ticks with t = beat_ticks / (dur_ticks - 1)
// computed before the increment, so the first update has t = 0 and the last
// has t = 1 exactly (a one-tick beat gets t = 1). After the last update, end() is called, current advances and the new
// beat's begin() runs; its first update is on the next tick. Nothing carries
// across beats, so the total number of tick() calls is the sum of the
// per-beat dur_ticks and a 600 s beat at 25 Hz ends on tick 15000 exactly.
// Timeline::elapsed is derived as beat_ticks * SS_TICK_DT for progress().
#include "scheduler.h"
#include "ease.h"

#include <math.h>

namespace ss {

namespace {
// Whole ticks a beat lasts; at least 1.
uint32_t beatDurTicks(const Beat& b) {
  const float x = ceilf(b.duration * (float)SS_TICK_HZ - 1e-3f);
  return (x >= 1.f) ? (uint32_t)x : 1u;
}
}

void Timeline::reset() {
  count = 0;
  current = -1;
  elapsed = 0.f;
  beat_ticks = 0;
  tick_count = 0;
  pending_count = 0;
}

bool Timeline::add(const Beat& b) {
  if (count >= SS_MAX_BEATS) return false;
  beats[count++] = b;
  return true;
}

void Timeline::insertAfterCurrent(const Beat& b) {
  if (count >= SS_MAX_BEATS) return;   // fails silently
  // Before start or after the end there is no current beat: append.
  const int pos = (current < 0) ? count : current + 1;
  for (int i = count; i > pos; --i) beats[i] = beats[i - 1];
  beats[pos] = b;
  ++count;
}

void Timeline::start(SceneCtx& ctx) {
  tick_count = 0;
  elapsed = 0.f;
  beat_ticks = 0;
  if (count <= 0) {
    current = -1;
    return;
  }
  current = 0;
  if (beats[0].begin) beats[0].begin(ctx);
}

bool Timeline::tick(SceneCtx& ctx) {
  ++tick_count;
  if (current < 0 || current >= count) return false;

  const Beat& b = beats[current];
  const uint32_t dur_ticks = beatDurTicks(b);
  // t spans 0 on the first update to exactly 1 on the last, so a beat's final
  // pose is reached and the next beat can hold it.
  const float t = dur_ticks > 1 ? clamp01((float)beat_ticks / (float)(dur_ticks - 1)) : 1.f;
  if (b.update) b.update(ctx, t, SS_TICK_DT);

  ++beat_ticks;
  elapsed = (float)beat_ticks * SS_TICK_DT;
  if (beat_ticks >= dur_ticks) {
    if (b.end) b.end(ctx);
    ++current;
    beat_ticks = 0;
    elapsed = 0.f;
    if (current >= count) {
      current = -1;
      return false;
    }
    if (beats[current].begin) beats[current].begin(ctx);
  }
  return true;
}

bool Timeline::post(Event e) {
  if (pending_count >= SS_MAX_PENDING_EVENTS) return false;
  pending[pending_count++] = e;
  return true;
}

int Timeline::takeEvents(Event* out, int max) {
  if (out == nullptr || max <= 0) return 0;
  const int n = pending_count < max ? pending_count : max;
  for (int i = 0; i < n; ++i) out[i] = pending[i];
  for (int i = n; i < pending_count; ++i) pending[i - n] = pending[i];
  pending_count -= n;
  return n;
}

void Timeline::jumpTo(SceneCtx& ctx, int index) {
  if (index < 0 || index >= count) return;
  if (current >= 0 && current < count && beats[current].end) beats[current].end(ctx);
  current = index;
  elapsed = 0.f;
  beat_ticks = 0;
  if (beats[current].begin) beats[current].begin(ctx);
}

float Timeline::progress() const {
  if (current < 0 || current >= count) return 0.f;
  const float d = beats[current].duration;
  return d > 0.f ? clamp01(elapsed / d) : 0.f;
}

}  // namespace ss
