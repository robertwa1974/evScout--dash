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
#include "nav_reader.h"
#include "nav_map_render.h"
#include <math.h>

lv_obj_t * ui_navScreen = NULL;

static lv_obj_t * ui_navCanvas = NULL;
static lv_obj_t * ui_navTileLayer = NULL;  // map geometry - created before
                                             // the trail/here-dot below so
                                             // they always draw on top of it
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

// --- NAV vector tile rendering ---
// Draws whatever tile currently covers the vehicle's position, reusing
// this file's existing flat-earth-meters projection (see header comment)
// for tile geometry too, so the trail and the map draw in the exact same
// coordinate system. Deliberately simple for a first pass: only the
// fixture's line/polygon/text/point feature kinds are handled, only a
// polygon's OUTER ring is drawn (no holes), and no de-dup/LOD by minZoom -
// see nav_reader.h's own caps-are-a-guess note, same "buildable now,
// revisit once real Tile-Generator output exists" spirit.
#define NAV_TILE_ZOOM 16  // matches firmware/assets/gen_nav_fixture.py's
                           // fixture; real map data should target this
                           // zoom too until there's a reason to vary it

static NavTileData currentTile;  // ~12KB - static, never a stack local
                                   // (see nav_reader.h)
static bool tileLoaded = false;
static uint32_t loadedTileX = 0, loadedTileY = 0;

// TEXT/POINT features only, now - LINESTRING/POLYGON render via
// nav_map_render.h's raster sprite instead (see that header's comment
// for why: real filled polygons with holes, which lv_line can't do).
static lv_obj_t * tileFeatureObjs[NAV_MAX_FEATURES_PER_TILE];

// +1 per feature so a closed polygon ring can repeat its first vertex.
// Plain int (not lv_point_t) - nav_map_render's polygon/line calls take
// separate px[]/py[] int arrays, matching upstream fillPolygonGeneral's
// own signature.
static int tileFeaturePX[NAV_MAX_FEATURES_PER_TILE][NAV_MAX_VERTICES_PER_FEATURE + 1];
static int tileFeaturePY[NAV_MAX_FEATURES_PER_TILE][NAV_MAX_VERTICES_PER_FEATURE + 1];

// RGB565 -> lv_color_t without assuming lv_color_t's internal union layout
// (LV_COLOR_DEPTH is 16 per include/lv_conf.h, but going through
// lv_color_make() rather than poking a union member keeps this correct
// even if that ever changes).
static lv_color_t navFeatureColor(uint16_t rgb565) {
    uint8_t r5 = (rgb565 >> 11) & 0x1F;
    uint8_t g6 = (rgb565 >> 5) & 0x3F;
    uint8_t b5 = rgb565 & 0x1F;
    return lv_color_make((r5 * 527 + 23) >> 6, (g6 * 259 + 33) >> 6, (b5 * 527 + 23) >> 6);
}

static void clearTileLayer(void) {
    for (int i = 0; i < NAV_MAX_FEATURES_PER_TILE; i++) {
        if (tileFeatureObjs[i]) {
            lv_obj_del(tileFeatureObjs[i]);
            tileFeatureObjs[i] = NULL;
        }
    }
}

// Creates one LVGL object per decoded feature in currentTile - called only
// when the vehicle crosses into a different tile (i.e. after a fresh
// nav_tile_load()), not on every GPS fix. Screen position isn't set here;
// repositionTileLayer() does that every fix, same split as the trail's
// addPoint()/redrawTrail().
static void buildTileLayer(void) {
    for (uint16_t i = 0; i < currentTile.featureCount; i++) {
        NavFeature *f = &currentTile.features[i];
        lv_color_t color = navFeatureColor(f->colorRgb565);

        if (f->geomType == NAV_GEOM_TEXT) {
            lv_obj_t * label = lv_label_create(ui_navTileLayer);
            lv_label_set_text(label, f->text);
            lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(label, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            if (f->shieldBgRgb565 != 0) {
                lv_obj_set_style_bg_color(label, navFeatureColor(f->shieldBgRgb565), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_opa(label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(label, navFeatureColor(f->shieldBorderRgb565), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_width(label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_pad_all(label, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            tileFeatureObjs[i] = label;
        } else if (f->geomType == NAV_GEOM_POINT) {
            lv_obj_t * dot = lv_obj_create(ui_navTileLayer);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(dot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            tileFeatureObjs[i] = dot;
        } else {
            // LINESTRING or POLYGON - rendered into nav_map_render's
            // raster sprite instead of an lv_obj (see file header); no
            // LVGL object needed, tileFeatureObjs[i] stays NULL so
            // repositionTileLayer() knows to route this feature there.
            tileFeatureObjs[i] = NULL;
        }
    }
}

// Recomputes every active tile feature's screen position relative to the
// CURRENT fix, exactly like redrawTrail() does for the breadcrumb trail -
// called every GPS fix (not just on tile reload) so map geometry stays
// centered as the vehicle moves, even between tile crossings.
static void repositionTileLayer(double curLat, double curLon, double cosLat) {
    int cx = CANVAS_W / 2;
    int cy = CANVAS_H / 2;

    // One raster frame for every LINESTRING/POLYGON feature this call -
    // begin/end bracket the per-feature nav_map_render_polygon()/_line()
    // calls below, matching nav_map_render.h's one-call-per-frame
    // contract. TEXT/POINT features (LVGL objects) are repositioned in
    // the same loop but don't touch the raster frame at all.
    nav_map_render_begin_frame(lv_color_to16(ui_theme_panel_bg()));

    for (uint16_t i = 0; i < currentTile.featureCount; i++) {
        NavFeature *f = &currentTile.features[i];

        if (f->geomType == NAV_GEOM_TEXT || f->geomType == NAV_GEOM_POINT) {
            lv_obj_t * obj = tileFeatureObjs[i];
            if (!obj) continue;
            int16_t lx = (f->geomType == NAV_GEOM_TEXT) ? f->textX : f->vx[0];
            int16_t ly = (f->geomType == NAV_GEOM_TEXT) ? f->textY : f->vy[0];
            double lat, lon;
            nav_tile_local_to_latlon(NAV_TILE_ZOOM, currentTile.tileX, currentTile.tileY, lx, ly, &lat, &lon);
            double metersNorth = (lat - curLat) * METERS_PER_DEG_LAT;
            double metersEast  = (lon - curLon) * METERS_PER_DEG_LAT * cosLat;
            lv_coord_t x = (lv_coord_t)(cx + metersEast / METERS_PER_PIXEL);
            lv_coord_t y = (lv_coord_t)(cy - metersNorth / METERS_PER_PIXEL);
            // Anchor at top-left rather than centering on the label/dot's
            // own size - close enough for a first pass, avoids a
            // lv_obj_get_size() round-trip per feature per fix.
            lv_obj_set_pos(obj, x, y);
        } else {
            // LINESTRING or POLYGON - project every vertex (all rings,
            // not just the outer one - nav_map_render_polygon() supports
            // holes via ringEnds, unlike the lv_line approach this
            // replaced) and hand off to the raster renderer.
            uint16_t vertCount = f->vertexCount;
            if (vertCount > NAV_MAX_VERTICES_PER_FEATURE) vertCount = NAV_MAX_VERTICES_PER_FEATURE;

            for (uint16_t v = 0; v < vertCount; v++) {
                double lat, lon;
                nav_tile_local_to_latlon(NAV_TILE_ZOOM, currentTile.tileX, currentTile.tileY, f->vx[v], f->vy[v], &lat, &lon);
                double metersNorth = (lat - curLat) * METERS_PER_DEG_LAT;
                double metersEast  = (lon - curLon) * METERS_PER_DEG_LAT * cosLat;
                tileFeaturePX[i][v] = (int)(cx + metersEast / METERS_PER_PIXEL);
                tileFeaturePY[i][v] = (int)(cy - metersNorth / METERS_PER_PIXEL);
            }

            // NavFeature.colorRgb565 is already packed r5g6b5 (see
            // nav_reader.h) - the exact same bit layout this sprite's
            // rgb565_nonswapped buffer expects (see nav_map_render.cpp's
            // header comment), so it's used directly here with no
            // lv_color_t round-trip - unlike the theme background color
            // above, which genuinely originates as an lv_color_t.
            uint16_t color565 = f->colorRgb565;

            if (f->geomType == NAV_GEOM_POLYGON) {
                bool drawCasing = f->isCasingOrSpecial;
                uint16_t casingColor = drawCasing ? nav_map_render_darken(f->colorRgb565, 0.35f) : 0;
                nav_map_render_polygon(tileFeaturePX[i], tileFeaturePY[i], vertCount,
                                        color565, f->ringCount, f->ringCount > 0 ? f->ringEnds : NULL,
                                        drawCasing, casingColor);
            } else {
                uint8_t widthPx = f->widthPx / 2;  // widthPx is in 0.5px units (nav_tile_format.h)
                if (widthPx == 0) widthPx = 1;
                bool drawCasing = f->isCasingOrSpecial;
                uint16_t casingColor = drawCasing ? nav_map_render_darken(f->colorRgb565, 0.3f) : 0;
                uint8_t casingWidthPx = widthPx + 2;
                nav_map_render_line(tileFeaturePX[i], tileFeaturePY[i], vertCount,
                                     widthPx, color565, drawCasing, casingColor, casingWidthPx);
            }
        }
    }

    nav_map_render_end_frame();
}

// Loads whichever tile now covers (lat, lon), only when that's actually a
// different tile than what's already loaded/built - SD reads only happen
// on a tile crossing, not every fix. Rebuilding the LVGL objects happens
// here too since the feature set (and therefore how many/what kind of
// objects are needed) only changes when the tile does.
// Does the actual blocking work (nav_tile_load() + rebuilding LVGL
// objects) - see ui_navScreen_processPendingTileLoad()'s header comment
// in ui_navScreen.h for why this must never be called from
// ui_navScreen_addPoint()/slowUpdate()'s Ticker-callback context.
static void loadTile(uint32_t tx, uint32_t ty, double lat, double lon) {
    loadedTileX = tx;
    loadedTileY = ty;
    clearTileLayer();

    bool ok = nav_tile_load(NAV_TILE_ZOOM, tx, ty, &currentTile);
    tileLoaded = ok;
    if (ok) {
        buildTileLayer();
        if (ui_navScaleLabel) lv_label_set_text_fmt(ui_navScaleLabel, "~%.0fm across", CANVAS_W * METERS_PER_PIXEL);
    } else if (ui_navScaleLabel) {
        lv_label_set_text_fmt(ui_navScaleLabel, "~%.0fm across - no map tiles yet", CANVAS_W * METERS_PER_PIXEL);
    }
    // Reposition immediately against the fix that triggered this load,
    // rather than waiting for the next addPoint() (~1s away at typical
    // GPS fix rates) to see the newly loaded tile's geometry.
    repositionTileLayer(lat, lon, cos(lat * M_PI / 180.0));
}

static bool tileLoadPending = false;
static double pendingLat = 0, pendingLon = 0;

// Cheap tile-crossing check only - never touches the SD card. See
// ui_navScreen_addPoint()'s comment in ui_navScreen.h for the full story:
// a real nav_tile_load() takes long enough (confirmed on real hardware
// against real California map data, 2026-09-15) that running it from
// slowUpdate()'s Ticker-callback context starves the watchdog. This just
// records that a load is needed; ui_navScreen_processPendingTileLoad()
// (called from firmware.ino's loop(), a normal task) does the real work.
static void updateNavTile(double lat, double lon) {
    uint32_t tx, ty;
    nav_latlon_to_tile(NAV_TILE_ZOOM, lat, lon, &tx, &ty);

    if (tileLoaded && tx == loadedTileX && ty == loadedTileY) return;

    tileLoadPending = true;
    pendingLat = lat;
    pendingLon = lon;
}

void ui_navScreen_processPendingTileLoad(void) {
    if (!tileLoadPending) return;
    tileLoadPending = false;

    // The screen may have been swiped away between the fix that queued
    // this load and this loop() iteration servicing it - ui_navTileLayer
    // (the LVGL parent buildTileLayer() creates objects under) would be
    // NULL, same "no work for a screen nobody has opened" convention as
    // ui_navScreen_addPoint().
    if (!ui_navScreen) return;

    uint32_t tx, ty;
    nav_latlon_to_tile(NAV_TILE_ZOOM, pendingLat, pendingLon, &tx, &ty);
    loadTile(tx, ty, pendingLat, pendingLon);
}

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

    // Map tile layer: flags a reload on a tile crossing but never blocks
    // here - see updateNavTile()'s comment. Repositioning (cheap, no I/O)
    // still happens every fix so the map stays centered like the trail.
    updateNavTile(lat, lon);
    repositionTileLayer(lat, lon, cos(lat * M_PI / 180.0));
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

    // Raster map layer (roads/polygons, filled with holes via
    // nav_map_render.h) - created FIRST so it's the bottommost child;
    // ui_navTileLayer's point/text objects and the trail/here-dot below
    // all draw on top of it. Its own background is opaque (the canvas
    // panel color, painted every frame by nav_map_render_begin_frame())
    // so it stands in for what used to be ui_navTileLayer's transparent
    // bg showing the canvas through - no separate transparency needed.
    nav_map_render_init(ui_navCanvas, CANVAS_W, CANVAS_H);

    // Point/text feature layer - created before the trail/here-dot below
    // so those still draw on top of it, same reasoning as before.
    // Transparent so the raster layer beneath shows through.
    ui_navTileLayer = lv_obj_create(ui_navCanvas);
    lv_obj_clear_flag(ui_navTileLayer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_navTileLayer, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(ui_navTileLayer, 0, 0);
    lv_obj_set_style_pad_all(ui_navTileLayer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_navTileLayer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navTileLayer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navTileLayer, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    for (int i = 0; i < NAV_MAX_FEATURES_PER_TILE; i++) tileFeatureObjs[i] = NULL;
    tileLoaded = false;

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
    if (ui_navScreen) lv_obj_del(ui_navScreen);  // recursively deletes ui_navTileLayer,
                                                   // the raster layer's lv_img, and every
                                                   // feature object under them too

    // The raster layer's backing sprite lives in nav_map_render.cpp's own
    // static storage (PSRAM), not owned by LVGL - lv_obj_del() above just
    // deleted the lv_img object that pointed at it. Free the sprite
    // itself here, same "explicit teardown, not left to next init()"
    // discipline as every other driver in this project.
    nav_map_render_deinit();

    ui_navScreen = NULL;
    ui_navCanvas = NULL;
    ui_navTileLayer = NULL;
    ui_navTrailLine = NULL;
    ui_navHereDot = NULL;
    ui_navStrip = NULL;
    ui_navStatusLabel = NULL;
    ui_navSpeedLabel = NULL;
    ui_navHeadingLabel = NULL;
    ui_navScaleLabel = NULL;
    trailCount = 0;
    trailHead = 0;

    // Feature objects were just deleted along with ui_navTileLayer above -
    // drop the now-dangling pointers and force a fresh nav_tile_load() +
    // rebuild next time the screen is opened, even at the same position.
    for (int i = 0; i < NAV_MAX_FEATURES_PER_TILE; i++) tileFeatureObjs[i] = NULL;
    tileLoaded = false;
}
