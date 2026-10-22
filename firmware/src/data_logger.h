#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CSV telemetry logger to the microSD card (2026-10-21)
// ============================================================================
// One file per logging session: /logs/LOG0001.csv, LOG0002.csv, ... (the
// counter lives in NVS, so numbering never depends on GPS time being
// available). Each row is a snapshot of myData plus GPS position/UTC time
// at the selected rate. Downloadable over the dash's local WiFi AP (see
// wifi_config_server.h's /logs page).
//
// On/off and rate are toggled from Settings > Data Logging and persist
// immediately, same "apply + persist, no Save step" convention as the WiFi
// switch. Left on, logging resumes into a NEW file on the next boot - so
// once enabled, every drive is captured without touching the screen.
//
// All SD I/O happens in data_logger_poll(), called from firmware.ino's
// loop() - never from a Ticker callback (same watchdog lesson as the NAV
// tile loader, see CLAUDE.md). Rows are buffered in RAM and written in
// ~4KB batches (or every FLUSH_INTERVAL_MS), each batch its own sdMutex
// critical section.
//
// Rate note: the SDO poll table refreshes every field roughly every 1.25s
// (zombie_updaters.h), so rates above 1Hz mostly repeat values for SDO
// fields - the higher rates exist for the GPS columns and for when a faster
// data source (canmap broadcast) is configured.
// ============================================================================

#define DATA_LOGGER_DIR "/logs"

#ifdef __cplusplus
extern "C" {
#endif

// Loads the on/off + rate prefs and starts a new file if logging was left
// on. Call once in setup(), after sd_init().
void data_logger_init(void);

// Start a new file / stop and close it. Both persist the on/off state.
// start returns false (and stays off) if there's no SD card or the file
// can't be created - see data_logger_status_text().
bool data_logger_start(void);
void data_logger_stop(void);
bool data_logger_is_running(void);

// 1, 2 or 5 Hz - anything else is clamped to the nearest. Persists.
void data_logger_set_rate_hz(uint8_t hz);
uint8_t data_logger_get_rate_hz(void);

// Short one-line status for the Settings row, e.g. "LOG0007.csv - 1234 rows"
// or "No SD card". Points at an internal buffer, valid until the next call.
const char *data_logger_status_text(void);

// Bare filename of the file currently being written ("LOG0007.csv"), or ""
// when stopped - the web server refuses to delete it and flushes before
// serving it.
const char *data_logger_current_file(void);

// Writes any buffered rows now (before a download of the active file).
void data_logger_flush(void);

// Call every loop() iteration - takes a snapshot when one is due and
// writes batched rows. Cheap no-op when stopped.
void data_logger_poll(void);

#ifdef __cplusplus
}
#endif
