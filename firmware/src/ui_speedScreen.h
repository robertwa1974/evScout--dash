#ifndef UI_SPEEDSCREEN_H
#define UI_SPEEDSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_speedScreen - HOME screen (2026-09-14 Speed/Drive/Status/Battery
// split, replacing the old quad-grid ui_mainScreen - see waveshare-dash-
// build.md). Full-screen lv_meter speedometer (CLAUDE.md: needle + tick
// scale -> lv_meter), displayed in **mph** (converted from myData.vehSpeedKph
// at display time only - the underlying field stays in kph since the dyno
// screens depend on it), plus a P/N/F/R shifter-position pill sourced from
// myData.dirState (PARAM_ID_DIR).
extern void ui_speedScreen_screen_init(void);
extern void ui_speedScreen_screen_destroy(void);
extern void ui_event_speedScreen(lv_event_t * e);
extern void ui_speedScreen_refresh_theme(void);

// Sets the P/N/F/R pill text+color from myData.dirState
// (-1=Reverse, 0=Neutral, 1=Drive/"F"orward, 2=Park - see PARAM_ID_DIR).
extern void ui_speedScreen_setDirState(int dirState);

extern lv_obj_t * ui_speedScreen;

extern lv_obj_t * ui_speedTitleLabel;
extern lv_obj_t * ui_speedMeter;
extern lv_meter_indicator_t * ui_speedNeedle;
extern lv_obj_t * ui_speedValLabel;
extern lv_obj_t * ui_speedUnitLabel;

extern lv_obj_t * ui_dirPill;
extern lv_obj_t * ui_dirLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
