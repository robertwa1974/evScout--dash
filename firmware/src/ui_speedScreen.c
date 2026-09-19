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
// Navigation: physical swipe LEFT -> Drive screen (stays - Speed/Drive are
// both in the dock's Telemetry group, so this intra-group link is kept).
// The physical-RIGHT swipe to Settings is REMOVED (styling/UX pass Phase 5,
// 2026-09-18) - Settings is its own dock-only destination now (cross-group
// swipes are what the dock replaces; see ui_dock.h's mapping comment and
// the plan's swipe-removal table). This board reports gesture direction
// inverted from the physical swipe (see CLAUDE.md's "Touch gesture
// direction" - raw tap position is confirmed correct, only the gesture-
// direction label is flipped), so the code below checks LV_DIR_LEFT for
// the physical-RIGHT swipe. Intentional; don't "fix" it without
// re-verifying on hardware first.
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
// Tracks the pill's current semantic state for refresh_theme() - see
// ui_gpsScreen.c's identical pattern/comment for why.
static ui_pill_state_t ui_dirPillState = UI_PILL_NEUTRAL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Speed is the Telemetry group's home/canonical screen.
static lv_obj_t * ui_speedScreenDock = NULL;

// File-local, same reasoning as ui_mainScreen.c's old speedScale: lv_meter
// has no simple style setter for tick color, only
// lv_meter_set_scale_ticks()/_major_ticks(), so re-invoking those with the
// same geometry and new colors is how ui_speedScreen_refresh_theme()
// "restyles" the tick scale.
static lv_meter_scale_t *speedScale = NULL;

void ui_event_speedScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    // Physical-RIGHT swipe to Settings REMOVED here - see this file's
    // header comment (styling/UX pass Phase 5).
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
    lv_obj_set_style_text_font(ui_speedTitleLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Full-screen speedometer, needle + tick scale ---
    // 300x300 and moved up (was 360x360 @ y=40) - shrunk to leave room for
    // the P/N/F/R pill above the new 72px bottom nav dock (styling/UX pass
    // Phase 5, 2026-09-18) without either overlapping the dock's own zone.
    ui_speedMeter = lv_meter_create(ui_speedScreen);
    lv_obj_set_size(ui_speedMeter, 300, 300);
    lv_obj_align(ui_speedMeter, LV_ALIGN_TOP_MID, 0, 32);
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
    lv_obj_set_style_text_font(ui_speedUnitLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- P/N/F/R shifter-position pill, below the meter ---
    // Styling pass: migrated onto ui_status_pill.h's shared pill (was a
    // hand-built lv_obj with no fade transition at all - one of the 2 of
    // 5 pills that previously snapped instantly, see that header's
    // rationale). Single-character text stays ExtraBold-32 - the 24px
    // font-weight audit doesn't apply at this size (see convert_font.py:
    // no SemiBold-32 exists, nothing at 32/48px is ever a label).
    // 56px tall @ y=348 (was 64px @ y=408) - shrunk/moved up alongside the
    // meter above so the bottom edge (348+56=404) clears the new 72px
    // bottom nav dock (dock zone starts at y=408) with a small margin.
    ui_dirPill = ui_pill_create(ui_speedScreen, 160, 56, 28, &ui_dirLabel, &font_montserrat_extrabold_32);
    lv_obj_align(ui_dirPill, LV_ALIGN_TOP_MID, 0, 348);
    ui_pill_setState(ui_dirPill, ui_dirLabel, UI_PILL_NEUTRAL, "N");

    ui_speedScreenDock = ui_dock_create(ui_speedScreen, UI_DOCK_TELEMETRY);

    lv_obj_add_event_cb(ui_speedScreen, ui_event_speedScreen, LV_EVENT_ALL, NULL);
}

void ui_speedScreen_setDirState(int dirState)
{
    if (ui_dirPill == NULL || ui_dirLabel == NULL) return;

    // -1=Reverse, 0=Neutral, 1=Drive ("F"orward per the P/N/F/R naming this
    // was requested with), 2=Park - see PARAM_ID_DIR in zombie_updaters.h.
    const char *text;
    switch (dirState) {
        case -1: text = "R"; ui_dirPillState = UI_PILL_CAUTION; break;
        case  1: text = "F"; ui_dirPillState = UI_PILL_ACTIVE;  break;
        case  2: text = "P"; ui_dirPillState = UI_PILL_NEUTRAL; break;
        case  0:
        default: text = "N"; ui_dirPillState = UI_PILL_NEUTRAL; break;
    }
    ui_pill_setState(ui_dirPill, ui_dirLabel, ui_dirPillState, text);
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
    ui_pill_refreshTheme(ui_dirPill, ui_dirPillState);
    ui_dock_refresh_theme(ui_speedScreenDock, UI_DOCK_TELEMETRY);
    // ui_speedValLabel is data-driven (warn color) and left alone here - it
    // self-corrects on its next natural data update, same reasoning as
    // every other screen in this codebase.
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
    ui_dirPillState = UI_PILL_NEUTRAL;
    ui_speedScreenDock = NULL;
}
