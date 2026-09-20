#pragma once

// Custom LVGL 8.4 filesystem driver wrapping this project's existing
// Arduino SD/File API (sd_driver.h/.cpp), registered under drive letter
// 'S' - paths look like "S:/splash_frames/frame_00.bin".
//
// Why hand-rolled instead of one of LVGL's own built-in backends
// (LV_USE_FS_FATFS/_STDIO/_POSIX/_LITTLEFS - lv_conf.h has ALL of them
// set to 0 in this project): none of them match this project's actual
// storage stack without extra glue - this project's SD access goes
// through Arduino's own SD library (itself a thin wrapper over ESP-IDF's
// FATFS/VFS), not raw FatFs `ff.h` calls (LV_USE_FS_FATFS) or a POSIX/
// stdio path (those need the VFS mounted at a POSIX path, which this
// project's sd_driver.cpp doesn't set up - it uses SD.begin() directly).
// Writing five small callback wrappers around SD.open()/File is less
// code and less risk than reconciling those. Matches this project's
// existing convention elsewhere (CH422G, CAN) of a thin direct driver
// over a mismatched off-the-shelf abstraction - see display_driver.h's
// header comment for that same reasoning applied to the CH422G.
//
// Only used by ui_splash_truck.h's per-frame SD-fallback path (the
// primary PSRAM-preload path builds LV_IMG_SRC_VARIABLE descriptors
// directly in memory and never touches lv_fs at all) - but registered
// unconditionally at boot since it's cheap and any future SD-backed LVGL
// image/font could reuse it.
//
// Read-only: nothing on this dashboard ever writes to the SD card, so
// write_cb/dir_*_cb are left NULL (lv_fs_drv_init() zeroes them, and
// LVGL treats a NULL write_cb as "not implemented" rather than crashing).

#ifdef __cplusplus
extern "C" {
#endif

// Registers the "S:" driver. Call once at boot, after lv_init() (this is
// an LVGL subsystem call, not an SD one - sd_init() itself can run
// before or after this, order between the two doesn't matter here,
// since ready_cb checks sd_available() dynamically on every open).
void lv_fs_sd_register(void);

#ifdef __cplusplus
}
#endif
