// nav_map_render.cpp - see nav_map_render.h for this port's scope and
// deliberate deviations from jgauchia/IceNav-v3's lib/maps/src/maps.cpp
// (pinned tag v.0.2.9, commit d1819b771e12394e185cdf18e8a14875b0998912).
//
// COLOR-DEPTH DEVIATION (read before touching fillPolygonGeneral() below -
// easy to silently get backwards): upstream's fillPolygonGeneral()
// byte-swaps its direct-buffer fast path
// (`rawColor = (color >> 8) | (color << 8)`) because TFT_eSPI sprites
// default to a byte-swapped RGB565 buffer layout (the wire format most
// SPI TFT panels want). LovyanGFX's LGFX_Sprite defaults to that exact
// same swapped layout too (color_depth_t::rgb565_2Byte) when you call the
// plain setColorDepth(16) overload - confirmed by reading
// LGFX_Sprite.hpp/colortype.hpp, `rgb565_2Byte` converts via `swap565_t`.
// But this project's LVGL is built with LV_COLOR_16_SWAP 0
// (firmware/include/lv_conf.h) - host-native RGB565 - and this sprite's
// buffer is wrapped DIRECTLY as an lv_img_dsc_t's data pointer with no
// panel-driver step in between to un-swap it (see nav_map_render_init()).
// So this sprite is explicitly created with the OTHER 16bpp depth,
// lgfx::rgb565_nonswapped, and the byte-swap is DROPPED from the fill
// path below - keeping it would silently swap the red/blue channels of
// every filled polygon on real hardware.
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
    if ((int)edgeBuckets.size() < bucketCount) edgeBuckets.resize(bucketCount, -1);
    else std::fill(edgeBuckets.begin(), edgeBuckets.begin() + bucketCount, -1);

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
                    while (row < rowEnd) *row++ = color;  // no byte-swap - see file header
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
    mapSprite.setColorDepth(lgfx::rgb565_nonswapped);
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

    // drawWideLine's r is a per-side radius (line is 2r+1 px wide) - see
    // this file's header comment for why LovyanGFX's native wedge-line is
    // used here instead of porting upstream's drawThickLine().
    float r = widthPx / 2.0f;
    if (drawCasing) {
        float rCasing = casingWidthPx / 2.0f;
        for (int i = 1; i < numPoints; i++)
            mapSprite.drawWideLine(px[i - 1], py[i - 1], px[i], py[i], rCasing, casingColor);
    }
    for (int i = 1; i < numPoints; i++)
        mapSprite.drawWideLine(px[i - 1], py[i - 1], px[i], py[i], r, color);
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
