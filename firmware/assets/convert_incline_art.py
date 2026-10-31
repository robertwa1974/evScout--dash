#!/usr/bin/env python3
"""
convert_incline_art.py - Incline screen Scout wireframes (2026-10-29)

Draws the two Incline screen views of the truck - side (nose right) and
rear (driver's left on screen left, matching how roll is applied) - as clean line art, and writes them as compact two-tone
coverage masks compiled into the firmware (firmware/src/img_incline_scout.c/.h).
Style "D2": silhouette, tyres and bumpers in the OUTLINE tone (bright),
windows, grille, lights, rims, door etc. in the DETAIL tone (dimmer).

The shapes are hand-traced: every coordinate below was measured off a grid
laid over renders of the actual truck (incline_src/scout_side.png and
scout_rear.png - kept out of git), in those renders' pixel space. Nothing
is copied from the renders' pixels; they're only the measuring reference.
An automatic edge-trace of the renders was tried first and rejected - rust,
grime and reflections turned into broken lines and specks.

The Incline screen expands each mask at runtime into an
LV_IMG_CF_TRUE_COLOR_ALPHA buffer in PSRAM in the current palette's colours
(outline = text primary, detail = text secondary) and rotates it with
lv_img_set_angle(). LVGL 8.4's software transform only rotates TRUE_COLOR*/
RGB565A8 images, so the colours can't stay a recolourable A8 mask.

Mask format: one byte per pixel, (outline << 4) | detail, each 0-15,
outline + detail <= 15 = alpha. Lines are drawn at SS x supersampling and
box-downscaled, so edges are antialiased.

Usage: python convert_incline_art.py [--preview]
--preview also writes incline_art_preview.png (day + night, a few angles).
Needs Pillow + numpy.

Editing shapes: add/move points in side()/rear() (render coordinates), run
with --preview, check the preview, then rebuild. Line weights are WIDTHS.
"""
import argparse
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).parent
OUT_C = HERE.parent / "src" / "img_incline_scout.c"
OUT_H = HERE.parent / "src" / "img_incline_scout.h"
PREVIEW = HERE / "incline_art_preview.png"

SS = 4                 # supersampling factor
WIDTHS = (2.0, 1.2)    # on-screen line widths (px): outline, detail
PAD = 2                # transparent margin (on-screen px) so strokes aren't clipped


# --- shape helpers (PIL angles: degrees clockwise from 3 o'clock) ------------

def arc_pts(cx, cy, r, a0, a1, n=24):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def rrect(x0, y0, x1, y1, r, n=6):
    p = arc_pts(x1 - r, y0 + r, r, 270, 360, n) + arc_pts(x1 - r, y1 - r, r, 0, 90, n) + \
        arc_pts(x0 + r, y1 - r, r, 90, 180, n) + arc_pts(x0 + r, y0 + r, r, 180, 270, n)
    return p + [p[0]]


def circle(cx, cy, r):
    return arc_pts(cx, cy, r, 0, 360, 36)


# --- the truck, in render pixel coordinates -----------------------------------

def side():
    rw, fw = (530, 790), (1590, 782)                      # wheel centres
    body = [(182, 700), (182, 445), (200, 410), (235, 400), (1300, 400), (1320, 392),
            (1700, 398), (1765, 425), (1790, 470), (1790, 640)]
    body += arc_pts(fw[0], fw[1], 245, 325, 200)          # front arch, front -> back
    body += [(1355, 740), (760, 740)]                     # sill
    body += arc_pts(rw[0], rw[1], 245, 340, 200)          # rear arch
    body += [(245, 700), (182, 700)]
    top = [(235, 400), (216, 220), (238, 160), (1240, 163), (1263, 205), (1300, 400)]  # hardtop
    return {
        "outline": [body, top, circle(*rw, 180), circle(*fw, 178)],
        "detail": [
            rrect(340, 225, 800, 375, 25),                                     # rear quarter window
            [(890, 232), (1178, 222), (1284, 398), (890, 398), (890, 232)],    # door glass
            [(862, 405), (862, 655), (895, 690), (1300, 690), (1306, 405)],    # door
            [(885, 455), (965, 455)],                                          # handle
            circle(655, 505, 20),                                              # fuel cap
            circle(*rw, 100), circle(*fw, 100), circle(*rw, 24), circle(*fw, 24),  # rims, hubs
            rrect(250, 522, 305, 540, 3), rrect(1715, 508, 1765, 525, 3),      # marker lights
            circle(1803, 495, 18),                                             # headlight
            rrect(1790, 585, 1847, 665, 6), [(1815, 515), (1815, 585)],        # push bar
            rrect(120, 645, 192, 720, 6),                                      # rear bumper
        ],
    }


def rear():
    # Seen from behind: the driver's left is on screen left, matching how
    # the Incline screen applies roll (a front view mirrored it - 2026-10-30).
    body = [(600, 745), (600, 445)] + arc_pts(640, 445, 40, 180, 270, 6) + \
           arc_pts(1350, 445, 40, 270, 360, 6) + [(1390, 745)]
    sill_l, sill_r = [(600, 745), (660, 745)], [(1335, 745), (1390, 745)]
    cab = [(640, 405), (640, 185), (650, 182), (662, 133), (1326, 133), (1338, 182), (1348, 185), (1348, 405)]
    tyre_l = [(600, 630), (572, 630), (572, 925)] + arc_pts(610, 927, 38, 180, 90, 6) + \
             [(689, 965)] + arc_pts(689, 927, 38, 90, 0, 6) + [(727, 750)]
    tyre_r = [(1225, 750)] + arc_pts(1263, 927, 38, 180, 90, 6) + [(1362, 965)] + \
             arc_pts(1362, 927, 38, 90, 0, 6) + [(1400, 630), (1390, 630)]
    return {
        "outline": [body, sill_l, sill_r, cab, tyre_l, tyre_r, rrect(660, 710, 1335, 750, 6)],  # + bumper
        "detail": [
            [(745, 185), (745, 405)], [(1245, 185), (1245, 405)],             # liftgate seams
            rrect(778, 207, 1212, 348, 20),                                    # rear window
            [(965, 373), (1025, 373)],                                         # liftgate handle
            [(738, 133), (738, 116), (1232, 116), (1232, 133)],                # roof rack
            rrect(606, 355, 636, 405, 5), rrect(1354, 355, 1384, 405, 5),      # mirrors
            rrect(748, 405, 1243, 710, 6), rrect(758, 445, 1233, 665, 6),      # tailgate + panel
            circle(673, 478, 38), circle(1317, 478, 38),                       # taillights
            circle(684, 665, 22), circle(1307, 663, 22),                       # reflectors
            [(727, 780), (1225, 780)], circle(965, 790, 35),                   # axle + diff
        ],
    }


VIEWS = [("side", side, 180), ("rear", rear, 120)]   # name, shapes, drawing width (px)


# --- rasterise -------------------------------------------------------------------

def rasterise(shapes, target_w):
    pts = [p for k in ("outline", "detail") for s in shapes[k] for p in s]
    x0, y0 = min(p[0] for p in pts), min(p[1] for p in pts)
    x1, y1 = max(p[0] for p in pts), max(p[1] for p in pts)
    s = target_w / (x1 - x0)
    w = target_w + 2 * PAD
    h = round((y1 - y0) * s) + 2 * PAD
    tf = lambda poly: [(((x - x0) * s + PAD) * SS, ((y - y0) * s + PAD) * SS) for x, y in poly]

    layers = {}
    for kind, wpx in zip(("outline", "detail"), WIDTHS):
        im = Image.new("L", (w * SS, h * SS), 0)
        d = ImageDraw.Draw(im)
        for poly in shapes[kind]:
            d.line(tf(poly), fill=255, width=max(1, round(wpx * SS)), joint="curve")
        layers[kind] = np.asarray(im.resize((w, h), Image.BOX)).astype(np.float64) / 255.0
    return w, h, layers["outline"], layers["detail"]


def pack(cov_o, cov_d):
    # Outline sits on top: detail only fills what the outline leaves.
    qo = np.round(cov_o * 15).astype(int)
    qd = np.round(cov_d * (1 - cov_o) * 15).astype(int)
    qd = np.minimum(qd, 15 - qo)
    return ((qo << 4) | qd).astype(np.uint8)


def write_c(results):
    lines = ["// Generated by firmware/assets/convert_incline_art.py - do not edit.",
             "// Packed two-tone coverage: high nibble = outline, low nibble = detail,",
             "// outline + detail <= 15 = alpha. See ui_inclineScreen.cpp for expansion.",
             '#include "img_incline_scout.h"', ""]
    hdr = ["#pragma once", "", "#include <stdint.h>", "", "#ifdef __cplusplus", 'extern "C" {', "#endif", "",
           "// Generated by firmware/assets/convert_incline_art.py - do not edit.",
           "// One byte per pixel, row-major: (outline << 4) | detail, 0-15 each.", ""]
    for name, (w, h, data) in results.items():
        up = name.upper()
        hdr += [f"#define INCLINE_{up}_W {w}", f"#define INCLINE_{up}_H {h}",
                f"extern const uint8_t incline_{name}_mask[INCLINE_{up}_W * INCLINE_{up}_H];", ""]
        flat = data.flatten()
        lines.append(f"const uint8_t incline_{name}_mask[INCLINE_{up}_W * INCLINE_{up}_H] = {{")
        for i in range(0, len(flat), 24):
            lines.append("    " + ", ".join(f"0x{v:02X}" for v in flat[i:i + 24]) + ",")
        lines += ["};", ""]
    hdr += ["#ifdef __cplusplus", "}", "#endif", ""]
    OUT_C.write_text("\n".join(lines), newline="\n")
    OUT_H.write_text("\n".join(hdr), newline="\n")
    print(f"wrote {OUT_C.name}, {OUT_H.name}")


def expand(data, outline_rgb, detail_rgb):
    o = (data >> 4).astype(np.float64)
    d = (data & 15).astype(np.float64)
    s = o + d
    with np.errstate(invalid="ignore", divide="ignore"):
        wo = np.where(s > 0, o / s, 0)
    col = wo[..., None] * np.array(outline_rgb) + (1 - wo[..., None]) * np.array(detail_rgb)
    return Image.fromarray(np.dstack([col, s / 15 * 255]).astype(np.uint8), "RGBA")


def write_preview(results):
    hexrgb = lambda v: ((v >> 16) & 255, (v >> 8) & 255, v & 255)
    themes = [(0x262626, 0xFFFFFF, 0xB0B0B0), (0x0D0D0D, 0xB35900, 0x663300)]  # panel, text primary, text secondary
    angles = [0, 20, -35]
    cell = 240
    canvas = Image.new("RGB", (cell * len(angles) * 2, cell * len(themes)))
    for ti, (panel, prim, sec) in enumerate(themes):
        for vi, name in enumerate(["side", "rear"]):
            img = expand(results[name][2], hexrgb(prim), hexrgb(sec))
            for ai, ang in enumerate(angles):
                tile = Image.new("RGB", (cell, cell), hexrgb(panel))
                r = img.rotate(ang, resample=Image.BICUBIC, expand=True)
                tile.paste(r, ((cell - r.width) // 2, (cell - r.height) // 2), r)
                canvas.paste(tile, ((vi * len(angles) + ai) * cell, ti * cell))
    canvas.save(PREVIEW)
    print(f"wrote {PREVIEW.name}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()
    results = {}
    for name, fn, tw in VIEWS:
        w, h, co, cd = rasterise(fn(), tw)
        results[name] = (w, h, pack(co, cd))
        print(f"{name}: {w}x{h}")
    write_c(results)
    if args.preview:
        write_preview(results)


if __name__ == "__main__":
    main()
