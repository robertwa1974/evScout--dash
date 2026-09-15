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
// Navigation, following the current topology (Settings <-> Speed(home) <->
// Drive <-> Status <-> Battery <-> Charging <-> GPS <-> GPS NAV <-> Dyno
// LIVE -> Dyno RESULTS - GPS NAV inserted 2026-09-14 between GPS and Dyno
// LIVE, this screen's and Dyno LIVE's gesture targets updated to match):
// physical swipe LEFT -> Charging (back), physical swipe RIGHT -> GPS NAV
// (forward). This board reports
// gesture direction inverted from the physical swipe (see CLAUDE.md's
// "Touch gesture direction") - the code checks LV_DIR_RIGHT for the
// physical-LEFT swipe and LV_DIR_LEFT for the physical-RIGHT swipe.
// Intentional; don't "fix" it without re-verifying on hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_gpsScreen = NULL;

lv_obj_t * ui_gpsStatusPanel = NULL;
lv_obj_t * ui_gpsStatusPill = NULL;
lv_obj_t * ui_gpsStatusLabel = NULL;
lv_obj_t * ui_gpsSatsLabel = NULL;

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

#define GRID_PANEL_W 388
#define GRID_PANEL_H 149
#define GRID_GAP     8

void ui_event_gpsScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_chargingScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_chargingScreen_screen_init);
        _ui_screen_delete(&ui_gpsScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_navScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_navScreen_screen_init);
        _ui_screen_delete(&ui_gpsScreen);
    }
}

// Same createPanel/createValueAndUnit/createBar pattern as
// ui_driveScreen.c/ui_statusScreen.c/ui_chargingScreen.c - kept file-local/
// duplicated rather than shared, matching this codebase's convention.
static lv_obj_t *createPanel(lv_obj_t *parent, int16_t x, int16_t y, const char *title, lv_obj_t **outTitleLabel) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, GRID_PANEL_W, GRID_PANEL_H);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *titleLabel = lv_label_create(panel);
    lv_obj_set_pos(titleLabel, 12, 8);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_color(titleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(titleLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (outTitleLabel) *outTitleLabel = titleLabel;
    return panel;
}

static void createValueAndUnit(lv_obj_t *panel, lv_obj_t **outVal, lv_obj_t **outUnit, const char *unitText) {
    lv_obj_t *val = lv_label_create(panel);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -8);
    lv_label_set_text(val, "0");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outVal) *outVal = val;

    lv_obj_t *unit = lv_label_create(panel);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 28);
    lv_label_set_text(unit, unitText);
    lv_obj_set_style_text_color(unit, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(unit, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outUnit) *outUnit = unit;
}

static lv_obj_t *createBar(lv_obj_t *panel, int32_t rangeMin, int32_t rangeMax) {
    lv_obj_t *bar = lv_bar_create(panel);
    lv_bar_set_range(bar, rangeMin, rangeMax);
    lv_bar_set_value(bar, rangeMin, LV_ANIM_OFF);
    lv_obj_set_size(bar, 320, 14);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 58);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bar, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return bar;
}

// Plain formatted coordinate value - no bar, matches Drive screen's
// enum-text-cell reasoning (a lat/lon isn't a progress-style quantity).
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
    ui_gpsStatusPanel = createPanel(ui_gpsScreen, GRID_GAP, GRID_GAP, "GPS STATUS", NULL);

    ui_gpsStatusPill = lv_obj_create(ui_gpsStatusPanel);
    lv_obj_clear_flag(ui_gpsStatusPill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_gpsStatusPill, 200, 44);
    lv_obj_align(ui_gpsStatusPill, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_radius(ui_gpsStatusPill, 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_gpsStatusPill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_gpsStatusPill, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_gpsStatusPill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Smooth color fade instead of an instant snap on fix acquired/lost
    // (design-review "motion" pass, rolled out from the Charging screen
    // prototype - see waveshare-dash-build.md, 2026-09-14). Static
    // storage: LVGL keeps a pointer to both structs, not a copy - fine
    // since screens in this codebase are never actually destroyed (see
    // _ui_screen_delete's known-inverted-condition note in ui_helpers.h).
    static const lv_style_prop_t pillTransProps[] = { LV_STYLE_BG_COLOR, 0 };
    static lv_style_transition_dsc_t pillTransDsc;
    static lv_style_t pillTransStyle;
    lv_style_transition_dsc_init(&pillTransDsc, pillTransProps, lv_anim_path_ease_out, 400, 0, NULL);
    lv_style_init(&pillTransStyle);
    lv_style_set_transition(&pillTransStyle, &pillTransDsc);
    lv_obj_add_style(ui_gpsStatusPill, &pillTransStyle, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_gpsStatusLabel = lv_label_create(ui_gpsStatusPill);
    lv_obj_center(ui_gpsStatusLabel);
    lv_label_set_text(ui_gpsStatusLabel, "NO FIX");
    lv_obj_set_style_text_color(ui_gpsStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_gpsStatusLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_gpsSatsLabel = lv_label_create(ui_gpsStatusPanel);
    lv_obj_align(ui_gpsSatsLabel, LV_ALIGN_CENTER, 0, 32);
    lv_label_set_text(ui_gpsSatsLabel, "Sats: --");
    lv_obj_set_style_text_color(ui_gpsSatsLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_gpsSatsLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Speed: km/h (GPS-derived) ---
    ui_gpsSpeedPanel = createPanel(ui_gpsScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, ICON_SPEED " SPEED (GPS)", NULL);
    createValueAndUnit(ui_gpsSpeedPanel, &ui_gpsSpeedValLabel, NULL, "km/h");
    ui_gpsSpeedBar = createBar(ui_gpsSpeedPanel, 0, 200);

    // --- Latitude / Longitude ---
    ui_gpsLatPanel = createPanel(ui_gpsScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, ICON_LOCATION_ON " LATITUDE", NULL);
    ui_gpsLatValLabel = createCoordValue(ui_gpsLatPanel);

    ui_gpsLonPanel = createPanel(ui_gpsScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, ICON_LOCATION_ON " LONGITUDE", NULL);
    ui_gpsLonValLabel = createCoordValue(ui_gpsLonPanel);

    // --- Heading / Altitude ---
    ui_gpsHeadingPanel = createPanel(ui_gpsScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_NAVIGATION " HEADING", NULL);
    createValueAndUnit(ui_gpsHeadingPanel, &ui_gpsHeadingValLabel, NULL, "\xC2\xB0");
    ui_gpsHeadingBar = createBar(ui_gpsHeadingPanel, 0, 360);

    // TODO: -50..3000m placeholder altitude range - fine for most driving,
    // widen if this vehicle ever sees serious elevation extremes.
    ui_gpsAltPanel = createPanel(ui_gpsScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, "ALTITUDE", NULL);
    createValueAndUnit(ui_gpsAltPanel, &ui_gpsAltValLabel, NULL, "m");
    ui_gpsAltBar = createBar(ui_gpsAltPanel, -50, 3000);

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

    lv_obj_t *bars[] = { ui_gpsSpeedBar, ui_gpsHeadingBar, ui_gpsAltBar };
    for (int i = 0; i < 3; i++) {
        if (!bars[i]) continue;
        lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bars[i], ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    // Value labels and the status pill's color are data-driven and left
    // alone here - they self-correct on their next natural data update,
    // same reasoning as every other screen in this codebase.
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
}
