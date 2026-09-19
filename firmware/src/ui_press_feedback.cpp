// ui_press_feedback.cpp - see ui_press_feedback.h for scope/rationale.
#include "ui_press_feedback.h"

#define UI_PRESS_ZOOM_NORMAL 256  // 100%, LVGL 8.x transform_zoom scale
#define UI_PRESS_ZOOM_PRESSED 244  // ~95%
#define UI_PRESS_ANIM_MS 100

static void setZoom(void *obj, int32_t v) {
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, (int16_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void animateZoomTo(lv_obj_t *target, int32_t to) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_exec_cb(&a, setZoom);
    lv_anim_set_values(&a, lv_obj_get_style_transform_zoom(target, LV_PART_MAIN), to);
    lv_anim_set_time(&a, UI_PRESS_ANIM_MS);
    lv_anim_start(&a);
}

static void pressFeedbackEventCb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_PRESSED) {
        animateZoomTo(target, UI_PRESS_ZOOM_PRESSED);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        animateZoomTo(target, UI_PRESS_ZOOM_NORMAL);
    }
}

void ui_press_feedback_attach(lv_obj_t *target) {
    if (!target) return;
    // Scale from the object's own center, not its top-left corner - must
    // be set before the first press, and needs the object's real size
    // already assigned (LV_PCT resolves against the object's own current
    // size at animation time, so callers should attach this AFTER
    // lv_obj_set_size(), same ordering every existing call site already
    // uses for other size-dependent style calls).
    lv_obj_set_style_transform_pivot_x(target, lv_pct(50), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_pivot_y(target, lv_pct(50), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(target, pressFeedbackEventCb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(target, pressFeedbackEventCb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(target, pressFeedbackEventCb, LV_EVENT_PRESS_LOST, NULL);
}
