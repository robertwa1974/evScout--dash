#pragma once

// Day/night palette system for the EV dash. NOT SquareLine's own theme
// mechanism - ui_helpers.cpp used to carry a dead _ui_switch_theme()/
// UI_THEME_ACTIVE stub left over from SquareLine's "Themes" project
// feature, removed in the 2026-09-14 cleanup pass (see
// waveshare-dash-build.md) since the generated ui_theme_set() it expected
// was never exported into this repo. This is a small, independent
// replacement: each screen queries the color getters below while building
// its widgets, and calls its own <screen>_refresh_theme() (declared in
// that screen's header) to repaint on a live toggle.

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    UI_THEME_DAY,
    UI_THEME_NIGHT,
} ui_theme_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

// Call once at boot, before any screen is created (setup(), before ui_init()).
void ui_theme_init(void);

// Sets the active palette, applies that mode's remembered backlight level
// (see ui_theme_set_brightness()/ui_theme_load_brightness() below - day and
// night each remember their own level, so switching modes doesn't clobber
// a brightness the user dialed in), and calls <screen>_refresh_theme() for
// every screen that currently exists (guarded, since most screens are only
// created lazily on first navigation - see ui.c / _ui_screen_change).
// Does NOT persist to NVS - callers that want that call updateDisplayMode()
// (zombie_updaters.h) afterwards, same as warningSet's save-on-demand
// pattern.
void ui_theme_set(ui_theme_mode_t mode);

ui_theme_mode_t ui_theme_get(void);

// Per-mode backlight level (55-255, clamped like setBrightness()). Day and
// night each remember their own level independently - e.g. the settings
// screen's brightness slider and uaDASH's swipe-up/down gesture
// (events.cpp upBrightness/downBribrightness) both adjust "whichever mode
// is active right now" via ui_theme_set_brightness(), and it sticks when
// you switch back to that mode later instead of being overwritten by a
// fixed day/night default.
void ui_theme_set_brightness(uint8_t val);   // sets + applies immediately for the current mode
uint8_t ui_theme_get_brightness(void);       // current mode's remembered level
uint8_t ui_theme_get_day_brightness(void);   // for persistence (zombie_updaters.h updateDisplayMode) -
uint8_t ui_theme_get_night_brightness(void); // independent of which mode is active right now
void ui_theme_load_brightness(uint8_t dayVal, uint8_t nightVal);  // seed both from NVS at boot, before ui_theme_set()

// Palette colors for the CURRENT mode. Structural/static widgets (screen
// background, panel background/border, titles, units) should pull these at
// creation time AND again from <screen>_refresh_theme(). Dynamic, data-driven
// colors (over-threshold warning red, charging/discharging status) are NOT
// part of the palette - they stay direct lv_obj_set_style_text_color() calls
// in zombie_updaters.cpp / the BMS status pill, using ui_theme_warning()/
// ui_theme_good()/ui_theme_bad() as the color source so they still shift
// with day/night, while their on/off logic stays data-driven.
lv_color_t ui_theme_bg(void);
lv_color_t ui_theme_panel_bg(void);
lv_color_t ui_theme_panel_border(void);
lv_color_t ui_theme_text_primary(void);
lv_color_t ui_theme_text_secondary(void);
lv_color_t ui_theme_accent(void);
// Distinct gold/amber accent for the two energy-identity screens (Battery,
// Charging) - design-review "semantic color palette" pass, 2026-09-14 (see
// waveshare-dash-build.md). Identical to ui_theme_accent() at night on
// purpose (preserving the night palette's amber-only dark-adaptation
// discipline) - the hue distinction is Day-mode-only. Every other screen
// keeps using ui_theme_accent() unchanged; this was deliberately NOT
// rolled out everywhere, only to the two screens whose entire identity is
// energy/battery.
lv_color_t ui_theme_accent_energy(void);
lv_color_t ui_theme_warning(void);
lv_color_t ui_theme_good(void);
lv_color_t ui_theme_bad(void);

#ifdef __cplusplus
}
#endif
