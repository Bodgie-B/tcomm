"""The outside tile's forecast icons, one per WeatherGate Zambretti symbol, with night versions.

They are drawn as the Pd boards draw theirs: 48-unit line drawings at 88 px, stroke 3.5, round
ends and joins, in one colour. Five come straight from the boards (sun, moon, sun behind cloud,
rain, snow; their path data is copied here as the boards have it). The rest are new, in the same
hand, after the WeatherGate symbols in its ZAMBRETTI_REFERENCE.md:

    sunny          sun                      (night: moon)
    partly_cloudy  sun behind a cloud       (night: moon behind a cloud)
    cloudy         a cloud on its own
    clearing       sun, a small two-lobe cloud breaking away   (night: moon)
    rain           cloud, three drops
    heavy_rain     cloud, five longer drops
    snow           cloud, five flakes
    storm          cloud, a lightning bolt
    danger heat    a bigger, filled sun; its rays are their own group so they can pulse

tools/boards_to_art.py imports this and writes the icons into board_art_data.h with the rest.
"""
import math
import re

SIZE = 88.0           # px on the panel
UNIT = SIZE / 48.0    # px per drawing unit
STROKE = 3.5          # the boards' icon stroke, in units

CLOUD = 'M14 28h20a7 7 0 0 0 1-13.900A10 10 0 0 0 15 16a6.500 6.500 0 0 0-1 12z'  # rain and snow (board)
CLOUD_SUN = 'M16 38h21a7 7 0 0 0 1-13.900A10 10 0 0 0 18 26a6.500 6.500 0 0 0-2 12z'  # behind the sun (board)
MOON = 'M31 7a17 17 0 1 0 11 27A14 14 0 0 1 31 7z'  # Night board
SUN_RAYS = 'M24 6v5M24 37v5M6 24h5M37 24h5M11.300 11.300l3.500 3.500M33.200 33.200l3.500 3.500M11.300 36.700l3.500-3.500M33.200 14.800l3.500-3.500'
# Clearing: a small two-lobe cloud, lower right, clear of the sun or moon.
CLOUD_PART = 'M25 41h14a5.500 5.500 0 0 0 0-11A7.500 7.500 0 0 0 25 41z'


def rays(cx, cy, r0, r1, n=8):
    return ''.join('M%.3f %.3fL%.3f %.3f' % (cx + r0 * math.cos(a), cy + r0 * math.sin(a), cx + r1 * math.cos(a), cy + r1 * math.sin(a))
                   for a in (2 * math.pi * k / n for k in range(n)))


# Each element: (kind, data, options). kind 'path' (data = SVG path d) or 'circle' (cx, cy, r).
# options: stroke (units, None for none), fill (bool), at = (scale, cx, cy, tx, ty): scale about
# (cx, cy), then move by (tx, ty).
def P(d, stroke=STROKE, fill=False, at=None):
    return ('path', d, {'stroke': stroke, 'fill': fill, 'at': at})


def C(cx, cy, r, stroke=STROKE, fill=False):
    return ('circle', (cx, cy, r), {'stroke': stroke, 'fill': fill, 'at': None})


ICONS = [
    # name, comment, elements
    ('kFcSun', 'sunny (Golden board)', [C(24, 24, 8), P(SUN_RAYS)]),
    ('kFcMoon', 'sunny at night (Night board)', [P(MOON)]),
    ('kFcCloudSun', 'partly cloudy (Dawn board)',
     [C(18, 17, 6, fill=True), P('M18 5v3M6 17h3M9 8l2 2M27 8l-2 2'), P(CLOUD_SUN)]),
    ('kFcCloudMoon', 'partly cloudy at night: the moon where the sun is',
     [P(MOON, fill=True, at=(0.5, 24.5, 24, -7, -8.5)), P(CLOUD_SUN)]),
    ('kFcCloud', 'cloudy: the cloud a size up, centred', [P(CLOUD, at=(1.2, 24.5, 17, 0, 7))]),
    ('kFcClearing', 'clearing: the sun, a small cloud breaking away',
     [C(15, 15, 5.5), P(rays(15, 15, 9.5, 12.5)), P(CLOUD_PART)]),
    ('kFcClearingMoon', 'clearing at night',
     [P(MOON, fill=True, at=(0.5, 24.5, 24, -9, -9)), P(CLOUD_PART)]),
    ('kFcRain', 'rain (Midday board)', [P(CLOUD), P('M17 34l-2 7M25 34l-2 7M33 34l-2 7')]),
    ('kFcHeavyRain', 'heavy rain: five drops, longer',
     [P(CLOUD), P('M13 33l-3 10M19 33l-3 10M25 33l-3 10M31 33l-3 10M37 33l-3 10')]),
    ('kFcSnow', 'snow (Morning board)', [P(CLOUD), P('M16 36h.1M24 38h.1M32 36h.1M20 43h.1M28 43h.1', stroke=5)]),
    ('kFcStorm', 'storm: a lightning bolt under the cloud',
     [P(CLOUD), P('M25.5 31L19 40H24L21 46L30.5 35.5H25.5L28.5 31Z', stroke=2, fill=True)]),
    ('kFcHeatDisc', 'danger heat: a bigger sun, filled', [C(24, 24, 10, fill=True)]),
    ('kFcHeatRays', 'danger heat: its rays, which pulse', [P(rays(24, 24, 15.5, 21.5))]),
]


# ---- SVG path flattening -------------------------------------------------------------------------

TOKEN = re.compile(r'[MmLlHhVvAaCcSsQqZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')


def _arc(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """SVG arc, endpoint form, as points after (x1, y1) (SVG 1.1 implementation notes F.6.5)."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    k = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        k = -k
    cxp, cyp = k * rx * y1p / ry, -k * ry * x1p / rx
    cx, cy = cp * cxp - sp * cyp + (x1 + x2) / 2, sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(2, int(math.ceil(abs(dt) * max(rx, ry) * UNIT / 1.5)))  # about 1.5 px per step
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    pts[-1] = (x2, y2)
    return pts


def _cubic(p0, p1, p2, p3):
    n = 24
    out = []
    for i in range(1, n + 1):
        t = i / n
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def flatten(d):
    """An SVG path as subpaths: [(points, closed)], in the path's own units."""
    toks = TOKEN.findall(d)
    subs, cur, closed = [], [], False
    x = y = sx = sy = 0.0
    last_c = None
    i, cmd = 0, None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def end():
        nonlocal cur, closed
        if len(cur) > 1:
            subs.append((cur, closed))
        cur, closed = [], False

    while i < len(toks):
        if re.match(r'[A-Za-z]', toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in 'Zz':
                if cur:
                    cur.append((sx, sy))
                    closed = True
                end()
                x, y = sx, sy
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            end()
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            sx, sy = x, y
            cur = [(x, y)]
            cmd = 'l' if rel else 'L'  # further pairs are lines
        elif c == 'L':
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            cur.append((x, y))
        elif c == 'H':
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
        elif c == 'V':
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
        elif c == 'A':
            rx, ry, phi, large, sweep, nx, ny = num(), num(), num(), num(), num(), num(), num()
            ex, ey = (x + nx, y + ny) if rel else (nx, ny)
            cur += _arc(x, y, rx, ry, phi, int(large), int(sweep), ex, ey)
            x, y = ex, ey
        elif c in 'CS':
            if c == 'C':
                a = (num(), num())
                a = (x + a[0], y + a[1]) if rel else a
            else:
                a = (2 * x - last_c[0], 2 * y - last_c[1]) if last_c else (x, y)
            b = (num(), num())
            e = (num(), num())
            if rel:
                b, e = (x + b[0], y + b[1]), (x + e[0], y + e[1])
            cur += _cubic((x, y), a, b, e)
            last_c = b
            x, y = e
            continue
        else:
            raise ValueError('unsupported path command ' + cmd)
        last_c = None
    end()
    return subs


# ---- to display-list shapes (what boards_to_art's shape_record takes), in px -----------------------

def _place(pts, at):
    if not at:
        return pts
    k, cx, cy, tx, ty = at
    return [(cx + (px - cx) * k + tx, cy + (py - cy) * k + ty) for px, py in pts]


def shapes(elements):
    """The elements as display-list shapes in panel px (88 x 88). A filled and stroked path
    becomes a fill and a stroke of the same outline; a filled and stroked circle one disc."""
    out = []
    for kind, data, o in elements:
        w = (o['stroke'] or 0) * UNIT
        if kind == 'circle':
            cx, cy, r = data
            out.append({'tag': 'circle', 'circle': (cx * UNIT, cy * UNIT, r * UNIT), 'fill': 'ink' if o['fill'] else 'none',
                        'stroke': 'ink' if o['stroke'] else 'none', 'strokeWidth': w})
            continue
        subs = [([(px * UNIT, py * UNIT) for px, py in _place(pts, o['at'])], closed) for pts, closed in flatten(data)]
        if o['fill']:
            out.append({'tag': 'path', 'fill': 'ink', 'stroke': 'none', 'strokeWidth': 0,
                        'subs': [p for p, _ in subs], 'closed': True})
        if o['stroke']:
            for p, closed in subs:  # each subpath its own polyline; a closed one ends where it began
                out.append({'tag': 'polyline', 'fill': 'none', 'stroke': 'ink', 'strokeWidth': w, 'subs': [p], 'closed': False})
    return out


def svg(elements, colour='#3A1F2B'):
    """The icon as an SVG document, for checking against the panel drawing."""
    parts = []
    for kind, data, o in elements:
        st = 'stroke="%s" stroke-width="%s"' % (colour, o['stroke']) if o['stroke'] else 'stroke="none"'
        fl = 'fill="%s"' % colour if o['fill'] else 'fill="none"'
        tr = ''
        if o['at']:
            k, cx, cy, tx, ty = o['at']
            tr = ' transform="translate(%g %g) translate(%g %g) scale(%g) translate(%g %g)"' % (tx, ty, cx, cy, k, -cx, -cy)
            st += ' vector-effect="non-scaling-stroke"'
        if kind == 'circle':
            parts.append('<circle cx="%g" cy="%g" r="%g" %s %s/>' % (data[0], data[1], data[2], fl, st))
        else:
            parts.append('<path d="%s" %s %s%s/>' % (data, fl, st, tr))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="88" height="88" viewBox="0 0 48 48" '
            'stroke-linecap="round" stroke-linejoin="round">%s</svg>' % ''.join(parts))
