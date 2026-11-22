# longshot_sheet.py: stage 22 step B5 stills (longshot_stills.cpp's PPMs and manifest.txt) to PNG sheets for
# Mike's review. Standard library only (zlib writes the PNGs; a small built-in pixel font writes the labels).
#
#   python -I screensaver/tools/longshot_sheet.py PPM_DIR OUT_DIR
#
# The manifest's lines are file|label|x0|y0|x1|y1|scale|sheet|row: each is a crop of a frame, enlarged by
# `scale` (whole pixels, no smoothing, so what is seen is what the panel's pixels are), placed on sheet
# `sheet` in row `row`, left to right. Writes OUT_DIR/<sheet>.png for every sheet named.
import os
import struct
import sys
import zlib

BG = (34, 34, 34)
BAR = (52, 52, 52)
TXT = (235, 235, 235)
GAP = 8
FONT_SCALE = 2

# 5 x 7 pixel font: each glyph is seven rows of five bits, top row first.
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
    "2": "01110 10001 00001 00010 00100 01000 11111", "3": "11110 00001 00001 01110 00001 00001 11110",
    "4": "00010 00110 01010 10010 11111 00010 00010", "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110", "7": "11111 00001 00010 00100 01000 01000 01000",
    "8": "01110 10001 10001 01110 10001 10001 01110", "9": "01110 10001 10001 01111 00001 00010 01100",
    " ": "00000 00000 00000 00000 00000 00000 00000", ".": "00000 00000 00000 00000 00000 01100 01100",
    ",": "00000 00000 00000 00000 01100 00100 01000", ":": "00000 01100 01100 00000 01100 01100 00000",
    "(": "00010 00100 01000 01000 01000 00100 00010", ")": "01000 00100 00010 00010 00010 00100 01000",
    "-": "00000 00000 00000 11111 00000 00000 00000", "/": "00001 00010 00010 00100 01000 01000 10000",
    "'": "00100 00100 01000 00000 00000 00000 00000", "%": "11001 11010 00010 00100 01000 01011 10011",
    "_": "00000 00000 00000 00000 00000 00000 11111", "+": "00000 00100 00100 11111 00100 00100 00000",
    "?": "01110 10001 00001 00010 00100 00000 00100",
}


class Image:
    def __init__(self, w, h, fill=(0, 0, 0)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(fill) * (w * h))

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.px[i:i + 3] = bytes(c)

    def rect(self, x0, y0, x1, y1, c):
        row = bytes(c) * (x1 - x0)
        for y in range(max(0, y0), min(self.h, y1)):
            i = (y * self.w + x0) * 3
            self.px[i:i + len(row)] = row

    def paste(self, other, x0, y0):
        for y in range(other.h):
            if 0 <= y0 + y < self.h:
                s = y * other.w * 3
                d = ((y0 + y) * self.w + x0) * 3
                self.px[d:d + other.w * 3] = other.px[s:s + other.w * 3]

    def text(self, x, y, s, c, k=FONT_SCALE):
        for ch in s.upper():
            g = FONT.get(ch, FONT["?"]).split()
            for r, bits in enumerate(g):
                for b, bit in enumerate(bits):
                    if bit == "1":
                        self.rect(x + b * k, y + r * k, x + b * k + k, y + r * k + k, c)
            x += 6 * k

    def save_png(self, path):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            raw += self.px[y * self.w * 3:(y + 1) * self.w * 3]

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
            f.write(chunk(b"IEND", b""))


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = []
    i = 0
    while len(parts) < 4:   # P6, width, height, maxval (no comments in our files)
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
    im.px = bytearray(data[i:i + w * h * 3])
    return im


def crop_scale(im, x0, y0, x1, y1, k):
    out = Image((x1 - x0) * k, (y1 - y0) * k)
    for y in range(y1 - y0):
        src = im.px[((y0 + y) * im.w + x0) * 3:((y0 + y) * im.w + x1) * 3]
        row = bytearray()
        for x in range(x1 - x0):
            row += bytes(src[x * 3:x * 3 + 3]) * k
        for r in range(k):
            d = ((y * k + r) * out.w) * 3
            out.px[d:d + len(row)] = row
    return out


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    sheets = {}
    cache = {}
    with open(os.path.join(src, "manifest.txt")) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            name, label, x0, y0, x1, y1, sc, sheet, row = line.split("|")
            if name not in cache:
                cache[name] = read_ppm(os.path.join(src, name))
            tile = crop_scale(cache[name], int(x0), int(y0), int(x1), int(y1), int(sc))
            sheets.setdefault(sheet, {}).setdefault(int(row), []).append((label, tile))
    bar = 7 * FONT_SCALE + 10
    for sheet, rows in sheets.items():
        n = 0
        layout = []
        width = 0
        y = GAP
        for r in sorted(rows):
            x = GAP
            th = 0
            for label, tile in rows[r]:
                n += 1
                tw = max(tile.w, (len(label) + 4) * 6 * FONT_SCALE if tile.w < 200 else tile.w)
                layout.append((x, y, "%d. %s" % (n, label), tile, tw))
                x += tw + GAP
                th = max(th, tile.h)
            width = max(width, x)
            y += th + bar + GAP
        img = Image(width, y, BG)
        for x, y0, label, tile, tw in layout:
            img.rect(x, y0, x + tw, y0 + bar, BAR)
            img.text(x + 5, y0 + 5, label[: max(4, tw // (6 * FONT_SCALE) - 1)], TXT)
            img.paste(tile, x, y0 + bar)
        img.save_png(os.path.join(out, sheet + ".png"))
        print("wrote", os.path.join(out, sheet + ".png"), img.w, "x", img.h)


if __name__ == "__main__":
    main()
