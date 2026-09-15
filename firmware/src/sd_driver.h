#pragma once

#include <stdbool.h>

// microSD card driver (2026-09-14, bring-up for the eventual offline map-
// tile screen - see ui_navScreen.h / waveshare-dash-build.md for why a
// real tile map is still blocked on real tile data, independent of this
// driver existing). Deliberately scoped to mount + basic file I/O only -
// no tile format or reading code yet, that's a follow-up once real map
// tiles exist to test against.
//
// Pin assignments confirmed against Waveshare's own official example
// (github.com/waveshareteam/ESP32-S3-Touch-LCD-7, examples/Arduino/
// examples/03_SD_Test), not guessed or reused from a different board:
//   SD_MOSI = GPIO11, SD_CLK = GPIO12, SD_MISO = GPIO13 (dedicated SPI3
//   bus, confirmed free - not used by this board's RGB/DPI display bus,
//   the CAN transceiver, or the I2C touch/CH422G/IMU bus).
//   SD_CS is NOT a direct GPIO - it's EXIO4 on the same CH422G I/O
//   expander this project already drives directly via lgfx::i2c (see
//   display_driver.cpp's ch422g_assert_sd_cs()), for the same reason the
//   CH422G library itself is avoided project-wide (i2c_master/legacy
//   driver conflict, see display_driver.h).
//
// SD_CS is asserted low ONCE at init and left low permanently, never
// toggled per SPI transaction - confirmed this is intentional in
// Waveshare's own demo (their code does the identical thing), because
// toggling a chip-select through an I2C-bus IO expander per SPI
// transaction would be far too slow for SPI timing anyway. Consequence:
// **nothing else may share this SPI bus** - there is no way to deselect
// the SD card once sd_init() runs. Nothing else currently uses GPIO11/12/
// 13, and no other SPI device exists on this board's design, so this is
// safe today - but any future SPI peripheral MUST use a different bus.
//
// SD/SPI are both bundled with the arduino-esp32 core (no platformio.ini
// lib_deps entry needed - same as Preferences/WiFi elsewhere in this
// project).

#ifdef __cplusplus
extern "C" {
#endif

// Asserts SD_CS via the CH422G, brings up the dedicated SPI bus, and
// mounts the card. Call once at boot, AFTER lcd_panel_start() (needs the
// I2C/CH422G bus already up) - order relative to imu_init()/gps_init()
// doesn't matter, they're independent buses. Returns false if no card is
// inserted or it fails to mount (not a crash - a car dashboard can't
// assume a card is always present). Safe to call even with no card; every
// other sd_* function checks sd_available() internally rather than
// assuming success.
bool sd_init(void);

// Cached result of the last sd_init() call - check this before attempting
// any file I/O once real map-tile reading code is added.
bool sd_available(void);

#ifdef __cplusplus
}
#endif
