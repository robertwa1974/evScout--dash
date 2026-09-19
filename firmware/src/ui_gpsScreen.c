// ============================================================================
// ui_gpsScreen.c - GPS telemetry, 2x3 grid
// ============================================================================
// Hand-written (2026-09-14), the buildable-now piece of the architecture
// doc's GPS navigation screen plan - see ui_gpsScreen.h and
// waveshare-dash-build.md for why the full IceNav-v3-style offline map
// isn't built yet (needs map tile assets + SD card wiring, neither exist
// in this repo). No SquareLine project (same situation as every other
// hand-written screen in this repo).
//
// 2x3 grid of six equal 388x149 panels, same geometry as ui_driveScreen.c/
// ui_statusScreen.c/ui_chargingScreen.c:
//   (0,0) 8,8     GPS Status - pill: "GPS FIX"/"NO FIX" (from
//                              gpsData.hasFix), secondary "Sats: N" label
//   (1,0) 404,8   Speed      - km/h (GPS-derived - the speed cross-check
//                              the architecture doc's dyno design calls
//                              for), lv_bar
//   (0,1) 8,165   Latitude   - plain formatted value (decimal degrees +
//                              N/S), no bar - a coordinate isn't a
//                              meaningful quantity to show as a filled
//                              strip, same reasoning as Drive screen's
//                              Gear/Motor Mode text-only cells
//   (1,1) 404,165 Longitude  - ditto, decimal degrees + E/W
//   (0,2) 8,322   Heading    - degrees, 0-360, lv_bar
//   (1,2) 404,322 Altitude   - meters, lv_bar
//
// Widget choice follows CLAUDE.md: lv_bar for the linear-strip numeric
// values, plain text for the two coordinate cells (not continuous
// progress-style quantities) - never lv_slider for read-only telemetry.
//
// Bound to gpsData (gps_driver.h) in zombie_updaters.cpp's slowUpdate -
// GPS fixes update at most once a second, no need for a faster tier.
//
// Navigation: physical swipe RIGHT -> GPS NAV (forward, stays - both in
// the dock's GPS group). Physical swipe LEFT to Charging is REMOVED
// (styling/UX pass Phase 5, 2026-09-18) - that was a cross-group link,
// Charging is in the Telemetry group and dock-reachable now (see
// ui_dock.h's mapping comment and the plan's swipe-removal table). GPS is
// this group's own dock destination, so no retarget was needed here
// (unlike ui_chargingScreen.c's situation, GPS doesn't lose reachability).
// This board reports gesture direction inverted from the physical swipe
// (see CLAUDE.md's "Touch gesture direction") - the code checks
// LV_DIR_LEFT for the physical-RIGHT swipe. Intentional; don't "fix" it
// without re-verifying on hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_gpsScreen = NULL;

lv_obj_t * ui_gpsStatusPanel = NULL;
lv_obj_t * ui_gpsStatusPill = NULL;
lv_obj_t * ui_gpsStatusLabel = NULL;
lv_obj_t * ui_gpsSatsLabel = NULL;
// Tracks the pill's current semantic state so refresh_theme() can re-apply
// the right color on a day/night toggle without re-deriving it from
// gpsData (ui_status_pill.h's pattern - see its header comment).
static ui_pill_state_t ui_gpsStatusPillState = UI_PILL_NEUTRAL;

lv_obj_t * ui_gpsSpeedPanel = NULL;
lv_obj_t * ui_gpsSpeedValLabel = NULL;
lv_obj_t * ui_gpsSpeedBar = NULL;

lv_obj_t * ui_gpsLatPanel = NULL;
lv_obj_t * ui_gpsLatValLabel = NULL;

lv_obj_t * ui_gpsLonPanel = NULL;
lv_obj_t * ui_gpsLonValLabel = NULL;

lv_obj_t * ui_gpsHeadingPanel = NULL;
lv_obj_t * ui_gpsHeadingValLabel = NULL;
lv_obj_t * ui_gpsHeadingBar = NULL;

lv_obj_t * ui_gpsAltPanel = NULL;
lv_obj_t * ui_gpsAltValLabel = NULL;
lv_obj_t * ui_gpsAltBar = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. GPS is this group's home screen (also covers GPS NAV).
static lv_obj_t * ui_gpsScreenDock = NULL;

void ui_event_gpsScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    // Physical-LEFT swipe to Charging REMOVED here - see this file's
    // header comment (styling/UX pass Phase 5).
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_navScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_navScreen_screen_init);
        _ui_screen_delete(&ui_gpsScreen);
    }
}

// Styling pass (2026-09-18): panel/value/bar construction now goes through
// ui_card_grid.h's shared ui_card_create*() - see that header for why (4
// files hand-duplicated this identically). None of this screen's bars
// (Speed/Heading/Altitude) have a real warningSet threshold, so all pass
// zoneDir=0 (flat track) at their call sites below - bar gradient/flat
// audit, styling pass item 5.
//
// Plain formatted coordinate value - no bar, matches Drive screen's
// enum-text-cell reasoning (a lat/lon isn't a progress-style quantity).
// Font weight audit item 2: this is a real numeric reading (not label/state
// text), so it stays ExtraBold - same "numeric hero" treatment as a gauge
// readout, unlike the enum-text cells on the Drive screen.
static lv_obj_t *createCoordValue(lv_obj_t *panel) {
    lv_obj_t *val = lv_label_create(panel);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 12);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    return val;
}

void ui_gpsScreen_screen_init(void)
{
    ui_gpsScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_gpsScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_gpsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_gpsScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- GPS Status: pill + secondary satellite-count label ---
    ui_gpsStatusPanel = ui_card_createPanel(ui_gpsScreen, GRID_GAP, GRID_GAP, "GPS STATUS", NULL);

    // Styling pass: migrated onto ui_status_pill.h's shared pill (was a
    // hand-built lv_obj + its own copy-pasted 400ms fade transition) - see
    // that header for the 4-state color model. GPS fix is a binary
    // good/neutral state, no caution/fault tier.
    ui_gpsStatusPill = ui_pill_create(ui_gpsStatusPanel, 200, 44, 22, &ui_gpsStatusLabel, &font_montserrat_semibold_24);
    lv_obj_align(ui_gpsStatusPill, LV_ALIGN_CENTER, 0, -8);
    ui_pill_setState(ui_gpsStatusPill, ui_gpsStatusLabel, UI_PILL_NEUTRAL, "NO FIX");

    ui_gpsSatsLabel = lv_label_create(ui_gpsStatusPanel);
    lv_obj_align(ui_gpsSatsLabel, LV_ALIGN_CENTER, 0, 32);
    lv_label_set_text(ui_gpsSatsLabel, "Sats: --");
    lv_obj_set_style_text_color(ui_gpsSatsLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_gpsSatsLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Speed: km/h (GPS-derived) ---
    ui_gpsSpeedPanel = ui_card_createPanel(ui_gpsScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, ICON_SPEED " SPEED (GPS)", NULL);
    ui_card_createValueAndUnit(ui_gpsSpeedPanel, &ui_gpsSpeedValLabel, NULL, "km/h");
    ui_gpsSpeedBar = ui_card_createBar(ui_gpsSpeedPanel, false, 0, 200, 0);

    // --- Latitude / Longitude ---
    ui_gpsLatPanel = ui_card_createPanel(ui_gpsScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, ICON_LOCATION_ON " LATITUDE", NULL);
    ui_gpsLatValLabel = createCoordValue(ui_gpsLatPanel);

    ui_gpsLonPanel = ui_card_createPanel(ui_gpsScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, ICON_LOCATION_ON " LONGITUDE", NULL);
    ui_gpsLonValLabel = createCoordValue(ui_gpsLonPanel);

    // --- Heading / Altitude ---
    ui_gpsHeadingPanel = ui_card_createPanel(ui_gpsScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_NAVIGATION " HEADING", NULL);
    ui_card_createValueAndUnit(ui_gpsHeadingPanel, &ui_gpsHeadingValLabel, NULL, "\xC2\xB0");
    ui_gpsHeadingBar = ui_card_createBar(ui_gpsHeadingPanel, false, 0, 360, 0);

    // TODO: -50..3000m placeholder altitude range - fine for most driving,
    // widen if this vehicle ever sees serious elevation extremes.
    ui_gpsAltPanel = ui_card_createPanel(ui_gpsScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, "ALTITUDE", NULL);
    ui_card_createValueAndUnit(ui_gpsAltPanel, &ui_gpsAltValLabel, NULL, "m");
    ui_gpsAltBar = ui_card_createBar(ui_gpsAltPanel, false, -50, 3000, 0);

    ui_gpsScreenDock = ui_dock_create(ui_gpsScreen, UI_DOCK_GPS);

    lv_obj_add_event_cb(ui_gpsScreen, ui_event_gpsScreen, LV_EVENT_ALL, NULL);
}

void ui_gpsScreen_refresh_theme(void)
{
    if (ui_gpsScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_gpsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *panels[] = { ui_gpsStatusPanel, ui_gpsSpeedPanel, ui_gpsLatPanel, ui_gpsLonPanel, ui_gpsHeadingPanel, ui_gpsAltPanel };
    for (int i = 0; i < 6; i++) {
        if (!panels[i]) continue;
        lv_obj_set_style_bg_color(panels[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(panels[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_gpsSatsLabel) lv_obj_set_style_text_color(ui_gpsSatsLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_pill_refreshTheme(ui_gpsStatusPill, ui_gpsStatusPillState);

    lv_obj_t *bars[] = { ui_gpsSpeedBar, ui_gpsHeadingBar, ui_gpsAltBar };
    for (int i = 0; i < 3; i++) {
        if (!bars[i]) continue;
        lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bars[i], ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    ui_dock_refresh_theme(ui_gpsScreenDock, UI_DOCK_GPS);
    // Value labels are data-driven and left alone here - they self-correct
    // on their next natural data update, same reasoning as every other
    // screen in this codebase.
}

// Called from zombie_updaters.cpp's slowUpdate() instead of that file
// poking ui_gpsStatusPill/ui_gpsStatusLabel directly - lets this screen
// track its own current pill state for refresh_theme() to re-apply on a
// day/night toggle (same setter pattern as ui_chargingScreen_setStatus()/
// ui_batteryScreen_setStatus()).
void ui_gpsScreen_setStatus(bool hasFix, uint8_t sats)
{
    if (ui_gpsStatusPill && ui_gpsStatusLabel) {
        // LV_SYMBOL_GPS prefix only while a fix is actually held -
        // design-review "icons" pass, 2026-09-14, same "only while active"
        // reasoning as the Charging screen's charge-bolt icon.
        ui_gpsStatusPillState = hasFix ? UI_PILL_ACTIVE : UI_PILL_NEUTRAL;
        ui_pill_setState(ui_gpsStatusPill, ui_gpsStatusLabel, ui_gpsStatusPillState,
                          hasFix ? LV_SYMBOL_GPS " GPS FIX" : "NO FIX");
    }
    if (ui_gpsSatsLabel) lv_label_set_text_fmt(ui_gpsSatsLabel, "Sats: %d", sats);
}

void ui_gpsScreen_screen_destroy(void)
{
    if (ui_gpsScreen) lv_obj_del(ui_gpsScreen);

    ui_gpsScreen = NULL;
    ui_gpsStatusPanel = NULL;
    ui_gpsStatusPill = NULL;
    ui_gpsStatusLabel = NULL;
    ui_gpsSatsLabel = NULL;
    ui_gpsSpeedPanel = NULL;
    ui_gpsSpeedValLabel = NULL;
    ui_gpsSpeedBar = NULL;
    ui_gpsLatPanel = NULL;
    ui_gpsLatValLabel = NULL;
    ui_gpsLonPanel = NULL;
    ui_gpsLonValLabel = NULL;
    ui_gpsHeadingPanel = NULL;
    ui_gpsHeadingValLabel = NULL;
    ui_gpsHeadingBar = NULL;
    ui_gpsAltPanel = NULL;
    ui_gpsAltValLabel = NULL;
    ui_gpsAltBar = NULL;
    ui_gpsStatusPillState = UI_PILL_NEUTRAL;
    ui_gpsScreenDock = NULL;
}
