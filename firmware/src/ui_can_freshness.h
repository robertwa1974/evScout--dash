#pragma once

// CAN telemetry freshness tracking (2026-09-18, styling/UX pass). Nothing
// like this existed before - myData/old_myData (zombie_updaters.h) are
// just zero-initialized structs overwritten field-by-field as SDO
// responses/broadcast frames arrive, with no timestamp, sequence counter,
// or "has real data arrived" flag anywhere. That gap caused a real bug:
// setWarnColor() (zombie_updaters.cpp) renders SOC and Pack Voltage in
// warning red at boot, before any CAN frame has ever arrived, because
// their threshold checks (soc <= lowSoc, packVoltage <= packVLow) are
// true against zero-initialized data. This module exists to fix that
// class of bug generally, and to drive the splash screen's "wait for
// first CAN frame" exit gate and the fault banner's CAN-dropout trigger.
//
// Two independent questions, two independent functions - do not conflate
// them:
//   hasEverReceived() - "has a frame EVER arrived since boot" - false only
//     once, forever (never resets true->false). Answers "is a 0 reading
//     real, or just because nothing has been decoded yet."
//   isLive() - "has a frame arrived within the last STALE_MS window" - can
//     flip both ways any number of times. Answers "was this working and
//     just stopped" (CAN dropout), a different question from the above.

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call from TaskCANReceiver (zombie_updaters.cpp) on every successfully
// parsed frame - both the SDO-response branch (after sdoParseResponse()
// succeeds) and the broadcast-frame branch (after decodeBroadcastFrame()).
// MUST be called with dataMutex already held (both real call sites already
// hold it for the myData write right next to this call) - this function
// does NOT take dataMutex itself, since dataMutex is a plain (non-
// recursive) FreeRTOS mutex and re-taking it here would deadlock the
// caller. Piggybacks on the caller's existing lock rather than adding a
// second one.
void ui_can_freshness_notifyFrame(void);

// True from the first frame ever received onward - never resets false
// once true. Cheap, lock-free single-word reads (see .cpp for why that's
// safe here) - callable from any task/context, including LVGL/UI code
// that isn't holding dataMutex.
bool ui_can_freshness_hasEverReceived(void);

// True if a frame has arrived within the last staleness window. Default
// window is a guess (2000ms) - this codebase's actual CAN broadcast/SDO-
// poll cadence hasn't been measured on a live bus yet (no bench access
// during this pass, see CLAUDE.md's deferred-verification note); retune
// once real traffic timing is known.
bool ui_can_freshness_isLive(void);

// Re-evaluates isLive()'s staleness window. Call once per slowUpdate()
// tick (zombie_updaters.cpp) - cheap millis() comparison, no I/O, safe
// from that Ticker context per firmware.ino's existing Ticker-vs-loop()
// division of labor. Not strictly required for hasEverReceived()/isLive()
// to return correct values (both are computed directly from
// lastRxMillis on every call, not cached) - this hook exists so a future
// caller (e.g. the fault banner) can react to a dropout at a predictable,
// bounded cadence rather than only when something else happens to poll.
void ui_can_freshness_tick(void);

#ifdef __cplusplus
}
#endif
