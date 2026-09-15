// ============================================================================
// ui_speedScreen.c - HOME screen, full-screen speedometer (mph) + P/N/F/R
// ============================================================================
// Hand-written (2026-09-14), replacing ui_mainScreen.c's quad-grid as the
// HOME screen in the Speed/Drive/Status/Battery split (see
// waveshare-dash-build.md for the discussion that led to this). No
// SquareLine project for this screen (same situation as the other
// hand-written screens in this repo).
//
// Full-screen lv_meter speedometer (needle + tick scale, per CLAUDE.md's
// widget-selection rule) with the digital mph readout centered inside it,
// and a P/N/F/R shifter-position pill below. Displayed speed is converted
// to mph from myData.vehSpeedKph at the point of display only - the
// underlying field stays in kph because the dyno screens (0-60 timing,
// road-load power calc) depend on it; nothing upstream of this file changes
// units.
//
// Bound to myData in zombie_updaters.cpp's fastUpdate (same dirty-check,
// dataMutex-then-uiMutex pattern as every other binding).
//
// Navigation, following the approved topology (Settings <-> Speed(home) <->
// Drive <-> Status <-> Battery <-> Dyno LIVE -> Dyno RESULTS): physical
// swipe LEFT -> settings (unchanged from the old ui_mainScreen), physical
// swipe RIGHT -> Drive screen (was BMS before this split). This board
// reports gesture direction inverted from the physical swipe (see
// CLAUDE.md's "Touch gesture direction" - raw tap position is confirmed
// correct, only the gesture-direction label is flipped), so the code below
// checks LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe. Intentional; don't "fix" it without re-verifying on
// hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_speedScreen = NULL;

lv_obj_t * ui_speedTitleLabel = NULL;
lv_obj_t * ui_speedMeter = NULL;
lv_meter_indicator_t * ui_speedNeedle = NULL;
lv_obj_t * ui_speedValLabel = NULL;
lv_obj_t * ui_speedUnitLabel = NULL;

lv_obj_t * ui_dirPill = NULL;
lv_obj_t * ui_dirLabel = NULL;

// File-local, same reasoning as ui_mainScreen.c's old speedScale: lv_meter
// has no simple style setter for tick color, only
// lv_meter_set_scale_ticks()/_major_ticks(), so re-invoking those with the
// same geometry and new colors is how ui_speedScreen_refresh_theme()
// "restyles" the tick scale.
static lv_meter_scale_t *speedScale = NULL;

void ui_event_speedScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_settingsScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_settingsScreen_screen_init);
        _ui_screen_delete(&ui_speedScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_driveScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_driveScreen_screen_init);
        _ui_screen_delete(&ui_speedScreen);
    }
    // Brightness gesture (CLAUDE.md: "keep uaDASH's existing swipe-up/down
    // brightness gesture working alongside" day/night). No screen change.
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        upBrightness(e);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
        downBribrightness(e);
    }
}

void ui_speedScreen_screen_init(void)
{
    ui_speedScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_speedScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_speedScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_speedScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_speedTitleLabel = lv_label_create(ui_speedScreen);
    lv_obj_set_pos(ui_speedTitleLabel, 12, 8);
    lv_label_set_text(ui_speedTitleLabel, "SPEED");
    lv_obj_set_style_text_color(ui_speedTitleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_speedTitleLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Full-screen speedometer, needle + tick scale ---
    ui_speedMeter = lv_meter_create(ui_speedScreen);
    lv_obj_set_size(ui_speedMeter, 360, 360);
    lv_obj_align(ui_speedMeter, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_clear_flag(ui_speedMeter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ui_speedMeter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_speedMeter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_speedMeter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // TODO: 0-140 mph range is a placeholder - tune to your vehicle's actual
    // top speed once known (same TODO as the other placeholder ranges in
    // this codebase).
    speedScale = lv_meter_add_scale(ui_speedMeter);
    lv_meter_set_scale_range(ui_speedMeter, speedScale, 0, 140, 270, 135);  // 270-degree sweep, gap at the bottom
    lv_meter_set_scale_ticks(ui_speedMeter, speedScale, 21, 2, 12, ui_theme_text_secondary());
    lv_meter_set_scale_major_ticks(ui_speedMeter, speedScale, 5, 3, 18, ui_theme_text_secondary(), 12);  // major tick every 5th minor -> labels at 0/35/70/105/140
    ui_speedNeedle = lv_meter_add_needle_line(ui_speedMeter, speedScale, 5, ui_theme_accent(), -14);
    lv_meter_set_indicator_value(ui_speedMeter, ui_speedNeedle, 0);

    // Digital readout, centered inside the meter face.
    ui_speedValLabel = lv_label_create(ui_speedMeter);
    lv_obj_align(ui_speedValLabel, LV_ALIGN_CENTER, 0, 8);
    lv_label_set_text(ui_speedValLabel, "0");
    lv_obj_set_style_text_color(ui_speedValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_speedValLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_speedUnitLabel = lv_label_create(ui_speedMeter);
    lv_obj_align(ui_speedUnitLabel, LV_ALIGN_CENTER, 0, 58);
    lv_label_set_text(ui_speedUnitLabel, "mph");
    lv_obj_set_style_text_color(ui_speedUnitLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_speedUnitLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- P/N/F/R shifter-position pill, below the meter ---
    ui_dirPill = lv_obj_create(ui_speedScreen);
    lv_obj_clear_flag(ui_dirPill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_dirPill, 160, 64);
    lv_obj_align(ui_dirPill, LV_ALIGN_TOP_MID, 0, 408);
    lv_obj_set_style_radius(ui_dirPill, 32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_dirPill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_dirPill, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_dirPill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_dirLabel = lv_label_create(ui_dirPill);
    lv_obj_center(ui_dirLabel);
    lv_label_set_text(ui_dirLabel, "N");
    lv_obj_set_style_text_color(ui_dirLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_dirLabel, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_speedScreen, ui_event_speedScreen, LV_EVENT_ALL, NULL);
}

void ui_speedScreen_setDirState(int dirState)
{
    if (ui_dirPill == NULL || ui_dirLabel == NULL) return;

    // -1=Reverse, 0=Neutral, 1=Drive ("F"orward per the P/N/F/R naming this
    // was requested with), 2=Park - see PARAM_ID_DIR in zombie_updaters.h.
    const char *text;
    lv_color_t color;
    switch (dirState) {
        case -1: text = "R"; color = ui_theme_bad();          break;
        case  1: text = "F"; color = ui_theme_good();         break;
        case  2: text = "P"; color = ui_theme_panel_border(); break;
        case  0:
        default: text = "N"; color = ui_theme_panel_border(); break;
    }
    lv_label_set_text(ui_dirLabel, text);
    lv_obj_set_style_bg_color(ui_dirPill, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_speedScreen_refresh_theme(void)
{
    if (ui_speedScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_speedScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_speedTitleLabel) lv_obj_set_style_text_color(ui_speedTitleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_speedUnitLabel) lv_obj_set_style_text_color(ui_speedUnitLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    if (ui_speedMeter && speedScale) {
        // Re-invoke with the same geometry, new colors only - see the
        // speedScale comment at the top of this file.
        lv_meter_set_scale_ticks(ui_speedMeter, speedScale, 21, 2, 12, ui_theme_text_secondary());
        lv_meter_set_scale_major_ticks(ui_speedMeter, speedScale, 5, 3, 18, ui_theme_text_secondary(), 12);
    }
    if (ui_speedNeedle) {
        ui_speedNeedle->type_data.needle_line.color = ui_theme_accent();
        lv_obj_invalidate(ui_speedMeter);
    }
    // ui_speedValLabel and ui_dirPill/ui_dirLabel are data-driven (warn
    // color / gear color) and left alone here - they self-correct on their
    // next natural data update, same reasoning as every other screen in
    // this codebase.
}

void ui_speedScreen_screen_destroy(void)
{
    if (ui_speedScreen) lv_obj_del(ui_speedScreen);

    ui_speedScreen = NULL;
    ui_speedTitleLabel = NULL;
    ui_speedMeter = NULL;
    ui_speedNeedle = NULL;   // owned by the meter, freed with it above
    speedScale = NULL;       // ditto
    ui_speedValLabel = NULL;
    ui_speedUnitLabel = NULL;
    ui_dirPill = NULL;
    ui_dirLabel = NULL;
}
