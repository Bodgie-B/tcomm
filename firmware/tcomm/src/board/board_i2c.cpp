// I2C bus lock, the IO expander, the SHT40 sensor and the PCF85063A clock.
#include "../core/civil.h"
#include "../core/sht4x.h"
#include "../ws/i2c/i2c.h"
#include "../ws/io_extension/io_extension.h"
#include "board.h"
#include "esp_log.h"
#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern io_extension_obj_t IO_EXTENSION;  // Waveshare's driver state (not exported by its header)

namespace board {

static SemaphoreHandle_t g_lock = nullptr;
static i2c_master_bus_handle_t g_bus = nullptr;
static uint32_t g_errors = 0;

I2cLock::I2cLock() { ok_ = g_lock && xSemaphoreTakeRecursive(g_lock, pdMS_TO_TICKS(200)) == pdTRUE; }
I2cLock::~I2cLock() {
  if (ok_) xSemaphoreGiveRecursive(g_lock);
}

bool i2c_begin() {
  g_bus = DEV_I2C_Get_Bus_Device();  // created by Waveshare's DEV_I2C_Init()
  if (!g_lock) g_lock = xSemaphoreCreateRecursiveMutex();
  return g_bus && g_lock;
}

i2c_master_dev_handle_t i2c_device(uint8_t addr, uint32_t scl_hz) {
  if (!g_bus) return nullptr;
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = addr;
  cfg.scl_speed_hz = scl_hz;
  i2c_master_dev_handle_t h = nullptr;
  if (i2c_master_bus_add_device(g_bus, &cfg, &h) != ESP_OK) return nullptr;
  return h;
}

static bool note(esp_err_t e) {
  if (e == ESP_OK) return true;
  if (g_errors++ % 10 == 0) elog("i2c error %s (%lu so far)", esp_err_to_name(e), (unsigned long)g_errors);
  return false;
}

bool i2c_write(i2c_master_dev_handle_t dev, const uint8_t *data, size_t n) {
  if (!dev) return false;
  I2cLock l;
  if (!l.ok()) return false;
  return note(i2c_master_transmit(dev, data, n, 50));
}

bool i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t n) {
  if (!dev) return false;
  I2cLock l;
  if (!l.ok()) return false;
  return note(i2c_master_transmit_receive(dev, &reg, 1, out, n, 50));
}

bool i2c_receive(i2c_master_dev_handle_t dev, uint8_t *out, size_t n) {
  if (!dev) return false;
  I2cLock l;
  if (!l.ok()) return false;
  return note(i2c_master_receive(dev, out, n, 50));
}

uint32_t i2c_errors() { return g_errors; }

// One device's known answer, read again and again.
static void test_one(const char *name, uint8_t addr, uint32_t hz, const uint8_t *reg, size_t reg_len,
                     const uint8_t *expect, size_t n, int tries) {
  i2c_master_dev_handle_t dev = i2c_device(addr, hz);
  if (!dev) {
    Serial.printf("i2c test: %s at %lu kHz: no handle\n", name, (unsigned long)(hz / 1000));
    return;
  }
  int failed = 0, wrong = 0;
  uint8_t got[8];
  for (int i = 0; i < tries; i++) {
    esp_err_t e;
    {
      I2cLock l;
      e = l.ok() ? i2c_master_transmit_receive(dev, reg, reg_len, got, n, 50) : ESP_ERR_TIMEOUT;
    }
    if (e != ESP_OK) failed++;
    else if (memcmp(got, expect, n) != 0) wrong++;
    if (i % 50 == 49) vTaskDelay(1);  // let touch and sound have the bus too
  }
  i2c_master_bus_rm_device(dev);
  Serial.printf("i2c test: %s at %lu kHz: %d reads, %d failed, %d wrong\n", name, (unsigned long)(hz / 1000), tries, failed,
         wrong);
}

void i2c_test(uint32_t scl_hz, int tries) {
  const uint8_t es_reg[1] = {0xFD}, es_id[1] = {0x83};               // ES8311 chip ID 1 (no auto-increment)
  const uint8_t gt_reg[2] = {0x81, 0x40}, gt_id[3] = {'9', '1', '1'};  // GT911 product ID
  test_one("codec ES8311", 0x18, scl_hz, es_reg, 1, es_id, 1, tries);
  test_one("touch GT911", 0x5D, scl_hz, gt_reg, 2, gt_id, 3, tries);
}

bool i2c_probe(uint8_t addr) {
  if (!g_bus) return false;
  I2cLock l;
  return l.ok() && i2c_master_probe(g_bus, addr, 50) == ESP_OK;
}

// ---------------------------------------------------------------------------------------------
// SHT40 on the screw terminal. 100 kHz rather than the bus's 400: it sits at the end of a cable,
// and the board's 4.7k pull-ups (through the H8 level shifter) are sized for short traces.

static i2c_master_dev_handle_t g_sht = nullptr;

bool sht40_begin(uint32_t *serial) {
  if (!i2c_probe(tc::sht4x::kAddress)) return false;
  if (!g_sht) g_sht = i2c_device(tc::sht4x::kAddress, 100000);
  const uint8_t cmd = tc::sht4x::kReadSerial;
  uint8_t raw[6];
  if (!i2c_write(g_sht, &cmd, 1)) return false;
  vTaskDelay(pdMS_TO_TICKS(2));  // datasheet: 1 ms
  return i2c_receive(g_sht, raw, 6) && tc::sht4x::decode_serial(raw, serial);
}

bool sht40_measure(int16_t &temp_tenths_f, uint16_t &hum_tenths) {
  if (!g_sht) return false;
  const uint8_t cmd = tc::sht4x::kMeasureHigh;
  uint8_t raw[6];
  if (!i2c_write(g_sht, &cmd, 1)) return false;
  vTaskDelay(pdMS_TO_TICKS(tc::sht4x::kMeasureMs));  // the bus is free for touch and the expander
  if (!i2c_receive(g_sht, raw, 6)) return false;
  tc::sht4x::Reading r = tc::sht4x::decode(raw);
  if (!r.ok) return false;
  temp_tenths_f = r.temp_tenths_f;
  hum_tenths = r.hum_tenths;
  return true;
}

// ---------------------------------------------------------------------------------------------
// Expander: registers from Waveshare's driver (0x02 mode, 0x03 outputs, 0x05 PWM).

static i2c_master_dev_handle_t g_exp = nullptr;
static uint8_t g_out = 0xFF;

bool expander_begin() {
  g_exp = i2c_device(0x24);
  if (!g_exp) return false;
  // Waveshare's init makes every pin an output and its touch reset leaves them all high. Make the
  // isolated inputs (EXIO0 DI0, EXIO5 DI1) inputs again, as Waveshare's isolation demo does, and
  // switch off the amp (EXIO3) and the isolated outputs (EXIO6 DO0, EXIO7 DO1; UNVERIFIED which
  // level is "off" for the optocouplers: low draws no current).
  uint8_t mode[2] = {0x02, 0xDE};
  bool ok = i2c_write(g_exp, mode, 2);
  g_out = (uint8_t)(IO_EXTENSION.Last_io_value & ~((1u << kExioAmp) | (1u << 6) | (1u << 7)));
  uint8_t out[2] = {0x03, g_out};
  return i2c_write(g_exp, out, 2) && ok;
}

bool expander_output(uint8_t pin, bool on) {
  I2cLock l;  // the shadow byte is read-modify-write
  if (!l.ok()) return false;
  uint8_t next = on ? (uint8_t)(g_out | (1u << pin)) : (uint8_t)(g_out & ~(1u << pin));
  uint8_t d[2] = {0x03, next};
  // Retried: a flash write elsewhere can stall the bus past a single transfer's timeout, and a
  // missed amp switch means a silent doorbell.
  for (int tries = 0; tries < 3; tries++) {
    if (i2c_write(g_exp, d, 2)) {
      g_out = next;
      return true;
    }
  }
  return false;
}

bool expander_read(uint8_t reg, uint8_t &value) { return i2c_read(g_exp, reg, &value, 1); }

bool expander_backlight(uint8_t reg) {
  uint8_t d[2] = {0x05, reg};
  return i2c_write(g_exp, d, 2);
}

// ---------------------------------------------------------------------------------------------
// PCF85063A at 0x51. Registers 0x00 CTRL1, 0x04..0x0A seconds..year (BCD).

static i2c_master_dev_handle_t g_rtc = nullptr;

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t dec2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

RtcTime rtc_read() {
  RtcTime t = {false, 0};
  if (!g_rtc) g_rtc = i2c_device(0x51);
  uint8_t r[7];
  if (!i2c_read(g_rtc, 0x04, r, sizeof(r))) return t;
  if (r[0] & 0x80) return t;  // oscillator stopped: time not trustworthy (measured: no coin cell fitted)
  int ss = bcd2dec(r[0] & 0x7F), mi = bcd2dec(r[1] & 0x7F), hh = bcd2dec(r[2] & 0x3F);
  int dd = bcd2dec(r[3] & 0x3F), mo = bcd2dec(r[5] & 0x1F), yy = bcd2dec(r[6]);
  // A year before this firmware was built, or more than 10 years after, is junk or the factory
  // firmware's 1970-based format (it left "55", which reads as 2055).
  const int build_yy = (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');  // "Oct 21 2026"
  if (yy < build_yy || yy > build_yy + 10 || mo < 1 || mo > 12 || dd < 1 || dd > 31 || hh > 23 || mi > 59 || ss > 59)
    return t;
  t.utc = (uint32_t)tc::seconds_from_civil(2000 + yy, mo, dd, hh, mi, ss);
  t.valid = true;
  return t;
}

bool rtc_write(uint32_t utc) {
  if (!g_rtc) g_rtc = i2c_device(0x51);
  tc::Civil c = tc::civil_from_seconds(utc);
  if (c.year < 2000 || c.year > 2099) return false;
  I2cLock l;
  if (!l.ok()) return false;
  // CTRL1: 24 h, 12.5 pF load (CAP_SEL). Power loss resets it to 0x00, so it's written every time.
  uint8_t ctrl[2] = {0x00, 0x01};
  uint8_t buf[8] = {0x04, dec2bcd(c.second), dec2bcd(c.minute), dec2bcd(c.hour), dec2bcd(c.day),
                    (uint8_t)c.weekday, dec2bcd(c.month), dec2bcd(c.year - 2000)};
  return i2c_write(g_rtc, ctrl, 2) && i2c_write(g_rtc, buf, sizeof(buf));  // writing seconds clears OS
}

}  // namespace board
