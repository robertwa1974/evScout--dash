#pragma once

// Low-12V shutdown warning sequence (styling/UX pass, 2026-09-18, Phase 6,
// items 12-13). Confirmed with Rob: full-screen takeover (not a banner),
// 11.5V threshold, triggered purely off the existing aux12V CAN telemetry -
// no ignition/key-off GPIO exists on this board, so a sustained low-12V
// reading (the car battery genuinely dying, not a momentary sag under
// cranking load) is the only signal this codebase has for "shutting down."
//
// Built as an overlay on lv_layer_top() (LVGL's always-on-top layer, sits
// above whatever screen is currently active), NOT as another entry in the
// screen chain - this is a modal takeover that can interrupt ANY screen,
// not a destination anyone navigates to, so it doesn't participate in
// _ui_screen_change()/the nav dock/swipe topology at all. Same layer
// Phase 7's fault banner (ui_fault_banner.h) will also use.
//
// Sequence: aux12V crosses below AUX12V_LOW_V (edge-triggered, not
// re-fired every update while it stays low) -> full-screen warning shown
// (opaque, readable, backlight still on) for AUX12V_WARNING_MS -> a black
// curtain fades in over it (bg_opa 0->255) while backlight_rampTo(0, ...)
// runs at the same time (display_driver.h, built in Phase 4) - "mirrors
// boot" per the plan, same primitive the splash screen's wipe uses, just
// the reverse direction and no logo. If aux12V recovers above
// AUX12V_RECOVER_V (a real hysteresis gap above the trip point, not the
// same value - avoids flicker right at the threshold) at ANY point in this
// sequence, it unwinds: backlight ramps back to whatever it was before the
// warning started, the overlay hides, nothing about the underlying screen
// was ever touched (it was never left).

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call on every aux12V update (zombie_updaters.cpp's slowUpdate(), same
// uiMutex-held context every other LVGL touch happens in - this function
// touches LVGL objects, so it must be called from there, not from
// dataMutex-only code). Lazily builds the overlay hierarchy on its first
// call (after lv_init() has definitely already run, unlike a fixed
// ui_init()-time construction which would need extra ordering care for no
// real benefit - this is never on the hot path, one dirty-checked aux12V
// update per ~800ms).
void ui_shutdown_notifyAux12V(float auxV);

#ifdef __cplusplus
}
#endif
