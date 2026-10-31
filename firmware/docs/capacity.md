# TComm panel capacity

What the panel (Waveshare ESP32-S3-Touch-LCD-4.3C) has, what TComm uses, and what's left. Mike
asked to be kept informed so the panel isn't overbuilt: update this with every feature, and say
in each report where it stands. Figures marked *estimate* are worked out from sizes in the code,
not measured; *measured* ones come from the panel's serial `info` and `bench` or the build.

Last updated 2026-10-25 evening: the radio build for the C6 gateway (below). Before that, the afternoon: the calendar's pickers, repeats, holidays and SD storage, and the Zambretti demo, flashed and running.

## Budget

| Resource | Size | TComm uses | Free | How known |
|---|---|---|---|---|
| Program flash (app slot) | 3 MB | 1.32 MB (44%) | 1.68 MB | measured (build) |
| Internal FAT partition | 9,996 KB | the calendar only while there is no readable card (and this morning's first file) | almost all | measured: formatted at the first boot in 204 ms |
| microSD card | 59,685 MB file system | the calendar: its current file (30 KB at most) and a file per past month, kept for good | all but a few MB, for decades | measured: FAT32, 32 KB clusters, formatted on the panel 2026-10-25; mounts at boot in 76 ms |
| Internal RAM, fixed | 320 KB | 71 KB | 249 KB | measured (build) |
| Internal RAM, while running | | | 101 KB | measured 2026-10-25 afternoon (`info`, free heap); 112 KB that morning, before the pickers, repeats and SD card |
| PSRAM | 8 MB | about 7 MB | 1.21 MB at start-up, 1.00 MB once the screen is drawn; less 0.1 to 0.2 MB with the calendar open, and 0.24 MB with the icicles shown | measured 2026-10-25 afternoon (`info`, SD card mounted); calendar open and icicles estimates |
| Processor (two cores, 240 MHz) | | idle until something changes | plenty | measured: no redraws in 30 s at rest |

## The radio build (2026-10-25 evening)

`bash tools/fw.sh radio` builds the panel with its real ESP-NOW on and demo mode off, for the C6
gateway; it's on the panel now. Measured on the panel:

| | Demo build | Radio build, first | Radio build, now |
|---|---|---|---|
| Program flash | 1.38 MB (44%) | 1.97 MB (62%) | 1.78 MB (56%) |
| Internal RAM, fixed | 72.8 KB | 88.5 KB | 84.7 KB |
| Internal RAM free, running | 101 KB | 66.9 KB | 75.3 KB |
| PSRAM free at start-up | 1.21 MB | 0.73 MB | 0.99 MB |
| PSRAM free once drawn | 1.00 MB | 0.51 MB | 0.77 MB |

The high_perf libraries copy all program code and constants into PSRAM at start-up
(`CONFIG_SPIRAM_XIP_FROM_PSRAM`), so code costs PSRAM as well as flash. The first radio build
started Wi-Fi with Arduino's `WiFi` class, and including `WiFi.h` links Arduino's whole Network
library and the TCP/IP stack with it (LWIP 129 KB, the Network library, esp_netif, EAP/TLS),
none of which ESP-NOW uses. Starting Wi-Fi with ESP-IDF's own calls (board_radio.cpp) dropped
205 KB of code, and the free PSRAM went up 263 KB. What's left of the radio's cost is the Wi-Fi
driver itself and the WPA and crypto code that its start-up always calls. Nothing leaked over 43
requests (0.77 MB free before and after). With the calendar open and the icicles shown, the
radio build should still have about 0.3 MB of PSRAM free (*estimate*, from the figures above).

The mail badge (2026-10-26) adds 3.1 KB of program flash (1,784,114 bytes, 56%), 0.5 KB of fixed
internal RAM, and two 34 x 34 pictures in PSRAM (about 7 KB, *estimate*). Downstairs, while mail
waits, its fade redraws only the badge (a 34 px square), so the redraw cost is small.

The GUI tweaks (2026-10-26): the bell as its own picture (about 9 KB of PSRAM), shaking at about
26 frames a second (measured: 95 redraws in 3.6 s, slowest 14 ms); the Settings roll-down, 15 to
16 frames a second down and 20 to 23 up (measured, the panel's log). The panel draws about 1.3
pixels a microsecond whatever they are, so an animation's cost is the pixels it redraws: see the
comment on the Settings roll in ui.cpp for what was tried. 2.8 KB more flash.

## Where the PSRAM goes (estimates)

The panel keeps ready-drawn pictures so changes appear without flicker. Each full-screen picture
costs 768 KB.

| What | Size |
|---|---|
| Screen frame buffer (one) | 0.77 MB |
| Sky, two copies (old and new, for the change-over) | 1.54 MB |
| Floor tiles, two copies of each of two tiles | 1.19 MB |
| Doorbell, four versions (two copies, each plain and pressed) | 0.58 MB |
| Outside tile, two copies | 0.29 MB |
| Date strip, icons, badges | about 0.15 MB |
| The chosen ringtone (room for the longest, Westminster) and the four short sounds | about 0.9 MB |
| Icicles, once shown | 0.24 MB |
| Calendar, always: 240 event slots (59 KB) and the demo's simulated panel's copy (59 KB); file buffers only while a file is saved | about 0.12 MB |
| Calendar, while past months are shown: up to six months' files, read from the card and kept | a few KB to about 0.1 MB |
| Calendar fonts (11 new sizes, read-only data run from PSRAM) and code | about 0.1 MB |
| Calendar, while open: its LVGL objects and bubble pictures (freed when it rolls up) | about 0.1 to 0.2 MB |
| LVGL, program code run from PSRAM (core 3.2.0 high_perf), the rest | the remainder |

Easy savings if it's ever needed: the doorbell's four versions could be two (0.29 MB back); the
sky could keep one copy (0.77 MB back) at the cost of a brief redraw when it changes.

## Speed (measured on the panel)

| Drawing | Time |
|---|---|
| Floor tile (132 shapes) | 0.38 s, on the drawing core |
| Sky | 0.10 s (0.06 s in strips) |
| Outside tile, doorbell tile | 0.12 s, 0.11 s |
| First frame after start-up | 0.12 s |

## The calendar (2026-10-25)

In memory: 240 events (what's upcoming, what repeats, the last 92 days, and recent deletions),
244 bytes each. Older events go to a file per month on the SD card and are read back when the
calendar shows that month; nothing is deleted (Mike, 2026-10-25). An event takes 30 to 240 bytes
in a file, so even 20 events a week for 50 years is under 10 MB of the 64 GB card. When the 240
fill with recent events, the one that ended longest ago is archived early; only 240 upcoming
events would fill it ("The calendar is full"). Serial `info` prints the slots in use and where
the files are; `sd` describes the card.

Measured on the panel 2026-10-25: a calendar save to the card takes 24 ms (170 bytes). Free
PSRAM was 0.89 MB after the calendar had been opened and closed (1.00 MB before). The roll ran at
5 fps (each frame redraws most of the screen, about 190 ms), so it's off; the calendar appears
at once.

## Limits to keep in mind

- **PSRAM is the tightest.** A new full-screen picture or a second set of tile canvases would
  take a large share of what's left.
- **Display bandwidth.** The screen is refreshed straight from PSRAM. Heavy PSRAM traffic gave
  streaks and jitter before (hardware-notes.md). Small animations (the pulsing heat sun) are
  fine; anything that redraws large areas continuously is a risk.
- **Storage doesn't add memory.** The microSD and the internal FAT partition hold files, but
  anything shown or played still has to fit in RAM, or be streamed.
- **Radio.** ESP-NOW messages are at most 250 bytes; the largest TComm message is well under.

## The microSD card

Fitted (64 GB, Mike, 2026-10-24) and, since 2026-10-25, the calendar's home (Mike: "rely on the
SD card"). The slot is wired as one-bit SDMMC (GPIO11 command, GPIO12 clock, GPIO13 data) with the
card's D3 on the IO expander (EXIO4), so it's slower than a four-bit slot.

Measured 2026-10-25: the card answered (an SDHC/SDXC card, 59,700 MB usable) but came formatted
exFAT, which core 3.2.0's FAT library can't read (FF_FS_EXFAT is 0). With Mike's OK the panel
formatted it as FAT32 with 32 KB clusters, in 16.5 s (`sd format yes`), and copied the calendar
onto it. It mounts at boot in 76 ms. Not yet measured: how long reads and writes take (each is
logged as it happens).

Good uses, if a feature calls for them: a long history (doorbell and status log, inside and
outside readings for graphs), real recorded sounds streamed instead of synthesized, settings
backups, firmware files. Writes to a card can stall for a moment and can be damaged by a power
cut mid-write, so they belong in a background task. For small files the internal FAT partition
is simpler (no card to work loose).
