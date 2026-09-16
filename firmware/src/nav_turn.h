#pragma once
// nav_turn.h - turn-by-turn guidance, ported from jgauchia/IceNav-v3
// (lib/utils/src/navigation.{hpp,cpp} + lib/gpx/src/gpxParser.cpp's
// GPXParser::getTurnPointsSlidingWindow(), pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912). IceNav's own turn-by-turn
// is "still quite basic" (see the routing plan's caveat) - confirmed by
// reading it: updateNavigation() (the function this file adapts) isn't
// even called anywhere in IceNav's own GUI at this pinned commit. Ported
// anyway since the DECISION logic itself (closest-point projection,
// off-track/finish/turn classification) is sound and exactly what the
// plan asked to reuse rather than write from scratch.
//
// Deviations from upstream:
// - TurnPoint/NavConfig, getTurnPointsSlidingWindow(),
//   advanceTurnIndex(), projectOnSegment() are ported close to verbatim
//   and KEEP their upstream names for traceability, matching this
//   project's router/NavReader port convention. NavState drops
//   isFinished (write-only in upstream too - set but never actually
//   read anywhere, confirmed by reading updateNavigation() in full).
// - findClosestTrackPoint()'s "Hierarchical Global Search" fallback
//   (upstream's TrackSegment/trackIndex spatial index, built while
//   parsing a loaded GPX file) is NOT ported - that index only exists to
//   make closest-point search fast against long recorded hiking tracks
//   (thousands of points). This project's TrackVector only ever comes
//   from Router::route()'s A* output (route_types.h) - a point-A-to-
//   point-B car route, realistically a few hundred nodes at most - where
//   a plain linear scan fallback is already fast enough. Flagged here
//   rather than silently dropped.
// - findNextValidTurn() drops its unused track/userLat/userLon/config
//   parameters (dead in the original function body - confirmed by
//   reading it, not assumed).
// - updateNavigation() is NOT ported as-is - upstream's version reaches
//   directly into LVGL widgets (lv_img_set_src() on hardcoded turnImg/
//   turnDistLabel globals), which doesn't fit this project's screens-own-
//   their-widgets convention (every other ui_*.c file owns and updates
//   its own objects; nothing here should reach into them from a
//   non-screen module). evaluateTurnGuidance() below ports the exact
//   same decision logic (closest point, off-track/finish/turn
//   classification, distance) but returns a plain TurnGuidance struct
//   instead - the new nav screen (component 5) turns that into an
//   icon/label update, the same way it already turns GpsData into widget
//   updates elsewhere. Also drops upstream's unused userHeading/
//   speed_kmh parameters (also dead in the original function body).
// - No u-turn classification: upstream declares uleft/uright icon
//   assets but its own updateNavigation() logic never actually detects
//   or selects them - TurnDirection below matches upstream's real
//   (not aspirational) behavior.
// - Street name resolution is explicitly NOT part of this (see the
//   routing plan's "one real gap" section) - TurnGuidance has no name
//   field. Icon + distance only for v1.

#include <vector>
#include "route_types.h"   // TrackVector, wayPoint
#include "psram_allocator.h"

// Ported from globalGpxDef.h - one detected turn along a track.
struct TurnPoint {
    int idx;          // index into the TrackVector this turn belongs to
    float angle;       // signed turn angle in degrees (positive = right, negative = left)
    float distance;    // accumulated distance from track start (meters) -
                         // NOT currently computed or read (upstream's own
                         // updateNavigation() doesn't read it either,
                         // confirmed by reading it) - always 0.0f here;
                         // kept as a field for shape fidelity with
                         // upstream and any future route-overview UI.
};

typedef std::vector<TurnPoint, PsramAllocator<TurnPoint>> TurnPointVector;

// Ported from navigation.hpp, unchanged.
struct NavConfig {
    int searchWindow = 100;
    float offTrackThreshold = 50.0f;
    float minTurnDistance = 5.0f;
    float maxTurnDistance = 2000.0f;
    int maxBackwardJump = 8;
};

// Ported from navigation.hpp - see this header's deviation note above
// for why isFinished was dropped.
struct NavState {
    int lastTrackIdx = 0;
    int nextTurnIdx = 0;
    int lastValidTurnIdx = 0;
    bool isOffTrack = false;
    float projLat = 0;
    float projLon = 0;
};

enum class TurnDirection {
    STRAIGHT,
    SOFT_LEFT,
    SOFT_RIGHT,
    HARD_LEFT,
    HARD_RIGHT,
    FINISH,
    OFF_TRACK,
};

struct TurnGuidance {
    TurnDirection direction = TurnDirection::STRAIGHT;
    float distanceMeters = 0.0f;  // to the next turn, or to the finish
};

// Ported from gpxParser.cpp's GPXParser::getTurnPointsSlidingWindow() -
// a free function here since this project has no GPXParser/GPX-loading
// context. Call ONCE after Router::route() produces a TrackVector, not
// per GPS fix. Defaults match the real values IceNav's own gpxScr.cpp
// calls it with (not guessed).
TurnPointVector nav_turn_detect(const TrackVector &track,
                                 float thresholdDeg = 18.0f, float minDist = 10.0f,
                                 float sharpTurnDeg = 70.0f, int windowSize = 5);

// Ported from navigation.cpp's findClosestTrackPoint() - see this
// header's deviation note above (no spatial-index fallback).
int findClosestTrackPoint(float userLat, float userLon, const TrackVector &track,
                           int lastIdx, const NavConfig &config = NavConfig{});

// Ported from navigation.cpp, unchanged.
void advanceTurnIndex(const TurnPointVector &turns, NavState &state, int closestIdx);

// Ported from navigation.cpp - trimmed signature, see deviation note above.
int findNextValidTurn(const TurnPointVector &turns, int closestIdx, NavState &state);

// Ported from navigation.cpp, unchanged.
float projectOnSegment(float pLat, float pLon, float aLat, float aLon, float bLat, float bLon,
                        float &outLat, float &outLon);

// NEW (not a verbatim port - see this header's deviation note above).
// Call once per GPS fix while a route is active.
TurnGuidance evaluateTurnGuidance(
    float userLat, float userLon,
    const TrackVector &track, const TurnPointVector &turns, NavState &state,
    float minAngleForCurve = 15.0f, float warnDist = 100.0f,
    const NavConfig &config = NavConfig{});
