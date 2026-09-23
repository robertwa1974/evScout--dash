#include "wifi_config_server.h"
#include "vehicle_config.h"
#include "zombie_updaters.h"  // myData, dataMutex - drive-lockout check
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

static WebServer server(80);
static bool running = false;
static Preferences preferences;  // own instance, own namespace - see vehicle_config.cpp's identical reasoning

// Display-only metric->imperial conversion (2026-09-22), same rule/values
// as the rest of this project's UI (ui_speedScreen.h's KPH_TO_MPH,
// ui_navScreen.cpp's formatDistance()) - the page shows lbs/mph, but
// dynoVehicleMassKg/dynoTargetKph/dynoLaunchKph stay kg/kph underneath,
// since that's the unit every consumer (road-load physics, CAN speed
// comparisons) actually needs. Converted at the two boundaries only: once
// generating the HTML, once parsing the submitted form.
static const float KG_TO_LBS = 2.20462262f;
static const float KPH_TO_MPH = 0.621371f;

static bool vehicleIsMoving(void) {
    bool moving = false;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        moving = (myData.vehSpeedKph > 0);
        xSemaphoreGive(dataMutex);
    }
    return moving;
}

static const char *LOCKED_PAGE =
    "<html><head><title>Scout 80 Config</title></head><body>"
    "<h2>Vehicle is moving</h2>"
    "<p>Configuration is locked while the vehicle is in motion. Stop the vehicle to edit.</p>"
    "</body></html>";

static void handleRoot() {
    if (vehicleIsMoving()) {
        server.send(200, "text/html", LOCKED_PAGE);
        return;
    }

    char page[2200];
    snprintf(page, sizeof(page),
        "<html><head><title>Scout 80 Config</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>"
        "<h2>Scout 80 Dash Config</h2>"
        "<form method='POST' action='/save'>"
        "<h3>Dyno</h3>"
        "Vehicle mass, incl. driver (lbs): <input name='mass' type='number' step='0.1' value='%.1f'><br><br>"
        "Target speed (mph, run ends here): <input name='target' type='number' step='0.1' value='%.1f'><br><br>"
        "Launch speed threshold (mph): <input name='launch' type='number' step='0.1' value='%.1f'><br><br>"
        "Launch IMU threshold (g): <input name='accg' type='number' step='0.01' value='%.2f'><br><br>"
        "<h3>Destinations</h3>"
        "Home latitude: <input name='homelat' type='number' step='0.0000001' value='%.7f'><br><br>"
        "Home longitude: <input name='homelon' type='number' step='0.0000001' value='%.7f'><br><br>"
        "Work latitude: <input name='worklat' type='number' step='0.0000001' value='%.7f'><br><br>"
        "Work longitude: <input name='worklon' type='number' step='0.0000001' value='%.7f'><br><br>"
        "<input type='submit' value='Save'>"
        "</form></body></html>",
        dynoVehicleMassKg * KG_TO_LBS, dynoTargetKph * KPH_TO_MPH, dynoLaunchKph * KPH_TO_MPH, dynoLaunchAccelDeltaG,
        homeLat, homeLon, workLat, workLon);
    server.send(200, "text/html", page);
}

// Parses arg name into *out if present and within [lo, hi] - silently
// ignores an out-of-range or unparseable value rather than accepting
// garbage, same "clamp, don't trust raw input" rule as every other
// numeric input in this project (e.g. ui_settingsScreen.cpp's spinbox
// range clamps). lo/hi are in *out's own (native, kg/kph) units.
static void applyFloatArg(const char *name, float *out, float lo, float hi) {
    if (!server.hasArg(name)) return;
    float v = server.arg(name).toFloat();
    if (v >= lo && v <= hi) *out = v;
}

// Same, but the submitted value is in a DISPLAY unit (lbs/mph) that needs
// converting to *out's native unit (kg/kph) first - toNativeScale is that
// conversion factor (e.g. 1/KG_TO_LBS). lo/hi are still in *out's native
// unit, same as applyFloatArg, so both call sites clamp against the same
// range regardless of which form field fed them.
static void applyFloatArgScaled(const char *name, float *out, float toNativeScale, float lo, float hi) {
    if (!server.hasArg(name)) return;
    float v = server.arg(name).toFloat() * toNativeScale;
    if (v >= lo && v <= hi) *out = v;
}

static void applyDoubleArg(const char *name, double *out, double lo, double hi) {
    if (!server.hasArg(name)) return;
    double v = server.arg(name).toDouble();
    if (v >= lo && v <= hi) *out = v;
}

static void handleSave() {
    if (vehicleIsMoving()) {
        server.send(403, "text/plain", "Vehicle is moving - save refused");
        return;
    }

    applyFloatArgScaled("mass", &dynoVehicleMassKg, 1.0f / KG_TO_LBS, 500.0f, 5000.0f);
    applyFloatArgScaled("target", &dynoTargetKph, 1.0f / KPH_TO_MPH, 1.0f, 300.0f);
    applyFloatArgScaled("launch", &dynoLaunchKph, 1.0f / KPH_TO_MPH, 0.0f, 50.0f);
    applyFloatArg("accg", &dynoLaunchAccelDeltaG, 0.02f, 2.0f);
    applyDoubleArg("homelat", &homeLat, -90.0, 90.0);
    applyDoubleArg("homelon", &homeLon, -180.0, 180.0);
    applyDoubleArg("worklat", &workLat, -90.0, 90.0);
    applyDoubleArg("worklon", &workLon, -180.0, 180.0);
    updateVehicleConfig();

    server.sendHeader("Location", "/");
    server.send(303);
}

void wifi_config_server_start(void) {
    if (running) return;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_CONFIG_SSID, WIFI_CONFIG_PASSWORD);
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    running = true;

    preferences.begin("wificfg", false);
    preferences.putBool("enabled", true);
    preferences.end();
}

void wifi_config_server_stop(void) {
    if (!running) return;
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    running = false;

    preferences.begin("wificfg", false);
    preferences.putBool("enabled", false);
    preferences.end();
}

bool wifi_config_server_is_running(void) {
    return running;
}

void wifi_config_server_init(void) {
    preferences.begin("wificfg", true);
    bool wasEnabled = preferences.getBool("enabled", false);
    preferences.end();
    if (wasEnabled) wifi_config_server_start();
}

void wifi_config_server_poll(void) {
    if (running) server.handleClient();
}
