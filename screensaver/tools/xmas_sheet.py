# xmas_sheet.py: stage 18 Christmas mockup stills (xmas_stills.cpp's PPMs and stills.txt) to PNGs,
# close-up crops and a numbered, labelled contact sheet. Needs Pillow.
#
#   python -I screensaver/tools/xmas_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/NN_name.png (the whole frame), OUT_DIR/NN_name_closeup.png (the crop stills.txt gives,
# scaled up with no smoothing, so each panel pixel is a block) and OUT_DIR/contact_sheet.png.
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    stills = []
    with open(os.path.join(src, "stills.txt")) as f:
        for line in f:
            p = line.rstrip("\n").split(" ", 6)
            if len(p) == 7:
                stills.append((p[0], (int(p[1]), int(p[2]), int(p[3]), int(p[4])), int(p[5]), p[6]))
    tiles = []
    for name, box, k, label in stills:
        im = Image.open(os.path.join(src, name + ".ppm")).convert("RGB")
        im.save(os.path.join(out, name + ".png"))
        c = im.crop(box)
        c = c.resize((c.width * k, c.height * k), Image.NEAREST)
        c.save(os.path.join(out, name + "_closeup.png"))
        tiles.append((label, im, c))
    tw, th, bar, gap = 400, 240, 22, 6
    cols = 4
    cells = []
    for label, im, c in tiles:   # each still, then its close-up, as two numbered tiles
        cells.append((label, im.resize((tw, th), Image.BOX)))
        r = min(tw / c.width, th / c.height)
        cells.append((label + " (close-up)", c.resize((max(1, int(c.width * r)), max(1, int(c.height * r))), Image.NEAREST)))
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tw + gap) + gap, rows * (th + bar + gap) + gap), (34, 34, 34))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default(size=13)
    except TypeError:
        font = ImageFont.load_default()
    for i, (label, im) in enumerate(cells):
        x0 = gap + (i % cols) * (tw + gap)
        y0 = gap + (i // cols) * (th + bar + gap)
        d.rectangle([x0, y0, x0 + tw - 1, y0 + bar - 1], fill=(52, 52, 52))
        d.text((x0 + 6, y0 + 4), "%d. %s" % (i + 1, label), fill=(235, 235, 235), font=font)
        sheet.paste(im, (x0 + (tw - im.width) // 2, y0 + bar + (th - im.height) // 2))
    sheet.save(os.path.join(out, "contact_sheet.png"))
    print("wrote %d stills, %d tiles" % (len(tiles), len(cells)))


if __name__ == "__main__":
    main()
