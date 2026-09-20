// lv_fs_sd.cpp - see lv_fs_sd.h for scope/rationale.
#include "lv_fs_sd.h"
#include "lvgl.h"
#include "sd_driver.h"
#include <SD.h>

static bool sdFsReadyCb(lv_fs_drv_t * drv) {
    LV_UNUSED(drv);
    return sd_available();
}

// LVGL's lv_fs_open() strips the "S:" drive prefix before calling this
// (see lv_fs_get_real_path() in LVGL's own lv_fs.c) - what arrives here
// already looks like "/splash_frames/frame_00.bin", exactly the form
// Arduino's SD.open() expects.
static void * sdFsOpenCb(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    if (mode & LV_FS_MODE_WR) return NULL;  // read-only driver, see header comment
    if (!sd_available()) return NULL;

    // sdMutex (sd_driver.h): this can race ui_splash_truck.cpp's
    // background preload task touching the same card - see that header's
    // comment on sdMutex for why every SD-touching call is its own
    // critical section.
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    File f = SD.open(path, FILE_READ);
    xSemaphoreGive(sdMutex);
    if (!f) return NULL;

    // Arduino's File wraps a shared_ptr internally (ESP32 core), so
    // copy-constructing here is safe and shares the same open handle -
    // this heap allocation is a real per-open file HANDLE, not the kind
    // of "new for something that should be a shared static" mistake
    // flagged elsewhere in this codebase (see ui_status_pill.cpp's
    // header comment for that other, unrelated case) - freed 1:1 in
    // sdFsCloseCb below.
    File * handle = new File(f);
    return (void *)handle;
}

static lv_fs_res_t sdFsCloseCb(lv_fs_drv_t * drv, void * file_p) {
    LV_UNUSED(drv);
    File * handle = (File *)file_p;
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    handle->close();
    xSemaphoreGive(sdMutex);
    delete handle;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdFsReadCb(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br) {
    LV_UNUSED(drv);
    File * handle = (File *)file_p;
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    size_t got = handle->read((uint8_t *)buf, btr);
    xSemaphoreGive(sdMutex);
    if (br) *br = (uint32_t)got;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdFsSeekCb(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    File * handle = (File *)file_p;
    SeekMode mode;
    switch (whence) {
        case LV_FS_SEEK_CUR: mode = SeekCur; break;
        case LV_FS_SEEK_END: mode = SeekEnd; break;
        case LV_FS_SEEK_SET:
        default:             mode = SeekSet; break;
    }
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    bool ok = handle->seek(pos, mode);
    xSemaphoreGive(sdMutex);
    return ok ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t sdFsTellCb(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p) {
    LV_UNUSED(drv);
    File * handle = (File *)file_p;
    *pos_p = (uint32_t)handle->position();
    return LV_FS_RES_OK;
}

void lv_fs_sd_register(void) {
    static lv_fs_drv_t drv;  // must outlive this function - lv_fs_drv_register() only stores the pointer
    lv_fs_drv_init(&drv);

    drv.letter = 'S';
    drv.ready_cb = sdFsReadyCb;
    drv.open_cb = sdFsOpenCb;
    drv.close_cb = sdFsCloseCb;
    drv.read_cb = sdFsReadCb;
    drv.seek_cb = sdFsSeekCb;
    drv.tell_cb = sdFsTellCb;
    // write_cb/dir_*_cb left NULL by lv_fs_drv_init() - read-only, no
    // directory iteration needed (ui_splash_truck.h opens frames by a
    // predictable, computed filename, never lists the directory).

    lv_fs_drv_register(&drv);
}
