#pragma once

#include <stdint.h>
#include <stdbool.h>

// MPU6050 6-DoF accelerometer/gyro driver (2026-09-14, Phase 1 of the
// architecture doc's dyno-screen IMU requirement - see
// scout80-dash-architecture.md's hardware table and "0-60 / virtual dyno
// design summary"). This file is deliberately scoped to just the driver +
// a sampling loop that populates imuData - it does NOT yet feed
// ui_dynoLiveScreen.cpp's launch detection or acceleration curve, which
// still use the documented speed-derived placeholder. Swapping that over
// is a real design decision (sample buffering/integration strategy against
// the dyno screen's existing 100ms lv_timer cadence vs. this driver's
// faster sampling) deferred to a follow-up pass once the driver is proven
// against real hardware - the IMU was only just ordered, not yet in hand.
//
// I2C bus: GPIO8 (SDA) / GPIO9 (SCL), I2C_NUM_0 - the SAME bus the touch
// controller and CH422G I/O expander already use (see display_driver.cpp).
// Confirmed via Waveshare's own docs.waveshare.com page for this board,
// which explicitly states GPIO8/9 serve "the IO expander chip, touch
// interface, and external I2C devices" - i.e. this is the documented,
// intended way to add an I2C sensor, not a guess. Uses LovyanGFX's legacy
// lgfx::i2c:: primitives (same pattern as display_driver.cpp's
// ch422gWriteReg), never Arduino Wire - mixing i2c drivers is what caused
// this board's original boot-loop bug (see display_driver.h's comment).
//
// Concurrency: I2C bus reads happen under uiMutex, NOT a new lock. Reason:
// the touch controller reads happen inside lv_timer_handler() (via
// touchpad_read() -> display.getTouch()), which firmware.ino's loop()
// already wraps in uiMutex. Sampling the IMU from a separate Ticker
// without also taking uiMutex would let two masters race the same I2C bus
// - this isn't optional, it's how the touch reads on this board are
// already synchronized.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float axG, ayG, azG;       // accelerometer, g (+-2g full scale, power-on default)
    float gxDps, gyDps, gzDps; // gyroscope, deg/s (+-250 dps full scale, power-on default)
    uint32_t sampleMs;         // millis() at the last successful sample
} ImuSample;

// Guarded by dataMutex (mutex.h) - same convention as myData in
// zombie_updaters.h. Stays all-zero (sampleMs never advances) if the chip
// never answered at imu_init() - check that before trusting non-zero data.
extern ImuSample imuData;

// Wakes the MPU6050 (it boots in sleep mode) and checks its WHO_AM_I
// register. Call once at boot, AFTER lcd_panel_start() - that's what
// brings up I2C_NUM_0 on this board (see display_driver.cpp). Returns
// false if the chip doesn't answer (not wired yet, wrong address, bad
// solder joint) - imu_start_sampling() is still safe to call either way,
// it just no-ops until a later imu_init() call succeeds.
bool imu_init(void);

// Starts a ~50Hz Ticker (matches the architecture doc's 50-100Hz target)
// that samples the IMU and updates imuData under dataMutex. Safe to call
// even if imu_init() returned false - the sampling callback checks the
// same "did the chip ever answer" flag and no-ops rather than hammering a
// dead I2C address forever.
void imu_start_sampling(void);

#ifdef __cplusplus
}
#endif
