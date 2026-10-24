// ============================================================================
// ui_speedScreen.c - HOME screen, twin-binnacle instrument cluster
// ============================================================================
// Rebuilt 2026-10-21 (was a single centered 300px speedometer + P/N/F/R
// pill). Layout concept from mianahm3d/webUI-STM_ESPCAR's "Mehran EV"
// cluster - Rob liked it; only the arrangement was taken, not code (theirs
// runs on sin() simulation with 10-14px fonts and 30px tap targets). Every
// readout here is real VCU data or odometer.h.
//
//   x 4..404            x 416..796
//   +--------------+    [ P ][ R ][ N ][ D ]      y 4..76    shifter strip
//   |  ODOMETER    |    +--------+ +--------+
//   |  123.4 mi    |    |  SOC   | |INVERTER|     y 84..254  needle gauges
//   |     42       |    |  (/)   | |  (/)   |
//   |    mph       |    +--------+ +--------+
//   |  TRIP        |
//   |  [ 4.6 mi ]  |    [ POWER ] [ PACK V ]      y 262..331 cards
//   +--------------+    [ PACK A ] [ 12V   ]      y 339..408
//   ===================== dock (72px) ===================== y 408..480
//
// Widget rules (CLAUDE.md): speedometer and the two gauges are lv_meter
// (needle + ticks), never sliders; nothing here is an input except the
// trip box (long-press to reset, 170x80 - at the 80px touch floor). The
// shifter strip is display-only - the VCU owns gear state.
//
// Speed/dir are bound in zombie_updaters.cpp's fastUpdate, power/current in
// midUpdate, the rest in slowUpdate - same dataMutex-then-uiMutex pattern
// as every other screen, via the setters declared in ui_speedScreen.h.
//
// Navigation (unchanged): physical swipe LEFT -> Drive. This board reports
// gesture direction inverted (CLAUDE.md "Touch gesture direction"), so the
// code checks LV_DIR_LEFT. Intentional; don't "fix" it.
// ============================================================================

#include "ui.h"
#include "odometer.h"
#include "ui_can_freshness.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define USABLE_H   (480 - UI_DOCK_H)   // 408
#define DIAL_SIZE  400
#define COL_X      416
#define COL_W      380
#define GAP        8

#define STRIP_Y    4
#define STRIP_H    72
#define GAUGE_Y    (STRIP_Y + STRIP_H + GAP)        // 84
#define GAUGE_H    170
#define CARD_Y     (GAUGE_Y + GAUGE_H + GAP)        // 262
#define CARD_H     ((USABLE_H - CARD_Y - GAP) / 2)  // 69
#define HALF_W     ((COL_W - GAP) / 2)              // 186

#define SOC_MAX    100
// Inverter (heatsink) temp, not motor temp (Rob, 2026-10-22): the inverter
// usually derates first and its sensor always reports, while tmpm depends on
// a separately wired motor thermistor (read -27.7C open on the bench).
// Motor temp stays on the Status screen; the fault banner warns on both.
#define TEMP_MAX   120

lv_obj_t * ui_speedScreen = NULL;
lv_obj_t * ui_speedMeter = NULL;
lv_meter_indicator_t * ui_speedNeedle = NULL;
lv_obj_t * ui_speedValLabel = NULL;
lv_obj_t * ui_speedUnitLabel = NULL;

static lv_obj_t * ui_speedScreenDock = NULL;

// See the old file's note (kept): lv_meter has no style setter for tick
// color, so refresh_theme() re-invokes the tick setters with new colors.
static lv_meter_scale_t *speedScale = NULL;

static lv_obj_t *odoCaption = NULL, *odoBox = NULL, *odoLabel = NULL;
static lv_obj_t *tripBox = NULL, *tripCaption = NULL, *tripLabel = NULL;
// Invisible long-press target over the TRIP caption + box (2026-10-21): the
// visible box is only 40px tall to match the odometer's, below CLAUDE.md's
// 80px touch floor, so the hit area is this separate 180x80 screen-level
// object instead. Screen child, not a meter child, so nothing in the
// meter's hierarchy can swallow the press.
static lv_obj_t *tripHit = NULL;
static lv_timer_t *tripFlashTimer = NULL;  // shows "RESET" for a moment after a reset

#define READOUT_BOX_W 180   // odometer + trip boxes, identical
#define READOUT_BOX_H 40
static long shownOdoTenths = -1, shownTripTenths = -1;

// P R N D strip - index order on screen, with the dirState each represents.
static const char *GEAR_TEXT[4] = {"P", "R", "N", "D"};
static const int GEAR_DIR[4] = {2, -1, 0, 1};
static lv_obj_t *gearBox[4], *gearLabel[4];
static int activeDir = 0;

typedef struct {
    lv_obj_t *panel, *title, *meter, *value;
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *needle, *zone;
} gauge_t;
static gauge_t socGauge, tempGauge;

typedef struct { lv_obj_t *panel, *title, *value; } card_t;
static card_t powerCard, rangeCard, packACard, auxCard;
// Last values drawn for the trip-computer readouts - redraw only on change.
static long shownRange = -2, shownTripWhmi = -2;   // -1 = "--", -2 = never drawn
static int rangeSoc = -1;
static float rangeBattKWh = 0;

// --- Small builders ---------------------------------------------------------

static lv_obj_t *createPanel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(p, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(p, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(p, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    return p;
}

static lv_obj_t *createLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(l, text);
    return l;
}

static void styleGaugeScale(gauge_t *g, int32_t max) {
    lv_meter_set_scale_ticks(g->meter, g->scale, 11, 2, 8, ui_theme_text_secondary());
    lv_meter_set_scale_range(g->meter, g->scale, 0, max, 240, 150);
}

static void createGauge(gauge_t *g, lv_coord_t x, const char *title, int32_t max, int32_t zoneStart, int32_t zoneEnd) {
    g->panel = createPanel(ui_speedScreen, x, GAUGE_Y, HALF_W, GAUGE_H);
    g->title = createLabel(g->panel, &font_montserrat_semibold_16, ui_theme_text_secondary(), title);
    lv_obj_set_pos(g->title, 10, 6);

    g->meter = lv_meter_create(g->panel);
    lv_obj_set_size(g->meter, 140, 140);
    lv_obj_align(g->meter, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_clear_flag(g->meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(g->meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g->meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g->meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    g->scale = lv_meter_add_scale(g->meter);
    styleGaugeScale(g, max);
    // Warning zone arc (low SOC / hot motor) - endpoints follow the Settings
    // thresholds via ui_speedScreen_setThresholds().
    g->zone = lv_meter_add_arc(g->meter, g->scale, 5, ui_theme_warning(), 0);
    lv_meter_set_indicator_start_value(g->meter, g->zone, zoneStart);
    lv_meter_set_indicator_end_value(g->meter, g->zone, zoneEnd);
    g->needle = lv_meter_add_needle_line(g->meter, g->scale, 4, ui_theme_accent(), -10);
    lv_meter_set_indicator_value(g->meter, g->needle, 0);

    // Below the pivot, inside the 240-degree sweep's open bottom.
    g->value = createLabel(g->meter, &font_montserrat_extrabold_24, ui_theme_dim(), "--");
    lv_obj_align(g->value, LV_ALIGN_CENTER, 0, 40);
}

static void createCard(card_t *c, lv_coord_t x, lv_coord_t y, const char *title) {
    c->panel = createPanel(ui_speedScreen, x, y, HALF_W, CARD_H);
    c->title = createLabel(c->panel, &font_montserrat_semibold_16, ui_theme_text_secondary(), title);
    lv_obj_set_pos(c->title, 10, 6);
    c->value = createLabel(c->panel, &font_montserrat_extrabold_24, ui_theme_dim(), "--");
    lv_obj_align(c->value, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
}

static void setCardValue(card_t *c, const char *text, lv_color_t color) {
    if (!c->value) return;
    lv_label_set_text(c->value, text);
    lv_obj_set_style_text_color(c->value, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// --- Events -----------------------------------------------------------------

void ui_event_speedScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_driveScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_driveScreen_screen_init);
        _ui_screen_delete(&ui_speedScreen);
    }
    // Brightness gesture (CLAUDE.md: keep uaDASH's swipe-up/down brightness).
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        upBrightness(e);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) {
        downBribrightness(e);
    }
}

static void tripFlashDoneCb(lv_timer_t *t) {
    LV_UNUSED(t);
    tripFlashTimer = NULL;  // one-shot (repeat count 1) - LVGL deletes it after this call
    if (!tripLabel) return;
    lv_obj_set_style_text_color(tripLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    shownTripTenths = -1;   // force the real value back on
    ui_speedScreen_refreshOdometer();
}

// Long-press, not tap: a reset shouldn't happen from a brush while driving.
// Pressed state outlines the box in the accent color; a completed reset
// flashes "RESET" for a second - visible even when the trip was already
// 0.0 (Rob's bench test found the old version gave no sign it had worked).
static void tripHitCb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_border_color(tripBox, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(tripBox, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_border_color(tripBox, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(tripBox, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        odometer_reset_trip();
        lv_label_set_text(tripLabel, "RESET");
        lv_obj_set_style_text_color(tripLabel, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (tripFlashTimer) lv_timer_del(tripFlashTimer);
        tripFlashTimer = lv_timer_create(tripFlashDoneCb, 1000, NULL);
        lv_timer_set_repeat_count(tripFlashTimer, 1);
    }
}

static lv_obj_t *createReadoutBox(lv_coord_t yFromCenter) {
    lv_obj_t *box = lv_obj_create(ui_speedMeter);
    lv_obj_set_size(box, READOUT_BOX_W, READOUT_BOX_H);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, yFromCenter);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(box, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(box, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(box, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    return box;
}

// --- Screen -----------------------------------------------------------------

void ui_speedScreen_screen_init(void)
{
    ui_speedScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_speedScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_speedScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_speedScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Left: speedometer -------------------------------------------------
    ui_speedMeter = lv_meter_create(ui_speedScreen);
    lv_obj_set_size(ui_speedMeter, DIAL_SIZE, DIAL_SIZE);
    lv_obj_set_pos(ui_speedMeter, 4, (USABLE_H - DIAL_SIZE) / 2);
    lv_obj_clear_flag(ui_speedMeter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ui_speedMeter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_speedMeter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_speedMeter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 0-120 mph (confirmed with Rob 2026-09-26), majors every 20, minors
    // every 5 - unchanged from the previous screen.
    speedScale = lv_meter_add_scale(ui_speedMeter);
    lv_meter_set_scale_range(ui_speedMeter, speedScale, 0, 120, 270, 135);
    lv_meter_set_scale_ticks(ui_speedMeter, speedScale, 25, 2, 12, ui_theme_text_primary());
    lv_meter_set_scale_major_ticks(ui_speedMeter, speedScale, 4, 3, 18, ui_theme_text_primary(), 16);
    // Tick numbers render via LV_PART_TICKS, not MAIN - see git history of
    // this file for the "no reference numbers" bug this fixed.
    lv_obj_set_style_text_font(ui_speedMeter, &font_montserrat_semibold_16, LV_PART_TICKS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_speedMeter, ui_theme_text_primary(), LV_PART_TICKS | LV_STATE_DEFAULT);
    ui_speedNeedle = lv_meter_add_needle_line(ui_speedMeter, speedScale, 5, ui_theme_accent(), -14);
    lv_meter_set_indicator_value(ui_speedMeter, ui_speedNeedle, 0);

    odoCaption = createLabel(ui_speedMeter, &font_montserrat_semibold_16, ui_theme_text_secondary(), "ODOMETER");
    lv_obj_align(odoCaption, LV_ALIGN_CENTER, 0, -112);
    odoBox = createReadoutBox(-78);
    odoLabel = createLabel(odoBox, &font_montserrat_extrabold_24, ui_theme_text_primary(), "0.0 mi");
    lv_obj_center(odoLabel);

    ui_speedValLabel = createLabel(ui_speedMeter, &font_montserrat_extrabold_48, ui_theme_text_primary(), "0");
    lv_obj_align(ui_speedValLabel, LV_ALIGN_CENTER, 0, 4);
    ui_speedUnitLabel = createLabel(ui_speedMeter, &font_montserrat_semibold_16, ui_theme_text_secondary(), "mph");
    lv_obj_align(ui_speedUnitLabel, LV_ALIGN_CENTER, 0, 48);

    // TRIP mirrors ODOMETER exactly (Rob, 2026-10-21): caption above, box
    // around the value only - same 34px caption-to-box spacing.
    tripCaption = createLabel(ui_speedMeter, &font_montserrat_semibold_16, ui_theme_text_secondary(), "TRIP");
    lv_obj_align(tripCaption, LV_ALIGN_CENTER, 0, 92);
    tripBox = createReadoutBox(126);
    tripLabel = createLabel(tripBox, &font_montserrat_extrabold_24, ui_theme_text_primary(), "0.0 mi");
    lv_obj_center(tripLabel);

    // Covers caption + box: meter center is at (4 + DIAL_SIZE/2, meterY +
    // DIAL_SIZE/2); caption top ~+84, box bottom +146 -> an 80px-tall area
    // from +76. Created after the meter so it sits on top of it.
    tripHit = lv_obj_create(ui_speedScreen);
    lv_obj_remove_style_all(tripHit);
    lv_obj_set_size(tripHit, READOUT_BOX_W, 80);
    lv_obj_set_pos(tripHit, 4 + DIAL_SIZE / 2 - READOUT_BOX_W / 2, (USABLE_H - DIAL_SIZE) / 2 + DIAL_SIZE / 2 + 76);
    lv_obj_add_flag(tripHit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tripHit, tripHitCb, LV_EVENT_ALL, NULL);

    // --- Right: P R N D strip ------------------------------------------------
    lv_coord_t gearW = (COL_W - 3 * GAP) / 4;
    for (int i = 0; i < 4; i++) {
        gearBox[i] = createPanel(ui_speedScreen, COL_X + i * (gearW + GAP), STRIP_Y, gearW, STRIP_H);
        gearLabel[i] = createLabel(gearBox[i], &font_montserrat_extrabold_32, ui_theme_dim(), GEAR_TEXT[i]);
        lv_obj_center(gearLabel[i]);
    }

    // --- Right: SOC + motor temp gauges ------------------------------------
    createGauge(&socGauge, COL_X, "SOC  %", SOC_MAX, 0, 15);
    createGauge(&tempGauge, COL_X + HALF_W + GAP, "INVERTER  \xC2\xB0" "C", TEMP_MAX, 85, TEMP_MAX);

    // --- Right: cards ---------------------------------------------------------
    createCard(&powerCard, COL_X, CARD_Y, "POWER");
    // RANGE replaced PACK V (2026-10-24) - pack voltage is still on the
    // Status and Battery screens; range is the more useful glance here.
    createCard(&rangeCard, COL_X + HALF_W + GAP, CARD_Y, "RANGE");
    createCard(&packACard, COL_X, CARD_Y + CARD_H + GAP, "PACK A");
    createCard(&auxCard, COL_X + HALF_W + GAP, CARD_Y + CARD_H + GAP, "12V AUX");

    shownOdoTenths = shownTripTenths = -1;
    ui_speedScreen_refreshOdometer();
    ui_speedScreen_setDirState(0);

    ui_speedScreenDock = ui_dock_create(ui_speedScreen, UI_DOCK_TELEMETRY);

    lv_obj_add_event_cb(ui_speedScreen, ui_event_speedScreen, LV_EVENT_ALL, NULL);
}

// --- Setters ----------------------------------------------------------------

static void styleGearBox(int i) {
    bool active = (GEAR_DIR[i] == activeDir);
    lv_color_t bg = ui_theme_panel_bg(), fg = ui_theme_dim(), border = ui_theme_panel_border();
    if (active) {
        // Same semantics as the old P/N/F/R pill (ui_status_pill.h): F green,
        // R amber caution, P/N neutral - neutral shown as an accent outline
        // so the active letter still stands out.
        if (activeDir == 1)       { bg = ui_theme_good(); fg = ui_theme_bg(); border = bg; }
        else if (activeDir == -1) { bg = ui_theme_bad();  fg = ui_theme_bg(); border = bg; }
        else                      { fg = ui_theme_text_primary(); border = ui_theme_accent(); }
    }
    lv_obj_set_style_bg_color(gearBox[i], bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(gearBox[i], border, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(gearBox[i], active ? 3 : 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(gearLabel[i], fg, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_speedScreen_setDirState(int dirState)
{
    if (ui_speedScreen == NULL) return;
    activeDir = (dirState >= -1 && dirState <= 2) ? dirState : 0;
    for (int i = 0; i < 4; i++) styleGearBox(i);
}

static void setGauge(gauge_t *g, int32_t needle, const char *text, lv_color_t color) {
    if (!g->meter) return;
    lv_meter_set_indicator_value(g->meter, g->needle, needle);
    lv_label_set_text(g->value, text);
    lv_obj_set_style_text_color(g->value, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_speedScreen_setSoc(int soc, bool warn, bool noData)
{
    if (ui_speedScreen == NULL) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d%%", soc);
    int32_t n = soc < 0 ? 0 : (soc > SOC_MAX ? SOC_MAX : soc);
    setGauge(&socGauge, n, buf, noData ? ui_theme_dim() : (warn ? ui_theme_warning() : ui_theme_text_primary()));
}

void ui_speedScreen_setInverterTemp(int tempC, bool warn)
{
    if (ui_speedScreen == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0" "C", tempC);
    int32_t n = tempC < 0 ? 0 : (tempC > TEMP_MAX ? TEMP_MAX : tempC);
    bool noData = !ui_can_freshness_hasEverReceived();
    setGauge(&tempGauge, n, buf, noData ? ui_theme_dim() : (warn ? ui_theme_warning() : ui_theme_text_primary()));
}

void ui_speedScreen_setThresholds(int lowSoc, int inverterTempWarn)
{
    if (ui_speedScreen == NULL) return;
    lv_meter_set_indicator_end_value(socGauge.meter, socGauge.zone, lowSoc);
    lv_meter_set_indicator_start_value(tempGauge.meter, tempGauge.zone, inverterTempWarn);
}

void ui_speedScreen_setSpeedCalibrated(bool calibrated)
{
    if (ui_speedScreen == NULL || ui_speedUnitLabel == NULL) return;
    // Quiet note, not a warning (2026-10-24): with the ORNL range ratios the
    // estimate is within a few % - calibration only refines it.
    const char *t = calibrated ? "mph" : "mph - estimated";
    if (strcmp(lv_label_get_text(ui_speedUnitLabel), t) == 0) return;
    lv_label_set_text(ui_speedUnitLabel, t);
}

void ui_speedScreen_setRangeInputs(int soc, float battKWh)
{
    rangeSoc = soc;
    rangeBattKWh = battKWh;
}

static lv_color_t liveColor(void) {
    return ui_can_freshness_hasEverReceived() ? ui_theme_text_primary() : ui_theme_dim();
}

void ui_speedScreen_setPackCurrent(float amps)
{
    if (ui_speedScreen == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f A", amps);
    setCardValue(&packACard, buf, liveColor());
}

void ui_speedScreen_setPower(float kw)
{
    if (ui_speedScreen == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f kW", kw);
    setCardValue(&powerCard, buf, liveColor());
}

void ui_speedScreen_setAuxVoltage(float volts)
{
    if (ui_speedScreen == NULL) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f V", volts);
    setCardValue(&auxCard, buf, liveColor());
}

void ui_speedScreen_refreshOdometer(void)
{
    if (ui_speedScreen == NULL) return;
    long odo = (long)(odometer_total_miles() * 10.0);
    long trip = (long)(odometer_trip_miles() * 10.0);
    char buf[16];
    if (odo != shownOdoTenths) {
        shownOdoTenths = odo;
        // Same format as TRIP - no leading zeros (Rob, 2026-10-21: the
        // zero-padded "000000.0" was too many digits). Wraps at 1,000,000.
        snprintf(buf, sizeof(buf), "%ld.%ld mi", (odo / 10) % 1000000L, odo % 10);
        lv_label_set_text(odoLabel, buf);
    }
    if (trip != shownTripTenths && !tripFlashTimer) {
        shownTripTenths = trip;
        snprintf(buf, sizeof(buf), "%ld.%ld mi", (trip / 10) % 10000L, trip % 10);
        lv_label_set_text(tripLabel, buf);
    }

    // Trip efficiency in the caption ("TRIP  312 Wh/mi"), plain "TRIP"
    // until the trip has enough measured distance (odometer.h).
    double whmi = odometer_trip_wh_per_mile();
    long whmiShown = isnan(whmi) ? -1 : lround(whmi);
    if (whmiShown != shownTripWhmi) {
        shownTripWhmi = whmiShown;
        if (whmiShown < 0) lv_label_set_text(tripCaption, "TRIP");
        else lv_label_set_text_fmt(tripCaption, "TRIP  %ld Wh/mi", whmiShown);
    }

    // Range: SOC x BattCap / recent Wh/mi - "--" until there's enough data.
    bool live = ui_can_freshness_hasEverReceived();
    double range = live ? odometer_range_miles(rangeSoc, rangeBattKWh) : NAN;
    long rangeShown = isnan(range) ? -1 : lround(range);
    if (rangeShown != shownRange) {
        shownRange = rangeShown;
        if (rangeShown < 0) {
            setCardValue(&rangeCard, "--", ui_theme_dim());
        } else {
            snprintf(buf, sizeof(buf), "%ld mi", rangeShown);
            setCardValue(&rangeCard, buf, ui_theme_text_primary());
        }
    }
}

// --- Theme ------------------------------------------------------------------

static void restylePanel(lv_obj_t *p) {
    if (!p) return;
    lv_obj_set_style_bg_color(p, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(p, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void restyleGauge(gauge_t *g, int32_t max) {
    restylePanel(g->panel);
    lv_obj_set_style_text_color(g->title, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    styleGaugeScale(g, max);
    g->needle->type_data.needle_line.color = ui_theme_accent();
    g->zone->type_data.arc.color = ui_theme_warning();
    lv_obj_invalidate(g->meter);
}

void ui_speedScreen_refresh_theme(void)
{
    if (ui_speedScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_speedScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_speedUnitLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(odoCaption, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(tripCaption, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(odoLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(tripLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    restylePanel(odoBox);
    restylePanel(tripBox);

    lv_meter_set_scale_ticks(ui_speedMeter, speedScale, 25, 2, 12, ui_theme_text_primary());
    lv_meter_set_scale_major_ticks(ui_speedMeter, speedScale, 4, 3, 18, ui_theme_text_primary(), 16);
    lv_obj_set_style_text_color(ui_speedMeter, ui_theme_text_primary(), LV_PART_TICKS | LV_STATE_DEFAULT);
    ui_speedNeedle->type_data.needle_line.color = ui_theme_accent();
    lv_obj_invalidate(ui_speedMeter);

    for (int i = 0; i < 4; i++) styleGearBox(i);
    restyleGauge(&socGauge, SOC_MAX);
    restyleGauge(&tempGauge, TEMP_MAX);

    card_t *cards[4] = {&powerCard, &rangeCard, &packACard, &auxCard};
    for (int i = 0; i < 4; i++) {
        restylePanel(cards[i]->panel);
        lv_obj_set_style_text_color(cards[i]->title, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    ui_dock_refresh_theme(ui_speedScreenDock, UI_DOCK_TELEMETRY);
    // Value labels (speed, gauges, cards) are data-driven and recolor on
    // their next update, same as every other screen.
}

void ui_speedScreen_screen_destroy(void)
{
    if (ui_speedScreen) lv_obj_del(ui_speedScreen);

    ui_speedScreen = NULL;
    ui_speedMeter = NULL;
    ui_speedNeedle = NULL;   // owned by the meter
    speedScale = NULL;
    ui_speedValLabel = NULL;
    ui_speedUnitLabel = NULL;
    odoCaption = odoBox = odoLabel = NULL;
    tripBox = tripCaption = tripLabel = tripHit = NULL;
    if (tripFlashTimer) lv_timer_del(tripFlashTimer);
    tripFlashTimer = NULL;
    shownOdoTenths = shownTripTenths = -1;
    for (int i = 0; i < 4; i++) gearBox[i] = gearLabel[i] = NULL;
    lv_memset_00(&socGauge, sizeof(socGauge));
    lv_memset_00(&tempGauge, sizeof(tempGauge));
    lv_memset_00(&powerCard, sizeof(powerCard));
    lv_memset_00(&rangeCard, sizeof(rangeCard));
    shownRange = shownTripWhmi = -2;
    lv_memset_00(&packACard, sizeof(packACard));
    lv_memset_00(&auxCard, sizeof(auxCard));
    activeDir = 0;
    ui_speedScreenDock = NULL;
}
