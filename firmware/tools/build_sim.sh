#!/usr/bin/env bash
# Builds the host tests and the screenshot simulator with the WinLibs GCC, runs both, and writes
# PNGs of every scenario to docs/screens and the sounds to docs/sounds (ringtones in their own folder).
# Run from the firmware folder in Git Bash:  bash tools/build_sim.sh
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
GB="$LOCALAPPDATA/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$GB:$PATH"
H="$LOCALAPPDATA/tcomm/host"
mkdir -p "$H/lvobj" "$H/fontobj" docs/screens docs/sounds/ringtones sim/out

SRC=tcomm/src
INC=(-I "$SRC" -I libraries -I libraries/lvgl)

# LVGL once (the demos and examples aren't needed)
if [ ! -f "$H/liblvgl.a" ] || [ libraries/lv_conf.h -nt "$H/liblvgl.a" ]; then  # rebuilt when lv_conf.h changes
  rm -f "$H"/lvobj/*.o
  (cd libraries/lvgl/src && find . -name "*.c" -not -path "./demos/*" -not -path "./examples/*") | while read -r f; do
    o="$H/lvobj/$(echo "${f#./}" | sed 's|/|_|g; s|\.c$|.o|')"
    gcc -O1 -w -I libraries -I libraries/lvgl -c "libraries/lvgl/src/$f" -o "$o"
  done
  ar rcs "$H/liblvgl.a" "$H"/lvobj/*.o
fi

# Host tests (core and controller)
g++ -std=c++17 -O1 -g -Wall -Wextra -I "$SRC" tests/test_core.cpp "$SRC"/core/*.cpp "$SRC"/app/controller.cpp \
  "$SRC"/app/demo.cpp -o "$H/tests.exe"
"$H/tests.exe" docs/sounds

# The ESP32-C6 gateway's logic, with the panels' own wire-format code
g++ -std=c++17 -O1 -g -Wall -Wextra -I "$SRC" -I ../gateway/gateway-c6/src ../gateway/tests/test_gateway.cpp \
  ../gateway/gateway-c6/src/gw/*.cpp "$SRC"/core/protocol.cpp "$SRC"/core/events.cpp "$SRC"/core/civil.cpp \
  -o "$H/test_gateway.exe"
"$H/test_gateway.exe"

# Fonts are C
for f in "$SRC"/ui/fonts/*.c; do
  gcc -O1 -w "${INC[@]}" -c "$f" -o "$H/fontobj/$(basename "${f%.c}").o"
done

g++ -std=c++17 -O1 -g -Wall -Wextra "${INC[@]}" sim/sim_main.cpp "$SRC"/core/*.cpp "$SRC"/app/controller.cpp \
  "$SRC"/app/demo.cpp "$SRC"/gfx/*.cpp "$SRC"/ui/*.cpp "$H"/fontobj/*.o "$H/liblvgl.a" -o "$H/sim.exe"
mkdir -p sim/out/sheets  # sheets of strips: not 800 x 480
"$H/sim.exe" sim/out
python -P tools/raw2png.py sim/out/sheets/strips.rgb docs/screens/strips_holidays.png 800 $((70 * 17))
for f in sim/out/*.rgb; do
  python -P tools/raw2png.py "$f" "docs/screens/$(basename "${f%.rgb}").png"
done

# The forecast icons on their own, in each light
g++ -std=c++17 -O1 -Wall -Wextra -I "$SRC" sim/icon_sheet.cpp "$SRC"/gfx/*.cpp "$SRC"/ui/art.cpp \
  "$SRC"/ui/board_art.cpp "$SRC"/ui/theme.cpp -o "$H/icon_sheet.exe"
mkdir -p sim/out/icons  # not sim/out itself: the screens loop above takes every .rgb there as 800 x 480
size=$("$H/icon_sheet.exe" sim/out/icons/forecast_icons.rgb)
python -P tools/raw2png.py sim/out/icons/forecast_icons.rgb docs/screens/forecast_icons.png $size
echo "screens in docs/screens"
