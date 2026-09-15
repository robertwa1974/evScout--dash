// ============================================================================
// ui_statusScreen.c - static/at-rest telemetry, 2x3 grid
// ============================================================================
// Hand-written (2026-09-14), part of the Speed/Drive/Status/Battery split
// (see waveshare-dash-build.md). No SquareLine project (same situation as
// every other hand-written screen in this repo).
//
// 2x3 grid of six equal 388x149 panels, same geometry as ui_driveScreen.c:
//   (0,0) 8,8     SOC             - %, ring/arc gauge (CLAUDE.md: ring
//                                    readouts -> lv_arc), scaled to fit the
//                                    149px panel height
//   (1,0) 404,8   Pack Voltage    - V, unsigned - lv_bar strip
//   (0,1) 8,165   Aux/12V Voltage - V, unsigned - lv_bar strip
//   (1,1) 404,165 Motor Temp      - degC, signed - lv_bar strip (SYMMETRICAL)
//   (0,2) 8,322   Inverter Temp   - degC, unsigned (heatsink temp,
//                                    myData.heatsinkTemp) - lv_bar strip
//   (1,2) 404,322 Max Battery Temp- degC, signed (myData.cellTMax via
//                                    PARAM_ID_BMS_TMAX) - lv_bar strip
//
// Widget choice follows CLAUDE.md: lv_arc for the SOC ring, lv_bar for
// every linear-strip value - never lv_slider for read-only telemetry.
//
// Bound to myData in zombie_updaters.cpp's fastUpdate/midUpdate/slowUpdate
// (same dirty-check, dataMutex-then-uiMutex pattern as every other binding).
//
// Navigation, following the approved topology (Settings <-> Speed(home) <->
// Drive <-> Status <-> Battery <-> Dyno LIVE -> Dyno RESULTS): physical
// swipe LEFT -> Drive (back), physical swipe RIGHT -> Battery (forward).
// This board reports gesture direction inverted from the physical swipe
// (see CLAUDE.md's "Touch gesture direction") - the code checks
// LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe. Intentional; don't "fix" it without re-verifying on
// hardware first.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_statusScreen = NULL;

lv_obj_t * ui_statusSocPanel = NULL;
lv_obj_t * ui_statusSocTitleLabel = NULL;
lv_obj_t * ui_statusSocArc = NULL;
lv_obj_t * ui_statusSocValLabel = NULL;

lv_obj_t * ui_statusPackVPanel = NULL;
lv_obj_t * ui_statusPackVTitleLabel = NULL;
lv_obj_t * ui_statusPackVValLabel = NULL;
lv_obj_t * ui_statusPackVUnitLabel = NULL;
lv_obj_t * ui_statusPackVBar = NULL;

lv_obj_t * ui_statusAuxVPanel = NULL;
lv_obj_t * ui_statusAuxVTitleLabel = NULL;
lv_obj_t * ui_statusAuxVValLabel = NULL;
lv_obj_t * ui_statusAuxVUnitLabel = NULL;
lv_obj_t * ui_statusAuxVBar = NULL;

lv_obj_t * ui_statusMotorTPanel = NULL;
lv_obj_t * ui_statusMotorTTitleLabel = NULL;
lv_obj_t * ui_statusMotorTValLabel = NULL;
lv_obj_t * ui_statusMotorTUnitLabel = NULL;
lv_obj_t * ui_statusMotorTBar = NULL;

lv_obj_t * ui_statusInvTPanel = NULL;
lv_obj_t * ui_statusInvTTitleLabel = NULL;
lv_obj_t * ui_statusInvTValLabel = NULL;
lv_obj_t * ui_statusInvTUnitLabel = NULL;
lv_obj_t * ui_statusInvTBar = NULL;

lv_obj_t * ui_statusMaxBattTPanel = NULL;
lv_obj_t * ui_statusMaxBattTTitleLabel = NULL;
lv_obj_t * ui_statusMaxBattTValLabel = NULL;
lv_obj_t * ui_statusMaxBattTUnitLabel = NULL;
lv_obj_t * ui_statusMaxBattTBar = NULL;

#define GRID_PANEL_W 388
#define GRID_PANEL_H 149
#define GRID_GAP     8

void ui_event_statusScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_driveScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_driveScreen_screen_init);
        _ui_screen_delete(&ui_statusScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_batteryScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_batteryScreen_screen_init);
        _ui_screen_delete(&ui_statusScreen);
    }
}

// Same createPanel/createValueAndUnit/createBar pattern as ui_driveScreen.c
// - kept file-local/duplicated rather than shared, matching this codebase's
// existing convention (ui_mainScreen.c/ui_bmsScreen.c each had their own
// copies too).
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
    *outVal = val;

    lv_obj_t *unit = lv_label_create(panel);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 28);
    lv_label_set_text(unit, unitText);
    lv_obj_set_style_text_color(unit, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(unit, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    *outUnit = unit;
}

// Applies a gradient to the bar's TRACK (LV_PART_MAIN - the fixed-size
// background, NOT the indicator, which grows/shrinks with the value).
// Design-review "gradient/zoned warning bars" pass, 2026-09-14 (see
// waveshare-dash-build.md). The indicator draws a solid, opaque color on
// top of the track from 0 up to the current value, so the only part of
// this gradient that stays visible is the UNFILLED remainder - in effect,
// "how much headroom is left before the danger end," colored by how close
// you are to it: a lot of muted green headroom when safe, a thin red
// sliver when close to the limit.
//
// zoneDir: +1 = danger at the HIGH end (green-left/red-right, e.g.
// over-temp), -1 = danger at the LOW end (red-left/green-right, e.g.
// under-voltage), 0 = no zone (plain flat track - most bars, see below).
//
// Deliberately only wired to bars with a REAL warningSet threshold behind
// them (Inverter Temp, Pack Voltage) - most bars in this codebase have no
// defined danger zone (no threshold exists for AC Volts, Regen Limit, cell
// voltages, etc.), and decorating them with a gradient anyway would imply
// a threshold that doesn't exist. Also not applied to the Motor Temp bar
// despite it having a real threshold - it's LV_BAR_MODE_SYMMETRICAL (fills
// outward from a non-zero center point, not from one fixed edge), and this
// codebase can't currently verify on hardware whether the resulting
// two-sided reveal reads sensibly - deferred rather than shipped unverified.
//
// Colors are ui_theme_good()/ui_theme_warning() blended 35% into
// ui_theme_bg() (lv_color_mix, ~90/255) rather than used at full
// saturation - a full-strength red/green gradient behind a thin 14px bar
// risked looking like a neon strip rather than a subtle zone hint, and
// that balance can't be confirmed without seeing it on the actual screen.
static void setZoneGradient(lv_obj_t *bar, int zoneDir) {
    if (zoneDir == 0) return;  // leave the flat ui_theme_bg() track set below
    lv_color_t bg = ui_theme_bg();
    lv_color_t safe   = lv_color_mix(ui_theme_good(), bg, 90);
    lv_color_t danger = lv_color_mix(ui_theme_warning(), bg, 90);
    lv_obj_set_style_bg_color(bar, zoneDir > 0 ? safe : danger, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bar, zoneDir > 0 ? danger : safe, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *createBar(lv_obj_t *panel, bool symmetrical, int32_t rangeMin, int32_t rangeMax, int zoneDir) {
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
    setZoneGradient(bar, zoneDir);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return bar;
}

void ui_statusScreen_screen_init(void)
{
    ui_statusScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_statusScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_statusScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_statusScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- SOC: ring/arc gauge, scaled to fit the 149px panel ---
    ui_statusSocPanel = createPanel(ui_statusScreen, GRID_GAP, GRID_GAP, "SOC", &ui_statusSocTitleLabel);

    ui_statusSocArc = lv_arc_create(ui_statusSocPanel);
    lv_obj_set_size(ui_statusSocArc, 120, 120);
    lv_obj_align(ui_statusSocArc, LV_ALIGN_CENTER, 0, 10);
    lv_arc_set_bg_angles(ui_statusSocArc, 135, 45);
    lv_arc_set_range(ui_statusSocArc, 0, 100);
    lv_arc_set_value(ui_statusSocArc, 0);
    lv_obj_remove_style(ui_statusSocArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ui_statusSocArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ui_statusSocArc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_statusSocArc, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_statusSocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_statusSocArc, 12, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_statusSocArc, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_statusSocValLabel = lv_label_create(ui_statusSocPanel);
    // Digits only, no "%" - same arc-containment reasoning as every other
    // SOC label in this codebase (120px arc, 12px stroke -> ~96px inner
    // diameter; the 24px tier, not 32/48, is what fits here).
    lv_obj_align(ui_statusSocValLabel, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(ui_statusSocValLabel, "0");
    lv_obj_set_style_text_color(ui_statusSocValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_statusSocValLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // TODO: bar ranges below are placeholder defaults - tune to your actual
    // pack/motor/inverter ratings once known.
    ui_statusPackVPanel = createPanel(ui_statusScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, "PACK VOLTAGE", &ui_statusPackVTitleLabel);
    createValueAndUnit(ui_statusPackVPanel, &ui_statusPackVValLabel, &ui_statusPackVUnitLabel, "V");
    // zoneDir=-1: danger is LOW voltage (warningSet.packVLow) - red-left/
    // green-right track gradient.
    ui_statusPackVBar = createBar(ui_statusPackVPanel, false, 0, 450, -1);

    ui_statusAuxVPanel = createPanel(ui_statusScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, "AUX 12V", &ui_statusAuxVTitleLabel);
    createValueAndUnit(ui_statusAuxVPanel, &ui_statusAuxVValLabel, &ui_statusAuxVUnitLabel, "V");
    ui_statusAuxVBar = createBar(ui_statusAuxVPanel, false, 8, 16, 0);  // no defined threshold - flat track

    ui_statusMotorTPanel = createPanel(ui_statusScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, ICON_THERMOSTAT " MOTOR TEMP", &ui_statusMotorTTitleLabel);
    createValueAndUnit(ui_statusMotorTPanel, &ui_statusMotorTValLabel, &ui_statusMotorTUnitLabel, "\xC2\xB0" "C");
    // Has a real threshold (warningSet.motorTemp) but SYMMETRICAL mode
    // (fills outward from a non-zero center) makes the "revealed remainder"
    // effect harder to reason about correctly without seeing it on
    // hardware - deferred, see setZoneGradient()'s comment. Flat track.
    ui_statusMotorTBar = createBar(ui_statusMotorTPanel, true, -40, 160, 0);

    // "INV TEMP" not "INVERTER TEMP" - label-shortening pass, 2026-09-14
    // (see waveshare-dash-build.md).
    ui_statusInvTPanel = createPanel(ui_statusScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_THERMOSTAT " INV TEMP", &ui_statusInvTTitleLabel);
    createValueAndUnit(ui_statusInvTPanel, &ui_statusInvTValLabel, &ui_statusInvTUnitLabel, "\xC2\xB0" "C");
    // zoneDir=+1: danger is HIGH temp (warningSet.heatsinkTemp) - green-
    // left/red-right track gradient.
    ui_statusInvTBar = createBar(ui_statusInvTPanel, false, 0, 120, 1);

    ui_statusMaxBattTPanel = createPanel(ui_statusScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_THERMOSTAT " MAX BATT TEMP", &ui_statusMaxBattTTitleLabel);
    createValueAndUnit(ui_statusMaxBattTPanel, &ui_statusMaxBattTValLabel, &ui_statusMaxBattTUnitLabel, "\xC2\xB0" "C");
    ui_statusMaxBattTBar = createBar(ui_statusMaxBattTPanel, true, -20, 80, 0);  // no defined threshold - flat track

    lv_obj_add_event_cb(ui_statusScreen, ui_event_statusScreen, LV_EVENT_ALL, NULL);
}

void ui_statusScreen_refresh_theme(void)
{
    if (ui_statusScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_statusScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *panels[] = { ui_statusSocPanel, ui_statusPackVPanel, ui_statusAuxVPanel, ui_statusMotorTPanel, ui_statusInvTPanel, ui_statusMaxBattTPanel };
    lv_obj_t *titles[] = { ui_statusSocTitleLabel, ui_statusPackVTitleLabel, ui_statusAuxVTitleLabel, ui_statusMotorTTitleLabel, ui_statusInvTTitleLabel, ui_statusMaxBattTTitleLabel };
    lv_obj_t *units[]  = { NULL, ui_statusPackVUnitLabel, ui_statusAuxVUnitLabel, ui_statusMotorTUnitLabel, ui_statusInvTUnitLabel, ui_statusMaxBattTUnitLabel };
    for (int i = 0; i < 6; i++) {
        if (panels[i]) {
            lv_obj_set_style_bg_color(panels[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(panels[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (titles[i]) lv_obj_set_style_text_color(titles[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (units[i])  lv_obj_set_style_text_color(units[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_statusSocArc) {
        lv_obj_set_style_arc_color(ui_statusSocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(ui_statusSocArc, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    // zoneDir mirrors the values passed to createBar() at creation time -
    // see setZoneGradient()'s comment for why only these two bars get one.
    lv_obj_t *bars[]    = { ui_statusPackVBar, ui_statusAuxVBar, ui_statusMotorTBar, ui_statusInvTBar, ui_statusMaxBattTBar };
    int       zoneDirs[] = {                -1,               0,                 0,               1,                   0 };
    for (int i = 0; i < 5; i++) {
        if (!bars[i]) continue;
        if (zoneDirs[i] != 0) {
            setZoneGradient(bars[i], zoneDirs[i]);
        } else {
            lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bars[i], ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    // Value labels are left alone here - they self-correct on their next
    // natural data update, same reasoning as every other screen.
}

void ui_statusScreen_screen_destroy(void)
{
    if (ui_statusScreen) lv_obj_del(ui_statusScreen);

    ui_statusScreen = NULL;
    ui_statusSocPanel = NULL;
    ui_statusSocTitleLabel = NULL;
    ui_statusSocArc = NULL;
    ui_statusSocValLabel = NULL;
    ui_statusPackVPanel = NULL;
    ui_statusPackVTitleLabel = NULL;
    ui_statusPackVValLabel = NULL;
    ui_statusPackVUnitLabel = NULL;
    ui_statusPackVBar = NULL;
    ui_statusAuxVPanel = NULL;
    ui_statusAuxVTitleLabel = NULL;
    ui_statusAuxVValLabel = NULL;
    ui_statusAuxVUnitLabel = NULL;
    ui_statusAuxVBar = NULL;
    ui_statusMotorTPanel = NULL;
    ui_statusMotorTTitleLabel = NULL;
    ui_statusMotorTValLabel = NULL;
    ui_statusMotorTUnitLabel = NULL;
    ui_statusMotorTBar = NULL;
    ui_statusInvTPanel = NULL;
    ui_statusInvTTitleLabel = NULL;
    ui_statusInvTValLabel = NULL;
    ui_statusInvTUnitLabel = NULL;
    ui_statusInvTBar = NULL;
    ui_statusMaxBattTPanel = NULL;
    ui_statusMaxBattTTitleLabel = NULL;
    ui_statusMaxBattTValLabel = NULL;
    ui_statusMaxBattTUnitLabel = NULL;
    ui_statusMaxBattTBar = NULL;
}
