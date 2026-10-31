// The panel owner's status and its optional timer. (No Undo: Mike, 2026-10-22, "it's easy enough
// to select a different status through the standard menu".)
#pragma once
#include "types.h"

namespace tc {

struct OwnState {
  Status status = Status::Open;
  uint32_t since = 0;   // UTC when this status began; 0 = unknown (clock wasn't set)
  uint32_t until = 0;   // UTC when the timer ends; 0 = no timer
  Status fallback = Status::Busy;
};

class StatusModel {
 public:
  void restore(const OwnState &s) {
    cur_ = s;
    since_pending_ = false;
  }
  const OwnState &state() const { return cur_; }

  // Any change made by the owner. Choosing a new status cancels a running timer.
  // Returns false if nothing changed (same status, no timer running).
  bool choose(Status s, const WallTime &now, uint32_t mono_ms);
  // Start a timed status. Needs a valid clock: the expiry has to survive a reboot.
  bool choose_timed(Status s, uint32_t duration_s, Status fallback, const WallTime &now);

  // Applies an expired timer: status becomes the fallback, starting at the expiry time. Does
  // nothing to the timer while the clock is unknown. Returns true if the status changed.
  bool tick(const WallTime &now);

  // The clock became valid: a status chosen while it was unknown gets a start time, back-dated by
  // the time that has passed since it was chosen. Returns true if the start time changed.
  bool clock_became_valid(uint32_t utc, uint32_t mono_ms);

 private:
  OwnState cur_;
  bool since_pending_ = false;  // chosen with no clock; since_mono_ is when
  uint32_t since_mono_ = 0;
};

}  // namespace tc
