// nav_reader.cpp (2026-09-15) - replaces the old nav_tile_reader.cpp's
// hand-rolled "open file, seek to index, read one entry" logic with a
// ported jgauchia/IceNav-v3 NavReader class (pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912 - lib/maps/src/nav_reader.hpp/
// .cpp), per the requirements doc directing this project to reuse
// IceNav-v3's actual code rather than keep extending from-scratch
// renderer/reader code. Class name and its band-caching algorithm are
// kept as close to the original as practical, for traceability - see
// what changed and why, below.
//
// WHAT CHANGED FROM THE ORIGINAL NavReader, AND WHY:
// - Original opens one flat "/sdcard/NAVMAP/Z{zoom}.nav" file per zoom
//   and mounts SD via ESP-IDF's native esp_vfs_fat_sdspi_mount (real GPIO
//   chip-select). This board's SD_CS is EXIO4 on the CH422G I/O expander,
//   not a real GPIO (see sd_driver.h) - whether that native driver can be
//   configured for an always-asserted, externally-managed CS the way
//   Arduino's SD.h supports is unverified, so this project keeps its own
//   already-hardware-proven sd_driver.h/Arduino SD.h access layer
//   instead of porting storage.hpp/storage.cpp. Every storage.xxx() call
//   below is Arduino SD.h's File API instead.
// - This project's data is split into regional files
//   (/maps/Z{zoom}_r{row}_c{col}.nav, NAV_REGION_TILES tiles/file - see
//   nav_tile_format.h) rather than one huge file per zoom, because the
//   Arduino SD library's seek() measured roughly O(distance) on a single
//   large file on real hardware (a seek ~660MB into a 1.3GB file took
//   9+ seconds and tripped the ESP32 task watchdog - see CLAUDE.md's "Map
//   tile format" section for the full story). openPack() takes
//   (zoom, tileX, tileY) instead of just zoom, and additionally tracks
//   which region (row, col) is currently open. The band-caching itself
//   (loadBand()/findTileInPack()) is otherwise the same algorithm as
//   upstream - with files this small (NAV_REGION_TILES=64 means at most
//   64*64*8 = 32KB of index, versus the 512KB band size) the whole
//   region's index typically loads in a single band and is served from
//   RAM for every subsequent lookup in that region, for free.
// - Feature *decoding* (turning a tile's raw NAV1 bytes into NavFeature
//   structs) isn't part of upstream's NavReader class at all - IceNav-v3
//   does that separately in maps.cpp's navDecodeFeatures(), tightly
//   coupled to its own rendering/feature-pool pipeline. This project
//   keeps its own existing, already-hardware-verified decode
//   implementation (decodeFeature() and friends below, unchanged from
//   the old nav_tile_reader.cpp) rather than porting that pipeline too -
//   NavReader here is purely the pack-open/index-lookup optimization.
#include "nav_reader.h"
#include "sd_driver.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <math.h>
#include "esp_heap_caps.h"

// --- Ported NavReader class (pack/index access + band-cached index) ---
class NavReader {
public:
    static bool openPack(uint8_t zoom, uint32_t tileX, uint32_t tileY);
    static void closePack();
    static bool findTileInPack(uint32_t tileX, uint32_t tileY, uint32_t &offset, uint32_t &size);

    static inline uint16_t paletteColor(uint8_t index) {
        return index < paletteCount ? colorPalette[index] : 0;
    }

    static File packFile;

private:
    static bool loadBand(uint32_t yOff);
    static void freeBand();

    static bool packOpen;
    static uint8_t currentZoom;
    static uint32_t currentRow, currentCol;  // which region file is open (not in
                                               // upstream - see file header comment)
    static uint32_t tilesWide, tilesHigh;
    static uint32_t minX, minY;

    static NavIndexEntry *bandBuffer;
    static uint32_t bandStartRow, bandRows;

    static uint16_t *colorPalette;
    static uint16_t paletteCount;

    static constexpr uint32_t NAV_INDEX_BAND_BYTES = 512u * 1024u;
};

File     NavReader::packFile     = File();
bool     NavReader::packOpen     = false;
uint8_t  NavReader::currentZoom  = 0;
uint32_t NavReader::currentRow   = 0;
uint32_t NavReader::currentCol   = 0;
uint32_t NavReader::tilesWide    = 0;
uint32_t NavReader::tilesHigh    = 0;
uint32_t NavReader::minX         = 0;
uint32_t NavReader::minY         = 0;

NavIndexEntry *NavReader::bandBuffer   = nullptr;
uint32_t       NavReader::bandStartRow = 0;
uint32_t       NavReader::bandRows     = 0;

uint16_t *NavReader::colorPalette = nullptr;
uint16_t  NavReader::paletteCount = 0;

bool NavReader::openPack(uint8_t zoom, uint32_t tileX, uint32_t tileY) {
    uint32_t row = tileY / NAV_REGION_TILES;
    uint32_t col = tileX / NAV_REGION_TILES;

    if (packOpen && currentZoom == zoom && currentRow == row && currentCol == col) return true;

    closePack();

    char path[48];
    snprintf(path, sizeof(path), "/maps/Z%u_r%u_c%u.nav", (unsigned)zoom, row, col);
    packFile = SD.open(path, FILE_READ);
    if (!packFile) return false;

    NavMapHeader mh;
    if ((size_t)packFile.read((uint8_t *)&mh, sizeof(mh)) != sizeof(mh) ||
        memcmp(mh.magic, NAV_MAP_MAGIC, 4) != 0) {
        closePack();
        return false;
    }

    tilesWide = mh.tilesWide;
    tilesHigh = mh.tilesHigh;
    minX = mh.bottomLeftX;
    minY = mh.bottomLeftY;

    if (mh.colorCount > 0) {
        uint32_t paletteOff = sizeof(NavMapHeader) + (uint32_t)tilesWide * tilesHigh * sizeof(NavIndexEntry);
        size_t paletteBytes = mh.colorCount * sizeof(uint16_t);
        colorPalette = static_cast<uint16_t *>(heap_caps_malloc(paletteBytes, MALLOC_CAP_SPIRAM));
        if (!colorPalette) colorPalette = static_cast<uint16_t *>(heap_caps_malloc(paletteBytes, MALLOC_CAP_INTERNAL));
        if (!colorPalette || !packFile.seek(paletteOff) ||
            (size_t)packFile.read((uint8_t *)colorPalette, paletteBytes) != paletteBytes) {
            closePack();
            return false;
        }
        paletteCount = mh.colorCount;
    }

    currentZoom = zoom;
    currentRow = row;
    currentCol = col;
    packOpen = true;
    return true;
}

void NavReader::closePack() {
    if (packOpen) {
        packFile.close();
        packOpen = false;
    }
    freeBand();
    if (colorPalette) {
        heap_caps_free(colorPalette);
        colorPalette = nullptr;
    }
    paletteCount = 0;
    currentZoom = 0;
    currentRow = 0;
    currentCol = 0;
    tilesWide = 0;
    tilesHigh = 0;
    minX = 0;
    minY = 0;
}

void NavReader::freeBand() {
    if (bandBuffer) {
        heap_caps_free(bandBuffer);
        bandBuffer = nullptr;
    }
    bandStartRow = 0;
    bandRows = 0;
}

bool NavReader::loadBand(uint32_t yOff) {
    uint32_t rowBytes = tilesWide * sizeof(NavIndexEntry);
    if (rowBytes == 0) return false;

    uint32_t maxRows = NAV_INDEX_BAND_BYTES / rowBytes;
    if (maxRows == 0) maxRows = 1;
    if (maxRows > tilesHigh) maxRows = tilesHigh;

    if (!bandBuffer) {
        bandBuffer = static_cast<NavIndexEntry *>(heap_caps_malloc(maxRows * rowBytes, MALLOC_CAP_SPIRAM));
        if (!bandBuffer) bandBuffer = static_cast<NavIndexEntry *>(heap_caps_malloc(maxRows * rowBytes, MALLOC_CAP_INTERNAL));
        if (!bandBuffer) return false;
    }

    uint32_t startRow = (yOff < maxRows / 2) ? 0 : yOff - maxRows / 2;
    if (startRow + maxRows > tilesHigh) startRow = tilesHigh - maxRows;

    uint32_t entryPos = sizeof(NavMapHeader) + startRow * tilesWide * sizeof(NavIndexEntry);
    if (!packFile.seek(entryPos) ||
        (size_t)packFile.read((uint8_t *)bandBuffer, maxRows * rowBytes) != maxRows * rowBytes) {
        freeBand();
        return false;
    }

    bandStartRow = startRow;
    bandRows = maxRows;
    return true;
}

bool NavReader::findTileInPack(uint32_t tileX, uint32_t tileY, uint32_t &offset, uint32_t &size) {
    if (!packOpen || tilesWide == 0 || tilesHigh == 0) return false;
    if (tileX < minX || tileY < minY) return false;

    uint32_t xOff = tileX - minX;
    uint32_t yOff = tileY - minY;
    if (xOff >= tilesWide || yOff >= tilesHigh) return false;

    bool inBand = bandBuffer && yOff >= bandStartRow && yOff < bandStartRow + bandRows;
    if (!inBand) inBand = loadBand(yOff) && yOff >= bandStartRow && yOff < bandStartRow + bandRows;

    NavIndexEntry entry;
    if (inBand) {
        entry = bandBuffer[(yOff - bandStartRow) * tilesWide + xOff];
    } else {
        uint32_t entryPos = sizeof(NavMapHeader) + (yOff * tilesWide + xOff) * sizeof(NavIndexEntry);
        if (!packFile.seek(entryPos)) return false;
        if ((size_t)packFile.read((uint8_t *)&entry, sizeof(entry)) != sizeof(entry)) return false;
    }

    if (entry.size == 0) return false;
    offset = entry.offset;
    size = entry.size;
    return true;
}

// --- Feature decoding (unchanged from the old nav_tile_reader.cpp - see
// nav_tile_format.h for the byte layout this parses) ---

static bool readVarint(File &f, uint64_t &out) {
    out = 0;
    for (int i = 0; i < 10; i++) {
        int b = f.read();
        if (b < 0) return false;
        out |= (uint64_t)(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) return true;
    }
    return false;  // malformed - more than 10 bytes, shouldn't happen for real data
}

static int64_t zigzagDecode(uint64_t n) {
    return (int64_t)(n >> 1) ^ -(int64_t)(n & 1);
}

static bool readU16LE(File &f, uint16_t &out) {
    uint8_t b[2];
    if (f.read(b, 2) != 2) return false;
    out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool readI16LE(File &f, int16_t &out) {
    uint16_t u;
    if (!readU16LE(f, u)) return false;
    out = (int16_t)u;
    return true;
}

// Decodes one feature's payload from the current file position, given its
// already-read NavFeatureHeader and payloadSize. Always advances through
// the FULL declared payload (even data past this feature's caps) so the
// caller's position ends up exactly at payloadEnd - the caller still
// unconditionally seeks there afterward as a belt-and-suspenders resync,
// but this keeps the normal path correct without relying on that.
static void decodeFeature(File &f, const NavFeatureHeader &fh, uint64_t coordCountRaw,
                           NavFeature *out, bool *truncated) {
    memset(out, 0, sizeof(*out));
    out->geomType = fh.geomType;
    out->colorRgb565 = NavReader::paletteColor(fh.colorIndex);
    out->minZoom = (fh.zoomPriority >> 4) & 0x0F;
    out->priority = fh.zoomPriority & 0x0F;
    out->isCasingOrSpecial = (fh.widthFlags & 0x80) != 0;
    out->widthPx = fh.widthFlags & 0x7F;
    out->bboxMinX = fh.minX; out->bboxMinY = fh.minY;
    out->bboxMaxX = fh.maxX; out->bboxMaxY = fh.maxY;

    if (fh.geomType == NAV_GEOM_TEXT) {
        // Text payload: int16 x, int16 y, uint8 textLen, textLen bytes,
        // optional 4-byte shield colors, zero-padded to a multiple of 4 -
        // coordCountRaw is a WORD COUNT here, not a vertex count (see
        // nav_tile_format.h's header comment) - not used for decoding,
        // only the framing (payloadSize, tracked by the caller) matters.
        int16_t tx, ty;
        if (!readI16LE(f, tx) || !readI16LE(f, ty)) return;
        int b = f.read();
        if (b < 0) return;
        uint8_t textLen = (uint8_t)b;
        out->textX = tx;
        out->textY = ty;
        out->textLen = textLen;
        uint8_t toStore = textLen < NAV_MAX_TEXT_LEN ? textLen : NAV_MAX_TEXT_LEN;
        if (toStore > 0) {
            if ((size_t)f.read((uint8_t *)out->text, toStore) != toStore) return;
        }
        out->text[toStore] = '\0';
        if (textLen > toStore) {
            f.seek(f.position() + (textLen - toStore));  // skip the rest we didn't store
            *truncated = true;
        }
        // Shield colors, if present, are read by the caller (it knows
        // payloadEnd and can tell whether 4 bytes remain) - see below.
        return;
    }

    // Point/line/polygon: coordCountRaw vertex pairs, delta+zigzag+varint
    // encoded, cumulative from an implicit (0,0) start.
    int32_t x = 0, y = 0;
    uint16_t stored = 0;
    for (uint64_t i = 0; i < coordCountRaw; i++) {
        uint64_t vdx, vdy;
        if (!readVarint(f, vdx) || !readVarint(f, vdy)) return;
        x += (int32_t)zigzagDecode(vdx);
        y += (int32_t)zigzagDecode(vdy);
        if (stored < NAV_MAX_VERTICES_PER_FEATURE) {
            out->vx[stored] = (int16_t)x;
            out->vy[stored] = (int16_t)y;
            stored++;
        } else {
            *truncated = true;
        }
    }
    out->vertexCount = stored;

    if (fh.geomType == NAV_GEOM_POLYGON) {
        uint16_t ringCount;
        if (!readU16LE(f, ringCount)) return;
        out->ringCount = ringCount < NAV_MAX_RINGS_PER_FEATURE ? ringCount : NAV_MAX_RINGS_PER_FEATURE;
        if (ringCount > NAV_MAX_RINGS_PER_FEATURE) *truncated = true;
        for (uint16_t i = 0; i < ringCount; i++) {
            uint16_t end;
            if (!readU16LE(f, end)) return;
            if (i < NAV_MAX_RINGS_PER_FEATURE) out->ringEnds[i] = end;
        }
    }
}

bool nav_tile_load(uint8_t zoom, uint32_t tileX, uint32_t tileY, NavTileData *out) {
    if (!sd_available()) return false;

    memset(out, 0, sizeof(*out));
    out->zoom = zoom;
    out->tileX = tileX;
    out->tileY = tileY;

    if (!NavReader::openPack(zoom, tileX, tileY)) return false;

    uint32_t offset, size;
    if (!NavReader::findTileInPack(tileX, tileY, offset, size)) return false;

    File &f = NavReader::packFile;
    if (!f.seek(offset)) return false;

    NavTileHeader th;
    if ((size_t)f.read((uint8_t *)&th, sizeof(th)) != sizeof(th)) return false;
    if (memcmp(th.magic, NAV_TILE_MAGIC, 4) != 0) return false;

    uint16_t toDecode = th.featureCount < NAV_MAX_FEATURES_PER_TILE ? th.featureCount : NAV_MAX_FEATURES_PER_TILE;
    if (th.featureCount > NAV_MAX_FEATURES_PER_TILE) out->truncated = true;

    // Loop bound is toDecode, NOT th.featureCount - a real dense urban
    // tile can report many hundreds/thousands of features even though we
    // only ever keep the first NAV_MAX_FEATURES_PER_TILE. Continuing to
    // parse-and-skip every feature past that cap wastes many SD reads/
    // seeks per tile for real-world data for no benefit.
    uint16_t decoded = 0;
    for (uint16_t i = 0; i < toDecode; i++) {
        NavFeatureHeader fh;
        if ((size_t)f.read((uint8_t *)&fh, sizeof(fh)) != sizeof(fh)) break;
        uint64_t coordCountRaw, payloadSize;
        if (!readVarint(f, coordCountRaw)) break;
        if (!readVarint(f, payloadSize)) break;

        uint32_t payloadStart = f.position();
        uint32_t payloadEnd = payloadStart + (uint32_t)payloadSize;

        decodeFeature(f, fh, coordCountRaw, &out->features[decoded], &out->truncated);
        if (fh.geomType == NAV_GEOM_TEXT) {
            // Shield colors, if present, sit right after the text
            // bytes and before the zero-padding - only readable here
            // since only the caller knows payloadEnd.
            uint32_t pos = f.position();
            if (payloadEnd - pos >= 4) {
                uint16_t bg, border;
                if (readU16LE(f, bg) && readU16LE(f, border)) {
                    out->features[decoded].shieldBgRgb565 = bg;
                    out->features[decoded].shieldBorderRgb565 = border;
                }
            }
        }
        decoded++;

        // Unconditional resync - correct regardless of how much of the
        // payload decodeFeature() actually consumed.
        f.seek(payloadEnd);
    }
    out->featureCount = decoded;

    return true;
}

// Standard slippy-map formulas (see e.g. the OSM wiki's "Slippy map
// tilenames" page) - Web Mercator, good to well under 1% error at driving
// latitudes, same "simple, sanity-checked, not aerospace-grade" bar as
// this project's flat-earth trail projection.
void nav_latlon_to_tile(uint8_t zoom, double lat, double lon, uint32_t *tileX, uint32_t *tileY) {
    double n = (double)(1u << zoom);
    double latRad = lat * M_PI / 180.0;
    *tileX = (uint32_t)((lon + 180.0) / 360.0 * n);
    *tileY = (uint32_t)((1.0 - asinh(tan(latRad)) / M_PI) / 2.0 * n);
}

void nav_tile_local_to_latlon(uint8_t zoom, uint32_t tileX, uint32_t tileY,
                               int16_t localX, int16_t localY, double *lat, double *lon) {
    double n = (double)(1u << zoom);
    double xFrac = (tileX + (double)localX / NAV_TILE_EXTENT) / n;
    double yFrac = (tileY + (double)localY / NAV_TILE_EXTENT) / n;
    *lon = xFrac * 360.0 - 180.0;
    *lat = atan(sinh(M_PI * (1.0 - 2.0 * yFrac))) * 180.0 / M_PI;
}
