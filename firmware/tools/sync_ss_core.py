"""Refreshes libraries/ss_core/src from screensaver/core for the panel build (tools/fw.sh calls it).

Each .cpp gets `#pragma GCC optimize("O2")` on top: Arduino builds everything for size (-Os), and the
screensaver's drawing is the hot path (measured on the panel, 5 Nov: 15 to 18 fps at -Os). The rest
of the firmware stays at -Os. A file is rewritten only when what it should hold changes (with a new
time stamp, so the Arduino build cache recompiles it), so an unchanged core doesn't rebuild; files
gone from the source are removed from the copy.
Args: source folder (screensaver/core), destination (libraries/ss_core/src).
"""
import os
import sys

PRAGMA = '#pragma GCC optimize("O2")  // tools/sync_ss_core.py: the screensaver draws at -O2\n'
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
wanted = set()
written = 0
for folder, _, names in os.walk(src):
    for name in names:
        s = os.path.join(folder, name)
        d = os.path.join(dst, os.path.relpath(s, src))
        wanted.add(os.path.normcase(os.path.normpath(d)))
        data = open(s, 'rb').read()
        if name.endswith('.cpp'):
            data = PRAGMA.encode() + data
        if os.path.exists(d) and open(d, 'rb').read() == data:
            continue
        os.makedirs(os.path.dirname(d), exist_ok=True)
        open(d, 'wb').write(data)
        written += 1
for folder, _, names in os.walk(dst):
    for name in names:
        d = os.path.join(folder, name)
        if os.path.normcase(os.path.normpath(d)) not in wanted:
            os.remove(d)
print(f'ss_core: {written} file(s) refreshed')
