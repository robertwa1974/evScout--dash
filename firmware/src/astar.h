// astar.h - A* routing algorithm with Haversine-ish heuristic, ported
// verbatim from jgauchia/IceNav-v3 (lib/router/src/astar.hpp, pinned tag
// v.0.2.9, commit d1819b771e12394e185cdf18e8a14875b0998912). No
// adaptation needed - operates purely on GraphLoader/RouteNode/RouteEdge/
// TrackVector, all already adapted in graph_loader.h/route_types.h.
#pragma once
#include "graph_loader.h"
#include "route_types.h"

TrackVector astarRoute(const GraphLoader &graph, uint32_t src_node, uint32_t dst_node, float maxSpeedKmh = 130.0f);
