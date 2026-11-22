#include "mail.h"

namespace tc {

void MailModel::set(bool waiting, bool none, const WallTime &now) {
  s_.waiting = waiting;
  s_.none = none;
  s_.since = now.valid ? now.utc : 0;
  s_.ver++;  // one past the highest seen: apply_remote keeps the higher counter
}

void MailModel::toggle(const WallTime &now) { set(!s_.waiting, false, now); }

void MailModel::toggle_none(const WallTime &now) { set(false, !s_.none, now); }

bool MailModel::acknowledge(const WallTime &now) {
  if (!s_.waiting && !s_.none) return false;
  set(false, false, now);
  return true;
}

bool MailModel::acknowledge_mail(const WallTime &now) {
  if (!s_.waiting) return false;
  set(false, false, now);
  return true;
}

bool MailModel::acknowledge_none(const WallTime &now) {
  if (!s_.none) return false;
  set(false, false, now);
  return true;
}

// 2 mail waiting, 1 no mail, 0 neither: on a true tie the louder one stays, the same on both panels.
static int rank(const MailState &s) { return s.waiting ? 2 : s.none ? 1 : 0; }

static bool newer(const MailState &in, const MailState &cur) {
  if (in.ver != cur.ver) return in.ver > cur.ver;
  if (in.since != cur.since) return in.since > cur.since;
  return rank(in) > rank(cur);  // a true tie: both panels keep the same one
}

bool MailModel::apply_remote(const MailState &in) {
  if (in == s_ || !newer(in, s_)) return false;
  s_ = in;
  return true;
}

}  // namespace tc
