// ============================================================================
// ui_navScreen.cpp - GPS NAV: offline map + turn-by-turn navigation
// ============================================================================
// See ui_navScreen.h for the component-5 rebuild history and why this is
// now .cpp. Layout, top to bottom: thin top status bar (time, SOC%), map
// canvas (single NAV tile's filled geometry + route line + turn banner
// overlay + "you are here" dot), bottom telemetry strip (fix status/
// speed/heading, then ETA/distance-remaining/swipe-position-dots).
//
// Projection: local flat-earth approximation, recomputed from scratch
// against the CURRENT (latest) fix every update, not the trail's start
// point - this is what keeps "you are here" pinned to canvas center as the
// vehicle moves. Standard equirectangular approximation (good to well
// under 1% error at the few-km scale this screen covers - the same
// "simple, sanity-checked, not aerospace-grade" bar already applied to the
// sunrise/solar-declination calc elsewhere in this project):
//   metersNorth = (lat - curLat) * 111320
//   metersEast  = (lon - curLon) * 111320 * cos(curLat in radians)
// 111320 = meters per degree of latitude, effectively constant at driving
// latitudes (varies <1% pole-to-equator) - good enough here, not orbital
// mechanics. The route line and turn-banner distance math (gps_math.h/
// nav_turn.h) use real Haversine distance instead - only the on-screen
// pixel projection uses this flat-earth shortcut.
//
// Routing (scope narrowed 2026-09-16 from "general nav" to a small fixed
// destination set, EXTENDED 2026-09-22 from Home-only to Home/Work/nearest
// charging station): a full destination-entry UI (address search, long-
// press-to-set) is real scope this project isn't taking on - the actual,
// explicitly requested feature is picking from a small, fixed list of
// named destinations (see ui_destinationsScreen.h), not navigation to
// anywhere. This is a real simplification, not a placeholder: no
// destination search UI is planned, ever. Destination PICKING lives on
// ui_destinationsScreen.cpp now; this file just owns the routing
// computation itself, button-TRIGGERED via ui_navScreen_requestRoute()
// (called from that screen's row-tap handler), never automatic on screen
// open. Router::route() needs a real ROUTE.bin (CAR profile) on the SD
// card to succeed - if that file doesn't exist yet, routing fails
// gracefully (RouterResult != OK) and the turn banner shows "SELECT
// DESTINATION" (was "PRESS HOME" before the Home-only button was
// replaced), same "false is fine, just means nothing there" convention
// nav_tile_load() already uses.
//
// No SquareLine project (same as every other hand-written screen here).
//
// Navigation: physical swipe LEFT -> GPS (back, stays - both in the
// dock's GPS group). Physical swipe RIGHT -> Destinations (2026-09-22,
// re-added here after being removed in the styling/UX pass - see
// ui_event_navScreen()'s comment; Dyno LIVE is one swipe further from
// there, and still dock-reachable directly - see ui_dock.h's mapping
// comment). This board reports gesture direction inverted from the
// physical swipe (see CLAUDE.md's "Touch gesture direction") - the code
// checks LV_DIR_RIGHT for the physical-LEFT swipe and LV_DIR_LEFT for the
// physical-RIGHT swipe. Intentional; don't "fix" it without re-verifying
// on hardware first.
// ============================================================================

#include "ui.h"
#include "nav_reader.h"
#include "nav_map_render.h"
#include "router.h"
#include "nav_turn.h"
#include "gps_math.h"
#include "gps_driver.h"
#include "zombie_updaters.h"  // utcOffsetHours - see updateTurnGuidance()'s ETA calc
#include <math.h>
#include <esp_heap_caps.h>

lv_obj_t * ui_navScreen = NULL;

static lv_obj_t * ui_navTopBar = NULL;
lv_obj_t * ui_navClockLabel = NULL;
lv_obj_t * ui_navSocLabel = NULL;

static lv_obj_t * ui_navCanvas = NULL;
static lv_obj_t * ui_navTileLayer = NULL;  // map geometry - created before
                                             // the trail/here-dot below so
                                             // they always draw on top of it
static lv_obj_t * ui_navTrailLine = NULL;
static lv_obj_t * ui_navHereDot = NULL;
static lv_obj_t * ui_navScaleLabel = NULL;

static lv_obj_t * ui_navTurnBanner = NULL;
static lv_obj_t * ui_navTurnIconLabel = NULL;
static lv_obj_t * ui_navTurnDistLabel = NULL;

static lv_obj_t * ui_navStrip = NULL;
lv_obj_t * ui_navStatusLabel = NULL;
lv_obj_t * ui_navSpeedLabel = NULL;
lv_obj_t * ui_navHeadingLabel = NULL;
static lv_obj_t * ui_navEtaLabel = NULL;
static lv_obj_t * ui_navDistRemainingLabel = NULL;

// Persistent bottom nav dock (styling/UX pass Phase 5, 2026-09-18) - see
// ui_dock.h. GPS NAV is inside the GPS dock group (home screen: GPS).
static lv_obj_t * ui_navScreenDock = NULL;

// Swipe-nav position dots - purely decorative, this screen's own visual
// convention (no other screen in this project has this pattern yet).
// Matches the current 11-screen topology (CLAUDE.md's "Project overview"
// chain) - update NAV_TOPOLOGY_SCREEN_COUNT/THIS_INDEX together if a
// screen is ever added/removed from that chain.
#define NAV_TOPOLOGY_SCREEN_COUNT 11
#define NAV_TOPOLOGY_THIS_INDEX   8   // 0=Splash,1=Settings,2=Speed,3=Drive,
                                       // 4=Status,5=Battery,6=Charging,7=GPS,
                                       // 8=GPS NAV,9=Dyno LIVE,10=Dyno RESULTS
static lv_obj_t * ui_navDots[NAV_TOPOLOGY_SCREEN_COUNT];

#define TOPBAR_H 32
#define CANVAS_W 800
#define STRIP_H  90
// Shrunk by UI_DOCK_H (styling/UX pass Phase 5, 2026-09-18) to leave room
// for the persistent bottom nav dock - was a flat (480 - TOPBAR_H -
// STRIP_H), which used the full screen height with zero slack.
#define CANVAS_H (480 - TOPBAR_H - STRIP_H - UI_DOCK_H)

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
#define NAV_TILE_ZOOM 16  // matches firmware/assets/gen_nav_fixture.py's
                           // fixture; real map data should target this
                           // zoom too until there's a reason to vary it

// RESOLVED 2026-09-20 - see nav_map_render.cpp's nav_map_render_line()
// comment for the full story: raising NAV_MAX_FEATURES_PER_TILE
// (nav_reader.h, 64->1024) to fix real per-tile DECODING surfaced a
// pre-existing hazard in the RENDER path instead - drawWideLine() (used
// for any LINESTRING/casing wider than 3px) could hang/reboot the board
// on a single occurrence, and a real tile is now guaranteed to include
// every wide-road feature it actually has (the old 64-feature cap just
// happened to get lucky and rarely/never decode one). Root-caused via
// per-feature Serial tracing on real hardware down to one specific
// feature (a widthPx=5 LINESTRING, last in a 233-feature tile) and fixed
// at the source in nav_map_render_line() (drawWideLine() no longer
// called at all). No render-time budget is needed anymore - every
// decoded feature (up to NAV_MAX_FEATURES_PER_TILE) is rendered.

// MULTI-TILE GRID (added 2026-09-20): a single NAV_TILE_ZOOM tile only
// covers ~513m across at this latitude (611.5m projected Mercator tile
// size * cos(33.06 deg)), while the canvas covers CANVAS_W*METERS_PER_PIXEL
// = 2400m across - a single loaded tile filled only a small, roughly
// centered patch of the screen ("I still only see a small map" - real
// user feedback, not a guess). Fix: load and render a grid of tiles
// around the vehicle instead of just one. Sized 5 wide x 2 tall rather
// than a symmetric NxN square:
// - Width (5 tiles = 2565m, centered on the vehicle's own tile: 2 tiles
//   each side) comfortably exceeds the 2400m canvas width with margin on
//   both edges regardless of the vehicle's exact position within its own
//   tile - this is the dimension that was badly undercovered (a single
//   tile's ~513m is only ~21% of the 2400m canvas width).
// - Height (2 tiles = 1026m: the vehicle's own tile + one neighbor,
//   asymmetric - only one side gets a full extra tile of margin) is a
//   deliberate compromise, not a full guarantee like width gets: a single
//   tile's ~513m already covered ~60% of the 858m canvas height (a much
//   smaller gap than width's 21%), so less additional coverage is needed
//   here, and the real constraint is PSRAM - see below.
// PSRAM: confirmed via ui_splash_truck.cpp's own boot-time
// heap_caps_get_free_size() log that only ~4.78MB PSRAM is free after
// splash preload (splash's own ~2.4MB of frame buffers are never freed -
// a separate, pre-existing inefficiency, not addressed here). At ~304KB
// per grid slot (NavTileData ~300KB dominates; gridFeatureObjs's
// NAV_MAX_FEATURES_PER_TILE pointers add ~4KB), a symmetric 5x3 (15
// tiles, ~4.56MB) would leave under 250KB free for GraphLoader's page
// cache (which needs up to ~960KB when routing - see graph_loader.h) -
// too tight, risking an allocation failure the instant "Home" is pressed
// while the full grid is loaded. 5x2 (10 tiles, ~3.04MB) leaves ~1.74MB
// free instead - comfortable margin for GraphLoader plus everything else.
//
// RESOLVED 2026-09-21, root cause found and fixed in
// nav_map_render.cpp::fillPolygonGeneral() - see that function's comment
// for the mechanism. Summary for anyone who finds the old investigation
// referenced elsewhere (git history, prior comments in this file): 5x2
// initially looked freeze-prone on real hardware in a way that survived
// disproving PSRAM/internal-RAM exhaustion, PSRAM-DMA bandwidth
// contention (a vTaskDelay(1) per-slot throttle, modeled on
// waveshareteam/ESP32-S3-Touch-LCD-5#1, did NOT fix it - removed again
// once the real cause was found), memory address/bank boundaries, and
// GPS-tick re-entrancy into repositionTileLayer(). Per-feature Serial
// tracing eventually caught it stalling inside fillPolygonGeneral()
// itself, on the first polygon in a whole 10-tile pass whose bounding
// box set a new maximum edgeBuckets size - a genuine memory-safety bug
// (stale linked-list data surviving a vector growth), not anything about
// PSRAM, timing, or this grid's dimensions. Confirmed stable at 5x2
// across multiple consecutive full render passes once fixed.
#define NAV_GRID_TILES_X 5
#define NAV_GRID_TILES_Y 2
#define NAV_GRID_TILE_COUNT (NAV_GRID_TILES_X * NAV_GRID_TILES_Y)

// One NavTileData + one LVGL-object-pointer array per grid slot, PSRAM-
// allocated by allocateNavBuffers() (called once, lazily, from the first
// ui_navScreen_screen_init()) - see nav_reader.h's sizing comment for why
// a single NavTileData can no longer be a plain static/global (~300KB at
// NAV_MAX_FEATURES_PER_TILE=1024). Allocated ONCE and kept for the
// firmware's lifetime rather than freed on every screen close/reopen
// (like nav_map_render's sprite is) - repeatedly alloc/freeing ~3MB
// across many screen visits over a real drive risks PSRAM fragmentation
// for no benefit.
static NavTileData * gridTiles[NAV_GRID_TILE_COUNT] = { nullptr };
static lv_obj_t ** gridFeatureObjs[NAV_GRID_TILE_COUNT] = { nullptr };
static bool gridSlotLoaded[NAV_GRID_TILE_COUNT] = { false };  // per-slot nav_tile_load() result - false is a legitimate "no data here" (e.g. open water), not an error

static bool gridLoaded = false;                 // whether loadGrid() has ever run
static uint32_t gridCenterTileX = 0, gridCenterTileY = 0;  // which tile the current grid is centered on

// Allocates every gridTiles[]/gridFeatureObjs[] slot above. Returns false
// (logged) if PSRAM is somehow already exhausted by something else -
// callers must treat that as "no map tile geometry available," same
// "degrade, don't crash" convention ui_splash_truck.cpp uses for its own
// PSRAM frame buffers, not a reason to abort screen init.
static bool allocateNavBuffers(void) {
    if (gridTiles[0]) return true;  // already allocated from an earlier call

    bool ok = true;
    for (int s = 0; s < NAV_GRID_TILE_COUNT; s++) {
        // calloc, not malloc: unlike the old single `static NavTileData`
        // (implicitly zero-initialized .bss), a PSRAM heap allocation is
        // NOT zeroed - featureCount must start at 0 so
        // repositionTileLayer()'s `for (i < tile->featureCount)` loop
        // can't ever iterate over uninitialized garbage before the first
        // real nav_tile_load() for that slot (possible: tile-load and
        // reposition/render are both pending-flag-serviced from loop()
        // independently - see ui_navScreen_processPendingTileLoad()/
        // ui_navScreen_processPendingReposition() - with no ordering
        // guarantee between them on a screen's first fix).
        gridTiles[s] = (NavTileData *)heap_caps_calloc(1, sizeof(NavTileData), MALLOC_CAP_SPIRAM);
        gridFeatureObjs[s] = (lv_obj_t **)heap_caps_calloc(
            NAV_MAX_FEATURES_PER_TILE, sizeof(lv_obj_t *), MALLOC_CAP_SPIRAM);
        if (!gridTiles[s] || !gridFeatureObjs[s]) ok = false;
    }

    if (!ok) {
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.println("[nav] PSRAM allocation failed for one or more grid tile buffers - map tile geometry may not fully render this session");
#endif
    }
    return ok;
}

// --- Routing (2026-09-22: generalized from "phone home" to an arbitrary
// destination - see ui_navScreen_requestRoute() below and
// ui_destinationsScreen.h for why: real bench use found a single Home
// button + "PRESS HOME" banner confusing and too much canvas real estate,
// so destination PICKING moved to its own screen (Home/Work/nearest
// charging station), while this file keeps owning the actual routing
// computation + turn-by-turn display. Still no address-entry/search UI -
// see this file's header comment for why that's out of scope, ever; the
// destination is always one of a small, fixed set chosen elsewhere, never
// typed in here.) ---
#define NAV_ROUTE_SPEED_KMH 60  // selects the CAR ROUTE.bin profile - see routeBinPath()

#define NAV_ROUTE_MAX_POINTS 300  // cap for on-screen route line drawing -
                                    // see this file's header comment; a
                                    // real A* route can be longer than
                                    // this, silently truncated same as
                                    // every other fixed-capacity buffer
                                    // in this project (not an error)
static int routePX[NAV_ROUTE_MAX_POINTS];
static int routePY[NAV_ROUTE_MAX_POINTS];

static TrackVector navRoute;
static TurnPointVector navTurns;
static NavState navState;
static bool routeComputePending = false;  // set by ui_navScreen_requestRoute(),
                                            // serviced from loop() - see
                                            // ui_navScreen_processPendingRoute()
static bool routeComputing = false;   // true from the request until
                                        // processPendingRoute() finishes -
                                        // lets updateTurnGuidance() show a
                                        // "ROUTING..." state instead of
                                        // "SELECT DESTINATION" during the
                                        // multi-second (sometimes 20+s, see
                                        // this file's header) blocking
                                        // computation
static double routeComputeLat = 0, routeComputeLon = 0;
static double routeDestLat = 0, routeDestLon = 0;  // target of the pending/active route - see ui_navScreen_requestRoute()

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

// Deletes every grid slot's LVGL objects (TEXT/POINT features) - called
// before a grid reload so a slot whose tile content changed (or that
// used to have data and now doesn't) doesn't leave stale objects behind.
static void clearGridFeatureObjs(void) {
    for (int s = 0; s < NAV_GRID_TILE_COUNT; s++) {
        lv_obj_t ** objs = gridFeatureObjs[s];
        if (!objs) continue;
        for (int i = 0; i < NAV_MAX_FEATURES_PER_TILE; i++) {
            if (objs[i]) {
                lv_obj_del(objs[i]);
                objs[i] = NULL;
            }
        }
    }
}

// Creates one LVGL object per decoded feature in gridTiles[slot] - called
// once per slot right after that slot's nav_tile_load() succeeds, not on
// every GPS fix. Screen position isn't set here; repositionTileLayer()
// does that every fix, same split as the trail's addPoint()/redrawTrail().
static void buildGridSlotLayer(int slot) {
    NavTileData * tile = gridTiles[slot];
    lv_obj_t ** objs = gridFeatureObjs[slot];
    if (!tile || !objs) return;  // PSRAM alloc failed - see allocateNavBuffers()
    for (uint16_t i = 0; i < tile->featureCount; i++) {
        NavFeature *f = &tile->features[i];
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
            objs[i] = label;
        } else if (f->geomType == NAV_GEOM_POINT) {
            lv_obj_t * dot = lv_obj_create(ui_navTileLayer);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(dot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            objs[i] = dot;
        } else {
            // LINESTRING or POLYGON - rendered into nav_map_render's
            // raster sprite instead of an lv_obj (see file header); no
            // LVGL object needed, objs[i] stays NULL so
            // repositionTileLayer() knows to route this feature there.
            objs[i] = NULL;
        }
    }
}

// Projects navRoute into screen space (same flat-earth-meters projection
// as everything else on this canvas) and draws it into nav_map_render's
// raster frame on top of the tile geometry - called from
// repositionTileLayer() once per frame, after all tile features. No-op if
// no route has been computed yet.
//
// widthPx MUST stay <= nav_map_render.cpp's WIDE_LINE_THRESHOLD_PX (3).
// A real hardware crash (2026-09-16) traced via decoded backtrace to
// exactly this call at widthPx=5: repositionTileLayer()/drawRouteLine()
// runs inside ui_navScreen_addPoint(), which runs inside
// zombie_updaters.cpp's slowUpdate() - a Ticker/esp_timer callback with a
// small, fixed stack. width>3 sends nav_map_render_line() down
// LovyanGFX's anti-aliased drawWideLine() path (drawWideLine ->
// draw_wedgeline -> draw_gradient_wedgeline -> fillRectAlpha ->
// writeFillRectAlphaPreclipped -> IPanel::effect<>), a deep templated
// call chain that overflowed that stack (Guru Meditation,
// InstrFetchProhibited, backtrace terminating at the classic 0xfffffffd
// sentinel) the first time a real multi-point route (93 waypoints) was
// drawn - tile road/casing features already stay under this same
// threshold in practice, which is why this was never hit before. Do not
// raise this past 3 without first moving the route-line draw out of
// Ticker context (same pending-flag-serviced-from-loop() pattern already
// used for nav_tile_load()/Router::route()).
static void drawRouteLine(double curLat, double curLon, double cosLat) {
    if (navRoute.empty()) return;

    int cx = CANVAS_W / 2;
    int cy = CANVAS_H / 2;

    int n = (int)navRoute.size();
    if (n > NAV_ROUTE_MAX_POINTS) n = NAV_ROUTE_MAX_POINTS;

    for (int i = 0; i < n; i++) {
        double metersNorth = (navRoute[i].lat - curLat) * METERS_PER_DEG_LAT;
        double metersEast  = (navRoute[i].lon - curLon) * METERS_PER_DEG_LAT * cosLat;
        routePX[i] = (int)(cx + metersEast / METERS_PER_PIXEL);
        routePY[i] = (int)(cy - metersNorth / METERS_PER_PIXEL);
    }

    uint16_t routeColor565 = lv_color_to16(ui_theme_accent());
    nav_map_render_line(routePX, routePY, n, /*widthPx=*/3, routeColor565,
                         /*drawCasing=*/false, 0, 0);
}

// Recomputes every active tile feature's screen position relative to the
// CURRENT fix, exactly like redrawTrail() does for the breadcrumb trail -
// called every GPS fix (not just on tile reload) so map geometry stays
// centered as the vehicle moves, even between tile crossings.
static void repositionTileLayer(double curLat, double curLon, double cosLat) {
#if defined(DEBUG) || defined(CAN_TRACE)
    uint32_t tRepoStart = millis();
#endif
    int cx = CANVAS_W / 2;
    int cy = CANVAS_H / 2;

    // One raster frame for every LINESTRING/POLYGON feature this call -
    // begin/end bracket the per-feature nav_map_render_polygon()/_line()
    // calls below (plus the route line), matching nav_map_render.h's
    // one-call-per-frame contract. TEXT/POINT features (LVGL objects) are
    // repositioned in the same loop but don't touch the raster frame at
    // all.
    nav_map_render_begin_frame(lv_color_to16(ui_theme_panel_bg()));

    // Per-feature scratch space, reused across every feature of every
    // grid slot - only ever holds ONE feature's projected vertices at a
    // time (computed, immediately handed to nav_map_render_polygon()/
    // _line(), then overwritten by the next feature), so this can be a
    // small stack local instead of a PSRAM buffer sized for a whole
    // tile's worth of features (the old tileFeaturePX/PY design
    // allocated NAV_MAX_FEATURES_PER_TILE rows but, on inspection, never
    // actually needed more than one row live at once either - this just
    // makes that explicit, and makes a multi-slot grid affordable memory-
    // wise: the per-slot NavTileData below is the only thing that
    // actually needs to persist between fixes, since decoded local-tile-
    // space vertices don't change, only their re-projection to screen
    // space each fix does).
    int scratchPX[NAV_MAX_VERTICES_PER_FEATURE + 1];
    int scratchPY[NAV_MAX_VERTICES_PER_FEATURE + 1];

    for (int slot = 0; slot < NAV_GRID_TILE_COUNT; slot++) {
        if (!gridSlotLoaded[slot]) continue;  // e.g. open water / outside generated area - not an error
        NavTileData * tile = gridTiles[slot];
        lv_obj_t ** objs = gridFeatureObjs[slot];
        if (!tile || !objs) continue;  // PSRAM alloc failed - see allocateNavBuffers()

#if defined(DEBUG) || defined(CAN_TRACE)
        uint32_t tSlot0 = millis();
        Serial.printf("[nav-render] >>> slot %d (tile %u,%u) featureCount=%u\n",
                      slot, (unsigned)tile->tileX, (unsigned)tile->tileY, (unsigned)tile->featureCount);
#endif
        for (uint16_t i = 0; i < tile->featureCount; i++) {
            NavFeature *f = &tile->features[i];

            if (f->geomType == NAV_GEOM_TEXT || f->geomType == NAV_GEOM_POINT) {
                lv_obj_t * obj = objs[i];
                if (!obj) continue;
                int16_t lx = (f->geomType == NAV_GEOM_TEXT) ? f->textX : f->vx[0];
                int16_t ly = (f->geomType == NAV_GEOM_TEXT) ? f->textY : f->vy[0];
                double lat, lon;
                nav_tile_local_to_latlon(NAV_TILE_ZOOM, tile->tileX, tile->tileY, lx, ly, &lat, &lon);
                double metersNorth = (lat - curLat) * METERS_PER_DEG_LAT;
                double metersEast  = (lon - curLon) * METERS_PER_DEG_LAT * cosLat;
                lv_coord_t x = (lv_coord_t)(cx + metersEast / METERS_PER_PIXEL);
                lv_coord_t y = (lv_coord_t)(cy - metersNorth / METERS_PER_PIXEL);
                // Anchor at top-left rather than centering on the label/dot's
                // own size - close enough for a first pass, avoids a
                // lv_obj_get_size() round-trip per feature per fix.
                lv_obj_set_pos(obj, x, y);
            } else {
                // Project every vertex (all rings, not just the outer one -
                // nav_map_render_polygon() supports holes via ringEnds,
                // unlike the lv_line approach this replaced) and hand off to
                // the raster renderer.
                uint16_t vertCount = f->vertexCount;
                if (vertCount > NAV_MAX_VERTICES_PER_FEATURE) vertCount = NAV_MAX_VERTICES_PER_FEATURE;

                for (uint16_t v = 0; v < vertCount; v++) {
                    double lat, lon;
                    nav_tile_local_to_latlon(NAV_TILE_ZOOM, tile->tileX, tile->tileY, f->vx[v], f->vy[v], &lat, &lon);
                    double metersNorth = (lat - curLat) * METERS_PER_DEG_LAT;
                    double metersEast  = (lon - curLon) * METERS_PER_DEG_LAT * cosLat;
                    scratchPX[v] = (int)(cx + metersEast / METERS_PER_PIXEL);
                    scratchPY[v] = (int)(cy - metersNorth / METERS_PER_PIXEL);
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
                    nav_map_render_polygon(scratchPX, scratchPY, vertCount,
                                            color565, f->ringCount, f->ringCount > 0 ? f->ringEnds : NULL,
                                            drawCasing, casingColor);
                } else {
                    uint8_t widthPx = f->widthPx / 2;  // widthPx is in 0.5px units (nav_tile_format.h)
                    if (widthPx == 0) widthPx = 1;
                    bool drawCasing = f->isCasingOrSpecial;
                    uint16_t casingColor = drawCasing ? nav_map_render_darken(f->colorRgb565, 0.3f) : 0;
                    uint8_t casingWidthPx = widthPx + 2;
                    nav_map_render_line(scratchPX, scratchPY, vertCount,
                                         widthPx, color565, drawCasing, casingColor, casingWidthPx);
                }
            }
        }
#if defined(DEBUG) || defined(CAN_TRACE)
        Serial.printf("[nav-render] <<< slot %d done: %lums\n", slot, (unsigned long)(millis() - tSlot0));
#endif
    }

    drawRouteLine(curLat, curLon, cosLat);

    nav_map_render_end_frame();
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[nav-timing] repositionTileLayer TOTAL: %lums\n", (unsigned long)(millis() - tRepoStart));
#endif
}

// Loads the whole NAV_GRID_TILES_X x NAV_GRID_TILES_Y grid of tiles
// centered on (centerTx, centerTy), only when that's actually a different
// center than what's already loaded/built - SD reads only happen on a
// grid-crossing, not every fix. Rebuilding the LVGL objects happens here
// too since the feature set (and therefore how many/what kind of objects
// are needed) only changes when a tile does.
// Does the actual blocking work (NAV_GRID_TILE_COUNT x nav_tile_load() +
// rebuilding LVGL objects) - see ui_navScreen_processPendingTileLoad()'s
// header comment in ui_navScreen.h for why this must never be called from
// ui_navScreen_addPoint()/slowUpdate()'s Ticker-callback context. Costs
// proportionally more than the single-tile load this replaced (up to
// NAV_GRID_TILE_COUNT sequential nav_tile_load() calls, each already
// individually measured at 100-800ms on real hardware) - still safe
// (runs from loop(), a normal task) but a real, visible multi-second
// touch-unresponsive hitch on every grid crossing is the accepted trade-
// off for filling the screen - see NAV_GRID_TILES_X/Y's comment.
static void loadGrid(uint32_t centerTx, uint32_t centerTy, double lat, double lon) {
    gridCenterTileX = centerTx;
    gridCenterTileY = centerTy;
    clearGridFeatureObjs();

    const int halfX = NAV_GRID_TILES_X / 2;
    const int halfY = NAV_GRID_TILES_Y / 2;
    bool centerOk = false;

#if defined(DEBUG) || defined(CAN_TRACE)
    uint32_t tLoad0 = millis();
#endif
    for (int gy = 0; gy < NAV_GRID_TILES_Y; gy++) {
        for (int gx = 0; gx < NAV_GRID_TILES_X; gx++) {
            int slot = gy * NAV_GRID_TILES_X + gx;
            uint32_t tx = (uint32_t)((int32_t)centerTx + (gx - halfX));
            uint32_t ty = (uint32_t)((int32_t)centerTy + (gy - halfY));

            // gridTiles[slot] is null if allocateNavBuffers() failed
            // (PSRAM exhausted) - treat exactly like "no data for this
            // tile" rather than crash.
            bool ok = gridTiles[slot] && nav_tile_load(NAV_TILE_ZOOM, tx, ty, gridTiles[slot]);
            gridSlotLoaded[slot] = ok;
            if (ok) buildGridSlotLayer(slot);
            if (gx == halfX && gy == halfY) centerOk = ok;  // the vehicle's own tile
        }
    }
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[nav-timing] loadGrid centered (%u,%u): %lums total for %d tiles\n",
                  (unsigned)centerTx, (unsigned)centerTy, (unsigned long)(millis() - tLoad0), NAV_GRID_TILE_COUNT);
#endif

    gridLoaded = true;
    if (centerOk) {
        // Display-only imperial conversion (2026-09-22), same rule as
        // formatDistance() above - CANVAS_W*METERS_PER_PIXEL itself stays
        // meters, this is a fixed constant so always comfortably >528ft,
        // always renders in miles.
        if (ui_navScaleLabel) lv_label_set_text_fmt(ui_navScaleLabel, "~%.1fmi across", CANVAS_W * METERS_PER_PIXEL * 3.28084f / 5280.0f);
    } else if (ui_navScaleLabel) {
        // Styling pass item 16: was "~%.0fm across - no map tiles yet" -
        // exposed internal implementation detail ("tiles") to the driver,
        // and a scale distance is meaningless without a map rendered to
        // scale against, so dropped rather than kept alongside a caveat.
        // Based on the CENTER slot only (the vehicle's own tile) - a
        // neighbor slot failing (e.g. open water) is normal and doesn't
        // mean "no map data here."
        lv_label_set_text(ui_navScaleLabel, ICON_LOCATION_ON " NO MAP DATA HERE");
    }
    // Reposition immediately against the fix that triggered this load,
    // rather than waiting for the next addPoint() (~1s away at typical
    // GPS fix rates) to see the newly loaded grid's geometry.
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

    if (gridLoaded && tx == gridCenterTileX && ty == gridCenterTileY) return;

    tileLoadPending = true;
    pendingLat = lat;
    pendingLon = lon;
}

void ui_navScreen_processPendingTileLoad(void) {
    if (!tileLoadPending) return;
    tileLoadPending = false;

    // The screen may have been swiped away between the fix that queued
    // this load and this loop() iteration servicing it - ui_navTileLayer
    // (the LVGL parent buildGridSlotLayer() creates objects under) would
    // be NULL, same "no work for a screen nobody has opened" convention
    // as ui_navScreen_addPoint().
    if (!ui_navScreen) return;

    uint32_t tx, ty;
    nav_latlon_to_tile(NAV_TILE_ZOOM, pendingLat, pendingLon, &tx, &ty);
    loadGrid(tx, ty, pendingLat, pendingLon);
}

// Flags a route computation toward an arbitrary destination - never blocks
// here, see ui_navScreen_processPendingRoute(). destLat/destLon come from
// whichever row was tapped on the Destinations screen (Home/Work/nearest
// charging station) via ui_navScreen_requestRoute(), not typed in here -
// see this file's header comment for why routing is a fixed-destination-
// picker, not general nav, and computes only on that tap, never
// automatically.
static void requestRouteTo(double lat, double lon, double destLat, double destLon) {
    if (routeComputing) return;  // already routing - ignore a double-tap
    routeComputing = true;
    routeComputePending = true;
    routeComputeLat = lat;
    routeComputeLon = lon;
    routeDestLat = destLat;
    routeDestLon = destLon;
}

// Public entry point - called from the Destinations screen's row-tap
// handler (ui_destinationsScreen.cpp). Ignores the request with no visible
// state change if there's no GPS fix yet (routing from (0,0) would be
// meaningless) or a route is already being computed (requestRouteTo()
// itself also guards this, but checking here too avoids even queuing a
// redundant SD-bound request).
void ui_navScreen_requestRoute(double destLat, double destLon) {
    if (!gpsData.hasFix || routeComputing) return;
    requestRouteTo(gpsData.latitude, gpsData.longitude, destLat, destLon);
}

void ui_navScreen_processPendingRoute(void) {
    if (!routeComputePending) return;
    routeComputePending = false;
    if (!ui_navScreen) { routeComputing = false; return; }  // screen closed before this got serviced

    RouterResult result = router.route((float)routeComputeLat, (float)routeComputeLon,
                                        (float)routeDestLat, (float)routeDestLon,
                                        NAV_ROUTE_SPEED_KMH, navRoute);
    if (result == RouterResult::OK && !navRoute.empty()) {
        navTurns = nav_turn_detect(navRoute);
    } else {
        navRoute.clear();
        navTurns.clear();
    }
    navState = NavState{};
    routeComputing = false;
}

// Maps a TurnDirection to its icon glyph - NONE case (OFF_TRACK, or no
// route computed) is handled by the caller instead of here since it also
// needs different label text, not just a different icon.
static const char* turnIconFor(TurnDirection dir) {
    switch (dir) {
        case TurnDirection::SOFT_LEFT:  return TURN_ICON_TURN_LEFT;
        case TurnDirection::SOFT_RIGHT: return TURN_ICON_TURN_RIGHT;
        case TurnDirection::HARD_LEFT:  return TURN_ICON_TURN_SHARP_LEFT;
        case TurnDirection::HARD_RIGHT: return TURN_ICON_TURN_SHARP_RIGHT;
        case TurnDirection::FINISH:     return TURN_ICON_FLAG;
        case TurnDirection::STRAIGHT:
        default:                        return TURN_ICON_STRAIGHT;
    }
}

// Display-only metric->imperial conversion (2026-09-22, matches this
// project's mph speed-unit convention - CLAUDE.md/ui_speedScreen.h): the
// distance math everywhere else in this file (calcDist(), route/turn
// geometry) stays in meters, only this formatting step converts. Switches
// to miles at 0.1mi (528ft), the same rough threshold turn-by-turn nav
// UIs commonly use.
static void formatDistance(char *buf, size_t bufSize, float meters) {
    float feet = meters * 3.28084f;
    if (feet < 528.0f) snprintf(buf, bufSize, "%.0f ft", feet);
    else snprintf(buf, bufSize, "%.1f mi", feet / 5280.0f);
}

// Updates the turn banner and the bottom strip's ETA/distance-remaining -
// called every GPS fix from ui_navScreen_addPoint(), pure math, no I/O.
static void updateTurnGuidance(double lat, double lon, float speedKph) {
    if (!ui_navTurnIconLabel || !ui_navTurnDistLabel) return;

    if (navRoute.empty()) {
        lv_label_set_text(ui_navTurnIconLabel, TURN_ICON_STRAIGHT);
        // "SELECT DESTINATION" (2026-09-22) overflowed this banner's fixed
        // 220x72 box + 24px ExtraBold font - both were sized for "PRESS
        // HOME" (10 chars) back when there was an on-screen button this
        // text referred to. That button is gone now (swipe to Destinations
        // is the only way there - see ui_event_navScreen()), so this
        // banner goes back to being a short, neutral status readout rather
        // than a call-to-action naming a control that isn't on this screen
        // anymore - matches "OFF ROUTE" and "ROUTING" for length/tone.
        lv_label_set_text(ui_navTurnDistLabel, routeComputing ? "ROUTING" : "NO ROUTE");
        if (ui_navEtaLabel) lv_label_set_text(ui_navEtaLabel, "ETA --:--");
        if (ui_navDistRemainingLabel) lv_label_set_text(ui_navDistRemainingLabel, "-- remaining");
        return;
    }

    TurnGuidance g = evaluateTurnGuidance((float)lat, (float)lon, navRoute, navTurns, navState);

    char distBuf[24];
    if (g.direction == TurnDirection::OFF_TRACK) {
        lv_label_set_text(ui_navTurnIconLabel, TURN_ICON_STRAIGHT);
        lv_label_set_text(ui_navTurnDistLabel, "OFF ROUTE");
    } else {
        lv_label_set_text(ui_navTurnIconLabel, turnIconFor(g.direction));
        formatDistance(distBuf, sizeof(distBuf), g.distanceMeters);
        lv_label_set_text(ui_navTurnDistLabel, distBuf);
    }

    // Distance remaining: straight-line to the route's final waypoint,
    // not the actual remaining path length - a deliberate simplification
    // (see this file's header comment's "simple, not aerospace-grade" bar).
    float distRemaining = calcDist((float)lat, (float)lon,
                                    navRoute.back().lat, navRoute.back().lon);
    if (ui_navDistRemainingLabel) {
        formatDistance(distBuf, sizeof(distBuf), distRemaining);
        lv_label_set_text_fmt(ui_navDistRemainingLabel, "%s remaining", distBuf);
    }

    if (ui_navEtaLabel) {
        if (speedKph < 1.0f) {
            lv_label_set_text(ui_navEtaLabel, "ETA --:--");
        } else {
            float etaHours = (distRemaining / 1000.0f) / speedKph;
            // Local-time display, same display-only utcOffsetHours shift as
            // the splash/nav clocks (zombie_updaters.cpp) - gpsData.hour
            // itself is untouched, this is a local copy for formatting only.
            int curHour = 0, curMinute = 0;
            if (gpsData.hasFix) {
                curHour = ((int)gpsData.hour + utcOffsetHours % 24 + 24) % 24;
                curMinute = gpsData.minute;
            }
            double totalMinutes = curHour * 60.0 + curMinute + etaHours * 60.0;
            int etaHour = ((int)(totalMinutes / 60.0)) % 24;
            int etaMinute = ((int)totalMinutes) % 60;
            if (etaHour < 0) etaHour += 24;
            if (etaMinute < 0) etaMinute += 60;
            lv_label_set_text_fmt(ui_navEtaLabel, "ETA %02d:%02d", etaHour, etaMinute);
        }
    }
}

void ui_event_navScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_gpsScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, &ui_gpsScreen_screen_init);
        _ui_screen_delete(&ui_navScreen);
    } else if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        // Physical-RIGHT swipe to Destinations (2026-09-22) - restores the
        // forward link this screen lost to Dyno LIVE in the styling/UX
        // pass, now pointing at the new Destinations screen instead (Dyno
        // LIVE is one swipe further from there, and still dock-reachable
        // directly - see ui_destinationsScreen.h's header comment).
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_destinationsScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_destinationsScreen_screen_init);
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

static bool repositionPending = false;  // see ui_navScreen_processPendingReposition()
static double pendingRepoLat = 0, pendingRepoLon = 0;

void ui_navScreen_addPoint(double lat, double lon) {
    if (!ui_navScreen) return;  // no work for a screen nobody has opened yet

    trailLat[trailHead] = lat;
    trailLon[trailHead] = lon;
    trailHead = (trailHead + 1) % TRAIL_MAX_POINTS;
    if (trailCount < TRAIL_MAX_POINTS) trailCount++;

    redrawTrail();

    // Map tile layer: flags a reload on a tile crossing but never blocks
    // here - see updateNavTile()'s comment. repositionTileLayer() itself
    // is ALSO now deferred (see ui_navScreen_processPendingReposition())
    // - it used to be called directly from here on the (wrong, as of
    // 2026-09-20) assumption that it was "cheap, no I/O". That was true
    // back when a real tile only ever decoded 64 features; once real
    // per-tile decoding/rendering was fixed to cover the true 174-1066
    // features a real tile has, repositionTileLayer()'s raster-fill pass
    // started taking 150-200+ms per call - confirmed on real hardware to
    // trip the IDLE0 task watchdog (same "CPU 0: esp_timer" signature as
    // the nav_tile_load()-from-Ticker bug this file's header already
    // documents) when called from here, since ui_navScreen_addPoint()
    // itself runs from slowUpdate()'s Ticker/esp_timer callback context.
    updateNavTile(lat, lon);
    repositionPending = true;
    pendingRepoLat = lat;
    pendingRepoLon = lon;

    // Routing is button-triggered now (ui_event_navHomeBtn), not computed
    // automatically here - see this file's header comment. Turn guidance
    // still updates every fix so it tracks progress once a route exists.
    updateTurnGuidance(lat, lon, gpsData.speedKph);
}

// Services a pending reposition/render flagged by ui_navScreen_addPoint()
// - see that function's comment for why this can't run from
// slowUpdate()'s Ticker-callback context (same reasoning as
// ui_navScreen_processPendingTileLoad()/ui_navScreen_processPendingRoute(),
// just for the raster-render pass instead of SD I/O). Safe to call every
// loop() iteration - cheap no-op when nothing's pending.
void ui_navScreen_processPendingReposition(void) {
    if (!repositionPending) return;
    repositionPending = false;
    if (!ui_navScreen) return;  // screen closed before this got serviced
    repositionTileLayer(pendingRepoLat, pendingRepoLon, cos(pendingRepoLat * M_PI / 180.0));
}

void ui_navScreen_screen_init(void)
{
#if defined(DEBUG) || defined(CAN_TRACE)
    uint32_t t0 = millis();
#endif
    allocateNavBuffers();  // no-op after the first successful call; see its comment
#if defined(DEBUG) || defined(CAN_TRACE)
    uint32_t t1 = millis();
    Serial.printf("[nav-timing] allocateNavBuffers: %lums\n", (unsigned long)(t1 - t0));
#endif

    ui_navScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_navScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_navScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Top status bar: time + SOC% (see this file's header comment -
    // no ambient-temperature sensor exists in this project, so the
    // original mockup's third "temp" slot is dropped rather than faked) ---
    ui_navTopBar = lv_obj_create(ui_navScreen);
    lv_obj_clear_flag(ui_navTopBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_navTopBar, CANVAS_W, TOPBAR_H);
    lv_obj_set_pos(ui_navTopBar, 0, 0);
    lv_obj_set_style_pad_all(ui_navTopBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_navTopBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navTopBar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navTopBar, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navTopBar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navClockLabel = lv_label_create(ui_navTopBar);
    lv_obj_align(ui_navClockLabel, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_text(ui_navClockLabel, "--:--");
    lv_obj_set_style_text_color(ui_navClockLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navClockLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navSocLabel = lv_label_create(ui_navTopBar);
    lv_obj_align(ui_navSocLabel, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_label_set_text(ui_navSocLabel, ICON_BOLT " --%");
    lv_obj_set_style_text_color(ui_navSocLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navSocLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Map canvas ---
    ui_navCanvas = lv_obj_create(ui_navScreen);
    lv_obj_clear_flag(ui_navCanvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_navCanvas, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(ui_navCanvas, 0, TOPBAR_H);
    lv_obj_set_style_pad_all(ui_navCanvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_navCanvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navCanvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navCanvas, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navCanvas, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Raster map layer (roads/polygons + route line, filled with holes
    // via nav_map_render.h) - created FIRST so it's the bottommost child;
    // ui_navTileLayer's point/text objects and the trail/here-dot/turn
    // banner below all draw on top of it.
#if defined(DEBUG) || defined(CAN_TRACE)
    uint32_t tRenderInit0 = millis();
#endif
    nav_map_render_init(ui_navCanvas, CANVAS_W, CANVAS_H);
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[nav-timing] nav_map_render_init: %lums\n", (unsigned long)(millis() - tRenderInit0));
#endif

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
    for (int s = 0; s < NAV_GRID_TILE_COUNT; s++) {
        if (!gridFeatureObjs[s]) continue;
        for (int i = 0; i < NAV_MAX_FEATURES_PER_TILE; i++) gridFeatureObjs[s][i] = NULL;
    }
    gridLoaded = false;

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
    // Styling pass, 2026-09-18 (item 9): was ui_theme_text_primary(), which
    // resolves to pure white (#FFFFFF) in Day mode - the one genuinely-
    // white element on this screen (the trail line and this dot's own
    // border already used ui_theme_accent()). Now solid accent-colored
    // fill+border, matching every other "live/active" indicator on this
    // screen instead of standing out as plain white wireframe.
    lv_obj_set_style_bg_color(ui_navHereDot, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navHereDot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navHereDot, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_navHereDot, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navScaleLabel = lv_label_create(ui_navCanvas);
    lv_obj_align(ui_navScaleLabel, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    // Styling pass item 16 (see loadGrid()'s matching comment): no tile
    // loaded yet at boot, so the honest default is "no map data" rather
    // than a scale figure with nothing to scale.
    lv_label_set_text(ui_navScaleLabel, ICON_LOCATION_ON " NO MAP DATA HERE");
    lv_obj_set_style_text_color(ui_navScaleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navScaleLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    // The on-canvas "Open Destinations" button (2026-09-22 addition, same
    // day removed again per Rob: redundant once the physical-RIGHT swipe
    // reaches ui_destinationsScreen directly - see ui_event_navScreen()) -
    // no widget here now, swipe is the only way to Destinations from this
    // screen. KEEPING the touch dead-zone finding below for whoever next
    // places a widget in this screen region, found via a decoded touch-
    // event trace on real hardware (2026-09-18), not guessed:
    // 1. The thin 32px top status bar is completely unreachable; real taps
    //    landed 35-40px below whatever's placed there.
    // 2. The map canvas's top-RIGHT corner is also completely unreachable,
    //    but for a different reason: taps in that screen region produced
    //    NO touch event at all (not even bubbling to the screen root),
    //    confirmed by repeated deliberate taps at the confirmed-correct
    //    on-screen location - a real touch-panel dead zone near that edge,
    //    not a widget/hit-testing bug.
    // A tap at TOP_MID, ~y=92 (horizontally centered, directly below the
    // turn banner, ~x=398 near the vertical center of the top bar/canvas
    // boundary) DID register correctly during the same trace, if a future
    // widget needs a proven-responsive spot here.

    // --- Turn-instruction banner, overlaid near the top of the map ---
    ui_navTurnBanner = lv_obj_create(ui_navCanvas);
    lv_obj_clear_flag(ui_navTurnBanner, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_navTurnBanner, 220, 72);
    lv_obj_align(ui_navTurnBanner, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(ui_navTurnBanner, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navTurnBanner, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navTurnBanner, 235, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navTurnBanner, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_navTurnBanner, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui_navTurnBanner, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navTurnIconLabel = lv_label_create(ui_navTurnBanner);
    lv_obj_align(ui_navTurnIconLabel, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_text(ui_navTurnIconLabel, TURN_ICON_STRAIGHT);
    lv_obj_set_style_text_color(ui_navTurnIconLabel, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navTurnIconLabel, &font_turn_icons_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navTurnDistLabel = lv_label_create(ui_navTurnBanner);
    lv_obj_align(ui_navTurnDistLabel, LV_ALIGN_RIGHT_MID, -4, 0);
    // Init default matches updateTurnGuidance()'s own no-route text ("NO
    // ROUTE" as of 2026-09-22 - see that function's comment on why it's
    // no longer "PRESS HOME") instead of leaving this showing something
    // stale until the first GPS fix triggers a real update.
    lv_label_set_text(ui_navTurnDistLabel, "NO ROUTE");
    lv_obj_set_style_text_color(ui_navTurnDistLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navTurnDistLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- Telemetry strip ---
    ui_navStrip = lv_obj_create(ui_navScreen);
    lv_obj_clear_flag(ui_navStrip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui_navStrip, CANVAS_W, STRIP_H);
    lv_obj_set_pos(ui_navStrip, 0, TOPBAR_H + CANVAS_H);
    lv_obj_set_style_pad_all(ui_navStrip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_navStrip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_navStrip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_navStrip, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_navStrip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Row 1 (top half): fix status / speed / heading - unchanged from the
    // breadcrumb-trail-only version.
    ui_navStatusLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navStatusLabel, LV_ALIGN_TOP_LEFT, 24, 6);
    lv_label_set_text(ui_navStatusLabel, "NO FIX");
    lv_obj_set_style_text_color(ui_navStatusLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // Font weight audit (item 2): state text ("NO FIX"/"FIX - N sats"),
    // same role as the GPS screen's fix pill - SemiBold, not the numeric
    // ExtraBold used for Speed/Heading on either side of it.
    lv_obj_set_style_text_font(ui_navStatusLabel, &font_montserrat_semibold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navSpeedLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navSpeedLabel, LV_ALIGN_TOP_MID, 0, 6);
    lv_label_set_text(ui_navSpeedLabel, ICON_SPEED " -- km/h");
    lv_obj_set_style_text_color(ui_navSpeedLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navSpeedLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navHeadingLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navHeadingLabel, LV_ALIGN_TOP_RIGHT, -24, 6);
    lv_label_set_text(ui_navHeadingLabel, ICON_NAVIGATION " --\xC2\xB0");
    lv_obj_set_style_text_color(ui_navHeadingLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navHeadingLabel, &font_montserrat_extrabold_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Row 2 (bottom half): ETA / distance-remaining / swipe-position dots.
    ui_navEtaLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navEtaLabel, LV_ALIGN_BOTTOM_LEFT, 24, -8);
    lv_label_set_text(ui_navEtaLabel, "ETA --:--");
    lv_obj_set_style_text_color(ui_navEtaLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navEtaLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_navDistRemainingLabel = lv_label_create(ui_navStrip);
    lv_obj_align(ui_navDistRemainingLabel, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(ui_navDistRemainingLabel, "-- remaining");
    lv_obj_set_style_text_color(ui_navDistRemainingLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_navDistRemainingLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * dotsRow = lv_obj_create(ui_navStrip);
    lv_obj_clear_flag(dotsRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dotsRow, NAV_TOPOLOGY_SCREEN_COUNT * 16, 8);
    lv_obj_align(dotsRow, LV_ALIGN_BOTTOM_RIGHT, -24, -12);
    lv_obj_set_style_bg_opa(dotsRow, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dotsRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(dotsRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(dotsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dotsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < NAV_TOPOLOGY_SCREEN_COUNT; i++) {
        lv_obj_t * dot = lv_obj_create(dotsRow);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        bool isThis = (i == NAV_TOPOLOGY_THIS_INDEX);
        lv_obj_set_style_bg_color(dot, isThis ? ui_theme_accent() : ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(dot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_navDots[i] = dot;
    }

    // Child of the SCREEN, not the canvas (ui_navCanvas) - CANVAS_H was
    // shrunk above precisely so this dock sits below the canvas/strip, not
    // over them.
#if defined(DEBUG) || defined(CAN_TRACE)
    uint32_t tDock0 = millis();
#endif
    ui_navScreenDock = ui_dock_create(ui_navScreen, UI_DOCK_GPS);
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.printf("[nav-timing] ui_dock_create: %lums, TOTAL screen_init: %lums\n",
                  (unsigned long)(millis() - tDock0), (unsigned long)(millis() - t0));
#endif

    lv_obj_add_event_cb(ui_navScreen, ui_event_navScreen, LV_EVENT_ALL, NULL);
}

void ui_navScreen_refresh_theme(void)
{
    if (ui_navScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_navScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navTopBar) lv_obj_set_style_bg_color(ui_navTopBar, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navCanvas) lv_obj_set_style_bg_color(ui_navCanvas, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navTrailLine) lv_obj_set_style_line_color(ui_navTrailLine, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navHereDot) {
        // Both fill and border are ui_theme_accent() now (see the create-
        // time comment) - this used to only re-touch the border, leaving
        // the fill stale after a day/night toggle until the next screen
        // rebuild. Fixed alongside the item-9 white->accent fill change,
        // same underlying oversight.
        lv_obj_set_style_bg_color(ui_navHereDot, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(ui_navHereDot, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_navScaleLabel) lv_obj_set_style_text_color(ui_navScaleLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navTurnBanner) {
        lv_obj_set_style_bg_color(ui_navTurnBanner, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(ui_navTurnBanner, ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui_navTurnIconLabel) lv_obj_set_style_text_color(ui_navTurnIconLabel, ui_theme_accent(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_navStrip) lv_obj_set_style_bg_color(ui_navStrip, ui_theme_panel_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    for (int i = 0; i < NAV_TOPOLOGY_SCREEN_COUNT; i++) {
        if (!ui_navDots[i]) continue;
        bool isThis = (i == NAV_TOPOLOGY_THIS_INDEX);
        lv_obj_set_style_bg_color(ui_navDots[i], isThis ? ui_theme_accent() : ui_theme_panel_border(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    ui_dock_refresh_theme(ui_navScreenDock, UI_DOCK_GPS);
    // Status/speed/heading/turn-dist/ETA/distance-remaining label text/
    // colors are data-driven and left alone here - they self-correct on
    // their next natural data update, same reasoning as every other
    // screen in this codebase.
}

void ui_navScreen_screen_destroy(void)
{
    if (ui_navScreen) lv_obj_del(ui_navScreen);  // recursively deletes ui_navTileLayer,
                                                   // the raster layer's lv_img, the turn
                                                   // banner, and every feature object
                                                   // under them too

    // The raster layer's backing sprite lives in nav_map_render.cpp's own
    // static storage (PSRAM), not owned by LVGL - lv_obj_del() above just
    // deleted the lv_img object that pointed at it. Free the sprite
    // itself here, same "explicit teardown, not left to next init()"
    // discipline as every other driver in this project.
    nav_map_render_deinit();
    router.unload();

    ui_navScreen = NULL;
    ui_navTopBar = NULL;
    ui_navClockLabel = NULL;
    ui_navSocLabel = NULL;
    ui_navCanvas = NULL;
    ui_navTileLayer = NULL;
    ui_navTrailLine = NULL;
    ui_navHereDot = NULL;
    ui_navScaleLabel = NULL;
    ui_navTurnBanner = NULL;
    ui_navTurnIconLabel = NULL;
    ui_navTurnDistLabel = NULL;
    ui_navStrip = NULL;
    ui_navStatusLabel = NULL;
    ui_navSpeedLabel = NULL;
    ui_navHeadingLabel = NULL;
    ui_navEtaLabel = NULL;
    ui_navDistRemainingLabel = NULL;
    ui_navScreenDock = NULL;
    for (int i = 0; i < NAV_TOPOLOGY_SCREEN_COUNT; i++) ui_navDots[i] = NULL;
    trailCount = 0;
    trailHead = 0;

    // Feature objects were just deleted along with ui_navTileLayer above -
    // drop the now-dangling pointers and force a fresh nav_tile_load() +
    // rebuild next time the screen is opened, even at the same position.
    // (gridTiles/gridFeatureObjs themselves are NOT freed here - see
    // allocateNavBuffers()'s comment on why they're kept for the
    // firmware's lifetime instead of torn down per screen visit.)
    for (int s = 0; s < NAV_GRID_TILE_COUNT; s++) {
        if (!gridFeatureObjs[s]) continue;
        for (int i = 0; i < NAV_MAX_FEATURES_PER_TILE; i++) gridFeatureObjs[s][i] = NULL;
    }
    gridLoaded = false;

    // Drop any route - the Home button on next open starts fresh, same
    // "don't carry stale nav state across screen visits" reasoning as the
    // trail/tile state cleared above.
    navRoute.clear();
    navTurns.clear();
    navState = NavState{};
    routeComputePending = false;
    routeComputing = false;
}
