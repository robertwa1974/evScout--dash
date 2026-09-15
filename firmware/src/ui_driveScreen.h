#ifndef UI_DRIVESCREEN_H
#define UI_DRIVESCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_driveScreen - in-motion telemetry, 2x3 grid (CLAUDE.md screen
// layout conventions, new 2026-09-14 for the Speed/Drive/Status/Battery
// split - see waveshare-dash-build.md). Six equal panels: Power, Pack
// Current, Gear Selection, Motor Mode, Regen Limit, and one spare slot
// reserved for a future field.
extern void ui_driveScreen_screen_init(void);
extern void ui_driveScreen_screen_destroy(void);
extern void ui_event_driveScreen(lv_event_t * e);
extern void ui_driveScreen_refresh_theme(void);

extern lv_obj_t * ui_driveScreen;

extern lv_obj_t * ui_drivePowerPanel;
extern lv_obj_t * ui_drivePowerTitleLabel;
extern lv_obj_t * ui_drivePowerValLabel;
extern lv_obj_t * ui_drivePowerUnitLabel;
extern lv_obj_t * ui_drivePowerBar;

extern lv_obj_t * ui_driveCurrPanel;
extern lv_obj_t * ui_driveCurrTitleLabel;
extern lv_obj_t * ui_driveCurrValLabel;
extern lv_obj_t * ui_driveCurrUnitLabel;
extern lv_obj_t * ui_driveCurrBar;

extern lv_obj_t * ui_driveGearPanel;
extern lv_obj_t * ui_driveGearTitleLabel;
extern lv_obj_t * ui_driveGearValLabel;

extern lv_obj_t * ui_driveMotorModePanel;
extern lv_obj_t * ui_driveMotorModeTitleLabel;
extern lv_obj_t * ui_driveMotorModeValLabel;

extern lv_obj_t * ui_driveRegenPanel;
extern lv_obj_t * ui_driveRegenTitleLabel;
extern lv_obj_t * ui_driveRegenValLabel;
extern lv_obj_t * ui_driveRegenUnitLabel;
extern lv_obj_t * ui_driveRegenBar;

extern lv_obj_t * ui_driveSparePanel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
