#include "gps_driver.h"
#include "mutex.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>

// See gps_driver.h for the full explanation of why this shares Serial/
// UART0 with the USB flash/debug port on this board, and why gps_init()/
// gps_poll() no-op under DEBUG/CAN_TRACE.

#define GPS_BAUD 9600  // NEO-6M/NEO-8M factory-default NMEA baud
#define GPS_FIX_STALE_MS 3000  // no fresh location sentence in this long -> treat as lost, not just "didn't update this exact poll"

GpsData gpsData = {0};
static TinyGPSPlus gps;

void gps_init(void) {
#if defined(DEBUG) || defined(CAN_TRACE)
    return;  // Serial already claimed at 115200 for debug/trace output - see gps_driver.h
#else
    Serial.begin(GPS_BAUD);
#endif
}

void gps_poll(void) {
#if defined(DEBUG) || defined(CAN_TRACE)
    return;
#else
    bool updated = false;
    while (Serial.available() > 0) {
        if (gps.encode(Serial.read())) {
            updated = true;
        }
    }
    if (!updated) return;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        if (gps.location.isValid()) {
            // BUG FIX (2026-09-22, found bench-testing real GPS hardware for
            // the first time - "intermittent fix/no-fix with 9-10 sats
            // locked" was the symptom): isValid() means "has this object
            // ever received a good fix" (persists true once set), while
            // isUpdated() means "did a NEW sentence complete since the last
            // time this was checked" - a genuinely momentary flag.
            // gps_poll() runs many times per second draining Serial, but a
            // location-bearing NMEA sentence typically only arrives ~1Hz, so
            // isUpdated() is false on nearly every poll regardless of fix
            // quality - it was flickering hasFix off on pure polling-cadence
            // noise, not a real loss of fix. age() (ms since the last valid
            // update) against a staleness window is the correct check for
            // "is this fix still current," the same pattern this project
            // already uses for CAN-freshness elsewhere.
            gpsData.hasFix    = gps.location.age() < GPS_FIX_STALE_MS;
            gpsData.latitude  = gps.location.lat();
            gpsData.longitude = gps.location.lng();
        }
        if (gps.speed.isValid())      gpsData.speedKph   = gps.speed.kmph();
        if (gps.course.isValid())     gpsData.headingDeg = gps.course.deg();
        if (gps.altitude.isValid())   gpsData.altitudeM  = gps.altitude.meters();
        if (gps.satellites.isValid()) gpsData.satellites = (uint8_t)gps.satellites.value();
        if (gps.date.isValid() && gps.time.isValid()) {
            gpsData.year   = gps.date.year();
            gpsData.month  = gps.date.month();
            gpsData.day    = gps.date.day();
            gpsData.hour   = gps.time.hour();
            gpsData.minute = gps.time.minute();
            gpsData.second = gps.time.second();
        }
        gpsData.lastFixMs = millis();
        xSemaphoreGive(dataMutex);
    }
#endif
}
