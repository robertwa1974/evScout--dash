// graph_loader.cpp - ported from jgauchia/IceNav-v3 (lib/router/src/
// graph_loader.cpp, pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912). The LRU page-cache algorithm,
// binary-search cellForNode(), and nearestNode()'s two-pass cached-only
// search are unchanged from upstream. Two deliberate deviations, both
// small:
// - File access goes through Arduino SD.h (this project's existing
//   sd_driver.h access layer, see nav_reader.cpp's header comment for why
//   IceNav's own storage.hpp/ESP-IDF-VFS approach isn't used here) instead
//   of storage.hpp - storage.seekAndRead() becomes the small local
//   seekAndRead() helper below.
// - load() takes a routeSpeed parameter instead of reading IceNav's own
//   navSet.routeSpeed global (this project has no equivalent settings
//   singleton for a route-profile preference yet - Router::route(), the
//   caller, threads its own routeSpeed parameter through instead).
#include "graph_loader.h"
#include <cmath>
#include <cstring>
#include "esp_heap_caps.h"

GraphLoader graphLoader;

static size_t seekAndRead(File &f, uint32_t offset, uint8_t *buf, size_t size) {
    if (!f.seek(offset)) return 0;
    return f.read(buf, size);
}

bool GraphLoader::load(uint16_t routeSpeed) {
    unload();

    File f = SD.open(routeBinPath(routeSpeed), FILE_READ);
    if (!f) return false;

    RouteFileHeader hdr;
    if ((size_t)f.read((uint8_t *)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return false;
    }

    if (memcmp(hdr.magic, ROUTE_MAGIC, 4) != 0) {
        f.close();
        return false;
    }

    cellIndex_.resize(hdr.cell_count);
    f.read((uint8_t *)cellIndex_.data(), hdr.cell_count * sizeof(CellIndexEntry));

    // Data block starts immediately after header + index.
    data_base_offset_ = sizeof(RouteFileHeader) + hdr.cell_count * sizeof(CellIndexEntry);

    totalNodes_ = 0;
    for (const auto &c : cellIndex_) {
        uint32_t end = c.node_offset + c.node_count;
        if (end > totalNodes_) totalNodes_ = end;
    }

    file_ = f;
    fileOpen_ = true;
    loaded_ = true;
    return true;
}

// Returns the cellIndex_ index for the cell owning global node gi. Uses
// binary search on node_offset (cellIndex_ is sorted ascending by it).
uint32_t GraphLoader::cellForNode(uint32_t gi) const {
    if (cellIndex_.empty()) return UINT32_MAX;

    uint32_t lo = 0;
    uint32_t hi = (uint32_t)cellIndex_.size() - 1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1) / 2;
        if (cellIndex_[mid].node_offset <= gi) lo = mid;
        else hi = mid - 1;
    }

    const CellIndexEntry &c = cellIndex_[lo];
    if (gi >= c.node_offset && gi < c.node_offset + c.node_count) return lo;
    return UINT32_MAX;
}

void GraphLoader::evictLRU() const {
    if (pageCache_.empty()) return;

    uint32_t oldest_key = 0;
    uint32_t oldest_stamp = UINT32_MAX;
    for (const auto &kv : pageCache_) {
        if (kv.second.lru_stamp < oldest_stamp) {
            oldest_stamp = kv.second.lru_stamp;
            oldest_key = kv.first;
        }
    }
    pageCache_.erase(oldest_key);
}

GraphLoader::PageData *GraphLoader::fetchPage(uint32_t cell_idx) const {
    auto it = pageCache_.find(cell_idx);
    if (it != pageCache_.end()) {
        it->second.lru_stamp = ++lru_clock_;
        return &it->second;
    }

    if (!fileOpen_) return nullptr;

    const CellIndexEntry &c = cellIndex_[cell_idx];
    size_t node_bytes = c.node_count * sizeof(RouteNode);
    size_t edge_bytes = c.edge_count * sizeof(RouteEdge);
    size_t needed = node_bytes + edge_bytes;

    if (pageCache_.size() >= PAGE_CACHE_MAX) evictLRU();

    size_t avail = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (needed + (64 * 1024) > avail) return nullptr;

    PageData page;
    page.cell_idx = cell_idx;
    page.lru_stamp = ++lru_clock_;

    try {
        page.nodes.resize(c.node_count);
        page.edges.resize(c.edge_count);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    uint32_t file_offset = data_base_offset_ + c.data_offset;

    if (c.node_count > 0)
        seekAndRead(file_, file_offset, (uint8_t *)page.nodes.data(), node_bytes);

    if (c.edge_count > 0)
        seekAndRead(file_, file_offset + node_bytes, (uint8_t *)page.edges.data(), edge_bytes);

    auto res = pageCache_.emplace(cell_idx, std::move(page));
    if (!res.second) return nullptr;
    return &res.first->second;
}

// Preload the single cell that contains the given lat/lon point - used
// before nearestNode() to ensure the cell is in PSRAM cache.
void GraphLoader::preloadPoint(float lat, float lon) const {
    int32_t lat_e4 = (int32_t)(floorf(lat * 10000.f / 500.f) * 500.f);
    int32_t lon_e4 = (int32_t)(floorf(lon * 10000.f / 500.f) * 500.f);

    for (uint32_t i = 0; i < (uint32_t)cellIndex_.size(); ++i) {
        if (cellIndex_[i].lat_e4 == lat_e4 && cellIndex_[i].lon_e4 == lon_e4) {
            fetchPage(i);
            return;
        }
    }
}

bool GraphLoader::getNode(uint32_t gi, RouteNode &out_node) const {
    uint32_t ci = cellForNode(gi);
    if (ci == UINT32_MAX) return false;

    PageData *page = fetchPage(ci);
    if (page) {
        uint32_t local = gi - cellIndex_[ci].node_offset;
        out_node = page->nodes[local];
        return true;
    }

    // Fallback: read directly from SD using the cell's data_offset.
    if (!fileOpen_) return false;
    const CellIndexEntry &cb = cellIndex_[ci];
    uint32_t local = gi - cb.node_offset;
    uint32_t file_off = data_base_offset_ + cb.data_offset + local * sizeof(RouteNode);
    return seekAndRead(file_, file_off, (uint8_t *)&out_node, sizeof(RouteNode)) == sizeof(RouteNode);
}

// Find the nearest graph node to the given coordinates. Searches only
// pages already in the PSRAM cache to avoid SD I/O and cache eviction -
// tries +/-0.3 degrees first, expands to +/-1.0 degrees if nothing cached.
uint32_t GraphLoader::nearestNode(float lat, float lon) const {
    uint32_t best_i = 0;
    float best_d = 1e30f;
    float cos_lat = cosf(lat * 3.14159265f / 180.f);

    static constexpr float RADII[2] = {0.3f, 1.0f};

    for (float radius : RADII) {
        for (uint32_t ci = 0; ci < (uint32_t)cellIndex_.size(); ++ci) {
            const CellIndexEntry &cell = cellIndex_[ci];
            float cell_lat = cell.lat_e4 / 10000.0f;
            float cell_lon = cell.lon_e4 / 10000.0f;

            if (cell_lat > lat + radius || cell_lat + 0.1f < lat - radius) continue;
            if (cell_lon > lon + radius || cell_lon + 0.1f < lon - radius) continue;

            // Only search pages already in PSRAM - never trigger an SD load here.
            auto it = pageCache_.find(ci);
            if (it == pageCache_.end()) continue;
            const PageData &page = it->second;

            for (uint32_t j = 0; j < cell.node_count; ++j) {
                const RouteNode &n = page.nodes[j];
                float dlat = n.lat - lat;
                float dlon = (n.lon - lon) * cos_lat;
                float d = dlat * dlat + dlon * dlon;
                if (d < best_d) { best_d = d; best_i = cell.node_offset + j; }
            }
        }
        if (best_d < 1e30f) break;  // found something - skip wider pass
    }

    return best_i;
}

bool GraphLoader::getEdgesForNode(uint32_t gi, RouteEdge *buf, uint32_t &count) const {
    uint32_t ci = cellForNode(gi);
    if (ci == UINT32_MAX) { count = 0; return true; }

    const CellIndexEntry &cell = cellIndex_[ci];
    uint32_t local_idx = gi - cell.node_offset;

    PageData *page = fetchPage(ci);

    uint32_t rel_e_start;
    uint32_t rel_e_end;

    if (page) {
        rel_e_start = page->nodes[local_idx].edge_offset;
        rel_e_end = (local_idx + 1 < cell.node_count) ? page->nodes[local_idx + 1].edge_offset : cell.edge_count;

        count = rel_e_end - rel_e_start;
        if (count > MAX_EDGES_PER_NODE_GL) { count = 0; return true; }

        memcpy(buf, &page->edges[rel_e_start], count * sizeof(RouteEdge));
        return true;
    }

    // Fallback: read from SD without page cache.
    if (!fileOpen_) { count = 0; return false; }

    RouteNode n, nxt;
    if (!getNode(gi, n)) { count = 0; return false; }
    rel_e_start = n.edge_offset;

    if (local_idx + 1 < cell.node_count) {
        rel_e_end = getNode(gi + 1, nxt) ? nxt.edge_offset : rel_e_start;
    } else {
        rel_e_end = cell.edge_count;
    }

    count = rel_e_end - rel_e_start;
    if (count > MAX_EDGES_PER_NODE_GL) { count = 0; return true; }

    // Edge block starts after the node block within this cell's data_offset.
    size_t bytes = count * sizeof(RouteEdge);
    uint32_t edges_start = data_base_offset_ + cell.data_offset
                          + cell.node_count * sizeof(RouteNode)
                          + rel_e_start * sizeof(RouteEdge);
    return seekAndRead(file_, edges_start, (uint8_t *)buf, bytes) == bytes;
}

void GraphLoader::unload() {
    if (fileOpen_) {
        file_.close();
        fileOpen_ = false;
    }
    cellIndex_.clear();
    pageCache_.clear();
    lru_clock_ = 0;
    data_base_offset_ = 0;
    totalNodes_ = 0;
    loaded_ = false;
}
