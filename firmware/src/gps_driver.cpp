#include "gps_driver.h"
#include "mutex.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>

// See gps_driver.h for the full explanation of why this shares Serial/
// UART0 with the USB flash/debug port on this board, and why gps_init()/
// gps_poll() no-op under DEBUG/CAN_TRACE.

#define GPS_BAUD 9600  // NEO-6M/NEO-8M factory-default NMEA baud

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
            gpsData.hasFix    = gps.location.isUpdated();
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
