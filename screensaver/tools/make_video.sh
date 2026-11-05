#!/usr/bin/env bash
# Stitch DIR/frame_%05d.ppm into an mp4 (or a gif when OUT ends in .gif).
# usage: make_video.sh DIR OUT.mp4 [fps]      (fps default 25)
set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
  echo "usage: $0 DIR OUT.mp4|OUT.gif [fps]" >&2
  exit 2
fi
dir=$1
out=$2
fps=${3:-25}

if [ ! -f "$dir/frame_00000.ppm" ]; then
  echo "$0: no $dir/frame_00000.ppm" >&2
  exit 1
fi

case "$out" in
  *.gif)
    pal=$(mktemp "${TMPDIR:-/tmp}/palette.XXXXXX.png")
    trap 'rm -f "$pal"' EXIT
    filters="fps=$fps,scale=800:-1:flags=lanczos"
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" -i "$dir/frame_%05d.ppm" \
      -vf "$filters,palettegen" "$pal"
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" -i "$dir/frame_%05d.ppm" -i "$pal" \
      -lavfi "$filters [x]; [x][1:v] paletteuse" "$out"
    ;;
  *)
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" -i "$dir/frame_%05d.ppm" \
      -c:v libx264 -pix_fmt yuv420p -crf 20 "$out"
    ;;
esac
echo "wrote $out"
