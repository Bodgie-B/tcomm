// The ESP32-C6-GEEK's 1.14 inch LCD (ST7789, 240 x 135 used sideways), with a small built-in
// 5 x 7 font: enough for the gateway to show its MAC and what it's doing. No display library.
//
// The pins (SCLK 1, MOSI 2, DC 3, RST 4, CS 5, backlight 6) come from Waveshare's ESP-IDF demo as
// reported on the Home Assistant forum, not the official wiki. On Mike's board (2026-10-25) every
// line showed in the right place and the right way round, so they, the offsets and kMadctl are
// right. UNVERIFIED: the colours (INVON, and the RGB order).
#pragma once
#include <stdint.h>

namespace lcd {

constexpr int kW = 240, kH = 135;

// 16-bit colours.
constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF, kGrey = 0x8410, kGreen = 0x07E0, kYellow = 0xFFE0,
                   kRed = 0xF800;

void begin();
void fill(int x, int y, int w, int h, uint16_t c);
// One line of text (upper case; lower case is drawn as upper), each pixel scale x scale, on bg.
// Characters the font hasn't got are drawn as "?". Returns the x after the text.
int text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);

}  // namespace lcd
