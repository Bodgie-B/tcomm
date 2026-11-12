// The panel's application logic: owns the status, doorbell, dog, peer and radio state, reacts to
// touches and radio messages, and builds a plain View for the interface to draw.
// Plain C++: everything hardware-shaped goes through Platform, so the same code runs on the panel,
// in the host tests, and in the screenshot simulator.
#pragma once
#include "../core/types.h"
#include "../core/settings.h"
#include "../core/status.h"
#include "../core/dog.h"
#include "../core/mail.h"
#include "../core/comfort.h"
#include "../core/requests.h"
#include "../core/comfort_chart.h"
#include "../core/note.h"
#include "../core/outside_log.h"
#include "../core/bell.h"
#include "../core/protocol.h"
#include "../core/sky.h"
#include "../core/events.h"
#include "../core/holidays.h"
#include "../core/presence.h"
#include <stdlib.h>

namespace tc {

class Platform {
 public:
  virtual ~Platform() {}
  virtual uint32_t mono_ms() = 0;
  virtual WallTime wall() = 0;
  virtual void set_wall(uint32_t utc, uint8_t quality) = 0;  // also writes the RTC
  virtual int32_t utc_offset(uint32_t utc) = 0;               // time-zone rules live in the platform
  // One try. `to` is a protocol sender id: the other panel, or the gateway (ACKs only).
  virtual void radio_send(uint8_t to, const uint8_t *data, size_t len) = 0;
  virtual void play(Sound s, uint8_t volume) = 0;
  // A sample from Settings (volume, ringtone): may be cut short by the next sound.
  virtual void preview(Sound s, uint8_t volume) { play(s, volume); }
  virtual void set_backlight(uint8_t reg) = 0;
  virtual void save_own(const OwnState &) {}
  virtual void save_dog(const DogState &) {}
  virtual void save_mail(const MailState &) {}
  virtual void save_settings(const Settings &) {}
  // The comfort survey (comfort.h): how many answers this panel has, kept across restarts
  // (downstairs: answers given; upstairs: the highest answer number copied here).
  virtual void save_comfort_count(uint32_t) {}
  virtual void save_requests(const RequestState &) {}  // the heat and cooling arrows (requests.h)
  // The comfort answers and removals still waiting for upstairs (oldest first), saved on every
  // change so a restart keeps them (comfort.h).
  // The two floors' tile messages (note.h), [0] upstairs, [1] downstairs: saved on every change.
  virtual void save_notes(const NoteState *notes2) { (void)notes2; }
  virtual void save_comfort_pending(const ComfortPending *p, int n) {
    (void)p;
    (void)n;
  }
  // In the file's line whose first column is n, replaces column `column` (0-based) with value.
  // Queued in order with append_line (an edit never overtakes a line still waiting) and written in
  // the background through a temporary file, so a power cut keeps the old file. For removing a
  // comfort answer (comfort_chart.h).
  virtual void set_csv_field(const char *name, uint32_t n, int column, const char *value) {
    (void)name;
    (void)n;
    (void)column;
    (void)value;
  }
  // Adds one line to a file in the calendar's folder, writing header first when the file is new or
  // empty. Both already end in a line end and are written as they are. Queued like write_file and
  // written in the background; nothing is read back.
  virtual void append_line(const char *name, const char *header, const char *line) {
    (void)name;
    (void)header;
    (void)line;
  }
  virtual uint32_t random32() = 0;
  virtual void log(const char *) {}
  // Large buffers (the calendar's). The panel gives PSRAM.
  virtual void *alloc_big(size_t n) { return malloc(n); }
  virtual void free_big(void *p) { free(p); }
  // The calendar's files: "calendar" (the store: upcoming, repeating and recent events) and the
  // archive's months ("2026-10") and "series". read_file returns a file's bytes in a buffer from
  // alloc_big (the caller frees it), or null if there's none. write_file takes buf (from
  // alloc_big) and frees it once written: the panel writes in the background, to the SD card, and
  // read_file returns what's still waiting to be written.
  virtual uint8_t *read_file(const char *name, size_t &len) {
    (void)name;
    len = 0;
    return nullptr;
  }
  virtual void write_file(const char *name, uint8_t *buf, size_t len) {
    (void)name;
    (void)len;
    free_big(buf);
  }
  // Display tests (Settings > Test), on the panel only: a label for the current choice, and the
  // next choice. strip_drawing: draw the artwork through internal RAM.
  virtual void display_test_label(char *out, size_t n) {
    if (n) out[0] = 0;
  }
  virtual void display_test_next() {}
  virtual bool strip_drawing() { return false; }
  virtual void toggle_strip_drawing() {}
  // Wi-Fi updates (docs/wifi-updates.md), on the panel only: whether the updater is installed, and
  // restarting into it.
  virtual bool updater_installed() { return false; }
  virtual void start_updater() {}
  // The presence sensor (presence.h), upstairs only: true while someone is there, debounced by the
  // panel. False on a panel with no sensor. Read once a tick, only when Settings says it is fitted.
  virtual bool presence() { return false; }
};

// ---- What the interface draws. Plain data, rebuilt by Controller::tick(). ----

struct FloorView {
  Floor floor;
  bool mine;
  bool no_signal;       // gray NO SIGNAL tile (also used before the other panel is first heard)
  Status status;        // colors the tile when !no_signal
  char word[12];        // "SLEEPING", "NO SIGNAL"
  char sub[48];         // No signal only: last known state and how long ago, "was BUSY, 17 min ago"
  char line[48];        // "since 10:15 PM", "until 7:10 PM", "last seen 7:08 PM"
  bool timer;           // line shows "until"
  // The floor's tile message (note.h, Mike 2026-11-07): when set, it shows instead of `line`, in
  // up to two lines (the screen wraps it). Downstairs' view of the UPSTAIRS tile shows "Action taken
  // on temperature" (RequestView acted) in that spot instead while it shows.
  bool note;
  char note_text[kNoteTextMax + 1];
  // Today's next calendar item for this floor (Mike, 2026-11-11), the lowest-priority line: the
  // interface shows it in place of `line` only when no "Action taken on temperature" and no tile
  // message is using the spot ("Dentist 2:30 PM"; an all-day event has no `agenda_when`). The
  // controller only sets it where `line` is a plain "since" line (not a timer's "until").
  // UPSTAIRS tile, sensor fitted (Mike, 2026-11-11): he is Busy but walking about the house, not at
  // his desk. `line` then reads "Away from desk" (below a tile message and the acted confirmation,
  // above the calendar item; a timer's "until" line is kept).
  bool away_desk;
  bool agenda;
  char agenda_title[kEventTitleMax + 1];
  char agenda_when[12];
  char temp[12];        // "69°F" or "--°F"
  char hum[12];         // "44%" or "--%"
};

struct OutsideView {
  bool valid;
  Forecast code;
  bool heat, cold;  // WeatherGate's danger heat and danger cold
  int16_t temp_tenths;  // the reading as numbers (the screensaver's weather): tenths of a degree F
  uint8_t hum_pct;      // and percent; both 0 when !valid
  char temp[12];
  char hum[12];
  char phrase[kMaxPhrase + 1];
};

struct BellView {
  BellOut out;
  bool ignoring;        // repeat taps are being ignored
  char line[24];        // "Ringing", "Delivered", "Come on", "Not now", "No answer yet", "Couldn't reach"
  char missed[32];      // "Missed 7:05 PM" or empty
  // Mike, 2026-10-26: the bell shakes for 3 s each time a ring goes out or comes in, and an
  // incoming ring turns it red until it's answered (or the banner dismissed), for 2 min at most.
  uint16_t shakes;      // one more for each ring sent or received
  bool alert;           // an incoming ring not yet answered: the bell shows red
};

struct DogView {
  Dog out;
  bool can_tap;
  // "COPPER OUT", "BOTH DOGS OUT"; once a dog has been out too long (Mike, 2026-11-11, dog_nudge):
  // "LUCY OUT 25 min, 18°F", or "BOTH OUT 25 min, 18°F" when both are. The temperature is left off
  // when there's no outside reading or no clock.
  char pill[40];
  bool alert;           // a dog is out too long: the pill pulses
};

// The mail badge on the DOWNSTAIRS tile (Mike, 2026-10-26). Upstairs it's always there: a faint
// outline to tap when the tenant has mail, solid while the mail waits. Downstairs it shows only
// while mail waits: solid, slowly fading between solid and outline until the tenant taps it.
struct MailView {
  bool show;
  bool solid;
  bool pulse;
};

// The heat and cooling arrows at the top right of the UPSTAIRS tile, on both panels (Mike,
// 2026-11-06, requests.h), and the downstairs panel's "Action taken on temperature" line.
struct RequestView {
  uint8_t n;                       // arrows showing, 0-3, oldest first: the newest is drawn on the right
  Ask ask[kMaxArrows];
  ReqSource source[kMaxArrows];
  bool flash;                      // an urgent arrow is showing: urgent arrows flash (kFlashHalfMs)
  bool acted;                      // downstairs: "Action taken on temperature" replaces the UPSTAIRS since line
};

struct BannerView {
  BannerKind kind;
  char title[40];       // "Doorbell from upstairs"
  char sub[64];         // "Rang at 3:05 PM while you were away"
};

struct View {
  Floor me;
  Mode mode;
  Phase phase;
  float disc_p;
  bool moon;
  bool clock_valid;
  char date[32];        // "Monday, Oct. 12" or "Clock not set"
  char time[12];        // "6:10 PM"
  // The date strip's messages for today, at its right end: a federal holiday's, a birthday's,
  // an anniversary's ("Happy Thanksgiving", "Happy Birthday, Sam"). The interface takes turns
  // when there's more than one, and uses short_text when text doesn't fit.
  struct StripMsg {
    char text[64];
    char short_text[44];
    DayIcon icon;
  } strip[3];
  uint8_t nstrip;
  FloorView floor[2];   // index by (int)Floor
  OutsideView outside;
  BellView bell;
  DogView dog;
  MailView mail;
  RequestView req;
  BannerView banner;
  // The tenant's toast, while a ring waits, may read "No one is near the panel" (the presence sensor,
  // upstairs: nobody near for 15 minutes; Mike, 2026-11-11). The tile has no room for a second line.
  char toast[48];       // brief notice, e.g. a timer ending; empty when none
  uint8_t backlight;
  // True for 3 seconds each time someone walks up to the UPSTAIRS panel's sensor after the place was
  // empty (at most once in 2 minutes; presence.h). Always false downstairs and with no sensor. The
  // screensaver's hiker waves at it.
  bool passer_by;
  // The presence sensor, for Settings and the serial `presence` command (upstairs only).
  struct PresenceView {
    bool fitted;          // Settings says a sensor is wired up (and this is the upstairs panel)
    bool seen_now;        // the debounced input says someone is there
    bool occupied;        // seen now, or within the hold time of the last time
    int32_t since_min;    // minutes since it last saw anyone; -1 = not since this start
  } presence;
};

class Controller : public CalendarSource {
 public:
  explicit Controller(Platform &p) : p_(p) {}
  ~Controller();  // the demo's simulated panel is rebuilt on each "plug in"
  Controller(const Controller &) = delete;
  Controller &operator=(const Controller &) = delete;

  void begin(const Settings &s, const OwnState &own, const DogState &dog, const MailState &mail = MailState());
  void tick();                                   // call every 20-100 ms
  void on_radio(const uint8_t *data, size_t len);
  void on_local_reading(bool valid, int16_t temp_tenths_f, uint16_t humidity_tenths);

  // Touch intents
  void touch();                                  // any touch, for the backlight
  bool choose_status(Status s);
  bool start_timer(Status s, uint32_t duration_s, Status fallback);
  void tap_bell();
  bool tap_dog();
  // The mail badge: upstairs marks mail for downstairs, or takes it back; downstairs acknowledges
  // it. False when there was nothing to do (downstairs, with no mail waiting).
  bool tap_mail();
  // The comfort survey (comfort.h), downstairs only: the tenant's answer, saved with the readings
  // and the time, and copied to upstairs. With sent == Send and an arrow, the arrow goes upstairs
  // too (requests.h). False on the upstairs panel.
  bool comfort_answer(Feel f, Ask ask = Ask::None, AskSent sent = AskSent::Blank);
  // A tap on the arrows: upstairs, Mike has acted and they all go; downstairs, the tenant takes
  // them back. False when none were showing.
  bool tap_requests();
  // Downstairs: a tap on "Action taken on temperature" puts it away.
  bool tap_acted();
  // The comfort chart (comfort_chart.h), downstairs only: the tenant removes answer n given by
  // mistake. It's marked removed (the time) in this panel's comfort.csv and upstairs'. False on the
  // upstairs panel or for n 0 or beyond the answers here.
  bool comfort_remove(uint32_t n);
  // At start-up: what was still waiting for upstairs when the panel stopped (save_comfort_pending).
  void restore_comfort_pending(const ComfortPending *p, int n);
  // Settings > Update over Wi-Fi > Both panels (Mike, 2026-11-07): asks the other panel to start
  // its updater too (an UPDATE message), then starts this panel's once the other has answered or
  // after a short wait. False if this panel has no updater.
  bool update_both();
  // This panel's own tile message (note.h): written from the keyboard, or taken away with a tap on
  // it. set_note with empty text clears. False when nothing changed.
  bool set_note(const char *text, uint32_t duration_s);
  bool clear_note();
  void restore_notes(const NoteState *notes2);  // at start-up: [0] upstairs, [1] downstairs
  const NoteState &note(Floor f) const { return notes_[(int)f].state(); }
  void restore_requests(const RequestState &s) {
    req_.restore(s);
    req_snapshot(true);
  }
  const RequestState &requests() const { return req_.state(); }
  // The number of answers this panel has (see Platform::save_comfort_count), restored at start-up.
  void set_comfort_count(uint32_t n) { comfort_count_ = n; }
  uint32_t comfort_count() const { return comfort_count_; }
  void answer_banner(bool come_on);
  void dismiss_banner();
  void apply_settings(const Settings &s);
  void set_clock_manual(uint32_t utc);
  // Which house events the upstairs panel reports to the Pi (NoticeKind, one bit each; protocol.h).
  // Kept in the saved Settings (version 6), so it survives a restart; a Settings page for it is a
  // follow-up.
  uint8_t notice_kinds() const { return settings_.notice_mask; }
  void set_notice_kinds(uint8_t mask) {
    settings_.notice_mask = (uint8_t)(mask & ((1u << kNoticeKinds) - 1));
    p_.save_settings(settings_);
  }
  void set_clean_screen(bool on) { clean_screen_ = on; }

  // The household calendar (calendar roll-down spec). Every change goes to the other panel and,
  // a moment later, to the SD card. Events long past are in the archive (events.h).
  const EventStore &events() const { return events_; }
  int range(int32_t d0, int32_t d1, Occ *out, int max) override;
  const Event *find(uint32_t id) override;
  uint32_t rev() const override { return events_.rev() + arch_rev_; }
  // A new event (e.id 0) gets this panel's floor with no prompt (spec 1.1); an edited one keeps
  // its own. Returns the event's id, or 0 if the calendar is full or the event has gone.
  uint32_t save_event(const Event &e);
  bool delete_event(uint32_t id);
  bool restore_event(uint32_t id);  // Undo after a delete
  // One occurrence of a series on its own (Mike, 2026-10-25, "just this one"): changed (it
  // becomes an event of its own, with the series' floor; returns its id) or deleted, and Undo.
  uint32_t save_occurrence(uint32_t series, int32_t day, const Event &e);
  bool delete_occurrence(uint32_t series, int32_t day);
  bool restore_occurrence(uint32_t series, int32_t day);
  int32_t today();                  // local day number, -1 while the clock isn't set

  const View &view() const { return view_; }
  const Settings &settings() const { return settings_; }
  const OwnState &own() const { return status_.state(); }
  const DogState &dog() const { return dog_.state(); }
  const MailState &mail() const { return mail_.state(); }
  bool peer_no_signal() const { return peer_.no_signal; }

  // Testing and demo: force the sky phase instead of the clock's (Phase count = off).
  void force_phase(int phase) { forced_phase_ = phase; }

 private:
  struct Peer {
    bool known = false;
    bool no_signal = false;
    OwnState st;
    uint32_t heard_utc = 0;
    bool reading = false;
    int16_t temp = 0;
    uint16_t hum = 0;
    bool away = false;         // STATE's kStateAwayDesk: upstairs is Busy and walking about
    bool nobody_near = false;  // STATE's kStateNobodyNear: no one near the upstairs panel for 15 min
  };
  struct OutsideData {
    bool have = false;
    uint32_t got_mono = 0;
    int16_t temp = 0;
    uint8_t hum = 0;
    uint8_t code = 0;
    uint8_t flags = 0;  // the OUTSIDE flags it came with
    uint32_t time = 0;
    char phrase[kMaxPhrase + 1] = {};
  };

  bool send(Msg m);  // false: the queue was full and it was dropped
  void send_state(bool please_reply);
  void send_dog();
  void send_mail();
  void send_note(Floor f);  // a floor's tile message (note.h)
  void send_notes();        // both floors' messages that have ever been written (boot, after an outage)
  void tick_outside_log(uint32_t now, const WallTime &w);
  void save_notes();
  void send_comfort();  // top up the radio from the outbox
  void comfort_out_add(const ComfortAnswer &a, bool del);
  // The heat and cooling arrows (requests.h). Downstairs writes requests.csv as arrows end.
  void send_requests();
  void req_log(const ReqArrow &a, ReqEnd why, uint32_t ended_utc);
  void req_snapshot(bool raw);  // what showed at the last change, to notice an arrow timing out
  void req_age_log();
  void req_ding();
  void tick_requests(uint32_t now);
  uint8_t soft_volume();
  void mail_alert();
  void send_reading();
  void send_outside();
  void send_event(const Event &e);
  // The archive: a month's file (or "series"), read once and kept in a few slots.
  struct ArchFile {
    char name[8] = "";
    Event *ev = nullptr;
    int n = 0;
    uint32_t used = 0;
  };
  ArchFile *archive_file(const char *name);
  void archive_forget(const char *name);
  bool archive_put(const char *name, const Event *ev, int n);  // merged into that file
  void archive_old(bool make_room);                           // old events out of the store
  const Event *make_active(uint32_t id);                      // an archived event back, to change it
  uint8_t me() const { return sender_id(settings_.floor); }
  void send_sum(bool please_reply);
  void events_changed();
  void tick_events(uint32_t now, const WallTime &w);
  void own_changed(Status before);
  void handle(const Msg &m);
  void heard_from_peer();
  void build_view();
  void fill_floor(FloorView &fv, bool mine);
  void update_agenda(const WallTime &w);  // refreshes agenda_[] when the minute, day or calendar changed
  // House events for the Pi (Mike, 2026-11-11), upstairs only: queued for the gateway, lowest
  // priority, dropped quietly if it doesn't ACK. time 0 = use the clock now (0 if it isn't set).
  void raise_notice(NoticeKind k, Floor floor, int16_t value, uint32_t time, const char *text = "");
  void tick_notices();  // the doorbell's unanswered rings, from the state held (edges between ticks)
  void tick_dog_nudge(uint32_t now, const WallTime &w);  // dog out too long: pill text, pulse, one chime
  // The presence sensor (presence.h, Mike 2026-11-11), upstairs and only when Settings says it is fitted.
  void tick_presence(uint32_t now);
  bool away_now() const { return away_ && status_.state().status == Status::Busy; }
  uint8_t presence_flags() const;  // STATE's kStateAwayDesk and kStateNobodyNear for now
  uint8_t current_volume();
  int32_t offset_now();
  void format_clock(char *out, size_t n, uint32_t utc);
  void maybe_copy_clock(const Msg &m);

  Platform &p_;
  Settings settings_;
  StatusModel status_;
  DogModel dog_;
  MailModel mail_;
  RequestModel req_;
  NoteModel notes_[2];  // [0] upstairs, [1] downstairs (note.h)
  BellSender bell_out_;
  BellReceiver bell_in_;
  RetryQueue retry_;
  Dedup dedup_;
  Peer peer_;
  OutsideData outside_;
  // A time from the gateway far behind our clock is first left alone; see the OUTSIDE handler.
  static constexpr int kBackNeeded = 3;          // readings in a row that agree with each other
  static constexpr int64_t kBackSlackS = 5;      // how far one may be off the elapsed time
  static constexpr int32_t kBackMinGapMs = 20000;  // closer than this is a copy, not a new reading
  uint32_t back_time_ = 0;  // the last time left alone
  uint32_t back_mono_ = 0;  // and when it came
  int back_n_ = 0;          // how many in a row have agreed
  View view_ = {};

  bool clock_was_valid_ = false;
  bool want_peer_state_ = true;  // keep asking for the other panel's STATE until one arrives
  uint8_t last_backlight_ = 0;
  bool backlight_set_ = false;
  bool last_missed_ = false;
  uint32_t last_touch_ = 0;
  bool touched_ = false;
  bool mail_wake_ = false;  // new mail woke the screen (downstairs), for touch_wake_ms
  uint32_t mail_wake_at_ = 0;
  bool clean_screen_ = false;
  uint16_t next_knock_ = 1;
  uint16_t bell_shakes_ = 0;
  bool ring_alert_ = false;  // an incoming ring not yet answered
  uint32_t ring_alert_at_ = 0;
  bool reading_valid_ = false;
  int16_t raw_temp_ = 0;      // as the sensor reported it
  int16_t reading_temp_ = 0;  // with the Settings offset applied: shown and sent
  uint16_t reading_hum_ = 0;
  bool reading_sent_ = false;
  bool sent_valid_ = false;
  int16_t sent_temp_ = 0;
  uint16_t sent_hum_ = 0;
  char toast_[48] = {};
  uint32_t toast_until_ = 0;
  uint32_t missed_mono_ = 0;
  uint32_t comfort_count_ = 0;
  // Answers downstairs not yet ACKed by upstairs, oldest first. seq is its message in the retry
  // queue while one is in flight, else 0. Kept when the tries run out; sent again when the other
  // panel is heard.
  static constexpr int kComfortOutbox = 32;
  // One at a time: upstairs keeps only a running count, so answer n+1 arriving before a lost answer n
  // would make it ignore n's retry for good. Each goes only after the ACK of the one before it.
  static constexpr int kComfortInFlight = 1;
  // del: this entry is a removal (COMFORT_DEL; only a.n, a.removed and a.offset are used). Answers and
  // removals share the outbox so a removal never overtakes its own answer (Mike, 2026-11-06).
  struct ComfortOut { ComfortAnswer a; uint16_t seq; bool del; };
  ComfortOut comfort_out_[kComfortOutbox] = {};
  int comfort_out_n_ = 0;
  // Saves the outbox through Platform::save_comfort_pending, on every change (Mike, 2026-11-07).
  void comfort_out_save();
  void tick_update(uint32_t now);
  // Settings > Update over Wi-Fi > Both panels (Mike, 2026-11-07). both_wait_: this panel's updater
  // starts when UPDATE is ACKed (both_seq_) or at both_at_, 2 s after it was sent. update_wait_: a
  // received UPDATE starts it at update_at_, about 700 ms later so the ACK gets out first.
  static constexpr uint32_t kBothWaitMs = 2000;
  static constexpr uint32_t kUpdateDelayMs = 700;
  bool both_wait_ = false;
  uint16_t both_seq_ = 0;
  uint32_t both_at_ = 0;
  uint32_t both_sent_ = 0;   // when UPDATE went out: once ACKed, this panel starts kUpdateDelayMs after it
  bool both_acked_ = false;
  bool update_wait_ = false;
  uint32_t update_at_ = 0;
  bool outside_fresh(uint32_t now) const;
  // The quarter-hour slot (local seconds / 900) of the last outside reading written to the SD card,
  // so a slot never gets two lines (Mike, 2026-11-07). RAM only; 0 = none yet.
  uint32_t outside_slot_ = 0;
  uint32_t started_ms_ = 0;  // begin(): a restart inside a quarter's first minute skips that quarter
  int forced_phase_ = 6;
  ReqArrow req_prev_[kMaxArrows] = {};
  int req_prev_n_ = 0;
  uint32_t req_prev_ver_ = 0;
  bool req_urgent_ = false;   // upstairs: an urgent arrow was showing at the last tick
  uint32_t req_ding_at_ = 0;

  // Each floor's next calendar item today (Mike, 2026-11-11), worked out once a minute or when the
  // calendar changes, not on every tick. Shared keys: both floors come from one pass over the day.
  struct Agenda {
    bool on = false;
    char title[kEventTitleMax + 1] = "";
    char when[12] = "";  // "2:30 PM"; empty for an all-day event
  };
  Agenda agenda_[2];  // by Floor
  int32_t agenda_day_ = -1, agenda_min_ = -1;
  uint32_t agenda_rev_ = 0;

  // Dog out too long (Mike, 2026-11-11). Per dog: whether it was out at the last tick and when this
  // panel first saw it out (for a missing clock), the minutes it has been out past the limit
  // (0 = fine), and whether this outing has had its chime. Every panel works it out for itself
  // from the same data, so no message is needed.
  struct DogNudge {
    bool seen_out = false;
    bool chimed = false;
    bool noticed = false;  // the Pi has been told about this outing
    uint32_t out_mono = 0;
    uint16_t over_min = 0;  // minutes out, only while past the limit; else 0
  };
  DogNudge nudge_[2];
  // Presence (upstairs, sensor fitted). pres_on_: the state below is live. near_ms_: the last time
  // anyone was near the panel (the sensor or a touch). away_: activity was seen while Busy and
  // kAwayClearMs have not passed since the last. occupied_: seen now or within the hold time.
  bool pres_on_ = false;
  bool pres_now_ = false;
  bool pres_seen_ = false;
  uint32_t pres_last_ms_ = 0;
  uint32_t near_ms_ = 0;
  bool away_ = false;
  bool nobody_near_ = false;
  bool occupied_ = false;
  bool passer_on_ = false;
  bool passer_ever_ = false;
  uint32_t passer_until_ = 0;
  uint32_t passer_last_ = 0;
  uint8_t sent_pres_flags_ = 0;  // what the last STATE carried
  // Downstairs: the last ring was sent while upstairs said nobody was near its panel, and when.
  bool knock_near_note_ = false;
  uint32_t knock_near_at_ = 0;
  BellOut notice_bell_out_ = BellOut::Idle;  // the doorbell's state at the last tick
  BannerKind notice_banner_ = BannerKind::None;
  bool ring_resolved_ = false;  // the banner was answered or dismissed (not left to run out)
  bool nudge_temp_ = false;  // the pill shows the outside temperature
  int16_t nudge_tenths_ = 0;

  EventStore events_;
  bool ev_dirty_ = false;
  uint32_t ev_save_at_ = 0;
  int ev_stream_ = -1;            // next slot to send while syncing with the other panel; -1 idle
  bool ev_resync_ = false;        // a calendar change was dropped (queue full): compare when there's room
  int32_t ev_archived_day_ = -1;
  ArchFile arch_[6];              // a month view needs three months and "series" at once
  uint32_t arch_rev_ = 0, arch_tick_ = 0;
};

const char *status_word(Status s);   // "OPEN"
const char *status_title(Status s);  // "Open"
const char *floor_word(Floor f);     // "UPSTAIRS"
const char *floor_lower(Floor f);    // "upstairs"
const char *dog_name(Dog d);         // "LUCY", "BOTH DOGS"

// Dog out too long (Mike, 2026-11-11): minutes a dog may be out before the pill shows the time and
// the outside temperature, pulses, and the panel chimes once. Shorter when it's harsh outside.
constexpr int kDogOutMinNormal = 30;   // no valid outside reading or no clock: elapsed time alone
constexpr int kDogOutMinExtreme = 15;  // outside below kDogColdTenthsF or above kDogHotTenthsF
constexpr int kDogOutMinDanger = 10;   // WeatherGate's danger heat or danger cold
constexpr int kDogColdTenthsF = 320;   // 32.0 F: colder than this is "extreme"
constexpr int kDogHotTenthsF = 880;    // 88.0 F: hotter than this is "extreme"

}  // namespace tc
