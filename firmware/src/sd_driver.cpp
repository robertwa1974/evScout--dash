#include "sd_driver.h"

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

    sdReady = SD.begin(SD_SS);
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
