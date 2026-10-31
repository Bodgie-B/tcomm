#include "controller.h"
#include "../core/brightness.h"
#include "../core/civil.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tc {

static const char kDeg[] = "\xC2\xB0";  // UTF-8 degree sign

const char *status_word(Status s) {
  static const char *const w[] = {"OPEN", "BUSY", "PRIVATE", "SLEEPING", "AWAY"};
  return w[(int)s];
}
const char *status_title(Status s) {
  static const char *const w[] = {"Open", "Busy", "Private", "Sleeping", "Away"};
  return w[(int)s];
}
const char *floor_word(Floor f) { return f == Floor::Upstairs ? "UPSTAIRS" : "DOWNSTAIRS"; }
const char *floor_lower(Floor f) { return f == Floor::Upstairs ? "upstairs" : "downstairs"; }
const char *dog_name(Dog d) {
  return d == Dog::Lucy ? "LUCY" : d == Dog::Copper ? "COPPER" : d == Dog::Both ? "BOTH DOGS" : "";
}

static int round_tenths(int v) { return v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10); }

// Disc positions drawn on the boards, used when a phase is forced for review.
static const float kBoardP[6] = {0.35f, 0.03f, 0.22f, 0.48f, 0.88f, 0.985f};
static constexpr uint32_t kRingAlertMs = 120000;  // Mike, 2026-10-26: the red bell, 2 min at most
static constexpr uint32_t kClockFloor = 1767225600u;  // 2026-01-01: an earlier time isn't a real one

// ---------------------------------------------------------------------------------------------
// Setup and the main tick

void Controller::begin(const Settings &s, const OwnState &own, const DogState &dog, const MailState &mail) {
  settings_ = s;
  status_.restore(own);
  dog_.restore(dog);
  mail_.restore(mail);
  retry_.set_seq((uint16_t)p_.random32());
  next_knock_ = (uint16_t)(p_.random32() | 1);
  WallTime w = p_.wall();
  clock_was_valid_ = w.valid;
  if (w.valid) status_.clock_became_valid(w.utc, p_.mono_ms());
  if (status_.tick(w)) p_.save_own(status_.state());  // a timer that ran out while off
  want_peer_state_ = true;
  send_state(true);  // HANDOFF boot exchange: the other panel answers with STATE, DOG and READING

  // The calendar: from its file, then compared with the other panel's.
  if (!events_.ready()) events_.init(p_.alloc_big(EventStore::memory_needed()));
  if (!events_.ready()) {
    p_.log("calendar: no memory for it");
  } else {
    size_t len = 0;
    uint8_t *buf = p_.read_file("calendar", len);
    if (buf && !events_.deserialize(buf, len)) p_.log("calendar: its file was unreadable, starting empty");
    p_.free_big(buf);
  }
  send_sum(true);
  build_view();
}

void Controller::tick() {
  uint32_t now = p_.mono_ms();
  WallTime w = p_.wall();
  if (w.valid && !clock_was_valid_ && status_.clock_became_valid(w.utc, now)) {
    p_.save_own(status_.state());
    send_state(false);  // the other panel was showing "since --:--"
  }
  clock_was_valid_ = w.valid;

  // Retire anything that has run its course, so nothing can come back when the millisecond
  // counter wraps after about 24.8 days.
  if (touched_ && ms_diff(now, last_touch_) >= (int32_t)settings_.touch_wake_ms) touched_ = false;
  if (mail_wake_ && ms_diff(now, mail_wake_at_) >= (int32_t)settings_.touch_wake_ms) mail_wake_ = false;
  if (toast_[0] && ms_reached(now, toast_until_)) toast_[0] = 0;
  if (outside_.have && !outside_fresh(now)) outside_.have = false;
  if (last_missed_ && ms_diff(now, missed_mono_) >= 3600000) bell_in_.clear_missed();  // PROPOSED: an hour
  if (ring_alert_ && ms_diff(now, ring_alert_at_) >= (int32_t)kRingAlertMs) ring_alert_ = false;

  Status before = status_.state().status;
  if (status_.tick(w)) {
    snprintf(toast_, sizeof(toast_), "Timer ended. Now %s.", status_title(status_.state().status));
    toast_until_ = now + 6000;
    own_changed(before);
  }
  bell_out_.tick(now);
  bell_in_.tick(now);

  while (const RetryQueue::Frame *f = retry_.due(now)) {
    p_.radio_send(sender_id(other_floor(settings_.floor)), f->data, f->len);
    retry_.sent(f->seq, now);
  }
  RetryQueue::Result r;
  while (retry_.expired(now, r)) {
    // HANDOFF: no ACK after every retry marks the other side as No signal.
    peer_.no_signal = true;
    if (r.type == MsgType::Knock) bell_out_.on_failed(r.knock_id, now);
    char line[64];
    snprintf(line, sizeof(line), "%s seq %u: no ACK after %d tries, other panel marked No signal", msg_name(r.type),
             (unsigned)r.seq, RetryQueue::kTries);
    p_.log(line);
  }

  tick_events(now, w);
  build_view();
  if (!backlight_set_ || view_.backlight != last_backlight_) {
    p_.set_backlight(view_.backlight);
    last_backlight_ = view_.backlight;
    backlight_set_ = true;
  }
}

// ---------------------------------------------------------------------------------------------
// Radio

Controller::~Controller() {
  p_.free_big(events_.memory());
  for (ArchFile &a : arch_) p_.free_big(a.ev);
}

bool Controller::send(Msg m) {
  if (retry_.push(m, sender_id(settings_.floor), p_.mono_ms())) return true;
  char line[48];
  snprintf(line, sizeof(line), "send queue full, dropped %s", msg_name(m.type));
  p_.log(line);
  return false;
}

void Controller::send_state(bool please_reply) {
  WallTime w = p_.wall();
  const OwnState &s = status_.state();
  Msg m;
  m.type = MsgType::State;
  m.u.state.status = (uint8_t)s.status;
  m.u.state.flags = (uint8_t)(((please_reply || want_peer_state_) ? kStatePleaseReply : 0) | (w.valid ? kStateClockValid : 0));
  m.u.state.since = s.since;
  m.u.state.until = s.until;
  m.u.state.fallback = (uint8_t)s.fallback;
  m.u.state.clock = w.valid ? w.utc : 0;
  m.u.state.clock_quality = w.valid ? w.quality : (uint8_t)kClockNone;
  send(m);
}

void Controller::send_dog() {
  Msg m;
  m.type = MsgType::DogMsg;
  for (int i = 0; i < 2; i++) {
    const DogOne &d = dog_.state().dog[i];
    m.u.dog.out[i] = d.out ? 1 : 0;
    m.u.dog.since[i] = d.since;
    m.u.dog.ver[i] = d.ver;
  }
  send(m);
}

void Controller::send_mail() {
  Msg m;
  m.type = MsgType::Mail;
  const MailState &s = mail_.state();
  m.u.mail.waiting = s.waiting ? 1 : 0;
  m.u.mail.since = s.since;
  m.u.mail.ver = s.ver;
  send(m);
}

void Controller::send_reading() {
  Msg m;
  m.type = MsgType::Reading;
  m.u.reading.flags = reading_valid_ ? kReadingValid : 0;
  m.u.reading.temp_tenths_f = reading_temp_;
  m.u.reading.humidity_tenths = reading_hum_;
  send(m);
}

bool Controller::outside_fresh(uint32_t now) const {
  return outside_.have && ms_diff(now, outside_.got_mono) >= 0 &&
         ms_diff(now, outside_.got_mono) < (int32_t)(settings_.outside_stale_min * 60000u);
}

void Controller::send_outside() {
  if (!outside_fresh(p_.mono_ms())) return;  // never pass an old reading on as new
  Msg m;
  m.type = MsgType::Outside;
  m.u.outside.flags = kOutsideValid | (outside_.flags & (kOutsideDangerHeat | kOutsideDangerCold));
  m.u.outside.temp_tenths_f = outside_.temp;
  m.u.outside.humidity = outside_.hum;
  m.u.outside.forecast = outside_.code;
  m.u.outside.time = outside_.time;
  strncpy(m.u.outside.phrase, outside_.phrase, kMaxPhrase);
  send(m);
}

void Controller::send_event(const Event &e) {
  Msg m;
  m.type = MsgType::EventMsg;
  m.u.event.len = (uint8_t)event_write(e, m.u.event.data, sizeof(m.u.event.data));
  if (m.u.event.len && !send(m)) ev_resync_ = true;  // not a timer: caused by the dropped change
}

void Controller::send_sum(bool please_reply) {
  Msg m;
  m.type = MsgType::EventSum;
  m.u.sum.count = (uint16_t)events_.count();
  m.u.sum.digest = events_.digest();
  m.u.sum.flags = please_reply ? kSumPleaseReply : 0;
  send(m);
}

void Controller::events_changed() {
  if (!ev_dirty_) ev_save_at_ = p_.mono_ms() + 2000;  // a burst of changes (a sync) is one write
  ev_dirty_ = true;
}

int32_t Controller::today() {
  WallTime w = p_.wall();
  return w.valid ? local_day(w.utc, offset_now()) : -1;
}

void Controller::tick_events(uint32_t now, const WallTime &w) {
  if (!events_.ready()) return;
  // Syncing: the other panel's calendar differed, so send ours, a couple of events at a time and
  // leaving the queue room for status and doorbell messages. Stopped when the other panel is lost
  // (its next message starts a new comparison).
  if (ev_stream_ >= 0) {
    if (peer_.no_signal) ev_stream_ = -1;
    while (ev_stream_ >= 0 && ev_stream_ < events_.count() && retry_.pending(MsgType::EventMsg) < 2 &&
           retry_.pending() < 6)
      send_event(events_.at(ev_stream_++));
    if (ev_stream_ >= events_.count()) ev_stream_ = -1;
  }
  if (ev_resync_ && !peer_.no_signal && retry_.pending() < 4) {
    ev_resync_ = false;
    send_sum(true);
  }
  // Once a day (and at start-up): what's long past goes to the archive.
  int32_t d = w.valid ? local_day(w.utc, offset_now()) : -1;
  if (d >= 0 && d != ev_archived_day_) {
    ev_archived_day_ = d;
    archive_old(false);
  }
  if (ev_dirty_ && ms_reached(now, ev_save_at_)) {
    ev_dirty_ = false;
    size_t size = events_.serialized_size();
    uint8_t *buf = (uint8_t *)p_.alloc_big(size);
    size_t n = buf ? events_.serialize(buf, size) : 0;
    if (n) p_.write_file("calendar", buf, n);
    else {
      p_.free_big(buf);
      p_.log("calendar: no memory to save it");
    }
  }
}

// ---------------------------------------------------------------------------------------------
// The archive

Controller::ArchFile *Controller::archive_file(const char *name) {
  ArchFile *victim = &arch_[0];
  for (ArchFile &a : arch_) {
    if (a.used && !strcmp(a.name, name)) {
      a.used = ++arch_tick_;
      return &a;
    }
    if (a.used < victim->used) victim = &a;
  }
  p_.free_big(victim->ev);
  *victim = ArchFile();
  snprintf(victim->name, sizeof(victim->name), "%s", name);
  victim->used = ++arch_tick_;
  size_t len = 0;
  uint8_t *buf = p_.read_file(name, len);
  if (buf && len >= 16) {
    int have = buf[6] | buf[7] << 8;
    victim->ev = (Event *)p_.alloc_big(sizeof(Event) * (have ? have : 1));
    if (victim->ev) {
      for (int i = 0; i < have; i++) new (&victim->ev[i]) Event();
      uint32_t clock = 0;
      victim->n = events_deserialize(buf, len, victim->ev, have, clock);
      if (victim->n < 0) {
        victim->n = 0;
        char line[64];
        snprintf(line, sizeof(line), "calendar: the archive's %s is unreadable", name);
        p_.log(line);
      }
    }
  }
  p_.free_big(buf);
  return victim;
}

void Controller::archive_forget(const char *name) {
  for (ArchFile &a : arch_)
    if (a.used && !strcmp(a.name, name)) {
      p_.free_big(a.ev);
      a = ArchFile();
    }
  arch_rev_++;
}

bool Controller::archive_put(const char *name, const Event *add, int n) {
  size_t len = 0;
  uint8_t *buf = p_.read_file(name, len);
  int have = buf && len >= 16 ? (buf[6] | buf[7] << 8) : 0, cap = have + n;
  Event *all = (Event *)p_.alloc_big(sizeof(Event) * cap);
  int m = 0;
  bool ok = all != nullptr;
  if (ok) {
    for (int i = 0; i < cap; i++) new (&all[i]) Event();
    uint32_t clock = 0;
    if (buf) m = events_deserialize(buf, len, all, cap, clock);
    if (m < 0) {
      // Never written over: the events stay in the store (and it says so in the log daily).
      char line[64];
      snprintf(line, sizeof(line), "calendar: the archive's %s is unreadable, not changed", name);
      p_.log(line);
      ok = false;
    }
  }
  p_.free_big(buf);
  if (ok) {
    bool changed = false;
    for (int i = 0; i < n; i++) changed |= archive_merge(all, m, cap, add[i]);
    if (changed) {
      size_t size = events_serialized_size(all, m);
      uint8_t *out = (uint8_t *)p_.alloc_big(size);
      size_t k = out ? events_serialize(all, m, 0, out, size) : 0;
      if (k) p_.write_file(name, out, k);
      else {
        p_.free_big(out);
        ok = false;
      }
    }
    archive_forget(name);
  }
  p_.free_big(all);
  return ok;
}

void Controller::archive_old(bool make_room) {
  int32_t d = today();
  WallTime w = p_.wall();
  if (d < 0 || !events_.ready()) return;
  Event *old = (Event *)p_.alloc_big(sizeof(Event) * 32);
  if (!old) return;
  for (int i = 0; i < 32; i++) new (&old[i]) Event();
  for (int batch = 0; batch < 8; batch++) {
    int k = events_.take_old(d, w.utc, old, 16, make_room && batch == 0);
    if (!k) break;
    // Each file once: the events for it gathered after the first 16.
    bool done[16] = {};
    for (int i = 0; i < k; i++) {
      if (done[i]) continue;
      char name[8], other[8];
      archive_name(old[i], name, sizeof(name));
      int g = 0;
      for (int j = i; j < k; j++) {
        archive_name(old[j], other, sizeof(other));
        if (!done[j] && !strcmp(name, other)) {
          old[16 + g++] = old[j];
          done[j] = true;
        }
      }
      if (!archive_put(name, old + 16, g))
        for (int j = 0; j < g; j++) events_.adopt(old[16 + j]);  // try again tomorrow
    }
    events_changed();
    if (k < 16) break;
  }
  p_.free_big(old);
}

const Event *Controller::make_active(uint32_t id) {
  if (const Event *e = events_.find(id)) return e;
  const Event *a = nullptr;
  for (ArchFile &f : arch_)
    for (int i = 0; i < f.n && !a; i++)
      if (f.ev[i].id == id) a = &f.ev[i];
  if (!a) return nullptr;
  Event copy = *a;
  if (events_.full()) archive_old(true);
  return events_.adopt(copy);
}

int Controller::range(int32_t d0, int32_t d1, Occ *out, int max) {
  int k = events_.range(d0, d1, out, max);
  int32_t t = today();
  if (t < 0 || d0 - kMaxEventDays > t) return k;  // nothing archived ends after today
  // The archive's months from the one a long event could start in, to the last one that's past,
  // then the series. An event in the store as well (brought back to change it) shows once.
  int32_t last = d1 < t ? d1 : t;
  char name[8], prev[8] = "";
  int32_t st[64];
  for (int32_t day = d0 - kMaxEventDays;; day += 28) {
    if (day > last) day = last;
    archive_month_name(day, name, sizeof(name));
    if (strcmp(name, prev)) {
      snprintf(prev, sizeof(prev), "%s", name);
      ArchFile *f = archive_file(name);
      for (int i = 0; i < f->n && k < max; i++) {
        const Event &e = f->ev[i];
        if (e.deleted || events_.find(e.id)) continue;
        int m = e.starts(d0, d1, st, 64);
        for (int j = 0; j < m && k < max; j++) out[k++] = Occ{&e, st[j]};
      }
    }
    if (day == last) break;
  }
  ArchFile *f = archive_file("series");
  for (int i = 0; i < f->n && k < max; i++) {
    const Event &e = f->ev[i];
    if (e.deleted || events_.find(e.id)) continue;
    int m = e.starts(d0, d1, st, 64);
    for (int j = 0; j < m && k < max; j++) out[k++] = Occ{&e, st[j]};
  }
  occ_sort(out, k);
  return k;
}

const Event *Controller::find(uint32_t id) {
  if (const Event *e = events_.find(id)) return e;
  for (ArchFile &f : arch_)
    for (int i = 0; i < f.n; i++)
      if (f.ev[i].id == id) return &f.ev[i];
  return nullptr;
}

uint32_t Controller::save_event(const Event &in) {
  WallTime w = p_.wall();
  Event e = in;
  uint32_t new_id = 0;
  if (e.id == 0) {
    e.floor = settings_.floor;  // spec 1.1: no prompt, no picker
    for (int i = 0; i < 8 && !new_id; i++) {
      uint32_t c = p_.random32();
      if (c && !find(c)) new_id = c;
    }
    if (events_.full()) archive_old(true);
  } else if (!make_active(e.id)) {
    return 0;
  }
  const Event *s = events_.put_local(e, new_id, me(), w.valid ? w.utc : 0);
  if (!s) return 0;
  send_event(*s);
  events_changed();
  return s->id;
}

bool Controller::delete_event(uint32_t id) {
  WallTime w = p_.wall();
  if (!make_active(id)) return false;
  const Event *s = events_.remove_local(id, me(), w.valid ? w.utc : 0);
  if (!s) return false;
  send_event(*s);
  events_changed();
  return true;
}

bool Controller::restore_event(uint32_t id) {
  WallTime w = p_.wall();
  const Event *s = events_.restore_local(id, me(), w.valid ? w.utc : 0);
  if (!s) return false;
  send_event(*s);
  events_changed();
  return true;
}

uint32_t Controller::save_occurrence(uint32_t series, int32_t day, const Event &in) {
  WallTime w = p_.wall();
  uint32_t utc = w.valid ? w.utc : 0;
  const Event *s = make_active(series);
  if (!s || !s->starts_on(day)) return 0;
  Floor floor = s->floor;
  if (!events_.skip_local(series, day, me(), utc)) return 0;
  // The changed occurrence, as an event of its own on the series' floor.
  Event one = in;
  one.id = 0;
  one.repeat = Repeat::None;
  one.floor = floor;
  uint32_t new_id = 0;
  for (int i = 0; i < 8 && !new_id; i++) {
    uint32_t c = p_.random32();
    if (c && !find(c)) new_id = c;
  }
  if (events_.full()) archive_old(true);
  const Event *n = events_.put_local(one, new_id, me(), utc);
  if (!n) {
    events_.unskip_local(series, day, me(), utc);  // the series as it was, but versioned anew
    send_event(*events_.find(series));
    events_changed();
    return 0;
  }
  send_event(*events_.find(series));
  send_event(*n);
  events_changed();
  return n->id;
}

bool Controller::delete_occurrence(uint32_t series, int32_t day) {
  WallTime w = p_.wall();
  if (!make_active(series)) return false;
  const Event *s = events_.skip_local(series, day, me(), w.valid ? w.utc : 0);
  if (!s) return false;
  send_event(*s);
  events_changed();
  return true;
}

bool Controller::restore_occurrence(uint32_t series, int32_t day) {
  WallTime w = p_.wall();
  const Event *s = events_.unskip_local(series, day, me(), w.valid ? w.utc : 0);
  if (!s) return false;
  send_event(*s);
  events_changed();
  return true;
}

void Controller::own_changed(Status before) {
  p_.save_own(status_.state());
  send_state(false);
  bell_in_.status_changed(before, status_.state().status, p_.mono_ms());
}

void Controller::heard_from_peer() {
  bool was_lost = peer_.no_signal;
  peer_.no_signal = false;
  WallTime w = p_.wall();
  peer_.heard_utc = w.valid ? w.utc : 0;
  // Something we sent during the outage never arrived. Catch the other panel up now: this is
  // triggered by its message, not by a timer, so it isn't a heartbeat.
  if (was_lost) {
    send_state(false);
    send_dog();
    if (mail_.state().ver) send_mail();
    if (reading_sent_) send_reading();
    send_outside();
    send_sum(true);  // and compare calendars: changes on either side may not have arrived
  }
}

void Controller::on_radio(const uint8_t *data, size_t len) {
  Msg m;
  if (!decode(data, len, m)) {
    char line[96];
    if (len >= 1 && data[0] != kProtoVersion)
      snprintf(line, sizeof(line), "dropped a frame with protocol version %u (this firmware speaks %u)", data[0],
               kProtoVersion);
    else
      snprintf(line, sizeof(line), "dropped a malformed radio frame (%u bytes)", (unsigned)len);
    p_.log(line);
    return;
  }
  uint32_t now = p_.mono_ms();
  uint8_t me = sender_id(settings_.floor);
  uint8_t peer = sender_id(other_floor(settings_.floor));
  if (m.sender == me) return;
  if (m.sender == peer) heard_from_peer();  // HANDOFF: any message from that unit clears No signal

  if (m.type == MsgType::Ack) {
    RetryQueue::Result r;
    if (retry_.ack(m.u.ack.seq, r) && r.type == MsgType::Knock) bell_out_.on_delivered(r.knock_id, now);
    return;
  }
  // ACK everything else, duplicates included: our first ACK may be the thing that got lost.
  Msg a;
  a.type = MsgType::Ack;
  a.sender = me;
  a.u.ack.seq = m.seq;
  a.u.ack.type = (uint8_t)m.type;
  uint8_t buf[16];
  size_t n = encode(a, buf, sizeof(buf));
  if (n) p_.radio_send(m.sender, buf, n);

  if (dedup_.seen(m.sender, m.seq, now)) return;
  handle(m);
}

void Controller::maybe_copy_clock(const Msg &m) {
  if (!(m.u.state.flags & kStateClockValid) || m.u.state.clock == 0) return;
  WallTime w = p_.wall();
  // Copy only from a better source, so two panels never pass the same time back and forth, and
  // only when it's off by more than 2 s (a retried STATE can arrive a second late).
  if (!w.valid) {
    p_.set_wall(m.u.state.clock, kClockFromPeer);
  } else if (m.u.state.clock_quality > w.quality) {
    int64_t diff = (int64_t)m.u.state.clock - (int64_t)w.utc;
    if (diff > 2 || diff < -2) p_.set_wall(m.u.state.clock, kClockFromPeer);
  }
}

void Controller::handle(const Msg &m) {
  uint32_t now = p_.mono_ms();
  WallTime w = p_.wall();
  uint8_t peer = sender_id(other_floor(settings_.floor));
  switch (m.type) {
    case MsgType::State: {
      if (m.sender != peer) return;
      peer_.known = true;
      want_peer_state_ = false;
      peer_.st.status = (Status)m.u.state.status;
      peer_.st.since = m.u.state.since;
      peer_.st.until = m.u.state.until;
      peer_.st.fallback = (Status)m.u.state.fallback;
      maybe_copy_clock(m);
      if (m.u.state.flags & kStatePleaseReply) {
        send_state(false);
        send_dog();
        if (mail_.state().ver) send_mail();
        if (reading_sent_) send_reading();
        send_outside();
      }
      break;
    }
    case MsgType::Knock: {
      if (m.sender != peer) return;
      KnockAction a = bell_in_.on_knock(m.u.knock.id, other_floor(settings_.floor), settings_, status_.state().status, now,
                                        w.valid ? w.utc : 0);
      if (a.banner || a.save || a.chime != Chime::None) {  // a new ring, not a repeat of one
        bell_shakes_++;
        ring_alert_ = true;
        ring_alert_at_ = now;
      }
      if (a.chime == Chime::Normal) p_.play(Sound::Chime, current_volume());
      if (a.chime == Chime::Soft) {
        uint8_t v = current_volume();
        p_.play(Sound::SoftChime, v > settings_.soft_chime_drop ? (uint8_t)(v - settings_.soft_chime_drop) : 0);
      }
      break;
    }
    case MsgType::Reply: {
      if (m.sender != peer) return;
      BellOut before = bell_out_.state();
      bell_out_.on_reply(m.u.reply.id, m.u.reply.come_on, now);
      if (bell_out_.state() != before) p_.play(m.u.reply.come_on ? Sound::ReplyYes : Sound::ReplyNo, current_volume());
      break;
    }
    case MsgType::DogMsg: {
      if (m.sender != peer) return;
      DogState in;
      for (int i = 0; i < 2; i++) in.dog[i] = DogOne{m.u.dog.out[i] != 0, m.u.dog.since[i], m.u.dog.ver[i]};
      if (dog_.apply_remote(in)) p_.save_dog(dog_.state());
      // Still different for a dog: ours is newer there, so share it.
      if (!(in.dog[0] == dog_.state().dog[0]) || !(in.dog[1] == dog_.state().dog[1])) send_dog();
      break;
    }
    case MsgType::Mail: {
      if (m.sender != peer) return;
      MailState in;
      in.waiting = m.u.mail.waiting != 0;
      in.since = m.u.mail.since;
      in.ver = m.u.mail.ver;
      bool was_waiting = mail_.state().waiting;
      if (mail_.apply_remote(in)) {
        p_.save_mail(mail_.state());
        if (!was_waiting && mail_.state().waiting && settings_.floor == Floor::Downstairs) mail_alert();
      }
      if (!(in == mail_.state())) send_mail();  // ours is newer: share it
      break;
    }
    case MsgType::Reading:
      if (m.sender != peer) return;
      peer_.reading = m.u.reading.flags & kReadingValid;
      peer_.temp = m.u.reading.temp_tenths_f;
      peer_.hum = m.u.reading.humidity_tenths;
      break;
    case MsgType::Lucy:
      if (m.sender != kSenderGateway) return;
      if (dog_.apply_lucy(m.u.lucy.out, m.u.lucy.since ? m.u.lucy.since : (w.valid ? w.utc : 0))) {
        p_.save_dog(dog_.state());
        send_dog();  // HANDOFF: a new Lucy value goes to the other panel as DOG
      }
      break;
    case MsgType::Outside:
      if (m.sender != kSenderGateway && m.sender != peer) return;
      // A panel that lost its clock (a power cut: no coin cell) takes the Pi's internet time from
      // the gateway's weather message (Mike, 2026-10-31), never a time of 0 (HANDOFF 2026-10-30:
      // the Pi hasn't synced yet). It's when the Pi took the reading, so it can lag a little: ranked
      // with a copy from the other panel, below a clock set by hand.
      if (!w.valid && m.sender == kSenderGateway && m.u.outside.time >= kClockFloor)
        p_.set_wall(m.u.outside.time, kClockFromPeer);
      if (!(m.u.outside.flags & kOutsideValid)) return;
      // HANDOFF (2026-10-30): staleness counts from when this panel received the reading, never from
      // its own time, which is 0 until the Pi's clock has synced and can be wrong before that.
      outside_.got_mono = now;
      outside_.have = true;
      outside_.temp = m.u.outside.temp_tenths_f;
      outside_.hum = m.u.outside.humidity;
      outside_.code = m.u.outside.forecast;
      outside_.flags = m.u.outside.flags;
      outside_.time = m.u.outside.time;
      memcpy(outside_.phrase, m.u.outside.phrase, sizeof(outside_.phrase));
      if (m.sender == kSenderGateway) send_outside();  // HANDOFF: upstairs passes it on to downstairs
      break;
    case MsgType::EventMsg: {
      if (m.sender != peer) return;
      Event e;
      if (!event_read(m.u.event.data, m.u.event.len, e)) return;
      if (events_.full() && !events_.find(e.id)) archive_old(true);
      EventStore::Merge r = events_.merge(e, today(), w.valid ? w.utc : 0);
      if (r == EventStore::kChanged) {
        events_changed();
      } else if (r == EventStore::kArchive) {
        char name[8];  // long past: straight into its archive file
        archive_name(e, name, sizeof(name));
        archive_put(name, &e, 1);
      }
      break;
    }
    case MsgType::EventSum:
      if (m.sender != peer) return;
      // Different calendars: send ours (and the other panel, given our summary back, sends its).
      if (m.u.sum.digest != events_.digest() || m.u.sum.count != events_.count()) ev_stream_ = 0;
      if (m.u.sum.flags & kSumPleaseReply) send_sum(false);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------------------------
// Sensor

void Controller::on_local_reading(bool valid, int16_t t, uint16_t h) {
  bool was = reading_valid_;
  reading_valid_ = valid;
  if (valid) {
    raw_temp_ = t;
    reading_temp_ = (int16_t)(t + settings_.temp_offset_tenths_f);
    reading_hum_ = h > 1000 ? 1000 : h;
  }
  // HANDOFF: send when temperature moves about 1 F or humidity about 3 points, and at boot. Also
  // whenever the sensor drops out or comes back, so a single failed read can't leave dashes.
  (void)was;
  bool moved = !reading_sent_ || valid != sent_valid_ || abs(reading_temp_ - sent_temp_) >= 10 ||
               abs((int)reading_hum_ - (int)sent_hum_) >= 30;
  if (moved && (valid || reading_sent_)) {
    send_reading();
    reading_sent_ = true;
    sent_valid_ = valid;
    sent_temp_ = reading_temp_;
    sent_hum_ = reading_hum_;
  }
}

// ---------------------------------------------------------------------------------------------
// Touch intents

void Controller::touch() {
  last_touch_ = p_.mono_ms();
  touched_ = true;
}

bool Controller::choose_status(Status s) {
  touch();
  Status before = status_.state().status;
  if (!status_.choose(s, p_.wall(), p_.mono_ms())) return false;
  own_changed(before);
  return true;
}

bool Controller::start_timer(Status s, uint32_t duration_s, Status fallback) {
  touch();
  Status before = status_.state().status;
  if (!status_.choose_timed(s, duration_s, fallback, p_.wall())) return false;
  own_changed(before);
  return true;
}

void Controller::tap_bell() {
  touch();
  uint16_t id = bell_out_.tap(p_.mono_ms(), next_knock_);
  if (!id) return;
  bell_shakes_++;
  next_knock_ = (uint16_t)(next_knock_ + 1);
  if (next_knock_ == 0) next_knock_ = 1;
  Msg m;
  m.type = MsgType::Knock;
  m.u.knock.id = id;
  send(m);
}

bool Controller::tap_dog() {
  touch();
  if (!dog_.tap(settings_.floor, p_.wall())) return false;
  p_.save_dog(dog_.state());
  send_dog();
  return true;
}

bool Controller::tap_mail() {
  touch();
  WallTime w = p_.wall();
  if (settings_.floor == Floor::Upstairs) mail_.toggle(w);
  else if (!mail_.acknowledge(w)) return false;
  p_.save_mail(mail_.state());
  send_mail();
  return true;
}

// New mail on the downstairs panel: wake the screen and a soft chime (Mike, 2026-10-26), by the
// doorbell's rules for the tenant's status. The chime only where their doorbell would make a sound
// (always the soft one), and the screen woken only where a knock would wake it, and as a knock
// does: at night to the night level, not the daytime one a touch gives.
void Controller::mail_alert() {
  KnockAction a = knock_action(settings_, status_.state().status);
  if (a.wake) {
    mail_wake_ = true;
    mail_wake_at_ = p_.mono_ms();
  }
  if (a.chime != Chime::None) {
    uint8_t v = current_volume();
    p_.play(Sound::SoftChime, v > settings_.soft_chime_drop ? (uint8_t)(v - settings_.soft_chime_drop) : 0);
  }
}

void Controller::answer_banner(bool come_on) {
  touch();
  ring_alert_ = false;
  uint16_t id = bell_in_.answer(p_.mono_ms());
  if (!id) return;
  Msg m;
  m.type = MsgType::Reply;
  m.u.reply.id = id;
  m.u.reply.come_on = come_on ? 1 : 0;
  send(m);
}

void Controller::dismiss_banner() {
  touch();
  ring_alert_ = false;
  bell_in_.dismiss();
}

void Controller::apply_settings(const Settings &s) {
  bool floor_changed = s.floor != settings_.floor;
  bool offset_changed = s.temp_offset_tenths_f != settings_.temp_offset_tenths_f;
  settings_ = s;
  // A new offset shows at once (and goes to the other panel), not at the sensor's next reading.
  if (offset_changed && reading_valid_) on_local_reading(true, raw_temp_, reading_hum_);
  p_.save_settings(settings_);
  if (floor_changed) {  // this panel now speaks for the other floor: start the exchange again
    peer_ = Peer();
    retry_ = RetryQueue();  // pending frames carry the old sender id
    retry_.set_seq((uint16_t)p_.random32());
    dedup_ = Dedup();
    bell_in_ = BellReceiver();
    bell_out_ = BellSender();
    want_peer_state_ = true;
    send_state(true);
  }
}

void Controller::set_clock_manual(uint32_t utc) {
  p_.set_wall(utc, kClockManual);
  send_state(false);  // the other panel copies a better clock from STATE
}

// ---------------------------------------------------------------------------------------------
// View

int32_t Controller::offset_now() {
  WallTime w = p_.wall();
  return w.valid ? p_.utc_offset(w.utc) : 0;
}

uint8_t Controller::current_volume() {
  uint8_t v = settings_.volume;
  WallTime w = p_.wall();
  // With no clock it could be 3 AM: use the quiet-hours cap rather than risk the full volume.
  bool quiet = !w.valid || in_quiet_hours(settings_, minutes_of_day((int64_t)w.utc + p_.utc_offset(w.utc)));
  if (quiet && v > settings_.quiet_volume_cap) v = settings_.quiet_volume_cap;
  return v;
}

// Time of day for "since"/"until": just the time within the last day, the date when older.
void Controller::format_clock(char *out, size_t n, uint32_t utc) {
  WallTime w = p_.wall();
  int64_t local = (int64_t)utc + p_.utc_offset(utc);
  if (w.valid && (int64_t)w.utc - (int64_t)utc > 20 * 3600) {
    static const char *const kMon[] = {"Jan.", "Feb.", "March", "April", "May", "June",
                                       "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec."};
    Civil c = civil_from_seconds(local);
    snprintf(out, n, "%s %d", kMon[c.month - 1], c.day);
  } else {
    format_time12(out, n, local);
  }
}

static void fmt_reading(char *temp, size_t tn, char *hum, size_t hn, bool valid, int16_t t, uint16_t h) {
  if (!valid) {
    snprintf(temp, tn, "--%sF", kDeg);
    snprintf(hum, hn, "--%%");
    return;
  }
  snprintf(temp, tn, "%d%sF", round_tenths(t), kDeg);
  snprintf(hum, hn, "%d%%", round_tenths((int)h));
}

void Controller::fill_floor(FloorView &fv, bool mine) {
  WallTime w = p_.wall();
  char when[24];
  fv.floor = mine ? settings_.floor : other_floor(settings_.floor);
  fv.mine = mine;
  OwnState st;
  if (mine) {
    st = status_.state();
    fmt_reading(fv.temp, sizeof(fv.temp), fv.hum, sizeof(fv.hum), reading_valid_, reading_temp_, reading_hum_);
  } else {
    // No signal: dashes, as on the Dusk board (GUI spec: the board wins). The last readings would
    // pass for current ones; they come back with the next message.
    bool lost = !peer_.known || peer_.no_signal;
    fmt_reading(fv.temp, sizeof(fv.temp), fv.hum, sizeof(fv.hum), peer_.reading && !lost, peer_.temp, peer_.hum);
    if (lost) {
      fv.no_signal = true;
      fv.status = peer_.st.status;
      snprintf(fv.word, sizeof(fv.word), "NO SIGNAL");
      // HANDOFF: gray tile, last known state, how long ago. Board P6: "last seen 7:08 PM".
      if (!peer_.known) {
        snprintf(fv.line, sizeof(fv.line), "not heard from yet");
      } else if (peer_.heard_utc && w.valid) {
        char ago[24];
        format_clock(when, sizeof(when), peer_.heard_utc);
        uint32_t gone = w.utc > peer_.heard_utc ? w.utc - peer_.heard_utc : 0;
        format_duration(ago, sizeof(ago), gone);
        if (gone < 60) snprintf(fv.sub, sizeof(fv.sub), "was %s just now", status_word(peer_.st.status));
        else snprintf(fv.sub, sizeof(fv.sub), "was %s, %s ago", status_word(peer_.st.status), ago);
        snprintf(fv.line, sizeof(fv.line), "last seen %s", when);  // GUI spec 4a
      } else {
        snprintf(fv.sub, sizeof(fv.sub), "was %s", status_word(peer_.st.status));
      }
      return;
    }
    st = peer_.st;
    // The other panel announces its own expiry, but if that message is late, show what it will say.
    if (w.valid && st.until && w.utc >= st.until) {
      st.status = st.fallback;
      st.since = st.until;
      st.until = 0;
    }
  }
  fv.status = st.status;
  snprintf(fv.word, sizeof(fv.word), "%s", status_word(st.status));
  if (st.until) {
    format_clock(when, sizeof(when), st.until);
    snprintf(fv.line, sizeof(fv.line), "until %s", when);
    fv.timer = true;
  } else if (st.since) {
    format_clock(when, sizeof(when), st.since);
    snprintf(fv.line, sizeof(fv.line), "since %s", when);
  } else {
    snprintf(fv.line, sizeof(fv.line), "since --:--");
  }
}

void Controller::build_view() {
  uint32_t now = p_.mono_ms();
  WallTime w = p_.wall();
  View &v = view_;
  memset(&v, 0, sizeof(v));
  v.me = settings_.floor;
  v.clock_valid = w.valid;

  if (forced_phase_ >= 0 && forced_phase_ < 6) {
    v.phase = (Phase)forced_phase_;
    v.disc_p = kBoardP[forced_phase_];
    v.moon = v.phase == Phase::Night;
  } else if (w.valid) {
    Sky s = sky_at(w.utc, p_.utc_offset(w.utc), settings_.latitude, settings_.longitude);
    v.phase = s.phase;
    v.disc_p = s.p;
    v.moon = s.moon;
  } else {
    v.phase = Phase::Dusk;  // PROPOSED: unknown time of day gets the in-between palette
    v.disc_p = kBoardP[(int)Phase::Dusk];
  }
  v.mode = mode_for(v.phase);
  // Today's messages for the date strip: a federal holiday, then birthdays and anniversaries.
  v.nstrip = 0;
  if (int32_t t = today(); t >= 0) {
    auto add = [&](const char *text, const char *short_text, DayIcon icon) {
      if (v.nstrip >= sizeof(v.strip) / sizeof(v.strip[0])) return;
      View::StripMsg &s = v.strip[v.nstrip++];
      snprintf(s.text, sizeof(s.text), "%s", text);
      snprintf(s.short_text, sizeof(s.short_text), "%s", short_text);
      s.icon = icon;
    };
    if (const Holiday *h = federal_holiday(t)) add(h->message, h->short_name, h->icon);
    Occ occ[24];
    int n = events_.range(t, t, occ, 24);
    char text[64];
    for (int i = 0; i < n; i++) {
      const Event &e = *occ[i].e;
      if (occ[i].day != t || e.kind == EventKind::Plain) continue;
      bool bday = e.kind == EventKind::Birthday;
      if (e.title[0]) snprintf(text, sizeof(text), "%s, %s", bday ? "Happy Birthday" : "Happy Anniversary", e.title);
      else snprintf(text, sizeof(text), "%s", bday ? "Happy Birthday" : "Happy Anniversary");
      add(text, e.title[0] ? e.title : (bday ? "Birthday" : "Anniversary"), bday ? DayIcon::Cake : DayIcon::Champagne);
    }
  }

  if (w.valid) {
    int64_t local = (int64_t)w.utc + offset_now();
    format_date_long(v.date, sizeof(v.date), local);
    format_time12(v.time, sizeof(v.time), local);
  } else {
    snprintf(v.date, sizeof(v.date), "Clock not set");
    snprintf(v.time, sizeof(v.time), "--:--");
  }

  fill_floor(v.floor[(int)settings_.floor], true);
  fill_floor(v.floor[(int)other_floor(settings_.floor)], false);

  // Outside
  bool fresh = outside_fresh(now);
  v.outside.valid = fresh;
  if (fresh) {
    v.outside.code = forecast_valid(outside_.code) ? (Forecast)outside_.code : Forecast::Unknown;
    v.outside.heat = outside_.flags & kOutsideDangerHeat;
    v.outside.cold = outside_.flags & kOutsideDangerCold;
    snprintf(v.outside.temp, sizeof(v.outside.temp), "%d%sF", round_tenths(outside_.temp), kDeg);
    snprintf(v.outside.hum, sizeof(v.outside.hum), "%u%%", (unsigned)outside_.hum);
    snprintf(v.outside.phrase, sizeof(v.outside.phrase), "%s", outside_.phrase);
  } else {
    v.outside.code = Forecast::Unknown;
    v.outside.heat = v.outside.cold = false;
    snprintf(v.outside.temp, sizeof(v.outside.temp), "--%sF", kDeg);
    snprintf(v.outside.hum, sizeof(v.outside.hum), "--%%");
    snprintf(v.outside.phrase, sizeof(v.outside.phrase), "No reading");
  }

  // Doorbell tile
  v.bell.out = bell_out_.state();
  v.bell.ignoring = bell_out_.ignoring(now);
  v.bell.shakes = bell_shakes_;
  v.bell.alert = ring_alert_;
  static const char *const kBell[] = {"", "Ringing", "Delivered", "Come on", "Not now", "No answer yet", "Couldn't reach"};
  snprintf(v.bell.line, sizeof(v.bell.line), "%s", kBell[(int)v.bell.out]);
  bool missed = bell_in_.missed();
  if (missed && !last_missed_) missed_mono_ = now;  // the note stays an hour (cleared in tick)
  last_missed_ = missed;
  if (missed) {
    if (bell_in_.missed_utc()) {
      char when[24];
      format_clock(when, sizeof(when), bell_in_.missed_utc());
      snprintf(v.bell.missed, sizeof(v.bell.missed), "Missed %s", when);
    } else {
      snprintf(v.bell.missed, sizeof(v.bell.missed), "Missed a ring");
    }
  }

  // Dog
  v.dog.out = dog_.state().out();
  v.dog.can_tap = true;  // each panel's button is its own dog's, whatever the other dog is doing
  if (v.dog.out != Dog::None) snprintf(v.dog.pill, sizeof(v.dog.pill), "%s OUT", dog_name(v.dog.out));

  // Mail for downstairs
  bool upstairs = settings_.floor == Floor::Upstairs;
  v.mail.solid = mail_.state().waiting;
  v.mail.show = upstairs || v.mail.solid;
  v.mail.pulse = !upstairs && v.mail.solid;

  // Banner
  v.banner.kind = bell_in_.banner();
  if (v.banner.kind != BannerKind::None) {
    snprintf(v.banner.title, sizeof(v.banner.title), "Doorbell from %s", floor_lower(bell_in_.from()));
    if (v.banner.kind == BannerKind::MissedWhileAway) {
      char when[24];
      if (bell_in_.knock_utc()) format_clock(when, sizeof(when), bell_in_.knock_utc());
      else snprintf(when, sizeof(when), "an unknown time");
      snprintf(v.banner.sub, sizeof(v.banner.sub), "Rang at %s while you were away", when);
    }
  }

  if (toast_[0]) snprintf(v.toast, sizeof(v.toast), "%s", toast_);

  BacklightInputs bi;
  bi.mode = v.mode;
  bi.touched_recently = touched_;  // cleared in tick once touch_wake_ms has passed
  bi.clock_unknown = !w.valid && forced_phase_ > 5;
  bi.doorbell_wake = v.banner.kind != BannerKind::None;
  bi.mail_wake = mail_wake_;
  bi.clean_screen = clean_screen_;
  v.backlight = backlight_for(settings_, bi);
}

}  // namespace tc
