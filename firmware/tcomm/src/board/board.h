// Board layer for the Waveshare ESP32-S3-Touch-LCD-4.3C: everything that touches hardware, apart
// from Waveshare's display, touch and codec drivers (src/ws). Nothing here reboots on an I2C error:
// Waveshare's helpers use ESP_ERROR_CHECK, so after start-up only these functions talk on the bus.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../core/dog.h"
#include "../core/mail.h"
#include "../core/settings.h"
#include "../core/status.h"
#include "driver/i2c_master.h"

namespace board {

// ---- I2C: one bus (GPIO8/9), one lock around every transfer this firmware makes (handoff). ----
// GT911 touch and the ES8311 codec use their own handles on the same bus; ESP-IDF's i2c_master
// driver serializes individual transfers between tasks.
// Touch and the expander run at 400 kHz, the codecs and the SHT40 at 100. With the SHT40's cable
// attached, serial "i2c test" found no failed or wrong reads at either speed (2026-10-24).
constexpr uint32_t kI2cHz = 400000;
bool i2c_begin();
i2c_master_dev_handle_t i2c_device(uint8_t addr, uint32_t scl_hz = kI2cHz);
bool i2c_write(i2c_master_dev_handle_t dev, const uint8_t *data, size_t n);
bool i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t n);
bool i2c_receive(i2c_master_dev_handle_t dev, uint8_t *out, size_t n);  // a plain read, no register
bool i2c_probe(uint8_t addr);
uint32_t i2c_errors();  // failed transfers through the functions above, since start-up
// Serial "i2c test": reads known IDs from the codec and the touch controller many times at the
// given speed and prints how many transfers failed or came back wrong.
void i2c_test(uint32_t scl_hz, int tries);
class I2cLock {
 public:
  I2cLock();
  ~I2cLock();
  bool ok() const { return ok_; }

 private:
  bool ok_;
};

// ---- IO expander (CH32V003 at 0x24). Pins: EXIO2 panel+backlight enable, EXIO3 speaker amp. ----
constexpr uint8_t kExioDisp = 2, kExioAmp = 3, kExioSdCs = 4;
bool expander_begin();  // after Waveshare's IO_EXTENSION_Init and touch reset
bool expander_output(uint8_t pin, bool on);
bool expander_read(uint8_t reg, uint8_t &value);  // 0x04: the pins' levels
bool expander_backlight(uint8_t reg);  // PWM register: lower is brighter, 255 dark (measured 2026-10-21)

// ---- SHT40 temperature and humidity sensor (0x44) on the screw terminal: VOUT, GND, SDA, SCL. ----
// sht40_begin: true if a SHT40 answers with a valid serial number (it may be plugged in later, so
// it can be called again). sht40_measure: about 10 ms; the I2C lock is not held while it waits.
bool sht40_begin(uint32_t *serial = nullptr);
bool sht40_measure(int16_t &temp_tenths_f, uint16_t &hum_tenths);

// ---- PCF85063A clock. 2000-based year, 24 h, UTC. ----
struct RtcTime {
  bool valid;    // false when the oscillator-stop flag is set (power was lost with no coin cell)
  uint32_t utc;
};
RtcTime rtc_read();
bool rtc_write(uint32_t utc);

// ---- Saved settings and state (NVS via Preferences). ----
void storage_begin();
bool load_settings(tc::Settings &s);
void save_settings(const tc::Settings &s);
bool load_own(tc::OwnState &s);
void save_own(const tc::OwnState &s);
bool load_dog(tc::DogState &d);
void save_dog(const tc::DogState &d);
bool load_mail(tc::MailState &m);
void save_mail(const tc::MailState &m);
uint8_t load_clock_quality();
void save_clock_quality(uint8_t q);
// The calendar's files (tc::Platform::read_file names), in /tcomm on the SD card (Mike,
// 2026-10-25: rely on the card). Without a readable card they go on the 9.9 MB FAT partition
// instead, and when the card is readable again they're copied onto it. Each write goes to a new
// file that is then renamed over the old one, so a power cut mid-write keeps the old file.
// calendar_fs_begin: after expander_begin (the card's D3 line is on EXIO4).
void calendar_fs_begin();
uint8_t *calendar_read(const char *name, size_t &len);  // a PSRAM buffer to free(), or null
bool calendar_write(const char *name, const uint8_t *buf, size_t len);
struct CalendarFs {
  bool sd, fat;               // the card is in use; the FAT partition is mounted
  uint64_t card, total, used;  // bytes: the card's size, then the file system in use
  int files;                   // in /tcomm
};
bool calendar_fs_info(CalendarFs &out);
// Serial "sd": what the card is. "sd format yes": formats the card as FAT32, only if it can't be
// mounted as it is (an exFAT card can't: this core reads FAT only), then moves the calendar's
// files onto it. UNVERIFIED on the panel.
void sd_report();
bool sd_format_if_unreadable();
// The display test's choice (Settings > Test): pixel clock and copy-buffer preset, and whether
// the artwork is drawn in internal-RAM strips. 0xFF when never chosen.
uint8_t load_display_test();
void save_display_test(uint8_t v);

// ---- Audio: the synthesized sounds, played from their own task. ----
bool audio_begin(uint8_t ringtone);  // after Waveshare's codec_init()
// preview: a sample from Settings, cut short by whatever is asked for next.
void audio_play(tc::Sound s, uint8_t volume, bool preview = false);
void audio_set_ringtone(uint8_t ringtone);
void audio_report();  // one line on Serial: plays, errors, the ringtone held and chosen
// For tests over serial: what the microphones hear for ms (RMS of each, and the peak, in dBFS);
// whether the amp stays on between sounds; closing and reopening the codec.
bool audio_listen(uint32_t ms, float level_db[2], float &peak_db);
void audio_amp_hold(bool hold);
void audio_reopen_codec();
void audio_log_state();  // the amp pin and the codec's registers, into the event log

// ---- Event log: the last 64 events with their time, for serial "log" (board_log.cpp). ----
// Any task may call elog; only the loop task writes to USB serial (elog_flush, elog_print).
void elog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void elog_flush();  // new lines to serial, from loop()
void elog_print();

// ---- ESP-NOW (phase 3; off unless TCOMM_RADIO_ESPNOW). ----
// Which floor this board is, from its Wi-Fi station MAC and the table in secrets.h: 0 upstairs,
// 1 downstairs, -1 not listed (or radio off). With the radio on, this decides the floor.
int radio_own_floor();
bool radio_begin();
void radio_send(uint8_t to, const uint8_t *data, size_t len);
bool radio_poll(uint8_t *out, size_t *len);  // one received frame from a known peer, if any
void radio_report();  // for serial "radio": counts, and the last frames in and out

}  // namespace board
