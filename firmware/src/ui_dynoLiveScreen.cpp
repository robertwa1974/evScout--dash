// ============================================================================
// ui_dynoLiveScreen.cpp - 0-60 / virtual dyno, LIVE mode
// ============================================================================
// Hand-written, no SquareLine project (same as every other screen built this
// session). Minimal chrome per CLAUDE.md: state pill, one dominant elapsed-
// time numeral, a secondary speed readout. Driven by its own lv_timer (runs
// inside lv_timer_handler(), which loop() already wraps in uiMutex - no
// separate mutex needed for the widget calls here, only dataMutex to read
// myData, same as everywhere else).
//
// State machine: READY (tap screen to arm) -> ARMED (waiting for the vehicle
// to start moving) -> RUNNING (timing + recording samples) -> DONE (target
// speed reached, hand off to RESULTS). See dyno_data.h for the recorded-run
// handoff.
//
// Launch detection (2026-09-22: added a real IMU trigger, previously CAN
// speed was the only signal): ARMED->RUNNING fires on EITHER
// myData.vehSpeedKph crossing dynoLaunchKph (the
// original proxy - always correct on a real run, since a real launch
// moves the wheels) OR a real accelerometer deviation from imuData's
// magnitude AT THE MOMENT OF ARMING (see armAccelMagBaseline) exceeding
// dynoLaunchAccelDeltaG - whichever comes first. Both are user-tunable
// now (vehicle_config.h), not hardcoded. The IMU path exists
// specifically so this can be armed and tested on the bench (shake/tilt
// the board, or a real walk-with-it-in-hand test) without needing live
// CAN wheel-speed data, which bench setups often don't have. Comparing
// against the magnitude captured AT ARM TIME, not a fixed 1.0g assumption,
// is deliberate - this board's mounting angle isn't perfectly level (bench
// testing measured ~0.87g resting magnitude, not a clean 1.0g), so a fixed
// "how far from 1.0g" threshold would misfire depending on mounting tilt;
// a delta from whatever the magnitude was right when you armed it is
// orientation-agnostic and only fires on real CHANGE. The RESULTS screen's
// road-load power derivation (ui_dynoResultsScreen.cpp) still uses the
// speed-derivative approach unchanged - swapping that for accelerometer
// integration is a bigger, separately-tunable change with real drift risk
// (no filtering/GPS correction pass exists), not undertaken here.
// ============================================================================

#include "zombie_updaters.h"  // myData, dataMutex - includes ui.h
#include "dyno_data.h"
#include "imu_driver.h"      // imuData - see launch-detection comment above
#include "vehicle_config.h"  // dynoTargetKph/dynoLaunchKph/dynoLaunchAccelDeltaG - user-tunable, see that header
#include <math.h>            // sqrtf/fabsf for imuData's acceleration magnitude

DynoSample dynoRun[DYNO_MAX_SAMPLES];
int dynoRunCount = 0;
float dynoRunElapsedSec = 0;

lv_obj_t * ui_dynoLiveScreen = NULL;

static lv_obj_t * statePill = NULL;
static lv_obj_t * stateLabel = NULL;
// Tracks the pill's current semantic state for refresh_theme() - see
// ui_gpsScreen.c's identical pattern/comment for why.
static ui_pill_state_t statePillState = UI_PILL_NEUTRAL;
static lv_obj_t * timerLabel = NULL;
static lv_obj_t * timerUnitLabel = NULL;
static lv_obj_t * speedLabel = NULL;
static lv_timer_t * sampleTimer = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Dyno LIVE is this group's home screen (also covers RESULTS).
static lv_obj_t * ui_dynoLiveScreenDock = NULL;

// dynoTargetKph/dynoLaunchKph/dynoLaunchAccelDeltaG moved to
// vehicle_config.h (2026-09-22) - was #define here, now NVS-backed and
// editable live from wifi_config_server.h's config page instead of
// needing a reflash to tune. Defaults unchanged (96.6kph/2.0kph/0.15g).
#define DYNO_SAMPLE_PERIOD_MS   100

// Display-only kph->mph conversion (2026-09-22), same constant/rule as the
// main Speed screen (ui_speedScreen.h) and the GPS screen's speed panel
// (zombie_updaters.cpp) - myData.vehSpeedKph and dynoRun[].speedKph both
// stay kph, since dynoTargetKph/dynoLaunchKph and the RESULTS screen's
// road-load power physics (ui_dynoResultsScreen.cpp) all do real math in
// kph/km - only this screen's live speedLabel text is mph.
static const float KPH_TO_MPH = 0.621371f;

typedef enum { DYNO_READY, DYNO_ARMED, DYNO_RUNNING, DYNO_DONE } dyno_state_t;
static dyno_state_t state = DYNO_READY;
static uint32_t startTick = 0;

// Accelerometer magnitude captured at the MOMENT of arming - see this
// file's header comment on why launch detection compares against this
// rather than a fixed 1.0g assumption. false/0 if the IMU never answered
// at imu_init() (imuData.sampleMs stays 0 in that case - see
// imu_driver.h), in which case the IMU launch-detection path is simply
// skipped and CAN speed is the only trigger, same as before this existed.
static float armAccelMagBaseline = 0.0f;
static bool armAccelBaselineValid = false;

static void setState(dyno_state_t s) {
    state = s;
    if (s == DYNO_ARMED) {
        float ax = 0, ay = 0, az = 0;
        armAccelBaselineValid = false;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            ax = imuData.axG; ay = imuData.ayG; az = imuData.azG;
            armAccelBaselineValid = (imuData.sampleMs != 0);
            xSemaphoreGive(dataMutex);
        }
        armAccelMagBaseline = armAccelBaselineValid ? sqrtf(ax * ax + ay * ay + az * az) : 0.0f;
#if defined(DEBUG) || defined(CAN_TRACE)
        // Kept (not just a one-off debug print) - useful for tuning
        // dynoLaunchAccelDeltaG against this specific board's real
        // mounting tilt (see this file's header comment on why the
        // baseline isn't a fixed 1.0g assumption).
        Serial.printf("[dyno] armed: IMU baseline valid=%d magnitude=%.3fg\n", (int)armAccelBaselineValid, armAccelMagBaseline);
#endif
    }
    const char *text;
    // Styling pass: migrated onto ui_status_pill.h's 4-state model - the
    // mapping below (READY=NEUTRAL, ARMED=CAUTION, RUNNING/DONE=ACTIVE) is
    // exactly what ui_status_pill.h's own header comment documents for
    // this screen. Two real color changes from the pre-migration version:
    // ARMED was ui_theme_accent() (cyan), now CAUTION/amber - reads as "a
    // notable, not-yet-critical state," which is what ARMED actually is;
    // DONE was ui_theme_warning() (red), now ACTIVE/green, confirmed with
    // Rob 2026-09-18 - a completed run is a success, not a fault (styling
    // pass item 15's strict "red means a real fault" rule).
    switch (s) {
        case DYNO_READY:   text = "READY - tap to arm"; statePillState = UI_PILL_NEUTRAL; break;
        case DYNO_ARMED:   text = "ARMED - go!";         statePillState = UI_PILL_CAUTION; break;
        case DYNO_RUNNING: text = "RUNNING";             statePillState = UI_PILL_ACTIVE;  break;
        case DYNO_DONE:    text = "DONE";                statePillState = UI_PILL_ACTIVE;  break;
        default:           text = "";                    statePillState = UI_PILL_NEUTRAL; break;
    }
    ui_pill_setState(statePill, stateLabel, statePillState, text);
}

static void sampleTimerCb(lv_timer_t *timer) {
    (void)timer;
    int speedKph = 0;
    float packV = 0, packA = 0;
    float imuAx = 0, imuAy = 0, imuAz = 0;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        speedKph = myData.vehSpeedKph;
        packV = myData.packVoltage;
        packA = myData.packCurrent;
        imuAx = imuData.axG; imuAy = imuData.ayG; imuAz = imuData.azG;
        xSemaphoreGive(dataMutex);
    }

    if (state == DYNO_ARMED) {
        bool launchedBySpeed = speedKph > dynoLaunchKph;
        bool launchedByImu = armAccelBaselineValid &&
            fabsf(sqrtf(imuAx * imuAx + imuAy * imuAy + imuAz * imuAz) - armAccelMagBaseline) > dynoLaunchAccelDeltaG;
        if (!launchedBySpeed && !launchedByImu) return;  // still waiting for the vehicle to start moving
        startTick = lv_tick_get();
        dynoRunCount = 0;
        setState(DYNO_RUNNING);
    }

    if (state != DYNO_RUNNING) return;

    float elapsed = lv_tick_elaps(startTick) / 1000.0f;
    if (timerLabel) lv_label_set_text_fmt(timerLabel, "%.1f", elapsed);
    if (speedLabel) lv_label_set_text_fmt(speedLabel, "%.0f mph", speedKph * KPH_TO_MPH);

    if (dynoRunCount < DYNO_MAX_SAMPLES) {
        dynoRun[dynoRunCount].tSec = elapsed;
        dynoRun[dynoRunCount].speedKph = (float)speedKph;
        dynoRun[dynoRunCount].packVoltage = packV;
        dynoRun[dynoRunCount].packCurrent = packA;
        dynoRunCount++;
    }

    if (speedKph >= dynoTargetKph || dynoRunCount >= DYNO_MAX_SAMPLES) {
        dynoRunElapsedSec = elapsed;
        setState(DYNO_DONE);
        _ui_screen_change(&ui_dynoResultsScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 300, &ui_dynoResultsScreen_screen_init);
        _ui_screen_delete(&ui_dynoLiveScreen);
    }
}

static void screenTapCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (state == DYNO_READY) {
        setState(DYNO_ARMED);
    } else if (state == DYNO_DONE) {
        // Tap again after a run to reset and go again.
        dynoRunCount = 0;
        if (timerLabel) lv_label_set_text(timerLabel, "0.0");
        if (speedLabel) lv_label_set_text(speedLabel, "0 mph");
        setState(DYNO_READY);
    }
    // ARMED/RUNNING: ignore taps - don't let an accidental touch abort a pull.
}

// Physical-RIGHT swipe to GPS NAV REMOVED (styling/UX pass Phase 5,
// 2026-09-18) - cross-group, GPS NAV is reachable via the dock's GPS icon
// now (see ui_dock.h's mapping comment and the plan's swipe-removal
// table).
//
// RE-ADDED 2026-09-22, now targeting Destinations instead of GPS NAV: the
// new ui_destinationsScreen inserted itself into the GPS group's swipe
// chain right next to Dyno LIVE (GPS <-> GPS NAV <-> Destinations <->
// Dyno LIVE - see ui_destinationsScreen.h), so this is once again an
// INTRA-group-adjacent link, not the cross-group one that was removed
// above - same "every intra-group swipe stays, only cross-group links get
// dropped" rule ui_dock.h documents. Dyno LIVE is also still dock-
// reachable directly either way. This board reports gesture direction
// inverted from the physical swipe (CLAUDE.md's "Touch gesture
// direction") - checks LV_DIR_LEFT for the physical-RIGHT swipe, matching
// every other screen here.
void ui_event_dynoLiveScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_destinationsScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_destinationsScreen_screen_init);
        _ui_screen_delete(&ui_dynoLiveScreen);
    }
}

void ui_dynoLiveScreen_screen_init(void)
{
    ui_dynoLiveScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_dynoLiveScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_dynoLiveScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_dynoLiveScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Styling pass: migrated onto ui_status_pill.h's shared pill (was a
    // hand-built lv_obj with no fade transition at all - one of the 2 of
    // 5 pills that previously snapped instantly). Label text is state/
    // status text, not a numeric value, so SemiBold-24 per the font-
    // weight audit (item 2).
    statePill = ui_pill_create(ui_dynoLiveScreen, 340, 60, 30, &stateLabel, &font_montserrat_semibold_24);
    lv_obj_align(statePill, LV_ALIGN_TOP_MID, 0, 24);
    ui_pill_setState(statePill, stateLabel, UI_PILL_NEUTRAL, "READY - tap to arm");

    // Dominant timer numeral - "minimal chrome" means dominance comes from
    // negative space, not a bigger font than the hero tier (CLAUDE.md's
    // 16/24/32/48 system), so this is 48px given plenty of room to breathe.
    timerLabel = lv_label_create(ui_dynoLiveScreen);
    lv_obj_align(timerLabel, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(timerLabel, "0.0");
    lv_obj_set_style_text_color(timerLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(timerLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    timerUnitLabel = lv_label_create(ui_dynoLiveScreen);
    lv_obj_align(timerUnitLabel, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(timerUnitLabel, "seconds");
    lv_obj_set_style_text_color(timerUnitLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(timerUnitLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    speedLabel = lv_label_create(ui_dynoLiveScreen);
    lv_obj_align(speedLabel, LV_ALIGN_CENTER, 0, 100);
    lv_label_set_text(speedLabel, "0 mph");
    lv_obj_set_style_text_color(speedLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(speedLabel, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    state = DYNO_READY;
    dynoRunCount = 0;

    ui_dynoLiveScreenDock = ui_dock_create(ui_dynoLiveScreen, UI_DOCK_DYNO);

    lv_obj_add_event_cb(ui_dynoLiveScreen, ui_event_dynoLiveScreen, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_dynoLiveScreen, screenTapCb, LV_EVENT_CLICKED, NULL);

    // Own timer, not the shared zombie_updaters.cpp Tickers - only needs to
    // run while this screen exists, and runs inside lv_timer_handler() so
    // widget calls above don't need uiMutex (only dataMutex for myData).
    sampleTimer = lv_timer_create(sampleTimerCb, DYNO_SAMPLE_PERIOD_MS, NULL);
}

void ui_dynoLiveScreen_refresh_theme(void)
{
    if (ui_dynoLiveScreen == NULL) return;
    lv_obj_set_style_bg_color(ui_dynoLiveScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (timerUnitLabel) lv_obj_set_style_text_color(timerUnitLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (speedLabel) lv_obj_set_style_text_color(speedLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    setState(state);  // re-applies the current state's themed pill color
    ui_dock_refresh_theme(ui_dynoLiveScreenDock, UI_DOCK_DYNO);
}

void ui_dynoLiveScreen_screen_destroy(void)
{
    if (sampleTimer) {
        lv_timer_del(sampleTimer);
        sampleTimer = NULL;
    }
    if (ui_dynoLiveScreen) lv_obj_del(ui_dynoLiveScreen);

    ui_dynoLiveScreen = NULL;
    statePill = NULL;
    stateLabel = NULL;
    timerLabel = NULL;
    timerUnitLabel = NULL;
    speedLabel = NULL;
    statePillState = UI_PILL_NEUTRAL;
    ui_dynoLiveScreenDock = NULL;
}
