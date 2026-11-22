"""The stage 22 deer visit's whole-scene sheets: herd_scene_stills' PPMs and manifest as PNG sheets.
zlib only, no Pillow.

    python -I screensaver/tools/herd_scene_sheet.py STILLS_DIR OUT_DIR

STILLS_DIR holds herd_scene_stills' sNNN.ppm frames and manifest.txt, a line
"sheet|scale|label|file|x0|y0|x1|y1" for each still (the crop of that frame to show, in panel pixels, and
the whole number it is enlarged by: square, sharp pixels, what the panel's pixels are). Writes
OUT_DIR/scene_<sheet>_N.png, the stills of a sheet in the manifest's order, labelled. The font and the PNG
writer are herd_sheet.py's (the same folder).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from herd_sheet import BG, BAR, GAP, cut, put_text, read_ppm, write_png  # noqa: E402

MAX_W = 1400      # a page is at most this wide
MAX_ROWS = 4      # and this many rows of stills


def page(path, cells):
    cw, ch = len(cells[0][1][0]) // 3, len(cells[0][1])
    cols = max(1, (MAX_W - GAP) // (cw + GAP))
    bar = 18
    nr = (len(cells) + cols - 1) // cols
    cols = min(cols, len(cells))
    W, H = GAP + cols * (cw + GAP), GAP + nr * (ch + bar + GAP)
    rows = [bytearray(bytes(BG) * W) for _ in range(H)]
    for i, (label, tile) in enumerate(cells):
        x = GAP + (i % cols) * (cw + GAP)
        y = GAP + (i // cols) * (ch + bar + GAP)
        for r in range(bar):
            rows[y + r][x * 3:(x + cw) * 3] = bytes(BAR) * cw
        ts = 2 if len(label) * 12 + 6 <= cw else 1
        put_text(rows, x + 3, y + (2 if ts == 2 else 5), label[:max(1, (cw - 6) // (6 * ts))], ts)
        for r, line in enumerate(tile):
            rows[y + bar + r][x * 3:(x + cw) * 3] = line
    write_png(path, rows)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    sheets, order = {}, []
    with open(os.path.join(src, "manifest.txt"), encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            sheet, k, label, name, x0, y0, x1, y1 = line.split("|")
            if sheet not in sheets:
                sheets[sheet] = []
                order.append(sheet)
            img = read_ppm(os.path.join(src, name))
            sheets[sheet].append((label, cut(img, (int(x0), int(y0), int(x1), int(y1)), int(k))))
    wrote = 0
    for sheet in order:
        cells = sheets[sheet]
        cw = len(cells[0][1][0]) // 3
        per = max(1, (MAX_W - GAP) // (cw + GAP)) * MAX_ROWS
        for p in range(0, len(cells), per):
            page(os.path.join(dst, "scene_%s_%d.png" % (sheet, p // per + 1)), cells[p:p + per])
            wrote += 1
    print("wrote %d sheets to %s" % (wrote, dst))


if __name__ == "__main__":
    main()
