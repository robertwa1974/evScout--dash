// ============================================================================
// ui_batteryScreen.c - cell-level BMS detail (renamed from ui_bmsScreen.c)
// ============================================================================
// Layout (800x480), hand-written, no SquareLine project (same situation as
// every other hand-written screen in this repo).
//
// Styling pass rebuild (2026-09-18, item 8): replaced the previous bespoke
// arc+pill+4-stacked-bar-row layout with the same ui_card_grid.h 2x3 grid
// pattern used on Drive/Status/GPS/Charging, for layout CONSISTENCY across
// the app (styling pass item 6) - this was the one screen still using a
// bespoke full-width layout instead of the shared card grid.
//   (0,0) 8,8     SOC             - %, ring/arc gauge, same treatment as
//                                    Status screen's SOC cell
//   (1,0) 404,8   Status          - pill: CHARGING/DISCHARGING/IDLE, from
//                                    sign of packCurrent
//   (0,1) 8,165   Max Cell V      - V, BMS_Vmax
//   (1,1) 404,165 Min Cell V      - V, BMS_Vmin
//   (0,2) 8,322   Cell Delta V    - V, computed (Vmax-Vmin), not a
//                                    separate SDO param
//   (1,2) 404,322 Max Cell Temp   - degC, BMS_Tmax
//
// Per-cell voltage bars for EVERY individual cell (JKBMS's actual headline
// feature) are still not built - the VCU only relays pack-level min/max/
// delta, not a full per-cell array (see firmware/can-bench.md). If a
// cell-level BMS data source with a full per-cell array gets added later,
// see the placeholder comment below for where that would go.
//
// None of this screen's bars (cell V max/min/delta, cell temp max) have a
// real warningSet threshold behind them (zombie_updaters.h's warning_set
// only defines lowSoc/motorTemp/heatsinkTemp/packVLow) - all pass
// zoneDir=0 (flat track), same bar gradient/flat audit as every other
// screen (styling pass item 5).
//
// Bound to myData in zombie_updaters.cpp, same dirty-check + dataMutex-then-
// uiMutex pattern as every other screen.
//
// Navigation: BOTH swipe directions REMOVED (styling/UX pass Phase 5,
// 2026-09-18) - Battery/BMS is its own dock group with no sibling screens
// (unlike Telemetry's Speed/Drive/Status/Charging), so every link off this
// screen was cross-group and is now dock-only (see ui_dock.h's mapping
// comment and the plan's swipe-removal table). This is the one screen in
// the whole app with no swipe navigation left at all - reachable only via
// the dock's BMS icon.
// ============================================================================

#include "ui.h"

lv_obj_t * ui_batteryScreen = NULL;

lv_obj_t * ui_batterySocPanel = NULL;
lv_obj_t * ui_batterySocTitleLabel = NULL;
lv_obj_t * ui_batterySocArc = NULL;
lv_obj_t * ui_batterySocValLabel = NULL;
lv_obj_t * ui_batterySocIconLabel = NULL;

lv_obj_t * ui_batteryStatusPanel = NULL;
lv_obj_t * ui_batteryStatusPill = NULL;
lv_obj_t * ui_batteryStatusLabel = NULL;
// Tracks the pill's current semantic state for refresh_theme() - see
// ui_gpsScreen.c's identical pattern/comment for why.
static ui_pill_state_t ui_batteryStatusPillState = UI_PILL_NEUTRAL;

lv_obj_t * ui_batteryVMaxLabel = NULL;
lv_obj_t * ui_batteryVMaxBar = NULL;
lv_obj_t * ui_batteryVMaxValLabel = NULL;
lv_obj_t * ui_batteryVMaxUnitLabel = NULL;

lv_obj_t * ui_batteryVMinLabel = NULL;
lv_obj_t * ui_batteryVMinBar = NULL;
lv_obj_t * ui_batteryVMinValLabel = NULL;
lv_obj_t * ui_batteryVMinUnitLabel = NULL;

lv_obj_t * ui_batteryDeltaVLabel = NULL;
lv_obj_t * ui_batteryDeltaVBar = NULL;
lv_obj_t * ui_batteryDeltaVValLabel = NULL;
lv_obj_t * ui_batteryDeltaVUnitLabel = NULL;

lv_obj_t * ui_batteryTMaxLabel = NULL;
lv_obj_t * ui_batteryTMaxBar = NULL;
lv_obj_t * ui_batteryTMaxValLabel = NULL;
lv_obj_t * ui_batteryTMaxUnitLabel = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Battery is its own BMS dock group (no sibling screens).
static lv_obj_t * ui_batteryScreenDock = NULL;

// TODO (full per-cell BMS data source, if one gets added): a scrollable
// list of N per-cell voltage bars would go here, one row each, JKBMS-style
// - a dynamically-built list of ui_card_createBar()-style rows instead of
// the 4 fixed cards above, sized to however many cells the pack reports.

// Empty on purpose (styling/UX pass Phase 5, 2026-09-18) - see this file's
// header comment: both swipe directions this screen used to have were
// cross-group and are now dock-only. Kept registered (not removed) so this
// screen still matches every other screen's "has its own ui_event_*
// handler" shape, in case a future non-navigation gesture (e.g. brightness)
// ever needs one here.
void ui_event_batteryScreen(lv_event_t * e)
{
    (void)e;
}

void ui_batteryScreen_screen_init(void)
{
    ui_batteryScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_batteryScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_batteryScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_batteryScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- SOC: ring/arc gauge, same treatment as ui_statusScreen.c's SOC
    // cell (96px arc, 10px stroke -> ~76px inner diameter, 24px digits) -
    // shrunk from 120x120 for the shorter 124px panel, see ui_card_grid.h's
    // GRID_PANEL_H comment. Offset -6 (not 0, unlike Status's SOC cell)
    // leaves room below the arc for this screen's own battery-level icon
    // glyph, which Status's plainer SOC cell doesn't have. ---
    ui_batterySocPanel = ui_card_createPanel(ui_batteryScreen, GRID_GAP, GRID_GAP, "SOC", &ui_batterySocTitleLabel);

    ui_batterySocArc = lv_arc_create(ui_batterySocPanel);
    lv_obj_set_size(ui_batterySocArc, 96, 96);
    lv_obj_align(ui_batterySocArc, LV_ALIGN_CENTER, 0, -6);
    lv_arc_set_bg_angles(ui_batterySocArc, 135, 45);
    lv_arc_set_range(ui_batterySocArc, 0, 100);
    lv_arc_set_value(ui_batterySocArc, 0);
    lv_obj_remove_style(ui_batterySocArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ui_batterySocArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ui_batterySocArc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_batterySocArc, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_batterySocArc, 10, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_batterySocValLabel = lv_label_create(ui_batterySocPanel);
    // Digits only, no "%" - same arc-containment reasoning as every other
    // SOC label in this codebase.
    lv_obj_align(ui_batterySocValLabel, LV_ALIGN_CENTER, 0, -6);
    lv_label_set_text(ui_batterySocValLabel, "0");
    lv_obj_set_style_text_color(ui_batterySocValLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_batterySocValLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_batterySocIconLabel = lv_label_create(ui_batterySocPanel);
    lv_obj_align(ui_batterySocIconLabel, LV_ALIGN_CENTER, 0, 18);
    lv_label_set_text(ui_batterySocIconLabel, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(ui_batterySocIconLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_batterySocIconLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Status pill ---
    ui_batteryStatusPanel = ui_card_createPanel(ui_batteryScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP, "STATUS", NULL);

    // Styling pass: migrated onto ui_status_pill.h's shared pill (was a
    // hand-built lv_obj + its own copy-pasted 400ms fade transition).
    ui_batteryStatusPill = ui_pill_create(ui_batteryStatusPanel, 240, 44, 22, &ui_batteryStatusLabel, &font_montserrat_semibold_24);
    lv_obj_align(ui_batteryStatusPill, LV_ALIGN_CENTER, 0, 0);
    ui_pill_setState(ui_batteryStatusPill, ui_batteryStatusLabel, UI_PILL_NEUTRAL, "IDLE");

    // --- 4 cell-level cards ---
    // TODO: cell voltage range (2.5-4.3V) and cell temp range are Li-ion
    // placeholder defaults - tune to your actual cell chemistry once known.
    // "MAX/MIN CELL V" not "...VOLTAGE" - found on hardware 2026-09-14
    // after the ExtraBold font pass (kept from the pre-rebuild version,
    // still applies with the card grid's own title width budget).
    lv_obj_t *vMaxPanel = ui_card_createPanel(ui_batteryScreen, GRID_GAP, GRID_GAP * 2 + GRID_PANEL_H, "MAX CELL V", &ui_batteryVMaxLabel);
    ui_card_createValueAndUnit(vMaxPanel, &ui_batteryVMaxValLabel, &ui_batteryVMaxUnitLabel, "V");
    // Cell voltage bars carry centivolts (value*100) for a bit of
    // resolution out of an integer lv_bar range - see zombie_updaters.cpp.
    ui_batteryVMaxBar = ui_card_createBar(vMaxPanel, false, 250, 430, 0);

    lv_obj_t *vMinPanel = ui_card_createPanel(ui_batteryScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 2 + GRID_PANEL_H, "MIN CELL V", &ui_batteryVMinLabel);
    ui_card_createValueAndUnit(vMinPanel, &ui_batteryVMinValLabel, &ui_batteryVMinUnitLabel, "V");
    ui_batteryVMinBar = ui_card_createBar(vMinPanel, false, 250, 430, 0);

    lv_obj_t *deltaVPanel = ui_card_createPanel(ui_batteryScreen, GRID_GAP, GRID_GAP * 3 + GRID_PANEL_H * 2, "CELL DELTA V", &ui_batteryDeltaVLabel);
    ui_card_createValueAndUnit(deltaVPanel, &ui_batteryDeltaVValLabel, &ui_batteryDeltaVUnitLabel, "V");
    ui_batteryDeltaVBar = ui_card_createBar(deltaVPanel, false, 0, 50, 0);

    lv_obj_t *tMaxPanel = ui_card_createPanel(ui_batteryScreen, GRID_GAP * 2 + GRID_PANEL_W, GRID_GAP * 3 + GRID_PANEL_H * 2, "MAX CELL TEMP", &ui_batteryTMaxLabel);
    ui_card_createValueAndUnit(tMaxPanel, &ui_batteryTMaxValLabel, &ui_batteryTMaxUnitLabel, "\xC2\xB0" "C");
    ui_batteryTMaxBar = ui_card_createBar(tMaxPanel, true, -20, 80, 0);

    ui_batteryScreenDock = ui_dock_create(ui_batteryScreen, UI_DOCK_BMS);

    lv_obj_add_event_cb(ui_batteryScreen, ui_event_batteryScreen, LV_EVENT_ALL, NULL);
}

void ui_batteryScreen_setStatus(float packCurrent) {
    if (ui_batteryStatusPill == NULL) return;

    const float deadband = 0.5f;  // avoid flicker right around 0A
    const char *text;
    if (packCurrent < -deadband) {
        text = "CHARGING";
        ui_batteryStatusPillState = UI_PILL_ACTIVE;
    } else if (packCurrent > deadband) {
        text = "DISCHARGING";
        ui_batteryStatusPillState = UI_PILL_CAUTION;
    } else {
        text = "IDLE";
        ui_batteryStatusPillState = UI_PILL_NEUTRAL;
    }
    ui_pill_setState(ui_batteryStatusPill, ui_batteryStatusLabel, ui_batteryStatusPillState, text);
}

void ui_batteryScreen_refresh_theme(void)
{
    if (ui_batteryScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_batteryScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *panels[] = { ui_batterySocPanel, ui_batteryStatusPanel };
    lv_obj_t *titles[] = { ui_batterySocTitleLabel, NULL };
    for (int i = 0; i < 2; i++) {
        if (panels[i]) {
            lv_obj_set_style_bg_color(panels[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(panels[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (titles[i]) lv_obj_set_style_text_color(titles[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_batterySocIconLabel) lv_obj_set_style_text_color(ui_batterySocIconLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(ui_batterySocArc, ui_theme_accent_energy(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    ui_pill_refreshTheme(ui_batteryStatusPill, ui_batteryStatusPillState);

    lv_obj_t *cardPanels[] = { lv_obj_get_parent(ui_batteryVMaxBar), lv_obj_get_parent(ui_batteryVMinBar), lv_obj_get_parent(ui_batteryDeltaVBar), lv_obj_get_parent(ui_batteryTMaxBar) };
    lv_obj_t *titles2[] = { ui_batteryVMaxLabel, ui_batteryVMinLabel, ui_batteryDeltaVLabel, ui_batteryTMaxLabel };
    lv_obj_t *units[]   = { ui_batteryVMaxUnitLabel, ui_batteryVMinUnitLabel, ui_batteryDeltaVUnitLabel, ui_batteryTMaxUnitLabel };
    lv_obj_t *bars[]    = { ui_batteryVMaxBar, ui_batteryVMinBar, ui_batteryDeltaVBar, ui_batteryTMaxBar };
    for (int i = 0; i < 4; i++) {
        if (cardPanels[i]) {
            lv_obj_set_style_bg_color(cardPanels[i], ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(cardPanels[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (titles2[i]) lv_obj_set_style_text_color(titles2[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (units[i])   lv_obj_set_style_text_color(units[i], ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (bars[i]) {
            // zoneDir=0 for all four (no real threshold behind any of
            // these - see this file's header comment), so refresh is just
            // the flat track + accent indicator, same as ui_card_setBarNoData's
            // "false" branch without the no-data override.
            lv_obj_set_style_bg_color(bars[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(bars[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(bars[i], ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
    }
    ui_dock_refresh_theme(ui_batteryScreenDock, UI_DOCK_BMS);
    // Value labels are data-driven and left alone here - they self-correct
    // on their next natural data update, same reasoning as every other
    // screen in this codebase.
}

void ui_batteryScreen_screen_destroy(void)
{
    if (ui_batteryScreen) lv_obj_del(ui_batteryScreen);

    ui_batteryScreen = NULL;
    ui_batterySocPanel = NULL;
    ui_batterySocTitleLabel = NULL;
    ui_batterySocArc = NULL;
    ui_batterySocValLabel = NULL;
    ui_batterySocIconLabel = NULL;
    ui_batteryStatusPanel = NULL;
    ui_batteryStatusPill = NULL;
    ui_batteryStatusLabel = NULL;
    ui_batteryStatusPillState = UI_PILL_NEUTRAL;
    ui_batteryVMaxLabel = NULL;
    ui_batteryVMaxBar = NULL;
    ui_batteryVMaxValLabel = NULL;
    ui_batteryVMaxUnitLabel = NULL;
    ui_batteryVMinLabel = NULL;
    ui_batteryVMinBar = NULL;
    ui_batteryVMinValLabel = NULL;
    ui_batteryVMinUnitLabel = NULL;
    ui_batteryDeltaVLabel = NULL;
    ui_batteryDeltaVBar = NULL;
    ui_batteryDeltaVValLabel = NULL;
    ui_batteryDeltaVUnitLabel = NULL;
    ui_batteryTMaxLabel = NULL;
    ui_batteryTMaxBar = NULL;
    ui_batteryTMaxValLabel = NULL;
    ui_batteryTMaxUnitLabel = NULL;
    ui_batteryScreenDock = NULL;
}
