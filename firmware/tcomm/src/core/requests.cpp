// The heat and cooling arrows (requests.h). Mike, 2026-11-06.
#include "requests.h"
#include "civil.h"
#include <stdio.h>
#include <string.h>

namespace tc {

const char kRequestsFile[] = "requests.csv";
const char kRequestsCsvHeader[] = "sent_date,sent_time,part_of_day,arrow,source,ended,ended_date,ended_time,waited_min\n";

Ask ask_for_feel(Feel f) {
  switch (f) {
    case Feel::MuchTooHot: return Ask::UrgentCool;
    case Feel::TooWarm: return Ask::MildCool;
    case Feel::TooCool: return Ask::MildHeat;
    case Feel::MuchTooCold: return Ask::UrgentHeat;
    default: return Ask::None;
  }
}

const char *req_end_code(ReqEnd e) {
  switch (e) {
    case ReqEnd::Cleared: return "CLEARED";
    case ReqEnd::TakenBack: return "TAKEN_BACK";
    case ReqEnd::PushedOff: return "PUSHED_OFF";
    case ReqEnd::TimedOut: return "TIMED_OUT";
  }
  return "";
}

bool operator==(const RequestState &a, const RequestState &b) {
  if (a.n != b.n || a.acted != b.acted || a.acted_dismissed != b.acted_dismissed || a.since != b.since || a.ver != b.ver)
    return false;
  for (int i = 0; i < a.n && i < kMaxArrows; i++)  // slots past n are leftovers, not state
    if (a.a[i].ask != b.a[i].ask || a.a[i].source != b.a[i].source || a.a[i].sent != b.a[i].sent) return false;
  return true;
}

// An arrow is past its life only when both times are known. A "sent" later than now (the clocks
// differ a little) counts as young.
static bool aged(uint32_t sent, uint32_t utc) {
  return sent && utc && utc > sent && utc - sent >= kArrowLifeS;
}

// Unused slots are kept zero so a state compares and travels the same on both panels.
static void tidy(RequestState &s) {
  for (int i = s.n; i < kMaxArrows; i++) s.a[i] = ReqArrow{0, 0, 0};
}

void RequestModel::bump(const WallTime &now) {
  s_.ver++;  // one past the highest seen: apply_remote keeps the higher counter
  if (now.valid) s_.since = now.utc;
}

void RequestModel::add(Ask a, ReqSource src, const WallTime &now, ReqArrow *pushed, bool *did_push) {
  if (did_push) *did_push = false;
  uint32_t utc = now.valid ? now.utc : 0;
  int k = 0;
  for (int i = 0; i < s_.n; i++)
    if (!aged(s_.a[i].sent, utc)) s_.a[k++] = s_.a[i];
  s_.n = (uint8_t)k;
  if (s_.n >= kMaxArrows) {  // Mike: a fourth pushes the oldest off
    if (pushed) *pushed = s_.a[0];
    if (did_push) *did_push = true;
    for (int i = 1; i < kMaxArrows; i++) s_.a[i - 1] = s_.a[i];
    s_.n = kMaxArrows - 1;
  }
  s_.a[s_.n++] = ReqArrow{(uint8_t)a, (uint8_t)src, utc};
  s_.acted = 0;
  s_.acted_dismissed = false;
  tidy(s_);
  bump(now);
}

bool RequestModel::take_back(const WallTime &now) {
  if (!showing(now.valid ? now.utc : 0, nullptr)) return false;
  s_.n = 0;
  tidy(s_);
  bump(now);
  return true;
}

bool RequestModel::clear(const WallTime &now) {
  if (!showing(now.valid ? now.utc : 0, nullptr)) return false;
  s_.n = 0;
  tidy(s_);
  s_.acted = now.valid ? now.utc : 1;  // 1: cleared with no clock, so the message shows until tapped
  s_.acted_dismissed = false;
  bump(now);
  return true;
}

bool RequestModel::dismiss_acted(const WallTime &now) {
  if (!acted_showing(now.valid ? now.utc : 0)) return false;
  s_.acted_dismissed = true;
  bump(now);
  return true;
}

// The same answer on both panels for any two states, so a tie can't bounce back and forth.
static bool wins(const RequestState &in, const RequestState &cur) {
  if (in.ver != cur.ver) return in.ver > cur.ver;
  if (in.since != cur.since) return in.since > cur.since;
  if (in.n != cur.n) return in.n > cur.n;
  // Field by field, not memcmp: the padding between fields needn't match on the two panels.
  for (int i = 0; i < in.n && i < kMaxArrows; i++) {
    if (in.a[i].ask != cur.a[i].ask) return in.a[i].ask > cur.a[i].ask;
    if (in.a[i].source != cur.a[i].source) return in.a[i].source > cur.a[i].source;
    if (in.a[i].sent != cur.a[i].sent) return in.a[i].sent > cur.a[i].sent;
  }
  if (in.acted != cur.acted) return in.acted > cur.acted;
  return in.acted_dismissed > cur.acted_dismissed;
}

bool RequestModel::apply_remote(const RequestState &in) {
  if (in == s_) return false;
  RequestState t = in;
  tidy(t);
  if (!wins(t, s_)) return false;
  s_ = t;
  return true;
}

bool RequestModel::stamp_unknown(const WallTime &now) {
  if (!now.valid) return false;
  bool any = false;
  for (int i = 0; i < s_.n && i < kMaxArrows; i++)
    if (!s_.a[i].sent) {
      s_.a[i].sent = now.utc;
      any = true;
    }
  if (any) bump(now);
  return any;
}

int RequestModel::showing(uint32_t utc, ReqArrow *out) const {
  int k = 0;
  for (int i = 0; i < s_.n && i < kMaxArrows; i++) {
    if (aged(s_.a[i].sent, utc)) continue;
    if (out) out[k] = s_.a[i];
    k++;
  }
  return k;
}

bool RequestModel::any_urgent(uint32_t utc) const {
  ReqArrow a[kMaxArrows];
  int n = showing(utc, a);
  for (int i = 0; i < n; i++)
    if (ask_urgent((Ask)a[i].ask)) return true;
  return false;
}

bool RequestModel::acted_showing(uint32_t utc) const {
  if (!s_.acted || s_.acted_dismissed) return false;
  return !utc || s_.acted == 1 || !(utc > s_.acted && utc - s_.acted >= kActedShowS);
}

size_t request_csv_line(const ReqArrow &a, ReqEnd why, uint32_t ended_utc, int32_t offset, char *out, size_t cap) {
  char sd[16] = "", st[8] = "", ed[16] = "", et[8] = "", wait[16] = "";
  const char *part = "";
  if (a.sent) {
    Civil c = civil_from_seconds((int64_t)a.sent + offset);
    snprintf(sd, sizeof(sd), "%04d-%02d-%02d", c.year, c.month, c.day);
    snprintf(st, sizeof(st), "%02d:%02d", c.hour, c.minute);
    part = day_part_word(day_part(c.hour * 60 + c.minute));
  }
  if (ended_utc) {
    Civil c = civil_from_seconds((int64_t)ended_utc + offset);
    snprintf(ed, sizeof(ed), "%04d-%02d-%02d", c.year, c.month, c.day);
    snprintf(et, sizeof(et), "%02d:%02d", c.hour, c.minute);
  }
  if (a.sent && ended_utc && ended_utc >= a.sent) snprintf(wait, sizeof(wait), "%u", (unsigned)((ended_utc - a.sent) / 60));
  int n = snprintf(out, cap, "%s,%s,%s,%s,%s,%s,%s,%s,%s\n", sd, st, part, ask_valid(a.ask) ? ask_code((Ask)a.ask) : "",
                   a.source == (uint8_t)ReqSource::Predicted ? "PREDICTED" : "TENANT", req_end_code(why), ed, et, wait);
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

}  // namespace tc
