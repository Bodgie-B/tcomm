#!/usr/bin/env bash
# Builds the panel firmware with arduino-esp32 3.2.0 and Espressif's "high_perf" libraries, and
# with a port given, uploads it. Confirm the port with Mike before every upload.
#
#   bash tools/fw.sh                   compile only
#   bash tools/fw.sh COM5              compile, then upload if the compile succeeded
#   bash tools/fw.sh radio [COM5]      the panel with its real ESP-NOW on and demo mode off, for
#                                      testing against the C6 gateway (bash tools/fw.sh COM5 goes back)
#   bash tools/fw.sh gateway [COM7]    the same for the ESP32-C6-GEEK gateway (../gateway/docs/gateway-c6.md)
#   bash tools/fw.sh listener [COM5]   the same for the gateway's test listener, onto the panel
#   bash tools/fw.sh mac COM7          read a board's MAC and write nothing (a new panel's line in
#                                      secrets.h); close any serial monitor on that port first
#   bash tools/fw.sh updater [COM5]    the Wi-Fi updater (docs/wifi-updates.md), over USB into the
#                                      panel's second app slot; the panel firmware stays as it is
#   bash tools/fw.sh ota HOST          the radio build, over Wi-Fi, to a panel waiting in its
#                                      updater (HOST: the address on its screen, or tcomm-upstairs.local)
#   bash tools/fw.sh ota-both          the same build to both panels at once (Mike, 2026-11-07): start
#                                      Settings > Update over Wi-Fi > BOTH PANELS (or serial "update
#                                      both") on a panel first; tools/find_panels.py finds the two
#                                      updaters, and the build goes to downstairs, then upstairs
#
# Run it from Git Bash. Bitdefender's Advanced Threat Defense stopped the same compile started from
# PowerShell (2026-10-25: powershell.exe running arduino-cli.exe) and quarantined the gateway's
# own build script; from bash it hasn't objected.
#
# Why 3.2.0 + high_perf: the stock libraries are built with 32-byte data-cache lines and code in
# flash, which starves the RGB display of PSRAM bandwidth (streaks, jitter). Espressif's fix for
# the Arduino IDE is a high_perf build of the libraries: 64-byte cache lines, code and read-only
# data run from PSRAM, -O2. It exists up to 3.2.0. It lives in its own Arduino folder,
# %LOCALAPPDATA%\tcomm\a15, so the Arduino IDE's own install (3.3.11) is untouched. To set it up
# again: see README, "Toolchain". The same folder has the ESP32-C6 parts the gateway needs.
set -uo pipefail
cd "$(dirname "$0")/.."
export ARDUINO_DIRECTORIES_DATA="$LOCALAPPDATA/tcomm/a15"
export ARDUINO_DIRECTORIES_DOWNLOADS="$LOCALAPPDATA/tcomm/a15/staging"
export ARDUINO_DIRECTORIES_USER="$LOCALAPPDATA/tcomm/sketchbook"
export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
ACLI="$LOCALAPPDATA/Programs/arduino-ide/resources/app/lib/backend/resources/arduino-cli.exe"
PANEL_FQBN='esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,UploadMode=default,UploadSpeed=921600,LoopCore=1,EventsCore=1'

if [ "${1:-}" = "mac" ]; then
  # esptool resets the board into its ROM loader, reads the MAC and resets it again. On the ESP32-S3
  # the "MAC" line is its Wi-Fi MAC; on the C6 use "BASE MAC". Not yet run on a new board.
  [ $# -ge 2 ] || { echo "usage: bash tools/fw.sh mac COM7"; exit 1; }
  ESPTOOL=$(ls "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/esptool_py/*/esptool.exe 2>/dev/null | head -1)
  [ -n "$ESPTOOL" ] || { echo "esptool isn't in $ARDUINO_DIRECTORIES_DATA (see README, Toolchain)"; exit 1; }
  "$ESPTOOL" --port "$2" read_mac 2>&1 | grep -E "^(Chip is|BASE MAC|MAC)|rror"
  exit "${PIPESTATUS[0]}"
fi

WHAT=panel
case "${1:-}" in
  gateway | listener | radio | updater | ota | ota-both)
    WHAT=$1
    shift
    ;;
esac
[ "$WHAT" != ota ] || [ $# -ge 1 ] || { echo "usage: bash tools/fw.sh ota HOST"; exit 1; }
# The gateway and listener use the panels' wire-format code (core/protocol.cpp and the rest) from
# tcomm/src instead of copies: it goes on the include path, and their src/shared files
# compile it from there.
SRC="$(cygpath -m "$PWD/tcomm/src" 2>/dev/null || echo "$PWD/tcomm/src")"
case $WHAT in
  panel)
    SKETCH=tcomm
    FQBN=$PANEL_FQBN
    OUT="$LOCALAPPDATA/tcomm/b/t32"
    LOG="$LOCALAPPDATA/tcomm/b/compile32.log"
    UP="$LOCALAPPDATA/tcomm/b/upload32.log"
    EXTRA=(--libraries libraries)
    ;;
  radio | ota | ota-both)
    # The same sketch with the two switches in tcomm_config.h set from here, in its own build
    # folder so going back and forth doesn't rebuild everything. ota sends this same build.
    SKETCH=tcomm
    FQBN=$PANEL_FQBN
    OUT="$LOCALAPPDATA/tcomm/b/t32radio"
    LOG="$LOCALAPPDATA/tcomm/b/compile-radio.log"
    UP="$LOCALAPPDATA/tcomm/b/upload-$WHAT.log"
    EXTRA=(--libraries libraries --build-property "compiler.cpp.extra_flags=-DTCOMM_RADIO_ESPNOW=1 -DTCOMM_DEMO=0")
    ;;
  gateway)
    # The C6-GEEK: 4 MB flash. CDCOnBoot puts Serial on the chip's USB Serial/JTAG port, which
    # the Pi talks to; DebugLevel none keeps the core's own log lines off it.
    SKETCH=../gateway/gateway-c6
    FQBN='esp32:esp32:esp32c6:CDCOnBoot=cdc,CPUFreq=160,FlashMode=qio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,UploadSpeed=921600,DebugLevel=none,EraseFlash=none'
    OUT="$LOCALAPPDATA/tcomm/b/gateway"
    LOG="$LOCALAPPDATA/tcomm/b/compile-gateway.log"
    UP="$LOCALAPPDATA/tcomm/b/upload-gateway.log"
    EXTRA=(--build-property "compiler.cpp.extra_flags=\"-I$SRC\"")
    ;;
  listener)
    # On the panel, with the panel's own board settings, so the upload leaves its partitions (and
    # the FFat copy of the calendar) as they are. bash tools/fw.sh COM5 puts the panel back.
    SKETCH=../gateway/gateway-c6/listener
    FQBN=$PANEL_FQBN
    OUT="$LOCALAPPDATA/tcomm/b/listener"
    LOG="$LOCALAPPDATA/tcomm/b/compile-listener.log"
    UP="$LOCALAPPDATA/tcomm/b/upload-listener.log"
    EXTRA=(--build-property "compiler.cpp.extra_flags=\"-I$SRC\"")
    ;;
  updater)
    # Built with the panel's board settings (the same partition table), but written by esptool
    # into app1 only, below, never with arduino-cli's upload, which would put it over the panel
    # firmware in app0.
    SKETCH=updater
    FQBN=$PANEL_FQBN
    OUT="$LOCALAPPDATA/tcomm/b/updater"
    LOG="$LOCALAPPDATA/tcomm/b/compile-updater.log"
    UP="$LOCALAPPDATA/tcomm/b/upload-updater.log"
    EXTRA=(--build-property "compiler.cpp.extra_flags=\"-I$SRC\"")
    ;;
esac
mkdir -p "$OUT"
# An HTTP upload to the updater's /update, sent from here, so nothing has to come in through the
# laptop's firewall (espota needs the panel to connect back, and Windows blocked that). One log per
# call (UP, or UP with a label); prints the result line and returns 0 if the panel took it.
ota_send() {  # ota_send HOST LOGFILE
  if curl -sS --fail --max-time 180 -u "tcomm:$ota_pass" -F "firmware=@$OUT/tcomm.ino.bin" "http://$1/update" > "$2" 2>&1 && grep -q "Update Success" "$2"; then
    echo "sent to $1; the panel restarts into the new firmware (log: $2)"
    return 0
  fi
  echo "UPLOAD FAILED (log: $2)"
  head -5 "$2"
  return 1
}
# The gateway and listener pick secrets.h or, without it, secrets.example.h with __has_include.
# The build cache only knows about the file it used last, so a secrets.h made after a build would
# be missed and the old placeholder keys flashed. Their own files always recompile (a few seconds;
# the Arduino core stays cached).
if [ "$WHAT" = gateway ] || [ "$WHAT" = listener ] || [ "$WHAT" = updater ]; then rm -rf "$OUT/sketch"; fi

# The campsite screensaver's core (screensaver/core, plain C++17) builds into the panel firmware as
# the ss_core library: refreshed into libraries/ss_core/src (not in git) before each panel build, so
# there is one copy of it in git. On GitHub the screensaver sits beside firmware/.
if [ "$WHAT" = panel ] || [ "$WHAT" = radio ] || [ "$WHAT" = ota ] || [ "$WHAT" = ota-both ]; then
  SS_CORE=screensaver/core
  [ -d "$SS_CORE" ] || SS_CORE=../screensaver/core
  [ -d "$SS_CORE" ] || { echo "screensaver/core not found: the panel firmware builds the screensaver in"; exit 1; }
  SYNC=tools/sync_ss_core.py
  [ -f "$SYNC" ] || SYNC=../tools/sync_ss_core.py
  python "$SYNC" "$SS_CORE" libraries/ss_core/src || exit 1   # -O2 on the screensaver only
fi

if [ "$WHAT" = ota-both ]; then
  # Find both waiting updaters before compiling, so a missing one stops the run at once and the
  # ten-minute wait isn't spent on the compile first (review, 2026-11-07).
  ota_pass=$(sed -n 's/^#define TCOMM_OTA_PASS *"\(.*\)".*/\1/p' tcomm/src/config/secrets.h 2>/dev/null)
  if [ -z "$ota_pass" ]; then
    echo "NOT UPLOADED: secrets.h needs TCOMM_OTA_PASS filled in (docs/wifi-updates.md)."
    exit 1
  fi
  found=$(python -I tools/find_panels.py) || { echo "NOT UPLOADED: tools/find_panels.py failed"; exit 1; }
  ip_down=$(echo "$found" | sed -n 's/^DOWNSTAIRS \([0-9.]*\)$/\1/p')
  ip_up=$(echo "$found" | sed -n 's/^UPSTAIRS \([0-9.]*\)$/\1/p')
  if [ -z "$ip_down" ] || [ -z "$ip_up" ]; then
    echo "NOT UPLOADED: both panels' updaters must be waiting first (Settings > Update over Wi-Fi > BOTH PANELS)."
    echo "$found"
    exit 1
  fi
fi

if ! grep -q "high_perf" "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/esp32-arduino-libs/*/versions.txt 2>/dev/null; then
  echo "The high_perf libraries aren't installed in $ARDUINO_DIRECTORIES_DATA (see README, Toolchain)."
  exit 1
fi
if ! "$ACLI" compile --fqbn "$FQBN" "${EXTRA[@]}" --warnings default --build-path "$OUT" "$SKETCH" > "$LOG" 2>&1; then
  echo "COMPILE FAILED, nothing uploaded (log: $LOG)"
  grep -n "error" "$LOG" | head -30
  exit 1
fi
grep "Sketch uses\|Global variables" "$LOG"
grep -n "warning" "$LOG" | grep -v "libraries[/\\]lvgl" | head -20
if [ "$WHAT" = ota-both ]; then
  # Mike, 2026-11-07: both panels from one command. Compiled once above; the addresses came from
  # tools/find_panels.py before the compile (it prints no secrets).
  rc=0
  echo "downstairs:"
  ota_send "$ip_down" "$UP.downstairs" || rc=1
  echo "upstairs:"
  ota_send "$ip_up" "$UP.upstairs" || rc=1
  exit $rc
fi
if [ $# -ge 1 ]; then
  if [ "$WHAT" != panel ] && [ ! -f tcomm/src/config/secrets.h ]; then
    echo "NOT UPLOADED: tcomm/src/config/secrets.h is missing (copy secrets.example.h and"
    echo "fill it in). Without it the $WHAT has placeholder keys and can't talk to anything."
    exit 1
  fi
  if [ "$WHAT" != panel ] && grep -q '"0123456789abcdef"' tcomm/src/config/secrets.h; then
    echo "NOT UPLOADED: secrets.h still has secrets.example.h's placeholder key. Put in real keys"
    echo "(../gateway/docs/gateway-c6.md, The secrets file)."
    exit 1
  fi
  # The updater's and ota's settings from secrets.h, checked for being there; never printed.
  ota_pass=$(sed -n 's/^#define TCOMM_OTA_PASS *"\(.*\)".*/\1/p' tcomm/src/config/secrets.h)
  wifi_name=$(sed -n 's/^#define TCOMM_WIFI_SSID *"\(.*\)".*/\1/p' tcomm/src/config/secrets.h)
  if [ "$WHAT" = updater ] || [ "$WHAT" = ota ]; then
    if [ -z "$ota_pass" ] || { [ "$WHAT" = updater ] && [ -z "$wifi_name" ]; }; then
      echo "NOT UPLOADED: secrets.h needs TCOMM_WIFI_SSID, TCOMM_WIFI_PASS and TCOMM_OTA_PASS filled in"
      echo "(docs/wifi-updates.md)."
      exit 1
    fi
  fi
  if [ "$WHAT" = updater ]; then
    # app1 starts at 0x310000 in the app3M_fat9M_16MB table; nothing else is written.
    ESPTOOL=$(ls "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/esptool_py/*/esptool.exe 2>/dev/null | head -1)
    "$ESPTOOL" --chip esp32s3 --port "$1" --baud 921600 write_flash 0x310000 "$OUT/updater.ino.bin" > "$UP" 2>&1
    if grep -q "Hash of data verified" "$UP"; then
      echo "updater written into app1 (0x310000); the panel firmware in app0 is untouched (log: $UP)"
      exit 0
    fi
    echo "UPLOAD FAILED (log: $UP)"
    grep -iE "error|fail" "$UP" | head
    exit 1
  fi
  if [ "$WHAT" = ota ]; then
    ota_send "$1" "$UP"
    exit $?
  fi
  "$ACLI" upload --fqbn "$FQBN" --input-dir "$OUT" -p "$1" > "$UP" 2>&1
  # The board's Wi-Fi MAC. The C6 also has an 8-byte 802.15.4 address, which esptool prints as
  # "MAC"; its Wi-Fi MAC is the "BASE MAC" line.
  grep -m1 "^BASE MAC" "$UP" || grep -m1 "^MAC" "$UP"
  n=$(grep -c "Hash of data verified" "$UP")
  echo "$n of 4 parts written and verified (bootloader, partition table, boot_app0, app)"
  # esptool in core 3.2.0 resets the board through its watchdog; the USB port drops out while it
  # restarts and esptool then reports a serial exception. That is expected after "Leaving...".
  if [ "$n" -eq 4 ] && grep -q "Leaving" "$UP"; then
    echo "upload OK (log: $UP)"
  else
    echo "UPLOAD FAILED (log: $UP)"
    grep -iE "error|fail" "$UP" | head
    exit 1
  fi
fi
