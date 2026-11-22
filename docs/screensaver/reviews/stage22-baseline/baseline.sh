#!/bin/bash
# usage: baseline.sh BUILD_DIR PROBE_EXE OUT_DIR
# Writes loops.txt, minors.txt, beats.txt and probe.txt into OUT_DIR.
set -e
B="$1"; P="$2"; O="$3"
mkdir -p "$O"
PV="$B/ss_preview.exe"
TMP=$(mktemp -d)
{
  for s in 1 7; do
    echo "# evening --seed $s --mood 4 --loops 10"; "$PV" --seed $s --mood 4 --loops 10
    echo "# night --seed $s --mood 0 --night --loops 10"; "$PV" --seed $s --mood 0 --night --loops 10
    echo "# morning --seed $s --spine morning --mood 1.5 --loops 10"; "$PV" --seed $s --spine morning --mood 1.5 --loops 10
    echo "# midday --seed $s --spine midday --mood 3 --loops 10"; "$PV" --seed $s --spine midday --mood 3 --loops 10
  done
} > "$O/loops.txt"
: > "$O/beats.txt"
for s in $(seq 1 12); do
  case $((s % 3)) in 0) M="--mood 3";; 1) M="--mood 0 --night";; 2) M="--mood 5";; esac
  echo "# seed $s $M --skip 20000 --frames 0 --log-beats" >> "$O/beats.txt"
  "$PV" --seed $s $M --skip 20000 --frames 0 --out "$TMP" --log-beats >> "$O/beats.txt"
done
grep -E '^# seed|minor=' "$O/beats.txt" > "$O/minors.txt"
rm -rf "$TMP"
"$P" > "$O/probe.txt"
