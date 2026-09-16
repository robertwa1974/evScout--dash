// nav_turn.cpp - see nav_turn.h for this port's scope and deviations
// from jgauchia/IceNav-v3 (pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912).
#include "nav_turn.h"
#include "gps_math.h"
#include <cmath>
#include <algorithm>

// Ported from GPXParser::getTurnPointsSlidingWindow() (lib/gpx/src/
// gpxParser.cpp) - a free function here since this project has no
// GPXParser/GPX-loading context to hang it off of.
TurnPointVector nav_turn_detect(const TrackVector &track, float thresholdDeg,
                                 float minDist, float sharpTurnDeg, int windowSize) {
    TurnPointVector turnPoints;
    if ((int)track.size() < 2 * windowSize + 1) return turnPoints;

    for (size_t i = (size_t)windowSize; i < track.size() - (size_t)windowSize; ++i) {
        float distWindow = 0.0f;
        bool skipWindow = false;
        for (int j = (int)i - windowSize; j < (int)i + windowSize; ++j) {
            float d = calcDist(track[j].lat, track[j].lon, track[j + 1].lat, track[j + 1].lon);
            if (d > 200.0f) { skipWindow = true; break; }
            distWindow += d;
        }
        if (skipWindow) continue;

        float brgStart = calcCourse(track[i - windowSize].lat, track[i - windowSize].lon,
                                     track[i].lat, track[i].lon);
        float brgEnd = calcCourse(track[i].lat, track[i].lon,
                                   track[i + windowSize].lat, track[i + windowSize].lon);
        float diff = calcAngleDiff(brgEnd, brgStart);

        if (fabsf(diff) > sharpTurnDeg) {
            turnPoints.push_back({(int)i, diff, 0.0f});
            continue;
        }
        if (distWindow < minDist) continue;
        if (fabsf(diff) > thresholdDeg)
            turnPoints.push_back({(int)i, diff, 0.0f});
    }
    return turnPoints;
}

// Ported from navigation.cpp's findClosestTrackPoint() - the
// "Hierarchical Global Search" spatial-index fallback is replaced with a
// plain linear scan (see nav_turn.h's deviation note).
int findClosestTrackPoint(float userLat, float userLon, const TrackVector &track,
                           int lastIdx, const NavConfig &config) {
    int n = (int)track.size();
    if (n == 0) return 0;

    const float uLatRad = DEG2RAD(userLat);
    const float uLonRad = DEG2RAD(userLon);
    const float invEarthRadius = 1.0f / EARTH_RADIUS;
    const float fastPathThresholdSq = (20.0f * invEarthRadius) * (20.0f * invEarthRadius);

    float minDistSq = 3.4e38f;
    int closestIdx = -1;

    if (lastIdx >= 0 && lastIdx < n) {
        int start = std::max(0, lastIdx - 10);
        int end = std::min(n - 1, lastIdx + config.searchWindow);
        for (int i = start; i <= end; ++i) {
            float dSq = calcDistSq(uLatRad, uLonRad, DEG2RAD(track[i].lat), DEG2RAD(track[i].lon));
            if (dSq < minDistSq) { minDistSq = dSq; closestIdx = i; }
        }
        if (minDistSq < fastPathThresholdSq) return closestIdx;
    }

    minDistSq = 3.4e38f;
    closestIdx = -1;
    for (int i = 0; i < n; ++i) {
        float dSq = calcDistSq(uLatRad, uLonRad, DEG2RAD(track[i].lat), DEG2RAD(track[i].lon));
        if (dSq < minDistSq) { minDistSq = dSq; closestIdx = i; }
    }

    if (closestIdx == -1) return std::max(0, lastIdx);
    if (closestIdx < lastIdx && (lastIdx - closestIdx) < config.maxBackwardJump) return lastIdx;
    return closestIdx;
}

// Ported from navigation.cpp, unchanged.
void advanceTurnIndex(const TurnPointVector &turns, NavState &state, int closestIdx) {
    while (state.nextTurnIdx < (int)turns.size() && turns[state.nextTurnIdx].idx <= closestIdx)
        state.nextTurnIdx++;
}

// Ported from navigation.cpp - trimmed signature (dropped unused
// track/userLat/userLon/config params, see nav_turn.h).
int findNextValidTurn(const TurnPointVector &turns, int closestIdx, NavState &state) {
    for (int i = state.nextTurnIdx; i < (int)turns.size(); ++i) {
        if (turns[i].idx <= closestIdx) continue;
        return i;
    }
    return -1;
}

// Ported from navigation.cpp, unchanged.
float projectOnSegment(float pLat, float pLon, float aLat, float aLon, float bLat, float bLon,
                        float &outLat, float &outLon) {
    float cosFactor = cosf(DEG2RAD((aLat + bLat) / 2.0f));

    float dLat = bLat - aLat;
    float dLon = (bLon - aLon) * cosFactor;
    float pLatRel = pLat - aLat;
    float pLonRel = (pLon - aLon) * cosFactor;

    float denom = dLat * dLat + dLon * dLon;
    if (denom == 0.0f) {
        outLat = aLat;
        outLon = aLon;
        return calcDistSq(DEG2RAD(pLat), DEG2RAD(pLon), DEG2RAD(aLat), DEG2RAD(aLon));
    }

    float t = (pLatRel * dLat + pLonRel * dLon) / denom;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    outLat = aLat + t * (bLat - aLat);
    outLon = aLon + t * (bLon - aLon);
    return calcDistSq(DEG2RAD(pLat), DEG2RAD(pLon), DEG2RAD(outLat), DEG2RAD(outLon));
}

// Adapted from navigation.cpp's updateNavigation() - same decision
// logic, returns a plain struct instead of touching LVGL widgets
// directly (see nav_turn.h's deviation note for why).
TurnGuidance evaluateTurnGuidance(
    float userLat, float userLon,
    const TrackVector &track, const TurnPointVector &turns, NavState &state,
    float minAngleForCurve, float warnDist, const NavConfig &config) {
    TurnGuidance result;
    if (track.empty()) return result;

    int closestIdx = findClosestTrackPoint(userLat, userLon, track, state.lastTrackIdx, config);

    const float uLatRad = DEG2RAD(userLat);
    const float uLonRad = DEG2RAD(userLon);

    float pLat = track[closestIdx].lat;
    float pLon = track[closestIdx].lon;
    float bestLat = pLat, bestLon = pLon;
    float minDistSq = calcDistSq(uLatRad, uLonRad, DEG2RAD(pLat), DEG2RAD(pLon));

    if (closestIdx > 0) {
        float tLat, tLon;
        float dSq = projectOnSegment(userLat, userLon,
                                      track[closestIdx - 1].lat, track[closestIdx - 1].lon,
                                      track[closestIdx].lat, track[closestIdx].lon, tLat, tLon);
        if (dSq < minDistSq) { minDistSq = dSq; bestLat = tLat; bestLon = tLon; }
    }
    if (closestIdx < (int)track.size() - 1) {
        float tLat, tLon;
        float dSq = projectOnSegment(userLat, userLon,
                                      track[closestIdx].lat, track[closestIdx].lon,
                                      track[closestIdx + 1].lat, track[closestIdx + 1].lon, tLat, tLon);
        if (dSq < minDistSq) { minDistSq = dSq; bestLat = tLat; bestLon = tLon; }
    }

    float distToTrack = sqrtf(minDistSq) * EARTH_RADIUS;
    state.projLat = bestLat;
    state.projLon = bestLon;

    if (distToTrack > config.offTrackThreshold) {
        if (!state.isOffTrack) {
            state.lastValidTurnIdx = state.nextTurnIdx;
            state.isOffTrack = true;
        }
        state.lastTrackIdx = closestIdx;
        result.direction = TurnDirection::OFF_TRACK;
        return result;
    }

    if (state.isOffTrack) {
        state.nextTurnIdx = state.lastValidTurnIdx;
        state.isOffTrack = false;
    }

    advanceTurnIndex(turns, state, closestIdx);

    const float distToEnd = calcDist(userLat, userLon, track.back().lat, track.back().lon);

    if (state.nextTurnIdx >= (int)turns.size()) {
        state.lastTrackIdx = closestIdx;
        result.direction = (distToEnd <= 30.0f) ? TurnDirection::FINISH : TurnDirection::STRAIGHT;
        result.distanceMeters = distToEnd;
        return result;
    }

    int nextEventIdx = findNextValidTurn(turns, closestIdx, state);
    if (nextEventIdx == -1) {
        state.lastTrackIdx = closestIdx;
        result.direction = (distToEnd <= 30.0f) ? TurnDirection::FINISH : TurnDirection::STRAIGHT;
        result.distanceMeters = distToEnd;
        return result;
    }

    const float turnLat = track[turns[nextEventIdx].idx].lat;
    const float turnLon = track[turns[nextEventIdx].idx].lon;
    const float distanceToNextEvent = calcDist(userLat, userLon, turnLat, turnLon);
    const float absAngle = fabsf(turns[nextEventIdx].angle);
    const bool isRight = (turns[nextEventIdx].angle > 0.0f);

    TurnDirection dir = TurnDirection::STRAIGHT;
    if (distanceToNextEvent <= warnDist) {
        if (absAngle >= minAngleForCurve && absAngle < 60.0f)
            dir = isRight ? TurnDirection::SOFT_RIGHT : TurnDirection::SOFT_LEFT;
        else if (absAngle >= 60.0f)
            dir = isRight ? TurnDirection::HARD_RIGHT : TurnDirection::HARD_LEFT;
    }

    result.direction = dir;
    result.distanceMeters = distanceToNextEvent;
    state.lastTrackIdx = closestIdx;
    return result;
}
