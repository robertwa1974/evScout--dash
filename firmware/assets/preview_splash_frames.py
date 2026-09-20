#!/usr/bin/env python3
"""
Visual verification harness for convert_splash_frames.py's output - same
discipline as this project's other preview_*.py scripts (a wrong pixel
pack or header bit offset would silently misrender every frame on real
hardware, exactly like preview_font.py's docstring warns about for glyph
offsets - verify before trusting it, don't assume the math is right).

Reads every frame_NN.bin back (parsing the same 4-byte lv_img_header_t
layout convert_splash_frames.py writes, then unpacking RGB565 -> RGB -
the literal inverse of the conversion, so this also double-checks the
packing math independently rather than just re-displaying whatever bytes
were written), and assembles them into one contact-sheet PNG for a quick
look before burning an SD card.

Usage: python preview_splash_frames.py [frames_dir]
(defaults to convert_splash_frames.py's own default output dir)
"""
import struct
import sys
from pathlib import Path

from PIL import Image

import convert_splash_frames as cf

HERE = Path(__file__).parent


def unpack_header(data: bytes):
    (value,) = struct.unpack_from("<I", data, 0)
    color_fmt = value & 0x1F
    w = (value >> 10) & 0x7FF
    h = (value >> 21) & 0x7FF
    return color_fmt, w, h


def decode_frame(path: Path) -> Image.Image:
    data = path.read_bytes()
    color_fmt, w, h = unpack_header(data)
    if color_fmt != cf.LV_IMG_CF_TRUE_COLOR:
        raise ValueError(f"{path.name}: unexpected color format {color_fmt}, expected {cf.LV_IMG_CF_TRUE_COLOR}")

    pixels = data[4:]
    expected = w * h * 2
    if len(pixels) != expected:
        raise ValueError(f"{path.name}: pixel data is {len(pixels)} bytes, expected {expected} for {w}x{h}")

    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        row_off = y * w * 2
        for x in range(w):
            (rgb565,) = struct.unpack_from("<H", pixels, row_off + x * 2)
            r = (rgb565 >> 11) & 0x1F
            g = (rgb565 >> 5) & 0x3F
            b = rgb565 & 0x1F
            # Expand back to 8-bit the same way the 5/6/5 truncation implies
            # (replicate high bits into the dropped low bits) - close enough
            # for a visual check, not meant to be a bit-exact round trip.
            r8 = (r << 3) | (r >> 2)
            g8 = (g << 2) | (g >> 4)
            b8 = (b << 3) | (b >> 2)
            px[x, y] = (r8, g8, b8)
    return im


def main():
    frames_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else (HERE / "splash_frames_out")
    frame_paths = sorted(frames_dir.glob("frame_*.bin"))
    if not frame_paths:
        print(f"error: no frame_*.bin files found in {frames_dir}", file=sys.stderr)
        print("(run convert_splash_frames.py first)", file=sys.stderr)
        sys.exit(1)

    frames = [decode_frame(p) for p in frame_paths]
    w, h = frames[0].size

    cols = 6
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows), (40, 0, 0))  # dark red gaps make missing frames obvious
    for i, im in enumerate(frames):
        x = (i % cols) * w
        y = (i // cols) * h
        sheet.paste(im, (x, y))

    out = HERE / "splash_frames_preview.png"
    sheet.save(out)
    print(f"Decoded {len(frames)} frames ({w}x{h} each) from {frames_dir}")
    print(f"Wrote {out} ({sheet.width}x{sheet.height}) - open it and confirm the truck rotates "
          "smoothly frame-to-frame with no color corruption or misalignment.")


if __name__ == "__main__":
    main()
