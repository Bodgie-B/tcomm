"""Mockups of the temperature request and comfort chart screens, drawn over real panel screenshots.
Args: project root, output folder. Draws at 3x and scales down so edges and text are smooth."""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
S = 3
JOST = os.path.join(ROOT, 'assets/fonts/src/jost/Jost-ExtraBold-800.ttf')
ATK = os.path.join(ROOT, 'assets/fonts/src/atkinson/AtkinsonHyperlegible-Bold.ttf')
ATKR = os.path.join(ROOT, 'assets/fonts/src/atkinson/AtkinsonHyperlegible-Regular.ttf')
_fc = {}


def font(path, px):
    k = (path, px)
    if k not in _fc:
        _fc[k] = ImageFont.truetype(path, int(px * S))
    return _fc[k]


PLUM = (59, 29, 43)
CREAM = (250, 240, 222)
SHEET = (246, 234, 214)
BEIGE = (230, 214, 184)
GREEN = (27, 107, 58)
GOLD = (245, 166, 35)
SURVEY = [  # label, fill, text colour
    ('MUCH TOO HOT', (139, 30, 30), CREAM),
    ('TOO WARM', (244, 167, 185), PLUM),
    ('JUST RIGHT', (77, 77, 77), CREAM),
    ('TOO COOL', (168, 208, 240), PLUM),
    ('MUCH TOO COLD', (30, 63, 140), CREAM),
]
ARROW = {  # (source, urgent) -> colour, for heat / cool
    ('tenant', False): ((255, 160, 160), (150, 200, 255)),
    ('tenant', True): ((255, 77, 77), (77, 163, 255)),
    ('predicted', False): ((255, 200, 140), (218, 198, 255)),
    ('predicted', True): ((255, 122, 26), (166, 107, 255)),
}


def shade(c, f=0.72):
    return tuple(int(v * f) for v in c)


def sc(box):
    return [v * S for v in box]


class Layer:
    def __init__(self):
        self.im = Image.new('RGBA', (800 * S, 480 * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def rr(self, box, r, fill=None, outline=None, width=0):
        self.d.rounded_rectangle(sc(box), r * S, fill=fill, outline=outline, width=int(width * S))

    def circle(self, cx, cy, r, fill=None, outline=None, width=0):
        self.d.ellipse(sc((cx - r, cy - r, cx + r, cy + r)), fill=fill, outline=outline, width=int(width * S))

    def text(self, xy, s, f, fill, anchor='la'):
        self.d.text((xy[0] * S, xy[1] * S), s, font=f, fill=fill, anchor=anchor)

    def width(self, s, f):
        return self.d.textlength(s, font=f) / S

    def button(self, box, fill, label, fcol, size=22, sub=None, selected=False, sel_col=PLUM):
        x0, y0, x1, y1 = box
        self.rr((x0, y0 + 5, x1, y1 + 5), 14, fill=shade(fill))
        self.rr(box, 14, fill=fill)
        if selected:
            self.rr((x0 - 3, y0 - 3, x1 + 3, y1 + 8), 16, outline=sel_col, width=4)
        cy = (y0 + y1) / 2
        if sub:
            self.text(((x0 + x1) / 2, cy - 9), label, font(JOST, size), fcol, 'mm')
            self.text(((x0 + x1) / 2, cy + 17), sub, font(ATK, 14), fcol, 'mm')
        else:
            self.text(((x0 + x1) / 2, cy + 1), label, font(JOST, size), fcol, 'mm')

    def arrow(self, cx, cy, size, up, double, col, alpha=255, gap_col=None):
        col = col + (alpha,)
        s = size
        def tf(pts):
            return [(cx * S + x * s * S, cy * S + (y if up else -y) * s * S) for x, y in pts]
        shaft_top = -0.02 if not double else 0.3
        self.d.rounded_rectangle(
            [min(p[0] for p in tf([(-0.13, shaft_top), (0.13, 0.5)])), min(p[1] for p in tf([(-0.13, shaft_top), (0.13, 0.5)])),
             max(p[0] for p in tf([(-0.13, shaft_top), (0.13, 0.5)])), max(p[1] for p in tf([(-0.13, shaft_top), (0.13, 0.5)]))],
            int(0.06 * s * S), fill=col)
        heads = [-0.5] if not double else [-0.1, -0.56]
        hw, hh = (0.44, 0.44) if not double else (0.5, 0.42)
        for top in heads:
            pts = [(0, top), (hw, top + hh), (-hw, top + hh)]
            if double and gap_col is not None and top == heads[-1]:
                g = 0.1
                self.d.polygon(tf([(0, top - g), (hw + g * 1.3, top + hh + g * 0.7), (-hw - g * 1.3, top + hh + g * 0.7)]), fill=gap_col + (255,))
            self.d.polygon(tf(pts), fill=col)

    def badge(self, cx, cy, r, up, urgent, source='tenant', alpha=255, style=None, faint=False):
        style = style or ('dark' if source == 'tenant' else 'filled')
        heat, cool = ARROW[(source, urgent)]
        col = heat if up else cool
        if style == 'dark':
            bg = PLUM
            if faint:
                self.circle(cx, cy, r, outline=CREAM + (150,), width=2)
                self.arrow(cx, cy + (1 if up else -1), r * 1.15, up, urgent, CREAM, alpha=110)
                return
            self.circle(cx, cy, r, fill=bg + (255,))
            self.arrow(cx, cy + (1 if up else -1), r * (1.3 if urgent else 1.15), up, urgent, col, alpha=alpha, gap_col=bg)
        else:  # filled: the colour fills the circle
            fill = PLUM if (source, urgent) == ('tenant', False) else col
            light = (source, urgent) == ('predicted', False)
            acol = PLUM if light else (255, 250, 240)
            self.circle(cx, cy, r, fill=fill + (alpha,))
            self.arrow(cx, cy + (1 if up else -1), r * (1.3 if urgent else 1.15), up, urgent, acol, alpha=alpha,
                       gap_col=fill if alpha == 255 else None)


def base(name, dim=0.0):
    im = Image.open(os.path.join(ROOT, 'docs/screens', name + '.png')).convert('RGBA')
    if dim:
        im = Image.alpha_composite(im, Image.new('RGBA', im.size, (20, 12, 18, int(255 * dim))))
    return im


def finish(im, layer, name):
    ov = layer.im.resize((800, 480), Image.LANCZOS)
    Image.alpha_composite(im, ov).convert('RGB').save(os.path.join(OUT, name + '.png'))
    print('wrote', name)


BADGE = (373.5, 98)  # the mail badge's spot, moved up to the UPSTAIRS card
STEP = 38


def survey_panel(L, picked=None, reading='67°F   47%'):
    L.rr((-30, 0, 262, 480), 22, fill=SHEET)
    L.text((18, 14), 'HOW DOES IT FEEL?', font(JOST, 21), PLUM)
    L.text((18, 44), 'Downstairs now  ' + reading, font(ATK, 14), shade(PLUM, 1.6))
    for i, (lab, fill, fcol) in enumerate(SURVEY):
        y = 74 + i * 78
        L.button((16, y, 246, y + 66), fill, lab, fcol, size=22, selected=(picked == i))


REQ = [  # label, sub, up, urgent
    ('WARMER', 'urgent', True, True),
    ('WARMER', 'mild', True, False),
    ('COOLER', 'mild', False, False),
    ('COOLER', 'urgent', False, True),
]


def request_panel(L, chosen):
    L.rr((538, 0, 830, 480), 22, fill=SHEET)
    L.text((556, 14), 'ASK UPSTAIRS', font(JOST, 21), PLUM)
    for i, (lab, sub, up, urg) in enumerate(REQ):
        y = 74 + i * 98
        sel = i == chosen
        fill = PLUM if sel else BEIGE
        tcol = CREAM if sel else PLUM
        L.rr((554, y + 5, 784, y + 86 + 5), 14, fill=shade(fill))
        L.rr((554, y, 784, y + 86), 14, fill=fill)
        if sel:
            L.rr((551, y - 3, 787, y + 94), 16, outline=GOLD, width=4)
        if sel:
            L.circle(669, y + 43, 34, fill=CREAM + (255,))
        L.badge(669, y + 43, 31, up, urg)


def middle(L, chosen):
    lab, sub, up, urg = REQ[chosen]
    L.rr((280, 92, 520, 398), 22, fill=SHEET)
    L.text((400, 116), 'SEND TO UPSTAIRS?', font(JOST, 18), PLUM, 'mm')
    L.badge(400, 196, 50, up, urg)
    L.button((296, 270, 504, 324), GREEN, 'SEND', CREAM, size=24)
    L.button((296, 338, 504, 386), BEIGE, 'CANCEL', PLUM, size=22)


# 01 survey open
im = base('04_p4_midday', dim=0.45)
L = Layer(); survey_panel(L); finish(im, L, '01_survey_open')

# 02 too warm -> mild cooling picked
im = base('04_p4_midday', dim=0.45)
L = Layer(); survey_panel(L, picked=1); request_panel(L, 2); middle(L, 2); finish(im, L, '02_too_warm_picks_mild_cooling')

# 03 much too cold -> urgent heat picked
im = base('04_p4_midday', dim=0.45)
L = Layer(); survey_panel(L, picked=4); request_panel(L, 0); middle(L, 0); finish(im, L, '03_much_too_cold_picks_urgent_heat')

# 04 downstairs after sending
im = base('04_p4_midday')
L = Layer(); L.badge(*BADGE, 17, False, False); finish(im, L, '04_downstairs_after_sending')


# 06 downstairs, action taken
im = base('04_p4_midday')
px_ = im.load()
x0, y0, x1, y1 = 30, 222, 230, 258
lum = lambda c: 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
mask = set()
for y in range(y0, y1):
    for x in range(x0, x1):
        if lum(px_[x, y]) > 95:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    mask.add((x + dx, y + dy))
for y in range(y0 - 1, y1 + 1):
    row = [x for x in range(x0 - 1, x1 + 1) if (x, y) in mask]
    for x in row:
        l = x - 1
        while (l, y) in mask: l -= 1
        r = x + 1
        while (r, y) in mask: r += 1
        a, b = px_[l, y], px_[r, y]
        f = (x - l) / (r - l)
        px_[x, y] = tuple(int(a[i] + (b[i] - a[i]) * f) for i in range(3)) + (255,)
L = Layer()
msg = 'Action taken on temperature'
size = 20
while L.width(msg, font(ATK, size)) > 250 and size > 14:
    size -= 0.5
print('action taken text size', size)
L.text((36, 239), msg, font(ATK, size), (251, 241, 220), 'lm')
finish(im, L, '06_downstairs_action_taken')

# 07 upstairs, mild request for heat
im = base('35_mail_outline_upstairs')
L = Layer(); L.badge(*BADGE, 17, True, False); finish(im, L, '07_upstairs_mild_heat')

# 08 upstairs, urgent heat (flashing, shown lit)
im = base('35_mail_outline_upstairs')
L = Layer(); L.badge(*BADGE, 17, True, True); finish(im, L, '08_upstairs_urgent_heat')

# 09 upstairs, three requests, newest on the right
im = base('35_mail_outline_upstairs')
L = Layer()
L.badge(BADGE[0] - 2 * STEP, BADGE[1], 17, True, False)
L.badge(BADGE[0] - STEP, BADGE[1], 17, True, False)
L.badge(BADGE[0], BADGE[1], 17, True, True)
finish(im, L, '09_upstairs_three_requests')

# 10 upstairs at night, urgent cooling lights the screen
im = base('39_mail_outline_upstairs_night')
L = Layer(); L.badge(*BADGE, 17, False, True); finish(im, L, '10_upstairs_night_urgent_cooling')

# 11 the arrow key: every arrow, the flash, and a filled-circle alternative
im = Image.new('RGBA', (800, 480), SHEET + (255,))
L = Layer()
L.text((24, 18), 'THE ARROWS', font(JOST, 24), PLUM)
L.text((24, 50), "The tenant's arrows sit in a dark circle; the prediction's fill the circle with their colour.",
       font(ATKR, 14), PLUM)
cols = [('WARMER', 'mild', True, False), ('WARMER', 'urgent', True, True), ('COOLER', 'mild', False, False),
        ('COOLER', 'urgent', False, True)]
xs = [300, 390, 480, 570]
for x, (lab, sub, up, urg) in zip(xs, cols):
    L.text((x, 92), lab, font(JOST, 13), PLUM, 'mm')
    L.text((x, 108), sub, font(ATK, 12), PLUM, 'mm')
L.text((670, 100), 'urgent, dim half', font(ATK, 12), PLUM, 'mm')
rows = [('Tenant', 'tenant', None, 180), ('Prediction (later)', 'predicted', None, 290)]
L.rr((16, 126, 784, 344), 14, fill=(255, 255, 255, 90))
for name, src, style, y in rows:
    L.text((30, y), name, font(JOST, 17), PLUM, 'lm')
    for x, (lab, sub, up, urg) in zip(xs, cols):
        L.badge(x, y, 26, up, urg, source=src, style=style)
    L.badge(640, y, 26, True, True, source=src, style=style, alpha=90)
    L.badge(700, y, 26, False, True, source=src, style=style, alpha=90)
L.text((24, 400), 'On the panel each arrow is about the size of the mail badge (34 px). Urgent ones flash steadily,',
       font(ATKR, 14), PLUM)
L.text((24, 420), 'lit for half a second and dim for half a second. Up to three sit side by side, newest on the right.',
       font(ATKR, 14), PLUM)
finish(im, L, '11_arrow_key')


# ---- the comfort chart ----
def gr(F, rh):
    c = (F - 32) * 5 / 9
    ps = 1000 * math.exp(16.6536 - 4030.183 / (c + 235))
    pw = rh * ps
    return 0.62198 * pw / (101325 - pw) * 7000


PARTS = ['NIGHT', 'MORNING', 'MIDDAY', 'EVENING']
CHIPS = ['ALL'] + PARTS
random.seed(11)
answers = []
neutral = {'NIGHT': 68.8, 'MORNING': 72.2, 'MIDDAY': 71.0, 'EVENING': 70.4}
weights = [('NIGHT', 16), ('MORNING', 26), ('MIDDAY', 20), ('EVENING', 30)]
day = 0
for part, n in weights:
    for _ in range(n):
        t = random.uniform(62.5, 79)
        rh = random.uniform(0.33, 0.62)
        g = gr(t, rh)
        feel = t + 0.045 * (g - 55) - neutral[part] + random.uniform(-1.3, 1.3)
        v = 4 if feel < -5 else 3 if feel < -1.7 else 2 if feel < 1.7 else 1 if feel < 5 else 0
        answers.append((part, t, rh, g, v))

X0, X1, Y1 = 55, 85, 120
PL, PR, PT, PB = 64, 712, 84, 404


def px(t):
    return PL + (t - X0) / (X1 - X0) * (PR - PL)


def py(g):
    return PB - g / Y1 * (PB - PT)


def chart(name, show=('NIGHT', 'MORNING', 'MIDDAY', 'EVENING'), pick=None, panel='downstairs'):
    im = base('04_p4_midday' if panel == 'downstairs' else '35_mail_outline_upstairs', dim=0.45)
    L = Layer()
    L.rr((8, 8, 792, 472), 24, fill=SHEET + (255,))
    L.text((28, 18), 'DOWNSTAIRS COMFORT', font(JOST, 20), PLUM)
    x = 28 + L.width('DOWNSTAIRS COMFORT', font(JOST, 20)) + 20
    chosen = 'ALL' if len(show) == 4 else show[0]
    for p in CHIPS:
        w = L.width(p, font(JOST, 14)) + 24
        sel = p == chosen
        L.rr((x, 22, x + w, 52), 15, fill=PLUM if sel else SHEET, outline=PLUM, width=2)
        if sel:
            L.rr((x - 4, 18, x + w + 4, 56), 19, outline=PLUM, width=2.5)
        L.text((x + w / 2, 37), p, font(JOST, 14), CREAM if sel else PLUM, 'mm')
        x += w + 10
    print(name, 'chips end at', round(x))
    L.circle(756, 37, 19, outline=PLUM, width=2.5)
    L.d.line(sc((749, 30, 763, 44)), fill=PLUM, width=int(2.5 * S))
    L.d.line(sc((763, 30, 749, 44)), fill=PLUM, width=int(2.5 * S))
    grid = (228, 216, 196)
    for t in range(55, 86, 5):
        L.d.line(sc((px(t), PT, px(t), PB)), fill=grid, width=S)
        L.text((px(t), PB + 16), f'{t}°F', font(ATK, 13), shade(PLUM, 1.7), 'mm')
    for g in range(0, 121, 20):
        L.d.line(sc((PL, py(g), PR, py(g))), fill=grid, width=S)
        L.text((PR + 8, py(g)), str(g), font(ATK, 13), shade(PLUM, 1.7), 'lm')
    L.text((PR + 8, PT - 14), 'gr/lb', font(ATK, 12), shade(PLUM, 1.7), 'lm')
    zones = [('WINTER CLOTHING', [70.9, 69.7, 68.4, 67.2], [79.0, 77.7, 76.5, 75.3], 67.0),
             ('SUMMER CLOTHING', [77.1, 76.2, 75.3, 74.4], [82.9, 82.0, 81.1, 80.2], 77.6)]
    gs = [0, 28, 56, 84]
    zl = Layer.__new__(Layer)
    zl.im = Image.new('RGBA', L.im.size, (0, 0, 0, 0)); zl.d = ImageDraw.Draw(zl.im)
    for zname, left, right, lx in zones:
        pts = [(px(left[i]) * S, py(gs[i]) * S) for i in range(4)] + [(px(right[i]) * S, py(gs[i]) * S) for i in range(3, -1, -1)]
        zl.d.polygon(pts, fill=(200, 196, 188, 110))
        zl.d.line(pts + [pts[0]], fill=(176, 172, 164, 255), width=2 * S)
    L.im = Image.alpha_composite(L.im, zl.im); L.d = ImageDraw.Draw(L.im)
    L.text((px(67.4), py(92)), 'WINTER CLOTHING', font(JOST, 11), (150, 146, 138), 'lm')
    L.text((px(76.6), py(100)), 'SUMMER CLOTHING', font(JOST, 11), (150, 146, 138), 'lm')
    for rh in (0.2, 0.4, 0.6, 0.8, 1.0):
        pts, last = [], None
        t = X0
        while t <= X1:
            g = gr(t, rh)
            if g > Y1:
                break
            pts.append((px(t) * S, py(g) * S)); last = (t, g)
            t += 0.25
        L.d.line(pts, fill=(206, 196, 178), width=int(1.3 * S))
        if last:
            if last[1] > Y1 - 6:
                L.text((px(last[0]) - 6, PT + 10), f'{int(rh * 100)}%', font(ATK, 12), (160, 150, 132), 'rm')
            else:
                tl = X1 - 1.2
                L.text((px(tl), py(gr(tl, rh)) - 10), f'{int(rh * 100)}%', font(ATK, 12), (160, 150, 132), 'mm')
    counts = [0] * 5
    for part, t, rh, g, v in answers:
        if part not in show:
            continue
        counts[v] += 1
        L.circle(px(t), py(g), 3.6, fill=SURVEY[v][1], outline=(43, 30, 36), width=1)
    nt, ng = 67.0, gr(67.0, 0.47)
    L.circle(px(nt), py(ng), 9, outline=PLUM, width=2.5)
    nw = L.width('NOW 67°F  47%', font(JOST, 13))
    L.d.line(sc((px(nt) - 7, py(ng) - 7, px(nt) - 18, py(ng) - 24)), fill=PLUM, width=2 * S)
    L.rr((px(nt) - 26 - nw, py(ng) - 36, px(nt) - 14, py(ng) - 14), 11, fill=PLUM)
    L.text((px(nt) - 20 - nw / 2 - 0, py(ng) - 25), 'NOW 67°F  47%', font(JOST, 13), CREAM, 'mm')
    x = 28
    for i in (0, 1, 2, 3, 4):
        lab = SURVEY[i][0].capitalize().replace('too', 'too')
        L.circle(x + 6, 452, 6, fill=SURVEY[i][1], outline=(43, 30, 36), width=1)
        s = f'{lab}  {counts[i]}'
        L.text((x + 17, 452), s, font(ATK, 14), PLUM, 'lm')
        x += 17 + L.width(s, font(ATK, 14)) + 22
    L.text((28, 52), f'{sum(counts)} answers since Oct 20', font(ATKR, 13), shade(PLUM, 1.7), 'lm')
    if pick is not None:
        part, t, rh, g, v = pick
        cx, cy = px(t), py(g)
        L.circle(cx, cy, 10, outline=GOLD, width=3)
        bx, by = cx + 22, cy - 120
        L.rr((bx, by + 5, bx + 250, by + 130), 16, fill=shade(BEIGE))
        L.rr((bx, by, bx + 250, by + 125), 16, fill=(255, 250, 240), outline=PLUM, width=2)
        L.rr((bx + 14, by + 14, bx + 26 + L.width(SURVEY[v][0], font(JOST, 14)), by + 40), 12, fill=SURVEY[v][1])
        L.text((bx + 20, by + 27), SURVEY[v][0], font(JOST, 14), SURVEY[v][2], 'lm')
        when = {'NIGHT': '11:40 PM', 'MORNING': '7:42 AM', 'MIDDAY': '12:15 PM', 'EVENING': '7:05 PM'}[part]
        L.text((bx + 14, by + 58), f'Tue, Nov 3   {when}   {part}', font(ATK, 14), PLUM, 'lm')
        L.text((bx + 14, by + 80), f'{t:.1f}°F   {int(rh * 100)}%', font(ATK, 14), PLUM, 'lm')
        L.button((bx + 14, by + 92, bx + 236, by + 116), (196, 69, 43), 'REMOVE', CREAM, size=15)
    finish(im, L, name)


chart('12_chart_all_answers')
chart('13_chart_mornings_only', show=('MORNING',))
pk = min((a for a in answers if a[4] == 3), key=lambda a: (a[0] != 'MORNING', abs(a[1] - 67)))
chart('14_chart_remove_a_mistake', pick=pk)
