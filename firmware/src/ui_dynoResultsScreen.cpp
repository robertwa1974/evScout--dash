// ============================================================================
// ui_dynoResultsScreen.cpp - 0-60 / virtual dyno, RESULTS mode
// ============================================================================
// Hand-written, no SquareLine project. Built ONCE at screen_init from the
// completed run's samples (dyno_data.h) - never refreshed live, per
// CLAUDE.md's performance note. LV_CHART_TYPE_SCATTER, shared kW Y-axis
// (road-load power vs electrical input power, both same unit), manually
// built legend (LVGL has no native one), peak-power headline callouts, and
// a computed efficiency % (road-load / electrical, averaged over the run).
//
// See dyno_data.h for why v1's "road-load power" doesn't need a real IMU:
// acceleration is the derivative of recorded speed samples.
// ============================================================================

#include "zombie_updaters.h"  // included for consistency with the rest of the UI (no myData/dataMutex use here - results are computed once from dyno_data.h, not live)
#include "dyno_data.h"

// TODO: tune to your actual curb weight + driver. This is the only physics
// constant the road-load estimate needs beyond the recorded samples.
#define DYNO_VEHICLE_MASS_KG 1800.0f

lv_obj_t * ui_dynoResultsScreen = NULL;

static lv_obj_t * chart = NULL;
static lv_obj_t * timeLabel = NULL;
static lv_obj_t * peakRoadLabel = NULL;
static lv_obj_t * peakElecLabel = NULL;
static lv_obj_t * effLabel = NULL;
static lv_obj_t * xAxisLabel = NULL;
static lv_obj_t * yAxisLabel = NULL;
static lv_obj_t * legendRoadSwatch = NULL;
static lv_obj_t * legendRoadLabel = NULL;
static lv_obj_t * legendElecSwatch = NULL;
static lv_obj_t * legendElecLabel = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. Dyno RESULTS is inside the Dyno group (home screen: LIVE).
static lv_obj_t * ui_dynoResultsScreenDock = NULL;

void ui_event_dynoResultsScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_dynoLiveScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_dynoLiveScreen_screen_init);
        _ui_screen_delete(&ui_dynoResultsScreen);
    }
}

static lv_obj_t *createLegendRow(lv_obj_t *parent, int16_t y, lv_color_t color, const char *text, lv_obj_t **outLabel) {
    lv_obj_t *swatch = lv_obj_create(parent);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(swatch, 18, 18);
    lv_obj_set_pos(swatch, 0, y);
    lv_obj_set_style_radius(swatch, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(swatch, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(swatch, color, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, 26, y - 2);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (outLabel) *outLabel = label;
    return swatch;
}

void ui_dynoResultsScreen_screen_init(void)
{
    ui_dynoResultsScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_dynoResultsScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_dynoResultsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_dynoResultsScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Headline callouts (numbers first - "that's what people actually
    // look at first", per CLAUDE.md) ---
    timeLabel = lv_label_create(ui_dynoResultsScreen);
    lv_obj_set_pos(timeLabel, 40, 10);
    lv_obj_set_style_text_color(timeLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(timeLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    peakRoadLabel = lv_label_create(ui_dynoResultsScreen);
    lv_obj_set_pos(peakRoadLabel, 400, 14);
    lv_obj_set_style_text_color(peakRoadLabel, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(peakRoadLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    peakElecLabel = lv_label_create(ui_dynoResultsScreen);
    lv_obj_set_pos(peakElecLabel, 400, 48);
    lv_obj_set_style_text_color(peakElecLabel, ui_theme_good(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(peakElecLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    effLabel = lv_label_create(ui_dynoResultsScreen);
    lv_obj_set_pos(effLabel, 620, 30);
    lv_obj_set_style_text_color(effLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(effLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Chart ---
    // Height 260, not 300 (styling/UX pass Phase 5, 2026-09-18) - shrunk so
    // the x-axis label below it clears the new 72px bottom nav dock.
    chart = lv_chart_create(ui_dynoResultsScreen);
    lv_obj_set_pos(chart, 40, 100);
    lv_obj_set_size(chart, 560, 260);
    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);
    lv_obj_set_style_bg_color(chart, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(chart, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_div_line_count(chart, 5, 6);

    // --- Legend (LVGL has no native one - build it manually) ---
    lv_obj_t *legendCont = lv_obj_create(ui_dynoResultsScreen);
    lv_obj_clear_flag(legendCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(legendCont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(legendCont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(legendCont, 160, 60);
    lv_obj_align_to(legendCont, chart, LV_ALIGN_OUT_RIGHT_TOP, 10, 0);
    legendRoadSwatch = createLegendRow(legendCont, 0, ui_theme_accent(), "Road-load", &legendRoadLabel);
    legendElecSwatch = createLegendRow(legendCont, 30, ui_theme_good(), "Electrical", &legendElecLabel);

    xAxisLabel = lv_label_create(ui_dynoResultsScreen);
    lv_obj_align_to(xAxisLabel, chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    lv_label_set_text(xAxisLabel, "Speed (km/h)");
    lv_obj_set_style_text_color(xAxisLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(xAxisLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    yAxisLabel = lv_label_create(ui_dynoResultsScreen);
    lv_obj_align_to(yAxisLabel, chart, LV_ALIGN_OUT_TOP_LEFT, 0, -4);
    lv_label_set_text(yAxisLabel, "Power (kW)");
    lv_obj_set_style_text_color(yAxisLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(yAxisLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Process the run (once - see file header) ---
    int n = dynoRunCount;
    if (n < 2) n = 0;  // degenerate run (aborted before any real samples) - draw an empty chart rather than divide-by-zero

    lv_chart_set_point_count(chart, n > 0 ? n : 1);
    lv_chart_series_t *roadSer = lv_chart_add_series(chart, ui_theme_accent(), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *elecSer = lv_chart_add_series(chart, ui_theme_good(), LV_CHART_AXIS_PRIMARY_Y);

    float peakRoad = 0, peakElec = 0, sumRoad = 0, sumElec = 0;
    int summedCount = 0;

    if (n > 0) {
        lv_coord_t *roadX = lv_chart_get_x_array(chart, roadSer);
        lv_coord_t *roadY = lv_chart_get_y_array(chart, roadSer);
        lv_coord_t *elecX = lv_chart_get_x_array(chart, elecSer);
        lv_coord_t *elecY = lv_chart_get_y_array(chart, elecSer);

        for (int i = 0; i < n; i++) {
            float elecKw = dynoRun[i].packVoltage * dynoRun[i].packCurrent / 1000.0f;

            // Road-load power = accel (dv/dt, from consecutive speed
            // samples) * mass * velocity - see file header for why this
            // doesn't need a real accelerometer for v1.
            float roadKw = 0;
            if (i > 0) {
                float dt = dynoRun[i].tSec - dynoRun[i - 1].tSec;
                if (dt > 0.001f) {
                    float dv_ms = (dynoRun[i].speedKph - dynoRun[i - 1].speedKph) / 3.6f;
                    float accel = dv_ms / dt;
                    float v_ms = dynoRun[i].speedKph / 3.6f;
                    roadKw = (accel * DYNO_VEHICLE_MASS_KG * v_ms) / 1000.0f;
                }
                sumRoad += roadKw;
                sumElec += elecKw;
                summedCount++;
            }

            int xVal = (int)(dynoRun[i].speedKph + 0.5f);
            roadX[i] = xVal; roadY[i] = (lv_coord_t)roadKw;
            elecX[i] = xVal; elecY[i] = (lv_coord_t)elecKw;

            if (roadKw > peakRoad) peakRoad = roadKw;
            if (elecKw > peakElec) peakElec = elecKw;
        }
    }

    float axisMaxKw = peakRoad > peakElec ? peakRoad : peakElec;
    if (axisMaxKw < 10.0f) axisMaxKw = 10.0f;  // floor so a near-zero/degenerate run doesn't collapse the axis
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (lv_coord_t)(axisMaxKw * 1.2f));
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 100);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 6, 3, 5, 2, true, 40);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 6, 3, 5, 2, true, 20);
    lv_chart_refresh(chart);

    float efficiency = (summedCount > 0 && sumElec > 0.01f) ? (sumRoad / sumElec * 100.0f) : 0.0f;

    lv_label_set_text_fmt(timeLabel, "%.1fs", dynoRunElapsedSec);
    lv_label_set_text_fmt(peakRoadLabel, "Peak road-load: %.1f kW", peakRoad);
    lv_label_set_text_fmt(peakElecLabel, "Peak electrical: %.1f kW", peakElec);
    lv_label_set_text_fmt(effLabel, "Efficiency: %.0f%%", efficiency);

    ui_dynoResultsScreenDock = ui_dock_create(ui_dynoResultsScreen, UI_DOCK_DYNO);

    lv_obj_add_event_cb(ui_dynoResultsScreen, ui_event_dynoResultsScreen, LV_EVENT_ALL, NULL);
}

void ui_dynoResultsScreen_refresh_theme(void)
{
    if (ui_dynoResultsScreen == NULL) return;
    lv_obj_set_style_bg_color(ui_dynoResultsScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (chart) {
        lv_obj_set_style_bg_color(chart, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(chart, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (xAxisLabel) lv_obj_set_style_text_color(xAxisLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (yAxisLabel) lv_obj_set_style_text_color(yAxisLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (effLabel) lv_obj_set_style_text_color(effLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_dock_refresh_theme(ui_dynoResultsScreenDock, UI_DOCK_DYNO);
    // Series colors, peak-callout colors, and the legend swatches are left
    // as their creation-time theme snapshot - this screen is rebuilt fresh
    // every run anyway (see screen_init), so it'll pick up the current
    // theme on the very next dyno pass regardless.
}

void ui_dynoResultsScreen_screen_destroy(void)
{
    if (ui_dynoResultsScreen) lv_obj_del(ui_dynoResultsScreen);

    ui_dynoResultsScreen = NULL;
    chart = NULL;
    timeLabel = NULL;
    peakRoadLabel = NULL;
    peakElecLabel = NULL;
    effLabel = NULL;
    xAxisLabel = NULL;
    yAxisLabel = NULL;
    legendRoadSwatch = NULL;
    legendRoadLabel = NULL;
    legendElecSwatch = NULL;
    legendElecLabel = NULL;
    ui_dynoResultsScreenDock = NULL;
}
