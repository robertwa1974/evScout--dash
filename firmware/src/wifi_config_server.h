#pragma once

#include <stdbool.h>

// Local WiFi config page (2026-09-22) - lets vehicle_config.h's values
// (dyno vehicle mass/target/launch thresholds, Home/Work coordinates) be
// edited from a phone without a firmware reflash. AP MODE ONLY - the board
// creates its own local network (WIFI_CONFIG_SSID below), your phone
// connects directly to it. This is the "local network" case in CLAUDE.md's
// Networking section - the board never joins an external network or makes
// an outbound request from here. Outside-internet features, if added, are a
// separate opt-in and must not live in this file.
//
// Pages (2026-10-21 additions marked *):
//   /           vehicle_config.h editor (dyno + Home/Work)
//   /live     * live telemetry, refreshed once a second from /api/live
//   /api/live * JSON snapshot of myData + GPS
//   /logs     * data_logger.h CSV files - download / delete
//
// Off by default, toggled from the Settings screen (ui_settingsScreen.cpp's
// "WiFi Config" row) - the AP does not broadcast at all times while
// driving, only when explicitly turned on, same "explicit, not ambient"
// posture as everything else user-configurable in this project.
//
// Drive lockout (confirmed with Rob, 2026-09-22): both the page itself and
// the save handler refuse to show/accept edits while myData.vehSpeedKph >
// 0 - a phone reaching this page mid-drive can look but not touch (/live
// stays available while moving; log download/delete are locked too, since
// streaming a file blocks loop() and freezes the screen meanwhile), same
// vehicle-safety caution already applied elsewhere in this project (no
// keyboard popups while driving, 80px+ touch targets, etc.).

#define WIFI_CONFIG_SSID     "Scout80-Config"
#define WIFI_CONFIG_PASSWORD "Scout80Dash!"  // WPA2, >=8 chars required - also shown on the Settings screen's WiFi Config row

#ifdef __cplusplus
extern "C" {
#endif

// Loads the saved on/off preference from NVS and starts the AP if it was
// left on. Call once at boot (firmware.ino setup()), after
// getVehicleConfig() (the config page needs those values populated) and
// after mutex_init() (dataMutex is needed for the drive-lockout check).
void wifi_config_server_init(void);

// Starts the AP + web server. Safe to call if already running (no-op).
// Persists the "on" state immediately, same "apply + persist immediately"
// convention as every other Settings-screen switch in this project.
void wifi_config_server_start(void);

// Stops the AP + web server, frees the WiFi radio. Safe to call if not
// running (no-op). Persists the "off" state immediately.
void wifi_config_server_stop(void);

// Whether the AP is currently running - the Settings screen's switch
// reads this to show its correct initial state.
bool wifi_config_server_is_running(void);

// Why the last start() failed, or "" - start() checks that the radio and
// AP actually came up rather than assuming it (2026-10-21).
const char *wifi_config_server_last_error(void);

// Services any pending HTTP request - cheap no-op whenever the AP isn't
// running. Call every loop() iteration (firmware.ino), no uiMutex needed
// (touches only vehicle_config.h's globals and the WebServer's own
// internal state, never an LVGL object).
void wifi_config_server_poll(void);

#ifdef __cplusplus
}
#endif
