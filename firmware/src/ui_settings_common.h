#pragma once

#include "ui.h"

// Shared between ui_settingsScreen.cpp and the Settings sub-pages that live
// in their own files (2026-10-21: ui_settingsVcuParams.cpp,
// ui_settingsDiagnostics.cpp, ui_settingsLogging.cpp) - so every page uses
// the same row geometry, stepper widget, and lv_menu contrast fix rather
// than each re-deriving them. Definitions live in ui_settingsScreen.cpp.

// See ui_settingsScreen.cpp's comment on these (2026-09-26 design pass):
// STEPPER_BTN is CLAUDE.md's 80px touch-target floor - never go below it.
#define ROW_HEIGHT    88
#define STEPPER_BTN   80

// Safety-critical confirmation buttons (CLAUDE.md: ~130px).
#define CONFIRM_BTN_H 130

// "Title" [-] [spinbox] [+] row. Children: [0]=label, [1]=minus,
// [2]=spinbox, [3]=plus - callers wire the +/- buttons themselves.
// intDigits = digits BEFORE the decimal point (LVGL's separator_position),
// 0 = no decimal point. NOT the number of decimals: 280.0 is (4, 3).
lv_obj_t *ui_settings_create_stepper_row(lv_obj_t *section, const char *title,
                                         int32_t rangeMin, int32_t rangeMax, int32_t initialValue,
                                         uint8_t digitCount, uint8_t intDigits);

// Explicit day/night colors for lv_menu's stock row/section containers -
// see ui_settingsScreen.cpp for why lv_menu needs this.
void ui_settings_style_row(lv_obj_t *row);
void ui_settings_style_section(lv_obj_t *section);

// Each sub-page module's lifecycle, driven by ui_settingsScreen.cpp:
//   *_create_page(menu)   build the page(s) once, at screen init
//   *_page_leave(page)    the menu just moved away from its page(s)
//                         (page == NULL: the whole Settings screen unloaded)
//   *_page_enter(page)    the menu is now showing `page` - a no-op unless
//                         it's one of this module's own
// leave() is always called for every module before any enter(), so a page
// that starts SDO polling never has it cancelled by another page's leave.
lv_obj_t *ui_settingsVcuParams_create_page(lv_obj_t *menu);
void ui_settingsVcuParams_page_leave(lv_obj_t *page);
void ui_settingsVcuParams_page_enter(lv_obj_t *page);
void ui_settingsVcuParams_refresh_theme(void);

lv_obj_t *ui_settingsDiagnostics_create_page(lv_obj_t *menu);
void ui_settingsDiagnostics_page_leave(lv_obj_t *page);
void ui_settingsDiagnostics_page_enter(lv_obj_t *page);
void ui_settingsDiagnostics_refresh_theme(void);

lv_obj_t *ui_settingsCanMonitor_create_page(lv_obj_t *menu);
void ui_settingsCanMonitor_page_leave(lv_obj_t *page);
void ui_settingsCanMonitor_page_enter(lv_obj_t *page);
void ui_settingsCanMonitor_refresh_theme(void);

// Road-speed calibration rows, built INTO the existing Calibration page
// (ui_settingsSpeedCal.cpp, 2026-10-24) - so build() takes the page and
// section rather than creating its own.
void ui_settingsSpeedCal_build(lv_obj_t *page, lv_obj_t *section);
void ui_settingsSpeedCal_page_leave(lv_obj_t *page);
void ui_settingsSpeedCal_page_enter(lv_obj_t *page);
void ui_settingsSpeedCal_refresh_theme(void);

lv_obj_t *ui_settingsLogging_create_page(lv_obj_t *menu);
void ui_settingsLogging_page_leave(lv_obj_t *page);
void ui_settingsLogging_page_enter(lv_obj_t *page);
void ui_settingsLogging_refresh_theme(void);
