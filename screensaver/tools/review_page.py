#!/usr/bin/env python3
"""Inject the clip manifest and the ten-loop tables into the review page.

usage: review_page.py --preview PATH --dir docs/screensaver/reviews/stages2-6 [--seed 1] [--loops 10]
           [--mood 4] [--mood-morning 1.5] [--mood-midday 3]

Reads DIR/manifest.json (from review_clips.py), runs `PREVIEW --loops N` three times for
the tables (the evening at --mood, the morning at --mood-morning with --spine morning, the
midday at --mood-midday with --spine midday; each row carries the loop's family and
weather) and rewrites the JSON <script> blocks (manifest, loops, loops_morning,
loops_midday) in DIR/index.html. Also checks that every clip the page's data block D names
exists (the Lookout section is a placeholder driven by lookout_* clips in the manifest and
is not checked) and lists the ones that don't. Standard library only; run with `python3 -I`.
"""
import argparse
import json
import os
import re
import subprocess
import sys


def parse_loops(text):
    rows = []
    for line in text.splitlines():
        m = re.match(r"loop=(\d+) seed=(\d+) length=(\d+)s plan=(.*?) memory=(.*)$", line.strip())
        if not m:
            continue
        plan_txt = m.group(4)
        plan, fillers, majors, family = {}, [], [], "evening"
        for tok in plan_txt.split():
            k, _, v = tok.partition("=")
            if k == "family":   # spine family (plan lines from before the families have none)
                family = v
            elif k == "fillers":
                fillers = [] if v == "none" else v.split(",")
            elif k == "majors":
                majors = [] if v == "none" else v.split(",")
            else:
                try:
                    plan[k] = int(v)
                except ValueError:
                    plan[k] = v
        # The memory column ends in " weather=KIND" (the loop's weather) on newer previews.
        memory, weather = m.group(5).strip(), ""
        wm = re.search(r"\s+weather=(\w+)$", memory)
        if wm:
            weather, memory = wm.group(1), memory[:wm.start()]
        rows.append({"loop": int(m.group(1)), "seed": int(m.group(2)), "length": int(m.group(3)),
                     "family": family, "weather": weather, "plan": plan, "fillers": fillers,
                     "majors": majors, "memory": memory})
    return rows


def replace_block(html, block_id, payload):
    pat = re.compile(r'(<script id="%s" type="application/json">)(.*?)(</script>)' % re.escape(block_id), re.S)
    if not pat.search(html):
        sys.exit("no <script id=%s> block in the page" % block_id)
    # '</' inside JSON strings would end the script early; the generator never produces it, but guard anyway.
    text = json.dumps(payload, separators=(",", ":")).replace("</", "<\\/")
    return pat.sub(lambda m: m.group(1) + text + m.group(3), html, count=1)


def run_loops(preview, seed, mood, n, spine=None):
    cmd = [preview, "--seed", str(seed), "--mood", "%g" % mood, "--loops", str(n)]
    if spine:
        cmd += ["--spine", spine]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    rows = parse_loops(out)
    if len(rows) != n:
        sys.exit("expected %d loop lines, parsed %d (%s)" % (n, len(rows), " ".join(cmd)))
    if spine and any(r["family"] != spine for r in rows):
        sys.exit("--spine %s: the preview played %s" % (spine, sorted({r["family"] for r in rows})))
    return rows


# data-block lists of the page's D object that name clips, and the clip-name prefix of each
# (the weather list names a day and a night clip per kind; weatherExtras names clips as is).
CLIP_LISTS = [("opening", ""), ("spine", ""), ("fillers", ""), ("minors", "minor_"),
              ("majors", ""), ("morning", "morning_"), ("midday", "midday_"),
              ("weather", "weather_"), ("weatherExtras", "")]


def wanted_clips(html):
    wanted = []
    for key, prefix in CLIP_LISTS:
        m = re.search(r"^    %s: \[\n(.*?)^    \]" % re.escape(key), html, re.S | re.M)
        if not m:
            continue
        body = m.group(1)
        # An entry is one line ({ id: 'x', ... }) or a block with v: { a: '..', b: '..' } }.
        for e in re.finditer(r"\{ id: '([a-z_]+)'", body):
            ident = e.group(1)
            rest = body[e.end():]
            nxt = re.search(r"\{ id: '", rest)
            rest = rest[:nxt.start()] if nxt else rest
            v = re.search(r"v: \{(.*)", rest, re.S)
            if v:   # a spine beat: one clip per variant letter
                letters = re.findall(r"(?:^|[\s,{])([a-d]): '", v.group(1))
                wanted += ["%s%s_%s" % (prefix, ident, L) for L in letters]
            elif key == "weather":
                wanted += ["weather_%s_day" % ident, "weather_%s_night" % ident]
            else:
                wanted.append(prefix + ident)
    return wanted


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--mood", type=float, default=4.0, help="the evening table's mood (default 4)")
    ap.add_argument("--mood-morning", type=float, default=1.5)
    ap.add_argument("--mood-midday", type=float, default=3.0)
    ap.add_argument("--loops", type=int, default=10)
    a = ap.parse_args()

    page = os.path.join(a.dir, "index.html")
    manifest_path = os.path.join(a.dir, "manifest.json")
    with open(manifest_path) as f:
        manifest = json.load(f)
    loops = run_loops(a.preview, a.seed, a.mood, a.loops)
    loops_morning = run_loops(a.preview, a.seed, a.mood_morning, a.loops, "morning")
    loops_midday = run_loops(a.preview, a.seed, a.mood_midday, a.loops, "midday")

    with open(page) as f:
        html = f.read()
    html = replace_block(html, "manifest", manifest)
    html = replace_block(html, "loops", loops)
    html = replace_block(html, "loops_morning", loops_morning)
    html = replace_block(html, "loops_midday", loops_midday)
    with open(page, "w") as f:
        f.write(html)

    # Every clip the page's data block names must exist on disk.
    wanted = wanted_clips(html)
    names = {c["name"] for c in manifest.get("clips", [])}
    missing = [w for w in wanted if w not in names or not os.path.exists(os.path.join(a.dir, "clips", w + ".mp4"))]
    # The overview cards are named in the page's script, outside D.
    for extra in re.findall(r"id: '((?:morning|midday)_overview)'", html):
        if extra not in names or not os.path.exists(os.path.join(a.dir, "clips", extra + ".mp4")):
            missing.append(extra)
        wanted.append(extra)
    lookout = sorted(n for n in names if n.startswith("lookout_"))
    print("page: %s; %d clips in manifest; %d clips checked; loops tables: %d evening, %d morning, %d midday"
          % (page, len(names), len(wanted), len(loops), len(loops_morning), len(loops_midday)))
    print("missing clips: " + (", ".join(missing) if missing else "none") + " (lookout placeholder, %d clips so far)" % len(lookout))
    total = sum(c.get("bytes", 0) for c in manifest.get("clips", []))
    print("clip bytes: %.1f MB" % (total / 1e6))
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
