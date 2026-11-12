"""The stage 13 review: dog_stills' frames as PNGs and one labelled contact sheet.

    python -I screensaver/tools/dog_sheet.py STILLS_DIR OUT_DIR

STILLS_DIR holds dog_stills' NN_name.ppm files and tiles.txt: "file|label" a line, or
"file|label|x0,y0,x1,y1" for a close-up of that box of the file, enlarged by a whole number (pixels
kept square and sharp, so it shows what the panel's pixels are).
Writes OUT_DIR/NN_name.png (800 x 480) for each full frame and OUT_DIR/contact_sheet.png.
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


def closeup(im, box):
    x0, y0, x1, y1 = box
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(im.width, x1), min(im.height, y1)
    part = im.crop((x0, y0, x1, y1))
    k = max(1, min(TILE_W // part.width, TILE_H // part.height))
    part = part.resize((part.width * k, part.height * k), Image.NEAREST)
    tile = Image.new("RGB", (TILE_W, TILE_H), BG)
    tile.paste(part, ((TILE_W - part.width) // 2, (TILE_H - part.height) // 2))
    return tile, k


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    tiles = []
    with open(os.path.join(src, "tiles.txt"), encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line:
                tiles.append(line.split("|"))
    rows = (len(tiles) + COLS - 1) // COLS
    sheet = Image.new("RGB", (GAP + COLS * (TILE_W + GAP), GAP + rows * (TILE_H + LABEL_H + GAP)), BG)
    draw = ImageDraw.Draw(sheet)
    fnt = font(16)
    saved = 0
    for i, parts in enumerate(tiles):
        name, label = parts[0], parts[1]
        im = Image.open(os.path.join(src, name)).convert("RGB")
        if len(parts) > 2:
            tile, k = closeup(im, [int(v) for v in parts[2].split(",")])
            label = "%s (x%d)" % (label, k)
        else:
            im.save(os.path.join(dst, os.path.splitext(name)[0] + ".png"))
            saved += 1
            tile = im.resize((TILE_W, TILE_H), Image.LANCZOS)
        x = GAP + (i % COLS) * (TILE_W + GAP)
        y = GAP + (i // COLS) * (TILE_H + LABEL_H + GAP)
        sheet.paste(tile, (x, y))
        draw.text((x + 2, y + TILE_H + 6), "%d  %s" % (i + 1, label), fill=INK, font=fnt)
    sheet.save(os.path.join(dst, "contact_sheet.png"))
    print("%d stills and contact_sheet.png (%d tiles) in %s" % (saved, len(tiles), dst))


if __name__ == "__main__":
    main()
