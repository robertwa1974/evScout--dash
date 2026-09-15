#ifndef UI_NAVSCREEN_H
#define UI_NAVSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_navScreen - GPS breadcrumb trail (2026-09-14). The buildable-
// now piece of the architecture doc's IceNav-v3-style offline map screen:
// a real map (raster tiles from OpenStreetMap or similar) still needs map
// tile assets prepared for wherever this vehicle actually drives (a data-
// acquisition decision, not a coding one) plus SD card wiring this repo has
// never touched - both still genuinely blocked, unchanged from
// ui_gpsScreen.h's note. This screen needs neither: it plots the
// accumulated recent GPS fix history as a line, current position always
// centered, using only gpsData (gps_driver.h) - real navigational value
// (where you've been, current heading) buildable in software today.
//
// Not the same screen as ui_gpsScreen (GPS telemetry grid, unchanged) -
// this is a new 12th screen inserted into the topology between GPS and
// Dyno LIVE: ... <-> GPS <-> GPS NAV <-> Dyno LIVE -> Dyno RESULTS.
extern void ui_navScreen_screen_init(void);
extern void ui_navScreen_screen_destroy(void);
extern void ui_event_navScreen(lv_event_t * e);
extern void ui_navScreen_refresh_theme(void);

// Appends the given fix to the trail history and redraws it, if the screen
// has been created (no-op otherwise, matching this codebase's convention
// of not doing work for screens nobody has visited yet - see the null
// checks around every other screen's widget pointers in
// zombie_updaters.cpp). Called from slowUpdate() alongside the existing
// gpsLatLonChanged dirty-check - GPS fixes update at most ~1Hz already, no
// separate throttling needed here.
//
// Deliberately does NOT call nav_tile_load() itself, even on a tile
// crossing - slowUpdate() runs as an ESP32 Ticker callback, i.e. from the
// esp_timer system task, not a normal task. Confirmed on real hardware
// (2026-09-15): every task-watchdog crash opening real (non-trivial)
// California map data showed "CPU 0: esp_timer" as the running task -
// the blocking SD I/O a real tile load needs (multiple sequential reads/
// seeks, unavoidably some tens to hundreds of ms each) starves the esp_timer
// task's own scheduler slice long enough to starve IDLE0 too, regardless
// of how fast any single SD call is. An isolated test calling the exact
// same nav_tile_load() from a normal task (setup()/loop()) never
// reproduced this - only the Ticker-callback context did. This function
// only ever records which tile is needed; ui_navScreen_processPendingTileLoad()
// does the actual blocking work, and must only ever be called from a
// normal task (firmware.ino's loop(), not any Ticker/esp_timer callback).
extern void ui_navScreen_addPoint(double lat, double lon);

// Services a pending tile load recorded by ui_navScreen_addPoint(), if
// any - safe to call every loop() iteration (cheap no-op when nothing's
// pending). MUST be called only from a normal task context - see
// ui_navScreen_addPoint()'s comment for why this can't run from
// slowUpdate()'s Ticker callback.
extern void ui_navScreen_processPendingTileLoad(void);

extern lv_obj_t * ui_navScreen;
extern lv_obj_t * ui_navStatusLabel;
extern lv_obj_t * ui_navSpeedLabel;
extern lv_obj_t * ui_navHeadingLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
