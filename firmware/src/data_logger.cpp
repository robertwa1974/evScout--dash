#include "data_logger.h"
#include "sd_driver.h"
#include "gps_driver.h"
#include "zombie_updaters.h"  // myData, dataMutex
#include "ui_can_freshness.h"
#include "odometer.h"
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>

#define BUF_SIZE          4096
#define BUF_FLUSH_AT      3584  // leave room for one more row before overflow
#define FLUSH_INTERVAL_MS 5000

static const char *CSV_HEADER =
    "ms,utc,gps_fix,lat,lon,gps_kph,veh_kph,motor_rpm,power_kw,pack_v,pack_a,soc,"
    "motor_c,inverter_c,aux_v,cell_vmax,cell_vmin,cell_tmax,opmode,dir,gear,mot_active,"
    "regen_max,last_err,chg_type,plug_det,ac_v,pilot_a,chg_c,can_live,odo_mi,trip_mi\n";

static Preferences preferences;  // own instance, own namespace - same reasoning as vehicle_config.cpp

static bool running = false;
static uint8_t rateHz = 1;
static File logFile;
static char fileName[16] = "";        // "LOG0007.csv"
static char status[48] = "Off";
static uint32_t rowCount = 0;
static uint32_t lastRowMs = 0;
static uint32_t lastFlushMs = 0;

static char buf[BUF_SIZE];
static size_t bufLen = 0;

static void persistEnabled(bool enabled) {
    preferences.begin("logger", false);
    preferences.putBool("enabled", enabled);
    preferences.end();
}

static void setStatus(const char *msg) {
    snprintf(status, sizeof(status), "%s", msg);
}

// Writes the RAM buffer to the card. On a write failure (card pulled, card
// full) logging stops rather than silently dropping rows forever.
static void writeBuffer(void) {
    if (bufLen == 0 || !logFile) return;
    size_t written = 0;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        written = logFile.write((const uint8_t *)buf, bufLen);
        logFile.flush();
        xSemaphoreGive(sdMutex);
    }
    bool ok = (written == bufLen);
    bufLen = 0;
    lastFlushMs = millis();
    if (!ok) {
        if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
            logFile.close();
            xSemaphoreGive(sdMutex);
        }
        running = false;
        fileName[0] = '\0';
        setStatus("SD write failed - stopped");
        // Deliberately NOT persisting "off" here: a full/removed card is a
        // fault, not a user choice - logging resumes next boot once fixed.
    }
}

static void appendRow(void) {
    struct_message d;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        d = myData;
        xSemaphoreGive(dataMutex);
    } else {
        return;
    }

    char utc[12] = "";
    if (gpsData.year > 2000) {
        snprintf(utc, sizeof(utc), "%02u:%02u:%02u", gpsData.hour, gpsData.minute, gpsData.second);
    }

    int n = snprintf(buf + bufLen, BUF_SIZE - bufLen,
        "%lu,%s,%d,%.6f,%.6f,%.1f,%d,%d,%.2f,%.1f,%.1f,%d,"
        "%d,%d,%.2f,%.3f,%.3f,%d,%d,%d,%d,%d,"
        "%.1f,%d,%d,%d,%.1f,%.1f,%d,%d,%.2f,%.2f\n",
        (unsigned long)millis(), utc, gpsData.hasFix ? 1 : 0,
        gpsData.hasFix ? gpsData.latitude : 0.0, gpsData.hasFix ? gpsData.longitude : 0.0,
        gpsData.speedKph,
        d.vehSpeedKph, d.motorRpm, d.powerKw, d.packVoltage, d.packCurrent, d.soc,
        d.motorTemp, d.heatsinkTemp, d.aux12vVoltage, d.cellVMax, d.cellVMin, d.cellTMax,
        d.opmode, d.dirState, d.gear, d.motActive,
        d.regenMax, d.lastErr, d.chgType, d.plugDet, d.acVolts, d.pilotLimA, d.chgTemp,
        ui_can_freshness_isLive() ? 1 : 0,
        odometer_total_miles(), odometer_trip_miles());
    if (n > 0 && (size_t)n < BUF_SIZE - bufLen) {
        bufLen += n;
        rowCount++;
    }
}

bool data_logger_start(void) {
    if (running) return true;
    if (!sd_available()) {
        setStatus("No SD card");
        return false;
    }

    preferences.begin("logger", false);
    uint32_t seq = preferences.getUInt("seq", 0) + 1;
    preferences.putUInt("seq", seq);
    preferences.end();
    snprintf(fileName, sizeof(fileName), "LOG%04lu.csv", (unsigned long)(seq % 10000));

    char path[32];
    snprintf(path, sizeof(path), DATA_LOGGER_DIR "/%s", fileName);
    bool ok = false;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        if (!SD.exists(DATA_LOGGER_DIR)) SD.mkdir(DATA_LOGGER_DIR);
        logFile = SD.open(path, FILE_WRITE);
        ok = (bool)logFile;
        if (ok) {
            logFile.print(CSV_HEADER);
            logFile.flush();
        }
        xSemaphoreGive(sdMutex);
    }
    if (!ok) {
        fileName[0] = '\0';
        setStatus("Can't create log file");
        return false;
    }

    bufLen = 0;
    rowCount = 0;
    lastRowMs = 0;
    lastFlushMs = millis();
    running = true;
    persistEnabled(true);
    return true;
}

void data_logger_stop(void) {
    persistEnabled(false);
    if (!running) {
        setStatus("Off");
        return;
    }
    writeBuffer();
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        logFile.close();
        xSemaphoreGive(sdMutex);
    }
    running = false;
    fileName[0] = '\0';
    setStatus("Off");
}

bool data_logger_is_running(void) {
    return running;
}

void data_logger_set_rate_hz(uint8_t hz) {
    rateHz = (hz >= 5) ? 5 : (hz >= 2 ? 2 : 1);
    preferences.begin("logger", false);
    preferences.putUChar("rate", rateHz);
    preferences.end();
}

uint8_t data_logger_get_rate_hz(void) {
    return rateHz;
}

const char *data_logger_status_text(void) {
    if (running) {
        snprintf(status, sizeof(status), "%s - %lu rows", fileName, (unsigned long)rowCount);
    }
    return status;
}

const char *data_logger_current_file(void) {
    return fileName;
}

void data_logger_flush(void) {
    if (running) writeBuffer();
}

void data_logger_init(void) {
    preferences.begin("logger", true);
    bool wasEnabled = preferences.getBool("enabled", false);
    rateHz = preferences.getUChar("rate", 1);
    preferences.end();
    if (rateHz != 1 && rateHz != 2 && rateHz != 5) rateHz = 1;
    if (wasEnabled) data_logger_start();
}

void data_logger_poll(void) {
    if (!running) return;
    uint32_t now = millis();
    if (lastRowMs == 0 || now - lastRowMs >= 1000u / rateHz) {
        lastRowMs = now;
        appendRow();
    }
    if (bufLen >= BUF_FLUSH_AT || (bufLen > 0 && now - lastFlushMs >= FLUSH_INTERVAL_MS)) {
        writeBuffer();
    }
}
