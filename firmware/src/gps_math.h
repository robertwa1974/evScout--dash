#pragma once
// gps_math.h - small subset of jgauchia/IceNav-v3's lib/utils/src/
// gpsMath.hpp/.cpp (pinned tag v.0.2.9, commit
// d1819b771e12394e185cdf18e8a14875b0998912), ported for nav_turn.cpp's
// turn-by-turn distance/bearing calculations.
//
// Only the plain (non-LUT) trig path is kept - upstream's sinLUT()/
// cosLUT() precomputed lookup tables exist to speed up ITS OWN
// continuous, per-frame 3D-perspective map rotation (maps.cpp's
// apply3DPerspective() - explicitly out of this project's scope, see
// nav_map_render.h's header comment for why). Turn guidance here runs at
// most once per GPS fix (~1Hz) against a route's worth of points (at
// most a few hundred), nowhere near that hot path, so the LUT's PSRAM
// allocation and one-time init call aren't worth adding for this.
// latFormatString()/lonFormatString() (DMS coordinate string
// formatting, unrelated to turn guidance) are the other thing NOT
// ported from gpsMath.
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EARTH_RADIUS 6378137.0f  // meters

static inline float DEG2RAD(float deg) { return deg * (float)(M_PI / 180.0); }
static inline float RAD2DEG(float rad) { return rad * (float)(180.0 / M_PI); }

// Haversine great-circle distance (meters).
float calcDist(float lat1, float lon1, float lat2, float lon2);

// Fast equirectangular-approximation SQUARED distance - expects radians,
// returns meters^2 (no sqrtf) - for comparing distances in a loop.
float calcDistSq(float lat1_rad, float lon1_rad, float lat2_rad, float lon2_rad);

// Initial bearing/course from point 1 to point 2, degrees 0-360.
float calcCourse(float lat1, float lon1, float lat2, float lon2);

// Signed angular difference a-b, normalized to [-180, 180].
float calcAngleDiff(float a, float b);
