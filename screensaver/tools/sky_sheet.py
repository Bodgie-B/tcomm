"""The stage 12 sky review: sky_stills' frames as PNGs and one labelled contact sheet.

    python -I screensaver/tools/sky_sheet.py STILLS_DIR OUT_DIR

STILLS_DIR holds sky_stills' NN_name.ppm files and tiles.txt ("file|label" a line).
Writes OUT_DIR/NN_name.png (800 x 480) and OUT_DIR/contact_sheet.png.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

COLS = 5
TILE_W, TILE_H = 400, 240     # half size
LABEL_H = 30
GAP = 8
BG = (24, 26, 30)
INK = (232, 232, 226)


def font(size):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    tiles = []
    with open(os.path.join(src, "tiles.txt"), encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line:
                name, label = line.split("|", 1)
                tiles.append((name, label))
    rows = (len(tiles) + COLS - 1) // COLS
    sheet = Image.new("RGB", (GAP + COLS * (TILE_W + GAP), GAP + rows * (TILE_H + LABEL_H + GAP)), BG)
    draw = ImageDraw.Draw(sheet)
    fnt = font(17)
    for i, (name, label) in enumerate(tiles):
        im = Image.open(os.path.join(src, name)).convert("RGB")
        im.save(os.path.join(dst, os.path.splitext(name)[0] + ".png"))
        x = GAP + (i % COLS) * (TILE_W + GAP)
        y = GAP + (i // COLS) * (TILE_H + LABEL_H + GAP)
        sheet.paste(im.resize((TILE_W, TILE_H), Image.LANCZOS), (x, y))
        draw.text((x + 2, y + TILE_H + 5), "%d  %s" % (i + 1, label), fill=INK, font=fnt)
    sheet.save(os.path.join(dst, "contact_sheet.png"))
    print("%d stills and contact_sheet.png in %s" % (len(tiles), dst))


if __name__ == "__main__":
    main()
