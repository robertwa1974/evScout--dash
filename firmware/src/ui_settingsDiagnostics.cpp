// ============================================================================
// Settings > Diagnostics (2026-10-21)
// ============================================================================
// Read-only "spot values" grid: ~40 raw VCU telemetry values, including
// ones no dashboard screen shows (status bits, pot readings, digital inputs,
// INVudc, torque derate, ...) - a bench/debug view, so values are shown raw
// with the VCU's own names, not dressed up like the driving screens.
//
// Polled via sdo_client_watch() only while this page is on screen; params
// that are also in the main poll table refresh for free (sdo_client.h).
// The main telemetry table runs at half rate while this page is open.
//
// One lv_table, 4 columns (name | value | name | value) - same 48KB LVGL
// heap reasoning as ui_settingsVcuParams.cpp. Rows aren't touch targets
// (nothing to tap), so they're compact rather than 80px.
// ============================================================================

#include "ui_settings_common.h"
#include "sdo_client.h"
#include "ui_can_freshness.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    uint16_t    id;
    uint8_t     decimals;
    const char *unit;
} DiagDef;

#define DEG_C "\xC2\xB0" "C"

static const DiagDef DIAG[] = {
    {"opmode",      2002, 0, ""},   {"status",       2005, 0, ""},
    {"lasterr",     2004, 0, ""},   {"version",      2000, 0, ""},
    {"udc",         2006, 1, "V"},  {"udc2",         2007, 1, "V"},
    {"INVudc",      2010, 1, "V"},  {"deltaV",       2009, 1, "V"},
    {"idc",         2012, 1, "A"},  {"power",        2011, 1, "kW"},
    {"KWh",         2013, 2, "kWh"},{"AMPh",         2014, 1, "Ah"},
    {"speed",       2016, 0, "rpm"},{"Veh_Speed",    2017, 0, "kph"},
    {"torque",      2018, 0, ""},   {"potnom",       2023, 1, "%"},
    {"pot",         2019, 0, ""},   {"pot2",         2020, 0, ""},
    {"potbrake",    2021, 0, ""},   {"dir",          2024, 0, ""},
    {"tmpm",        2029, 0, DEG_C},{"tmphs",        2028, 0, DEG_C},
    {"tmpaux",      2030, 0, DEG_C},{"U12V",         2070, 2, "V"},
    {"I12V",        2083, 1, "A"},  {"canio",        2032, 0, ""},
    {"din_start",   2036, 0, ""},   {"din_brake",    2037, 0, ""},
    {"din_forward", 2038, 0, ""},   {"din_reverse",  2039, 0, ""},
    {"din_bms",     2040, 0, ""},   {"handbrk",      2041, 0, ""},
    {"T15Stat",     2045, 0, ""},   {"InvStat",      2046, 0, ""},
    {"BMS_Vmin",    2084, 3, "V"},  {"BMS_Vmax",     2085, 3, "V"},
    {"BMS_Tmin",    2086, 0, DEG_C},{"BMS_Tmax",     2087, 0, DEG_C},
    {"BMS_ChargeLim",2088,0, "A"},  {"TorqDerate",   2102, 0, ""},
    {"CanAct",      2107, 0, ""},   {"BMS_Tavg",     2103, 0, DEG_C},
};
#define DIAG_COUNT (sizeof(DIAG) / sizeof(DIAG[0]))
#define DIAG_ROWS  ((DIAG_COUNT + 1) / 2)

static lv_obj_t *diagPage = NULL;
static lv_obj_t *diagSection = NULL;
static lv_obj_t *headerLabel = NULL;
static lv_obj_t *table = NULL;
static lv_timer_t *timer = NULL;
static bool watching = false;

static float shownValue[DIAG_COUNT];
static int8_t shownState[DIAG_COUNT];

static void styleTable(void) {
    lv_obj_set_style_bg_color(table, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(table, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(table, ui_theme_panel_bg(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(table, ui_theme_panel_border(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(table, ui_theme_text_primary(), LV_PART_ITEMS | LV_STATE_DEFAULT);
}

// Name columns in secondary text, value columns right-aligned.
static void tableDrawCb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->label_dsc) return;
    if (dsc->id % 2 == 0) dsc->label_dsc->color = ui_theme_text_secondary();
    else dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    const char *header = ui_can_freshness_isLive()
        ? "Raw VCU values - refresh every 2-3 s while this page is open"
        : "No CAN data";
    if (strcmp(lv_label_get_text(headerLabel), header) != 0) lv_label_set_text(headerLabel, header);

    char buf[24];
    for (size_t i = 0; i < DIAG_COUNT; i++) {
        float v = 0;
        sdo_value_state_t st = sdo_client_get(DIAG[i].id, &v);
        if ((int8_t)st == shownState[i] && (st != SDO_VALUE_VALID || v == shownValue[i])) continue;
        shownState[i] = (int8_t)st;
        shownValue[i] = v;
        if (st == SDO_VALUE_VALID) snprintf(buf, sizeof(buf), "%.*f %s", DIAG[i].decimals, v, DIAG[i].unit);
        else if (st == SDO_VALUE_UNSUPPORTED) snprintf(buf, sizeof(buf), "n/a");
        else snprintf(buf, sizeof(buf), "--");
        lv_table_set_cell_value(table, i / 2, (i % 2) * 2 + 1, buf);
    }
}

static void onDelete(lv_event_t *e) {
    (void)e;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    if (watching) sdo_client_unwatch();
    watching = false;
    diagPage = diagSection = headerLabel = table = NULL;
}

lv_obj_t *ui_settingsDiagnostics_create_page(lv_obj_t *menu) {
    diagPage = lv_menu_page_create(menu, (char *)"Diagnostics");
    lv_obj_set_style_pad_all(diagPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(diagPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    diagSection = lv_menu_section_create(diagPage);

    lv_obj_t *headerRow = lv_menu_cont_create(diagSection);
    headerLabel = lv_label_create(headerRow);
    lv_obj_set_style_text_font(headerLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(headerLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(headerLabel, "");

    lv_obj_t *tableRow = lv_menu_cont_create(diagSection);
    lv_obj_set_style_pad_all(tableRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    table = lv_table_create(tableRow);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_CLICKABLE);
    lv_table_set_col_cnt(table, 4);
    lv_table_set_row_cnt(table, DIAG_ROWS);
    lv_table_set_col_width(table, 0, 200);
    lv_table_set_col_width(table, 1, 160);
    lv_table_set_col_width(table, 2, 200);
    lv_table_set_col_width(table, 3, 160);
    lv_obj_set_style_text_font(table, &font_montserrat_extrabold_16, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(table, 8, LV_PART_ITEMS | LV_STATE_DEFAULT);
    styleTable();
    for (size_t i = 0; i < DIAG_COUNT; i++) {
        lv_table_set_cell_value(table, i / 2, (i % 2) * 2, DIAG[i].name);
        lv_table_set_cell_value(table, i / 2, (i % 2) * 2 + 1, "--");
        shownState[i] = -1;
    }
    lv_obj_add_event_cb(table, tableDrawCb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    ui_settings_style_section(diagSection);
    lv_obj_add_event_cb(diagPage, onDelete, LV_EVENT_DELETE, NULL);
    return diagPage;
}

void ui_settingsDiagnostics_page_leave(lv_obj_t *page) {
    if (page != NULL && page == diagPage) return;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    if (watching) {
        sdo_client_unwatch();
        watching = false;
    }
}

void ui_settingsDiagnostics_page_enter(lv_obj_t *page) {
    if (page == NULL || page != diagPage) return;
    if (!watching) {
        uint16_t ids[DIAG_COUNT];
        for (size_t i = 0; i < DIAG_COUNT; i++) ids[i] = DIAG[i].id;
        sdo_client_watch(ids, DIAG_COUNT, 1000);
        watching = true;
        for (size_t i = 0; i < DIAG_COUNT; i++) shownState[i] = -1;
    }
    if (!timer) timer = lv_timer_create(timerCb, 500, NULL);
}

void ui_settingsDiagnostics_refresh_theme(void) {
    if (!diagPage) return;
    lv_obj_set_style_bg_color(diagPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_settings_style_section(diagSection);
    styleTable();
    lv_obj_set_style_text_color(headerLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
}
