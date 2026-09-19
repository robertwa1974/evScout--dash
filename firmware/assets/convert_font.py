#!/usr/bin/env python3
"""
Bakes real bold-weight bitmap fonts for this project from Google's
Montserrat variable font (firmware/assets/fonts/Montserrat-Variable.ttf,
SIL Open Font License - freely redistributable, downloaded from Google's
own github.com/google/fonts repo), for the "much bolder/chunkier fonts
throughout" design pass (2026-09-14 - see waveshare-dash-build.md).

Why hand-rolled instead of LVGL's own lv_font_conv tool: that's a Node.js
CLI and Node isn't available in this environment. LVGL's bundled
Montserrat fonts (lv_font_montserrat_16/24/32/48, currently used
everywhere) are REGULAR weight only - there is no runtime "make it bolder"
style property for a bitmap font, so getting real bold weight means baking
new bitmap fonts from an actual bold TTF. This script does that directly
with Pillow (already a dependency for the logo conversion), generating
LVGL 8.x's native "fmt_txt" font format.

Weight: ExtraBold (800) for numeric values/hero readouts - "Bold" (700)
didn't feel like enough of a change for the "much bolder, chunkier" ask,
and Black (900) risks the letterforms blobbing together at 16px. This is a
judgment call that can't be verified without seeing it on the actual
screen - easy to regenerate at a different weight (edit the WEIGHTS list
below) if it needs adjusting either direction.

SemiBold (600) added 2026-09-18 for card labels/titles specifically (the
styling pass's "bold reserved for values only" rule) - visibly lighter
than ExtraBold while still reading as part of the same bold-leaning family
this project deliberately moved to, not a jump all the way to Regular
(400), which felt like too big a contrast drop from the "bolder, for
readability" direction. Confirmed "SemiBold" is a real named instance in
Montserrat-Variable.ttf's variation-font axis (checked via
ImageFont.get_variation_names() before assuming the string would work -
Thin/ExtraLight/Light/Regular/Medium/SemiBold/Bold/ExtraBold/Black are all
present). Only baked at 16/24px - the two label-role tiers; 32/48px are
numeric-hero-value-only across every screen, so there's no call site that
would ever use a lighter weight at those sizes.

Format details - verified against this project's own vendored LVGL source
before writing a single byte, not assumed (a subtly wrong offset here
would misalign every character on every screen):
  - Bit-packing: lv_draw_sw_letter.c's width_bit/bit_ofs math confirms
    rows are packed CONTIGUOUSLY at the bit level (bit_ofs = row*box_w*bpp
    + col*bpp) - no per-row byte padding, unlike some other bitmap
    formats. Bits are read MSB-first per byte.
  - bpp=4 (16 opacity levels via _lv_bpp4_opa_table, level*17 ~= level/15
    * 255) - matches what LVGL's own bundled fonts typically use, a good
    antialiasing/size tradeoff.
  - ofs_y convention reverse-engineered from lv_draw_sw_letter.c's actual
    draw-position formula (gpos.y = line_top + (line_height - base_line)
    - box_h - ofs_y), NOT from the header comments, which are actually
    self-contradictory between lv_font.h ("base_line measured from the
    top") and the real generated font files' own comment ("measured from
    the bottom") - the code is the only source of truth here. Verified
    empirically too: a flat-bottomed capital ('A') computes ofs_y=0
    (sits exactly on the baseline) at every tested size, confirming the
    derived formula before trusting it for all ~380 glyphs generated.
  - No kerning table - LVGL will just use each glyph's plain advance
    width between letter pairs. A minor spacing refinement being skipped,
    not a correctness issue.
  - Icon glyphs (LV_SYMBOL_BATTERY_FULL etc, added in the earlier icons
    pass) are NOT included in these bold fonts - only ASCII 0x20-0x7E plus
    the degree sign (0xB0, used for temperature units). Each generated
    font's `.fallback` points at the original REGULAR lv_font_montserrat_N
    of the same size, so LVGL automatically falls back to the regular
    font's symbol glyphs for any codepoint these bold fonts don't cover -
    no FontAwesome asset needed.
  - The 16px size additionally chains through the custom icon font
    (firmware/assets/convert_icons.py's font_icons_20, 5 Material Icons
    glyphs) BEFORE falling back to lv_font_montserrat_16: 16px is the size
    every panel title uses, and lv_font_get_glyph_dsc's fallback walk (see
    lv_font.c) is a `while(f)` loop, not a single hop - so chaining
    extrabold_16 -> font_icons_20 -> lv_font_montserrat_16 lets one
    lv_label mix bold text, a custom icon, AND an LV_SYMBOL_* glyph, with
    no new widget and no per-panel layout change. Confirmed by reading
    lv_font.c before relying on it, same as every other format detail here.

Usage: python convert_font.py
(Run manually when the source TTF or WEIGHT_NAME changes - not part of
the PlatformIO build, same as this project's other asset conversions.)
"""
from pathlib import Path
from PIL import ImageFont

HERE = Path(__file__).parent
SRC_TTF = HERE / "fonts" / "Montserrat-Variable.ttf"
OUT_DIR = HERE.parent / "src"

SIZES = [16, 24, 32, 48]
BPP = 4
BPP_LEVELS = (1 << BPP) - 1  # 15 for bpp=4

# Two cmap ranges: printable ASCII, and the degree sign (used for temp units).
RANGES = [(0x20, 0x7E), (0xB0, 0xB0)]

# Sizes whose fallback chains through the custom icon font first (see
# convert_icons.py) before reaching the regular-weight Montserrat fallback.
# Only 16px is used for panel titles, the only place icons are used so far -
# applies at any weight (both ExtraBold and SemiBold 16px chain to icons),
# since either can be used for a panel title/label.
ICON_FONT_SIZES = {16}
ICON_FONT_SYM = "font_icons_20"

# (weight name, sizes to bake, output symbol prefix). ExtraBold (800) is the
# original "much bolder/chunkier" pass (2026-09-14, see this file's header
# comment on why 800 not 700/900). SemiBold (600) is the label/card-title
# weight added 2026-09-18 (styling pass, item 1: bold reserved for numeric
# values only, labels get a visibly lighter but still-bold-family weight -
# Regular/400 was considered and rejected as too big a jump from this
# project's established "bolder, for readability" direction, see CLAUDE.md).
# Only 16/24px, since those are the only label-role tiers (32/48px are
# numeric-hero-value-only, per the per-screen font-usage audit) - no need
# to bake sizes nothing will ever set.
WEIGHTS = [
    ("ExtraBold", SIZES, "font_montserrat_extrabold"),
    ("SemiBold", [16, 24], "font_montserrat_semibold"),
]


def quantize(px):
    return round(px / 255 * BPP_LEVELS)


def build_size(size, weight_name):
    font = ImageFont.truetype(str(SRC_TTF), size)
    font.set_variation_by_name(weight_name)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    glyphs = []  # list of dicts: adv_w, box_w, box_h, ofs_x, ofs_y, bits (list of 0..15)
    # id 0: reserved placeholder, matches LVGL's own convention.
    glyphs.append(dict(adv_w=0, box_w=0, box_h=0, ofs_x=0, ofs_y=0, bits=[]))

    codepoints = []
    for lo, hi in RANGES:
        codepoints.extend(range(lo, hi + 1))

    for cp in codepoints:
        ch = chr(cp)
        adv_w = round(font.getlength(ch) * 16)  # 8.4 fixed point
        mask, (ox, oy) = font.getmask2(ch, mode="L")
        w, h = mask.size
        if w == 0 or h == 0:
            glyphs.append(dict(adv_w=adv_w, box_w=0, box_h=0, ofs_x=0, ofs_y=0, bits=[]))
            continue
        ofs_x = ox
        ofs_y = ascent - (oy + h)
        # getmask2 returns a raw ImagingCore (no .getdata()/.tobytes()) -
        # read pixels individually via .getpixel(), row-major to match the
        # bit-packing order below.
        px = [mask.getpixel((x, y)) for y in range(h) for x in range(w)]
        bits = [quantize(p) for p in px]
        glyphs.append(dict(adv_w=adv_w, box_w=w, box_h=h, ofs_x=ofs_x, ofs_y=ofs_y, bits=bits))

    # Pack all glyph bitmaps into one contiguous bitstream (MSB-first,
    # BPP bits/level, no row padding - see header comment).
    bitmap_bytes = bytearray()
    bit_buf = 0
    bit_cnt = 0
    bitmap_index = []  # byte offset (into bitmap_bytes) where each glyph's data starts
    for g in glyphs:
        # A glyph must start byte-aligned so its bitmap_index is a clean
        # byte offset - flush any partial byte from the previous glyph
        # first (LVGL's own converter does the same; a glyph's bit_ofs
        # formula assumes it starts its own bitstream at bit 0 of
        # bitmap[bitmap_index]).
        if bit_cnt:
            bitmap_bytes.append((bit_buf << (8 - bit_cnt)) & 0xFF)
            bit_buf = 0
            bit_cnt = 0
        bitmap_index.append(len(bitmap_bytes))
        for level in g["bits"]:
            bit_buf = (bit_buf << BPP) | level
            bit_cnt += BPP
            while bit_cnt >= 8:
                bit_cnt -= 8
                bitmap_bytes.append((bit_buf >> bit_cnt) & 0xFF)
                bit_buf &= (1 << bit_cnt) - 1
    if bit_cnt:
        bitmap_bytes.append((bit_buf << (8 - bit_cnt)) & 0xFF)

    return dict(
        font=font, ascent=ascent, descent=descent, line_height=line_height,
        base_line=base_line, glyphs=glyphs, bitmap_index=bitmap_index,
        bitmap_bytes=bitmap_bytes,
    )


def emit(size, data, sym, weight_name):
    glyphs = data["glyphs"]
    bidx = data["bitmap_index"]
    out_c = OUT_DIR / f"{sym}.c"
    out_h = OUT_DIR / f"{sym}.h"

    chain_icons = size in ICON_FONT_SIZES

    with open(out_c, "w", newline="\n") as f:
        f.write(
            f"// Auto-generated by firmware/assets/convert_font.py from\n"
            f"// Montserrat-Variable.ttf ({weight_name} weight) - do not hand-edit.\n"
            f"// Re-run the script if the weight or source font changes.\n"
            f"#include \"{sym}.h\"\n"
        )
        if chain_icons:
            f.write(f"#include \"{ICON_FONT_SYM}.h\"\n")
        f.write("\n")
        f.write(f"static const uint8_t glyph_bitmap[] = {{\n")
        b = data["bitmap_bytes"]
        for i in range(0, len(b), 16):
            chunk = b[i:i + 16]
            f.write("    " + ",".join(f"0x{x:02X}" for x in chunk) + ",\n")
        f.write("};\n\n")

        f.write(f"static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{\n")
        for i, g in enumerate(glyphs):
            bi = bidx[i] if i < len(bidx) else 0
            f.write(
                f"    {{.bitmap_index = {bi}, .adv_w = {g['adv_w']}, "
                f".box_w = {g['box_w']}, .box_h = {g['box_h']}, "
                f".ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}},\n"
            )
        f.write("};\n\n")

        # Two ranges -> two FORMAT0_TINY cmaps (sequential, no sparse list
        # needed since both ranges are fully populated with no gaps).
        f.write(
            "static const lv_font_fmt_txt_cmap_t cmaps[] = {\n"
            "    {\n"
            f"        .range_start = 0x20, .range_length = {0x7E - 0x20 + 1},\n"
            "        .glyph_id_start = 1,\n"
            "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n"
            "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n"
            "    },\n"
            "    {\n"
            f"        .range_start = 0xB0, .range_length = 1,\n"
            f"        .glyph_id_start = {0x7E - 0x20 + 1 + 1},\n"
            "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0,\n"
            "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n"
            "    }\n"
            "};\n\n"
        )

        f.write(
            "static lv_font_fmt_txt_glyph_cache_t cache;\n\n"
            "static const lv_font_fmt_txt_dsc_t font_dsc = {\n"
            "    .glyph_bitmap = glyph_bitmap,\n"
            "    .glyph_dsc = glyph_dsc,\n"
            "    .cmaps = cmaps,\n"
            "    .kern_dsc = NULL,\n"
            "    .kern_scale = 0,\n"
            "    .cmap_num = 2,\n"
            "    .bpp = 4,\n"
            "    .kern_classes = 0,\n"
            "    .bitmap_format = 0,\n"
            "    .cache = &cache\n"
            "};\n\n"
            f"const lv_font_t {sym} = {{\n"
            "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
            "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
            f"    .line_height = {data['line_height']},\n"
            f"    .base_line = {data['base_line']},\n"
            "    .subpx = LV_FONT_SUBPX_NONE,\n"
            "    .underline_position = -1,\n"
            "    .underline_thickness = 1,\n"
            "    .dsc = &font_dsc,\n"
            f"    .fallback = &{ICON_FONT_SYM if chain_icons else f'lv_font_montserrat_{size}'}\n"
            "};\n"
        )

    with open(out_h, "w", newline="\n") as f:
        f.write(
            "#pragma once\n\n"
            f"// Auto-generated by firmware/assets/convert_font.py - {weight_name}-weight\n"
            f"// Montserrat at {size}px, baked as a real bold bitmap font (LVGL's bundled\n"
            "// Montserrat fonts are regular-weight only). ASCII 0x20-0x7E + degree sign\n"
            "// (0xB0) only - falls back to the regular-weight font of the same size for\n"
            "// any other codepoint (the LV_SYMBOL_* icons), see this file's .c header\n"
            "// comment for why that needs no extra icon-font asset.\n\n"
            "#include \"lvgl.h\"\n\n"
            f"extern const lv_font_t {sym};\n"
        )

    print(f"Wrote {out_c.name}, {out_h.name}: {len(glyphs)} glyphs, "
          f"{len(data['bitmap_bytes'])} bytes bitmap data")


def main():
    for weight_name, sizes, sym_prefix in WEIGHTS:
        for size in sizes:
            data = build_size(size, weight_name)
            emit(size, data, f"{sym_prefix}_{size}", weight_name)


if __name__ == "__main__":
    main()
