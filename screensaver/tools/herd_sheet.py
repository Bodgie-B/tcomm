"""The stage 22 deer pose sheet: herd_stills' PPMs and manifest as PNG sheets. zlib only, no Pillow.

    python -I screensaver/tools/herd_sheet.py STILLS_DIR OUT_DIR

STILLS_DIR holds herd_stills' poses_day.ppm, poses_golden.ppm, poses_night.ppm, strips_day.ppm,
strips_night.ppm and manifest.txt, a line "file|label|x0|y0|x1|y1" for each cell (its box in the PPM, in
panel pixels). Each cell is cut down to the deer's own part of it, enlarged by a whole number (square, sharp
pixels: what the panel's pixels are) and labelled. Writes, in OUT_DIR:

    poses_day_2x_N.png, poses_golden_2x_N.png, poses_night_2x_N.png   the poses, 2x, many to a page
    poses_day_4x_N.png                                                  the poses, 4x, a dozen to a page
    strips_day_2x_N.png, strips_night_2x_N.png                          the strips (real frames of a move in
                                                                        a row of eight), 2x, three to a page
    strips_day_4x_N.png                                                 the strips, 4x, four frames to a row

(N counts the pages.) The labels use a 5 x 7 bitmap font built in below.
"""
import os
import struct
import sys
import zlib

BG = (30, 32, 36)
BAR = (52, 54, 60)
INK = (236, 236, 230)
GAP = 6

# Crop of every cell: the deer's own part (the feet are at row 68 of the cell's 80).
CROP = (8, 12, 112, 74)

# The classic 5 x 7 font, ASCII 0x20 to 0x5A, five column bytes a glyph (bit 0 the top row).
FONT = [
    0x0000000000, 0x00005F0000, 0x0007000700, 0x147F147F14, 0x242A7F2A12, 0x2313086462, 0x3649552250,
    0x0005030000, 0x001C224100, 0x0041221C00, 0x14083E0814, 0x08083E0808, 0x0050300000, 0x0808080808,
    0x0060600000, 0x2010080402, 0x3E5149453E, 0x00427F4000, 0x4261514946, 0x2141454B31, 0x1814127F10,
    0x2745454539, 0x3C4A494930, 0x0171090503, 0x3649494936, 0x064949291E, 0x0036360000, 0x0056360000,
    0x0814224100, 0x1414141414, 0x0041221408, 0x0201510906, 0x324979413E, 0x7E1111117E,
    0x7F49494936, 0x3E41414122, 0x7F4141221C, 0x7F49494941, 0x7F09090901, 0x3E4149497A, 0x7F0808087F,
    0x00417F4100, 0x2040413F01, 0x7F08142241, 0x7F40404040, 0x7F020C027F, 0x7F0408107F, 0x3E4141413E,
    0x7F09090906, 0x3E4151215E, 0x7F09192946, 0x4649494931, 0x01017F0101, 0x3F4040403F, 0x1F2040201F,
    0x3F4038403F, 0x6314081463, 0x0708700807, 0x6151494543,
]


def glyph_cols(ch):
    o = ord(ch.upper())
    if o < 0x20 or o > 0x5A:
        o = 0x3F
    v = FONT[o - 0x20]
    return [(v >> (8 * (4 - i))) & 0xFF for i in range(5)]


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


def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)


def write_png(path, rows):
    h, w = len(rows), len(rows[0]) // 3
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def put_text(rows, x, y, text, k=2):
    for ch in text:
        for ci, col in enumerate(glyph_cols(ch)):
            for ri in range(7):
                if col >> ri & 1:
                    for dy in range(k):
                        row = rows[y + ri * k + dy]
                        for dx in range(k):
                            xx = (x + ci * k + dx) * 3
                            row[xx:xx + 3] = bytes(INK)
        x += 6 * k


def cut(img, box, k):
    w, h, px = img
    x0, y0, x1, y1 = box
    out = []
    for y in range(y0, y1):
        line = bytearray()
        for x in range(x0, x1):
            p = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
            line += p * k
        for _ in range(k):
            out.append(line)
    return out


def sheet(path, cells, cols, k):
    cw = (CROP[2] - CROP[0]) * k
    ch = (CROP[3] - CROP[1]) * k
    ts = 2 if cw >= 380 else 1          # the label's size: small on the 2x sheets so a long one fits
    bar = 18 if ts == 2 else 12
    n = len(cells)
    nr = (n + cols - 1) // cols
    W, H = GAP + cols * (cw + GAP), GAP + nr * (ch + bar + GAP)
    rows = [bytearray(bytes(BG) * W) for _ in range(H)]
    for i, (label, tile) in enumerate(cells):
        x = GAP + (i % cols) * (cw + GAP)
        y = GAP + (i // cols) * (ch + bar + GAP)
        for r in range(bar):
            rows[y + r][x * 3:(x + cw) * 3] = bytes(BAR) * cw
        maxc = max(1, (cw - 6) // (6 * ts))
        put_text(rows, x + 3, y + 2 if ts == 2 else y + 2, label[:maxc], ts)
        for r, line in enumerate(tile):
            rows[y + bar + r][x * 3:(x + cw) * 3] = line
    write_png(path, rows)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    cells = {}
    with open(os.path.join(src, "manifest.txt"), encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            if line:
                name, label, x0, y0, x1, y1 = line.split("|")
                cells.setdefault(name, []).append((label, int(x0), int(y0)))
    wrote = 0
    for name, items in cells.items():
        kind = name.split("_")[0]
        look = name.split("_")[1].split(".")[0]
        img = read_ppm(os.path.join(src, name))
        layouts = ((2, 7, 42), (4, 3, 12)) if kind == "poses" else ((2, 8, 24), (4, 4, 16))
        for k, cols, per in layouts:
            if k == 4 and look != "day":
                continue
            tiles = []
            for label, x0, y0 in items:
                box = (x0 + CROP[0], y0 + CROP[1], x0 + CROP[2], y0 + CROP[3])
                tiles.append((label, cut(img, box, k)))
            for page in range(0, len(tiles), per):
                out = os.path.join(dst, "%s_%s_%dx_%d.png" % (kind, look, k, page // per + 1))
                sheet(out, tiles[page:page + per], cols, k)
                wrote += 1
    print("wrote %d sheets to %s" % (wrote, dst))


if __name__ == "__main__":
    main()
