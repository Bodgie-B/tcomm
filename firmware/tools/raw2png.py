"""Convert raw RGB dumps from the simulator to PNG.  python -P tools/raw2png.py in.rgb out.png [w h]"""
import sys

from PIL import Image


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 800
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 480
    with open(src, "rb") as f:
        data = f.read()
    Image.frombytes("RGB", (w, h), data).save(dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
