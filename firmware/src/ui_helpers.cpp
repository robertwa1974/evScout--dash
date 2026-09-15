// Originally SquareLine-Studio-generated - see ui_helpers.h for the
// 2026-09-14 cleanup-pass note on why this was trimmed from ~40 functions
// down to the 3 actually used anywhere in this project.
//
// Renamed .c -> .cpp (2026-09-14, same reason as ui_theme.cpp/
// ui_settingsScreen.cpp before it): _ui_screen_change() now calls
// ui_notify_screen_created(), which needs zombie_updaters.h, which pulls in
// C++-only headers (Ticker.h/Preferences.h). ui_helpers.h's declarations
// stay extern "C", so every .c screen file that calls
// _ui_screen_change()/_ui_screen_delete() needs no changes.

#include "ui_helpers.h"
#include "zombie_updaters.h"  // ui_notify_screen_created()

void _ui_screen_change(lv_obj_t ** target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void))
{
    if(*target == NULL) {
        target_init();
        // A screen was just created for the first time - force the next
        // fastUpdate/midUpdate/slowUpdate tick to push every field to
        // every currently-existing widget, bypassing the dirty check, so
        // this freshly-created screen's widgets get seeded with the
        // current value even if that field hasn't changed since the last
        // poll (see uiGeneration's comment in zombie_updaters.h - this is
        // the fix for the "Motor Mode never shows up" bug found on the
        // bench 2026-09-14).
        ui_notify_screen_created();
    }
    lv_scr_load_anim(*target, fademode, spd, delay, false);
}

void _ui_screen_delete(lv_obj_t ** target)
{
    if(*target == NULL) {
        lv_obj_del(*target);
        target = NULL;
    }
}

void _ui_spinbox_step(lv_obj_t * target, int val)
{
    if(val > 0) lv_spinbox_increment(target);
    else lv_spinbox_decrement(target);

    lv_event_send(target, LV_EVENT_VALUE_CHANGED, 0);
}
