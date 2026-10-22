#pragma once

#include "lvgl.h"
#include <stdint.h>

// ============================================================================
// Full-screen quick editor for a single VCU param (2026-10-21) - the Drive
// screen's tap-to-edit cards (Gear, Motor Mode, Regen Limit, Brake Regen),
// the touchscreen equivalent of Rob's Dial firmware's edit mode
// (Zombieverter-Dial-Display/src/main.cpp):
//   choice  - up to 4 large option buttons; tapping one writes it at once
//             (Dial: tap an option to set it)
//   stepper - 130px -/+ buttons, step 1, hold to repeat (Dial: encoder
//             steps of 1). Rapid taps are coalesced - only the latest
//             target is ever sent, one SDO write in flight at a time.
// Writes are live-only (not saved to VCU flash), same as the Dial.
// Interlock comes from sdo_client_write_allowed() - buttons are disabled
// with the reason shown when a write isn't allowed right now.
//
// Built on whatever screen is active, deleted on DONE or when that screen
// unloads. Only one editor exists at a time.
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

void ui_param_editor_open_choice(const char *title, uint16_t paramId,
                                 const char *const *options, int count);
void ui_param_editor_open_stepper(const char *title, uint16_t paramId,
                                  int minValue, int maxValue, const char *unit);
void ui_param_editor_close(void);

#ifdef __cplusplus
}
#endif
