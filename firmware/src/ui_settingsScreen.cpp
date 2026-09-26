// ============================================================================
// ui_settingsScreen.c - lv_menu settings screen (rebuilt 2026-09-13)
// ============================================================================
// Hand-written, no SquareLine project (same situation as ui_mainScreen.c/
// ui_bmsScreen.c). Replaces the old rusEFI warning-stepper screen entirely
// (milestone-1 decision, 2026-09-06: "leave settings for the redesign" -
// this is that redesign). Per CLAUDE.md:
//
//   lv_menu with 3 lv_menu_section groups:
//     1. WARNING THRESHOLDS - low SOC / high motor temp / high heatsink
//        temp / low pack voltage, each an lv_spinbox driven by +/- buttons
//        (never direct tap-to-type - no keyboard popup while driving).
//        Explicit Save button, dirty-state highlighted, lv_msgbox
//        confirmation, NVS write only on Save.
//     2. DISPLAY - day/night (Auto/Day/Night, applied+persisted live),
//        brightness (a real lv_slider - the "never lv_slider for read-only
//        telemetry" rule doesn't apply to a direct input control), UTC
//        offset, and a WiFi Config toggle (2026-09-22 - see
//        wifi_config_server.h: a local-only AP + web page for editing
//        vehicle_config.h's values without a reflash).
//     3. CALIBRATION - dyno workflow entry point (stub - the dyno screens
//        haven't been built yet).
//
// Touch targets: buttons are 84px (>=80px minimum per CLAUDE.md).
// Navigation (styling/UX pass Phase 5, 2026-09-18): physical swipe RIGHT
// to Speed is REMOVED - Settings is a standalone dock destination now
// (see ui_dock.h's mapping comment and the plan's swipe-removal table).
// Physical swipe LEFT to Splash/Clock STAYS - Splash sits outside the dock
// system entirely (confirmed with Rob), so this bookend link is
// unaffected by the dock restructure. This board reports gesture
// direction inverted from the physical swipe - see CLAUDE.md's "Touch
// gesture direction" - so the code checks LV_DIR_RIGHT for the physical-
// LEFT swipe. Intentional; don't "fix" it.
// ============================================================================

#include "zombie_updaters.h"  // warningSet, updateWarningsSet, displayPref, applyDisplayMode - includes ui.h
#include "build_info.h"
#include "wifi_config_server.h"

lv_obj_t * ui_settingsScreen = NULL;

static lv_obj_t * menu = NULL;
// Kept as statics (not screen_init() locals) so the contrast fix below -
// see styleMenuSection()'s comment - can re-apply explicit colors from
// refresh_theme() too, not just at creation time.
//
// Sub-page split (2026-09-26, design pass): this screen used to be one
// shared lv_menu page holding all three sections stacked, requiring a lot
// of scrolling to reach Display/Calibration. settingsPage is now the
// TOP-LEVEL row list (Warning Thresholds / Display / Calibration, each a
// tap target); thPage/dispPage/calPage are each section's own dedicated
// page, reached via lv_menu_set_load_page_event() - LVGL's own back-button/
// history mechanism (lv_menu_get_main_header_back_btn()) handles returning
// to this list, no custom back-navigation code needed. rootSection is the
// list's own lv_menu_section_create() (for styleMenuSection() theming,
// same as the other three).
static lv_obj_t * settingsPage = NULL;
static lv_obj_t * rootSection = NULL;
static lv_obj_t * thPage = NULL;
static lv_obj_t * dispPage = NULL;
static lv_obj_t * calPage = NULL;
static lv_obj_t * thSection = NULL;
static lv_obj_t * dispSection = NULL;
static lv_obj_t * calSection = NULL;
static lv_obj_t * fwRow = NULL;

// Warning-threshold spinboxes - read back on Save, written on Default.
static lv_obj_t * socSpinbox = NULL;
static lv_obj_t * motorTSpinbox = NULL;
static lv_obj_t * heatsinkTSpinbox = NULL;
static lv_obj_t * packVSpinbox = NULL;

static lv_obj_t * saveBtn = NULL;
static lv_obj_t * saveBtnLabel = NULL;
static bool settingsDirty = false;

// Applies + persists immediately, like Day/Night below - NOT gated behind
// the Save button above (see zombie_updaters.h's comment on
// lowVoltageShutdownEnabled for why this setting exists).
static lv_obj_t * lowVoltShutdownSwitch = NULL;

static lv_obj_t * autoBtn = NULL;
static lv_obj_t * dayBtn = NULL;
static lv_obj_t * nightBtn = NULL;
static lv_obj_t * brightnessSlider = NULL;

// UTC offset stepper (2026-09-22) - applies + persists immediately, same
// "live preview" convention as Day/Night and Brightness above (not gated
// behind the Warning Thresholds section's Save button, even though it's
// built with the same createThresholdRow() widget helper for visual
// consistency - see utcOffsetStepperCb() for why it needs its own wiring
// instead of reusing wireThresholdRow()/stepperEventCb()).
static lv_obj_t * utcOffsetSpinbox = NULL;

// WiFi Config toggle (2026-09-22) - applies + persists immediately, same
// convention as Day/Night/Brightness/UTC Offset above. Off by default; the
// AP only broadcasts while this is explicitly on (wifi_config_server.h).
static lv_obj_t * wifiConfigSwitch = NULL;
static lv_obj_t * wifiConfigStatusLabel = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Settings is its own standalone dock group.
static lv_obj_t * ui_settingsScreenDock = NULL;

// Shrunk 2026-09-26 (design pass - see waveshare-dash-build.md/CLAUDE.md's
// note on this screen never having had one before): ROW_HEIGHT 100->88,
// STEPPER_BTN 84->80 (CLAUDE.md's stated touch-target floor, not below it) -
// the threshold rows read as visually lighter without dropping under the
// safety-minimum tap size. Interaction model unchanged (+/- stepper, no
// keypad) - CLAUDE.md documents that as a deliberate driving-safety choice,
// not an oversight, so it stays even though the buttons are getting smaller.
#define ROW_HEIGHT    88
#define STEPPER_BTN   80

// Shared by every +/- stepper button on this screen - only one can be held
// at a time on a single-touch panel, so one counter is enough. Reset on
// PRESSED, grows on each LONG_PRESSED_REPEAT, scales the step size (not the
// repeat rate, which LVGL fixes globally) - "accelerating hold" per
// CLAUDE.md without a custom timer.
static int repeatCount = 0;

static void updateSaveButtonState(void) {
    if (!saveBtn) return;
    if (settingsDirty) {
        lv_obj_clear_state(saveBtn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(saveBtn, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(saveBtnLabel, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_add_state(saveBtn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(saveBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(saveBtnLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void markDirty(void) {
    settingsDirty = true;
    updateSaveButtonState();
}

void ui_event_settingsScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    // Physical-RIGHT swipe to Speed REMOVED here - see this file's header
    // comment (styling/UX pass Phase 5).
    // Previously unused direction on this screen - wired 2026-09-14 to the
    // new Splash/Clock screen, completing the loop: Splash/Clock <->
    // Settings <-> Speed(home) <-> ... See ui_splashScreen.c's header
    // comment for the full topology.
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_splashScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_splashScreen_screen_init);
        _ui_screen_delete(&ui_settingsScreen);
    }
}

// --- Warning-threshold row: "Title" [-] [ NNN unit ] [+] --------------------
static lv_obj_t *createThresholdRow(lv_obj_t *section, const char *title,
                                     int32_t rangeMin, int32_t rangeMax, int32_t initialValue,
                                     uint8_t digitCount, uint8_t decimalPos) {
    lv_obj_t *cont = lv_menu_cont_create(section);
    lv_obj_set_height(cont, ROW_HEIGHT);

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(label, 1);

    lv_obj_t *minusBtn = lv_btn_create(cont);
    lv_obj_set_size(minusBtn, STEPPER_BTN, STEPPER_BTN);
    lv_obj_set_style_radius(minusBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(minusBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *minusLabel = lv_label_create(minusBtn);
    lv_label_set_text(minusLabel, LV_SYMBOL_MINUS);
    lv_obj_center(minusLabel);

    lv_obj_t *spinbox = lv_spinbox_create(cont);
    // 130px @ 24px font (shrunk 2026-09-26, was 150px @ 32px) - still fits
    // the biggest case (packVLow: "280.0", 5 glyphs) plus the spinbox's own
    // internal padding, at CLAUDE.md's "standard value" font tier (one step
    // down from hero-numeric) rather than the same size the primary
    // telemetry hero numbers use elsewhere - appropriate for a secondary
    // settings field.
    lv_obj_set_size(spinbox, 130, STEPPER_BTN);
    lv_spinbox_set_range(spinbox, rangeMin, rangeMax);
    lv_spinbox_set_digit_format(spinbox, digitCount, decimalPos);
    lv_spinbox_set_rollover(spinbox, false);
    lv_spinbox_set_value(spinbox, initialValue);
    lv_obj_clear_flag(spinbox, LV_OBJ_FLAG_CLICKABLE);  // display-only: +/- buttons drive it, never a keypad popup
    lv_obj_set_style_bg_color(spinbox, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(spinbox, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(spinbox, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(spinbox, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(spinbox, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *plusBtn = lv_btn_create(cont);
    lv_obj_set_size(plusBtn, STEPPER_BTN, STEPPER_BTN);
    lv_obj_set_style_radius(plusBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(plusBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *plusLabel = lv_label_create(plusBtn);
    lv_label_set_text(plusLabel, LV_SYMBOL_PLUS);
    lv_obj_center(plusLabel);

    return spinbox;  // caller keeps this for Save/Default; minus/plus wiring happens separately (needs the callback)
}

static void stepperEventCb(lv_event_t *e, int dir) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *spinbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (code == LV_EVENT_PRESSED) {
        repeatCount = 0;
    } else if (code == LV_EVENT_CLICKED) {
        _ui_spinbox_step(spinbox, dir);
        markDirty();
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        repeatCount++;
        int step = (repeatCount > 15) ? 5 : (repeatCount > 6 ? 2 : 1);  // accelerate the longer it's held
        for (int i = 0; i < step; i++) {
            _ui_spinbox_step(spinbox, dir);
        }
        markDirty();
    }
}
static void minusStepperCb(lv_event_t *e) { stepperEventCb(e, -1); }
static void plusStepperCb(lv_event_t *e)  { stepperEventCb(e, 1); }

// Wires a threshold row's +/- buttons - split from createThresholdRow() so
// that function can stay a simple "build it, return the spinbox" helper.
static void wireThresholdRow(lv_obj_t *spinbox) {
    // The row's children are: [0]=label, [1]=minusBtn, [2]=spinbox, [3]=plusBtn
    lv_obj_t *cont = lv_obj_get_parent(spinbox);
    lv_obj_t *minusBtn = lv_obj_get_child(cont, 1);
    lv_obj_t *plusBtn = lv_obj_get_child(cont, 3);
    lv_obj_add_event_cb(minusBtn, minusStepperCb, LV_EVENT_ALL, spinbox);
    lv_obj_add_event_cb(plusBtn, plusStepperCb, LV_EVENT_ALL, spinbox);
}

// UTC offset's own stepper handler - deliberately NOT stepperEventCb()/
// wireThresholdRow(): that pair marks settingsDirty and waits for the
// Warning Thresholds section's Save button, but this control lives in the
// Display section and needs the same "apply + persist immediately, live
// preview" behavior as Day/Night and Brightness (CLAUDE.md: "don't wait for
// Save to show the visual change"). Reuses createThresholdRow() purely for
// its widget layout (label + minus + spinbox + plus), not its Save-flow
// wiring.
static void utcOffsetStepperCb(lv_event_t *e, int dir) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        repeatCount = 0;
    } else if (code == LV_EVENT_CLICKED) {
        _ui_spinbox_step(utcOffsetSpinbox, dir);
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        repeatCount++;
        int step = (repeatCount > 15) ? 5 : (repeatCount > 6 ? 2 : 1);
        for (int i = 0; i < step; i++) _ui_spinbox_step(utcOffsetSpinbox, dir);
    } else {
        return;
    }
    utcOffsetHours = lv_spinbox_get_value(utcOffsetSpinbox);
    updateDisplayMode();     // persists immediately, same as Day/Night/Brightness
    refreshClockDisplay();   // instant preview - see its header comment for why
}
static void utcOffsetMinusCb(lv_event_t *e) { utcOffsetStepperCb(e, -1); }
static void utcOffsetPlusCb(lv_event_t *e)  { utcOffsetStepperCb(e, 1); }

// Explicit background/border colors for lv_menu's own section and row
// containers (2026-09-22 - real-hardware bug: Day mode's white text was
// unreadable here). Every other screen in this codebase hand-builds its own
// panels and colors them explicitly; this is the one screen built on
// lv_menu, and lv_menu's stock containers otherwise fall back to LVGL's
// built-in default theme - which ui.c's ui_init() sets to light mode once
// at boot and never toggles (that global theme has nothing to do with this
// project's own day/night palette). The result was this project's Day-mode
// white text painted onto LVGL's separate, unrelated light-gray widget
// chrome. Fix: color every row/section explicitly from ui_theme, same as
// every hand-built panel elsewhere - both at creation and again in
// refresh_theme() so a day/night toggle re-applies it too.
static void styleMenuRow(lv_obj_t *row) {
    if (!row) return;
    lv_obj_set_style_bg_color(row, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void styleMenuSection(lv_obj_t *section) {
    if (!section) return;
    lv_obj_set_style_bg_color(section, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(section, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    uint32_t n = lv_obj_get_child_cnt(section);
    for (uint32_t i = 0; i < n; i++) styleMenuRow(lv_obj_get_child(section, i));
}

static void saveBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !settingsDirty) return;

    warningSet.lowSoc       = lv_spinbox_get_value(socSpinbox);
    warningSet.motorTemp    = lv_spinbox_get_value(motorTSpinbox);
    warningSet.heatsinkTemp = lv_spinbox_get_value(heatsinkTSpinbox);
    warningSet.packVLow     = lv_spinbox_get_value(packVSpinbox) / 10.0f;  // spinbox stores deciVolts
    updateWarningsSet();

    settingsDirty = false;
    updateSaveButtonState();

    static const char *btns[] = {"OK", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Saved", "Warning thresholds saved.", btns, true);
    lv_obj_center(mbox);
}

static void lowVoltShutdownSwitchCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    bool enabled = lv_obj_has_state(lowVoltShutdownSwitch, LV_STATE_CHECKED);
    setLowVoltageShutdownEnabled(enabled);  // applies + persists immediately, no Save step
}

static void defaultBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    setDefaultWarnSet();  // writes warningSet AND persists immediately (existing function)
    lv_spinbox_set_value(socSpinbox, warningSet.lowSoc);
    lv_spinbox_set_value(motorTSpinbox, warningSet.motorTemp);
    lv_spinbox_set_value(heatsinkTSpinbox, warningSet.heatsinkTemp);
    lv_spinbox_set_value(packVSpinbox, (int32_t)(warningSet.packVLow * 10));

    settingsDirty = false;  // setDefaultWarnSet() already persisted
    updateSaveButtonState();
}

// --- Display section ---------------------------------------------------

static void updateDayNightButtons(void) {
    lv_obj_t *btns[3] = { autoBtn, dayBtn, nightBtn };
    for (int i = 0; i < 3; i++) {
        if (!btns[i]) continue;
        if ((int)displayPref == i) {
            lv_obj_add_state(btns[i], LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(btns[i], ui_theme_accent(), LV_PART_MAIN | LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(btns[i], LV_STATE_CHECKED);
        }
    }
}

static void dayNightBtnCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    displayPref = (display_pref_t)(intptr_t)lv_event_get_user_data(e);
    applyDisplayMode();     // live preview, per CLAUDE.md - not gated behind Save
    updateDisplayMode();    // persists immediately, same reasoning
    updateDayNightButtons();
    if (brightnessSlider) {
        lv_slider_set_value(brightnessSlider, ui_theme_get_brightness(), LV_ANIM_ON);
    }
}

static lv_obj_t *createDayNightBtn(lv_obj_t *cont, const char *text, display_pref_t pref) {
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 110, STEPPER_BTN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, dayNightBtnCb, LV_EVENT_CLICKED, (void *)(intptr_t)pref);
    return btn;
}

static void brightnessSliderCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    ui_theme_set_brightness((uint8_t)lv_slider_get_value(brightnessSlider));
    updateDisplayMode();
}

static void updateWifiConfigStatusLabel(void) {
    if (!wifiConfigStatusLabel) return;
    if (wifi_config_server_is_running()) {
        lv_label_set_text(wifiConfigStatusLabel,
            "SSID: " WIFI_CONFIG_SSID "  Pass: " WIFI_CONFIG_PASSWORD "  ->  192.168.4.1");
    } else {
        lv_label_set_text(wifiConfigStatusLabel, "Off");
    }
}

static void wifiConfigSwitchCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    bool enabled = lv_obj_has_state(wifiConfigSwitch, LV_STATE_CHECKED);
    if (enabled) wifi_config_server_start();
    else wifi_config_server_stop();
    updateWifiConfigStatusLabel();
}

// Root-list row: title + a trailing chevron, tap navigates into `target`
// via LVGL's own lv_menu_set_load_page_event() (2026-09-26 sub-page split -
// see the statics block above). Sized to ROW_HEIGHT like every other row on
// this screen, comfortably above CLAUDE.md's 80px touch-target floor.
static lv_obj_t *createNavRow(lv_obj_t *section, const char *title, lv_obj_t *target) {
    lv_obj_t *row = lv_menu_cont_create(section);
    lv_obj_set_height(row, ROW_HEIGHT);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_menu_set_load_page_event(menu, row, target);
    return row;
}

void ui_settingsScreen_screen_init(void)
{
    ui_settingsScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_settingsScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_settingsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_settingsScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    menu = lv_menu_create(ui_settingsScreen);
    // Height reduced by UI_DOCK_H (styling/UX pass Phase 5, 2026-09-18) to
    // leave room for the persistent bottom nav dock - lv_menu's page is
    // internally scrollable, so this is just less visible height at once,
    // not a clipping risk the way the fixed-position screens elsewhere in
    // this codebase are.
    lv_obj_set_size(menu, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL) - UI_DOCK_H);
    // DISABLED is still correct here (2026-09-26 sub-page split) - this
    // controls whether the back button shows AT THE ROOT list too (it
    // wouldn't have anywhere useful to go); LVGL's own lv_menu already
    // shows it automatically once you're two levels deep (inside
    // thPage/dispPage/calPage, reached via lv_menu_set_load_page_event()
    // below), which is exactly the behavior wanted - hidden at the list,
    // visible once inside a section. Physical swipe RIGHT still exits the
    // whole screen from any depth - see ui_event_settingsScreen().
    lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_DISABLED);
    lv_obj_set_style_bg_color(menu, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Warning Thresholds page ---
    thPage = lv_menu_page_create(menu, (char *)"Warning Thresholds");
    lv_obj_set_style_pad_all(thPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(thPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    thSection = lv_menu_section_create(thPage);
    socSpinbox = createThresholdRow(thSection, "Low SOC (%)", 0, 100, warningSet.lowSoc, 3, 0);
    motorTSpinbox = createThresholdRow(thSection, "High Motor Temp (\xC2\xB0" "C)", -40, 200, warningSet.motorTemp, 3, 0);
    heatsinkTSpinbox = createThresholdRow(thSection, "High Heatsink Temp (\xC2\xB0" "C)", 0, 150, warningSet.heatsinkTemp, 3, 0);
    packVSpinbox = createThresholdRow(thSection, "Low Pack Voltage (V)", 2000, 4500, (int32_t)(warningSet.packVLow * 10), 4, 1);
    wireThresholdRow(socSpinbox);
    wireThresholdRow(motorTSpinbox);
    wireThresholdRow(heatsinkTSpinbox);
    wireThresholdRow(packVSpinbox);

    // Applies + persists immediately (like Day/Night below), not part of
    // the numeric-threshold Save flow above it - see zombie_updaters.h's
    // comment on lowVoltageShutdownEnabled for why this exists: a bench
    // ZombieVerter with no U12V sensor wired legitimately reports ~0V,
    // indistinguishable from a real dying battery, so this is a manual
    // override rather than something CAN data alone can resolve.
    lv_obj_t *lowVoltRow = lv_menu_cont_create(thSection);
    lv_obj_set_height(lowVoltRow, ROW_HEIGHT);
    lv_obj_t *lowVoltLabel = lv_label_create(lowVoltRow);
    lv_label_set_text(lowVoltLabel, "Low 12V Shutdown");
    lv_obj_set_style_text_color(lowVoltLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lowVoltLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(lowVoltLabel, 1);
    lowVoltShutdownSwitch = lv_switch_create(lowVoltRow);
    lv_obj_set_size(lowVoltShutdownSwitch, 70, 40);
    if (lowVoltageShutdownEnabled) lv_obj_add_state(lowVoltShutdownSwitch, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(lowVoltShutdownSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(lowVoltShutdownSwitch, lowVoltShutdownSwitchCb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *saveRow = lv_menu_cont_create(thSection);
    lv_obj_set_height(saveRow, ROW_HEIGHT);
    lv_obj_t *defaultBtn = lv_btn_create(saveRow);
    lv_obj_set_size(defaultBtn, 160, STEPPER_BTN);
    lv_obj_set_style_radius(defaultBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(defaultBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *defaultLabel = lv_label_create(defaultBtn);
    lv_label_set_text(defaultLabel, "Defaults");
    lv_obj_center(defaultLabel);
    lv_obj_add_event_cb(defaultBtn, defaultBtnCb, LV_EVENT_ALL, NULL);

    lv_obj_t *saveSpacer = lv_obj_create(saveRow);
    lv_obj_set_size(saveSpacer, 1, 1);
    lv_obj_set_style_bg_opa(saveSpacer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(saveSpacer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(saveSpacer, 1);

    saveBtn = lv_btn_create(saveRow);
    lv_obj_set_size(saveBtn, 160, STEPPER_BTN);
    lv_obj_set_style_radius(saveBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    saveBtnLabel = lv_label_create(saveBtn);
    lv_label_set_text(saveBtnLabel, "SAVE");
    lv_obj_center(saveBtnLabel);
    lv_obj_add_event_cb(saveBtn, saveBtnCb, LV_EVENT_ALL, NULL);
    updateSaveButtonState();  // starts disabled/dim - nothing dirty yet

    // --- Display page ---
    dispPage = lv_menu_page_create(menu, (char *)"Display");
    lv_obj_set_style_pad_all(dispPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dispPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    dispSection = lv_menu_section_create(dispPage);

    lv_obj_t *dayNightRow = lv_menu_cont_create(dispSection);
    lv_obj_set_height(dayNightRow, ROW_HEIGHT);
    lv_obj_t *dayNightLabel = lv_label_create(dayNightRow);
    lv_label_set_text(dayNightLabel, "Day / Night");
    lv_obj_set_style_text_color(dayNightLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(dayNightLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(dayNightLabel, 1);
    // TODO: "Auto" currently always resolves to Day - no RTC or light
    // sensor on this board yet. See resolveDisplayPref() in
    // zombie_updaters.cpp. The 3-way control and persistence are already
    // wired for real; only the resolution logic needs a sensor later.
    autoBtn = createDayNightBtn(dayNightRow, "Auto", DISPLAY_PREF_AUTO);
    dayBtn = createDayNightBtn(dayNightRow, "Day", DISPLAY_PREF_DAY);
    nightBtn = createDayNightBtn(dayNightRow, "Night", DISPLAY_PREF_NIGHT);
    updateDayNightButtons();

    lv_obj_t *brightRow = lv_menu_cont_create(dispSection);
    lv_obj_set_height(brightRow, ROW_HEIGHT);
    lv_obj_t *brightLabel = lv_label_create(brightRow);
    lv_label_set_text(brightLabel, "Brightness");
    lv_obj_set_style_text_color(brightLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(brightLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    brightnessSlider = lv_slider_create(brightRow);
    lv_obj_set_flex_grow(brightnessSlider, 1);
    lv_obj_set_height(brightnessSlider, 20);
    lv_slider_set_range(brightnessSlider, 55, 255);
    lv_slider_set_value(brightnessSlider, ui_theme_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightnessSlider, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(brightnessSlider, ui_theme_accent(), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(brightnessSlider, brightnessSliderCb, LV_EVENT_VALUE_CHANGED, NULL);

    // UTC offset (2026-09-22) - whole hours, -12..+14 covers every real
    // UTC offset in use. Built with the same row widget as the Warning
    // Thresholds spinboxes for visual consistency, but wired separately
    // (utcOffsetMinusCb/PlusCb) for immediate apply+persist - see
    // utcOffsetStepperCb()'s comment.
    utcOffsetSpinbox = createThresholdRow(dispSection, "UTC Offset (hr)", -12, 14, utcOffsetHours, 2, 0);
    {
        lv_obj_t *cont = lv_obj_get_parent(utcOffsetSpinbox);
        lv_obj_t *minusBtn = lv_obj_get_child(cont, 1);
        lv_obj_t *plusBtn = lv_obj_get_child(cont, 3);
        lv_obj_add_event_cb(minusBtn, utcOffsetMinusCb, LV_EVENT_ALL, NULL);
        lv_obj_add_event_cb(plusBtn, utcOffsetPlusCb, LV_EVENT_ALL, NULL);
    }

    // WiFi Config (2026-09-22) - local-only AP + web page for editing
    // vehicle_config.h's values without a reflash (see
    // wifi_config_server.h). Off by default; a separate thin row below
    // shows the SSID/password/URL only while it's on, same "don't show
    // controls for a state that isn't active" restraint as the rest of
    // this screen.
    lv_obj_t *wifiRow = lv_menu_cont_create(dispSection);
    lv_obj_set_height(wifiRow, ROW_HEIGHT);
    lv_obj_t *wifiLabel = lv_label_create(wifiRow);
    lv_label_set_text(wifiLabel, "WiFi Config");
    lv_obj_set_style_text_color(wifiLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(wifiLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(wifiLabel, 1);
    wifiConfigSwitch = lv_switch_create(wifiRow);
    lv_obj_set_size(wifiConfigSwitch, 70, 40);
    if (wifi_config_server_is_running()) lv_obj_add_state(wifiConfigSwitch, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(wifiConfigSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(wifiConfigSwitch, wifiConfigSwitchCb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *wifiStatusRow = lv_menu_cont_create(dispSection);
    lv_obj_set_height(wifiStatusRow, 40);
    wifiConfigStatusLabel = lv_label_create(wifiStatusRow);
    lv_obj_set_style_text_color(wifiConfigStatusLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(wifiConfigStatusLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    updateWifiConfigStatusLabel();

    // --- Calibration page ---
    calPage = lv_menu_page_create(menu, (char *)"Calibration");
    lv_obj_set_style_pad_all(calPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(calPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    calSection = lv_menu_section_create(calPage);
    lv_obj_t *calRow = lv_menu_cont_create(calSection);
    lv_obj_set_height(calRow, ROW_HEIGHT);
    lv_obj_t *calLabel = lv_label_create(calRow);
    lv_label_set_text(calLabel, "Dyno drivetrain calibration");
    lv_obj_set_style_text_color(calLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(calLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_grow(calLabel, 1);
    // Stub - specifically the CALIBRATION workflow (enter real chassis-dyno
    // numbers, solve for correction constants - architecture doc's third
    // dyno mode). The LIVE + RESULTS screens (ui_dynoLiveScreen.cpp /
    // ui_dynoResultsScreen.cpp, reachable via BMS screen swipe-left) are
    // built and functional; this entry point stays disabled until the
    // separate calibration-math workflow is too.
    lv_obj_t *calBtn = lv_btn_create(calRow);
    lv_obj_set_size(calBtn, 160, STEPPER_BTN);
    lv_obj_add_state(calBtn, LV_STATE_DISABLED);
    lv_obj_set_style_radius(calBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(calBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *calBtnLabel = lv_label_create(calBtn);
    lv_label_set_text(calBtnLabel, "Not built yet");
    lv_obj_center(calBtnLabel);

    // --- Root list page (2026-09-26 sub-page split) ---
    // What used to be the single shared page holding all three sections
    // stacked - now just a short list of the three section names, each
    // navigating into its own page above via createNavRow(). This is what
    // eliminates the scrolling: each section's own page only holds ITS
    // rows, not all three sections' worth at once.
    settingsPage = lv_menu_page_create(menu, (char *)"Settings");
    lv_obj_set_style_pad_all(settingsPage, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(settingsPage, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    rootSection = lv_menu_section_create(settingsPage);
    createNavRow(rootSection, "Warning Thresholds", thPage);
    createNavRow(rootSection, "Display", dispPage);
    createNavRow(rootSection, "Calibration", calPage);

    // Firmware version - global info, not section-specific, so it stays on
    // the list itself rather than living inside any one section's page
    // (unchanged from where it sat before the split, just relocated onto
    // the new root page instead of the old shared one).
    fwRow = lv_menu_cont_create(settingsPage);
    lv_obj_t *fwLabel = lv_label_create(fwRow);
    lv_label_set_text_fmt(fwLabel, "Firmware: %s", DASH_TAG);
    lv_obj_set_style_text_color(fwLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(fwLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Explicit contrast-fix colors - see styleMenuSection()'s header comment
    // for the root cause. fwRow sits directly on settingsPage, not inside
    // rootSection, so it needs its own styleMenuRow() call.
    styleMenuSection(rootSection);
    styleMenuSection(thSection);
    styleMenuSection(dispSection);
    styleMenuSection(calSection);
    styleMenuRow(fwRow);

    lv_menu_set_page(menu, settingsPage);

    // Back button touch-target size (2026-09-26) - lv_menu's own back
    // button (shown automatically once inside thPage/dispPage/calPage,
    // hidden at this root list - see the lv_menu_set_mode_root_back_btn()
    // comment above) defaults to a small icon-sized hit area. Resized here
    // to CLAUDE.md's 80px touch-target floor, same as every other control
    // on this screen - this is new interaction surface on this screen, not
    // present before the sub-page split.
    // 2026-09-26, found on real hardware: styling just the header container
    // (below) did NOT make the icon visible - lv_btn's own built-in LVGL
    // theme applies a LOCAL text_color to the button itself, which blocks
    // inheritance from an ancestor (local styles always win over inherited
    // ones), and the icon (an lv_img with a LV_SYMBOL_LEFT source, drawn
    // using text-style properties, not img_recolor) inherits FROM the
    // button, not the header - so it rendered in LVGL's default (dark,
    // meant for a light background) color, invisible against this
    // project's own dark panel background. Same root cause this file's
    // styleMenuRow()/styleMenuSection() already fixed once for row/section
    // backgrounds; fixed here the same way - style the actual button and
    // its icon child directly, not an ancestor, so there's no inheritance
    // chain to get blocked.
    lv_obj_t *menuBackBtn = lv_menu_get_main_header_back_btn(menu);
    if (menuBackBtn) {
        lv_obj_set_size(menuBackBtn, 80, 80);
        lv_obj_set_style_bg_color(menuBackBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(menuBackBtn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(menuBackBtn, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *backIcon = lv_obj_get_child(menuBackBtn, 0);
        if (backIcon) {
            lv_obj_center(backIcon);
            lv_obj_set_style_text_color(backIcon, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(backIcon, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor(backIcon, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // Header bar contrast fix (2026-09-26) - same root cause/fix as
    // styleMenuRow()/styleMenuSection() above: this is more never-touched
    // lv_menu stock chrome (the fixed title bar showing each page's title +
    // the back button), which otherwise falls back to LVGL's built-in
    // light theme. No per-object title-label getter exists in this LVGL
    // version, so styled on the header CONTAINER instead - text color/font
    // are inheritable LVGL style properties, so this cascades to the title
    // label and back-button icon without needing to reach into lv_menu's
    // internal child structure.
    lv_obj_t *menuHeader = lv_menu_get_main_header(menu);
    if (menuHeader) {
        lv_obj_set_style_bg_color(menuHeader, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(menuHeader, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(menuHeader, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(menuHeader, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Sibling of the menu, not a child of it - stays fixed at the bottom
    // regardless of the menu's own internal scroll position.
    ui_settingsScreenDock = ui_dock_create(ui_settingsScreen, UI_DOCK_SETTINGS);

    // Gesture handler on both the screen AND the menu itself: lv_menu's
    // page fills the whole screen and is a scroll target, so a swipe might
    // be delivered to the menu rather than bubbling to the screen object -
    // attaching to both guarantees the LEFT-swipe-back gesture is caught
    // regardless of which object LVGL treats as the gesture's origin. This
    // exits the WHOLE Settings screen from any depth (root list or inside
    // a section page) - distinct from the menu's own back button above,
    // which only steps out of a section page back to this root list.
    lv_obj_add_event_cb(ui_settingsScreen, ui_event_settingsScreen, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(menu, ui_event_settingsScreen, LV_EVENT_ALL, NULL);
}

void ui_settingsScreen_refresh_theme(void)
{
    if (ui_settingsScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_settingsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (menu) lv_obj_set_style_bg_color(menu, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *spinboxes[] = { socSpinbox, motorTSpinbox, heatsinkTSpinbox, packVSpinbox, utcOffsetSpinbox };
    for (int i = 0; i < 5; i++) {
        if (!spinboxes[i]) continue;
        lv_obj_set_style_bg_color(spinboxes[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(spinboxes[i], ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(spinboxes[i], ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    updateSaveButtonState();
    updateDayNightButtons();
    if (brightnessSlider) {
        lv_obj_set_style_bg_color(brightnessSlider, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(brightnessSlider, ui_theme_accent(), LV_PART_KNOB | LV_STATE_DEFAULT);
    }
    if (lowVoltShutdownSwitch) {
        lv_obj_set_style_bg_color(lowVoltShutdownSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
    if (wifiConfigSwitch) {
        lv_obj_set_style_bg_color(wifiConfigSwitch, ui_theme_accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
    if (wifiConfigStatusLabel) {
        lv_obj_set_style_text_color(wifiConfigStatusLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    // Every sub-page's own background needs its own explicit recolor too
    // (2026-09-26 sub-page split) - each is a separate lv_menu_page_create()
    // result now, not one shared page.
    lv_obj_t *pages[] = { settingsPage, thPage, dispPage, calPage };
    for (int i = 0; i < 4; i++) {
        if (pages[i]) lv_obj_set_style_bg_color(pages[i], ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    styleMenuSection(rootSection);
    styleMenuSection(thSection);
    styleMenuSection(dispSection);
    styleMenuSection(calSection);
    styleMenuRow(fwRow);
    if (menu) {
        lv_obj_t *menuHeader = lv_menu_get_main_header(menu);
        if (menuHeader) {
            lv_obj_set_style_bg_color(menuHeader, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(menuHeader, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // See the back-button styling comment in screen_init() - restyled
        // directly here too, not just the header, for the same
        // inheritance-blocked-by-lv_btn's-own-theme reason.
        lv_obj_t *menuBackBtn = lv_menu_get_main_header_back_btn(menu);
        if (menuBackBtn) {
            lv_obj_set_style_bg_color(menuBackBtn, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_t *backIcon = lv_obj_get_child(menuBackBtn, 0);
            if (backIcon) {
                lv_obj_set_style_text_color(backIcon, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_img_recolor(backIcon, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }
    ui_dock_refresh_theme(ui_settingsScreenDock, UI_DOCK_SETTINGS);
    // Section header labels, row title labels, and the +/-/default/cal
    // button chrome are left as their creation-time colors here - this
    // screen is dense enough that a full re-walk on every toggle isn't
    // worth the code size; the parts that matter most for readability
    // (background, section/row containers, spinbox text, save button,
    // day/night selection) are covered above.
}

void ui_settingsScreen_screen_destroy(void)
{
    if (ui_settingsScreen) lv_obj_del(ui_settingsScreen);

    ui_settingsScreen = NULL;
    menu = NULL;
    settingsPage = NULL;
    rootSection = NULL;
    thPage = NULL;
    dispPage = NULL;
    calPage = NULL;
    thSection = NULL;
    dispSection = NULL;
    calSection = NULL;
    fwRow = NULL;
    socSpinbox = NULL;
    motorTSpinbox = NULL;
    heatsinkTSpinbox = NULL;
    packVSpinbox = NULL;
    utcOffsetSpinbox = NULL;
    saveBtn = NULL;
    saveBtnLabel = NULL;
    settingsDirty = false;
    autoBtn = NULL;
    dayBtn = NULL;
    nightBtn = NULL;
    brightnessSlider = NULL;
    lowVoltShutdownSwitch = NULL;
    wifiConfigSwitch = NULL;
    wifiConfigStatusLabel = NULL;
    ui_settingsScreenDock = NULL;
}
