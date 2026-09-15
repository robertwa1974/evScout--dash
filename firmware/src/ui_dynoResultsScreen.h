#ifndef UI_DYNORESULTSSCREEN_H
#define UI_DYNORESULTSSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_dynoResultsScreen - 0-60 / virtual dyno, RESULTS mode
// (CLAUDE.md). Built ONCE from the completed run's samples (dyno_data.h),
// not live - a single lv_chart (LV_CHART_TYPE_SCATTER, shared kW Y-axis)
// with two series (road-load power, electrical input power) plotted against
// vehicle speed, a manually-built legend (LVGL has no native one), peak
// power callouts, and a computed efficiency % (road-load / electrical,
// averaged over the run - the gap between the curves).
//
// Navigation: physical swipe LEFT -> back to ui_dynoLiveScreen (run again).
// This board reports gesture direction inverted from the physical swipe -
// see CLAUDE.md's "Touch gesture direction" - so the code checks
// LV_DIR_RIGHT for this physical-LEFT swipe. Intentional; don't "fix" it
// without re-verifying on hardware.
extern void ui_dynoResultsScreen_screen_init(void);
extern void ui_dynoResultsScreen_screen_destroy(void);
extern void ui_event_dynoResultsScreen(lv_event_t * e);
extern void ui_dynoResultsScreen_refresh_theme(void);

extern lv_obj_t * ui_dynoResultsScreen;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
