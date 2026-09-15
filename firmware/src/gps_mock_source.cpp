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
// Route: a small canned rectangular loop (roughly 400m x 250m) so the nav
// screen's breadcrumb trail has turns to render, not just a straight line.
// Coordinates are an arbitrary base point (not a real location) offset by
// simple lat/lon deltas - precision doesn't matter here, only that
// consecutive points are a plausible ~1 Hz driving cadence apart.
#include "gps_driver.h"
#include "mutex.h"
#include <Arduino.h>

GpsData gpsData = {0};

struct MockPoint { double lat, lon; float speedKph, headingDeg; };

// Base point is arbitrary (not a real address) - a rectangular loop with
// speed easing at the "corners" so it doesn't look robotic on the trail.
#define MOCK_BASE_LAT 40.0000
#define MOCK_BASE_LON -75.0000
// ~1 degree latitude = 111320 m; longitude scaled by cos(lat) elsewhere,
// but for this canned table it's simplest to just hand-pick deltas that
// produce a visually sensible loop rather than compute them.
static const MockPoint MOCK_ROUTE[] = {
    {40.00000, -75.00000,  0, 0},
    {40.00090, -75.00000, 25, 0},    // heading north, speeding up
    {40.00180, -75.00000, 45, 0},
    {40.00270, -75.00000, 45, 0},
    {40.00360, -75.00000, 30, 0},
    {40.00450, -75.00000, 15, 350},  // slowing into the corner
    {40.00450, -75.00120, 20, 270},  // heading west
    {40.00450, -75.00240, 40, 270},
    {40.00450, -75.00360, 40, 270},
    {40.00450, -75.00480, 25, 260},
    {40.00360, -75.00500, 15, 190},  // corner
    {40.00270, -75.00500, 30, 180},  // heading south
    {40.00180, -75.00500, 45, 180},
    {40.00090, -75.00500, 45, 180},
    {40.00000, -75.00500, 20, 170},
    {40.00000, -75.00380, 25, 90},   // corner, heading east
    {40.00000, -75.00260, 40, 90},
    {40.00000, -75.00140, 35, 90},
    {40.00000, -75.00040, 15, 80},
    {40.00000, -75.00000,  5, 45},   // back near the start
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
