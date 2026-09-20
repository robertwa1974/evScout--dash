#!/usr/bin/env python3
"""
convert_splash_frames.py - splash-screen "truck revolve" frame converter

Converts a folder of full-resolution turntable-render PNGs (source: 22
frames, ~1900x1070px each, ~2MB each / ~44MB total) into LVGL 8.4 raw
RGB565 binary image files, one per frame, sized for the microSD card - NOT
compiled into firmware (this project's flash partition is fixed at
app3M_fat9M_16MB and animation assets deliberately do not go there; see
ui_splash_truck.h for the runtime side that loads these from SD into
PSRAM).

Why raw binary output, not a C array (unlike this project's other asset
scripts - convert_font.py, convert_icons.py, convert_logo.py - which all
emit .c/.h for compile-time linking): those assets are small and static;
22 frames at any usable resolution would bloat the firmware binary and
can't benefit from PSRAM the way an SD-loaded runtime asset can. LVGL's
built-in image decoder (lv_img_decoder_built_in_open/read_line,
lv_img_decoder.c) supports loading TRUE_COLOR images directly from a
".bin" file through a registered lv_fs driver - no C array needed, and no
"LVGL image converter tool" dependency either: the binary layout is just
LVGL's own lv_img_dsc_t header serialized as raw bytes, which is simple
and stable enough to reproduce directly (see HEADER FORMAT below) rather
than pull in an extra conversion tool as a script dependency.

CANVAS / RESIZE:
Each source frame is downscaled (preserving aspect ratio - these are
~16:9-ish renders, not square) to fit within a --size x --size square,
then centered on a solid black canvas. No alpha/transparency is written -
confirmed against the actual source PNGs before writing this (every
sampled frame's alpha channel is a flat 255) - so the output is plain
LV_IMG_CF_TRUE_COLOR, 2 bytes/pixel, not TRUE_COLOR_ALPHA's 3.

The source PNGs' own studio floor/backdrop is near-black but not truly
(0,0,0) - a subtle dark-grey vignette (sampled 13-41 per channel at the
corners/edges of the real source frames, 2026-09-20). That looked fine in
a normal image viewer but showed up as an obvious rectangle once actually
displayed against this project's genuinely pure-black splash screen on
real hardware - see crush_near_black() for the fix (flattens anything at
or below --bg-threshold to true black before resizing).

HEADER FORMAT (LVGL 8.4's lv_img_header_t, from lv_img_buf.h - verified
directly against the vendored copy in this project's .pio/libdeps, not
assumed from memory or docs, since this exact bitfield layout is also
endian-dependent and the header comment there warns about that):
  a little-endian uint32:
    bits  0-4  : cf (color format) = 4 for LV_IMG_CF_TRUE_COLOR
    bits  5-7  : always_zero = 0
    bits  8-9  : reserved = 0
    bits 10-20 : w (11 bits)
    bits 21-31 : h (11 bits)
  followed immediately by w*h*2 bytes of RGB565 pixel data, row-major,
  each pixel packed exactly as this project's other RGB565 conversion
  (see convert_logo.py) already does: little-endian uint16,
  ((r>>3)<<11)|((g>>2)<<5)|(b>>3) - confirmed against this project's
  actual LVGL build config, LV_COLOR_DEPTH=16 / LV_COLOR_16_SWAP=0.
This is the exact byte layout LVGL's own built-in image decoder expects
when it reads a ".bin" file's first 4 bytes as the header (see
lv_img_decoder_built_in_info() in lv_img_decoder.c) - there is no extra
magic number or file-level metadata beyond this.

USAGE:
    pip install Pillow
    python convert_splash_frames.py <input_dir> [-o OUTPUT_DIR] [--size N]

    Example, against the actual source screenshots on this machine:
    python convert_splash_frames.py "C:/Users/rober/Desktop/scout" -o frame_out

Input frames are sorted by filename (the source screenshots are
timestamp-named, e.g. "Screenshot 2026-09-19 071911.png" - lexicographic
sort on that filename is also chronological order, which is turntable
order) and renumbered sequentially frame_00.bin .. frame_21.bin - the
input filenames themselves never need to already be sequential.

Output goes to firmware/assets/splash_frames_out/ by default (a working
copy for you to copy onto the SD card's /splash_frames/ directory - see
ui_splash_truck.h - this script does NOT write to the SD card directly,
it has no way to know if/where one is mounted on this machine).
"""
import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

LV_IMG_CF_TRUE_COLOR = 4
DEFAULT_SIZE = 320
FRAME_COUNT_EXPECTED = 22  # informational only - the script still runs with a different count, just warns


def pack_header(w: int, h: int) -> bytes:
    if w >= 2048 or h >= 2048:
        raise ValueError(f"{w}x{h} exceeds lv_img_header_t's 11-bit w/h field (max 2047)")
    cf = LV_IMG_CF_TRUE_COLOR
    always_zero = 0
    reserved = 0
    value = (cf & 0x1F) | ((always_zero & 0x7) << 5) | ((reserved & 0x3) << 8) | ((w & 0x7FF) << 10) | ((h & 0x7FF) << 21)
    return struct.pack("<I", value)


def rgb565_bytes(im: Image.Image) -> bytes:
    """im must already be mode 'RGB' at its final output size."""
    w, h = im.size
    px = im.load()
    out = bytearray(w * h * 2)
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            struct.pack_into("<H", out, i, rgb565)
            i += 2
    return bytes(out)


def crush_near_black(im: Image.Image, threshold: int) -> Image.Image:
    """Flattens near-black pixels to pure (0,0,0). Added 2026-09-20: this
    project's source PNGs' own studio floor/backdrop is a subtle dark-grey
    vignette (sampled 13-41 per channel at the corners/edges of the real
    source frames, not a flat color), not true black despite looking that
    way in a normal image viewer - confirmed visually WRONG on real
    hardware, where it showed up as an obvious rectangle against the
    splash screen's genuinely pure-black (0,0,0) background. threshold=48
    (the default - see --bg-threshold) clears that whole sampled range
    with margin while staying well below the truck's own painted/chrome
    surfaces, so only background gets crushed, not real content."""
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r <= threshold and g <= threshold and b <= threshold:
                px[x, y] = (0, 0, 0)
    return im


def resize_and_center(im: Image.Image, size: int, bg_threshold: int) -> Image.Image:
    """Fit im within size x size (preserving aspect ratio), centered on a
    solid black square canvas - never crops, may letterbox/pillarbox.
    Crushes near-black background to pure black first (see
    crush_near_black()) so the source's own dark backdrop blends into the
    letterbox bars instead of showing up as a visible rectangle."""
    im = im.convert("RGB")
    im = crush_near_black(im, bg_threshold)
    src_w, src_h = im.size
    scale = min(size / src_w, size / src_h)
    new_w = max(1, round(src_w * scale))
    new_h = max(1, round(src_h * scale))
    resized = im.resize((new_w, new_h), Image.LANCZOS)

    canvas = Image.new("RGB", (size, size), (0, 0, 0))
    canvas.paste(resized, ((size - new_w) // 2, (size - new_h) // 2))
    return canvas


def main():
    ap = argparse.ArgumentParser(description="Convert splash-screen truck-revolve PNGs to LVGL raw RGB565 .bin frames")
    ap.add_argument("input_dir", type=Path, help="Folder containing the source PNG frames")
    ap.add_argument("-o", "--output-dir", type=Path, default=Path(__file__).parent / "splash_frames_out",
                     help="Where to write frame_00.bin.. (default: %(default)s)")
    ap.add_argument("--size", type=int, default=DEFAULT_SIZE,
                     help="Square canvas size in pixels (default: %(default)s)")
    ap.add_argument("--bg-threshold", type=int, default=48,
                     help="Per-channel brightness at/below which a pixel is treated as background "
                          "and crushed to pure black (default: %(default)s) - see crush_near_black()'s "
                          "docstring for why this exists")
    args = ap.parse_args()

    if not args.input_dir.is_dir():
        print(f"error: {args.input_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    sources = sorted(args.input_dir.glob("*.png"))
    if not sources:
        print(f"error: no .png files found in {args.input_dir}", file=sys.stderr)
        sys.exit(1)
    if len(sources) != FRAME_COUNT_EXPECTED:
        print(f"warning: found {len(sources)} PNGs, expected {FRAME_COUNT_EXPECTED} - continuing anyway "
              f"(frame_00.bin..frame_{len(sources)-1:02d}.bin will be written)")

    args.output_dir.mkdir(parents=True, exist_ok=True)

    sizes = []
    for i, src_path in enumerate(sources):
        im = Image.open(src_path)
        canvas = resize_and_center(im, args.size, args.bg_threshold)
        pixel_data = rgb565_bytes(canvas)
        header = pack_header(args.size, args.size)

        out_path = args.output_dir / f"frame_{i:02d}.bin"
        out_path.write_bytes(header + pixel_data)
        sizes.append((out_path.name, len(header) + len(pixel_data), src_path.name))

    total = sum(n for _, n, _ in sizes)
    print(f"\n{len(sizes)} frames converted, canvas {args.size}x{args.size}, output: {args.output_dir}\n")
    for name, n, src_name in sizes:
        print(f"  {name:<16s} {n:>8,d} bytes   <- {src_name}")
    print(f"\nTotal: {total:,} bytes ({total / (1024*1024):.2f} MiB)")
    if total > 8 * 1024 * 1024:
        print("WARNING: total exceeds 8 MiB - check this fits the target board's actual PSRAM size "
              "before relying on the full-preload path (see ui_splash_truck.h's per-frame SD fallback "
              "for what happens if it doesn't).")
    else:
        print("(Comfortably under a typical PSRAM budget - see ui_splash_truck.h's preload for the "
              "actual on-device confirmation via heap_caps_get_free_size logging.)")

    print(f"\nNext step: copy every file in {args.output_dir} onto the microSD card at "
          f"/splash_frames/ (see ui_splash_truck.h's SPLASH_FRAMES_DIR).")


if __name__ == "__main__":
    main()
