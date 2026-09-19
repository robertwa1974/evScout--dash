#pragma once

// Shared "designed empty state" builder (styling/UX pass, 2026-09-18,
// item 16). Replaces raw debug-console-style strings ("NO FIX", "NO
// ROUTE", "--:--", "~%.0fm across - no map tiles yet") with a centered
// icon + one clean, user-facing sentence, wherever a screen is genuinely
// waiting for data rather than displaying a real (if boring) value.
//
// Deliberately NOT used for "the value is legitimately 0/idle" states
// (e.g. speed=0 while stopped) - that's real data, not absence of data.
// This is for "nothing has arrived to show yet" specifically, same
// distinction ui_can_freshness.h draws between hasEverReceived() and a
// genuine zero reading.

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Builds a small container (icon above a wrapped, centered sentence) as
// a child of parent, styled in ui_theme_dim() - same "this is absence,
// not a value" visual language as ui_card_grid.h's no-data state. icon is
// a UTF-8 byte-string literal (an ICON_*/DOCK_ICON_*-style macro, or a
// plain LV_SYMBOL_*) - pass NULL for a text-only empty state where no
// relevant icon exists. Caller owns positioning via lv_obj_align()/
// lv_obj_set_pos() on the returned container, same as any other widget
// this codebase builds.
lv_obj_t *ui_empty_state_create(lv_obj_t *parent, const char *icon, const char *message);

// Toggles visibility without destroying/rebuilding - call once real data
// arrives (or is lost again) rather than churning the widget tree.
void ui_empty_state_setVisible(lv_obj_t *emptyState, bool visible);

#ifdef __cplusplus
}
#endif
