#include "controller.h"
#include "../core/brightness.h"
#include "../core/civil.h"
#include "../core/voice_dsp.h"  // voice_meter_level (inline)
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

// Settings::notice_mask's starting value is written as a number there (settings.h can't include protocol.h).
static_assert(kNoticeDefaultMask == Settings().notice_mask, "Settings::notice_mask must start at kNoticeDefaultMask");

void Controller::begin(const Settings &s, const OwnState &own, const DogState &dog, const MailState &mail) {
  started_ms_ = p_.mono_ms();
  near_ms_ = started_ms_;  // nobody-near counts from the start, not from the beginning of time
  settings_ = s;
  settings_.notice_mask &= (uint8_t)((1u << kNoticeKinds) - 1);
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
  // Voice notes: xfers start somewhere new each boot (a C6 that kept a session from before a restart
  // never takes the new job's frames for it), the saved inbox comes back, and on the downstairs auto
  // route the first ping goes out a few seconds after start-up.
  vxfer_ = (uint8_t)(p_.random32() % 255);
  {
    VoiceInboxData d;
    if (p_.load_voice_inbox(d)) restore_voice_inbox(d);
  }
  vping_next_ = started_ms_ + 3000;
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
  if (voice_wake_ && ms_diff(now, voice_wake_at_) >= (int32_t)settings_.touch_wake_ms) voice_wake_ = false;
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
  tick_notices();

  while (const RetryQueue::Frame *f = retry_.due(now)) {
    // A NOTICE or a ROOMS goes to the gateway; everything else to the other panel.
    p_.radio_send(for_gateway(f->type) ? (uint8_t)kSenderGateway : sender_id(other_floor(settings_.floor)), f->data, f->len);
    retry_.sent(f->seq, now);
  }
  RetryQueue::Result r;
  while (retry_.expired(now, r)) {
    if (r.type == MsgType::Notice) {
      // No gateway answered: the notice is dropped. Never No signal: it wasn't for the other panel.
      p_.log("notice: no ACK from the gateway, dropped");
      continue;
    }
    if (r.type == MsgType::Rooms) {  // the same: never No signal. tick_rooms tries again later.
      if (r.seq == rooms_.seq) {
        rooms_.failed = true;
        rooms_.acked = false;
      }
      if (!rooms_fail_logged_) p_.log("rooms: no ACK from the gateway, again in a minute");
      rooms_fail_logged_ = true;
      continue;
    }
    // HANDOFF: no ACK after every retry marks the other side as No signal.
    peer_.no_signal = true;
    if (r.type == MsgType::Knock) bell_out_.on_failed(r.knock_id, now);
    if (r.type == MsgType::Comfort || r.type == MsgType::ComfortDel) {  // it stays in the outbox
      for (int i = 0; i < comfort_out_n_; i++) if (comfort_out_[i].seq == r.seq) comfort_out_[i].seq = 0;
    }
    char line[64];
    snprintf(line, sizeof(line), "%s seq %u: no ACK after %d tries, other panel marked No signal", msg_name(r.type),
             (unsigned)r.seq, RetryQueue::kTries);
    p_.log(line);
  }

  tick_update(now);
  tick_outside_log(now, w);
  send_comfort();
  tick_requests(now);
  tick_events(now, w);
  tick_dog_nudge(now, w);
  tick_presence(now);
  tick_rooms(now, w);
  tick_voice(now, w);
  update_agenda(w);
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
  store_->~Store();
  p_.free_state(store_);
}

// The Store (controller.h): never null on the panel (PSRAM, else internal RAM); stop if there is none.
Controller::Store *Controller::make_store(Platform &p) {
  void *m = p.alloc_state(sizeof(Store));
  if (!m) abort();
  return new (m) Store();
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
  sent_pres_flags_ = presence_flags();
  m.u.state.flags = (uint8_t)(((please_reply || want_peer_state_) ? kStatePleaseReply : 0) | (w.valid ? kStateClockValid : 0) |
                              sent_pres_flags_ | kStateVoice |  // this firmware takes voice messages (SAY)
                              (vgw_heard_ ? kStateGwVoice : 0) |  // only what this panel heard itself, never echoed
                              (vgw_heard_ && store_->vgw_mulaw ? kStateGwMulaw : 0));
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
  m.u.mail.waiting = s.waiting ? 1 : s.none ? 2 : 0;  // 2 = no mail (Mike, 2026-11-18)
  m.u.mail.since = s.since;
  m.u.mail.ver = s.ver;
  send(m);
}

void Controller::send_note(Floor f) {
  Msg m;
  m.type = MsgType::Note;
  m.u.note.floor = (uint8_t)f;
  m.u.note.st = notes_[(int)f].state();
  send(m);
}

void Controller::send_notes() {
  // Mike, 2026-11-07: both floors' messages go across, so a panel that lost its saved state can take
  // back its own floor's message from the other panel's copy (like MAIL).
  for (int f = 0; f < 2; f++)
    if (notes_[f].state().ver) send_note((Floor)f);
}

// Mike, 2026-11-07: every 15 minutes (local :00, :15, :30, :45) each panel adds the outside reading to
// the day's file on the SD card, forever (outside_log.h). Only in the first minute of a quarter, so
// the time on the line is the quarter it stands for; a quarter with no fresh reading then is a gap.
void Controller::tick_outside_log(uint32_t now, const WallTime &w) {
  if (!w.valid || w.utc < kClockFloor || !outside_fresh(now)) return;
  int32_t off = p_.utc_offset(w.utc);
  int64_t local = (int64_t)w.utc + off;
  if (local < 0) return;
  uint32_t slot = (uint32_t)(local / kOutsideEveryS) + 1;  // +1 so 0 can mean none yet
  if (slot == outside_slot_ || local % kOutsideEveryS >= 60) return;
  outside_slot_ = slot;
  // Just started (a restart or a new build) inside a quarter's first minute: that quarter may be in
  // the file already from before the restart, and the slot is only kept in RAM, so leave a gap rather
  // than write it twice (review, 2026-11-07).
  if (ms_diff(now, started_ms_) < 90000) return;
  OutsideSample s;
  s.utc = w.utc;
  s.offset = off;
  s.t = outside_.temp;
  s.h = outside_.hum;
  s.forecast = outside_.code;
  s.flags = outside_.flags & (kOutsideDangerHeat | kOutsideDangerCold);
  char name[kOutsideNameLen], line[96];
  outside_file_name(w.utc, off, name);
  if (outside_csv_line(s, line, sizeof(line))) p_.append_line(name, kOutsideCsvHeader, line);
}

void Controller::save_notes() {
  NoteState both[2] = {notes_[0].state(), notes_[1].state()};
  p_.save_notes(both);
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
  // The time rides along as the clock for the other panel (see the OUTSIDE handler), so it is brought
  // up to now: a reading passed on again later (the other panel just started) would otherwise carry a
  // time minutes old.
  m.u.outside.time = outside_.time ? outside_.time + (uint32_t)ms_diff(p_.mono_ms(), outside_.got_mono) / 1000u : 0;
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
  // A dropped comparison would leave the calendars apart until something else changed them, so
  // tick_events sends it again when the queue has room.
  if (!send(m)) ev_resync_ = true;
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

// The house events (see NoticeKind). Only the upstairs panel sends them, and only the kinds that are
// switched on. The text is cut to kNoticeTextMax and made printable ASCII.
void Controller::raise_notice(NoticeKind k, Floor floor, int16_t value, uint32_t time, const char *text) {
  if (settings_.floor != Floor::Upstairs || !(settings_.notice_mask & notice_bit(k))) return;
  if (!time) {
    WallTime w = p_.wall();
    time = w.valid ? w.utc : 0;
  }
  Msg m;
  m.type = MsgType::Notice;
  m.u.notice.kind = (uint8_t)k;
  m.u.notice.floor = (uint8_t)floor;
  m.u.notice.value = value;
  m.u.notice.time = time;
  size_t n = 0;
  for (; text && text[n] && n < kNoticeTextMax; n++) {
    char c = text[n];
    m.u.notice.text[n] = ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) ? '?' : c;
  }
  m.u.notice.text[n] = 0;
  m.u.notice.text_len = (uint8_t)n;
  if (!retry_.push(m, me(), p_.mono_ms())) p_.log("notice: too many waiting, dropped");
}

// The phones' rules (Mike, 2026-11-15): only a floor that is Open or Busy, and never in quiet hours.
// Quiet hours come from this panel's settings and clock. Without a clock it could be 3 AM, so no
// push (as current_volume takes the quiet cap then): a missed push is the quiet failure, and the
// panels stay the real doorbell.
bool Controller::phones_ok(Status rung_status) {
  if (rung_status != Status::Open && rung_status != Status::Busy) return false;
  WallTime w = p_.wall();
  return w.valid && !in_quiet_hours(settings_, minutes_of_day((int64_t)w.utc + p_.utc_offset(w.utc)));
}

// A ring that should push the rung floor's phones (upstairs only, like every notice). Remembered so
// that an answer or dismiss at a panel can send the quiet follow-up "ANSWERED".
void Controller::ring_notice(Floor rung, uint16_t knock_id, uint32_t utc) {
  if (settings_.floor != Floor::Upstairs) return;
  raise_notice(NoticeKind::Ring, rung, (int16_t)knock_id, utc, "RING");
  ring_pushed_ = true;
  ring_pushed_floor_ = rung;
  ring_pushed_id_ = knock_id;
  ring_pushed_utc_ = utc;
  ring_pushed_mono_ = p_.mono_ms();
}

// That ring was answered or dismissed at a panel, inside its 2 minutes: tell the phones. Not for a
// phone's own answer (the Pi knows), and not once the window is over (the phones have given up).
void Controller::ring_answered_here(Floor rung, uint16_t knock_id) {
  if (!ring_pushed_ || rung != ring_pushed_floor_ || knock_id != ring_pushed_id_) return;
  ring_pushed_ = false;
  int32_t age = ms_diff(p_.mono_ms(), ring_pushed_mono_);
  if (age < 0 || age >= (int32_t)BellReceiver::kBannerMs) return;
  raise_notice(NoticeKind::Ring, rung, (int16_t)knock_id, ring_pushed_utc_, "ANSWERED");
}

// Unanswered rings, both ways. Upstairs rang and downstairs never replied: the sender's "No answer
// yet". Downstairs rang and nobody here answered or dismissed the banner within its 2 minutes: it ran
// out (bell.cpp). A ring while this panel is Away is reported when it arrives (handle, Knock).
void Controller::tick_notices() {
  BellOut o = bell_out_.state();
  if (o == BellOut::NoAnswer && notice_bell_out_ != BellOut::NoAnswer)
    raise_notice(NoticeKind::Knock, Floor::Upstairs, 0, 0);
  notice_bell_out_ = o;
  BannerKind b = bell_in_.banner();
  if (notice_banner_ == BannerKind::Knock && b == BannerKind::None) {
    if (!ring_resolved_) raise_notice(NoticeKind::Knock, other_floor(settings_.floor), 0, bell_in_.knock_utc());
    ring_resolved_ = false;
  }
  notice_banner_ = b;
}

void Controller::own_changed(Status before) {
  p_.save_own(status_.state());
  if (status_.state().status != before)
    raise_notice(NoticeKind::Status, settings_.floor, 0, 0, status_word(status_.state().status));
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
  // The most this queues: STATE, DOG, MAIL, NOTE x2, REQUESTS, READING, OUTSIDE, COMFORT, then the sum.
  static_assert(RetryQueue::kSlots >= 10 + 4, "the catch-up burst plus a few more must fit the retry queue");
  if (was_lost) {
    send_state(false);
    send_dog();
    if (mail_.state().ver) send_mail();
    send_notes();
    if (req_.state().ver) send_requests();
    if (reading_sent_) send_reading();
    send_outside();
    send_comfort();  // answers that never got through
    send_sum(true);  // and compare calendars: changes on either side may not have arrived
  }
}

void Controller::on_radio(const uint8_t *data, size_t len) {
  // Voice frames (types 20-30) have their own codec and never use the ACK, Dedup or RetryQueue.
  if (is_voice_frame(data, len)) {
    on_voice(data, len);
    return;
  }
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
    bool acked = retry_.ack(m.u.ack.seq, r);
    if (acked && r.type == MsgType::Knock) bell_out_.on_delivered(r.knock_id, now);
    if (acked && r.type == MsgType::Rooms && m.sender == kSenderGateway && m.u.ack.seq == rooms_.seq) {
      rooms_.acked = true;
      rooms_.failed = false;
      rooms_fail_logged_ = false;
    }
    if (acked && (r.type == MsgType::Comfort || r.type == MsgType::ComfortDel)) {
      for (int i = 0; i < comfort_out_n_; i++) {
        if (comfort_out_[i].seq != m.u.ack.seq) continue;
        for (int j = i + 1; j < comfort_out_n_; j++) comfort_out_[j - 1] = comfort_out_[j];
        comfort_out_n_--;
        comfort_out_save();
        break;
      }
    }
    if (acked && r.type == MsgType::Update && both_wait_ && m.u.ack.seq == both_seq_) {
      // Not at once: the loop task writes queued saves (the comfort outbox among them) to flash, and
      // a restart a few ms after the tap could cut them off (review, 2026-11-07). The same 700 ms the
      // other panel waits.
      both_acked_ = true;
      both_at_ = both_sent_ + kUpdateDelayMs;
      p_.log("update both: the other panel answered");
    }
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
      bool had_state = peer_.known;
      Status was = peer_.st.status;
      peer_.known = true;
      want_peer_state_ = false;
      if (had_state && was != (Status)m.u.state.status)
        raise_notice(NoticeKind::Status, other_floor(settings_.floor), 0, 0, status_word((Status)m.u.state.status));
      peer_.st.status = (Status)m.u.state.status;
      peer_.st.since = m.u.state.since;
      peer_.st.until = m.u.state.until;
      peer_.st.fallback = (Status)m.u.state.fallback;
      peer_.away = (m.u.state.flags & kStateAwayDesk) != 0;
      peer_.nobody_near = (m.u.state.flags & kStateNobodyNear) != 0;
      vpeer_voice_ = (m.u.state.flags & kStateVoice) != 0;
      vpeer_gw_voice_ = (m.u.state.flags & kStateGwVoice) != 0;
      store_->vpeer_gw_mulaw = (m.u.state.flags & kStateGwMulaw) != 0;
      maybe_copy_clock(m);
      if (m.u.state.flags & kStatePleaseReply) {
        send_state(false);
        send_dog();
        if (mail_.state().ver) send_mail();
        send_notes();
        if (req_.state().ver) send_requests();
        if (reading_sent_) send_reading();
        send_outside();
        send_comfort();  // the other panel has just started: answers still waiting
      }
      break;
    }
    case MsgType::Knock: {
      if (m.sender != peer) return;
      KnockAction a = bell_in_.on_knock(m.u.knock.id, other_floor(settings_.floor), settings_, status_.state().status, now,
                                        w.valid ? w.utc : 0);
      if (a.save) raise_notice(NoticeKind::Knock, other_floor(settings_.floor), 1, w.valid ? w.utc : 0);
      if (a.banner) {
        ring_resolved_ = false;
        // A new ring on the banner (a duplicate returns no action): push this floor's phones.
        if (phones_ok(status_.state().status)) ring_notice(settings_.floor, m.u.knock.id, w.valid ? w.utc : 0);
      }
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
      if (m.u.reply.dismissed) {
        // The other panel dismissed our ring's banner: nothing shows here (a dismiss stays silent to the
        // ringer), but the phones that were pushed hear about it, and a later phone answer to that ring
        // is not passed on (2026-11-15).
        if (bell_out_.awaiting(m.u.reply.id)) ring_dismissed_id_ = m.u.reply.id;
        ring_answered_here(other_floor(settings_.floor), m.u.reply.id);
        break;
      }
      BellOut before = bell_out_.state();
      bell_out_.on_reply(m.u.reply.id, m.u.reply.come_on, now, m.u.reply.phone != 0);
      if (bell_out_.state() != before) {
        p_.play(m.u.reply.come_on ? Sound::ReplyYes : Sound::ReplyNo, current_volume());
        // Answered on the downstairs panel itself: the phones that were pushed hear about it.
        if (!m.u.reply.phone) ring_answered_here(other_floor(settings_.floor), m.u.reply.id);
      }
      break;
    }
    case MsgType::PhoneAnswer:
      on_phone_answer(m);
      break;
    case MsgType::MailAck: {
      // Mike, 2026-11-15: "Got it" on a downstairs phone, through the gateway to upstairs only. The same
      // as the downstairs panel's tap: the mail is picked up, and downstairs clears it from MAIL.
      // The ACK to the gateway is out already (on_radio). No Mail notice for this. Mike, 2026-11-18:
      // it clears whichever is showing, the mail badge or the no-mail one.
      if (m.sender != kSenderGateway || settings_.floor != Floor::Upstairs) {
        p_.log("mail ack: ignored (only the gateway, to the upstairs panel)");
        return;
      }
      if (!mail_.acknowledge(w)) {
        p_.log("mail ack: nothing showing, ignored");
        return;
      }
      p_.save_mail(mail_.state());
      send_mail();
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
    case MsgType::Comfort: {
      // Mike, 2026-11-06: upstairs keeps the same file. Each answer is numbered; a resend is skipped.
      if (m.sender != peer || settings_.floor != Floor::Upstairs) return;
      const ComfortAnswer &a = m.u.comfort;
      if (a.n <= comfort_count_) return;
      char line[kComfortLineMax];
      if (a.n > comfort_count_ + 1) {
        snprintf(line, sizeof(line), "comfort: answers %u to %u never arrived", (unsigned)comfort_count_ + 1, (unsigned)a.n - 1);
        p_.log(line);
      }
      size_t len = comfort_csv_line(a, line, sizeof(line));
      if (len) p_.append_line(kComfortFile, kComfortCsvHeader, line);
      comfort_count_ = a.n;
      p_.save_comfort_count(a.n);
      break;
    }
    case MsgType::ComfortDel: {
      // Mike, 2026-11-06: the tenant's removal is marked in upstairs' file too, with the same text.
      if (m.sender != peer || settings_.floor != Floor::Upstairs) return;
      uint32_t n = m.u.comfort_del.n;
      char line[kComfortLineMax];
      if (n < 1 || n > comfort_count_) {
        snprintf(line, sizeof(line), "comfort: removal of answer %u ignored, only %u here", (unsigned)n, (unsigned)comfort_count_);
        p_.log(line);
        return;
      }
      char text[24];
      comfort_removed_text(m.u.comfort_del.removed, m.u.comfort_del.offset, text, sizeof(text));
      p_.set_csv_field(kComfortFile, n, kComfortRemovedColumn, text);
      break;
    }
    case MsgType::Update: {
      if (m.sender == kSenderGateway) {
        // Mike, 2026-11-07: the Pi asked the gateway for UPDATE, no touch on either panel. Upstairs does
        // what its own "both panels" button does; the gateway only ever sends to upstairs, which
        // passes it on. The ACK to the gateway is out already (on_radio), and a retried UPDATE the
        // dedup window lets through must not restart a wait that is running.
        if (settings_.floor != Floor::Upstairs) {
          p_.log("update: the gateway asked this (downstairs) panel, ignored: it only goes to upstairs");
          return;
        }
        if (both_wait_ || update_wait_) return;
        p_.log("update: asked by the gateway");
        if (!update_both()) p_.log("update: asked by the gateway, but this panel has no updater");
        return;
      }
      // Mike, 2026-11-07: the other panel's "both panels" button. Already ACKed by on_radio; the
      // updater starts a little later (tick_update) so that ACK is out before this panel restarts.
      if (m.sender != peer) return;
      if (!p_.updater_installed()) {
        p_.log("update: asked by the other panel, but this panel has no updater");
        return;
      }
      update_wait_ = true;
      update_at_ = now + kUpdateDelayMs;
      break;
    }
    case MsgType::Mail: {
      if (m.sender != peer) return;
      MailState in;
      in.waiting = m.u.mail.waiting == 1;
      in.none = m.u.mail.waiting == 2;  // Mike, 2026-11-18: "no mail"; no chime, no wake, on either panel
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
    case MsgType::Note: {
      // Mike, 2026-11-07: a tile message belongs to the floor that wrote it. The other panel sends
      // both floors' (like MAIL, the higher counter wins): about its own floor, the message itself;
      // about ours, its copy of ours, which is how a panel whose saved state was lost takes its own
      // message back. When ours is the newer one, it goes back. No touch(): a message arriving from
      // the other floor must not wake the screen or end the screensaver (review, 2026-11-07).
      if (m.sender != peer) return;
      uint8_t f = m.u.note.floor;
      if (f > 1) return;
      NoteState in = m.u.note.st;
      NoteState prev = notes_[f].state();
      if (notes_[f].apply_remote(in)) {
        save_notes();
        const NoteState &cur_st = notes_[f].state();
        // The other floor's message, new or changed (a copy of ours coming back is not news).
        if (f == (uint8_t)other_floor(settings_.floor) && cur_st.text[0] &&
            (cur_st.set != prev.set || strcmp(cur_st.text, prev.text)))
          raise_notice(NoticeKind::Note, (Floor)f, 0, cur_st.set, cur_st.text);
      }
      if (!(in == notes_[f].state())) send_note((Floor)f);
      break;
    }
    case MsgType::Requests: {
      // Mike, 2026-11-06: the arrows are kept like the mail badge, the higher counter wins.
      if (m.sender != peer) return;
      uint32_t utc = w.valid ? w.utc : 0;
      bool down = settings_.floor == Floor::Downstairs;
      req_age_log();
      RequestState in = m.u.req;
      ReqArrow old[kMaxArrows], cur[kMaxArrows];
      int on = req_.showing(utc, old);
      if (req_.apply_remote(in)) {
        p_.save_requests(req_.state());
        int cn = req_.showing(utc, cur);
        if (down) {  // upstairs only ever clears: the arrows that went are Mike's doing
          uint32_t at = req_.state().acted > 1 ? req_.state().acted : utc;
          for (int i = 0; i < on; i++) {
            bool kept = false;
            for (int j = 0; j < cn; j++) kept |= old[i].ask == cur[j].ask && old[i].source == cur[j].source && old[i].sent == cur[j].sent;
            if (!kept) req_log(old[i], ReqEnd::Cleared, at);
          }
        } else if (cn && (!on || old[on - 1].ask != cur[cn - 1].ask || old[on - 1].sent != cur[cn - 1].sent)) {
          const ReqArrow &na = cur[cn - 1];
          if (ask_urgent((Ask)na.ask)) req_ding();  // a new urgent arrow: wakes the screen (build_view) and dings at once
          // Told to the Pi once per arrow: only one the old list didn't hold, and only the tenant's.
          bool known = false;
          for (int i = 0; i < on; i++) known |= old[i].ask == na.ask && old[i].source == na.source && old[i].sent == na.sent;
          if (!known && na.source == (uint8_t)ReqSource::Tenant)
            raise_notice(ask_urgent((Ask)na.ask) ? NoticeKind::ArrowUrgent : NoticeKind::ArrowMild, Floor::Downstairs,
                         ask_heat((Ask)na.ask) ? 1 : -1, na.sent);
        }
        req_snapshot(false);
      }
      if (!(in == req_.state())) send_requests();  // ours is newer: share it
      break;
    }
    case MsgType::Reading:
      if (m.sender != peer) return;
      peer_.reading = m.u.reading.flags & kReadingValid;
      peer_.temp = m.u.reading.temp_tenths_f;
      peer_.hum = m.u.reading.humidity_tenths;
      peer_rd_have_ = true;  // for ROOMS: one has arrived since start-up (Mike, 2026-11-22)
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
      // Mike, 2026-11-07: the WeatherGate sends internet-verified time with every reading (the gateway's,
      // or upstairs passing it on, brought up to date in send_outside), so this is the panel's network
      // clock and the panel no longer asks pool.ntp.org itself. It is taken when the clock isn't set or
      // is more than 2 s off either way (kClockNetwork beats a time set by hand and a copy from the other
      // panel), never a time below kClockFloor: the Pi sends 0 until it has synced (HANDOFF 2026-10-30).
      if (m.u.outside.time >= kClockFloor) {
        int64_t diff = (int64_t)m.u.outside.time - (int64_t)w.utc;
        // A big step back on a clock already set from here would most likely be an old reading sent
        // again, not a correction: small ones (the clock running fast) are taken, jumps of more than
        // 2 minutes back are left (review, 2026-11-07).
        bool old = w.valid && w.quality >= kClockNetwork && diff < -120;
        // But one wrong time from the gateway that put the clock ahead must not pin it there for good
        // (audit, 2026-11-11). A re-sent old reading stands still while real time moves on; a gateway
        // that has really stepped back keeps sending times that advance in step with our own clock. So
        // when 3 readings in a row (2 consistent intervals) agree with each other that way, take it.
        // Downstairs gets the same through send_outside, which adds the elapsed seconds.
        if (old) {
          int32_t el = ms_diff(now, back_mono_);
          int64_t drift = (int64_t)m.u.outside.time - ((int64_t)back_time_ + el / 1000);
          bool steady = back_n_ > 0 && drift >= -kBackSlackS && drift <= kBackSlackS;
          if (steady && el < kBackMinGapMs) {
            // A copy sent again a moment later (the catch-up): neither a new reading nor a mismatch.
          } else {
            back_n_ = steady ? back_n_ + 1 : 1;
            back_time_ = m.u.outside.time;
            back_mono_ = now;
            if (back_n_ >= kBackNeeded) {
              p_.log("clock: the gateway kept to an earlier time, taking it");
              p_.set_wall(m.u.outside.time, kClockNetwork);
              back_n_ = 0;
            }
          }
        } else {
          back_n_ = 0;
          if (!w.valid || diff > 2 || diff < -2) p_.set_wall(m.u.outside.time, kClockNetwork);
        }
      }
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
  near_ms_ = last_touch_;  // someone is at the panel
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
  // The tenant ringing while upstairs says nobody is near its panel: the toast says so while the
  // ring waits (build_view). Noted now, so a STATE arriving meanwhile doesn't change this ring's.
  knock_near_note_ = settings_.floor == Floor::Downstairs && peer_.known && !peer_.no_signal && peer_.nobody_near;
  knock_near_at_ = p_.mono_ms();
  bell_shakes_++;
  next_knock_ = (uint16_t)(next_knock_ + 1);
  if (next_knock_ == 0) next_knock_ = 1;
  Msg m;
  m.type = MsgType::Knock;
  m.u.knock.id = id;
  send(m);
  ring_dismissed_id_ = 0;  // a new ring: nobody has dismissed it yet
  // Upstairs ringing downstairs: push the tenants' phones by downstairs's last known status.
  if (settings_.floor == Floor::Upstairs && peer_.known && phones_ok(peer_.st.status)) {
    WallTime w = p_.wall();
    ring_notice(Floor::Downstairs, id, w.valid ? w.utc : 0);
  }
}

bool Controller::tap_dog() {
  touch();
  if (!dog_.tap(settings_.floor, p_.wall())) return false;
  p_.save_dog(dog_.state());
  send_dog();
  return true;
}

// Mike, 2026-11-06: every answer is kept forever and copied upstairs, with the readings then.
bool Controller::comfort_answer(Feel f, Ask ask, AskSent sent) {
  if (settings_.floor != Floor::Downstairs || !feel_valid((uint8_t)f)) return false;
  touch();
  WallTime w = p_.wall();
  uint32_t now = p_.mono_ms();
  ComfortAnswer a = {};
  a.n = comfort_count_ + 1;
  a.utc = w.valid ? w.utc : 0;
  a.offset = a.utc ? p_.utc_offset(a.utc) : 0;
  a.clock_q = w.valid ? w.quality : (uint8_t)kClockNone;
  a.feel = (uint8_t)f;
  a.ask = (uint8_t)ask;  // stage 2: SEND also puts the arrow up, below
  a.sent = (uint8_t)sent;
  a.status = (uint8_t)status_.state().status;
  if (reading_valid_) { a.have |= kHaveInside; a.in_t = reading_temp_; a.in_h = reading_hum_; }
  if (peer_.known && peer_.reading && !peer_.no_signal) { a.have |= kHaveUpstairs; a.up_t = peer_.temp; a.up_h = peer_.hum; }
  if (outside_fresh(now)) { a.have |= kHaveOutside; a.out_t = outside_.temp; a.out_h = outside_.hum; }
  comfort_count_ = a.n;
  p_.save_comfort_count(a.n);
  char line[kComfortLineMax];
  size_t len = comfort_csv_line(a, line, sizeof(line));
  if (len) p_.append_line(kComfortFile, kComfortCsvHeader, line);
  snprintf(line, sizeof(line), "comfort: answer %u %s", (unsigned)a.n, feel_code(f));
  p_.log(line);
  comfort_out_add(a, false);
  // Mike, 2026-11-06: SEND also puts the arrow up. A fourth pushes the oldest off.
  if (sent == AskSent::Send && ask != Ask::None && ask_valid((uint8_t)ask)) {
    req_age_log();
    ReqArrow pushed = {};
    bool did_push = false;
    req_.add(ask, ReqSource::Tenant, w, &pushed, &did_push);
    if (did_push) req_log(pushed, ReqEnd::PushedOff, w.valid ? w.utc : 0);
    p_.save_requests(req_.state());
    send_requests();
    req_snapshot(false);
  }
  return true;
}

// Sends waiting answers, oldest first, one at a time (kComfortInFlight). Not while
// the other panel has no signal: it is sent again when that panel is heard (no timer retries).
void Controller::send_comfort() {
  if (settings_.floor != Floor::Downstairs || peer_.no_signal) return;
  for (int i = 0; i < comfort_out_n_ && retry_.pending(MsgType::Comfort) + retry_.pending(MsgType::ComfortDel) < kComfortInFlight; i++) {
    if (comfort_out_[i].seq) continue;
    // A removal goes only when everything before it is ACKed, so it can't arrive before its answer.
    if (comfort_out_[i].del && i > 0) return;
    Msg m;
    if (comfort_out_[i].del) {
      m.type = MsgType::ComfortDel;
      m.u.comfort_del = {comfort_out_[i].a.n, comfort_out_[i].a.removed, comfort_out_[i].a.offset};
    } else {
      m.type = MsgType::Comfort;
      m.u.comfort = comfort_out_[i].a;
    }
    uint16_t seq = retry_.push(m, me(), p_.mono_ms());
    if (!seq) return;  // the queue is full: tick tries again
    comfort_out_[i].seq = seq;
  }
}

void Controller::comfort_out_add(const ComfortAnswer &a, bool del) {
  if (comfort_out_n_ == kComfortOutbox) {
    p_.log("comfort: outbox full, the oldest entry will reach upstairs only from the SD card");
    for (int i = 1; i < comfort_out_n_; i++) comfort_out_[i - 1] = comfort_out_[i];
    comfort_out_n_--;
  }
  comfort_out_[comfort_out_n_++] = ComfortOut{a, 0, del};
  comfort_out_save();
  send_comfort();
}

void Controller::comfort_out_save() {
  ComfortPending pend[kComfortPendingMax];
  int n = comfort_out_n_ < kComfortPendingMax ? comfort_out_n_ : kComfortPendingMax;
  int skip = comfort_out_n_ - n;  // never, while the outbox is no larger than the saved list
  for (int i = 0; i < n; i++) pend[i] = ComfortPending{comfort_out_[skip + i].a, (uint8_t)(comfort_out_[skip + i].del ? 1 : 0)};
  p_.save_comfort_pending(pend, n);
}

// ---------------------------------------------------------------------------------------------
// The heat and cooling arrows (requests.h)

void Controller::send_requests() {
  Msg m;
  m.type = MsgType::Requests;
  m.u.req = req_.state();
  send(m);
}

void Controller::req_log(const ReqArrow &a, ReqEnd why, uint32_t ended_utc) {
  if (settings_.floor != Floor::Downstairs) return;
  uint32_t at = ended_utc ? ended_utc : a.sent;
  char line[kComfortLineMax];
  size_t n = request_csv_line(a, why, ended_utc, at ? p_.utc_offset(at) : 0, line, sizeof(line));
  if (n) p_.append_line(kRequestsFile, kRequestsCsvHeader, line);
}

// Remembers the arrows showing (raw: all that are held, for a restart) so tick can tell when one
// timed out, which is no state change and so nobody else would log it.
void Controller::req_snapshot(bool raw) {
  WallTime w = p_.wall();
  if (raw) {
    const RequestState &s = req_.state();
    req_prev_n_ = s.n > kMaxArrows ? kMaxArrows : s.n;
    for (int i = 0; i < req_prev_n_; i++) req_prev_[i] = s.a[i];
  } else {
    req_prev_n_ = req_.showing(w.valid ? w.utc : 0, req_prev_);
  }
  req_prev_ver_ = req_.state().ver;
}

void Controller::req_age_log() {
  if (settings_.floor != Floor::Downstairs) return;
  WallTime w = p_.wall();
  if (req_.state().ver == req_prev_ver_) {
    ReqArrow cur[kMaxArrows];
    int cn = req_.showing(w.valid ? w.utc : 0, cur);
    for (int i = 0; i < req_prev_n_; i++) {
      bool kept = false;
      for (int j = 0; j < cn; j++)
        kept |= req_prev_[i].ask == cur[j].ask && req_prev_[i].source == cur[j].source && req_prev_[i].sent == cur[j].sent;
      if (!kept) req_log(req_prev_[i], ReqEnd::TimedOut, req_prev_[i].sent ? req_prev_[i].sent + kArrowLifeS : (w.valid ? w.utc : 0));
    }
  }
  req_snapshot(false);
}

// Busy's soft chime level (the knock rule): the volume less soft_chime_drop.
uint8_t Controller::soft_volume() {
  uint8_t v = current_volume();
  return v > settings_.soft_chime_drop ? (uint8_t)(v - settings_.soft_chime_drop) : 0;
}

// Upstairs, an urgent arrow dings (Mike, 2026-11-06) at the Busy soft chime level, even when
// Mike is Busy, but not in the screen's dark hours. The screen is lit by build_view meanwhile.
void Controller::req_ding() {
  req_urgent_ = true;
  req_ding_at_ = p_.mono_ms();
  WallTime w = p_.wall();
  if (w.valid && in_screen_off_hours(settings_, minutes_of_day((int64_t)w.utc + p_.utc_offset(w.utc)))) return;
  p_.play(Sound::SoftChime, soft_volume());
}

void Controller::tick_requests(uint32_t now) {
  WallTime w = p_.wall();
  if (settings_.floor == Floor::Downstairs) {
    // Arrows sent before the clock was set get its time now, so the 4-hour limit holds for them too.
    if (req_.stamp_unknown(w)) {
      p_.save_requests(req_.state());
      send_requests();
    }
    req_age_log();  // after the stamp: its new counter means nothing is mistaken for timed out
    return;
  }
  if (!req_.any_urgent(w.valid ? w.utc : 0)) {
    req_urgent_ = false;
  } else if (!req_urgent_ || ms_diff(now, req_ding_at_) >= (int32_t)kUrgentDingMs) {
    req_ding();
  }
}

// One tap on the arrows (Mike, 2026-11-06): upstairs it clears them all; downstairs the tenant
// takes them back.
bool Controller::tap_requests() {
  touch();
  req_age_log();
  WallTime w = p_.wall();
  ReqArrow shown[kMaxArrows];
  int n = req_.showing(w.valid ? w.utc : 0, shown);
  bool up = settings_.floor == Floor::Upstairs;
  if (!(up ? req_.clear(w) : req_.take_back(w))) return false;
  // Mike, 2026-11-07: "Action taken on temperature" takes his own message away too; the
  // confirmation shows in that spot.
  if (up && notes_[(int)Floor::Upstairs].clear(w)) {
    save_notes();
    send_note(Floor::Upstairs);
  }
  if (!up)
    for (int i = 0; i < n; i++) req_log(shown[i], ReqEnd::TakenBack, w.valid ? w.utc : 0);
  p_.save_requests(req_.state());
  send_requests();
  req_snapshot(false);
  return true;
}

bool Controller::tap_acted() {
  touch();
  if (settings_.floor != Floor::Downstairs || !req_.dismiss_acted(p_.wall())) return false;
  p_.save_requests(req_.state());
  send_requests();
  req_snapshot(false);
  return true;
}

// Mike, 2026-11-06: a mistaken answer is marked removed (with the time) in both files, never erased.
bool Controller::comfort_remove(uint32_t n) {
  if (settings_.floor != Floor::Downstairs || n < 1 || n > comfort_count_) return false;
  touch();
  WallTime w = p_.wall();
  ComfortAnswer a = {};
  a.n = n;
  a.removed = w.valid ? w.utc : 0;
  a.offset = a.removed ? p_.utc_offset(a.removed) : 0;
  char text[24];
  comfort_removed_text(a.removed, a.offset, text, sizeof(text));
  p_.set_csv_field(kComfortFile, n, kComfortRemovedColumn, text);
  char line[64];
  snprintf(line, sizeof(line), "comfort: answer %u removed", (unsigned)n);
  p_.log(line);
  comfort_out_add(a, true);
  return true;
}

// Mike, 2026-11-07: a restart must not lose answers or removals upstairs has not ACKed. They come
// back with no seq, so they are sent again (oldest first) when the other panel is heard.
void Controller::restore_comfort_pending(const ComfortPending *p, int n) {
  if (!p || n <= 0) return;
  int skip = n > kComfortOutbox ? n - kComfortOutbox : 0;  // too many: keep the newest
  comfort_out_n_ = 0;
  for (int i = skip; i < n; i++) comfort_out_[comfort_out_n_++] = ComfortOut{p[i].a, 0, p[i].del != 0};
  if (skip) comfort_out_save();
  send_comfort();
}

// Mike, 2026-11-07: one tap updates both panels. UPDATE goes to the other panel; this panel's
// updater starts when that is ACKed, or after 2 s if it never is (the other may be off or out of range).
bool Controller::update_both() {
  if (!p_.updater_installed()) return false;
  Msg m;
  m.type = MsgType::Update;
  uint16_t seq = retry_.push(m, me(), p_.mono_ms());
  if (!seq) {
    p_.log("update both: the radio queue is full, updating this panel only");
    p_.start_updater();
    return true;
  }
  both_wait_ = true;
  both_acked_ = false;
  both_seq_ = seq;
  both_sent_ = p_.mono_ms();
  both_at_ = both_sent_ + kBothWaitMs;
  p_.log("update both: UPDATE sent to the other panel");
  return true;
}

// The waits of update_both and of a received UPDATE.
void Controller::tick_update(uint32_t now) {
  if (both_wait_ && ms_reached(now, both_at_)) {
    both_wait_ = false;
    p_.log(both_acked_ ? "update both: starting this panel's updater"
                       : "update both: no answer from the other panel in 2 s, starting this panel's updater");
    p_.start_updater();
  }
  if (update_wait_ && ms_reached(now, update_at_)) {
    update_wait_ = false;
    p_.log("update: asked by the other panel, starting the updater");
    p_.start_updater();
  }
}

// Tile messages (note.h, Mike 2026-11-07). A panel only changes its own floor's message; the other
// floor's copy changes only when a NOTE arrives.
bool Controller::set_note(const char *text, uint32_t duration_s) {
  touch();
  NoteModel &n = notes_[(int)settings_.floor];
  NoteState before = n.state();
  n.set(text, duration_s, p_.wall());
  if (n.state() == before) return false;
  if (n.state().text[0]) raise_notice(NoticeKind::Note, settings_.floor, 0, n.state().set, n.state().text);
  save_notes();
  send_note(settings_.floor);
  return true;
}

bool Controller::clear_note() {
  touch();
  if (!notes_[(int)settings_.floor].clear(p_.wall())) return false;
  save_notes();
  send_note(settings_.floor);
  return true;
}

void Controller::restore_notes(const NoteState *notes2) {
  if (!notes2) return;
  notes_[0].restore(notes2[0]);
  notes_[1].restore(notes2[1]);
}

bool Controller::tap_mail() {
  touch();
  WallTime w = p_.wall();
  if (settings_.floor == Floor::Upstairs) {
    mail_.toggle(w);
    // The notice pushes the downstairs phones (Mike, 2026-11-15): only while downstairs is Open or Busy
    // and not in quiet hours.
    if (mail_.state().waiting && peer_.known && phones_ok(peer_.st.status))
      raise_notice(NoticeKind::Mail, Floor::Downstairs, 1, mail_.state().since);
  } else if (!mail_.acknowledge_mail(w)) {
    return false;
  }
  p_.save_mail(mail_.state());
  send_mail();
  return true;
}

// Mike, 2026-11-18: "no mail", the badge left of the mail one. The same rules as the mail badge,
// except it is quiet: no chime, no screen wake, no pulse, on either panel. The phones get one notice
// (the Mail kind, value 3, text NOMAIL) under the same gate as a mail notice; the phone's "Got it"
// comes back as MAIL_ACK and clears it. Marking it clears mail waiting, and the reverse.
bool Controller::tap_nomail() {
  touch();
  WallTime w = p_.wall();
  if (settings_.floor == Floor::Upstairs) {
    mail_.toggle_none(w);
    if (mail_.state().none && peer_.known && phones_ok(peer_.st.status))
      raise_notice(NoticeKind::Mail, Floor::Downstairs, 3, mail_.state().since, "NOMAIL");
  } else if (!mail_.acknowledge_none(w)) {
    return false;
  }
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

void Controller::answer_banner(bool come_on, bool phone) {
  if (!phone) touch();  // a phone's answer is nobody at this panel
  ring_alert_ = false;
  ring_resolved_ = true;
  uint16_t id = bell_in_.answer(p_.mono_ms());
  if (!id) return;
  Msg m;
  m.type = MsgType::Reply;
  m.u.reply.id = id;
  m.u.reply.come_on = come_on ? 1 : 0;
  m.u.reply.phone = phone ? 1 : 0;
  send(m);
  if (!phone) ring_answered_here(settings_.floor, id);
}

void Controller::dismiss_banner() {
  touch();
  ring_alert_ = false;
  ring_resolved_ = true;
  if (bell_in_.banner() == BannerKind::Knock) {
    uint16_t id = bell_in_.id();
    ring_answered_here(settings_.floor, id);
    // Tell the ringer's panel, which shows nothing for it: upstairs needs it when downstairs dismisses,
    // to send the phones ANSWERED and stop passing on their answers (2026-11-15).
    Msg m;
    m.type = MsgType::Reply;
    m.u.reply.id = id;
    m.u.reply.dismissed = 1;
    send(m);
  }
  bell_in_.dismiss();
}

// Mike, 2026-11-15: a phone answered a ring (ntfy, the Pi, the gateway). The gateway sends only to
// upstairs (already ACKed by on_radio). A ring to upstairs is answered here as if tapped; a ring to
// downstairs is passed on, unchanged, while upstairs still waits on that knock, and downstairs
// answers its own banner. A ring no longer showing (answered, dismissed, run out, another id) ignores
// it, so a late or repeated tap on a phone does nothing.
void Controller::on_phone_answer(const Msg &m) {
  uint8_t peer = sender_id(other_floor(settings_.floor));
  Floor rung = m.u.phone.floor ? Floor::Downstairs : Floor::Upstairs;
  bool from_gw = m.sender == kSenderGateway && settings_.floor == Floor::Upstairs;
  bool relayed = m.sender == peer && settings_.floor == Floor::Downstairs && rung == Floor::Downstairs;
  if (!from_gw && !relayed) {
    p_.log("phone answer: ignored (wrong sender for this panel)");
    return;
  }
  if (rung == settings_.floor) {
    if (bell_in_.banner() != BannerKind::Knock || bell_in_.id() != m.u.phone.id) {
      p_.log("phone answer: that ring isn't showing, ignored");
      return;
    }
    answer_banner(m.u.phone.come_on != 0, true);
    return;
  }
  // Upstairs, a ring to downstairs: pass it on while this panel still waits on that knock.
  if (!bell_out_.awaiting(m.u.phone.id) || m.u.phone.id == ring_dismissed_id_) {
    p_.log("phone answer: that ring to downstairs is over, ignored");
    return;
  }
  Msg r;
  r.type = MsgType::PhoneAnswer;
  r.u.phone = m.u.phone;
  send(r);
}

void Controller::apply_settings(const Settings &s) {
  bool floor_changed = s.floor != settings_.floor;
  bool offset_changed = s.temp_offset_tenths_f != settings_.temp_offset_tenths_f;
  settings_ = s;
  settings_.notice_mask &= (uint8_t)((1u << kNoticeKinds) - 1);
  // A new offset shows at once (and goes to the other panel), not at the sensor's next reading.
  if (offset_changed && reading_valid_) on_local_reading(true, raw_temp_, reading_hum_);
  p_.save_settings(settings_);
  if (floor_changed) {  // this panel now speaks for the other floor: start the exchange again
    peer_ = Peer();
    peer_rd_have_ = false;
    rooms_ = RoomsInfo();  // upstairs now (or no longer): the next ROOMS goes at once, past the first 20 s
    rooms_fail_logged_ = false;
    retry_ = RetryQueue();  // pending frames carry the old sender id
    retry_.set_seq((uint16_t)p_.random32());
    dedup_ = Dedup();
    bell_in_ = BellReceiver();
    bell_out_ = BellSender();
    notice_bell_out_ = BellOut::Idle;
    notice_banner_ = BannerKind::None;
    ring_resolved_ = false;
    ring_pushed_ = false;
    ring_dismissed_id_ = 0;
    // Voice: a job, a SAY or a PUSH in flight carries the old floor; the inbox stays this panel's.
    if (vjob_) voice_cancel();
    vout_.clear();
    vsay_id_ = 0;
    vpeer_voice_ = false;
    vpeer_gw_voice_ = false;
    store_->vpeer_gw_mulaw = false;
    vdirect_ok_ = true;
    vping_miss_ = 0;
    vping_wait_ = false;
    vping_next_ = p_.mono_ms();
    if (vui_ != VoiceUi::Idle) voice_set_ui(VoiceUi::Idle);
    want_peer_state_ = true;
    send_state(true);
  }
}

void Controller::set_clock_manual(uint32_t utc) {
  p_.set_wall(utc, kClockManual);
  send_state(false);  // the other panel copies a better clock from STATE
}

// ---------------------------------------------------------------------------------------------
// Voice notes (docs/voice-notes/DESIGN.md; piece B): the upload (voice_tx), the route, SAY to the
// other panel, the arrival inbox, the phones' push and their dismiss. Frames are never traced and the
// text of a message is never logged.

bool Controller::VLink::voice_send(uint8_t to, const uint8_t *d, size_t n) { return c_->p_.radio_send_voice(to, d, n); }

static const char *voice_err_word(VoiceErr e) {
  static const char *const w[] = {"none", "no Pi", "gateway old", "send failed", "lost", "nothing heard",
                                  "not caught", "no memory", "peer no answer", "bell", "busy"};
  return (unsigned)e < sizeof(w) / sizeof(w[0]) ? w[(int)e] : "?";
}

bool Controller::voice_capturing() const { return vrec_ && !vrec_->done.load(std::memory_order_acquire); }

void Controller::voice_set_ui(VoiceUi u, VoiceErr e) {
  if (vui_ == u && verr_ == e) return;
  vui_ = u;
  verr_ = e;
  vui_at_ = p_.mono_ms();
}

// Upstairs always talks to the gateway directly (the proven link). Downstairs: as the route says; on
// auto, direct while the pings are answered, else through upstairs when it can relay (it advertises
// voice and is not No signal), else direct anyway (nothing else to try).
uint8_t Controller::voice_hop() const {
  if (settings_.floor == Floor::Upstairs) return kSenderGateway;
  const bool relay_ok = vpeer_voice_ && peer_.known && !peer_.no_signal;
  switch (vroute_) {
    case VoiceRoute::Direct: return kSenderGateway;
    case VoiceRoute::Relay: return kSenderUpstairs;
    default: return (vdirect_ok_ || !relay_ok) ? (uint8_t)kSenderGateway : (uint8_t)kSenderUpstairs;
  }
}

int Controller::voice_fallback() const {
  if (settings_.floor != Floor::Downstairs || vroute_ != VoiceRoute::Auto || voice_hop() != kSenderGateway) return -1;
  return vpeer_voice_ && peer_.known && !peer_.no_signal ? (int)kSenderUpstairs : -1;
}

// Frames from the gateway's side for this panel's floor: from the gateway itself, or (downstairs) relayed
// by upstairs. Upstairs relays only what voice_relay_to allows, so a relayed frame is the gateway's.
bool Controller::voice_from_gateway(const VoiceMsg &m) const {
  if (m.floor != (uint8_t)settings_.floor) return false;
  if (m.sender == kSenderGateway) return true;
  return settings_.floor == Floor::Downstairs && m.sender == kSenderUpstairs;
}

void Controller::voice_send_got(uint8_t to, uint8_t kind, uint32_t key, VoiceGotResult r) {
  uint8_t buf[kVoiceGotLen];
  size_t n = voice_make_got(me(), (uint8_t)settings_.floor, ++vseq_, kind, key, r, buf, sizeof(buf));
  if (n) p_.radio_send_voice(to, buf, n);
}

void Controller::voice_save_inbox() {
  p_.save_voice_inbox(vinbox_.data());
  vinbox_seq_++;
}

void Controller::on_voice(const uint8_t *data, size_t len) {
  uint32_t now = p_.mono_ms();
  VoiceMsg m;
  if (!voice_decode(data, len, m)) {
    vbad_n_++;
    if (!vbad_logged_ || ms_diff(now, vbad_log_) >= 60000) {  // once a minute at most
      char line[64];
      snprintf(line, sizeof(line), "voice: dropped malformed voice frames (%lu so far)", (unsigned long)vbad_n_);
      p_.log(line);
      vbad_logged_ = true;
      vbad_log_ = now;
    }
    return;
  }
  const uint8_t peer = sender_id(other_floor(settings_.floor));
  if (m.sender == me()) return;
  if (m.sender == kSenderGateway) {  // its hello, or any answer: it takes voice; the hello's key: mu-law too
    const bool hello = m.type == VType::Got && m.u.got.kind == (uint8_t)VType::Ping && voice_hello_key(m.u.got.key);
    voice_heard_gateway(now, hello ? (m.u.got.key == kVoiceHelloKeyMulaw ? 1 : 0) : -1);
  }
  if (m.sender == peer) heard_from_peer();  // a relayed frame or a SAY: the other panel is there
  switch (m.type) {
    case VType::Ack:
      if (voice_from_gateway(m)) vtx_.on_ack(m, now);
      break;
    case VType::Abort:
      if (voice_from_gateway(m)) vtx_.on_abort(m, now);
      break;
    case VType::Text: {
      if (!voice_from_gateway(m)) break;
      VoiceTextIs r = vtx_.on_text(m, now);
      // Always answered (a lost GOT means the C6 tries again); a dead xfer's text is "gone" and ignored.
      voice_send_got(m.sender, (uint8_t)VType::Text, voice_text_key(m.u.text.xfer, m.u.text.status),
                     r == VoiceTextIs::NotMine ? VoiceGotResult::Gone : VoiceGotResult::Ok);
      break;
    }
    case VType::Say:
      if (m.sender == peer && m.floor == (uint8_t)other_floor(settings_.floor)) voice_arrived(m);
      break;
    case VType::Got:
      if (m.u.got.kind == (uint8_t)VType::Say) {
        if (m.sender != peer) break;
        if (vout_.on_got(m.u.got.kind, m.u.got.key) && m.u.got.key == vsay_id_) {
          vsay_id_ = 0;
          voice_set_ui(VoiceUi::Delivered);
        }
      } else if (m.u.got.kind == (uint8_t)VType::Push) {
        if (!voice_from_gateway(m)) break;
        if (vout_.on_got(m.u.got.kind, m.u.got.key) && m.u.got.result == (uint8_t)VoiceGotResult::Refused)
          p_.log("voice: the Pi has no voice service, the phones were not told");
      } else if (m.u.got.kind == (uint8_t)VType::Ping) {
        // Only straight from the gateway: a ping proves the direct link, so a relayed answer never counts.
        if (m.sender != kSenderGateway || !vping_wait_ || m.u.got.key != vping_nonce_) break;
        vping_wait_ = false;
        vping_miss_ = 0;
        if (!vdirect_ok_) p_.log("voice: the gateway answers direct again, direct route");
        vdirect_ok_ = true;
        vping_next_ = now + 600000;
      }
      break;
    case VType::Dismiss: {
      // A phone's Got it (phone_bell, the bridge, the C6). Only from the gateway's side.
      if (!voice_from_gateway(m)) break;
      bool removed = vinbox_.remove(m.u.dismiss.id);
      if (removed) {
        voice_save_inbox();
        p_.log("voice: a message was dismissed from a phone");
      }
      voice_send_got(m.sender, (uint8_t)VType::Dismiss, m.u.dismiss.id, removed ? VoiceGotResult::Ok : VoiceGotResult::Gone);
      break;
    }
    default:
      // BEGIN, DATA, END, PUSH and PING are for the C6. On upstairs board_radio passes downstairs' on
      // (the vrelay task) before they get here; anything else is not for a panel.
      break;
  }
}

// A SAY from the other panel (DESIGN 2.3, 7.4): into the inbox (dedup by id), answered with VOICE_GOT
// (a duplicate too: its first GOT may be what was lost), then the ding, the wake and the phones.
void Controller::voice_arrived(const VoiceMsg &m) {
  const uint8_t peer = sender_id(other_floor(settings_.floor));
  bool dropped = false;
  VoiceAdd r = vinbox_.add(m.u.say.id, m.u.say.time, m.floor, m.u.say.text, m.u.say.len, &dropped);
  voice_send_got(peer, (uint8_t)VType::Say, m.u.say.id, r == VoiceAdd::Bad ? VoiceGotResult::Refused : VoiceGotResult::Ok);
  if (r != VoiceAdd::Added) return;
  voice_save_inbox();
  char line[80];
  snprintf(line, sizeof(line), "voice: a message from %s arrived (id %lu)%s", floor_lower(other_floor(settings_.floor)),
           (unsigned long)m.u.say.id, dropped ? ", the oldest dropped" : "");
  p_.log(line);
  WallTime w = p_.wall();
  bool quiet = false, dark = false;
  if (w.valid) {
    int minute = minutes_of_day((int64_t)w.utc + p_.utc_offset(w.utc));
    quiet = in_quiet_hours(settings_, minute);
    dark = settings_.screen_off_overnight && in_screen_off_hours(settings_, minute);
  }
  VoiceArrival a = voice_arrival(status_.state().status, w.valid, quiet, dark, voice_capturing(),
                                 bell_in_.banner() != BannerKind::None);
  if (a.wake) {
    voice_wake_ = true;
    voice_wake_at_ = p_.mono_ms();
  }
  // The panel's play() stops the screensaver before the ding renders (check fix C3, tcomm.ino).
  if (a.ding) p_.play(Sound::Ding, soft_volume());
  if (a.defer) vding_due_ = true;
  if (a.push) voice_push(vinbox_.data().msg[vinbox_.count() - 1]);
}

void Controller::voice_test_arrival(const char *text) {
  size_t len = text ? strlen(text) : 0;
  if (len > kVoiceTextMax) len = kVoiceTextMax;
  if (!len) return;
  WallTime w = p_.wall();
  const uint32_t id = p_.random32() | 1u;
  bool dropped = false;
  VoiceAdd r = vinbox_.add(id, w.valid ? w.utc : 0, (uint8_t)other_floor(settings_.floor), text, (uint8_t)len, &dropped);
  if (r != VoiceAdd::Added) return;
  voice_save_inbox();
  p_.log("voice: a test arrival was shown (serial voice test; not sent, no phone push)");
  voice_wake_ = true;
  voice_wake_at_ = p_.mono_ms();
  p_.play(Sound::Ding, soft_volume());
}

// The receiving panel tells its floor's phones (DESIGN 7.4): VOICE_PUSH to the gateway by this panel's
// route, retried until VOICE_GOT kind 28; no answer is one log line.
void Controller::voice_push(const VoiceNote &n) {
  if (!voice_gateway_ok()) {  // a C6 0.4, or none: it gets no voice frame (field fix 2026-11-20)
    p_.log("voice: the gateway has not said it takes voice, the phones were not told");
    return;
  }
  VoiceMsg m;
  m.type = VType::Push;
  m.sender = me();
  m.floor = (uint8_t)settings_.floor;
  m.seq = ++vseq_;
  m.u.push.id = n.id;
  m.u.push.from = n.from;
  m.u.push.time = n.time;
  m.u.push.len = n.len;
  memcpy(m.u.push.text, n.text, n.len);
  uint8_t buf[kMaxMsg];
  size_t len = voice_encode(m, buf, sizeof(buf));
  if (!len || !vout_.add(voice_hop(), (uint8_t)VType::Push, n.id, buf, len, p_.mono_ms()))
    p_.log("voice: no room to tell the phones, dropped");
  else
    vout_.tick(p_.mono_ms(), vlink_);  // the first try now, not at the next tick
}

bool Controller::voice_press(VoicePurpose purpose) {
  if (vjob_ || vui_ == VoiceUi::Sending) return false;  // one voice job per panel
  touch();
  if (!voice_gateway_ok()) {
    // No voice hello from the gateway (a C6 0.4, or one that has not said so since this panel started):
    // "Gateway needs an update", and not one frame goes out (field fix 2026-11-20).
    p_.log("voice: the gateway has not said it takes voice, nothing sent");
    voice_set_ui(VoiceUi::Error, VoiceErr::GatewayOld);
    return false;
  }
  if (vrec_) voice_free_clip();  // the last clip, if the mic task has let go of it by now
  if (vrec_) {
    voice_set_ui(VoiceUi::Error, VoiceErr::NoMemory);
    return false;
  }
  const uint32_t now = p_.mono_ms();
  // The codec (mu-law when the gateway takes it, else ADPCM; serial `voice codec` by hand) sets the
  // clip's size: mu-law 210 chunks (about 51 KB of PSRAM), ADPCM 104.
  const uint8_t codec = (uint8_t)voice_codec();
  const uint8_t clip = (uint8_t)voice_clip_chunks(codec);
  const size_t bytes = voice_rec_bytes(clip);
  void *mem = p_.alloc_big(bytes);
  VoiceRec *rec = voice_rec_init(mem, bytes, clip);
  if (!rec) {
    p_.free_big(mem);
    p_.log("voice: not enough memory for the clip");
    voice_set_ui(VoiceUi::Error, VoiceErr::NoMemory);
    return false;
  }
  vxfer_ = (uint8_t)(vxfer_ % 255 + 1);
  const uint8_t hop = voice_hop();
  rec->xfer = vxfer_;
  rec->floor = (uint8_t)settings_.floor;
  rec->sender = me();
  rec->to = hop;
  rec->codec = codec;
  rec->chunk_samples = voice_chunk_samples(codec);
  rec->press_ms = now;
  rec->early_stop = kVoiceEarlyStop;
  if (!p_.voice_rec_begin(rec)) {
    voice_rec_destroy(rec);
    p_.free_big(mem);
    p_.log("voice: the microphone would not start");
    voice_set_ui(VoiceUi::Error, VoiceErr::NoMemory);
    return false;
  }
  vrec_ = rec;
  vjob_ = true;
  vpurpose_ = purpose;
  vpress_ms_ = now;
  vtext_[0] = 0;
  vtext_flags_ = 0;
  vtx_.start(rec, purpose, hop, voice_fallback(), now, vlink_);
  voice_set_ui(VoiceUi::Recording);
  return true;
}

void Controller::voice_stop() {
  if (vjob_ && voice_capturing()) p_.voice_rec_stop(vrec_, VoiceStop::StopKey);
}

void Controller::voice_cancel() {
  if (vjob_) {
    if (voice_capturing()) p_.voice_rec_stop(vrec_, VoiceStop::Cancel);
    vtx_.cancel(VoiceAbort::User, p_.mono_ms(), vlink_);
    vtx_.forget_rec();
    vtx_.reset();
    vjob_ = false;
    voice_free_clip();
  }
  vsay_id_ = 0;  // a SAY still in flight may arrive; this screen no longer waits for it
  voice_set_ui(VoiceUi::Idle);
}

void Controller::voice_done() {
  if (vjob_) {
    voice_cancel();
    return;
  }
  vsay_id_ = 0;
  voice_set_ui(VoiceUi::Idle);
}

bool Controller::voice_send(const char *text) {
  if (!text || vjob_ || vsay_id_ || !vpeer_voice_ || !peer_.known || peer_.no_signal) return false;
  VoiceMsg m;
  m.type = VType::Say;
  m.sender = me();
  m.floor = (uint8_t)settings_.floor;  // SAY: the writer's floor
  m.seq = ++vseq_;
  // Printable ASCII only, no spaces at either end, 120 characters at most.
  while (*text == ' ') text++;
  size_t n = 0;
  for (; *text && n < kVoiceTextMax; text++) {
    uint8_t c = (uint8_t)*text;
    if (c >= 0x20 && c <= 0x7E) m.u.say.text[n++] = (char)c;
  }
  while (n && m.u.say.text[n - 1] == ' ') n--;
  if (!n) return false;
  m.u.say.len = (uint8_t)n;
  m.u.say.text[n] = 0;
  uint32_t id = 0;
  while (!id) id = p_.random32();
  m.u.say.id = id;
  WallTime w = p_.wall();
  m.u.say.time = w.valid ? w.utc : 0;
  uint8_t buf[kMaxMsg];
  size_t len = voice_encode(m, buf, sizeof(buf));
  if (!len || !vout_.add(sender_id(other_floor(settings_.floor)), (uint8_t)VType::Say, id, buf, len, p_.mono_ms()))
    return false;
  touch();
  vsay_id_ = id;
  vpurpose_ = VoicePurpose::Message;
  voice_set_ui(VoiceUi::Sending);
  vout_.tick(p_.mono_ms(), vlink_);  // the first try now, not at the next tick
  return true;
}

bool Controller::voice_dismiss() {
  if (!vinbox_.pop_top()) return false;
  touch();
  voice_save_inbox();
  // TODO(voice): clear the phone push. Owner default: a dismiss at the panel sends nothing to the phones
  // yet (kVoiceDismissClearsPhone false); a later phone tap gets "Already dismissed at the panel".
  if (kVoiceDismissClearsPhone) {
  }
  return true;
}

void Controller::restore_voice_inbox(const VoiceInboxData &d) {
  if (!vinbox_.restore(d)) p_.log("voice: the saved inbox was unreadable, starting empty");
  vinbox_seq_++;
}

void Controller::set_voice_route(VoiceRoute r) {
  vroute_ = r;
  vping_wait_ = false;
  vping_miss_ = 0;
  if (r == VoiceRoute::Auto) {
    vdirect_ok_ = true;
    vping_next_ = p_.mono_ms();  // check it now
  }
}

// The clip is let go of once the mic task has finished with it (voice_rec_release), never sooner.
void Controller::voice_free_clip() {
  if (!vrec_ || !p_.voice_rec_release(vrec_)) return;  // still inside it: the next tick tries again
  voice_rec_destroy(vrec_);
  p_.free_big(vrec_);
  vrec_ = nullptr;
}

void Controller::voice_end_job() {
  const bool failed = vtx_.phase() == VoiceTxPhase::Failed;
  vlast_stats_ = vtx_.stats();
  vlast_err_ = vtx_.err();
  vlast_fell_back_ = vtx_.fell_back();
  vjobs_++;
  if (failed) vjob_fails_++;
  char line[120];
  snprintf(line, sizeof(line), "voice: x=%u %s%s, %u chunks, %lu resends, %lu BEGIN, text %lu ms after the end",
           (unsigned)vtx_.xfer(), failed ? "failed: " : "text in", failed ? voice_err_word(vtx_.err()) : "",
           (unsigned)vlast_stats_.chunks, (unsigned long)vlast_stats_.resends, (unsigned long)vlast_stats_.begin_sends,
           (unsigned long)vlast_stats_.ms_to_text);
  p_.log(line);
  // The direct route went unanswered for this BEGIN: the auto route checks it with a ping now.
  if (vtx_.direct_failed() && settings_.floor == Floor::Downstairs && vroute_ == VoiceRoute::Auto) vping_next_ = p_.mono_ms();
  vtx_.forget_rec();
  vtx_.reset();
  vjob_ = false;
  if (voice_capturing()) p_.voice_rec_stop(vrec_, VoiceStop::Cancel);
  voice_free_clip();
}

void Controller::voice_meter() {
  Store &st = *store_;
  const uint32_t now = p_.mono_ms();
  if (!st.vmeter_on) st.vmeter_read = now - kVoiceMeterEveryMs;  // the first reading at the next tick
  st.vmeter_on = true;
  st.vmeter_at = now;
}

void Controller::tick_voice(uint32_t now, const WallTime &w) {
  (void)w;
  {  // The badge row's live meter (field fix 2026-11-21): the mic task's own block peaks, no capture.
    Store &st = *store_;
    if (st.vmeter_on && ms_diff(now, st.vmeter_at) > (int32_t)kVoiceMeterHoldMs) {
      st.vmeter_on = false;  // the row closed or started recording: nobody asks any more
      st.vmeter_level = 0;
    }
    if (st.vmeter_on && !voice_capturing() && ms_diff(now, st.vmeter_read) >= (int32_t)kVoiceMeterEveryMs) {
      st.vmeter_read = now;
      float peak = -99.0f;
      st.vmeter_level = p_.mic_meter(peak) ? voice_meter_level(peak) : 0;
    }
  }
  // The doorbell stopped the recording (the mic task turns g_rec_bell into rec->stop = Bell).
  bool bell = vjob_ && vrec_ && vrec_->stopped.load(std::memory_order_acquire) == (uint8_t)VoiceStop::Bell;
  if (bell && vtx_.active()) vtx_.cancel(VoiceAbort::Bell, now, vlink_);
  vtx_.tick(now, vlink_);
  vout_.tick(now, vlink_);
  uint8_t kind = 0;
  uint32_t key = 0;
  while (vout_.expired(now, kind, key)) {
    if (kind == (uint8_t)VType::Say) {
      // Like a failed KNOCK: the other panel was offered only because it advertised voice.
      peer_.no_signal = true;
      p_.log("voice: SAY had no answer, other panel marked No signal");
      if (key == vsay_id_) {
        vsay_id_ = 0;
        voice_set_ui(VoiceUi::Error, VoiceErr::PeerNoAnswer);
      }
    } else {
      p_.log("voice: VOICE_PUSH had no answer from the gateway, dropped");
    }
  }
  if (vjob_) {
    const bool cap = voice_capturing();
    switch (vtx_.phase()) {
      case VoiceTxPhase::Begin:
      case VoiceTxPhase::Stream:
      case VoiceTxPhase::Ending:
      case VoiceTxPhase::WaitText:
        voice_set_ui(cap ? VoiceUi::Recording : vtx_.waiting() ? VoiceUi::Waiting : VoiceUi::Working);
        break;
      case VoiceTxPhase::Done:
        snprintf(vtext_, sizeof(vtext_), "%s", vtx_.text());
        vtext_flags_ = vtx_.text_flags();
        vtext_seq_++;
        voice_set_ui(VoiceUi::Result);
        voice_end_job();
        break;
      case VoiceTxPhase::Failed: {
        VoiceErr e = bell ? VoiceErr::Bell : vtx_.err();
        voice_set_ui(e == VoiceErr::None ? VoiceUi::Idle : VoiceUi::Error, e);
        voice_end_job();
        break;
      }
      default:
        vjob_ = false;
        break;
    }
  }
  if (!vjob_ && vrec_) voice_free_clip();
  if (vgw_heard_ && ms_diff(now, vgw_heard_at_) >= (int32_t)kVoiceGwStaleMs) {  // the hellos stopped
    vgw_heard_ = false;
    store_->vgw_mulaw = false;
    p_.log("voice: no voice hello from the gateway for 10 minutes, voice to it stopped");
    send_state(false);  // the other panel's kStateGwVoice goes with it
  }
  if (vui_ == VoiceUi::Error && ms_diff(now, vui_at_) >= 4000) voice_set_ui(VoiceUi::Idle);
  if (vui_ == VoiceUi::Delivered && ms_diff(now, vui_at_) >= 1500) voice_set_ui(VoiceUi::Idle);
  if (vding_due_ && !voice_capturing()) {  // a message came while recording
    vding_due_ = false;
    p_.play(Sound::Ding, soft_volume());
  }
  tick_voice_ping(now);
}

// The downstairs auto route (DESIGN 4.7): a VOICE_PING straight to the gateway a few seconds after
// start-up, every 10 minutes, and after a direct BEGIN went unanswered. VOICE_GOT kind 30 within 300 ms
// = direct is good; a miss is tried again 2 s later, and 3 in a row = through upstairs until a good one.
// Field fix 2026-11-20: no ping at all until the gateway has said it takes voice (its hello, or
// upstairs' kStateGwVoice), so a C6 0.4 never gets one; and after 3 misses the gap doubles from 10
// minutes up to an hour.
void Controller::tick_voice_ping(uint32_t now) {
  if (settings_.floor != Floor::Downstairs || vroute_ != VoiceRoute::Auto || !voice_gateway_ok()) {
    vping_wait_ = false;
    return;
  }
  if (vping_wait_) {
    if (ms_diff(now, vping_sent_) < 300) return;
    vping_wait_ = false;
    if (vping_miss_ < 255) vping_miss_++;
    if (vping_miss_ >= 3 && vdirect_ok_) {
      vdirect_ok_ = false;
      p_.log("voice: 3 pings in a row unanswered direct, using the relay through upstairs");
    }
    const int doublings = vping_miss_ < 3 ? 0 : vping_miss_ - 3 > 3 ? 3 : vping_miss_ - 3;
    const uint32_t gap = vping_miss_ < 3 ? 2000u : 600000u << doublings;
    vping_next_ = now + (gap > 3600000u ? 3600000u : gap);
    return;
  }
  if (!ms_reached(now, vping_next_)) return;
  if (vjob_) {  // never in the middle of an upload
    vping_next_ = now + 1000;
    return;
  }
  uint32_t nonce = p_.random32() | 1u;
  if ((nonce >> 16) == kReachPingTag) nonce ^= 0x80000000u;  // never mistaken for the serial reach test's
  if (voice_hello_key(nonce)) nonce = 3;                      // nor for the gateway's hello (key 0 or 1)
  VoiceMsg m;
  m.type = VType::Ping;
  m.sender = me();
  m.floor = (uint8_t)settings_.floor;
  m.seq = ++vseq_;
  m.u.ping.nonce = nonce;
  uint8_t buf[kVoicePingLen];
  size_t n = voice_encode(m, buf, sizeof(buf));
  if (!n) return;
  p_.radio_send_voice(kSenderGateway, buf, n);
  vping_nonce_ = nonce;
  vping_wait_ = true;
  vping_sent_ = now;
}

// A voice frame straight from the gateway: it takes voice. The first one after none (start-up, or
// after the hellos stopped) is logged and goes to the other panel in a STATE (kStateGwVoice). A hello
// also says whether it takes mu-law (mulaw 1, key 1: C6 0.6 on) or ADPCM only (0, key 0: C6 0.5); a
// change is logged and goes out in a STATE too (kStateGwMulaw), for the next recording on either panel.
void Controller::voice_heard_gateway(uint32_t now, int mulaw) {
  const bool was = vgw_heard_;
  vgw_heard_ = true;
  vgw_heard_at_ = now;
  bool changed = false;
  if (mulaw >= 0 && store_->vgw_mulaw != (mulaw == 1)) {
    store_->vgw_mulaw = mulaw == 1;
    changed = true;
  }
  if (was && !changed) return;
  if (!was) p_.log("voice: the gateway says it takes voice");
  if (changed) p_.log(store_->vgw_mulaw ? "voice: the gateway takes mu-law (its hello), recording in mu-law"
                                        : "voice: the gateway takes ADPCM only (its hello), recording in ADPCM");
  send_state(false);
}

void Controller::voice_report(char *out, size_t n) {
  static const char *const kRoute[] = {"auto", "direct", "relay"};
  const bool up = settings_.floor == Floor::Upstairs;
  snprintf(out, n,
           "voice: route %s (%s now%s), pings missed in a row %u; jobs %lu, %lu failed; last: %u chunks, %lu resends, "
           "%lu BEGIN%s, %s, text %lu ms after the end; inbox %d; other panel %s; gateway %s%s; codec %s%s",
           up ? "direct (upstairs)" : kRoute[(int)vroute_], voice_hop() == kSenderGateway ? "direct" : "via upstairs",
           (!up && vroute_ == VoiceRoute::Auto) ? (vdirect_ok_ ? ", pings answered" : ", pings unanswered") : "",
           (unsigned)vping_miss_, (unsigned long)vjobs_, (unsigned long)vjob_fails_, (unsigned)vlast_stats_.chunks,
           (unsigned long)vlast_stats_.resends, (unsigned long)vlast_stats_.begin_sends,
           vlast_fell_back_ ? " (fell back to upstairs)" : "", voice_err_word(vlast_err_),
           (unsigned long)vlast_stats_.ms_to_text, vinbox_.count(),
           vpeer_voice_ ? "takes voice" : "has no voice (or not heard)",
           vgw_heard_ ? "takes voice (its hello)" : vpeer_gw_voice_ ? "takes voice (upstairs heard it)" : "no voice hello, nothing sent to it",
           !voice_gateway_ok() ? "" : voice_gateway_mulaw() ? ", mu-law too" : ", ADPCM only",
           voice_codec() == VoiceCodec::Mulaw ? "1 mu-law" : "2 ADPCM", store_->vcodec ? " by hand" : "");
}

void Controller::fill_voice_view(VoiceView &vv, const WallTime &w) {
  vv.capable = true;
  vv.peer_offline = !peer_.known || peer_.no_signal;
  vv.peer_voice = vpeer_voice_;
  vv.ui = vui_;
  vv.purpose = vui_ == VoiceUi::Idle ? (VoicePurpose)0 : vpurpose_;
  vv.press_ms = vpress_ms_;
  // While capturing, the recording's own level; with the badge row open before START, the live meter.
  if (store_->vmeter_on && !voice_capturing()) vv.level = store_->vmeter_level;
  else vv.level = vrec_ ? vrec_->level.load(std::memory_order_relaxed) : 0;
  vv.err = verr_;
  vv.text_seq = vtext_seq_;
  vv.text_flags = vtext_flags_;
  snprintf(vv.text, sizeof(vv.text), "%s", vtext_);
  vv.inbox_n = (uint8_t)vinbox_.count();
  vv.inbox_seq = vinbox_seq_;
  if (const VoiceNote *t = vinbox_.top()) {
    vv.top = *t;
    if (t->time && w.valid) format_time12(vv.top_when, sizeof(vv.top_when), (int64_t)t->time + p_.utc_offset(t->time));
  }
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

// Mike, 2026-11-11: each floor's tile shows that floor's next calendar item today, the lowest-
// priority line. "Next" = not ended yet and starting soonest, so one in progress counts until it
// ends. Timed items come before all-day ones (an all-day "Trash day" would otherwise hide the
// 2:30 dentist). Events belong to the floor of the panel that made them (Event::floor); there is
// no shared or both-floors event. Birthdays and anniversaries already show on the date strip, so
// they are left out here. Worked out when the minute, the day or the calendar changes.
void Controller::update_agenda(const WallTime &w) {
  if (!w.valid) {
    agenda_[0] = agenda_[1] = Agenda();
    agenda_day_ = agenda_min_ = -1;
    return;
  }
  int64_t local = (int64_t)w.utc + p_.utc_offset(w.utc);
  int32_t day = local_day(w.utc, p_.utc_offset(w.utc));
  int32_t min = (int32_t)((local - (int64_t)day * 86400) / 60);
  if (day == agenda_day_ && min == agenda_min_ && events_.rev() == agenda_rev_) return;
  agenda_day_ = day;
  agenda_min_ = min;
  agenda_rev_ = events_.rev();
  agenda_[0] = agenda_[1] = Agenda();
  if (!events_.ready()) return;
  int best[2] = {-1, -1};  // start minute of the one kept; -1 = none, 2000 = an all-day one
  Occ occ[24];
  int n = events_.range(day, day, occ, 24);
  for (int i = 0; i < n; i++) {
    const Event &e = *occ[i].e;
    if (e.kind != EventKind::Plain || !e.title[0]) continue;
    int rank;
    if (e.all_day) {
      rank = 2000;
    } else {
      if (occ[i].day != day || min >= e.end_min()) continue;  // already over
      rank = e.start_min;
    }
    int f = (int)e.floor;
    if (best[f] >= 0 && rank >= best[f]) continue;
    best[f] = rank;
    Agenda &a = agenda_[f];
    a.on = true;
    snprintf(a.title, sizeof(a.title), "%s", e.title);
    a.when[0] = 0;
    if (!e.all_day) format_time12(a.when, sizeof(a.when), (int64_t)day * 86400 + (int64_t)e.start_min * 60);
  }
}

// Mike, 2026-11-11: a dog out longer than its limit makes the pill show the minutes and the outside
// temperature and pulse on both panels. Mike, 2026-11-13: the sound is five dings (Sound::DogAlert,
// not the soft chime, which was taken for a doorbell), only on the panel of the floor the dog
// belongs to (Lucy upstairs, Copper downstairs), at the doorbell's volume, and again every
// kDogRingRepeatMin minutes until the dog is in. Mike, 2026-11-22: the limit is each dog's own, from
// the "feels like" temperature outside (dog_limit_min, core/dog.h: 5 to 60 minutes, none at all from
// 45 to 89 F); with no clock or no fresh outside reading, kDogOutMinNormal and no temperature. How long it has been out
// comes from the dog's "since" time; if that or the clock is missing, from when this panel first
// saw it out. Both panels do this alone from the same data. Cheap: a few integer compares.
void Controller::tick_dog_nudge(uint32_t now, const WallTime &w) {
  int limit[2] = {kDogOutMinNormal, kDogOutMinNormal};
  nudge_temp_ = false;
  if (w.valid && outside_fresh(now)) {
    nudge_temp_ = true;
    nudge_tenths_ = outside_.temp;
    const int16_t feels = feels_like_tenths(outside_.temp, outside_.hum);
    for (int i = 0; i < 2; i++) limit[i] = dog_limit_min(i, feels);
  }
  bool told = false;    // a dog went past the limit this tick: the Pi is told once, after the loop
  for (int i = 0; i < 2; i++) {
    DogNudge &n = nudge_[i];
    const DogOne &d = dog_.state().dog[i];
    if (!d.out) {
      n = DogNudge();  // in: the next outing starts afresh
      continue;
    }
    if (!n.seen_out) {
      n.seen_out = true;
      n.rang = false;
      n.out_mono = now;
    }
    uint32_t secs;
    if (w.valid && d.since >= kClockFloor && w.utc >= d.since) secs = w.utc - d.since;
    else secs = (uint32_t)(ms_diff(now, n.out_mono) / 1000);
    uint32_t mins = secs / 60;
    n.over_min = limit[i] > 0 && secs >= (uint32_t)limit[i] * 60u ? (uint16_t)(mins > 65535u ? 65535u : mins) : 0;
    if (n.over_min && !n.noticed) {  // for the Pi, whatever the quiet hours or status say about the chime
      n.noticed = true;
      told = true;
    }
    if (!n.over_min) continue;
    // Only the dog's own floor makes the sound; the other panel shows the pill and nothing more.
    if (settings_.floor != (i == 0 ? Floor::Upstairs : Floor::Downstairs)) continue;
    if (n.rang && mins < n.rang_min + kDogRingRepeatMin) continue;
    // The gate the mail chime uses: only where this panel's own status lets a doorbell make a
    // sound (Open, Busy). Not in quiet hours; the dog is still out when they end, so it rings
    // then. Without a clock, the volume's quiet-hours cap applies.
    KnockAction a = knock_action(settings_, status_.state().status);
    bool quiet = w.valid && in_quiet_hours(settings_, minutes_of_day((int64_t)w.utc + p_.utc_offset(w.utc)));
    if (a.chime == Chime::None || quiet) continue;
    n.rang = true;
    n.rang_min = mins;
    p_.play(Sound::DogAlert, current_volume());
  }
  if (told) {
    // One notice for the dogs past the limit now: LUCY, COPPER or BOTH. The time is when the outing
    // began (the later one for both), so a repeat after a restart is the same notice to the Pi.
    bool l = nudge_[0].over_min != 0, c = nudge_[1].over_min != 0;
    uint16_t mins = l && c ? (nudge_[0].over_min > nudge_[1].over_min ? nudge_[0].over_min : nudge_[1].over_min)
                           : (l ? nudge_[0].over_min : nudge_[1].over_min);
    uint32_t since = l && c ? (dog_.state().dog[0].since > dog_.state().dog[1].since ? dog_.state().dog[0].since
                                                                                        : dog_.state().dog[1].since)
                            : dog_.state().dog[l ? 0 : 1].since;
    raise_notice(NoticeKind::DogOut, Floor::Upstairs, (int16_t)(mins > 32767 ? 32767 : mins), since,
                 l && c ? "BOTH" : l ? "LUCY" : "COPPER");
  }
}

// The presence sensor (Mike, 2026-11-11). Upstairs only, and only once Settings says one is fitted;
// otherwise every flag here is false and nothing about the panel changes. Cheap: a few compares.
//  - away_ ("Away from desk"): activity seen while Busy; ends after kAwayClearMs with none, or when
//    the status is no longer Busy.
//  - nobody_near_: no activity and no touch for kNobodyNearMs. Both go to the tenant's panel in STATE.
//  - passer_by: the place was empty (not seen for the hold time) and someone arrives; 3 s, at most
//    one every kPasserByGapMs.
// The night path (the screen lighting at night) is in build_view, from pres_last_ms_.
void Controller::tick_presence(uint32_t now) {
  const bool on = settings_.floor == Floor::Upstairs && settings_.presence_fitted;
  if (!on) {
    pres_on_ = pres_now_ = pres_seen_ = away_ = nobody_near_ = occupied_ = passer_on_ = false;
  } else {
    if (!pres_on_) {  // just switched on (or this panel just became upstairs): start clean
      pres_on_ = true;
      pres_seen_ = false;
      occupied_ = false;
      near_ms_ = now;
    }
    pres_now_ = p_.presence();
    if (pres_now_) {
      pres_last_ms_ = now;
      pres_seen_ = true;
      near_ms_ = now;
    }
    const bool busy = status_.state().status == Status::Busy;
    if (pres_now_ && busy) away_ = true;
    if (away_ && (!busy || (pres_seen_ && ms_diff(now, pres_last_ms_) >= (int32_t)kAwayClearMs))) away_ = false;
    nobody_near_ = ms_diff(now, near_ms_) >= (int32_t)kNobodyNearMs;
    const bool occ = pres_now_ || (pres_seen_ && ms_diff(now, pres_last_ms_) < (int32_t)((uint32_t)settings_.presence_hold_s * 1000u));
    if (occ && !occupied_ && (!passer_ever_ || ms_diff(now, passer_last_) >= (int32_t)kPasserByGapMs)) {
      passer_ever_ = true;
      passer_last_ = now;
      passer_on_ = true;
      passer_until_ = now + kPasserByShowMs;
    }
    occupied_ = occ;
    if (passer_on_ && ms_reached(now, passer_until_)) passer_on_ = false;
  }
  // The other panel hears of a change at once (a new STATE, no timer).
  if (presence_flags() != sent_pres_flags_) send_state(false);
}

uint8_t Controller::presence_flags() const {
  return (uint8_t)((away_now() ? kStateAwayDesk : 0) | (nobody_near_ ? kStateNobodyNear : 0));
}

// ROOMS (Mike, 2026-11-22): the upstairs panel tells the gateway both floors' indoor readings (as the
// tiles show them, offset applied) and whether upstairs is occupied, for Apple Home through the Pi.
// Downstairs never sends one. When: the first about kRoomsFirstMs after start-up; an occupancy change
// at once; a reading that moved 0.5 F or 2 points of humidity since the last one sent, or a reading
// coming or going, but never within kRoomsMinGapMs of the last; and at least every kRoomsBeatMs (every
// kRoomsRetryMs after one went unACKed). It rides the retry queue behind panel traffic and notices.
void Controller::tick_rooms(uint32_t now, const WallTime &w) {
  if (!rooms_warm_ && ms_diff(now, started_ms_) >= (int32_t)kRoomsFirstMs) rooms_warm_ = true;
  if (settings_.floor != Floor::Upstairs || !rooms_warm_) return;
  Msg m;
  m.type = MsgType::Rooms;
  auto &r = m.u.rooms;
  if (reading_valid_) {
    r.flags |= kRoomsUpValid;
    r.up_t = reading_temp_;
    r.up_h = reading_hum_;
  }
  // Downstairs (Mike's call via the lead, 2026-11-22): valid while its last READING said valid and the
  // link to it is up. Downstairs sends a READING only when it moves (no heartbeats), so an age limit
  // here would flap the Home tile in a steady room; the Pi's 15-minute check on its own file, which
  // this message's heartbeat keeps fresh, catches a ROOMS stream that stops.
  if (peer_rd_have_ && peer_.reading && !peer_.no_signal) {
    r.flags |= kRoomsDownValid;
    r.down_t = peer_.temp;
    r.down_h = peer_.hum > 1000 ? 1000 : peer_.hum;
  }
  if (settings_.presence_fitted) r.flags |= (uint8_t)(kRoomsOccKnown | (occupied_ ? kRoomsOccupied : 0));
  if (rooms_.any) {
    const uint8_t occ = kRoomsOccKnown | kRoomsOccupied, valid = kRoomsUpValid | kRoomsDownValid;
    const int32_t since = ms_diff(now, rooms_.at);
    const bool occ_moved = (r.flags & occ) != (rooms_.flags & occ);
    const bool moved = (r.flags & valid) != (rooms_.flags & valid) ||
                       ((r.flags & kRoomsUpValid) && (abs(r.up_t - rooms_.up_t) >= 5 || abs((int)r.up_h - (int)rooms_.up_h) >= 20)) ||
                       ((r.flags & kRoomsDownValid) &&
                        (abs(r.down_t - rooms_.down_t) >= 5 || abs((int)r.down_h - (int)rooms_.down_h) >= 20));
    const bool beat = since >= (int32_t)(rooms_.failed ? kRoomsRetryMs : kRoomsBeatMs) || since < 0;
    if (!occ_moved && !((moved || beat) && (since >= (int32_t)kRoomsMinGapMs || since < 0))) return;
  }
  r.time = w.valid ? w.utc : 0;
  const bool failed = rooms_.failed;
  uint16_t seq = retry_.push(m, me(), now);  // latest wins: replaces one still waiting
  rooms_.any = true;
  rooms_.acked = false;
  rooms_.failed = seq == 0 || failed;  // not queued (full, or refused by encode) counts as unACKed: again in a minute
  rooms_.flags = r.flags;
  rooms_.up_t = r.up_t;
  rooms_.up_h = r.up_h;
  rooms_.down_t = r.down_t;
  rooms_.down_h = r.down_h;
  rooms_.time = r.time;
  rooms_.at = now;
  rooms_.seq = seq;
  if (!seq) p_.log("rooms: could not be queued, again in a minute");
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
    v.outside.cold = (outside_.flags & kOutsideDangerCold) || cold_test_;
    v.outside.temp_tenths = outside_.temp;
    v.outside.hum_pct = outside_.hum;
    snprintf(v.outside.temp, sizeof(v.outside.temp), "%d%sF", round_tenths(outside_.temp), kDeg);
    snprintf(v.outside.hum, sizeof(v.outside.hum), "%u%%", (unsigned)outside_.hum);
    snprintf(v.outside.phrase, sizeof(v.outside.phrase), "%s", outside_.phrase);
  } else {
    v.outside.code = Forecast::Unknown;
    v.outside.heat = v.outside.cold = false;
    v.outside.temp_tenths = 0;
    v.outside.hum_pct = 0;
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
  bool by_phone = bell_out_.from_phone() && (v.bell.out == BellOut::ComeOn || v.bell.out == BellOut::NotNow);
  // "Come on (from phone)" is 212 px in B20, inside the 250 px tile (Mike, 2026-11-15).
  snprintf(v.bell.line, sizeof(v.bell.line), "%s%s", kBell[(int)v.bell.out], by_phone ? " (from phone)" : "");
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
  if (uint16_t m = nudge_[0].over_min > nudge_[1].over_min ? nudge_[0].over_min : nudge_[1].over_min; m && v.dog.out != Dog::None) {
    // Out too long: the dog(s) past the limit by name ("BOTH" when both are, to keep it short).
    const char *who = nudge_[0].over_min && nudge_[1].over_min ? "BOTH" : nudge_[0].over_min ? "LUCY" : "COPPER";
    if (nudge_temp_) snprintf(v.dog.pill, sizeof(v.dog.pill), "%s OUT %u min, %d%sF", who, (unsigned)m, round_tenths(nudge_tenths_), kDeg);
    else snprintf(v.dog.pill, sizeof(v.dog.pill), "%s OUT %u min", who, (unsigned)m);
    v.dog.alert = true;
  }

  // Mail for downstairs
  bool upstairs = settings_.floor == Floor::Upstairs;
  v.mail.solid = mail_.state().waiting;
  v.mail.show = upstairs || v.mail.solid;
  v.mail.pulse = !upstairs && v.mail.solid;
  v.nomail.active = mail_.state().none;
  v.nomail.show = upstairs || v.nomail.active;

  // The heat and cooling arrows
  {
    uint32_t utc = w.valid ? w.utc : 0;
    ReqArrow ra[kMaxArrows];
    int n = req_.showing(utc, ra);
    v.req.n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
      v.req.ask[i] = (Ask)ra[i].ask;
      v.req.source[i] = (ReqSource)ra[i].source;
    }
    v.req.flash = req_.any_urgent(utc);
    v.req.acted = !upstairs && req_.acted_showing(utc);
  }

  // Tile messages: each floor's, on both panels. The peer's tile in No signal keeps its "last seen"
  // line; and downstairs, the UPSTAIRS tile shows the acted text in that spot while it shows.
  {
    uint32_t utc = w.valid ? w.utc : 0;
    for (int f = 0; f < 2; f++) {
      FloorView &fv = v.floor[f];  // v.floor is indexed by Floor
      if (!notes_[f].showing(utc) || fv.no_signal) continue;
      fv.note = true;
      snprintf(fv.note_text, sizeof(fv.note_text), "%s", notes_[f].state().text);
    }
    // Mike, 2026-11-07: "Action taken on temperature" hides the upstairs message only while the message
    // is older than the acted time; one Mike writes after clearing the arrows shows in its place.
    if (v.req.acted) {
      FloorView &up = v.floor[(int)Floor::Upstairs];
      if (up.note && notes_[(int)Floor::Upstairs].state().set > req_.state().acted) v.req.acted = false;
      else up.note = false;
    }
  }

  // "Away from desk" (Mike, 2026-11-11, presence sensor): the UPSTAIRS tile while he is Busy and
  // walking about the house. Below a tile message and the acted confirmation, above the calendar item;
  // a timer's "until" line (the time Busy ends) and No signal keep their lines. Upstairs knows it
  // itself, downstairs hears it in STATE.
  {
    FloorView &up = v.floor[(int)Floor::Upstairs];
    const bool away = upstairs ? away_now() : peer_.away;
    if (away && up.status == Status::Busy && !up.no_signal && !up.note && !up.timer && !v.req.acted) {
      up.away_desk = true;
      snprintf(up.line, sizeof(up.line), "Away from desk");
    }
  }

  // The lowest-priority line: the floor's next calendar item today, where the line is a plain "since"
  // (not a timer's "until", not No signal) and nothing above it is using the spot: the acted
  // confirmation (downstairs, UPSTAIRS tile), a tile message or "Away from desk". A tap on it still
  // opens the editor.
  for (int f = 0; f < 2; f++) {
    FloorView &fv = v.floor[f];
    const Agenda &a = agenda_[f];
    if (!a.on || fv.no_signal || fv.note || fv.timer || fv.away_desk) continue;
    if (v.req.acted && f == (int)Floor::Upstairs) continue;
    fv.agenda = true;
    snprintf(fv.agenda_title, sizeof(fv.agenda_title), "%s", a.title);
    snprintf(fv.agenda_when, sizeof(fv.agenda_when), "%s", a.when);
  }

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
  // The tenant rang while upstairs said nobody was near its panel: say so while the ring waits.
  else if (knock_near_note_ && (v.bell.out == BellOut::Ringing || v.bell.out == BellOut::Delivered) &&
           ms_diff(now, knock_near_at_) < (int32_t)kNearNoteMs)
    snprintf(v.toast, sizeof(v.toast), "No one is near the panel");

  fill_voice_view(v.voice, w);

  v.passer_by = passer_on_;
  v.presence.fitted = pres_on_;
  v.presence.seen_now = pres_on_ && pres_now_;
  v.presence.occupied = pres_on_ && occupied_;
  v.presence.since_min = pres_on_ && pres_seen_ ? ms_diff(now, pres_last_ms_) / 60000 : -1;

  BacklightInputs bi;
  bi.mode = v.mode;
  bi.touched_recently = touched_;  // cleared in tick once touch_wake_ms has passed
  bi.clock_unknown = !w.valid && forced_phase_ > 5;
  bi.minute_of_day = w.valid ? minutes_of_day((int64_t)w.utc + p_.utc_offset(w.utc)) : -1;  // the dark hours go by the clock
  bi.doorbell_wake = v.banner.kind != BannerKind::None;
  bi.mail_wake = mail_wake_ || (upstairs && v.req.flash);  // urgent arrows keep it lit (Mike, 2026-11-06)
  bi.voice_wake = voice_wake_;
  bi.clean_screen = clean_screen_;
  // The night path: someone walking past lights the screen (at the night level) for kNightWakeMs.
  bi.presence_wake = pres_on_ && pres_seen_ && ms_diff(now, pres_last_ms_) < (int32_t)kNightWakeMs;
  v.backlight = backlight_for(settings_, bi);
}

}  // namespace tc
