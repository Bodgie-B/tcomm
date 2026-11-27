#include "status.h"
#include <stdio.h>
#include <string.h>

namespace tc {

// ---- Substatuses (Mike, 2026-11-26) ----

namespace {
// [floor][person]: upstairs 1 and 2, downstairs 1 and 2. The fallbacks never name anyone.
char g_away[2][2][kAwayNameMax + 1] = {{"Person 1", "Person 2"}, {"Person 3", "Person 4"}};
}  // namespace

void set_away_name(Floor f, int i, const char *name) {
  if (i < 0 || i > 1 || !name || !name[0]) return;
  snprintf(g_away[(int)f][i], sizeof(g_away[0][0]), "%s", name);
}

const char *away_name(Floor f, int i) { return g_away[(int)f][i ? 1 : 0]; }

void sub_label(char *out, size_t n, Sub s, Floor f) {
  switch (s) {
    case Sub::Courtesy: snprintf(out, n, "Courtesy ring"); break;
    case Sub::Free: snprintf(out, n, "Free passage"); break;
    case Sub::Quiet: snprintf(out, n, "Quiet passage"); break;
    case Sub::PreferNo: snprintf(out, n, "Prefer no passage"); break;
    case Sub::AllOut: snprintf(out, n, "All out"); break;
    case Sub::OneOut1: snprintf(out, n, "%s out", away_name(f, 0)); break;
    case Sub::OneOut2: snprintf(out, n, "%s out", away_name(f, 1)); break;
    default: if (n) out[0] = 0; break;
  }
}

// PROPOSED wording: what the other floor sees and what a ring does here, short enough for one line
// beside the badge in the menu.
const char *sub_effect(Status st, Sub s) {
  switch (s) {
    case Sub::Free: return "Bell off: COME ON IN";
    case Sub::Quiet: return "Bell off: COME IN QUIETLY";
    case Sub::Courtesy: return st == Status::Busy ? "A soft chime when they ring" : "The bell rings as usual";
    case Sub::PreferNo: return st == Status::Sleeping ? "Silent. Kept for when you're back" : "A quiet chime, only if needed";
    case Sub::AllOut: return "Rings are kept for your return";
    case Sub::OneOut1:
    case Sub::OneOut2: return "Someone's home: the bell rings";
    default: return "";
  }
}

bool own_from_saved(uint16_t ver, const uint8_t *data, size_t size, OwnState &out) {
  if (!data || size != sizeof(OwnState)) return false;
  OwnState s;
  if (ver == 1) {
    OwnStateV1 v1;
    memcpy((void *)&v1, data, sizeof(v1));
    s.status = v1.status;
    s.since = v1.since;
    s.until = v1.until;
    s.fallback = v1.fallback;
    if (!status_valid((uint8_t)s.status)) return false;
    s.sub = default_sub(s.status);
  } else if (ver == kOwnVersion) {
    memcpy((void *)&s, data, sizeof(s));
  } else {
    return false;
  }
  if (!status_valid((uint8_t)s.status) || !status_valid((uint8_t)s.fallback)) return false;
  if (!sub_valid(s.status, (uint8_t)s.sub)) return false;
  out = s;
  return true;
}

// ---- The status ----

void StatusModel::restart_since(const WallTime &now, uint32_t mono_ms) {
  cur_.since = now.valid ? now.utc : 0;
  since_pending_ = !now.valid;
  since_mono_ = mono_ms;
}

bool StatusModel::choose(Status s, const WallTime &now, uint32_t mono_ms) {
  const Sub d = default_sub(s);
  if (s == cur_.status && cur_.until == 0 && cur_.sub == d) return false;
  // A new status, or the same one with an option put back to its default: the line starts again.
  if (s != cur_.status || cur_.sub != d) restart_since(now, mono_ms);
  cur_.status = s;
  cur_.sub = d;
  cur_.until = 0;  // a plain choice cancels any timer
  return true;
}

bool StatusModel::choose_timed(Status s, uint32_t duration_s, Status fallback, const WallTime &now) {
  if (!now.valid || duration_s == 0 || fallback == s) return false;
  if (s != cur_.status || cur_.sub != default_sub(s)) {
    cur_.since = now.utc;
    since_pending_ = false;
  }
  cur_.status = s;
  cur_.sub = default_sub(s);
  cur_.until = now.utc + duration_s;
  cur_.fallback = fallback;
  return true;
}

bool StatusModel::choose_sub(Sub s, const WallTime &now, uint32_t mono_ms) {
  if (!sub_valid(cur_.status, (uint8_t)s) || s == cur_.sub) return false;
  cur_.sub = s;
  restart_since(now, mono_ms);
  return true;
}

bool StatusModel::tick(const WallTime &now) {
  if (!now.valid || cur_.until == 0 || now.utc < cur_.until) return false;
  cur_.since = cur_.until;
  cur_.status = cur_.fallback;
  cur_.sub = default_sub(cur_.status);
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
