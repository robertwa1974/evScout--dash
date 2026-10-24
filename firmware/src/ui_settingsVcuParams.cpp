// ============================================================================
// Settings > VCU Parameters (2026-10-21)
// ============================================================================
// Read, adjust and write ZombieVerter tunables over SDO, then optionally
// tell the VCU to save them to its own flash - an on-dash subset of the
// openinverter web UI's parameter page. Two lv_menu pages:
//
//   vcuPage  - lock-status line, one lv_table listing every editable param
//              with the VCU's current value (tap a row to edit it), and a
//              SAVE TO VCU FLASH button behind a 130px confirm dialog.
//   editPage - one param: VCU value, +/- spinbox (same stepper row as the
//              Warning Thresholds page - no keypad, CLAUDE.md), WRITE.
//
// Curated list, not all ~120 of kSdoParams: only plain numeric tunables.
// Enum/config params (inverter type, CAN routing, pin function codes) are
// deliberately left off - a wrong value there can stop the drivetrain
// working entirely and needs the full web UI to recover anyway.
//
// The min/max below are only the spinbox's travel. The VCU enforces each
// param's real limits itself and aborts an out-of-range write, which shows
// here as "Value out of range" - so these are kept generous rather than
// guessed tight.
//
// Safety interlock: every write is refused unless sdo_client_vehicle_parked()
// (CAN live, Veh_Speed 0, opmode not Run) - enforced inside sdo_client.cpp,
// and mirrored here by disabling WRITE/SAVE with the reason shown.
//
// One lv_table (not a widget row per param) because every Settings page is
// built at once - a table is one object holding short strings, far cheaper
// than ~40 rows of separate widgets. (LVGL allocates through
// lv_psram_alloc.h - PSRAM first - since 2026-10-23; before that every
// widget came out of scarce internal RAM.)
// ============================================================================

#include "ui_settings_common.h"
#include "sdo_client.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;    // NULL id-0 entry = group heading
    uint16_t    id;
    const char *unit;
    float       min, max;
    uint8_t     decimals;
    uint16_t    step;    // spinbox step in display units * 10^decimals (power of 10)
} VcuParamDef;

static const VcuParamDef PARAMS[] = {
    {"THROTTLE", 0, "", 0, 0, 0, 0},
    {"throtmax",       25,  "%",       0,     100,   0, 1},
    {"throtmin",       26,  "%",       -100,  0,     0, 1},
    {"throtmaxRev",    123, "%",       0,     100,   0, 1},
    {"throtdead",      76,  "%",       0,     50,    0, 1},
    {"throtramp",      13,  "%/10ms",  0,     100,   0, 1},
    {"throtramprpm",   14,  "rpm",     0,     20000, 0, 100},
    {"revlim",         15,  "rpm",     0,     20000, 0, 100},
    {"REGEN", 0, "", 0, 0, 0, 0},
    {"regenmax",       61,  "%",       -100,  100,   0, 1},
    {"regenramp",      68,  "%/10ms",  0,     100,   0, 1},
    {"regenrpm",       60,  "rpm",     0,     20000, 0, 100},
    {"regenendrpm",    126, "rpm",     0,     20000, 0, 100},
    {"regenBrake",     122, "%",       -100,  100,   0, 1},
    {"RegenBrakeLight",128, "%",       -100,  100,   0, 1},
    {"LIMITS", 0, "", 0, 0, 0, 0},
    {"idcmax",         21,  "A",       0,     5000,  0, 10},
    {"idcmin",         22,  "A",       -5000, 0,     0, 10},
    {"udcmin",         19,  "V",       0,     1000,  0, 1},
    {"udclim",         20,  "V",       0,     1000,  0, 1},
    {"udcsw",          32,  "V",       0,     1000,  0, 1},
    {"tmphsmax",       23,  "\xC2\xB0" "C", 0, 200, 0, 1},
    {"tmpmmax",        24,  "\xC2\xB0" "C", 0, 200, 0, 1},
    {"CHARGING", 0, "", 0, 0, 0, 0},
    {"BattCap",        38,  "kWh",     0,     200,   1, 1},
    {"SOCFC",          79,  "%",       0,     100,   0, 1},
    {"Voltspnt",       40,  "V",       0,     1000,  0, 1},
    {"Pwrspnt",        41,  "W",       0,     30000, 0, 100},
    {"IdcTerm",        56,  "A",       0,     500,   0, 1},
    {"CCS_ILim",       43,  "A",       0,     500,   0, 1},
    {"CCS_SOCLim",     44,  "%",       0,     100,   0, 1},
    {"ChgAcVolt",      120, "Vac",     0,     500,   0, 1},
    {"ChgEff",         121, "%",       0,     100,   0, 1},
    {"BMS", 0, "", 0, 0, 0, 0},
    {"BMS_VminLimit",  92,  "V",       0,     5,     2, 1},
    {"BMS_VmaxLimit",  93,  "V",       0,     5,     2, 1},
    {"BMS_TminLimit",  94,  "\xC2\xB0" "C", -40, 100, 0, 1},
    {"BMS_TmaxLimit",  95,  "\xC2\xB0" "C", -40, 100, 0, 1},
    {"OTHER", 0, "", 0, 0, 0, 0},
    {"DCSetPnt",       106, "V",       0,     20,    1, 1},
    {"FanTemp",        134, "\xC2\xB0" "C", 0, 120, 0, 1},
    {"HeatPwr",        59,  "W",       0,     10000, 0, 100},
    {"HeatPercnt",     124, "%",       0,     100,   0, 1},
};
#define PARAM_ROWS (sizeof(PARAMS) / sizeof(PARAMS[0]))

typedef enum { OP_NONE, OP_WRITE, OP_SAVE } pending_op_t;

static lv_obj_t *menuObj = NULL;
static lv_obj_t *vcuPage = NULL;
static lv_obj_t *editPage = NULL;
static lv_obj_t *vcuSection = NULL;
static lv_obj_t *editSection = NULL;
static lv_obj_t *lockLabel = NULL;
static lv_obj_t *table = NULL;
static lv_obj_t *saveFlashBtn = NULL;
static lv_obj_t *saveStatusLabel = NULL;

static lv_obj_t *editTitle = NULL;
static lv_obj_t *editInfo = NULL;
static lv_obj_t *editLockLabel = NULL;
static lv_obj_t *editSpinbox = NULL;
static lv_obj_t *editRowLabel = NULL;
static lv_obj_t *writeBtn = NULL;
static lv_obj_t *writeStatusLabel = NULL;

static lv_obj_t *confirmOverlay = NULL;
static lv_timer_t *timer = NULL;

static bool watching = false;
static int selectedRow = -1;
static bool editTouched = false;   // user has stepped the spinbox since opening
static pending_op_t pendingOp = OP_NONE;
static int repeatCount = 0;

// Last value/state shown per table row - only cells that actually changed
// get rewritten (each lv_table_set_cell_value() invalidates the table).
static float shownValue[PARAM_ROWS];
static int8_t shownState[PARAM_ROWS];

static int32_t pow10i(uint8_t n) {
    int32_t p = 1;
    while (n--) p *= 10;
    return p;
}

static uint8_t digitsFor(int32_t v) {
    if (v < 0) v = -v;
    uint8_t d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

static void formatValue(char *out, size_t len, const VcuParamDef *p, sdo_value_state_t st, float v) {
    if (st == SDO_VALUE_VALID) snprintf(out, len, "%.*f %s", p->decimals, v, p->unit);
    else if (st == SDO_VALUE_UNSUPPORTED) snprintf(out, len, "n/a");
    else snprintf(out, len, "--");
}

// --- Lock state -------------------------------------------------------------

static const char *lockText(bool parked) {
    if (parked) return "Vehicle parked - editing enabled. Writes take effect immediately.";
    return "Locked: needs live CAN, vehicle stopped, VCU not in Run.";
}

static void setBtnEnabled(lv_obj_t *btn, bool enabled) {
    if (!btn) return;
    if (enabled) {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(btn, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(btn, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

// --- Editor -------------------------------------------------------------------

static void configureEditor(int row) {
    const VcuParamDef *p = &PARAMS[row];
    int32_t scale = pow10i(p->decimals);
    int32_t lo = (int32_t)lroundf(p->min * scale);
    int32_t hi = (int32_t)lroundf(p->max * scale);
    uint8_t digits = digitsFor(fabsf(p->min) > fabsf(p->max) ? lo : hi);
    if (digits <= p->decimals) digits = p->decimals + 1;

    lv_label_set_text(editTitle, p->name);
    lv_label_set_text_fmt(editRowLabel, "New value (%s)", p->unit);
    lv_spinbox_set_range(editSpinbox, lo, hi);
    lv_spinbox_set_digit_format(editSpinbox, digits, p->decimals ? digits - p->decimals : 0);
    lv_spinbox_set_step(editSpinbox, p->step);

    float v = 0;
    sdo_value_state_t st = sdo_client_get(p->id, &v);
    lv_spinbox_set_value(editSpinbox, st == SDO_VALUE_VALID ? (int32_t)lroundf(v * scale) : lo);
    editTouched = false;
    lv_label_set_text(writeStatusLabel, "");
}

static void editStepperCb(lv_event_t *e, int dir) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        repeatCount = 0;
    } else if (code == LV_EVENT_CLICKED) {
        _ui_spinbox_step(editSpinbox, dir);
        editTouched = true;
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        repeatCount++;
        int step = (repeatCount > 15) ? 5 : (repeatCount > 6 ? 2 : 1);  // same acceleration as the threshold rows
        for (int i = 0; i < step; i++) _ui_spinbox_step(editSpinbox, dir);
        editTouched = true;
    }
}
static void editMinusCb(lv_event_t *e) { editStepperCb(e, -1); }
static void editPlusCb(lv_event_t *e)  { editStepperCb(e, 1); }

static void writeBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || selectedRow < 0) return;
    const VcuParamDef *p = &PARAMS[selectedRow];
    float value = (float)lv_spinbox_get_value(editSpinbox) / (float)pow10i(p->decimals);
    if (sdo_client_write_param(p->id, value)) {
        pendingOp = OP_WRITE;
        lv_label_set_text(writeStatusLabel, "Writing...");
    } else {
        lv_label_set_text(writeStatusLabel, sdo_client_vehicle_parked() ? "Busy - try again" : "Locked - see above");
    }
}

static void tableClickCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    uint16_t row, col;
    lv_table_get_selected_cell(table, &row, &col);
    if (row == LV_TABLE_CELL_NONE || row >= PARAM_ROWS || PARAMS[row].id == 0) return;
    selectedRow = row;
    sdo_client_refresh(PARAMS[row].id);  // show the VCU's current value, not a stale read
    configureEditor(row);
    lv_menu_set_page(menuObj, editPage);
}

// Group-heading rows: accent text, no cell background. Value column right-aligned.
static void tableDrawCb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->label_dsc || !dsc->rect_dsc) return;
    uint32_t row = dsc->id / 2;
    uint32_t col = dsc->id % 2;
    if (row < PARAM_ROWS && PARAMS[row].id == 0) {
        dsc->label_dsc->color = ui_theme_accent();
        dsc->rect_dsc->bg_color = ui_theme_bg();
    } else if (col == 1) {
        dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
    }
}

// --- Save to flash -----------------------------------------------------------

static void closeConfirm(void) {
    if (confirmOverlay) {
        lv_obj_del(confirmOverlay);
        confirmOverlay = NULL;
    }
}

static void confirmCancelCb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) closeConfirm();
}

static void confirmSaveCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    closeConfirm();
    if (sdo_client_save_to_flash()) {
        pendingOp = OP_SAVE;
        lv_label_set_text(saveStatusLabel, "Saving...");
    } else {
        lv_label_set_text(saveStatusLabel, sdo_client_vehicle_parked() ? "Busy - try again" : "Locked - see above");
    }
}

static lv_obj_t *createConfirmBtn(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 240, CONFIRM_BTN_H);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, primary ? ui_theme_accent() : ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, primary ? ui_theme_bg() : ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

// Custom overlay rather than lv_msgbox: CLAUDE.md wants ~130px targets for
// safety-critical confirmations, and lv_msgbox's button matrix doesn't size
// its buttons that way. Child of the Settings screen (created after the dock,
// so it draws on top) - deleted with the screen, never left on lv_layer_top().
static void saveFlashBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || confirmOverlay) return;
    lv_obj_t *scr = lv_obj_get_screen(menuObj);
    confirmOverlay = lv_obj_create(scr);
    lv_obj_remove_style_all(confirmOverlay);
    lv_obj_set_size(confirmOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(confirmOverlay, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(confirmOverlay, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(confirmOverlay, LV_OBJ_FLAG_CLICKABLE);  // swallow taps behind the dialog

    lv_obj_t *panel = lv_obj_create(confirmOverlay);
    lv_obj_set_size(panel, 600, 320);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *msg = lv_label_create(panel);
    lv_label_set_text(msg, "Save all current VCU parameters\nto the VCU's flash?\nThey will survive a VCU restart.");
    lv_obj_set_style_text_font(msg, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(msg, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *cancel = createConfirmBtn(panel, "CANCEL", false, confirmCancelCb);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_t *save = createConfirmBtn(panel, "SAVE", true, confirmSaveCb);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
}

// --- Periodic refresh (only while one of this module's pages is shown) -------

static void handleOpResult(void) {
    if (pendingOp == OP_NONE) return;
    uint32_t abortCode = 0;
    sdo_op_state_t st = sdo_client_op_state(&abortCode);
    if (st == SDO_OP_QUEUED || st == SDO_OP_SENT) return;

    lv_obj_t *label = (pendingOp == OP_WRITE) ? writeStatusLabel : saveStatusLabel;
    switch (st) {
        case SDO_OP_OK:
            lv_label_set_text(label, pendingOp == OP_WRITE
                ? "Written. Not yet saved - use SAVE TO VCU FLASH to keep it."
                : "Saved to VCU flash.");
            if (pendingOp == OP_WRITE) editTouched = false;  // let the read-back value show
            break;
        case SDO_OP_FAILED:  lv_label_set_text(label, sdo_client_abort_text(abortCode)); break;
        case SDO_OP_TIMEOUT: lv_label_set_text(label, "No response from VCU"); break;
        case SDO_OP_REFUSED: lv_label_set_text(label, "Locked - see above"); break;
        default: break;
    }
    sdo_client_op_clear();
    pendingOp = OP_NONE;
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    bool parked = sdo_client_vehicle_parked();
    const char *lt = lockText(parked);
    if (strcmp(lv_label_get_text(lockLabel), lt) != 0) {
        lv_label_set_text(lockLabel, lt);
        lv_label_set_text(editLockLabel, lt);
    }
    bool idle = (pendingOp == OP_NONE);
    setBtnEnabled(writeBtn, parked && idle);
    setBtnEnabled(saveFlashBtn, parked && idle);

    char buf[32];
    for (uint16_t r = 0; r < PARAM_ROWS; r++) {
        if (PARAMS[r].id == 0) continue;
        float v = 0;
        sdo_value_state_t st = sdo_client_get(PARAMS[r].id, &v);
        if ((int8_t)st == shownState[r] && (st != SDO_VALUE_VALID || v == shownValue[r])) continue;
        shownState[r] = (int8_t)st;
        shownValue[r] = v;
        formatValue(buf, sizeof(buf), &PARAMS[r], st, v);
        lv_table_set_cell_value(table, r, 1, buf);
    }

    if (selectedRow >= 0) {
        const VcuParamDef *p = &PARAMS[selectedRow];
        float v = 0;
        sdo_value_state_t st = sdo_client_get(p->id, &v);
        formatValue(buf, sizeof(buf), p, st, v);
        lv_label_set_text_fmt(editInfo, "VCU value: %s", buf);
        if (st == SDO_VALUE_VALID && !editTouched) {
            lv_spinbox_set_value(editSpinbox, (int32_t)lroundf(v * pow10i(p->decimals)));
        }
    }

    handleOpResult();
}

// --- Page construction -------------------------------------------------------

static lv_obj_t *createInfoLabel(lv_obj_t *section, lv_coord_t h, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *row = lv_menu_cont_create(section);
    lv_obj_set_height(row, h);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, "");
    return label;
}

static lv_obj_t *createActionBtn(lv_obj_t *parent, const char *text, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void styleTable(void) {
    lv_obj_set_style_bg_color(table, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(table, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(table, ui_theme_panel_bg(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(table, ui_theme_panel_border(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(table, ui_theme_panel_border(), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(table, ui_theme_text_primary(), LV_PART_ITEMS | LV_STATE_DEFAULT);
}

static void onDelete(lv_event_t *e) {
    (void)e;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    if (watching) sdo_client_unwatch();
    watching = false;
    menuObj = vcuPage = editPage = vcuSection = editSection = NULL;
    lockLabel = table = saveFlashBtn = saveStatusLabel = NULL;
    editTitle = editInfo = editLockLabel = editSpinbox = editRowLabel = NULL;
    writeBtn = writeStatusLabel = NULL;
    confirmOverlay = NULL;  // child of the screen - already being deleted with it
    selectedRow = -1;
    pendingOp = OP_NONE;
}

lv_obj_t *ui_settingsVcuParams_create_page(lv_obj_t *menu) {
    menuObj = menu;

    // --- Editor page (built first so table rows can target it) ---
    editPage = lv_menu_page_create(menu, (char *)"Edit Parameter");
    lv_obj_set_style_pad_all(editPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(editPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    editSection = lv_menu_section_create(editPage);

    editTitle = createInfoLabel(editSection, 48, &font_montserrat_extrabold_24, ui_theme_text_primary());
    editInfo = createInfoLabel(editSection, 40, &font_montserrat_extrabold_16, ui_theme_text_primary());
    editLockLabel = createInfoLabel(editSection, 40, &font_montserrat_semibold_16, ui_theme_text_secondary());

    editSpinbox = ui_settings_create_stepper_row(editSection, "New value", 0, 100, 0, 3, 0);
    {
        lv_obj_t *cont = lv_obj_get_parent(editSpinbox);
        editRowLabel = lv_obj_get_child(cont, 0);
        lv_obj_add_event_cb(lv_obj_get_child(cont, 1), editMinusCb, LV_EVENT_ALL, NULL);
        lv_obj_add_event_cb(lv_obj_get_child(cont, 3), editPlusCb, LV_EVENT_ALL, NULL);
    }

    lv_obj_t *writeRow = lv_menu_cont_create(editSection);
    lv_obj_set_height(writeRow, ROW_HEIGHT);
    writeStatusLabel = lv_label_create(writeRow);
    lv_label_set_long_mode(writeStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(writeStatusLabel, 1);
    lv_obj_set_style_text_font(writeStatusLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(writeStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(writeStatusLabel, "");
    writeBtn = createActionBtn(writeRow, "WRITE", 160, STEPPER_BTN, writeBtnCb);

    // --- List page ---
    vcuPage = lv_menu_page_create(menu, (char *)"VCU Parameters");
    lv_obj_set_style_pad_all(vcuPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(vcuPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    vcuSection = lv_menu_section_create(vcuPage);

    lockLabel = createInfoLabel(vcuSection, 48, &font_montserrat_semibold_16, ui_theme_text_secondary());

    lv_obj_t *tableRow = lv_menu_cont_create(vcuSection);
    lv_obj_set_style_pad_all(tableRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    table = lv_table_create(tableRow);
    lv_table_set_col_cnt(table, 2);
    lv_table_set_row_cnt(table, PARAM_ROWS);
    lv_table_set_col_width(table, 0, 420);
    lv_table_set_col_width(table, 1, 300);
    // 24px text + 28px top/bottom padding = ~80px rows, CLAUDE.md's touch floor.
    lv_obj_set_style_text_font(table, &font_montserrat_extrabold_24, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(table, 28, LV_PART_ITEMS | LV_STATE_DEFAULT);
    styleTable();
    for (uint16_t r = 0; r < PARAM_ROWS; r++) {
        lv_table_set_cell_value(table, r, 0, PARAMS[r].name);
        lv_table_set_cell_value(table, r, 1, PARAMS[r].id ? "--" : "");
        shownState[r] = -1;
    }
    lv_obj_add_event_cb(table, tableClickCb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(table, tableDrawCb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_t *saveRow = lv_menu_cont_create(vcuSection);
    lv_obj_set_height(saveRow, CONFIRM_BTN_H + 16);
    saveStatusLabel = lv_label_create(saveRow);
    lv_label_set_long_mode(saveStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(saveStatusLabel, 1);
    lv_obj_set_style_text_font(saveStatusLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(saveStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(saveStatusLabel, "Writes are live but lost on VCU restart until saved.");
    saveFlashBtn = createActionBtn(saveRow, "SAVE TO VCU FLASH", 280, CONFIRM_BTN_H - 30, saveFlashBtnCb);

    lv_label_set_text(lockLabel, lockText(false));
    lv_label_set_text(editLockLabel, lockText(false));
    setBtnEnabled(writeBtn, false);
    setBtnEnabled(saveFlashBtn, false);

    ui_settings_style_section(vcuSection);
    ui_settings_style_section(editSection);
    lv_obj_add_event_cb(vcuPage, onDelete, LV_EVENT_DELETE, NULL);
    return vcuPage;
}

void ui_settingsVcuParams_page_leave(lv_obj_t *page) {
    if (page != NULL && (page == vcuPage || page == editPage)) return;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    if (watching) {
        sdo_client_unwatch();
        watching = false;
    }
    closeConfirm();
}

void ui_settingsVcuParams_page_enter(lv_obj_t *page) {
    if (page == NULL || (page != vcuPage && page != editPage)) return;
    if (!watching) {
        uint16_t ids[SDO_CLIENT_MAX_WATCH];
        size_t n = 0;
        for (size_t r = 0; r < PARAM_ROWS && n < SDO_CLIENT_MAX_WATCH; r++) {
            if (PARAMS[r].id) ids[n++] = PARAMS[r].id;
        }
        sdo_client_watch(ids, n, 0);  // tunables: read once, re-read after each write
        watching = true;
        for (size_t r = 0; r < PARAM_ROWS; r++) shownState[r] = -1;
    }
    if (!timer) timer = lv_timer_create(timerCb, 250, NULL);
}

void ui_settingsVcuParams_refresh_theme(void) {
    if (!vcuPage) return;
    lv_obj_set_style_bg_color(vcuPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(editPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_settings_style_section(vcuSection);
    ui_settings_style_section(editSection);
    styleTable();
    lv_obj_set_style_text_color(lockLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(editLockLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(editTitle, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(editInfo, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(writeStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(saveStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(editSpinbox, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(editSpinbox, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    bool parked = sdo_client_vehicle_parked();
    setBtnEnabled(writeBtn, parked && pendingOp == OP_NONE);
    setBtnEnabled(saveFlashBtn, parked && pendingOp == OP_NONE);
}
