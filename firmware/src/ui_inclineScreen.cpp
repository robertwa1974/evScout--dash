// ============================================================================
// ui_inclineScreen.cpp - inclinometer: pitch and roll, degrees
// ============================================================================
// Hand-written (2026-10-29). Two side-by-side 388x392 panels above the dock:
//   left  PITCH - Scout side view (nose right), tilts with pitch
//   right ROLL  - Scout rear view, tilts with roll. Seen from behind, the
//                 driver's left is on screen left, so LEANING LEFT tips the
//                 picture left. (Was a front view until 2026-10-30: the
//                 picture then read as leaning the opposite way to the
//                 caption, since from the front the sides are mirrored.)
// Each: an lv_meter used for its +-45 degree tick scale and amber/red zone
// arcs only (labels rewritten to absolute values in a draw-part callback) -
// no meter needle, since a needle from the centre would cut through the
// truck; the pointer is a short lv_line at the arc instead.
//
// Truck artwork: wireframes hand-traced from renders of the actual truck
// (firmware/assets/convert_incline_art.py), stored as packed outline/detail
// coverage masks (img_incline_scout.h). expandArt() turns each into an
// LV_IMG_CF_TRUE_COLOR_ALPHA buffer in PSRAM in the current palette's
// colours (outline = text primary, detail = text secondary) - redone on a
// day/night switch - and lv_img_set_angle() rotates it. LVGL 8.4's software
// transform only handles TRUE_COLOR*/RGB565A8, which is why the colours are
// baked here rather than recoloured at draw time.
//
// Angles: imu_get_attitude() - complementary filter in the IMU task, see
// imu_driver.h for how launch/brake and cornering are subtracted. Zones:
// amber from 75% of warningSet.pitchDeg/rollDeg, red at them. Those are
// user-tuned starting points, not rollover limits - the user manual says so.
//
// Never falls back to raw chip axes (CLAUDE.md, IMU level): no IMU or no
// calibration -> empty state pointing at Settings > Calibration.
//
// Updates from an lv_timer (inside lv_timer_handler, uiMutex already held)
// that only runs while this screen is loaded, redrawing only when an angle
// moves >= ANGLE_EPS_DEG (image rotation is the expensive part).
//
// Navigation (Telemetry group): physical swipe LEFT -> Drive (back),
// physical swipe RIGHT -> Status (forward). This board reports gesture
// direction inverted (CLAUDE.md "Touch gesture direction") - LV_DIR_RIGHT
// is the physical-LEFT swipe. Intentional; don't "fix" it.
// ============================================================================

#include "ui.h"
#include "imu_driver.h"
#include "img_incline_scout.h"
#include "zombie_updaters.h"  // warningSet.pitchDeg / rollDeg
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_W        GRID_PANEL_W
#define PANEL_H        392          // 8 + 392 + 8 = 408 = 480 - UI_DOCK_H
#define CX             (PANEL_W / 2)
#define CY             190          // pivot of the truck and centre of the scale
#define METER_D        340
#define SCALE_MAX      45
#define POINTER_R0     150          // outside the tick labels, over the ticks themselves
#define POINTER_R1     172
#define VALUE_Y        300          // clear of the truck's lowest corner at 45 degrees
#define CAPTION_Y      356
#define ANGLE_EPS_DEG  0.3f
#define TICK_MS        50

typedef struct {
    const uint8_t *mask;
    uint16_t w, h;
    uint8_t *buf;          // PSRAM, w*h*3
    lv_img_dsc_t dsc;
} art_t;

typedef struct {
    lv_obj_t *panel, *title, *tag, *meter, *value, *caption, *pointerLine, *groundLine, *img;
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *zone[4];  // amber -, amber +, red -, red +
    lv_point_t pointerPts[2];
    lv_point_t groundPts[2];        // lv_line keeps these pointers - must outlive the lines
    art_t *art;
    float shownDeg;     // last drawn angle (NAN = never)
    int shownInt;       // last label value
    int zoneLevel;      // 0 normal, 1 amber, 2 red (-1 = force)
    int warnDeg;        // threshold the zone arcs were last set to
} gauge_t;

lv_obj_t * ui_inclineScreen = NULL;

static art_t sideArt = { incline_side_mask, INCLINE_SIDE_W, INCLINE_SIDE_H, NULL, {} };
static art_t rearArt = { incline_rear_mask, INCLINE_REAR_W, INCLINE_REAR_H, NULL, {} };
static gauge_t pitchG, rollG;
static lv_obj_t *emptyNoImu = NULL, *emptyUncal = NULL;
static lv_obj_t *ui_inclineScreenDock = NULL;
static lv_timer_t *timer = NULL;

// --- artwork ------------------------------------------------------------------

// Mask byte = (outline << 4) | detail, each 0-15, sum = coverage.
static void expandArt(art_t *a) {
    size_t n = (size_t)a->w * a->h;
    if (!a->buf) {
        a->buf = (uint8_t *)heap_caps_malloc(n * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!a->buf) a->buf = (uint8_t *)malloc(n * 3);
        if (!a->buf) return;
        a->dsc.header.always_zero = 0;
        a->dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        a->dsc.header.w = a->w;
        a->dsc.header.h = a->h;
        a->dsc.data_size = n * 3;
        a->dsc.data = a->buf;
    }
    lv_color_t outline = ui_theme_text_primary(), detail = ui_theme_text_secondary();
    for (size_t i = 0; i < n; i++) {
        uint8_t v = a->mask[i];
        unsigned o = v >> 4, d = v & 15, s = o + d;
        lv_color_t c = s ? lv_color_mix(outline, detail, (uint8_t)(o * 255 / s)) : detail;
        // TRUE_COLOR_ALPHA at 16-bit, SWAP=0: RGB565 little-endian, then alpha.
        a->buf[i * 3] = c.full & 0xFF;
        a->buf[i * 3 + 1] = c.full >> 8;
        a->buf[i * 3 + 2] = (uint8_t)(s * 17);
    }
    lv_img_cache_invalidate_src(&a->dsc);
}

// --- drawing helpers ----------------------------------------------------------

// Rotates a point (x right, y up, about the pivot) CCW by (cos,sin) into panel coords.
static lv_point_t toPanel(int16_t x, int16_t y, float c, float s) {
    lv_point_t p;
    p.x = (lv_coord_t)lroundf(CX + x * c - y * s);
    p.y = (lv_coord_t)lroundf(CY - (x * s + y * c));
    return p;
}

static lv_obj_t *makeLine(lv_obj_t *parent, int width, lv_color_t color) {
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_width(line, width, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(line, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
}

// thetaDeg: CCW rotation of the drawing on screen.
static void drawGauge(gauge_t *g, float thetaDeg) {
    float d = fmaxf(-SCALE_MAX, fminf(SCALE_MAX, thetaDeg));
    float rad = d * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);

    if (g->img) lv_img_set_angle(g->img, (int16_t)lroundf(-d * 10));  // LVGL: 0.1 deg units, clockwise

    // Ground line just under the tyres, tilting with the truck.
    int16_t gx = g->art->w / 2 + 4, gy = -(g->art->h / 2) - 1;
    g->groundPts[0] = toPanel(-gx, gy, c, s);
    g->groundPts[1] = toPanel(gx, gy, c, s);
    lv_obj_invalidate(g->groundLine);  // old extent - set_points only invalidates the new one
    lv_line_set_points(g->groundLine, g->groundPts, 2);

    // Pointer: the truck's "up" direction.
    g->pointerPts[0] = toPanel(0, POINTER_R0, c, s);
    g->pointerPts[1] = toPanel(0, POINTER_R1, c, s);
    lv_obj_invalidate(g->pointerLine);
    lv_line_set_points(g->pointerLine, g->pointerPts, 2);
}

static void setZones(gauge_t *g, int warnDeg) {
    int amber = (warnDeg * 3 + 2) / 4;  // 75%, rounded
    lv_meter_set_indicator_start_value(g->meter, g->zone[0], -warnDeg);
    lv_meter_set_indicator_end_value(g->meter, g->zone[0], -amber);
    lv_meter_set_indicator_start_value(g->meter, g->zone[1], amber);
    lv_meter_set_indicator_end_value(g->meter, g->zone[1], warnDeg);
    lv_meter_set_indicator_start_value(g->meter, g->zone[2], -SCALE_MAX);
    lv_meter_set_indicator_end_value(g->meter, g->zone[2], -warnDeg);
    lv_meter_set_indicator_start_value(g->meter, g->zone[3], warnDeg);
    lv_meter_set_indicator_end_value(g->meter, g->zone[3], SCALE_MAX);
    g->warnDeg = warnDeg;
}

static void zoneColors(gauge_t *g) {
    lv_color_t amber = ui_theme_bad(), red = ui_theme_warning();  // palette names: bad = amber, warning = red
    g->zone[0]->type_data.arc.color = amber;
    g->zone[1]->type_data.arc.color = amber;
    g->zone[2]->type_data.arc.color = red;
    g->zone[3]->type_data.arc.color = red;
    lv_obj_invalidate(g->meter);
}

static void tickColors(gauge_t *g) {
    lv_meter_set_scale_ticks(g->meter, g->scale, SCALE_MAX * 2 / 5 + 1, 2, 10, ui_theme_text_secondary());
    lv_meter_set_scale_major_ticks(g->meter, g->scale, 3, 3, 16, ui_theme_text_primary(), 10);
    lv_obj_set_style_text_color(g->meter, ui_theme_text_secondary(), LV_PART_TICKS | LV_STATE_DEFAULT);
}

// Tick labels as magnitudes (15 30 45 either side) - the side is in the caption.
static void meterDrawCb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->class_p == &lv_meter_class && dsc->type == LV_METER_DRAW_PART_TICK && dsc->text) {
        lv_snprintf(dsc->text, dsc->text_length, "%d", (int)abs((int)dsc->value));
    }
}

static void buildGauge(gauge_t *g, int16_t x, const char *title, art_t *art) {
    memset(g, 0, sizeof(*g));
    g->art = art;
    g->panel = ui_card_createPanel(ui_inclineScreen, x, GRID_GAP, title, &g->title);
    lv_obj_set_size(g->panel, PANEL_W, PANEL_H);

    g->tag = lv_label_create(g->panel);
    lv_obj_align(g->tag, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_label_set_text(g->tag, "UNCORRECTED");
    lv_obj_set_style_text_font(g->tag, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g->tag, ui_theme_bad(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(g->tag, LV_OBJ_FLAG_HIDDEN);

    g->meter = lv_meter_create(g->panel);
    lv_obj_set_size(g->meter, METER_D, METER_D);
    lv_obj_set_pos(g->meter, CX - METER_D / 2, CY - METER_D / 2);
    lv_obj_clear_flag(g->meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(g->meter, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g->meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g->meter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g->meter, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);  // no centre dot
    lv_obj_set_style_text_font(g->meter, &font_montserrat_semibold_16, LV_PART_TICKS | LV_STATE_DEFAULT);
    g->scale = lv_meter_add_scale(g->meter);
    // 90 degree sweep centred on 12 o'clock (meter angles: clockwise from 3 o'clock).
    lv_meter_set_scale_range(g->meter, g->scale, -SCALE_MAX, SCALE_MAX, SCALE_MAX * 2, 270 - SCALE_MAX);
    tickColors(g);
    for (int i = 0; i < 4; i++) g->zone[i] = lv_meter_add_arc(g->meter, g->scale, 8, ui_theme_warning(), 0);
    zoneColors(g);
    lv_obj_add_event_cb(g->meter, meterDrawCb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    g->groundLine = makeLine(g->panel, 2, ui_theme_text_secondary());

    expandArt(art);
    if (art->buf) {
        g->img = lv_img_create(g->panel);
        lv_img_set_src(g->img, &art->dsc);
        lv_obj_clear_flag(g->img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(g->img, CX - art->w / 2, CY - art->h / 2);
        lv_img_set_pivot(g->img, art->w / 2, art->h / 2);
        lv_img_set_antialias(g->img, true);
    }

    g->value = lv_label_create(g->panel);
    lv_obj_align(g->value, LV_ALIGN_TOP_MID, 0, VALUE_Y);
    lv_label_set_text(g->value, "--\xC2\xB0");
    lv_obj_set_style_text_font(g->value, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g->value, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    g->caption = lv_label_create(g->panel);
    lv_obj_align(g->caption, LV_ALIGN_TOP_MID, 0, CAPTION_Y);
    lv_label_set_text(g->caption, "");
    lv_obj_set_style_text_font(g->caption, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g->caption, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);

    g->pointerLine = makeLine(g->panel, 7, ui_theme_accent());

    g->shownDeg = NAN;
    g->shownInt = 9999;
    g->zoneLevel = -1;
}

// --- live update -----------------------------------------------------------------

static void updateGauge(gauge_t *g, float deg, float thetaDeg, int warnDeg,
                        const char *posText, const char *negText, bool compensated) {
    if (warnDeg != g->warnDeg) {
        setZones(g, warnDeg);
        g->zoneLevel = -1;
    }
    if (isnan(g->shownDeg) || fabsf(deg - g->shownDeg) >= ANGLE_EPS_DEG) {
        drawGauge(g, thetaDeg);
        g->shownDeg = deg;
    }

    int shown = (int)lroundf(fabsf(deg));
    if (shown != g->shownInt) {
        lv_label_set_text_fmt(g->value, "%d\xC2\xB0", shown);
        lv_label_set_text(g->caption, shown == 0 ? "LEVEL" : (deg > 0 ? posText : negText));
        g->shownInt = shown;
    }

    float a = fabsf(deg);
    int level = a >= warnDeg ? 2 : (a >= warnDeg * 0.75f ? 1 : 0);
    if (level != g->zoneLevel) {
        lv_color_t col = level == 2 ? ui_theme_warning() : (level == 1 ? ui_theme_bad() : ui_theme_text_primary());
        lv_obj_set_style_text_color(g->value, col, LV_PART_MAIN | LV_STATE_DEFAULT);
        g->zoneLevel = level;
    }

    if (compensated) lv_obj_add_flag(g->tag, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(g->tag, LV_OBJ_FLAG_HIDDEN);
}

static int clampWarn(int deg) { return deg < 5 ? 5 : (deg > SCALE_MAX ? SCALE_MAX : deg); }

static void setGaugesVisible(bool on) {
    lv_obj_t *panels[2] = { pitchG.panel, rollG.panel };
    for (int i = 0; i < 2; i++) {
        if (on) lv_obj_clear_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void timerCb(lv_timer_t *t) {
    (void)t;
    if (lv_scr_act() != ui_inclineScreen) return;

    ImuAttitude att;
    bool ok = imu_get_attitude(&att);
    bool present = imu_present();
    ui_empty_state_setVisible(emptyNoImu, !present);
    ui_empty_state_setVisible(emptyUncal, present && !ok);
    setGaugesVisible(ok);
    if (!ok) return;

    // Pitch: nose is on the right, so nose-up is a CCW rotation.
    updateGauge(&pitchG, att.pitchDeg, att.pitchDeg, clampWarn(warningSet.pitchDeg), "NOSE UP", "NOSE DOWN", att.compensated);
    // Roll: driver's left on screen left, so left side up turns the picture clockwise.
    updateGauge(&rollG, att.rollDeg, -att.rollDeg, clampWarn(warningSet.rollDeg), "LEANING RIGHT", "LEANING LEFT", att.compensated);
}

static void screenLoadCb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCREEN_LOADED) {
        if (!timer) timer = lv_timer_create(timerCb, TICK_MS, NULL);
        timerCb(NULL);
    } else if (code == LV_EVENT_SCREEN_UNLOADED && timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
}

void ui_event_inclineScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_driveScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_driveScreen_screen_init);
        _ui_screen_delete(&ui_inclineScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_statusScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_statusScreen_screen_init);
        _ui_screen_delete(&ui_inclineScreen);
    }
}

void ui_inclineScreen_screen_init(void)
{
    ui_inclineScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_inclineScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_inclineScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_inclineScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    buildGauge(&pitchG, GRID_GAP, "PITCH", &sideArt);
    buildGauge(&rollG, GRID_GAP * 2 + PANEL_W, "ROLL", &rearArt);
    drawGauge(&pitchG, 0);
    drawGauge(&rollG, 0);
    setZones(&pitchG, clampWarn(warningSet.pitchDeg));
    setZones(&rollG, clampWarn(warningSet.rollDeg));

    emptyNoImu = ui_empty_state_create(ui_inclineScreen, NULL, "IMU not detected");
    lv_obj_align(emptyNoImu, LV_ALIGN_CENTER, 0, -UI_DOCK_H / 2);
    emptyUncal = ui_empty_state_create(ui_inclineScreen, NULL,
                                       "Level the IMU first: Settings > Calibration > IMU level, parked on level ground.");
    lv_obj_align(emptyUncal, LV_ALIGN_CENTER, 0, -UI_DOCK_H / 2);
    ui_empty_state_setVisible(emptyNoImu, false);
    ui_empty_state_setVisible(emptyUncal, false);

    ui_inclineScreenDock = ui_dock_create(ui_inclineScreen, UI_DOCK_TELEMETRY);

    lv_obj_add_event_cb(ui_inclineScreen, ui_event_inclineScreen, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_inclineScreen, screenLoadCb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(ui_inclineScreen, screenLoadCb, LV_EVENT_SCREEN_UNLOADED, NULL);
}

static void refreshGaugeTheme(gauge_t *g) {
    if (!g->panel) return;
    lv_obj_set_style_bg_color(g->panel, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(g->panel, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g->title, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g->tag, ui_theme_bad(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g->caption, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(g->pointerLine, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(g->groundLine, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    tickColors(g);
    zoneColors(g);
    expandArt(g->art);
    if (g->img) lv_obj_invalidate(g->img);
    g->zoneLevel = -1;  // value colour is re-picked on the next tick
}

void ui_inclineScreen_refresh_theme(void)
{
    if (ui_inclineScreen == NULL) return;
    lv_obj_set_style_bg_color(ui_inclineScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    refreshGaugeTheme(&pitchG);
    refreshGaugeTheme(&rollG);
    ui_dock_refresh_theme(ui_inclineScreenDock, UI_DOCK_TELEMETRY);
}

void ui_inclineScreen_screen_destroy(void)
{
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    if (ui_inclineScreen) lv_obj_del(ui_inclineScreen);
    ui_inclineScreen = NULL;
    // The PSRAM art buffers are kept - rebuilt in place if the screen is recreated.
    memset(&pitchG, 0, sizeof(pitchG));
    memset(&rollG, 0, sizeof(rollG));
    emptyNoImu = NULL;
    emptyUncal = NULL;
    ui_inclineScreenDock = NULL;
}
