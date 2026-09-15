#ifndef UI_BATTERYSCREEN_H
#define UI_BATTERYSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_batteryScreen - cell-level BMS detail (renamed from
// ui_bmsScreen 2026-09-14 as part of the Speed/Drive/Status/Battery split -
// see waveshare-dash-build.md). Still JKBMS-style (holoduke/JKBMS): a large
// SOC ring at top, a charging/discharging/idle status pill - but the
// aggregate pack voltage/current bars moved to ui_statusScreen/
// ui_driveScreen, and this screen's 4 bar rows are now genuinely cell-level:
// Max Cell Voltage, Min Cell Voltage, Cell Delta V (computed), Max Cell
// Temp - sourced from the VCU's BMS_Vmax/BMS_Vmin/BMS_Tmax SDO params
// (PARAM_ID_BMS_VMAX/_VMIN/_TMAX), not the old pack-aggregate udc/idc.
extern void ui_batteryScreen_screen_init(void);
extern void ui_batteryScreen_screen_destroy(void);
extern void ui_event_batteryScreen(lv_event_t * e);
extern void ui_batteryScreen_refresh_theme(void);

// Sets the status pill from the sign of pack current (small deadband around
// 0 to avoid flicker): negative = charging/regen, positive = discharging.
// Pack current itself has no widget on this screen anymore (see above) -
// it's still needed here as the pill's data source.
extern void ui_batteryScreen_setStatus(float packCurrent);

extern lv_obj_t * ui_batteryScreen;

extern lv_obj_t * ui_batterySocArc;
extern lv_obj_t * ui_batterySocValLabel;
// Small battery-level icon (LV_SYMBOL_BATTERY_FULL/3/2/1/EMPTY) below the
// percentage - design-review "icons" pass, 2026-09-14 (see
// waveshare-dash-build.md). Reinforces the number with a shape rather than
// leaving SOC as a bare digit on this screen (unlike Charging, which
// already has a full drawn battery shape - this is the lighter-weight
// version for a screen that keeps the arc).
extern lv_obj_t * ui_batterySocIconLabel;

extern lv_obj_t * ui_batteryStatusPill;
extern lv_obj_t * ui_batteryStatusLabel;

extern lv_obj_t * ui_batteryVMaxLabel;
extern lv_obj_t * ui_batteryVMaxBar;
extern lv_obj_t * ui_batteryVMaxValLabel;

extern lv_obj_t * ui_batteryVMinLabel;
extern lv_obj_t * ui_batteryVMinBar;
extern lv_obj_t * ui_batteryVMinValLabel;

extern lv_obj_t * ui_batteryDeltaVLabel;
extern lv_obj_t * ui_batteryDeltaVBar;
extern lv_obj_t * ui_batteryDeltaVValLabel;

extern lv_obj_t * ui_batteryTMaxLabel;
extern lv_obj_t * ui_batteryTMaxBar;
extern lv_obj_t * ui_batteryTMaxValLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
