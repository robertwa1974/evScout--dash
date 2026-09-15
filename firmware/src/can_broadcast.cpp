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

// Maps a decoded broadcast value to the right myData field by param ID.
static void applyBroadcastValue(uint16_t paramId, float value) {
    switch (paramId) {
        case 2028: myData.heatsinkTemp   = value;       break; // tmphs
        case 2070: myData.aux12vVoltage  = value;        break; // U12V
        case 2016: myData.motorRpm       = (int)value;   break; // "speed" - actually motor rpm
        case 27:   myData.gear           = (int)value;   break;
        case 129:  myData.motActive      = (int)value;   break;
        case 61:   myData.regenMax       = value;         break;
        case 2015: myData.soc            = (int)value;   break;
        case 2006: myData.packVoltage    = value;         break; // udc
        case 2012: myData.packCurrent    = value;         break; // idc
        case 2029: myData.motorTemp      = value;         break; // tmpm
        default: break; // param not wired to a myData field yet
    }
}

void decodeBroadcastFrame(uint32_t canId, const uint8_t *data, uint8_t length) {
    for (size_t i = 0; i < kCanBroadcastParamsCount; i++) {
        const CanBroadcastParam &p = kCanBroadcastParams[i];
        if (p.canId != canId) continue;

        uint8_t byteOffset = p.byteOffset / 8;
        uint8_t fieldBytes = p.bitLength / 8;
        if (byteOffset + fieldBytes > length) continue; // guard against short/malformed frames

        int32_t raw = extractField(data, p.byteOffset, p.bitLength, p.isSigned);
        float value = (float)raw * p.gain;
        canTraceField(p.name, p.paramId, raw, value, p.unit);
        applyBroadcastValue(p.paramId, value);
    }
}
