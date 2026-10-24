#include "can_capture.h"
#include "can_sdo.h"     // SDO_TX_COBID / SDO_RX_COBID
#include "sd_driver.h"
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define RING_FRAMES       4096          // ~1s of a fully loaded 500k bus
#define WRITE_BUF_SIZE    16384
#define WRITE_FLUSH_AT    (WRITE_BUF_SIZE - 128)
#define FLUSH_INTERVAL_MS 1000

typedef struct {
    uint64_t us;
    uint32_t id;
    uint8_t  len;
    uint8_t  flags;     // bit0 extended, bit1 tx
    uint8_t  data[8];
} RingFrame;

static const char *CSV_HEADER = "Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static RingFrame *ring = NULL;        // PSRAM, allocated on first start, kept
// Ring indices/counter: only touched under `lock` (the one unlocked read,
// poll()'s early-out, is a harmless hint re-checked under the lock).
static uint32_t head = 0;    // next write slot (producers)
static uint32_t tail = 0;    // next read slot (poll)
static uint32_t dropped = 0;
static volatile bool running = false;
static bool includeSdo = true;
static uint64_t startUs = 0;

static char *wbuf = NULL;             // PSRAM
static size_t wlen = 0;
static File file;
static char fileName[16] = "";
static char status[96] = "Not capturing";
static uint32_t written = 0;
static uint32_t lastFlushMs = 0;
static Preferences prefs;  // own instance - same reasoning as vehicle_config.cpp

static void setStatus(const char *s) { snprintf(status, sizeof(status), "%s", s); }

void can_capture_on_frame(uint32_t id, bool extended, const uint8_t *data, uint8_t len, bool tx) {
    if (!running) return;
    if (!includeSdo && !extended && (id == SDO_TX_COBID || id == SDO_RX_COBID)) return;
    if (len > 8) len = 8;
    uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if (head - tail >= RING_FRAMES) {
        dropped++;
    } else {
        RingFrame *f = &ring[head % RING_FRAMES];
        f->us = now - startUs;
        f->id = id;
        f->len = len;
        f->flags = (extended ? 1 : 0) | (tx ? 2 : 0);
        memcpy(f->data, data, len);
        head++;
    }
    portEXIT_CRITICAL(&lock);
}

static bool writeOut(void) {
    if (wlen == 0) return true;
    size_t n = 0;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        n = file.write((const uint8_t *)wbuf, wlen);
        file.flush();
        xSemaphoreGive(sdMutex);
    }
    bool ok = (n == wlen);
    wlen = 0;
    lastFlushMs = millis();
    return ok;
}

static void closeFile(void) {
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        file.close();
        xSemaphoreGive(sdMutex);
    }
}

bool can_capture_start(void) {
    if (running) return true;
    if (!sd_available()) { setStatus("No SD card"); return false; }
    if (!ring) ring = (RingFrame *)heap_caps_malloc(RING_FRAMES * sizeof(RingFrame), MALLOC_CAP_SPIRAM);
    if (!wbuf) wbuf = (char *)heap_caps_malloc(WRITE_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!ring || !wbuf) { setStatus("No PSRAM for capture buffer"); return false; }

    prefs.begin("canmon", false);
    uint32_t seq = prefs.getUInt("seq", 0) + 1;
    prefs.putUInt("seq", seq);
    prefs.end();
    snprintf(fileName, sizeof(fileName), "CAN%04lu.csv", (unsigned long)(seq % 10000));

    char path[32];
    snprintf(path, sizeof(path), CAN_CAPTURE_DIR "/%s", fileName);
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[capture] opening %s - internal free=%u largest=%u\n", path,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif
    // fopen() aborts (not fails) when it can't allocate its FILE mutex from
    // internal RAM - refuse up front instead of rebooting the dash.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 4096) {
        fileName[0] = '\0';
        setStatus("Not enough internal RAM to open a file");
        return false;
    }
    bool ok = false;
    errno = 0;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        if (!SD.exists(CAN_CAPTURE_DIR)) SD.mkdir(CAN_CAPTURE_DIR);
        file = SD.open(path, FILE_WRITE);
        ok = (bool)file;
        if (ok) { file.print(CSV_HEADER); file.flush(); }
        xSemaphoreGive(sdMutex);
    }
    if (!ok) {
        // errno from the VFS/FatFS layer says why (23 = too many open files,
        // 28 = card full, 5 = I/O error / damaged filesystem) - shown on the
        // page, since the shipping build logs nothing (CORE_DEBUG_LEVEL=0).
        int err = errno;
        char buf[96];
        uint64_t freeMB = 0;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
            freeMB = (SD.totalBytes() - SD.usedBytes()) / (1024ULL * 1024ULL);
            xSemaphoreGive(sdMutex);
        }
        snprintf(buf, sizeof(buf), "Can't create capture file (errno %d %s, %lu MB free)",
                 err, strerror(err), (unsigned long)freeMB);
        setStatus(buf);
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.printf("[capture] %s\n", buf);
#endif
        fileName[0] = '\0';
        return false;
    }

    portENTER_CRITICAL(&lock);
    head = tail = 0;
    dropped = 0;
    portEXIT_CRITICAL(&lock);
    wlen = 0;
    written = 0;
    lastFlushMs = millis();
    startUs = esp_timer_get_time();
    running = true;
    return true;
}

void can_capture_stop(void) {
    if (!running) return;
    running = false;          // producers stop first...
    can_capture_poll();       // ...then drain what's left
    writeOut();
    closeFile();
    char buf[96];
    snprintf(buf, sizeof(buf), "Saved %s - %lu frames, %lu dropped", fileName,
             (unsigned long)written, (unsigned long)dropped);
    setStatus(buf);
    fileName[0] = '\0';
}

bool can_capture_is_running(void) { return running; }
void can_capture_set_include_sdo(bool include) { includeSdo = include; }
bool can_capture_include_sdo(void) { return includeSdo; }
const char *can_capture_current_file(void) { return running ? fileName : ""; }

void can_capture_flush(void) {
    if (running) {
        can_capture_poll();
        writeOut();
    }
}

const char *can_capture_status_text(void) {
    if (running) {
        snprintf(status, sizeof(status), "Capturing %s - %lu frames, %lu dropped", fileName,
                 (unsigned long)written, (unsigned long)dropped);
    }
    return status;
}

// Card full / pulled: stop rather than silently drop everything after.
static void failStop(void) {
    running = false;
    closeFile();
    setStatus("SD write failed - capture stopped");
    fileName[0] = '\0';
}

void can_capture_poll(void) {
    if (!ring || (!running && head == tail)) return;
    for (;;) {
        RingFrame f;
        portENTER_CRITICAL(&lock);
        bool empty = (tail == head);
        if (!empty) { f = ring[tail % RING_FRAMES]; tail++; }
        portEXIT_CRITICAL(&lock);
        if (empty) break;

        // Byte-for-byte the layout of a real SavvyCAN native-CSV capture of
        // Rob's ZombieVerter (openinverter-can-tool's test data,
        // zombieverter-node3-query-paramdb.csv): every field INCLUDING each
        // data byte is followed by a comma, and only LEN bytes are written:
        //   17410960,00000583,false,Rx,0,8,43,00,50,03,89,C3,02,00,
        int n = snprintf(wbuf + wlen, WRITE_BUF_SIZE - wlen, "%llu,%08lX,%s,%s,0,%u,",
                         (unsigned long long)f.us, (unsigned long)f.id,
                         (f.flags & 1) ? "true" : "false", (f.flags & 2) ? "Tx" : "Rx", f.len);
        if (n > 0) wlen += n;
        for (uint8_t b = 0; b < f.len; b++) {
            n = snprintf(wbuf + wlen, WRITE_BUF_SIZE - wlen, "%02X,", f.data[b]);
            if (n > 0) wlen += n;
        }
        wbuf[wlen++] = '\n';
        written++;
        if (wlen >= WRITE_FLUSH_AT && !writeOut()) { failStop(); return; }
    }
    if (running && wlen > 0 && millis() - lastFlushMs >= FLUSH_INTERVAL_MS && !writeOut()) failStop();
}
