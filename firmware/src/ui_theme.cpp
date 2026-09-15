#include "ui_theme.h"
#include "ui.h"
#include "display_driver.h"  // setBrightness()

typedef struct {
    uint32_t bg;
    uint32_t panelBg;
    uint32_t panelBorder;
    uint32_t textPrimary;
    uint32_t textSecondary;
    uint32_t accent;
    uint32_t accentEnergy;
    uint32_t warning;
    uint32_t good;
    uint32_t bad;
} ui_palette_t;

// Colors only now - backlight is tracked separately per mode below (see
// dayBrightness/nightBrightness) so a manual brightness adjustment
// (settings slider or the swipe gesture) survives a day<->night switch
// instead of snapping back to a fixed default every time.

// Day: bright, high-contrast, for sunlight readability.
static const ui_palette_t kPaletteDay = {
    .bg            = 0x1A1A1A,
    .panelBg       = 0x262626,
    .panelBorder   = 0x444444,
    .textPrimary   = 0xFFFFFF,
    .textSecondary = 0xB0B0B0,
    .accent        = 0x00D9FF,
    // Distinct gold/amber accent for energy-identity screens (Battery,
    // Charging) - design-review "semantic color palette" pass, 2026-09-14
    // (see waveshare-dash-build.md). Deliberately NOT reused for Drive/
    // Status/GPS's default cyan accent, so those screens keep reading as
    // "motion/general telemetry" while Battery/Charging read as "energy"
    // at a glance, without touching hue on every single bar in the app.
    .accentEnergy  = 0xFFD60A,
    .warning       = 0xFF3B30,
    .good          = 0x34C759,
    .bad           = 0xFF9500,
};

// Night: low-glare, amber-shifted to preserve dark adaptation (classic
// automotive night-mode convention - avoids white/blue at night).
static const ui_palette_t kPaletteNight = {
    .bg            = 0x000000,
    .panelBg       = 0x0D0D0D,
    .panelBorder   = 0x2A1500,
    .textPrimary   = 0xB35900,
    .textSecondary = 0x663300,
    .accent        = 0xCC5200,
    // Deliberately IDENTICAL to accent in Night mode, not a distinct hue -
    // the entire point of this palette is staying in the amber family to
    // preserve dark adaptation (see the comment above this struct); adding
    // a second, different-hued "energy" accent at night would undermine
    // that on the two screens that use it. The semantic distinction this
    // color exists for is a Day-mode-only refinement.
    .accentEnergy  = 0xCC5200,
    .warning       = 0xB30000,
    .good          = 0x1F7A1F,
    .bad           = 0x995200,
};

static ui_theme_mode_t currentMode = UI_THEME_DAY;
static uint8_t dayBrightness = 220;
static uint8_t nightBrightness = 60;

static const ui_palette_t *active(void) {
    return (currentMode == UI_THEME_NIGHT) ? &kPaletteNight : &kPaletteDay;
}

void ui_theme_init(void) {
    currentMode = UI_THEME_DAY;
}

void ui_theme_set(ui_theme_mode_t mode) {
    currentMode = mode;
    setBrightness(currentMode == UI_THEME_NIGHT ? nightBrightness : dayBrightness);

    // Each screen only exists once first navigated to (see ui.c / the
    // _ui_screen_change lazy-init pattern) - guard against screens that
    // haven't been created yet.
    if (ui_speedScreen != NULL) {
        ui_speedScreen_refresh_theme();
    }
    if (ui_driveScreen != NULL) {
        ui_driveScreen_refresh_theme();
    }
    if (ui_statusScreen != NULL) {
        ui_statusScreen_refresh_theme();
    }
    if (ui_batteryScreen != NULL) {
        ui_batteryScreen_refresh_theme();
    }
    if (ui_chargingScreen != NULL) {
        ui_chargingScreen_refresh_theme();
    }
    if (ui_gpsScreen != NULL) {
        ui_gpsScreen_refresh_theme();
    }
    if (ui_navScreen != NULL) {
        ui_navScreen_refresh_theme();
    }
    if (ui_settingsScreen != NULL) {
        ui_settingsScreen_refresh_theme();
    }
    if (ui_splashScreen != NULL) {
        ui_splashScreen_refresh_theme();
    }
}

ui_theme_mode_t ui_theme_get(void) {
    return currentMode;
}

static uint8_t clampBrightness(int val) {
    if (val < 55) return 55;
    if (val > 255) return 255;
    return (uint8_t)val;
}

void ui_theme_set_brightness(uint8_t val) {
    uint8_t clamped = clampBrightness(val);
    if (currentMode == UI_THEME_NIGHT) {
        nightBrightness = clamped;
    } else {
        dayBrightness = clamped;
    }
    setBrightness(clamped);
}

uint8_t ui_theme_get_brightness(void) {
    return currentMode == UI_THEME_NIGHT ? nightBrightness : dayBrightness;
}

uint8_t ui_theme_get_day_brightness(void)   { return dayBrightness; }
uint8_t ui_theme_get_night_brightness(void) { return nightBrightness; }

void ui_theme_load_brightness(uint8_t dayVal, uint8_t nightVal) {
    dayBrightness = clampBrightness(dayVal);
    nightBrightness = clampBrightness(nightVal);
}

lv_color_t ui_theme_bg(void)             { return lv_color_hex(active()->bg); }
lv_color_t ui_theme_panel_bg(void)       { return lv_color_hex(active()->panelBg); }
lv_color_t ui_theme_panel_border(void)   { return lv_color_hex(active()->panelBorder); }
lv_color_t ui_theme_text_primary(void)   { return lv_color_hex(active()->textPrimary); }
lv_color_t ui_theme_text_secondary(void) { return lv_color_hex(active()->textSecondary); }
lv_color_t ui_theme_accent(void)         { return lv_color_hex(active()->accent); }
lv_color_t ui_theme_accent_energy(void)  { return lv_color_hex(active()->accentEnergy); }
lv_color_t ui_theme_warning(void)        { return lv_color_hex(active()->warning); }
lv_color_t ui_theme_good(void)           { return lv_color_hex(active()->good); }
lv_color_t ui_theme_bad(void)            { return lv_color_hex(active()->bad); }
