#!/usr/bin/env python3
"""Cut one review clip per beat (and variant) of the screensaver loop.

usage: python3 -I review_clips.py --preview PATH --out DIR [--seeds 1,2,3 | 1-12]
           [--mood 4] [--every 2] [--width 640] [--crf 28] [--max-seconds 70]
           [--loops-overview] [--keep] [--force]

For each seed a cheap coarse run of ss_preview (--log-beats, no useful frames) gives
the beat timeline of loop 0. Each wanted beat is then rendered with --skip so that
only its own frames are written (frame f shows tick SKIP + (f+1)*N, same rule as
README.md), encoded to DIR/clips/<beat>[_<letter>].mp4 with a PNG poster from its
middle frame, and the frames are deleted. A name already collected (earlier seed,
or an earlier run whose manifest is in DIR) is skipped, so running seeds 1-12
gathers every variant once. DIR/manifest.json lists everything.

Exit codes: 0 ok, 1 a preview/ffmpeg run failed, 2 usage or ffmpeg missing.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys

TICKS_PER_SECOND = 25
PROBE_EVERY = 50           # ticks per frame of the log-only probe run
PROBE_TICKS = 20000        # > longest loop (16,500 ticks) so loop 1 always starts in it
LETTERS = "abc"
SKIP_BEATS = {"pause"}

BEAT_RE = re.compile(r"^tick=(\d+) loop=(\d+) seed=(-?\d+) beat=(\S+)\s*$")
START_RE = re.compile(r"^loop=(\d+) seed=(-?\d+) start tick=(\d+)(?: plan=(.*))?$")


def die(msg, code=2):
    print("review_clips: " + msg, file=sys.stderr)
    sys.exit(code)


def parse_seeds(text):
    seeds = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.fullmatch(r"(-?\d+)-(\d+)", part)
        if m and int(m.group(1)) <= int(m.group(2)):
            seeds.extend(range(int(m.group(1)), int(m.group(2)) + 1))
        else:
            try:
                seeds.append(int(part))
            except ValueError:
                die("bad --seeds element %r" % part)
    if not seeds:
        die("--seeds is empty")
    return seeds


def parse_plan(rest):
    """'arrival=0 setup=2 ... fillers=a,b' -> ({'arrival': 0, ...}, ['a', 'b'])"""
    plan, fillers = {}, []
    if not rest:
        return plan, fillers
    plan_part, _, filler_part = rest.partition("fillers=")
    for tok in plan_part.split():
        k, eq, v = tok.partition("=")
        if eq and re.fullmatch(r"-?\d+", v):
            plan[k] = int(v)
    fillers = [f for f in filler_part.strip().split(",") if f]
    return plan, fillers


def parse_log(text):
    """Return (loop0 info dict, loop0 beats [(tick, name)], loop0 end tick or None)."""
    info = None
    beats = []
    end_tick = None
    for line in text.splitlines():
        m = START_RE.match(line)
        if m:
            loop, seed, tick = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if loop == 0 and info is None:
                plan, fillers = parse_plan(m.group(4))
                info = {"loop_seed": seed, "start_tick": tick, "plan": plan,
                        "fillers": fillers, "line": line.strip()}
            elif loop != 0 and end_tick is None:
                end_tick = tick
            continue
        m = BEAT_RE.match(line)
        if m:
            tick, loop, name = int(m.group(1)), int(m.group(2)), m.group(4)
            if loop == 0:
                beats.append((tick, name))
            elif end_tick is None:
                end_tick = tick
    return info, beats, end_tick


def run_preview(args, seed, every, frames, skip, outdir, log_beats):
    cmd = [args.preview, "--seed", str(seed), "--mood", str(args.mood),
           "--every", str(every), "--frames", str(frames), "--out", outdir]
    if skip:
        cmd += ["--skip", str(skip)]
    if log_beats:
        cmd.append("--log-beats")
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        raise RuntimeError("preview failed (%d): %s\n%s" % (
            r.returncode, " ".join(cmd), (r.stderr or r.stdout)[-600:]))
    return r.stdout


def ffmpeg(argv):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"] + argv,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        raise RuntimeError("ffmpeg failed: %s" % r.stderr.strip()[-600:])


def fmt_rate(r):
    return ("%.6f" % r).rstrip("0").rstrip(".")


def encode(framedir, nframes, rate, width, crf, mp4, png, poster_index):
    ffmpeg(["-framerate", fmt_rate(rate), "-start_number", "0",
            "-i", os.path.join(framedir, "frame_%05d.ppm"), "-frames:v", str(nframes),
            "-vf", "scale=%d:-2" % width, "-c:v", "libx264", "-pix_fmt", "yuv420p",
            "-crf", str(crf), "-movflags", "+faststart", mp4])
    ffmpeg(["-i", os.path.join(framedir, "frame_%05d.ppm" % poster_index),
            "-frames:v", "1", "-vf", "scale=%d:-2" % width, png])


def frame_of_tick(tick, every, skip):
    return max(0, math.ceil((tick - skip) / every) - 1)


def cut_clip(args, seed, name, start_tick, end_tick, work, clips_dir):
    """Render and encode one clip. Returns (seconds, mp4 path, png path)."""
    N = args.every
    rate = TICKS_PER_SECOND / N
    f0 = frame_of_tick(start_tick, N, 0)
    f1 = frame_of_tick(end_tick, N, 0)
    nframes = max(1, f1 - f0)
    nframes = min(nframes, int(args.max_seconds * rate))
    framedir = os.path.join(work, "seed%d_%s" % (seed, name))
    shutil.rmtree(framedir, ignore_errors=True)
    # --skip f0*N makes the frame 0 of this run show tick (f0+1)*N, i.e. frame f0 of a full run.
    run_preview(args, seed, N, nframes, f0 * N, framedir, False)
    mp4 = os.path.join(clips_dir, name + ".mp4")
    png = os.path.join(clips_dir, name + ".png")
    try:
        encode(framedir, nframes, rate, args.width, args.crf, mp4, png, nframes // 2)
    finally:
        if not args.keep:
            shutil.rmtree(framedir, ignore_errors=True)
    return nframes / rate, mp4, png


def variant_letter(plan, beat):
    if beat in plan and 0 <= plan[beat] < len(LETTERS):
        return LETTERS[plan[beat]]
    return ""


def main():
    ap = argparse.ArgumentParser(description="Cut per-beat review clips from ss_preview.")
    ap.add_argument("--preview", required=True, help="path to the ss_preview executable")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--seeds", default="11", help="e.g. 1,2,3 or 1-12 (default 11)")
    ap.add_argument("--mood", type=float, default=4)
    ap.add_argument("--every", type=int, default=2, help="ticks per frame (default 2)")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--crf", type=int, default=28)
    ap.add_argument("--max-seconds", type=float, default=70, help="clip length cap")
    ap.add_argument("--loops-overview", action="store_true",
                    help="also render a whole-loop 8x clip of the first seed")
    ap.add_argument("--keep", action="store_true", help="keep the frame work dir")
    ap.add_argument("--force", action="store_true",
                    help="re-cut names already in the manifest")
    args = ap.parse_args()

    if shutil.which("ffmpeg") is None:
        die("ffmpeg not found on PATH")
    if args.every < 1:
        die("--every must be >= 1")
    if not (os.path.isfile(args.preview) and os.access(args.preview, os.X_OK)):
        die("preview executable not found: %s" % args.preview)
    args.preview = os.path.abspath(args.preview)
    seeds = parse_seeds(args.seeds)

    out = os.path.abspath(args.out)
    clips_dir = os.path.join(out, "clips")
    work = os.path.join(out, "work")
    os.makedirs(clips_dir, exist_ok=True)
    os.makedirs(work, exist_ok=True)

    # Carry over what earlier runs into the same DIR collected.
    manifest_path = os.path.join(out, "manifest.json")
    clips, plans = [], []
    if os.path.isfile(manifest_path) and not args.force:
        try:
            with open(manifest_path) as f:
                old = json.load(f)
            clips = [c for c in old.get("clips", [])
                     if os.path.isfile(os.path.join(out, c.get("file", "")))]
            plans = old.get("plans", [])
        except (OSError, ValueError):
            pass
    have = {c["name"] for c in clips}
    all_plan_beats = set()
    for p in plans:
        all_plan_beats.update(p.get("plan", {}))
    failed = False
    new_rows = []
    overview_done = False

    for seed in seeds:
        try:
            log = run_preview(args, seed, PROBE_EVERY, PROBE_TICKS // PROBE_EVERY,
                              0, os.path.join(work, "probe_seed%d" % seed), True)
        except RuntimeError as e:
            print("seed %d: %s" % (seed, e), file=sys.stderr)
            failed = True
            continue
        finally:
            shutil.rmtree(os.path.join(work, "probe_seed%d" % seed), ignore_errors=True)
        info, beats, end_tick = parse_log(log)
        if info is None or not beats:
            print("seed %d: no loop 0 beat log in preview output" % seed, file=sys.stderr)
            failed = True
            continue
        if end_tick is None:
            end_tick = PROBE_TICKS
        plans = [p for p in plans if p.get("seed") != seed]
        plans.append(dict(info, seed=seed, mood=args.mood))
        all_plan_beats.update(info["plan"])

        seen = set()
        for i, (tick, beat) in enumerate(beats):
            nxt = beats[i + 1][0] if i + 1 < len(beats) else end_tick
            if beat in SKIP_BEATS or beat in seen or nxt <= tick:
                continue
            seen.add(beat)
            letter = variant_letter(info["plan"], beat)
            name = beat + ("_" + letter if letter else "")
            if name in have:
                continue
            try:
                secs, mp4, png = cut_clip(args, seed, name, tick, nxt, work, clips_dir)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, name, e), file=sys.stderr)
                failed = True
                continue
            have.add(name)
            row = {"name": name, "beat": beat, "variant": letter or None, "seed": seed,
                   "start_tick": tick, "seconds": round(secs, 2),
                   "file": "clips/" + name + ".mp4", "poster": "clips/" + name + ".png",
                   "bytes": os.path.getsize(mp4)}
            clips.append(row)
            new_rows.append(row)
            print("  cut %-20s seed %-6d %6.1f s %8d B" % (name, seed, secs, row["bytes"]),
                  flush=True)

        if args.loops_overview and not overview_done:
            overview_done = True
            name = "loop_overview_seed%d" % seed
            nframes = max(1, end_tick // 8)
            framedir = os.path.join(work, name)
            try:
                run_preview(args, seed, 8, nframes, 0, framedir, False)
                mp4 = os.path.join(clips_dir, name + ".mp4")
                png = os.path.join(clips_dir, name + ".png")
                try:
                    encode(framedir, nframes, TICKS_PER_SECOND, args.width, args.crf,
                           mp4, png, nframes // 2)
                finally:
                    if not args.keep:
                        shutil.rmtree(framedir, ignore_errors=True)
                clips = [c for c in clips if c["name"] != name]
                row = {"name": name, "beat": "loop_overview", "variant": None, "seed": seed,
                       "start_tick": 0, "seconds": round(nframes / TICKS_PER_SECOND, 2),
                       "file": "clips/" + name + ".mp4", "poster": "clips/" + name + ".png",
                       "bytes": os.path.getsize(mp4)}
                clips.append(row)
                new_rows.append(row)
                print("  cut %-20s seed %-6d %6.1f s %8d B" % (name, seed, row["seconds"],
                                                              row["bytes"]), flush=True)
            except RuntimeError as e:
                print("seed %d overview: %s" % (seed, e), file=sys.stderr)
                failed = True

    collected = {}
    for c in clips:
        if c["variant"]:
            collected.setdefault(c["beat"], set()).add(c["variant"])
    missing = {}
    for beat in sorted(all_plan_beats):
        left = [l for l in LETTERS if l not in collected.get(beat, set())]
        if left:
            missing[beat] = left

    manifest = {"clips": clips, "plans": plans, "missing_variants": missing}
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)

    print()
    print("%-24s %-10s %8s %10s" % ("clip", "seed", "seconds", "size"))
    for c in clips:
        print("%-24s %-10d %8.1f %10d" % (c["name"], c["seed"], c["seconds"], c["bytes"]))
    print("%d clips (%d new), manifest %s" % (len(clips), len(new_rows), manifest_path))
    if missing:
        print("missing variants: " + ", ".join("%s:%s" % (b, "".join(l)) for b, l in missing.items()))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
