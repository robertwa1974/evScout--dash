#include "sdo_client.h"
#include "can_sdo.h"
#include "zombie_updaters.h"  // myData, dataMutex, PARAM_ID_* - parked interlock
#include "ui_can_freshness.h"
#include <Arduino.h>
#include <math.h>

#define RETRY_UNANSWERED_MS 500   // re-send a read that never got a response
#define OP_TIMEOUT_MS       1000  // param write ack
#define SAVE_TIMEOUT_MS     3000  // flash save - VCU erases/writes a flash page before acking

typedef struct {
    uint16_t id;
    int32_t  raw;
    uint32_t lastTouchMs;   // last request sent OR response received, whichever is later
    bool     requested;     // at least one request sent since (re)watch/refresh
    sdo_value_state_t state;
} WatchEntry;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static WatchEntry watch[SDO_CLIENT_MAX_WATCH];
static size_t watchCount = 0;
static size_t watchCursor = 0;
static uint32_t watchRefreshMs = 0;

static sdo_op_state_t opState = SDO_OP_IDLE;
static uint16_t opIndex = 0;
static uint8_t  opSubindex = 0;
static int32_t  opRaw = 0;
static uint32_t opSentMs = 0;
static uint32_t opTimeoutMs = OP_TIMEOUT_MS;
static uint32_t opAbortCode = 0;

static WatchEntry *findEntry(uint16_t id) {
    for (size_t i = 0; i < watchCount; i++) {
        if (watch[i].id == id) return &watch[i];
    }
    return NULL;
}

void sdo_client_watch(const uint16_t *ids, size_t n, uint32_t refreshMs) {
    if (n > SDO_CLIENT_MAX_WATCH) n = SDO_CLIENT_MAX_WATCH;
    portENTER_CRITICAL(&lock);
    for (size_t i = 0; i < n; i++) {
        watch[i].id = ids[i];
        watch[i].raw = 0;
        watch[i].lastTouchMs = 0;
        watch[i].requested = false;
        watch[i].state = SDO_VALUE_PENDING;
    }
    watchCount = n;
    watchCursor = 0;
    watchRefreshMs = refreshMs;
    portEXIT_CRITICAL(&lock);
}

void sdo_client_unwatch(void) {
    sdo_client_watch(NULL, 0, 0);
}

void sdo_client_refresh(uint16_t id) {
    portENTER_CRITICAL(&lock);
    WatchEntry *e = findEntry(id);
    if (e) {
        e->requested = false;
        if (e->state == SDO_VALUE_UNSUPPORTED) e->state = SDO_VALUE_PENDING;
    }
    portEXIT_CRITICAL(&lock);
}

sdo_value_state_t sdo_client_get(uint16_t id, float *outValue) {
    sdo_value_state_t st = SDO_VALUE_NOT_WATCHED;
    portENTER_CRITICAL(&lock);
    WatchEntry *e = findEntry(id);
    if (e) {
        st = e->state;
        if (outValue) *outValue = sdoToFloat(e->raw);
    }
    portEXIT_CRITICAL(&lock);
    return st;
}

bool sdo_client_vehicle_parked(void) {
    if (!ui_can_freshness_isLive()) return false;
    bool parked = false;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        parked = (myData.vehSpeedKph == 0) && (myData.opmode != 1);
        xSemaphoreGive(dataMutex);
    }
    return parked;
}

bool sdo_client_write_allowed(uint16_t paramId, const char **reason) {
    const char *why = NULL;
    if (!ui_can_freshness_isLive()) {
        why = "No CAN data from VCU";
    } else if (paramId == PARAM_ID_REGENMAX || paramId == PARAM_ID_REGENBRAKE) {
        // Live-adjustable, like the Dial.
    } else if (paramId == PARAM_ID_GEAR || paramId == PARAM_ID_MOT_ACTIVE) {
        int kph = 0, rpm = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            kph = myData.vehSpeedKph;
            rpm = myData.motorRpm;
            xSemaphoreGive(dataMutex);
        }
        if (kph != 0 || abs(rpm) > 50) why = "Stop the vehicle to change this";
    } else if (!sdo_client_vehicle_parked()) {
        why = "Vehicle must be stopped and not in Run";
    }
    if (reason) *reason = why;
    return why == NULL;
}

static bool queueOp(uint16_t index, uint8_t subindex, int32_t raw, uint32_t timeoutMs, bool allowed) {
    bool parked = allowed;  // interlock evaluated by the caller - takes dataMutex, must be outside the spinlock
    bool ok = false;
    portENTER_CRITICAL(&lock);
    if (opState != SDO_OP_QUEUED && opState != SDO_OP_SENT) {
        if (parked) {
            opIndex = index;
            opSubindex = subindex;
            opRaw = raw;
            opTimeoutMs = timeoutMs;
            opAbortCode = 0;
            opState = SDO_OP_QUEUED;
            ok = true;
        } else {
            opState = SDO_OP_REFUSED;
        }
    }
    portEXIT_CRITICAL(&lock);
    return ok;
}

bool sdo_client_write_param(uint16_t id, float value) {
    int32_t raw = (int32_t)lroundf(value * OI_FIXED_POINT_SCALE);
    return queueOp(SDO_BASE_INDEX | (id >> 8), id & 0xFF, raw, OP_TIMEOUT_MS, sdo_client_write_allowed(id, NULL));
}

bool sdo_client_save_to_flash(void) {
    return queueOp(SDO_INDEX_COMMANDS, SDO_SUBCMD_SAVE, 0, SAVE_TIMEOUT_MS, sdo_client_vehicle_parked());
}

sdo_op_state_t sdo_client_op_state(uint32_t *outAbortCode) {
    portENTER_CRITICAL(&lock);
    sdo_op_state_t st = opState;
    if (outAbortCode) *outAbortCode = opAbortCode;
    portEXIT_CRITICAL(&lock);
    return st;
}

void sdo_client_op_clear(void) {
    portENTER_CRITICAL(&lock);
    if (opState != SDO_OP_QUEUED && opState != SDO_OP_SENT) opState = SDO_OP_IDLE;
    portEXIT_CRITICAL(&lock);
}

const char *sdo_client_abort_text(uint32_t abortCode) {
    switch (abortCode) {
        case SDO_ABORT_RANGE:     return "Value out of range";
        case SDO_ABORT_NO_OBJECT: return "Parameter not found";
        default:                  return "Rejected by VCU";
    }
}

// Is this entry due for a (re-)read right now?
static bool entryDue(const WatchEntry *e, uint32_t now) {
    if (e->state == SDO_VALUE_UNSUPPORTED) return false;
    if (!e->requested) return true;
    uint32_t age = now - e->lastTouchMs;
    if (e->state == SDO_VALUE_PENDING) return age >= RETRY_UNANSWERED_MS;
    return watchRefreshMs > 0 && age >= watchRefreshMs;
}

bool sdo_client_poll_slot(void) {
    uint32_t now = millis();
    enum { NONE, SEND_OP, SEND_READ } action = NONE;
    uint16_t idx = 0, readId = 0;
    uint8_t sub = 0;
    int32_t raw = 0;

    portENTER_CRITICAL(&lock);
    if (opState == SDO_OP_SENT && now - opSentMs > opTimeoutMs) {
        opState = SDO_OP_TIMEOUT;
    }
    if (opState == SDO_OP_QUEUED) {
        action = SEND_OP;
        idx = opIndex;
        sub = opSubindex;
        raw = opRaw;
        opState = SDO_OP_SENT;
        opSentMs = now;
    } else {
        for (size_t n = 0; n < watchCount; n++) {
            WatchEntry *e = &watch[watchCursor];
            watchCursor = (watchCursor + 1) % watchCount;
            if (entryDue(e, now)) {
                e->requested = true;
                e->lastTouchMs = now;
                readId = e->id;
                action = SEND_READ;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&lock);

    if (action == SEND_OP) {
        sdoWriteRaw(idx, sub, raw);
        return true;
    }
    if (action == SEND_READ) {
        sdoRequestRead(readId);
        return true;
    }
    return false;
}

void sdo_client_on_value(uint16_t paramId, int32_t raw) {
    portENTER_CRITICAL(&lock);
    WatchEntry *e = findEntry(paramId);
    if (e) {
        e->raw = raw;
        e->state = SDO_VALUE_VALID;
        e->requested = true;
        e->lastTouchMs = millis();
    }
    portEXIT_CRITICAL(&lock);
}

void sdo_client_on_write_ack(uint16_t index, uint8_t subindex) {
    portENTER_CRITICAL(&lock);
    if (opState == SDO_OP_SENT && index == opIndex && subindex == opSubindex) {
        opState = SDO_OP_OK;
        if (sdoIndexIsParam(index)) {
            // Read back what the VCU actually stored, not what we sent.
            WatchEntry *e = findEntry(sdoParamIdFromIndex(index, subindex));
            if (e) e->requested = false;
        }
    }
    portEXIT_CRITICAL(&lock);
}

void sdo_client_on_abort(uint16_t index, uint8_t subindex, uint32_t abortCode) {
    portENTER_CRITICAL(&lock);
    if (opState == SDO_OP_SENT && index == opIndex && subindex == opSubindex) {
        opState = SDO_OP_FAILED;
        opAbortCode = abortCode;
    } else if (sdoIndexIsParam(index)) {
        WatchEntry *e = findEntry(sdoParamIdFromIndex(index, subindex));
        if (e) e->state = SDO_VALUE_UNSUPPORTED;
    }
    portEXIT_CRITICAL(&lock);
}
