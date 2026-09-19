#pragma once

#include <stdbool.h>

// Global cross-screen fault banner (styling/UX pass, 2026-09-18, Phase 7,
// items 14-15). A thin strip pinned to the top of lv_layer_top() (LVGL's
// always-on-top layer - also used by ui_shutdown.h's takeover overlay,
// see that header for why this codebase uses that layer for both rather
// than adding another entry to the screen chain), hidden when there's
// nothing to report. Created once from ui_init() so it's present over
// every screen without any per-screen wiring (unlike ui_dock.h, which is
// deliberately one instance per screen - this banner is a single global
// instance instead, since there's exactly one fault state at a time, not
// a per-screen one).
//
// Three source conditions, ONE combined display state - priority order
// confirmed: a real VCU fault or an over-temp reading outranks a CAN
// dropout (losing telemetry matters, but an active fault while telemetry
// was still flowing is the more urgent thing to show), which outranks
// showing nothing. UI_FAULT_CAN_DROPOUT is gated by the caller on
// ui_can_freshness_hasEverReceived() before it's even passed in here (see
// zombie_updaters.cpp's call site) - the exact same "never seen data yet
// isn't the same as data having stopped" distinction item 15 is about,
// so a fresh boot with no CAN connected never shows a false dropout
// banner.
typedef enum {
    UI_FAULT_NONE,
    UI_FAULT_CAN_DROPOUT,  // ui_can_freshness_isLive() went false after having been true at least once
    UI_FAULT_OVERTEMP,     // motor or heatsink temp over its warningSet threshold
    UI_FAULT_VCU_ERROR,    // myData.lastErr != 0 - see zombieverter_params.h's "lasterr" SDO comment for the code list
} ui_fault_t;

#ifdef __cplusplus
extern "C" {
#endif

// Builds the (initially hidden) banner - call once from ui_init().
void ui_fault_banner_init(void);

// Call whenever any of the three source conditions might have changed
// (zombie_updaters.cpp's slowUpdate(), same uiMutex-held context every
// other LVGL touch happens in). Applies the priority order above
// internally and only touches the widget if the RESULTING effective
// state actually changed, so callers don't need their own dirty-check on
// top of their existing one. lastErr is passed as the raw SDO value
// (0=none) rather than a pre-converted bool so this function can look up
// and display the specific fault name, not just "a fault exists."
void ui_fault_banner_update(bool canDropout, bool overTemp, int lastErr);

#ifdef __cplusplus
}
#endif
