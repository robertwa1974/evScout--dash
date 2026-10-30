#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// MPU6050 6-DoF accelerometer/gyro driver + mount-level calibration.
//
// Driver (2026-09-14): wakes the chip and samples it into imuData. Reworked
// 2026-10-28: explicit range/filter config (+-2g, +-250dps, DLPF ~21Hz,
// 100Hz sample rate) and a FreeRTOS task instead of the old 50Hz Ticker -
// the Ticker ran in the esp_timer task and blocked on uiMutex, i.e. it
// stalled (along with every other esp_timer callback) for as long as loop()
// held uiMutex, which is 150-200ms+ during a NAV tile load or map render.
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
// Concurrency: every runtime I2C transaction is under i2cMutex (mutex.h) -
// this task's reads, and touchpad_read()'s display.getTouch() (which runs
// inside lv_timer_handler(), so loop() takes it while already holding
// uiMutex; this task never takes uiMutex, so there's no lock-order issue).
// The CH422G writes only happen in setup(), before this task exists.
//
// ---------------------------------------------------------------------------
// Mount-level calibration ("IMU leveling", 2026-10-28)
// ---------------------------------------------------------------------------
// The board sits parallel to the back of the display in a near-vertical
// dash, so chip Z (board normal) points roughly fore-aft - but the board can
// be fitted either way round and the dash leans, so no axis mapping is
// hard-coded. Dash lean leaks gravity into the fore-aft axis (g*sin(theta),
// 10 deg = 0.17g, vs. a 0.3-0.5g hard EV launch), which is what this
// removes. Parked on level ground, the calibration averages 2s of samples:
//   up   = normalize(mean accel)           (at rest the chip reads +1g up)
//   fwd  = board Z with its 'up' component removed, normalized
//          (falls back to board X if the board is nearly horizontal)
//   left = up x fwd                         (right-handed: fwd x left = up)
//   gyro bias = mean gyro
// "Invert forward" flips fwd/left at runtime, so a wrong sign is fixed by a
// toggle, never a recalibration.
//
// Staged / explicit-save, like the rest of Settings: a passed calibration,
// an Invert toggle or a Clear takes effect immediately (live preview in
// imu_get_vehicle_accel()), but only reaches NVS (namespace "imu") on
// imu_cal_save(); imu_cal_cancel() reverts to what's saved.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float axG, ayG, azG;       // accelerometer, g (+-2g full scale), raw chip axes
    float gxDps, gyDps, gzDps; // gyroscope, deg/s (+-250 dps full scale), raw chip axes
    uint32_t sampleMs;         // millis() at the last successful sample
} ImuSample;

// Guarded by dataMutex (mutex.h) - same convention as myData in
// zombie_updaters.h. Stays all-zero (sampleMs never advances) if the chip
// never answered at imu_begin() - check that before trusting non-zero data.
extern ImuSample imuData;

// Vehicle-frame view of the latest sample. Only meaningful when valid.
typedef struct {
    bool  valid;      // IMU present AND calibrated - never falls back to raw axes
    float aLongG;     // + = accelerating forward
    float aLatG;      // + = toward the vehicle's left
    float aVertG;     // ~0 at rest (the calibrated rest magnitude is subtracted)
    float rollDps;    // about fwd
    float pitchDps;   // about left
    float yawDps;     // about up
    uint32_t sampleMs;
} ImuVehicleAccel;

// Vehicle pitch/roll (inclinometer, 2026-10-29). Complementary filter run in
// the sampling task: gyro rates carry short-term motion, the accelerometer's
// gravity direction pulls it back long-term - but only after subtracting the
// acceleration the truck itself is doing, or a launch reads as a climb and a
// turn reads as body roll:
//   longitudinal: d(road speed)/dt  (speed_cal.h's rpm-derived speed while
//                 CAN is live, else GPS speed, else nothing)
//   lateral:      road speed x yaw rate (centripetal)
// Accel samples far from 1g (pothole, curb) are ignored for that step.
// Same rule as the vehicle-frame accel: never valid without a calibration.
typedef struct {
    bool  valid;        // IMU present AND calibrated
    bool  compensated;  // false = IMU sees motion but there's no speed source to correct it with
    float pitchDeg;     // + = nose up
    float rollDeg;      // + = left side up (leaning right)
    uint32_t sampleMs;
} ImuAttitude;

// Fills *out and returns out->valid. Safe from any task.
bool imu_get_attitude(ImuAttitude *out);

// Configures the MPU6050, loads the saved calibration from NVS, and starts
// the 100Hz sampling task. Call once at boot, AFTER lcd_panel_start() - that's
// what brings up I2C_NUM_0 on this board. Returns false if the chip doesn't
// answer (not wired, wrong address, bad joint); no task is started then.
bool imu_begin(void);
bool imu_present(void);

// Fills *out and returns out->valid. Safe from any task.
bool imu_get_vehicle_accel(ImuVehicleAccel *out);

typedef enum {
    IMU_CAL_IDLE,        // nothing running (check imu_is_calibrated())
    IMU_CAL_COLLECTING,  // settling + 2s of samples
    IMU_CAL_PASSED,      // result applied as a preview, awaiting save/cancel
    IMU_CAL_FAILED,      // rejected - see imu_cal_status_text()
} imu_cal_state_t;

// Starts a run. Returns false (state FAILED, reason in the status text) if
// the IMU is missing or the vehicle is moving. Discards any unsaved result.
bool imu_calibrate_start(void);
imu_cal_state_t imu_cal_state(void);
float imu_cal_progress(void);  // 0..1 while COLLECTING

bool  imu_is_calibrated(void);  // the ACTIVE calibration (saved or previewed)
float imu_cal_tilt_deg(void);   // board-normal angle above horizontal
bool  imu_cal_used_fallback(void);  // board was ~horizontal - X axis used as fwd
bool  imu_cal_invert(void);
void  imu_cal_set_invert(bool on);
bool  imu_cal_dirty(void);      // active differs from what's in NVS

void imu_cal_save(void);    // persist the active state to NVS (loop() context)
void imu_cal_cancel(void);  // abort a run and/or revert to the saved state
void imu_cal_clear(void);   // stage "uncalibrated" (Save makes it stick)

// One-line run status (progress / pass / reason for failure).
void imu_cal_status_text(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
