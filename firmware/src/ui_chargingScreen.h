#ifndef UI_CHARGINGSCREEN_H
#define UI_CHARGINGSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_chargingScreen - charging status, 2x3 grid (CLAUDE.md screen
// layout conventions, new 2026-09-14 per Rob's request - see
// waveshare-dash-build.md). Six equal panels: SOC, Charge Status (pill,
// from opmode==Charge + chgtyp + PlugDet), Charge Setpoint (Voltspnt - this
// VCU charges to a target pack VOLTAGE, not a target SOC%, confirmed by
// Rob), Charger Temperature, AC Supply Voltage, and Cable/EVSE Current
// Limit (PilotLim primary + CableLim secondary - the closest thing this
// VCU exposes to a raw "PPVal"; see the PARAM_ID_PILOTLIM/_CABLELIM
// comments in zombie_updaters.h for why there are two, not one).
extern void ui_chargingScreen_screen_init(void);
extern void ui_chargingScreen_screen_destroy(void);
extern void ui_event_chargingScreen(lv_event_t * e);
extern void ui_chargingScreen_refresh_theme(void);

// Sets the Charge Status pill from opmode/chgType/plugDet. Called from
// slowUpdate() whenever any of the three changes (or on a forced push -
// see uiGeneration in zombie_updaters.h) - no-op if the screen doesn't
// exist yet.
extern void ui_chargingScreen_setStatus(int opmode, int chgType, int plugDet);

extern lv_obj_t * ui_chargingScreen;

// SOC "battery" visual - a custom-drawn battery silhouette (body + terminal
// nub + an animated fill rect), not an lv_arc ring like every other SOC
// readout in this codebase. Deliberate design-review decision (2026-09-14,
// see waveshare-dash-build.md): the Charging screen is the one place SOC
// should visually read as "battery filling up," not just "a percentage."
extern lv_obj_t * ui_chargingSocPanel;
extern lv_obj_t * ui_chargingBatteryBody;
extern lv_obj_t * ui_chargingBatteryNub;
extern lv_obj_t * ui_chargingBatteryFill;
extern lv_obj_t * ui_chargingSocValLabel;

// Updates the battery fill (animated width tween), the percentage label,
// and the warn color. `warn` matches the same low-SOC threshold check
// every other SOC display in this codebase uses.
extern void ui_chargingScreen_setSoc(int soc, bool warn);

extern lv_obj_t * ui_chargingStatusPanel;
extern lv_obj_t * ui_chargingStatusPill;
extern lv_obj_t * ui_chargingStatusLabel;
extern lv_obj_t * ui_chargingPlugLabel;

extern lv_obj_t * ui_chargingSetpointPanel;
extern lv_obj_t * ui_chargingSetpointValLabel;
extern lv_obj_t * ui_chargingSetpointBar;

extern lv_obj_t * ui_chargingTempPanel;
extern lv_obj_t * ui_chargingTempValLabel;
extern lv_obj_t * ui_chargingTempBar;

extern lv_obj_t * ui_chargingAcVPanel;
extern lv_obj_t * ui_chargingAcVValLabel;
extern lv_obj_t * ui_chargingAcVBar;

extern lv_obj_t * ui_chargingLimPanel;
extern lv_obj_t * ui_chargingLimValLabel;
extern lv_obj_t * ui_chargingLimBar;
extern lv_obj_t * ui_chargingCableLimLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
