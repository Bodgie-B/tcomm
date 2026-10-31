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
#include "../core/bell.h"
#include "../core/protocol.h"
#include "../core/sky.h"
#include "../core/events.h"
#include "../core/holidays.h"
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
  char temp[12];        // "69°F" or "--°F"
  char hum[12];         // "44%" or "--%"
};

struct OutsideView {
  bool valid;
  Forecast code;
  bool heat, cold;  // WeatherGate's danger heat and danger cold
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
  char pill[16];        // "COPPER OUT", "BOTH DOGS OUT"
};

// The mail badge on the DOWNSTAIRS tile (Mike, 2026-10-26). Upstairs it's always there: a faint
// outline to tap when the tenant has mail, solid while the mail waits. Downstairs it shows only
// while mail waits: solid, slowly fading between solid and outline until the tenant taps it.
struct MailView {
  bool show;
  bool solid;
  bool pulse;
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
  BannerView banner;
  char toast[48];       // brief notice, e.g. a timer ending; empty when none
  uint8_t backlight;
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
  void answer_banner(bool come_on);
  void dismiss_banner();
  void apply_settings(const Settings &s);
  void set_clock_manual(uint32_t utc);
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
  uint8_t current_volume();
  int32_t offset_now();
  void format_clock(char *out, size_t n, uint32_t utc);
  void maybe_copy_clock(const Msg &m);

  Platform &p_;
  Settings settings_;
  StatusModel status_;
  DogModel dog_;
  MailModel mail_;
  BellSender bell_out_;
  BellReceiver bell_in_;
  RetryQueue retry_;
  Dedup dedup_;
  Peer peer_;
  OutsideData outside_;
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
  bool outside_fresh(uint32_t now) const;
  int forced_phase_ = 6;

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

}  // namespace tc
