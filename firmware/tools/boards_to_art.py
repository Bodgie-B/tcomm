#!/usr/bin/env python3
"""Turns the six Pd boards (P1d to P6d, the Claude Design canvas "Basement Signal Panel") into the
panel's artwork data, tcomm/src/ui/board_art_data.h.

Input: one display list per board (Night, Dawn, Morning, Midday, Golden, Dusk .json), made by
tools/board_extract.js in a browser showing the board at 800 x 480 (served, and the lists saved,
by tools/board_server.py): every SVG shape flattened to screen points, every painted HTML box,
every text run. The boards themselves are in docs/design/boards. Usage:

    python tools/boards_to_art.py <folder with the six .json> tcomm/src/ui/board_art_data.h

The boards show six status and light combinations; the panel needs every one. So each scenery
colour is stored as a rule on its zone's base colour (scaled by k, or lightened toward white by t),
fitted across the boards, one rule each for dark tiles by day and evening, light tiles (BUSY by day
and evening, dark text) and night. The script checks the rules reproduce every board within 1 level
and prints what it couldn't fit.
"""
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # forecast_icons, beside this file
import forecast_icons  # noqa: E402

BOARDS = ['Night', 'Dawn', 'Morning', 'Midday', 'Golden', 'Dusk']
PHASE = {'Night': 0, 'Dawn': 1, 'Morning': 2, 'Midday': 3, 'Golden': 4, 'Dusk': 5}
MODE = {'Night': 2, 'Dawn': 1, 'Morning': 0, 'Midday': 0, 'Golden': 0, 'Dusk': 1}  # 0 day, 1 eve, 2 night
DARK, LIGHT, NIGHT = 0, 1, 2
TOL = 0.06  # px, simplification tolerance


def rgb(s):
    m = re.match(r'rgba?\((\d+), (\d+), (\d+)', s or '')
    return tuple(int(v) for v in m.groups()) if m else None


def hexs(c):
    return '0x%02X%02X%02X' % c


# ---- geometry -----------------------------------------------------------------------------------

def dp(pts, tol):
    """Douglas-Peucker on an open polyline."""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy)
        best, bi = -1, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            d = abs(dy * (px - ax) - dx * (py - ay)) / L if L > 1e-9 else math.hypot(px - ax, py - ay)
            if d > best:
                best, bi = d, i
        if best > tol:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def simplify(sub, closed):
    pts = [tuple(p) for p in sub]
    out = dp(pts, TOL)
    if closed and len(out) > 2 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < 0.05:
        out = out[:-1]
    return out


def rect_ring(x, y, w, h, r):
    if r <= 0:
        return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    r = min(r, w / 2, h / 2)
    pts = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for i in range(9):
            a = math.radians(a0 + i * 90 / 8)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def bbox(s):
    if 'circle' in s:
        x, y, r = s['circle']
        return (x - r, y - r, x + r, y + r)
    if 'rect' in s:
        x, y, w, h, _ = s['rect']
        return (x, y, x + w, y + h)
    pts = [p for sub in s['subs'] for p in sub]
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


# ---- colour rules -------------------------------------------------------------------------------

def apply(rule, base):
    op, v = rule
    if op == 'k':
        return tuple(min(255, round(c * v)) for c in base)
    return tuple(round(c + (255 - c) * v) for c in base)


def fit(samples):
    """[(base, colour)] -> (rule, max error)."""
    best = None
    ks = [c[j] / b[j] for b, c in samples for j in range(3) if b[j] > 12]
    if ks:
        k = sum(ks) / len(ks)
        r = ('k', k)
        e = max(abs(a - b) for bs, c in samples for a, b in zip(apply(r, bs), c))
        best = (r, e)
    ts = [(c[j] - b[j]) / (255 - b[j]) for b, c in samples for j in range(3) if b[j] < 240]
    if ts:
        t = sum(ts) / len(ts)
        r = ('L', t)
        e = max(abs(a - b) for bs, c in samples for a, b in zip(apply(r, bs), c))
        if best is None or e < best[1]:
            best = (r, e)
    return best


def mirror(rule):
    """A dark tile's rule as a light tile has it: darkening by k becomes lightening by 1 - k."""
    op, v = rule
    return ('L', 1 - v) if op == 'k' else ('k', 1 - v)


# ---- output -------------------------------------------------------------------------------------

class Out:
    def __init__(self):
        self.pts = []      # (x, y) floats
        self.subs = []     # (pt0, n, closed)
        self.groups = []   # (name, comment, [shape dicts])
        self.notes = []

    def add_subs(self, subs, closed, ox, oy):
        s0 = len(self.subs)
        for sub in subs:
            pts = simplify(sub, closed)
            if len(pts) < 2:
                continue
            self.subs.append((len(self.pts), len(pts), 1 if closed else 0))
            self.pts += [(x - ox, y - oy) for x, y in pts]
        return s0, len(self.subs) - s0


DOT = re.compile(r'M\s*([-\d.]+)[\s,]+([-\d.]+)\s*h\s*\.1\s*')


def dot_subs(s):
    """A path of round-capped dots ("M16 36h.1M24 38h.1..."). The extractor samples paths along
    their length, and a 0.1-unit subpath can fall between two samples (one of the snow icon's five
    dots did), so the dots are rebuilt from the path data, placed by the ones that were found."""
    d = s.get('d') or ''
    dots = [(float(x), float(y)) for x, y in DOT.findall(d)]
    if not dots or DOT.sub('', d).strip() or len(s['subs']) == len(dots) or len(s['subs']) < 2:
        return None
    (a, b), (p, q) = s['subs'][0][0], s['subs'][1][0]
    (u, v), (m, n) = dots[0], dots[1]
    k = (p - a) / (m - u) if abs(m - u) > abs(n - v) else (q - b) / (n - v)
    tx, ty = a - k * u, b - k * v
    for sub in s['subs']:  # every dot that was found must be one of the path's
        x, y = sub[0]
        if min(abs(x - (k * u + tx)) + abs(y - (k * v + ty)) for u, v in dots) > 0.5:
            return None
    return [[(k * u + tx, k * v + ty), (k * (u + 0.1) + tx, k * v + ty)] for u, v in dots]


def shape_record(out, s, ox, oy, role, **kw):
    """One display-list shape as an output shape, its points relative to (ox, oy)."""
    rec = {'role': role, 'clip': 0, 'ops': 0, 'v': [0, 0, 0], 'rgb': [0, 0, 0], 'w': 0, 'cx': 0, 'cy': 0, 'sub0': 0, 'nsub': 0}
    rec.update(kw)
    stroke = s['stroke'] != 'none' and (s['fill'] == 'none' or s['tag'] in ('polyline', 'line'))
    if 'circle' in s:
        cx, cy, r = s['circle']
        rec['cx'], rec['cy'] = cx - ox, cy - oy
        if stroke:
            rec['kind'] = 'kRing'
            rec['w'] = s['strokeWidth']
            rec['r'] = r
        else:
            rec['kind'] = 'kDisc'
            # filled and outlined in the same colour (the lock's keyhole, the sun behind a cloud)
            rec['r'] = r + (s['strokeWidth'] / 2 if s['stroke'] != 'none' else 0)
    else:
        if 'rect' in s:
            x, y, w, h, r = s['rect']
            subs, closed = [rect_ring(x, y, w, h, r)], True
        else:
            subs, closed = dot_subs(s) or s['subs'], s['closed']
        if stroke:
            rec['kind'] = 'kStroke'
            rec['w'] = s['strokeWidth']
            if 'rect' in s:
                subs = [subs[0] + [subs[0][0]]]
                closed = False
        else:
            rec['kind'] = 'kFill'
        rec['sub0'], rec['nsub'] = out.add_subs(subs, closed and not stroke, ox, oy)
    return rec


def colour_of(s):
    return rgb(s['fill']) if s['fill'] != 'none' else rgb(s['stroke'])


# ---- the parts of a board -----------------------------------------------------------------------

def tile_shapes(shapes, x, y):
    for s in shapes:
        h = s['hclip']
        if h and abs(h['x'] - x) < 1 and abs(h['y'] - y) < 1 and s['opacity']['groups']:
            return [t for t in shapes if t['svg'] == s['svg']]
    raise SystemExit('no tile at %s,%s' % (x, y))


def svg_shapes_in(shapes, x0, y0, x1, y1, exclude=()):
    """Shapes not in a tile group, whose bounding box lies inside the rectangle."""
    out = []
    for s in shapes:
        if s['svg'] in exclude:
            continue
        b = bbox(s)
        if b[0] >= x0 - 0.5 and b[1] >= y0 - 0.5 and b[2] <= x1 + 0.5 and b[3] <= y1 + 0.5:
            out.append(s)
    return out


def main():
    J, dest = sys.argv[1], sys.argv[2]
    data = {b: json.load(open('%s/%s.json' % (J, b))) for b in BOARDS}
    out = Out()
    report = []

    # ---- floor tiles ----
    for tile, (tx, ty) in (('up', (12, 74)), ('down', (12, 279))):
        per = {b: tile_shapes(data[b]['shapes'], tx, ty) for b in BOARDS}
        n = len(per['Midday'])
        assert all(len(v) == n for v in per.values()), 'tile shape counts differ'
        first_clip = next(i for i, s in enumerate(per['Midday']) if s['clip'])
        base_idx = {'W': 0, 'S': first_clip - 1}  # weather zone fill; the status zone's own outline
        cls_of = {}
        for b in BOARDS:  # a light tile is one with dark text: BUSY by day and evening
            label = next(t for t in data[b]['texts'] if abs(t['x'] - 36) < 1 and abs(t['y'] - (ty + 14.5)) < 2)
            cls_of[b] = NIGHT if MODE[b] == 2 else (LIGHT if sum(rgb(label['color'])) < 300 else DARK)
        recs, worst = [], 0
        for i in range(n):
            s0 = per['Midday'][i]
            own = 'S' if (s0['clip'] or i == base_idx['S']) else 'W'
            choices = []
            for ref in ('W', 'S'):
                by_cls = {DARK: [], LIGHT: [], NIGHT: []}
                for b in BOARDS:
                    by_cls[cls_of[b]].append((colour_of(per[b][base_idx[ref]]), colour_of(per[b][i])))
                fits = {c: fit(v) for c, v in by_cls.items() if v}
                err = max(f[1] for f in fits.values())
                choices.append((err + (0 if ref == own else 0.5), ref, fits))
            err, ref, fits = min(choices, key=lambda c: c[0])
            rules = {c: f[0] for c, f in fits.items()}
            if LIGHT not in rules:  # no light tile on the boards for this tile: mirror the dark rule
                rules[LIGHT] = mirror(rules[DARK]) if ref == 'S' else rules[DARK]
            e = max(f[1] for f in fits.values())
            worst = max(worst, e)
            if e > 1:
                report.append('%s tile shape %d (%s, ref %s): misfit %s' % (tile, i, s0['tag'], ref, {c: (f[0][0], round(f[0][1], 3), f[1]) for c, f in fits.items()}))
            ops = sum((1 << c) for c in (DARK, LIGHT, NIGHT) if rules[c][0] == 'L')
            rec = shape_record(out, s0, tx, ty, 'kZoneS' if ref == 'S' else 'kZoneW', clip=1 if s0['clip'] else 0,
                               ops=ops, v=[rules[DARK][1], rules[LIGHT][1], rules[NIGHT][1]])
            recs.append(rec)
        # the mirror rule, checked where the boards do show a light tile
        if tile == 'down':
            mis = 0
            for i, r in enumerate(recs):
                if r['role'] != 'kZoneS':
                    continue
                d = (('L' if r['ops'] & 1 else 'k'), r['v'][0])
                l = (('L' if r['ops'] & 2 else 'k'), r['v'][1])
                m = mirror(d)
                if m[0] != l[0] or abs(m[1] - l[1]) > 0.02:
                    mis += 1
            report.append('down tile: %d status-zone shapes where BUSY is not the mirror of the dark rule' % mis)
        report.append('%s tile: %d shapes, worst colour error %d levels' % (tile, n, worst))
        out.groups.append(('k%sTile' % tile.capitalize(), '%s floor tile, tile coordinates (514 x 193)' % tile, recs))

    # ---- outside tile: one colour set per light ----
    src = {0: 'Midday', 1: 'Dawn', 2: 'Night'}
    per = {m: tile_shapes(data[b]['shapes'], 538, 74) for m, b in src.items()}
    recs = []
    for i, s in enumerate(per[0]):
        rec = shape_record(out, s, 538, 74, 'kPerMode', rgb=[int(hexs(colour_of(per[m][i])), 16) for m in (0, 1, 2)])
        recs.append(rec)
    out.groups.append(('kOutsideTile', 'Outside tile scenery, tile coordinates (250 x 193), a colour per light', recs))

    # ---- sky: per phase ----
    sky_rows = []
    for b in BOARDS:
        sh = data[b]['shapes']
        sky = [s for s in sh if s['svg'] == sh[0]['svg']]
        rects = [colour_of(s) for s in sky if s['tag'] == 'rect']
        circles = [s for s in sky if s['tag'] == 'circle']
        big = [s for s in circles if s['circle'][2] > 5]
        stars = [s for s in circles if s['circle'][2] <= 5]
        hills = [s for s in sky if s['tag'] == 'path']
        ov = [s for s in sh if s['tag'] == 'circle' and s['fillOpacity'] < 0.2]
        sky_rows.append({
            'b': b, 'bands': rects, 'rings': [(s['circle'][2], colour_of(s)) for s in big],
            'centre': big[0]['circle'][:2], 'hills': [colour_of(s) for s in hills],
            'overlay': (colour_of(ov[0]), [s['fillOpacity'] for s in ov]) if ov else None,
            'stars': [(s['circle'][0], s['circle'][1], s['circle'][2]) for s in stars]})
        if b == 'Midday':
            hill_recs = [shape_record(out, s, 0, 0, 'kFixed') for s in hills]
    out.groups.append(('kSkyHills', 'the two hills along the bottom of the sky, screen coordinates; colours per phase', hill_recs))

    # ---- header strip scenery ----
    def strip_parts(b):
        sh = data[b]['shapes']
        box = next(x for x in data[b]['boxes'] if abs(x['y'] - 6) < 0.5 and x['h'] == 56 and x['opacity']['total'] < 1)
        return [s for s in sh if s['hclip'] and abs(s['hclip']['y'] - 6) < 0.5 and s['hclip']['h'] == 56], box

    strip_groups = {}
    for name, b, region_left in (('kStripOut', 'Midday', 604), ('kStripIn', 'Golden', 423)):
        parts, box = strip_parts(b)
        base = rgb(box['bg'])
        recs = []
        for s in parts[1:]:  # [0] is the disc, placed by p at run time
            c = colour_of(s)
            k = sum(c) / sum(base)
            recs.append(shape_record(out, s, region_left, 6, 'kStripK', v=[k, k, k]))
        strip_groups[name] = (region_left, parts[0])
        out.groups.append((name, 'strip scenery with the dog %s, relative to the scenery region (%s); colours the strip x k'
                           % ('out' if name == 'kStripOut' else 'in', region_left), recs))
    # holly (holiday) and the night crescent and stars
    parts, box = strip_parts('Morning')
    holly_left = 423
    recs = [shape_record(out, s, holly_left, 6, 'kFixed', rgb=[int(hexs(colour_of(s)), 16)] * 3) for s in parts[1:]]
    out.groups.append(('kStripHolly', 'the holly sprig with a holiday message, relative to its region (423, Morning board)', recs))
    parts, box = strip_parts('Night')
    moon = [s for s in parts if s['tag'] == 'circle' and s['circle'][2] > 5]
    stars = [s for s in parts if s['tag'] == 'circle' and s['circle'][2] <= 5]
    crescent = [(s['circle'][0] - 423, s['circle'][1] - 6, s['circle'][2], colour_of(s)) for s in moon]
    star_pos = [((s['circle'][0] - 423) / 299.0, s['circle'][1] - 6, s['circle'][2]) for s in stars]
    discs = {}
    for b in BOARDS:
        prt, bx = strip_parts(b)
        d = prt[0]
        discs[b] = (colour_of(d), d['circle'])

    # ---- badges and icons ----
    def icon_group(name, comment, shapes, ox, oy, fg, bg, extra=()):
        recs = list(extra)
        for s in shapes:
            c = colour_of(s)
            role = 'kFg' if c == fg else ('kBg' if c == bg else None)
            if role is None:
                out.notes.append('%s: a colour that is neither fg nor bg: %s' % (name, c))
                role = 'kFg'
            recs.append(shape_record(out, s, ox, oy, role))
        out.groups.append((name, comment, recs))

    def tile_colours(b, x, y):
        """fg and bg of the floor tile at (x, y) on board b."""
        t = tile_shapes(data[b]['shapes'], x, y)
        first_clip = next(i for i, s in enumerate(t) if s['clip'])
        bg = colour_of(t[first_clip - 1])
        txt = next(tt for tt in data[b]['texts'] if abs(tt['x'] - 36) < 1 and abs(tt['y'] - (y + 14.5)) < 2)  # the floor label
        return rgb(txt['color']), bg

    badges = (('kBadgeOpen', 'Morning', 74), ('kBadgeBusy', 'Midday', 279), ('kBadgePrivate', 'Golden', 74),
              ('kBadgeSleeping', 'Dawn', 74), ('kBadgeAway', 'Midday', 74), ('kBadgeNoSignal', 'Dusk', 74))
    for name, b, ty in badges:
        bx, by = 301, ty + 105
        fg, bg = tile_colours(b, 12, ty)
        sh = svg_shapes_in(data[b]['shapes'], bx, by, bx + 80, by + 80)
        sh = [s for s in sh if not s['opacity']['groups']]
        extra = []
        if not any(s['tag'] == 'circle' and colour_of(s) == fg and s['fill'] != 'none' for s in sh) and name == 'kBadgeOpen':
            extra.append({'kind': 'kDisc', 'role': 'kFg', 'clip': 0, 'ops': 0, 'v': [0, 0, 0], 'rgb': [0, 0, 0], 'w': 0,
                          'cx': 40, 'cy': 40, 'r': 40, 'sub0': 0, 'nsub': 0})  # an HTML disc on the board
        icon_group(name, 'badge, 80 x 80 (%s board)' % b, sh, bx, by, fg, bg, extra)

    mid = data['Midday']['shapes']
    gold = data['Golden']['shapes']
    pal_dogc = (63, 124, 133)
    icon_group('kPawIn', 'paw, 36 x 36 (dog in)', svg_shapes_in(gold, 22, 16, 58, 52), 22, 16, pal_dogc, None)
    icon_group('kPawOut', 'paw, 34 x 34 (dog out, in the white circle)', svg_shapes_in(mid, 23, 17, 57, 51), 23, 17, pal_dogc, None)
    icon_group('kGear', 'gear, 32 x 32', svg_shapes_in(mid, 744, 18, 776, 50), 744, 18, (58, 31, 43), None)
    chev = next(x for x in data['Midday']['boxes'] if x['w'] == 40)
    icon_group('kCaret', 'chevron caret, 16 x 16', svg_shapes_in(mid, chev['x'] + 12, 437, chev['x'] + 28, 453), chev['x'] + 12, 437,
               (58, 31, 43), None)
    # humidity drops: the inside one is 15 x 19 at (text x - 18, row centre - 9.5); outside 14 x 18
    t_in = next(t for t in data['Midday']['texts'] if t['text'] == '40%')
    ox, oy = t_in['x'] - 18, t_in['y'] + t_in['h'] / 2 - 9.5
    icon_group('kDropIn', 'humidity drop, 15 x 19', svg_shapes_in(mid, ox, oy, ox + 15, oy + 19), ox, oy, (58, 31, 43), None)
    t_out = next(t for t in data['Midday']['texts'] if t['text'] == '52%')
    ox, oy = t_out['x'] - 17, t_out['y'] + t_out['h'] / 2 - 9
    icon_group('kDropOut', 'humidity drop, 14 x 18', svg_shapes_in(mid, ox, oy, ox + 14, oy + 18), ox, oy, (58, 31, 43), None)
    icon_group('kBell', 'doorbell bell, 54 x 54', svg_shapes_in(mid, 636, 334.5, 690, 388.5), 636, 334.5, (251, 241, 220), None)
    # Forecast icons, from their SVG in tools/forecast_icons.py (five are the boards' own).
    for name, comment, elements in forecast_icons.ICONS:
        recs = [shape_record(out, s, 0, 0, 'kFg') for s in forecast_icons.shapes(elements)]
        out.groups.append((name, 'forecast icon, 88 x 88: ' + comment, recs))

    # ---- write ----
    w = []
    w.append('// Generated by tools/boards_to_art.py from the six Pd boards (P1d to P6d). Do not edit.')
    w.append('// Points are 1/16 px. Colour rules: see board_art.h.')
    w.append('#pragma once')
    w.append('#include "board_art.h"')
    w.append('')
    w.append('namespace boardart {')
    w.append('')
    w.append('const int16_t kPts[] = {')
    line = []
    for x, y in out.pts:
        line.append('%d,%d' % (round(x * 16), round(y * 16)))
        if len(line) == 12:
            w.append('    ' + ', '.join(line) + ',')
            line = []
    if line:
        w.append('    ' + ', '.join(line) + ',')
    w.append('};')
    w.append('const Sub kSubs[] = {')
    for i in range(0, len(out.subs), 8):
        w.append('    ' + ' '.join('{%d, %d, %d},' % s for s in out.subs[i:i + 8]))
    w.append('};')
    w.append('')
    for name, comment, recs in out.groups:
        w.append('// %s' % comment)
        w.append('const Shape %s_shapes[] = {' % name)
        for r in recs:
            w.append('    {%s, %s, %d, %d, {%.4ff, %.4ff, %.4ff}, {0x%06X, 0x%06X, 0x%06X}, %.3ff, %.3ff, %.3ff, %.3ff, %d, %d},' % (
                r['kind'], r['role'], r['clip'], r['ops'], r['v'][0], r['v'][1], r['v'][2], r['rgb'][0], r['rgb'][1], r['rgb'][2],
                r['w'], r.get('r', 0), r['cx'], r['cy'], r['sub0'], r['nsub']))
        w.append('};')
        w.append('const Group %s = {%s_shapes, %d};' % (name, name, len(recs)))
        w.append('')
    # sky table
    w.append('// Sky per phase (Night, Dawn, Morning, Midday, Golden, Dusk): band colours from y 0, 190, 290, 380;')
    w.append('// rings around the sun or moon, outer to inner, then the disc; the two hills; the overlay over everything.')
    w.append('const SkyPhase kSky[6] = {')
    for row in sky_rows:
        rings = row['rings']
        ov = row['overlay']
        w.append('    {{%s}, {%s}, {%s}, {%s, %s}, 0x%06X, {%s}, // %s' % (
            ', '.join(hexs(c) for c in row['bands']),
            ', '.join('%.1ff' % r for r, _ in rings), ', '.join(hexs(c) for _, c in rings),
            hexs(row['hills'][0]), hexs(row['hills'][1]), int(hexs(ov[0]), 16), ', '.join('%.3ff' % o for o in ov[1]), row['b']))
        w.append('     {%.1ff, %.1ff}},' % tuple(row['centre']))
    w.append('};')
    stars = next(r for r in sky_rows if r['b'] == 'Night')['stars']
    w.append('const float kSkyStars[][3] = {%s};' % ', '.join('{%.1ff, %.1ff, %.1ff}' % s for s in stars))
    w.append('')
    w.append('// Strip discs per phase (colour, radius), the night crescent (region 423 to 722 on the Night board),')
    w.append('// and the night stars (x as a share of the region, y in the strip).')
    w.append('const uint32_t kStripDisc[6] = {%s};' % ', '.join(hexs(discs[b][0]) for b in BOARDS))
    w.append('const float kStripDiscR = %.1ff;' % discs['Midday'][1][2])
    w.append('const float kCrescent[2][4] = {%s};' % ', '.join('{%.2ff, %.2ff, %.2ff, 0}' % c[:3] for c in crescent))
    w.append('const uint32_t kCrescentColour = %s;' % hexs(crescent[0][3]))
    w.append('const float kStripStars[][3] = {%s};' % ', '.join('{%.4ff, %.2ff, %.2ff}' % s for s in star_pos))
    w.append('')
    w.append('}  // namespace boardart')
    open(dest, 'w', newline='\n').write('\n'.join(w) + '\n')

    report.append('%d points, %d subpaths, %d groups' % (len(out.pts), len(out.subs), len(out.groups)))
    for b in BOARDS:
        d = discs[b]
        report.append('strip disc %s: centre %.1f, %.1f r %.1f colour %s' % (b, d[1][0], d[1][1], d[1][2], hexs(d[0])))
    for line in report + out.notes:
        print(line)


if __name__ == '__main__':
    main()
