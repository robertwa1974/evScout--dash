#ifndef UI_SPEEDSCREEN_H
#define UI_SPEEDSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_speedScreen - HOME screen. Twin-binnacle instrument-cluster
// layout (2026-10-21, replacing the single centered speedometer; layout
// idea taken from mianahm3d/webUI-STM_ESPCAR's "Mehran EV" cluster - the
// concept only, none of its code, which runs on simulated data):
//   left   - 400px lv_meter speedometer, mph (converted from
//            myData.vehSpeedKph at display time only - the field stays kph
//            for the dyno screens), with the odometer above the digital
//            readout and the trip meter below it (long-press trip to reset)
//   right  - P R N D shifter strip (display-only, from myData.dirState),
//            SOC and inverter-temp needle gauges, and power / pack V / pack A /
//            12V cards
// Everything sits above the 72px dock (408px of usable height).
extern void ui_speedScreen_screen_init(void);
extern void ui_speedScreen_screen_destroy(void);
extern void ui_event_speedScreen(lv_event_t * e);
extern void ui_speedScreen_refresh_theme(void);

// Highlights the active letter in the P R N D strip from myData.dirState
// (-1=Reverse, 0=Neutral, 1=Drive, 2=Park - see PARAM_ID_DIR).
extern void ui_speedScreen_setDirState(int dirState);

// Right-column readouts - each is a no-op if the screen doesn't exist yet.
// noData: nothing has ever arrived from the VCU (dim, not red/zero - same
// rule as ui_card_setValueNoData()).
extern void ui_speedScreen_setSoc(int soc, bool warn, bool noData);
extern void ui_speedScreen_setInverterTemp(int tempC, bool warn);  // heatsinkTemp (tmphs)
// Inputs for the RANGE card (2026-10-24, replaced PACK V): SOC % and the
// VCU's BattCap (kWh). Stored only - drawn by refreshOdometer().
extern void ui_speedScreen_setRangeInputs(int soc, float battKWh);

// Whether road speed for the engaged range is GPS-calibrated (speed_cal.h).
// Uncalibrated, it's estimated from the LS600h range ratios - the unit label
// says "mph - estimated" (2026-10-24).
extern void ui_speedScreen_setSpeedCalibrated(bool calibrated);
extern void ui_speedScreen_setPackCurrent(float amps);
extern void ui_speedScreen_setPower(float kw);
extern void ui_speedScreen_setAuxVoltage(float volts);

// Moves the gauges' red warning arcs to the Settings thresholds
// (warningSet.lowSoc / .heatsinkTemp). Cheap; called from slowUpdate().
extern void ui_speedScreen_setThresholds(int lowSoc, int inverterTempWarn);

// Re-reads odometer.h - odometer, trip, trip Wh/mi caption and the RANGE
// card - redrawing only what changed. Called every slowUpdate() tick.
extern void ui_speedScreen_refreshOdometer(void);

extern lv_obj_t * ui_speedScreen;

extern lv_obj_t * ui_speedMeter;
extern lv_meter_indicator_t * ui_speedNeedle;
extern lv_obj_t * ui_speedValLabel;
extern lv_obj_t * ui_speedUnitLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
