#ifndef UI_INCLINESCREEN_H
#define UI_INCLINESCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_inclineScreen - inclinometer (2026-10-29): pitch and roll in
// degrees, each as a Scout outline that tilts with the truck over a +-45
// degree tick arc with amber/red zones from Settings > Warning Thresholds.
// Data from imu_driver.h's imu_get_attitude(). Telemetry dock group,
// between Drive and Status in the swipe chain.
extern void ui_inclineScreen_screen_init(void);
extern void ui_inclineScreen_screen_destroy(void);
extern void ui_event_inclineScreen(lv_event_t * e);
extern void ui_inclineScreen_refresh_theme(void);

extern lv_obj_t * ui_inclineScreen;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
