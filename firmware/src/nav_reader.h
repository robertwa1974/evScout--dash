#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "nav_tile_format.h"

// Decoded-tile interface for the NAV vector tile format (see
// nav_tile_format.h for the on-disk byte layout and why it's pinned to
// jgauchia/Tile-Generator tag v.0.9.0). This header is the ONLY thing
// any screen/renderer should ever include for map data - it never sees
// NPK2/NAV1 bytes, varints, or palette indices, only these plain decoded
// structs. If the upstream format changes, nav_tile_format.h and
// nav_reader.cpp are the only files that should need to change.
//
// Renamed from nav_tile_reader.h (2026-09-15) when the pack/index-lookup
// portion of nav_tile_load() was replaced internally with a ported
// jgauchia/IceNav-v3 NavReader class (pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912) - see nav_reader.cpp's header
// comment for what was and wasn't carried over. This PUBLIC interface
// (function names/signatures, decoded struct shapes) is UNCHANGED on
// purpose so nothing consuming it needed to change for this swap.
//
// Fixed-capacity, no heap allocation INSIDE nav_tile_load() itself
// (matches this project's existing convention, e.g. dyno_data.h's
// DynoSample[DYNO_MAX_SAMPLES]) - the internal NavReader class DOES use
// PSRAM heap allocation for its index band cache and color palette, same
// as upstream. A tile with more features/vertices/rings than the caps
// allow is NOT an error - it's silently truncated (truncated flag set)
// rather than corrupting memory or failing the whole tile.
//
// Caps below were RAISED 2026-09-20 after real California data exposed
// the original 64/32/4 guess as badly undersized, not just "conservative":
// direct inspection of the real regional .nav files (not a mock/fixture)
// found ordinary tiles with 174-316 features, and the single densest real
// region (downtown LA, Z16_r409_c175.nav, 3762 populated tiles) had a
// median of 320 features/tile, p99 of 791, and a max of 1066 - the old
// cap of 64 was silently dropping 70-95% of most tiles' content, which is
// what made the on-screen map look sparse/incomplete ("I do not see all
// the tiles loaded" - it wasn't a loading bug, it was per-tile decode
// truncation). NAV_MAX_FEATURES_PER_TILE=1024 covers all but 2 of the
// 3762 tiles in that single densest region (99.9%), and comfortably
// covers ordinary (non-downtown) tiles with huge margin. Vertex counts
// within features were NOT similarly bad (p90=5, p99=29, max=65 in the
// same densest tile - only 0.8% of features exceeded the old cap of 32),
// so NAV_MAX_VERTICES_PER_FEATURE only needed a modest bump to 64 to
// close that remaining gap almost entirely. Ring counts were fine as-is
// (max observed 3, well under the existing cap of 4) - left unchanged.
//
// SIZING CONSEQUENCE: at these caps NavTileData is ~300KB (dominated by
// NAV_MAX_FEATURES_PER_TILE * NAV_MAX_VERTICES_PER_FEATURE int16 pairs) -
// far too large for this MCU's internal static RAM (the old ~12KB fit
// fine as a plain `static NavTileData` global; this size does not).
// Callers MUST heap_caps_malloc() this on PSRAM (MALLOC_CAP_SPIRAM) - see
// ui_navScreen.cpp's allocateNavBuffers() - never as a stack local or a
// plain static/global. The 8MB PSRAM on this board has ample room; this
// was a real constraint on internal SRAM, not on PSRAM.
#define NAV_MAX_FEATURES_PER_TILE 1024
#define NAV_MAX_VERTICES_PER_FEATURE 64
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

// ~300KB per NavTileData at the caps above (dominated by
// NAV_MAX_FEATURES_PER_TILE * NAV_MAX_VERTICES_PER_FEATURE int16 pairs) -
// MUST be a PSRAM heap_caps_malloc() allocation now, never a stack local
// AND never a plain static/global either (see this header's sizing
// comment above the caps - this grew ~25x when the caps were raised to
// match real map data, well past what fits in internal SRAM alongside
// everything else this MCU is already doing).

// Loads one tile from the SD card (mounted by sd_driver.h - checks
// sd_available() internally), from whichever regional file
// (/maps/Z{zoom}_r{row}_c{col}.nav, NAV_REGION_TILES tiles per file - see
// nav_tile_format.h) covers (tileX, tileY). Returns false if: no card
// mounted, the regional file for that area doesn't exist, (tileX,tileY)
// falls outside its generated bounding box, or the index says that tile
// slot is empty (offset==0/size==0 - a legitimate "no data here" case,
// e.g. open water or outside the generated area, not an error) - matches
// this project's existing "false is fine, just means nothing there"
// convention (gps_driver.h, imu_driver.h). Does not distinguish those
// cases in the return value, same reasoning as those drivers: a renderer
// just needs to know whether it has a tile to draw, not why it doesn't.
bool nav_tile_load(uint8_t zoom, uint32_t tileX, uint32_t tileY, NavTileData *out);

// Standard OSM/Google "slippy map" tile <-> lat/lon conversions (Web
// Mercator) - pure math, no SD access. Kept here rather than in a
// renderer since they're defined in terms of this format's own tile
// numbering/NAV_TILE_EXTENT convention, not renderer-specific.
//
// nav_latlon_to_tile: which absolute tile (at the given zoom) contains a
// GPS fix - used to decide which tile to nav_tile_load().
// nav_tile_local_to_latlon: the inverse, from one feature vertex's 0-4096
// local-tile-space coordinate back to real lat/lon - used to project
// loaded tile geometry into the same lat/lon space a renderer's own
// breadcrumb trail already projects onto the canvas, so both draw
// consistently off one projection.
void nav_latlon_to_tile(uint8_t zoom, double lat, double lon, uint32_t *tileX, uint32_t *tileY);
void nav_tile_local_to_latlon(uint8_t zoom, uint32_t tileX, uint32_t tileY,
                               int16_t localX, int16_t localY, double *lat, double *lon);

#ifdef __cplusplus
}
#endif
