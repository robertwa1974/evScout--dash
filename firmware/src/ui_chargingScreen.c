// ============================================================================
// ui_chargingScreen.c - charging status, 2x3 grid
// ============================================================================
// Hand-written (2026-09-14), inserted into the screen chain per Rob's
// request (see waveshare-dash-build.md). No SquareLine project (same
// situation as every other hand-written screen in this repo).
//
// 2x3 grid of six equal 388x149 panels, same geometry as ui_driveScreen.c/
// ui_statusScreen.c:
//   (0,0) 8,8     SOC              - %, a drawn battery silhouette (body +
//                                    terminal nub + animated fill), not an
//                                    lv_arc ring - design-review decision,
//                                    2026-09-14 (see waveshare-dash-build.md):
//                                    this is the one screen SOC should
//                                    visually read as "a battery filling
//                                    up," not just a percentage
//   (1,0) 404,8   Charge Status    - pill: "CHARGING - AC"/"CHARGING -
//                                    DCFC"/"NOT CHARGING" (from opmode==4
//                                    + chgType), plus a secondary "Plug:
//                                    Connected/Not detected" label (PlugDet)
//   (0,1) 8,165   Charge Setpoint  - V, Voltspnt - this VCU charges to a
//                                    target pack VOLTAGE, not a target
//                                    SOC% (confirmed by Rob - CCS_SOCLim/
//                                    BMS_ChargeLim/BMS_MaxCharge were all
//                                    considered and rejected, see
//                                    zombie_updaters.h's PARAM_ID_VOLTSPNT)
//   (1,1) 404,165 Charger Temp     - degC, signed (ChgTemp)
//   (0,2) 8,322   AC Supply Volts  - V (AC_Volts) - a charger-health check
//   (1,2) 404,322 Cable/EVSE Limit - A, PilotLim (what the EVSE is
//                                    currently offering, primary bar+value)
//                                    with CableLim (the cable's rated
//                                    capacity, secondary text) underneath -
//                                    this VCU has no raw "PPVal" telemetry,
//                                    these two derived current limits are
//                                    the closest equivalent
//
// Widget choice follows CLAUDE.md: lv_arc for the SOC ring, lv_bar for
// every linear-strip numeric value, a pill (like ui_batteryScreen.c's
// status pill) for the discrete charge-state indicator - never lv_slider
// for read-only telemetry.
//
// Bound to myData in zombie_updaters.cpp's slowUpdate (same dirty-check,
// dataMutex-then-uiMutex pattern as every other binding - all these fields
// are genuinely slow-changing, no need for the fast/mid tiers).
//
// Navigation, following the current topology (Settings <-> Speed(home) <->
// Drive <-> Status <-> Battery <-> Charging <-> GPS <-> Dyno LIVE -> Dyno
// RESULTS - GPS inserted 2026-09-14 between Charging and Dyno LIVE):
// physical swipe LEFT -> Battery (back), physical swipe RIGHT -> GPS
// (forward, was Dyno LIVE before GPS was inserted). This board reports
// gesture direction inverted from the physical swipe (see CLAUDE.md's
// "Touch gesture direction") - the code checks LV_DIR_RIGHT for the
// physical-LEFT swipe and LV_DIR_LEFT for the physical-RIGHT swipe.
// Intentional; don't "fix" it without re-verifying on hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_chargingScreen = NULL;

lv_obj_t * ui_chargingSocPanel = NULL;
lv_obj_t * ui_chargingBatteryBody = NULL;
lv_obj_t * ui_chargingBatteryNub = NULL;
lv_obj_t * ui_chargingBatteryFill = NULL;
lv_obj_t * ui_chargingSocValLabel = NULL;

// Max width the fill rect can animate to (== battery body inner width,
// after the border/inset) - see ui_chargingScreen_setSoc().
#define BATTERY_FILL_MAX_W 138

lv_obj_t * ui_chargingStatusPanel = NULL;
lv_obj_t * ui_chargingStatusPill = NULL;
lv_obj_t * ui_chargingStatusLabel = NULL;
lv_obj_t * ui_chargingPlugLabel = NULL;

lv_obj_t * ui_chargingSetpointPanel = NULL;
lv_obj_t * ui_chargingSetpointValLabel = NULL;
lv_obj_t * ui_chargingSetpointBar = NULL;

lv_obj_t * ui_chargingTempPanel = NULL;
lv_obj_t * ui_chargingTempValLabel = NULL;
lv_obj_t * ui_chargingTempBar = NULL;

lv_obj_t * ui_chargingAcVPanel = NULL;
lv_obj_t * ui_chargingAcVValLabel = NULL;
lv_obj_t * ui_chargingAcVBar = NULL;

lv_obj_t * ui_chargingLimPanel = NULL;
lv_obj_t * ui_chargingLimValLabel = NULL;
lv_obj_t * ui_chargingLimBar = NULL;
lv_obj_t * ui_chargingCableLimLabel = NULL;

#define GRID_PANEL_W 388
#define GRID_PANEL_H 149
#define GRID_GAP     8

void ui_event_chargingScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_batteryScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_batteryScreen_screen_init);
        _ui_screen_delete(&ui_chargingScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_gpsScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_gpsScreen_screen_init);
        _ui_screen_delete(&ui_chargingScreen);
    }
}

// Same createPanel/createValueAndUnit/createBar pattern as
// ui_driveScreen.c/ui_statusScreen.c - kept file-local/duplicated rather
// than shared, matching this codebase's existing convention.
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
    lv_obj_set_style_bg_color(bar, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return bar;
}

void ui_chargingScreen_screen_init(void)
{
    ui_chargingScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_chargingScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_chargingScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- SOC: a drawn battery silhouette (body + terminal nub + animated
    // fill), not an lv_arc ring - design-review decision, see the header
    // comment. Body 150x64, nub 10x26 attached to its right edge, fill
    // inset 6px on every side of the body's inner area (150-2*6=138 max
    // width, matches BATTERY_FILL_MAX_W).
    ui_chargingSocPanel = createPanel(ui_chargingScreen, GRID_GAP, GRID_GAP, "SOC", NULL);

    ui_chargingBatteryBody = lv_obj_create(ui_chargingSocPanel);
    lv_obj_clear_flag(ui_chargingBatteryBody, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_chargingBatteryBody, 150, 64);
    lv_obj_align(ui_chargingBatteryBody, LV_ALIGN_LEFT_MID, 16, 4);
    lv_obj_set_style_radius(ui_chargingBatteryBody, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_chargingBatteryBody, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_chargingBatteryBody, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chargingBatteryBody, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingBatteryBody, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_chargingBatteryBody, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Child of the PANEL, not the body - a child positioned outside its
    // parent's bounds gets clipped away by LVGL's default child clipping,
    // which is exactly what made the nub invisible when it was a child of
    // ui_chargingBatteryBody aligned outside the body's own edge (found on
    // hardware, 2026-09-14). Positioned in panel coordinates instead, just
    // past the body's right edge (16+150=166, 3px overlap for a seamless
    // join), with the same y-offset as the body so both objects' own
    // independently-computed vertical centers land on the same line.
    ui_chargingBatteryNub = lv_obj_create(ui_chargingSocPanel);
    lv_obj_clear_flag(ui_chargingBatteryNub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_chargingBatteryNub, 10, 26);
    lv_obj_align(ui_chargingBatteryNub, LV_ALIGN_LEFT_MID, 16 + 150 - 3, 4);
    lv_obj_set_style_radius(ui_chargingBatteryNub, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_chargingBatteryNub, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chargingBatteryNub, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingBatteryNub, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Fill starts at width 0 (empty) - ui_chargingScreen_setSoc() animates
    // it to the real value on the first data update (or immediately on a
    // forced push - see uiGeneration in zombie_updaters.h).
    ui_chargingBatteryFill = lv_obj_create(ui_chargingBatteryBody);
    lv_obj_clear_flag(ui_chargingBatteryFill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_chargingBatteryFill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_chargingBatteryFill, 0, 52);
    lv_obj_align(ui_chargingBatteryFill, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_radius(ui_chargingBatteryFill, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_chargingBatteryFill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chargingBatteryFill, ui_theme_accent_energy(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingBatteryFill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_chargingSocValLabel = lv_label_create(ui_chargingSocPanel);
    // Hero-tier number to the right of the battery shape, not overlaid on
    // it (avoids the arc-containment problem entirely by not sharing space
    // with the shape at all) - digits only, "%" would be redundant next to
    // a battery icon anyway.
    lv_obj_align(ui_chargingSocValLabel, LV_ALIGN_LEFT_MID, 200, 4);
    lv_label_set_text(ui_chargingSocValLabel, "0");
    lv_obj_set_style_text_color(ui_chargingSocValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_chargingSocValLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Charge Status: pill + secondary plug-detected label ---
    ui_chargingStatusPanel = createPanel(ui_chargingScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, "CHARGE STATUS", NULL);

    ui_chargingStatusPill = lv_obj_create(ui_chargingStatusPanel);
    lv_obj_clear_flag(ui_chargingStatusPill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_chargingStatusPill, 320, 44);
    lv_obj_align(ui_chargingStatusPill, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(ui_chargingStatusPill, 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_chargingStatusPill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chargingStatusPill, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingStatusPill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Smooth color fade instead of an instant snap when charge status
    // changes (design-review "motion" pass, 2026-09-14) - a style
    // transition, not a one-shot lv_anim like the battery fill above,
    // since this needs to apply to every future lv_obj_set_style_bg_color
    // call on this pill (in ui_chargingScreen_setStatus()), not just one
    // tween. static storage: LVGL keeps a pointer to both structs, not a
    // copy, so they must outlive this function - fine here since screens
    // in this codebase are never actually destroyed (see _ui_screen_delete's
    // known-inverted-condition note in ui_helpers.h).
    static const lv_style_prop_t pillTransProps[] = { LV_STYLE_BG_COLOR, 0 };
    static lv_style_transition_dsc_t pillTransDsc;
    static lv_style_t pillTransStyle;
    lv_style_transition_dsc_init(&pillTransDsc, pillTransProps, lv_anim_path_ease_out, 400, 0, NULL);
    lv_style_init(&pillTransStyle);
    lv_style_set_transition(&pillTransStyle, &pillTransDsc);
    lv_obj_add_style(ui_chargingStatusPill, &pillTransStyle, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_chargingStatusLabel = lv_label_create(ui_chargingStatusPill);
    lv_obj_center(ui_chargingStatusLabel);
    lv_label_set_text(ui_chargingStatusLabel, "NOT CHARGING");
    lv_obj_set_style_text_color(ui_chargingStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_chargingStatusLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_chargingPlugLabel = lv_label_create(ui_chargingStatusPanel);
    lv_obj_align(ui_chargingPlugLabel, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(ui_chargingPlugLabel, "Plug: --");
    lv_obj_set_style_text_color(ui_chargingPlugLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_chargingPlugLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Charge Setpoint: Voltspnt, V ---
    // TODO: 0-450V range is a placeholder, matching the other pack-voltage
    // bars in this codebase - tune to your actual pack once known.
    // "SETPOINT" not "CHARGE SETPOINT" - label-shortening pass, 2026-09-14
    // (see waveshare-dash-build.md) - the screen title already says
    // "charging", repeating it in every panel title is redundant.
    ui_chargingSetpointPanel = createPanel(ui_chargingScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, "SETPOINT", NULL);
    createValueAndUnit(ui_chargingSetpointPanel, &ui_chargingSetpointValLabel, NULL, "V");
    ui_chargingSetpointBar = createBar(ui_chargingSetpointPanel, false, 0, 450);

    // --- Charger Temp: ChgTemp, degC (signed) ---
    ui_chargingTempPanel = createPanel(ui_chargingScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, ICON_THERMOSTAT " CHARGER TEMP", NULL);
    createValueAndUnit(ui_chargingTempPanel, &ui_chargingTempValLabel, NULL, "\xC2\xB0" "C");
    ui_chargingTempBar = createBar(ui_chargingTempPanel, true, -20, 100);

    // --- AC Supply Voltage: AC_Volts, V ---
    // TODO: 0-260V placeholder covers single/split-phase AC - widen if this
    // vehicle ever sees 3-phase supply.
    // "AC VOLTS" not "AC SUPPLY VOLTS" - label-shortening pass, 2026-09-14
    // (see waveshare-dash-build.md).
    ui_chargingAcVPanel = createPanel(ui_chargingScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_BOLT " AC VOLTS", NULL);
    createValueAndUnit(ui_chargingAcVPanel, &ui_chargingAcVValLabel, NULL, "V");
    ui_chargingAcVBar = createBar(ui_chargingAcVPanel, false, 0, 260);

    // --- Cable/EVSE Limit: PilotLim (primary) + CableLim (secondary) ---
    // TODO: 0-80A placeholder covers typical J1772/Type2 cable ratings.
    //
    // This is the one cell in this codebase with FOUR stacked elements
    // (value/unit/bar/secondary-label) instead of createValueAndUnit()'s +
    // createBar()'s usual three (-8/+28/+58 offsets) - those three already
    // use nearly the panel's full 149px height, so a 4th line built by hand
    // here instead of via the shared helpers, with tighter offsets, rather
    // than reusing spacing that was never budgeted for a 4th line. Found on
    // hardware (2026-09-14): the original version called the shared
    // helpers unchanged and just bolted the Cable label onto the bottom
    // corner, which put it directly on top of the bar.
    ui_chargingLimPanel = createPanel(ui_chargingScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, "EVSE LIMIT", NULL);

    ui_chargingLimValLabel = lv_label_create(ui_chargingLimPanel);
    lv_obj_align(ui_chargingLimValLabel, LV_ALIGN_CENTER, 0, -26);
    lv_label_set_text(ui_chargingLimValLabel, "0");
    lv_obj_set_style_text_color(ui_chargingLimValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_chargingLimValLabel, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *limUnit = lv_label_create(ui_chargingLimPanel);
    lv_obj_align(limUnit, LV_ALIGN_CENTER, 0, 2);
    lv_label_set_text(limUnit, "A");
    lv_obj_set_style_text_color(limUnit, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(limUnit, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_chargingLimBar = lv_bar_create(ui_chargingLimPanel);
    lv_bar_set_range(ui_chargingLimBar, 0, 80);
    lv_bar_set_value(ui_chargingLimBar, 0, LV_ANIM_OFF);
    lv_obj_set_size(ui_chargingLimBar, 320, 14);
    lv_obj_align(ui_chargingLimBar, LV_ALIGN_CENTER, 0, 24);
    lv_obj_set_style_radius(ui_chargingLimBar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chargingLimBar, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingLimBar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_chargingLimBar, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_chargingLimBar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_chargingLimBar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chargingLimBar, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_chargingLimBar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_chargingCableLimLabel = lv_label_create(ui_chargingLimPanel);
    lv_obj_align(ui_chargingCableLimLabel, LV_ALIGN_CENTER, 0, 48);
    lv_label_set_text(ui_chargingCableLimLabel, "Cable: --");
    lv_obj_set_style_text_color(ui_chargingCableLimLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_chargingCableLimLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_chargingScreen, ui_event_chargingScreen, LV_EVENT_ALL, NULL);
}

static void batteryFillAnimExec(void * obj, int32_t v) {
    lv_obj_set_width((lv_obj_t *)obj, v);
}

void ui_chargingScreen_setSoc(int soc, bool warn)
{
    if (ui_chargingSocValLabel) {
        lv_label_set_text_fmt(ui_chargingSocValLabel, "%d", soc);
        lv_obj_set_style_text_color(ui_chargingSocValLabel, warn ? ui_theme_warning() : ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_chargingBatteryFill == NULL) return;

    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    int32_t targetW = (BATTERY_FILL_MAX_W * soc) / 100;

    // Eased fill tween (design-review "motion as a design material" pass,
    // 2026-09-14 - see waveshare-dash-build.md) rather than an instant
    // jump. lv_bar's own LV_ANIM_ON does this natively for bars; this is a
    // plain lv_obj (drawn battery shape, not a bar widget), so it needs its
    // own lv_anim_t targeting the fill rect's width.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_chargingBatteryFill);
    lv_anim_set_exec_cb(&a, batteryFillAnimExec);
    lv_anim_set_values(&a, lv_obj_get_width(ui_chargingBatteryFill), targetW);
    lv_anim_set_time(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_obj_set_style_bg_color(ui_chargingBatteryFill, warn ? ui_theme_warning() : ui_theme_accent_energy(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_chargingScreen_setStatus(int opmode, int chgType, int plugDet)
{
    if (ui_chargingStatusPill == NULL || ui_chargingStatusLabel == NULL) return;

    // opmode: 0=Off,1=Run,2=Precharge,3=PchFail,4=Charge (PARAM_ID_OPMODE)
    bool charging = (opmode == 4);
    lv_color_t color;
    if (charging) {
        // chgType: 0=Off,1=AC,2=DCFC (PARAM_ID_CHGTYP). LV_SYMBOL_CHARGE
        // prefix - design-review "icons" pass, 2026-09-14 (see
        // waveshare-dash-build.md) - only shown while actually charging,
        // not as a static decoration.
        lv_label_set_text_fmt(ui_chargingStatusLabel, LV_SYMBOL_CHARGE " CHARGING - %s", (chgType == 2) ? "DCFC" : "AC");
        color = ui_theme_good();
    } else {
        lv_label_set_text(ui_chargingStatusLabel, "NOT CHARGING");
        color = ui_theme_panel_border();
    }
    lv_obj_set_style_bg_color(ui_chargingStatusPill, color, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (ui_chargingPlugLabel) {
        // plugDet: 0=Off,1=On,2=na (PARAM_ID_PLUGDET)
        const char *plugText = (plugDet == 1) ? "Plug: Connected"
                              : (plugDet == 0) ? "Plug: Not detected"
                              : "Plug: --";
        lv_label_set_text(ui_chargingPlugLabel, plugText);
    }
}

void ui_chargingScreen_refresh_theme(void)
{
    if (ui_chargingScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_chargingScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *panels[] = { ui_chargingSocPanel, ui_chargingStatusPanel, ui_chargingSetpointPanel, ui_chargingTempPanel, ui_chargingAcVPanel, ui_chargingLimPanel };
    for (int i = 0; i < 6; i++) {
        if (!panels[i]) continue;
        lv_obj_set_style_bg_color(panels[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(panels[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_chargingBatteryBody) {
        lv_obj_set_style_border_color(ui_chargingBatteryBody, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_chargingBatteryBody, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_chargingBatteryNub) lv_obj_set_style_bg_color(ui_chargingBatteryNub, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // ui_chargingBatteryFill's color is data-driven (warn vs accent) - left
    // alone here, same reasoning as the value labels below.
    if (ui_chargingPlugLabel) lv_obj_set_style_text_color(ui_chargingPlugLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_chargingCableLimLabel) lv_obj_set_style_text_color(ui_chargingCableLimLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *bars[] = { ui_chargingSetpointBar, ui_chargingTempBar, ui_chargingAcVBar, ui_chargingLimBar };
    for (int i = 0; i < 4; i++) {
        if (!bars[i]) continue;
        lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bars[i], ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    // Value labels and the status pill's color are data-driven and left
    // alone here - they self-correct on their next natural data update,
    // same reasoning as every other screen in this codebase.
}

void ui_chargingScreen_screen_destroy(void)
{
    if (ui_chargingScreen) lv_obj_del(ui_chargingScreen);

    ui_chargingScreen = NULL;
    ui_chargingSocPanel = NULL;
    ui_chargingBatteryBody = NULL;
    ui_chargingBatteryNub = NULL;
    ui_chargingBatteryFill = NULL;
    ui_chargingSocValLabel = NULL;
    ui_chargingStatusPanel = NULL;
    ui_chargingStatusPill = NULL;
    ui_chargingStatusLabel = NULL;
    ui_chargingPlugLabel = NULL;
    ui_chargingSetpointPanel = NULL;
    ui_chargingSetpointValLabel = NULL;
    ui_chargingSetpointBar = NULL;
    ui_chargingTempPanel = NULL;
    ui_chargingTempValLabel = NULL;
    ui_chargingTempBar = NULL;
    ui_chargingAcVPanel = NULL;
    ui_chargingAcVValLabel = NULL;
    ui_chargingAcVBar = NULL;
    ui_chargingLimPanel = NULL;
    ui_chargingLimValLabel = NULL;
    ui_chargingLimBar = NULL;
    ui_chargingCableLimLabel = NULL;
}
