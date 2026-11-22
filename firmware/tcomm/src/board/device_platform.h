// tc::Platform on the panel: millis, the system clock backed by the RTC, POSIX time-zone rules,
// ESP-NOW (or the demo loopback), the audio task, the backlight and NVS.
#pragma once
#include "../app/controller.h"
#include "../app/demo.h"
#include "../core/outside_log.h"  // kOutsideNameLen: the outside day files' names
#include "board.h"                 // the voice capture hooks

namespace board {

class DevicePlatform : public tc::Platform {
 public:
  void begin(const tc::Settings &s);  // time zone, and the clock from the RTC if it kept time
  void set_demo(tc::Demo *d) { demo_ = d; }

  uint32_t mono_ms() override;
  tc::WallTime wall() override;
  void set_wall(uint32_t utc, uint8_t quality) override;
  int32_t utc_offset(uint32_t utc) override;
  void radio_send(uint8_t to, const uint8_t *data, size_t len) override;
  void play(tc::Sound s, uint8_t volume) override;
  void preview(tc::Sound s, uint8_t volume) override;
  void set_backlight(uint8_t reg) override;
  void save_own(const tc::OwnState &s) override;
  void save_dog(const tc::DogState &d) override;
  void save_mail(const tc::MailState &m) override;
  void save_settings(const tc::Settings &s) override;
  void save_comfort_count(uint32_t n) override;
  void save_comfort_pending(const tc::ComfortPending *p, int n) override;
  void save_requests(const tc::RequestState &s) override;
  void save_notes(const tc::NoteState *notes2) override;
  void append_line(const char *name, const char *header, const char *line) override;
  void set_csv_field(const char *name, uint32_t n, int column, const char *value) override;
  void *alloc_big(size_t n) override;
  void free_big(void *p) override { free(p); }  // heap_caps memory too
  // The controller's Store (about 3.7 KB): PSRAM like alloc_big (field fix 2026-11-20). Called while
  // g_ctrl is constructed, before setup(); ESP-IDF adds PSRAM to the heap before the constructors run.
  void *alloc_state(size_t n) override { return DevicePlatform::alloc_big(n); }
  void free_state(void *p) override { free(p); }
  uint8_t *read_file(const char *name, size_t &len) override;
  void write_file(const char *name, uint8_t *buf, size_t len) override;
  uint32_t random32() override;
  void log(const char *line) override;
  // Voice notes (docs/voice-notes/DESIGN.md; piece B): ESP-NOW voice frames (never through the demo
  // loopback), the capture handoff (board_audio.cpp, piece A) and the arrival inbox (NVS, via flush_saves).
  bool radio_send_voice(uint8_t to, const uint8_t *data, size_t len) override;
  bool voice_rec_begin(tc::VoiceRec *rec) override { return board::voice_rec_begin(rec); }
  void voice_rec_stop(tc::VoiceRec *rec, tc::VoiceStop why) override { board::voice_rec_stop(rec, why); }
  bool voice_rec_release(tc::VoiceRec *rec) override { return board::voice_rec_release(rec); }
  bool mic_meter(float &peak_db) override {
    float rms = -99.0f;
    return board::audio_mic_recent(200, peak_db, rms);
  }
  void save_voice_inbox(const tc::VoiceInboxData &d) override;
  bool load_voice_inbox(tc::VoiceInboxData &d) override;

  // The presence sensor (core/presence.h). The loop task reads the input and calls set_presence with
  // the debounced state (and the raw pin level, -1 when not read); the controller asks presence() from
  // the LVGL task. Single bytes, so no lock. The serial `presence sim` overrides the sensor for tests.
  bool presence() override { return pres_sim_ >= 0 ? pres_sim_ != 0 : pres_; }
  void set_presence(bool debounced, int raw_level) {
    pres_ = debounced;
    pres_raw_ = (int8_t)raw_level;
  }
  void set_presence_sim(int v) { pres_sim_ = (int8_t)v; }  // -1 off, 0 absent, 1 present
  int presence_sim() const { return pres_sim_; }
  int presence_raw() const { return pres_raw_; }  // 0 or 1; -1 not read yet (or not fitted)
  bool presence_debounced() const { return pres_; }

  // Saves are queued here and written by flush_saves() from the Arduino loop task: a flash write
  // stalls both cores' cache, and in the LVGL task it would also delay touch and radio ACKs.
  void flush_saves();

 private:
  void apply_tz(const char *tz);
  // 16 bits: the eight flags above are used up (kDirtyNotes is the ninth; Mike, 2026-11-07).
  enum : uint16_t {
    kDirtyOwn = 1,
    kDirtyDog = 2,
    kDirtySettings = 4,
    kDirtyClock = 8,
    kDirtyMail = 16,
    kDirtyComfort = 32,
    kDirtyRequests = 64,
    kDirtyPending = 128,  // the comfort outbox (Mike, 2026-11-07)
    kDirtyNotes = 256,    // the tile messages (note.h, Mike 2026-11-07)
    kDirtyVoice = 512     // the voice-message inbox (voice_inbox.h)
  };
  volatile uint16_t dirty_ = 0;
  tc::NoteState notes_[2] = {};  // [0] upstairs, [1] downstairs
  // The voice inbox: [0] waiting for flush_saves, [1] its copy outside the lock. About 1.1 KB, made in
  // PSRAM at the first save (field fix 2026-11-20: no longer in internal RAM).
  tc::VoiceInboxData *voice_box_ = nullptr;
  tc::OwnState own_;
  tc::DogState dog_;
  tc::MailState mail_;
  tc::Settings settings_;
  uint32_t comfort_n_ = 0;
  tc::ComfortPending pending_[tc::kComfortPendingMax] = {};
  int npending_ = 0;
  tc::RequestState requests_ = {};
  uint8_t clock_q_ = 0;
  // The calendar's files waiting to be written by the loop task, oldest first. An entry stays while
  // it's being written, so read_file can still answer from it; a newer one for the same file
  // replaces a waiting one. Guarded by files_lock_ (a mutex: reads copy the buffer).
  struct PendingFile {
    char name[tc::kOutsideNameLen];  // "o20261107.csv" is 13 characters (Mike, 2026-11-07)
    uint8_t *buf;
    size_t len;
    bool writing;
  };
  PendingFile pend_[16] = {};
  int npend_ = 0;
  // Lines waiting to be appended, and fields waiting to be set (the comfort survey), in order; same
  // lock. Names are shorter than tc::kOutsideNameLen, the header and line (or the value) at most kComfortLineMax.
  struct PendingLine {
    char name[tc::kOutsideNameLen];
    bool set;        // false: append line; true: set column `column` of the line whose first column is n
    uint32_t n;
    int column;
    char header[tc::kComfortLineMax];  // append only
    char line[tc::kComfortLineMax];    // the line to append, or the value to set
  };
  PendingLine lines_[16] = {};
  int nlines_ = 0;
  void queue_line(const PendingLine &l);
  void write_pending_line(const PendingLine &l);  // files lock held by the caller
  void *files_lock_ = nullptr;
  void lock_files();
  void unlock_files();
  volatile bool pres_ = false;
  volatile int8_t pres_raw_ = -1;
  volatile int8_t pres_sim_ = -1;
  bool valid_ = false;
  uint8_t quality_ = tc::kClockNone;
  tc::Demo *demo_ = nullptr;
};

}  // namespace board
