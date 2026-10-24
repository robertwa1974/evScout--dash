#include "wifi_config_server.h"
#include "vehicle_config.h"
#include "zombie_updaters.h"  // myData, dataMutex - drive-lockout check
#include "data_logger.h"
#include "can_capture.h"
#include "odometer.h"
#include "sd_driver.h"
#include "gps_driver.h"
#include "ui_can_freshness.h"
#include "wifi_pages.h"
#include <SD.h>
#include <vector>
#include <math.h>
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

// --- Page chrome (2026-10-23 restyle) ----------------------------------------
// Shared header/nav/footer around every page; styles come from /style.css
// (wifi_pages.h), served once and cached by the phone.
static void pageStart(String &html, const char *title, const char *active) {
    html.reserve(6144);
    html += "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta name='theme-color' content='#111111'><title>";
    html += title;
    // Bump ?v= whenever STYLE_CSS changes, so phones drop their cached copy.
    html += " - Scout 80</title><link rel='stylesheet' href='/style.css?v=1'></head><body>"
            "<header><div class='brand'>SCOUT <b>80</b></div><nav>";
    static const char *const LINKS[][2] = {{"/", "Config"}, {"/live", "Live"}, {"/logs", "Logs"}};
    for (const auto &l : LINKS) {
        html += "<a href='";
        html += l[0];
        html += strcmp(active, l[0]) == 0 ? "' class='on'>" : "'>";
        html += l[1];
        html += "</a>";
    }
    html += "</nav></header><main>";
}

static void pageEnd(String &html) {
    html += "</main><footer>Scout 80 dash &middot; local hotspot " WIFI_CONFIG_SSID
            " &middot; no internet</footer></body></html>";
}

// Pages are never cached (2026-10-23): with no cache headers, phone browsers
// kept showing the old pages after a firmware update. Only the stylesheet is
// cacheable, and its URL is versioned in pageStart() so a restyle still lands.
static void sendPage(String &html) {
    pageEnd(html);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", html);
}

static void handleStyle() {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send(200, "text/css", STYLE_CSS);
}

// One labelled number input: label + hint on the left, input + unit on the right.
static void field(String &html, const char *label, const char *hint, const char *name,
                  const char *step, const char *value, const char *unit) {
    html += "<div class='field'><label for='";
    html += name;
    html += "'>";
    html += label;
    html += "</label><div class='inp'><input type='number' inputmode='decimal' id='";
    html += name;
    html += "' name='";
    html += name;
    html += "' step='";
    html += step;
    html += "' value='";
    html += value;
    html += "'><span class='unit'>";
    html += unit;
    html += "</span></div>";
    if (hint && hint[0]) {
        html += "<small>";
        html += hint;
        html += "</small>";
    }
    html += "</div>";
}

static void fieldF(String &html, const char *label, const char *hint, const char *name,
                   const char *step, int decimals, double v, const char *unit) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    field(html, label, hint, name, step, buf, unit);
}

static void handleRoot() {
    String html;
    pageStart(html, "Config", "/");
    html += "<h1>Dash settings</h1>";
    if (vehicleIsMoving()) {
        html += "<div class='banner warn'>Vehicle is moving &mdash; settings are locked. "
                "Stop the vehicle to edit.</div>";
        sendPage(html);
        return;
    }
    if (server.hasArg("saved")) html += "<div class='banner ok'>Saved.</div>";

    html += "<form method='POST' action='/save'>";
    html += "<div class='card'><h2>Dyno</h2>";
    fieldF(html, "Vehicle mass", "Including driver", "mass", "0.1", 1, dynoVehicleMassKg * KG_TO_LBS, "lbs");
    fieldF(html, "Target speed", "The run ends here", "target", "0.1", 1, dynoTargetKph * KPH_TO_MPH, "mph");
    fieldF(html, "Launch speed", "Speed that counts as launched", "launch", "0.1", 1, dynoLaunchKph * KPH_TO_MPH, "mph");
    fieldF(html, "Launch IMU threshold", "Acceleration that counts as launched", "accg", "0.01", 2, dynoLaunchAccelDeltaG, "g");
    html += "</div>";

    html += "<div class='card'><h2>Destinations</h2>";
    fieldF(html, "Home latitude", "", "homelat", "0.0000001", 7, homeLat, "&deg;");
    fieldF(html, "Home longitude", "", "homelon", "0.0000001", 7, homeLon, "&deg;");
    fieldF(html, "Work latitude", "", "worklat", "0.0000001", 7, workLat, "&deg;");
    fieldF(html, "Work longitude", "", "worklon", "0.0000001", 7, workLon, "&deg;");
    html += "</div>";

    html += "<div class='card'><h2>Odometer</h2>";
    fieldF(html, "Odometer", "Only changes if you edit it", "odo", "0.1", 1, odometer_total_miles(), "mi");
    html += "</div>";

    html += "<button class='btn primary' type='submit'>Save</button></form>";
    sendPage(html);
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

    server.sendHeader("Location", "/?saved=1");
    server.send(303);
}

// --- Live telemetry (2026-10-21) ---------------------------------------------
// Static page + JSON endpoint rather than server-rendered HTML, so the phone
// refreshes values without reloading. Read-only, so NOT drive-locked.
static const char *OPMODE_NAMES[] = {"Off", "Run", "Precharge", "PchFail", "Charge"};

static void handleLive() {
    String html;
    pageStart(html, "Live", "/live");
    html += "<h1>Live telemetry</h1>";
    html += LIVE_BODY;
    sendPage(html);
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
    // Efficiency (2026-10-24): JSON has no NaN, so "not enough data yet" is -1.
    double tripWhmi = odometer_trip_wh_per_mile();
    double recentWhmi = odometer_recent_wh_per_mile();
    double rangeMi = ui_can_freshness_hasEverReceived() ? odometer_range_miles(d.soc, d.battCapKWh) : NAN;
    if (isnan(tripWhmi)) tripWhmi = -1;
    if (isnan(recentWhmi)) recentWhmi = -1;
    if (isnan(rangeMi)) rangeMi = -1;
    char json[760];
    snprintf(json, sizeof(json),
        "{\"can_live\":%s,\"veh_mph\":%.0f,\"power_kw\":%.1f,\"soc\":%d,"
        "\"pack_v\":%.1f,\"pack_a\":%.1f,\"motor_rpm\":%d,\"motor_c\":%d,\"inverter_c\":%d,"
        "\"cell_vmax\":%.3f,\"cell_vmin\":%.3f,\"cell_tmax\":%d,\"aux_v\":%.2f,"
        "\"opmode\":\"%s\",\"last_err\":%d,\"gps_fix\":%s,\"gps_sats\":%u,"
        "\"lat\":%.6f,\"lon\":%.6f,\"odo_mi\":%.1f,\"trip_mi\":%.1f,\"logging\":\"%s\","
        "\"range_mi\":%.0f,\"trip_whmi\":%.0f,\"recent_whmi\":%.0f}",
        ui_can_freshness_isLive() ? "true" : "false",
        d.vehSpeedKph * KPH_TO_MPH, d.powerKw, d.soc,
        d.packVoltage, d.packCurrent, d.motorRpm, d.motorTemp, d.heatsinkTemp,
        d.cellVMax, d.cellVMin, d.cellTMax, d.aux12vVoltage,
        opmode, d.lastErr, gpsData.hasFix ? "true" : "false", (unsigned)gpsData.satellites,
        gpsData.hasFix ? gpsData.latitude : 0.0, gpsData.hasFix ? gpsData.longitude : 0.0,
        odometer_total_miles(), odometer_trip_miles(),
        data_logger_is_running() ? data_logger_current_file() : (data_logger_is_enabled() ? "waiting" : "off"),
        rangeMi, tripWhmi, recentWhmi);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

// --- Data logs (2026-10-21) --------------------------------------------------
// Only names this firmware itself produces are accepted - "LOG"+4 digits
// (data_logger.h, in /logs) or "CAN"+4 digits (can_capture.h, in /can,
// 2026-10-22), + ".csv". The filename comes straight from the URL, so this
// is also the path-traversal guard.
static bool isLogName(const String &name) {
    if (name.length() != 11 || !name.endsWith(".csv")) return false;
    if (!name.startsWith("LOG") && !name.startsWith("CAN")) return false;
    for (int i = 3; i < 7; i++) {
        if (!isDigit(name[i])) return false;
    }
    return true;
}

static String logPath(const String &name) {
    return String(name.startsWith("CAN") ? CAN_CAPTURE_DIR "/" : DATA_LOGGER_DIR "/") + name;
}

// Being written right now - flushed before download, never deleted.
static bool isActiveLog(const String &name) {
    return name == data_logger_current_file() || name == can_capture_current_file();
}

static void flushIfActive(const String &name) {
    if (name == data_logger_current_file()) data_logger_flush();
    if (name == can_capture_current_file()) can_capture_flush();
}

// Appends one table row per log file in dirPath. Caller holds sdMutex.
static int listLogDir(const char *dirPath, String &html) {
    int count = 0;
    File dir = SD.open(dirPath);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            String name = f.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (!f.isDirectory() && isLogName(name)) {
                char row[240];
                size_t bytes = f.size();
                char size[16];
                if (bytes >= 1024UL * 1024UL) snprintf(size, sizeof(size), "%.1f MB", bytes / 1048576.0);
                else snprintf(size, sizeof(size), "%lu KB", (unsigned long)((bytes + 1023) / 1024));
                snprintf(row, sizeof(row),
                    "<tr><td class='name'>%s</td><td class='size'>%s</td><td class='act'>"
                    "<a class='btn sm' href='/logs/get?f=%s'>Download</a>",
                    name.c_str(), size, name.c_str());
                html += row;
                if (isActiveLog(name)) {
                    html += " <span class='badge'>RECORDING</span>";
                } else {
                    html += "<form method='POST' action='/logs/delete' "
                            "onsubmit=\"return confirm('Delete " + name + "?')\">"
                            "<input type='hidden' name='f' value='" + name + "'>"
                            "<button class='btn sm danger' type='submit'>Delete</button></form>";
                }
                html += "</td></tr>";
                count++;
            }
            f.close();
            f = dir.openNextFile();
        }
    }
    if (dir) dir.close();
    return count;
}

// "Delete all" form under a card's table (2026-10-24) - confirm() first, since
// it removes every finished file in that folder at once.
static void deleteAllButton(String &html, const char *dir, const char *what) {
    html += "<form method='POST' action='/logs/delete-all' style='margin-top:12px' "
            "onsubmit=\"return confirm('Delete ALL ";
    html += what;
    html += "? Files still recording are kept.')\">"
            "<input type='hidden' name='dir' value='";
    html += dir;
    html += "'><button class='btn sm danger' type='submit'>Delete all</button></form>";
}

static void handleLogs() {
    String html;
    pageStart(html, "Logs", "/logs");
    html += "<h1>Logs &amp; captures</h1>";
    if (!sd_available()) {
        html += "<div class='banner warn'>No SD card in the dash.</div>";
        sendPage(html);
        return;
    }
    if (vehicleIsMoving()) {
        html += "<div class='banner warn'>Vehicle is moving &mdash; download and delete are locked "
                "until it stops.</div>";
    }
    int logCount = 0, canCount = 0;
    String logRows, canRows;
    if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        logCount = listLogDir(DATA_LOGGER_DIR, logRows);
        canCount = listLogDir(CAN_CAPTURE_DIR, canRows);
        xSemaphoreGive(sdMutex);
    }
    html += "<div class='card'><h2>Telemetry logs (CSV)</h2>";
    if (logCount) {
        html += "<table>" + logRows + "</table>";
        deleteAllButton(html, "logs", "telemetry logs");
    }
    else html += "<p class='empty'>None yet &mdash; turn on Settings &rsaquo; Data Logging on the dash.</p>";
    html += "</div><div class='card'><h2>CAN captures (SavvyCAN CSV)</h2>";
    if (canCount) {
        html += "<table>" + canRows + "</table>";
        deleteAllButton(html, "can", "CAN captures");
    }
    else html += "<p class='empty'>None yet &mdash; Settings &rsaquo; CAN Monitor &rsaquo; START CAPTURE on the dash.</p>";
    html += "</div><p class='hint'>Open captures in SavvyCAN with File &rsaquo; Load Log File "
            "(GVRET / native CSV).</p>";
    sendPage(html);
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
    flushIfActive(name);

    String path = logPath(name);
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

// Deletes every finished log in one folder (dir=logs or dir=can). Names are
// collected first, then removed - deleting while iterating a FAT directory
// can skip entries. Same guards as single delete: drive lock, name check,
// never the file currently recording.
static void handleLogDeleteAll() {
    if (vehicleIsMoving()) {
        server.send(403, "text/plain", "Vehicle is moving - delete locked");
        return;
    }
    String which = server.arg("dir");
    const char *dirPath = (which == "can") ? CAN_CAPTURE_DIR : (which == "logs") ? DATA_LOGGER_DIR : NULL;
    if (dirPath && sd_available()) {
        std::vector<String> names;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
            File dir = SD.open(dirPath);
            if (dir && dir.isDirectory()) {
                File f = dir.openNextFile();
                while (f) {
                    String name = f.name();
                    int slash = name.lastIndexOf('/');
                    if (slash >= 0) name = name.substring(slash + 1);
                    if (!f.isDirectory() && isLogName(name) && !isActiveLog(name)) names.push_back(name);
                    f.close();
                    f = dir.openNextFile();
                }
            }
            if (dir) dir.close();
            xSemaphoreGive(sdMutex);
        }
        for (const String &name : names) {
            String path = logPath(name);
            if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
                SD.remove(path);
                xSemaphoreGive(sdMutex);
            }
        }
    }
    server.sendHeader("Location", "/logs");
    server.send(303);
}

static void handleLogDelete() {
    if (vehicleIsMoving()) {
        server.send(403, "text/plain", "Vehicle is moving - delete locked");
        return;
    }
    String name = server.arg("f");
    if (isLogName(name) && sd_available() && !isActiveLog(name)) {
        String path = logPath(name);
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
        server.on("/style.css", HTTP_GET, handleStyle);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/live", HTTP_GET, handleLive);
        server.on("/api/live", HTTP_GET, handleApiLive);
        server.on("/logs", HTTP_GET, handleLogs);
        server.on("/logs/get", HTTP_GET, handleLogGet);
        server.on("/logs/delete", HTTP_POST, handleLogDelete);
        server.on("/logs/delete-all", HTTP_POST, handleLogDeleteAll);
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
