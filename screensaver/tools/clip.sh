#!/usr/bin/env bash
# Cut a clip from a frame dump: clip.sh DIR OUT.mp4|OUT.gif START_FRAME N_FRAMES [fps] [width]
# START_FRAME is the frame number in DIR/frame_%05d.ppm (use --log-beats tick
# numbers divided by --every to find it). fps default 25, width default 800.
set -euo pipefail

if [ $# -lt 4 ] || [ $# -gt 6 ]; then
  echo "usage: $0 DIR OUT.mp4|OUT.gif START_FRAME N_FRAMES [fps] [width]" >&2
  exit 2
fi
dir=$1; out=$2; start=$3; n=$4; fps=${5:-25}; width=${6:-800}

first=$(printf "%s/frame_%05d.ppm" "$dir" "$start")
if [ ! -f "$first" ]; then
  echo "$0: no $first" >&2
  exit 1
fi

case "$out" in
  *.gif)
    pal=$(mktemp "${TMPDIR:-/tmp}/palette.XXXXXX.png")
    trap 'rm -f "$pal"' EXIT
    filters="fps=$fps,scale=$width:-1:flags=lanczos"
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" -start_number "$start" -i "$dir/frame_%05d.ppm" \
      -frames:v "$n" -vf "$filters,palettegen" "$pal"
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" -start_number "$start" -i "$dir/frame_%05d.ppm" -i "$pal" \
      -frames:v "$n" -lavfi "$filters [x]; [x][1:v] paletteuse" "$out"
    ;;
  *)
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" -start_number "$start" -i "$dir/frame_%05d.ppm" \
      -frames:v "$n" -vf "scale=$width:-2" -c:v libx264 -pix_fmt yuv420p -crf 23 -movflags +faststart "$out"
    ;;
esac
echo "wrote $out"
