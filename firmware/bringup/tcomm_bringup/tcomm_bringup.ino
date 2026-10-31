/*
 * TComm phase 1 bring-up sketch.
 *
 * Answers the board questions Waveshare's demos leave open:
 *   - which I2C addresses answer (and whether 0x44 is free for the SHT40)
 *   - whether the RTC keeps time with the power off (no coin cell fitted)
 *   - how the backlight PWM register behaves, step by step, down to dark
 *   - how loud the speaker gets on a full-scale tone, and whether switching the amp off stops the hiss
 *   - how the design palette looks on the real panel at day, evening and night levels
 *   - the chip's MAC addresses, flash and PSRAM size, and reset reason
 *
 * Drivers under src/ are unmodified copies of Waveshare's Apache-2.0 example code
 * (ESP32-S3-Touch-LCD-4.3C, examples/arduino 06_touch and 11_speaker_microphone, commit 44e412f).
 *
 * Touch: two rows of 90 px buttons at the bottom.
 *   Speaker:   WEST, WEST HI, WEST 2 (Westminster variants), VOL -, VOL +,
 *              AMP (AUTO = on only while a sound plays, ON = held on)
 *   Backlight: BRIGHTER, STEP, DIMMER
 * Serial (115200, USB CDC): type "help". Opening the port from a PC resets the board.
 */
#include "src/rgb_lcd_port/rgb_lcd_port.h"
#include "src/rgb_lcd_port/gui_paint/gui_paint.h"
#include "src/gt911/gt911.h"
#include "src/speaker_microphone/codec_dev.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_idf_version.h"

extern io_extension_obj_t IO_EXTENSION;  // defined in io_extension.cpp, not exported by its header

// Polarity confirmed on Mike's board 2026-10-21: lower value is brighter, 224 is the last step before black.
static int bl_raw = 128;
static int bl_step = 16;

static i2c_master_dev_handle_t rtc_dev;  // raw handle so the OS flag and year byte can be read unmasked
static uint8_t i2c_found[32];
static int i2c_found_n = 0;

// Audio: Waveshare's codec setup runs the I2S bus at 44.1 kHz, 16-bit stereo.
static const int SAMPLE_RATE = CODEC_DEFAULT_SAMPLE_RATE;
static i2c_master_dev_handle_t es8311_dev;  // raw handle to read back the DAC volume register
static bool audio_ok = false;
static bool amp_hold = false;  // false: amp on only while a sound plays, which is the plan for the real firmware
static int volume = 100;
static int dac_reg = -1;       // ES8311 register 0x32: 0xBF = 0 dB, 0.5 dB per step
static int16_t *tone_buf, *chime_buf, *west_buf;
static size_t tone_bytes, chime_bytes, west_bytes;
static float *west_mix;  // mono scratch for rendering a Westminster variant on demand
static const float WEST_SECONDS = 7.0f;

static UBYTE *canvas;
static uint32_t last_draw_ms = 0;

// Defined before any function: the Arduino builder inserts its generated prototypes ahead of the first one.
struct RtcRead {
  bool ok;
  uint8_t reg[11];  // 0x00..0x0A
  bool os;          // oscillator-stop flag: set means time is not trustworthy
  int yy, mo, dd, hh, mi, ss, wd;
};

// A chime note's three partials: frequency ratios to the pitch, starting levels, decay rates (per second).
struct Timbre {
  float ratio[3], amp[3], decay[3];
};
// Struck bar: a free bar's inharmonic modes, the upper ones dying faster. Rings for seconds.
static const Timbre BAR = {{1.0f, 2.756f, 5.404f}, {1.0f, 0.35f, 0.12f}, {1.2f, 3.0f, 7.0f}};
// Same notes with less low-frequency energy: a weaker, shorter fundamental and a strong octave, so the
// small speaker's cone travels less while the ear still hears the same pitch.
static const Timbre LIGHT = {{1.0f, 2.0f, 2.756f}, {0.4f, 0.7f, 0.3f}, {2.5f, 2.0f, 4.0f}};

// ---------- helpers ----------

static UWORD rgb565(uint32_t hex, float scale = 1.0f) {
  int r = ((hex >> 16) & 0xFF) * scale;
  int g = ((hex >> 8) & 0xFF) * scale;
  int b = (hex & 0xFF) * scale;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(int v) { return ((v / 10) << 4) | (v % 10); }

static const char *i2c_name(uint8_t a) {
  switch (a) {
    case 0x14: return "GT911 touch (alt addr)";
    case 0x18: return "ES8311 codec";
    case 0x24: return "IO expander (CH32V003)";
    case 0x40: return "ES7210 mic ADC";
    case 0x44: return "SHT40 (if fitted)";
    case 0x45: return "SHT40 alt addr";
    case 0x51: return "PCF85063A RTC";
    case 0x5D: return "GT911 touch";
    default: return "unknown";
  }
}

// ---------- I2C scan ----------

static void i2c_scan() {
  i2c_master_bus_handle_t bus = DEV_I2C_Get_Bus_Device();
  i2c_found_n = 0;
  printf("I2C scan 0x08-0x77:\r\n");
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (i2c_master_probe(bus, a, 50) == ESP_OK) {
      if (i2c_found_n < (int)sizeof(i2c_found)) i2c_found[i2c_found_n++] = a;
      printf("  0x%02X  %s\r\n", a, i2c_name(a));
    }
  }
  bool clash = false;
  for (int i = 0; i < i2c_found_n; i++) clash |= (i2c_found[i] == 0x44);
  printf("  %d device(s). 0x44 %s\r\n", i2c_found_n, clash ? "IS IN USE" : "is free for the SHT40");
}

// ---------- RTC ----------

static RtcRead rtc_read() {
  RtcRead r = {};
  uint8_t start = 0x00;
  r.ok = i2c_master_transmit_receive(rtc_dev, &start, 1, r.reg, sizeof(r.reg), 100) == ESP_OK;
  if (!r.ok) return r;
  r.os = r.reg[4] & 0x80;
  r.ss = bcd2dec(r.reg[4] & 0x7F);
  r.mi = bcd2dec(r.reg[5] & 0x7F);
  r.hh = bcd2dec(r.reg[6] & 0x3F);  // assumes 24 h mode (CTRL1 bit 1 = 0); printed below
  r.dd = bcd2dec(r.reg[7] & 0x3F);
  r.wd = r.reg[8] & 0x07;
  r.mo = bcd2dec(r.reg[9] & 0x1F);
  r.yy = bcd2dec(r.reg[10]);
  return r;
}

static void rtc_print(const char *why) {
  RtcRead r = rtc_read();
  if (!r.ok) { printf("RTC (%s): read FAILED\r\n", why); return; }
  printf("RTC (%s): year byte %02d -> %d if 2000-based, %d if 1970-based (Waveshare driver)\r\n",
         why, r.yy, 2000 + r.yy, 1970 + r.yy);
  printf("  %02d-%02d %02d:%02d:%02d weekday %d  OS flag %d (%s)  CTRL1 0x%02X (%s)\r\n",
         r.mo, r.dd, r.hh, r.mi, r.ss, r.wd, r.os,
         r.os ? "clock stopped since last set, time NOT valid" : "clock ran continuously",
         r.reg[0], (r.reg[0] & 0x02) ? "12 h mode" : "24 h mode");
  printf("  raw 00-0A:");
  for (int i = 0; i < 11; i++) printf(" %02X", r.reg[i]);
  printf("\r\n");
}

// Sakamoto's method, 0 = Sunday
static int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Writes the time with a 2000-based year and 24 h mode. Writing the seconds register clears the OS flag.
static bool rtc_set(int Y, int M, int D, int h, int m, int s) {
  if (Y < 2000 || Y > 2099 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || s > 59) return false;
  uint8_t ctrl[2] = {0x00, 0x01};  // CTRL1: 24 h mode, 12.5 pF load (same CAP_SEL as Waveshare's init)
  if (i2c_master_transmit(rtc_dev, ctrl, 2, 100) != ESP_OK) return false;
  uint8_t buf[8] = {0x04, dec2bcd(s), dec2bcd(m), dec2bcd(h), dec2bcd(D),
                    (uint8_t)weekday(Y, M, D), dec2bcd(M), dec2bcd(Y - 2000)};
  return i2c_master_transmit(rtc_dev, buf, sizeof(buf), 100) == ESP_OK;
}

// ---------- backlight ----------

static void bl_write(int raw) {
  bl_raw = constrain(raw, 0, 255);
  uint8_t d[2] = {IO_EXTENSION_PWM_ADDR, (uint8_t)bl_raw};
  DEV_I2C_Write_Nbyte(IO_EXTENSION.addr, d, 2);
  printf("Backlight PWM register = %d\r\n", bl_raw);
}

// ---------- audio ----------

static void amp_set(bool on) { IO_EXTENSION_Output(IO_EXTENSION_IO_3, on); }

// One partial of the test chime: 5 ms attack, bell-like exponential decay, a little second harmonic.
static float note(float f, float t) {
  float attack = fminf(1.0f, t / 0.005f);
  return attack * expf(-3.0f * t) * (0.8f * sinf(2 * PI * f * t) + 0.2f * sinf(2 * PI * 2 * f * t));
}

// kind 0: 1 kHz sine, 1.5 s, 10 ms fades. kind 1: "ding-dong" on E6 then C6, 1.8 s.
// The chime is only something doorbell-shaped for judging loudness, not the final sound.
static float sound_sample(int kind, float t) {
  if (kind == 0) return fminf(1.0f, fminf(t, 1.5f - t) / 0.01f) * sinf(2 * PI * 1000.0f * t);
  if (t < 0.6f) return note(1318.5f, t) * fminf(1.0f, (0.6f - t) / 0.02f);
  return note(1046.5f, t - 0.6f) * fminf(1.0f, (1.8f - t) / 0.02f);
}

// Interleaved stereo at a peak of 32000 of 32767 (about -0.2 dBFS), in PSRAM.
static int16_t *make_sound(int kind, float seconds, size_t *bytes) {
  size_t frames = seconds * SAMPLE_RATE;
  int16_t *buf = (int16_t *)heap_caps_malloc(frames * 4, MALLOC_CAP_SPIRAM);
  if (!buf) return NULL;
  for (size_t i = 0; i < frames; i++) {
    int16_t v = (int16_t)(32000.0f * sound_sample(kind, (float)i / SAMPLE_RATE));
    buf[2 * i] = v;
    buf[2 * i + 1] = v;
  }
  *bytes = frames * 4;
  return buf;
}

// Adds one chime note to a mono mix. Recursive oscillators keep the synthesis fast enough to run on a tap.
static void add_note(float *mix, size_t frames, float hz, float start, const Timbre &tb) {
  const float ring = 3.5f;
  size_t i0 = start * SAMPLE_RATE, len = ring * SAMPLE_RATE;
  for (int p = 0; p < 3; p++) {
    float w = 2 * PI * hz * tb.ratio[p] / SAMPLE_RATE;
    float c = cosf(w), s = sinf(w), d = expf(-tb.decay[p] / SAMPLE_RATE);
    float x = tb.amp[p], y = 0;
    for (size_t k = 0; k < len && i0 + k < frames; k++) {
      float t = (float)k / SAMPLE_RATE;
      float env = fminf(1.0f, t / 0.003f) * fminf(1.0f, (ring - t) / 0.3f);
      mix[i0 + k] += env * y;
      float nx = (x * c - y * s) * d;
      y = (x * s + y * c) * d;
      x = nx;
    }
  }
}

// Westminster doorbell: changes 4 and 5 of the Westminster Quarters, in C (E C D G, G D E C).
// pitch 1 = around 392-660 Hz, pitch 2 = an octave up. Renders into west_buf.
// Every variant peaks at -3.5 dBFS so it stays unclipped at volume 100, where the codec adds +3.5 dB.
static void render_westminster(float pitch, const Timbre &tb) {
  const float hz[8] = {659.26f, 523.25f, 587.33f, 392.00f, 392.00f, 587.33f, 659.26f, 523.25f};
  const float at[8] = {0.0f, 0.6f, 1.2f, 1.8f, 3.0f, 3.6f, 4.2f, 4.8f};
  size_t frames = west_bytes / 4;
  memset(west_mix, 0, frames * sizeof(float));
  for (int n = 0; n < 8; n++) add_note(west_mix, frames, hz[n] * pitch, at[n], tb);
  float peak = 1e-6f;
  for (size_t i = 0; i < frames; i++) peak = fmaxf(peak, fabsf(west_mix[i]));
  float gain = 32767.0f * 0.668f / peak;  // 0.668 = -3.5 dB
  for (size_t i = 0; i < frames; i++) west_buf[2 * i] = west_buf[2 * i + 1] = (int16_t)(west_mix[i] * gain);
}

// 0: struck bar at the usual pitch, 1: same an octave up, 2: usual pitch with LIGHT timbre.
static void play_west(int variant) {
  if (!audio_ok) { printf("Audio not ready\r\n"); return; }
  static const char *name[3] = {"Westminster", "Westminster, octave up", "Westminster, light bass"};
  uint32_t t0 = millis();
  render_westminster(variant == 1 ? 2.0f : 1.0f, variant == 2 ? LIGHT : BAR);
  printf("Rendered %s in %lu ms\r\n", name[variant], (unsigned long)(millis() - t0));
  play(west_buf, west_bytes, name[variant]);
}

static void volume_set(int v) {
  volume = constrain(v, 0, 100);
  speaker_codec_volume_set(volume, NULL);
  uint8_t reg = 0x32, val = 0;
  dac_reg = (i2c_master_transmit_receive(es8311_dev, &reg, 1, &val, 1, 100) == ESP_OK) ? val : -1;
  printf("Volume %d: ES8311 DAC register 0x32 = 0x%02X (%+.1f dB)\r\n", volume, dac_reg, (dac_reg - 0xBF) / 2.0f);
}

static void play(const int16_t *buf, size_t bytes, const char *name) {
  if (!audio_ok) { printf("Audio not ready\r\n"); return; }
  printf("Playing %s at volume %d, amp %s\r\n", name, volume, amp_hold ? "held on" : "auto");
  if (!amp_hold) { amp_set(true); delay(30); }  // UNVERIFIED settle time: listen for a pop at the start
  size_t written = 0;
  speaker_i2s_write((void *)buf, bytes, &written, 0);
  delay(60);  // let the last DMA buffers drain before the amp goes off
  if (!amp_hold) amp_set(false);
}

static void audio_init() {
  codec_init();  // Waveshare: I2S, ES8311 speaker codec, ES7210 mics. It switches the amp on.
  amp_set(amp_hold);
  DEV_I2C_Set_Slave_Addr(&es8311_dev, 0x18);
  tone_buf = make_sound(0, 1.5f, &tone_bytes);
  chime_buf = make_sound(1, 1.8f, &chime_bytes);
  size_t west_frames = WEST_SECONDS * SAMPLE_RATE;
  west_mix = (float *)heap_caps_malloc(west_frames * sizeof(float), MALLOC_CAP_SPIRAM);
  west_buf = (int16_t *)heap_caps_malloc(west_frames * 4, MALLOC_CAP_SPIRAM);
  west_bytes = west_frames * 4;
  audio_ok = tone_buf && chime_buf && west_mix && west_buf;
  volume_set(volume);
  printf("Audio %s\r\n", audio_ok ? "ready" : "buffers FAILED");
}

// ---------- screen ----------

static const uint32_t STATUS_HEX[6] = {0x1F6B3A, 0xF2A71B, 0xB8442E, 0x5A4A8C, 0x66705C, 0x49535B};
static const char *STATUS_NAME[6] = {"OPEN", "BUSY", "PRIVATE", "SLEEP", "AWAY", "NO SIG"};

static void button(int x0, int y0, int x1, int y1, const char *label, UWORD face, sFONT *font) {
  Paint_DrawRectangle(x0, y0, x1, y1, face, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  int tw = strlen(label) * font->Width;
  Paint_DrawString_EN(x0 + (x1 - x0 - tw) / 2, y0 + (y1 - y0 - font->Height) / 2, label, font, rgb565(0xFBF1DC), face);
}

static void draw_screen() {
  char line[96];
  UWORD bg = rgb565(0x1B2620);  // night paper
  UWORD ink = rgb565(0xE4D8C0); // night text
  Paint_Clear(bg);

  Paint_DrawString_EN(12, 8, "TComm bring-up", &Font24, ink, bg);

  int n = snprintf(line, sizeof(line), "I2C:");
  for (int i = 0; i < i2c_found_n && n < (int)sizeof(line) - 6; i++) n += snprintf(line + n, sizeof(line) - n, " %02X", i2c_found[i]);
  Paint_DrawString_EN(12, 34, line, &Font24, ink, bg);

  RtcRead r = rtc_read();
  if (r.ok) snprintf(line, sizeof(line), "RTC: 20%02d-%02d-%02d %02d:%02d:%02d  %s", r.yy, r.mo, r.dd, r.hh, r.mi, r.ss, r.os ? "STOPPED" : "ran");
  else snprintf(line, sizeof(line), "RTC: read failed");
  Paint_DrawString_EN(12, 60, line, &Font24, r.ok && !r.os ? ink : rgb565(0xF2A71B), bg);

  snprintf(line, sizeof(line), "Backlight register %d  step %d", bl_raw, bl_step);
  Paint_DrawString_EN(12, 86, line, &Font24, ink, bg);

  if (audio_ok) snprintf(line, sizeof(line), "Speaker vol %d  DAC %+.1f dB  amp %s", volume, (dac_reg - 0xBF) / 2.0f, amp_hold ? "ON" : "AUTO");
  else snprintf(line, sizeof(line), "Speaker: audio init failed");
  Paint_DrawString_EN(12, 112, line, &Font24, audio_ok ? ink : rgb565(0xF2A71B), bg);

  // Status colors at the three palette levels from the design brief (day 100%, evening ~86%, night ~50%).
  const float level[3] = {1.0f, 0.86f, 0.5f};
  const char *level_name[3] = {"DAY", "EVE", "NIGHT"};
  for (int row = 0; row < 3; row++) {
    int y = 142 + row * 38;
    Paint_DrawString_EN(8, y + 9, level_name[row], &Font16, ink, bg);
    for (int i = 0; i < 6; i++) {
      int x = 76 + i * 120;
      Paint_DrawRectangle(x, y, x + 112, y + 34, rgb565(STATUS_HEX[i], level[row]), DOT_PIXEL_1X1, DRAW_FILL_FULL);
      if (row == 0) Paint_DrawString_EN(x + 4, y + 9, STATUS_NAME[i], &Font16, i == 1 ? rgb565(0x3A1F2B) : rgb565(0xFBF1DC), rgb565(STATUS_HEX[i]));
    }
  }

  // 16-step gray ramp, for spotting banding and flicker at low backlight.
  for (int i = 0; i < 16; i++) {
    uint8_t v = i * 17;
    Paint_DrawRectangle(i * 50, 258, i * 50 + 50, 284, rgb565((v << 16) | (v << 8) | v), DOT_PIXEL_1X1, DRAW_FILL_FULL);
  }

  // Speaker row, then backlight row. Both 90 px tall.
  const char *audio_label[6] = {"WEST", "WEST HI", "WEST 2", "VOL -", "VOL +", amp_hold ? "AMP ON" : "AMP AUTO"};
  for (int i = 0; i < 6; i++) button(i * 133 + 4, 290, (i == 5) ? 796 : i * 133 + 129, 380, audio_label[i], rgb565(0x6B4655), &Font20);
  const char *bl_label[3] = {"BRIGHTER", "STEP", "DIMMER"};
  for (int i = 0; i < 3; i++) button(i * 267 + 4, 386, (i == 2) ? 796 : i * 267 + 262, 476, bl_label[i], rgb565(0x3F7C85), &Font24);

  waveshare_rgb_lcd_display(canvas);
  last_draw_ms = millis();
}

// ---------- serial commands ----------

static void print_info() {
  uint8_t mac[6];
  uint32_t flash = 0;
  esp_flash_get_size(NULL, &flash);
  printf("Arduino core %s, ESP-IDF %s\r\n", ESP_ARDUINO_VERSION_STR, esp_get_idf_version());
  printf("Flash %lu bytes, PSRAM %lu bytes (free %lu)\r\n", (unsigned long)flash, (unsigned long)ESP.getPsramSize(), (unsigned long)ESP.getFreePsram());
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  printf("MAC Wi-Fi station %02X:%02X:%02X:%02X:%02X:%02X  (ESP-NOW uses this one)\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  printf("MAC soft-AP        %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  esp_read_mac(mac, ESP_MAC_BT);
  printf("MAC Bluetooth      %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  printf("Reset reason %d\r\n", (int)esp_reset_reason());
}

static void print_help() {
  printf("Commands:\r\n");
  printf("  info                          chip, memory, MACs, reset reason\r\n");
  printf("  scan                          I2C bus scan\r\n");
  printf("  rtc                           read the clock (raw)\r\n");
  printf("  time YYYY-MM-DD HH:MM:SS      set the clock (2000-based year, 24 h)\r\n");
  printf("  bl N                          write backlight PWM register, 0-255 (lower is brighter)\r\n");
  printf("  tone | chime                  play the 1 kHz test tone or the E6/C6 test chime\r\n");
  printf("  west | west hi | west 2       Westminster: usual pitch, octave up, or lighter bass\r\n");
  printf("  vol N                         speaker volume 0-100\r\n");
  printf("  amp on|auto                   hold the amp on, or switch it on only while a sound plays\r\n");
}

static void handle_line(char *s) {
  int Y, M, D, h, m, sec, v;
  char word[8];
  if (!strcmp(s, "help")) print_help();
  else if (!strcmp(s, "info")) print_info();
  else if (!strcmp(s, "scan")) { i2c_scan(); draw_screen(); }
  else if (!strcmp(s, "rtc")) rtc_print("on request");
  else if (sscanf(s, "time %d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec) == 6) {
    printf(rtc_set(Y, M, D, h, m, sec) ? "Clock set.\r\n" : "Clock NOT set (bad value or I2C error).\r\n");
    rtc_print("after set");
    draw_screen();
  }
  else if (sscanf(s, "bl %d", &v) == 1) { bl_write(v); draw_screen(); }
  else if (!strcmp(s, "tone")) play(tone_buf, tone_bytes, "1 kHz tone");
  else if (!strcmp(s, "chime")) play(chime_buf, chime_bytes, "chime");
  else if (!strcmp(s, "west")) play_west(0);
  else if (!strcmp(s, "west hi")) play_west(1);
  else if (!strcmp(s, "west 2")) play_west(2);
  else if (sscanf(s, "vol %d", &v) == 1) { volume_set(v); draw_screen(); }
  else if (sscanf(s, "amp %7s", word) == 1) {
    amp_hold = strcmp(word, "on") == 0;
    amp_set(amp_hold);
    printf("Amp %s\r\n", amp_hold ? "held on" : "auto (off between sounds)");
    draw_screen();
  }
  else if (*s) printf("Unknown command \"%s\". Type help.\r\n", s);
}

static void poll_serial() {
  static char buf[64];
  static int len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') { buf[len] = 0; handle_line(buf); len = 0; }
    else if (len < (int)sizeof(buf) - 1) buf[len++] = c;
  }
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);  // give the USB CDC host a moment to attach

  printf("\r\n=== TComm bring-up ===\r\n");
  print_info();

  DEV_I2C_Init();
  IO_EXTENSION_Init();
  amp_set(false);  // avoids idle hiss until the codec is up; DISP (EXIO2) stays high
  DEV_I2C_Set_Slave_Addr(&rtc_dev, 0x51);

  rtc_print("at boot, before anything writes it");

  touch_gt911_init(DEV_I2C_Get_Bus_Device());
  waveshare_esp32_s3_rgb_lcd_init();
  i2c_scan();  // after touch reset, so the GT911 address is the one it latched
  bl_write(bl_raw);
  audio_init();

  canvas = (UBYTE *)heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2, MALLOC_CAP_SPIRAM);
  if (!canvas) { printf("Canvas allocation failed\r\n"); return; }
  Paint_NewImage(canvas, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 0, WHITE);
  Paint_SetScale(65);
  draw_screen();
  print_help();
}

void loop() {
  static bool was_down = false;
  touch_gt911_point_t p = touch_gt911_read_point(1);
  bool down = p.cnt > 0;
  if (down && !was_down) {
    int x = p.x[0], y = p.y[0];
    if (y >= 386) {
      if (x < 267) bl_write(bl_raw - bl_step);
      else if (x < 534) { bl_step = (bl_step == 16) ? 1 : 16; printf("Step %d\r\n", bl_step); }
      else bl_write(bl_raw + bl_step);
      draw_screen();
    } else if (y >= 290) {
      switch (x / 133) {
        case 0: play_west(0); break;
        case 1: play_west(1); break;
        case 2: play_west(2); break;
        case 3: volume_set(volume - 10); break;
        case 4: volume_set(volume + 10); break;
        default: amp_hold = !amp_hold; amp_set(amp_hold); printf("Amp %s\r\n", amp_hold ? "held on" : "auto"); break;
      }
      draw_screen();
    }
  }
  was_down = down;

  poll_serial();
  if (canvas && millis() - last_draw_ms > 1000) draw_screen();
  delay(30);
}
