#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Raw CAN capture to SD in SavvyCAN's native CSV format (2026-10-22, stage 4
// of the CAN Monitor plan)
// ============================================================================
// Purpose: record the real bus in the vehicle, then open the file in SavvyCAN
// on the bench for full decoding (Rob's workflow - the dash itself only does
// light decoding, see can_monitor.h).
//
// File: /can/CAN0001.csv, CAN0002.csv, ... (counter in NVS, namespace
// "canmon"), one per capture session. Format - SavvyCAN's "GVRET native"
// CSV, the format SavvyCAN itself saves:
//   Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
//   1234567,00000583,false,Rx,0,8,43,06,21,00,00,2C,00,00,
// (trailing comma included - matches a real SavvyCAN capture exactly)
// Time Stamp is microseconds since capture start.
//
// Includes the dash's OWN transmissions (SDO requests on 0x603, marked Tx -
// the TWAI controller never receives its own frames back, so can_sdo.cpp
// reports them here directly), so request/response pairs read correctly.
// The "SDO" option drops 0x603/0x583 both ways, leaving only other devices.
//
// Producers (CAN RX task, SDO senders) push into a PSRAM ring under a
// spinlock - no SD I/O in their context. can_capture_poll(), from
// firmware.ino's loop(), drains the ring and writes batches under sdMutex -
// same normal-task-only SD rule as data_logger.h. Ring overflow is counted
// as "dropped", never blocks the CAN task.
//
// Keeps running when the CAN Monitor page is closed (start it, then drive);
// stops on STOP or an SD error. Not resumed after a reboot - each capture is
// a deliberate session, unlike data_logger.h's always-on telemetry log.
// ============================================================================

#define CAN_CAPTURE_DIR "/can"

#ifdef __cplusplus
extern "C" {
#endif

bool can_capture_start(void);   // false: no SD card / can't create file / no PSRAM - see status text
void can_capture_stop(void);
bool can_capture_is_running(void);

void can_capture_set_include_sdo(bool include);
bool can_capture_include_sdo(void);

// Producers - safe from any task, cheap no-op while not capturing.
void can_capture_on_frame(uint32_t id, bool extended, const uint8_t *data, uint8_t len, bool tx);

// Call every loop() iteration.
void can_capture_poll(void);

// One-line status for the CAN Monitor page; valid until the next call.
const char *can_capture_status_text(void);

// Bare filename being written ("CAN0003.csv") or "" - the web server
// flushes before serving it and refuses to delete it.
const char *can_capture_current_file(void);
void can_capture_flush(void);

#ifdef __cplusplus
}
#endif
