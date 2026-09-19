// ui_fault_banner.cpp - see ui_fault_banner.h for scope/rationale.
#include "ui_fault_banner.h"
#include "ui.h"

#define BANNER_H 40

static lv_obj_t * banner = NULL;
static lv_obj_t * bannerLabel = NULL;
static ui_fault_t currentState = UI_FAULT_NONE;

// Matches zombieverter_params.h's "lasterr" SDO comment exactly: "0=NONE,
// 1=BMSCOMM, 2=OVERVOLTAGE, 3=PRECHARGE, 4=THROTTLE1, 5=THROTTLE2,
// 6=THROTTLE12, 7=THROTTLE12DIFF, 8=THROTTLEMODE, 9=CANTIMEOUT,
// 10=TMPHSMAX, 11=TMPMMAX" - index 0 (NONE) is never looked up here since
// callers only reach the VCU_ERROR branch when lastErr != 0.
static const char *lastErrText(int lastErr) {
    switch (lastErr) {
        case 1:  return "BMS COMM FAULT";
        case 2:  return "OVERVOLTAGE FAULT";
        case 3:  return "PRECHARGE FAULT";
        case 4:  return "THROTTLE 1 FAULT";
        case 5:  return "THROTTLE 2 FAULT";
        case 6:  return "THROTTLE 1/2 FAULT";
        case 7:  return "THROTTLE 1/2 MISMATCH";
        case 8:  return "THROTTLE MODE FAULT";
        case 9:  return "CAN TIMEOUT FAULT";
        case 10: return "HEATSINK OVERTEMP FAULT";
        case 11: return "MOTOR OVERTEMP FAULT";
        default: return "VCU FAULT";
    }
}

static void applyState(ui_fault_t state, const char *text) {
    currentState = state;
    if (state == UI_FAULT_NONE) {
        lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_color_t color = (state == UI_FAULT_CAN_DROPOUT) ? ui_theme_bad() : ui_theme_warning();
    lv_obj_set_style_bg_color(banner, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(bannerLabel, text);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(banner);
}

void ui_fault_banner_init(void) {
    banner = lv_obj_create(lv_layer_top());
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(banner, 800, BANNER_H);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(banner, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(banner, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(banner, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Defensive fix alongside the same bug found in ui_shutdown.cpp's
    // overlay - lv_theme_default's non-zero default padding on
    // lv_layer_top() (this object's parent) would otherwise nudge this
    // banner off the true top edge.
    lv_obj_set_style_pad_all(banner, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);

    bannerLabel = lv_label_create(banner);
    lv_obj_center(bannerLabel);
    lv_label_set_text(bannerLabel, "");
    lv_obj_set_style_text_color(bannerLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bannerLabel, &font_montserrat_semibold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_fault_banner_update(bool canDropout, bool overTemp, int lastErr) {
    if (banner == NULL) return;  // ui_fault_banner_init() hasn't run yet - no-op, same convention as every other <screen>_setX() guard in this codebase

    ui_fault_t newState;
    const char *text = "";
    if (lastErr != 0) {
        newState = UI_FAULT_VCU_ERROR;
        text = lastErrText(lastErr);
    } else if (overTemp) {
        newState = UI_FAULT_OVERTEMP;
        text = LV_SYMBOL_WARNING " OVERTEMP WARNING";
    } else if (canDropout) {
        newState = UI_FAULT_CAN_DROPOUT;
        text = LV_SYMBOL_WARNING " CAN SIGNAL LOST";
    } else {
        newState = UI_FAULT_NONE;
    }

    if (newState == currentState && newState != UI_FAULT_VCU_ERROR) return;  // no visible change - VCU_ERROR re-applies every call since the specific fault code can change without the enum tier changing
    applyState(newState, text);
}
