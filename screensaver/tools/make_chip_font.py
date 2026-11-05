#!/usr/bin/env python3
"""Cut the status chip's label font out of the panel's LVGL font source.

core/ cannot call LVGL, so the chip draws its six status words from a small
table of glyph metrics and 4-bit alpha bitmaps. This tool reads the Jost
ExtraBold LVGL C file the panel firmware already has (written by
tools/make_fonts.sh with lv_font_conv, plain 4 bpp, no compression), keeps only
the glyphs the labels need, and writes core/scene/chip_font.h.

    python3 -I screensaver/tools/make_chip_font.py
    python3 -I screensaver/tools/make_chip_font.py --font PATH.c --out PATH.h \
        --labels "OPEN,BUSY,PRIVATE,SLEEPING,AWAY,NO SIGNAL"

Standard library only. Exits non-zero, writing nothing, when the font lacks a
glyph a label needs, or when the font file is not the plain 4 bpp format this
tool reads (a compressed lv_font_conv file is refused rather than guessed at).

The labels are the panel's status words (firmware/tcomm/src/app/controller.cpp
status_word and the "NO SIGNAL" tile). core/scene/chip.cpp keeps its own copy of
the words; tests/test_chip.cpp checks every letter of them is in the table.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
DEFAULT_FONT = os.path.join(ROOT, "firmware", "tcomm", "src", "ui", "fonts", "font_jost_800_23.c")
DEFAULT_OUT = os.path.join(ROOT, "screensaver", "core", "scene", "chip_font.h")
DEFAULT_LABELS = "OPEN,BUSY,PRIVATE,SLEEPING,AWAY,NO SIGNAL"


def die(msg):
    sys.stderr.write("make_chip_font: " + msg + "\n")
    sys.exit(1)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def array_body(text, name):
    """The text between the braces of `name[] = { ... };`, comments removed."""
    m = re.search(r"\b" + re.escape(name) + r"\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", text, flags=re.S)
    if not m:
        die("array %s not found in the font file" % name)
    return m.group(1)


def int_list(body):
    return [int(v, 0) for v in re.findall(r"-?0[xX][0-9a-fA-F]+|-?\d+", body)]


def field(text, name, required=True):
    m = re.search(r"\." + re.escape(name) + r"\s*=\s*(-?\w+)", text)
    if not m:
        if required:
            die("field .%s not found in the font file" % name)
        return None
    return m.group(1)


class LvglFont:
    """The pieces of an lv_font_conv --format lvgl --no-compress file this tool reads."""

    def __init__(self, path):
        with open(path, "r", encoding="utf-8") as f:
            raw = f.read()
        head = re.search(r"Size:\s*(\d+)\s*px.*?Bpp:\s*(\d+)", raw, flags=re.S)
        if not head:
            die("no 'Size: N px / Bpp: N' banner in %s" % path)
        self.size_px = int(head.group(1))
        self.bpp = int(head.group(2))
        opts = re.search(r"Opts:\s*(.*)", raw)
        self.opts = opts.group(1).strip() if opts else ""
        text = strip_comments(raw)

        if self.bpp != 4:
            die("%s is %d bpp; this tool reads 4 bpp" % (path, self.bpp))
        fmt = int(field(text, "bitmap_format"))
        if fmt != 0:
            die("%s has bitmap_format %d (compressed). Rebuild it with --no-compress "
                "(tools/make_fonts.sh does) or teach this tool the LVGL RLE decoder." % (path, fmt))
        self.line_height = int(field(text, "line_height"))
        self.base_line = int(field(text, "base_line"))
        self.kern_scale = int(field(text, "kern_scale", required=False) or 16)

        self.bitmap = bytes(int_list(array_body(text, "glyph_bitmap")))

        self.glyphs = []
        gd = array_body(text, "glyph_dsc")
        for m in re.finditer(r"\{([^{}]*)\}", gd):
            vals = dict((k, int(v)) for k, v in re.findall(r"\.(\w+)\s*=\s*(-?\d+)", m.group(1)))
            self.glyphs.append(vals)
        if not self.glyphs:
            die("glyph_dsc is empty")

        self.cmaps = []
        cm = re.search(r"cmaps\s*\[\s*\]\s*=\s*\n?\s*\{(.*?)\n\};", text, flags=re.S)
        if not cm:
            die("cmaps not found")
        for m in re.finditer(r"\{([^{}]*)\}", cm.group(1)):
            body = m.group(1)
            c = {
                "start": int(field(body, "range_start")),
                "length": int(field(body, "range_length")),
                "gid0": int(field(body, "glyph_id_start")),
                "type": field(body, "type"),
                "list": None,
            }
            ul = field(body, "unicode_list")
            if ul != "NULL":
                c["list"] = int_list(array_body(text, ul))
            if field(body, "glyph_id_ofs_list") != "NULL":
                die("cmap with glyph_id_ofs_list is not supported")
            if c["type"] not in ("LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY", "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY"):
                die("cmap type %s is not supported" % c["type"])
            self.cmaps.append(c)

        # Class kerning (lv_font_conv writes this for fonts with a kern table).
        self.kern = None
        if re.search(r"\.kern_classes\s*=\s*1", text):
            left = int_list(array_body(text, "kern_left_class_mapping"))
            right = int_list(array_body(text, "kern_right_class_mapping"))
            vals = int_list(array_body(text, "kern_class_values"))
            lc = int(field(text, "left_class_cnt"))
            rc = int(field(text, "right_class_cnt"))
            if len(vals) != lc * rc:
                die("kerning table is %d values, expected %d x %d" % (len(vals), lc, rc))
            if len(left) != len(self.glyphs) or len(right) != len(self.glyphs):
                die("kerning class maps are %d/%d long for %d glyph ids" % (len(left), len(right), len(self.glyphs)))
            self.kern = (left, right, vals, rc)

    def gid(self, cp):
        for c in self.cmaps:
            if c["start"] <= cp < c["start"] + c["length"]:
                if c["type"].endswith("FORMAT0_TINY"):
                    return c["gid0"] + (cp - c["start"])
                off = cp - c["start"]
                if off in c["list"]:
                    return c["gid0"] + c["list"].index(off)
        return 0

    def kern_value(self, gl, gr):
        """Kerning between two glyph ids in 1/16 px (LVGL: value * kern_scale >> 4)."""
        if not self.kern:
            return 0
        left, right, vals, rc = self.kern
        lc, rcl = left[gl], right[gr]
        if lc == 0 or rcl == 0:
            return 0
        v = vals[(lc - 1) * rc + (rcl - 1)]
        return (v * self.kern_scale) >> 4

    def pixels(self, gid):
        """The glyph's alpha values 0..15 as rows of lists (plain 4 bpp, packed across rows)."""
        g = self.glyphs[gid]
        w, h, base = g["box_w"], g["box_h"], g["bitmap_index"]
        rows = []
        for y in range(h):
            row = []
            for x in range(w):
                i = y * w + x
                b = self.bitmap[base + (i >> 1)]
                row.append((b >> 4) if (i & 1) == 0 else (b & 0x0F))
            rows.append(row)
        return rows


def pack4(rows):
    flat = [v for r in rows for v in r]
    if len(flat) & 1:
        flat.append(0)
    return bytes((flat[i] << 4) | flat[i + 1] for i in range(0, len(flat), 2))


def charlit(ch):
    return "' '" if ch == " " else "'%s'" % ch


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--font", default=DEFAULT_FONT, help="LVGL font C file (Jost, plain 4 bpp)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="header to write")
    ap.add_argument("--labels", default=DEFAULT_LABELS, help="comma-separated words the chip shows")
    args = ap.parse_args()

    labels = [s for s in args.labels.split(",") if s]
    chars = sorted(set("".join(labels)))
    for ch in chars:
        if ord(ch) > 0x7E or ord(ch) < 0x20 or ch in "'\\":
            die("label character %r is not plain printable ASCII" % ch)
    font = LvglFont(args.font)

    missing = [ch for ch in chars if ch != " " and (font.gid(ord(ch)) == 0 or font.glyphs[font.gid(ord(ch))]["box_w"] == 0)]
    if missing:
        die("the font has no glyph for: %s (the panel trims some Jost sizes to the letters they use)" % " ".join(missing))
    if " " in chars and font.gid(32) == 0:
        die("the font has no space")

    # Glyph table, bitmaps packed back to back (4 bpp, packed across rows, high nibble first).
    glyph_rows, blob = [], bytearray()
    for ch in chars:
        gid = font.gid(ord(ch))
        g = font.glyphs[gid]
        bm = pack4(font.pixels(gid)) if g["box_w"] and g["box_h"] else b""
        glyph_rows.append((ch, gid, g, len(blob)))
        blob += bm
    if len(blob) > 0xFFFF:
        die("bitmaps exceed 64 KB")

    # Kerning for every pair of the glyph set that has a non-zero value.
    kerns = []
    for a in chars:
        for b in chars:
            v = font.kern_value(font.gid(ord(a)), font.gid(ord(b)))
            if v:
                if not -128 <= v <= 127:
                    die("kerning %s%s out of int8 range" % (a, b))
                kerns.append((a, b, v))

    # Cap height from a flat-topped, flat-bottomed capital (no overshoot).
    cap = None
    for ch in "EHIT":
        if ch in chars:
            g = font.glyphs[font.gid(ord(ch))]
            cap = g["box_h"] + g["ofs_y"]
            break
    if cap is None:
        die("none of E H I T in the labels: cannot take a cap height")

    src = os.path.relpath(args.font, ROOT).replace("\\", "/")
    out = []
    w = out.append
    w("// Generated by screensaver/tools/make_chip_font.py. Do not edit by hand.")
    w("//")
    w("// Source font: %s" % src)
    w("//   Jost ExtraBold (weight 800), %d px, %d bpp, plain (uncompressed) LVGL format. This is the" % (font.size_px, font.bpp))
    w("//   panel's own UI font (firmware tools/make_fonts.sh, lv_font_conv 1.5.3), so the chip's words are")
    w("//   drawn from the glyphs the status tiles use. Jost is SIL Open Font License 1.1.")
    w("//   lv_font_conv options: %s" % font.opts)
    w("// Glyphs kept: %s (the letters of: %s)." % (" ".join('"%s"' % c for c in chars), ", ".join(labels)))
    w("//")
    w("// Bitmaps are LVGL's: 4-bit alpha (0..15), rows left to right, top to bottom, packed across row")
    w("// ends with no padding, high nibble first; a glyph's pixel i = y * w + x is nibble i of its run.")
    w("// A glyph's pen advance in pixels is (adv + kern(this, next) + 8) >> 4, as lv_font_fmt_txt does.")
    w("// Its top-left corner is at x = pen + ofs_x, y = baseline - ofs_y - h (y grows down).")
    w("#pragma once")
    w("")
    w("#include <stdint.h>")
    w("")
    w("namespace ss {")
    w("")
    w("struct ChipGlyph {")
    w("  char ch;")
    w("  uint8_t w, h;         // bitmap size in pixels (0 for the space)")
    w("  int8_t ofs_x, ofs_y;  // left bearing; the bitmap's bottom above (+) or below (-) the baseline")
    w("  uint16_t adv;         // pen advance, 1/16 px")
    w("  uint16_t bitmap;      // byte offset into kChipBitmaps")
    w("};")
    w("")
    w("struct ChipKern {")
    w("  char left, right;")
    w("  int8_t value;         // 1/16 px added to the left glyph's advance")
    w("};")
    w("")
    w("constexpr int kChipFontSize = %d;         // source font size, px" % font.size_px)
    w("constexpr int kChipFontLineHeight = %d;   // lv_font_t line_height" % font.line_height)
    w("constexpr int kChipFontBaseLine = %d;      // lv_font_t base_line: baseline above the line's bottom" % font.base_line)
    w("constexpr int kChipFontCapHeight = %d;    // height of a flat capital above the baseline" % cap)
    w("")
    w("constexpr ChipGlyph kChipGlyphs[] = {   // sorted by ch")
    for ch, gid, g, off in glyph_rows:
        w("    {%s, %d, %d, %d, %d, %d, %d}," % (charlit(ch), g["box_w"], g["box_h"], g["ofs_x"], g["ofs_y"], g["adv_w"], off))
    w("};")
    w("constexpr int kChipGlyphCount = %d;" % len(glyph_rows))
    w("")
    w("constexpr ChipKern kChipKerns[] = {   // pairs of the glyphs above with a non-zero value")
    for a, b, v in kerns:
        w("    {%s, %s, %d}," % (charlit(a), charlit(b), v))
    if not kerns:
        w("    {0, 0, 0},")
    w("};")
    w("constexpr int kChipKernCount = %d;" % len(kerns))
    w("")
    w("constexpr uint8_t kChipBitmaps[%d] = {" % max(len(blob), 1))
    for i in range(0, len(blob), 16):
        w("    " + ", ".join("0x%02x" % b for b in blob[i:i + 16]) + ",")
    if not blob:
        w("    0,")
    w("};")
    w("")
    w("}  // namespace ss")
    w("")

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    sys.stdout.write("wrote %s: %d glyphs, %d kerning pairs, %d bitmap bytes, cap height %d\n"
                     % (os.path.relpath(args.out, ROOT).replace("\\", "/"), len(glyph_rows), len(kerns), len(blob), cap))
    # Label widths with the panel's own layout rule, for sizing the chip.
    for lab in labels:
        pen = 0
        for i, ch in enumerate(lab):
            gl = font.gid(ord(ch))
            gr = font.gid(ord(lab[i + 1])) if i + 1 < len(lab) else 0
            kv = font.kern_value(gl, gr) if gr else 0
            pen += (font.glyphs[gl]["adv_w"] + kv + 8) >> 4
        sys.stdout.write("  %-10s pen width %d px\n" % (lab, pen))


if __name__ == "__main__":
    main()
