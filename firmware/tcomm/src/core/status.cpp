#include "status.h"

namespace tc {

bool StatusModel::choose(Status s, const WallTime &now, uint32_t mono_ms) {
  if (s == cur_.status && cur_.until == 0) return false;
  if (s != cur_.status) {
    cur_.since = now.valid ? now.utc : 0;
    since_pending_ = !now.valid;
    since_mono_ = mono_ms;
  }
  cur_.status = s;
  cur_.until = 0;  // a plain choice cancels any timer
  return true;
}

bool StatusModel::choose_timed(Status s, uint32_t duration_s, Status fallback, const WallTime &now) {
  if (!now.valid || duration_s == 0 || fallback == s) return false;
  if (s != cur_.status) {
    cur_.since = now.utc;
    since_pending_ = false;
  }
  cur_.status = s;
  cur_.until = now.utc + duration_s;
  cur_.fallback = fallback;
  return true;
}

bool StatusModel::tick(const WallTime &now) {
  if (!now.valid || cur_.until == 0 || now.utc < cur_.until) return false;
  cur_.since = cur_.until;
  cur_.status = cur_.fallback;
  cur_.until = 0;
  since_pending_ = false;
  return true;
}

bool StatusModel::clock_became_valid(uint32_t utc, uint32_t mono_ms) {
  if (cur_.since != 0) return false;
  uint32_t ago = since_pending_ ? (uint32_t)ms_diff(mono_ms, since_mono_) / 1000 : 0;
  cur_.since = ago < utc ? utc - ago : utc;
  since_pending_ = false;
  return true;
}

}  // namespace tc
