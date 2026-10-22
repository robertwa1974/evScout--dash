#pragma once

#include <stdbool.h>

// ============================================================================
// Odometer + trip meter (2026-10-21)
// ============================================================================
// The ZombieVerter has no distance parameter, so the dash keeps its own:
// distance is integrated from Veh_Speed (myData.vehSpeedKph) while CAN is
// live, falling back to GPS ground speed when it isn't (GPS readings under
// GPS_MIN_KPH are treated as stationary drift, not motion). Started at 0 -
// the Scout hadn't entered service yet when this was built (confirmed with
// Rob); correctable later from the WiFi config page.
//
// Accuracy is bounded by the source: Veh_Speed arrives as whole kph about
// every 1.25s (zombie_updaters.h's poll table), so expect roughly 1-2%
// under-reading, not odometer-grade precision.
//
// Persistence (NVS, namespace "odo"): saved every ODO_SAVE_EVERY_MI of
// travel, on trip reset, on a config-page edit, and when the low-12V
// shutdown sequence starts (ui_shutdown.cpp calls odometer_request_save()).
// A sudden power loss without that warning loses at most one save interval.
// Once a mile keeps NVS wear negligible over the vehicle's life.
//
// All NVS writes happen in odometer_poll(), called from firmware.ino's
// loop() - request_save() only sets a flag, so it's safe from Ticker/LVGL
// callbacks. Getters are safe from any task.
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Loads saved totals. Call once in setup(), before ui_init().
void odometer_init(void);

// Integrates distance and performs any due/requested save. Call every
// loop() iteration.
void odometer_poll(void);

// Ask for a save on the next poll (low-12V shutdown path).
void odometer_request_save(void);

double odometer_total_miles(void);
double odometer_trip_miles(void);

// Zeroes the trip meter and saves. Called from the Speed screen's
// long-press on the trip readout (LVGL context - the save is deferred to
// poll like everything else).
void odometer_reset_trip(void);

// Overwrites the total (WiFi config page). Saved on the next poll.
void odometer_set_total_miles(double miles);

#ifdef __cplusplus
}
#endif
