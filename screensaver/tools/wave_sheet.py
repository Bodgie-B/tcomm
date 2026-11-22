# wave_sheet.py: the stills of wave_stills.cpp (2026-11-16) as one labelled sheet: the old wave on the top
# row, the new one below it, eight moments each, cropped round the hiker and enlarged, with the time into
# the wave on every frame, and one full-scene frame (the new wave's peak) underneath. Needs Pillow.
#
#   python -I screensaver/tools/wave_sheet.py STILLS_DIR OUT_DIR
#
# STILLS_DIR holds wave_stills' new_NN.ppm / old_NN.ppm and new_rect.txt / old_rect.txt ("k secs x0 y0 x1 y1"
# a line). Writes OUT_DIR/wave_sheet.png.
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ZOOM = 3
PAD_X, PAD_TOP, PAD_BOT = 28, 30, 8


def load_rects(path):
    rows = []
    with open(path) as f:
        for line in f:
            p = line.split()
            rows.append((int(p[0]), float(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5])))
    return rows


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = {k: load_rects(os.path.join(src, k + "_rect.txt")) for k in ("old", "new")}
    allr = rows["old"] + rows["new"]
    box = (
        max(0, min(r[2] for r in allr) - PAD_X),
        max(0, min(r[3] for r in allr) - PAD_TOP),
        min(800, max(r[4] for r in allr) + PAD_X),
        min(480, max(r[5] for r in allr) + PAD_BOT),
    )
    cw, ch = (box[2] - box[0]) * ZOOM, (box[3] - box[1]) * ZOOM
    try:
        font = ImageFont.load_default(size=18)
        big = ImageFont.load_default(size=22)
    except TypeError:
        font = big = ImageFont.load_default()
    gap, bar, left = 8, 26, 120
    full = Image.open(os.path.join(src, "new_05.ppm")).convert("RGB")
    sheet_w = left + 8 * (cw + gap) + gap
    sheet_h = 34 + 2 * (bar + ch + gap) + 34 + full.height + gap
    sheet = Image.new("RGB", (sheet_w, sheet_h), (34, 34, 34))
    d = ImageDraw.Draw(sheet)
    d.text((gap, 6), "The hiker's wave to a passer-by, daytime camp. Old wave 2.6 s above, new wave 3.8 s below "
           "(same moments of each, scaled), crops enlarged %dx." % ZOOM, fill=(235, 235, 235), font=big)
    y = 34
    for key, title in (("old", "OLD\n2.6 s"), ("new", "NEW\n3.8 s")):
        d.text((gap, y + bar + ch // 2 - 20), title, fill=(255, 214, 120) if key == "new" else (180, 180, 180), font=big)
        for k, secs, *_ in rows[key]:
            x = left + (k - 1) * (cw + gap)
            im = Image.open(os.path.join(src, "%s_%02d.ppm" % (key, k))).convert("RGB").crop(box)
            d.rectangle([x, y, x + cw - 1, y + bar - 1], fill=(52, 52, 52))
            d.text((x + 6, y + 3), "%d.  %.2f s" % (k, secs), fill=(235, 235, 235), font=font)
            sheet.paste(im.resize((cw, ch), Image.NEAREST), (x, y + bar))
        y += bar + ch + gap
    d.text((gap, y + 4), "The whole scene at 1.5 s into the new wave (the crop above is the area round him)",
           fill=(235, 235, 235), font=big)
    sheet.paste(full, (left, y + 34))
    path = os.path.join(out, "wave_sheet.png")
    sheet.save(path)
    print("wrote %s %dx%d" % (path, sheet.width, sheet.height))


if __name__ == "__main__":
    main()
