#include "can_monitor.h"
#include <Arduino.h>
#include "can_sdo.h"  // SDO_TX_COBID / SDO_RX_COBID, sdoParseResponseEx()
#include "zombieverter_params.h"
#include <string.h>
#include <stdio.h>

#define IVT_CURRENT_ID 0x521
#define IVT_U1_ID      0x522
#define SHUNT_FRESH_MS 3000

#define CHANGED_WINDOW_MS 1000
#define BITS_PER_FRAME    125      // 8-byte standard frame incl. typical bit stuffing
#define BUS_BITRATE       500000.0f

typedef struct {
    uint32_t id;
    bool     extended;
    uint8_t  len;
    uint8_t  data[8];
    uint8_t  changedMask;
    uint32_t changedAtMs[8];
    uint32_t count;
    uint32_t countAtWindow;
    float    rateHz;
    uint32_t lastMs;
    bool     seenOnce;
} Entry;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool active = false;
static Entry entries[CAN_MONITOR_MAX_IDS];
static size_t entryCount = 0;
static uint32_t totalFrames = 0, totalAtWindow = 0, tableFull = 0;
static uint32_t windowStartMs = 0;
static float framesPerSec = 0;
static float shuntI = 0, shuntU = 0;
static uint32_t shuntIMs = 0, shuntUMs = 0;
static bool shuntISeen = false, shuntUSeen = false;

// IVT-S result value: bytes 2-5, little-endian signed (as the ZombieVerter
// configures and reads the shunt - see can_monitor.h).
static int32_t ivtValue(const uint8_t *d) {
    return (int32_t)((uint32_t)d[2] | ((uint32_t)d[3] << 8) | ((uint32_t)d[4] << 16) | ((uint32_t)d[5] << 24));
}

void can_monitor_set_active(bool a) {
    if (a && !active) can_monitor_clear();
    active = a;
}

void can_monitor_clear(void) {
    portENTER_CRITICAL(&lock);
    entryCount = 0;
    totalFrames = totalAtWindow = tableFull = 0;
    shuntISeen = shuntUSeen = false;
    framesPerSec = 0;
    windowStartMs = millis();
    portEXIT_CRITICAL(&lock);
}

void can_monitor_on_frame(uint32_t id, bool extended, const uint8_t *data, uint8_t len) {
    if (!active) return;
    if (len > 8) len = 8;
    uint32_t now = millis();
    portENTER_CRITICAL(&lock);
    totalFrames++;
    Entry *e = NULL;
    for (size_t i = 0; i < entryCount; i++) {
        if (entries[i].id == id && entries[i].extended == extended) { e = &entries[i]; break; }
    }
    if (!e) {
        if (entryCount < CAN_MONITOR_MAX_IDS) {
            e = &entries[entryCount++];
            memset(e, 0, sizeof(*e));
            e->id = id;
            e->extended = extended;
        } else {
            tableFull++;
        }
    }
    if (e) {
        for (uint8_t b = 0; b < len; b++) {
            if (e->seenOnce && e->data[b] != data[b]) e->changedAtMs[b] = now;
            e->data[b] = data[b];
        }
        e->len = len;
        e->count++;
        e->lastMs = now;
        e->seenOnce = true;
    }
    if (!extended && len >= 6) {
        if (id == IVT_CURRENT_ID) { shuntI = ivtValue(data) / 1000.0f; shuntIMs = now; shuntISeen = true; }
        else if (id == IVT_U1_ID) { shuntU = ivtValue(data) / 1000.0f; shuntUMs = now; shuntUSeen = true; }
    }
    portEXIT_CRITICAL(&lock);
}

static int cmpRow(const void *a, const void *b) {
    const can_monitor_row_t *ra = (const can_monitor_row_t *)a, *rb = (const can_monitor_row_t *)b;
    if (ra->extended != rb->extended) return ra->extended ? 1 : -1;
    return (ra->id > rb->id) - (ra->id < rb->id);
}

size_t can_monitor_snapshot(can_monitor_row_t *rows, size_t maxRows, can_monitor_stats_t *stats) {
    uint32_t now = millis();
    size_t n = 0;
    portENTER_CRITICAL(&lock);
    // Rate window: recompute rates once per second.
    uint32_t span = now - windowStartMs;
    if (span >= 1000) {
        float secs = span / 1000.0f;
        for (size_t i = 0; i < entryCount; i++) {
            entries[i].rateHz = (entries[i].count - entries[i].countAtWindow) / secs;
            entries[i].countAtWindow = entries[i].count;
        }
        framesPerSec = (totalFrames - totalAtWindow) / secs;
        totalAtWindow = totalFrames;
        windowStartMs = now;
    }
    for (size_t i = 0; i < entryCount && n < maxRows; i++) {
        const Entry *e = &entries[i];
        can_monitor_row_t *r = &rows[n++];
        r->id = e->id;
        r->extended = e->extended;
        r->len = e->len;
        memcpy(r->data, e->data, 8);
        r->changedMask = 0;
        for (uint8_t b = 0; b < e->len; b++) {
            if (e->changedAtMs[b] && now - e->changedAtMs[b] < CHANGED_WINDOW_MS) r->changedMask |= (1u << b);
        }
        r->count = e->count;
        r->rateHz = e->rateHz;
        r->ageMs = now - e->lastMs;
    }
    if (stats) {
        stats->totalFrames = totalFrames;
        stats->framesPerSec = framesPerSec;
        stats->busLoadPct = framesPerSec * BITS_PER_FRAME / BUS_BITRATE * 100.0f;
        stats->tableFull = tableFull;
        stats->shuntCurrentValid = shuntISeen && now - shuntIMs < SHUNT_FRESH_MS;
        stats->shuntVoltageValid = shuntUSeen && now - shuntUMs < SHUNT_FRESH_MS;
        stats->shuntCurrentA = shuntI;
        stats->shuntVoltageV = shuntU;
    }
    portEXIT_CRITICAL(&lock);
    qsort(rows, n, sizeof(rows[0]), cmpRow);
    return n;
}

const char *can_monitor_id_name(uint32_t id, bool extended) {
    if (extended) return NULL;
    if (id == SDO_TX_COBID) return "SDO request";
    if (id == SDO_RX_COBID) return "SDO response (VCU)";
    switch (id) {
        // ZombieVerter canmap broadcasts (dormant on Rob's VCU - see zombie_updaters.h)
        case 0x126: return "ZV heatsink temp";
        case 0x210: return "ZV 12V";
        case 0x257: return "ZV motor rpm";
        case 0x300: return "ZV gear";
        case 0x301: return "ZV motor active";
        case 0x302: return "ZV regen max";
        case 0x355: return "SOC (ZV / BMS)";    // DBC: ZV and SimpBMS both use 0x355/0x356
        case 0x356: return "ZV pack / BMS";
        // Isabellenhuette IVT-S shunt (defaults; ZombieVerter's IsaInit sets it up)
        case 0x411: return "IVT-S command";
        case 0x511: return "IVT-S response";
        case 0x521: return "IVT-S current";
        case 0x522: return "IVT-S U1 (pack)";
        case 0x523: return "IVT-S U2";
        case 0x524: return "IVT-S U3";
        case 0x525: return "IVT-S temp";
        case 0x526: return "IVT-S power";
        case 0x527: return "IVT-S Ah";
        case 0x528: return "IVT-S Wh";
        // SimpBMS
        case 0x351: return "BMS limits";
        case 0x35A: return "BMS alarms";
        case 0x35E: return "BMS name";
        case 0x373: return "BMS cell data";
        default:    return NULL;
    }
}

// --- Stage 2 decoding ----------------------------------------------------------

// Param name + display unit for an SDO reply. Enum params carry their value
// list in the unit field ("0=Off, 1=On, ...") - too long for a table cell,
// so only short units are shown.
static void sdoParamInfo(uint16_t paramId, const char **name, const char **unit) {
    *name = NULL;
    *unit = "";
    for (size_t i = 0; i < kTelemetryParamsCount; i++) {
        if (kTelemetryParams[i].paramId == paramId) { *name = kTelemetryParams[i].name; *unit = kTelemetryParams[i].unit; break; }
    }
    if (!*name) {
        for (size_t i = 0; i < kSdoParamsCount; i++) {
            if (kSdoParams[i].paramId == paramId) { *name = kSdoParams[i].name; *unit = kSdoParams[i].unit; break; }
        }
    }
    if (strlen(*unit) > 5) *unit = "";
}

static uint16_t le16(const uint8_t *d) { return (uint16_t)(d[0] | (d[1] << 8)); }

bool can_monitor_decode(uint32_t id, bool extended, const uint8_t *data, uint8_t len,
                        char *out, size_t outLen) {
    if (extended) return false;

    if (id == SDO_RX_COBID) {
        uint16_t index; uint8_t sub; int32_t raw;
        SdoResult res = sdoParseResponseEx(data, len, &index, &sub, &raw);
        if (res == SDO_RESULT_INVALID) return false;
        if (!sdoIndexIsParam(index)) {
            snprintf(out, outLen, "%s 0x%04X/%u", res == SDO_RESULT_ABORT ? "abort" : "reply", index, sub);
            return true;
        }
        uint16_t pid = sdoParamIdFromIndex(index, sub);
        const char *name, *unit;
        sdoParamInfo(pid, &name, &unit);
        char nameBuf[12];
        if (!name) { snprintf(nameBuf, sizeof(nameBuf), "#%u", pid); name = nameBuf; }
        if (res == SDO_RESULT_ABORT)      snprintf(out, outLen, "%s: abort %08lX", name, (unsigned long)(uint32_t)raw);
        else if (res == SDO_RESULT_WRITE_ACK) snprintf(out, outLen, "%s: write OK", name);
        else                              snprintf(out, outLen, "%s = %.2f %s", name, sdoToFloat(raw), unit);
        return true;
    }

    if (id >= 0x521 && id <= 0x528) {
        if (len < 6) return false;
        int32_t v = ivtValue(data);
        switch (id) {
            case 0x521: snprintf(out, outLen, "%.3f A", v / 1000.0f); break;
            case 0x522: case 0x523: case 0x524:
                        snprintf(out, outLen, "%.2f V", v / 1000.0f); break;
            case 0x525: snprintf(out, outLen, "%.1f \xC2\xB0" "C", v / 10.0f); break;
            case 0x526: snprintf(out, outLen, "%.2f kW", v / 1000.0f); break;
            case 0x527: snprintf(out, outLen, "%.2f Ah", v / 3600.0f); break;
            case 0x528: snprintf(out, outLen, "%.3f kWh", v / 1000.0f); break;
        }
        return true;
    }

    switch (id) {
        case 0x373:  // BMS_CellData
            if (len < 8) return false;
            snprintf(out, outLen, "%.3f-%.3f V  %.0f/%.0f \xC2\xB0" "C",
                     le16(data) * 0.001f, le16(data + 2) * 0.001f,
                     le16(data + 4) * 0.1f - 273.1f, le16(data + 6) * 0.1f - 273.1f);
            return true;
        case 0x351:  // BMS_Limits
            if (len < 8) return false;
            snprintf(out, outLen, "CV %.1f DV %.1f CI %.0f DI %.0f",
                     le16(data) * 0.1f, le16(data + 2) * 0.1f,
                     le16(data + 4) * 0.1f - 3276.8f, le16(data + 6) * 0.1f - 3276.8f);
            return true;
        case 0x35A: {  // BMS_Alarms: nine 2-bit fields, 0=OK 1=warn 2=alarm 3=n/a
            if (len < 3) return false;
            uint32_t bits = data[0] | (data[1] << 8) | ((uint32_t)data[2] << 16);
            int warn = 0, alarm = 0;
            for (int f = 0; f < 9; f++) {
                uint32_t s = (bits >> (f * 2)) & 3;
                if (s == 1) warn++;
                else if (s == 2) alarm++;
            }
            if (!warn && !alarm) snprintf(out, outLen, "no alarms");
            else snprintf(out, outLen, "%d ALARM  %d warn", alarm, warn);
            return true;
        }
        case 0x35E: {  // BMS_Name: ASCII
            size_t n = 0;
            for (uint8_t b = 0; b < len && n + 1 < outLen; b++) {
                if (data[b] >= 32 && data[b] < 127) out[n++] = (char)data[b];
            }
            out[n] = '\0';
            return n > 0;
        }
        default:
            return false;
    }
}

// --- Stage 3: baseline -----------------------------------------------------------

#include <Preferences.h>

#define BASELINE_FRESH_MS 3000   // only IDs seen this recently go into a new baseline

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint8_t  extended;
    float    rateHz;
} StoredBase;

static can_monitor_base_t baseline[CAN_MONITOR_MAX_IDS];
static size_t baselineCount = 0;
static Preferences basePrefs;  // own instance, own namespace - same reasoning as vehicle_config.cpp

void can_monitor_baseline_load(void) {
    StoredBase stored[CAN_MONITOR_MAX_IDS];
    basePrefs.begin("canmon", true);
    size_t bytes = basePrefs.getBytesLength("base");
    size_t n = 0;
    if (bytes > 0 && bytes <= sizeof(stored) && bytes % sizeof(StoredBase) == 0) {
        basePrefs.getBytes("base", stored, bytes);
        n = bytes / sizeof(StoredBase);
    }
    basePrefs.end();
    for (size_t i = 0; i < n; i++) {
        baseline[i].id = stored[i].id;
        baseline[i].extended = stored[i].extended != 0;
        baseline[i].rateHz = stored[i].rateHz;
    }
    baselineCount = n;
}

size_t can_monitor_baseline_save(void) {
    StoredBase stored[CAN_MONITOR_MAX_IDS];
    uint32_t now = millis();
    size_t n = 0;
    portENTER_CRITICAL(&lock);
    for (size_t i = 0; i < entryCount && n < CAN_MONITOR_MAX_IDS; i++) {
        const Entry *e = &entries[i];
        if (!e->seenOnce || now - e->lastMs > BASELINE_FRESH_MS) continue;
        baseline[n].id = e->id;
        baseline[n].extended = e->extended;
        baseline[n].rateHz = e->rateHz;
        stored[n].id = e->id;
        stored[n].extended = e->extended ? 1 : 0;
        stored[n].rateHz = e->rateHz;
        n++;
    }
    baselineCount = n;
    portEXIT_CRITICAL(&lock);

    basePrefs.begin("canmon", false);
    if (n) basePrefs.putBytes("base", stored, n * sizeof(StoredBase));
    else basePrefs.remove("base");
    basePrefs.end();
    return n;
}

void can_monitor_baseline_forget(void) {
    baselineCount = 0;
    basePrefs.begin("canmon", false);
    basePrefs.remove("base");
    basePrefs.end();
}

size_t can_monitor_baseline_count(void) {
    return baselineCount;
}

const can_monitor_base_t *can_monitor_baseline_at(size_t i) {
    return i < baselineCount ? &baseline[i] : NULL;
}

const can_monitor_base_t *can_monitor_baseline_find(uint32_t id, bool extended) {
    for (size_t i = 0; i < baselineCount; i++) {
        if (baseline[i].id == id && baseline[i].extended == extended) return &baseline[i];
    }
    return NULL;
}
