// router.h - public routing interface, ported from jgauchia/IceNav-v3
// (lib/router/src/router.hpp, pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912). This is exactly the "put
// ROUTE.bin loading/A* consumption behind an interface" the requirements
// doc asked for - already clean, reused as designed. One adaptation:
// route() takes a routeSpeed parameter (see router.cpp's header comment
// for why - IceNav reads its own navSet.routeSpeed global instead, which
// this project doesn't have an equivalent of yet).
#pragma once
#include "graph_loader.h"
#include "astar.h"

enum class RouterResult {
    OK,
    NO_GRAPH_FILE,
    OUT_OF_MEMORY,
    NO_PATH,
    LOAD_ERROR,
};

class Router {
public:
    // routeSpeed selects both the ROUTE.bin profile (CAR/BIKE/WALK - see
    // routeBinPath() in route_types.h) and the A* heuristic's assumed max
    // speed: <=5 km/h => WALK, <=25 => BIKE, else CAR.
    RouterResult route(float src_lat, float src_lon,
                        float dst_lat, float dst_lon,
                        uint16_t routeSpeed, TrackVector &out_track);
    void unload();
    bool isLoaded() const { return loader_.isLoaded(); }

private:
    GraphLoader loader_;
};

extern Router router;
