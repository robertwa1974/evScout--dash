#include "zombie_updaters.h"
#include "display_driver.h"

// Wired to the Speed screen's swipe-up/down gesture (ui_speedScreen.c) - keeps
// uaDASH's existing brightness gesture working per CLAUDE.md, now routed
// through ui_theme's per-mode brightness (see ui_theme.h) so it adjusts
// whichever of day/night is currently active and persists immediately,
// rather than a single brightness value that day/night switching would
// otherwise clobber.
void upBrightness(lv_event_t *e) {
  ui_theme_set_brightness(ui_theme_get_brightness() + 20);
  updateDisplayMode();
}

void downBribrightness(lv_event_t *e) {
  ui_theme_set_brightness(ui_theme_get_brightness() - 20);
  updateDisplayMode();
}

// --- Bench/engine-config handlers removed entirely ---
// (benchIGN1-8, benchINJ1-8, bench(), StartStop(), benchFuelPump/Fan1/Fan2,
//  benchScreenInitSetup, engineSettingScreenInitSettup, clearEngConfiguration,
//  setDisp48L/53L/57L/60L/62L/70L, setTrig24/58, setCamS1/2/4, engSetSave)
// These were rusEFI ICE bench-test/engine-config callbacks tied to the
// now-deleted ui_benchScreen/ui_engineConfigScreen and ui_debugStatus
// widget. No EV equivalent - deleted, not ported.

// --- Settings-screen warning steppers / turbo switch / save / default ---
// Removed entirely (2026-09-13, settings screen rebuilt as lv_menu per
// CLAUDE.md). These existed only because SquareLine's codegen always routes
// widget events through an events.cpp wrapper; the new ui_settingsScreen.c
// is hand-written (like ui_speedScreen.c/ui_batteryScreen.c) with its own static
// event handlers directly in that file - no indirection layer needed. See
// git history for the old rusEFI-field no-op stubs that lived here.
