#ifndef UI_GPSSCREEN_H
#define UI_GPSSCREEN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_gpsScreen - GPS telemetry, 2x3 grid (CLAUDE.md screen layout
// conventions, new 2026-09-14). Deliberately telemetry-only - NOT the
// IceNav-v3-style offline map screen from the architecture doc's original
// 7-screen plan, which needs map tile assets + SD card wiring neither of
// which exist in this repo yet (see waveshare-dash-build.md). This is the
// buildable-now piece: a fix-status pill, satellite count, speed, lat/lon,
// heading, and altitude, all sourced from gps_driver.h's gpsData.
extern void ui_gpsScreen_screen_init(void);
extern void ui_gpsScreen_screen_destroy(void);
extern void ui_event_gpsScreen(lv_event_t * e);
extern void ui_gpsScreen_refresh_theme(void);
// Sets the GPS-status pill + satellite-count label from zombie_updaters.cpp
// - see ui_gpsScreen.c's definition for why this is a setter rather than
// that file touching the pill/label directly (pill-state tracking for
// refresh_theme(), same pattern as ui_chargingScreen_setStatus()).
extern void ui_gpsScreen_setStatus(bool hasFix, uint8_t sats);

extern lv_obj_t * ui_gpsScreen;

extern lv_obj_t * ui_gpsStatusPanel;
extern lv_obj_t * ui_gpsStatusPill;
extern lv_obj_t * ui_gpsStatusLabel;
extern lv_obj_t * ui_gpsSatsLabel;

extern lv_obj_t * ui_gpsSpeedPanel;
extern lv_obj_t * ui_gpsSpeedValLabel;
extern lv_obj_t * ui_gpsSpeedBar;

extern lv_obj_t * ui_gpsLatPanel;
extern lv_obj_t * ui_gpsLatValLabel;

extern lv_obj_t * ui_gpsLonPanel;
extern lv_obj_t * ui_gpsLonValLabel;

extern lv_obj_t * ui_gpsHeadingPanel;
extern lv_obj_t * ui_gpsHeadingValLabel;
extern lv_obj_t * ui_gpsHeadingBar;

extern lv_obj_t * ui_gpsAltPanel;
extern lv_obj_t * ui_gpsAltValLabel;
extern lv_obj_t * ui_gpsAltBar;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
