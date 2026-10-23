// ============================================================================
// Settings > CAN Monitor (2026-10-22)
// ============================================================================
// SavvyCAN-style "overwrite mode" ID summary: one row per CAN ID with its
// known-device name, frames/s, and latest data. A bus-health line on top:
// driver state, frames/s, estimated bus load, TX/RX error counters, missed
// frames.
//
// For checking Rob's own bus setup (VCU, IVT-S shunt, BMS) and spotting new
// devices - full decoding is done in SavvyCAN on a separate bench setup.
//
//   Stage 1 - ID table + bus health. Raw view highlights bytes that changed
//             in the last second; IDs not seen for STALE_MS are greyed.
//   Stage 2 - RAW/DECODED toggle (can_monitor_decode(): VCU SDO replies,
//             IVT-S, SimpBMS), plus a shunt-vs-VCU cross-check line: when the
//             VCU's ShuntType is ISA its idc/udc come straight from the
//             shunt's frames, so they should match what the dash decodes
//             itself - a mismatch means the shunt, the VCU's shunt settings,
//             or the decoding is wrong.
//   Stage 3 - known-good baseline (can_monitor.h): SAVE BASELINE records the
//             live ID set + rates; rows are then flagged NEW (not in the
//             baseline), MISSING (baseline ID silent for STALE_MS - shown as
//             its own row even if never seen this session) or RATE (rate off
//             by >30% and >2 Hz; only for IDs normally >=2 Hz and after a
//             warm-up, since 1 s rate windows jitter on slow IDs). Hold
//             FORGET to erase it.
//   Stage 4 - START/STOP CAPTURE: raw frames (incl. the dash's own SDO
//             requests as Tx) to /can/CANnnnn.csv in SavvyCAN's native CSV
//             format - see can_capture.h. SDO toggle drops 0x603/0x583 so
//             other devices stand out. Capture keeps running after leaving
//             this page; the status line shows it whenever the page is open.
//
// Capture only runs while this page is on screen. One lv_table, rows
// rewritten in place - same 48KB LVGL heap reasoning as the Diagnostics
// page; total rows capped at CAN_MONITOR_MAX_IDS.
// ============================================================================

#include "ui_settings_common.h"
#include "can_monitor.h"
#include "can_capture.h"
#include "can_sdo.h"  // extern ESP32S3_TWAI can
#include "zombie_updaters.h"  // myData, dataMutex - VCU side of the shunt cross-check
#include "ui_can_freshness.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REFRESH_MS      250
#define STALE_MS        3000
#define RATE_WARMUP_MS  3000
#define RATE_MIN_BASE   2.0f    // Hz - slower IDs aren't rate-checked
#define RATE_TOL_FRAC   0.30f
#define RATE_TOL_MIN    2.0f    // Hz
#define COLS            5

typedef enum { FLAG_NONE, FLAG_NEW, FLAG_MISSING, FLAG_RATE } row_flag_t;

typedef struct {
    can_monitor_row_t r;
    row_flag_t flag;
    bool stale;
    bool synthetic;   // baseline ID never seen this session - no data to show
} display_row_t;

static lv_obj_t *canPage = NULL;
static lv_obj_t *canSection = NULL;
static lv_obj_t *healthLabel = NULL;
static lv_obj_t *shuntLabel = NULL;
static lv_obj_t *baseLabel = NULL;
static lv_obj_t *hintLabel = NULL;
static lv_obj_t *modeBtnLabel = NULL;
static lv_obj_t *captureLabel = NULL;
static lv_obj_t *captureBtn = NULL, *captureBtnLabel = NULL;
static lv_obj_t *sdoBtnLabel = NULL;
static lv_obj_t *table = NULL;
static lv_timer_t *timer = NULL;
static bool decodedMode = true;
static uint32_t warmStartMs = 0;

static can_monitor_row_t live[CAN_MONITOR_MAX_IDS];
static display_row_t shown[CAN_MONITOR_MAX_IDS];
static size_t shownCount = 0;

static void setCell(uint16_t r, uint16_t c, const char *text) {
    const char *cur = lv_table_get_cell_value(table, r, c);
    if (!cur || strcmp(cur, text) != 0) lv_table_set_cell_value(table, r, c, text);
}

static void setLabel(lv_obj_t *l, const char *text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

static void updateHealth(const can_monitor_stats_t *st, size_t ids) {
    char buf[200];
    twai_status_info_t s;
    if (!can.isInitialized() || twai_get_status_info(&s) != ESP_OK) {
        snprintf(buf, sizeof(buf), "Bus: NO ACK / BUS DEAD - driver off, retrying every 2 s");
    } else {
        const char *state = "RUNNING";
        if (s.state == TWAI_STATE_BUS_OFF) state = "BUS-OFF";
        else if (s.state == TWAI_STATE_RECOVERING) state = "RECOVERING";
        else if (s.state == TWAI_STATE_STOPPED) state = "STOPPED";
        else if (s.tx_error_counter >= 128 || s.rx_error_counter >= 128) state = "ERROR-PASSIVE";
        snprintf(buf, sizeof(buf), "Bus: %s   %.0f fr/s   load ~%.0f%%   TEC %lu  REC %lu   missed %lu   IDs %u%s",
                 state, st->framesPerSec, st->busLoadPct,
                 (unsigned long)s.tx_error_counter, (unsigned long)s.rx_error_counter,
                 (unsigned long)s.rx_missed_count, (unsigned)ids,
                 st->tableFull ? "  (ID table full)" : "");
    }
    setLabel(healthLabel, buf);
}

// Mismatch threshold: 2% of reading or 1 A / 2 V, whichever is larger -
// the two values are sampled at different instants, so a small gap under
// changing load is normal.
static void updateShuntCheck(const can_monitor_stats_t *st) {
    char buf[160];
    bool mismatch = false;
    if (!st->shuntCurrentValid && !st->shuntVoltageValid) {
        snprintf(buf, sizeof(buf), "Shunt vs VCU: no IVT-S frames (0x521/0x522) seen");
    } else {
        float vcuI = 0, vcuU = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            vcuI = myData.packCurrent;
            vcuU = myData.packVoltage;
            xSemaphoreGive(dataMutex);
        }
        bool vcuLive = ui_can_freshness_isLive();
        char iPart[64] = "I --", uPart[64] = "U --";
        if (st->shuntCurrentValid) {
            float d = st->shuntCurrentA - vcuI;
            snprintf(iPart, sizeof(iPart), "I %.2f A / VCU %.1f A", st->shuntCurrentA, vcuI);
            if (vcuLive && fabsf(d) > fmaxf(1.0f, fabsf(st->shuntCurrentA) * 0.02f)) mismatch = true;
        }
        if (st->shuntVoltageValid) {
            float d = st->shuntVoltageV - vcuU;
            snprintf(uPart, sizeof(uPart), "U %.1f V / VCU %.1f V", st->shuntVoltageV, vcuU);
            if (vcuLive && fabsf(d) > fmaxf(2.0f, fabsf(st->shuntVoltageV) * 0.02f)) mismatch = true;
        }
        snprintf(buf, sizeof(buf), "Shunt vs VCU:  %s    %s%s", iPart, uPart,
                 mismatch ? "   MISMATCH" : (vcuLive ? "   OK" : "   (no VCU data)"));
    }
    setLabel(shuntLabel, buf);
    lv_obj_set_style_text_color(shuntLabel, mismatch ? ui_theme_warning() : ui_theme_text_primary(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static int cmpShown(const void *a, const void *b) {
    const can_monitor_row_t *ra = &((const display_row_t *)a)->r, *rb = &((const display_row_t *)b)->r;
    if (ra->extended != rb->extended) return ra->extended ? 1 : -1;
    return (ra->id > rb->id) - (ra->id < rb->id);
}

// Builds shown[] from the live snapshot + the baseline, and counts flags.
static void buildRows(size_t liveCount, int *nNew, int *nMissing, int *nRate) {
    *nNew = *nMissing = *nRate = 0;
    size_t baseN = can_monitor_baseline_count();
    bool warm = millis() - warmStartMs > RATE_WARMUP_MS;
    shownCount = 0;

    for (size_t i = 0; i < liveCount && shownCount < CAN_MONITOR_MAX_IDS; i++) {
        display_row_t *d = &shown[shownCount++];
        d->r = live[i];
        d->stale = live[i].ageMs > STALE_MS;
        d->synthetic = false;
        d->flag = FLAG_NONE;
        if (baseN == 0) continue;
        const can_monitor_base_t *b = can_monitor_baseline_find(live[i].id, live[i].extended);
        if (!b) {
            d->flag = FLAG_NEW;
            (*nNew)++;
        } else if (d->stale) {
            d->flag = FLAG_MISSING;
            (*nMissing)++;
        } else if (warm && b->rateHz >= RATE_MIN_BASE &&
                   fabsf(live[i].rateHz - b->rateHz) > fmaxf(RATE_TOL_MIN, b->rateHz * RATE_TOL_FRAC)) {
            d->flag = FLAG_RATE;
            (*nRate)++;
        }
    }

    // Baseline IDs not seen at all this session.
    for (size_t j = 0; j < baseN; j++) {
        const can_monitor_base_t *b = can_monitor_baseline_at(j);
        bool present = false;
        for (size_t i = 0; i < liveCount; i++) {
            if (live[i].id == b->id && live[i].extended == b->extended) { present = true; break; }
        }
        if (present) continue;
        (*nMissing)++;
        if (shownCount >= CAN_MONITOR_MAX_IDS) continue;  // still counted, just not listed
        display_row_t *d = &shown[shownCount++];
        memset(d, 0, sizeof(*d));
        d->r.id = b->id;
        d->r.extended = b->extended;
        d->flag = FLAG_MISSING;
        d->stale = true;
        d->synthetic = true;
    }
    qsort(shown, shownCount, sizeof(shown[0]), cmpShown);
}

static void updateBaselineLine(int nNew, int nMissing, int nRate) {
    char buf[160];
    size_t baseN = can_monitor_baseline_count();
    bool alert = false;
    if (baseN == 0) {
        snprintf(buf, sizeof(buf), "Baseline: none - SAVE BASELINE records how this bus normally looks");
    } else {
        alert = nNew || nMissing || nRate;
        snprintf(buf, sizeof(buf), "Baseline: %u IDs   %d NEW   %d MISSING   %d RATE%s",
                 (unsigned)baseN, nNew, nMissing, nRate, alert ? "" : "   - matches");
    }
    setLabel(baseLabel, buf);
    lv_obj_set_style_text_color(baseLabel, alert ? ui_theme_warning() : ui_theme_text_primary(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void updateCapture(void) {
    bool on = can_capture_is_running();
    setLabel(captureLabel, can_capture_status_text());
    setLabel(captureBtnLabel, on ? "STOP CAPTURE" : "START CAPTURE");
    lv_obj_set_style_bg_color(captureBtn, on ? ui_theme_warning() : ui_theme_panel_border(),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    setLabel(sdoBtnLabel, can_capture_include_sdo() ? "SDO: KEEP" : "SDO: DROP");
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    updateCapture();
    can_monitor_stats_t st;
    size_t liveCount = can_monitor_snapshot(live, CAN_MONITOR_MAX_IDS, &st);
    updateHealth(&st, liveCount);
    updateShuntCheck(&st);
    int nNew, nMissing, nRate;
    buildRows(liveCount, &nNew, &nMissing, &nRate);
    updateBaselineLine(nNew, nMissing, nRate);

    if (lv_table_get_row_cnt(table) != shownCount + 1) lv_table_set_row_cnt(table, shownCount + 1);

    // Changed bytes recolored with LVGL's "#RRGGBB text#" syntax (enabled
    // for the data column in tableDrawCb).
    char accent[8];
    snprintf(accent, sizeof(accent), "%06lx", (unsigned long)(lv_color_to32(ui_theme_accent()) & 0xFFFFFF));

    static const char *FLAG_TEXT[] = {"", "NEW", "MISSING", "RATE"};
    char buf[96];
    for (size_t i = 0; i < shownCount; i++) {
        const display_row_t *d = &shown[i];
        const can_monitor_row_t *r = &d->r;
        uint16_t tr = i + 1;

        snprintf(buf, sizeof(buf), r->extended ? "%08lX" : "%03lX", (unsigned long)r->id);
        setCell(tr, 0, buf);
        const char *name = can_monitor_id_name(r->id, r->extended);
        setCell(tr, 1, name ? name : "-");
        setCell(tr, 4, FLAG_TEXT[d->flag]);

        if (d->synthetic) {
            const can_monitor_base_t *b = can_monitor_baseline_find(r->id, r->extended);
            setCell(tr, 2, "0");
            snprintf(buf, sizeof(buf), "not seen (normally %.0f Hz)", b ? b->rateHz : 0.0f);
            setCell(tr, 3, buf);
            continue;
        }
        snprintf(buf, sizeof(buf), "%.0f", r->rateHz);
        setCell(tr, 2, buf);

        if (decodedMode && can_monitor_decode(r->id, r->extended, r->data, r->len, buf, sizeof(buf))) {
            setCell(tr, 3, buf);
            continue;
        }
        size_t p = 0;
        for (uint8_t b = 0; b < r->len && p < sizeof(buf) - 16; b++) {
            if (r->changedMask & (1u << b)) p += snprintf(buf + p, sizeof(buf) - p, "#%s %02X# ", accent, r->data[b]);
            else p += snprintf(buf + p, sizeof(buf) - p, "%02X ", r->data[b]);
        }
        if (p) buf[p - 1] = '\0'; else buf[0] = '\0';
        setCell(tr, 3, buf);
    }
}

// Header row in secondary text; data column recolorable; stale rows dimmed;
// flags colored (NEW = accent, MISSING/RATE = warning).
static void tableDrawCb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->label_dsc) return;
    uint32_t row = dsc->id / COLS, col = dsc->id % COLS;
    if (row == 0) {
        dsc->label_dsc->color = ui_theme_text_secondary();
        return;
    }
    if (row - 1 >= shownCount) return;
    const display_row_t *d = &shown[row - 1];
    if (col == 3) dsc->label_dsc->flag |= LV_TEXT_FLAG_RECOLOR;
    if (col == 2) dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
    if (col == 4) {
        dsc->label_dsc->color = (d->flag == FLAG_NEW) ? ui_theme_accent() : ui_theme_warning();
        return;
    }
    if (d->stale) dsc->label_dsc->color = ui_theme_dim();
}

static void modeCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    decodedMode = !decodedMode;
    lv_label_set_text(modeBtnLabel, decodedMode ? "SHOW RAW" : "DECODE");
    lv_table_set_cell_value(table, 0, 3, decodedMode ? "VALUE" : "DATA");
}

static void clearCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    can_monitor_clear();
    warmStartMs = millis();
    lv_table_set_row_cnt(table, 1);
}

static void saveBaselineCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    size_t n = can_monitor_baseline_save();
    char buf[96];
    snprintf(buf, sizeof(buf), n ? "Baseline saved: %u IDs" : "Nothing to save - no IDs seen in the last 3 s", (unsigned)n);
    setLabel(hintLabel, buf);
}

// Long-press so a stray tap can't wipe a baseline Rob set up carefully.
static void forgetBaselineCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
    can_monitor_baseline_forget();
    setLabel(hintLabel, "Baseline erased");
}

static void captureCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (can_capture_is_running()) can_capture_stop();
    else can_capture_start();   // failure reason lands in the status text
    updateCapture();
}

static void sdoToggleCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    can_capture_set_include_sdo(!can_capture_include_sdo());
    updateCapture();
}

static lv_obj_t *createBtn(lv_obj_t *parent, const char *text, lv_coord_t w, lv_event_cb_t cb,
                           lv_event_code_t code, lv_obj_t **outLabel) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, STEPPER_BTN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, code, NULL);
    if (outLabel) *outLabel = lbl;
    return btn;
}

static lv_obj_t *createInfoLabel(lv_obj_t *section, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *row = lv_menu_cont_create(section);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(l, "");
    return l;
}

static void styleTable(void) {
    lv_obj_set_style_bg_color(table, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(table, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(table, ui_theme_panel_bg(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(table, ui_theme_panel_border(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(table, ui_theme_text_primary(), LV_PART_ITEMS | LV_STATE_DEFAULT);
}

static void onDelete(lv_event_t *e) {
    (void)e;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    can_monitor_set_active(false);
    canPage = canSection = healthLabel = shuntLabel = baseLabel = hintLabel = modeBtnLabel = table = NULL;
    captureLabel = captureBtn = captureBtnLabel = sdoBtnLabel = NULL;
    shownCount = 0;
}

lv_obj_t *ui_settingsCanMonitor_create_page(lv_obj_t *menu) {
    can_monitor_baseline_load();

    canPage = lv_menu_page_create(menu, (char *)"CAN Monitor");
    lv_obj_set_style_pad_all(canPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(canPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    canSection = lv_menu_section_create(canPage);

    healthLabel = createInfoLabel(canSection, &font_montserrat_extrabold_16, ui_theme_text_primary());
    shuntLabel = createInfoLabel(canSection, &font_montserrat_extrabold_16, ui_theme_text_primary());
    baseLabel = createInfoLabel(canSection, &font_montserrat_extrabold_16, ui_theme_text_primary());

    // 160+140+200+140 = 640px of buttons - leaves room for the row's own
    // padding/gaps inside the 776px page without overflowing.
    lv_obj_t *btnRow = lv_menu_cont_create(canSection);
    lv_obj_set_height(btnRow, ROW_HEIGHT);
    createBtn(btnRow, decodedMode ? "SHOW RAW" : "DECODE", 160, modeCb, LV_EVENT_CLICKED, &modeBtnLabel);
    createBtn(btnRow, "CLEAR", 140, clearCb, LV_EVENT_CLICKED, NULL);
    createBtn(btnRow, "SAVE BASELINE", 200, saveBaselineCb, LV_EVENT_CLICKED, NULL);
    createBtn(btnRow, "FORGET", 140, forgetBaselineCb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *capRow = lv_menu_cont_create(canSection);
    lv_obj_set_height(capRow, ROW_HEIGHT);
    captureBtn = createBtn(capRow, "START CAPTURE", 220, captureCb, LV_EVENT_CLICKED, &captureBtnLabel);
    createBtn(capRow, "SDO: KEEP", 170, sdoToggleCb, LV_EVENT_CLICKED, &sdoBtnLabel);
    captureLabel = lv_label_create(capRow);
    lv_label_set_long_mode(captureLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(captureLabel, 1);
    lv_obj_set_style_text_font(captureLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(captureLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(captureLabel, "");

    hintLabel = createInfoLabel(canSection, &font_montserrat_semibold_16, ui_theme_text_secondary());
    lv_label_set_text(hintLabel, "Raw view highlights bytes changed in the last second. Grey rows: not seen for 3 s. "
                                 "Hold FORGET to erase the baseline.");

    lv_obj_t *tableRow = lv_menu_cont_create(canSection);
    lv_obj_set_style_pad_all(tableRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    table = lv_table_create(tableRow);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_CLICKABLE);
    lv_table_set_col_cnt(table, COLS);
    lv_table_set_row_cnt(table, 1);
    lv_table_set_col_width(table, 0, 80);
    lv_table_set_col_width(table, 1, 190);
    lv_table_set_col_width(table, 2, 60);
    lv_table_set_col_width(table, 3, 300);
    lv_table_set_col_width(table, 4, 100);
    lv_obj_set_style_text_font(table, &font_montserrat_extrabold_16, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(table, 8, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(table, 8, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_table_set_cell_value(table, 0, 0, "ID");
    lv_table_set_cell_value(table, 0, 1, "DEVICE");
    lv_table_set_cell_value(table, 0, 2, "HZ");
    lv_table_set_cell_value(table, 0, 3, decodedMode ? "VALUE" : "DATA");
    lv_table_set_cell_value(table, 0, 4, "FLAG");
    styleTable();
    lv_obj_add_event_cb(table, tableDrawCb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    ui_settings_style_section(canSection);
    lv_obj_add_event_cb(canPage, onDelete, LV_EVENT_DELETE, NULL);
    return canPage;
}

void ui_settingsCanMonitor_page_leave(lv_obj_t *page) {
    if (page != NULL && page == canPage) return;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    can_monitor_set_active(false);
}

void ui_settingsCanMonitor_page_enter(lv_obj_t *page) {
    if (page == NULL || page != canPage) return;
    can_monitor_set_active(true);
    warmStartMs = millis();
    lv_table_set_row_cnt(table, 1);
    if (!timer) timer = lv_timer_create(timerCb, REFRESH_MS, NULL);
}

void ui_settingsCanMonitor_refresh_theme(void) {
    if (!canPage) return;
    lv_obj_set_style_bg_color(canPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_settings_style_section(canSection);
    styleTable();
    lv_obj_set_style_text_color(healthLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hintLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(captureLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    updateCapture();
}
