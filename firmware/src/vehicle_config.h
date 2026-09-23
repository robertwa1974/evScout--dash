#pragma once

// User-tunable vehicle/dyno/destination constants (2026-09-22) - previously
// hardcoded #defines scattered across ui_dynoLiveScreen.cpp,
// ui_dynoResultsScreen.cpp, and ui_destinationsScreen.cpp, each requiring a
// full edit/build/reflash cycle to change. Centralized here, NVS-backed
// (Preferences, namespace "vehcfg" - same pattern as displayPref/
// utcOffsetHours in zombie_updaters.h), and editable live from
// wifi_config_server.h's local config page - no reflash needed to tune the
// dyno's vehicle mass or update Home/Work after a move.
//
// Defaults below match what shipped as hardcoded #defines before this
// existed - unchanged behavior until someone actually edits a value via
// the config page.

#ifdef __cplusplus
extern "C" {
#endif

extern float dynoVehicleMassKg;      // default 1800.0 - road-load power's only physics constant beyond recorded samples (ui_dynoResultsScreen.cpp)
extern float dynoTargetKph;          // default 96.6 (60mph) - run ends when speed reaches this (ui_dynoLiveScreen.cpp)
extern float dynoLaunchKph;          // default 2.0 - CAN-speed launch-detection threshold (ui_dynoLiveScreen.cpp)
extern float dynoLaunchAccelDeltaG;  // default 0.15 - IMU-magnitude-deviation launch-detection threshold (ui_dynoLiveScreen.cpp)

extern double homeLat, homeLon;  // default 1377 Calle Scott, Encinitas, CA 92024 (ui_destinationsScreen.cpp)
extern double workLat, workLon;  // default 6155 El Camino Real, Carlsbad, CA 92009 (ui_destinationsScreen.cpp)

// Loads all of the above from NVS (or leaves defaults if never saved
// before). Call once at boot (firmware.ino setup()), before any screen
// that reads these is created.
void getVehicleConfig(void);

// Persists the current values of all of the above to NVS. Call after
// changing any of them (wifi_config_server.h's save handler is the only
// current caller).
void updateVehicleConfig(void);

#ifdef __cplusplus
}
#endif
