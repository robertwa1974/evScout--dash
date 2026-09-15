// Mock GPS source (2026-09-14) - drop-in replacement for gps_driver.cpp's
// gps_init()/gps_poll(), same signatures, same gpsData global, so nothing
// downstream (ui_gpsScreen.c, ui_navScreen.c, zombie_updaters.cpp's
// slowUpdate, resolveDisplayPref's sunrise calc) can tell the difference -
// see [env:mock-gps] in platformio.ini for the single build-time switch
// point that compiles this file INSTEAD OF gps_driver.cpp (not alongside
// it - both define the same symbols, so exactly one may be linked in).
//
// Why: lets the GPS nav screen (and anything else gpsData-driven) be built
// and visually verified on real hardware before a physical GPS module is
// on the bench - the standing project discipline is "verify on real
// hardware before calling it done," which still applies even without the
// sensor itself.
//
// Route: a small canned rectangular loop (roughly 500m x 440m) so the nav
// screen's breadcrumb trail has turns to render, not just a straight line.
// Coordinates are centered on real downtown San Francisco (Union Square
// area, 2026-09-15 - previously an arbitrary Pennsylvania-ish point with
// no real map data anywhere near it) so this build also exercises
// ui_navScreen.c's NAV vector tile renderer against the real California
// Tile-Generator output on /maps/Z16.nav (see CLAUDE.md's "Map tile
// format" section), not just the trail line - the exact tile at this
// base point was confirmed non-empty on real hardware before picking it.
// Deltas below are still simple hand-picked offsets, not computed from
// meters - precision doesn't matter here, only that consecutive points
// are a plausible ~1 Hz driving cadence apart.
#include "gps_driver.h"
#include "mutex.h"
#include <Arduino.h>

GpsData gpsData = {0};

struct MockPoint { double lat, lon; float speedKph, headingDeg; };

// Real coordinates now (see file header) - kept as named constants since
// gps_init()/gps_poll() below read from MOCK_ROUTE[], not these directly;
// documents what the route is centered on.
#define MOCK_BASE_LAT 37.77490
#define MOCK_BASE_LON -122.41940
static const MockPoint MOCK_ROUTE[] = {
    {37.77490, -122.41940,  0, 0},
    {37.77580, -122.41940, 25, 0},    // heading north, speeding up
    {37.77670, -122.41940, 45, 0},
    {37.77760, -122.41940, 45, 0},
    {37.77850, -122.41940, 30, 0},
    {37.77940, -122.41940, 15, 350},  // slowing into the corner
    {37.77940, -122.42060, 20, 270},  // heading west
    {37.77940, -122.42180, 40, 270},
    {37.77940, -122.42300, 40, 270},
    {37.77940, -122.42420, 25, 260},
    {37.77850, -122.42440, 15, 190},  // corner
    {37.77760, -122.42440, 30, 180},  // heading south
    {37.77670, -122.42440, 45, 180},
    {37.77580, -122.42440, 45, 180},
    {37.77490, -122.42440, 20, 170},
    {37.77490, -122.42320, 25, 90},   // corner, heading east
    {37.77490, -122.42200, 40, 90},
    {37.77490, -122.42080, 35, 90},
    {37.77490, -122.41980, 15, 80},
    {37.77490, -122.41940,  5, 45},   // back near the start
};
#define MOCK_ROUTE_LEN (sizeof(MOCK_ROUTE) / sizeof(MOCK_ROUTE[0]))
#define MOCK_TICK_MS 1000  // matches real GPS's ~1Hz fix cadence

static size_t s_idx = 0;
static uint32_t s_lastTickMs = 0;

void gps_init(void) {
    s_idx = 0;
    s_lastTickMs = millis();
    // Seed gpsData immediately so the nav/GPS screens don't sit on "NO FIX"
    // for a full tick after boot.
    gpsData.hasFix = true;
    gpsData.latitude  = MOCK_ROUTE[0].lat;
    gpsData.longitude = MOCK_ROUTE[0].lon;
    gpsData.speedKph   = MOCK_ROUTE[0].speedKph;
    gpsData.headingDeg = MOCK_ROUTE[0].headingDeg;
    gpsData.altitudeM  = 120.0f;  // arbitrary plausible flatland altitude
    gpsData.satellites = 9;
    gpsData.lastFixMs  = s_lastTickMs;
}

void gps_poll(void) {
    uint32_t now = millis();
    if (now - s_lastTickMs < MOCK_TICK_MS) return;
    s_lastTickMs = now;

    s_idx = (s_idx + 1) % MOCK_ROUTE_LEN;
    const MockPoint &p = MOCK_ROUTE[s_idx];

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        gpsData.hasFix     = true;
        gpsData.latitude   = p.lat;
        gpsData.longitude  = p.lon;
        gpsData.speedKph   = p.speedKph;
        gpsData.headingDeg = p.headingDeg;
        gpsData.altitudeM  = 120.0f;
        gpsData.satellites = 9;
        gpsData.lastFixMs  = now;
        xSemaphoreGive(dataMutex);
    }
}
