// gps_math.cpp - see gps_math.h for scope. Ported verbatim (minus the
// LUT path) from jgauchia/IceNav-v3's lib/utils/src/gpsMath.cpp (pinned
// tag v.0.2.9, commit d1819b771e12394e185cdf18e8a14875b0998912).
#include "gps_math.h"

float calcDist(float lat1, float lon1, float lat2, float lon2) {
    float lat1_rad = DEG2RAD(lat1);
    float lon1_rad = DEG2RAD(lon1);
    float lat2_rad = DEG2RAD(lat2);
    float lon2_rad = DEG2RAD(lon2);
    float dlat = lat2_rad - lat1_rad;
    float dlon = lon2_rad - lon1_rad;

    float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(lat1_rad) * cosf(lat2_rad) *
              sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return EARTH_RADIUS * c;
}

float calcDistSq(float lat1_rad, float lon1_rad, float lat2_rad, float lon2_rad) {
    float x = (lon2_rad - lon1_rad) * cosf((lat1_rad + lat2_rad) / 2.0f);
    float y = lat2_rad - lat1_rad;
    return (x * x + y * y);
}

float calcCourse(float lat1, float lon1, float lat2, float lon2) {
    lat1 = DEG2RAD(lat1);
    lat2 = DEG2RAD(lat2);
    float dLon = DEG2RAD(lon2 - lon1);

    float y = sinf(dLon) * cosf(lat2);
    float x = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(dLon);
    float course = RAD2DEG(atan2f(y, x));

    if (course < 0.0f) course += 360.0f;
    return course;
}

float calcAngleDiff(float a, float b) {
    float diff = a - b;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}
