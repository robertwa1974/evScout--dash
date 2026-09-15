// ============================================================================
// ui_navScreen.c - GPS breadcrumb trail (2026-09-14)
// ============================================================================
// See ui_navScreen.h for why this exists instead of a full tile-based map.
// Full-screen trail canvas (800x410) with a thin telemetry strip at the
// bottom (800x70) - "thin telemetry strip overlay (speed, heading) at the
// edge" per CLAUDE.md's original GPS navigation note, kept even without
// real map tiles underneath yet.
//
// Projection: local flat-earth approximation, recomputed from scratch
// against the CURRENT (latest) fix every update, not the trail's start
// point - this is what keeps "you are here" pinned to canvas center as the
// vehicle moves. Standard equirectangular approximation (good to well
// under 1% error at the few-km scale this trail covers - the same
// "simple, sanity-checked, not aerospace-grade" bar already applied to the
// sunrise/solar-declination calc elsewhere in this project):
//   metersNorth = (lat - curLat) * 111320
//   metersEast  = (lon - curLon) * 111320 * cos(curLat in radians)
// 111320 = meters per degree of latitude, effectively constant at driving
// latitudes (varies <1% pole-to-equator) - good enough here, not orbital
// mechanics.
//
// No SquareLine project (same as every other hand-written screen here).
//
// Navigation: inserted between GPS and Dyno LIVE (2026-09-14) - current
// topology: ... <-> Charging <-> GPS <-> GPS NAV <-> Dyno LIVE -> Dyno
// RESULTS. Physical swipe LEFT -> GPS (back), physical swipe RIGHT -> Dyno
// LIVE (forward). This board reports gesture direction inverted from the
// physical swipe (see CLAUDE.md's "Touch gesture direction") - the code
// checks LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe. Intentional; don't "fix" it without re-verifying
// on hardware first.
// ============================================================================

#include "ui.h"
#include <math.h>

lv_obj_t * ui_navScreen = NULL;

static lv_obj_t * ui_navCanvas = NULL;
static lv_obj_t * ui_navTrailLine = NULL;
static lv_obj_t * ui_navHereDot = NULL;
static lv_obj_t * ui_navStrip = NULL;
lv_obj_t * ui_navStatusLabel = NULL;
lv_obj_t * ui_navSpeedLabel = NULL;
lv_obj_t * ui_navHeadingLabel = NULL;
static lv_obj_t * ui_navScaleLabel = NULL;

#define CANVAS_W 800
#define CANVAS_H 410
#define STRIP_H  70

#define TRAIL_MAX_POINTS 120  // ~2 minutes of trail at the ~1Hz GPS fix rate
#define METERS_PER_DEG_LAT 111320.0
#define METERS_PER_PIXEL 3.0  // fixed scale - see header comment; not yet
                               // tuned against a real driving route

static double trailLat[TRAIL_MAX_POINTS];
static double trailLon[TRAIL_MAX_POINTS];
static int trailCount = 0;   // number of valid entries, caps at TRAIL_MAX_POINTS
static int trailHead = 0;    // index the NEXT point will be written to

static lv_point_t trailScreenPoints[TRAIL_MAX_POINTS];

void ui_event_navScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_gpsScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_gpsScreen_screen_init);
        _ui_screen_delete(&ui_navScreen);
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_dynoLiveScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_dynoLiveScreen_screen_init);
        _ui_screen_delete(&ui_navScreen);
    }
}

// Recomputes every trail point's screen position relative to the CURRENT
// fix (the most recently written point) and updates the line widget.
// Called after every ui_navScreen_addPoint() - trailCount is at most 120,
// cheap to fully recompute rather than incrementally patch.
static void redrawTrail(void) {
    if (trailCount == 0 || !ui_navTrailLine) return;

    // The most recent point is at (trailHead - 1), wrapping.
    int curIdx = (trailHead - 1 + TRAIL_MAX_POINTS) % TRAIL_MAX_POINTS;
    double curLat = trailLat[curIdx];
    double curLon = trailLon[curIdx];
    double cosLat = cos(curLat * M_PI / 180.0);

    int cx = CANVAS_W / 2;
    int cy = CANVAS_H / 2;

    // Oldest-to-newest order so the line is drawn as a continuous path.
    int oldestIdx = (trailCount < TRAIL_MAX_POINTS) ? 0 : trailHead;
    for (int i = 0; i < trailCount; i++) {
        int idx = (oldestIdx + i) % TRAIL_MAX_POINTS;
        double metersNorth = (trailLat[idx] - curLat) * METERS_PER_DEG_LAT;
        double metersEast  = (trailLon[idx] - curLon) * METERS_PER_DEG_LAT * cosLat;
        trailScreenPoints[i].x = (lv_coord_t)(cx + metersEast / METERS_PER_PIXEL);
        trailScreenPoints[i].y = (lv_coord_t)(cy - metersNorth / METERS_PER_PIXEL);
    }
    lv_line_set_points(ui_navTrailLine, trailScreenPoints, trailCount);
}

void ui_navScreen_addPoint(double lat, double lon) {
    if (!ui_navScreen) return;  // no work for a screen nobody has opened yet

    trailLat[trailHead] = lat;
    trailLon[trailHead] = lon;
    trailHead = (trailHead + 1) % TRAIL_MAX_POINTS;
    if (trailCount < TRAIL_MAX_POINTS) trailCount++;

    redrawTrail();
}

void ui_navScreen_screen_init(void)
{
    ui_navScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_navScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_navScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Trail canvas ---
    ui_navCanvas = lv_obj_create(ui_navScreen);
    lv_obj_clear_flag(ui_navCanvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_navCanvas, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(ui_navCanvas, 0, 0);
    lv_obj_set_style_pad_all(ui_navCanvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_navCanvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navCanvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navCanvas, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navCanvas, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navTrailLine = lv_line_create(ui_navCanvas);
    lv_obj_set_size(ui_navTrailLine, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(ui_navTrailLine, 0, 0);
    lv_line_set_points(ui_navTrailLine, trailScreenPoints, 0);
    lv_obj_set_style_line_width(ui_navTrailLine, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(ui_navTrailLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(ui_navTrailLine, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);

    // "You are here" - fixed at canvas center by construction (every
    // redraw recenters the projection on the latest fix), never moved.
    ui_navHereDot = lv_obj_create(ui_navCanvas);
    lv_obj_clear_flag(ui_navHereDot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_navHereDot, 20, 20);
    lv_obj_align(ui_navHereDot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(ui_navHereDot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navHereDot, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navHereDot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navHereDot, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_navHereDot, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navScaleLabel = lv_label_create(ui_navCanvas);
    lv_obj_align(ui_navScaleLabel, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_label_set_text_fmt(ui_navScaleLabel, "~%.0fm across - no map tiles yet", CANVAS_W * METERS_PER_PIXEL);
    lv_obj_set_style_text_color(ui_navScaleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navScaleLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Telemetry strip ---
    ui_navStrip = lv_obj_create(ui_navScreen);
    lv_obj_clear_flag(ui_navStrip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_navStrip, CANVAS_W, STRIP_H);
    lv_obj_set_pos(ui_navStrip, 0, CANVAS_H);
    lv_obj_set_style_pad_all(ui_navStrip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_navStrip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navStrip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navStrip, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navStrip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navStatusLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navStatusLabel, LV_ALIGN_LEFT_MID, 24, 0);
    lv_label_set_text(ui_navStatusLabel, "NO FIX");
    lv_obj_set_style_text_color(ui_navStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navStatusLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navSpeedLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navSpeedLabel, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(ui_navSpeedLabel, ICON_SPEED " -- km/h");
    lv_obj_set_style_text_color(ui_navSpeedLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navSpeedLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navHeadingLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navHeadingLabel, LV_ALIGN_RIGHT_MID, -24, 0);
    lv_label_set_text(ui_navHeadingLabel, ICON_NAVIGATION " --\xC2\xB0");
    lv_obj_set_style_text_color(ui_navHeadingLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navHeadingLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_navScreen, ui_event_navScreen, LV_EVENT_ALL, NULL);
}

void ui_navScreen_refresh_theme(void)
{
    if (ui_navScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_navScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navCanvas) lv_obj_set_style_bg_color(ui_navCanvas, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navTrailLine) lv_obj_set_style_line_color(ui_navTrailLine, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navHereDot) lv_obj_set_style_border_color(ui_navHereDot, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navScaleLabel) lv_obj_set_style_text_color(ui_navScaleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navStrip) lv_obj_set_style_bg_color(ui_navStrip, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // Status/speed/heading label text/colors are data-driven and left
    // alone here - they self-correct on their next natural data update,
    // same reasoning as every other screen in this codebase.
}

void ui_navScreen_screen_destroy(void)
{
    if (ui_navScreen) lv_obj_del(ui_navScreen);

    ui_navScreen = NULL;
    ui_navCanvas = NULL;
    ui_navTrailLine = NULL;
    ui_navHereDot = NULL;
    ui_navStrip = NULL;
    ui_navStatusLabel = NULL;
    ui_navSpeedLabel = NULL;
    ui_navHeadingLabel = NULL;
    ui_navScaleLabel = NULL;
    trailCount = 0;
    trailHead = 0;
}
