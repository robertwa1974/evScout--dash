// nav_map_render.cpp - see nav_map_render.h for this port's scope and
// deliberate deviations from jgauchia/IceNav-v3's lib/maps/src/maps.cpp
// (pinned tag v.0.2.9, commit d1819b771e12394e185cdf18e8a14875b0998912).
//
// COLOR-DEPTH NOTE: this sprite is created with lgfx::rgb565_nonswapped
// (not upstream's plain setColorDepth(16), which resolves to LovyanGFX's
// default color_depth_t::rgb565_2Byte) so that a plain R:15-11/G:10-5/
// B:4-0 uint16_t (this project's NavFeature.colorRgb565 convention
// everywhere else, and what this project's LVGL build expects too -
// LV_COLOR_16_SWAP 0, firmware/include/lv_conf.h) can be written directly
// into fillPolygonGeneral's direct-buffer-pointer fast path below with no
// byte-swap, and into every other draw call in this file (fillScreen(),
// drawLine(), drawWideLine(), drawFastHLine()) the normal way - confirmed
// on real hardware (2026-09-16) by writing a known raw value directly
// through the fast path and photographing the result.
//
// UNRESOLVED (2026-09-16, investigation paused, not abandoned): a real
// tile's large background/landcover polygon was expected to render as
// near-white and instead showed as blue/cyan on real hardware. A byte-
// swap theory was tried and seemed to fit one piece of evidence, but a
// follow-up hardware test (writing a known raw color with NO swap)
// contradicted it - the color pipeline itself may be entirely correct,
// and the visible blue/cyan may simply be a DIFFERENT, legitimately
// blue/cyan feature (this tile has 298 features total) drawn on top of
// the near-white one, not a bug at all. Don't re-attempt a byte-swap fix
// here without first re-checking the full decoded feature list for the
// actual tile in view. See the separately fixed performance issue below
// (nav_map_render_line's PERFORMANCE comment) - that one IS confirmed
// fixed on real hardware, unrelated to this open color question.
#include "nav_map_render.h"
#include <LovyanGFX.hpp>
#include <vector>
#include <algorithm>
#include <climits>
#include "psram_allocator.h"

namespace {

lgfx::LGFX_Sprite mapSprite;
lv_obj_t * mapImg = nullptr;
lv_img_dsc_t mapImgDsc;
int canvasW = 0, canvasH = 0;

// Edge/edgePool/edgeBuckets mirror Maps' own member variables (maps.hpp) -
// reused across calls (not reallocated per polygon) for the same reason
// upstream keeps them as class members rather than locals.
struct Edge {
    int32_t yMax;
    int32_t xVal;   // 16.16 fixed point
    int32_t slope;  // 16.16 fixed point
    int nextInBucket;
    int nextActive;
};

std::vector<Edge, PsramAllocator<Edge>> edgePool;
std::vector<int, PsramAllocator<int>> edgeBuckets;

// Ported from Maps::fillPolygonGeneral (maps.cpp ~line 1279). xOffset/
// yOffset dropped from the signature (always 0 here - upstream used them
// for its multi-tile viewport compositing, which this project's single
// fixed-region canvas doesn't have); tileWidth/tileHeight become
// canvasW/canvasH.
void fillPolygonGeneral(const int *px, const int *py, int numPoints, uint16_t color,
                         uint16_t ringCount, const uint16_t *ringEnds) {
    if (numPoints < 3) return;

    uint16_t *buf = static_cast<uint16_t*>(mapSprite.getBuffer());
    uint32_t stride = 0;
    if (buf) stride = mapSprite.bufferLength() / ((uint32_t)canvasH * 2);

    int minY = INT_MAX, maxY = INT_MIN;
    for (int i = 0; i < numPoints; i++) {
        if (py[i] < minY) minY = py[i];
        if (py[i] > maxY) maxY = py[i];
    }
    if (maxY < 0 || minY >= canvasH) return;

    edgePool.clear();
    int clampedMaxY = std::min(maxY, canvasH - 1);
    int bucketCount = clampedMaxY - minY + 1;
    if (bucketCount <= 0) return;
    // ROOT CAUSE (found 2026-09-21, after days of investigation - see the
    // grid-freeze history in ui_navScreen.cpp's NAV_GRID_TILES_X/Y comment
    // for everything this was mistaken for first): edgeBuckets is reused
    // across every polygon in every tile, growing only when a polygon
    // needs more buckets than any previous one seen so far. The old code
    // only initialized buckets to -1 on the ELSE branch (bucketCount fits
    // in the existing size) - on the GROWTH branch, resize(n, -1) only
    // value-initializes the NEWLY added elements; it leaves every
    // pre-existing bucket slot holding whatever edgePool index it held
    // after the PREVIOUS call. Since edgePool.clear() resets the pool to
    // size 0 every call, those stale indices point past the current
    // edgePool's live range - reading edgePool[staleIdx] pulls
    // uninitialized bytes out of the vector's unused capacity as a fake
    // Edge, and that garbage nextInBucket/nextActive/yMax can chain into a
    // cycle that spins the scanline walk below forever (no crash, since
    // the memory itself is validly-mapped PSRAM - it just never
    // terminates). This is why the freeze looked content/order/address/
    // timing-independent: it strikes whichever polygon is the first, in
    // cumulative processing order across the whole grid, to set a new
    // maximum bucket count - not any particular slot, tile, or feature.
    // Fix: always clear the full [0, bucketCount) range actually used this
    // call, whether or not a resize happened.
    if ((int)edgeBuckets.size() < bucketCount) edgeBuckets.resize(bucketCount);
    std::fill(edgeBuckets.begin(), edgeBuckets.begin() + bucketCount, -1);

    uint16_t count = (ringCount == 0) ? 1 : ringCount;
    uint16_t defaultEnds[1] = { (uint16_t)numPoints };
    const uint16_t *ends = (ringEnds == nullptr) ? defaultEnds : ringEnds;
    int ringStart = 0;

    for (uint16_t r = 0; r < count; r++) {
        int ringEnd = ends[r];
        if (ringEnd > numPoints) ringEnd = numPoints;
        int ringNumPoints = ringEnd - ringStart;
        if (ringNumPoints < 3) { ringStart = ringEnd; continue; }
        for (int i = 0; i < ringNumPoints; i++) {
            int next = (i + 1) % ringNumPoints;
            int x1 = px[ringStart + i], y1 = py[ringStart + i];
            int x2 = px[ringStart + next], y2 = py[ringStart + next];
            if (y1 == y2) continue;
            int startBucketY = (y1 < y2) ? y1 : y2;
            if (startBucketY > clampedMaxY) continue;
            Edge e;
            e.nextActive = -1;
            if (y1 < y2) {
                e.yMax = y2;
                e.xVal = x1 << 16;
                e.slope = ((x2 - x1) << 16) / (y2 - y1);
                e.nextInBucket = edgeBuckets[y1 - minY];
                edgePool.push_back(e);
                edgeBuckets[y1 - minY] = (int)edgePool.size() - 1;
            } else {
                e.yMax = y1;
                e.xVal = x2 << 16;
                e.slope = ((x1 - x2) << 16) / (y1 - y2);
                e.nextInBucket = edgeBuckets[y2 - minY];
                edgePool.push_back(e);
                edgeBuckets[y2 - minY] = (int)edgePool.size() - 1;
            }
        }
        ringStart = ringEnd;
    }

    int activeHead = -1;
    int startY = std::max(minY, 0);
    int endY = std::min(maxY, canvasH - 1);

    for (int y = startY; y <= endY; y++) {
        int eIdx = edgeBuckets[y - minY];
        while (eIdx != -1) {
            int nextIdx = edgePool[eIdx].nextInBucket;
            edgePool[eIdx].nextActive = activeHead;
            activeHead = eIdx;
            eIdx = nextIdx;
        }
        int *pCurrIdx = &activeHead;
        while (*pCurrIdx != -1) {
            if (edgePool[*pCurrIdx].yMax <= y) *pCurrIdx = edgePool[*pCurrIdx].nextActive;
            else pCurrIdx = &(edgePool[*pCurrIdx].nextActive);
        }
        if (activeHead == -1) continue;

        int sorted = -1;
        int active = activeHead;
        while (active != -1) {
            int nextActive = edgePool[active].nextActive;
            if (sorted == -1 || edgePool[active].xVal < edgePool[sorted].xVal) {
                edgePool[active].nextActive = sorted;
                sorted = active;
            } else {
                int s = sorted;
                while (edgePool[s].nextActive != -1 && edgePool[edgePool[s].nextActive].xVal < edgePool[active].xVal)
                    s = edgePool[s].nextActive;
                edgePool[active].nextActive = edgePool[s].nextActive;
                edgePool[s].nextActive = active;
            }
            active = nextActive;
        }
        activeHead = sorted;

        int left = activeHead;
        while (left != -1 && edgePool[left].nextActive != -1) {
            int right = edgePool[left].nextActive;
            int xStart = edgePool[left].xVal >> 16;
            int xEnd = edgePool[right].xVal >> 16;
            if (xStart < 0) xStart = 0;
            if (xEnd > canvasW) xEnd = canvasW;
            if (xEnd > xStart) {
                if (buf) {
                    uint16_t *row = buf + (uint32_t)y * stride + xStart;
                    uint16_t *rowEnd = row + (xEnd - xStart);
                    while (row < rowEnd) *row++ = color;
                } else {
                    mapSprite.drawFastHLine(xStart, y, xEnd - xStart, color);
                }
            }
            left = edgePool[right].nextActive;
        }
        for (int a = activeHead; a != -1; a = edgePool[a].nextActive)
            edgePool[a].xVal += edgePool[a].slope;
    }
}

} // namespace

lv_obj_t * nav_map_render_init(lv_obj_t * parent, int16_t width, int16_t height) {
    canvasW = width;
    canvasH = height;

    mapSprite.setPsram(true);
    mapSprite.setColorDepth(lgfx::rgb565_nonswapped);  // see file header comment
    mapSprite.createSprite(width, height);
    edgeBuckets.reserve(height);

    mapImgDsc.header.always_zero = 0;
    mapImgDsc.header.reserved = 0;
    mapImgDsc.header.w = width;
    mapImgDsc.header.h = height;
    mapImgDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    mapImgDsc.data_size = (uint32_t)width * (uint32_t)height * 2;
    mapImgDsc.data = (const uint8_t *)mapSprite.getBuffer();

    mapImg = lv_img_create(parent);
    lv_img_set_src(mapImg, &mapImgDsc);
    lv_obj_set_pos(mapImg, 0, 0);
    lv_obj_clear_flag(mapImg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return mapImg;
}

void nav_map_render_deinit(void) {
    mapSprite.deleteSprite();
    mapImg = nullptr;
    canvasW = 0;
    canvasH = 0;
}

void nav_map_render_begin_frame(uint16_t bgColor565) {
    mapSprite.fillScreen(bgColor565);
}

void nav_map_render_polygon(const int *px, const int *py, int numPoints,
                             uint16_t color, uint16_t ringCount, const uint16_t *ringEnds,
                             bool drawCasing, uint16_t casingColor) {
    fillPolygonGeneral(px, py, numPoints, color, ringCount, ringEnds);
    if (!drawCasing) return;

    // Ring-edge outline pass, ported from renderNavPolygon's casing block
    // (maps.cpp ~line 1713) - always applied here rather than gated on
    // navLastZoom_ >= 16 upstream, since this project is fixed at
    // NAV_TILE_ZOOM 16 (nav_reader.h) so that gate is always true anyway.
    uint16_t numRings = (ringCount > 0) ? ringCount : 1;
    uint16_t defaultEnds[1] = { (uint16_t)numPoints };
    const uint16_t *ends = (ringEnds == nullptr) ? defaultEnds : ringEnds;
    int ringStart = 0;
    for (uint16_t r = 0; r < numRings; r++) {
        int ringEnd = ends[r];
        if (ringEnd > numPoints) ringEnd = numPoints;
        for (int j = ringStart; j < ringEnd; j++) {
            int next = (j + 1 < ringEnd) ? (j + 1) : ringStart;
            mapSprite.drawLine(px[j], py[j], px[next], py[next], casingColor);
        }
        ringStart = ringEnd;
    }
}

void nav_map_render_line(const int *px, const int *py, int numPoints,
                          uint8_t widthPx, uint16_t color,
                          bool drawCasing, uint16_t casingColor, uint8_t casingWidthPx) {
    if (numPoints < 2) return;

    // PERFORMANCE (2026-09-16, escalated to a hard disable 2026-09-20):
    // drawWideLine() is an anti-aliased wedge rasterizer - real per-call
    // cost on this hardware, and a real tile can have on the order of a
    // few hundred LINESTRING segments (casing doubles that). The original
    // 2026-09-16 fix only routed THIN lines through cheap plain
    // drawLine() and kept calling drawWideLine() for anything wider than
    // WIDE_LINE_THRESHOLD_PX, on the theory that wide (major-road) lines
    // are rare enough per tile to afford it. Real hardware disproved that
    // 2026-09-20: a real California tile (11423,26384, 233 features) had
    // exactly ONE feature with widthPx above this threshold - the LAST
    // one in the array (index 232) - and hitting it even once froze the
    // board hard enough to trip the watchdog and reboot (confirmed via
    // per-feature Serial tracing: the ">>> LINE i=232 ... widthPx=5"
    // entry printed, its matching "<<< done" never did, "Setup starting"
    // followed immediately). One occurrence per tile is enough to make
    // this unusable, not just slow - drawWideLine() is now never called
    // at all, full stop, in favor of plain drawLine() (cheap Bresenham,
    // no AA, no width) unconditionally. Trade-off: wide/major roads no
    // longer render visually thicker than residential streets - real,
    // but preferable to an unusable/crashing map. Revisiting this means
    // either profiling why drawWideLine() is this expensive on this
    // hardware/LovyanGFX version specifically, or implementing thickness
    // a cheaper way (e.g. a few parallel offset drawLine() calls) rather
    // than re-enabling drawWideLine() itself.
    (void)widthPx;      // no longer used to select a draw path - kept as a
    (void)casingWidthPx; // parameter for signature/caller compatibility
    if (drawCasing) {
        // Casing first (underneath), same order as the original
        // drawWideLine()-based version - it's meant as a border the main
        // color then draws on top of, not a cover-up.
        for (int i = 1; i < numPoints; i++)
            mapSprite.drawLine(px[i - 1], py[i - 1], px[i], py[i], casingColor);
    }
    for (int i = 1; i < numPoints; i++)
        mapSprite.drawLine(px[i - 1], py[i - 1], px[i], py[i], color);
}

void nav_map_render_end_frame(void) {
    if (!mapImg) return;
    lv_obj_invalidate(mapImg);
}

// Ported from Maps::darkenRGB565 (maps.cpp ~line 1238), minus its
// single-entry memo cache (upstream added that as a micro-optimization
// for its own much higher call volume rendering a multi-tile viewport
// every frame; this project's per-frame call count is small enough - at
// most NAV_MAX_FEATURES_PER_TILE - that it isn't worth the added state).
uint16_t nav_map_render_darken(uint16_t color, float amount) {
    uint16_t factor = (uint16_t)((1.0f - amount) * 256.0f);
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    r = (uint8_t)((r * factor) >> 8);
    g = (uint8_t)((g * factor) >> 8);
    b = (uint8_t)((b * factor) >> 8);
    return (uint16_t)((r << 11) | (g << 5) | b);
}
