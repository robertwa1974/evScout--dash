// ui_empty_state.cpp - see ui_empty_state.h for scope/rationale.
#include "ui_empty_state.h"
#include "ui_theme.h"
#include "ui.h"

lv_obj_t *ui_empty_state_create(lv_obj_t *parent, const char *icon, const char *message) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    if (icon) {
        lv_obj_t *iconLabel = lv_label_create(cont);
        lv_label_set_text(iconLabel, icon);
        lv_obj_set_style_text_color(iconLabel, ui_theme_dim(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(iconLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(iconLabel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_t *msgLabel = lv_label_create(cont);
    lv_label_set_long_mode(msgLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msgLabel, 300);
    lv_obj_set_style_text_align(msgLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(msgLabel, message);
    lv_obj_set_style_text_color(msgLabel, ui_theme_dim(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(msgLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    return cont;
}

void ui_empty_state_setVisible(lv_obj_t *emptyState, bool visible) {
    if (!emptyState) return;
    if (visible) lv_obj_clear_flag(emptyState, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(emptyState, LV_OBJ_FLAG_HIDDEN);
}
