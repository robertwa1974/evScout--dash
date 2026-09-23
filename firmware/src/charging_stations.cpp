#include "charging_stations.h"
#include "sd_driver.h"
#include "gps_math.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <esp_heap_caps.h>

static ChargingStation * stations = NULL;
static uint32_t stationCount = 0;

bool charging_stations_load(void) {
    if (!sd_available()) return false;

    // Every SD-touching call site takes sdMutex before the operation and
    // gives it back immediately after - see sd_driver.h's comment on
    // sdMutex for why (SD_CS is asserted once and left permanently low, so
    // there's no way to even electrically arbitrate between two callers;
    // the mutex is the only thing preventing two tasks' multi-step SD
    // command sequences from interleaving).
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    File f = SD.open(CHARGING_STATIONS_PATH, FILE_READ);
    if (!f) {
        xSemaphoreGive(sdMutex);
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.println("[charging] stations.bin not found - run gen_charging_stations.py and copy its output to the SD card");
#endif
        return false;
    }

    char magic[4];
    uint32_t count = 0;
    bool headerOk = (f.read((uint8_t *)magic, 4) == 4) &&
                     (f.read((uint8_t *)&count, 4) == 4) &&
                     memcmp(magic, CHG_FILE_MAGIC, 4) == 0;
    if (!headerOk || count == 0) {
        f.close();
        xSemaphoreGive(sdMutex);
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.println("[charging] stations.bin header invalid or empty - not loading");
#endif
        return false;
    }

    ChargingStation * buf = (ChargingStation *)heap_caps_malloc(
        (size_t)count * sizeof(ChargingStation), MALLOC_CAP_SPIRAM);
    if (!buf) {
        f.close();
        xSemaphoreGive(sdMutex);
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.println("[charging] PSRAM allocation failed - not loading");
#endif
        return false;
    }

    size_t expectedBytes = (size_t)count * sizeof(ChargingStation);
    size_t gotBytes = f.read((uint8_t *)buf, expectedBytes);
    f.close();
    xSemaphoreGive(sdMutex);

    if (gotBytes != expectedBytes) {
        heap_caps_free(buf);
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.printf("[charging] short read (%u of %u bytes) - not loading\n", (unsigned)gotBytes, (unsigned)expectedBytes);
#endif
        return false;
    }

    stations = buf;
    stationCount = count;
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[charging] loaded %u stations from %s\n", (unsigned)stationCount, CHARGING_STATIONS_PATH);
#endif
    return true;
}

uint32_t charging_stations_count(void) {
    return stationCount;
}

uint32_t charging_stations_find_nearest_n(double lat, double lon, uint32_t maxResults,
                                           ChargingStation *outStations, float *outDistances) {
    if (!stations || stationCount == 0 || !outStations || !outDistances || maxResults == 0) return 0;

    // Single pass, maintaining a small nearest-first sorted list as we go -
    // insertion-sort-style insert into the (at most maxResults-sized)
    // result set. O(n * maxResults) worst case, trivial at this dataset's
    // scale (see this file's header comment) - no need to sort the whole
    // dataset just to keep the closest few.
    uint32_t found = 0;
    for (uint32_t i = 0; i < stationCount; i++) {
        float d = calcDist((float)lat, (float)lon, (float)stations[i].lat, (float)stations[i].lon);
        if (found < maxResults) {
            // Room left - insert in sorted position.
            uint32_t pos = found;
            while (pos > 0 && outDistances[pos - 1] > d) {
                outDistances[pos] = outDistances[pos - 1];
                outStations[pos] = outStations[pos - 1];
                pos--;
            }
            outDistances[pos] = d;
            outStations[pos] = stations[i];
            found++;
        } else if (d < outDistances[maxResults - 1]) {
            // Full - only insert if this beats the current worst kept.
            uint32_t pos = maxResults - 1;
            while (pos > 0 && outDistances[pos - 1] > d) {
                outDistances[pos] = outDistances[pos - 1];
                outStations[pos] = outStations[pos - 1];
                pos--;
            }
            outDistances[pos] = d;
            outStations[pos] = stations[i];
        }
    }
    return found;
}
