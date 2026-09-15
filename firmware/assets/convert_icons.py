#!/usr/bin/env python3
"""
Bakes a small custom icon font from Google's Material Icons
(firmware/assets/fonts/MaterialIcons-Regular.ttf, Apache 2.0 - downloaded
from google/material-design-icons on GitHub, same trusted-source pattern
as the Montserrat font used for convert_font.py), for the label-shortening/
"what else were you thinking" pass (2026-09-14 - see waveshare-dash-build.md).

Why this, not hand-drawn icons: this project doesn't have icon design
assets or a designer-drawn icon set, and hand-rolling ~5 recognizable
pictograms (a lightning bolt, a thermostat, a compass, etc.) in code would
either take a very long time to look right or look worse than plain text -
actively working against the "more premium" goal this whole design pass is
for. Material Icons is a large, free, professionally designed icon set
with an exact codepoint-to-name mapping file, so a handful of specific,
well-chosen icons can be extracted precisely instead of guessed at.

Format: same LVGL 8.x "fmt_txt" font format as convert_font.py, but with a
SPARSE cmap instead of a contiguous range - these 5 icon codepoints are
scattered across Material Icons' codepoint space (0xE0C8-0xF076), so
covering that whole span as one contiguous range would waste a glyph_dsc
slot for every one of the ~4000 unused codepoints in between. Sparse
tiny cmap format (LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, see lv_font_fmt_txt.h):
glyph_id = glyph_id_start + index of this codepoint in a sorted
unicode_list - exactly designed for "a few arbitrary codepoints," which is
what this is.

This font's own fallback points at lv_font_montserrat_16 (LVGL's bundled
regular font, which carries the LV_SYMBOL_* glyphs) so the chain built by
convert_font.py - font_montserrat_extrabold_16 -> font_icons_20 ->
lv_font_montserrat_16 - keeps LV_SYMBOL_* glyphs reachable from a 16px
label even after this icon font is spliced into the middle of that chain.
lv_font_get_glyph_dsc's fallback walk (lv_font.c) is a `while(f)` loop, so
multi-hop chains like this resolve correctly - confirmed by reading that
loop before relying on it.

Icons baked (name: Material Icons codepoint -> meaning here):
  bolt          0xEA0B -> generic "this is an electrical quantity"
                          (power, voltage, current panels)
  thermostat    0xF076 -> temperature panels
  location_on   0xE0C8 -> GPS latitude/longitude
  navigation    0xE55D -> GPS heading (compass-style arrow)
  speed         0xE9E4 -> GPS speed panel
Deliberately NOT one icon per distinct electrical unit (power vs voltage
vs current all share "bolt") - a glance-first dashboard needs "this is
electrical" more than three visually-similar bolt variants that would be
hard to tell apart at 20px anyway; the panel's text label still says
exactly which quantity it is.

Usage: python convert_icons.py
"""
from pathlib import Path
from PIL import ImageFont

HERE = Path(__file__).parent
SRC_TTF = HERE / "fonts" / "MaterialIcons-Regular.ttf"
OUT_DIR = HERE.parent / "src"

ICON_SIZE = 20  # slightly larger than the 16px panel-title text it sits
                # inline with, for visual balance - icons read as smaller
                # than the same pixel height of text due to their shapes.
BPP = 4
BPP_LEVELS = (1 << BPP) - 1

# name -> codepoint, in the fixed order used to build glyph ids 1..5.
ICONS = {
    "bolt": 0xEA0B,
    "thermostat": 0xF076,
    "location_on": 0xE0C8,
    "navigation": 0xE55D,
    "speed": 0xE9E4,
}


def quantize(px):
    return round(px / 255 * BPP_LEVELS)


def main():
    font = ImageFont.truetype(str(SRC_TTF), ICON_SIZE)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent

    names = list(ICONS.keys())
    codepoints = [ICONS[n] for n in names]

    glyphs = [dict(adv_w=0, box_w=0, box_h=0, ofs_x=0, ofs_y=0, bits=[])]  # id 0 reserved
    for cp in codepoints:
        ch = chr(cp)
        adv_w = round(font.getlength(ch) * 16)
        mask, (ox, oy) = font.getmask2(ch, mode="L")
        w, h = mask.size
        if w == 0 or h == 0:
            glyphs.append(dict(adv_w=adv_w, box_w=0, box_h=0, ofs_x=0, ofs_y=0, bits=[]))
            continue
        ofs_x = ox
        ofs_y = ascent - (oy + h)
        bits = [quantize(font.getmask2(ch, mode="L")[0].getpixel((x, y)))
                for y in range(h) for x in range(w)]
        glyphs.append(dict(adv_w=adv_w, box_w=w, box_h=h, ofs_x=ofs_x, ofs_y=ofs_y, bits=bits))

    bitmap_bytes = bytearray()
    bit_buf = 0
    bit_cnt = 0
    bitmap_index = []
    for g in glyphs:
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

    # Sparse tiny cmap: range covers min..max codepoint, unicode_list holds
    # the *relative* codepoints (cp - range_start) in ascending order, and
    # glyph_id = glyph_id_start + index-in-unicode_list (search returns the
    # index, per lv_font_fmt_txt.h's "Sparse tiny" formula) - so
    # unicode_list and the glyph_dsc array must be in the SAME sorted
    # order, not the ICONS dict's declaration order.
    range_start = min(codepoints)
    order = sorted(range(len(codepoints)), key=lambda i: codepoints[i])
    sorted_glyphs = [glyphs[0]] + [glyphs[i + 1] for i in order]
    sorted_bitmap_index = [bitmap_index[0]] + [bitmap_index[i + 1] for i in order]
    unicode_list = [codepoints[i] - range_start for i in order]
    icon_name_by_glyph_id = {i + 1: names[order[i]] for i in range(len(order))}

    sym = "font_icons_20"
    out_c = OUT_DIR / f"{sym}.c"
    out_h = OUT_DIR / f"{sym}.h"

    with open(out_c, "w", newline="\n") as f:
        f.write(
            f"// Auto-generated by firmware/assets/convert_icons.py from\n"
            f"// MaterialIcons-Regular.ttf - do not hand-edit.\n"
            f"#include \"{sym}.h\"\n\n"
        )
        f.write("static const uint8_t glyph_bitmap[] = {\n")
        for i in range(0, len(bitmap_bytes), 16):
            chunk = bitmap_bytes[i:i + 16]
            f.write("    " + ",".join(f"0x{x:02X}" for x in chunk) + ",\n")
        f.write("};\n\n")

        f.write("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n")
        for i, g in enumerate(sorted_glyphs):
            bi = sorted_bitmap_index[i]
            name_comment = f" // {icon_name_by_glyph_id[i]}" if i in icon_name_by_glyph_id else ""
            f.write(
                f"    {{.bitmap_index = {bi}, .adv_w = {g['adv_w']}, "
                f".box_w = {g['box_w']}, .box_h = {g['box_h']}, "
                f".ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}},{name_comment}\n"
            )
        f.write("};\n\n")

        f.write(
            f"static const uint16_t unicode_list[] = {{ {', '.join(str(u) for u in unicode_list)} }};\n\n"
            "static const lv_font_fmt_txt_cmap_t cmaps[] = {\n"
            "    {\n"
            f"        .range_start = 0x{range_start:04X}, "
            f".range_length = 0x{(max(codepoints) - range_start + 1):04X},\n"
            "        .glyph_id_start = 1,\n"
            "        .unicode_list = unicode_list, .glyph_id_ofs_list = NULL,\n"
            f"        .list_length = {len(unicode_list)},\n"
            "        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n"
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
            "    .cmap_num = 1,\n"
            "    .bpp = 4,\n"
            "    .kern_classes = 0,\n"
            "    .bitmap_format = 0,\n"
            "    .cache = &cache\n"
            "};\n\n"
            f"const lv_font_t {sym} = {{\n"
            "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
            "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
            f"    .line_height = {line_height},\n"
            f"    .base_line = {base_line},\n"
            "    .subpx = LV_FONT_SUBPX_NONE,\n"
            "    .underline_position = -1,\n"
            "    .underline_thickness = 1,\n"
            "    .dsc = &font_dsc,\n"
            "    .fallback = &lv_font_montserrat_16\n"
            "};\n"
        )

    utf8_defines = []
    for name, cp in ICONS.items():
        utf8 = chr(cp).encode("utf-8")
        literal = "".join(f"\\x{b:02X}" for b in utf8)
        utf8_defines.append(f'#define ICON_{name.upper()} "{literal}"')

    with open(out_h, "w", newline="\n") as f:
        f.write(
            "#pragma once\n\n"
            "// Auto-generated by firmware/assets/convert_icons.py from Google's\n"
            "// Material Icons (Apache 2.0). 5 icons only, sparse cmap - see this\n"
            "// file's .c header comment for the codepoint-to-meaning mapping and\n"
            "// why these specific icons. Use as e.g.\n"
            "//   lv_label_set_text_fmt(label, \"%s POWER\", ICON_BOLT);\n"
            "// (ICON_* is a raw UTF-8 byte-string literal, not a codepoint constant).\n\n"
            "#include \"lvgl.h\"\n\n"
            f"extern const lv_font_t {sym};\n\n"
            "// Icon codepoints, ready to use directly in a format string:\n"
            + "\n".join(utf8_defines) + "\n"
        )

    print(f"Wrote {out_c.name}, {out_h.name}: {len(sorted_glyphs)} glyphs "
          f"({', '.join(names)}), {len(bitmap_bytes)} bytes bitmap data")


if __name__ == "__main__":
    main()
