// ============================================================================
// ui_destinationsScreen.cpp - predetermined destination list
// ============================================================================
// See ui_destinationsScreen.h for the full design rationale (replaces GPS
// NAV's old single on-canvas "HOME" button). No SquareLine project, same as
// every other hand-written screen here.
//
// Coordinates (moved here 2026-09-22 from ui_navScreen.cpp, now that
// routing is destination-generic rather than Home-only - see
// ui_navScreen_requestRoute()):
//   HOME: 1377 Calle Scott, Encinitas, CA 92024
//   WORK: 6155 El Camino Real, Carlsbad, CA 92009
// Both geocoded via OpenStreetMap's Nominatim (fitting, since NAVMAP/
// ROUTE.bin are both OSM-derived data already), not GPS-surveyed - good
// enough for road-level routing, not meant to be rooftop-accurate points.
// NVS-backed and editable live via wifi_config_server.h's local config
// page as of 2026-09-22 (vehicle_config.h) - was a hardcoded #define pair
// before that. Still no on-dash address-entry/search UI, and still no
// arbitrary-address input even on the config page - just editing these
// two fixed named points (see ui_navScreen.cpp's header comment for the
// original "no destination search, ever" decision; this doesn't change
// that, it just moves WHERE the two fixed points are edited).
//
// Charging stations: see charging_stations.h - an offline OpenChargeMap
// dataset for San Diego County, loaded once at boot. Shows the top
// CHG_MAX_NEAREST (5) stations nearest the CURRENT fix, not just the
// single closest - "realistically I would need top 5" (Rob, 2026-09-22),
// e.g. in case the closest one is occupied or the wrong connector type.
// Refreshed via ui_destinationsScreen_updateDistances() (see that
// function's comment in the header for why it's driven from
// zombie_updaters.cpp rather than a per-visit "on show" hook - this
// codebase's screens are created once and never destroyed).
//
// Layout: Home and Work stay fixed at the top (784x124 each, same
// GRID_PANEL_H=124 this project's 2x3 grid screens already use). Five
// charging-station rows don't fit in the remaining fixed height alongside
// them, so they live in a SCROLLABLE container below Work instead (same
// "content taller than viewport, drag to see more" pattern
// ui_settingsScreen.cpp's lv_menu already uses, just a plain lv_obj here
// rather than lv_menu - this is a flat list, not a sectioned menu). The
// dock stays a sibling of the scroll container, not a child, so it's
// fixed at the bottom regardless of scroll position - same convention
// ui_settingsScreen.cpp's dock placement already established.
//
// Navigation: new 3rd screen in the GPS group's swipe chain. Physical
// swipe LEFT -> GPS NAV (back). Physical swipe RIGHT -> Dyno LIVE
// (forward) - takes over the link GPS NAV used to have before this screen
// existed (see ui_navScreen.cpp's header comment) and that Dyno LIVE lost
// in the styling/UX pass (ui_dynoLiveScreen.cpp's ui_event_dynoLiveScreen()
// comment) - restored here pointing at Destinations instead of GPS NAV.
// This board reports gesture direction inverted from the physical swipe
// (see CLAUDE.md's "Touch gesture direction") - the code checks
// LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe, matching every other screen here.
// ============================================================================

#include "ui.h"
#include "gps_driver.h"
#include "gps_math.h"
#include "charging_stations.h"
#include "vehicle_config.h"  // homeLat/homeLon/workLat/workLon - was #define HOME_LAT etc. here, now NVS-backed/editable live via wifi_config_server.h
#include <stdio.h>

lv_obj_t * ui_destinationsScreen = NULL;

#define ROW_W (800 - 2 * GRID_GAP)
#define ROW_H GRID_PANEL_H     // 124px - Home/Work, matches this project's existing grid row height
#define CHG_ROW_H 100          // denser rows for the scrollable 5-item list - matches ui_settingsScreen.cpp's ROW_HEIGHT

// DEST_CHARGE_0..DEST_CHARGE_4 must stay contiguous and in this order -
// destRowClickedCb() derives the nearestStations[] index as id - DEST_CHARGE_0.
typedef enum {
    DEST_HOME, DEST_WORK,
    DEST_CHARGE_0, DEST_CHARGE_1, DEST_CHARGE_2, DEST_CHARGE_3, DEST_CHARGE_4,
} dest_id_t;

static lv_obj_t * homeDetailLabel = NULL;
static lv_obj_t * workDetailLabel = NULL;
static lv_obj_t * chargeScroll = NULL;
static lv_obj_t * chargeTitleLabel[CHG_MAX_NEAREST] = { NULL };
static lv_obj_t * chargeDetailLabel[CHG_MAX_NEAREST] = { NULL };
static lv_obj_t * chargeRow[CHG_MAX_NEAREST] = { NULL };

// Persistent bottom nav dock (styling/UX pass Phase 5 convention, extended
// to this screen 2026-09-22) - see ui_dock.h. Destinations is inside the
// GPS dock group (home screen: GPS), same as GPS NAV.
static lv_obj_t * ui_destinationsScreenDock = NULL;

// Cached result of the last top-5 lookup, nearest-first - what gets routed
// to if a charging row is tapped, kept in sync with what's DISPLAYED so a
// tap always routes to exactly the station the driver just read on screen.
static ChargingStation nearestStations[CHG_MAX_NEAREST];
static float nearestDistances[CHG_MAX_NEAREST];
static uint32_t nearestCount = 0;

// Display-only metric->imperial conversion (2026-09-22, matches this
// project's mph speed-unit convention and ui_navScreen.cpp's
// formatDistance()) - the distance math stays in meters throughout
// (charging_stations_find_nearest_n()/calcDist()), only this formatting
// step converts. Same 0.1mi (528ft) miles-switchover threshold as
// ui_navScreen.cpp's turn-banner distance, for consistency.
static void formatDistanceLabel(lv_obj_t *label, const char *prefix, float meters) {
    float feet = meters * 3.28084f;
    if (feet < 528.0f) lv_label_set_text_fmt(label, "%s%.0f ft away", prefix, feet);
    else lv_label_set_text_fmt(label, "%s%.1f mi away", prefix, feet / 5280.0f);
}

void ui_destinationsScreen_updateDistances(double lat, double lon) {
    if (!ui_destinationsScreen) return;

    if (homeDetailLabel) {
        formatDistanceLabel(homeDetailLabel, "", calcDist((float)lat, (float)lon, (float)homeLat, (float)homeLon));
    }
    if (workDetailLabel) {
        formatDistanceLabel(workDetailLabel, "", calcDist((float)lat, (float)lon, (float)workLat, (float)workLon));
    }

    nearestCount = charging_stations_find_nearest_n(lat, lon, CHG_MAX_NEAREST, nearestStations, nearestDistances);
    for (uint32_t i = 0; i < CHG_MAX_NEAREST; i++) {
        if (!chargeTitleLabel[i] || !chargeDetailLabel[i]) continue;
        if (i < nearestCount) {
            lv_label_set_text_fmt(chargeTitleLabel[i], LV_SYMBOL_CHARGE " %s", nearestStations[i].name);
            char buf[64];
            if (nearestStations[i].powerKw > 0) {
                snprintf(buf, sizeof(buf), "%s \xC2\xB7 %.0fkW \xC2\xB7 ", nearestStations[i].connectorType, nearestStations[i].powerKw);
            } else {
                snprintf(buf, sizeof(buf), "%s \xC2\xB7 ", nearestStations[i].connectorType);
            }
            formatDistanceLabel(chargeDetailLabel[i], buf, nearestDistances[i]);
        } else {
            lv_label_set_text(chargeTitleLabel[i], LV_SYMBOL_CHARGE " --");
            lv_label_set_text(chargeDetailLabel[i],
                               charging_stations_count() == 0 ? "No charging data loaded" : "--");
        }
    }
}

void ui_event_destinationsScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_navScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_navScreen_screen_init);
        _ui_screen_delete(&ui_destinationsScreen);
    } else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_dynoLiveScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_dynoLiveScreen_screen_init);
        _ui_screen_delete(&ui_destinationsScreen);
    }
}

static void destRowClickedCb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    dest_id_t id = (dest_id_t)(intptr_t)lv_event_get_user_data(e);

    double lat = 0, lon = 0;
    if (id == DEST_HOME) {
        lat = homeLat; lon = homeLon;
    } else if (id == DEST_WORK) {
        lat = workLat; lon = workLon;
    } else {
        uint32_t idx = (uint32_t)id - DEST_CHARGE_0;
        if (idx >= nearestCount) return;  // an empty slot below the current results - ignore the tap
        lat = nearestStations[idx].lat;
        lon = nearestStations[idx].lon;
    }

    ui_navScreen_requestRoute(lat, lon);
    _ui_screen_change(&ui_navScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_navScreen_screen_init);
}

static lv_obj_t *createDestRow(lv_obj_t *parent, int16_t y, int16_t h, const char *titleText,
                                lv_obj_t **outTitleLabel, lv_obj_t **outDetailLabel, dest_id_t id) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, GRID_GAP, y);
    lv_obj_set_size(row, ROW_W, h);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(row, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *titleLabel = lv_label_create(row);
    lv_label_set_text(titleLabel, titleText);
    lv_obj_set_style_text_color(titleLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(titleLabel, &font_montserrat_semibold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    if (outTitleLabel) *outTitleLabel = titleLabel;

    lv_obj_t *detailLabel = lv_label_create(row);
    lv_label_set_text(detailLabel, "--");
    lv_obj_set_style_text_color(detailLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(detailLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(detailLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (outDetailLabel) *outDetailLabel = detailLabel;

    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(chevron, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);

    ui_press_feedback_attach(row);
    lv_obj_add_event_cb(row, destRowClickedCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);

    return row;
}

void ui_destinationsScreen_screen_init(void)
{
    ui_destinationsScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_destinationsScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_destinationsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_destinationsScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    createDestRow(ui_destinationsScreen, GRID_GAP, ROW_H,
                  LV_SYMBOL_HOME " HOME", NULL, &homeDetailLabel, DEST_HOME);
    createDestRow(ui_destinationsScreen, GRID_GAP * 2 + ROW_H, ROW_H,
                  LV_SYMBOL_DIRECTORY " WORK", NULL, &workDetailLabel, DEST_WORK);

    // --- Scrollable "nearest 5 charging stations" list below Home/Work ---
    int16_t scrollY = GRID_GAP * 3 + ROW_H * 2;
    int16_t scrollH = (480 - UI_DOCK_H) - scrollY - GRID_GAP;
    chargeScroll = lv_obj_create(ui_destinationsScreen);
    lv_obj_set_pos(chargeScroll, 0, scrollY);
    lv_obj_set_size(chargeScroll, 800, scrollH);
    lv_obj_set_style_bg_opa(chargeScroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chargeScroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(chargeScroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(chargeScroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chargeScroll, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *chargeHeader = lv_label_create(chargeScroll);
    lv_label_set_text(chargeHeader, LV_SYMBOL_CHARGE " NEAREST CHARGING STATIONS");
    lv_obj_set_style_text_color(chargeHeader, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(chargeHeader, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(chargeHeader, GRID_GAP, 0);

    int16_t rowY = 28 + GRID_GAP;
    for (int i = 0; i < CHG_MAX_NEAREST; i++) {
        chargeRow[i] = createDestRow(chargeScroll, rowY, CHG_ROW_H,
                                      LV_SYMBOL_CHARGE " --", &chargeTitleLabel[i], &chargeDetailLabel[i],
                                      (dest_id_t)(DEST_CHARGE_0 + i));
        rowY += CHG_ROW_H + GRID_GAP;
    }

    if (charging_stations_count() == 0) {
        for (int i = 0; i < CHG_MAX_NEAREST; i++) {
            if (chargeDetailLabel[i]) lv_label_set_text(chargeDetailLabel[i], "No charging data loaded");
        }
    }

    ui_destinationsScreenDock = ui_dock_create(ui_destinationsScreen, UI_DOCK_GPS);

    lv_obj_add_event_cb(ui_destinationsScreen, ui_event_destinationsScreen, LV_EVENT_ALL, NULL);

    // Seed the rows immediately from whatever fix already exists (same
    // "don't wait for the next natural GPS tick" reasoning as
    // refreshClockDisplay() - see zombie_updaters.h) rather than showing
    // "--" until the next fix arrives, which could be up to a second away
    // or, if this is the very first screen visit right after boot with no
    // fix yet, indefinitely.
    if (gpsData.hasFix) ui_destinationsScreen_updateDistances(gpsData.latitude, gpsData.longitude);
}

void ui_destinationsScreen_refresh_theme(void)
{
    if (ui_destinationsScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_destinationsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    uint32_t n = lv_obj_get_child_cnt(ui_destinationsScreen);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(ui_destinationsScreen, i);
        if (child == ui_destinationsScreenDock || child == chargeScroll) continue;  // re-themed separately below
        lv_obj_set_style_bg_color(child, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(child, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (homeDetailLabel) lv_obj_set_style_text_color(homeDetailLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (workDetailLabel) lv_obj_set_style_text_color(workDetailLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    for (int i = 0; i < CHG_MAX_NEAREST; i++) {
        if (chargeRow[i]) {
            lv_obj_set_style_bg_color(chargeRow[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(chargeRow[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (chargeDetailLabel[i]) lv_obj_set_style_text_color(chargeDetailLabel[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    ui_dock_refresh_theme(ui_destinationsScreenDock, UI_DOCK_GPS);
}

void ui_destinationsScreen_screen_destroy(void)
{
    if (ui_destinationsScreen) lv_obj_del(ui_destinationsScreen);

    ui_destinationsScreen = NULL;
    homeDetailLabel = NULL;
    workDetailLabel = NULL;
    chargeScroll = NULL;
    for (int i = 0; i < CHG_MAX_NEAREST; i++) {
        chargeTitleLabel[i] = NULL;
        chargeDetailLabel[i] = NULL;
        chargeRow[i] = NULL;
    }
    ui_destinationsScreenDock = NULL;
    nearestCount = 0;
}
