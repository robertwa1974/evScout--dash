#ifndef UI_DYNOLIVESCREEN_H
#define UI_DYNOLIVESCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_dynoLiveScreen - 0-60 / virtual dyno, LIVE mode (CLAUDE.md).
// Minimal chrome: one dominant elapsed-time numeral, a secondary live speed
// readout, a state badge (READY/ARMED/RUNNING/DONE). Label updates only, at
// the dedicated sample-timer's cadence - never a chart on this screen.
//
// Launch detection is speed-based (armed -> speed departs ~0 -> start timing)
// rather than accelerometer-based - there's no IMU on this board yet (see
// scout80-dash-architecture.md's dyno design, which assumes one). This is a
// documented placeholder for that, not a permanent design choice.
//
// Navigation: physical swipe LEFT -> GPS screen (back) - was Charging
// screen before GPS was inserted between them (2026-09-14). This board
// reports gesture direction inverted
// from the physical swipe - see CLAUDE.md's "Touch gesture direction" - so
// the code checks LV_DIR_RIGHT for this physical-LEFT swipe. Intentional;
// don't "fix" it without re-verifying on hardware. On DONE, auto-advances
// to ui_dynoResultsScreen (see dyno_data.h for how the run handoff works).
extern void ui_dynoLiveScreen_screen_init(void);
extern void ui_dynoLiveScreen_screen_destroy(void);
extern void ui_event_dynoLiveScreen(lv_event_t * e);
extern void ui_dynoLiveScreen_refresh_theme(void);

extern lv_obj_t * ui_dynoLiveScreen;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
