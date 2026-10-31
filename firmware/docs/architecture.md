# Firmware architecture (phase 2)

The firmware is one Arduino sketch, `tcomm/`, built with the pinned toolchain in the README. The handoff asked for application logic in modules with no hardware calls, so most of the code also builds on the PC, where it's tested and used to render screenshots.

| Folder | What's in it | Runs on |
|---|---|---|
| `src/core/` | Status, timer and Undo; doorbell sender and receiver; shared dog value; radio message format, duplicate filter and retry queue; sunrise, sunset and sky phases; backlight policy; sound synthesis; calendar maths; the household calendar's event store, repeats, its merge rules and file format (`events.*`); US federal holidays (`holidays.*`). No Arduino, ESP-IDF or LVGL headers. | panel and PC |
| `src/app/` | `Controller` ties the core together and builds a plain `View` for the screen. `Platform` is the interface to everything hardware-shaped. `Demo` runs a second Controller as a simulated other panel plus a fake gateway, over a loopback radio. `PanelApp` turns touches into controller calls. | panel and PC |
| `src/gfx/` | A small anti-aliased rasterizer that draws flat shapes into LVGL-format image buffers. | panel and PC |
| `src/ui/` | The LVGL 8.4 interface: theme colors, artwork (sky, tiles, badges, icons), main screen, sheets, Settings, the calendar roll-down (`ui_calendar.cpp`). Draws a `View`; sends touches to `ui::Actions`. | panel and PC |
| `src/board/` | I2C helpers under one lock that return errors instead of rebooting, the expander (backlight, amp), the PCF85063A clock, NVS storage, the calendar's files on the SD card (the FAT partition without one), the audio task, ESP-NOW, and `DevicePlatform`. | panel |
| `src/ws/` | Waveshare's display, touch, LVGL-port and codec drivers, unchanged except the LVGL task stack (6 KB to 16 KB). | panel |
| `tests/` | Host tests for core and controller, including two controllers talking over a fake radio. | PC |
| `sim/` | The screenshot simulator: LVGL on the PC drawing into memory. | PC |

## Tasks on the panel

Waveshare's LVGL task (core 1) draws the screen, reads touch, and runs the 50 ms app tick. The tick drains received radio frames, ticks the Controller and the Demo, and renders the View. Because the application runs inside the LVGL task, it needs no locks of its own; serial commands take the LVGL lock before calling in.

The audio task (core 0) synthesizes the sounds once at start-up, then plays queued requests, switching the amp on only while one plays.

In phase 3 the ESP-NOW receive callback, which runs in ESP-IDF's Wi-Fi task, puts frames on a queue for the app tick.

## Radio

`core/protocol.*` is the wire format from the handoff: a 5-byte header (version, type, sender, sequence), then the payload, all under 250 bytes. The `RetryQueue` sends up to 5 tries at 0, 150, 400, 750 and 1150 ms and gives up at 1.5 s, which marks the other panel No signal. Messages where only the latest matters (STATE, DOG, READING, OUTSIDE, EVENT_SUM, MAIL) replace an older pending copy. MAIL is the mail badge (Mike, 2026-10-26, `core/mail.h`): upstairs marks mail for downstairs or takes it back, downstairs acknowledges it, and like a dog it carries a change counter so the later change wins; the panels exchange it at boot and after an outage once it has ever been used. Both panels must run firmware that knows MAIL: one that does not cannot ACK it, which marks the other panel No signal. EVENT carries one calendar event; EVENT_SUM a summary of the whole calendar, sent when the panels find each other (see `docs/calendar.md`). The receiver ACKs everything, duplicates included, and drops repeats by (sender, sequence). Entries expire after 10 s and each boot starts at a random sequence number, so a rebooted sender isn't mistaken for a duplicate.

## What the PC can and can't check

The PC runs the same core, controller, demo and interface code. It can't check display timing, touch, real ESP-NOW, the codec, the RTC, NVS, task stacks or memory pressure on the ESP32. Those wait for the board.
