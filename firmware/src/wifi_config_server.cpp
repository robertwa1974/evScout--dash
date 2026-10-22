#include "wifi_config_server.h"
#include "vehicle_config.h"
#include "zombie_updaters.h"  // myData, dataMutex - drive-lockout check
#include "data_logger.h"
#include "odometer.h"
#include "sd_driver.h"
#include "gps_driver.h"
#include "ui_can_freshness.h"
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
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

    char page[3000];
    snprintf(page, sizeof(page),
        "<html><head><title>Scout 80 Config</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>"
        "<h2>Scout 80 Dash Config</h2>"
        "<p><a href='/live'>Live telemetry</a> &middot; <a href='/logs'>Data logs</a></p>"
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
        "<h3>Odometer</h3>"
        "Odometer (mi): <input name='odo' type='number' step='0.1' min='0' value='%.1f'><br><br>"
        "<input type='submit' value='Save'>"
        "</form></body></html>",
        dynoVehicleMassKg * KG_TO_LBS, dynoTargetKph * KPH_TO_MPH, dynoLaunchKph * KPH_TO_MPH, dynoLaunchAccelDeltaG,
        homeLat, homeLon, workLat, workLon, odometer_total_miles());
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

    // Odometer (2026-10-21): the form always resubmits the shown value, so
    // only apply a real edit - re-saving the rounded display value would
    // throw away up to 0.05 mi of accumulated distance on every Save.
    if (server.hasArg("odo")) {
        double miles = server.arg("odo").toDouble();
        if (miles >= 0 && miles < 1000000.0 && fabs(miles - odometer_total_miles()) > 0.05) {
            odometer_set_total_miles(miles);
        }
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

// --- Live telemetry (2026-10-21) ---------------------------------------------
// Static page + JSON endpoint rather than server-rendered HTML, so the phone
// refreshes values without reloading. Read-only, so NOT drive-locked.
static const char *LIVE_PAGE =
    "<html><head><title>Scout 80 Live</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:sans-serif;background:#111;color:#eee;margin:16px}"
    "table{border-collapse:collapse;width:100%;max-width:480px}"
    "td{padding:6px 8px;border-bottom:1px solid #333}td.v{text-align:right;font-weight:bold}"
    "a{color:#8cf}#s{color:#aaa}</style></head><body>"
    "<h2>Scout 80 Live</h2><p><a href='/'>Config</a> &middot; <a href='/logs'>Data logs</a></p>"
    "<p id='s'>Connecting...</p><table id='t'></table>"
    "<script>"
    "const L=[['veh_mph','Speed','mph'],['power_kw','Power','kW'],['soc','SOC','%'],"
    "['pack_v','Pack voltage','V'],['pack_a','Pack current','A'],['motor_rpm','Motor','rpm'],"
    "['motor_c','Motor temp','&deg;C'],['inverter_c','Inverter temp','&deg;C'],"
    "['cell_vmax','Cell V max','V'],['cell_vmin','Cell V min','V'],['cell_tmax','Cell T max','&deg;C'],"
    "['aux_v','12V aux','V'],['opmode','Op mode',''],['last_err','Last error',''],"
    "['gps_sats','GPS satellites',''],['odo_mi','Odometer','mi'],['trip_mi','Trip','mi'],['logging','Logging','']];"
    "async function u(){try{const d=await(await fetch('/api/live')).json();"
    "document.getElementById('s').textContent=d.can_live?'CAN live':'No CAN data';"
    "document.getElementById('t').innerHTML=L.map(r=>'<tr><td>'+r[1]+'</td><td class=v>'+d[r[0]]+' '+r[2]+'</td></tr>').join('');"
    "}catch(e){document.getElementById('s').textContent='Dash not reachable';}}"
    "u();setInterval(u,1000);"
    "</script></body></html>";

static const char *OPMODE_NAMES[] = {"Off", "Run", "Precharge", "PchFail", "Charge"};

static void handleLive() {
    server.send(200, "text/html", LIVE_PAGE);
}

static void handleApiLive() {
    struct_message d;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        d = myData;
        xSemaphoreGive(dataMutex);
    } else {
        server.send(503, "text/plain", "busy");
        return;
    }
    const char *opmode = (d.opmode >= 0 && d.opmode <= 4) ? OPMODE_NAMES[d.opmode] : "?";
    char json[640];
    snprintf(json, sizeof(json),
        "{\"can_live\":%s,\"veh_mph\":%.0f,\"power_kw\":%.1f,\"soc\":%d,"
        "\"pack_v\":%.1f,\"pack_a\":%.1f,\"motor_rpm\":%d,\"motor_c\":%d,\"inverter_c\":%d,"
        "\"cell_vmax\":%.3f,\"cell_vmin\":%.3f,\"cell_tmax\":%d,\"aux_v\":%.2f,"
        "\"opmode\":\"%s\",\"last_err\":%d,\"gps_fix\":%s,\"gps_sats\":%u,"
        "\"lat\":%.6f,\"lon\":%.6f,\"odo_mi\":%.1f,\"trip_mi\":%.1f,\"logging\":\"%s\"}",
        ui_can_freshness_isLive() ? "true" : "false",
        d.vehSpeedKph * KPH_TO_MPH, d.powerKw, d.soc,
        d.packVoltage, d.packCurrent, d.motorRpm, d.motorTemp, d.heatsinkTemp,
        d.cellVMax, d.cellVMin, d.cellTMax, d.aux12vVoltage,
        opmode, d.lastErr, gpsData.hasFix ? "true" : "false", (unsigned)gpsData.satellites,
        gpsData.hasFix ? gpsData.latitude : 0.0, gpsData.hasFix ? gpsData.longitude : 0.0,
        odometer_total_miles(), odometer_trip_miles(),
        data_logger_is_running() ? data_logger_current_file() : "off");
    server.send(200, "application/json", json);
}

// --- Data logs (2026-10-21) --------------------------------------------------
// Only names data_logger.cpp itself produces ("LOG" + 4 digits + ".csv") are
// accepted - the filename comes straight from the URL, so this is also the
// path-traversal guard.
static bool isLogName(const String &name) {
    if (name.length() != 11 || !name.startsWith("LOG") || !name.endsWith(".csv")) return false;
    for (int i = 3; i < 7; i++) {
        if (!isDigit(name[i])) return false;
    }
    return true;
}

static void handleLogs() {
    if (!sd_available()) {
        server.send(200, "text/html", "<html><body><h2>Data logs</h2><p>No SD card.</p><p><a href='/'>Back</a></p></body></html>");
        return;
    }
    String html =
        "<html><head><title>Scout 80 Logs</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>"
        "<h2>Data logs</h2><p><a href='/'>Config</a> &middot; <a href='/live'>Live telemetry</a></p>";
    if (vehicleIsMoving()) html += "<p><b>Vehicle is moving - download and delete are locked until stopped.</b></p>";
    html += "<table>";
    String active = data_logger_current_file();
    int count = 0;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        File dir = SD.open(DATA_LOGGER_DIR);
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                String name = f.name();
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                if (!f.isDirectory() && isLogName(name)) {
                    bool isActive = (name == active);
                    char row[200];
                    snprintf(row, sizeof(row),
                        "<tr><td><a href='/logs/get?f=%s'>%s</a></td><td>%lu KB</td><td>",
                        name.c_str(), name.c_str(), (unsigned long)((f.size() + 1023) / 1024));
                    html += row;
                    if (isActive) {
                        html += "(recording)";
                    } else {
                        html += "<form method='POST' action='/logs/delete' style='margin:0'>"
                                "<input type='hidden' name='f' value='" + name + "'>"
                                "<input type='submit' value='Delete'></form>";
                    }
                    html += "</td></tr>";
                    count++;
                }
                f.close();
                f = dir.openNextFile();
            }
        }
        if (dir) dir.close();
        xSemaphoreGive(sdMutex);
    }
    html += "</table>";
    if (count == 0) html += "<p>No logs yet - turn on Settings &gt; Data Logging on the dash.</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

// Streams the file in 2KB chunks, taking sdMutex per chunk (sd_driver.h's
// "one call per critical section" rule) rather than across the whole send.
static void handleLogGet() {
    if (vehicleIsMoving()) {
        server.send(403, "text/plain", "Vehicle is moving - download locked");
        return;
    }
    String name = server.arg("f");
    if (!isLogName(name) || !sd_available()) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    if (name == data_logger_current_file()) data_logger_flush();

    String path = String(DATA_LOGGER_DIR "/") + name;
    File f;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        f = SD.open(path, FILE_READ);
        xSemaphoreGive(sdMutex);
    }
    if (!f) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    server.sendHeader("Content-Disposition", String("attachment; filename=") + name);
    server.setContentLength(f.size());
    server.send(200, "text/csv", "");
    static uint8_t chunk[2048];
    while (true) {
        size_t n = 0;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
            n = f.read(chunk, sizeof(chunk));
            xSemaphoreGive(sdMutex);
        }
        if (n == 0) break;
        if (server.client().write(chunk, n) != n) break;  // phone went away
    }
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        f.close();
        xSemaphoreGive(sdMutex);
    }
}

static void handleLogDelete() {
    if (vehicleIsMoving()) {
        server.send(403, "text/plain", "Vehicle is moving - delete locked");
        return;
    }
    String name = server.arg("f");
    if (isLogName(name) && sd_available() && name != data_logger_current_file()) {
        String path = String(DATA_LOGGER_DIR "/") + name;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
            SD.remove(path);
            xSemaphoreGive(sdMutex);
        }
    }
    server.sendHeader("Location", "/logs");
    server.send(303);
}

// Why the last start attempt failed ("" if it didn't) - shown on the
// Settings WiFi row. Added 2026-10-21 after the bench found the AP
// invisible while the screen claimed it was on: start() used to ignore
// WiFi.mode()/softAP()'s return values entirely.
static char lastError[96] = "";

static void logLine(const char *msg) {
#if defined(DEBUG) || defined(CAN_TRACE)
    Serial.println(msg);
#else
    (void)msg;
#endif
}

// Phone join/leave diagnostics (debug builds only) - the disconnect reason
// code distinguishes a wrong password from a handshake timeout.
static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
#if defined(DEBUG) || defined(CAN_TRACE)
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.printf("[wifi] phone associated, aid=%d\n", info.wifi_ap_staconnected.aid);
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.printf("[wifi] phone left, reason=%d\n", info.wifi_ap_stadisconnected.reason);
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            Serial.printf("[wifi] phone got IP " IPSTR "\n", IP2STR(&info.wifi_ap_staipassigned.ip));
            break;
        default:
            break;
    }
#else
    (void)event; (void)info;
#endif
}

void wifi_config_server_start(void) {
    if (running) return;
    static bool eventsHooked = false;
    if (!eventsHooked) {
        WiFi.onEvent(onWifiEvent);
        eventsHooked = true;
    }
    char msg[160];
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    snprintf(msg, sizeof(msg), "[wifi] start: internal heap free=%u largest=%u, psram free=%u",
             (unsigned)freeInt, (unsigned)largestInt, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    logLine(msg);

    bool modeOk = WiFi.mode(WIFI_AP);
    bool apOk = modeOk && WiFi.softAP(WIFI_CONFIG_SSID, WIFI_CONFIG_PASSWORD);
    snprintf(msg, sizeof(msg), "[wifi] mode=%d softAP=%d ip=%s ch=%d",
             modeOk, apOk, WiFi.softAPIP().toString().c_str(), (int)WiFi.channel());
    logLine(msg);
    if (!apOk) {
        snprintf(lastError, sizeof(lastError), "WiFi failed to start (%s; %uKB RAM free)",
                 modeOk ? "AP" : "radio", (unsigned)(freeInt / 1024));
        WiFi.mode(WIFI_OFF);
        return;  // stays off, and the "off" preference is left as it was
    }
    lastError[0] = '\0';

    static bool routesAdded = false;  // WebServer keeps handlers across stop()/begin()
    if (!routesAdded) {
        server.on("/", HTTP_GET, handleRoot);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/live", HTTP_GET, handleLive);
        server.on("/api/live", HTTP_GET, handleApiLive);
        server.on("/logs", HTTP_GET, handleLogs);
        server.on("/logs/get", HTTP_GET, handleLogGet);
        server.on("/logs/delete", HTTP_POST, handleLogDelete);
        routesAdded = true;
    }
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

const char *wifi_config_server_last_error(void) {
    return lastError;
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
