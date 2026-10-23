#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// CAN bus monitor - per-ID capture for Settings > CAN Monitor (2026-10-22)
// ============================================================================
// Purpose (Rob): check the dash's own bus setup (VCU, IVT-S shunt, BMS) and
// debug new devices as they're added - NOT a full decoder; real decoding
// happens in SavvyCAN on a separate bench setup.
//
// TaskCANReceiver hands every received frame to can_monitor_on_frame()
// (one spinlock + table lookup per frame - cheap even at bus-load rates).
// Only while active (the page is on screen), so it costs nothing otherwise.
// The UI reads snapshots via can_monitor_snapshot() on its own timer,
// never per frame.
//
// Note: the dash's own transmissions (SDO requests on 0x603) are not
// received back by the TWAI controller, so they don't appear here; the
// VCU's replies on 0x583 do.
// ============================================================================

#define CAN_MONITOR_MAX_IDS 64

typedef struct {
    uint32_t id;
    bool     extended;
    uint8_t  len;
    uint8_t  data[8];
    uint8_t  changedMask;   // bit n = data[n] changed within the last ~1s
    uint32_t count;         // frames since last clear
    float    rateHz;        // over the last rate window
    uint32_t ageMs;         // since last seen
} can_monitor_row_t;

typedef struct {
    uint32_t totalFrames;   // since last clear
    float    framesPerSec;
    float    busLoadPct;    // estimate: frames/s x ~125 bits / 500kbit
    uint32_t tableFull;     // frames dropped because CAN_MONITOR_MAX_IDS distinct IDs were already seen
    // Latest IVT-S readings (stage 2), for the shunt-vs-VCU cross-check.
    // Valid = a frame arrived within the last 3s.
    bool     shuntCurrentValid, shuntVoltageValid;
    float    shuntCurrentA, shuntVoltageV;
} can_monitor_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

void can_monitor_set_active(bool active);

// Called from TaskCANReceiver for every frame. No-op while inactive.
void can_monitor_on_frame(uint32_t id, bool extended, const uint8_t *data, uint8_t len);

// Copies up to maxRows rows, sorted by ID, plus totals. Also advances the
// rate/changed-byte windows - call it on a steady UI timer (e.g. 250ms).
size_t can_monitor_snapshot(can_monitor_row_t *rows, size_t maxRows, can_monitor_stats_t *stats);

void can_monitor_clear(void);

// Short known-device name for an ID ("IVT-S Current", "SDO response", ...)
// or NULL. Names come from Rob's zombieverter_ecosystem.dbc
// (Zombieverter-Dial-Display/data) plus this project's own SDO IDs.
const char *can_monitor_id_name(uint32_t id, bool extended);

// Stage 2 (2026-10-22): human-readable value for the devices on Rob's bus,
// or false (caller shows hex). Deliberately light - real decoding is done
// in SavvyCAN on the bench. Scalings are taken from code that actually
// talks to this hardware, not guessed:
//   0x583 SDO reply  - param name + value (zombieverter_params.h tables)
//   0x521-0x528 IVT-S - little-endian int32 in bytes 2-5, scaled exactly as
//                       the ZombieVerter does (Stm32-vcu utils.cpp /
//                       isa_shunt.cpp): mA, mV, 0.1C, W, As, Wh
//   0x351/0x35A/0x35E/0x373 SimpBMS - per zombieverter_ecosystem.dbc
bool can_monitor_decode(uint32_t id, bool extended, const uint8_t *data, uint8_t len,
                        char *out, size_t outLen);

// --- Stage 3: known-good baseline (2026-10-22) -----------------------------
// "How my bus normally looks": the set of IDs seen, with their rates, saved
// on request from the live table. Stored in NVS (namespace "canmon"), not SD,
// so it works without a card. The CAN Monitor page compares live traffic
// against it to flag NEW / MISSING / RATE.

typedef struct {
    uint32_t id;
    bool     extended;
    float    rateHz;
} can_monitor_base_t;

// Loads the saved baseline from NVS. Call once (the page does, on creation).
void can_monitor_baseline_load(void);

// Replaces the baseline with every ID seen in the last STALE window of the
// live table, with its current rate, and persists it. Returns the ID count.
size_t can_monitor_baseline_save(void);

// Erases the baseline (RAM + NVS).
void can_monitor_baseline_forget(void);

size_t can_monitor_baseline_count(void);
const can_monitor_base_t *can_monitor_baseline_at(size_t i);
// NULL if the ID isn't in the baseline.
const can_monitor_base_t *can_monitor_baseline_find(uint32_t id, bool extended);

#ifdef __cplusplus
}
#endif
