// Fixed-timestep timeline. See scheduler.h.
//
// Boundary rule: a beat ends on the first tick whose accumulated elapsed time
// reaches its duration (with a 0.1% of a tick tolerance for float drift). On
// that tick the beat's update has already run with the pre-increment t, then
// end() is called, current advances, elapsed = leftover (never negative), and
// the new beat's begin() is called. The new beat's first update happens on the
// next tick. Leftover carries across beats, so the total number of tick() calls
// is ceil(sum(durations) * SS_TICK_HZ), whatever the beat split.
#include "scheduler.h"
#include "ease.h"

namespace ss {

namespace {
constexpr float kBoundaryEps = SS_TICK_DT * 0.001f;
}

void Timeline::reset() {
  count = 0;
  current = -1;
  elapsed = 0.f;
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
  const float t = (b.duration > 0.f) ? clamp01(elapsed / b.duration) : 1.f;
  if (b.update) b.update(ctx, t, SS_TICK_DT);

  elapsed += SS_TICK_DT;
  if (elapsed + kBoundaryEps >= b.duration) {
    float leftover = elapsed - b.duration;
    if (leftover < 0.f) leftover = 0.f;
    if (b.end) b.end(ctx);
    ++current;
    elapsed = leftover;
    if (current >= count) {
      current = -1;
      elapsed = 0.f;
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
  if (beats[current].begin) beats[current].begin(ctx);
}

float Timeline::progress() const {
  if (current < 0 || current >= count) return 0.f;
  const float d = beats[current].duration;
  return d > 0.f ? clamp01(elapsed / d) : 0.f;
}

}  // namespace ss
