#pragma once

#include <Arduino.h>
#include "twai_lib.h"

// ============================================================================
// CANopen SDO client for ZombieVerter VCU
// ============================================================================
// Confirmed against Rob's own working SDOManager.cpp (Dial firmware) -
// this is a direct port of that encoding, not an inferred one.
// ============================================================================

// --- Node/COB-ID config -----------------------------------------------------
// ZOMBIE_NODE_ID is the CANopen node ID of the *VCU we are querying*, not this
// dash. An SDO client has no node ID of its own. The ZombieVerter is node 3
// (confirmed on Rob's bus), so requests go to 0x603 and responses come back on
// 0x583. Change this only if the VCU's node ID is reconfigured.
#define ZOMBIE_NODE_ID      3
#define SDO_TX_COBID        (0x600 + ZOMBIE_NODE_ID)   // request -> VCU (0x603)
#define SDO_RX_COBID        (0x580 + ZOMBIE_NODE_ID)   // response <- VCU (0x583)

// ZombieVerter folds a flat 16-bit param ID into CANopen's two-field
// (16-bit index + 8-bit subindex) addressing:
//   index    = SDO_BASE_INDEX | (paramId >> 8)
//   subindex = paramId & 0xFF
// This holds for the full ID range (1-255 legacy params and 2000+ spot
// telemetry values alike) - pure bit arithmetic, confirmed working in
// the Dial firmware's SDOManager.
#define SDO_BASE_INDEX      0x2100

// SDO command bytes (CANopen standard)
#define SDO_CMD_READ_REQ    0x40   // upload request (dash -> VCU)
#define SDO_CMD_WRITE_REQ   0x23   // expedited download request, 4 data bytes
#define SDO_CMD_RESP_4BYTE  0x43   // expedited upload response, 4 data bytes
#define SDO_CMD_RESP_2BYTE  0x4B   // expedited upload response, 2 data bytes
#define SDO_CMD_RESP_1BYTE  0x4F   // expedited upload response, 1 data byte
#define SDO_CMD_WRITE_ACK   0x60   // expedited download response (success)
#define SDO_CMD_ABORT       0x80   // error/abort response

// OpenInverter fixed-point scale: most params are stored as value * 32.
// TODO: confirm this against your Dial firmware - not all params
// necessarily use the same scale factor.
#define OI_FIXED_POINT_SCALE 32.0f

// --- Public API --------------------------------------------------------------

extern ESP32S3_TWAI can;

// Send an SDO read (upload) request for the given parameter ID.
// Non-blocking - the response arrives later via the receive loop.
bool sdoRequestRead(uint16_t paramId);

// Send an SDO write (download) request for the given parameter ID,
// with value already converted to the VCU's fixed-point integer format
// (e.g. call sdoWrite(25, (int32_t)(realValue * OI_FIXED_POINT_SCALE))).
bool sdoWrite(uint16_t paramId, int32_t rawValue);

// Parse a raw CAN frame that arrived on SDO_RX_COBID. If it's a valid
// read-response, fills outParamId and outValue and returns true.
bool sdoParseResponse(const uint8_t *data, uint8_t length,
                       uint16_t *outParamId, int32_t *outValue);

inline float sdoToFloat(int32_t rawValue) {
    return (float)rawValue / OI_FIXED_POINT_SCALE;
}
