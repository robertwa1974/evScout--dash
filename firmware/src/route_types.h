// route_types.h - ROUTE.bin binary format structs, ported from
// jgauchia/IceNav-v3 (lib/router/src/route_types.hpp, pinned tag
// v.0.2.9, commit d1819b771e12394e185cdf18e8a14875b0998912) as part of
// adopting IceNav-v3's actual routing code per the requirements doc's
// direction to reuse it rather than build a router from scratch. This
// matches the requirements doc's own description of the format
// byte-for-byte (32B header, 20B cell index, 12B nodes/edges) - unlike
// the NAV tile format description in that same doc, which nav_reader.h's
// header comment documents as inaccurate.
//
// ONE deviation from upstream: routeBinPath() returns paths without the
// "/sdcard" prefix - that's IceNav's own ESP-IDF VFS mount point; this
// project's SD access goes through Arduino SD.h (see sd_driver.h), whose
// paths are already relative to the FAT root (matches this project's
// existing /maps/... convention, not /sdcard/maps/...).
#pragma once
#include <cstdint>
#include <vector>
#include "psram_allocator.h"

static constexpr char ROUTE_MAGIC[4] = {'R', 'O', 'U', 'T'};

// Returns the ROUTE.bin path for the given max-speed preference (km/h).
// Must match the subdirectory layout produced by route_generator: CAR / BIKE / WALK.
static inline const char* routeBinPath(uint16_t routeSpeed) {
    if (routeSpeed <= 5) return "/ROUTE/WALK/ROUTE.bin";
    if (routeSpeed <= 25) return "/ROUTE/BIKE/ROUTE.bin";
    return "/ROUTE/CAR/ROUTE.bin";
}

#pragma pack(push, 1)

// File header - 32 bytes
struct RouteFileHeader {
    char magic[4];
    uint32_t sub_step_e4;  // 500 = 0.05 degree cells
    uint32_t cell_count;
    uint32_t reserved[5];
};
static_assert(sizeof(RouteFileHeader) == 32, "RouteFileHeader size mismatch");

// Per-cell index entry - 20 bytes
struct CellIndexEntry {
    int32_t lat_e4;         // lat x 10000, snapped to 0.1 degree grid
    int32_t lon_e4;         // lon x 10000, snapped to 0.1 degree grid
    uint32_t node_offset;   // global index of first node (for cellForNode / nearestNode)
    uint16_t node_count;
    uint32_t data_offset;   // byte offset from start of data block: cell stores [nodes][edges] contiguously
    uint16_t edge_count;
};
static_assert(sizeof(CellIndexEntry) == 20, "CellIndexEntry size mismatch");

struct RouteNode {
    float lat;
    float lon;
    uint32_t edge_offset;   // relative to this cell's edge block
};
static_assert(sizeof(RouteNode) == 12, "RouteNode size mismatch");

struct RouteEdge {
    uint32_t dst_node;      // global node index (absolute)
    uint32_t cost;          // tenths of second
    uint16_t dist_m;
    uint8_t flags;
    uint8_t reserved;
};
static_assert(sizeof(RouteEdge) == 12, "RouteEdge size mismatch");

#pragma pack(pop)

static inline uint8_t edge_highway_class(uint8_t f) { return (f >> 1) & 0x07; }
static inline bool edge_is_oneway(uint8_t f) { return (f & 0x01) != 0; }

// NOT part of upstream's route_types.hpp - IceNav's astarRoute()/Router
// return a TrackVector of its own GPX wayPoint struct (from
// lib/gpx/src/globalGpxDef.h), which carries a lot of GPX-file-logging
// fields (name/desc/time/src/sym/type char*, hdop/vdop/pdop, satellite
// count) this project has no GPX-logging feature to populate - only
// astarRoute() setting .lat/.lon is actually used. Trimmed to just what
// routing needs rather than porting the GPX-logging feature too; kept
// the same `wayPoint`/`TrackVector` names so astar.cpp/router.cpp's own
// logic (ported close to verbatim) didn't need to change internally.
struct wayPoint {
    float lat = 0, lon = 0;
};
typedef std::vector<wayPoint, PsramAllocator<wayPoint>> TrackVector;
