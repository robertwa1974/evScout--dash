#pragma once

#include <stdint.h>
#include <stdbool.h>

// Offline charging-station point dataset (2026-09-22) - the data half of
// the Destinations screen's "Nearest Charging Station" row (see
// ui_destinationsScreen.h). Data comes from OpenChargeMap
// (openchargemap.org, community-maintained, free API), pulled ONE TIME (or
// periodically, by re-running the script) via
// firmware/assets/gen_charging_stations.py for a bounding box around San
// Diego County - NOT a live API call from the vehicle. This project has no
// internet connectivity on the dash itself; same offline-data-prep
// philosophy already established for the NAV vector tiles
// (jgauchia/Tile-Generator, see CLAUDE.md's "Map tile format" section) -
// generate on a dev machine, ship the output to the SD card, firmware only
// ever reads a local file.
//
// Deliberately NOT the NAV vector tile format (nav_tile_format.h). That
// format's zoom-tiled regional files + banded index exist to handle a
// full statewide road network too large to load into RAM at once. A
// charging-station point list for one county is small (typically a few
// hundred to low thousands of records - well under 200KB at this struct's
// size) - one flat file, loaded whole into PSRAM once at boot, linear-
// scanned for "nearest" on demand. No spatial index needed at this scale.

#define CHARGING_STATIONS_PATH "/charging/stations.bin"
#define CHG_NAME_MAXLEN 40
#define CHG_CONNECTOR_MAXLEN 20
#define CHG_FILE_MAGIC "CHG1"  // 4 bytes on disk, no null terminator
#define CHG_MAX_NEAREST 5  // top-N shown on the Destinations screen - "realistically I would need top 5" (Rob, 2026-09-22), not just the single closest

// On-disk format: an 8-byte header (magic[4] + uint32 count, both little-
// endian - every machine this gets generated on or read on is little-
// endian, no byte-swap needed), then `count` of these records back to
// back, no padding (pragma pack below matches gen_charging_stations.py's
// struct.pack layout exactly).
#pragma pack(push, 1)
typedef struct {
    double lat;
    double lon;
    char name[CHG_NAME_MAXLEN];
    char connectorType[CHG_CONNECTOR_MAXLEN];  // e.g. "CCS", "CHAdeMO", "J1772", "Tesla" - see gen_charging_stations.py's connector-name mapping
    float powerKw;   // highest-power connection reported at this station; 0 if OpenChargeMap didn't report one
} ChargingStation;
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

// Reads CHARGING_STATIONS_PATH from the SD card into a PSRAM buffer once.
// Call from firmware.ino's setup(), after sd_init(). Returns false (and
// leaves the station count at 0) if the card isn't mounted, the file
// doesn't exist yet (normal/expected until the data-prep script has been
// run once and its output copied to the card), or the file's header magic
// doesn't match - the Destinations screen degrades gracefully in that case
// (same "false is fine, just means no data yet" convention as
// nav_tile_load()/sd_init() elsewhere in this project).
bool charging_stations_load(void);

// Number of stations currently loaded (0 if charging_stations_load() was
// never called or didn't succeed).
uint32_t charging_stations_count(void);

// Single-pass linear scan keeping a small sorted (nearest-first) top-N as
// it goes - fine at this dataset's scale (a few hundred to low thousands
// of records, see this header's top comment); no need to fully sort the
// whole dataset just to keep the closest few. outStations/outDistances
// must each have room for at least maxResults entries (CHG_MAX_NEAREST is
// the Destinations screen's own choice of N, but this function itself is
// generic over maxResults). Returns the actual number found, which is
// less than maxResults if fewer stations are loaded than that. Uses the
// same calcDist() Haversine helper as the rest of this project's GPS math
// (gps_math.h), not re-derived here.
uint32_t charging_stations_find_nearest_n(double lat, double lon, uint32_t maxResults,
                                           ChargingStation *outStations, float *outDistances);

#ifdef __cplusplus
}
#endif
