#pragma once

// Shared between ui_dynoLiveScreen.cpp (writer, during a RUNNING pass) and
// ui_dynoResultsScreen.cpp (reader, once per completed run). Kept in its own
// header rather than zombie_updaters.h since this is dyno-run-specific state,
// not live CAN telemetry.
//
// v1 deliberately doesn't need an IMU: road-load power is approximated from
// the derivative of recorded vehicle-speed samples (accel = dv/dt) rather
// than a real accelerometer, and electrical power is packVoltage*packCurrent
// - both already-validated real telemetry (see can-bench.md). Good enough to
// stand up the whole LIVE -> RESULTS pipeline now; swap in real IMU-derived
// acceleration later (architecture doc's dyno design) without touching the
// UI or the results math structure, only how accel gets computed.

#define DYNO_MAX_SAMPLES 100

typedef struct {
    float tSec;
    float speedKph;
    float packVoltage;
    float packCurrent;
} DynoSample;

extern DynoSample dynoRun[DYNO_MAX_SAMPLES];
extern int dynoRunCount;
extern float dynoRunElapsedSec;  // final elapsed time when the target speed was crossed
