#include "lcd.h"
#include <Arduino.h>
#include <SPI.h>
#include "gfx/font5x7.h"  // firmware/tcomm/src, on the include path (firmware/tools/fw.sh)

namespace lcd {

namespace {

constexpr int kSclk = 1, kMosi = 2, kDc = 3, kRst = 4, kCs = 5, kBacklight = 6;
// The controller is 240 x 320; the glass shows a 240 x 135 window of it. Sideways (MADCTL row and
// column exchange) the window starts 40 columns and 53 rows in (checked on the board, see lcd.h).
constexpr int kColOffset = 40, kRowOffset = 53;
constexpr uint8_t kMadctl = 0x60;  // MV | MX: landscape, USB plug on the left

SPISettings g_spi(40000000, MSBFIRST, SPI_MODE0);

void cmd(uint8_t c, const uint8_t *data = nullptr, int n = 0) {
  SPI.beginTransaction(g_spi);
  digitalWrite(kCs, LOW);
  digitalWrite(kDc, LOW);
  SPI.transfer(c);
  digitalWrite(kDc, HIGH);
  for (int i = 0; i < n; i++) SPI.transfer(data[i]);
  digitalWrite(kCs, HIGH);
  SPI.endTransaction();
}

void window(int x0, int y0, int x1, int y1) {
  x0 += kColOffset, x1 += kColOffset, y0 += kRowOffset, y1 += kRowOffset;
  const uint8_t ca[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t ra[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  cmd(0x2A, ca, 4);  // CASET
  cmd(0x2B, ra, 4);  // RASET
}

// Pixels into the window just set, from RAMWR on.
void pixels_begin() {
  SPI.beginTransaction(g_spi);
  digitalWrite(kCs, LOW);
  digitalWrite(kDc, LOW);
  SPI.transfer(0x2C);  // RAMWR
  digitalWrite(kDc, HIGH);
}
void pixels_end() {
  digitalWrite(kCs, HIGH);
  SPI.endTransaction();
}

}  // namespace

void begin() {
  pinMode(kCs, OUTPUT);
  pinMode(kDc, OUTPUT);
  pinMode(kRst, OUTPUT);
  pinMode(kBacklight, OUTPUT);
  digitalWrite(kCs, HIGH);
  digitalWrite(kBacklight, LOW);
  SPI.begin(kSclk, -1, kMosi, kCs);
  digitalWrite(kRst, LOW);
  delay(10);
  digitalWrite(kRst, HIGH);
  delay(120);
  cmd(0x01);  // SWRESET
  delay(150);
  cmd(0x11);  // SLPOUT
  delay(120);
  const uint8_t colmod = 0x55;  // 16 bits a pixel
  cmd(0x3A, &colmod, 1);
  cmd(0x36, &kMadctl, 1);
  cmd(0x21);  // INVON: these IPS panels show the right colours inverted
  cmd(0x13);  // NORON
  fill(0, 0, kW, kH, kBlack);
  cmd(0x29);  // DISPON
  digitalWrite(kBacklight, HIGH);
}

// Pixels go out a buffer at a time: one SPI call per row, not one per pixel.
static uint8_t g_buf[kW * 2];

static void put(uint8_t *p, uint16_t c) {
  p[0] = (uint8_t)(c >> 8);  // the panel takes the high byte first
  p[1] = (uint8_t)c;
}

void fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > kW) w = kW - x;
  if (y + h > kH) h = kH - y;
  if (w <= 0 || h <= 0) return;
  for (int i = 0; i < w; i++) put(g_buf + 2 * i, c);
  window(x, y, x + w - 1, y + h - 1);
  pixels_begin();
  for (int r = 0; r < h; r++) SPI.writeBytes(g_buf, 2 * w);
  pixels_end();
}

int text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg) {
  if (scale < 1) scale = 1;
  const int cw = 6 * scale, chh = 7 * scale;  // a column of space after each character
  if (y < 0 || y + chh > kH) return x;
  for (; *s && x >= 0 && x + cw <= kW; s++, x += cw) {
    const uint8_t *g = gfx::glyph5x7(*s);
    window(x, y, x + cw - 1, y + chh - 1);
    pixels_begin();
    for (int r = 0; r < chh; r++) {
      uint8_t bits = g[r / scale];
      for (int col = 0; col < cw; col++)
        put(g_buf + 2 * col, col / scale < 5 && (bits & (0x10 >> (col / scale))) ? fg : bg);
      SPI.writeBytes(g_buf, 2 * cw);
    }
    pixels_end();
  }
  return x;
}

}  // namespace lcd
