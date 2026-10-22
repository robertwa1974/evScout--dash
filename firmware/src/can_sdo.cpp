#include "can_sdo.h"
#include "can_trace.h"

// Encoding confirmed against Rob's own SDOManager.cpp (Dial firmware).

bool sdoRequestRead(uint16_t paramId) {
    uint8_t data[8] = {0};

    uint16_t index    = SDO_BASE_INDEX | (paramId >> 8);
    uint8_t  subindex = paramId & 0xFF;

    data[0] = SDO_CMD_READ_REQ;
    data[1] = (uint8_t)(index & 0xFF);
    data[2] = (uint8_t)(index >> 8);
    data[3] = subindex;
    // bytes 4-7 stay zero for a read request

    canTraceSdoTx(SDO_TX_COBID, data, sizeof(data), paramId);
    return can.send(SDO_TX_COBID, data, sizeof(data), false); // standard (11-bit) ID
}

bool sdoWriteRaw(uint16_t index, uint8_t subindex, int32_t rawValue) {
    uint8_t data[8] = {0};

    data[0] = SDO_CMD_WRITE_REQ;
    data[1] = (uint8_t)(index & 0xFF);
    data[2] = (uint8_t)(index >> 8);
    data[3] = subindex;
    data[4] = (uint8_t)(rawValue & 0xFF);
    data[5] = (uint8_t)((rawValue >> 8) & 0xFF);
    data[6] = (uint8_t)((rawValue >> 16) & 0xFF);
    data[7] = (uint8_t)((rawValue >> 24) & 0xFF);

    canTraceSdoTx(SDO_TX_COBID, data, sizeof(data), sdoParamIdFromIndex(index, subindex));
    return can.send(SDO_TX_COBID, data, sizeof(data), false);
}

bool sdoWrite(uint16_t paramId, int32_t rawValue) {
    return sdoWriteRaw(SDO_BASE_INDEX | (paramId >> 8), paramId & 0xFF, rawValue);
}

static int32_t readLe32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

SdoResult sdoParseResponseEx(const uint8_t *data, uint8_t length,
                             uint16_t *outIndex, uint8_t *outSubindex, int32_t *outValue) {
    if (length < 4) {
        return SDO_RESULT_INVALID;
    }

    uint8_t cmd = data[0];
    uint16_t index = (uint16_t)(data[1] | (data[2] << 8));
    uint8_t subindex = data[3];
    uint16_t paramId = sdoParamIdFromIndex(index, subindex);
    *outIndex = index;
    *outSubindex = subindex;

    if (cmd == SDO_CMD_ABORT) {
        canTraceSdoRx(paramId, cmd, 0, 0.0f, false);
        if (length >= 8) *outValue = readLe32(&data[4]);
        else *outValue = 0;
        return SDO_RESULT_ABORT;
    }

    if (cmd == SDO_CMD_WRITE_ACK) {
        return SDO_RESULT_WRITE_ACK;
    }

    int32_t raw = 0;
    switch (cmd) {
        case SDO_CMD_RESP_4BYTE:
            if (length < 8) return SDO_RESULT_INVALID;
            raw = readLe32(&data[4]);
            break;
        case SDO_CMD_RESP_2BYTE:
            if (length < 6) return SDO_RESULT_INVALID;
            raw = (int16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
            break;
        case SDO_CMD_RESP_1BYTE:
            if (length < 5) return SDO_RESULT_INVALID;
            raw = (int8_t)data[4];
            break;
        default:
            return SDO_RESULT_INVALID; // unrecognized command byte
    }

    canTraceSdoRx(paramId, cmd, raw, sdoToFloat(raw), true);

    *outValue = raw;
    return SDO_RESULT_VALUE;
}

bool sdoParseResponse(const uint8_t *data, uint8_t length,
                       uint16_t *outParamId, int32_t *outValue) {
    uint16_t index;
    uint8_t subindex;
    int32_t raw;
    if (sdoParseResponseEx(data, length, &index, &subindex, &raw) != SDO_RESULT_VALUE) {
        return false;
    }
    *outParamId = sdoParamIdFromIndex(index, subindex);
    *outValue = raw;
    return true;
}
