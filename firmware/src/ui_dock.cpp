// ui_dock.cpp - see ui_dock.h for scope/rationale.
#include "ui_dock.h"
#include "ui.h"

#define UI_DOCK_W       800
#define UI_DOCK_BTN_W   (UI_DOCK_W / 5)
#define UI_DOCK_ICON_Y  6
#define UI_DOCK_LABEL_Y 44

typedef struct {
    ui_dock_dest_t dest;
    const char *icon;
    const char *label;
    lv_obj_t **screenPtr;
    void (*initFn)(void);
} ui_dock_entry_t;

// Fixed, shared across every dock instance - the destination table doesn't
// change per screen, only which entry is "active" does (a per-instance
// param, not baked into this table). See ui_dock.h's mapping comment for
// why each group targets the ONE screen listed here, not its siblings.
static const ui_dock_entry_t UI_DOCK_ENTRIES[5] = {
    { UI_DOCK_TELEMETRY, DOCK_ICON_HOME,         "TELEMETRY", &ui_speedScreen,    &ui_speedScreen_screen_init },
    { UI_DOCK_BMS,       DOCK_ICON_BATTERY_FULL, "BMS",       &ui_batteryScreen,  &ui_batteryScreen_screen_init },
    { UI_DOCK_GPS,       DOCK_ICON_MAP,          "GPS",       &ui_gpsScreen,      &ui_gpsScreen_screen_init },
    { UI_DOCK_DYNO,      DOCK_ICON_BOLT,         "DYNO",      &ui_dynoLiveScreen, &ui_dynoLiveScreen_screen_init },
    { UI_DOCK_SETTINGS,  DOCK_ICON_SETTINGS,     "SETTINGS",  &ui_settingsScreen, &ui_settingsScreen_screen_init },
};

static void setButtonState(lv_obj_t *btn, lv_obj_t *iconLabel, lv_obj_t *textLabel, bool active) {
    if (active) {
        // Tinted background AND icon/label color - "not just text color"
        // per the confirmed spec, so the active state reads even at a
        // glance/peripheral-vision distance while driving.
        lv_color_t tint = lv_color_mix(ui_theme_accent(), ui_theme_panel_bg(), 60);
        lv_obj_set_style_bg_color(btn, tint, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(iconLabel, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(textLabel, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_bg_opa(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(iconLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(textLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void dockBtnClickedCb(lv_event_t *e) {
    const ui_dock_entry_t *entry = (const ui_dock_entry_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *dockBar = lv_obj_get_parent(btn);
    ui_dock_dest_t activeDest = (ui_dock_dest_t)(intptr_t)lv_obj_get_user_data(dockBar);

    if (entry->dest == activeDest) return;  // already in this group - dock only does cross-group jumps

    // Deliberately NOT calling _ui_screen_delete() on the outgoing screen
    // here, unlike every swipe handler's own gesture callback - that call
    // is a documented permanent no-op in this codebase (inverted condition,
    // see ui_helpers.h), so omitting it here changes nothing functionally;
    // it's only ever called at the swipe site because that's where the
    // outgoing screen's own pointer is already in scope, which a generic
    // dock handler doesn't have without extra bookkeeping this doesn't need.
    _ui_screen_change(entry->screenPtr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, entry->initFn);
}

lv_obj_t *ui_dock_create(lv_obj_t *screenParent, ui_dock_dest_t activeDest) {
    lv_obj_t *dock = lv_obj_create(screenParent);
    lv_obj_clear_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dock, UI_DOCK_W, UI_DOCK_H);
    lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(dock, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(dock, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dock, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(dock, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dock, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dock, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dock, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Stashes this instance's fixed activeDest for dockBtnClickedCb()'s
    // no-op check - see ui_dock.h's comment for why one dock per screen
    // rather than one shared instance.
    lv_obj_set_user_data(dock, (void *)(intptr_t)activeDest);

    for (int i = 0; i < 5; i++) {
        const ui_dock_entry_t *entry = &UI_DOCK_ENTRIES[i];
        bool active = (entry->dest == activeDest);

        lv_obj_t *btn = lv_obj_create(dock);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(btn, UI_DOCK_BTN_W, UI_DOCK_H);
        lv_obj_set_pos(btn, i * UI_DOCK_BTN_W, 0);
        lv_obj_set_style_radius(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *iconLabel = lv_label_create(btn);
        lv_obj_align(iconLabel, LV_ALIGN_TOP_MID, 0, UI_DOCK_ICON_Y);
        lv_label_set_text(iconLabel, entry->icon);
        lv_obj_set_style_text_font(iconLabel, &font_dock_icons_32, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *textLabel = lv_label_create(btn);
        lv_obj_align(textLabel, LV_ALIGN_TOP_MID, 0, UI_DOCK_LABEL_Y);
        lv_label_set_text(textLabel, entry->label);
        lv_obj_set_style_text_font(textLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

        setButtonState(btn, iconLabel, textLabel, active);

        // >=64x64 touch target per the confirmed spec - this button is
        // UI_DOCK_BTN_W (160) x UI_DOCK_H (72), well over that minimum, so
        // no separate invisible hit-area padding is needed the way some
        // smaller controls elsewhere in this codebase need.
        ui_press_feedback_attach(btn);
        lv_obj_add_event_cb(btn, dockBtnClickedCb, LV_EVENT_CLICKED, (void *)entry);
    }

    return dock;
}

void ui_dock_refresh_theme(lv_obj_t *dock, ui_dock_dest_t activeDest) {
    if (!dock) return;
    lv_obj_set_style_border_color(dock, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dock, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    uint32_t childCount = lv_obj_get_child_cnt(dock);
    for (uint32_t i = 0; i < childCount && i < 5; i++) {
        lv_obj_t *btn = lv_obj_get_child(dock, (int32_t)i);
        if (!btn) continue;
        lv_obj_t *iconLabel = lv_obj_get_child(btn, 0);
        lv_obj_t *textLabel = lv_obj_get_child(btn, 1);
        if (!iconLabel || !textLabel) continue;
        setButtonState(btn, iconLabel, textLabel, UI_DOCK_ENTRIES[i].dest == activeDest);
    }
}
