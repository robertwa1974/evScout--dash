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
// VCU's GearFB (2047). The reduction ratios themselves aren't known, so K is
// measured: "Calibrate from GPS" averages GPS speed / motor rpm while
// driving steadily, separately for whichever range is engaged. That one
// number absorbs the reduction, any transfer-case ratio, the axle and the
// real rolling diameter.
//
// Until a range is calibrated its K comes from the axle ratio and tire
// diameter assuming a 1.00 reduction - shown as "uncalibrated". Changing
// the axle or tire setting resets both ranges to that estimate.
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
// Both persist and reset the GPS calibration of both ranges.
void speed_cal_set_axle_ratio(float ratio);
void speed_cal_set_tire_inches(float inches);

bool speed_cal_is_calibrated(int range);
// Overall reduction between motor and axle implied by a range's K
// (i.e. the transaxle/transfer-case ratio, axle ratio factored out).
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
