# stream_sheet.py: stage 17 stream stills (round 2) (stream_stills.cpp's PPMs) to PNGs, close-up crops and a
# numbered, labelled contact sheet. Needs Pillow.
#
#   python -I screensaver/tools/stream_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/NN_name.png (the whole frame), OUT_DIR/NN_name_closeup.png (a crop, scaled up with
# no smoothing, so each panel pixel is a block) and OUT_DIR/contact_sheet.png.
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# name, label, crop box (x0, y0, x1, y1) in frame px, scale. The boulder's foot is at (612, 394).
STILLS = [
    ("01_day", "Day: the boulder in the current", (556, 350, 676, 420), 3),
    ("02_dusk_firelight", "Dusk, fire lit", (556, 350, 676, 420), 3),
    ("03_night_moon", "Night: moon glint on the wake", (556, 350, 676, 420), 3),
    ("04_leaf", "Leaves drifting (no month: the mix)", (480, 372, 580, 426), 3),
    ("05_stick_snagged", "A stick snagged on the boulder", (576, 362, 676, 414), 3),
    ("06_pine_cone", "A pine cone (and a stick)", (430, 388, 510, 432), 4),
    ("07_fish_jump", "The bigger fish at the top of its jump", (385, 396, 455, 440), 4),
    ("08_fish_splash", "Its splash going back in", (370, 410, 430, 446), 4),
    ("09_fish_rings", "Its rings", (365, 412, 440, 450), 4),
    ("10_rise_ring", "A rise ring, no fish", (340, 418, 420, 458), 4),
    ("11_rain", "Rain: faster, browner, more debris", (420, 350, 700, 444), 2),
    ("12_heron_arriving", "Heron: flying in", (395, 270, 505, 340), 3),
    ("13_heron_standing", "Heron: standing in the shallows", (360, 380, 520, 466), 3),
    ("14_heron_strike", "Heron: a strike at the water", (360, 380, 520, 466), 3),
    ("15_heron_ring", "Heron: the ring where it struck", (360, 380, 520, 466), 3),
    ("16_deer_walking", "Deer: walking in along the bank", (610, 290, 800, 400), 3),
    ("17_deer_drinking", "Deer: drinking, rings at its muzzle", (600, 300, 780, 400), 3),
    ("18_deer_looking", "Deer: looking up between drinks", (600, 300, 780, 400), 3),
    ("19_leaves_summer", "Leaves in a summer month", (390, 390, 530, 450), 3),
    ("20_leaves_autumn", "Leaves in mid autumn", (390, 390, 530, 450), 3),
    ("21_leaves_winter", "A leaf in winter (and a stick)", (390, 390, 530, 450), 3),
    ("22_duck_mockup", "Mockup only: a duck drifting through", (436, 384, 514, 428), 4),
    ("23_dragonfly_mockup", "Mockup only: a dragonfly over the water", (540, 368, 620, 410), 4),
    ("24_dog_heron", "Lucy charges, the heron lifts off", (300, 250, 580, 470), 2),
    ("25_dog_deer", "Copper runs at the deer, it bolts", (480, 280, 800, 420), 2),
]


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    tiles = []
    for name, label, box, k in STILLS:
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
        font = ImageFont.load_default(size=15)
    except TypeError:
        font = ImageFont.load_default()
    for i, (label, im) in enumerate(cells):
        x0 = gap + (i % cols) * (tw + gap)
        y0 = gap + (i // cols) * (th + bar + gap)
        d.rectangle([x0, y0, x0 + tw - 1, y0 + bar - 1], fill=(52, 52, 52))
        d.text((x0 + 6, y0 + 3), "%d. %s" % (i + 1, label), fill=(235, 235, 235), font=font)
        sheet.paste(im, (x0 + (tw - im.width) // 2, y0 + bar + (th - im.height) // 2))
    sheet.save(os.path.join(out, "contact_sheet.png"))
    print("wrote %d stills, %d tiles" % (len(tiles), len(cells)))


if __name__ == "__main__":
    main()
