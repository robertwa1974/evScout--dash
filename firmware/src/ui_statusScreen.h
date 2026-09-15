#ifndef UI_STATUSSCREEN_H
#define UI_STATUSSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_statusScreen - static/at-rest telemetry, 2x3 grid (CLAUDE.md
// screen layout conventions, new 2026-09-14 for the Speed/Drive/Status/
// Battery split - see waveshare-dash-build.md). Six equal panels: SOC, Pack
// Voltage, Aux/12V Voltage, Motor Temp, Inverter (heatsink) Temp, Max
// Battery Temp.
extern void ui_statusScreen_screen_init(void);
extern void ui_statusScreen_screen_destroy(void);
extern void ui_event_statusScreen(lv_event_t * e);
extern void ui_statusScreen_refresh_theme(void);

extern lv_obj_t * ui_statusScreen;

extern lv_obj_t * ui_statusSocPanel;
extern lv_obj_t * ui_statusSocTitleLabel;
extern lv_obj_t * ui_statusSocArc;
extern lv_obj_t * ui_statusSocValLabel;

extern lv_obj_t * ui_statusPackVPanel;
extern lv_obj_t * ui_statusPackVTitleLabel;
extern lv_obj_t * ui_statusPackVValLabel;
extern lv_obj_t * ui_statusPackVUnitLabel;
extern lv_obj_t * ui_statusPackVBar;

extern lv_obj_t * ui_statusAuxVPanel;
extern lv_obj_t * ui_statusAuxVTitleLabel;
extern lv_obj_t * ui_statusAuxVValLabel;
extern lv_obj_t * ui_statusAuxVUnitLabel;
extern lv_obj_t * ui_statusAuxVBar;

extern lv_obj_t * ui_statusMotorTPanel;
extern lv_obj_t * ui_statusMotorTTitleLabel;
extern lv_obj_t * ui_statusMotorTValLabel;
extern lv_obj_t * ui_statusMotorTUnitLabel;
extern lv_obj_t * ui_statusMotorTBar;

extern lv_obj_t * ui_statusInvTPanel;
extern lv_obj_t * ui_statusInvTTitleLabel;
extern lv_obj_t * ui_statusInvTValLabel;
extern lv_obj_t * ui_statusInvTUnitLabel;
extern lv_obj_t * ui_statusInvTBar;

extern lv_obj_t * ui_statusMaxBattTPanel;
extern lv_obj_t * ui_statusMaxBattTTitleLabel;
extern lv_obj_t * ui_statusMaxBattTValLabel;
extern lv_obj_t * ui_statusMaxBattTUnitLabel;
extern lv_obj_t * ui_statusMaxBattTBar;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
