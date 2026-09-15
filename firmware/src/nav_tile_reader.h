#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "nav_tile_format.h"

// Decoded-tile interface for the NAV vector tile format (see
// nav_tile_format.h for the on-disk byte layout and why it's pinned to
// jgauchia/Tile-Generator tag v.0.9.0). This header is the ONLY thing
// ui_navScreen.c (or any future renderer) should ever include for map
// data - it never sees NPK2/NAV1 bytes, varints, or palette indices,
// only these plain decoded structs. If the upstream format changes,
// nav_tile_format.h and nav_tile_reader.cpp are the only files that
// should need to change.
//
// Fixed-capacity, no heap allocation - matches this project's existing
// convention (e.g. dyno_data.h's DynoSample[DYNO_MAX_SAMPLES]) and this
// MCU's constraints. Caps below are a conservative starting guess for a
// small personal-route area, NOT verified against real tile density yet
// (no real Tile-Generator output exists in this repo - see
// waveshare-dash-build.md). Revisit once real generated tiles exist to
// measure against. A tile with more features/vertices/rings than the caps
// allow is NOT an error - it's silently truncated (truncated flag set)
// rather than corrupting memory or failing the whole tile.

#define NAV_MAX_FEATURES_PER_TILE 64
#define NAV_MAX_VERTICES_PER_FEATURE 32
#define NAV_MAX_RINGS_PER_FEATURE 4
#define NAV_MAX_TEXT_LEN 24

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t geomType;        // NAV_GEOM_* (nav_tile_format.h)
    uint16_t colorRgb565;    // already resolved via the zoom's palette
    uint8_t minZoom;         // unpacked from zoomPriority's high nibble
    uint8_t priority;        // unpacked from zoomPriority's low nibble
    bool isCasingOrSpecial;  // widthFlags bit7 (casing for lines, building
                              // flag for polygons at z>=16, bridge flag)
    uint8_t widthPx;         // widthFlags bits 0-6, in 0.5px units per the format doc
    uint8_t bboxMinX, bboxMinY, bboxMaxX, bboxMaxY;  // local tile space /16, coarse

    // Geometry (point/line/polygon) - vertices in NAV_TILE_EXTENT (0-4096)
    // local tile space, decoded from the delta-zigzag-varint payload.
    uint16_t vertexCount;    // number actually stored (<= NAV_MAX_VERTICES_PER_FEATURE)
    int16_t vx[NAV_MAX_VERTICES_PER_FEATURE];
    int16_t vy[NAV_MAX_VERTICES_PER_FEATURE];
    uint16_t ringCount;      // polygons only; 0 for point/line
    uint16_t ringEnds[NAV_MAX_RINGS_PER_FEATURE];  // cumulative vertex index at each ring's end

    // Text only (geomType == NAV_GEOM_TEXT) - vx/vy/ringCount above are
    // unused for text features.
    int16_t textX, textY;    // anchor point, same 0-4096 local tile space
    uint8_t textLen;         // 0 if not a text feature
    char text[NAV_MAX_TEXT_LEN + 1];  // null-terminated, truncated if longer
    uint16_t shieldBgRgb565;      // 0 if no label shield background
    uint16_t shieldBorderRgb565;
} NavFeature;

typedef struct {
    uint8_t zoom;
    uint32_t tileX, tileY;
    uint16_t featureCount;   // number actually decoded (<= NAV_MAX_FEATURES_PER_TILE)
    bool truncated;          // true if the real tile had more features, vertices,
                              // or rings than the caps above allow
    NavFeature features[NAV_MAX_FEATURES_PER_TILE];
} NavTileData;

// ~12KB per NavTileData at the caps above (dominated by
// NAV_MAX_FEATURES_PER_TILE * NAV_MAX_VERTICES_PER_FEATURE int16 pairs) -
// MUST be a static/global buffer, never a stack local; this MCU's task
// stacks are far smaller than that.

// Loads one tile from /maps/Z{zoom}.nav (SD card, mounted by sd_driver.h -
// checks sd_available() internally). Returns false if: no card mounted,
// the file for that zoom doesn't exist, (tileX,tileY) falls outside the
// file's generated bounding box, or the index says that tile slot is
// empty (offset==0/size==0 - a legitimate "no data here" case, e.g. open
// water or outside the generated area, not an error) - matches this
// project's existing "false is fine, just means nothing there" convention
// (gps_driver.h, imu_driver.h). Does not distinguish those cases in the
// return value, same reasoning as those drivers: a renderer just needs to
// know whether it has a tile to draw, not why it doesn't.
bool nav_tile_load(uint8_t zoom, uint32_t tileX, uint32_t tileY, NavTileData *out);

#ifdef __cplusplus
}
#endif
