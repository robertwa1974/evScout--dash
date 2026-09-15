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

#define GRID_PANEL_W 388
#define GRID_PANEL_H 149
#define GRID_GAP     8

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

// Themed panel "card" at a grid cell, with a title label top-left - same
// pattern as the old ui_mainScreen.c's createPanel().
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

// Value + unit, centered - same pattern as ui_mainScreen.c's
// createValueAndUnit(), just re-centered for this grid's shorter 149px panel.
static void createValueAndUnit(lv_obj_t *panel, lv_obj_t **outVal, lv_obj_t **outUnit, const char *unitText) {
    lv_obj_t *val = lv_label_create(panel);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -8);
    lv_label_set_text(val, "0");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    *outVal = val;

    lv_obj_t *unit = lv_label_create(panel);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 28);
    lv_label_set_text(unit, unitText);
    lv_obj_set_style_text_color(unit, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(unit, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    *outUnit = unit;
}

// Linear-strip lv_bar below a panel's digital readout - same pattern as
// ui_mainScreen.c's createSignedBar(), re-centered for the 149px panel.
static lv_obj_t *createBar(lv_obj_t *panel, bool symmetrical, int32_t rangeMin, int32_t rangeMax) {
    lv_obj_t *bar = lv_bar_create(panel);
    if (symmetrical) lv_bar_set_mode(bar, LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_range(bar, rangeMin, rangeMax);
    lv_bar_set_value(bar, symmetrical ? 0 : rangeMin, LV_ANIM_OFF);
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

// Big centered text value for a discrete enum state (gear selection, motor
// mode) - no bar, this isn't a continuous quantity.
static lv_obj_t *createEnumValue(lv_obj_t *panel) {
    lv_obj_t *val = lv_label_create(panel);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 12);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    ui_drivePowerPanel = createPanel(ui_driveScreen, GRID_GAP, GRID_GAP, ICON_BOLT " POWER", &ui_drivePowerTitleLabel);
    createValueAndUnit(ui_drivePowerPanel, &ui_drivePowerValLabel, &ui_drivePowerUnitLabel, "kW");
    ui_drivePowerBar = createBar(ui_drivePowerPanel, true, -30, 150);

    ui_driveCurrPanel = createPanel(ui_driveScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, "PACK CURRENT", &ui_driveCurrTitleLabel);
    createValueAndUnit(ui_driveCurrPanel, &ui_driveCurrValLabel, &ui_driveCurrUnitLabel, "A");
    ui_driveCurrBar = createBar(ui_driveCurrPanel, true, -150, 300);

    // "GEAR" not "GEAR SELECTION" - label-shortening pass, 2026-09-14 (see
    // waveshare-dash-build.md) - terser is better for a glance-first
    // dashboard even where the longer text technically still fit.
    ui_driveGearPanel = createPanel(ui_driveScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, "GEAR", &ui_driveGearTitleLabel);
    ui_driveGearValLabel = createEnumValue(ui_driveGearPanel);

    ui_driveMotorModePanel = createPanel(ui_driveScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, "MOTOR MODE", &ui_driveMotorModeTitleLabel);
    ui_driveMotorModeValLabel = createEnumValue(ui_driveMotorModePanel);

    ui_driveRegenPanel = createPanel(ui_driveScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, "REGEN LIMIT", &ui_driveRegenTitleLabel);
    createValueAndUnit(ui_driveRegenPanel, &ui_driveRegenValLabel, &ui_driveRegenUnitLabel, "%");
    ui_driveRegenBar = createBar(ui_driveRegenPanel, false, 0, 100);

    // Spare slot, deliberately empty - reserved for a future field.
    ui_driveSparePanel = createPanel(ui_driveScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, "", NULL);

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
}
