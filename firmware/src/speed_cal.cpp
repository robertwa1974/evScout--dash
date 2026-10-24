#include "speed_cal.h"
#include "zombie_updaters.h"  // myData, dataMutex
#include "gps_driver.h"
#include "ui_can_freshness.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>

#define INCH_KM            0.0000254
#define DEFAULT_AXLE       3.54f     // Rob's Scout 80 axle (2026-10-24)
#define DEFAULT_TIRE_IN    31.0f
#define DEFAULT_TRANSFER   1.00f     // LS600h transfer case: centre diff, no reduction - see speed_cal.h

// LS600h L110F MG2 range reductions - ORNL/TM-2008/185 "Evaluation of the
// 2008 Lexus LS 600h Hybrid Synergy Drive System" (osti.gov/biblio/947393),
// section 2.3, p.41: LOW = 87-tooth ring held, 30-tooth sun in, carrier out
// = 1 + 87/30 = 3.90 (exact from tooth counts); HIGH = "about 1.9" (the
// report gives no exact tooth path for it - GPS calibration pins it down).
static const float RANGE_REDUCTION[2] = {3.90f, 1.90f};

// GPS calibration acceptance (see speed_cal.h).
#define CAL_SAMPLE_MS      200
#define CAL_NEED_SAMPLES   100       // 20 s of good samples
#define CAL_TIMEOUT_MS     120000UL
#define CAL_MIN_GPS_KPH    40.0f     // ~25 mph - GPS speed error shrinks with speed
#define CAL_MIN_RPM        300
#define CAL_GPS_MAX_AGE_MS 1500
#define CAL_MAX_SPREAD     0.04      // reject if K varied by more than 4% (std/mean)
#define CAL_MAX_DEVIATION  0.25f     // reject if >25% off the expected ratio - wrong range or bad data

static Preferences prefs;  // own instance, own namespace - same reasoning as vehicle_config.cpp
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static float axleRatio = DEFAULT_AXLE;
static float tireInches = DEFAULT_TIRE_IN;
static float transferRatio = DEFAULT_TRANSFER;
static float k[2];          // kph per motor rpm, per range
static bool calibrated[2];

static speed_cal_state_t calState = SPEED_CAL_IDLE;
static int calRange = -1;
static uint32_t calStartMs = 0, calLastSampleMs = 0;
static int calCount = 0;
static double calSum = 0, calSumSq = 0;
static char status[120] = "";

// K for a given range reduction:
//   kph = rpm / (range reduction x transfer x axle) x tire circumference x 60
static float kForReduction(float rangeReduction) {
    double circKm = M_PI * tireInches * INCH_KM;
    return (float)(circKm * 60.0 / (axleRatio * transferRatio * rangeReduction));
}

static void save(void) {
    prefs.begin("spdcal", false);
    prefs.putFloat("axle", axleRatio);
    prefs.putFloat("tire", tireInches);
    prefs.putFloat("xfer", transferRatio);
    prefs.putFloat("k0", k[0]);
    prefs.putFloat("k1", k[1]);
    prefs.putBool("cal0", calibrated[0]);
    prefs.putBool("cal1", calibrated[1]);
    prefs.end();
}

// Uncalibrated ranges are always derived from the current settings, never
// read back from NVS - so changing a default (or fixing one, as when the
// 1.00:1 placeholder became the ORNL 3.90/1.90 on 2026-10-24) takes effect.
static void refreshEstimates(void) {
    portENTER_CRITICAL(&lock);
    for (int r = 0; r < 2; r++) {
        if (!calibrated[r]) k[r] = kForReduction(RANGE_REDUCTION[r]);
    }
    portEXIT_CRITICAL(&lock);
}

static void resetCalibration(void) {
    portENTER_CRITICAL(&lock);
    calibrated[0] = calibrated[1] = false;
    portEXIT_CRITICAL(&lock);
    refreshEstimates();
}

void speed_cal_init(void) {
    prefs.begin("spdcal", true);
    axleRatio = prefs.getFloat("axle", DEFAULT_AXLE);
    tireInches = prefs.getFloat("tire", DEFAULT_TIRE_IN);
    transferRatio = prefs.getFloat("xfer", DEFAULT_TRANSFER);
    k[0] = prefs.getFloat("k0", 0);
    k[1] = prefs.getFloat("k1", 0);
    calibrated[0] = prefs.getBool("cal0", false) && k[0] > 0;
    calibrated[1] = prefs.getBool("cal1", false) && k[1] > 0;
    prefs.end();
    refreshEstimates();
}

int speed_cal_range_for(int gearFb, int dirState) {
    if (gearFb == 0) return SPEED_RANGE_LOW;
    if (gearFb == 3 && dirState == -1) return SPEED_RANGE_LOW;   // HIGHFWDLOWREV in reverse
    return SPEED_RANGE_HIGH;
}

float speed_cal_kph(int motorRpm, int range) {
    if (range != SPEED_RANGE_LOW) range = SPEED_RANGE_HIGH;
    portENTER_CRITICAL(&lock);
    float kk = k[range];
    portEXIT_CRITICAL(&lock);
    return fabsf((float)motorRpm) * kk;
}

float speed_cal_axle_ratio(void) { return axleRatio; }
float speed_cal_tire_inches(void) { return tireInches; }
float speed_cal_transfer_ratio(void) { return transferRatio; }
float speed_cal_nominal_reduction(int range) { return RANGE_REDUCTION[range == SPEED_RANGE_LOW ? 0 : 1]; }

void speed_cal_set_axle_ratio(float ratio) {
    if (ratio < 1.0f || ratio > 9.0f) return;
    axleRatio = ratio;
    resetCalibration();
    save();
}

void speed_cal_set_tire_inches(float inches) {
    if (inches < 20.0f || inches > 45.0f) return;
    tireInches = inches;
    resetCalibration();
    save();
}

void speed_cal_set_transfer_ratio(float ratio) {
    if (ratio < 0.5f || ratio > 5.0f) return;
    transferRatio = ratio;
    resetCalibration();
    save();
}

bool speed_cal_is_calibrated(int range) {
    return calibrated[range == SPEED_RANGE_LOW ? 0 : 1];
}

// The range (MG2) reduction a K implies, with axle, transfer and tire factored out.
float speed_cal_implied_reduction(int range) {
    int r = (range == SPEED_RANGE_LOW) ? 0 : 1;
    portENTER_CRITICAL(&lock);
    float kk = k[r];
    portEXIT_CRITICAL(&lock);
    if (kk <= 0) return NAN;
    return kForReduction(1.0f) / kk;
}

void speed_cal_start(void) {
    calState = SPEED_CAL_RUNNING;
    calRange = -1;
    calCount = 0;
    calSum = calSumSq = 0;
    calStartMs = calLastSampleMs = millis();
    snprintf(status, sizeof(status), "Drive steadily above 25 mph...");
}

void speed_cal_cancel(void) {
    if (calState == SPEED_CAL_RUNNING) {
        calState = SPEED_CAL_IDLE;
        snprintf(status, sizeof(status), "Cancelled");
    }
}

speed_cal_state_t speed_cal_state(void) { return calState; }

static void fail(const char *why) {
    calState = SPEED_CAL_FAILED;
    snprintf(status, sizeof(status), "%s", why);
}

void speed_cal_poll(void) {
    if (calState != SPEED_CAL_RUNNING) return;
    uint32_t now = millis();
    if (now - calStartMs > CAL_TIMEOUT_MS) {
        fail(calCount ? "Timed out - couldn't hold a steady speed for 20 s" : "Timed out - no GPS fix / not fast enough");
        return;
    }
    if (now - calLastSampleMs < CAL_SAMPLE_MS) return;
    calLastSampleMs = now;

    int rpm = 0, gearFb = 1, dir = 1;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        rpm = myData.motorRpm;
        gearFb = myData.gearFb;
        dir = myData.dirState;
        xSemaphoreGive(dataMutex);
    }
    bool gpsOk = gpsData.hasFix && now - gpsData.lastFixMs < CAL_GPS_MAX_AGE_MS;
    if (!ui_can_freshness_isLive()) { snprintf(status, sizeof(status), "Waiting for CAN data..."); return; }
    if (!gpsOk) { snprintf(status, sizeof(status), "Waiting for GPS fix..."); return; }
    if (gpsData.speedKph < CAL_MIN_GPS_KPH || abs(rpm) < CAL_MIN_RPM) {
        snprintf(status, sizeof(status), "Speed up - need a steady 25+ mph (%d/%d s)", calCount / 5, CAL_NEED_SAMPLES / 5);
        return;
    }
    int range = speed_cal_range_for(gearFb, dir);
    if (calRange >= 0 && range != calRange) {   // the VCU shifted mid-run - start over in the new range
        calCount = 0;
        calSum = calSumSq = 0;
    }
    calRange = range;

    double kk = gpsData.speedKph / fabs((double)rpm);
    calSum += kk;
    calSumSq += kk * kk;
    calCount++;
    snprintf(status, sizeof(status), "Hold steady - %s range, %d/%d s",
             range == SPEED_RANGE_LOW ? "LOW" : "HIGH", calCount / 5, CAL_NEED_SAMPLES / 5);
    if (calCount < CAL_NEED_SAMPLES) return;

    double mean = calSum / calCount;
    double var = calSumSq / calCount - mean * mean;
    double spread = (var > 0 && mean > 0) ? sqrt(var) / mean : 0;
    if (spread > CAL_MAX_SPREAD) {
        fail("Speed wasn't steady enough - try again on a straight, level road");
        return;
    }
    float reduction = kForReduction(1.0f) / (float)mean;
    float expected = RANGE_REDUCTION[calRange];
    if (fabsf(reduction - expected) / expected > CAL_MAX_DEVIATION) {
        char why[120];
        snprintf(why, sizeof(why), "Measured %.2f:1, expected ~%.2f:1 - check GearFB, axle, tire, transfer",
                 reduction, expected);
        fail(why);
        return;
    }
    portENTER_CRITICAL(&lock);
    k[calRange] = (float)mean;
    calibrated[calRange] = true;
    portEXIT_CRITICAL(&lock);
    save();
    calState = SPEED_CAL_DONE;
    snprintf(status, sizeof(status), "%s range calibrated - measured %.2f:1 (nominal %.2f:1)",
             calRange == SPEED_RANGE_LOW ? "LOW" : "HIGH", reduction, expected);
}

const char *speed_cal_status_text(void) {
    return status;
}
