#include "odometer.h"
#include "zombie_updaters.h"  // myData, dataMutex
#include "gps_driver.h"
#include "ui_can_freshness.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>

#define METERS_PER_MILE    1609.344
#define ODO_SAVE_EVERY_MI  1.0
#define GPS_MIN_KPH        3.0f    // below this, GPS speed is position jitter, not motion
#define GPS_MAX_AGE_MS     3000
#define INTEGRATE_EVERY_MS 200
#define MAX_STEP_MS        1000    // cap one step so a stalled loop() can't invent distance

static Preferences preferences;  // own instance, own namespace - same reasoning as vehicle_config.cpp
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static double totalM = 0;
static double tripM = 0;
static double lastSavedTotalM = 0;
static volatile bool saveRequested = false;
static uint32_t lastStepMs = 0;

static void save(void) {
    portENTER_CRITICAL(&lock);
    double t = totalM, tr = tripM;
    portEXIT_CRITICAL(&lock);
    preferences.begin("odo", false);
    preferences.putDouble("total_m", t);
    preferences.putDouble("trip_m", tr);
    preferences.end();
    lastSavedTotalM = t;
}

// Current ground speed in km/h from the best available source, 0 if none.
static float currentKph(void) {
    if (ui_can_freshness_isLive()) {
        int kph = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            kph = myData.vehSpeedKph;
            xSemaphoreGive(dataMutex);
        }
        return (float)abs(kph);  // reverse counts as distance too
    }
    if (gpsData.hasFix && millis() - gpsData.lastFixMs < GPS_MAX_AGE_MS && gpsData.speedKph >= GPS_MIN_KPH) {
        return gpsData.speedKph;
    }
    return 0;
}

void odometer_init(void) {
    preferences.begin("odo", true);
    totalM = preferences.getDouble("total_m", 0);
    tripM = preferences.getDouble("trip_m", 0);
    preferences.end();
    lastSavedTotalM = totalM;
    lastStepMs = millis();
}

void odometer_poll(void) {
    uint32_t now = millis();
    uint32_t dt = now - lastStepMs;
    if (dt >= INTEGRATE_EVERY_MS) {
        lastStepMs = now;
        if (dt > MAX_STEP_MS) dt = MAX_STEP_MS;
        double stepM = currentKph() / 3.6 * (dt / 1000.0);
        if (stepM > 0) {
            portENTER_CRITICAL(&lock);
            totalM += stepM;
            tripM += stepM;
            portEXIT_CRITICAL(&lock);
        }
    }
    if (saveRequested || totalM - lastSavedTotalM >= ODO_SAVE_EVERY_MI * METERS_PER_MILE) {
        saveRequested = false;
        save();
    }
}

void odometer_request_save(void) {
    saveRequested = true;
}

double odometer_total_miles(void) {
    portENTER_CRITICAL(&lock);
    double m = totalM;
    portEXIT_CRITICAL(&lock);
    return m / METERS_PER_MILE;
}

double odometer_trip_miles(void) {
    portENTER_CRITICAL(&lock);
    double m = tripM;
    portEXIT_CRITICAL(&lock);
    return m / METERS_PER_MILE;
}

void odometer_reset_trip(void) {
    portENTER_CRITICAL(&lock);
    tripM = 0;
    portEXIT_CRITICAL(&lock);
    saveRequested = true;
}

void odometer_set_total_miles(double miles) {
    if (miles < 0) return;
    portENTER_CRITICAL(&lock);
    totalM = miles * METERS_PER_MILE;
    portEXIT_CRITICAL(&lock);
    saveRequested = true;
}
