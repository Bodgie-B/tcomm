// Platform contract for the screensaver core.
//
// core/ is plain C++17. It draws into a uint16_t RGB565 buffer and makes no
// ESP-IDF, Arduino, LVGL or OS calls. Everything it needs from the host comes
// through this header so the same sources build on the PC preview and on the
// ESP32-S3 with no changes.
//
// Rules for core/ code:
//   - No exceptions, no RTTI, no std containers that allocate in hot paths.
//   - No heap allocation after init. Scratch buffers are static or caller-owned.
//   - Single-precision float only (the S3 FPU is single precision). Use sinf,
//     cosf, sqrtf, fabsf, floorf. Never double literals in hot code (write 0.5f).
//   - Fixed logical timestep. Rendering may skip a frame; simulation never does.
#pragma once

#include <stdint.h>
#include <stddef.h>

// Screen geometry. The panel is 800x480 landscape. The preview uses the same.
#define SS_WIDTH  800
#define SS_HEIGHT 480

// Logical simulation rate (ticks per second). The render rate may be lower.
#define SS_TICK_HZ 25
#define SS_TICK_DT (1.0f / SS_TICK_HZ)

// Strip renderer: the frame is composited in horizontal strips so each strip
// can live in a small internal-SRAM buffer and be written to the framebuffer in
// one sequential run (see docs/screensaver brief, section 10).
#define SS_STRIP_ROWS  32
#define SS_STRIP_COUNT (SS_HEIGHT / SS_STRIP_ROWS)   // 15
#define SS_STRIP_BYTES (SS_WIDTH * SS_STRIP_ROWS * 2)   // 51,200: one strip of RGB565

// Fast memory: the strip buffer (SS_STRIP_BYTES) followed by the rasterizer's
// scratch (gfx/raster.cpp, about 11 KB). Every pixel drawn is read and written
// there, so on the board it belongs in internal SRAM. The panel has little
// internal SRAM to spare, so the host lends a block it owns but doesn't use
// while the screensaver runs (the panel: LVGL's 64,000-byte draw buffer, idle
// while the screensaver owns the screen). When the screensaver is stopped it
// holds no internal SRAM at all.
#define SS_FAST_BYTES (62 * 1024)

// Large buffers (layer cache, main-screen copy) come from the host, which puts
// them in PSRAM on the board and on the heap on the PC. Allocation happens once
// at screensaver start; core never frees or reallocates during a loop.
namespace ss {

struct HostAlloc {
  void* (*alloc_large)(size_t bytes);   // PSRAM on board, malloc on PC
  void  (*free_large)(void* p);
  // SS_FAST_BYTES of the host's fastest memory, 4-byte aligned, lent from
  // appStart until appRevealBegin or appStop, whichever comes first; the host
  // leaves it alone meanwhile (on the panel: no LVGL flush from it may still be
  // running when appStart is called). After appRevealBegin core never touches
  // it again, so the host can use it to draw the main screen for the reveal.
  // nullptr: core takes the block from alloc_large instead (the PC, the tests).
  void* fast;
};

// Pixel format: logical RGB565, 5 bits red (high), 6 green, 5 blue (low), stored
// in a native uint16_t. Whether the panel wants the bytes swapped is a port
// concern (unverified on the Waveshare RGB panel); the port converts on copy if
// it has to.
typedef uint16_t Pixel;

}  // namespace ss
