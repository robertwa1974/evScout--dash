#pragma once

#include <stdint.h>

// NAV vector tile format constants/structs (2026-09-14) - the on-SD-card
// map data format for the eventual GPS NAV offline map, replacing the
// Maperitive/PNG-raster-tile plan entirely per Rob's explicit direction.
//
// Source of truth: jgauchia/Tile-Generator, PINNED to tag v.0.9.0, commit
// 7f8e9819133369cd270c51f427a562cf2a8279fc
// (https://github.com/jgauchia/Tile-Generator/tree/v.0.9.0) - the format
// is pre-1.0 and stated by its own author to be actively evolving; do NOT
// re-derive this against a newer commit without re-reading
// docs/bin_tile_format.md AND src/tile_processor.hpp's actual byte-writing
// code again (they can and did, at least transiently, disagree - see
// below). If the upstream format changes, only THIS file and
// nav_reader.cpp should need updating - ui_navScreen.c and everything
// else consumes the decoded TileData/TileFeature structs in
// nav_reader.h, never these raw bytes.
//
// How this was actually verified (not assumed from the docs alone): read
// docs/bin_tile_format.md, then read src/tile_processor.hpp's real
// encoder to confirm it matched - it did NOT, on first read. The tile-
// building code writes each feature with a TRANSIENT 9-byte header
// (geom_type, then a full inline 2-byte RGB565 color, then
// zoom_priority/width_byte/bbox) into a per-tile buffer. A SEPARATE later
// pass (still within the same generation run, before anything reaches
// disk) scans every already-built tile buffer, collects the distinct
// colors actually used into one palette per zoom level, and REWRITES each
// feature's inline 2-byte color down to a 1-byte palette index in place -
// only THIS collapsed, 8-byte-header form is what ends up in the final
// Z{zoom}.nav file. The doc describes the collapsed on-disk form
// correctly; the confusion was purely about which encoder pass to trust
// as "the real bytes on disk," resolved by reading both.
//
// Container layout of one Z{zoom}.nav file, in order:
//   MapHeader (23 bytes)
//   IndexEntry[tiles_wide * tiles_high]  (8 bytes each, row-major, Y outer/X inner)
//   uint16_t palette[color_count]         (RGB565, little-endian)
//   tile data blocks, back-to-back, referenced by IndexEntry.offset/size
//     (each block is one NAV1 tile - see NavTileHeader below)
//
// NOT a z/x/y-per-tile file tree (that was the PNG-raster-tile
// convention this project is explicitly dropping) - ONE file per zoom
// level covers every tile in that zoom's bounding box; tile lookup is a
// direct O(1) array index into that single file, not a file-path lookup.

#pragma pack(push, 1)

// 23 bytes. bottom_left is in ABSOLUTE TILE coordinates (slippy-map z/x/y
// tile numbers), NOT scaled lat/lon - confirmed from nav_types.hpp's own
// field comment ("[0]=min_x, [1]=min_y") and the encoder computing it
// directly from tile x/y, never from a coordinate transform.
typedef struct {
    char     magic[4];        // "NPK2"
    uint8_t  zoom;
    uint32_t tilesWide;
    uint32_t tilesHigh;
    uint32_t bottomLeftX;      // absolute tile X of the bounding box origin
    uint32_t bottomLeftY;      // absolute tile Y of the bounding box origin
    uint16_t colorCount;       // palette entries immediately after the index table
} NavMapHeader;

// 8 bytes, one per (x,y) slot in the tilesWide*tilesHigh flat array.
// offset==0 && size==0 means an empty slot (no data for that tile - a
// legitimate case, e.g. open water/off the generated area, not an error).
typedef struct {
    uint32_t offset;   // absolute byte offset into the Z{zoom}.nav file
    uint32_t size;      // bytes of NAV1 tile data at that offset (0 = empty)
} NavIndexEntry;

// 6 bytes - the header of one tile's feature block (referenced by an
// NavIndexEntry). This is the thing actually called "NAV1" in the format,
// confusingly at a different structural level than the file/tile
// terminology might suggest - it's the INNER per-tile block, not the
// outer per-zoom file (that's NPK2).
typedef struct {
    char     magic[4];       // "NAV1"
    uint16_t featureCount;
} NavTileHeader;

// The FINAL on-disk fixed part of one feature record (8 bytes) - after
// the palette-collapse pass, confirmed against tile_processor.hpp's
// rewrite loop, not the doc alone. Immediately followed in the byte
// stream by: varint(coordCount), varint(payloadSize), then payloadSize
// bytes of payload - see nav_reader.cpp for how those are decoded,
// since coordCount and the payload's meaning differ for GEOM_TEXT vs the
// geometric types (see below).
typedef struct {
    uint8_t geomType;      // NAV_GEOM_* below
    uint8_t colorIndex;    // index into the zoom level's palette
    uint8_t zoomPriority;  // (minZoom << 4) | (priority & 0x0F)
    uint8_t widthFlags;    // bit7 = casing/building/bridge flag, bits0-6 = width
    uint8_t minX, minY, maxX, maxY;  // local tile-space bbox, each value/16, clamped 0-255
} NavFeatureHeader;

#pragma pack(pop)

#define NAV_MAP_MAGIC   "NPK2"
#define NAV_TILE_MAGIC  "NAV1"

#define NAV_GEOM_POINT      1
#define NAV_GEOM_LINESTRING 2
#define NAV_GEOM_POLYGON    3
#define NAV_GEOM_TEXT       4

// Coordinates in a feature's payload are in a 0-4096 LOCAL TILE SPACE
// (the same per-tile pixel-grid convention Mapbox Vector Tiles popularized -
// confirmed here from tile_processor.hpp clamping bbox corners to this
// range), NOT raw lat/lon and NOT screen pixels - the renderer must scale
// this 0-4096 range to whatever pixel footprint it draws each tile at.
#define NAV_TILE_EXTENT 4096

// NOT part of jgauchia/Tile-Generator's own format - this project's OWN
// scheme, layered on top of its output (see nav_reader.cpp's
// nav_tile_load()). Tile-Generator emits one NPK2 file per zoom covering
// its ENTIRE input PBF's bounding box; for a whole-state extract
// (California, ~1.3GB at zoom 16) that means a single huge file, and the
// Arduino ESP32 SD library's seek() turned out to be roughly O(distance)
// on a large file - a seek ~660MB into that 1.3GB file took long enough
// to trip the ESP32 task watchdog and reboot the board (confirmed on real
// hardware 2026-09-15, see CLAUDE.md's "Map tile format" section - the
// tiny synthetic test fixture and even a single real-tile spot check
// never seeked far enough into a file to expose this). Fix: re-pack the
// SAME already-generated tile data (no re-running Tile-Generator, no
// re-parsing feature contents - see firmware/assets or the sibling
// tilegen-build checkout's split_nav_file.py) into a grid of smaller
// per-region .nav files, each independently a fully valid instance of
// the exact same NPK2/NAV1 format. NAV_REGION_TILES tiles square per
// region file, keyed by ABSOLUTE slippy-map tile numbers (not relative to
// any particular PBF extract), named Z{zoom}_r{tileY/NAV_REGION_TILES}_
// c{tileX/NAV_REGION_TILES}.nav - see nav_tile_load()'s path construction.
// 64 tiles/region measured empirically against real California data:
// max resulting file ~45MB (worst case, densest LA-area region), average
// ~1.7MB, 739 files total - comfortably bounds any single seek's cost.
#define NAV_REGION_TILES 64

// Non-text payload: coordCount (x,y) vertex pairs, each delta-encoded
// against the PREVIOUS vertex (starting from an implicit (0,0)) then
// zigzag-encoded then LEB128-varint-encoded - i.e. the same scheme Mapbox
// Vector Tiles / protobuf use, confirmed here from utils.hpp's
// zigzag_encode()/to_varint() (standard (n<<1)^(n>>63) zigzag, standard
// 7-bit-per-byte continuation-bit varint). If geomType==NAV_GEOM_POLYGON,
// the vertex data is followed by a ring-index block: uint16 ringCount,
// then ringCount x uint16 CUMULATIVE vertex-count-at-the-end-of-each-ring
// (so ring boundaries can be found by taking consecutive differences).
//
// Text payload is a COMPLETELY DIFFERENT layout, not vertex data at all:
// int16 x, int16 y (both little-endian, same 0-4096 local tile space -
// this is the label's anchor point, not a bounding box; NavFeatureHeader's
// min/max fields are both set to this same point for text, not a real
// bbox), uint8 textLen, textLen bytes of text, and - ONLY if a "shield"
// background is present (detected by the reader as bgColor!=0, there is
// no explicit flag byte) - 4 more bytes: uint16 bgColorRgb565, uint16
// borderColorRgb565 - all zero-padded at the end to a multiple of 4 bytes.
// For text, the "coordCount" varint preceding payloadSize is NOT a vertex
// count - it's (paddedPayloadSize/4), a word-count left over from how the
// encoder framed the padding. A parser must branch on geomType BEFORE
// deciding what coordCount means - this is the easiest part of the whole
// format to get wrong, confirmed by reading the encoder's text-writing
// path separately from its geometry-writing path, since they don't share
// code and don't agree on what that field means.
