#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// On-demand SDO access (2026-10-21) - everything beyond the fixed telemetry
// poll table in zombie_updaters.cpp: reading arbitrary params for the
// Settings > Diagnostics and Settings > VCU Parameters pages, writing a
// tunable, and telling the VCU to save its params to flash.
//
// Shares the existing 50ms SDO poll slot rather than sending on its own
// schedule: zombie_updaters.cpp's sendNextPollRequest() alternates between
// its own table and sdo_client_poll_slot(), so the bus never sees more than
// one request per 50ms no matter what's queued here. With nothing watched
// and no write pending, poll_slot() sends nothing and the main table runs
// at its normal full rate.
//
// Responses are matched by param ID, not by transaction - same as the main
// poll table. sdo_client_on_*() are fed every response TaskCANReceiver sees,
// including the main table's own, so a watched param that's also in the
// poll table stays fresh without a second request ever being sent for it.
//
// Thread safety: called from three contexts (UI/loop task, the poll Ticker's
// esp_timer task, TaskCANReceiver) - all state is behind one spinlock, and
// no CAN send happens while it's held.
// ============================================================================

#define SDO_CLIENT_MAX_WATCH 48

typedef enum {
    SDO_VALUE_PENDING,      // requested, no response yet
    SDO_VALUE_VALID,
    SDO_VALUE_UNSUPPORTED,  // VCU aborted the read (param doesn't exist on this firmware)
    SDO_VALUE_NOT_WATCHED,
} sdo_value_state_t;

typedef enum {
    SDO_OP_IDLE,
    SDO_OP_QUEUED,   // waiting for the next poll slot
    SDO_OP_SENT,     // waiting for the VCU's ack
    SDO_OP_OK,
    SDO_OP_FAILED,   // VCU aborted - see abort code
    SDO_OP_TIMEOUT,  // no answer
    SDO_OP_REFUSED,  // vehicle not parked / no CAN - never sent
} sdo_op_state_t;

#ifdef __cplusplus
extern "C" {
#endif

// Replaces the watch list. refreshMs = 0 reads each param once (tunables
// that only change when written); > 0 re-reads every refreshMs (live
// telemetry). Pass n = 0 (or call unwatch) to stop all extra polling.
void sdo_client_watch(const uint16_t *ids, size_t n, uint32_t refreshMs);
void sdo_client_unwatch(void);

// Forces a re-read of one watched param on the next free slot.
void sdo_client_refresh(uint16_t id);

// Latest value of a watched param (converted with the /32 fixed-point scale).
sdo_value_state_t sdo_client_get(uint16_t id, float *outValue);

// Write / save. Both refuse (return false, state SDO_OP_REFUSED) unless the
// param's interlock allows it (sdo_client_write_allowed(); save always needs
// sdo_client_vehicle_parked()), and refuse while another op is QUEUED/SENT.
// A successful param write automatically re-reads that param if watched.
bool sdo_client_write_param(uint16_t id, float value);
bool sdo_client_save_to_flash(void);
sdo_op_state_t sdo_client_op_state(uint32_t *outAbortCode);
void sdo_client_op_clear(void);  // back to IDLE once the UI has shown the result

// Strictest interlock - tuning writes (Settings > VCU Parameters) and save to
// flash: CAN data is live, the vehicle is stopped (Veh_Speed == 0), and the
// VCU is not in Run (opmode 1).
// Charging (opmode 4) is allowed, matching what the openinverter web UI
// permits, so charge setpoints can be adjusted on the plug.
bool sdo_client_vehicle_parked(void);

// Per-param write interlock (2026-10-21, matching Rob's Dial firmware for
// the four Drive-screen params - see Zombieverter-Dial-Display/src/main.cpp):
//   regenmax (61), regenBrake (122)  - CAN live only: adjustable WHILE
//                                       DRIVING, as on the Dial
//   Gear (27), MotActive (129)        - CAN live + stopped (Veh_Speed 0 and
//                                       |motor rpm| <= 50, the Dial's own
//                                       threshold); Run mode is fine
//   everything else                   - sdo_client_vehicle_parked()
// reason (optional) gets a short user-facing explanation when false.
bool sdo_client_write_allowed(uint16_t paramId, const char **reason);

// Human-readable reason for the last FAILED op ("Out of range", ...).
const char *sdo_client_abort_text(uint32_t abortCode);

// --- Hooks for zombie_updaters.cpp only ------------------------------------
// Called from the poll Ticker on alternate slots. Returns true if it sent a
// frame (the slot is used up).
bool sdo_client_poll_slot(void);
void sdo_client_on_value(uint16_t paramId, int32_t raw);
void sdo_client_on_write_ack(uint16_t index, uint8_t subindex);
void sdo_client_on_abort(uint16_t index, uint8_t subindex, uint32_t abortCode);

#ifdef __cplusplus
}
#endif
