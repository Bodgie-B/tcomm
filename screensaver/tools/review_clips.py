#!/usr/bin/env python3
"""Cut one review clip per beat (and variant) of the screensaver loop.

usage: python3 -I review_clips.py --preview PATH --out DIR [--seeds 1,2,3 | 1-12]
           [--mood 4] [--night] [--every 2] [--width 640] [--crf 28]
           [--max-seconds 70] [--minor-seconds 22] [--gif] [--loops-overview]
           [--keep] [--force]
       python3 -I review_clips.py --out DIR --list
       python3 -I review_clips.py --preview PATH --out DIR --family morning|midday
           [--seeds 1-40] [--mood M] [--pad-seconds 1]
       python3 -I review_clips.py --preview PATH --out DIR --weather-clips
           [--seeds 1-30] [--weather-seed 3]

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

--family morning|midday cuts the spine of that family instead of the evening: the preview
runs with --spine FAMILY --weather clear at mood 1.5 (morning) or 3 (midday), every beat x
variant seen over the seeds is cut as clips/<family>_<beat>_<letter>.mp4 (the beat's window
plus --pad-seconds either side), and clips/<family>_overview.mp4 is the whole first loop at
8x. Fillers, majors and minors are not cut (the evening runs have them). Rows carry
"family". Seeds are tried in order until every beat has all its variants (FAMILY_BEATS).
--weather-clips cuts the weather set (see cut_weather_set): weather_<kind>_day/night,
weather_storm_flash, weather_wind_hat, major_rain_shelter and morning_fog_lift. --seeds is
where major_rain_shelter is searched; the rest use --weather-seed (default 3).
Fixed seeds and a fixed rule keep the output deterministic.

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


# The beats of the morning and midday spines and how many variants each has (the plan line
# gives only the one a loop plays; this is what "all collected" means).
FAMILY_BEATS = {
    "morning": {"wake": 3, "coffee": 3, "bag_down": 2, "break_camp": 3, "douse": 2, "leave": 3},
    "midday": {"return": 3, "lunch": 2, "wade": 3, "nap": 3, "map": 2, "afternoon": 2},
}
FAMILY_MOOD = {"morning": 1.5, "midday": 3.0}
WEATHER_KINDS = ["wind", "rain", "snow", "fog", "heat", "chill", "storm"]
WEATHER_SETTLED_TICK = 4000    # snow has grown on the tent (140 s), the gags have had pauses
WEATHER_CLIP_SECONDS = 25
FLASH_SCAN = (500, 1800)       # the first strike lands 20 to 40 s after the build
FAMILY_MAX_SECONDS = 90        # the longest beat is 74 s (coffee c); the cap is for runaways
FLASH_STEP = 3                 # ticks per probe frame (a flash lasts 7.5 ticks)
FOG_LIFT_SECONDS = 150

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


def plan_family(rest):
    """The spine family a plan line names ('family=morning wake=1 ...'); lines
    from before the families went in have none and are the evening."""
    m = re.search(r"(?:^|\s)family=(\w+)", rest or "")
    return m.group(1) if m else "evening"


def parse_plan(rest):
    """'family=evening arrival=0 ... fillers=a,b majors=c,d memory=k=v k=v' ->
    (plan dict, fillers list, majors list, memory dict). 'none' lists are empty.
    The family token is not numeric, so it stays out of the plan dict (see
    plan_family); the morning's beats (wake=.. leave=..) go in it (its fog=.. after
    the majors does not)."""
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
                        "family": plan_family(m.group(4)),
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


def run_preview(args, seed, every, frames, skip, outdir, log_beats, extra=(), mood=None,
                night=None):
    """mood / night override the run's own (args.mood_eff / args.night); extra is a list of
    more preview flags (--spine, --weather)."""
    mood = args.mood_eff if mood is None else mood
    night = args.night if night is None else night
    cmd = [args.preview, "--seed", str(seed), "--mood", "%g" % mood]
    if night:
        cmd.append("--night")
    cmd += list(extra)
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


def cut_clip(args, seed, name, start_tick, end_tick, work, clips_dir, max_seconds,
             extra=(), mood=None, night=None, every=None, play_rate=None):
    """Render and encode one clip. Returns (seconds, mp4 path, png path, gif path or None).
    every / play_rate: ticks per frame and playback frame rate (default: args.every and
    real time; every 8 with play_rate 25 is the 8x overview look)."""
    N = every or args.every
    rate = play_rate or TICKS_PER_SECOND / N
    f0 = frame_of_tick(start_tick, N, 0)
    f1 = frame_of_tick(end_tick, N, 0)
    nframes = max(1, f1 - f0)
    nframes = min(nframes, max(1, int(max_seconds * rate)))
    framedir = os.path.join(work, "seed%d_%s" % (seed, name))
    shutil.rmtree(framedir, ignore_errors=True)
    # --skip f0*N makes the frame 0 of this run show tick (f0+1)*N, i.e. frame f0 of a full run.
    run_preview(args, seed, N, nframes, f0 * N, framedir, False, extra, mood, night)
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
    """Evening beats (those named by the evening plans) lack any of LETTERS not collected;
    the morning and midday beats (once the manifest has a clip of that family) lack any of
    the first FAMILY_BEATS[family][beat] letters, reported as <family>_<beat>."""
    collected = {}
    for c in clips:
        if c.get("variant"):
            key = (c.get("family") or "evening", c["beat"])
            collected.setdefault(key, set()).add(c["variant"])
    missing = {}
    for beat in sorted(all_plan_beats):
        left = [l for l in LETTERS if l not in collected.get(("evening", beat), set())]
        if left:
            missing[beat] = left
    seen = {c.get("family") for c in clips}
    for fam, beats in FAMILY_BEATS.items():
        if fam not in seen:
            continue
        for beat, n in beats.items():
            left = [l for l in LETTERS[:n] if l not in collected.get((fam, beat), set())]
            if left:
                missing["%s_%s" % (fam, beat)] = left
    return missing


def beat_kind(info, beat):
    if beat in info["majors"]:
        return "major"
    if beat in info["fillers"]:
        return "filler"
    return "beat"


class State:
    """What a run has collected so far: the manifest rows (clips, plans), which names exist
    (have: name -> set of kinds), the rows this run added, and whether anything failed."""

    def __init__(self, args, clips, plans):
        self.args = args
        self.clips = clips
        self.plans = plans
        self.have = {}
        for c in clips:
            self.have.setdefault(c["name"], set()).add(c["kind"])
        self.new_rows = []
        self.failed = False

    def add_row(self, name, beat, kind, variant, seed, tick, secs, mp4, gif, mood=None,
                night=None, **more):
        row = {"name": name, "beat": beat, "kind": kind, "variant": variant, "seed": seed,
               "start_tick": tick, "seconds": round(secs, 2),
               "mood": self.args.mood_eff if mood is None else mood,
               "night": bool(self.args.night if night is None else night),
               "file": "clips/" + name + ".mp4", "poster": "clips/" + name + ".png",
               "bytes": os.path.getsize(mp4)}
        row.update(more)
        if gif:
            row["gif"] = "clips/" + name + ".gif"
        self.have.setdefault(name, set()).add(kind)
        self.clips[:] = [c for c in self.clips
                         if not (c["name"] == name and c["kind"] == kind)]
        self.clips.append(row)
        self.new_rows.append(row)
        print("  cut %-26s %-8s seed %-6d %6.1f s %8d B" % (name, kind, seed, secs,
                                                           row["bytes"]), flush=True)

    def want(self, name, kind):
        return kind not in self.have.get(name, ())


def probe_log(args, seed, work, extra=(), mood=None, night=None):
    """Coarse --log-beats run of seed (no useful frames); returns parse_log's result."""
    d = os.path.join(work, "probe_seed%d" % seed)
    try:
        log = run_preview(args, seed, PROBE_EVERY, PROBE_TICKS // PROBE_EVERY, 0, d, True,
                          extra, mood, night)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    info, beats, minors, end_tick = parse_log(log)
    if info is None or not beats:
        raise RuntimeError("no loop 0 beat log in preview output")
    return info, beats, minors, (PROBE_TICKS if end_tick is None else end_tick)


def beat_windows(beats, end_tick):
    """[(tick, name, next tick)] for the beats of loop 0."""
    return [(t, b, beats[i + 1][0] if i + 1 < len(beats) else end_tick)
            for i, (t, b) in enumerate(beats)]


def run_family(args, seeds, st, work, clips_dir):
    """--family: every beat x variant of the morning or midday spine, plus the overview."""
    fam = args.family
    wanted = FAMILY_BEATS[fam]
    extra = ["--spine", fam, "--weather", "clear"]
    pad = int(round(args.pad_seconds * TICKS_PER_SECOND))
    overview = "%s_overview" % fam
    for seed in seeds:
        names = ["%s_%s_%s" % (fam, b, LETTERS[k]) for b, n in wanted.items() for k in range(n)]
        if all(not st.want(n, "beat") for n in names) and not st.want(overview, "overview"):
            break
        try:
            info, beats, minors, end_tick = probe_log(args, seed, work, extra)
        except RuntimeError as e:
            print("seed %d: %s" % (seed, e), file=sys.stderr)
            st.failed = True
            continue
        if info["family"] != fam:
            print("seed %d: the preview played the %s spine, not the %s (is it built?)" % (
                seed, info["family"], fam), file=sys.stderr)
            st.failed = True
            continue
        st.plans[:] = [p for p in st.plans
                       if not (p.get("seed") == seed and bool(p.get("night")) == bool(args.night)
                               and p.get("family", "evening") == fam)]
        st.plans.append(dict(info, seed=seed, mood=args.mood_eff, night=bool(args.night),
                             weather="clear"))
        seen = set()
        for tick, beat, nxt in beat_windows(beats, end_tick):
            if beat not in wanted or beat in seen or nxt <= tick:
                continue
            seen.add(beat)
            letter = variant_letter(info["plan"], beat)
            name = "%s_%s_%s" % (fam, beat, letter)
            if not letter or not st.want(name, "beat"):
                continue
            start = max(0, tick - pad)
            try:
                secs, mp4, png, gif = cut_clip(args, seed, name, start, nxt + pad, work,
                                               clips_dir, FAMILY_MAX_SECONDS,
                                               extra)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, name, e), file=sys.stderr)
                st.failed = True
                continue
            st.add_row(name, beat, "beat", letter, seed, start, secs, mp4, gif, family=fam,
                       weather="clear")
        if st.want(overview, "overview"):
            nframes_end = (end_tick // 8) * 8 + 1   # nframes = end_tick // 8, as the evening's
            try:
                secs, mp4, png, gif = cut_clip(args, seed, overview, 0, nframes_end, work,
                                               clips_dir, 10 ** 6, extra, every=8,
                                               play_rate=TICKS_PER_SECOND)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, overview, e), file=sys.stderr)
                st.failed = True
                continue
            st.add_row(overview, "loop_overview", "overview", None, seed, 0, secs, mp4, gif,
                       family=fam, weather="clear")


def ppm_mean(path, stride=53):
    """Mean of every stride-th byte of a binary P6 PPM's pixel data (a cheap brightness)."""
    with open(path, "rb") as f:
        data = f.read()
    # header: P6 <w> <h> <maxval> separated by single whitespace
    pos, fields = 2, 0
    while fields < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields += 1
    pos += 1
    px = data[pos::stride]
    return sum(px) / max(1, len(px))


def find_flash(args, seed, work, lo, hi, extra, mood, night):
    """Tick of the brightest frame of a coarse run over [lo, hi), or None when no frame
    stands out (< 1.1x the median). Frame f of a run with --skip T shows tick T + (f+1)*N."""
    ys = []
    t = lo
    while t < hi:
        n = min(100, (hi - t) // FLASH_STEP)
        if n < 1:
            break
        d = os.path.join(work, "flash_seed%d" % seed)
        shutil.rmtree(d, ignore_errors=True)
        try:
            run_preview(args, seed, FLASH_STEP, n, t, d, False, extra, mood, night)
            for f in range(n):
                ys.append((t + (f + 1) * FLASH_STEP, ppm_mean(os.path.join(d, "frame_%05d.ppm" % f))))
        finally:
            shutil.rmtree(d, ignore_errors=True)
        t += n * FLASH_STEP
    if not ys:
        return None, 0.0, 0.0
    med = sorted(y for _, y in ys)[len(ys) // 2]
    tick, peak = max(ys, key=lambda a: a[1])
    return (tick if peak > 1.1 * med else None), peak, med


def first_pause(beats, end_tick, min_tick, min_ticks):
    """(start, end) of the first pause of loop 0 starting at or after min_tick and lasting
    at least min_ticks, else None."""
    for tick, beat, nxt in beat_windows(beats, end_tick):
        if beat == "pause" and tick >= min_tick and nxt - tick >= min_ticks:
            return tick, nxt
    return None


def run_weather(args, seeds, st, work, clips_dir):
    """--weather-clips: the weather set (evening spine unless noted; clips at real time).
      weather_<kind>_day    mood 4, WEATHER_CLIP_SECONDS from 1 s before the first pause of
                            8 s or more at or after tick WEATHER_SETTLED_TICK (the gags
                            start on a pause's first tick; the effects have settled)
      weather_<kind>_night  the same with --night --mood 0
      weather_storm_flash   10 s centred on the brightest frame of a coarse run over
                            FLASH_SCAN (the first strike), mood 4
      weather_wind_hat      15 s from 2 s before the first pause of the loop (the hat gag
                            starts there), wind, mood 4
      major_rain_shelter    the rain_shelter major's beat window, storm, mood 4, first of
                            --seeds whose loop 0 plans it
      morning_fog_lift      FOG_LIFT_SECONDS of a clear morning (mood 1.5) at 8x: the
                            morning's own fog (the plan's fog=N steps) lifting
    All but major_rain_shelter use --weather-seed."""
    ws = args.weather_seed
    rate = TICKS_PER_SECOND
    secs_ticks = WEATHER_CLIP_SECONDS * rate

    def cut(name, tag, seed, start, end, max_s, extra, mood, night, kind="weather",
            cutopts=None, **more):
        try:
            secs, mp4, png, gif = cut_clip(args, seed, name, start, end, work, clips_dir, max_s,
                                           extra, mood, night, **(cutopts or {}))
        except RuntimeError as e:
            print("%s: %s" % (name, e), file=sys.stderr)
            st.failed = True
            return
        st.add_row(name, tag, kind, None, seed, start, secs, mp4, None, mood=mood, night=night,
                   **more)

    for kind in WEATHER_KINDS:
        for night in (False, True):
            name = "weather_%s_%s" % (kind, "night" if night else "day")
            if not st.want(name, "weather"):
                continue
            mood = 0.0 if night else 4.0
            extra = ["--weather", kind]
            try:
                info, beats, minors, end_tick = probe_log(args, ws, work, extra, mood, night)
            except RuntimeError as e:
                print("%s: %s" % (name, e), file=sys.stderr)
                st.failed = True
                continue
            p = first_pause(beats, end_tick, WEATHER_SETTLED_TICK, 200)
            if p is None:
                print("%s: no settled pause in seed %d" % (name, ws), file=sys.stderr)
                st.failed = True
                continue
            start = p[0] - rate
            cut(name, "weather_" + kind, ws, start, start + secs_ticks, WEATHER_CLIP_SECONDS + 1,
                extra, mood, night, weather=kind)

    name = "weather_storm_flash"
    if st.want(name, "weather"):
        extra = ["--weather", "storm"]
        try:
            tick, peak, med = find_flash(args, ws, work, FLASH_SCAN[0], FLASH_SCAN[1], extra, 4.0, False)
        except RuntimeError as e:
            print("%s: %s" % (name, e), file=sys.stderr)
            st.failed = True
            tick = None
        else:
            if tick is None:
                print("%s: no flash found in ticks %d-%d of seed %d" % (name, FLASH_SCAN[0],
                                                                        FLASH_SCAN[1], ws), file=sys.stderr)
                st.failed = True
            else:
                print("  flash at tick %d (brightness %.1f against median %.1f)" % (tick, peak, med))
                start = max(0, tick - 5 * rate)
                cut(name, "weather_storm", ws, start, start + 10 * rate, 11, extra, 4.0, False,
                    weather="storm", flash_tick=tick)

    name = "weather_wind_hat"
    if st.want(name, "weather"):
        extra = ["--weather", "wind"]
        try:
            info, beats, minors, end_tick = probe_log(args, ws, work, extra, 4.0, False)
            p = first_pause(beats, end_tick, 0, 200)
        except RuntimeError as e:
            print("%s: %s" % (name, e), file=sys.stderr)
            st.failed = True
            p = None
        if p is not None:
            start = max(0, p[0] - 2 * rate)
            cut(name, "weather_wind", ws, start, start + 15 * rate, 16, extra, 4.0, False,
                weather="wind")

    name = "major_rain_shelter"
    if st.want(name, "major"):
        extra = ["--weather", "storm"]
        done = False
        for seed in seeds:
            try:
                info, beats, minors, end_tick = probe_log(args, seed, work, extra, 4.0, False)
            except RuntimeError as e:
                print("seed %d: %s" % (seed, e), file=sys.stderr)
                st.failed = True
                continue
            win = [w for w in beat_windows(beats, end_tick) if w[1] == "rain_shelter"]
            if "rain_shelter" not in info["majors"] or not win:
                continue
            tick, _, nxt = win[0]
            cut(name, "rain_shelter", seed, tick, nxt, args.max_seconds, extra, 4.0, False,
                kind="major", weather="storm")
            done = True
            break
        if not done:
            print("%s: no seed in --seeds plans it in loop 0" % name, file=sys.stderr)
            st.failed = True

    name = "morning_fog_lift"
    if st.want(name, "weather"):
        extra = ["--spine", "morning", "--weather", "clear"]
        cut(name, "morning_fog", ws, 0, FOG_LIFT_SECONDS * rate, 10 ** 6, extra, 1.5, False,
            weather="clear", family="morning", cutopts={"every": 8, "play_rate": rate})


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
    ap.add_argument("--family", choices=sorted(FAMILY_BEATS),
                    help="cut that spine family's beats x variants (clips/<family>_<beat>_<letter>) "
                         "and its overview, at mood 1.5 (morning) or 3 (midday), weather clear")
    ap.add_argument("--pad-seconds", type=float, default=1.0,
                    help="--family: seconds added either side of each beat (default 1)")
    ap.add_argument("--weather-clips", action="store_true",
                    help="cut the weather set (weather_<kind>_day/night, storm flash, wind hat, "
                         "major_rain_shelter, morning_fog_lift)")
    ap.add_argument("--weather-seed", type=int, default=3,
                    help="--weather-clips: the seed of every clip but major_rain_shelter (default 3)")
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
            if p.get("family", "evening") == "evening":
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
    if args.family and args.weather_clips:
        die("--family and --weather-clips are separate runs")
    if args.family:
        if args.night:
            die("--family is a day run (no --night)")
        default_mood = FAMILY_MOOD[args.family]
    else:
        default_mood = 0.0 if args.night else 4.0
    args.mood_eff = args.mood if args.mood is not None else default_mood
    seeds = parse_seeds(args.seeds)

    clips_dir = os.path.join(out, "clips")
    work = os.path.join(out, "work")
    os.makedirs(clips_dir, exist_ok=True)
    os.makedirs(work, exist_ok=True)

    # Carry over what earlier runs into the same DIR collected.
    clips, plans = [], []
    if os.path.isfile(manifest_path):
        clips, plans = load_manifest(manifest_path, out)
    # st.have: name -> set of kinds collected under that name. A name is skipped only for the
    # same kind, so a beat that is a filler in one seed and a major in another is cut for both.
    # --force forgets what is there (so every name is cut again and replaces its row); rows of
    # names this run does not cut stay in the manifest.
    st = State(args, clips, plans)
    if args.force:
        st.have = {}
    all_plan_beats = set()
    for p in plans:
        if p.get("family", "evening") == "evening":
            all_plan_beats.update(p.get("plan", {}))
    overview_done = False

    if args.family or args.weather_clips:
        if args.family:
            run_family(args, seeds, st, work, clips_dir)
        else:
            run_weather(args, seeds, st, work, clips_dir)
        seeds = []   # the evening loop below has nothing to do

    for seed in seeds:
        try:
            log = run_preview(args, seed, PROBE_EVERY, PROBE_TICKS // PROBE_EVERY,
                              0, os.path.join(work, "probe_seed%d" % seed), True)
        except RuntimeError as e:
            print("seed %d: %s" % (seed, e), file=sys.stderr)
            st.failed = True
            continue
        finally:
            shutil.rmtree(os.path.join(work, "probe_seed%d" % seed), ignore_errors=True)
        info, beats, minors, end_tick = parse_log(log)
        if info is None or not beats:
            print("seed %d: no loop 0 beat log in preview output" % seed, file=sys.stderr)
            st.failed = True
            continue
        if end_tick is None:
            end_tick = PROBE_TICKS
        st.plans[:] = [p for p in st.plans
                       if not (p.get("seed") == seed and bool(p.get("night")) == args.night
                               and p.get("family", "evening") == info["family"])]
        st.plans.append(dict(info, seed=seed, mood=args.mood_eff, night=bool(args.night)))
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
            if kind in st.have.get(name, ()):
                continue
            if name in st.have:   # same name collected as another kind: keep both files
                name += "_" + kind
                if kind in st.have.get(name, ()):
                    continue
            try:
                secs, mp4, png, gif = cut_clip(args, seed, name, tick, nxt, work, clips_dir,
                                               args.max_seconds)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, name, e), file=sys.stderr)
                st.failed = True
                continue
            st.add_row(name, beat, kind, letter or None, seed, tick, secs, mp4, gif)

        for tick, minor in minors:
            name = "minor_" + minor
            if "minor" in st.have.get(name, ()):
                continue
            end = tick + int(round(args.minor_seconds * TICKS_PER_SECOND))
            try:
                secs, mp4, png, gif = cut_clip(args, seed, name, tick, end, work, clips_dir,
                                               args.minor_seconds)
            except RuntimeError as e:
                print("seed %d %s: %s" % (seed, name, e), file=sys.stderr)
                st.failed = True
                continue
            st.add_row(name, minor, "minor", None, seed, tick, secs, mp4, gif)

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
                st.add_row(name, "loop_overview", "overview", None, seed, 0,
                        nframes / TICKS_PER_SECOND, mp4, None)
            except RuntimeError as e:
                print("seed %d overview: %s" % (seed, e), file=sys.stderr)
                st.failed = True

    clips, plans = st.clips, st.plans
    missing = missing_variants(clips, all_plan_beats)
    manifest = {"clips": clips, "plans": plans, "missing_variants": missing}
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)

    print()
    print_table(clips)
    print("%d clips (%d new), manifest %s" % (len(clips), len(st.new_rows), manifest_path))
    if missing:
        print("missing variants: " + ", ".join("%s:%s" % (b, "".join(l)) for b, l in missing.items()))
    return 1 if st.failed else 0


if __name__ == "__main__":
    sys.exit(main())
