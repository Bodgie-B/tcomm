#!/usr/bin/env python3
"""Inject the clip manifest and a ten-loop table into the review page.

usage: review_page.py --preview PATH --dir docs/screensaver/reviews/stages2-6 [--seed 1] [--mood 4] [--loops 10]

Reads DIR/manifest.json (from review_clips.py), runs `PREVIEW --loops N` for
the table, and rewrites the two JSON <script> blocks in DIR/index.html. Also
checks that every clip the page references exists and lists the ones that
don't. Standard library only; run with `python3 -I`.
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
        plan, fillers, majors = {}, [], []
        for tok in plan_txt.split():
            k, _, v = tok.partition("=")
            if k == "fillers":
                fillers = [] if v == "none" else v.split(",")
            elif k == "majors":
                majors = [] if v == "none" else v.split(",")
            else:
                try:
                    plan[k] = int(v)
                except ValueError:
                    plan[k] = v
        rows.append({"loop": int(m.group(1)), "seed": int(m.group(2)), "length": int(m.group(3)),
                     "plan": plan, "fillers": fillers, "majors": majors, "memory": m.group(5).strip()})
    return rows


def replace_block(html, block_id, payload):
    pat = re.compile(r'(<script id="%s" type="application/json">)(.*?)(</script>)' % re.escape(block_id), re.S)
    if not pat.search(html):
        sys.exit("no <script id=%s> block in the page" % block_id)
    # '</' inside JSON strings would end the script early; the generator never produces it, but guard anyway.
    text = json.dumps(payload, separators=(",", ":")).replace("</", "<\\/")
    return pat.sub(lambda m: m.group(1) + text + m.group(3), html, count=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--mood", type=float, default=4.0)
    ap.add_argument("--loops", type=int, default=10)
    a = ap.parse_args()

    page = os.path.join(a.dir, "index.html")
    manifest_path = os.path.join(a.dir, "manifest.json")
    with open(manifest_path) as f:
        manifest = json.load(f)
    out = subprocess.run([a.preview, "--seed", str(a.seed), "--mood", str(a.mood), "--loops", str(a.loops)],
                         capture_output=True, text=True, check=True).stdout
    loops = parse_loops(out)
    if len(loops) != a.loops:
        sys.exit("expected %d loop lines, parsed %d" % (a.loops, len(loops)))

    with open(page) as f:
        html = f.read()
    html = replace_block(html, "manifest", manifest)
    html = replace_block(html, "loops", loops)
    with open(page, "w") as f:
        f.write(html)

    # Every clip the page's data block names must exist on disk.
    ids = re.findall(r"\{ id: '([a-z_]+)'", html)
    spine = re.findall(r"\{ id: '([a-z_]+)', title: '[^']*', v: \{", html)
    wanted = []
    for i in ids:
        if i in spine:
            wanted += [i + "_" + L for L in "abc"]
        else:
            wanted.append(i)
    # Minors are stored as minor_<name>; the page's data uses the bare name for those in D.minors.
    minors = re.search(r"minors: \[(.*?)\n    \]", html, re.S)
    minor_ids = re.findall(r"\{ id: '([a-z_]+)'", minors.group(1)) if minors else []
    wanted = [("minor_" + w) if w in minor_ids else w for w in wanted]
    names = {c["name"] for c in manifest.get("clips", [])}
    missing = [w for w in wanted if w not in names or not os.path.exists(os.path.join(a.dir, "clips", w + ".mp4"))]
    print("page: %s; %d clips in manifest; %d loops in table" % (page, len(names), len(loops)))
    print("missing clips: " + (", ".join(missing) if missing else "none"))
    total = sum(c.get("bytes", 0) for c in manifest.get("clips", []))
    print("clip bytes: %.1f MB" % (total / 1e6))


if __name__ == "__main__":
    main()
