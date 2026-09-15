#!/usr/bin/env python3
"""
Visual verification harness for convert_icons.py's output - same discipline
as preview_font.py: re-implements LVGL's glyph-positioning formula in
Python against the exact in-memory glyph data the converter produces, and
renders each icon to a PNG so it can be eyeballed before ever touching a
screen file or flashing hardware.

Usage: python preview_icons.py
"""
from pathlib import Path
from PIL import Image, ImageDraw

from convert_icons import ICONS, ICON_SIZE, main as build_icons  # noqa: E402
import convert_icons

HERE = Path(__file__).parent


def render():
    # Re-derive the same glyph data convert_icons.main() computes, without
    # re-running the file-writing side effects.
    from PIL import ImageFont
    font = ImageFont.truetype(str(convert_icons.SRC_TTF), ICON_SIZE)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    names = list(ICONS.keys())
    pad = 8
    cell = ICON_SIZE + pad * 2
    img = Image.new("RGB", (cell * len(names), cell + 20), (20, 20, 20))
    draw = ImageDraw.Draw(img)

    for i, name in enumerate(names):
        cp = ICONS[name]
        ch = chr(cp)
        mask, (ox, oy) = font.getmask2(ch, mode="L")
        w, h = mask.size
        ofs_y = ascent - (oy + h)
        # LVGL's formula: gy = line_top + ascent - box_h - ofs_y
        line_top = i // 999 * cell  # single row
        gx = i * cell + pad + ox
        gy = line_top + ascent - h - ofs_y
        for y in range(h):
            for x in range(w):
                v = mask.getpixel((x, y))
                if v:
                    px = gx + x
                    py = gy + y
                    if 0 <= px < img.width and 0 <= py < img.height:
                        img.putpixel((px, py), (v, v, v))
        draw.text((i * cell + 2, cell + 2), name[:8], fill=(0, 200, 255))
        draw.rectangle([i * cell, 0, i * cell, cell - 1], outline=(60, 60, 60))

    out = HERE / "icon_preview.png"
    img.save(out)
    print(f"Wrote {out} ({img.width}x{img.height})")


if __name__ == "__main__":
    render()
