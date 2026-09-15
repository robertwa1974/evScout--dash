#pragma once

#include <stdint.h>
#include <stdbool.h>

// GPS module driver (2026-09-14, Phase 1 of the architecture doc's GPS
// requirement - see scout80-dash-architecture.md's hardware table).
// Deliberately scoped to driver + NMEA parsing only - does NOT yet build
// the GPS navigation screen (IceNav-v3-style map rendering needs map tile
// assets that don't exist in this repo yet) or wire gpsData into the Clock
// screen / day/night Auto resolution (resolveDisplayPref()'s TODO in
// zombie_updaters.cpp) - both are natural follow-ups once this driver is
// proven on real hardware, deferred rather than guessed at now.
//
// Wiring: this board's "UART1" (USB-C, via the onboard CH343 chip) and
// "UART2" (a direct header for external modules) are NOT two separate
// UARTs - both are the SAME ESP32-S3 UART0 peripheral (GPIO43 TX / GPIO44
// RX), routed by a physical DIP switch to one destination or the other
// (confirmed from Rob's board manual, not guessed). The GPS module goes on
// the UART2 header - meaning it shares hardware with the exact port used
// for flashing and `pio device monitor`. Consequence: DEBUG/CAN_TRACE
// builds (which already claim Serial at 115200 for debug/trace output) and
// live GPS parsing (which needs Serial at the GPS module's baud) are
// mutually exclusive on this board, not just in firmware - flip the DIP
// switch to USB to reflash/monitor, flip it to the header for GPS to
// actually reach the ESP32. gps_init()/gps_poll() no-op harmlessly under
// DEBUG/CAN_TRACE rather than fighting over Serial's baud rate.
//
// Uses TinyGPSPlus (platformio.ini lib_deps) for NMEA sentence parsing.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     hasFix;
    double   latitude;    // decimal degrees, +north/-south
    double   longitude;   // decimal degrees, +east/-west
    float    speedKph;
    float    headingDeg;  // course over ground
    float    altitudeM;
    uint8_t  satellites;
    // UTC date/time - earmarked for the future Clock screen and the
    // day/night Auto resolution TODO in zombie_updaters.cpp, not consumed
    // by anything yet.
    uint16_t year;
    uint8_t  month, day, hour, minute, second;
    uint32_t lastFixMs;    // millis() at the last sentence that updated a fix
} GpsData;

// Guarded by dataMutex (mutex.h) - same convention as myData/imuData.
extern GpsData gpsData;

// Starts Serial at the GPS module's baud rate. Call once at boot - safe to
// call before lcd_panel_start() since it doesn't touch I2C/the display.
// No-ops under DEBUG/CAN_TRACE (see the big comment above).
void gps_init(void);

// Drains any available Serial bytes into the NMEA parser and updates
// gpsData when a sentence completes a fix. Cheap, non-blocking - call every
// loop() iteration. No-ops under DEBUG/CAN_TRACE.
void gps_poll(void);

#ifdef __cplusplus
}
#endif
