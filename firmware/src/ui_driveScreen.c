// ============================================================================
// ui_driveScreen.c - in-motion telemetry, 2x3 grid
// ============================================================================
// Hand-written (2026-09-14), part of the Speed/Drive/Status/Battery split
// (see waveshare-dash-build.md). No SquareLine project (same situation as
// every other hand-written screen in this repo).
//
// 2x3 grid of six equal 388x149 panels, 8px outer margin/gap
// (8+388+8+388+8=800 exact fit; 8+149+8+149+8+149+8=479, 1px slack at the
// bottom - not visually significant):
//   (0,0) 8,8     Power          - kW, signed (negative = regen) - lv_bar
//                                   strip (SYMMETRICAL)
//   (1,0) 404,8   Pack Current   - A, signed - lv_bar strip (SYMMETRICAL)
//   (0,1) 8,165   Gear Selection - text enum (LOW/HIGH/AUTO/HI-FOR-LOW-REV,
//                                   myData.gear via PARAM_ID_GEAR) - no bar,
//                                   this is a discrete state, not a
//                                   continuous quantity
//   (1,1) 404,165 Motor Mode     - text enum (MG1/MG2/MG1+MG2/BLEND,
//                                   myData.motActive via PARAM_ID_MOT_ACTIVE)
//   (0,2) 8,322   Regen Limit    - %, unsigned - lv_bar strip
//   (1,2) 404,322 (spare)        - reserved for a future field, deliberately
//                                   left empty per the approved 6th-slot plan
//
// Widget choice follows CLAUDE.md: lv_bar for the linear-strip signed/
// unsigned values, plain text for the two discrete enum states - never
// lv_slider for read-only telemetry.
//
// Bound to myData in zombie_updaters.cpp's fastUpdate/midUpdate/slowUpdate
// (same dirty-check, dataMutex-then-uiMutex pattern as every other binding).
//
// Navigation, following the approved topology (Settings <-> Speed(home) <->
// Drive <-> Status <-> Battery <-> Dyno LIVE -> Dyno RESULTS): physical
// swipe LEFT -> Speed (back), physical swipe RIGHT -> Status (forward).
// This board reports gesture direction inverted from the physical swipe
// (see CLAUDE.md's "Touch gesture direction") - the code checks
// LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe. Intentional; don't "fix" it without re-verifying on
// hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_driveScreen = NULL;

lv_obj_t * ui_drivePowerPanel = NULL;
lv_obj_t * ui_drivePowerTitleLabel = NULL;
lv_obj_t * ui_drivePowerValLabel = NULL;
lv_obj_t * ui_drivePowerUnitLabel = NULL;
lv_obj_t * ui_drivePowerBar = NULL;

lv_obj_t * ui_driveCurrPanel = NULL;
lv_obj_t * ui_driveCurrTitleLabel = NULL;
lv_obj_t * ui_driveCurrValLabel = NULL;
lv_obj_t * ui_driveCurrUnitLabel = NULL;
lv_obj_t * ui_driveCurrBar = NULL;

lv_obj_t * ui_driveGearPanel = NULL;
lv_obj_t * ui_driveGearTitleLabel = NULL;
lv_obj_t * ui_driveGearValLabel = NULL;

lv_obj_t * ui_driveMotorModePanel = NULL;
lv_obj_t * ui_driveMotorModeTitleLabel = NULL;
lv_obj_t * ui_driveMotorModeValLabel = NULL;

lv_obj_t * ui_driveRegenPanel = NULL;
lv_obj_t * ui_driveRegenTitleLabel = NULL;
lv_obj_t * ui_driveRegenValLabel = NULL;
lv_obj_t * ui_driveRegenUnitLabel = NULL;
lv_obj_t * ui_driveRegenBar = NULL;

lv_obj_t * ui_driveSparePanel = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Drive is inside the Telemetry group (home screen: Speed).
static lv_obj_t * ui_driveScreenDock = NULL;

void ui_event_driveScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_speedScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_speedScreen_screen_init);
        _ui_screen_delete(&ui_driveScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_statusScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_statusScreen_screen_init);
        _ui_screen_delete(&ui_driveScreen);
    }
}

// Styling pass (2026-09-18): panel/value/bar construction now goes through
// ui_card_grid.h's shared ui_card_create*() instead of a file-local copy -
// see that header for why (4 files hand-duplicated this identically).
//
// Big centered text value for a discrete enum state (gear selection, motor
// mode) - no bar, this isn't a continuous quantity. Font weight audit item
// 2: this is label/state text (not a numeric hero value), so it gets the
// SemiBold-24 weight, not ExtraBold.
static lv_obj_t *createEnumValue(lv_obj_t *panel) {
    lv_obj_t *val = lv_label_create(panel);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 12);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_semibold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    return val;
}

void ui_driveScreen_screen_init(void)
{
    ui_driveScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_driveScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_driveScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_driveScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // TODO: bar ranges below are placeholder defaults - tune to your
    // actual motor/inverter/pack ratings once known.
    // zoneDir=0 on all three: no warningSet threshold exists for power,
    // pack current, or regen limit (see zombie_updaters.h's warning_set -
    // only lowSoc/motorTemp/heatsinkTemp/packVLow are real thresholds) -
    // bar gradient/flat audit, styling pass item 5.
    ui_drivePowerPanel = ui_card_createPanel(ui_driveScreen, GRID_GAP, GRID_GAP, ICON_BOLT " POWER", &ui_drivePowerTitleLabel);
    ui_card_createValueAndUnit(ui_drivePowerPanel, &ui_drivePowerValLabel, &ui_drivePowerUnitLabel, "kW");
    ui_drivePowerBar = ui_card_createBar(ui_drivePowerPanel, true, -30, 150, 0);

    ui_driveCurrPanel = ui_card_createPanel(ui_driveScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, "PACK CURRENT", &ui_driveCurrTitleLabel);
    ui_card_createValueAndUnit(ui_driveCurrPanel, &ui_driveCurrValLabel, &ui_driveCurrUnitLabel, "A");
    ui_driveCurrBar = ui_card_createBar(ui_driveCurrPanel, true, -150, 300, 0);

    // "GEAR" not "GEAR SELECTION" - label-shortening pass, 2026-09-14 (see
    // waveshare-dash-build.md) - terser is better for a glance-first
    // dashboard even where the longer text technically still fit.
    ui_driveGearPanel = ui_card_createPanel(ui_driveScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, "GEAR", &ui_driveGearTitleLabel);
    ui_driveGearValLabel = createEnumValue(ui_driveGearPanel);

    ui_driveMotorModePanel = ui_card_createPanel(ui_driveScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, "MOTOR MODE", &ui_driveMotorModeTitleLabel);
    ui_driveMotorModeValLabel = createEnumValue(ui_driveMotorModePanel);

    ui_driveRegenPanel = ui_card_createPanel(ui_driveScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, "REGEN LIMIT", &ui_driveRegenTitleLabel);
    ui_card_createValueAndUnit(ui_driveRegenPanel, &ui_driveRegenValLabel, &ui_driveRegenUnitLabel, "%");
    ui_driveRegenBar = ui_card_createBar(ui_driveRegenPanel, false, 0, 100, 0);

    // Spare slot, deliberately empty - reserved for a future field.
    ui_driveSparePanel = ui_card_createPanel(ui_driveScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, "", NULL);

    ui_driveScreenDock = ui_dock_create(ui_driveScreen, UI_DOCK_TELEMETRY);

    lv_obj_add_event_cb(ui_driveScreen, ui_event_driveScreen, LV_EVENT_ALL, NULL);
}

void ui_driveScreen_refresh_theme(void)
{
    if (ui_driveScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_driveScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *panels[] = { ui_drivePowerPanel, ui_driveCurrPanel, ui_driveGearPanel, ui_driveMotorModePanel, ui_driveRegenPanel, ui_driveSparePanel };
    lv_obj_t *titles[] = { ui_drivePowerTitleLabel, ui_driveCurrTitleLabel, ui_driveGearTitleLabel, ui_driveMotorModeTitleLabel, ui_driveRegenTitleLabel, NULL };
    lv_obj_t *units[]  = { ui_drivePowerUnitLabel, ui_driveCurrUnitLabel, NULL, NULL, ui_driveRegenUnitLabel, NULL };
    for (int i = 0; i < 6; i++) {
        if (panels[i]) {
            lv_obj_set_style_bg_color(panels[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(panels[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (titles[i]) lv_obj_set_style_text_color(titles[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (units[i])  lv_obj_set_style_text_color(units[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_t *bars[] = { ui_drivePowerBar, ui_driveCurrBar, ui_driveRegenBar };
    for (int i = 0; i < 3; i++) {
        if (!bars[i]) continue;
        lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bars[i], ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    ui_dock_refresh_theme(ui_driveScreenDock, UI_DOCK_TELEMETRY);
    // Value labels (power/current/regen numerics + gear/motorMode enum text)
    // are left alone here - fastUpdate/midUpdate/slowUpdate recompute their
    // color from the current theme on every data update, same reasoning as
    // every other screen in this codebase.
}

void ui_driveScreen_screen_destroy(void)
{
    if (ui_driveScreen) lv_obj_del(ui_driveScreen);

    ui_driveScreen = NULL;
    ui_drivePowerPanel = NULL;
    ui_drivePowerTitleLabel = NULL;
    ui_drivePowerValLabel = NULL;
    ui_drivePowerUnitLabel = NULL;
    ui_drivePowerBar = NULL;
    ui_driveCurrPanel = NULL;
    ui_driveCurrTitleLabel = NULL;
    ui_driveCurrValLabel = NULL;
    ui_driveCurrUnitLabel = NULL;
    ui_driveCurrBar = NULL;
    ui_driveGearPanel = NULL;
    ui_driveGearTitleLabel = NULL;
    ui_driveGearValLabel = NULL;
    ui_driveMotorModePanel = NULL;
    ui_driveMotorModeTitleLabel = NULL;
    ui_driveMotorModeValLabel = NULL;
    ui_driveRegenPanel = NULL;
    ui_driveRegenTitleLabel = NULL;
    ui_driveRegenValLabel = NULL;
    ui_driveRegenUnitLabel = NULL;
    ui_driveRegenBar = NULL;
    ui_driveSparePanel = NULL;
    ui_driveScreenDock = NULL;
}
