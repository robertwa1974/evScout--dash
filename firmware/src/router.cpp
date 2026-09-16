// router.cpp - ported from jgauchia/IceNav-v3 (lib/router/src/
// router.cpp, pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912). Combines GraphLoader and A*
// exactly as upstream. Two deliberate deviations:
// - route() takes routeSpeed as a parameter instead of reading IceNav's
//   own navSet.routeSpeed global (this project has no settings-singleton
//   equivalent yet - a future Settings screen entry, per CLAUDE.md's
//   Settings section, would be the natural place to pick CAR/BIKE/WALK,
//   not added here since it's out of this port's scope).
// - The diagnostic timing line uses this project's existing
//   DEBUG/CAN_TRACE-guarded Serial.printf() convention (see e.g.
//   firmware.ino) instead of introducing ESP-IDF's ESP_LOG* macros,
//   which nothing else in this codebase uses.
#include "router.h"
#include "esp_timer.h"
#include <Arduino.h>

Router router;

RouterResult Router::route(float src_lat, float src_lon,
                            float dst_lat, float dst_lon,
                            uint16_t routeSpeed, TrackVector &out_track) {
    int64_t t_start = esp_timer_get_time();

    if (!loader_.isLoaded()) {
        if (!loader_.load(routeSpeed)) return RouterResult::LOAD_ERROR;
    }

    loader_.preloadPoint(src_lat, src_lon);
    loader_.preloadPoint(dst_lat, dst_lon);

    uint32_t src_node = loader_.nearestNode(src_lat, src_lon);
    uint32_t dst_node = loader_.nearestNode(dst_lat, dst_lon);

    out_track = astarRoute(loader_, src_node, dst_node, (float)routeSpeed);

#if defined(DEBUG) || defined(CAN_TRACE)
    int64_t elapsed_us = esp_timer_get_time() - t_start;
    Serial.printf("[router] (%.5f,%.5f)->(%.5f,%.5f): nodes %u->%u, %lldms, waypoints=%u\n",
                  src_lat, src_lon, dst_lat, dst_lon, src_node, dst_node,
                  elapsed_us / 1000, (unsigned)out_track.size());
#endif

    if (out_track.empty()) return RouterResult::NO_PATH;
    return RouterResult::OK;
}

void Router::unload() {
    loader_.unload();
}
