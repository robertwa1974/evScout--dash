#include "odometer.h"
#include "zombie_updaters.h"  // myData, dataMutex
#include "gps_driver.h"
#include "ui_can_freshness.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <math.h>

#define METERS_PER_MILE    1609.344
// Save policy (2026-10-24, for ignition-switched power - key-off cuts power
// instantly, with no warning): every 0.25 mi while moving, plus once the
// vehicle has been stopped STOP_SAVE_MS after moving (key-off nearly
// always follows a stop), if at least STOP_SAVE_MIN_M was travelled since
// the last save. Was once per mile.
#define ODO_SAVE_EVERY_MI  0.25
#define STOP_SAVE_MS       3000UL
#define STOP_SAVE_MIN_M    10.0
#define GPS_MIN_KPH        3.0f    // below this, GPS speed is position jitter, not motion
#define GPS_MAX_AGE_MS     3000
#define INTEGRATE_EVERY_MS 200
#define MAX_STEP_MS        1000    // cap one step so a stalled loop() can't invent distance

// Efficiency (2026-10-24) - see odometer.h for what counts as "measured".
#define OPMODE_CHARGE      4
#define SEG_M              (0.2 * METERS_PER_MILE)   // recent-efficiency segment length
#define SEG_COUNT          50                         // 50 x 0.2 mi = last ~10 mi
#define RECENT_MIN_SEGS    5                          // 1 mi before "recent" replaces lifetime
#define TRIP_MIN_MI        0.5
#define LIFETIME_MIN_MI    1.0
#define WHMI_PLAUSIBLE_MIN 50.0                       // below this, range would be absurd
#define STOP_GRACE_MS      60000UL                    // idle draw still counted this long after stopping

static uint32_t lastMovingMs = 0;
static bool movedSinceSave = false;   // moving -> stopped transition pending a save

static Preferences preferences;  // own instance, own namespace - same reasoning as vehicle_config.cpp
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static double totalM = 0;
static double tripM = 0;
static double lastSavedTotalM = 0;
static volatile bool saveRequested = false;
static uint32_t lastStepMs = 0;

// Energy over "measured" distance only (CAN live, not charging).
static double effWh = 0, effM = 0;            // lifetime
static double tripWh = 0, tripEffM = 0;       // since trip reset
static double segWh = 0, segM = 0;            // segment being filled
static float  segRing[SEG_COUNT];             // Wh per completed 0.2 mi segment
static int    segHead = 0, segFilled = 0;

static void save(void) {
    portENTER_CRITICAL(&lock);
    double t = totalM, tr = tripM, ew = effWh, em = effM, tw = tripWh, tem = tripEffM;
    portEXIT_CRITICAL(&lock);
    preferences.begin("odo", false);
    preferences.putDouble("total_m", t);
    preferences.putDouble("trip_m", tr);
    preferences.putDouble("eff_wh", ew);
    preferences.putDouble("eff_m", em);
    preferences.putDouble("trip_wh", tw);
    preferences.putDouble("trip_em", tem);
    preferences.end();
    lastSavedTotalM = t;
}

// Current ground speed in km/h from the best available source, 0 if none.
// *powerKw/*measured describe whether this step's energy can be counted.
static float currentKph(float *powerKw, bool *measured) {
    *measured = false;
    *powerKw = 0;
    if (ui_can_freshness_isLive()) {
        int kph = 0, opmode = 0;
        float kw = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            kph = myData.vehSpeedKph;
            kw = myData.powerKw;
            opmode = myData.opmode;
            xSemaphoreGive(dataMutex);
        }
        *powerKw = kw;
        // Count energy while moving, and for STOP_GRACE_MS after the last
        // movement (lights, junctions) - but not a parked VCU sitting in Run
        // for hours on the bench/in the garage, which would pile idle draw
        // onto zero miles and wreck the averages.
        uint32_t now = millis();
        if (kph != 0) lastMovingMs = now;
        bool recentlyMoving = (kph != 0) || (lastMovingMs != 0 && now - lastMovingMs < STOP_GRACE_MS);
        *measured = (opmode != OPMODE_CHARGE) && recentlyMoving;
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
    effWh = preferences.getDouble("eff_wh", 0);
    effM = preferences.getDouble("eff_m", 0);
    tripWh = preferences.getDouble("trip_wh", 0);
    tripEffM = preferences.getDouble("trip_em", 0);
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
        float kw;
        bool measured;
        double secs = dt / 1000.0;
        float kph = currentKph(&kw, &measured);
        // Any movement - CAN or GPS fallback - restarts the stop timer.
        if (kph > 0) {
            movedSinceSave = true;
            lastMovingMs = now;
        }
        double stepM = kph / 3.6 * secs;
        // Energy counts whenever measurable (see currentKph(): CAN live, not
        // charging, moving or within STOP_GRACE_MS of moving - idle draw at a
        // light is real consumption). Regen arrives as negative power.
        double stepWh = measured ? kw * 1000.0 * secs / 3600.0 : 0;
        portENTER_CRITICAL(&lock);
        totalM += stepM;
        tripM += stepM;
        if (measured) {
            effWh += stepWh;   effM += stepM;
            tripWh += stepWh;  tripEffM += stepM;
            segWh += stepWh;   segM += stepM;
            if (segM >= SEG_M) {
                segRing[segHead] = (float)segWh;
                segHead = (segHead + 1) % SEG_COUNT;
                if (segFilled < SEG_COUNT) segFilled++;
                segWh = 0;
                segM = 0;
            }
        }
        portEXIT_CRITICAL(&lock);
    }
    // Stopped long enough after moving -> save now, before a key-off can lose it.
    bool stopSave = movedSinceSave && lastMovingMs != 0 && now - lastMovingMs >= STOP_SAVE_MS &&
                    totalM - lastSavedTotalM >= STOP_SAVE_MIN_M;
    if (stopSave) movedSinceSave = false;
    if (saveRequested || stopSave || totalM - lastSavedTotalM >= ODO_SAVE_EVERY_MI * METERS_PER_MILE) {
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
    tripWh = 0;
    tripEffM = 0;
    portEXIT_CRITICAL(&lock);
    saveRequested = true;
}

void odometer_set_total_miles(double miles) {
    if (miles < 0) return;
    portENTER_CRITICAL(&lock);
    totalM = miles * METERS_PER_MILE;   // efficiency totals untouched - they're measured, not edited
    portEXIT_CRITICAL(&lock);
    saveRequested = true;
}

double odometer_trip_wh_per_mile(void) {
    portENTER_CRITICAL(&lock);
    double wh = tripWh, m = tripEffM;
    portEXIT_CRITICAL(&lock);
    double mi = m / METERS_PER_MILE;
    return mi >= TRIP_MIN_MI ? wh / mi : NAN;
}

double odometer_lifetime_wh_per_mile(void) {
    portENTER_CRITICAL(&lock);
    double wh = effWh, m = effM;
    portEXIT_CRITICAL(&lock);
    double mi = m / METERS_PER_MILE;
    return mi >= LIFETIME_MIN_MI ? wh / mi : NAN;
}

double odometer_recent_wh_per_mile(void) {
    double sum = 0;
    int n;
    portENTER_CRITICAL(&lock);
    n = segFilled;
    for (int i = 0; i < n; i++) sum += segRing[i];
    portEXIT_CRITICAL(&lock);
    if (n >= RECENT_MIN_SEGS) return sum / (n * (SEG_M / METERS_PER_MILE));
    return odometer_lifetime_wh_per_mile();
}

double odometer_range_miles(int socPct, float battKWh) {
    double whmi = odometer_recent_wh_per_mile();
    if (isnan(whmi) || whmi < WHMI_PLAUSIBLE_MIN || battKWh <= 0 || socPct < 0) return NAN;
    double range = (socPct / 100.0) * battKWh * 1000.0 / whmi;
    return range > 999 ? 999 : range;
}
