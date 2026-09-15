// ============================================================================
// ui_batteryScreen.c - cell-level BMS detail (renamed from ui_bmsScreen.c)
// ============================================================================
// Layout (800x480), hand-written, no SquareLine project (same situation as
// every other hand-written screen in this repo):
//   Large SOC ring, centered top:     200x200 arc, x=300 y=10
//   Status pill (charge/discharge/idle), centered: 240x44, x=280 y=220
//   4 stacked labeled bar rows, y=284..480 (40px row + 12px gap):
//     Max Cell Voltage, Min Cell Voltage, Cell Delta V (computed), Max Cell
//     Temp - see waveshare-dash-build.md for the 2026-09-14 decision to
//     drop the old pack-aggregate voltage/current rows (moved to
//     ui_statusScreen.c/ui_driveScreen.c) in favor of genuinely cell-level
//     data now that BMS_Vmax/BMS_Vmin/BMS_Tmax are polled (PARAM_ID_BMS_*
//     in zombie_updaters.h).
//
// Per-cell voltage bars for EVERY individual cell (JKBMS's actual headline
// feature) are still not built - the VCU only relays pack-level min/max/
// delta, not a full per-cell array (see firmware/can-bench.md). If a
// cell-level BMS data source with a full per-cell array gets added later,
// see the placeholder comment below for where that would go.
//
// Bound to myData in zombie_updaters.cpp, same dirty-check + dataMutex-then-
// uiMutex pattern as every other screen.
//
// Navigation, following the approved topology (Settings <-> Speed(home) <->
// Drive <-> Status <-> Battery <-> Charging <-> Dyno LIVE -> Dyno RESULTS -
// Charging inserted 2026-09-14 between Battery and Dyno LIVE): physical
// swipe LEFT -> Status (back), physical swipe RIGHT -> Charging (forward,
// was Dyno LIVE before Charging was inserted). This board reports gesture
// direction inverted from the physical swipe
// (see CLAUDE.md's "Touch gesture direction") - the code checks
// LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe. Intentional; don't "fix" it without re-verifying on
// hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_batteryScreen = NULL;

lv_obj_t * ui_batterySocArc = NULL;
lv_obj_t * ui_batterySocValLabel = NULL;
lv_obj_t * ui_batterySocIconLabel = NULL;

lv_obj_t * ui_batteryStatusPill = NULL;
lv_obj_t * ui_batteryStatusLabel = NULL;

lv_obj_t * ui_batteryVMaxLabel = NULL;
lv_obj_t * ui_batteryVMaxBar = NULL;
lv_obj_t * ui_batteryVMaxValLabel = NULL;

lv_obj_t * ui_batteryVMinLabel = NULL;
lv_obj_t * ui_batteryVMinBar = NULL;
lv_obj_t * ui_batteryVMinValLabel = NULL;

lv_obj_t * ui_batteryDeltaVLabel = NULL;
lv_obj_t * ui_batteryDeltaVBar = NULL;
lv_obj_t * ui_batteryDeltaVValLabel = NULL;

lv_obj_t * ui_batteryTMaxLabel = NULL;
lv_obj_t * ui_batteryTMaxBar = NULL;
lv_obj_t * ui_batteryTMaxValLabel = NULL;

// TODO (full per-cell BMS data source, if one gets added): a scrollable
// list of N per-cell voltage bars would go here, one row each, JKBMS-style
// - mirroring createBarRow() below but built dynamically for however many
// cells the pack reports instead of the 4 fixed rows.

void ui_event_batteryScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_statusScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_statusScreen_screen_init);
        _ui_screen_delete(&ui_batteryScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_chargingScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_chargingScreen_screen_init);
        _ui_screen_delete(&ui_batteryScreen);
    }
}

static void createBarRow(lv_obj_t *parent, int16_t y, const char *title, bool symmetrical,
                          int32_t rangeMin, int32_t rangeMax,
                          lv_obj_t **outTitle, lv_obj_t **outBar, lv_obj_t **outVal) {
    lv_obj_t *title_label = lv_label_create(parent);
    lv_obj_set_pos(title_label, 40, y + 10);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outTitle) *outTitle = title_label;

    lv_obj_t *bar = lv_bar_create(parent);
    if (symmetrical) {
        lv_bar_set_mode(bar, LV_BAR_MODE_SYMMETRICAL);
    }
    lv_bar_set_range(bar, rangeMin, rangeMax);
    lv_bar_set_value(bar, symmetrical ? 0 : rangeMin, LV_ANIM_OFF);
    lv_obj_set_size(bar, 410, 20);
    lv_obj_set_pos(bar, 200, y + 10);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bar, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    if (outBar) *outBar = bar;

    lv_obj_t *val = lv_label_create(parent);
    lv_obj_set_pos(val, 620, y + 8);
    lv_obj_set_width(val, 160);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(val, "0");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outVal) *outVal = val;
}

void ui_batteryScreen_screen_init(void)
{
    ui_batteryScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_batteryScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_batteryScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_batteryScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- SOC ring, centered top ---
    ui_batterySocArc = lv_arc_create(ui_batteryScreen);
    lv_obj_set_size(ui_batterySocArc, 200, 200);
    lv_obj_set_pos(ui_batterySocArc, 300, 10);
    lv_arc_set_bg_angles(ui_batterySocArc, 135, 45);
    lv_arc_set_range(ui_batterySocArc, 0, 100);
    lv_arc_set_value(ui_batterySocArc, 0);
    lv_obj_remove_style(ui_batterySocArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ui_batterySocArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ui_batterySocArc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_batterySocArc, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_batterySocArc, 18, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_batterySocValLabel = lv_label_create(ui_batteryScreen);
    // Centered on the arc's actual vertical middle (arc: y=10, 200 tall ->
    // center y=110). Digits only, no "%" - same arc-containment reasoning
    // as every other SOC label in this codebase (200px arc, 18px stroke ->
    // ~164px inner diameter).
    lv_obj_align(ui_batterySocValLabel, LV_ALIGN_TOP_MID, 0, 110 - 24);
    lv_label_set_text(ui_batterySocValLabel, "0");
    lv_obj_set_style_text_color(ui_batterySocValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_batterySocValLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Battery-level icon below the percentage (design-review "icons" pass,
    // 2026-09-14) - LVGL's built-in symbol glyphs (baked into the bundled
    // Montserrat fonts, no new asset/font-conversion pipeline needed).
    // y=160 sits well inside the arc's inner circle (arc spans screen
    // y=10..210; the value label above ends around y=144), so this is a
    // second, lighter-weight glanceable cue, not a replacement for it.
    ui_batterySocIconLabel = lv_label_create(ui_batteryScreen);
    lv_obj_align(ui_batterySocIconLabel, LV_ALIGN_TOP_MID, 0, 160);
    lv_label_set_text(ui_batterySocIconLabel, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(ui_batterySocIconLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_batterySocIconLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Status pill ---
    ui_batteryStatusPill = lv_obj_create(ui_batteryScreen);
    lv_obj_clear_flag(ui_batteryStatusPill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_batteryStatusPill, 240, 44);
    lv_obj_set_pos(ui_batteryStatusPill, 280, 220);
    lv_obj_set_style_radius(ui_batteryStatusPill, 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_batteryStatusPill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_batteryStatusPill, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_batteryStatusPill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Smooth color fade instead of an instant snap when charge status
    // changes (design-review "motion" pass, rolled out from the Charging
    // screen prototype - see waveshare-dash-build.md, 2026-09-14). Static
    // storage: LVGL keeps a pointer to both structs, not a copy - fine
    // since screens in this codebase are never actually destroyed (see
    // _ui_screen_delete's known-inverted-condition note in ui_helpers.h).
    static const lv_style_prop_t pillTransProps[] = { LV_STYLE_BG_COLOR, 0 };
    static lv_style_transition_dsc_t pillTransDsc;
    static lv_style_t pillTransStyle;
    lv_style_transition_dsc_init(&pillTransDsc, pillTransProps, lv_anim_path_ease_out, 400, 0, NULL);
    lv_style_init(&pillTransStyle);
    lv_style_set_transition(&pillTransStyle, &pillTransDsc);
    lv_obj_add_style(ui_batteryStatusPill, &pillTransStyle, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_batteryStatusLabel = lv_label_create(ui_batteryStatusPill);
    lv_obj_center(ui_batteryStatusLabel);
    lv_label_set_text(ui_batteryStatusLabel, "IDLE");
    lv_obj_set_style_text_color(ui_batteryStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_batteryStatusLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- 4 cell-level bar rows ---
    // TODO: cell voltage range (2.5-4.3V) and cell temp range are Li-ion
    // placeholder defaults - tune to your actual cell chemistry once known.
    // "MAX/MIN CELL V" not "...VOLTAGE" - found on hardware 2026-09-14
    // after the ExtraBold font pass: the full word measured 175px/168px in
    // the new bold 16px font against this row's ~160px title-before-bar
    // budget (title at x=40, bar starts at x=200), clashing with the bar.
    // Shortened to match "CELL DELTA V" below, which already used this
    // abbreviation - not a new convention, just applied consistently.
    createBarRow(ui_batteryScreen, 284, "MAX CELL V", false, 250, 430,
                 &ui_batteryVMaxLabel, &ui_batteryVMaxBar, &ui_batteryVMaxValLabel);
    createBarRow(ui_batteryScreen, 336, "MIN CELL V", false, 250, 430,
                 &ui_batteryVMinLabel, &ui_batteryVMinBar, &ui_batteryVMinValLabel);
    createBarRow(ui_batteryScreen, 388, "CELL DELTA V", false, 0, 50,
                 &ui_batteryDeltaVLabel, &ui_batteryDeltaVBar, &ui_batteryDeltaVValLabel);
    createBarRow(ui_batteryScreen, 440, "MAX CELL TEMP", true, -20, 80,
                 &ui_batteryTMaxLabel, &ui_batteryTMaxBar, &ui_batteryTMaxValLabel);

    lv_obj_add_event_cb(ui_batteryScreen, ui_event_batteryScreen, LV_EVENT_ALL, NULL);
}

void ui_batteryScreen_setStatus(float packCurrent) {
    if (ui_batteryStatusPill == NULL) return;

    const float deadband = 0.5f;  // avoid flicker right around 0A
    const char *text;
    lv_color_t color;
    if (packCurrent < -deadband) {
        text = "CHARGING";
        color = ui_theme_good();
    } else if (packCurrent > deadband) {
        text = "DISCHARGING";
        color = ui_theme_bad();
    } else {
        text = "IDLE";
        color = ui_theme_panel_border();
    }
    lv_label_set_text(ui_batteryStatusLabel, text);
    lv_obj_set_style_bg_color(ui_batteryStatusPill, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_batteryScreen_refresh_theme(void)
{
    if (ui_batteryScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_batteryScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_batterySocValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_batterySocIconLabel) lv_obj_set_style_text_color(ui_batterySocIconLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_t *titles[] = { ui_batteryVMaxLabel, ui_batteryVMinLabel, ui_batteryDeltaVLabel, ui_batteryTMaxLabel };
    lv_obj_t *bars[]   = { ui_batteryVMaxBar, ui_batteryVMinBar, ui_batteryDeltaVBar, ui_batteryTMaxBar };
    lv_obj_t *vals[]   = { ui_batteryVMaxValLabel, ui_batteryVMinValLabel, ui_batteryDeltaVValLabel, ui_batteryTMaxValLabel };
    for (int i = 0; i < 4; i++) {
        if (titles[i]) lv_obj_set_style_text_color(titles[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (bars[i]) {
            lv_obj_set_style_bg_color(bars[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(bars[i], ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        if (vals[i]) lv_obj_set_style_text_color(vals[i], ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Status pill color is data-driven (charge/discharge/idle) - left alone
    // here, same reasoning as the value labels.
}

void ui_batteryScreen_screen_destroy(void)
{
    if (ui_batteryScreen) lv_obj_del(ui_batteryScreen);

    ui_batteryScreen = NULL;
    ui_batterySocArc = NULL;
    ui_batterySocValLabel = NULL;
    ui_batterySocIconLabel = NULL;
    ui_batteryStatusPill = NULL;
    ui_batteryStatusLabel = NULL;
    ui_batteryVMaxLabel = NULL;
    ui_batteryVMaxBar = NULL;
    ui_batteryVMaxValLabel = NULL;
    ui_batteryVMinLabel = NULL;
    ui_batteryVMinBar = NULL;
    ui_batteryVMinValLabel = NULL;
    ui_batteryDeltaVLabel = NULL;
    ui_batteryDeltaVBar = NULL;
    ui_batteryDeltaVValLabel = NULL;
    ui_batteryTMaxLabel = NULL;
    ui_batteryTMaxBar = NULL;
    ui_batteryTMaxValLabel = NULL;
}
