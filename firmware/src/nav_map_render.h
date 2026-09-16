#pragma once
// nav_map_render.h - trimmed C++ port of jgauchia/IceNav-v3's
// lib/maps/src/maps.cpp raster drawing primitives (pinned tag v.0.2.9,
// commit d1819b771e12394e185cdf18e8a14875b0998912), adapted from
// TFT_eSPI's TFT_eSprite to this project's LovyanGFX LGFX_Sprite (see
// nav_map_render.cpp's header comment for the color-depth deviation this
// required - easy to get backwards, read it before touching the fill
// path), and trimmed from IceNav's multi-tile pannable/rotatable viewport
// down to this project's single fixed-region map area (CANVAS_W x
// CANVAS_H in ui_navScreen.c, recentered every GPS fix rather than
// panned/zoomed by the user).
//
// Ported: fillPolygonGeneral() (scanline Active-Edge-List fill with
// ring/hole support - the actual filled-polygon capability this whole
// port exists to get, versus the outline-only lv_line approach it
// replaces).
//
// Deliberately NOT ported, with reasons (flagging per the requirements
// doc's "match this structure unless there's a good technical reason to
// deviate"):
// - drawThickLine() (parallel-Bresenham-offset thick line, maps.cpp
//   ~line 1502): IceNav wrote this because TFT_eSPI's own thick-line
//   drawing wasn't good enough for its needs. LovyanGFX ships a native
//   anti-aliased equivalent, drawWideLine() (a proper wedge/capsule
//   rasterizer, not a parallel-lines hack) - using it directly avoids
//   porting a workaround for a limitation this project's graphics
//   library doesn't have.
// - lv_async_call()-wrapped redraw (mainScr.cpp's pattern, cited in the
//   original plan): IceNav needs it because its map render runs on a
//   separate FreeRTOS task (mapRenderTask) it creates itself. This
//   project has no such task - ui_navScreen_addPoint() (which calls into
//   this module) already only ever runs inside firmware.ino/
//   zombie_updaters.cpp's existing uiMutex-guarded block, the same
//   "direct LVGL calls under uiMutex" convention every other screen in
//   this codebase uses. Wrapping in lv_async_call() on top of that would
//   be redundant, not safer.
//
// Point and text NavFeature kinds are NOT rendered here - they stay LVGL
// objects overlaid on top of this module's lv_img (ui_navScreen.c's
// existing buildTileLayer()/repositionTileLayer() already handles them
// fine); only LINESTRING/POLYGON move to this raster path.

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Allocates the backing sprite (PSRAM) at (width x height) and an lv_img
// object under `parent` sourced from it, positioned at (0,0). Call once
// per screen construction (mirrors ui_navScreen.c's other one-time
// per-screen setup in ui_navScreen_screen_init()). Returns the lv_img
// object so the caller can order it relative to other layers (z-order,
// lv_obj_move_foreground/background) exactly as it already does for
// ui_navTileLayer.
lv_obj_t * nav_map_render_init(lv_obj_t * parent, int16_t width, int16_t height);

// Releases the sprite - call from screen teardown (ui_navScreen_screen_destroy())
// alongside the lv_obj_del() that already deletes the lv_img object itself.
void nav_map_render_deinit(void);

// Clears the sprite to a flat color (RGB565) - this module has no
// per-pixel alpha (LV_IMG_CF_TRUE_COLOR, not _ALPHA - not worth doubling
// this buffer's memory for two feature kinds that don't need it), so the
// caller passes the canvas panel's current background color to match
// what the outline-only lv_line layer's transparent background already
// showed through. Call once per redraw before any polygon/line draws.
void nav_map_render_begin_frame(uint16_t bgColor565);

// px/py: screen-space vertex arrays, already projected and centered by
// the caller (ui_navScreen.c's repositionTileLayer(), same flat-earth-
// meters projection the breadcrumb trail uses - this module has no
// lat/lon or projection knowledge of its own). ringCount==0 means "one
// ring, no holes" (matches upstream fillPolygonGeneral's own
// convention); ringEnds[r] is the cumulative vertex index ending ring r,
// same encoding as NavFeature.ringEnds in nav_reader.h. Pass
// drawCasing=true (from NavFeature.isCasingOrSpecial - the building/
// bridge flag for polygons) to also stroke each ring's edges in
// casingColor after the fill, matching upstream's z>=16 building-outline
// behavior (this project is fixed at NAV_TILE_ZOOM 16, so that zoom gate
// is always satisfied here).
void nav_map_render_polygon(const int *px, const int *py, int numPoints,
                             uint16_t color, uint16_t ringCount, const uint16_t *ringEnds,
                             bool drawCasing, uint16_t casingColor);

// Draws a connected polyline through px/py (already projected/centered,
// same convention as nav_map_render_polygon()). Pass drawCasing=true
// (from NavFeature.isCasingOrSpecial - the line-casing flag) to first
// stroke a wider casingColor pass underneath, matching upstream's
// road-casing look (a darker outline behind the fill color - see
// nav_map_render.cpp for how casingColor is derived).
void nav_map_render_line(const int *px, const int *py, int numPoints,
                          uint8_t widthPx, uint16_t color,
                          bool drawCasing, uint16_t casingColor, uint8_t casingWidthPx);

// Pushes the finished frame to the screen by invalidating the lv_img -
// call once after all of a frame's polygon/line draws. Safe to call
// directly (no lv_async_call) - see this header's comment above on why.
void nav_map_render_end_frame(void);

// RGB565 -> darkened RGB565, same simple multiply-by-factor darkening
// IceNav's Maps::darkenRGB565() uses for casing/outline colors. Exposed
// so ui_navScreen.c can derive a NavFeature's casingColor from its
// colorRgb565 without duplicating this math.
uint16_t nav_map_render_darken(uint16_t color, float amount);

#ifdef __cplusplus
}
#endif
