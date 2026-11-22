# taichi_sheet.py: stage 22 tai chi stills (taichi_stills.cpp's PPM tiles and manifest.txt) to PNG sheets.
# zlib only (no Pillow): the labels are drawn with a small built-in 5x7 font, in capitals.
#
#   python -I screensaver/tools/taichi_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/taichi_s1_postures.png (every form at 2x), taichi_s2_chapter1..4.png (a frame every 2 s at
# panel size), taichi_s4_turn.png (the turn at 3x, aided above, plain below), taichi_s6_feet.png and
# taichi_s7_gags.png. From taichi_scene_stills.cpp's tiles (the major in the real scene, step C2):
# taichi_s3_guests.png, taichi_s3_deer.png, taichi_s5_tight.png, taichi_s7_gags.png (in the scene, with
# Copper), taichi_s8_dogs.png, taichi_s9_doorbell.png, taichi_s10_light.png, taichi_s11_cattails.png.
import os
import struct
import sys
import zlib

BG = (34, 34, 34)
BAR = (52, 52, 52)
TXT = (235, 235, 235)

# 5x7 capitals, digits and a little punctuation; each glyph is 7 rows of 5 bits (bit 4 the left column).
FONT = {
    "A": "0E 11 11 1F 11 11 11", "B": "1E 11 11 1E 11 11 1E", "C": "0E 11 10 10 10 11 0E",
    "D": "1E 11 11 11 11 11 1E", "E": "1F 10 10 1E 10 10 1F", "F": "1F 10 10 1E 10 10 10",
    "G": "0E 11 10 17 11 11 0F", "H": "11 11 11 1F 11 11 11", "I": "0E 04 04 04 04 04 0E",
    "J": "07 02 02 02 02 12 0C", "K": "11 12 14 18 14 12 11", "L": "10 10 10 10 10 10 1F",
    "M": "11 1B 15 15 11 11 11", "N": "11 11 19 15 13 11 11", "O": "0E 11 11 11 11 11 0E",
    "P": "1E 11 11 1E 10 10 10", "Q": "0E 11 11 11 15 12 0D", "R": "1E 11 11 1E 14 12 11",
    "S": "0F 10 10 0E 01 01 1E", "T": "1F 04 04 04 04 04 04", "U": "11 11 11 11 11 11 0E",
    "V": "11 11 11 11 11 0A 04", "W": "11 11 11 15 15 15 0A", "X": "11 11 0A 04 0A 11 11",
    "Y": "11 11 0A 04 04 04 04", "Z": "1F 01 02 04 08 10 1F",
    "0": "0E 11 13 15 19 11 0E", "1": "04 0C 04 04 04 04 0E", "2": "0E 11 01 02 04 08 1F",
    "3": "1F 02 04 02 01 11 0E", "4": "02 06 0A 12 1F 02 02", "5": "1F 10 1E 01 01 11 0E",
    "6": "06 08 10 1E 11 11 0E", "7": "1F 01 02 04 08 08 08", "8": "0E 11 11 0E 11 11 0E",
    "9": "0E 11 11 0F 01 02 0C",
    " ": "00 00 00 00 00 00 00", ".": "00 00 00 00 00 0C 0C", ",": "00 00 00 00 0C 04 08",
    ":": "00 0C 0C 00 0C 0C 00", ";": "00 0C 0C 00 0C 04 08", "'": "0C 04 08 00 00 00 00",
    "-": "00 00 00 1F 00 00 00", "+": "00 04 04 1F 04 04 00", "(": "02 04 08 08 08 04 02",
    ")": "08 04 02 02 02 04 08", "/": "01 01 02 04 08 10 10", "=": "00 00 1F 00 1F 00 00",
    "?": "0E 11 01 02 04 00 04",
}
GLYPH = {k: [int(b, 16) for b in v.split()] for k, v in FONT.items()}


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


class Canvas:
    def __init__(self, w, h, fill):
        self.w, self.h = w, h
        self.rows = [bytearray(bytes(fill) * w) for _ in range(h)]

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h, y1)):
            r = self.rows[y]
            r[max(0, x0) * 3:min(self.w, x1) * 3] = bytes(c) * (min(self.w, x1) - max(0, x0))

    def text(self, x, y, s, c, scale=1):
        for ch in s.upper():
            g = GLYPH.get(ch, GLYPH["?"])
            for gy, bits in enumerate(g):
                for gx in range(5):
                    if bits & (16 >> gx):
                        self.rect(x + gx * scale, y + gy * scale, x + (gx + 1) * scale, y + (gy + 1) * scale, c)
            x += 6 * scale

    def paste(self, x0, y0, w, h, px, sx0, sy0, sx1, sy1, scale):
        # crop (sx0, sy0)-(sx1, sy1) of a w x h image, enlarged `scale` times with no smoothing
        for y in range(sy0, sy1):
            src = px[(y * w + sx0) * 3:(y * w + sx1) * 3]
            if scale > 1:
                src = b"".join(src[i:i + 3] * scale for i in range(0, len(src), 3))
            for k in range(scale):
                yy = y0 + (y - sy0) * scale + k
                if 0 <= yy < self.h:
                    self.rows[yy][x0 * 3:x0 * 3 + len(src)] = src

    def save(self, path):
        def chunk(tag, body):
            return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
        raw = b"".join(b"\x00" + bytes(r) for r in self.rows)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def load(src):
    rows = []
    with open(os.path.join(src, "manifest.txt")) as f:
        for line in f:
            line = line.strip()
            if line:
                g, name, label, x0, y0, x1, y1, sc = line.split("|")
                rows.append((g, name, label, int(x0), int(y0), int(x1), int(y1), int(sc)))
    return rows


def sheet(src, rows, out, cols, title):
    if not rows:
        return
    tiles = []
    for g, name, label, x0, y0, x1, y1, sc in rows:
        w, h, px = read_ppm(os.path.join(src, name))
        tiles.append((label, w, h, px, x0, y0, min(x1, w), min(y1, h), sc))
    tw = max((t[6] - t[4]) * t[8] for t in tiles)
    th = max((t[7] - t[5]) * t[8] for t in tiles)
    bar, gap, head = 12, 4, 16
    nrows = (len(tiles) + cols - 1) // cols
    cv = Canvas(cols * (tw + gap) + gap, head + nrows * (th + bar + gap) + gap, BG)
    cv.text(gap, 4, title, TXT)
    for i, (label, w, h, px, x0, y0, x1, y1, sc) in enumerate(tiles):
        x = gap + (i % cols) * (tw + gap)
        y = head + gap + (i // cols) * (th + bar + gap)
        cv.rect(x, y, x + tw, y + bar, BAR)
        cv.text(x + 2, y + 2, label[: max(1, (tw - 4) // 6)], TXT)
        cv.paste(x, y + bar, w, h, px, x0, y0, x1, y1, sc)
    cv.save(out)
    print("wrote %s (%d tiles, %dx%d)" % (out, len(tiles), cv.w, cv.h))


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = load(src)
    pick = lambda g: [r for r in rows if r[0] == g]
    sheet(src, pick("s1"), os.path.join(out, "taichi_s1_postures.png"), 6,
          "S1 TAI CHI POSTURES, 2X, HAT OFF (CHAPTER.KEY)")
    for c in range(1, 5):
        sheet(src, pick("s2c%d" % c), os.path.join(out, "taichi_s2_chapter%d.png" % c), 8,
              "S2 CHAPTER %d, A FRAME EVERY 2 S, PANEL SIZE" % c)
    sheet(src, pick("s4a") + pick("s4b"), os.path.join(out, "taichi_s4_turn.png"), 16,
          "S4 THE TURN, 3X, TICKS FROM THE FLIP: ABOVE WITH END-ON FEET AND STRAIGHT KNEES, BELOW WITHOUT")
    sheet(src, pick("s6"), os.path.join(out, "taichi_s6_feet.png"), 1, "S6 FIVE BODIES, ONE PAIR OF FEET, 3X")
    scene = bool(pick("s3"))   # taichi_scene_stills.cpp's tiles (step C2): the major in the real scene
    sheet(src, pick("s7w") + pick("s7i") + pick("s7b"), os.path.join(out, "taichi_s7_gags.png"), 4 if scene else 5,
          "S7 THE GAGS IN THE SCENE, 2X: THE WOBBLE ON THE ROOSTER, THE ITCH, COPPER'S BUMP" if scene
          else "S7 THE GAGS, 2X (THE BUMP WITHOUT THE DOG)")
    sheet(src, pick("s3"), os.path.join(out, "taichi_s3_guests.png"), 4, "S3 THE GUESTS, 2X")
    sheet(src, pick("s3w"), os.path.join(out, "taichi_s3_deer.png"), 1, "S3 THE DEER AT THE TREE LINE (CHAPTER 4), 2X")
    sheet(src, pick("s5"), os.path.join(out, "taichi_s5_tight.png"), 3, "S5 THE TIGHT SEED: THE PINE'S SKIRT, 2X")
    sheet(src, pick("s8"), os.path.join(out, "taichi_s8_dogs.png"), 2, "S8 LUCY WATCHING; BOTH DOGS, 2X")
    sheet(src, pick("s9"), os.path.join(out, "taichi_s9_doorbell.png"), 4, "S9 THE DOORBELL MID-ROOSTER, 2X")
    sheet(src, pick("s10"), os.path.join(out, "taichi_s10_light.png"), 3, "S10 LIGHT, PANEL SIZE")
    sheet(src, pick("s11"), os.path.join(out, "taichi_s11_cattails.png"), 2, "S11 BEHIND THE CATTAILS; THE HAT, 2X")


if __name__ == "__main__":
    main()
