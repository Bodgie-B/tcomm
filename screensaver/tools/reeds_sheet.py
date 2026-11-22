# reeds_sheet.py: stage 22 reeds stills (reeds_stills.cpp's PPMs and manifest.txt) to PNG sheets, with the
# standard library alone (zlib for the PNG, a small built-in 5x7 font for the labels): no Pillow needed.
#
#   python -I screensaver/tools/reeds_sheet.py PPM_DIR OUT_DIR
#
# The manifest's lines are file|label|x0|y0|x1|y1|scale: the crop of the 800 x 480 frame and how much to
# enlarge it (each panel pixel becomes a scale x scale block, no smoothing). Full frames (scale 1) are shown
# whole, halved with a box filter so a row of them fits. One sheet per letter the files start with:
#   sheet_S_scene.png, sheet_C_clumps.png, sheet_M_seasons.png, sheet_W_wind.png, sheet_F_gust_strip.png,
#   sheet_G_calm_strip.png, sheet_B_before_after.png, sheet_N_neighbours.png; step C1: sheet_P_pick.png,
#   sheet_T_stem.png, sheet_L_life.png, sheet_K_chip.png
import os
import struct
import sys
import zlib

BG = (34, 34, 34)
BAR = (52, 52, 52)
TXT = (235, 235, 235)

# 5x7 glyphs, one string of 7 rows of 5 bits each; lower case is drawn as upper case.
FONT = {
    "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110",
    "C": "01110 10001 10000 10000 10000 10001 01110", "D": "11110 10001 10001 10001 10001 10001 11110",
    "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001",
    "I": "01110 00100 00100 00100 00100 00100 01110", "J": "00111 00010 00010 00010 00010 10010 01100",
    "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 10001 11001 10101 10011 10001 10001",
    "O": "01110 10001 10001 10001 10001 10001 01110", "P": "11110 10001 10001 11110 10000 10000 10000",
    "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100",
    "U": "10001 10001 10001 10001 10001 10001 01110", "V": "10001 10001 10001 10001 10001 01010 00100",
    "W": "10001 10001 10001 10101 10101 10101 01010", "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
    "0": "01110 10001 10011 10101 11001 10001 01110", "1": "00100 01100 00100 00100 00100 00100 01110",
    "2": "01110 10001 00001 00010 00100 01000 11111", "3": "11111 00010 00100 00010 00001 10001 01110",
    "4": "00010 00110 01010 10010 11111 00010 00010", "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110", "7": "11111 00001 00010 00100 01000 01000 01000",
    "8": "01110 10001 10001 01110 10001 10001 01110", "9": "01110 10001 10001 01111 00001 00010 01100",
    " ": "00000 00000 00000 00000 00000 00000 00000", ".": "00000 00000 00000 00000 00000 01100 01100",
    ",": "00000 00000 00000 00000 01100 00100 01000", ":": "00000 01100 01100 00000 01100 01100 00000",
    "(": "00010 00100 01000 01000 01000 00100 00010", ")": "01000 00100 00010 00010 00010 00100 01000",
    "-": "00000 00000 00000 11111 00000 00000 00000", "+": "00000 00100 00100 11111 00100 00100 00000",
    "'": "01100 00100 01000 00000 00000 00000 00000", "/": "00001 00010 00010 00100 01000 01000 10000",
    "%": "11001 11010 00010 00100 01000 01011 10011", "_": "00000 00000 00000 00000 00000 00000 11111",
    "?": "01110 10001 00001 00010 00100 00000 00100",
}


class Image:
    def __init__(self, w, h, fill=BG):
        self.w, self.h = w, h
        self.px = bytearray(bytes(fill) * (w * h))

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = 3 * (y * self.w + x)
            self.px[i:i + 3] = bytes(c)

    def rect(self, x0, y0, x1, y1, c):
        row = bytes(c) * max(0, min(x1, self.w) - max(x0, 0))
        for y in range(max(y0, 0), min(y1, self.h)):
            i = 3 * (y * self.w + max(x0, 0))
            self.px[i:i + len(row)] = row

    def paste(self, im, x0, y0):
        for y in range(im.h):
            if 0 <= y0 + y < self.h:
                i = 3 * ((y0 + y) * self.w + x0)
                self.px[i:i + 3 * im.w] = im.px[3 * y * im.w:3 * (y + 1) * im.w]

    def text(self, x, y, s, c=TXT, k=2):
        for ch in s.upper():
            g = FONT.get(ch, FONT["?"]).split()
            for r, bits in enumerate(g):
                for b, v in enumerate(bits):
                    if v == "1":
                        for dy in range(k):
                            for dx in range(k):
                                self.put(x + b * k + dx, y + r * k + dy, c)
            x += 6 * k

    def save_png(self, path):
        raw = bytearray()
        stride = 3 * self.w
        for y in range(self.h):
            raw.append(0)
            raw += self.px[y * stride:(y + 1) * stride]

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
        with open(path, "wb") as f:
            f.write(png)


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = []
    i = 0
    while len(parts) < 4:   # magic, width, height, maxval
        while data[i:i + 1].isspace():
            i += 1
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1
    w, h = int(parts[1]), int(parts[2])
    im = Image(w, h)
    im.px = bytearray(data[i:i + 3 * w * h])
    return im


def crop_scale(im, x0, y0, x1, y1, k):
    w, h = x1 - x0, y1 - y0
    out = Image(w * k, h * k)
    for y in range(h):
        src = im.px[3 * ((y0 + y) * im.w + x0):3 * ((y0 + y) * im.w + x1)]
        row = bytearray()
        for x in range(w):
            row += bytes(src[3 * x:3 * x + 3]) * k
        for dy in range(k):
            i = 3 * ((y * k + dy) * out.w)
            out.px[i:i + len(row)] = row
    return out


def half(im):
    out = Image(im.w // 2, im.h // 2)
    p = im.px
    for y in range(out.h):
        for x in range(out.w):
            acc = [0, 0, 0]
            for dy in (0, 1):
                i = 3 * ((2 * y + dy) * im.w + 2 * x)
                for c in range(3):
                    acc[c] += p[i + c] + p[i + 3 + c]
            j = 3 * (y * out.w + x)
            out.px[j:j + 3] = bytes(v // 4 for v in acc)
    return out


def load(src):
    rows = []
    with open(os.path.join(src, "manifest.txt")) as f:
        for line in f:
            line = line.strip()
            if line:
                name, label, x0, y0, x1, y1, k = line.split("|")
                rows.append((name, label, int(x0), int(y0), int(x1), int(y1), int(k)))
    return rows


def tile(src, row):
    name, label, x0, y0, x1, y1, k = row
    im = read_ppm(os.path.join(src, name))
    if k <= 1 and (x1 - x0) >= 800:
        return label, half(im)
    return label, crop_scale(im, x0, y0, x1, y1, k)


def sheet(cells, out, cols):
    if not cells:
        return
    gap, bar = 8, 22
    tw = max(c[1].w for c in cells)
    th = max(c[1].h for c in cells)
    nrows = (len(cells) + cols - 1) // cols
    img = Image(cols * (tw + gap) + gap, nrows * (th + bar + gap) + gap)
    for i, (label, im) in enumerate(cells):
        x = gap + (i % cols) * (tw + gap)
        y = gap + (i // cols) * (th + bar + gap)
        img.rect(x, y, x + tw, y + bar, BAR)
        img.text(x + 5, y + 4, ("%d. %s" % (i + 1, label))[: max(8, (tw - 10) // 12)])
        img.paste(im, x, y + bar)
    img.save_png(out)
    print("wrote", out, "(%d tiles)" % len(cells))


SHEETS = [("S", "scene", 3), ("C", "clumps", 4), ("M", "seasons", 3), ("W", "wind", 4), ("F", "gust_strip", 4),
          ("G", "calm_strip", 4), ("B", "before_after", 2), ("N", "neighbours", 3), ("P", "pick", 4), ("T", "stem", 4),
          ("L", "life", 3), ("K", "chip", 1)]


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = load(src)
    for tag, name, cols in SHEETS:
        mine = [r for r in rows if r[0].startswith(tag + "_")]
        if not mine:
            continue
        sheet([tile(src, r) for r in mine], os.path.join(out, "sheet_%s_%s.png" % (tag, name)), cols)


if __name__ == "__main__":
    main()
