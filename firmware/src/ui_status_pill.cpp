// ui_status_pill.cpp - see ui_status_pill.h for scope/rationale.
#include "ui_status_pill.h"
#include "ui_theme.h"

static lv_color_t colorForState(ui_pill_state_t state) {
    switch (state) {
        case UI_PILL_ACTIVE:  return ui_theme_good();
        case UI_PILL_CAUTION: return ui_theme_bad();
        case UI_PILL_FAULT:   return ui_theme_warning();
        case UI_PILL_NEUTRAL:
        default:              return ui_theme_panel_border();
    }
}

lv_obj_t *ui_pill_create(lv_obj_t *parent, int16_t w, int16_t h, int16_t radius,
                          lv_obj_t **outLabel, const lv_font_t *labelFont) {
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pill, w, h);
    lv_obj_set_style_radius(pill, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(pill, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(pill, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Shared 400ms ease-out bg-color fade - previously copy-pasted
    // verbatim at 3 of the 5 pill call sites, missing from 2 (Speed's
    // shifter pill, Dyno's state pill), which snapped instantly. ONE
    // function-local static pair, shared by every pill this function ever
    // creates (LVGL styles are independent of any one object's lifetime -
    // lv_obj_add_style() just stores a pointer into the object's style
    // list, so many objects referencing the same static style is the
    // normal LVGL pattern, not a bug) - initialized once on the first
    // call, same "screens are never destroyed" static-storage convention
    // already used throughout this codebase (see ui_helpers.h's
    // _ui_screen_delete note), just consolidated from N copies to 1.
    static const lv_style_prop_t pillTransProps[] = { LV_STYLE_BG_COLOR, (lv_style_prop_t)0 };
    static lv_style_transition_dsc_t pillTransDsc;
    static lv_style_t pillTransStyle;
    static bool pillTransInit = false;
    if (!pillTransInit) {
        lv_style_transition_dsc_init(&pillTransDsc, pillTransProps, lv_anim_path_ease_out, 400, 0, NULL);
        lv_style_init(&pillTransStyle);
        lv_style_set_transition(&pillTransStyle, &pillTransDsc);
        pillTransInit = true;
    }
    lv_obj_add_style(pill, &pillTransStyle, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(pill);
    lv_obj_center(label);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_color(label, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (labelFont) lv_obj_set_style_text_font(label, labelFont, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outLabel) *outLabel = label;

    return pill;
}

void ui_pill_setState(lv_obj_t *pill, lv_obj_t *label, ui_pill_state_t state, const char *text) {
    if (pill) lv_obj_set_style_bg_color(pill, colorForState(state), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (label && text) lv_label_set_text(label, text);
}

void ui_pill_refreshTheme(lv_obj_t *pill, ui_pill_state_t state) {
    if (!pill) return;
    lv_obj_set_style_bg_color(pill, colorForState(state), LV_PART_MAIN | LV_STATE_DEFAULT);
}
