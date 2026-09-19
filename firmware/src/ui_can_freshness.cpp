// ui_can_freshness.cpp - see ui_can_freshness.h for scope/rationale.
#include "ui_can_freshness.h"
#include <Arduino.h>

#define UI_CAN_STALE_MS 2000  // guess, see header comment - retune on bench

// volatile, not dataMutex-guarded: notifyFrame() is called from inside an
// already-dataMutex-held block (can't re-take a non-recursive mutex from
// there without deadlocking), and the getters below are simple aligned
// 32-bit/bool reads - safe without a lock on this platform for a
// monotonically-written timestamp, the same lightweight-cross-task-flag
// pattern already used elsewhere in this codebase (e.g. gps_mock_source.cpp's
// s_lastTickMs). Not a general-purpose substitute for dataMutex where real
// multi-field consistency matters.
static volatile uint32_t s_lastRxMillis = 0;
static volatile bool s_everReceived = false;

void ui_can_freshness_notifyFrame(void) {
    s_lastRxMillis = millis();
    s_everReceived = true;
}

bool ui_can_freshness_hasEverReceived(void) {
    return s_everReceived;
}

bool ui_can_freshness_isLive(void) {
    if (!s_everReceived) return false;
    return (millis() - s_lastRxMillis) < UI_CAN_STALE_MS;
}

void ui_can_freshness_tick(void) {
    // isLive()/hasEverReceived() are computed fresh on every call, not
    // cached - this is a no-op today, kept as the documented hook point
    // for a future caller (fault banner) that wants a bounded-cadence
    // dropout check rather than only reacting when polled incidentally.
}
