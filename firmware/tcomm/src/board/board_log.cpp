// A small event log kept on the panel: the last kLines events, each stamped with millis().
// elog() only stores the line, so any task can call it: the Arduino loop task copies new lines to
// USB serial (elog_flush). Writing to USB serial from the sound or interface task is what froze
// the sound on 2026-10-24 (see setup() in tcomm.ino), and the log keeps a fault that happened with
// the laptop's serial monitor closed for serial "log" to print.
#include <Arduino.h>
#include <stdarg.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"

namespace board {

static constexpr int kLines = 64, kLen = 120;
static char *g_lines = nullptr;   // kLines x kLen, in PSRAM; line number n sits at n % kLines
static uint32_t g_seq = 0;        // lines stored since start-up
static uint32_t g_printed = 0;    // lines copied to serial (loop task only)
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

void elog(const char *fmt, ...) {
  char line[kLen];
  int n = snprintf(line, sizeof(line), "%7lu ", (unsigned long)millis());
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);
  if (!g_lines) {
    char *mem = (char *)heap_caps_calloc(kLines, kLen, MALLOC_CAP_SPIRAM);
    taskENTER_CRITICAL(&g_mux);
    if (!g_lines) {
      g_lines = mem;
      mem = nullptr;
    }
    taskEXIT_CRITICAL(&g_mux);
    free(mem);  // another task got there first
    if (!g_lines) return;
  }
  taskENTER_CRITICAL(&g_mux);
  memcpy(g_lines + (g_seq % kLines) * kLen, line, kLen);
  g_seq++;
  taskEXIT_CRITICAL(&g_mux);
}

// Copies line number i, if it's still held. False once it has been written over.
static bool line_at(uint32_t i, char *out) {
  bool ok = false;
  taskENTER_CRITICAL(&g_mux);
  if (g_lines && i < g_seq && g_seq - i <= (uint32_t)kLines) {
    memcpy(out, g_lines + (i % kLines) * kLen, kLen);
    ok = true;
  }
  taskEXIT_CRITICAL(&g_mux);
  out[kLen - 1] = 0;
  return ok;
}

void elog_flush() {
  char line[kLen];
  uint32_t seq = g_seq;
  if (seq - g_printed > (uint32_t)kLines) g_printed = seq - kLines;  // the oldest are gone
  for (; g_printed < seq; g_printed++)
    if (line_at(g_printed, line)) Serial.printf("[log] %s\n", line);
}

void elog_print() {
  uint32_t seq = g_seq, first = seq > (uint32_t)kLines ? seq - kLines : 0;
  if (seq == first) {
    Serial.println("log: empty");
    return;
  }
  Serial.printf("log: the last %lu events (ms since start-up)\n", (unsigned long)(seq - first));
  char line[kLen];
  for (uint32_t i = first; i < seq; i++)
    if (line_at(i, line)) Serial.println(line);
}

}  // namespace board
