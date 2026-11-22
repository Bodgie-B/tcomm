"""Refreshes libraries/ss_core/src from screensaver/core for the panel build (tools/fw.sh calls it).

Each .cpp gets `#pragma GCC optimize("O2")` on top: Arduino builds everything for size (-Os), and the
screensaver's drawing is the hot path (measured on the panel, 5 Nov: 15 to 18 fps at -Os). The rest
of the firmware stays at -Os. A file is rewritten only when what it should hold changes (with a new
time stamp, so the Arduino build cache recompiles it), so an unchanged core doesn't rebuild; files
gone from the source are removed from the copy.

The story files in AT_OS stay at the firmware's -Os (memory work, 2026-11-21): they say who walks
where during one episode and hand every pixel to gfx/raster.cpp, which stays at -O2, so speed hardly
moves (host timing of every frame of six seeds: within 2 percent, the same as run-to-run noise), and
the picture is the same (host frame checksums identical). The panel copies its code into PSRAM in
64 KB pages, so code size is PSRAM: these files are 74,628 bytes smaller at -Os (relinked from the
2026-11-21 build), one page (65,536 bytes of PSRAM, idle and with the screensaver running). Files
that touch pixels themselves, and the parts drawn in every frame (landscape, stream, ambient, world,
app, the spines, critters, props), are not in it.

Every .cpp also gets `#pragma GCC optimize("no-exceptions")` after its #include lines (NOEXC; after
them because the C++ library's headers need exceptions on). The core never throws or catches (its
host build uses -fno-exceptions, screensaver/CMakeLists.txt), and nothing in the firmware catches,
so a throw would end in the same reset either way; but at the firmware's -fexceptions every function
carried unwind tables in read-only data, which is copied into PSRAM too: 35,248 bytes, and the -Os
files above added 1,632 more, leaving read-only data 368 bytes short of a twelfth page. Without
them it has 35,616 spare. docs/memory/WORKLOG.md has the figures.
Args: source folder (screensaver/core), destination (libraries/ss_core/src).
"""
import os
import sys

PRAGMA = '#pragma GCC optimize("O2")  // tools/sync_ss_core.py: the screensaver draws at -O2\n'
NOEXC = '#pragma GCC optimize("no-exceptions")  // tools/sync_ss_core.py: no unwind tables (PSRAM)'
AT_OS_LINE = '// tools/sync_ss_core.py: a story file, left at the firmware\'s -Os (its code is PSRAM)\n'
AT_OS = {
    'scene/' + n + '.cpp' for n in (
        'majors', 'bike', 'minors', 'visitors', 'reeds', 'housedogs', 'day_trip', 'plink', 'archery',
        'kite', 'ropes', 'taichi', 'morning_stay', 'day_longshot', 'lookout', 'slackline', 'herd',
        'sunrise', 'sunrise_land', 'fetch', 'lookout_land', 'mailbird', 'day_plink', 'trip',
        'day_taichi', 'patch', 'day_herd', 'skytrip', 'day_boomerang', 'memory', 'day_kite',
        'director', 'buildup', 'day_bike', 'day_slackline', 'day_archery', 'day_ropes',
        'stage22_switch')
}


def no_exceptions(text):
    """NOEXC on the line after the file's last #include line (none: unchanged)."""
    lines = text.split(b'\n')
    last = max((i for i, l in enumerate(lines) if l.startswith(b'#include')), default=-1)
    if last < 0:
        return text
    eol = b'\r' if lines[last].endswith(b'\r') else b''
    lines.insert(last + 1, NOEXC.encode() + eol)
    return b'\n'.join(lines)


src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
wanted = set()
written = 0
for folder, _, names in os.walk(src):
    for name in names:
        s = os.path.join(folder, name)
        rel = os.path.relpath(s, src)
        d = os.path.join(dst, rel)
        wanted.add(os.path.normcase(os.path.normpath(d)))
        data = open(s, 'rb').read()
        if name.endswith('.cpp'):
            data = (AT_OS_LINE if rel.replace(os.sep, '/') in AT_OS else PRAGMA).encode() + no_exceptions(data)
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
