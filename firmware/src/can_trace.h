#pragma once

#include <Arduino.h>

// ============================================================================
// CAN bench-tracing layer  --  compiled in ONLY when -DCAN_TRACE is set
// (see the [env:cantrace] build in platformio.ini). With CAN_TRACE off every
// call below is an empty inline and costs nothing.
//
// What it prints on UART0 @ 115200:
//   * every received frame, raw:      RX 0x356 [8] A0 0F 2B 02 26 00 00 00
//   * every decoded broadcast field:  FLD udc         id=2006 raw=4000      -> 360.000 V
//   * every SDO request sent:         SDO> 0x603 [8] 40 07 21 E1 00 00 00 00   (Veh_Speed 2017)
//   * every SDO response parsed:      SDO< 2017 cmd=0x43 raw=1920     -> 60.000
//   * 1 Hz dump of the whole telemetry struct
// ============================================================================

#ifdef CAN_TRACE

void canTraceFrame(const char *dir, uint32_t id, const uint8_t *data, uint8_t len);
void canTraceField(const char *name, uint16_t paramId, int32_t raw, float scaled, const char *unit);
void canTraceSdoTx(uint32_t cobid, const uint8_t *data, uint8_t len, uint16_t paramId);
void canTraceSdoRx(uint16_t paramId, uint8_t cmd, int32_t raw, float scaled, bool ok);
void canTraceDumpMyData(void);

#else

static inline void canTraceFrame(const char *, uint32_t, const uint8_t *, uint8_t) {}
static inline void canTraceField(const char *, uint16_t, int32_t, float, const char *) {}
static inline void canTraceSdoTx(uint32_t, const uint8_t *, uint8_t, uint16_t) {}
static inline void canTraceSdoRx(uint16_t, uint8_t, int32_t, float, bool) {}
static inline void canTraceDumpMyData(void) {}

#endif
