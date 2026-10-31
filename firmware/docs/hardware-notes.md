# Hardware notes: Waveshare ESP32-S3-Touch-LCD-4.3C

Sources, in order of trust: the V1.1 schematic (`ref/ws43c/hardware/schematics/`), Waveshare's example source, then their `HARDWARE_REFERENCE.md`. Waveshare repo commit `44e412f` (2026-08-14). Anything marked UNVERIFIED has not been measured or seen on Mike's board.

## Corrections to the handoff brief

| Handoff said | What the schematic and code show |
|---|---|
| Amp enable on EXIO4 | Amp enable (PA_CTRL to the NS4150B) is **EXIO3**. EXIO4 is the SD card's D3/CS line (net SDCS). There is no double assignment, so the SD card and the amp can be used together. |
| I2S data in GPIO15, data out GPIO43 | From the ESP32's side it's the other way round. GPIO15 carries audio out to the ES8311 (speaker). GPIO43 carries audio in from the ES7210 (microphones). |
| Backlight PWM through the expander, steps undocumented | EXIO2 is the DISP net. It drives the panel's DISP pin and the enable pin of the backlight boost converter (AP3032). Brightness comes from a separate expander pin, EXIO_PWM (test pad "BL", P16), which feeds the boost converter through R11. The PWM register is 8 bits. In Waveshare's code it's **inverted**: lower value is brighter. Their helper takes 0-100, multiplies by 2.55 and caps input at 95 so the screen never goes fully dark. |
| Coin cell not in the package list, holding time unknown | The PCF85063A is powered through two diodes, one from 3.3 V and one from **BAT1, a coin-cell holder on the board** (near MIC2 on the silkscreen). With no cell in BAT1 the clock should lose its time whenever power is removed. Cell size is not printed on the schematic (UNVERIFIED). |
| VOUT resistor selects 3.3 V or 5 V | VOUT on the screw terminal sits on the Vout_3V3 net. The 3.3 V / 5 V choice is header/jumper **H8**, which sets the pull-up voltage (I2C_VCC) on the terminal's SDA and SCL. Those lines pass through a level shifter (M1, NDC7002N). The SHT40 is rated to 3.6 V, so H8 must be on 3V3. Measure VOUT and the idle SDA voltage on the terminal before connecting the sensor. |

## Screw terminal (P1), 16 positions

From the VIN end (schematic P1 pins 1-16, the PCB silkscreen, and the label on Mike's case, where the terminal runs down the right edge seen from the back with VIN at the bottom): 1 VIN (6-36 V), 2 GND, 3 VOUT (3.3 V, the Vout_3V3 rail), 4 GND, 5 SDA, 6 SCL, 7 GND, 8 GND, 9 5V, 10 5V, 11 DO0, 12 DO1, 13 DI COM, 14 GND, 15 DI0, 16 DI1. The case groups 1-2 as "6~36V", 3-6 as "I2C", 7-10 as "POWER", 11-16 as "Isolated I/O". The SHT40 takes positions 3-6. SDA and SCL there pass through the M1 level shifter to GPIO8/9; their pull-up voltage comes from H8, whose factory setting is 3V3 ("The default paste is 3V3" on the schematic). The wiring guide is `docs/sensor-wiring.html`.

Mike wired an SHT40 to positions 3-6 on 2026-10-22: found at start-up (serial 0966D95B, 100 kHz), reading 71.2 F and 51.6-52.9 % on his bench, 5 reads and none failed in the first minute. On the screen the panel's own tile showed about 71 F, and the humidity jumped when Mike breathed on the sensor. Not yet checked: readings against a reference thermometer, and how much the panel's own heat moves them once mounted (Settings > Temp offset corrects a steady difference).

## Confirmed by schematic and code

The IO expander is a CH32V003 microcontroller running Waveshare firmware at I2C address 0x24. The example comments call it a CH422G; the schematic part (U10, 20-pin PA/PC/PD pinout) and Waveshare's own `docs/components.md` say CH32V003. Registers used by the examples: 0x02 pin mode, 0x03 outputs, 0x04 inputs, 0x05 PWM, 0x06 ADC (battery voltage through a 20k/10k divider), 0x07 RTC interrupt line.

| Expander pin | Net | Use |
|---|---|---|
| EXIO0 | DI0 | isolated input 0 |
| EXIO1 | CTP_RST | touch reset |
| EXIO2 | DISP | panel enable and backlight boost enable |
| EXIO3 | PA_CTRL | speaker amp enable |
| EXIO4 | SDCS | SD card D3/CS |
| EXIO5 | DI1 | isolated input 1 |
| EXIO6 | DO0 | isolated output 0 |
| EXIO7 | DO1 | isolated output 1 |

I2C bus on GPIO8 (SDA) and GPIO9 (SCL), 400 kHz in the examples. Expected devices: ES8311 codec 0x18, expander 0x24, ES7210 0x40 (its A0 and A1 are tied low), PCF85063A 0x51, GT911 0x5D (0x14 if INT is high at reset). On paper nothing sits at 0x44, so the SHT40 should fit. The bring-up sketch scans the real bus.

Display: 800x480 RGB565 parallel, 16 MHz pixel clock, one framebuffer in PSRAM (Waveshare's setup had two; see "Display link" below). Touch interrupt GPIO4. SD card is one-bit SDMMC on GPIO11 (CMD), GPIO12 (CLK), GPIO13 (D0), with D3 driven high through EXIO4 before mounting. Measured 2026-10-25: Mike's 64 GB card answered at 20 MHz, came exFAT (unreadable by core 3.2.0), and was formatted FAT32 with 32 KB clusters in 16.5 s. Core 3.2.0's own format (`SD_MMC.begin(..., true)`) would have used 512-byte clusters; and its `SD_MMC.totalBytes`/`usedBytes` measure FatFs drive 0, the FAT partition when that mounts first, so the firmware asks ESP-IDF by mount point instead. I2S: MCLK GPIO6, BCLK GPIO44, LRCK GPIO16.

The schematic carries two notes worth keeping. The screen alone draws close to 550 mA, and a low-output (under 500 mA) computer USB port may not be enough. And if a lithium battery is ever fitted to the battery connector, switch SW1 off while connected to a computer.

## Toolchain (pinned to Waveshare's CI)

arduino-cli 1.5.1, ESP32 Arduino core 3.3.11, LVGL 8.4.0 (copied from Waveshare's bundled library into `libraries/`, with their `lv_conf.h`). Board string:

```
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadMode=default,UploadSpeed=921600,LoopCore=1,EventsCore=1
```

The `app3M_fat9M_16MB` partition table (core 3.3.11) has two 3 MB app slots (ota_0 and ota_1), so maintenance-mode firmware updates can write the idle slot and fall back if the new image fails. It also has a 9.9 MB FAT partition and a 64 KB core-dump partition. The FAT area could hold the four chime files and converted fonts, which would remove the microSD from the parts list. The core dump will help explain any unplanned reboot during the 7-day run. The bring-up sketch uses 376 KB of the 3 MB slot.

## Things that will matter for our firmware

Waveshare's I2C helpers wrap every transfer in `ESP_ERROR_CHECK`, so any failed transfer reboots the board. A missing or unplugged SHT40 would cause a reboot loop. Our board layer has to return errors instead, and it has to own the single I2C lock the brief asks for.

The examples use ESP-IDF's newer `i2c_master` driver. Arduino's `Wire` library must not be used on the same port, and sensor code should go through the same bus handle.

The year byte in the RTC is two digits. Waveshare's driver treats it as 1970 + n; the datasheet convention is 2000 + n. Our firmware uses 2000 + n. The factory firmware's choice is unknown, which is one of the things the bring-up sketch prints.

Without a coin cell, both panels lose the time in a household power cut. Copying the clock from the other panel's STATE message doesn't help when both lost power together. They'd stay without time until maintenance mode fetches it from the network or someone sets it by hand.

## Measured on Mike's board (2026-10-21, bring-up sketch)

- I2C: 0x18, 0x24, 0x40, 0x51, 0x5D, nothing else. 0x44 is free.
- Backlight: lower register value is brighter, as in Waveshare's code. Single register steps are visible. 224 is the last value before the screen goes black, and Mike judged it right for midnight. Waveshare's 0-100 helper caps at 95, which writes 242, so it can't reach this range precisely; our firmware should write the register directly.
- RTC: does not hold time without a coin cell. After 60 s unplugged it restarts at 2000-01-01 00:00:00 with the OS flag set, and CTRL1 resets to 0x00, so the firmware must rewrite the 12.5 pF CAP_SEL bit whenever it finds the OS flag set. The factory firmware had left the clock in 1970-based format at 2025-09-15.
- Speaker: loud enough for chimes. Mike's levels: volume 90 by day, 80 at night. At 100 the Westminster chime rattled the plastic speaker at the end of each note; at 90 it's clean and still easily heard ("West HI", an octave up, was his pick). No hiss with the amp held on or switched per sound. At volume 100 Waveshare's codec setup puts the ES8311 DAC at +3.5 dB, so full-scale audio clips there; master the chime files about 3.5 dB below full scale.
- Opening the USB serial port from a plain .NET serial client sometimes resets the board (reset reason 11, USB). Keep that in mind during the 24-hour and 7-day runs: attaching a monitor restarts the panel.
- Flashing over USB works without BOOT: esptool resets the board into download mode by itself.

## Measured with the phase 2 firmware (2026-10-22, demo mode)

- Sounds are synthesized in 2.41 s at boot, in their own task. Well inside the 5 s watchdog.
- Free memory once running: 2.8 MB PSRAM, 249 KB internal heap.
- Artwork drawing times (serial `bench`): sky 0.61 s (0.63 s at night with the moon and stars), floor tile 0.35 s, doorbell tile 0.13 s, outside tile 0.08 s, date strip 0.04 s, a status badge 7 ms. That's after making the rasterizer about twice as fast; the first build was over 5 s for the first frame and tripped the watchdog.
- GUI spec rebuild (2026-10-24), artwork from the Pd boards: a floor tile is 132 shapes. As first flashed it took 1.18 s (sky 0.96 s, outside tile 0.22 s). Fills now store their solid stretches directly instead of blending every pixel five times, and the clip's solid part is exact row by row. On the panel, in strips as the drawing task does it: floor tile 0.38 s, sky 0.06 s (0.10 s straight into PSRAM, moon and stars included), outside tile 0.12 s, doorbell tile 0.11 s, date strip 0.05 s, badge 6 ms. First frame after boot 0.12 s. Free PSRAM at start-up 1.71 MB.
- First flash: the first frame after boot took 2.4 s, a status change 0.33 s and each sky redraw 0.6 s, all inside the interface task, so touch stalled meanwhile. Since then the large artwork is drawn by its own task on core 0 into second buffers: the first frame holds the interface 0.21 s and a status change no longer stalls it. Free after that change: 1.6 MB PSRAM, 207 KB internal.
- With nobody touching the panel, LVGL redraws nothing (0 redraws in 30 s, measured with `info`).
- Display link, 2026-10-22, what Mike saw on the stock core 3.3.11 libraries (32-byte data-cache lines, code in flash), in order of trying:
  - Waveshare's setup (16 MHz, 10-line copy buffers, LVGL direct mode in two PSRAM frame buffers): streaks at the left of lines and a picture shifted a row or two, worst during large redraws, sometimes lingering.
  - 12/14/16 MHz with 10- or 20-line copy buffers: all streaked; 20 lines worst. Artwork drawn through internal-RAM strips: no better.
  - No copy buffer (the display reads PSRAM by DMA): no streaks, no shift, but the whole picture jitters sideways and blinks black, even at rest.
  - LVGL painting into a 40-line internal-RAM buffer with one frame buffer: still jitter without copy buffers, streaks with them.
  - Espressif's FAQ names this ("drift") and its cure: with copy buffers the data-cache line must be 64 bytes, plus code run from PSRAM and -O2. Those are build settings of the precompiled libraries. Mike chose the move to core 3.2.0 with Espressif's high_perf libraries (README, Toolchain).
- On core 3.2.0 with the high_perf libraries (2026-10-22): the firmware built with no changes and no warnings (961 KB). Sounds ready in 1.67 s (was 2.2), LVGL's start-up redraws 262 ms (was 398), free PSRAM 1.45 MB (the program now runs from PSRAM), free internal RAM 140 KB. Artwork drawing times unchanged (sky 0.61 s, tile 0.34 s). On display preset C (16 MHz, 10-line copy buffer) Mike saw no streaks, no jitter and no black flashes through status changes, the status menu and Settings scrolling (2026-10-22). C is the default for a board that has never chosen one. The flash Mike saw after using the doorbell went away with it, so it was the same display problem (the doorbell's sound preview never flashed). The other presets stay in Settings > Test: Espressif's FAQ says Wi-Fi and flash writes can bring drift back, so phase 3 (ESP-NOW) should be checked on the same screens.
- Core 3.2.0's esptool resets the board through its watchdog after an upload; the USB port drops out and esptool reports a serial exception after "Leaving...". The image is written and verified by then (`tools/fw.sh` checks for the four "Hash of data verified" lines). Opening the serial port right after an upload can also restart the board once.
- Display link (serial `pattern` test screen, Mike's eyes):
  - With Waveshare's 10-line copy buffers, short streaks appeared at the left end of lines: stale pixels from rows above, the sign of a late refill from PSRAM. With 20-line buffers the stripes are clean and Mike saw no glitches.
  - With Waveshare's vertical back porch of 8 lines the picture sat one row high: the top row was lost and a blank row showed under the bottom one. With 9 the red top and blue bottom bands look equal and there's no gap (Mike, 2026-10-22).
- The panel starts scanning (and calling its vsync interrupt) before LVGL's task exists. Waveshare's port notified a NULL task handle there, which asserted and rebooted in a loop; `lvgl_port.cpp` now checks the handle.

## Ringtones (2026-10-24)

- 17 ringtones, synthesized on the panel. Only the chosen one is held in PSRAM (room for the longest, Westminster, 736 KB); choosing another renders it in its place before it plays.
- First build: Westminster took 2.25 s to render and all sounds 2.8 s. The Arduino build's -Os had put the sine lookup in its own function, called for every sample with the registers spilled to the stack (about 100 cycles a sample). With `#pragma GCC optimize("O2")` in synth.cpp and struck partials back on the old rotating phasor (10 instructions a sample), the render times measured with serial `ring N` are:
  - Westminster 424 ms at boot (410 ms when chosen), all sounds ready 582 ms after start-up (the old engine took 1.67 s for its five).
  - Ding-dong 110, Rising chime 187, Church bells 363, Rounds 668, Music box 305, Old bell 387 ms.
  - (Five sci-fi ringtones, 92 to 554 ms, were removed the same day.)
  - Chickadee 74, Cardinal 93, Robin 150, Wind chimes 411, Water drops 95 ms.
  - Nightingale 143, Woodpecker 146, Frogs 216, Crickets 88, Loon 404 ms (settings version 2 firmware). Chickadee, saved as 12 by version 1, came back as 7.
  - After six plays through serial `ring`: no codec or amp errors, and free PSRAM unchanged at 1.74 MB.
  - Redone with noise and drift (12 ringtones, ids): Church bells 1090 ms the first time, 574 ms after (the gain is kept); Loon 1170 / 605 ms (its drifting pitch costs three table lookups a sample); Woodpecker 147 ms; Cardinal 71 ms.
  - Loudness at the microphones in the middle of each sound, volume 90 (`listen`, RMS): Westminster -1.5, Church bells -8.2, Loon -3.2 dBFS (the microphones clip on peaks at this volume). The end-of-sound check only sees the last second, so a long ringing tail reads much quieter there (bells -40).
- So a tap in the ringtone list plays within 0.1 to 0.7 s. Rendering is two passes (peak, then write); a table of each ringtone's gain would halve these times if that's ever needed.
- Free PSRAM 2.07 MB a second after start-up (1.45 MB before, with two chimes held and a full-length float scratch at boot), 1.72 MB after 90 s of use with the ringtone list open.
- A chosen ringtone survives a restart: Chickadee (then number 12) came back after a USB reset. Settings version 2 renumbers the ringtones and converts version 1's numbers on load.
- Silent sound, 2026-10-24, found and fixed. After a few quick taps in the ringtone list the sound stopped until a restart or until a serial monitor opened. Cause: `Serial.setTxTimeoutMs(0)`. In core 3.2.0 `HWCDC::write` counts its retries down from the timeout, so 0 wraps round and a write waits until the laptop reads. With the USB cable in and no monitor open, the send buffer filled after a few log lines and the audio task (which logged every ringtone it rendered) stopped inside `Serial.printf`. The screen kept working. What showed it: a log line stamped 82.8 s stored after one from 96.9 s, "sound queue full" while Mike tapped, and the sound coming back the moment the laptop opened the port. Fix: a 2 ms timeout, and only the loop task writes to serial; other tasks write to the event log (`board::elog`, serial `log`). Mike then tapped through the whole list twice with no monitor open and every sound played.
- Ruled out on the way: the amp (the expander's pin read high), the codec (its registers unchanged), and the I2C bus (serial `i2c test`: 4000 reads of the codec and touch IDs at 400 and 100 kHz with the sensor cable attached, none failed or wrong).
- Every sound is now checked through the microphones (a task listens all the time): one they don't hear is logged as SILENT with the amp pin and the codec's registers, and gets a codec reopen, then an expander set-up. At volume 60 sounds peak around -12 dBFS at the microphones and the room around -49.

## Core 3.3.11 settings found in the overnight review

These come from reading the core's sdkconfig and source, not from the board.

- Task watchdog: 5 s, panics, and watches the core-0 idle task only. Anything that runs more than a few seconds on core 0 without yielding reboots the board. Sound synthesis now yields between notes, and the LVGL task adds itself to the watchdog.
- `CONFIG_LOG_MAXIMUM_LEVEL=1`: every `ESP_LOGW`/`ESP_LOGI` is compiled out. Field diagnostics have to use `Serial`.
- USB serial (HWCDC) blocks writes for up to 2 s per line when the laptop is attached but nothing is reading. The firmware calls `Serial.setTxTimeoutMs(0)` so lines are dropped instead.
- The RGB LCD interrupt is not IRAM-safe, so a flash write (NVS) can glitch one frame. NVS writes are deferred to the loop task.
- `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`: plain `malloc` puts small blocks in internal RAM, where Wi-Fi needs room. LVGL's heap is pointed at PSRAM.

## Still UNVERIFIED

- The dimmest readable backlight level in a dark room, and whether it flickers at low levels. The schematic suggests the PWM is filtered into the boost converter, which would make the LED current smooth, but that's a reading of the drawing, not a measurement.
- Speaker size, impedance, loudness.
- Current draw with backlight and speaker running.
- ESP-NOW peer address: the chip's Wi-Fi station MAC is the usual choice. The bring-up sketch prints it to compare with the flash tool's 14:C1:9F:54:BE:20.
