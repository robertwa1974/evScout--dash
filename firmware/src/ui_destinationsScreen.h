#ifndef UI_DESTINATIONSSCREEN_H
#define UI_DESTINATIONSSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

// SCREEN: ui_destinationsScreen (added 2026-09-22) - a tappable list of
// predetermined destinations: Home and Work (fixed at the top), plus the
// nearest CHG_MAX_NEAREST (5) charging stations - not just the single
// closest, in a scrollable list below (from the offline OpenChargeMap
// dataset - see charging_stations.h). Replaces GPS NAV's old on-canvas
// "HOME" button + "PRESS HOME" turn-banner text combo, which real bench
// use showed was confusing and ate too much of the map canvas for a
// single destination. Still no address-entry/search UI - see
// ui_navScreen.cpp's header comment for why that's out of scope, ever;
// this screen just lists a FIXED, small set of named destinations instead
// of a single hardcoded one.
//
// New 3rd screen in the GPS group's intra-group swipe chain: GPS <-> GPS
// NAV <-> Destinations <-> Dyno LIVE - Destinations takes over GPS NAV's
// old forward-swipe link to Dyno LIVE (Dyno stays reachable one swipe
// further, and is also still dock-reachable directly - see ui_dock.h).
// Not a dock-addressable screen itself (same as GPS NAV) - reached only by
// swipe, per ui_dock.h's "each group's OTHER member screens... reached the
// same way they always were" convention.
//
// Tapping a row calls ui_navScreen_requestRoute() (generalized from the
// old Home-only requestRouteHome()) with that row's coordinates, then
// swipes back to GPS NAV to show routing progress on the turn banner.
extern void ui_destinationsScreen_screen_init(void);
extern void ui_destinationsScreen_screen_destroy(void);
extern void ui_event_destinationsScreen(lv_event_t * e);
extern void ui_destinationsScreen_refresh_theme(void);

// Re-renders the Home/Work distance labels and the nearest-charging-
// station rows (up to CHG_MAX_NEAREST) from the given fix - called from
// zombie_updaters.cpp's gpsLatLonChanged block, same call site as
// ui_navScreen_addPoint(), so this stays reasonably fresh (~1Hz) whenever
// GPS NAV or Destinations has ever been visited, not just while
// Destinations is the currently active screen - this codebase's screens
// are created once and kept alive for
// the rest of the session (see ui_dock.h's "screens are never destroyed"
// note), so there's no per-visit "on show" hook to hang a refresh off of
// instead. No-ops if the screen hasn't been created yet, same convention
// as every other screen's data-binding setter.
extern void ui_destinationsScreen_updateDistances(double lat, double lon);

extern lv_obj_t * ui_destinationsScreen;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
