// ui_splash_truck.cpp - see ui_splash_truck.h for scope/rationale.
#include "ui_splash_truck.h"
#include "sd_driver.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <Arduino.h>

// 80ms period (12.5fps) - within the requested 12-15fps range. No integer
// fps in that range divides 1000ms evenly (1000/12=83.3, 1000/13=76.9,
// 1000/14=71.4, 1000/15=66.7), so a clean, round timer PERIOD was
// prioritized over forcing an exact-integer fps. 22 frames * 80ms = a
// 1.76s full revolution, which reads as a smooth, deliberate turntable
// loop rather than a flicker - reasonable for a several-second hold, easy
// to retune (one #define) once seen on real hardware.
#define FRAME_PERIOD_MS 80
#define FRAME_PIXEL_BYTES (UI_SPLASH_TRUCK_FRAME_SIZE * UI_SPLASH_TRUCK_FRAME_SIZE * 2)  // RGB565, 2 bytes/px

// Logging gated the same way every other Serial diagnostic in this
// project is (firmware.ino only calls Serial.begin() under DEBUG/
// CAN_TRACE) - flash the cantrace env (or add -DDEBUG) to see preload/
// fallback logs during bench verification (items 14-15).
#if defined(DEBUG) || defined(CAN_TRACE)
// Flushed on every call, not just buffered - if a subsequent step hangs
// or crashes, whatever was already logged needs to actually be on the
// wire, not sitting in an unflushed UART/USB-CDC buffer. Diagnostic-path
// logging only (preload runs once at boot), so the flush cost doesn't
// matter here the way it would on a hot path. Every line is stamped with
// millis() so a slow-but-not-hung preload (confirmed on hardware,
// 2026-09-19 - a full preload actually completed, just took ~2 minutes,
// nowhere close to a hang) shows exactly which step the time is going
// into, not just a pass/fail per frame.
#define TRUCK_LOG(...) do { Serial.printf("[%lu] ", (unsigned long)millis()); Serial.printf(__VA_ARGS__); Serial.flush(); } while (0)
#else
#define TRUCK_LOG(...) do {} while (0)
#endif

static lv_img_dsc_t frames[UI_SPLASH_TRUCK_FRAME_COUNT];
static bool frameInPsram[UI_SPLASH_TRUCK_FRAME_COUNT];
static lv_obj_t * truckImgObj = NULL;
static lv_timer_t * truckTimer = NULL;
// Real elapsed time since ui_splash_truck_start(), not a per-tick counter -
// see truckTimerCb()'s comment for why the displayed frame is derived from
// this instead of simply advancing to "whatever's next ready" each tick.
static uint32_t truckStartMs = 0;
static int lastDrawnFrame = -1;
// Set once, as the very last step of preloadTaskFn() below, after every
// frames[]/frameInPsram[] write has already happened - see ui_splash_
// truck.h's "Thread-safety" comment for why a single volatile flag is
// enough here (single-writer-then-flag handoff, not concurrent access).
static volatile bool preloadDone = false;

// Runs on its own FreeRTOS task (spawned by ui_splash_truck_preload()
// below) - see ui_splash_truck.h's LOADING STRATEGY comment for why this
// moved off the caller's own task (it was blocking the logo wipe-reveal
// from even starting).
static void preloadTaskFn(void * pvParameters) {
    LV_UNUSED(pvParameters);
    for (int i = 0; i < UI_SPLASH_TRUCK_FRAME_COUNT; i++) frameInPsram[i] = false;

    if (!sd_available()) {
        TRUCK_LOG("[splash-truck] SD card not available - all %d frames will fall back to on-demand SD reads (which will also fail without a card; the hold state will simply show nothing, not crash)\n",
                  UI_SPLASH_TRUCK_FRAME_COUNT);
    } else {
        size_t psramFreeBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        TRUCK_LOG("[splash-truck] preload starting, PSRAM free: %u bytes\n", (unsigned)psramFreeBefore);

        uint32_t loopStart = millis();
        for (int i = 0; i < UI_SPLASH_TRUCK_FRAME_COUNT; i++) {
            char path[48];
            snprintf(path, sizeof(path), SPLASH_FRAMES_DIR "/frame_%02d.bin", i);

            // Logged BEFORE each blocking call, not just on failure - if
            // this hangs, the last line on the wire pinpoints which exact
            // operation (open/seek/malloc/read) never returned, rather
            // than leaving a silent gap after the previous frame's
            // success line. Explicit elapsed-ms deltas around open/read
            // specifically - those are this loop's only two calls that
            // touch the SD card, so they're the two candidates for where
            // time is actually going (this is how the CORE_DEBUG_LEVEL
            // logging-overhead bug - see platformio.ini's comment on that
            // flag - was originally found and confirmed fixed).
            // sdMutex (sd_driver.h): this preload task and lv_fs_sd.cpp's
            // registered LVGL fs driver (hit by truckTimerCb's SD-fallback
            // path below whenever a frame isn't in PSRAM yet) both touch
            // the same card from different tasks - see sd_driver.h's
            // comment on sdMutex for why every SD call needs to be its
            // own critical section rather than holding the lock across
            // this whole per-frame block.
            uint32_t openStart = millis();
            TRUCK_LOG("[splash-truck] frame %d: opening %s\n", i, path);
            xSemaphoreTake(sdMutex, portMAX_DELAY);
            File f = SD.open(path, FILE_READ);
            xSemaphoreGive(sdMutex);
            uint32_t openMs = millis() - openStart;
            if (!f) {
                TRUCK_LOG("[splash-truck] frame %d: could not open %s (%u ms) - falling back to on-demand SD read\n", i, path, (unsigned)openMs);
                continue;
            }

            // Skip the 4-byte lv_img_header_t the file starts with (see
            // convert_splash_frames.py's HEADER FORMAT comment) - this
            // module builds its own in-memory header below from known
            // constants rather than parsing it back out of the file,
            // since the frame size is fixed and shared with the
            // conversion script.
            TRUCK_LOG("[splash-truck] frame %d: opened in %u ms (size %u bytes), seeking past header\n", i, (unsigned)openMs, (unsigned)f.size());
            xSemaphoreTake(sdMutex, portMAX_DELAY);
            f.seek(4);
            xSemaphoreGive(sdMutex);

            uint8_t * buf = (uint8_t *)heap_caps_malloc(FRAME_PIXEL_BYTES, MALLOC_CAP_SPIRAM);
            if (buf == NULL) {
                TRUCK_LOG("[splash-truck] frame %d: PSRAM allocation failed (%u bytes requested) - falling back to on-demand SD read\n",
                          i, (unsigned)FRAME_PIXEL_BYTES);
                xSemaphoreTake(sdMutex, portMAX_DELAY);
                f.close();
                xSemaphoreGive(sdMutex);
                continue;
            }

            uint32_t readStart = millis();
            xSemaphoreTake(sdMutex, portMAX_DELAY);
            size_t got = f.read(buf, FRAME_PIXEL_BYTES);
            f.close();
            xSemaphoreGive(sdMutex);
            uint32_t readMs = millis() - readStart;
            TRUCK_LOG("[splash-truck] frame %d: read %u of %u bytes in %u ms (%u KB/s)\n",
                      i, (unsigned)got, (unsigned)FRAME_PIXEL_BYTES, (unsigned)readMs,
                      readMs > 0 ? (unsigned)((got / 1024) * 1000 / readMs) : 0);
            if (got != FRAME_PIXEL_BYTES) {
                TRUCK_LOG("[splash-truck] frame %d: short read (%u of %u bytes) - discarding, falling back to on-demand SD read\n",
                          i, (unsigned)got, (unsigned)FRAME_PIXEL_BYTES);
                heap_caps_free(buf);
                continue;
            }

            frames[i].header.cf = LV_IMG_CF_TRUE_COLOR;
            frames[i].header.always_zero = 0;
            frames[i].header.reserved = 0;
            frames[i].header.w = UI_SPLASH_TRUCK_FRAME_SIZE;
            frames[i].header.h = UI_SPLASH_TRUCK_FRAME_SIZE;
            frames[i].data_size = FRAME_PIXEL_BYTES;
            frames[i].data = buf;
            frameInPsram[i] = true;

            // Found on real hardware (2026-09-19): this tight open/read/
            // close loop, run back-to-back for 22 frames on a task pinned
            // to core 0, starves that core's IDLE0 task long enough to
            // trip the task watchdog ("Task watchdog got triggered...
            // IDLE0 (CPU 0) did not reset the watchdog in time... Tasks
            // currently running: CPU 0: splashTruckPreload") - a hard
            // panic/reboot, not the stack overflow this was first
            // mistaken for. Same underlying class of bug CLAUDE.md
            // already documents for the NAV-tile loader (blocking SD I/O
            // starving a watchdog from the wrong context) - the fix there
            // was moving the work to a normal task; here the work is
            // ALREADY on a normal (if dedicated) task, so the fix is
            // simply yielding periodically so IDLE0 gets scheduled. One
            // tick is enough - this only needs to happen often enough to
            // pet the watchdog before its timeout, not stay off the CPU
            // for any real duration.
            vTaskDelay(1);
        }

        size_t psramFreeAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        int okCount = 0;
        for (int i = 0; i < UI_SPLASH_TRUCK_FRAME_COUNT; i++) {
            if (frameInPsram[i]) okCount++;
        }
        // Verification (item 14): actual PSRAM footprint vs. the ~4-5MB
        // estimate from convert_splash_frames.py's own summary output.
        TRUCK_LOG("[splash-truck] preload done in %u ms total: %d/%d frames in PSRAM, %u bytes used, PSRAM free after: %u bytes\n",
                  (unsigned)(millis() - loopStart), okCount, UI_SPLASH_TRUCK_FRAME_COUNT,
                  (unsigned)(psramFreeBefore - psramFreeAfter), (unsigned)psramFreeAfter);
    }

    preloadDone = true;
    vTaskDelete(NULL);  // required - a FreeRTOS task function must never plain-`return`
}

void ui_splash_truck_preload(void) {
    preloadDone = false;
    // Priority/core mirror firmware.ino's existing TaskCANReceiver
    // (priority 1, core 0) - this task is similarly I/O-bound and belongs
    // off core 1, where the main loop()/LVGL rendering (and this screen's
    // logo wipe animation) runs, so the two can genuinely overlap instead
    // of one blocking the other. Stack is 10KB, NOT the 4KB TaskCANReceiver
    // uses - found on real hardware (2026-09-19) that 4KB boot-looped the
    // whole board the instant a real SD card was present: SD/FatFS file
    // I/O has a much deeper call stack than TaskCANReceiver's CAN-only
    // work ever needed, and 4KB silently overflowed. TaskCANReceiver
    // itself never touches SD, so its 4KB was never actually validated
    // against this kind of load - don't copy that number for a new task
    // that does real file I/O without re-deriving it.
    xTaskCreatePinnedToCore(preloadTaskFn, "splashTruckPreload", 10 * 1024, NULL, 1, NULL, 0);
}

bool ui_splash_truck_isPreloadDone(void) {
    return preloadDone;
}

static void drawFrameFromPsram(int idx) {
    lv_img_set_src(truckImgObj, &frames[idx]);
}

// SD fallback (item 9): LVGL's built-in decoder streams TRUE_COLOR pixel
// data directly from the file via lv_fs_sd.h's registered "S:" driver
// during the actual draw call - nothing read into RAM here,
// lv_img_set_src() just points at the path. Real hardware measured this
// at ~500ms for a 200KB frame (~400KB/s) - see ui_splash_truck_start()
// and truckTimerCb() below for why this is now called deliberately only
// once (the very first frame), never from the recurring timer tick.
static void drawFrameViaSdFallback(int idx) {
    char path[48];
    snprintf(path, sizeof(path), "S:" SPLASH_FRAMES_DIR "/frame_%02d.bin", idx);
    lv_img_set_src(truckImgObj, path);
    TRUCK_LOG("[splash-truck] frame %d: SD fallback (on-demand read) - watch for stutter\n", idx);
}

static void truckTimerCb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    if (!truckImgObj) return;

    // Hold-when-not-ready, not skip-ahead: the first version of this fix
    // (2026-09-19) advanced to "whatever frame IS in PSRAM" every single
    // 80ms tick regardless of how many frames actually existed yet - with
    // only a handful preloaded, that cycled through just those few frames
    // every tick, so a full "rotation" through e.g. 5 ready frames took
    // 400ms instead of the intended 1.76s for all 22 - visibly wrong,
    // reported on real hardware as "strange fast spinning." The actual
    // requirement is that the ON-SCREEN frame always matches how much
    // REAL TIME has elapsed (so the revolve never runs faster than
    // designed), even when the SD card hasn't kept up - so the target
    // frame index is computed from elapsed time, not from a per-tick
    // counter. If that exact frame isn't in PSRAM yet, this simply holds
    // whatever's already on screen and tries again next tick, rather than
    // substituting some other (wrongly-timed) frame or blocking on a slow
    // SD read - see this file's other header comment on why blocking here
    // was already ruled out (serializes with preload's own ~500ms reads
    // via sdMutex).
    uint32_t elapsedMs = millis() - truckStartMs;
    int targetFrame = (int)((elapsedMs / FRAME_PERIOD_MS) % UI_SPLASH_TRUCK_FRAME_COUNT);
    if (targetFrame == lastDrawnFrame) return;  // already showing the time-correct frame
    if (!frameInPsram[targetFrame]) return;      // not ready yet - hold, don't substitute or block

    drawFrameFromPsram(targetFrame);
    lastDrawnFrame = targetFrame;
}

void ui_splash_truck_start(lv_obj_t * imgObj) {
    if (truckTimer) return;  // already running

    truckImgObj = imgObj;
    truckStartMs = millis();
    // One deliberate blocking SD-fallback call is still allowed HERE,
    // and only here, so something real is on screen immediately instead
    // of blank - see truckTimerCb()'s comment above for why every
    // subsequent tick holds rather than blocking again.
    if (frameInPsram[0]) {
        drawFrameFromPsram(0);
    } else {
        drawFrameViaSdFallback(0);
    }
    lastDrawnFrame = 0;
    truckTimer = lv_timer_create(truckTimerCb, FRAME_PERIOD_MS, NULL);
}

void ui_splash_truck_stop(void) {
    if (truckTimer) {
        lv_timer_del(truckTimer);
        truckTimer = NULL;
    }
    truckImgObj = NULL;
}
