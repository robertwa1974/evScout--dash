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

bool sdoWrite(uint16_t paramId, int32_t rawValue) {
    uint8_t data[8] = {0};

    uint16_t index    = SDO_BASE_INDEX | (paramId >> 8);
    uint8_t  subindex = paramId & 0xFF;

    data[0] = SDO_CMD_WRITE_REQ;
    data[1] = (uint8_t)(index & 0xFF);
    data[2] = (uint8_t)(index >> 8);
    data[3] = subindex;
    data[4] = (uint8_t)(rawValue & 0xFF);
    data[5] = (uint8_t)((rawValue >> 8) & 0xFF);
    data[6] = (uint8_t)((rawValue >> 16) & 0xFF);
    data[7] = (uint8_t)((rawValue >> 24) & 0xFF);

    canTraceSdoTx(SDO_TX_COBID, data, sizeof(data), paramId);
    return can.send(SDO_TX_COBID, data, sizeof(data), false);
}

bool sdoParseResponse(const uint8_t *data, uint8_t length,
                       uint16_t *outParamId, int32_t *outValue) {
    if (length < 4) {
        return false;
    }

    uint8_t cmd = data[0];
    uint16_t index = (uint16_t)(data[1] | (data[2] << 8));
    uint8_t subindex = data[3];

    // Reconstruct the flat 16-bit param ID from index + subindex
    uint16_t paramId = (uint16_t)(((index & 0xFF) << 8) | subindex);

    if (cmd == SDO_CMD_ABORT) {
        canTraceSdoRx(paramId, cmd, 0, 0.0f, false);
        return false;
    }

    if (cmd == SDO_CMD_WRITE_ACK) {
        return false; // write succeeded, no value to report
    }

    int32_t raw = 0;
    switch (cmd) {
        case SDO_CMD_RESP_4BYTE:
            if (length < 8) return false;
            raw = (int32_t)((uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                             ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24));
            break;
        case SDO_CMD_RESP_2BYTE:
            if (length < 6) return false;
            raw = (int16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
            break;
        case SDO_CMD_RESP_1BYTE:
            if (length < 5) return false;
            raw = (int8_t)data[4];
            break;
        default:
            return false; // unrecognized command byte
    }

    canTraceSdoRx(paramId, cmd, raw, sdoToFloat(raw), true);

    *outParamId = paramId;
    *outValue = raw;
    return true;
}
