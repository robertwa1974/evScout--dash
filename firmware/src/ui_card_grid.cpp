// ui_card_grid.cpp - see ui_card_grid.h for scope/rationale.
#include "ui_card_grid.h"
#include "ui.h"

lv_obj_t *ui_card_createPanel(lv_obj_t *parent, int16_t x, int16_t y,
                               const char *title, lv_obj_t **outTitleLabel) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, GRID_PANEL_W, GRID_PANEL_H);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *titleLabel = lv_label_create(panel);
    lv_obj_set_pos(titleLabel, 12, 8);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_color(titleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(titleLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (outTitleLabel) *outTitleLabel = titleLabel;
    return panel;
}

void ui_card_createValueAndUnit(lv_obj_t *panel, lv_obj_t **outVal,
                                 lv_obj_t **outUnit, const char *unitText) {
    lv_obj_t *val = lv_label_create(panel);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -8);
    lv_label_set_text(val, "0");
    lv_obj_set_style_text_color(val, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(val, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outVal) *outVal = val;

    lv_obj_t *unit = lv_label_create(panel);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 28);
    lv_label_set_text(unit, unitText);
    lv_obj_set_style_text_color(unit, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(unit, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outUnit) *outUnit = unit;
}

// See ui_card_grid.h's comment on ui_card_setZoneGradient() for the
// zoneDir semantics and why the color is a 35%-strength blend rather than
// full saturation - ported verbatim from ui_statusScreen.c's original
// setZoneGradient(), only the flat/zoneDir==0 early-return behavior is
// unchanged (leaves whatever bg_color the caller already set, i.e. the
// plain ui_theme_bg() track from ui_card_createBar()).
void ui_card_setZoneGradient(lv_obj_t *bar, int zoneDir) {
    if (zoneDir == 0) return;
    lv_color_t bg = ui_theme_bg();
    lv_color_t safe   = lv_color_mix(ui_theme_good(), bg, 90);
    lv_color_t danger = lv_color_mix(ui_theme_warning(), bg, 90);
    lv_obj_set_style_bg_color(bar, zoneDir > 0 ? safe : danger, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bar, zoneDir > 0 ? danger : safe, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t *ui_card_createBar(lv_obj_t *panel, bool symmetrical,
                             int32_t rangeMin, int32_t rangeMax, int zoneDir) {
    lv_obj_t *bar = lv_bar_create(panel);
    if (symmetrical) lv_bar_set_mode(bar, LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_range(bar, rangeMin, rangeMax);
    lv_bar_set_value(bar, symmetrical ? 0 : rangeMin, LV_ANIM_OFF);
    lv_obj_set_size(bar, 320, 14);
    // +48, not the original +58 - GRID_PANEL_H shrank from 149 to 124 for
    // the bottom nav dock (see ui_card_grid.h's comment); +58 against the
    // new shorter panel would put the bar's bottom edge past the panel's
    // own bottom edge. +48 keeps a ~7px margin (panel center is 62, bar
    // center lands at 110, half-height 7 -> spans 103-117, inside 0-124).
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 48);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bar, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_card_setZoneGradient(bar, zoneDir);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return bar;
}

void ui_card_setBarNoData(lv_obj_t *bar, bool noData, int zoneDir) {
    if (!bar) return;
    if (noData) {
        lv_color_t dim = ui_theme_dim();
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bar, dim, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bar, dim, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    } else {
        ui_card_setZoneGradient(bar, zoneDir);
        if (zoneDir == 0) lv_obj_set_style_bg_color(bar, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(bar, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }
}

void ui_card_setValueNoData(lv_obj_t *val, lv_obj_t *unit, bool noData) {
    lv_color_t dim = ui_theme_dim();
    if (val) lv_obj_set_style_text_color(val, noData ? dim : ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (unit) lv_obj_set_style_text_color(unit, noData ? dim : ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
}
