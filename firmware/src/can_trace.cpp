#include "can_trace.h"

#ifdef CAN_TRACE

#include "zombie_updaters.h"  // struct_message, myData, dataMutex

static void printBytes(const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        Serial.printf(" %02X", data[i]);
    }
}

void canTraceFrame(const char *dir, uint32_t id, const uint8_t *data, uint8_t len) {
    Serial.printf("%s 0x%03X [%u]", dir, (unsigned)id, len);
    printBytes(data, len);
    Serial.println();
}

void canTraceField(const char *name, uint16_t paramId, int32_t raw, float scaled, const char *unit) {
    Serial.printf("FLD %-10s id=%-4u raw=%-8ld -> %.3f %s\n",
                  name, paramId, (long)raw, scaled, unit ? unit : "");
}

void canTraceSdoTx(uint32_t cobid, const uint8_t *data, uint8_t len, uint16_t paramId) {
    Serial.printf("SDO> 0x%03X [%u]", (unsigned)cobid, len);
    printBytes(data, len);
    Serial.printf("   (param %u)\n", paramId);
}

void canTraceSdoRx(uint16_t paramId, uint8_t cmd, int32_t raw, float scaled, bool ok) {
    if (!ok) {
        Serial.printf("SDO< param %u  PARSE FAIL / abort (cmd=0x%02X)\n", paramId, cmd);
        return;
    }
    Serial.printf("SDO< %-4u cmd=0x%02X raw=%-8ld -> %.3f\n", paramId, cmd, (long)raw, scaled);
}

void canTraceDumpMyData(void) {
    struct_message d;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) != pdTRUE) return;
    d = myData;
    xSemaphoreGive(dataMutex);

    Serial.printf("==== myData @ %lu ms ====\n", (unsigned long)millis());
    Serial.printf("  [bcast] motorRpm=%d  packV=%.2f  packA=%.2f  motT=%d  hsT=%d  aux12V=%.2f\n",
                  d.motorRpm, d.packVoltage, d.packCurrent, d.motorTemp, d.heatsinkTemp, d.aux12vVoltage);
    Serial.printf("  [bcast] soc=%d  gear=%d  motActive=%d  regenMax=%.2f\n",
                  d.soc, d.gear, d.motActive, d.regenMax);
    Serial.printf("  [sdo]   vehSpeedKph=%d  opmode=%d  powerKw=%.2f  lastErr=%d  dcdcState=%d\n",
                  d.vehSpeedKph, d.opmode, d.powerKw, d.lastErr, d.dcdcState);
}

#endif  // CAN_TRACE
