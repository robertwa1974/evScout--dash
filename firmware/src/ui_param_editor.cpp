#include "ui_param_editor.h"
#include "ui.h"
#include "sdo_client.h"
#include "zombie_updaters.h"  // myData, dataMutex, PARAM_ID_*
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX_OPTIONS 4
#define TICK_MS     100

static lv_obj_t *screenObj = NULL;
static lv_obj_t *overlay = NULL;
static lv_obj_t *statusLbl = NULL;
static lv_obj_t *valueLbl = NULL;
static lv_obj_t *vcuLbl = NULL;
static lv_obj_t *minusBtn = NULL, *plusBtn = NULL;
static lv_obj_t *optBtns[MAX_OPTIONS];
static int optCount = 0;
static lv_timer_t *timer = NULL;

static uint16_t paramId = 0;
static bool isChoice = false;
static int minV = 0, maxV = 0;
static const char *unitText = "";
static int target = 0;
static bool targetDirty = false;
static bool opPending = false;
static int lastSent = 0;

// The four params this editor is used for, read from / written back to
// myData (written optimistically on a successful ack so the card updates
// before the next poll cycle - the Dial does the same with its cache).
static int readCurrent(void) {
    int v = 0;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        switch (paramId) {
            case PARAM_ID_GEAR:       v = myData.gear; break;
            case PARAM_ID_MOT_ACTIVE: v = myData.motActive; break;
            case PARAM_ID_REGENMAX:   v = (int)lroundf(myData.regenMax); break;
            case PARAM_ID_REGENBRAKE: v = (int)lroundf(myData.regenBrake); break;
        }
        xSemaphoreGive(dataMutex);
    }
    return v;
}

static void writeCurrent(int v) {
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        switch (paramId) {
            case PARAM_ID_GEAR:       myData.gear = v; break;
            case PARAM_ID_MOT_ACTIVE: myData.motActive = v; break;
            case PARAM_ID_REGENMAX:   myData.regenMax = (float)v; break;
            case PARAM_ID_REGENBRAKE: myData.regenBrake = (float)v; break;
        }
        xSemaphoreGive(dataMutex);
    }
}

static void setEnabled(lv_obj_t *btn, bool en) {
    if (!btn) return;
    if (en) lv_obj_clear_state(btn, LV_STATE_DISABLED);
    else lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(btn, en ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void setStatus(const char *text) {
    if (strcmp(lv_label_get_text(statusLbl), text) != 0) lv_label_set_text(statusLbl, text);
}

static void refreshValue(void) {
    if (isChoice) {
        for (int i = 0; i < optCount; i++) {
            bool sel = (i == target);
            lv_obj_set_style_bg_color(optBtns[i], sel ? ui_theme_accent() : ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(optBtns[i], sel ? ui_theme_bg() : ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        lv_label_set_text_fmt(valueLbl, "%d %s", target, unitText);
    }
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    const char *why = NULL;
    bool allowed = sdo_client_write_allowed(paramId, &why);

    if (opPending) {
        uint32_t abortCode = 0;
        sdo_op_state_t st = sdo_client_op_state(&abortCode);
        if (st != SDO_OP_QUEUED && st != SDO_OP_SENT) {
            switch (st) {
                case SDO_OP_OK:      writeCurrent(lastSent); setStatus("Sent to VCU (not saved to flash)"); break;
                case SDO_OP_FAILED:  setStatus(sdo_client_abort_text(abortCode)); break;
                case SDO_OP_TIMEOUT: setStatus("No response from VCU"); break;
                default:             setStatus(why ? why : "Not sent"); break;
            }
            sdo_client_op_clear();
            opPending = false;
        }
    }
    if (!opPending && targetDirty && allowed) {
        if (sdo_client_write_param(paramId, (float)target)) {
            opPending = true;
            lastSent = target;
            targetDirty = false;
            setStatus("Sending...");
        }
    }
    if (!allowed) {
        targetDirty = false;  // never send a change queued before the lock engaged
        setStatus(why);
    }

    for (int i = 0; i < optCount; i++) setEnabled(optBtns[i], allowed);
    setEnabled(minusBtn, allowed);
    setEnabled(plusBtn, allowed);

    int current = readCurrent();
    if (!opPending && !targetDirty && target != current) {
        target = current;  // follow the VCU when nothing of ours is in flight
        refreshValue();
    }
    if (vcuLbl) lv_label_set_text_fmt(vcuLbl, "VCU: %d %s", current, unitText);
}

static void optionCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    target = (int)(intptr_t)lv_event_get_user_data(e);
    targetDirty = true;
    refreshValue();
}

static void stepCb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int v = target + dir;
    if (v < minV) v = minV;
    if (v > maxV) v = maxV;
    if (v != target) {
        target = v;
        targetDirty = true;
        refreshValue();
    }
}

static void doneCb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) ui_param_editor_close();
}

static void screenUnloadCb(lv_event_t *e) {
    (void)e;
    ui_param_editor_close();
}

static lv_obj_t *createBtn(lv_obj_t *parent, const char *text, lv_coord_t w, lv_coord_t h, const lv_font_t *font) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(l);
    return btn;
}

static lv_obj_t *openCommon(const char *title, uint16_t id) {
    ui_param_editor_close();
    paramId = id;
    targetDirty = false;
    opPending = false;
    optCount = 0;
    minusBtn = plusBtn = valueLbl = vcuLbl = NULL;
    target = readCurrent();
    sdo_client_op_clear();

    screenObj = lv_scr_act();
    overlay = lv_obj_create(screenObj);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);         // swallow taps behind the editor
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);  // no swipe-to-navigate with it open

    lv_obj_t *panel = lv_obj_create(overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *titleLbl = lv_label_create(panel);
    lv_label_set_text(titleLbl, title);
    lv_obj_set_style_text_font(titleLbl, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(titleLbl, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(titleLbl, 20, 16);

    statusLbl = lv_label_create(panel);
    lv_label_set_text(statusLbl, "");
    lv_obj_set_width(statusLbl, 720);
    lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(statusLbl, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(statusLbl, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(statusLbl, 20, 52);

    lv_obj_t *done = createBtn(panel, "DONE", 220, 90, &font_montserrat_extrabold_24);
    lv_obj_align(done, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(done, doneCb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(screenObj, screenUnloadCb, LV_EVENT_SCREEN_UNLOAD_START, NULL);
    timer = lv_timer_create(timerCb, TICK_MS, NULL);
    return panel;
}

void ui_param_editor_open_choice(const char *title, uint16_t id, const char *const *options, int count) {
    lv_obj_t *panel = openCommon(title, id);
    isChoice = true;
    optCount = count > MAX_OPTIONS ? MAX_OPTIONS : count;
    for (int i = 0; i < optCount; i++) {
        optBtns[i] = createBtn(panel, options[i], 350, 110, &font_montserrat_extrabold_24);
        lv_obj_set_pos(optBtns[i], 20 + (i % 2) * 370, 88 + (i / 2) * 122);
        lv_obj_add_event_cb(optBtns[i], optionCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    refreshValue();
    timerCb(timer);
}

void ui_param_editor_open_stepper(const char *title, uint16_t id, int minValue, int maxValue, const char *unit) {
    lv_obj_t *panel = openCommon(title, id);
    isChoice = false;
    minV = minValue;
    maxV = maxValue;
    unitText = unit;

    minusBtn = createBtn(panel, LV_SYMBOL_MINUS, 140, 140, &font_montserrat_extrabold_32);
    lv_obj_set_pos(minusBtn, 40, 110);
    lv_obj_add_event_cb(minusBtn, stepCb, LV_EVENT_ALL, (void *)(intptr_t)-1);
    plusBtn = createBtn(panel, LV_SYMBOL_PLUS, 140, 140, &font_montserrat_extrabold_32);
    lv_obj_set_pos(plusBtn, 760 - 40 - 140, 110);
    lv_obj_add_event_cb(plusBtn, stepCb, LV_EVENT_ALL, (void *)(intptr_t)1);

    valueLbl = lv_label_create(panel);
    lv_obj_set_style_text_font(valueLbl, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(valueLbl, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(valueLbl, LV_ALIGN_TOP_MID, 0, 150);
    vcuLbl = lv_label_create(panel);
    lv_obj_set_style_text_font(vcuLbl, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(vcuLbl, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(vcuLbl, LV_ALIGN_TOP_MID, 0, 220);
    lv_label_set_text(vcuLbl, "");

    refreshValue();
    timerCb(timer);
}

void ui_param_editor_close(void) {
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    if (screenObj) {
        lv_obj_remove_event_cb(screenObj, screenUnloadCb);
        screenObj = NULL;
    }
    if (overlay) {
        lv_obj_del(overlay);
        overlay = NULL;
    }
    statusLbl = valueLbl = vcuLbl = minusBtn = plusBtn = NULL;
    for (int i = 0; i < MAX_OPTIONS; i++) optBtns[i] = NULL;
    optCount = 0;
    if (opPending) sdo_client_op_clear();  // no-op if still in flight; result is simply not shown
    opPending = false;
}
