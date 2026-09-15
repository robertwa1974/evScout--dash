#include "imu_driver.h"
#include "mutex.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>  // lgfx::i2c:: primitives - same include pattern as display_driver.h
#include <Ticker.h>
#include <Arduino.h>

// MPU6050 register map (public datasheet) - only the handful needed here.
#define MPU6050_ADDR             0x68  // AD0 tied low - the default on every GY-521 breakout
#define MPU6050_REG_PWR_MGMT1    0x6B
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_REG_ACCEL_XOUT_H 0x3B  // burst-read start: AX AY AZ TEMP GX GY GZ, 14 bytes total
#define MPU6050_WHO_AM_I_VAL     0x68

// Power-on default full-scale ranges (not reconfigured - plenty of
// headroom for launch detection / an acceleration curve, no need to widen
// these for this project).
static const float ACCEL_LSB_PER_G  = 16384.0f;  // +-2g
static const float GYRO_LSB_PER_DPS = 131.0f;     // +-250 deg/s

ImuSample imuData = {0};
static bool imuOk = false;
static Ticker imuTicker;

bool imu_init(void) {
    bool wrote = false;
    uint8_t whoAmI = 0;
    bool readOk = false;

    // I2C bus access happens under uiMutex - see imu_driver.h's
    // concurrency comment for why (shared bus with the touch controller).
    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        wrote = lgfx::i2c::writeRegister8(I2C_NUM_0, MPU6050_ADDR, MPU6050_REG_PWR_MGMT1, 0x00).has_value();
        if (wrote) {
            readOk = lgfx::i2c::readRegister(I2C_NUM_0, MPU6050_ADDR, MPU6050_REG_WHO_AM_I, &whoAmI, 1).has_value();
        }
        xSemaphoreGive(uiMutex);
    }

    imuOk = wrote && readOk && (whoAmI == MPU6050_WHO_AM_I_VAL);
#ifdef DEBUG
    Serial.printf("imu_init: wrote=%d whoAmI=0x%02X ok=%d\n", wrote, whoAmI, imuOk);
#endif
    return imuOk;
}

static void sampleImu() {
    if (!imuOk) return;  // never answered at imu_init() - don't hammer a dead bus address every tick

    uint8_t raw[14];
    bool ok = false;

    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        ok = lgfx::i2c::readRegister(I2C_NUM_0, MPU6050_ADDR, MPU6050_REG_ACCEL_XOUT_H, raw, 14).has_value();
        xSemaphoreGive(uiMutex);
    }
    if (!ok) return;

    // Burst layout: AX AY AZ TEMP GX GY GZ, each a big-endian signed int16.
    // raw[6..7] (temperature) is read but unused here.
    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t gx = (int16_t)((raw[8] << 8) | raw[9]);
    int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        imuData.axG = ax / ACCEL_LSB_PER_G;
        imuData.ayG = ay / ACCEL_LSB_PER_G;
        imuData.azG = az / ACCEL_LSB_PER_G;
        imuData.gxDps = gx / GYRO_LSB_PER_DPS;
        imuData.gyDps = gy / GYRO_LSB_PER_DPS;
        imuData.gzDps = gz / GYRO_LSB_PER_DPS;
        imuData.sampleMs = millis();
        xSemaphoreGive(dataMutex);
    }
}

void imu_start_sampling(void) {
    imuTicker.attach_ms(20, sampleImu);  // ~50Hz - architecture doc targets 50-100Hz
}
