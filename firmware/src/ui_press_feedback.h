#pragma once

// Shared touch press-feedback animation (styling/UX pass, 2026-09-18,
// item 17). This board has no haptics and audio feedback is out of
// scope - a brief scale-down on press is the ONLY confirmation a driver
// gets that a tap registered, which matters more here than on a phone:
// this dash runs in an off-road vehicle with real vibration, where a
// touch can land slightly off-target or bounce, and "did that actually
// register" is a real question worth answering visually.
//
// Confirmed nothing like this existed anywhere in this codebase before -
// zero transform_width/transform_height usage, and the one existing
// LV_EVENT_PRESSED handler (ui_settingsScreen.cpp's spinbox steppers)
// only resets a long-press repeat counter, no visual feedback at all.
//
// Uses LV_STYLE_TRANSFORM_ZOOM (LVGL 8.x's uniform-scale style prop,
// 256=100%), NOT transform_width/height - zoom scales around a pivot
// point in one call; width/height would need separate x/y math to stay
// centered and don't antialias the same way.

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Attaches press/release feedback to any clickable lv_obj: scales down to
// ~95% (zoom 244/256) over ~100ms on LV_EVENT_PRESSED, scales back to
// 100% on LV_EVENT_RELEASED or LV_EVENT_PRESS_LOST (a touch that slides
// off the object or gets interrupted still needs to visually "let go",
// not stay shrunk). Idempotent to call multiple times on the same object
// (LVGL just adds another event callback) - don't call twice on purpose,
// but doing so isn't harmful beyond a redundant animation restart.
void ui_press_feedback_attach(lv_obj_t *target);

#ifdef __cplusplus
}
#endif
