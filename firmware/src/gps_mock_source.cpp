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
//
// Relocated 2026-09-18 from downtown San Francisco to Encinitas, CA
// (~3.3km from HOME_LAT/HOME_LON in ui_navScreen.cpp - 1377 Calle Scott,
// 92024) specifically so the new "phone home" button's Router::route()
// call has a realistic LOCAL destination to test against. The SF-based
// route (loaded from ~840km away) was a genuine problem, not just a slow
// test: GraphLoader's page cache (PAGE_CACHE_MAX=48 cells) had to hold
// nodes/edges from cells scattered the length of the state, and the A*
// search ran for 13+ minutes on real hardware without finishing or
// crashing - almost certainly LRU cache thrashing (repeatedly evicting
// and re-reading the same distant cells from SD) rather than genuine
// progress. A real driver only ever presses "Home" from somewhere
// reasonably close to home, so this mismatch was a test-harness problem,
// not a real usage scenario - fixing the mock route's location to match
// how the feature is actually used, not tuning the router around an
// unrealistic worst case.
//
// Same hand-picked (not meters-computed) delta pattern as the original
// SF loop, just re-centered - precision doesn't matter here, only that
// consecutive points are a plausible ~1 Hz driving cadence apart and
// this exercises the same NAV vector tile renderer against real
// California Tile-Generator output (see CLAUDE.md's "Map tile format"
// section). Covered by the same whole-state california-latest.osm.pbf
// generation as the old SF point (so NAV tiles/ROUTE.bin should exist
// here too), but this exact base tile hasn't been individually spot-
// checked the way the SF one originally was - first real hardware flash
// against this location IS that check.
#include "gps_driver.h"
#include "mutex.h"
#include <Arduino.h>

GpsData gpsData = {0};

struct MockPoint { double lat, lon; float speedKph, headingDeg; };

// Real coordinates now (see file header) - kept as named constants since
// gps_init()/gps_poll() below read from MOCK_ROUTE[], not these directly;
// documents what the route is centered on.
#define MOCK_BASE_LAT 33.06000
#define MOCK_BASE_LON -117.25000
static const MockPoint MOCK_ROUTE[] = {
    {33.06000, -117.25000,  0, 0},
    {33.06090, -117.25000, 25, 0},    // heading north, speeding up
    {33.06180, -117.25000, 45, 0},
    {33.06270, -117.25000, 45, 0},
    {33.06360, -117.25000, 30, 0},
    {33.06450, -117.25000, 15, 350},  // slowing into the corner
    {33.06450, -117.25120, 20, 270},  // heading west
    {33.06450, -117.25240, 40, 270},
    {33.06450, -117.25360, 40, 270},
    {33.06450, -117.25480, 25, 260},
    {33.06360, -117.25500, 15, 190},  // corner
    {33.06270, -117.25500, 30, 180},  // heading south
    {33.06180, -117.25500, 45, 180},
    {33.06090, -117.25500, 45, 180},
    {33.06000, -117.25500, 20, 170},
    {33.06000, -117.25380, 25, 90},   // corner, heading east
    {33.06000, -117.25260, 40, 90},
    {33.06000, -117.25140, 35, 90},
    {33.06000, -117.25040, 15, 80},
    {33.06000, -117.25000,  5, 45},   // back near the start
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
