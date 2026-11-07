// tc::Platform on the panel: millis, the system clock backed by the RTC, POSIX time-zone rules,
// ESP-NOW (or the demo loopback), the audio task, the backlight and NVS.
#pragma once
#include "../app/controller.h"
#include "../app/demo.h"

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
  void save_requests(const tc::RequestState &s) override;
  void append_line(const char *name, const char *header, const char *line) override;
  void set_csv_field(const char *name, uint32_t n, int column, const char *value) override;
  void *alloc_big(size_t n) override;
  void free_big(void *p) override { free(p); }  // heap_caps memory too
  uint8_t *read_file(const char *name, size_t &len) override;
  void write_file(const char *name, uint8_t *buf, size_t len) override;
  uint32_t random32() override;
  void log(const char *line) override;

  // Saves are queued here and written by flush_saves() from the Arduino loop task: a flash write
  // stalls both cores' cache, and in the LVGL task it would also delay touch and radio ACKs.
  void flush_saves();

 private:
  void apply_tz(const char *tz);
  enum : uint8_t {
    kDirtyOwn = 1,
    kDirtyDog = 2,
    kDirtySettings = 4,
    kDirtyClock = 8,
    kDirtyMail = 16,
    kDirtyComfort = 32,
    kDirtyRequests = 64
  };
  volatile uint8_t dirty_ = 0;
  tc::OwnState own_;
  tc::DogState dog_;
  tc::MailState mail_;
  tc::Settings settings_;
  uint32_t comfort_n_ = 0;
  tc::RequestState requests_ = {};
  uint8_t clock_q_ = 0;
  // The calendar's files waiting to be written by the loop task, oldest first. An entry stays while
  // it's being written, so read_file can still answer from it; a newer one for the same file
  // replaces a waiting one. Guarded by files_lock_ (a mutex: reads copy the buffer).
  struct PendingFile {
    char name[12];
    uint8_t *buf;
    size_t len;
    bool writing;
  };
  PendingFile pend_[16] = {};
  int npend_ = 0;
  // Lines waiting to be appended, and fields waiting to be set (the comfort survey), in order; same
  // lock. Names are at most 11 characters, the header and line (or the value) at most kComfortLineMax.
  struct PendingLine {
    char name[12];
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
  bool valid_ = false;
  uint8_t quality_ = tc::kClockNone;
  tc::Demo *demo_ = nullptr;
};

}  // namespace board
