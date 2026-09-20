#pragma once

// Splash-screen "truck revolve" playback (2026-09-19). Replaces the
// previously-static hold-and-wait-for-CAN state on the Splash/Clock
// screen with a looping turntable animation of the Scout - see
// ui_splashScreen.cpp's header comment for exactly where this fits in
// the boot sequence (starts when the logo wipe-reveal finishes, stops
// the instant the existing CAN-arrival/timeout gate fires or the user
// taps to skip).
//
// SD CARD LAYOUT: frames live at /splash_frames/frame_00.bin through
// frame_21.bin (UI_SPLASH_TRUCK_FRAME_COUNT frames, SPLASH_FRAMES_DIR
// below) - generated offline by firmware/assets/convert_splash_frames.py
// from a folder of full-resolution turntable-render PNGs (see that
// script's own header for the full pipeline and the exact byte layout
// it writes: LVGL's raw lv_img_header_t + RGB565 pixel data, no
// compile-time C array - these do NOT go into flash; the
// app3M_fat9M_16MB partition table is untouched by this feature).
//
// LOADING STRATEGY: ui_splash_truck_preload() reads every frame off the
// SD card ONCE and copies its pixel data into a PSRAM buffer
// (heap_caps_malloc(..., MALLOC_CAP_SPIRAM)), building an in-memory
// lv_img_dsc_t per frame (LV_IMG_SRC_VARIABLE - no filesystem access
// needed during actual playback for a successfully-preloaded frame).
//
// Runs on its OWN FreeRTOS task (same pattern as firmware.ino's
// TaskCANReceiver - pinned to core 0, away from the main/LVGL-rendering
// loop on core 1), not inline in the caller - found on real hardware
// (2026-09-19) that even after fixing the CORE_DEBUG_LEVEL logging-
// overhead bug (see platformio.ini's comment on that flag), a real SD
// preload still takes ~11.5s, which is long enough that running it
// inline (as this originally did) visibly delayed the logo wipe-reveal
// itself - the wipe was blocked from even STARTING until preload
// finished. Call ui_splash_truck_preload() ONCE, as early as possible in
// the splash screen's own init (before any visual element is created -
// see ui_splashScreen.cpp's call site) - it returns immediately (the task
// runs in the background) so the wipe can start right away.
// ui_splash_truck_isPreloadDone() reports when every frame has been
// attempted (success or fallback) - kept for diagnostics/logging, but
// ui_splashScreen.cpp deliberately does NOT wait for it before calling
// ui_splash_truck_start(): a real preload takes far longer (~11.5s for
// 22 frames) than the splash screen's own CAN-arrival/timeout exit gate
// (a few seconds), so gating playback start on full preload completion
// meant the exit gate always fired first and the truck never appeared at
// all (found on real hardware, 2026-09-19). Playback is safe to start the
// instant the wipe finishes instead - see "Thread-safety" below for why.
//
// Thread-safety: the preload task only WRITES a given frames[i]/
// frameInPsram[i] pair, finishing frames[i] before setting
// frameInPsram[i] true; ui_splash_truck_start()/the timer callback only
// ever READ frames[i] after observing frameInPsram[i] true for that same
// i - a per-frame single-writer-then-flag handoff, not concurrent access
// to the same element, so no separate mutex is used. This holds even
// before preload has finished: frameInPsram[] is a zero-initialized
// static array, so an index the background task hasn't reached yet
// simply reads as "not ready" and the timer callback takes the SD-
// fallback path below for that one frame, same as a genuine PSRAM
// allocation failure - the two cases are indistinguishable to the
// reader, and both are already handled safely.
//
// If PSRAM allocation fails for a given frame (logged clearly, not
// silently), that frame falls back to being loaded from SD ON DEMAND
// during playback, through lv_fs_sd.h's registered "S:" driver
// (LV_IMG_SRC_FILE - LVGL's built-in decoder streams TRUE_COLOR pixel
// data directly from the file during the draw call itself, see
// lv_img_decoder_built_in_read_line() in LVGL's own lv_img_decoder.c -
// no separate full-file read implemented or needed here). This keeps the
// boot sequence alive either way - degraded (and, per the SD-fallback
// path's own nature, probably visibly stuttering on that one frame -
// logged when it happens) is fine, a boot-loop or a hard failure is not.

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SPLASH_TRUCK_FRAME_COUNT 22
// Must match convert_splash_frames.py's --size (default 320, also 320).
#define UI_SPLASH_TRUCK_FRAME_SIZE  320
#define SPLASH_FRAMES_DIR "/splash_frames"

// See this header's LOADING STRATEGY comment above. Safe to call even if
// the SD card isn't mounted, or the /splash_frames directory or any
// individual frame is missing - every failure path falls back to
// per-frame SD-on-demand rather than aborting. Returns immediately - the
// actual work happens on a background task.
void ui_splash_truck_preload(void);

// True once the background preload task has finished (successfully or
// not - a frame that fell back to SD-on-demand still counts as "handled,
// safe to start playback"). Poll this before calling
// ui_splash_truck_start() - see ui_splashScreen.cpp's poll loop.
bool ui_splash_truck_isPreloadDone(void);

// Starts the revolve loop on imgObj (an lv_img the caller already
// created and positioned/sized - this module only ever calls
// lv_img_set_src() on it, never touches its geometry). Steps through the
// 22 frames at UI_SPLASH_TRUCK_FRAME_PERIOD_MS, wrapping from frame 21
// back to frame 0 indefinitely. No-op if a loop is already running.
void ui_splash_truck_start(lv_obj_t * imgObj);

// Stops the loop immediately, mid-frame - does not wait for the current
// revolution to finish. Call this from the exact same place the splash
// screen's own exit gate (CAN arrival / timeout) or tap-to-skip handler
// fires, before the crossfade-to-telemetry transition starts.
void ui_splash_truck_stop(void);

#ifdef __cplusplus
}
#endif
