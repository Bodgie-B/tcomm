# plink_sheet.py: stage 21 plinking stills (plink_stills.cpp's PPMs and manifest.txt) to PNGs: one sheet per
# variant (a row of moments, each cropped and enlarged, labelled with what it is and the seconds into the
# major) and a contact sheet with the first of every variant's full frames. Needs Pillow.
#
#   python -I screensaver/tools/plink_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/sheet_A.png, sheet_B.png, sheet_C.png (the variants), sheet_mirror.png (the two mirrored
# layouts) and contact_sheet.png (the full-frame stills).
import os
import sys

from PIL import Image, ImageDraw, ImageFont

BG = (34, 34, 34)
BAR = (52, 52, 52)
TXT = (235, 235, 235)


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


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


def tile(src, row, width):
    name, label, x0, y0, x1, y1, sc = row
    im = Image.open(os.path.join(src, name)).convert("RGB").crop((x0, y0, x1, y1))
    w = width
    h = int(im.height * (w / im.width))
    return label, im.resize((w, h), Image.NEAREST if sc >= 3 else Image.BOX)


def sheet(src, rows, out, cols, tw):
    cells = [tile(src, r, tw) for r in rows]
    if not cells:
        return
    f = font(13)
    th = max(c[1].height for c in cells)
    bar, gap = 34, 6
    nrows = (len(cells) + cols - 1) // cols
    img = Image.new("RGB", (cols * (tw + gap) + gap, nrows * (th + bar + gap) + gap), BG)
    d = ImageDraw.Draw(img)
    for i, (label, im) in enumerate(cells):
        x = gap + (i % cols) * (tw + gap)
        y = gap + (i // cols) * (th + bar + gap)
        d.rectangle([x, y, x + tw - 1, y + bar - 1], fill=BAR)
        words = ("%d. %s" % (i + 1, label))
        d.text((x + 5, y + 2), words[:46], fill=TXT, font=f)
        d.text((x + 5, y + 17), words[46:92], fill=TXT, font=f)
        img.paste(im, (x, y + bar))
    img.save(out)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = load(src)
    for tag in "ABC":
        mine = [r for r in rows if r[0].startswith(tag + "_") and "full" not in r[0]]
        sheet(src, mine, os.path.join(out, "sheet_%s.png" % tag), 4, 420)
    mirror = [r for r in rows if r[0][0] in "LR"]
    sheet(src, mirror, os.path.join(out, "sheet_mirror.png"), 2, 420)
    full = [(r[0], r[1], 0, 0, 800, 480, 1) for r in rows if "full" in r[0] and r[0][0] in "ABC"]
    sheet(src, full, os.path.join(out, "contact_sheet.png"), 2, 640)
    print("wrote sheets to", out)


if __name__ == "__main__":
    main()
