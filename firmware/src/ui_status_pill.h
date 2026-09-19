#pragma once

// Shared status-pill pattern (styling/UX pass, 2026-09-18). Standardizes
// the 5 previously hand-built, independently-styled pills (Speed screen's
// P/N/F/R shifter pill, GPS FIX/NO FIX, Battery charge status, Charging
// status, Dyno READY/ARMED/RUNNING/DONE) onto one 4-state color model and
// one shared 400ms bg-color fade transition (previously present on 3 of
// the 5 pills, copy-pasted identically at each - missing entirely from
// the Speed and Dyno pills, which snapped instantly).
//
// Geometry (width/height/radius) stays a per-call param - the 5 existing
// pills are deliberately different sizes for their different contexts
// (a full-width Charging status pill vs. a small 160x64 shifter pill),
// and unifying THAT wasn't asked for or warranted. Only color semantics
// and motion are unified here.

#include "lvgl.h"

typedef enum {
    UI_PILL_NEUTRAL,  // panelBorder - idle/no-state (P/N gear, no GPS fix,
                       // dyno READY, not charging/discharging)
    UI_PILL_ACTIVE,   // ui_theme_good() (green) - positive/engaged state
                       // (forward gear, GPS fix, charging, dyno RUNNING,
                       // dyno DONE - a completed run is a success, not a
                       // fault, confirmed 2026-09-18)
    UI_PILL_CAUTION,   // ui_theme_bad() (amber/orange) - secondary,
                       // notable-but-not-a-fault state (reverse gear,
                       // discharging, dyno ARMED)
    UI_PILL_FAULT,     // ui_theme_warning() (red) - a genuine fault only,
                       // same strict rule as everywhere else in this pass
                       // (styling item 15) - nothing currently maps here
                       // among the 5 existing pills; reserved for the
                       // fault banner and any future pill that needs it.
} ui_pill_state_t;

#ifdef __cplusplus
extern "C" {
#endif

// Creates the pill lv_obj (radius/w/h per caller) + a centered label +
// the shared 400ms ease-out bg-color fade transition, starting in
// UI_PILL_NEUTRAL. labelFont lets callers keep their existing per-pill
// text size (24px for most, 32px for the Speed shifter pill) - this
// doesn't touch font weight/size, only color/motion.
lv_obj_t *ui_pill_create(lv_obj_t *parent, int16_t w, int16_t h, int16_t radius,
                          lv_obj_t **outLabel, const lv_font_t *labelFont);

// Sets the pill's bg color from state and the label's text in one call -
// every existing pill's own state-decision logic (e.g. "packCurrent <
// -0.5 => charging") stays local to its own screen/updater file; only the
// color/text application is centralized here.
void ui_pill_setState(lv_obj_t *pill, lv_obj_t *label, ui_pill_state_t state, const char *text);

// Re-applies the CURRENT state's color on a day/night theme toggle -
// callers track their own current ui_pill_state_t (same as every other
// screen tracking its own live data) and pass it back here from their
// <screen>_refresh_theme().
void ui_pill_refreshTheme(lv_obj_t *pill, ui_pill_state_t state);

#ifdef __cplusplus
}
#endif
