// ui_shutdown.cpp - see ui_shutdown.h for scope/rationale.
#include "ui_shutdown.h"
#include "ui.h"
#include "display_driver.h"

#define AUX12V_LOW_V       11.5f  // confirmed trip point
#define AUX12V_RECOVER_V   12.0f  // real hysteresis gap above the trip point - avoids flicker right at 11.5V
#define SHUTDOWN_WARNING_MS 4000  // how long the readable warning shows before the fade starts
#define SHUTDOWN_FADE_MS     800  // backlight ramp + curtain fade duration, matches display_driver.h's boot ramp

typedef enum {
    SHUTDOWN_IDLE,     // normal operation, overlay hidden
    SHUTDOWN_WARNING,  // full-screen warning shown, backlight still full, waiting out SHUTDOWN_WARNING_MS
    SHUTDOWN_FADING,   // curtain fading in + backlight ramping down, waiting out SHUTDOWN_FADE_MS
    SHUTDOWN_OFF,       // backlight at 0, curtain fully opaque - screen is physically dark
} shutdown_state_t;

static shutdown_state_t state = SHUTDOWN_IDLE;
static lv_obj_t * overlay = NULL;          // root container, parented to lv_layer_top()
static lv_obj_t * warningIconLabel = NULL;
static lv_obj_t * warningTitleLabel = NULL;
static lv_obj_t * warningVoltLabel = NULL;
static lv_obj_t * blackCurtain = NULL;      // separate object, fades in ON TOP of the warning content during FADING
static lv_timer_t * stateTimer = NULL;      // one-shot, drives WARNING->FADING->OFF
static int savedBrightness = 0;             // brightnessVal captured at WARNING-entry, restored on recovery

static void buildOverlay(void) {
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Found on hardware: lv_theme_default's non-zero default padding
    // offsets where LVGL 8 positions an absolutely-placed child (relative
    // to the PADDED content box, not the outer edge) - without this,
    // blackCurtain below rendered inset and clipped instead of covering
    // the full screen. Every other panel-like container in this codebase
    // already resets this explicitly (see ui_card_grid.cpp's
    // ui_card_createPanel) - missed here on the first pass.
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(overlay, ui_theme_warning(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(overlay, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

    warningIconLabel = lv_label_create(overlay);
    lv_obj_align(warningIconLabel, LV_ALIGN_CENTER, 0, -80);
    lv_label_set_text(warningIconLabel, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(warningIconLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(warningIconLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    warningTitleLabel = lv_label_create(overlay);
    lv_obj_align(warningTitleLabel, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(warningTitleLabel, "LOW 12V SUPPLY");
    lv_obj_set_style_text_color(warningTitleLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(warningTitleLabel, &font_montserrat_extrabold_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    warningVoltLabel = lv_label_create(overlay);
    lv_obj_align(warningVoltLabel, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(warningVoltLabel, "-- V - display shutting down");
    lv_obj_set_style_text_color(warningVoltLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(warningVoltLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Separate object, not just the overlay's own bg_opa - stacked ON TOP
    // (created after, so it draws over the warning content), initially
    // fully transparent. FADING animates this alone, leaving the warning
    // text/icon underneath untouched (irrelevant once this covers them).
    blackCurtain = lv_obj_create(overlay);
    lv_obj_clear_flag(blackCurtain, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(blackCurtain, 800, 480);
    lv_obj_set_pos(blackCurtain, 0, 0);
    lv_obj_set_style_radius(blackCurtain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(blackCurtain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(blackCurtain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(blackCurtain, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(blackCurtain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void curtainOpaAnimExec(void *obj, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void enterFading(void) {
    state = SHUTDOWN_FADING;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, blackCurtain);
    lv_anim_set_exec_cb(&a, curtainOpaAnimExec);
    lv_anim_set_values(&a, lv_obj_get_style_bg_opa(blackCurtain, LV_PART_MAIN), 255);
    lv_anim_set_time(&a, SHUTDOWN_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    backlight_rampTo(0, SHUTDOWN_FADE_MS);
}

static void stateTimerCb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    stateTimer = NULL;  // one-shot, LVGL deletes it after this call returns
    if (state == SHUTDOWN_WARNING) {
        enterFading();
        // Re-arm once more to flip FADING->OFF after the fade completes -
        // no functional difference once OFF (both states keep the overlay
        // fully opaque and the backlight at 0), this just keeps `state`
        // accurate for any future code that inspects it.
        stateTimer = lv_timer_create(stateTimerCb, SHUTDOWN_FADE_MS, NULL);
    } else if (state == SHUTDOWN_FADING) {
        state = SHUTDOWN_OFF;
    }
}

static void enterWarning(void) {
    state = SHUTDOWN_WARNING;
    savedBrightness = brightnessVal;
    lv_obj_set_style_bg_opa(blackCurtain, 0, LV_PART_MAIN | LV_STATE_DEFAULT);  // in case a prior cycle left it opaque
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    stateTimer = lv_timer_create(stateTimerCb, SHUTDOWN_WARNING_MS, NULL);
}

static void recover(void) {
    if (stateTimer) {
        lv_timer_del(stateTimer);
        stateTimer = NULL;
    }
    state = SHUTDOWN_IDLE;
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    backlight_rampTo(savedBrightness, SHUTDOWN_FADE_MS);
}

void ui_shutdown_notifyAux12V(float auxV) {
    if (overlay == NULL) buildOverlay();

    if (state == SHUTDOWN_IDLE) {
        if (auxV < AUX12V_LOW_V) enterWarning();
    } else if (auxV >= AUX12V_RECOVER_V) {
        // Any active phase (WARNING/FADING/OFF) unwinds on recovery - the
        // vehicle's alternator coming back online (engine started, etc.)
        // is exactly the case this needs to handle gracefully, not just a
        // one-way trip.
        recover();
    }

    // Ordered after the state transition above so the very call that
    // enters WARNING also seeds this label immediately, instead of
    // showing the "--" placeholder for one extra update cycle.
    if (warningVoltLabel && state != SHUTDOWN_IDLE) {
        lv_label_set_text_fmt(warningVoltLabel, "%.1f V - display shutting down", auxV);
    }
}
