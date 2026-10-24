#pragma once

#include <stdbool.h>

// ============================================================================
// Road speed from motor rpm, with GPS calibration (2026-10-24)
// ============================================================================
// Why: on Rob's ZombieVerter (Vehicle = Classic) Veh_Speed (2017) is never
// set - in Stm32-vcu only the BMW_E31/E39/E65 vehicle classes write it, from
// the BMW's own wheel-speed CAN. What the VCU does report is `speed` (2016),
// the motor (MG2) rpm. So the dash computes road speed itself and writes it
// into myData.vehSpeedKph, which every consumer already uses (speedometer,
// odometer/efficiency, dyno, drive locks). The VCU's own Veh_Speed is kept
// separately in myData.vcuVehSpeedKph for diagnostics.
//
//   kph = |motor rpm| x K[range]
//
// The LS600h L110F transaxle has a two-speed MG2 reduction (Lo / Hi range -
// the VCU's Gear LOW/HIGH/AUTO), so there are two K values, picked from the
// VCU's GearFB (2047). Nominal reductions from ORNL/TM-2008/185 (2008 LS 600h
// teardown, section 2.3): LOW 3.90:1 (exact, from tooth counts), HIGH ~1.9:1.
// The LS600h transfer case is a planetary centre differential, not a
// reduction, so its ratio defaults to 1.00:1 - it's a setting anyway for
// other drivetrains.
//
// Until a range is GPS-calibrated, K is ESTIMATED from those ratios plus
// axle and tire (typically a few % high: a tire's loaded rolling diameter is
// smaller than nominal). "Calibrate from GPS" averages GPS speed / motor rpm
// while driving steadily, per range, and stores the measured K - it absorbs
// rolling diameter and confirms the HIGH ratio. Changing axle, tire or
// transfer ratio resets both ranges to the estimate.
//
// Settings live in NVS (namespace "spdcal"). speed_cal_poll() runs the GPS
// calibration and must be called from loop() (it reads gpsData).
// ============================================================================

#define SPEED_RANGE_LOW  0
#define SPEED_RANGE_HIGH 1

typedef enum {
    SPEED_CAL_IDLE,
    SPEED_CAL_RUNNING,
    SPEED_CAL_DONE,
    SPEED_CAL_FAILED,
} speed_cal_state_t;

#ifdef __cplusplus
extern "C" {
#endif

void speed_cal_init(void);

// Road speed for a motor rpm in the given range. Safe from any task.
float speed_cal_kph(int motorRpm, int range);

// Which range the VCU says is engaged: GearFB (0=LOW, 1=HIGH, 2=AUTO,
// 3=HIGHFWDLOWREV) plus direction (-1 = reverse) for the 3 case.
int speed_cal_range_for(int gearFb, int dirState);

float speed_cal_axle_ratio(void);
float speed_cal_tire_inches(void);
float speed_cal_transfer_ratio(void);
// All three persist and reset the GPS calibration of both ranges.
void speed_cal_set_axle_ratio(float ratio);
void speed_cal_set_tire_inches(float inches);
void speed_cal_set_transfer_ratio(float ratio);

// The published reduction for a range (3.90 LOW / 1.90 HIGH).
float speed_cal_nominal_reduction(int range);

bool speed_cal_is_calibrated(int range);
// The range (MG2) reduction implied by a range's K, with axle, transfer
// ratio and tire factored out - comparable to speed_cal_nominal_reduction().
float speed_cal_implied_reduction(int range);

// GPS calibration - start from the UI, then poll() from loop().
void speed_cal_start(void);
void speed_cal_cancel(void);
void speed_cal_poll(void);
speed_cal_state_t speed_cal_state(void);
// One-line progress/result text, valid until the next call.
const char *speed_cal_status_text(void);

#ifdef __cplusplus
}
#endif
