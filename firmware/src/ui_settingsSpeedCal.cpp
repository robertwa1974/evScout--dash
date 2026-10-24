// ============================================================================
// Settings > Calibration: road-speed calibration rows (2026-10-24)
// ============================================================================
// Axle ratio + tire diameter steppers (apply + persist immediately, like the
// Display page's UTC offset - and reset both ranges' GPS calibration, since
// the old K no longer matches), a live status block, and CALIBRATE FROM GPS.
// The math and the GPS sampling live in speed_cal.h; this file is only UI.
//
// Built into Settings' existing Calibration page/section (above the dyno
// placeholder row), so it shares that page's lifecycle - the 250ms status
// timer only runs while the page is shown.
// ============================================================================

#include "ui_settings_common.h"
#include "speed_cal.h"
#include "zombie_updaters.h"  // myData, dataMutex - live rpm/range readout
#include <stdio.h>
#include <string.h>
#include <math.h>

static const float KPH_TO_MPH = 0.621371f;

static lv_obj_t *calPageRef = NULL;
static lv_obj_t *axleSpin = NULL, *tireSpin = NULL, *xferSpin = NULL;
static lv_obj_t *liveLabel = NULL, *rangesLabel = NULL, *calStatusLabel = NULL;
static lv_obj_t *calBtn = NULL, *calBtnLabel = NULL;
static lv_timer_t *timer = NULL;
static int repeatCount = 0;

static void setLabel(lv_obj_t *l, const char *t) {
    if (l && strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static void updateStatus(void) {
    int rpm = 0, gearFb = 1, dir = 1, kph = 0;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        rpm = myData.motorRpm;
        gearFb = myData.gearFb;
        dir = myData.dirState;
        kph = myData.vehSpeedKph;
        xSemaphoreGive(dataMutex);
    }
    int range = speed_cal_range_for(gearFb, dir);
    char buf[200];
    snprintf(buf, sizeof(buf), "Now: %s range (GearFB %d) - motor %d rpm = %.0f mph%s",
             range == SPEED_RANGE_LOW ? "LOW" : "HIGH", gearFb, rpm, kph * KPH_TO_MPH,
             speed_cal_is_calibrated(range) ? "" : "  (estimated)");
    setLabel(liveLabel, buf);

    char lo[64], hi[64];
    for (int r = 0; r < 2; r++) {
        char *dst = r == 0 ? lo : hi;
        if (speed_cal_is_calibrated(r)) snprintf(dst, 64, "GPS-calibrated, measured %.2f:1", speed_cal_implied_reduction(r));
        else snprintf(dst, 64, "estimated from %.2f:1 (ORNL)", speed_cal_nominal_reduction(r));
    }
    snprintf(buf, sizeof(buf), "LOW: %s\nHIGH: %s", lo, hi);
    setLabel(rangesLabel, buf);

    setLabel(calStatusLabel, speed_cal_status_text());
    bool running = speed_cal_state() == SPEED_CAL_RUNNING;
    setLabel(calBtnLabel, running ? "CANCEL" : "CALIBRATE FROM GPS");
    lv_obj_set_style_bg_color(calBtn, running ? ui_theme_warning() : ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(calBtn, running ? ui_theme_text_primary() : ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    updateStatus();
}

static void applySteppers(void) {
    float axle = lv_spinbox_get_value(axleSpin) / 100.0f;
    float tire = lv_spinbox_get_value(tireSpin) / 10.0f;
    float xfer = lv_spinbox_get_value(xferSpin) / 100.0f;
    if (fabsf(axle - speed_cal_axle_ratio()) > 0.001f) speed_cal_set_axle_ratio(axle);
    if (fabsf(tire - speed_cal_tire_inches()) > 0.01f) speed_cal_set_tire_inches(tire);
    if (fabsf(xfer - speed_cal_transfer_ratio()) > 0.001f) speed_cal_set_transfer_ratio(xfer);
    updateStatus();
}

static void stepperCb(lv_event_t *e, int dir) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *spin = (lv_obj_t *)lv_event_get_user_data(e);
    if (code == LV_EVENT_PRESSED) {
        repeatCount = 0;
        return;
    } else if (code == LV_EVENT_CLICKED) {
        _ui_spinbox_step(spin, dir);
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        repeatCount++;
        int step = (repeatCount > 15) ? 5 : (repeatCount > 6 ? 2 : 1);  // same acceleration as the other steppers
        for (int i = 0; i < step; i++) _ui_spinbox_step(spin, dir);
    } else {
        return;
    }
    applySteppers();
}
static void minusCb(lv_event_t *e) { stepperCb(e, -1); }
static void plusCb(lv_event_t *e)  { stepperCb(e, 1); }

static void wire(lv_obj_t *spin) {
    lv_obj_t *cont = lv_obj_get_parent(spin);
    lv_obj_add_event_cb(lv_obj_get_child(cont, 1), minusCb, LV_EVENT_ALL, spin);
    lv_obj_add_event_cb(lv_obj_get_child(cont, 3), plusCb, LV_EVENT_ALL, spin);
}

static void calBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (speed_cal_state() == SPEED_CAL_RUNNING) speed_cal_cancel();
    else speed_cal_start();
    updateStatus();
}

static lv_obj_t *infoLabel(lv_obj_t *section, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *row = lv_menu_cont_create(section);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(l, "");
    return l;
}

static void onDelete(lv_event_t *e) {
    (void)e;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    calPageRef = axleSpin = tireSpin = xferSpin = liveLabel = rangesLabel = calStatusLabel = calBtn = calBtnLabel = NULL;
}

void ui_settingsSpeedCal_build(lv_obj_t *page, lv_obj_t *section) {
    calPageRef = page;

    lv_obj_t *head = infoLabel(section, &font_montserrat_extrabold_16, ui_theme_text_primary());
    lv_label_set_text(head, "Road speed (from motor rpm)");
    lv_obj_t *hint = infoLabel(section, &font_montserrat_semibold_16, ui_theme_text_secondary());
    lv_label_set_text(hint, "The VCU doesn't report road speed, so the dash computes it from motor rpm and "
                            "the LS600h range ratios (LOW 3.90, HIGH ~1.9). GPS calibration per range makes "
                            "it exact. Changing axle, tire or transfer ratio resets calibration.");

    axleSpin = ui_settings_create_stepper_row(section, "Axle ratio", 100, 900,
                                              (int32_t)lroundf(speed_cal_axle_ratio() * 100), 3, 1);
    tireSpin = ui_settings_create_stepper_row(section, "Tire diameter (in)", 200, 450,
                                              (int32_t)lroundf(speed_cal_tire_inches() * 10), 3, 2);
    // Transfer case reduction (2026-10-24): 1.00 for the LS600h (centre diff,
    // no reduction) - a setting so other drivetrains can use this too.
    xferSpin = ui_settings_create_stepper_row(section, "Transfer case ratio", 50, 500,
                                              (int32_t)lroundf(speed_cal_transfer_ratio() * 100), 3, 1);
    wire(axleSpin);
    wire(tireSpin);
    wire(xferSpin);

    liveLabel = infoLabel(section, &font_montserrat_semibold_16, ui_theme_text_primary());
    rangesLabel = infoLabel(section, &font_montserrat_semibold_16, ui_theme_text_primary());

    lv_obj_t *btnRow = lv_menu_cont_create(section);
    lv_obj_set_height(btnRow, ROW_HEIGHT);
    calBtn = lv_btn_create(btnRow);
    lv_obj_set_size(calBtn, 300, STEPPER_BTN);
    lv_obj_set_style_radius(calBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    calBtnLabel = lv_label_create(calBtn);
    lv_label_set_text(calBtnLabel, "CALIBRATE FROM GPS");
    lv_obj_center(calBtnLabel);
    lv_obj_add_event_cb(calBtn, calBtnCb, LV_EVENT_CLICKED, NULL);
    calStatusLabel = lv_label_create(btnRow);
    lv_label_set_long_mode(calStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(calStatusLabel, 1);
    lv_obj_set_style_text_font(calStatusLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(calStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(calStatusLabel, "");

    updateStatus();
    lv_obj_add_event_cb(calBtn, onDelete, LV_EVENT_DELETE, NULL);
}

void ui_settingsSpeedCal_page_leave(lv_obj_t *page) {
    if (page != NULL && page == calPageRef) return;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    // A running calibration keeps going - it's driven by speed_cal_poll()
    // from loop(), not this page's timer.
}

void ui_settingsSpeedCal_page_enter(lv_obj_t *page) {
    if (page == NULL || page != calPageRef) return;
    updateStatus();
    if (!timer) timer = lv_timer_create(timerCb, 250, NULL);
}

void ui_settingsSpeedCal_refresh_theme(void) {
    if (!calPageRef) return;
    lv_obj_t *spins[3] = {axleSpin, tireSpin, xferSpin};
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(spins[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(spins[i], ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_set_style_text_color(liveLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(rangesLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(calStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    updateStatus();
}
