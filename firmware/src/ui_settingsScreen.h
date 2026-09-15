#ifndef UI_SETTINGSSCREEN_H
#define UI_SETTINGSSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_settingsScreen - rebuilt 2026-09-13 as an lv_menu per
// CLAUDE.md (Warning Thresholds / Display / Calibration sections). Hand-
// written, not SquareLine-generated - like ui_mainScreen.c/ui_bmsScreen.c,
// its widgets are all file-local (static) since nothing outside this file
// needs to touch them; only warningSet/displayPref/ui_theme's state (all in
// zombie_updaters.h / ui_theme.h) cross the file boundary.
extern void ui_settingsScreen_screen_init(void);
extern void ui_settingsScreen_screen_destroy(void);
extern void ui_event_settingsScreen(lv_event_t * e);
extern void ui_settingsScreen_refresh_theme(void);

extern lv_obj_t * ui_settingsScreen;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
