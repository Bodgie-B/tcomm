#!/usr/bin/env python3
"""Cut one review clip per beat (and variant) of the screensaver loop.

usage: python3 -I review_clips.py --preview PATH --out DIR [--seeds 1,2,3 | 1-12]
           [--mood 4] [--night] [--every 2] [--width 640] [--crf 28]
           [--max-seconds 70] [--minor-seconds 22] [--gif] [--loops-overview]
           [--keep] [--force]
       python3 -I review_clips.py --out DIR --list

For each seed a cheap coarse run of ss_preview (--log-beats, no useful frames) gives
the beat timeline of loop 0. Each wanted beat is then rendered with --skip so that
only its own frames are written (frame f shows tick SKIP + (f+1)*N, same rule as
README.md), encoded to DIR/clips/<beat>[_<letter>].mp4 with a PNG poster from its
middle frame, and the frames are deleted. A name already collected (earlier seed,
or an earlier run whose manifest is in DIR) is skipped, so running seeds 1-12
gathers every variant once. DIR/manifest.json lists everything.

Minor effects (the "tick=T minor=NAME" lines of the beat log) are cut as
clips/minor_<NAME>.mp4 from their start tick for --minor-seconds, first seed wins.
Every clip row records kind (beat, filler, major, minor, overview), mood and night.
--night runs the preview with --night (and --mood 0 unless --mood is given). Run the
script twice into the same DIR, once by day and once with --night, to collect the
night-only majors and minors: names already in the manifest are not re-cut.
--gif also writes a GIF (two-pass palette, as tools/clip.sh) for clips under 10 s.
--list prints the manifest table without rendering.

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
LETTERS = "abcd"   # dinner has a fourth variant (fish dinner, after fishing_good)
SKIP_BEATS = {"pause"}

BEAT_RE = re.compile(r"^tick=(\d+) loop=(\d+) seed=(-?\d+) beat=(\S+)\s*$")
MINOR_RE = re.compile(r"^tick=(\d+) minor=(\S+)\s*$")
GIF_MAX_SECONDS = 10
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


def parse_memory(text):
    """'loop=1 cairn=0 stolen=none brave=a,b' -> {'loop': 1, 'cairn': 0, 'stolen': 'none',
    'brave': ['a', 'b']}"""
    mem = {}
    for tok in text.split():
        k, eq, v = tok.partition("=")
        if not eq:
            continue
        if re.fullmatch(r"-?\d+", v):
            mem[k] = int(v)
        elif "," in v:
            mem[k] = [x for x in v.split(",") if x]
        else:
            mem[k] = v
    return mem


def parse_plan(rest):
    """'arrival=0 ... fillers=a,b majors=c,d memory=k=v k=v' ->
    (plan dict, fillers list, majors list, memory dict). 'none' lists are empty."""
    plan, fillers, majors, memory = {}, [], [], {}
    if not rest:
        return plan, fillers, majors, memory
    rest, _, mem_part = rest.partition("memory=")
    memory = parse_memory(mem_part)
    plan_part, _, tail = rest.partition("fillers=")
    for tok in plan_part.split():
        k, eq, v = tok.partition("=")
        if eq and re.fullmatch(r"-?\d+", v):
            plan[k] = int(v)
    toks = tail.split()
    if toks:
        fillers = [f for f in toks[0].split(",") if f and f != "none"]
    for tok in toks[1:]:
        if tok.startswith("majors="):
            majors = [m for m in tok[7:].split(",") if m and m != "none"]
    return plan, fillers, majors, memory


def parse_log(text):
    """Return (loop0 info dict, loop0 beats [(tick, name)], loop0 minors [(tick, name)],
    loop0 end tick or None)."""
    info = None
    beats = []
    minors = []
    end_tick = None
    for line in text.splitlines():
        m = START_RE.match(line)
        if m:
            loop, seed, tick = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if loop == 0 and info is None:
                plan, fillers, majors, memory = parse_plan(m.group(4))
                info = {"loop_seed": seed, "start_tick": tick, "plan": plan,
                        "fillers": fillers, "majors": majors, "memory": memory,
                        "line": line.strip()}
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
            continue
        m = MINOR_RE.match(line)
        if m and end_tick is None:
            minors.append((int(m.group(1)), m.group(2)))
    return info, beats, minors, end_tick


def run_preview(args, seed, every, frames, skip, outdir, log_beats):
    cmd = [args.preview, "--seed", str(seed), "--mood", "%g" % args.mood_eff]
    if args.night:
        cmd.append("--night")
    cmd += ["--every", str(every), "--frames", str(frames), "--out", outdir]
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


def encode_gif(framedir, nframes, rate, width, gif):
    """Two-pass palette GIF, same filters as tools/clip.sh."""
    pal = os.path.join(framedir, "palette.png")
    src = ["-framerate", fmt_rate(rate), "-start_number", "0",
           "-i", os.path.join(framedir, "frame_%05d.ppm")]
    filters = "fps=%s,scale=%d:-1:flags=lanczos" % (fmt_rate(rate), width)
    ffmpeg(src + ["-frames:v", str(nframes), "-vf", filters + ",palettegen=stats_mode=full",
                  pal])
    ffmpeg(src + ["-i", pal, "-frames:v", str(nframes), "-lavfi",
                  filters + " [x]; [x][1:v] paletteuse=dither=sierra2_4a", gif])


def frame_of_tick(tick, every, skip):
    return max(0, math.ceil((tick - skip) / every) - 1)


def cut_clip(args, seed, name, start_tick, end_tick, work, clips_dir, max_seconds):
    """Render and encode one clip. Returns (seconds, mp4 path, png path, gif path or None)."""
    N = args.every
    rate = TICKS_PER_SECOND / N
    f0 = frame_of_tick(start_tick, N, 0)
    f1 = frame_of_tick(end_tick, N, 0)
    nframes = max(1, f1 - f0)
    nframes = min(nframes, max(1, int(max_seconds * rate)))
    framedir = os.path.join(work, "seed%d_%s" % (seed, name))
    shutil.rmtree(framedir, ignore_errors=True)
    # --skip f0*N makes the frame 0 of this run show tick (f0+1)*N, i.e. frame f0 of a full run.
    run_preview(args, seed, N, nframes, f0 * N, framedir, False)
    mp4 = os.path.join(clips_dir, name + ".mp4")
    png = os.path.join(clips_dir, name + ".png")
    gif = None
    try:
        encode(framedir, nframes, rate, args.width, args.crf, mp4, png, nframes // 2)
        if args.gif and nframes / rate < GIF_MAX_SECONDS:
            gif = os.path.join(clips_dir, name + ".gif")
            encode_gif(framedir, nframes, rate, args.width, gif)
    finally:
        if not args.keep:
            shutil.rmtree(framedir, ignore_errors=True)
    return nframes / rate, mp4, png, gif


def variant_letter(plan, beat):
    if beat in plan and 0 <= plan[beat] < len(LETTERS):
        return LETTERS[plan[beat]]
    return ""


def load_manifest(path, out, check_files=True):
    """Return (clips, plans) from an earlier run; clips whose file vanished are dropped."""
    clips, plans = [], []
    try:
        with open(path) as f:
            old = json.load(f)
    except (OSError, ValueError):
        return clips, plans
    for c in old.get("clips", []):
        if check_files and not os.path.isfile(os.path.join(out, c.get("file", ""))):
            continue
        c.setdefault("kind", "overview" if c.get("beat") == "loop_overview" else "beat")
        c.setdefault("mood", None)
        c.setdefault("night", False)
        clips.append(c)
    for p in old.get("plans", []):
        p.setdefault("night", False)
        p.setdefault("majors", [])
        p.setdefault("memory", {})
        plans.append(p)
    return clips, plans


def print_table(clips):
    print("%-26s %-8s %-8s %5s %5s %8s %10s" % ("clip", "kind", "seed", "mood", "night",
                                                "seconds", "size"))
    for c in clips:
        mood = c.get("mood")
        print("%-26s %-8s %-8d %5s %5s %8.1f %10d" % (
            c["name"], c.get("kind", "beat"), c["seed"],
            "-" if mood is None else "%g" % mood, "yes" if c.get("night") else "no",
            c["seconds"], c["bytes"]))


def missing_variants(clips, all_plan_beats):
    collected = {}
    for c in clips:
        if c.get("variant"):
            collected.setdefault(c["beat"], set()).add(c["variant"])
    missing = {}
    for beat in sorted(all_plan_beats):
        left = [l for l in LETTERS if l not in collected.get(beat, set())]
        if left:
            missing[beat] = left
    return missing


def beat_kind(info, beat):
    if beat in info["majors"]:
        return "major"
    if beat in info["fillers"]:
        return "filler"
    return "beat"


def main():
    ap = argparse.ArgumentParser(description="Cut per-beat review clips from ss_preview.")
    ap.add_argument("--preview", help="path to the ss_preview executable")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--seeds", default="11", help="e.g. 1,2,3 or 1-12 (default 11)")
    ap.add_argument("--mood", type=float, default=None,
                    help="preview mood (default 4, or 0 with --night)")
    ap.add_argument("--night", action="store_true",
                    help="run the preview with --night (mood 0 unless --mood is given)")
    ap.add_argument("--every", type=int, default=2, help="ticks per frame (default 2)")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--crf", type=int, default=28)
    ap.add_argument("--max-seconds", type=float, default=70, help="beat clip length cap")
    ap.add_argument("--minor-seconds", type=float, default=22,
                    help="length of each minor-effect clip (default 22)")
    ap.add_argument("--gif", action="store_true", help="also write a GIF for clips under 10 s")
    ap.add_argument("--loops-overview", action="store_true",
                    help="also render a whole-loop 8x clip of the first seed")
    ap.add_argument("--keep", action="store_true", help="keep the frame work dir")
    ap.add_argument("--force", action="store_true",
                    help="re-cut names already in the manifest")
    ap.add_argument("--list", action="store_true",
                    help="print the manifest table and exit without rendering")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    manifest_path = os.path.join(out, "manifest.json")

    if args.list:
        if not os.path.isfile(manifest_path):
            die("no manifest at %s" % manifest_path)
        clips, plans = load_manifest(manifest_path, out, check_files=False)
        print_table(clips)
        all_beats = set()
        for p in plans:
            all_beats.update(p.get("plan", {}))
        miss = missing_variants(clips, all_beats)
        print("%d clips, manifest %s" % (len(clips), manifest_path))
        if miss:
            print("missing variants: " + ", ".join("%s:%s" % (b, "".join(l)) for b, l in miss.items()))
        return 0

    if not args.preview:
        die("--preview is required (unless --list)")
    if shutil.which("ffmpeg") is None:
        die("ffmpeg not found on PATH")
    if args.every < 1:
        die("--every must be >= 1")
    if not (os.path.isfile(args.preview) and os.access(args.preview, os.X_OK)):
        die("preview executable not found: %s" % args.preview)
    args.preview = os.path.abspath(args.preview)
    args.mood_eff = args.mood if args.mood is not None else (0.0 if args.night else 4.0)
    seeds = parse_seeds(args.seeds)

    clips_dir = os.path.join(out, "clips")
    work = os.path.join(out, "work")
    os.makedirs(clips_dir, exist_ok=True)
    os.makedirs(work, exist_ok=True)

    # Carry over what earlier runs into the same DIR collected.
    clips, plans = [], []
    if os.path.isfile(manifest_path) and not args.force:
        clips, plans = load_manifest(manifest_path, out)
    # name -> set of kinds collected under that name. A name is skipped only for the same
    # kind, so a beat that is a filler in one seed and a major in another is cut for both.
    have = {}
    for c in clips:
        have.setdefault(c["name"], set()).add(c["kind"])
    all_plan_beats = set()
    for p in plans:
        all_plan_beats.update(p.get("plan", {}))
    failed = False
    new_rows = []
    overview_done = False

    def add_row(name, beat, kind, variant, seed, tick, secs, mp4, gif):
        row = {"name": name, "beat": beat, "kind": kind, "variant": variant, "seed": seed,
               "start_tick": tick, "seconds": round(secs, 2),
               "mood": args.mood_eff, "night": bool(args.night),
               "file": "clips/" + name + ".mp4", "poster": "clips/" + name + ".png",
               "bytes": os.path.getsize(mp4)}
        if gif:
            row["gif"] = "clips/" + name + ".gif"
        have.setdefault(name, set()).add(kind)
        clips.append(row)
        new_rows.append(row)
        print("  cut %-24s %-7s seed %-6d %6.1f s %8d B" % (name, kind, seed, secs,
                                                           row["bytes"]), flush=True)

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
        info, beats, minors, end_tick = parse_log(log)
        if info is None or not beats:
            print("seed %d: no loop 0 beat log in preview output" % seed, file=sys.stderr)
            failed = True
            continue
        if end_tick is None:
            end_tick = PROBE_TICKS
        plans = [p for p in plans
                 if not (p.get("seed") == seed and bool(p.get("night")) == args.night)]
        plans.append(dict(info, seed=seed, mood=args.mood_eff, night=bool(args.night)))
        all_plan_beats.update(info["plan"])

        seen = set()
        for i, (tick, beat) in enumerate(beats):
            nxt = beats[i + 1][0] if i + 1 < len(beats) else end_tick
            if beat in SKIP_BEATS or beat in seen or nxt <= tick:
                continue
            seen.add(beat)
            kind = beat_kind(info, beat)
            letter = variant_letter(info["plan"], beat)
            name = beat + ("_" + letter if letter else "")
            if kind in have.get(name, ()):
                continue
            if name in have:   # same name collected as another kind: keep both files
                name += "_" + kind
                if kind in have.get(name, ()):
                    continue
            try:
                secs, mp4, png, gif = cut_clip(args, seed, name, tick, nxt, work, clips_dir,
                                               args.max_seconds)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, name, e), file=sys.stderr)
                failed = True
                continue
            add_row(name, beat, kind, letter or None, seed, tick, secs, mp4, gif)

        for tick, minor in minors:
            name = "minor_" + minor
            if "minor" in have.get(name, ()):
                continue
            end = tick + int(round(args.minor_seconds * TICKS_PER_SECOND))
            try:
                secs, mp4, png, gif = cut_clip(args, seed, name, tick, end, work, clips_dir,
                                               args.minor_seconds)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, name, e), file=sys.stderr)
                failed = True
                continue
            add_row(name, minor, "minor", None, seed, tick, secs, mp4, gif)

        if args.loops_overview and not overview_done:
            overview_done = True
            name = "loop_overview_seed%d%s" % (seed, "_night" if args.night else "")
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
                add_row(name, "loop_overview", "overview", None, seed, 0,
                        nframes / TICKS_PER_SECOND, mp4, None)
            except RuntimeError as e:
                print("seed %d overview: %s" % (seed, e), file=sys.stderr)
                failed = True

    missing = missing_variants(clips, all_plan_beats)
    manifest = {"clips": clips, "plans": plans, "missing_variants": missing}
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)

    print()
    print_table(clips)
    print("%d clips (%d new), manifest %s" % (len(clips), len(new_rows), manifest_path))
    if missing:
        print("missing variants: " + ", ".join("%s:%s" % (b, "".join(l)) for b, l in missing.items()))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
