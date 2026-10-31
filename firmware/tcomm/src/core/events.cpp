#include "events.h"
#include <stdio.h>
#include <string.h>
#include <new>
#include "civil.h"

namespace tc {

namespace {

constexpr uint16_t kFileVersion = 2;  // 2: repeating events (version 1 files read the same)
constexpr int kMaxCount = 999;         // the most occurrences a series can be limited to

bool newer(const Event &a, const Event &b) { return a.ver > b.ver || (a.ver == b.ver && a.by > b.by); }

void clean_text(char *s, size_t max) {
  size_t n = strnlen(s, max);
  s[n] = 0;
  for (size_t i = 0; i < n; i++)
    if ((uint8_t)s[i] < 0x20 || (uint8_t)s[i] > 0x7E) s[i] = '?';
  size_t a = 0;
  while (a < n && s[a] == ' ') a++;
  while (n > a && s[n - 1] == ' ') n--;
  memmove(s, s + a, n - a);
  s[n - a] = 0;
}

uint32_t mix(uint32_t h) {  // murmur3's finalizer
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

uint32_t fnv(const uint8_t *p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

// ---- Dates ----

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int days_in(int y, int m) {
  static const int k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leap(y) ? 29 : k[m - 1];
}
struct Ymd {
  int y, m, d;
};
Ymd ymd(int32_t day) {
  Civil c = civil_from_seconds((int64_t)day * 86400);
  return Ymd{c.year, c.month, c.day};
}
int32_t dnum(int y, int m, int d) { return (int32_t)days_from_civil(y, m, d); }
int64_t month_index(const Ymd &a) { return (int64_t)a.y * 12 + (a.m - 1); }

// The k-th place of a series' rule (k from 0). False when that month has no such day (a monthly
// series on the 31st); st is then the first day of that month, so a caller can tell it's past.
bool slot(const Event &e, int64_t k, int32_t &st) {
  switch (e.repeat) {
    case Repeat::Daily: st = (int32_t)(e.day + k); return true;
    case Repeat::Weekly: st = (int32_t)(e.day + 7 * k); return true;
    case Repeat::Monthly: {
      Ymd a = ymd(e.day);
      int64_t mi = month_index(a) + k;
      int y = (int)(mi / 12), m = (int)(mi % 12) + 1;
      if (a.d > days_in(y, m)) {
        st = dnum(y, m, 1);
        return false;
      }
      st = dnum(y, m, a.d);
      return true;
    }
    case Repeat::Yearly: {
      Ymd a = ymd(e.day);
      int y = a.y + (int)k, d = a.d;
      if (a.m == 2 && d == 29 && !leap(y)) d = 28;  // a February 29 birthday: the 28th other years
      st = dnum(y, a.m, d);
      return true;
    }
    default: st = e.day; return k == 0;
  }
}

// A place to start looking for occurrences from day lo: at or before the first one there.
int64_t first_k(const Event &e, int32_t lo) {
  if (lo <= e.day) return 0;
  int64_t k = 0;
  switch (e.repeat) {
    case Repeat::Daily: k = lo - e.day; break;
    case Repeat::Weekly: k = (lo - e.day) / 7; break;
    case Repeat::Monthly: k = month_index(ymd(lo)) - month_index(ymd(e.day)) - 1; break;
    case Repeat::Yearly: k = ymd(lo).y - ymd(e.day).y - 1; break;
    default: break;
  }
  return k < 0 ? 0 : k;
}

// Whether places of the rule can be missing (so an occurrence's number isn't its place).
bool gappy(const Event &e) { return e.repeat == Repeat::Monthly && ymd(e.day).d > 28; }

// The n-th occurrence's start (n from 0), counting only places that exist.
int32_t nth_start(const Event &e, int n) {
  int32_t st = e.day;
  if (!gappy(e)) {
    slot(e, n, st);
    return st;
  }
  for (int64_t k = 0, seen = 0; k < 100000; k++)
    if (slot(e, k, st) && seen++ == n) return st;
  return st;
}

// Earliest first: first day, then all-day before timed, then start time, then title.
bool before(const Occ &a, const Occ &b) {
  if (a.day != b.day) return a.day < b.day;
  const Event &x = *a.e, &y = *b.e;
  if (x.all_day != y.all_day) return x.all_day;
  if (x.all_day && x.length != y.length) return x.length > y.length;  // the longer bubble on top
  if (!x.all_day && x.start_min != y.start_min) return x.start_min < y.start_min;
  int c = strcmp(x.title, y.title);
  return c != 0 ? c < 0 : x.id < y.id;
}

bool old_enough(const Event &e, int32_t today, uint32_t utc) {
  if (today < 0) return false;
  if (e.deleted)  // a deletion: once the other panel has surely heard of it
    return e.changed ? (utc > e.changed && utc - e.changed > (uint32_t)kRecentDays * 86400u)
                     : e.end_day() < today - kRecentDays;
  return e.end_day() < today - kRecentDays;
}

struct W {
  uint8_t *p;
  size_t cap, n = 0;
  bool ok = true;
  void u8(uint8_t v) {
    if (n + 1 > cap) { ok = false; return; }
    p[n++] = v;
  }
  void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
  void str(const char *s, size_t max) {
    size_t len = strnlen(s, max);
    u8((uint8_t)len);
    if (n + len > cap) { ok = false; return; }
    memcpy(p + n, s, len);
    n += len;
  }
};

struct R {
  const uint8_t *p;
  size_t len, n = 0;
  bool ok = true;
  uint8_t u8() {
    if (n + 1 > len) { ok = false; return 0; }
    return p[n++];
  }
  uint16_t u16() { uint16_t lo = u8(); return (uint16_t)(lo | (u8() << 8)); }
  uint32_t u32() { uint32_t lo = u16(); return lo | ((uint32_t)u16() << 16); }
  bool str(char *out, size_t max) {
    size_t l = u8();
    if (!ok || l > max || n + l > len) { ok = false; return false; }
    memcpy(out, p + n, l);
    out[l] = 0;
    n += l;
    return true;
  }
};

}  // namespace

// ---- Event ----

int Event::end_min() const {
  int e = start_min + length;
  return e > kMaxEventMinutes ? kMaxEventMinutes : e;
}

bool Event::skips(int32_t d) const {
  int32_t off = d - day;
  for (int i = 0; i < nskip && i < kMaxSkips; i++)
    if (skip[i] == off) return true;
  return false;
}

int32_t Event::end_day() const {
  if (!repeats()) return last_day();
  if (!count && !until) return kForever;
  int32_t last = count ? nth_start(*this, count - 1) : kForever;
  if (until && until < last) {
    // The last place on or before until.
    int32_t st = day, best = day;
    int64_t k0 = first_k(*this, until);
    for (int64_t k = k0; k < k0 + 400; k++) {
      bool ok = slot(*this, k, st);
      if (st > until) break;
      if (ok) best = st;
    }
    last = best;
  }
  return last + span() - 1;
}

int Event::starts(int32_t d0, int32_t d1, int32_t *out, int max) const {
  const int32_t lo = d0 - span() + 1, hi = d1;
  int n = 0;
  if (!repeats()) {
    if (day >= lo && day <= hi && n < max) out[n++] = day;
    return n;
  }
  if (hi < day || (until && lo > until)) return 0;
  const bool exact = count && gappy(*this);  // the occurrence's number has to be counted from the start
  int64_t k = exact ? 0 : first_k(*this, lo), number = k;
  for (int guard = 0; guard < 100000 && n < max; guard++, k++) {
    int32_t st;
    bool ok = slot(*this, k, st);
    if (st > hi || (until && st > until)) break;
    if (!ok) continue;
    if (count && number >= count) break;
    number++;
    if (st >= lo && !skips(st)) out[n++] = st;
  }
  return n;
}

bool Event::starts_on(int32_t d) const {
  int32_t s[8];
  int n = starts(d, d, s, 8);
  for (int i = 0; i < n; i++)
    if (s[i] == d) return true;
  return false;
}

void event_normalize(Event &e) {
  clean_text(e.title, kEventTitleMax);
  clean_text(e.notes, kEventNotesMax);
  if (e.all_day) {
    e.start_min = 0;
    if (e.length < 1) e.length = 1;
    if (e.length > kMaxEventDays) e.length = kMaxEventDays;
  } else {
    if (e.start_min < 0) e.start_min = 0;
    if (e.start_min > kMaxEventMinutes - 1) e.start_min = kMaxEventMinutes - 1;
    if (e.length < 1) e.length = 1;
    if (e.length > kMaxEventMinutes) e.length = kMaxEventMinutes;
  }
  if ((uint8_t)e.repeat > (uint8_t)Repeat::Yearly) e.repeat = Repeat::None;
  if ((uint8_t)e.kind > (uint8_t)EventKind::Anniversary) e.kind = EventKind::Plain;
  if (!e.repeats()) {
    e.until = 0;
    e.count = 0;
    e.nskip = 0;
  } else {
    if (e.until && e.until < e.day) e.until = e.day;
    if (e.count > kMaxCount) e.count = kMaxCount;
    if (e.nskip > kMaxSkips) e.nskip = kMaxSkips;
  }
  for (int i = e.nskip; i < kMaxSkips; i++) e.skip[i] = 0;
}

void occ_sort(Occ *o, int n) {
  for (int i = 1; i < n; i++) {  // insertion sort: n is small
    Occ x = o[i];
    int j = i;
    for (; j > 0 && before(x, o[j - 1]); j--) o[j] = o[j - 1];
    o[j] = x;
  }
}

void archive_month_name(int32_t day, char *out, size_t n) {
  Ymd a = ymd(day);
  snprintf(out, n, "%04d-%02d", a.y, a.m);
}

void archive_name(const Event &e, char *out, size_t n) {
  if (e.repeats()) snprintf(out, n, "series");
  else archive_month_name(e.day, out, n);
}

int32_t local_day(uint32_t utc, int32_t utc_offset) {
  int64_t l = (int64_t)utc + utc_offset;
  return (int32_t)(l >= 0 ? l / 86400 : -((-l + 86399) / 86400));
}

// ---- Encoding ----

size_t event_write(const Event &e, uint8_t *out, size_t cap) {
  W w{out, cap};
  w.u32(e.id);
  w.u32(e.ver);
  w.u8(e.by);
  w.u8((uint8_t)e.floor);
  w.u8((uint8_t)((e.deleted ? 1 : 0) | (e.all_day ? 2 : 0) | ((uint8_t)e.repeat << 2) | ((uint8_t)e.kind << 5)));
  w.u32((uint32_t)e.day);
  w.u16((uint16_t)e.start_min);
  w.u16(e.length);
  w.u32(e.changed);
  w.str(e.title, kEventTitleMax);
  w.str(e.notes, kEventNotesMax);
  if (e.repeats()) {
    w.u32((uint32_t)e.until);
    w.u16(e.count);
    int ns = e.nskip > kMaxSkips ? kMaxSkips : e.nskip;
    w.u8((uint8_t)ns);
    for (int i = 0; i < ns; i++) w.u16(e.skip[i]);
  }
  return w.ok ? w.n : 0;
}

size_t event_read(const uint8_t *in, size_t len, Event &e) {
  R r{in, len};
  e = Event();
  e.id = r.u32();
  e.ver = r.u32();
  e.by = r.u8();
  uint8_t floor = r.u8(), flags = r.u8();
  e.day = (int32_t)r.u32();
  e.start_min = (int16_t)r.u16();
  e.length = r.u16();
  e.changed = r.u32();
  uint8_t rep = (flags >> 2) & 7, kind = (flags >> 5) & 3;
  if (!r.ok || floor > 1 || (flags & 0x80) || rep > (uint8_t)Repeat::Yearly || kind > (uint8_t)EventKind::Anniversary ||
      e.id == 0)
    return 0;
  e.floor = (Floor)floor;
  e.deleted = flags & 1;
  e.all_day = flags & 2;
  e.repeat = (Repeat)rep;
  e.kind = (EventKind)kind;
  if (!r.str(e.title, kEventTitleMax) || !r.str(e.notes, kEventNotesMax)) return 0;
  if (e.repeats()) {
    e.until = (int32_t)r.u32();
    e.count = r.u16();
    e.nskip = r.u8();
    if (!r.ok || e.nskip > kMaxSkips) return 0;
    for (int i = 0; i < e.nskip; i++) e.skip[i] = r.u16();
    if (!r.ok) return 0;
  }
  event_normalize(e);
  return r.n;
}

size_t events_serialized_size(const Event *ev, int n) {
  uint8_t buf[kEventWireMax];
  size_t total = 12 + 4;
  for (int i = 0; i < n; i++) total += event_write(ev[i], buf, sizeof(buf));
  return total;
}

size_t events_serialize(const Event *ev, int n, uint32_t clock, uint8_t *out, size_t cap) {
  if (cap < 16) return 0;
  W w{out, cap};
  w.u8('T'); w.u8('C'); w.u8('E'); w.u8('V');
  w.u16(kFileVersion);
  w.u16((uint16_t)n);
  w.u32(clock);
  for (int i = 0; i < n && w.ok; i++) {
    size_t k = event_write(ev[i], out + w.n, cap - w.n);
    if (!k) return 0;
    w.n += k;
  }
  if (!w.ok) return 0;
  w.u32(fnv(out, w.n));
  return w.ok ? w.n : 0;
}

int events_deserialize(const uint8_t *in, size_t len, Event *out, int max, uint32_t &clock) {
  if (!in || len < 16 || in[0] != 'T' || in[1] != 'C' || in[2] != 'E' || in[3] != 'V') return -1;
  if (fnv(in, len - 4) != (uint32_t)(in[len - 4] | in[len - 3] << 8 | in[len - 2] << 16 | (uint32_t)in[len - 1] << 24))
    return -1;
  R r{in, len - 4};
  r.n = 4;
  uint16_t version = r.u16();
  if (version < 1 || version > kFileVersion) return -1;
  int count = r.u16();
  clock = r.u32();
  if (!r.ok || count > max) return -1;
  int n = 0;
  for (int i = 0; i < count; i++) {
    Event e;
    size_t used = event_read(in + r.n, r.len - r.n, e);
    if (!used) return -1;
    r.n += used;
    bool dup = false;
    for (int j = 0; j < n && !dup; j++) dup = out[j].id == e.id;
    if (dup) continue;  // keep the first
    out[n++] = e;
    if (e.ver > clock) clock = e.ver;
  }
  return r.n == r.len ? n : -1;
}

bool archive_merge(Event *ev, int &n, int max, const Event &e) {
  for (int i = 0; i < n; i++) {
    if (ev[i].id != e.id) continue;
    if (!newer(e, ev[i])) return false;
    Floor keep = ev[i].floor;
    ev[i] = e;
    ev[i].floor = keep;
    return true;
  }
  if (n >= max) return false;
  ev[n++] = e;
  return true;
}

// ---- Store ----

size_t EventStore::memory_needed() { return sizeof(Event) * kMaxEvents; }

bool EventStore::init(void *mem) {
  if (ev_) return true;
  ev_ = (Event *)mem;
  if (!ev_) return false;
  for (int i = 0; i < kMaxEvents; i++) new (&ev_[i]) Event();
  n_ = 0;
  return true;
}

int EventStore::index_of(uint32_t id) const {
  for (int i = 0; i < n_; i++)
    if (ev_[i].id == id) return i;
  return -1;
}

const Event *EventStore::find(uint32_t id) const {
  int i = id ? index_of(id) : -1;
  return i >= 0 ? &ev_[i] : nullptr;
}

void EventStore::note(const Event &e) {
  if (e.ver > clock_) clock_ = e.ver;
  rev_++;
}

int EventStore::range(int32_t d0, int32_t d1, Occ *out, int max) const {
  int k = 0;
  int32_t st[64];
  for (int i = 0; i < n_ && k < max; i++) {
    const Event &e = ev_[i];
    if (e.deleted) continue;
    int m = e.starts(d0, d1, st, 64);
    for (int j = 0; j < m && k < max; j++) out[k++] = Occ{&e, st[j]};
  }
  occ_sort(out, k);
  return k;
}

Event *EventStore::slot_for_new() {
  if (!ev_ || n_ >= kMaxEvents) return nullptr;
  return &ev_[n_++];
}

const Event *EventStore::put_local(const Event &in, uint32_t new_id, uint8_t me, uint32_t utc) {
  if (!ev_) return nullptr;
  Event e = in;
  event_normalize(e);
  Event *slot;
  if (e.id == 0) {
    if (new_id == 0 || index_of(new_id) >= 0) return nullptr;
    slot = slot_for_new();
    if (!slot) return nullptr;
    e.id = new_id;
  } else {
    int i = index_of(e.id);
    if (i < 0 || ev_[i].deleted) return nullptr;
    slot = &ev_[i];
    e.floor = slot->floor;  // spec 1.4: the floor of a saved event never changes
  }
  e.deleted = false;
  e.ver = next_ver();
  e.by = me;
  e.changed = utc;
  *slot = e;
  note(*slot);
  return slot;
}

const Event *EventStore::remove_local(uint32_t id, uint8_t me, uint32_t utc) {
  int i = id ? index_of(id) : -1;
  if (i < 0 || ev_[i].deleted) return nullptr;
  Event &e = ev_[i];
  e.deleted = true;  // the text stays, so Undo can bring it back
  e.ver = next_ver();
  e.by = me;
  e.changed = utc;
  note(e);
  return &e;
}

const Event *EventStore::restore_local(uint32_t id, uint8_t me, uint32_t utc) {
  int i = id ? index_of(id) : -1;
  if (i < 0 || !ev_[i].deleted) return nullptr;
  Event &e = ev_[i];
  e.deleted = false;
  e.ver = next_ver();
  e.by = me;
  e.changed = utc;
  note(e);
  return &e;
}

const Event *EventStore::skip_local(uint32_t id, int32_t day, uint8_t me, uint32_t utc) {
  int i = id ? index_of(id) : -1;
  if (i < 0) return nullptr;
  Event &e = ev_[i];
  if (e.deleted || !e.repeats() || !e.starts_on(day) || e.nskip >= kMaxSkips || day - e.day > 0xFFFF) return nullptr;
  e.skip[e.nskip++] = (uint16_t)(day - e.day);
  e.ver = next_ver();
  e.by = me;
  e.changed = utc;
  note(e);
  return &e;
}

const Event *EventStore::unskip_local(uint32_t id, int32_t day, uint8_t me, uint32_t utc) {
  int i = id ? index_of(id) : -1;
  if (i < 0) return nullptr;
  Event &e = ev_[i];
  int32_t off = day - e.day;
  int k = 0;
  while (k < e.nskip && e.skip[k] != off) k++;
  if (e.deleted || k >= e.nskip) return nullptr;
  e.skip[k] = e.skip[--e.nskip];
  e.skip[e.nskip] = 0;
  e.ver = next_ver();
  e.by = me;
  e.changed = utc;
  note(e);
  return &e;
}

const Event *EventStore::adopt(const Event &in) {
  if (!ev_ || !in.id) return nullptr;
  int i = index_of(in.id);
  if (i >= 0) return &ev_[i];
  Event *slot = slot_for_new();
  if (!slot) return nullptr;
  *slot = in;
  event_normalize(*slot);
  note(*slot);
  return slot;
}

EventStore::Merge EventStore::merge(const Event &in, int32_t today, uint32_t utc) {
  if (!ev_ || in.id == 0) return kSame;
  if (in.ver > clock_) clock_ = in.ver;  // a later local change must outrank everything seen
  int i = index_of(in.id);
  if (i >= 0) {
    if (!newer(in, ev_[i])) return kSame;
    Floor keep = ev_[i].floor;
    ev_[i] = in;
    ev_[i].floor = keep;
    event_normalize(ev_[i]);
    note(ev_[i]);
    return kChanged;
  }
  if (old_enough(in, today, utc)) return kArchive;
  Event *slot = slot_for_new();
  if (!slot) return kSame;  // full: the controller makes room first, so this is rare
  *slot = in;
  event_normalize(*slot);
  note(*slot);
  return kChanged;
}

uint32_t EventStore::digest() const {
  uint32_t sum = 0;
  for (int i = 0; i < n_; i++) sum += mix(ev_[i].id ^ mix(ev_[i].ver * 2u + ev_[i].by));
  return sum;
}

int EventStore::take_old(int32_t today, uint32_t utc, Event *out, int max, bool make_room) {
  if (!ev_ || today < 0 || max <= 0) return 0;
  int k = 0, w = 0;
  for (int i = 0; i < n_; i++) {
    if (k < max && old_enough(ev_[i], today, utc)) {
      out[k++] = ev_[i];
      continue;
    }
    if (w != i) ev_[w] = ev_[i];
    w++;
  }
  n_ = w;
  if (!k && make_room && full()) {
    // Nothing old enough, and no room: the event (or deletion) that ended longest ago, if any has.
    int best = -1;
    for (int i = 0; i < n_; i++) {
      int32_t end = ev_[i].end_day();
      if (end < today && (best < 0 || end < ev_[best].end_day())) best = i;
    }
    if (best >= 0) {
      out[k++] = ev_[best];
      for (int i = best; i + 1 < n_; i++) ev_[i] = ev_[i + 1];
      n_--;
    }
  }
  if (k) rev_++;
  return k;
}

size_t EventStore::serialize(uint8_t *out, size_t cap) const { return events_serialize(ev_, n_, clock_, out, cap); }

size_t EventStore::serialized_size() const { return events_serialized_size(ev_, n_); }

bool EventStore::deserialize(const uint8_t *in, size_t len) {
  if (!ev_) return false;
  n_ = 0;
  rev_++;
  uint32_t clock = 0;
  int n = events_deserialize(in, len, ev_, kMaxEvents, clock);
  if (n < 0) {
    for (int i = 0; i < kMaxEvents; i++) ev_[i] = Event();
    return false;
  }
  n_ = n;
  clock_ = clock;
  return true;
}

}  // namespace tc
