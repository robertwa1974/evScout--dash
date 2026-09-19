#!/usr/bin/env python3
"""
Verification harness for convert_font.py - re-implements LVGL's own glyph
positioning/unpacking logic (lv_draw_sw_letter.c) in Python against the
SAME in-memory glyph data convert_font.py builds, and renders a test
string to a PNG. This is a self-consistency check on the bit-packing/
offset math before trusting it enough to wire into ~30 call sites across
the firmware - a wrong offset would misalign every character on every
screen, and that's not something worth discovering only after flashing.
"""
import sys
from pathlib import Path
from PIL import Image, ImageFont

sys.path.insert(0, str(Path(__file__).parent))
from convert_font import build_size, RANGES, BPP, BPP_LEVELS  # noqa: E402

TEST_STRING = "SPEED 88.4 kW"


def glyph_id_for(cp):
    gid = 1
    for lo, hi in RANGES:
        if lo <= cp <= hi:
            return gid + (cp - lo)
        gid += (hi - lo + 1)
    return None


def render(size, weight_name, out_path):
    data = build_size(size, weight_name)
    glyphs = data["glyphs"]
    line_height = data["line_height"]
    ascent = data["ascent"]

    canvas_w = 900
    canvas_h = line_height + 20
    img = Image.new("L", (canvas_w, canvas_h), color=0)

    pen_x = 5
    line_top = 10
    for ch in TEST_STRING:
        cp = ord(ch)
        gid = glyph_id_for(cp)
        g = glyphs[gid] if gid else glyphs[0]
        if g["box_w"] and g["box_h"]:
            # Same formula as lv_draw_sw_letter.c:
            #   gpos.y = line_top + (line_height - base_line) - box_h - ofs_y
            gy = line_top + ascent - g["box_h"] - g["ofs_y"]
            gx = pen_x + g["ofs_x"]
            for row in range(g["box_h"]):
                for col in range(g["box_w"]):
                    level = g["bits"][row * g["box_w"] + col]
                    val = round(level / BPP_LEVELS * 255)
                    px, py = gx + col, gy + row
                    if 0 <= px < canvas_w and 0 <= py < canvas_h:
                        existing = img.getpixel((px, py))
                        img.putpixel((px, py), max(existing, val))
        pen_x += round(g["adv_w"] / 16)

    img.save(out_path)
    print(f"{size}px -> {out_path} (line_height={line_height}, ascent={ascent}, "
          f"rendered width={pen_x}px)")


if __name__ == "__main__":
    out_dir = Path(__file__).parent
    for size in (16, 24, 32, 48):
        render(size, "ExtraBold", out_dir / f"preview_extrabold_{size}.png")
    # SemiBold only baked at the two label-role sizes - see convert_font.py's WEIGHTS.
    for size in (16, 24):
        render(size, "SemiBold", out_dir / f"preview_semibold_{size}.png")
