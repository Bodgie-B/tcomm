// The household calendar (calendar roll-down spec, section 2): one shared list of events that both
// panels keep a copy of and sync over the radio.
//
// PROPOSED, from the spec's data model with these changes: a timed event's length is kept in
// minutes (the Day board shows a 10:00 - 10:30 AM call, which whole hours can't hold); deleting
// keeps a tombstone, so syncing with the other panel can't bring an event back; and each event
// carries a version, so when both panels changed the same event the later change wins.
//
// Repeating events (Mike, 2026-10-25): daily, weekly, monthly or yearly, ending on a date, after a
// number of times, or never. Birthdays and anniversaries repeat yearly and show their own icon.
// One occurrence changed or deleted on its own ("just this one") is taken out of the series by its
// day, and a changed one becomes an event of its own.
//
// Past events are kept for good (Mike, 2026-10-25). The store in memory holds what's upcoming,
// what repeats, and the last kRecentDays; older events move to the archive, a file per month (see
// archive_name), and are read back when the calendar shows those months.
#pragma once
#include <stddef.h>
#include "types.h"

namespace tc {

constexpr int kMaxEvents = 240;      // in memory: upcoming, repeating and recent events, recent deletions
constexpr int kRecentDays = 92;      // older ones go to the archive
constexpr size_t kEventTitleMax = 40;
constexpr size_t kEventNotesMax = 120;
constexpr int kMaxEventDays = 31;    // PROPOSED: the longest all-day event
constexpr int kMaxEventMinutes = 24 * 60;
constexpr int kMaxSkips = 24;        // occurrences of one series changed or deleted on their own
constexpr int32_t kForever = 0x3FFFFFFF;  // the end of a series that never ends (no overflow adding to it)

enum class Repeat : uint8_t { None, Daily, Weekly, Monthly, Yearly };
enum class EventKind : uint8_t { Plain, Birthday, Anniversary };

struct Event {
  uint32_t id = 0;          // unique, never 0 for a stored event
  uint32_t ver = 0;         // the higher version wins when the panels sync
  uint8_t by = 0;           // sender id of the panel that made this version (breaks a tie)
  Floor floor = Floor::Downstairs;  // the creating panel's floor; never changes (spec 1.4)
  bool deleted = false;     // a tombstone
  bool all_day = false;     // length in days (spec length_unit "days"); else timed, in minutes
  int32_t day = 0;          // first day, local: days since 1970-01-01 (days_from_civil)
  int16_t start_min = 0;    // timed: minutes after midnight
  uint16_t length = 60;     // timed: minutes; all-day: days
  uint32_t changed = 0;     // UTC of this version, 0 if the clock wasn't set
  char title[kEventTitleMax + 1] = "";  // a birthday's or anniversary's: the name
  char notes[kEventNotesMax + 1] = "";
  Repeat repeat = Repeat::None;
  EventKind kind = EventKind::Plain;  // the editor makes these yearly; one changed on its own isn't
  int32_t until = 0;        // repeating: no occurrence starts after this day; 0 for no end date
  uint16_t count = 0;       // repeating: this many occurrences at most; 0 for no limit
  uint8_t nskip = 0;
  uint16_t skip[kMaxSkips] = {};  // occurrences taken out, as days after `day`

  bool repeats() const { return repeat != Repeat::None; }
  int span() const { return all_day ? length : 1; }  // days one occurrence covers
  // The first occurrence's last day, whether it covers d, and whether it's more than a day.
  int32_t last_day() const { return day + span() - 1; }
  bool covers(int32_t d) const { return d >= day && d <= last_day(); }
  bool multi_day() const { return all_day && length > 1; }
  // The last day any occurrence covers; kForever for a series with no end.
  int32_t end_day() const;
  // The first days of the occurrences (not taken out) touching days d0..d1, earliest first.
  // Returns how many it wrote, at most max.
  int starts(int32_t d0, int32_t d1, int32_t *out, int max) const;
  bool starts_on(int32_t d) const;  // an occurrence starts on d
  bool skips(int32_t d) const;      // the occurrence on d was taken out
  // Timed: when it ends, minutes after midnight, at most midnight (spec: past midnight is out of
  // scope for now).
  int end_min() const;
};

// One occurrence of an event, for the views.
struct Occ {
  const Event *e;
  int32_t day;  // its first day
  int32_t last_day() const { return day + e->span() - 1; }
  bool covers(int32_t d) const { return d >= day && d <= last_day(); }
  bool multi_day() const { return e->multi_day(); }
};

// Clamps the length, start and repeat rules to what the calendar can show, and makes the texts
// printable ASCII (the fonts carry nothing else) with no leading or trailing spaces.
void event_normalize(Event &e);
// Sorts occurrences: by first day, all-day before timed (longer first), then start time and title.
void occ_sort(Occ *o, int n);

// Where an archived event is kept: "2026-10" (the month of its first day), or "series" for a
// series (its occurrences run across months).
void archive_name(const Event &e, char *out, size_t n);
void archive_month_name(int32_t day, char *out, size_t n);

class EventStore {
 public:
  // The slots live in memory_needed() bytes from the caller (the panel gives PSRAM).
  static size_t memory_needed();
  bool init(void *mem);
  bool ready() const { return ev_ != nullptr; }
  void *memory() const { return ev_; }  // what init was given, for the owner to free

  int count() const { return n_; }  // slots in use: live events and tombstones
  const Event &at(int i) const { return ev_[i]; }
  const Event *find(uint32_t id) const;
  // Bumped on every change, so the interface knows when to redraw.
  uint32_t rev() const { return rev_; }

  // Occurrences of live events touching days d0..d1, sorted (occ_sort). Returns how many it wrote.
  int range(int32_t d0, int32_t d1, Occ *out, int max) const;

  // Changes made on this panel. new_id is used when e.id is 0 (a new event); it must be nonzero
  // and unused. The floor of an existing event is kept whatever e says. Returns the stored event,
  // or null when the store is full (with nothing it could archive) or the event doesn't exist.
  const Event *put_local(const Event &e, uint32_t new_id, uint8_t me, uint32_t utc);
  const Event *remove_local(uint32_t id, uint8_t me, uint32_t utc);
  const Event *restore_local(uint32_t id, uint8_t me, uint32_t utc);  // undo a deletion
  // One occurrence of a series taken out (it was deleted, or changed into an event of its own),
  // and put back (Undo). Null if there's no such series or occurrence, or no room for another.
  const Event *skip_local(uint32_t id, int32_t day, uint8_t me, uint32_t utc);
  const Event *unskip_local(uint32_t id, int32_t day, uint8_t me, uint32_t utc);
  // An archived event brought back to be changed (it goes back to the archive later). Null if full.
  const Event *adopt(const Event &e);

  // A version from the other panel. Changed: stored (newer than ours, or new to us). Archive: we
  // don't have it here and it's old enough for the archive; the caller merges it into that file.
  enum Merge { kSame, kChanged, kArchive };
  Merge merge(const Event &e, int32_t today, uint32_t utc);

  // Order-independent summary of every slot's (id, version): equal on both panels when in sync.
  uint32_t digest() const;
  // Takes out up to max events old enough for the archive: ended more than kRecentDays ago, or
  // deleted more than kRecentDays ago. With make_room and nothing that old, the store being full,
  // the event that ended longest ago goes instead. Returns how many it copied to out.
  int take_old(int32_t today, uint32_t utc, Event *out, int max, bool make_room);
  bool full() const { return n_ >= kMaxEvents; }

  // The whole store as a blob, for the calendar's file. Returns the length, 0 if cap is short.
  size_t serialize(uint8_t *out, size_t cap) const;
  size_t serialized_size() const;
  bool deserialize(const uint8_t *in, size_t len);  // false (and the store emptied) if malformed

  // A new version number for a local change: above every version seen.
  uint32_t next_ver() const { return clock_ + 1; }

 private:
  int index_of(uint32_t id) const;
  Event *slot_for_new();
  void note(const Event &e);
  Event *ev_ = nullptr;
  int n_ = 0;
  uint32_t clock_ = 0;  // the highest version stored or seen
  uint32_t rev_ = 0;
};

// The calendar's file format, for the store and the archive's month files alike: "TCEV" | version
// u16 | count u16 | clock u32 | events | FNV-1a u32 of everything before it.
size_t events_serialize(const Event *ev, int n, uint32_t clock, uint8_t *out, size_t cap);
size_t events_serialized_size(const Event *ev, int n);
// Reads a file into out (at most max events; duplicates keep the first). Returns how many, or -1
// if it's malformed.
int events_deserialize(const uint8_t *in, size_t len, Event *out, int max, uint32_t &clock);
// Merges e into an archive month's events (n of them, room for max): newer wins, a new one is
// added. Returns true if anything changed.
bool archive_merge(Event *ev, int &n, int max, const Event &e);

// Wire and file encoding of one event (little-endian): id u32 | ver u32 | by u8 | floor u8 |
// flags u8 (1 deleted, 2 all-day, bits 2-4 repeat, bits 5-6 kind) | day i32 | start i16 |
// length u16 | changed u32 | title_len u8 | title | notes_len u8 | notes, then for a repeating
// event: until i32 | count u16 | skips u8 | skip u16 each. At most kEventWireMax bytes. A
// version 1 record (no repeat bits) reads the same.
constexpr size_t kEventWireMax =
    4 + 4 + 1 + 1 + 1 + 4 + 2 + 2 + 4 + 1 + kEventTitleMax + 1 + kEventNotesMax + 4 + 2 + 1 + 2 * kMaxSkips;
size_t event_write(const Event &e, uint8_t *out, size_t cap);
// Reads one event; returns the bytes used, or 0 if malformed (bad floor, flags, lengths).
size_t event_read(const uint8_t *in, size_t len, Event &e);

// What the interface reads the calendar through: the store and, for the past, the archive.
class CalendarSource {
 public:
  virtual int range(int32_t d0, int32_t d1, Occ *out, int max) = 0;  // as EventStore::range
  virtual const Event *find(uint32_t id) = 0;                          // in the store or the archive
  virtual uint32_t rev() const = 0;                                    // changes with any change
  virtual ~CalendarSource() = default;
};

// Local day number for a moment: floor((utc + offset) / 86400).
int32_t local_day(uint32_t utc, int32_t utc_offset);
inline int weekday_of(int32_t day) { return (int)(((day % 7) + 11) % 7); }  // 0 = Sunday (1970-01-01 was a Thursday)

}  // namespace tc
