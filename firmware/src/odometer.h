#pragma once

#include <stdbool.h>

// ============================================================================
// Odometer + trip meter (2026-10-21)
// ============================================================================
// The ZombieVerter has no distance parameter, so the dash keeps its own:
// distance is integrated from road speed (myData.vehSpeedKph - computed from
// motor rpm by speed_cal.h, since the VCU's Veh_Speed is never set) while CAN is
// live, falling back to GPS ground speed when it isn't (GPS readings under
// GPS_MIN_KPH are treated as stationary drift, not motion). Started at 0 -
// the Scout hadn't entered service yet when this was built (confirmed with
// Rob); correctable later from the WiFi config page.
//
// Accuracy is bounded by the source: Veh_Speed arrives as whole kph about
// every 1.25s (zombie_updaters.h's poll table), so expect roughly 1-2%
// under-reading, not odometer-grade precision.
//
// Persistence (NVS, namespace "odo"): saved every 0.25 mi of travel, 3 s
// after the vehicle stops (2026-10-24 - so an instant key-off on
// ignition-switched power loses nothing), on trip reset, on a config-page
// edit, when the low-12V shutdown sequence starts (ui_shutdown.cpp calls
// odometer_request_save()), and before a WiFi firmware update reboots.
// A power cut while moving loses at most 0.25 mi. NVS wear stays far below
// flash endurance over the vehicle's life.
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

// --- Efficiency and range (2026-10-24) -------------------------------------
// Energy is integrated from the VCU's power (myData.powerKw; regen is
// negative) over "measured" distance only: CAN live AND not charging
// (opmode 4). GPS-only distance and charging don't count, so a plug-in
// session or a CAN dropout can't distort the figures. Idle draw counts
// while moving and for 60 s after the last movement (stops at lights), but
// not a VCU left in Run while parked/on the bench. Saved alongside the
// odometer; the recent window is RAM-only and refills after a reboot.
//
// Each returns NAN until there's enough distance to mean anything.

// Since the last trip reset (reset with odometer_reset_trip()); needs 0.5 mi.
double odometer_trip_wh_per_mile(void);

// Since the dash first ran this code; needs 1 mi.
double odometer_lifetime_wh_per_mile(void);

// Last ~10 mi in 0.2 mi segments; falls back to lifetime until 1 mi of
// recent data exists. This is what drives the range estimate.
double odometer_recent_wh_per_mile(void);

// SOC x battery capacity / recent Wh/mi, to 0% SOC, capped at 999.
// battKWh is the VCU's BattCap param (myData.battCapKWh).
double odometer_range_miles(int socPct, float battKWh);

#ifdef __cplusplus
}
#endif
