#pragma once

// Persistent bottom navigation dock (styling/UX pass, 2026-09-18, Phase 5).
// Replaces full-deck cross-group swipe navigation with 5 always-visible
// icon buttons, spec confirmed with Rob: Telemetry/BMS/GPS/Dyno/Settings.
//
// Dock-to-screen mapping (confirmed):
//   TELEMETRY -> Speed (home), also covers Drive/Status/Charging
//   BMS       -> Battery
//   GPS       -> GPS (telemetry screen), also covers GPS NAV
//   DYNO      -> Dyno LIVE, also covers Dyno RESULTS
//   SETTINGS  -> Settings
// Each group's OTHER member screens (Drive/Status/Charging under
// Telemetry; GPS NAV under GPS; Dyno RESULTS under Dyno) are reached the
// same way they always were - intra-group physical swipe - the dock never
// targets them directly. Only the 8 CROSS-group swipe links get removed
// (see the plan's swipe-removal table); every intra-group swipe stays.
//
// One dock instance is built PER SCREEN (screens in this codebase are
// never destroyed once created - see ui_helpers.h's _ui_screen_delete
// note - so "one shared dock reused across screens" isn't how this
// codebase's screen model works; each screen owns its own dock child,
// same pattern as every other per-screen widget here).

#include "lvgl.h"

#define UI_DOCK_H 72

typedef enum {
    UI_DOCK_TELEMETRY,
    UI_DOCK_BMS,
    UI_DOCK_GPS,
    UI_DOCK_DYNO,
    UI_DOCK_SETTINGS,
} ui_dock_dest_t;

#ifdef __cplusplus
extern "C" {
#endif

// Builds one ~800x72 horizontal dock bar as a child of screenParent,
// pinned LV_ALIGN_BOTTOM_MID. activeDest is fixed for this dock instance's
// whole lifetime - it's which group the OWNING screen belongs to, so that
// button renders highlighted and tapping it is a no-op (you're already in
// that group; the dock only handles CROSS-group jumps, intra-group
// movement stays swipe-driven, see this header's mapping comment). Every
// other button fires _ui_screen_change(..., LV_SCR_LOAD_ANIM_FADE_ON, 300,
// 0, ...) to its group's home screen. Each button gets
// ui_press_feedback_attach() automatically.
lv_obj_t *ui_dock_create(lv_obj_t *screenParent, ui_dock_dest_t activeDest);

// Re-applies the active/inactive tint + icon/label colors on a day/night
// toggle - call from the owning screen's own refresh_theme(), passing the
// same activeDest given to ui_dock_create() (screens track this the same
// way they track any other data-driven state, e.g. ui_status_pill.h's
// state-tracking pattern).
void ui_dock_refresh_theme(lv_obj_t *dock, ui_dock_dest_t activeDest);

#ifdef __cplusplus
}
#endif
