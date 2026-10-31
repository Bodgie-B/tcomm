"""Turn the panel's serial "shot" (its frame buffer: RGB565, little-endian) into a PNG.

python -P tools/shot2png.py in.raw out.png [w h]   (800 x 480 unless given)
"""
import sys

from PIL import Image


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 800
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 480
    with open(src, "rb") as f:
        data = f.read()
    if len(data) != w * h * 2:
        print(f"{src}: {len(data)} bytes, not the {w * h * 2} of a {w} x {h} screen")
        return 1
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = data[2 * i] | data[2 * i + 1] << 8
        r, g, b = v >> 11, (v >> 5) & 63, v & 31
        rgb[3 * i] = r << 3 | r >> 2
        rgb[3 * i + 1] = g << 2 | g >> 4
        rgb[3 * i + 2] = b << 3 | b >> 2
    Image.frombytes("RGB", (w, h), bytes(rgb)).save(dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
