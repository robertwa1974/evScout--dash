#include "nav_tile_reader.h"
#include "sd_driver.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>

// See nav_tile_format.h for the verified on-disk byte layout this parses.
// Streams directly from the open SD File rather than reading a whole tile
// into a temp buffer first - avoids needing a large RAM copy per load and
// matches this MCU's constraints.

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
                           const uint16_t *palette, uint16_t paletteCount,
                           NavFeature *out, bool *truncated) {
    memset(out, 0, sizeof(*out));
    out->geomType = fh.geomType;
    out->colorRgb565 = (fh.colorIndex < paletteCount) ? palette[fh.colorIndex] : 0;
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

    char path[24];
    snprintf(path, sizeof(path), "/maps/Z%u.nav", (unsigned)zoom);
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    NavMapHeader mh;
    if ((size_t)f.read((uint8_t *)&mh, sizeof(mh)) != sizeof(mh)) { f.close(); return false; }
    if (memcmp(mh.magic, NAV_MAP_MAGIC, 4) != 0) { f.close(); return false; }

    if (tileX < mh.bottomLeftX || tileY < mh.bottomLeftY) { f.close(); return false; }
    uint32_t xOff = tileX - mh.bottomLeftX;
    uint32_t yOff = tileY - mh.bottomLeftY;
    if (xOff >= mh.tilesWide || yOff >= mh.tilesHigh) { f.close(); return false; }

    uint32_t flatIdx = yOff * mh.tilesWide + xOff;
    uint32_t indexPos = sizeof(NavMapHeader) + flatIdx * sizeof(NavIndexEntry);
    if (!f.seek(indexPos)) { f.close(); return false; }

    NavIndexEntry entry;
    if ((size_t)f.read((uint8_t *)&entry, sizeof(entry)) != sizeof(entry)) { f.close(); return false; }
    if (entry.size == 0) { f.close(); return false; }  // legitimately empty tile

    // Palette sits right after the flat index table, before the tile
    // data - read fresh each call rather than cached across zoom levels,
    // simplest correct approach until real usage shows this needs
    // caching (color_count is capped at 255 by the format's own 1-byte
    // index, so this is at most 510 bytes).
    uint64_t flatCount = (uint64_t)mh.tilesWide * mh.tilesHigh;
    uint32_t paletteOffset = sizeof(NavMapHeader) + (uint32_t)(flatCount * sizeof(NavIndexEntry));
    uint16_t palette[256];
    uint16_t paletteCount = mh.colorCount < 256 ? mh.colorCount : 256;
    if (paletteCount > 0) {
        if (!f.seek(paletteOffset)) { f.close(); return false; }
        for (uint16_t i = 0; i < paletteCount; i++) {
            if (!readU16LE(f, palette[i])) { f.close(); return false; }
        }
    }

    if (!f.seek(entry.offset)) { f.close(); return false; }

    NavTileHeader th;
    if ((size_t)f.read((uint8_t *)&th, sizeof(th)) != sizeof(th)) { f.close(); return false; }
    if (memcmp(th.magic, NAV_TILE_MAGIC, 4) != 0) { f.close(); return false; }

    uint16_t toDecode = th.featureCount < NAV_MAX_FEATURES_PER_TILE ? th.featureCount : NAV_MAX_FEATURES_PER_TILE;
    if (th.featureCount > NAV_MAX_FEATURES_PER_TILE) out->truncated = true;

    uint16_t decoded = 0;
    for (uint16_t i = 0; i < th.featureCount; i++) {
        NavFeatureHeader fh;
        if ((size_t)f.read((uint8_t *)&fh, sizeof(fh)) != sizeof(fh)) break;
        uint64_t coordCountRaw, payloadSize;
        if (!readVarint(f, coordCountRaw)) break;
        if (!readVarint(f, payloadSize)) break;

        uint32_t payloadStart = f.position();
        uint32_t payloadEnd = payloadStart + (uint32_t)payloadSize;

        if (i < toDecode) {
            decodeFeature(f, fh, coordCountRaw, palette, paletteCount, &out->features[decoded], &out->truncated);
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
        }
        // Unconditional resync - correct regardless of how much of the
        // payload decodeFeature() actually consumed (including a fully
        // truncated/skipped feature when i >= toDecode).
        f.seek(payloadEnd);
    }
    out->featureCount = decoded;

    f.close();
    return true;
}
