# clouds_sheet.py: stage 19 cloud stills (clouds_stills.cpp's PPMs) to PNGs and a numbered, labelled
# contact sheet. Needs Pillow.
#
#   python -I screensaver/tools/clouds_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/NN_name.png (the whole frame), OUT_DIR/NN_name_sky.png (the sky band, rows 0-200, at
# full size) and OUT_DIR/contact_sheet.png.
import os
import sys

from PIL import Image, ImageDraw, ImageFont

STILLS = [
    ("01_day_cover3_t0", "Day, cover 3: the start"),
    ("02_day_cover3_t20s", "Day, cover 3: 20 s later (all five clouds left)"),
    ("03_day_cover3_t40s", "Day, cover 3: 40 s later"),
    ("04_dusk_cover3", "Dusk, cover 3"),
    ("05_night_cover3", "Night, cover 3"),
]
SKY = (0, 0, 800, 200)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    cells = []
    for name, label in STILLS:
        im = Image.open(os.path.join(src, name + ".ppm")).convert("RGB")
        im.save(os.path.join(out, name + ".png"))
        im.crop(SKY).save(os.path.join(out, name + "_sky.png"))
        cells.append((label, im))
    tw, th, bar, gap = 400, 240, 22, 6
    cols = 3
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tw + gap) + gap, rows * (th + bar + gap) + gap), (34, 34, 34))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default(size=15)
    except TypeError:
        font = ImageFont.load_default()
    for i, (label, im) in enumerate(cells):
        x0 = gap + (i % cols) * (tw + gap)
        y0 = gap + (i // cols) * (th + bar + gap)
        d.rectangle([x0, y0, x0 + tw - 1, y0 + bar - 1], fill=(52, 52, 52))
        d.text((x0 + 6, y0 + 3), "%d. %s" % (i + 1, label), fill=(235, 235, 235), font=font)
        sheet.paste(im.resize((tw, th), Image.BOX), (x0, y0 + bar))
    sheet.save(os.path.join(out, "contact_sheet.png"))
    print("wrote %d stills" % len(cells))


if __name__ == "__main__":
    main()
