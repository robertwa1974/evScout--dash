#ifndef UI_NAVSCREEN_H
#define UI_NAVSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_navScreen - GPS NAV: offline map + turn-by-turn (component 5
// of the IceNav-v3-based nav screen rebuild, 2026-09-16 - see the ported
// NavReader/router/nav_map_render/nav_turn components this wires
// together). Started 2026-09-14 as a breadcrumb-trail stand-in (see
// waveshare-dash-build.md's milestone history); now the real thing: a
// single NAV tile's filled road/polygon geometry (nav_map_render.h)
// recentered every GPS fix, a calculated route line + turn-by-turn banner
// (router.h/nav_turn.h) over it, current position always centered.
// Converted from ui_navScreen.c to .cpp for this pass - Router/NavState/
// TrackVector/TurnPointVector are C++-only, no C wrapper (same reasoning
// ui_dynoLiveScreen.cpp already established for touching C++ types
// directly rather than adding wrapper boilerplate).
//
// Not the same screen as ui_gpsScreen (GPS telemetry grid, unchanged) -
// this screen sits in the topology between GPS and Destinations: ... <->
// GPS <-> GPS NAV <-> Destinations <-> Dyno LIVE -> Dyno RESULTS (the
// Destinations screen, added 2026-09-22, took over this screen's old
// direct link to Dyno LIVE - see ui_destinationsScreen.h).
extern void ui_navScreen_screen_init(void);
extern void ui_navScreen_screen_destroy(void);
extern void ui_event_navScreen(lv_event_t * e);
extern void ui_navScreen_refresh_theme(void);

// Public routing entry point - called from the Destinations screen's row-
// tap handler with the coordinates of whichever destination (Home, Work,
// nearest charging station) was tapped. Ignores the request with no
// visible state change if there's no GPS fix yet or a route is already
// being computed. Cheap to call from LVGL's own input-handling context
// (already under uiMutex) - it only sets a pending-flag; the actual
// blocking Router::route() call still happens in
// ui_navScreen_processPendingRoute() from firmware.ino's loop().
extern void ui_navScreen_requestRoute(double destLat, double destLon);

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
//
// The same restriction applies to repositionTileLayer() as of 2026-09-20
// (see ui_navScreen_processPendingReposition() below) - it was originally
// assumed cheap ("pure math, no I/O") and called directly from here, which
// was true only as long as a real tile's decode/render was still capped
// at 64 features. Once that cap was fixed to cover a real tile's true
// 174-1066 features, repositionTileLayer()'s raster-fill pass started
// taking 150-200+ms per call - confirmed on real hardware to trip the
// exact same IDLE0/esp_timer watchdog signature as the nav_tile_load()
// issue above when left running directly from here.
extern void ui_navScreen_addPoint(double lat, double lon);

// Services a pending tile load recorded by ui_navScreen_addPoint(), if
// any - safe to call every loop() iteration (cheap no-op when nothing's
// pending). MUST be called only from a normal task context - see
// ui_navScreen_addPoint()'s comment for why this can't run from
// slowUpdate()'s Ticker callback.
extern void ui_navScreen_processPendingTileLoad(void);

// Services a pending repositionTileLayer() (map recenter + raster render)
// recorded by ui_navScreen_addPoint() - see that function's comment for
// why. MUST be called only from a normal task context (firmware.ino's
// loop()), never from slowUpdate()'s Ticker callback. Safe to call every
// loop() iteration (cheap no-op when nothing's pending).
extern void ui_navScreen_processPendingReposition(void);

// Services a pending route computation (component 5) - Router::route()
// does blocking SD I/O (ROUTE.bin header/index/page-cache reads), so like
// ui_navScreen_processPendingTileLoad() this must only ever be called
// from a normal task (firmware.ino's loop()), never from slowUpdate()'s
// Ticker-callback context. Safe to call every loop() iteration (cheap
// no-op when nothing's pending).
extern void ui_navScreen_processPendingRoute(void);

extern lv_obj_t * ui_navScreen;
extern lv_obj_t * ui_navStatusLabel;
extern lv_obj_t * ui_navSpeedLabel;
extern lv_obj_t * ui_navHeadingLabel;
extern lv_obj_t * ui_navClockLabel;
extern lv_obj_t * ui_navSocLabel;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
