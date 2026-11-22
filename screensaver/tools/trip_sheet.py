# trip_sheet.py: stage 22 mushroom stills (trip_stills.cpp's PPMs and manifest.txt) to PNG review sheets.
# zlib only: no Pillow, no ffmpeg. Labels are drawn with a small built-in 5 x 7 font (capitals).
#
#   python -I screensaver/tools/trip_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/
#   patch_left.png, patch_middle.png, patch_right.png   the patch at stages 1 to 7 and drooping, per mother place
#   patch_conditions.png                                 dusk, night, rain, the chip and dogs, the build-up pop
#   patch_frame.png                                      one whole frame with a ripe patch
#   sky_moments.png                                      the sky alone at ten moments of the 104 s
#   sky_strip_04s.png                                    ten frames 0.4 s apart at t 50 (crop, 3x)
#   sky_strip_ticks.png                                  ten consecutive ticks at t 50 (crop, 3x)
#   sky_variants.png                                     cover 3, cover 6, mood 2.6 and 3.4, the sun low, night
import os
import struct
import sys
import zlib

BG = (34, 34, 34)
BAR = (52, 52, 52)
TXT = (235, 235, 235)

# 5 x 7 glyphs, one string of 7 rows of 5 bits each ("#" on).
FONT = {
    " ": ".....|.....|.....|.....|.....|.....|.....",
    "0": ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    "1": "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
    "2": ".###.|#...#|....#|...#.|..#..|.#...|#####",
    "3": "####.|....#|....#|.###.|....#|....#|####.",
    "4": "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    "5": "#####|#....|####.|....#|....#|#...#|.###.",
    "6": "..##.|.#...|#....|####.|#...#|#...#|.###.",
    "7": "#####|....#|...#.|..#..|.#...|.#...|.#...",
    "8": ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    "9": ".###.|#...#|#...#|.####|....#|...#.|.##..",
    "A": ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    "B": "####.|#...#|#...#|####.|#...#|#...#|####.",
    "C": ".###.|#...#|#....|#....|#....|#...#|.###.",
    "D": "###..|#..#.|#...#|#...#|#...#|#..#.|###..",
    "E": "#####|#....|#....|####.|#....|#....|#####",
    "F": "#####|#....|#....|####.|#....|#....|#....",
    "G": ".###.|#...#|#....|#.###|#...#|#...#|.####",
    "H": "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    "I": ".###.|..#..|..#..|..#..|..#..|..#..|.###.",
    "J": "..###|...#.|...#.|...#.|...#.|#..#.|.##..",
    "K": "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    "L": "#....|#....|#....|#....|#....|#....|#####",
    "M": "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
    "N": "#...#|#...#|##..#|#.#.#|#..##|#...#|#...#",
    "O": ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    "P": "####.|#...#|#...#|####.|#....|#....|#....",
    "Q": ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
    "R": "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    "S": ".####|#....|#....|.###.|....#|....#|####.",
    "T": "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    "U": "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
    "V": "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
    "W": "#...#|#...#|#...#|#.#.#|#.#.#|#.#.#|.#.#.",
    "X": "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
    "Y": "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    "Z": "#####|....#|...#.|..#..|.#...|#....|#####",
    ".": ".....|.....|.....|.....|.....|.##..|.##..",
    ",": ".....|.....|.....|.....|.##..|..#..|.#...",
    ":": ".....|.##..|.##..|.....|.##..|.##..|.....",
    ";": ".....|.##..|.##..|.....|.##..|..#..|.#...",
    "-": ".....|.....|.....|.###.|.....|.....|.....",
    "(": "...#.|..#..|.#...|.#...|.#...|..#..|...#.",
    ")": ".#...|..#..|...#.|...#.|...#.|..#..|.#...",
    "%": "##...|##..#|...#.|..#..|.#...|#..##|...##",
    "/": ".....|....#|...#.|..#..|.#...|#....|.....",
    "'": "..#..|..#..|.#...|.....|.....|.....|.....",
    "?": ".###.|#...#|....#|...#.|..#..|.....|..#..",
}


def read_ppm(path):
    d = open(path, "rb").read()
    tok, pos = [], 0
    while len(tok) < 4:
        while d[pos:pos + 1].isspace():
            pos += 1
        s = pos
        while not d[pos:pos + 1].isspace():
            pos += 1
        tok.append(d[s:pos])
    w, h = int(tok[1]), int(tok[2])
    px = d[pos + 1:pos + 1 + w * h * 3]
    if tok[0] != b"P6" or tok[3] != b"255" or len(px) != w * h * 3:
        sys.exit(path + ": not a whole P6 255 PPM")
    return w, h, px


class Img:
    def __init__(self, w, h, col):
        self.w, self.h = w, h
        self.rows = [bytearray(bytes(col) * w) for _ in range(h)]

    def rect(self, x0, y0, x1, y1, col):
        line = bytes(col) * max(0, x1 - x0)
        for y in range(max(0, y0), min(self.h, y1)):
            self.rows[y][x0 * 3:x1 * 3] = line

    def paste(self, tile, x, y):
        for j, r in enumerate(tile):
            if 0 <= y + j < self.h:
                self.rows[y + j][x * 3:x * 3 + len(r)] = r

    def text(self, x, y, s, col, size=2):
        for ch in s.upper():
            g = FONT.get(ch, FONT["?"]).split("|")
            for j, row in enumerate(g):
                for i, c in enumerate(row):
                    if c == "#":
                        self.rect(x + i * size, y + j * size, x + (i + 1) * size, y + (j + 1) * size, col)
            x += 6 * size

    def save(self, path):
        def chunk(tag, body):
            return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
        raw = b"".join(b"\x00" + bytes(r) for r in self.rows)
        open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def crop(src, row, factor):
    """The manifest's crop, enlarged `factor` times (nearest) or, for a negative factor, shrunk by
    averaging -factor x -factor blocks."""
    name, label, x0, y0, x1, y1, _ = row
    w, h, px = read_ppm(os.path.join(src, name))
    out = []
    if factor >= 1:
        for y in range(y0, y1):
            r = bytearray()
            for x in range(x0, x1):
                p = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
                r += p * factor
            for _ in range(factor):
                out.append(bytes(r))
    else:
        k = -factor
        for y in range(y0, y1 - k + 1, k):
            r = bytearray()
            for x in range(x0, x1 - k + 1, k):
                acc = [0, 0, 0]
                for dy in range(k):
                    base = ((y + dy) * w + x) * 3
                    for dx in range(k):
                        for c in range(3):
                            acc[c] += px[base + dx * 3 + c]
                r += bytes(v // (k * k) for v in acc)
            out.append(bytes(r))
    return label, out


def sheet(src, rows, out, cols, factor):
    cells = [crop(src, r, factor) for r in rows]
    if not cells:
        return
    tw = max(len(c[1][0]) // 3 for c in cells)
    th = max(len(c[1]) for c in cells)
    size = 2
    per_line = max(10, (tw - 8) // (6 * size))
    bar, gap = 2 * (7 * size + 6) + 4, 6
    n = len(cells)
    nrows = (n + cols - 1) // cols
    img = Img(cols * (tw + gap) + gap, nrows * (th + bar + gap) + gap, BG)
    for i, (label, tile) in enumerate(cells):
        x = gap + (i % cols) * (tw + gap)
        y = gap + (i // cols) * (th + bar + gap)
        img.rect(x, y, x + tw, y + bar, BAR)
        words = "%d. %s" % (i + 1, label)
        img.text(x + 4, y + 4, words[:per_line], TXT, size)
        img.text(x + 4, y + 4 + 7 * size + 6, words[per_line:2 * per_line], TXT, size)
        img.paste(tile, x, y + bar)
    img.save(out)
    print("wrote %s: %d tiles, %dx%d" % (out, n, img.w, img.h))


def load(src):
    rows = []
    with open(os.path.join(src, "manifest.txt")) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            name, label, x0, y0, x1, y1, sc = line.split("|")
            rows.append((name, label, int(x0), int(y0), int(x1), int(y1), int(sc)))
    return rows


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = load(src)

    def pick(pred):
        return [r for r in rows if pred(r[0])]

    for m, place in enumerate(["left", "middle", "right"]):
        mine = pick(lambda n, m=m: n.startswith("P_") and ("_m%d_stage" % m in n))
        wilt = pick(lambda n: n.startswith("P_") and n.endswith("_wilt.ppm"))
        if m < len(wilt):
            mine.append(wilt[m])
        sheet(src, mine, os.path.join(out, "patch_%s.png" % place), 4, 2)
    cond = pick(lambda n: n.startswith("P_") and any(k in n for k in ("_dusk", "_night", "_rain", "_chip", "_pop")))
    sheet(src, cond, os.path.join(out, "patch_conditions.png"), 4, 2)
    sheet(src, pick(lambda n: n.startswith("P_") and "ripe_full" in n), os.path.join(out, "patch_frame.png"), 1, 1)
    sheet(src, pick(lambda n: n.startswith("S_")), os.path.join(out, "sky_moments.png"), 2, -2)
    sheet(src, pick(lambda n: n.startswith("R_")), os.path.join(out, "sky_strip_04s.png"), 2, 3)
    sheet(src, pick(lambda n: n.startswith("K_")), os.path.join(out, "sky_strip_ticks.png"), 2, 3)
    sheet(src, pick(lambda n: n.startswith("V_")), os.path.join(out, "sky_variants.png"), 2, -2)


if __name__ == "__main__":
    main()
