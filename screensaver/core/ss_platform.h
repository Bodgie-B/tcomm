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

// Large buffers (layer cache, main-screen copy) come from the host, which puts
// them in PSRAM on the board and on the heap on the PC. Allocation happens once
// at screensaver start; core never frees or reallocates during a loop.
namespace ss {

struct HostAlloc {
  void* (*alloc_large)(size_t bytes);   // PSRAM on board, malloc on PC
  void  (*free_large)(void* p);
};

// Pixel format: logical RGB565, 5 bits red (high), 6 green, 5 blue (low), stored
// in a native uint16_t. Whether the panel wants the bytes swapped is a port
// concern (unverified on the Waveshare RGB panel); the port converts on copy if
// it has to.
typedef uint16_t Pixel;

}  // namespace ss
