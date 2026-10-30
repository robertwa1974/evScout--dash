#include "imu_driver.h"
#include "mutex.h"
#include "zombie_updaters.h"   // myData - motion gate
#include "gps_driver.h"        // gpsData - motion gate
#include "ui_can_freshness.h"  // only trust myData's speed when CAN is live
#include "speed_cal.h"         // float road speed for the attitude filter's d(speed)/dt

#define LGFX_USE_V1
#include <LovyanGFX.hpp>  // lgfx::i2c:: primitives - same include pattern as display_driver.h
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// MPU6050 register map (public datasheet) - only the handful needed here.
#define MPU6050_ADDR             0x68  // AD0 tied low - the default on every GY-521 breakout
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B  // burst-read start: AX AY AZ TEMP GX GY GZ, 14 bytes total
#define MPU6050_REG_PWR_MGMT1    0x6B
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_WHO_AM_I_VAL     0x68

// CONFIG DLPF_CFG=4: accel 21Hz / gyro 20Hz bandwidth, 1kHz internal rate,
// so SMPLRT_DIV=9 gives 100Hz - matching the task period below.
#define MPU6050_DLPF_CFG         0x04
#define MPU6050_SMPLRT_DIV_100HZ 9
#define MPU6050_CLK_PLL_XGYRO    0x01  // more stable than the internal 8MHz RC

static const float ACCEL_LSB_PER_G  = 16384.0f;  // +-2g
static const float GYRO_LSB_PER_DPS = 131.0f;     // +-250 deg/s

static const uint32_t SAMPLE_PERIOD_MS = 10;     // 100Hz

// Calibration run parameters.
static const uint32_t CAL_SETTLE_MS   = 1000;   // let the tap that started it die out
static const int      CAL_SAMPLES     = 200;    // 2s at 100Hz
static const float    CAL_MAX_STD_G   = 0.02f;
static const float    CAL_MIN_MAG_G   = 0.90f;
static const float    CAL_MAX_MAG_G   = 1.10f;
static const float    CAL_MIN_HORIZ   = 0.2f;   // |ez - (ez.up)up| below this -> board ~horizontal
static const float    GPS_STOPPED_KPH = 0.5f;
static const int      STOPPED_RPM     = 50;     // same threshold as sdo_client's Gear interlock
static const uint32_t GPS_FRESH_MS    = 3000;

static const uint8_t NVS_VERSION = 1;

// Attitude filter (see imu_driver.h). Time constants are how long the accel
// takes to pull the gyro estimate back: short when parked (accel is pure
// gravity then), longer when moving, longest with no speed to correct with.
static const float    ATT_TAU_STOPPED_S  = 0.5f;
static const float    ATT_TAU_MOVING_S   = 2.0f;
static const float    ATT_TAU_UNCOMP_S   = 4.0f;
static const float    ATT_MAX_ACCEL_DEV_G = 0.3f;  // | |f| - 1g | above this: skip the accel correction
static const float    ATT_STOPPED_MPS    = 0.3f;
static const float    ATT_STILL_DPS      = 2.0f;
// With no speed source, "uncorrected" is only flagged while the IMU itself
// sees motion - any rotation above this, or |a| this far off the rest
// magnitude - held for ATT_MOTION_HOLD_MS so it doesn't flicker. A steady
// straight cruise isn't detected, but then there's nothing to correct.
static const float    ATT_MOTION_DPS     = 3.0f;
static const float    ATT_MOTION_G       = 0.05f;
static const uint32_t ATT_MOTION_HOLD_MS = 3000;
static const float    G_MPS2             = 9.80665f;
static const uint32_t ATT_SPEED_STEP_MS  = 100;    // CAN speed sampled into a ring this often...
static const int      ATT_SPEED_RING     = 5;      // ...and differentiated across it (400ms): rpm only updates ~200-400ms

ImuSample imuData = {0};
static bool imuOk = false;

typedef struct {
    bool  valid;
    bool  fallback;
    float fwd[3], left[3], up[3];  // un-inverted basis, in chip axes
    float gyroBias[3];
    float tiltDeg;
    float gRef;                    // rest magnitude - subtracted from a_vert
} ImuCal;

typedef enum {
    FAIL_NONE,
    FAIL_NO_IMU,
    FAIL_MOVING_CAN,
    FAIL_MOVING_GPS,
    FAIL_NOISY,
    FAIL_MAGNITUDE,
} cal_fail_t;

// Everything below is shared between the sampling task and the UI (loop())
// - guarded by calLock, held only for small copies.
static portMUX_TYPE calLock = portMUX_INITIALIZER_UNLOCKED;
static ImuCal activeCal, savedCal;
static bool activeInv = false, savedInv = false;
static bool dirty = false;
static imu_cal_state_t runState = IMU_CAL_IDLE;
static bool startRequested = false;
static int collected = 0;
static cal_fail_t failReason = FAIL_NONE;
static float failValue = 0;  // the offending std/magnitude, for the status text

static Preferences prefs;  // own instance, own namespace - same reasoning as vehicle_config.cpp

// --- small vector helpers --------------------------------------------------

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void cross3(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// out = v - (v.up)up, returns its length.
static float rejectFrom(const float *v, const float *up, float *out) {
    float d = dot3(v, up);
    for (int i = 0; i < 3; i++) out[i] = v[i] - d * up[i];
    return sqrtf(dot3(out, out));
}

// --- I2C -------------------------------------------------------------------

static bool writeReg(uint8_t reg, uint8_t value) {
    return lgfx::i2c::writeRegister8(I2C_NUM_0, MPU6050_ADDR, reg, value).has_value();
}

// --- NVS -------------------------------------------------------------------

static void loadCal(void) {
    ImuCal c;
    memset(&c, 0, sizeof(c));
    prefs.begin("imu", true);
    bool inv = prefs.getBool("inv_fwd", false);
    if (prefs.getUChar("ver", 0) == NVS_VERSION) {
        float basis[9];
        bool ok = prefs.getBytes("basis", basis, sizeof(basis)) == sizeof(basis) &&
                  prefs.getBytes("gyro_bias", c.gyroBias, sizeof(c.gyroBias)) == sizeof(c.gyroBias);
        if (ok) {
            memcpy(c.fwd, &basis[0], sizeof(c.fwd));
            memcpy(c.left, &basis[3], sizeof(c.left));
            memcpy(c.up, &basis[6], sizeof(c.up));
            c.tiltDeg = prefs.getFloat("tilt_deg", 0);
            c.gRef = prefs.getFloat("g_ref", 1.0f);
            c.fallback = prefs.getBool("fallback", false);
            c.valid = true;
        }
    }
    prefs.end();

    portENTER_CRITICAL(&calLock);
    activeCal = savedCal = c;
    activeInv = savedInv = inv;
    dirty = false;
    portEXIT_CRITICAL(&calLock);
}

void imu_cal_save(void) {
    portENTER_CRITICAL(&calLock);
    ImuCal c = activeCal;
    bool inv = activeInv;
    portEXIT_CRITICAL(&calLock);

    prefs.begin("imu", false);
    prefs.putBool("inv_fwd", inv);  // a mounting preference - kept across Clear
    if (c.valid) {
        float basis[9];
        memcpy(&basis[0], c.fwd, sizeof(c.fwd));
        memcpy(&basis[3], c.left, sizeof(c.left));
        memcpy(&basis[6], c.up, sizeof(c.up));
        prefs.putBytes("basis", basis, sizeof(basis));
        prefs.putBytes("gyro_bias", c.gyroBias, sizeof(c.gyroBias));
        prefs.putFloat("tilt_deg", c.tiltDeg);
        prefs.putFloat("g_ref", c.gRef);
        prefs.putBool("fallback", c.fallback);
        prefs.putUChar("ver", NVS_VERSION);
    } else {
        prefs.remove("ver");  // loadCal() treats a missing ver as uncalibrated
        prefs.remove("basis");
        prefs.remove("gyro_bias");
        prefs.remove("tilt_deg");
        prefs.remove("g_ref");
        prefs.remove("fallback");
    }
    prefs.end();

    portENTER_CRITICAL(&calLock);
    savedCal = c;
    savedInv = inv;
    dirty = false;
    if (runState == IMU_CAL_PASSED) runState = IMU_CAL_IDLE;
    portEXIT_CRITICAL(&calLock);
}

// --- motion gate -----------------------------------------------------------

// The accel noise check catches real motion on its own; this is the cheap
// early "you're driving" answer, and the only check that sees a perfectly
// smooth cruise. Either source alone is enough to reject.
static cal_fail_t motionCheck(void) {
    bool canLive = ui_can_freshness_isLive();
    int rpm = 0, kph = 0;
    bool gpsFresh = false;
    float gpsKph = 0;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        rpm = myData.motorRpm;
        kph = myData.vehSpeedKph;
        gpsFresh = gpsData.hasFix && (millis() - gpsData.lastFixMs) < GPS_FRESH_MS;
        gpsKph = gpsData.speedKph;
        xSemaphoreGive(dataMutex);
    }
    if (canLive && (kph != 0 || abs(rpm) > STOPPED_RPM)) return FAIL_MOVING_CAN;
    if (gpsFresh && gpsKph >= GPS_STOPPED_KPH) return FAIL_MOVING_GPS;
    return FAIL_NONE;
}

// --- calibration math ------------------------------------------------------

static double sumA[3], sumA2[3], sumG[3];

static void finishCalibration(void) {
    const double n = CAL_SAMPLES;
    float mean[3], gyro[3], maxStd = 0;
    for (int i = 0; i < 3; i++) {
        double m = sumA[i] / n;
        double var = sumA2[i] / n - m * m;
        float sd = var > 0 ? (float)sqrt(var) : 0.0f;
        if (sd > maxStd) maxStd = sd;
        mean[i] = (float)m;
        gyro[i] = (float)(sumG[i] / n);
    }
    float mag = sqrtf(dot3(mean, mean));

    cal_fail_t fail = motionCheck();
    float value = 0;
    if (fail == FAIL_NONE && maxStd > CAL_MAX_STD_G) { fail = FAIL_NOISY; value = maxStd; }
    if (fail == FAIL_NONE && (mag < CAL_MIN_MAG_G || mag > CAL_MAX_MAG_G)) { fail = FAIL_MAGNITUDE; value = mag; }

    ImuCal c;
    memset(&c, 0, sizeof(c));
    if (fail == FAIL_NONE) {
        for (int i = 0; i < 3; i++) c.up[i] = mean[i] / mag;
        const float ez[3] = {0, 0, 1};
        const float ex[3] = {1, 0, 0};
        float horiz[3];
        float len = rejectFrom(ez, c.up, horiz);
        if (len < CAL_MIN_HORIZ) {
            // Board nearly horizontal: Z is ~vertical, so use X instead. Its
            // projection is then ~unit length (X is ~perpendicular to up).
            len = rejectFrom(ex, c.up, horiz);
            c.fallback = true;
        }
        for (int i = 0; i < 3; i++) c.fwd[i] = horiz[i] / len;
        cross3(c.up, c.fwd, c.left);
        memcpy(c.gyroBias, gyro, sizeof(gyro));
        float s = dot3(ez, c.up);
        s = s > 1 ? 1 : (s < -1 ? -1 : s);
        c.tiltDeg = asinf(s) * 180.0f / (float)M_PI;
        c.gRef = mag;
        c.valid = true;
    }

    portENTER_CRITICAL(&calLock);
    if (runState == IMU_CAL_COLLECTING) {  // not cancelled meanwhile
        failReason = fail;
        failValue = value;
        if (fail == FAIL_NONE) {
            activeCal = c;  // preview - Save persists it, Cancel reverts
            dirty = true;
            runState = IMU_CAL_PASSED;
        } else {
            runState = IMU_CAL_FAILED;
        }
    }
    portEXIT_CRITICAL(&calLock);

#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[imu] cal: fail=%d mean=(%.3f,%.3f,%.3f) |a|=%.3f maxStd=%.4f tilt=%.1f fallback=%d\n",
                  (int)fail, mean[0], mean[1], mean[2], mag, maxStd, c.tiltDeg, (int)c.fallback);
#endif
}

// --- attitude filter -------------------------------------------------------

// Published result - under calLock, same as the calibration state.
static ImuAttitude attOut = {0};

// Filter state - sampling task only.
static bool  attInit = false;
static float attPitch = 0, attRoll = 0;
static uint32_t attLastMs = 0;
static float canRing[ATT_SPEED_RING];
static int   canRingN = 0, canRingHead = 0;
static uint32_t canRingLastMs = 0;
static uint32_t gpsLastFixMs = 0;
static float gpsPrevMps = 0;
static float kinLongMps2 = 0;  // held between speed updates
static uint32_t lastMotionMs = 0;
static bool sawMotion = false;

static void publishAttitude(const ImuAttitude &a) {
    portENTER_CRITICAL(&calLock);
    attOut = a;
    portEXIT_CRITICAL(&calLock);
}

// Signed road speed (m/s, + = forward) and its derivative. Returns false
// when there's no speed source at all.
static bool roadSpeed(uint32_t now, float *mps) {
    bool canLive = ui_can_freshness_isLive();
    int rpm = 0, gearFb = 0, dir = 0;
    bool gpsFresh = false;
    float gpsKph = 0;
    uint32_t fixMs = 0;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        rpm = myData.motorRpm;
        gearFb = myData.gearFb;
        dir = myData.dirState;
        fixMs = gpsData.lastFixMs;
        gpsFresh = gpsData.hasFix && (now - fixMs) < GPS_FRESH_MS;
        gpsKph = gpsData.speedKph;
        xSemaphoreGive(dataMutex);
    }

    if (canLive) {
        float v = speed_cal_kph(rpm, speed_cal_range_for(gearFb, dir)) / 3.6f;
        if (dir == -1) v = -v;
        if (now - canRingLastMs >= ATT_SPEED_STEP_MS) {
            canRingLastMs = now;
            canRing[canRingHead] = v;
            canRingHead = (canRingHead + 1) % ATT_SPEED_RING;
            if (canRingN < ATT_SPEED_RING) canRingN++;
            if (canRingN == ATT_SPEED_RING) {
                // canRingHead now points at the oldest entry
                float oldest = canRing[canRingHead];
                kinLongMps2 = (v - oldest) / ((ATT_SPEED_RING - 1) * ATT_SPEED_STEP_MS / 1000.0f);
            }
        }
        gpsLastFixMs = 0;
        *mps = v;
        return true;
    }
    if (canRingN) {  // CAN just dropped - don't carry its last derivative over
        canRingN = 0;
        kinLongMps2 = 0;
    }

    if (gpsFresh) {
        float v = gpsKph / 3.6f;  // GPS speed is unsigned - assume forward
        if (fixMs != gpsLastFixMs) {
            if (gpsLastFixMs != 0 && fixMs > gpsLastFixMs) {
                kinLongMps2 = (v - gpsPrevMps) / ((fixMs - gpsLastFixMs) / 1000.0f);
            }
            gpsLastFixMs = fixMs;
            gpsPrevMps = v;
        }
        *mps = v;
        return true;
    }
    gpsLastFixMs = 0;
    kinLongMps2 = 0;
    *mps = 0;
    return false;
}

static void updateAttitude(uint32_t now) {
    ImuAttitude out;
    memset(&out, 0, sizeof(out));

    ImuVehicleAccel v;
    if (!imu_get_vehicle_accel(&v)) {
        attInit = false;
        publishAttitude(out);
        return;
    }
    portENTER_CRITICAL(&calLock);
    float gRef = activeCal.gRef;
    portEXIT_CRITICAL(&calLock);

    float mps = 0;
    bool haveSpeed = roadSpeed(now, &mps);
    float kinLongG = haveSpeed ? kinLongMps2 / G_MPS2 : 0;
    float kinLatG = haveSpeed ? mps * (v.yawDps * (float)M_PI / 180.0f) / G_MPS2 : 0;

    // Specific force minus the truck's own acceleration = gravity direction.
    float fx = v.aLongG - kinLongG;
    float fy = v.aLatG - kinLatG;
    float fz = v.aVertG + gRef;
    float mag = sqrtf(fx * fx + fy * fy + fz * fz);
    const float RAD2DEG = 180.0f / (float)M_PI;
    float accPitch = atan2f(fx, sqrtf(fy * fy + fz * fz)) * RAD2DEG;
    float accRoll = atan2f(fy, fz) * RAD2DEG;

    // pitchDps is about +left: positive tips the nose DOWN (fwd x left = up).
    float noseUpDps = -v.pitchDps;
    float leftUpDps = v.rollDps;

    if (!attInit) {
        attPitch = accPitch;
        attRoll = accRoll;
        attInit = true;
    } else {
        float dt = (now - attLastMs) / 1000.0f;
        if (dt < 0 || dt > 0.1f) dt = 0.1f;
        attPitch += noseUpDps * dt;
        attRoll += leftUpDps * dt;

        bool still = fabsf(mps) < ATT_STOPPED_MPS && fabsf(noseUpDps) < ATT_STILL_DPS &&
                     fabsf(leftUpDps) < ATT_STILL_DPS && fabsf(v.yawDps) < ATT_STILL_DPS;
        float tau = still ? ATT_TAU_STOPPED_S : (haveSpeed ? ATT_TAU_MOVING_S : ATT_TAU_UNCOMP_S);
        if (fabsf(mag - 1.0f) < ATT_MAX_ACCEL_DEV_G) {
            float k = dt / (tau + dt);
            attPitch += k * (accPitch - attPitch);
            attRoll += k * (accRoll - attRoll);
        }
    }
    attLastMs = now;

    out.valid = true;
    float rawMag = sqrtf(v.aLongG * v.aLongG + v.aLatG * v.aLatG + fz * fz);
    if (fabsf(noseUpDps) > ATT_MOTION_DPS || fabsf(leftUpDps) > ATT_MOTION_DPS ||
        fabsf(v.yawDps) > ATT_MOTION_DPS || fabsf(rawMag - gRef) > ATT_MOTION_G) {
        lastMotionMs = now;
        sawMotion = true;
    }
    bool moving = sawMotion && (now - lastMotionMs) < ATT_MOTION_HOLD_MS;

    out.compensated = haveSpeed || !moving;
    out.pitchDeg = attPitch;
    out.rollDeg = attRoll;
    out.sampleMs = now;
    publishAttitude(out);
}

// --- sampling task ---------------------------------------------------------

static void imuTask(void *arg) {
    (void)arg;
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t settleUntil = 0;
    bool collecting = false;

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        uint8_t raw[14];
        bool ok = false;
        if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
            ok = lgfx::i2c::readRegister(I2C_NUM_0, MPU6050_ADDR, MPU6050_REG_ACCEL_XOUT_H, raw, 14).has_value();
            xSemaphoreGive(i2cMutex);
        }
        if (!ok) continue;

        // Burst layout: AX AY AZ TEMP GX GY GZ, each a big-endian signed int16.
        // raw[6..7] (temperature) is read but unused here.
        float a[3], g[3];
        for (int i = 0; i < 3; i++) {
            a[i] = (int16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]) / ACCEL_LSB_PER_G;
            g[i] = (int16_t)((raw[8 + i * 2] << 8) | raw[9 + i * 2]) / GYRO_LSB_PER_DPS;
        }
        uint32_t now = millis();

        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            imuData.axG = a[0];
            imuData.ayG = a[1];
            imuData.azG = a[2];
            imuData.gxDps = g[0];
            imuData.gyDps = g[1];
            imuData.gzDps = g[2];
            imuData.sampleMs = now;
            xSemaphoreGive(dataMutex);
        }

        // Calibration accumulation - runs here so it sees every sample at
        // the real 100Hz, with no LVGL timer cadence in the way.
        portENTER_CRITICAL(&calLock);
        bool start = startRequested;
        startRequested = false;
        if (start) collected = 0;  // here, not only in start(): pairs the count with the sums reset below
        bool stillRunning = runState == IMU_CAL_COLLECTING;
        portEXIT_CRITICAL(&calLock);

        if (start) {
            memset(sumA, 0, sizeof(sumA));
            memset(sumA2, 0, sizeof(sumA2));
            memset(sumG, 0, sizeof(sumG));
            settleUntil = now + CAL_SETTLE_MS;
            collecting = true;
        }
        if (!stillRunning) collecting = false;  // cancelled

        updateAttitude(now);
        if (collecting && (int32_t)(now - settleUntil) >= 0) {
            for (int i = 0; i < 3; i++) {
                sumA[i] += a[i];
                sumA2[i] += (double)a[i] * a[i];
                sumG[i] += g[i];
            }
            portENTER_CRITICAL(&calLock);
            int n = ++collected;
            portEXIT_CRITICAL(&calLock);
            if (n >= CAL_SAMPLES) {
                collecting = false;
                finishCalibration();
            }
        }

#if defined(DEBUG) || defined(CAN_TRACE)
        // Rate-limited to ~1Hz - enough to watch values move while
        // bench-testing, without flooding the log at the full sample rate.
        static uint32_t lastPrintMs = 0;
        if (now - lastPrintMs >= 1000) {
            lastPrintMs = now;
            ImuVehicleAccel v;
            imu_get_vehicle_accel(&v);
            ImuAttitude att;
            imu_get_attitude(&att);
            Serial.printf("[imu] a=(%.2f,%.2f,%.2f)g  g=(%.1f,%.1f,%.1f)dps  long/lat/vert=%s(%.2f,%.2f,%.2f)  pitch/roll=%.1f/%.1f%s\n",
                          a[0], a[1], a[2], g[0], g[1], g[2],
                          v.valid ? "" : "UNCAL", v.aLongG, v.aLatG, v.aVertG,
                          att.pitchDeg, att.rollDeg, att.compensated ? "" : " (uncomp)");
        }
#endif
    }
}

// --- public API ------------------------------------------------------------

bool imu_begin(void) {
    loadCal();  // even with no IMU, so Settings shows the saved state consistently

    bool wrote = false, readOk = false;
    uint8_t whoAmI = 0;
    if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
        wrote = writeReg(MPU6050_REG_PWR_MGMT1, MPU6050_CLK_PLL_XGYRO);  // wake (boots asleep) + PLL clock
        if (wrote) {
            delay(5);  // PLL lock after wake
            readOk = lgfx::i2c::readRegister(I2C_NUM_0, MPU6050_ADDR, MPU6050_REG_WHO_AM_I, &whoAmI, 1).has_value();
            wrote = writeReg(MPU6050_REG_CONFIG, MPU6050_DLPF_CFG) &&
                    writeReg(MPU6050_REG_SMPLRT_DIV, MPU6050_SMPLRT_DIV_100HZ) &&
                    writeReg(MPU6050_REG_GYRO_CONFIG, 0x00) &&   // +-250 dps
                    writeReg(MPU6050_REG_ACCEL_CONFIG, 0x00);    // +-2 g
        }
        xSemaphoreGive(i2cMutex);
    }

    imuOk = wrote && readOk && (whoAmI == MPU6050_WHO_AM_I_VAL);
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("imu_begin: wrote=%d whoAmI=0x%02X ok=%d\n", wrote, whoAmI, imuOk);
#endif
    if (imuOk) {
        // Core 0 with the CAN task, one priority above it - the work is a
        // ~0.4ms I2C read every 10ms, so it never meaningfully delays CAN.
        xTaskCreatePinnedToCore(imuTask, "TaskIMU", 4 * 1024, NULL, 2, NULL, 0);
    }
    return imuOk;
}

bool imu_present(void) { return imuOk; }

bool imu_get_vehicle_accel(ImuVehicleAccel *out) {
    memset(out, 0, sizeof(*out));
    if (!imuOk) return false;

    portENTER_CRITICAL(&calLock);
    ImuCal c = activeCal;
    bool inv = activeInv;
    portEXIT_CRITICAL(&calLock);
    if (!c.valid) return false;

    float a[3], g[3];
    uint32_t ms = 0;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        a[0] = imuData.axG; a[1] = imuData.ayG; a[2] = imuData.azG;
        g[0] = imuData.gxDps; g[1] = imuData.gyDps; g[2] = imuData.gzDps;
        ms = imuData.sampleMs;
        xSemaphoreGive(dataMutex);
    }
    if (ms == 0) return false;

    float sign = inv ? -1.0f : 1.0f;
    float w[3];
    for (int i = 0; i < 3; i++) w[i] = g[i] - c.gyroBias[i];
    out->aLongG = sign * dot3(a, c.fwd);
    out->aLatG = sign * dot3(a, c.left);
    out->aVertG = dot3(a, c.up) - c.gRef;
    out->rollDps = sign * dot3(w, c.fwd);
    out->pitchDps = sign * dot3(w, c.left);
    out->yawDps = dot3(w, c.up);
    out->sampleMs = ms;
    out->valid = true;
    return true;
}

bool imu_get_attitude(ImuAttitude *out) {
    portENTER_CRITICAL(&calLock);
    *out = attOut;
    portEXIT_CRITICAL(&calLock);
    return out->valid;
}

bool imu_calibrate_start(void) {
    cal_fail_t fail = !imuOk ? FAIL_NO_IMU : motionCheck();  // dataMutex - outside the spinlock
    portENTER_CRITICAL(&calLock);
    if (runState == IMU_CAL_PASSED) {  // a new run replaces an unsaved result
        activeCal = savedCal;
        dirty = activeInv != savedInv;
    }
    collected = 0;
    failReason = fail;
    failValue = 0;
    runState = fail == FAIL_NONE ? IMU_CAL_COLLECTING : IMU_CAL_FAILED;
    startRequested = fail == FAIL_NONE;
    portEXIT_CRITICAL(&calLock);
    return fail == FAIL_NONE;
}

imu_cal_state_t imu_cal_state(void) {
    portENTER_CRITICAL(&calLock);
    imu_cal_state_t s = runState;
    portEXIT_CRITICAL(&calLock);
    return s;
}

float imu_cal_progress(void) {
    portENTER_CRITICAL(&calLock);
    int n = collected;
    portEXIT_CRITICAL(&calLock);
    return (float)n / CAL_SAMPLES;
}

bool imu_is_calibrated(void) {
    portENTER_CRITICAL(&calLock);
    bool v = activeCal.valid;
    portEXIT_CRITICAL(&calLock);
    return v;
}

float imu_cal_tilt_deg(void) {
    portENTER_CRITICAL(&calLock);
    float t = activeCal.tiltDeg;
    portEXIT_CRITICAL(&calLock);
    return t;
}

bool imu_cal_used_fallback(void) {
    portENTER_CRITICAL(&calLock);
    bool f = activeCal.valid && activeCal.fallback;
    portEXIT_CRITICAL(&calLock);
    return f;
}

bool imu_cal_invert(void) {
    portENTER_CRITICAL(&calLock);
    bool v = activeInv;
    portEXIT_CRITICAL(&calLock);
    return v;
}

void imu_cal_set_invert(bool on) {
    portENTER_CRITICAL(&calLock);
    if (activeInv != on) {
        activeInv = on;
        dirty = true;
    }
    portEXIT_CRITICAL(&calLock);
}

bool imu_cal_dirty(void) {
    portENTER_CRITICAL(&calLock);
    bool d = dirty;
    portEXIT_CRITICAL(&calLock);
    return d;
}

void imu_cal_cancel(void) {
    portENTER_CRITICAL(&calLock);
    activeCal = savedCal;
    activeInv = savedInv;
    dirty = false;
    runState = IMU_CAL_IDLE;
    startRequested = false;
    collected = 0;
    portEXIT_CRITICAL(&calLock);
}

void imu_cal_clear(void) {
    portENTER_CRITICAL(&calLock);
    if (runState != IMU_CAL_COLLECTING) runState = IMU_CAL_IDLE;
    if (activeCal.valid) {
        memset(&activeCal, 0, sizeof(activeCal));
        dirty = true;
    }
    portEXIT_CRITICAL(&calLock);
}

void imu_cal_status_text(char *buf, size_t len) {
    portENTER_CRITICAL(&calLock);
    imu_cal_state_t s = runState;
    cal_fail_t fail = failReason;
    float value = failValue;
    int n = collected;
    float tilt = activeCal.tiltDeg;
    bool fallback = activeCal.fallback;
    portEXIT_CRITICAL(&calLock);

    switch (s) {
        case IMU_CAL_COLLECTING:
            if (n == 0) snprintf(buf, len, "Hold still...");
            else snprintf(buf, len, "Measuring... %d%%", n * 100 / CAL_SAMPLES);
            break;
        case IMU_CAL_PASSED:
            snprintf(buf, len, "PASSED - mount tilt %.1f°%s. SAVE to keep it.", tilt,
                     fallback ? " (board ~horizontal: X axis used as forward - check Invert)" : "");
            break;
        case IMU_CAL_FAILED:
            switch (fail) {
                case FAIL_NO_IMU:     snprintf(buf, len, "FAILED - IMU not detected"); break;
                case FAIL_MOVING_CAN: snprintf(buf, len, "FAILED - vehicle moving (motor rpm / speed not 0)"); break;
                case FAIL_MOVING_GPS: snprintf(buf, len, "FAILED - vehicle moving (GPS speed)"); break;
                case FAIL_NOISY:
                    snprintf(buf, len, "FAILED - vehicle moving or not level? Try again (noise %.3f g)", value);
                    break;
                case FAIL_MAGNITUDE:
                    snprintf(buf, len, "FAILED - vehicle moving or not level? Try again (|a| = %.2f g, needs 0.90-1.10)", value);
                    break;
                default: snprintf(buf, len, "FAILED"); break;
            }
            break;
        default:
            snprintf(buf, len, "Park on level ground, motor off, then CALIBRATE.");
            break;
    }
}
