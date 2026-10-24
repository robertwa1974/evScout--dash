#include "can_broadcast.h"
#include "zombieverter_params.h"
#include "zombie_updaters.h" // for the myData struct definition
#include "can_trace.h"

// Extracts a bit-aligned field from a CAN payload. All observed
// ZombieVerter broadcast fields are byte-aligned (offset is a multiple
// of 8), so this is really just byte extraction with a bit-length choice.
// When isSigned is set the raw field is sign-extended to int32_t before
// the caller applies gain (needed for idc, motor rpm in reverse, sub-zero
// temps - see kCanBroadcastParams).
static int32_t extractField(const uint8_t *data, uint8_t bitOffset, uint8_t bitLength, bool isSigned) {
    uint8_t byteOffset = bitOffset / 8;
    if (bitLength == 8) {
        uint8_t raw = data[byteOffset];
        return isSigned ? (int32_t)(int8_t)raw : (int32_t)raw;
    }
    // 16-bit, little-endian
    uint16_t raw = (uint16_t)data[byteOffset] | ((uint16_t)data[byteOffset + 1] << 8);
    return isSigned ? (int32_t)(int16_t)raw : (int32_t)raw;
}

// Broadcast values are traced only, never written to myData (2026-10-24).
// Rob's VCU does send these canmap frames (10 Hz, seen in the truck), but:
//  - every param here is also SDO-polled at full /32 resolution, and the
//    broadcast copy overwrote it every 100 ms - with udc at 0.09 gain that's
//    ~11 V per step, U12V ~11 V per step, rpm ~11 rpm per step;
//  - the old decode multiplied by the gain instead of dividing (the VCU's
//    canmap sends value x gain), so udc 366 V showed as 3.2 V and U12V
//    14.3 V as 0.09 V - caught by the CAN Monitor's shunt-vs-VCU check.
// SDO stays the single source for myData.
void decodeBroadcastFrame(uint32_t canId, const uint8_t *data, uint8_t length) {
    for (size_t i = 0; i < kCanBroadcastParamsCount; i++) {
        const CanBroadcastParam &p = kCanBroadcastParams[i];
        if (p.canId != canId) continue;

        uint8_t byteOffset = p.byteOffset / 8;
        uint8_t fieldBytes = p.bitLength / 8;
        if (byteOffset + fieldBytes > length) continue; // guard against short/malformed frames

        int32_t raw = extractField(data, p.byteOffset, p.bitLength, p.isSigned);
        float value = p.gain != 0.0f ? (float)raw / p.gain : (float)raw;  // canmap sends value x gain
        canTraceField(p.name, p.paramId, raw, value, p.unit);
    }
}
