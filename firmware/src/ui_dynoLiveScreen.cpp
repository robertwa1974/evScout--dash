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
static lv_obj_t * timerLabel = NULL;
static lv_obj_t * timerUnitLabel = NULL;
static lv_obj_t * speedLabel = NULL;
static lv_timer_t * sampleTimer = NULL;

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
    lv_color_t color;
    switch (s) {
        case DYNO_READY:   text = "READY - tap to arm"; color = ui_theme_panel_border(); break;
        case DYNO_ARMED:   text = "ARMED - go!";         color = ui_theme_accent();       break;
        case DYNO_RUNNING: text = "RUNNING";             color = ui_theme_good();         break;
        case DYNO_DONE:    text = "DONE";                color = ui_theme_warning();      break;
        default:           text = "";                    color = ui_theme_panel_border(); break;
    }
    if (stateLabel) lv_label_set_text(stateLabel, text);
    if (statePill) lv_obj_set_style_bg_color(statePill, color, LV_PART_MAIN | LV_STATE_DEFAULT);
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

void ui_event_dynoLiveScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_navScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_navScreen_screen_init);
        _ui_screen_delete(&ui_dynoLiveScreen);
    }
}

void ui_dynoLiveScreen_screen_init(void)
{
    ui_dynoLiveScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_dynoLiveScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_dynoLiveScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_dynoLiveScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    statePill = lv_obj_create(ui_dynoLiveScreen);
    lv_obj_clear_flag(statePill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(statePill, 340, 60);
    lv_obj_align(statePill, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_radius(statePill, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(statePill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(statePill, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);

    stateLabel = lv_label_create(statePill);
    lv_obj_center(stateLabel);
    lv_label_set_text(stateLabel, "READY - tap to arm");
    lv_obj_set_style_text_color(stateLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(stateLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

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
    lv_obj_set_style_text_font(timerUnitLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    speedLabel = lv_label_create(ui_dynoLiveScreen);
    lv_obj_align(speedLabel, LV_ALIGN_CENTER, 0, 100);
    lv_label_set_text(speedLabel, "0 km/h");
    lv_obj_set_style_text_color(speedLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(speedLabel, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    state = DYNO_READY;
    dynoRunCount = 0;

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
}
