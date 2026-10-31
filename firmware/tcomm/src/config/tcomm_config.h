// Build switches for the panel firmware.
#pragma once

// Phase 2: a simulated other panel and gateway in the firmware, and a Test panel in Settings.
// bash tools/fw.sh radio builds with this 0 and the radio on (below), without editing this file.
#ifndef TCOMM_DEMO
#define TCOMM_DEMO 1
#endif

// Phase 3: real ESP-NOW. Needs config/secrets.h (copy secrets.example.h; never commit it).
// Being tested against the C6 gateway from 2026-10-25 (bash tools/fw.sh radio); not yet on two panels.
#ifndef TCOMM_RADIO_ESPNOW
#define TCOMM_RADIO_ESPNOW 0
#endif

// HANDOFF: neither panel joins the home Wi-Fi in normal mode, which keeps one fixed channel.
#define TCOMM_ESPNOW_CHANNEL 1

// Phase 5: the SHT40 on the screw terminal (0x44). Read every TCOMM_SENSOR_PERIOD_MS; after
// TCOMM_SENSOR_MISSES failed reads in a row the tile shows dashes (a single failure doesn't).
// If it isn't found at start-up, the panel looks for it again every 30 s.
#define TCOMM_SENSOR 1
#define TCOMM_SENSOR_PERIOD_MS 10000
#define TCOMM_SENSOR_MISSES 3

// With no SHT40 found, demo mode feeds this inside reading (tenths of a degree F, tenths of a percent).
#define TCOMM_FAKE_INSIDE_TEMP 670
#define TCOMM_FAKE_INSIDE_HUM 480
