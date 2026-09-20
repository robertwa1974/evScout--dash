// ui_splash_truck.cpp - see ui_splash_truck.h for scope/rationale.
#include "ui_splash_truck.h"
#include "sd_driver.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <Arduino.h>

// Originally 80ms (12.5fps, within the requested 12-15fps range) - bumped
// to 400ms on real hardware (2026-09-19) after the 12.5fps pace was found
// to look visibly broken, not just slow. Root cause: LVGL's built-in
// TRUE_COLOR file decoder (used for the SD-fallback path, see
// drawFrameViaSdFallback() below) streams pixel data row-by-row DURING
// the draw call itself (lv_img_decoder_built_in_read_line() in LVGL's own
// lv_img_decoder.c) - a slow SD read doesn't just delay when a frame
// pops in, it's visibly drawn top-to-bottom in real time, which reads as
// the image "stopping and rebuilding" mid-frame. At 80ms/frame, playback
// demands a new frame ~4x faster than the background preload task (and a
// solo fallback read) can supply one (~300-340ms/frame measured at
// UI_SPLASH_TRUCK_FRAME_SIZE=240) - preload can never stay ahead of that
// demand, so nearly every frame past the first handful loses the race and
// hits the slow, visibly-streaming fallback path instead of an instant
// PSRAM blit. 400ms keeps playback's demand roughly matched to what SD
// can actually sustain, so preload usually wins the race and a frame is
// already cached by the time it's needed - the fallback path still
// exists for whichever frame(s) genuinely can't keep up (typically just
// frame 0, before preload has had any time to run at all), but it's no
// longer the common case. A full 22-frame lap now takes ~8.8s instead of
// 1.76s - deliberately not "smooth 12.5fps" anymore, but this project's
// SD throughput doesn't support that pace for a turntable-sized frame,
// and a slower-but-correctly-drawn revolve reads far better than a fast
// one that visibly tears on most frames.
#define FRAME_PERIOD_MS 400
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
// Tracks distinct frames actually drawn since start() - see
// ui_splash_truck_hasCompletedOneLap()'s header comment for why this is
// keyed on "which frames have been shown" rather than elapsed time.
static bool frameEverShown[UI_SPLASH_TRUCK_FRAME_COUNT];
static bool lapComplete = false;
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
// lv_img_set_src() just points at the path. Real hardware measured SD
// reads at ~400KB/s, so this blocks for roughly (frame size in bytes /
// 400KB/s) - called both from ui_splash_truck_start() for the very first
// frame, and from truckTimerCb() below for any later frame the
// background preload task hasn't reached yet (see that function's own
// comment for why blocking here, once per frame, is an accepted
// tradeoff).
static void drawFrameViaSdFallback(int idx) {
    char path[48];
    snprintf(path, sizeof(path), "S:" SPLASH_FRAMES_DIR "/frame_%02d.bin", idx);
    lv_img_set_src(truckImgObj, path);
    TRUCK_LOG("[splash-truck] frame %d: SD fallback (on-demand read) - watch for stutter\n", idx);
}

static int currentFrame = 0;

static void markShown(int idx) {
    frameEverShown[idx] = true;
    if (lapComplete) return;
    for (int i = 0; i < UI_SPLASH_TRUCK_FRAME_COUNT; i++) {
        if (!frameEverShown[i]) return;
    }
    lapComplete = true;
    TRUCK_LOG("[splash-truck] one full lap complete - all %d frames shown at least once\n", UI_SPLASH_TRUCK_FRAME_COUNT);
}

// Sequential stepper, not elapsed-time-paced (2026-09-19, second revision -
// see git history for the elapsed-time version this replaced, and for the
// even-larger 480px native size this replaced too - see
// UI_SPLASH_TRUCK_FRAME_SIZE's comment). At the current 240px native size
// every one of the 22 frames fits comfortably in PSRAM, but this stepper
// doesn't actually depend on that: it always shows the NEXT frame in
// sequence, from PSRAM if it's there (fast) or via a blocking SD-fallback
// read if not (slow, ~0.3s at this frame size). That makes it correct
// even if PSRAM were ever tight again - a permanently-uncached frame
// would just always take the fallback path once per lap rather than
// hanging forever, unlike the earlier elapsed-time/hold-when-not-ready
// design, which only ever allowed the SD fallback for frame 0 at start()
// and would hold forever on any later frame that never got cached.
// FRAME_PERIOD_MS is therefore only a MINIMUM tick period for
// already-cached frames, not a real-time pacing guarantee.
static void truckTimerCb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    if (!truckImgObj) return;

    int idx = currentFrame;
    if (frameInPsram[idx]) {
        drawFrameFromPsram(idx);
    } else {
        drawFrameViaSdFallback(idx);
    }
    markShown(idx);
    currentFrame = (currentFrame + 1) % UI_SPLASH_TRUCK_FRAME_COUNT;
}

void ui_splash_truck_start(lv_obj_t * imgObj) {
    if (truckTimer) return;  // already running

    truckImgObj = imgObj;
    currentFrame = 0;
    for (int i = 0; i < UI_SPLASH_TRUCK_FRAME_COUNT; i++) frameEverShown[i] = false;
    lapComplete = false;
    truckTimerCb(NULL);  // show + mark frame 0 immediately, advances currentFrame to 1
    truckTimer = lv_timer_create(truckTimerCb, FRAME_PERIOD_MS, NULL);
}

bool ui_splash_truck_hasCompletedOneLap(void) {
    return lapComplete;
}

void ui_splash_truck_stop(void) {
    if (truckTimer) {
        lv_timer_del(truckTimer);
        truckTimer = NULL;
    }
    truckImgObj = NULL;
}
