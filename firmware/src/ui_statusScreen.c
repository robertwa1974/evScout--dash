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
// Navigation: physical swipe LEFT -> Drive (back). Physical swipe RIGHT
// used to go to Battery - REMOVED (styling/UX pass Phase 5, 2026-09-18),
// that was a cross-group link, Battery/BMS is dock-only now (see
// ui_dock.h's mapping comment and the plan's swipe-removal table) -
// RETARGETED to Charging instead of just deleted: Charging is in this
// screen's own Telemetry dock group, but its ORIGINAL swipe neighbors
// (Battery, GPS) were both other groups, so it had no intra-group link at
// all before this - without this retarget, Charging would have become
// unreachable by swipe from anywhere in its own group (the dock's
// Telemetry icon only lands on Speed, not Charging directly). This link
// makes Speed<->Drive<->Status<->Charging the real intra-Telemetry swipe
// chain, matching the dock's own grouping for the first time. This board
// reports gesture direction inverted from the physical swipe (see
// CLAUDE.md's "Touch gesture direction") - the code checks LV_DIR_RIGHT
// for the physical-LEFT swipe and LV_DIR_LEFT for the physical-RIGHT
// swipe. Intentional; don't "fix" it without re-verifying on hardware
// first.
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

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Status is inside the Telemetry group (home screen: Speed).
static lv_obj_t * ui_statusScreenDock = NULL;

void ui_event_statusScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_driveScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_driveScreen_screen_init);
        _ui_screen_delete(&ui_statusScreen);
    }
    // Retargeted to Charging, was Battery - see this file's header comment
    // (styling/UX pass Phase 5).
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_chargingScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_chargingScreen_screen_init);
        _ui_screen_delete(&ui_statusScreen);
    }
}

// Styling pass (2026-09-18): panel/value/bar construction + the zone-
// gradient logic now live in ui_card_grid.h's shared ui_card_create*()/
// ui_card_setZoneGradient() - this was the reference implementation that
// consolidation was extracted from (see that header's rationale). The
// zoneDir choices at each call site below are unchanged from before the
// consolidation: only Pack Voltage (-1, warningSet.packVLow) and Inverter
// Temp (+1, warningSet.heatsinkTemp) have a real threshold behind them;
// Motor Temp has one too (warningSet.motorTemp) but stays flat pending
// hardware verification of the SYMMETRICAL-mode gradient reveal - still
// unverified, no bench access this pass, so still deferred rather than
// guessed (styling pass item 5).
void ui_statusScreen_screen_init(void)
{
    ui_statusScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_statusScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_statusScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_statusScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- SOC: ring/arc gauge, scaled to fit the 124px panel (shrunk from
    // 96x96, was 120x120 - see ui_card_grid.h's GRID_PANEL_H comment for
    // why the panel itself shrank; re-centered at offset 0 instead of +10
    // so the smaller arc doesn't overflow the shorter panel) ---
    ui_statusSocPanel = ui_card_createPanel(ui_statusScreen, GRID_GAP, GRID_GAP, "SOC", &ui_statusSocTitleLabel);

    ui_statusSocArc = lv_arc_create(ui_statusSocPanel);
    lv_obj_set_size(ui_statusSocArc, 96, 96);
    lv_obj_align(ui_statusSocArc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(ui_statusSocArc, 135, 45);
    lv_arc_set_range(ui_statusSocArc, 0, 100);
    lv_arc_set_value(ui_statusSocArc, 0);
    lv_obj_remove_style(ui_statusSocArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ui_statusSocArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ui_statusSocArc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_statusSocArc, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_statusSocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_statusSocArc, 10, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_statusSocArc, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_statusSocValLabel = lv_label_create(ui_statusSocPanel);
    // Digits only, no "%" - same arc-containment reasoning as every other
    // SOC label in this codebase (96px arc, 10px stroke -> ~76px inner
    // diameter; the 24px tier, not 32/48, is what fits here).
    lv_obj_align(ui_statusSocValLabel, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(ui_statusSocValLabel, "0");
    lv_obj_set_style_text_color(ui_statusSocValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_statusSocValLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // TODO: bar ranges below are placeholder defaults - tune to your actual
    // pack/motor/inverter ratings once known.
    ui_statusPackVPanel = ui_card_createPanel(ui_statusScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, "PACK VOLTAGE", &ui_statusPackVTitleLabel);
    ui_card_createValueAndUnit(ui_statusPackVPanel, &ui_statusPackVValLabel, &ui_statusPackVUnitLabel, "V");
    // zoneDir=-1: danger is LOW voltage (warningSet.packVLow) - red-left/
    // green-right track gradient.
    ui_statusPackVBar = ui_card_createBar(ui_statusPackVPanel, false, 0, 450, -1);

    ui_statusAuxVPanel = ui_card_createPanel(ui_statusScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, "AUX 12V", &ui_statusAuxVTitleLabel);
    ui_card_createValueAndUnit(ui_statusAuxVPanel, &ui_statusAuxVValLabel, &ui_statusAuxVUnitLabel, "V");
    ui_statusAuxVBar = ui_card_createBar(ui_statusAuxVPanel, false, 8, 16, 0);  // no defined threshold - flat track

    ui_statusMotorTPanel = ui_card_createPanel(ui_statusScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, ICON_THERMOSTAT " MOTOR TEMP", &ui_statusMotorTTitleLabel);
    ui_card_createValueAndUnit(ui_statusMotorTPanel, &ui_statusMotorTValLabel, &ui_statusMotorTUnitLabel, "\xC2\xB0" "C");
    // Has a real threshold (warningSet.motorTemp) but SYMMETRICAL mode
    // (fills outward from a non-zero center) makes the "revealed remainder"
    // effect harder to reason about correctly without seeing it on
    // hardware - deferred, see ui_card_grid.h's setZoneGradient comment.
    // Flat track.
    ui_statusMotorTBar = ui_card_createBar(ui_statusMotorTPanel, true, -40, 160, 0);

    // "INV TEMP" not "INVERTER TEMP" - label-shortening pass, 2026-09-14
    // (see waveshare-dash-build.md).
    ui_statusInvTPanel = ui_card_createPanel(ui_statusScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_THERMOSTAT " INV TEMP", &ui_statusInvTTitleLabel);
    ui_card_createValueAndUnit(ui_statusInvTPanel, &ui_statusInvTValLabel, &ui_statusInvTUnitLabel, "\xC2\xB0" "C");
    // zoneDir=+1: danger is HIGH temp (warningSet.heatsinkTemp) - green-
    // left/red-right track gradient.
    ui_statusInvTBar = ui_card_createBar(ui_statusInvTPanel, false, 0, 120, 1);

    ui_statusMaxBattTPanel = ui_card_createPanel(ui_statusScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, ICON_THERMOSTAT " MAX BATT TEMP", &ui_statusMaxBattTTitleLabel);
    ui_card_createValueAndUnit(ui_statusMaxBattTPanel, &ui_statusMaxBattTValLabel, &ui_statusMaxBattTUnitLabel, "\xC2\xB0" "C");
    ui_statusMaxBattTBar = ui_card_createBar(ui_statusMaxBattTPanel, true, -20, 80, 0);  // no defined threshold - flat track

    ui_statusScreenDock = ui_dock_create(ui_statusScreen, UI_DOCK_TELEMETRY);

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
            ui_card_setZoneGradient(bars[i], zoneDirs[i]);
        } else {
            lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bars[i], ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
    ui_dock_refresh_theme(ui_statusScreenDock, UI_DOCK_TELEMETRY);
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
    ui_statusScreenDock = NULL;
}
