#ifndef UI_SPLASHSCREEN_H
#define UI_SPLASHSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_splashScreen - Splash/Clock, combined per CLAUDE.md's "Screen
// layout conventions" ("logo and a large digital or analog clock face, no
// data density"). New 2026-09-14 once Rob supplied a real Scout wordmark
// (firmware/assets/SCOUT.png, converted to img_scout_logo.c/.h - see that
// file's header for the conversion pipeline). Deliberately simple: the
// logo image, a digital clock, nothing else - no bars, no arcs, no
// warning-color logic. CLAUDE.md's rule that the splash graphic never
// appears as a placeholder on any other screen still applies - this is the
// ONLY screen that uses img_scout_logo.
//
// Clock is UTC, sourced from gpsData (gps_driver.h) - no timezone
// conversion (would need the user's location and a TZ database entry,
// neither of which this project has any source for yet), no RTC/light-
// sensor fallback (same "doesn't exist on this board" gap
// resolveDisplayPref() already documents). Shows "--:--" until a real GPS
// fix has actually been seen (gpsData.hasFix), same "safe until real
// hardware arrives" pattern as every other GPS-dependent value in this
// project.
extern void ui_splashScreen_screen_init(void);
extern void ui_splashScreen_screen_destroy(void);
extern void ui_event_splashScreen(lv_event_t * e);
extern void ui_splashScreen_refresh_theme(void);

extern lv_obj_t * ui_splashScreen;
extern lv_obj_t * ui_splashLogo;
extern lv_obj_t * ui_splashClockLabel;
extern lv_obj_t * ui_splashClockCaptionLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
