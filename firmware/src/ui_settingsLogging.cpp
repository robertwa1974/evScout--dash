// ============================================================================
// Settings > Data Logging (2026-10-21)
// ============================================================================
// On/off switch + 1/2/5 Hz rate for data_logger.h's CSV logging to the SD
// card, plus a live status line (current file, row count, or why it isn't
// running). Both controls apply and persist immediately - same convention
// as the WiFi Config switch, no Save step.
// ============================================================================

#include "ui_settings_common.h"
#include "data_logger.h"
#include "wifi_config_server.h"
#include <string.h>

static lv_obj_t *logPage = NULL;
static lv_obj_t *logSection = NULL;
static lv_obj_t *logSwitch = NULL;
static lv_obj_t *rateBtns[3] = {NULL, NULL, NULL};
static lv_obj_t *statusLabel = NULL;
static lv_obj_t *hintLabel = NULL;
static lv_timer_t *timer = NULL;

static const uint8_t RATES[3] = {1, 2, 5};

static void updateStatus(void) {
    const char *s = data_logger_status_text();
    if (strcmp(lv_label_get_text(statusLabel), s) != 0) lv_label_set_text(statusLabel, s);
    bool on = data_logger_is_enabled();
    if (on != lv_obj_has_state(logSwitch, LV_STATE_CHECKED)) {
        if (on) lv_obj_add_state(logSwitch, LV_STATE_CHECKED);
        else lv_obj_clear_state(logSwitch, LV_STATE_CHECKED);
    }
}

static void updateRateBtns(void) {
    for (int i = 0; i < 3; i++) {
        if (!rateBtns[i]) continue;
        if (RATES[i] == data_logger_get_rate_hz()) {
            lv_obj_add_state(rateBtns[i], LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(rateBtns[i], ui_theme_accent(), LV_PART_MAIN | LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(rateBtns[i], LV_STATE_CHECKED);
        }
    }
}

static void switchCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    if (lv_obj_has_state(logSwitch, LV_STATE_CHECKED)) data_logger_start();  // may fail (no card) - updateStatus() flips the switch back
    else data_logger_stop();
    updateStatus();
}

static void rateBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    data_logger_set_rate_hz((uint8_t)(intptr_t)lv_event_get_user_data(e));
    updateRateBtns();
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    updateStatus();
}

static lv_obj_t *createRow(lv_obj_t *section, const char *title) {
    lv_obj_t *row = lv_menu_cont_create(section);
    lv_obj_set_height(row, ROW_HEIGHT);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(label, 1);
    return row;
}

static void onDelete(lv_event_t *e) {
    (void)e;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    logPage = logSection = logSwitch = statusLabel = hintLabel = NULL;
    for (int i = 0; i < 3; i++) rateBtns[i] = NULL;
}

lv_obj_t *ui_settingsLogging_create_page(lv_obj_t *menu) {
    logPage = lv_menu_page_create(menu, (char *)"Data Logging");
    lv_obj_set_style_pad_all(logPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(logPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    logSection = lv_menu_section_create(logPage);

    lv_obj_t *switchRow = createRow(logSection, "Log to SD card");
    logSwitch = lv_switch_create(switchRow);
    lv_obj_set_size(logSwitch, 70, 40);
    lv_obj_set_style_bg_color(logSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(logSwitch, switchCb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *rateRow = createRow(logSection, "Rate");
    static const char *RATE_TEXT[3] = {"1 Hz", "2 Hz", "5 Hz"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(rateRow);
        lv_obj_set_size(btn, 110, STEPPER_BTN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, RATE_TEXT[i]);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, rateBtnCb, LV_EVENT_CLICKED, (void *)(intptr_t)RATES[i]);
        rateBtns[i] = btn;
    }

    lv_obj_t *statusRow = lv_menu_cont_create(logSection);
    lv_obj_set_height(statusRow, 48);
    statusLabel = lv_label_create(statusRow);
    lv_obj_set_style_text_font(statusLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(statusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(statusLabel, "");

    lv_obj_t *hintRow = lv_menu_cont_create(logSection);
    hintLabel = lv_label_create(hintRow);
    lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(hintLabel, 1);
    lv_obj_set_style_text_font(hintLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hintLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(hintLabel,
        "Left on, each drive gets its own file - it starts once the VCU leaves Off, so "
        "bench boots with the VCU off leave nothing. "
        "To download: Display > WiFi Config on, join " WIFI_CONFIG_SSID ", open 192.168.4.1/logs");

    updateStatus();
    updateRateBtns();
    ui_settings_style_section(logSection);
    lv_obj_add_event_cb(logPage, onDelete, LV_EVENT_DELETE, NULL);
    return logPage;
}

void ui_settingsLogging_page_leave(lv_obj_t *page) {
    if (page != NULL && page == logPage) return;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
}

void ui_settingsLogging_page_enter(lv_obj_t *page) {
    if (page == NULL || page != logPage) return;
    updateStatus();
    if (!timer) timer = lv_timer_create(timerCb, 1000, NULL);
}

void ui_settingsLogging_refresh_theme(void) {
    if (!logPage) return;
    lv_obj_set_style_bg_color(logPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_settings_style_section(logSection);
    lv_obj_set_style_bg_color(logSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(statusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hintLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    updateRateBtns();
}
