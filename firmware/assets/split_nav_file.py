#!/usr/bin/env python3
"""
Splits one big Z{zoom}.nav (NPK2 format, one file covering a whole PBF
extract's bounding box) into a grid of smaller regional .nav files, each
independently valid in the exact same format - added 2026-09-15 after
discovering the Arduino ESP32 SD library's seek() is roughly O(distance)
on a single large file: a seek ~660MB into California's 1.3GB Z16.nav
measured ~9+ seconds against a ~500ms seek at 35MB. Splitting keeps any
single seek within one small file, bounded by REGION_TILES regardless of
which region of the state is being looked up - a real, worthwhile fix on
its own merits, but NOT what actually fixed the real-hardware crash this
was originally written to chase down (that turned out to be
nav_tile_load() being called from a Ticker/esp_timer callback context
rather than a normal task - see CLAUDE.md's "Map tile format" section,
"the real task-watchdog crash", for the full story and the actual fix).

This is NOT part of jgauchia/Tile-Generator's own format/tooling - it's a
post-processing step layered on top of its output, invented for this
project's SD-card-performance constraint. Re-packs existing tile data
byte-for-byte (no re-parsing of feature contents, no re-running the
generator against OSM data) - just a smaller repackaging of what
Tile-Generator already produced.

Output naming: Z{zoom}_r{row}_c{col}.nav, where row = tileY // REGION_TILES,
col = tileX // REGION_TILES, using ABSOLUTE slippy-map tile numbers (not
relative to any particular PBF extract's bounding box) - this keeps the
same row/col scheme meaningful regardless of which region generated a
given input file, and matches nav_tile_reader.cpp's lookup logic exactly.

Usage: python split_nav_file.py <input Z16.nav> <output_dir> [--region-tiles N]
"""
import struct
import sys
import os
import argparse

MAP_HEADER_FMT = "<4sBIIIIH"
MAP_HEADER_SIZE = 23
INDEX_ENTRY_FMT = "<II"
INDEX_ENTRY_SIZE = 8


def split(input_path, output_dir, region_tiles):
    with open(input_path, "rb") as f:
        data = f.read()

    magic, zoom, tiles_wide, tiles_high, bottom_left_x, bottom_left_y, color_count = \
        struct.unpack_from(MAP_HEADER_FMT, data, 0)
    assert magic == b"NPK2", f"bad magic {magic!r}"
    print(f"Input: zoom={zoom} tilesWide={tiles_wide} tilesHigh={tiles_high} "
          f"bottomLeft=({bottom_left_x},{bottom_left_y}) colorCount={color_count}")

    idx_off = MAP_HEADER_SIZE
    pal_off = idx_off + tiles_wide * tiles_high * INDEX_ENTRY_SIZE
    palette_bytes = data[pal_off:pal_off + color_count * 2]
    assert len(palette_bytes) == color_count * 2

    # Bucket every non-empty tile into its grid cell, keyed by (row, col) of
    # ABSOLUTE tile numbers - matches nav_tile_reader.cpp's row/col math.
    cells = {}  # (row, col) -> list of (absTileX, absTileY, offset, size)
    for yOff in range(tiles_high):
        absTileY = bottom_left_y + yOff
        row = absTileY // region_tiles
        rowBase = yOff * tiles_wide
        for xOff in range(tiles_wide):
            entry_off = idx_off + (rowBase + xOff) * INDEX_ENTRY_SIZE
            offset, size = struct.unpack_from(INDEX_ENTRY_FMT, data, entry_off)
            if size == 0:
                continue
            absTileX = bottom_left_x + xOff
            col = absTileX // region_tiles
            cells.setdefault((row, col), []).append((absTileX, absTileY, offset, size))

    os.makedirs(output_dir, exist_ok=True)
    sizes = []
    for (row, col), tiles in sorted(cells.items()):
        cellMinX = col * region_tiles
        cellMinY = row * region_tiles
        cellTilesX = [t[0] - cellMinX for t in tiles]
        cellTilesY = [t[1] - cellMinY for t in tiles]
        cell_tiles_wide = min(region_tiles, tiles_wide + bottom_left_x - cellMinX)
        cell_tiles_high = min(region_tiles, tiles_high + bottom_left_y - cellMinY)
        # Clamp to the actual max tile touched in this cell too (defensive,
        # in case the source file's own extent doesn't align to the grid).
        cell_tiles_wide = max(cell_tiles_wide, max(cellTilesX) + 1)
        cell_tiles_high = max(cell_tiles_high, max(cellTilesY) + 1)

        flat_count = cell_tiles_wide * cell_tiles_high
        new_index = bytearray(flat_count * INDEX_ENTRY_SIZE)  # zero = empty, correct default
        tile_blocks = bytearray()
        data_base = MAP_HEADER_SIZE + flat_count * INDEX_ENTRY_SIZE + len(palette_bytes)

        for (absTileX, absTileY, offset, size) in tiles:
            localX = absTileX - cellMinX
            localY = absTileY - cellMinY
            flat_idx = localY * cell_tiles_wide + localX
            new_offset = data_base + len(tile_blocks)
            struct.pack_into(INDEX_ENTRY_FMT, new_index, flat_idx * INDEX_ENTRY_SIZE, new_offset, size)
            tile_blocks += data[offset:offset + size]

        new_header = struct.pack(MAP_HEADER_FMT, b"NPK2", zoom, cell_tiles_wide, cell_tiles_high,
                                  cellMinX, cellMinY, color_count)
        out_path = os.path.join(output_dir, f"Z{zoom}_r{row}_c{col}.nav")
        with open(out_path, "wb") as out:
            out.write(new_header)
            out.write(new_index)
            out.write(palette_bytes)
            out.write(tile_blocks)
        total = len(new_header) + len(new_index) + len(palette_bytes) + len(tile_blocks)
        sizes.append(total)
        print(f"  {out_path}: {len(tiles)} tiles, {total/1024/1024:.2f} MB "
              f"({cell_tiles_wide}x{cell_tiles_high} grid, origin ({cellMinX},{cellMinY}))")

    print(f"\n{len(cells)} region files written to {output_dir}")
    if sizes:
        print(f"Size range: {min(sizes)/1024/1024:.2f} MB - {max(sizes)/1024/1024:.2f} MB "
              f"(avg {sum(sizes)/len(sizes)/1024/1024:.2f} MB)")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output_dir")
    # 64 empirically bounds the worst case at ~45MB against real
    # California data (739 files, avg 1.7MB) - 128 still left a ~108MB
    # outlier (dense LA/SF areas), too close to real seek-latency risk.
    ap.add_argument("--region-tiles", type=int, default=64)
    args = ap.parse_args()
    split(args.input, args.output_dir, args.region_tiles)
