#!/usr/bin/env python3
"""
Hand-builds a tiny, valid NAV/NPK2 vector tile file for testing
nav_tile_reader.cpp - a stand-in for real jgauchia/Tile-Generator output
until real OSM tile data exists (see waveshare-dash-build.md). This is
NOT a general-purpose encoder - it's a fixed, minimal fixture: one zoom
level, one tile, three hand-picked features (a line, a polygon, a text
label) chosen to exercise every payload branch the reader has to handle.

Format verified against jgauchia/Tile-Generator, PINNED to tag v.0.9.0,
commit 7f8e9819133369cd270c51f427a562cf2a8279fc - see
firmware/src/nav_tile_format.h's header comment for how this was
confirmed (docs/bin_tile_format.md and src/tile_processor.hpp's actual
byte-writing code had to be reconciled against each other, not just read
once and trusted).

This script also DECODES what it just wrote, independently of
nav_tile_reader.cpp's logic (re-implemented here in Python from the same
spec, not by importing/copying the C++), and asserts the recovered
features match what was encoded - same "verify with an independent
re-implementation before trusting it on hardware" discipline as
preview_font.py/preview_icons.py earlier in this project. Actually running
the real C++ parser against this fixture still requires the SD card
(arriving 2026-09-15) with this file copied to /maps/Z16.nav.

Usage: python gen_nav_fixture.py
Writes firmware/assets/fixtures/Z16.nav
"""
import struct
from pathlib import Path

HERE = Path(__file__).parent
OUT = HERE / "fixtures" / "Z16.nav"
OUT.parent.mkdir(exist_ok=True)

ZOOM = 16
TILE_X = 100
TILE_Y = 100
GEOM_LINESTRING = 2
GEOM_POLYGON = 3
GEOM_TEXT = 4


def to_varint(value):
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def zigzag_encode(n):
    return (n << 1) ^ (n >> 63) if n < 0 else (n << 1)


def pack_zoom_priority(min_zoom, priority):
    return ((min_zoom & 0x0F) << 4) | (priority & 0x0F)


def encode_geometry_feature(geom_type, color_index, min_zoom, priority, width_flags,
                             rings, palette_colors_used):
    all_pts = [p for ring in rings for p in ring]
    xs = [p[0] for p in all_pts]
    ys = [p[1] for p in all_pts]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)

    fixed = struct.pack("<BBBBBBBB", geom_type, color_index,
                         pack_zoom_priority(min_zoom, priority), width_flags,
                         min(255, min_x // 16), min(255, min_y // 16),
                         min(255, max_x // 16), min(255, max_y // 16))

    payload = bytearray()
    lx, ly = 0, 0
    for ring in rings:
        for (x, y) in ring:
            payload += to_varint(zigzag_encode(x - lx))
            payload += to_varint(zigzag_encode(y - ly))
            lx, ly = x, y

    ring_index = bytearray()
    if geom_type == GEOM_POLYGON:
        ring_index += struct.pack("<H", len(rings))
        cum = 0
        for ring in rings:
            cum += len(ring)
            ring_index += struct.pack("<H", cum)

    coord_count = len(all_pts)
    body = fixed + to_varint(coord_count) + to_varint(len(payload) + len(ring_index)) + bytes(payload) + bytes(ring_index)
    return body


def encode_text_feature(color_index, min_zoom, priority, font_size, x, y, text,
                         shield_bg=None, shield_border=None):
    bx, by = min(255, max(0, x >> 4)), min(255, max(0, y >> 4))
    fixed = struct.pack("<BBBBBBBB", GEOM_TEXT, color_index,
                         pack_zoom_priority(min_zoom, priority), font_size,
                         bx, by, bx, by)

    text_bytes = text.encode("ascii")
    payload = bytearray()
    payload += struct.pack("<hh", x, y)
    payload += struct.pack("<B", len(text_bytes))
    payload += text_bytes
    has_shield = shield_bg is not None
    if has_shield:
        payload += struct.pack("<HH", shield_bg, shield_border)

    data_size = len(payload)
    coord_count = (data_size + 3) // 4  # word count, NOT a vertex count - see nav_tile_format.h
    padded_size = coord_count * 4
    payload += b"\x00" * (padded_size - data_size)

    body = fixed + to_varint(coord_count) + to_varint(len(payload)) + bytes(payload)
    return body


def build():
    palette = [0xF800, 0x07E0, 0x0000]  # red, green, black (RGB565)

    line = encode_geometry_feature(
        GEOM_LINESTRING, color_index=0, min_zoom=10, priority=8, width_flags=3,
        rings=[[(100, 100), (500, 300), (900, 100)]], palette_colors_used=palette)

    polygon = encode_geometry_feature(
        GEOM_POLYGON, color_index=1, min_zoom=12, priority=7, width_flags=0x80,
        rings=[[(1000, 1000), (1000, 1400), (1400, 1400), (1400, 1000)]],
        palette_colors_used=palette)

    text = encode_text_feature(
        color_index=2, min_zoom=14, priority=15, font_size=12,
        x=700, y=700, text="TEST")

    features = [line, polygon, text]
    tile_body = struct.pack("<4sH", b"NAV1", len(features)) + b"".join(features)

    map_header_size = 23
    index_entry_size = 8
    palette_bytes = len(palette) * 2
    data_offset = map_header_size + 1 * index_entry_size + palette_bytes  # 1 tile (tilesWide*tilesHigh=1)

    map_header = struct.pack("<4sBIIIIH", b"NPK2", ZOOM, 1, 1, TILE_X, TILE_Y, len(palette))
    assert len(map_header) == map_header_size, len(map_header)

    index_entry = struct.pack("<II", data_offset, len(tile_body))
    palette_bin = b"".join(struct.pack("<H", c) for c in palette)

    full = map_header + index_entry + palette_bin + tile_body
    OUT.write_bytes(full)
    print(f"Wrote {OUT} ({len(full)} bytes): "
          f"header={map_header_size}B index={index_entry_size}B palette={palette_bytes}B "
          f"tile={len(tile_body)}B ({len(features)} features)")
    return full, palette


# --- Independent decoder, re-implemented from the spec (not from the C++) ---

def read_varint(buf, pos):
    result = 0
    shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def zigzag_decode(n):
    return (n >> 1) ^ -(n & 1)


def verify(full, palette):
    magic, zoom, tiles_wide, tiles_high, blx, bly, color_count = struct.unpack_from("<4sBIIIIH", full, 0)
    assert magic == b"NPK2"
    assert zoom == ZOOM and tiles_wide == 1 and tiles_high == 1
    assert blx == TILE_X and bly == TILE_Y
    assert color_count == len(palette)

    idx_off = 23
    offset, size = struct.unpack_from("<II", full, idx_off)
    pal_off = idx_off + 8
    decoded_palette = [struct.unpack_from("<H", full, pal_off + i * 2)[0] for i in range(color_count)]
    assert decoded_palette == palette

    pos = offset
    tmagic, fcount = struct.unpack_from("<4sH", full, pos)
    assert tmagic == b"NAV1"
    pos += 6

    results = []
    for _ in range(fcount):
        geom_type, color_index, zoom_priority, width_flags, minx, miny, maxx, maxy = \
            struct.unpack_from("<BBBBBBBB", full, pos)
        pos += 8
        coord_count, pos = read_varint(full, pos)
        payload_size, pos = read_varint(full, pos)
        payload_end = pos + payload_size

        if geom_type == GEOM_TEXT:
            x, y = struct.unpack_from("<hh", full, pos)
            text_len = full[pos + 4]
            text = full[pos + 5:pos + 5 + text_len].decode("ascii")
            results.append(("text", x, y, text))
        else:
            lx, ly = 0, 0
            pts = []
            for _ in range(coord_count):
                vdx, pos = read_varint(full, pos)
                vdy, pos = read_varint(full, pos)
                lx += zigzag_decode(vdx)
                ly += zigzag_decode(vdy)
                pts.append((lx, ly))
            ring_ends = []
            if geom_type == GEOM_POLYGON:
                (ring_count,) = struct.unpack_from("<H", full, pos)
                pos += 2
                for _ in range(ring_count):
                    (end,) = struct.unpack_from("<H", full, pos)
                    pos += 2
                    ring_ends.append(end)
            results.append(("geom", geom_type, pts, ring_ends))

        pos = payload_end

    assert results[0] == ("geom", GEOM_LINESTRING, [(100, 100), (500, 300), (900, 100)], [])
    assert results[1] == ("geom", GEOM_POLYGON, [(1000, 1000), (1000, 1400), (1400, 1400), (1400, 1000)], [4])
    assert results[2] == ("text", 700, 700, "TEST")
    print("Round-trip verified: line, polygon, and text all decode back exactly as encoded.")


if __name__ == "__main__":
    full, palette = build()
    verify(full, palette)
