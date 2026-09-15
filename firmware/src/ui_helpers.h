#ifndef _SQUARELINE_PROJECT_UI_HELPERS_H
#define _SQUARELINE_PROJECT_UI_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ui.h"

// Originally SquareLine-Studio-generated (SquareLine Studio 1.5.0, LVGL
// 8.3.11) with ~40 helper functions covering every widget type SquareLine's
// codegen can emit (dropdown/roller/textarea/keyboard property setters,
// slider/arc "set_text_value" helpers, a full animation-timeline callback
// set, flag/state modifiers, etc). Trimmed 2026-09-14 to just the 3 actually
// called anywhere in this hand-written UI (every screen in this project is
// hand-written per CLAUDE.md, not SquareLine-round-tripped, so the rest was
// 100% dead code - confirmed via a project-wide grep before removing, not
// assumed). If you ever bring SquareLine codegen back for a new screen, the
// full original file is in git history (see waveshare-dash-build.md's
// cleanup-pass entry for this date).

// Lazily creates *target via target_init() the first time this screen is
// navigated to, then plays fademode. Also calls ui_notify_screen_created()
// on first creation - see its comment in zombie_updaters.h for why (forces
// the next widget-update tick to push current values into every widget on
// the freshly-created screen, bypassing the dirty check).
void _ui_screen_change(lv_obj_t ** target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void));

// NOTE: this has a long-standing inverted condition (should be
// `if (*target != NULL)`) inherited from the original SquareLine codegen,
// so it never actually frees a screen - see waveshare-dash-build.md's
// milestone-3 entry for why this is left as-is (every visited screen
// staying resident is empirically safe at the current screen count).
void _ui_screen_delete(lv_obj_t ** target);

// +/-1 step on an lv_spinbox, used by the settings screen's warning-
// threshold steppers.
void _ui_spinbox_step(lv_obj_t * target, int val);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
