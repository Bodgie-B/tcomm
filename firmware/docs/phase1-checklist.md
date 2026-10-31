# Phase 1 bring-up checklist

Done when every item below has a result and the flash path has worked at least twice without surprises. Confirm the COM port before each flash. Factory restore if needed: Flash Download Tool, `ESP32-S3-Touch-LCD-4.3C-Test.bin` at 0x00.

## Our bring-up sketch (`bringup/tcomm_bringup`)

| Check | How | Result |
|---|---|---|
| Flash path | Compile and upload commands in the README. Note whether it needed BOOT + replug. | 2026-10-21: passed. esptool reset the board into download mode over USB by itself, no BOOT needed. All four images hash-verified. |
| MACs | `info` over serial. Station MAC should be 14:C1:9F:54:BE:20. | Station 14:C1:9F:54:BE:20, matches. Under Arduino core 3.3.11 the soft-AP MAC is 16:C1:9F:54:BE:20 and Bluetooth is 14:C1:9F:54:BE:21, not the :21 and :22 the flash tool listed (the core derives them differently). ESP-NOW uses the station MAC, so this doesn't matter. Flash 16 MB, PSRAM 8 MB, ESP-IDF 5.5.5. |
| I2C scan | Printed at boot and on `scan`. Expect 0x18, 0x24, 0x40, 0x51, 0x5D. 0x44 should be free. | Exactly 0x18, 0x24, 0x40, 0x51, 0x5D. 0x44 is free for the SHT40. |
| Factory clock format | First boot prints the RTC as the factory firmware left it, with the year byte read both ways. | Year byte 55, weekday 1, so 2025-09-15 (a Monday) in Waveshare's 1970-based format. It read 13:19 and was ticking with the OS flag clear. The factory firmware probably sets a fixed date at boot (UNVERIFIED). Set to laptop time 2026-10-21 17:51:16 at 17:51, 2000-based. |
| Display | Bring-up screen draws: text, swatches, buttons. | 2026-10-21: Mike sees the TComm bring-up screen with the color bands and the BRIGHTER, STEP and DIMMER buttons. |
| RTC hold | `time` with the current time, unplug 60 s, replug, `rtc`. OS flag 1 or a wrong time means it didn't hold. | 2026-10-21: does NOT hold. Set 17:51:16, unplugged 60 s, read at 18:00:47: clock showed 2000-01-01 00:00:53 (power-on default, counting from replug), OS flag 1, CTRL1 back to 0x00 (12.5 pF load setting lost). No coin cell in BAT1. |
| Backlight polarity | BRIGHTER/DIMMER zones. Which direction is brighter? | 2026-10-21: matches Waveshare. Lower register value is brighter. Mike sees a change at single steps (step 1). |
| Backlight range | Register value where the screen is first unreadable in a dark room, and where it goes black. | 2026-10-21: 224 is the last step before the screen goes black. Mike: bright enough to see, not too bright for midnight use. Candidate for the dimmest night level. |
| Backlight steps | STEP zone switches between steps of 16 and 1. Can you see single steps near the dark end? | Single steps visible (checked in room light, not yet near the dark end in a dark room). |
| Flicker | Slow-motion phone video at the dimmest readable level. | |
| Palette | Day, evening and night swatches. Do PRIVATE and SLEEPING read as different colors at night level? | |
| Touch zones | All buttons respond at the edges. | |
| Speaker loudness | Bring-up TONE and CHIME (full-scale test sounds) at several volumes. | 2026-10-21: Mike: volume 100 is right for daytime, 80 for night. The built-in speaker is fine for the chimes. At volume 100 the ES8311 DAC gain reads 0xC6 = +3.5 dB, so sound files should peak around -3.5 dBFS to stay unclipped there. |
| Westminster chime (low, E5 C5 D5 G4) | WEST button, struck-bar timbre, peak -3.5 dBFS. | 2026-10-21: at volume 100 the ringing notes rattle the speaker, and the whole chime sounds about 30% quieter than the 1 kHz tone or the E6/C6 test chime. Clean at 90. Amp held on is slightly better at 100, but each ringing note still shakes a little at its tail. Electrically it isn't clipping, so this is cone excursion on 392-660 Hz notes that ring for seconds. |
| Westminster variants | WEST (low), WEST HI (octave up, E6 C6 D6 G5), WEST 2 (low, lighter bass), all at -3.5 dBFS peak. | 2026-10-21: Mike's pick is WEST HI at volume 90: best sound and carries better. This becomes the normal doorbell chime. |
| Speaker hiss and amp switching | Listen close with the amp held on and with it on only during sounds. | 2026-10-21: no hiss either way, and no audible difference between the two modes (no pop reported). Switching the amp off between sounds stays in the plan since it costs nothing, but it isn't needed for hiss. |

## Waveshare demos

Build any of them with the README's compile command, pointing at `ref/ws43c/examples/arduino/<name>` and a build path named after the demo. Compiled 2026-10-21 into `%LOCALAPPDATA%\tcomm\b\<name>`: all six compiled cleanly: 03_lcd (1.18 MB), 05_sd, 06_touch, 11_speaker_microphone, 12_lvgl_transplant (641 KB), 14_lvgl_slider (594 KB). Flashing a demo replaces the bring-up sketch; reflash it afterwards.

| Demo | What to look for | Result |
|---|---|---|
| 03_lcd | Image shows, colors correct, no tearing | |
| 04_isolation_io | Needs DO0 wired to DI0 and DO1 to DI1. Red screen without wiring is expected. Skip unless wired. | |
| 05_sd | Card mounts (FAT32), size printed | |
| 06_touch | Five touch points tracked to the corners | |
| 11_speaker_microphone | Records 5 s, plays back. Note loudness and hiss. | 2026-10-21: works. A little hiss (the demo leaves the amp on throughout). Playback at volume 100 was "not very loud at all". Not yet a fair loudness test: playback level depends on how loud the recording was (mic gain 30 dB, voice at arm's length), so a full-scale test tone is needed before judging the speaker. |
| 12_lvgl_transplant | LVGL widgets demo runs smoothly | |
| 14_lvgl_slider | Slider dims the backlight; battery label reads something (no battery fitted) | |
| 15_udp_tcp_ntp | Needs Wi-Fi credentials in an untracked `arduino_secrets.h`. Optional for phase 1. | |
