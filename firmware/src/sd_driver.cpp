#include "sd_driver.h"

// Defined unconditionally (not inside the board #if below) so every
// consumer (ui_splash_truck.cpp, lv_fs_sd.cpp) links against a real
// mutex regardless of board - see sd_driver.h's comment on sdMutex for
// why this exists. On the no-op board branch nothing ever actually
// contends for it, which is harmless.
SemaphoreHandle_t sdMutex = xSemaphoreCreateMutex();

#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)

#include "display_driver.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// See sd_driver.h for the pin assignments and the "CS asserted once,
// never toggled" constraint - both confirmed against Waveshare's own
// official example, not guessed.
#define SD_MOSI 11
#define SD_CLK  12
#define SD_MISO 13
#define SD_FAST_HZ 20000000
#define SD_MAX_OPEN_FILES 10
#define SD_SS   -1  // externally managed via the CH422G, not a GPIO the
                     // SD library should try to drive itself

static bool sdReady = false;

bool sd_init(void) {
    ch422g_assert_sd_cs();  // must run after lcd_panel_start()
    delay(10);              // let the expander's I2C write settle before
                             // the first SPI transaction - matches the
                             // 100ms settle Waveshare's own demo uses
                             // after its full CH422G bring-up sequence

    SPI.setHwCs(false);
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_SS);

    // SPI clock (2026-10-21): SD.begin()'s default is 4MHz, which made the
    // splash truck preload take ~11.5s (~380KB/s) and forced its playback
    // down to 2.5fps. 20MHz is within the SD spec's 25MHz SPI-mode limit;
    // if a particular card won't mount at that speed, fall back to the old
    // default rather than leave the dash without its card.
    //
    // Max open files (2026-10-23): the library default is 5, but this
    // firmware can legitimately hold 6 at once - telemetry log, CAN capture,
    // GPS NAV's region file, the router's graph file, an LVGL image read,
    // and a /logs web download - and the 6th open() just fails ("Can't
    // create capture file" on the bench). Each slot is ~0.5KB of internal
    // RAM, allocated at mount.
    sdReady = SD.begin(SD_SS, SPI, SD_FAST_HZ, "/sd", SD_MAX_OPEN_FILES);
    if (!sdReady) {
        SD.end();
        sdReady = SD.begin(SD_SS, SPI, 4000000, "/sd", SD_MAX_OPEN_FILES);
    }
    if (!sdReady) {
        return false;  // no card inserted, or it failed to mount - not a
                        // crash, a dashboard can't assume a card is
                        // always present
    }

    uint8_t cardType = SD.cardType();
    sdReady = (cardType != CARD_NONE);
    return sdReady;
}

bool sd_available(void) {
    return sdReady;
}

#else  // board with no confirmed SD wiring - safe no-op rather than a
       // guess at pins that might conflict with something else

bool sd_init(void) { return false; }
bool sd_available(void) { return false; }

#endif
