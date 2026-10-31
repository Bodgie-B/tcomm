#include "mail.h"

namespace tc {

void MailModel::set(bool waiting, const WallTime &now) {
  s_.waiting = waiting;
  s_.since = now.valid ? now.utc : 0;
  s_.ver++;  // one past the highest seen: apply_remote keeps the higher counter
}

void MailModel::toggle(const WallTime &now) { set(!s_.waiting, now); }

bool MailModel::acknowledge(const WallTime &now) {
  if (!s_.waiting) return false;
  set(false, now);
  return true;
}

static bool newer(const MailState &in, const MailState &cur) {
  if (in.ver != cur.ver) return in.ver > cur.ver;
  if (in.since != cur.since) return in.since > cur.since;
  return in.waiting && !cur.waiting;  // a true tie: both panels keep the same one
}

bool MailModel::apply_remote(const MailState &in) {
  if (in == s_ || !newer(in, s_)) return false;
  s_ = in;
  return true;
}

}  // namespace tc
