#include "vehicle_config.h"
#include <Preferences.h>

float dynoVehicleMassKg = 1800.0f;
float dynoTargetKph = 96.6f;
float dynoLaunchKph = 2.0f;
float dynoLaunchAccelDeltaG = 0.15f;

// 1377 Calle Scott, Encinitas, CA 92024 / 6155 El Camino Real, Carlsbad, CA
// 92009 - both geocoded via OpenStreetMap's Nominatim, not GPS-surveyed -
// see ui_destinationsScreen.cpp's header comment for the full history.
double homeLat = 33.0304742, homeLon = -117.2533789;
double workLat = 33.1268426, workLon = -117.2670955;

// Own Preferences instance, not shared with zombie_updaters.cpp's - each
// Preferences object is a lightweight per-namespace handle in ESP32
// Arduino, cheap to have more than one, no reason to force a shared global.
static Preferences preferences;

void getVehicleConfig(void) {
    preferences.begin("vehcfg", true);
    dynoVehicleMassKg = preferences.getFloat("massKg", 1800.0f);
    dynoTargetKph = preferences.getFloat("targetKph", 96.6f);
    dynoLaunchKph = preferences.getFloat("launchKph", 2.0f);
    dynoLaunchAccelDeltaG = preferences.getFloat("launchAccG", 0.15f);
    homeLat = preferences.getDouble("homeLat", 33.0304742);
    homeLon = preferences.getDouble("homeLon", -117.2533789);
    workLat = preferences.getDouble("workLat", 33.1268426);
    workLon = preferences.getDouble("workLon", -117.2670955);
    preferences.end();
}

void updateVehicleConfig(void) {
    preferences.begin("vehcfg", false);
    preferences.putFloat("massKg", dynoVehicleMassKg);
    preferences.putFloat("targetKph", dynoTargetKph);
    preferences.putFloat("launchKph", dynoLaunchKph);
    preferences.putFloat("launchAccG", dynoLaunchAccelDeltaG);
    preferences.putDouble("homeLat", homeLat);
    preferences.putDouble("homeLon", homeLon);
    preferences.putDouble("workLat", workLat);
    preferences.putDouble("workLon", workLon);
    preferences.end();
}
