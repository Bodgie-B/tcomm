# trip_scene_sheet.py: stage 22 mushroom story stills (trip_scene_stills.cpp's PPM tiles and manifest.txt)
# to PNG sheets (MUSHROOM_PLAN section 12 items 5 to 8). zlib only; it borrows taichi_sheet.py's canvas.
#
#   python -I screensaver/tools/trip_scene_sheet.py PPM_DIR OUT_DIR
#
# Writes OUT_DIR/story_left.png, story_right.png, vision_set0.png, vision_set1.png, story_sky.png,
# story_dogs.png, story_doorbell.png, story_pocket.png, contact_set0.png, contact_set1.png.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import taichi_sheet as ts  # noqa: E402


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rows = ts.load(src)
    pick = lambda g: [r for r in rows if r[0] == g]
    sh = lambda g, name, cols, title: ts.sheet(src, pick(g), os.path.join(out, name), cols, title)
    sh("st", "story_left.png", 5, "THE MUSHROOM STORY, STEP BY STEP, 2X (MOTHER ON THE LEFT)")
    sh("str", "story_right.png", 4, "THE CROSSING AND THE PICKING WITH THE MOTHER ON THE RIGHT, 2X")
    sh("v0", "vision_set0.png", 4, "SET 0, THE VISITORS: THE BIG CHIPMUNK, THE DEER WITH THE TREE, 2X")
    sh("v1", "vision_set1.png", 4, "SET 1, THE WATER: THE SLOW FISH, SMOKE RINGS FROM THE COLD PIT, 2X")
    sh("sky", "story_sky.png", 2, "THE SKY OVER HIM, PANEL SIZE")
    sh("dog", "story_dogs.png", 3, "LUCY BESIDE HIM; COPPER BESIDE HIM, 2X")
    sh("bell", "story_doorbell.png", 3, "THE DOORBELL MID-SKY, PANEL SIZE")
    sh("pk", "story_pocket.png", 4, "THE POCKET ENDING, 2X")
    sh("c0", "contact_set0.png", 6, "SET 0, A HALF-SIZE FRAME EVERY 8 S OF THE BEAT")
    sh("c1", "contact_set1.png", 6, "SET 1, A HALF-SIZE FRAME EVERY 8 S OF THE BEAT")


if __name__ == "__main__":
    main()
