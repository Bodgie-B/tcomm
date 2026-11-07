// The downstairs tenant's requests for heat or cooling (Mike, 2026-11-06; docs/temperature-requests.md,
// stage 2). After a survey answer the tenant can SEND an arrow upstairs: mild heat, urgent heat, mild
// cooling or urgent cooling. Up to three arrows show at the top right of the UPSTAIRS tile on both
// panels, newest on the right; a fourth pushes the oldest off. Mike clears them all with one tap,
// and the downstairs panel then shows "Action taken on temperature" for 30 minutes or until the
// tenant taps it. The tenant can tap the arrows to take them back: gone at once on both panels. An
// arrow nobody clears goes after 4 hours.
//
// Kept like the mail badge: one state with a change counter, and the higher counter wins; a tie
// goes to the later change, then to the state with more arrows (better an extra arrow than a lost
// request), then to the larger bytes, so both panels always pick the same one. Age limits are worked out from the times, not stored as changes, so the
// two panels never disagree about them and nothing is sent when an arrow times out.
//
// Plain data with no default member initializers: RequestState rides in the protocol's Msg union.
#pragma once
#include "types.h"
#include "comfort.h"

namespace tc {

// Who sent an arrow. Predicted arrows come later, from the comfort chart, when Mike turns them on.
enum class ReqSource : uint8_t { Tenant = 0, Predicted = 1 };
inline bool req_source_valid(uint8_t v) { return v <= 1; }

constexpr int kMaxArrows = 3;
constexpr uint32_t kArrowLifeS = 4 * 3600;  // Mike asked for "some hours"; 4 is my pick
constexpr uint32_t kActedShowS = 30 * 60;   // Mike: the message stays 30 minutes unless tapped
constexpr uint32_t kUrgentDingMs = 15000;   // Mike: an urgent arrow dings upstairs every 15 s
constexpr uint32_t kFlashHalfMs = 500;      // Mike: urgent arrows lit half a second, dim half a second

inline bool ask_urgent(Ask a) { return a == Ask::UrgentHeat || a == Ask::UrgentCool; }
inline bool ask_heat(Ask a) { return a == Ask::UrgentHeat || a == Ask::MildHeat; }
// The arrow the survey picks for an answer: MUCH TOO HOT urgent cooling, TOO WARM mild cooling,
// TOO COOL mild heat, MUCH TOO COLD urgent heat, JUST RIGHT none.
Ask ask_for_feel(Feel f);

struct ReqArrow {
  uint8_t ask;      // an Ask, never None
  uint8_t source;   // a ReqSource
  uint32_t sent;    // UTC it was sent; 0 = the clock wasn't set (it then never times out)
};

struct RequestState {
  uint8_t n;                    // arrows held, oldest first
  ReqArrow a[kMaxArrows];
  uint32_t acted;               // UTC Mike cleared them, for the downstairs message; 0 = none
  bool acted_dismissed;         // the tenant tapped the message away
  uint32_t since;               // UTC of the last change; 0 = unknown
  uint32_t ver;                 // the change counter
};
bool operator==(const RequestState &a, const RequestState &b);

// Why an arrow ended, for the requests file.
enum class ReqEnd : uint8_t { Cleared = 0, TakenBack = 1, PushedOff = 2, TimedOut = 3 };
const char *req_end_code(ReqEnd e);  // "CLEARED", "TAKEN_BACK", "PUSHED_OFF", "TIMED_OUT"

class RequestModel {
 public:
  void restore(const RequestState &s) { s_ = s; }
  const RequestState &state() const { return s_; }

  // Downstairs: a new arrow, newest on the right. A fourth pushes the oldest off: it's copied to
  // *pushed (if given) and true is returned in *did_push. Arrows already past kArrowLifeS are
  // dropped first. Clears any "action taken" message.
  void add(Ask a, ReqSource s, const WallTime &now, ReqArrow *pushed = nullptr, bool *did_push = nullptr);
  // Downstairs: the tenant took them back. False if none were showing.
  bool take_back(const WallTime &now);
  // Upstairs: Mike has acted. All arrows go and the message shows downstairs. False if none were
  // showing.
  bool clear(const WallTime &now);
  // Downstairs: the tenant tapped the message away. False if it wasn't showing.
  bool dismiss_acted(const WallTime &now);
  // A REQUESTS message from the other panel. True if anything changed here.
  bool apply_remote(const RequestState &in);
  // Downstairs: arrows sent before the clock was set get the time now, so they age out like the
  // others (they would otherwise stay up, and an urgent one ding, until Mike taps). True if any did.
  bool stamp_unknown(const WallTime &now);

  // What shows now (utc 0: no clock, so nothing ages out). Arrows older than kArrowLifeS don't
  // count; the message shows for kActedShowS after Mike's tap unless dismissed.
  int showing(uint32_t utc, ReqArrow *out) const;  // fills out[kMaxArrows], oldest first
  bool any_urgent(uint32_t utc) const;
  bool acted_showing(uint32_t utc) const;

 private:
  void bump(const WallTime &now);
  RequestState s_ = {};
};

// The requests file ("requests.csv" in /tcomm, downstairs only): one line per arrow once it ends.
//   sent_date,sent_time,part_of_day,arrow,source,ended,ended_date,ended_time,waited_min
// Times local (utc + offset); blank when a time is 0. waited_min: whole minutes from sent to ended,
// blank if either is unknown.
extern const char kRequestsFile[];       // "requests.csv"
extern const char kRequestsCsvHeader[];  // the header above, with its "\n"
size_t request_csv_line(const ReqArrow &a, ReqEnd why, uint32_t ended_utc, int32_t offset, char *out, size_t cap);

}  // namespace tc
