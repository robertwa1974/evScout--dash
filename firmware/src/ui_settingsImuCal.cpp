// ============================================================================
// Settings > Calibration: IMU level rows (2026-10-28)
// ============================================================================
// Status, a live a_long/a_lat/a_vert readout (for checking the result),
// Invert forward, and CALIBRATE with a progress bar. Staged like the Warning
// Thresholds page: a passed run, an Invert toggle or CLEAR applies at once
// (so the live readout previews it) but only persists on SAVE; CANCEL
// reverts. The math, the sampling and the NVS live in imu_driver.h; this
// file is only UI.
//
// Built into its own section on Settings' Calibration page, sharing that
// page's lifecycle - the 100ms readout timer only runs while it's shown. A
// running calibration keeps going if the page is left (the IMU task drives
// it, not this timer).
// ============================================================================

#include "ui_settings_common.h"
#include "imu_driver.h"
#include "zombie_updaters.h"  // dataMutex, imuData raw readout when uncalibrated
#include <stdio.h>
#include <string.h>

static lv_obj_t *calPageRef = NULL;
static lv_obj_t *statusLabel = NULL, *liveLabel = NULL, *runLabel = NULL;
static lv_obj_t *invertSwitch = NULL, *progressBar = NULL;
static lv_obj_t *calBtn = NULL, *calBtnLabel = NULL;
static lv_obj_t *saveBtn = NULL, *saveBtnLabel = NULL;
static lv_obj_t *cancelBtn = NULL, *cancelBtnLabel = NULL;
static lv_obj_t *clearBtn = NULL, *clearBtnLabel = NULL;
static lv_timer_t *timer = NULL;

static void setLabel(lv_obj_t *l, const char *t) {
    if (l && strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

// Accent when actionable, panel-border grey + disabled otherwise - same
// look as the Warning Thresholds Save button's dirty state.
static void setBtnEnabled(lv_obj_t *btn, lv_obj_t *label, bool enabled, lv_color_t activeColor) {
    if (enabled) {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, activeColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void updateStatus(void) {
    if (!calPageRef) return;
    char buf[160];
    bool present = imu_present();
    bool cal = imu_is_calibrated();
    bool dirty = imu_cal_dirty();
    imu_cal_state_t st = imu_cal_state();

    if (!present) snprintf(buf, sizeof(buf), "IMU not detected");
    else if (cal) snprintf(buf, sizeof(buf), "Calibrated, mount tilt %.1f\xC2\xB0%s", imu_cal_tilt_deg(), dirty ? "  (unsaved)" : "");
    else snprintf(buf, sizeof(buf), "Not calibrated%s", dirty ? "  (unsaved)" : "");
    setLabel(statusLabel, buf);

    ImuVehicleAccel v;
    if (imu_get_vehicle_accel(&v)) {
        snprintf(buf, sizeof(buf), "a_long %+.2f g    a_lat %+.2f g    a_vert %+.2f g", v.aLongG, v.aLatG, v.aVertG);
    } else if (present) {
        float x = 0, y = 0, z = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            x = imuData.axG; y = imuData.ayG; z = imuData.azG;
            xSemaphoreGive(dataMutex);
        }
        snprintf(buf, sizeof(buf), "Uncalibrated - raw chip axes x %+.2f  y %+.2f  z %+.2f g", x, y, z);
    } else {
        snprintf(buf, sizeof(buf), "No data");
    }
    setLabel(liveLabel, buf);

    imu_cal_status_text(buf, sizeof(buf));
    setLabel(runLabel, buf);

    bool running = st == IMU_CAL_COLLECTING;
    lv_bar_set_value(progressBar, running ? (int32_t)(imu_cal_progress() * 100) : (st == IMU_CAL_PASSED ? 100 : 0), LV_ANIM_OFF);
    setLabel(calBtnLabel, running ? "STOP" : "CALIBRATE");
    setBtnEnabled(calBtn, calBtnLabel, present, running ? ui_theme_warning() : ui_theme_accent());
    if (running) lv_obj_set_style_text_color(calBtnLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    setBtnEnabled(saveBtn, saveBtnLabel, dirty && !running, ui_theme_accent());
    setBtnEnabled(cancelBtn, cancelBtnLabel, dirty || st == IMU_CAL_FAILED, ui_theme_accent());
    setBtnEnabled(clearBtn, clearBtnLabel, cal && !running, ui_theme_warning());
    if (cal && !running) lv_obj_set_style_text_color(clearBtnLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // Reflect Cancel/Clear reverting the toggle.
    bool inv = imu_cal_invert();
    if (inv != lv_obj_has_state(invertSwitch, LV_STATE_CHECKED)) {
        if (inv) lv_obj_add_state(invertSwitch, LV_STATE_CHECKED);
        else lv_obj_clear_state(invertSwitch, LV_STATE_CHECKED);
    }
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    updateStatus();
}

static void calBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (imu_cal_state() == IMU_CAL_COLLECTING) imu_cal_cancel();
    else imu_calibrate_start();
    updateStatus();
}

static void saveBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !imu_cal_dirty()) return;
    imu_cal_save();
    updateStatus();
    ui_settings_show_saved("IMU level calibration saved.");
}

static void cancelBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    imu_cal_cancel();
    updateStatus();
}

static void clearBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    imu_cal_clear();
    updateStatus();
}

static void invertCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    imu_cal_set_invert(lv_obj_has_state(invertSwitch, LV_STATE_CHECKED));
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

static lv_obj_t *button(lv_obj_t *row, const char *text, int width, lv_event_cb_t cb, lv_obj_t **labelOut) {
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_size(btn, width, STEPPER_BTN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    *labelOut = l;
    return btn;
}

static void onDelete(lv_event_t *e) {
    (void)e;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    calPageRef = statusLabel = liveLabel = runLabel = invertSwitch = progressBar = NULL;
    calBtn = calBtnLabel = saveBtn = saveBtnLabel = cancelBtn = cancelBtnLabel = clearBtn = clearBtnLabel = NULL;
}

void ui_settingsImuCal_build(lv_obj_t *page, lv_obj_t *section) {
    calPageRef = page;

    lv_obj_t *head = infoLabel(section, &font_montserrat_extrabold_16, ui_theme_text_primary());
    lv_label_set_text(head, "IMU level (0-60 / dyno, Incline screen)");
    lv_obj_t *hint = infoLabel(section, &font_montserrat_semibold_16, ui_theme_text_secondary());
    lv_label_set_text(hint, "Measures how the dash-mounted IMU sits, so dash lean doesn't leak gravity into "
                            "the fore-aft reading. Park on level ground, motor off, then CALIBRATE and don't "
                            "touch the truck for 3 s. If gentle acceleration reads negative a_long, flip "
                            "Invert forward - no need to recalibrate. This also sets the Incline screen's zero, "
                            "so a sloped spot here shows up as a constant offset there.");

    statusLabel = infoLabel(section, &font_montserrat_extrabold_16, ui_theme_text_primary());
    liveLabel = infoLabel(section, &font_montserrat_semibold_16, ui_theme_text_primary());

    lv_obj_t *invRow = lv_menu_cont_create(section);
    lv_obj_set_height(invRow, ROW_HEIGHT);
    lv_obj_t *invLabel = lv_label_create(invRow);
    lv_label_set_text(invLabel, "Invert forward");
    lv_obj_set_style_text_color(invLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(invLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(invLabel, 1);
    invertSwitch = lv_switch_create(invRow);
    lv_obj_set_size(invertSwitch, 70, 40);  // same as Settings' other switches
    if (imu_cal_invert()) lv_obj_add_state(invertSwitch, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(invertSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(invertSwitch, invertCb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *calRow = lv_menu_cont_create(section);
    lv_obj_set_height(calRow, ROW_HEIGHT);
    calBtn = button(calRow, "CALIBRATE", 220, calBtnCb, &calBtnLabel);
    progressBar = lv_bar_create(calRow);  // display-only progress - lv_bar, never lv_slider
    lv_obj_set_height(progressBar, 16);
    lv_obj_set_flex_grow(progressBar, 1);
    lv_bar_set_range(progressBar, 0, 100);
    lv_obj_set_style_bg_color(progressBar, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(progressBar, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    runLabel = infoLabel(section, &font_montserrat_semibold_16, ui_theme_text_primary());

    lv_obj_t *actRow = lv_menu_cont_create(section);
    lv_obj_set_height(actRow, ROW_HEIGHT);
    saveBtn = button(actRow, "SAVE", 160, saveBtnCb, &saveBtnLabel);
    cancelBtn = button(actRow, "CANCEL", 160, cancelBtnCb, &cancelBtnLabel);
    clearBtn = button(actRow, "CLEAR", 160, clearBtnCb, &clearBtnLabel);

    updateStatus();
    lv_obj_add_event_cb(calBtn, onDelete, LV_EVENT_DELETE, NULL);
}

void ui_settingsImuCal_page_leave(lv_obj_t *page) {
    if (page != NULL && page == calPageRef) return;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
}

void ui_settingsImuCal_page_enter(lv_obj_t *page) {
    if (page == NULL || page != calPageRef) return;
    updateStatus();
    if (!timer) timer = lv_timer_create(timerCb, 100, NULL);
}

void ui_settingsImuCal_refresh_theme(void) {
    if (!calPageRef) return;
    lv_obj_set_style_text_color(statusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(liveLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(runLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(invertSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(progressBar, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(progressBar, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    updateStatus();
}
