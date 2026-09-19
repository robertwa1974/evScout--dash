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
// handoff and the "no IMU yet" note on launch detection.
// ============================================================================

#include "zombie_updaters.h"  // myData, dataMutex - includes ui.h
#include "dyno_data.h"

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

// TODO: tune to your vehicle/pack. DYNO_LAUNCH_KPH is the "did it start
// moving" proxy standing in for real accelerometer launch detection.
#define DYNO_TARGET_KPH       96.6f   // 60 mph
#define DYNO_LAUNCH_KPH        2.0f
#define DYNO_SAMPLE_PERIOD_MS   100

typedef enum { DYNO_READY, DYNO_ARMED, DYNO_RUNNING, DYNO_DONE } dyno_state_t;
static dyno_state_t state = DYNO_READY;
static uint32_t startTick = 0;

static void setState(dyno_state_t s) {
    state = s;
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
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        speedKph = myData.vehSpeedKph;
        packV = myData.packVoltage;
        packA = myData.packCurrent;
        xSemaphoreGive(dataMutex);
    }

    if (state == DYNO_ARMED) {
        if (speedKph <= DYNO_LAUNCH_KPH) return;  // still waiting for the vehicle to start moving
        startTick = lv_tick_get();
        dynoRunCount = 0;
        setState(DYNO_RUNNING);
    }

    if (state != DYNO_RUNNING) return;

    float elapsed = lv_tick_elaps(startTick) / 1000.0f;
    if (timerLabel) lv_label_set_text_fmt(timerLabel, "%.1f", elapsed);
    if (speedLabel) lv_label_set_text_fmt(speedLabel, "%d km/h", speedKph);

    if (dynoRunCount < DYNO_MAX_SAMPLES) {
        dynoRun[dynoRunCount].tSec = elapsed;
        dynoRun[dynoRunCount].speedKph = (float)speedKph;
        dynoRun[dynoRunCount].packVoltage = packV;
        dynoRun[dynoRunCount].packCurrent = packA;
        dynoRunCount++;
    }

    if (speedKph >= DYNO_TARGET_KPH || dynoRunCount >= DYNO_MAX_SAMPLES) {
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
        if (speedLabel) lv_label_set_text(speedLabel, "0 km/h");
        setState(DYNO_READY);
    }
    // ARMED/RUNNING: ignore taps - don't let an accidental touch abort a pull.
}

// Physical-RIGHT swipe to GPS NAV REMOVED (styling/UX pass Phase 5,
// 2026-09-18) - cross-group, GPS NAV is reachable via the dock's GPS icon
// now (see ui_dock.h's mapping comment and the plan's swipe-removal
// table). This screen has no swipe links left at all - same situation as
// Battery/BMS (see ui_batteryScreen.c), reachable only via the dock's Dyno
// icon; the LIVE->RESULTS transition was always automatic on run
// completion, never swipe-driven, so nothing else needed a swipe here to
// begin with.
void ui_event_dynoLiveScreen(lv_event_t * e)
{
    (void)e;
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
    lv_label_set_text(speedLabel, "0 km/h");
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
