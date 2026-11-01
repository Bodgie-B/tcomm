# TComm panel firmware

Firmware for two wall panels on the basement stairs (UPSTAIRS and DOWNSTAIRS), on the Waveshare ESP32-S3-Touch-LCD-4.3C. The panels talk to each other over ESP-NOW, and an ESP32-C6 on the Raspberry Pi sends them the outside weather and Lucy (`../gateway/`).

Where it stands (2026-10-31): the first panel runs the radio build against the C6 gateway on the Pi, with weather, Lucy, the mail badge and the calendar working. The second panel hasn't arrived; `docs/second-panel.md` has the steps for it. Two panels talking to each other is still untested.

Paths in this folder are relative to `firmware/`. The briefs live in the repository's own `docs/`: `../docs/CLAUDE_CODE_HANDOFF.md` and `../docs/design/README.md`.

## Layout

| Path | What it is |
|---|---|
| `tcomm/` | The panel firmware (Arduino sketch). Structure in `docs/architecture.md`. |
| `updater/` | The Wi-Fi updater, a small sketch in each panel's second app slot (`docs/wifi-updates.md`) |
| `../gateway/` | The ESP32-C6-GEEK gateway between the Pi and the panels, its test listener, tests and docs. See `../gateway/docs/gateway-c6.md`. |
| `bringup/tcomm_bringup/` | Phase 1 sketch: I2C scan, RTC test, backlight steps, speaker tone and chime tests |
| `libraries/` | LVGL 8.4.0, cut down to its source (its demos, examples, docs, tests and scripts left out; the full release is github.com/lvgl/lvgl, tag v8.4.0), and `lv_conf.h` (from Waveshare; `LV_MEM_CUSTOM` changed to 1) |
| `tests/` | Host tests for the core logic and controller |
| `sim/` | PC screenshot simulator (LVGL drawing into memory) |
| `tools/` | `fw.sh` (build and flash the panel, the gateway or the listener), `build_sim.sh` (tests, screenshots, sounds), `make_fonts.sh` (font conversion), `raw2png.py` |
| `assets/fonts/src/` | Jost and Atkinson Hyperlegible sources with their OFL licences |
| `docs/` | Hardware notes, decisions, checklists, the design boards, screenshots, sounds |

## Toolchain

Arduino IDE 2.3.10 (installed with winget) and the arduino-cli 1.5.1 bundled inside it. The laptop's antivirus removed a standalone arduino-cli, so builds use the IDE's copy. Builds land in `%LOCALAPPDATA%\tcomm\b` because Windows path limits break builds inside deep folders.

The panel firmware builds with **ESP32 Arduino core 3.2.0 and Espressif's high_perf libraries** (Mike's decision, 2026-10-22). The stock libraries of every core version are built with 32-byte data-cache lines and code in flash, and the RGB display then can't get enough PSRAM bandwidth: streaks at the left, a shifted picture, or (without copy buffers) a picture that jitters and blinks black. Espressif's fix for the Arduino IDE is a high_perf build of the libraries with 64-byte cache lines, code and read-only data run from PSRAM (XIP) and `-O2` ([ESP FAQ](https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/peripherals/lcd.html), [arduino-esp32-sdk](https://github.com/esp-arduino-libs/arduino-esp32-sdk)); the newest is for 3.2.0. It lives in its own Arduino folder, `%LOCALAPPDATA%\tcomm\a15`, so the IDE's own install (core 3.3.11, Waveshare's CI version, still used for the bring-up sketch) is untouched. To set it up again in Git Bash:

```
export ARDUINO_DIRECTORIES_DATA="$LOCALAPPDATA/tcomm/a15" ARDUINO_DIRECTORIES_DOWNLOADS="$LOCALAPPDATA/tcomm/a15/staging" ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
ACLI="$LOCALAPPDATA/Programs/arduino-ide/resources/app/lib/backend/resources/arduino-cli.exe"
"$ACLI" core update-index && "$ACLI" core install esp32:esp32@3.2.0
# then replace the contents of $ARDUINO_DIRECTORIES_DATA/packages/esp32/tools/esp32-arduino-libs/idf-release_v5.4-2f7dcd86-v1
# with the contents of arduino-esp32-libs-all-release_v5.4-2f7dcd862a from
# https://dl.espressif.com/AE/esp-arduino-libs/esp32-3.2.0-h.zip (343 MB, SHA-256 3d6a0ca36c644b1ad1b3b1c405423c7e2fcd947ae8bd2a733d54289c7d540b07)
```

For the PC side: WinLibs GCC 16.2 (winget), Node (for `npx lv_font_conv`), Python with Pillow and fonttools. On this laptop Python can't see `%LOCALAPPDATA%`, so run it with `-P` and keep its inputs inside the firmware folder.

## Build and flash the panel firmware (Git Bash)

The scripts change to the firmware folder themselves, so from the repository's root `bash firmware/tools/fw.sh` works the same.

```
bash tools/fw.sh          # compile only (the demo build: a simulated other panel and gateway)
bash tools/fw.sh COM5     # compile, then upload if the compile succeeded
bash tools/fw.sh radio COM5       # the real build: ESP-NOW on, demo off (what the panels run)
bash tools/fw.sh mac COM7         # read a board's MAC, writing nothing
bash tools/fw.sh updater COM5     # the Wi-Fi updater into the panel's second slot (docs/wifi-updates.md)
bash tools/fw.sh ota 192.168.1.37 # the radio build over Wi-Fi, to a panel waiting in its updater
bash tools/fw.sh gateway COM7     # the same for the C6 gateway (../gateway/docs/gateway-c6.md)
bash tools/fw.sh listener COM5    # the gateway's test listener, onto the panel
```

`tools/fw.sh` holds the board string and points the IDE's arduino-cli at the 3.2.0 + high_perf folder. Check the port with Mike before every upload. Upload writes bootloader, partition table and app, and never erases the whole chip. esptool resets the board into download mode by itself; if it ever can't, hold BOOT, unplug and replug USB-C, release BOOT, upload again, then press RESET. The bring-up sketch builds the same way with `bringup/tcomm_bringup` and its own build path.

Build from Git Bash, never PowerShell: on 2026-10-25 Bitdefender's Advanced Threat Defense blocked a compile started from PowerShell (powershell.exe running arduino-cli.exe) and quarantined the gateway's first build script. From bash the same compiles go through.

## Tests and screenshots

```
bash tools/build_sim.sh
```

Runs the host tests, writes the synthesized sounds to `docs/sounds/` (the 17 doorbell ringtones in `docs/sounds/ringtones/`, numbered as Settings stores them), and renders every scenario in `sim/sim_main.cpp` to `docs/screens/`.

## Secrets

ESP-NOW keys, the MAC table, the panels' location, and the Wi-Fi name and passwords go in `tcomm/src/config/secrets.h`, copied from `secrets.example.h`. The C6 gateway and its listener are built from the same file. The repository's `.gitignore` keeps `secrets.*` out, and `firmware/.gitignore` lets the template back in. The panel firmware joins the home Wi-Fi only for a few seconds, to get the time after a power cut; the updater joins it for new firmware. Nothing from Mike's other projects goes in this repository.

## Licences

Waveshare's code (`tcomm/src/ws/`, `bringup/tcomm_bringup/src/`, `libraries/lv_conf.h`) is Apache 2.0 (copies of the licence sit beside it as `LICENSE-waveshare*.txt`). LVGL is MIT. Jost and Atkinson Hyperlegible are SIL OFL 1.1 (`assets/fonts/src/*/OFL.txt`).
