#!/usr/bin/env python3
"""
Visual verification harness for convert_turn_icons.py's output - same
discipline as preview_icons.py (parameterized copy, see that script's and
convert_turn_icons.py's header comments for why this project uses one
script per asset rather than shared imports).

Usage: python preview_turn_icons.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

import convert_turn_icons as ct

HERE = Path(__file__).parent


def render():
    font = ImageFont.truetype(str(ct.SRC_TTF), ct.ICON_SIZE)
    ascent, descent = font.getmetrics()

    names = list(ct.ICONS.keys())
    pad = 8
    cell = ct.ICON_SIZE + pad * 2
    img = Image.new("RGB", (cell * len(names), cell + 20), (20, 20, 20))
    draw = ImageDraw.Draw(img)

    for i, name in enumerate(names):
        cp = ct.ICONS[name]
        ch = chr(cp)
        mask, (ox, oy) = font.getmask2(ch, mode="L")
        w, h = mask.size
        ofs_y = ascent - (oy + h)
        gx = i * cell + pad + ox
        gy = ascent - h - ofs_y
        for y in range(h):
            for x in range(w):
                v = mask.getpixel((x, y))
                if v:
                    px = gx + x
                    py = gy + y
                    if 0 <= px < img.width and 0 <= py < img.height:
                        img.putpixel((px, py), (v, v, v))
        draw.text((i * cell + 2, cell + 2), name[:14], fill=(0, 200, 255))
        draw.rectangle([i * cell, 0, i * cell, cell - 1], outline=(60, 60, 60))

    out = HERE / "turn_icon_preview.png"
    img.save(out)
    print(f"Wrote {out} ({img.width}x{img.height})")


if __name__ == "__main__":
    render()
