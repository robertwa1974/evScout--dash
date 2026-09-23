#include "zombie_updaters.h"
#include "can_trace.h"
#include "gps_driver.h"
#include "ui_can_freshness.h"
#include "ui_card_grid.h"
#include "ui_shutdown.h"
#include <math.h>

ESP32S3_TWAI can;
Preferences preferences;

struct_message myData = {0};
struct_message old_myData = {0};
warning_set warningSet = {0};

// See the big comment on uiGeneration in zombie_updaters.h. Starts at 1 (not
// 0) so the very first fastUpdate/midUpdate/slowUpdate tick after boot also
// does a full push, not just ones after a lazily-created screen.
volatile uint32_t uiGeneration = 1;

void ui_notify_screen_created(void) {
    uiGeneration++;
}

// Forward declaration - defined near applyDisplayMode()/resolveDisplayPref()
// further down, but TaskCANReceiver() (right below) needs to attach it to a
// Ticker before that point in the file.
static void checkAutoTheme();

// --- SDO poll table --------------------------------------------------------
// All 25 live telemetry params (see the big comment in zombie_updaters.h for
// why this covers what used to be CAN-broadcast-only fields too).
struct PollEntry { uint16_t paramId; };
static const PollEntry pollTable[] = {
    {PARAM_ID_VEH_SPEED},
    {PARAM_ID_OPMODE},
    {PARAM_ID_POWER},
    {PARAM_ID_LASTERR},
    {PARAM_ID_SOC},
    {PARAM_ID_UDC},
    {PARAM_ID_IDC},
    {PARAM_ID_TMPM},
    {PARAM_ID_TMPHS},
    {PARAM_ID_U12V},
    {PARAM_ID_MOTOR_SPEED},
    {PARAM_ID_GEAR},
    {PARAM_ID_MOT_ACTIVE},
    {PARAM_ID_REGENMAX},
    {PARAM_ID_DIR},
    {PARAM_ID_BMS_VMAX},
    {PARAM_ID_BMS_VMIN},
    {PARAM_ID_BMS_TMAX},
    {PARAM_ID_VOLTSPNT},
    {PARAM_ID_CHGTEMP},
    {PARAM_ID_CHGTYP},
    {PARAM_ID_PLUGDET},
    {PARAM_ID_AC_VOLTS},
    {PARAM_ID_PILOTLIM},
    {PARAM_ID_CABLELIM},
};
static const size_t pollTableSize = sizeof(pollTable) / sizeof(pollTable[0]);
static size_t pollCursor = 0;

static void sendNextPollRequest() {
    if (pollTableSize == 0) return;
    sdoRequestRead(pollTable[pollCursor].paramId);
    pollCursor = (pollCursor + 1) % pollTableSize;
}

// Set only inside the PARAM_ID_U12V case below - i.e. only when the VCU has
// actually returned a real (non-abort) SDO value for U12V at least once.
// Needed because ui_can_freshness_hasEverReceived() is a single global
// flag covering ANY successfully-decoded param, not this one specifically
// - see the aux12V update site further down for why that distinction
// matters (a ZombieVerter with no U12V measurement configured aborts every
// read of this one param forever, while everything else on the bus
// responds normally).
static bool aux12vEverReceived = false;

static void applySdoValue(uint16_t paramId, int32_t rawValue) {
    float value = sdoToFloat(rawValue);
    switch (paramId) {
        case PARAM_ID_VEH_SPEED:   myData.vehSpeedKph   = (int)value; break;
        case PARAM_ID_OPMODE:      myData.opmode        = (int)value; break;
        case PARAM_ID_POWER:       myData.powerKw       = value;       break;
        case PARAM_ID_LASTERR:     myData.lastErr       = (int)value; break;
        case PARAM_ID_SOC:         myData.soc           = (int)value; break;
        case PARAM_ID_UDC:         myData.packVoltage   = value;       break;
        case PARAM_ID_IDC:         myData.packCurrent   = value;       break;
        case PARAM_ID_TMPM:        myData.motorTemp     = (int)value; break;
        case PARAM_ID_TMPHS:       myData.heatsinkTemp  = (int)value; break;
        case PARAM_ID_U12V:        myData.aux12vVoltage = value; aux12vEverReceived = true; break;
        case PARAM_ID_MOTOR_SPEED: myData.motorRpm      = (int)value; break;
        case PARAM_ID_GEAR:        myData.gear          = (int)value; break;
        case PARAM_ID_MOT_ACTIVE:  myData.motActive     = (int)value; break;
        case PARAM_ID_REGENMAX:    myData.regenMax      = value;       break;
        case PARAM_ID_DIR:         myData.dirState      = (int)value; break;
        case PARAM_ID_BMS_VMAX:    myData.cellVMax      = value;       break;
        case PARAM_ID_BMS_VMIN:    myData.cellVMin      = value;       break;
        case PARAM_ID_BMS_TMAX:    myData.cellTMax      = (int)value; break;
        case PARAM_ID_VOLTSPNT:    myData.chargeSetpointV = value;     break;
        case PARAM_ID_CHGTEMP:     myData.chgTemp       = (int)value; break;
        case PARAM_ID_CHGTYP:      myData.chgType       = (int)value; break;
        case PARAM_ID_PLUGDET:     myData.plugDet       = (int)value; break;
        case PARAM_ID_AC_VOLTS:    myData.acVolts       = value;       break;
        case PARAM_ID_PILOTLIM:    myData.pilotLimA     = value;       break;
        case PARAM_ID_CABLELIM:    myData.cableLimA     = value;       break;
        default: break;
    }
}

void TaskCANReceiver(void *pvParameters) {
    uint32_t id;
    uint8_t data[8];
    uint8_t length;
    bool extended;

    Ticker updateUiFast;
    Ticker updateUiMid;
    Ticker updateUiSlow;
    Ticker sendPoll;
    Ticker autoThemeTicker;
#ifdef CAN_TRACE
    Ticker traceDump;
#endif

    getWarningsSet();

    bool ret = false;
#ifdef DEBUG
    Serial.println("TaskCANReceiver init");
#endif

    while (!ret) {
        ret = can.init(); // TODO: verify 500kbit default in twai_lib.h matches your VCU's bus
#ifdef DEBUG
        Serial.printf("can.init : %s\n", ret ? "OK" : "FAIL");
#endif
    }

    // TODO: twai_lib's default filter is tuned for rusEFI's extended IDs.
    // Left accept-everything for now (useFilter defaults false in twai_lib's
    // init signature) - revisit once bus load is a known quantity.
    ret = can.alertConfigure(TWAI_ALERT_RX_DATA | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF);
#ifdef DEBUG
    Serial.printf("can.alertConfigure : %s\n", ret ? "OK" : "FAIL");
#endif

    updateUiFast.attach_ms(PERIOD_FAST_MS, fastUpdate);
    updateUiMid.attach_ms(PERIOD_MID_MS, midUpdate);
    updateUiSlow.attach_ms(PERIOD_SLOW_MS, slowUpdate);
    sendPoll.attach_ms(PERIOD_SDO_POLL_MS, sendNextPollRequest); // 25-entry table -> ~1250ms full refresh
    autoThemeTicker.attach_ms(PERIOD_AUTO_THEME_MS, checkAutoTheme);
#ifdef CAN_TRACE
    traceDump.attach_ms(1000, canTraceDumpMyData);
#endif

    while (1) {
        if (can.getAlerts()) {
            while (can.receive(&id, data, &length, &extended)) {
                canTraceFrame("RX", id, data, length);
                if (extended) {
                    continue; // ZombieVerter traffic here is standard (11-bit) ID
                }

                if (id == SDO_RX_COBID) {
                    uint16_t paramId;
                    int32_t rawValue;
                    if (sdoParseResponse(data, length, &paramId, &rawValue)) {
                        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
                            applySdoValue(paramId, rawValue);
                            ui_can_freshness_notifyFrame();
                            xSemaphoreGive(dataMutex);
                        }
                    }
                } else {
                    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
                        decodeBroadcastFrame(id, data, length);
                        ui_can_freshness_notifyFrame();
                        xSemaphoreGive(dataMutex);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// --- Widget updaters ------------------------------------------------------
// Bound across the four telemetry screens from the 2026-09-14 Speed/Drive/
// Status/Battery split (see waveshare-dash-build.md):
//   ui_speedScreen  - vehicle speed (mph, converted from vehSpeedKph) + the
//                      P/N/F/R shifter-position pill (dirState)
//   ui_driveScreen  - power, pack current, gear selection (text enum),
//                      motor mode (text enum), regen limit
//   ui_statusScreen - SOC, pack voltage, aux/12V voltage, motor temp,
//                      inverter (heatsink) temp, max battery temp
//   ui_batteryScreen- SOC (again - it's genuinely useful on both the
//                      at-a-glance Status screen and the BMS detail
//                      screen), status pill (from packCurrent's sign), max/
//                      min cell voltage, computed cell delta-V, max cell
//                      temp
// opmode, lastErr, dcdcState still have no widget on any current screen -
// same "deferred" status as before this split, can come back as a fault
// banner later.
//
// Pattern: snapshot changed fields out of myData under dataMutex, release
// it, THEN take uiMutex and touch LVGL objects. Don't hold both at once -
// loop() in firmware.ino guards every lv_timer_handler() call with uiMutex,
// so any LVGL call made from these Ticker callbacks (a different task
// context) without it would race the render loop.
//
// Every widget touch is NULL-guarded: only ui_speedScreen exists at boot
// (ui_init() creates it eagerly; the other three are created lazily on
// first swipe - see ui.c and each screen's _ui_screen_change call), and
// these Tickers start running (inside TaskCANReceiver) well before the user
// necessarily visits the other three.
//
// Theming: each screen's own <screen>_refresh_theme() repaints static chrome
// (backgrounds, titles, units) on a live day/night toggle, but deliberately
// leaves the dynamic value labels alone - those get their color re-set here
// on every natural data update instead (via setWarnColor, themed either way),
// so a value that hasn't changed in a while can lag the new theme by at most
// one update cycle (<=800ms) rather than needing every screen to walk every
// label on toggle.

static void setWarnColor(lv_obj_t *obj, bool warn) {
    if (!obj) return;
    lv_obj_set_style_text_color(obj, warn ? ui_theme_warning() : ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

// Battery-level icon (LV_SYMBOL_BATTERY_FULL/3/2/1/EMPTY) to reinforce a
// bare SOC% with a shape (design-review "icons" pass, 2026-09-14 - see
// waveshare-dash-build.md). Thresholds are evenly spaced for a glanceable
// indicator, not tied to any particular cell chemistry's real discharge
// curve - this isn't a calibrated fuel gauge.
static const char *batteryIconForSoc(int soc) {
    if (soc >= 80) return LV_SYMBOL_BATTERY_FULL;
    if (soc >= 55) return LV_SYMBOL_BATTERY_3;
    if (soc >= 30) return LV_SYMBOL_BATTERY_2;
    if (soc >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

// kph -> mph, display-only conversion (see ui_speedScreen.h) - the
// underlying myData.vehSpeedKph field stays in kph because the dyno screens
// depend on it.
static const float KPH_TO_MPH = 0.621371f;

// PARAM_ID_GEAR: 0=LOW,1=HIGH,2=AUTO,3=HIGHFWDLOWREV (ZombieVerter's own
// reduction-gear setting - NOT the P/N/F/R shifter position, see dirState).
static const char *gearText(int gear) {
    switch (gear) {
        case 0:  return "LOW";
        case 1:  return "HIGH";
        case 2:  return "AUTO";
        case 3:  return "HI-FOR/LO-REV";
        default: return "--";
    }
}

// PARAM_ID_MOT_ACTIVE: 0=Mg1and2,1=Mg1,2=Mg2,3=BlendingMG2and1.
static const char *motorModeText(int motActive) {
    switch (motActive) {
        case 0:  return "MG1+MG2";
        case 1:  return "MG1";
        case 2:  return "MG2";
        case 3:  return "BLEND";
        default: return "--";
    }
}

void fastUpdate() {
    // See uiGeneration's comment in zombie_updaters.h: force one full push
    // (bypassing the dirty check) after any screen is (re)created, so a
    // freshly-created widget gets seeded with the current value even if
    // that field hasn't changed since the last poll.
    static uint32_t lastGen = 0;
    bool forcePush = (lastGen != uiGeneration);

    bool speedChanged = false, dirChanged = false;
    int speedKph = 0, dirState = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        if (forcePush) lastGen = uiGeneration;
        if (myData.vehSpeedKph != old_myData.vehSpeedKph || forcePush) {
            speedChanged = true;
            speedKph = myData.vehSpeedKph;
            old_myData.vehSpeedKph = myData.vehSpeedKph;
        }
        if (myData.dirState != old_myData.dirState || forcePush) {
            dirChanged = true;
            dirState = myData.dirState;
            old_myData.dirState = myData.dirState;
        }
        xSemaphoreGive(dataMutex);
    }
    if (!speedChanged && !dirChanged) return;

    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        if (speedChanged) {
            int speedMph = (int)(speedKph * KPH_TO_MPH + 0.5f);
            if (ui_speedValLabel) {
                lv_label_set_text_fmt(ui_speedValLabel, "%d", speedMph);
                setWarnColor(ui_speedValLabel, false);
            }
            if (ui_speedMeter && ui_speedNeedle) {
                lv_meter_set_indicator_value(ui_speedMeter, ui_speedNeedle, speedMph);
            }
        }
        if (dirChanged) {
            ui_speedScreen_setDirState(dirState);  // no-op if the screen doesn't exist yet
        }
        xSemaphoreGive(uiMutex);
    }
}

void midUpdate() {
    static uint32_t lastGen = 0;
    bool forcePush = (lastGen != uiGeneration);

    bool aChanged = false, pChanged = false;
    float a = 0, p = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        if (forcePush) lastGen = uiGeneration;
        if (myData.packCurrent != old_myData.packCurrent || forcePush) {
            aChanged = true; a = myData.packCurrent; old_myData.packCurrent = myData.packCurrent;
        }
        if (myData.powerKw != old_myData.powerKw || forcePush) {
            pChanged = true; p = myData.powerKw; old_myData.powerKw = myData.powerKw;
        }
        xSemaphoreGive(dataMutex);
    }
    if (!aChanged && !pChanged) return;

    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        if (aChanged) {
            // Drive screen's current bar + Battery screen's status pill
            // (pack current itself has no bar on the Battery screen
            // anymore - see ui_batteryScreen.c).
            if (ui_driveCurrBar) lv_bar_set_value(ui_driveCurrBar, (int)a, LV_ANIM_ON);
            if (ui_driveCurrValLabel) {
                lv_label_set_text_fmt(ui_driveCurrValLabel, "%.1f A", a);
                setWarnColor(ui_driveCurrValLabel, false);
            }
            ui_batteryScreen_setStatus(a);  // charging/discharging/idle pill - no-ops if screen doesn't exist yet
        }
        if (pChanged) {
            if (ui_drivePowerValLabel) {
                lv_label_set_text_fmt(ui_drivePowerValLabel, "%.1f", p);
                setWarnColor(ui_drivePowerValLabel, false);
            }
            if (ui_drivePowerBar) lv_bar_set_value(ui_drivePowerBar, (int)p, LV_ANIM_ON);
        }
        xSemaphoreGive(uiMutex);
    }
}

void slowUpdate() {
    ui_can_freshness_tick();

    static uint32_t lastGen = 0;
    bool forcePush = (lastGen != uiGeneration);

    bool socChanged = false, motTChanged = false, hsTChanged = false, voltChanged = false;
    bool auxVChanged = false, gearChanged = false, motModeChanged = false, regenChanged = false;
    bool vMaxChanged = false, vMinChanged = false, tMaxChanged = false;
    bool setpointChanged = false, chgTempChanged = false, chgStatusChanged = false;
    bool acVChanged = false, limChanged = false;
    int soc = 0, motT = 0, hsT = 0, gear = 0, motActive = 0, cellTMax = 0;
    int chgTempV = 0, opmode = 0, chgType = 0, plugDet = 0;
    float volt = 0, auxV = 0, regen = 0, cellVMax = 0, cellVMin = 0;
    float setpointV = 0, acV = 0, pilotLim = 0, cableLim = 0;

    bool gpsStatusChanged = false, gpsSpeedChanged = false, gpsLatLonChanged = false;
    bool gpsHeadingChanged = false, gpsAltChanged = false;
    bool gpsHasFix = false;
    uint8_t gpsSats = 0;
    float gpsSpeed = 0, gpsHeading = 0, gpsAlt = 0;
    double gpsLat = 0, gpsLon = 0;

    bool clockChanged = false;

    // Fault banner (styling pass Phase 7) needs the CURRENT value every
    // tick, not just on change - a CAN dropout is detected purely by time
    // passing with no new frame, which the changed-flag pattern above
    // can't express. Read unconditionally below, not gated behind the
    // early-return further down.
    int curMotorTemp = 0, curHeatsinkTemp = 0, curLastErr = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        if (forcePush) lastGen = uiGeneration;
        if (myData.soc != old_myData.soc || forcePush) {
            socChanged = true; soc = myData.soc; old_myData.soc = myData.soc;
        }
        if (myData.motorTemp != old_myData.motorTemp || forcePush) {
            motTChanged = true; motT = myData.motorTemp; old_myData.motorTemp = myData.motorTemp;
        }
        if (myData.heatsinkTemp != old_myData.heatsinkTemp || forcePush) {
            hsTChanged = true; hsT = myData.heatsinkTemp; old_myData.heatsinkTemp = myData.heatsinkTemp;
        }
        curMotorTemp = myData.motorTemp;
        curHeatsinkTemp = myData.heatsinkTemp;
        if (myData.packVoltage != old_myData.packVoltage || forcePush) {
            voltChanged = true; volt = myData.packVoltage; old_myData.packVoltage = myData.packVoltage;
        }
        if (myData.aux12vVoltage != old_myData.aux12vVoltage || forcePush) {
            auxVChanged = true; auxV = myData.aux12vVoltage; old_myData.aux12vVoltage = myData.aux12vVoltage;
        }
        if (myData.gear != old_myData.gear || forcePush) {
            gearChanged = true; gear = myData.gear; old_myData.gear = myData.gear;
        }
        if (myData.motActive != old_myData.motActive || forcePush) {
            motModeChanged = true; motActive = myData.motActive; old_myData.motActive = myData.motActive;
        }
        if (myData.regenMax != old_myData.regenMax || forcePush) {
            regenChanged = true; regen = myData.regenMax; old_myData.regenMax = myData.regenMax;
        }
        if (myData.cellVMax != old_myData.cellVMax || forcePush) {
            vMaxChanged = true; old_myData.cellVMax = myData.cellVMax;
        }
        if (myData.cellVMin != old_myData.cellVMin || forcePush) {
            vMinChanged = true; old_myData.cellVMin = myData.cellVMin;
        }
        // Snapshot both together (not just the one that changed) so the
        // computed delta below is never torn between an old and a new
        // value - cheap, and avoids reading myData outside dataMutex.
        cellVMax = myData.cellVMax;
        cellVMin = myData.cellVMin;
        if (myData.cellTMax != old_myData.cellTMax || forcePush) {
            tMaxChanged = true; cellTMax = myData.cellTMax; old_myData.cellTMax = myData.cellTMax;
        }
        if (myData.opmode != old_myData.opmode || forcePush) {
            // TODO: opmode itself was also once considered as a fault-
            // banner input, separate from the lastErr/overtemp/CAN-dropout
            // set ui_fault_banner.h actually wires (Phase 7, 2026-09-18) -
            // opmode is a normal operating-mode enum (Off/Run/Precharge/
            // PchFail/Charge), not itself a fault signal, so it was left
            // out of that set; still just consumed below, by the Charging
            // screen's status pill.
            chgStatusChanged = true;
            old_myData.opmode = myData.opmode;
        }
        if (myData.lastErr != old_myData.lastErr) {
            old_myData.lastErr = myData.lastErr;
        }
        curLastErr = myData.lastErr;
        if (myData.chargeSetpointV != old_myData.chargeSetpointV || forcePush) {
            setpointChanged = true; setpointV = myData.chargeSetpointV; old_myData.chargeSetpointV = myData.chargeSetpointV;
        }
        if (myData.chgTemp != old_myData.chgTemp || forcePush) {
            chgTempChanged = true; chgTempV = myData.chgTemp; old_myData.chgTemp = myData.chgTemp;
        }
        if (myData.acVolts != old_myData.acVolts || forcePush) {
            acVChanged = true; acV = myData.acVolts; old_myData.acVolts = myData.acVolts;
        }
        if (myData.pilotLimA != old_myData.pilotLimA || forcePush) {
            limChanged = true; old_myData.pilotLimA = myData.pilotLimA;
        }
        if (myData.cableLimA != old_myData.cableLimA || forcePush) {
            limChanged = true; old_myData.cableLimA = myData.cableLimA;
        }
        pilotLim = myData.pilotLimA;
        cableLim = myData.cableLimA;
        // Charge Status pill needs opmode/chgType/plugDet together (same
        // "snapshot together, not just the one that changed" reasoning as
        // cellVMax/cellVMin above) - opmode is already read above for the
        // TODO fault-banner check, snapshot it here too rather than
        // duplicating that comparison.
        if (myData.chgType != old_myData.chgType || forcePush) {
            chgStatusChanged = true; old_myData.chgType = myData.chgType;
        }
        if (myData.plugDet != old_myData.plugDet || forcePush) {
            chgStatusChanged = true; old_myData.plugDet = myData.plugDet;
        }
        opmode = myData.opmode;
        chgType = myData.chgType;
        plugDet = myData.plugDet;

        // GPS - gpsData is a separate struct (gps_driver.h), same dataMutex
        // (reusing the critical section already open here rather than a
        // second take/release cycle). Own static "old" snapshot since
        // there's no old_gpsData sibling to myData/old_myData.
        static bool     old_hasFix = false;
        static uint8_t  old_sats = 0;
        static float    old_gpsSpeed = 0, old_gpsHeading = 0, old_gpsAlt = 0;
        static double   old_lat = 0, old_lon = 0;
        if (gpsData.hasFix != old_hasFix || gpsData.satellites != old_sats || forcePush) {
            gpsStatusChanged = true;
            old_hasFix = gpsData.hasFix;
            old_sats = gpsData.satellites;
        }
        if (gpsData.speedKph != old_gpsSpeed || forcePush) {
            gpsSpeedChanged = true; old_gpsSpeed = gpsData.speedKph;
        }
        if (gpsData.latitude != old_lat || gpsData.longitude != old_lon || forcePush) {
            gpsLatLonChanged = true; old_lat = gpsData.latitude; old_lon = gpsData.longitude;
        }
        if (gpsData.headingDeg != old_gpsHeading || forcePush) {
            gpsHeadingChanged = true; old_gpsHeading = gpsData.headingDeg;
        }
        if (gpsData.altitudeM != old_gpsAlt || forcePush) {
            gpsAltChanged = true; old_gpsAlt = gpsData.altitudeM;
        }
        // Splash/Clock screen's UTC clock - reuses this same GPS snapshot
        // rather than a separate critical section. hasFix is part of the
        // dirty-check too so the caption flips from "waiting for fix" to a
        // real time (and back, if the fix is ever lost) even on minutes
        // where the clock digits themselves don't change.
        static uint8_t old_clockHour = 255, old_clockMinute = 255;  // sentinel - never a real value, forces the first push
        static bool old_clockHasFix = false;
        if (gpsData.hour != old_clockHour || gpsData.minute != old_clockMinute || gpsData.hasFix != old_clockHasFix || forcePush) {
            clockChanged = true;
            old_clockHour = gpsData.hour;
            old_clockMinute = gpsData.minute;
            old_clockHasFix = gpsData.hasFix;
        }
        gpsHasFix = gpsData.hasFix;
        gpsSats = gpsData.satellites;
        gpsSpeed = gpsData.speedKph;
        gpsLat = gpsData.latitude;
        gpsLon = gpsData.longitude;
        gpsHeading = gpsData.headingDeg;
        gpsAlt = gpsData.altitudeM;
        xSemaphoreGive(dataMutex);
    }

    // Fault banner (styling pass Phase 7): evaluated every tick,
    // deliberately NOT folded into the early-return below - a CAN dropout
    // is detected by time passing with no new frame, which wouldn't set
    // any of the changed-flags this early-return checks. ui_fault_banner_
    // update() does its own internal dirty-check, so this is cheap on the
    // (overwhelmingly common) tick where nothing about the fault state
    // actually changed.
    bool canDropout = ui_can_freshness_hasEverReceived() && !ui_can_freshness_isLive();
    bool overTemp = (curMotorTemp >= warningSet.motorTemp) || (curHeatsinkTemp >= warningSet.heatsinkTemp);
    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        ui_fault_banner_update(canDropout, overTemp, curLastErr);
        xSemaphoreGive(uiMutex);
    }

    bool deltaVChanged = vMaxChanged || vMinChanged;
    if (!(socChanged || motTChanged || hsTChanged || voltChanged || auxVChanged ||
          gearChanged || motModeChanged || regenChanged || vMaxChanged || vMinChanged || tMaxChanged ||
          setpointChanged || chgTempChanged || chgStatusChanged || acVChanged || limChanged ||
          gpsStatusChanged || gpsSpeedChanged || gpsLatLonChanged || gpsHeadingChanged || gpsAltChanged ||
          clockChanged)) {
        return;
    }

    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        if (socChanged) {
            // Gate on CAN freshness, not just the raw threshold check -
            // fixes a real bug (styling pass items 3-4, 2026-09-18):
            // myData.soc is zero-initialized at boot, and 0 <= lowSoc
            // (default 15) is true, so this label rendered in warning red
            // before any real CAN frame ever arrived. A value that's
            // never been reported isn't "critically low," it's simply
            // unknown - ui_card_setValueNoData() below shows that
            // honestly (dim gray) instead of either red or a confident-
            // looking plain white "0".
            bool noData = !ui_can_freshness_hasEverReceived();
            bool warn = !noData && (soc <= warningSet.lowSoc);
            const char *battIcon = batteryIconForSoc(soc);
            if (ui_statusSocArc) lv_arc_set_value(ui_statusSocArc, soc);
            if (ui_statusSocValLabel) {
                lv_label_set_text_fmt(ui_statusSocValLabel, "%d", soc);  // no "%" - see ui_statusScreen.c arc-containment comment
                if (noData) ui_card_setValueNoData(ui_statusSocValLabel, NULL, true);
                else { ui_card_setValueNoData(ui_statusSocValLabel, NULL, false); setWarnColor(ui_statusSocValLabel, warn); }
            }
            if (ui_statusSocTitleLabel) lv_label_set_text_fmt(ui_statusSocTitleLabel, "%s SOC", battIcon);
            if (ui_batterySocArc) lv_arc_set_value(ui_batterySocArc, soc);
            if (ui_batterySocValLabel) {
                lv_label_set_text_fmt(ui_batterySocValLabel, "%d", soc);  // no "%" - see ui_batteryScreen.c arc-containment comment
                if (noData) ui_card_setValueNoData(ui_batterySocValLabel, NULL, true);
                else { ui_card_setValueNoData(ui_batterySocValLabel, NULL, false); setWarnColor(ui_batterySocValLabel, warn); }
            }
            if (ui_batterySocIconLabel) {
                lv_label_set_text(ui_batterySocIconLabel, battIcon);
                if (!noData) setWarnColor(ui_batterySocIconLabel, warn);
            }
            // Charging screen's SOC is a drawn battery shape, not an arc -
            // ui_chargingScreen_setSoc() handles the label text/color AND
            // the animated fill tween in one call (no-op if the screen
            // doesn't exist yet).
            ui_chargingScreen_setSoc(soc, warn);
            // GPS NAV screen's top status bar (component 5) - plain text,
            // same reasoning as everywhere else that isn't cell-level detail.
            if (ui_navSocLabel) lv_label_set_text_fmt(ui_navSocLabel, ICON_BOLT " %d%%", soc);
        }
        if (motTChanged) {
            bool warn = motT >= warningSet.motorTemp;
            if (ui_statusMotorTValLabel) {
                lv_label_set_text_fmt(ui_statusMotorTValLabel, "%d\xC2\xB0" "C", motT);
                setWarnColor(ui_statusMotorTValLabel, warn);
            }
            if (ui_statusMotorTBar) lv_bar_set_value(ui_statusMotorTBar, motT, LV_ANIM_ON);
        }
        if (hsTChanged) {
            // Status screen's "Inverter Temp" panel - heatsinkTemp is the
            // inverter's heatsink temperature.
            bool warn = hsT >= warningSet.heatsinkTemp;
            if (ui_statusInvTBar) lv_bar_set_value(ui_statusInvTBar, hsT, LV_ANIM_ON);
            if (ui_statusInvTValLabel) {
                lv_label_set_text_fmt(ui_statusInvTValLabel, "%d\xC2\xB0" "C", hsT);
                setWarnColor(ui_statusInvTValLabel, warn);
            }
        }
        if (voltChanged) {
            // Same no-data gate as SOC above, same bug (0 <= packVLow
            // default 280.0 was true at boot before any real frame).
            bool noData = !ui_can_freshness_hasEverReceived();
            bool warn = !noData && (volt <= warningSet.packVLow);
            if (ui_statusPackVBar) lv_bar_set_value(ui_statusPackVBar, (int)volt, LV_ANIM_ON);
            if (ui_statusPackVValLabel) {
                lv_label_set_text_fmt(ui_statusPackVValLabel, "%.1f V", volt);
                if (noData) ui_card_setValueNoData(ui_statusPackVValLabel, NULL, true);
                else { ui_card_setValueNoData(ui_statusPackVValLabel, NULL, false); setWarnColor(ui_statusPackVValLabel, warn); }
            }
        }
        if (auxVChanged) {
            if (ui_statusAuxVBar) lv_bar_set_value(ui_statusAuxVBar, (int)auxV, LV_ANIM_ON);
            if (ui_statusAuxVValLabel) {
                lv_label_set_text_fmt(ui_statusAuxVValLabel, "%.1f V", auxV);
                setWarnColor(ui_statusAuxVValLabel, false);
            }
            // Low-12V shutdown sequence (styling pass Phase 6, item 12-13) -
            // driven off this same dirty-checked aux12V value, no separate
            // polling path. Gated on real CAN data having arrived at least
            // once - same false-positive-at-boot bug class as items 3-4
            // (SOC/Pack Voltage): aux12vVoltage is zero-initialized, and
            // 0.0 < 11.5 is true before any real frame, which would
            // otherwise show the shutdown warning immediately at boot. See
            // ui_shutdown.h for the full state machine.
            //
            // Also gated on aux12vEverReceived specifically (found on real
            // hardware, 2026-09-19: a bench ZombieVerter with no U12V
            // measurement configured aborts every SDO read of PARAM_ID_U12V
            // forever, while every other param responds normally - so
            // ui_can_freshness_hasEverReceived() alone goes true almost
            // immediately, but aux12vVoltage never leaves its 0.0 default,
            // permanently satisfying 0.0 < AUX12V_LOW_V and forcing the
            // shutdown sequence on every bench run). Both flags must be true:
            // the dash has heard from the VCU at all, AND U12V specifically
            // has ever come back as a real reading - not an abort.
            if (ui_can_freshness_hasEverReceived() && aux12vEverReceived) ui_shutdown_notifyAux12V(auxV);
        }
        if (gearChanged) {
            if (ui_driveGearValLabel) lv_label_set_text(ui_driveGearValLabel, gearText(gear));
        }
        if (motModeChanged) {
            if (ui_driveMotorModeValLabel) lv_label_set_text(ui_driveMotorModeValLabel, motorModeText(motActive));
        }
        if (regenChanged) {
            if (ui_driveRegenBar) lv_bar_set_value(ui_driveRegenBar, (int)regen, LV_ANIM_ON);
            if (ui_driveRegenValLabel) {
                lv_label_set_text_fmt(ui_driveRegenValLabel, "%.0f", regen);
                setWarnColor(ui_driveRegenValLabel, false);
            }
        }
        if (vMaxChanged) {
            // Cell voltage bars carry centivolts (value*100) for a bit of
            // resolution out of an integer lv_bar range - see the
            // placeholder ranges in ui_batteryScreen.c.
            if (ui_batteryVMaxBar) lv_bar_set_value(ui_batteryVMaxBar, (int)(cellVMax * 100), LV_ANIM_ON);
            if (ui_batteryVMaxValLabel) {
                lv_label_set_text_fmt(ui_batteryVMaxValLabel, "%.2f V", cellVMax);
                setWarnColor(ui_batteryVMaxValLabel, false);
            }
        }
        if (vMinChanged) {
            if (ui_batteryVMinBar) lv_bar_set_value(ui_batteryVMinBar, (int)(cellVMin * 100), LV_ANIM_ON);
            if (ui_batteryVMinValLabel) {
                lv_label_set_text_fmt(ui_batteryVMinValLabel, "%.2f V", cellVMin);
                setWarnColor(ui_batteryVMinValLabel, false);
            }
        }
        if (deltaVChanged) {
            // Computed, not a separate SDO param - see zombie_updaters.h.
            // cellVMax/cellVMin were snapshotted together under dataMutex
            // above, so this is never torn between an old and a new value.
            float deltaV = cellVMax - cellVMin;
            if (ui_batteryDeltaVBar) lv_bar_set_value(ui_batteryDeltaVBar, (int)(deltaV * 100), LV_ANIM_ON);
            if (ui_batteryDeltaVValLabel) {
                lv_label_set_text_fmt(ui_batteryDeltaVValLabel, "%.2f V", deltaV);
                setWarnColor(ui_batteryDeltaVValLabel, false);
            }
        }
        if (tMaxChanged) {
            if (ui_batteryTMaxBar) lv_bar_set_value(ui_batteryTMaxBar, cellTMax, LV_ANIM_ON);
            if (ui_batteryTMaxValLabel) {
                lv_label_set_text_fmt(ui_batteryTMaxValLabel, "%d\xC2\xB0" "C", cellTMax);
                setWarnColor(ui_batteryTMaxValLabel, false);
            }
            if (ui_statusMaxBattTBar) lv_bar_set_value(ui_statusMaxBattTBar, cellTMax, LV_ANIM_ON);
            if (ui_statusMaxBattTValLabel) {
                lv_label_set_text_fmt(ui_statusMaxBattTValLabel, "%d\xC2\xB0" "C", cellTMax);
                setWarnColor(ui_statusMaxBattTValLabel, false);
            }
        }
        // Charging screen's bars animate into their new value (LV_ANIM_ON -
        // lv_bar's own native tween, design-review "motion" pass 2026-09-14,
        // see waveshare-dash-build.md) rather than snapping like every
        // other screen's bars still do (this was deliberately prototyped on
        // one screen first, not rolled out everywhere at once).
        if (setpointChanged) {
            if (ui_chargingSetpointBar) lv_bar_set_value(ui_chargingSetpointBar, (int)setpointV, LV_ANIM_ON);
            if (ui_chargingSetpointValLabel) {
                lv_label_set_text_fmt(ui_chargingSetpointValLabel, "%.1f", setpointV);
                setWarnColor(ui_chargingSetpointValLabel, false);
            }
        }
        if (chgTempChanged) {
            if (ui_chargingTempBar) lv_bar_set_value(ui_chargingTempBar, chgTempV, LV_ANIM_ON);
            if (ui_chargingTempValLabel) {
                lv_label_set_text_fmt(ui_chargingTempValLabel, "%d", chgTempV);
                setWarnColor(ui_chargingTempValLabel, false);
            }
        }
        if (acVChanged) {
            if (ui_chargingAcVBar) lv_bar_set_value(ui_chargingAcVBar, (int)acV, LV_ANIM_ON);
            if (ui_chargingAcVValLabel) {
                // Styling pass item 18: was "%.0f" - a voltage is a
                // voltage regardless of screen, same 1-decimal precision
                // as pack/aux voltage (ui_statusPackVValLabel/
                // ui_statusAuxVValLabel above). Setpoint voltage and
                // charger temp (just below) were also flagged for a unit-
                // suffix change by the original audit, but both already
                // have their own separate unit label via
                // createValueAndUnit()'s unitText param ("V"/"\xC2\xB0C") -
                // embedding the unit into the value text too would show it
                // twice, not fix an inconsistency, so those two are left
                // as-is; this field's actual issue (precision, not a
                // missing unit) is the one real fix here.
                lv_label_set_text_fmt(ui_chargingAcVValLabel, "%.1f", acV);
                setWarnColor(ui_chargingAcVValLabel, false);
            }
        }
        if (limChanged) {
            if (ui_chargingLimBar) lv_bar_set_value(ui_chargingLimBar, (int)pilotLim, LV_ANIM_ON);
            if (ui_chargingLimValLabel) {
                lv_label_set_text_fmt(ui_chargingLimValLabel, "%.0f", pilotLim);
                setWarnColor(ui_chargingLimValLabel, false);
            }
            if (ui_chargingCableLimLabel) {
                lv_label_set_text_fmt(ui_chargingCableLimLabel, "Cable: %.0fA", cableLim);
            }
        }
        if (chgStatusChanged) {
            ui_chargingScreen_setStatus(opmode, chgType, plugDet);  // no-op if the screen doesn't exist yet
        }
        if (gpsStatusChanged) {
            // Styling pass: pill + sats label now go through
            // ui_gpsScreen_setStatus() (ui_status_pill.h migration) instead
            // of this file poking the pill/label directly - no-op if the
            // screen doesn't exist yet, same convention as every other
            // screen's own setter.
            ui_gpsScreen_setStatus(gpsHasFix, gpsSats);
            if (ui_navStatusLabel) {
                lv_label_set_text_fmt(ui_navStatusLabel, gpsHasFix ? LV_SYMBOL_GPS " FIX - %d sats" : "NO FIX", gpsSats);
            }
        }
        if (gpsSpeedChanged) {
            // Display-only kph->mph conversion, same rule/constant as the
            // main Speed screen (ui_speedScreen.h) - gpsData.speedKph itself
            // stays kph, since GPS NAV's ETA math (updateTurnGuidance())
            // does real distance/time physics on it in kph/km.
            float gpsSpeedMph = gpsSpeed * KPH_TO_MPH;
            if (ui_gpsSpeedBar) lv_bar_set_value(ui_gpsSpeedBar, (int)gpsSpeedMph, LV_ANIM_ON);
            if (ui_gpsSpeedValLabel) {
                lv_label_set_text_fmt(ui_gpsSpeedValLabel, "%.0f", gpsSpeedMph);
                setWarnColor(ui_gpsSpeedValLabel, false);
            }
            if (ui_navSpeedLabel) lv_label_set_text_fmt(ui_navSpeedLabel, ICON_SPEED " %.0f mph", gpsSpeedMph);
        }
        if (gpsLatLonChanged) {
            if (ui_gpsLatValLabel) {
                lv_label_set_text_fmt(ui_gpsLatValLabel, "%.4f\xC2\xB0 %s", fabs(gpsLat), gpsLat >= 0 ? "N" : "S");
            }
            if (ui_gpsLonValLabel) {
                lv_label_set_text_fmt(ui_gpsLonValLabel, "%.4f\xC2\xB0 %s", fabs(gpsLon), gpsLon >= 0 ? "E" : "W");
            }
            // GPS NAV breadcrumb trail - reuses this same dirty-check
            // rather than a separate one (a new trail point only ever
            // makes sense when the position actually moved). No-ops
            // internally if ui_navScreen hasn't been created yet.
            ui_navScreen_addPoint(gpsLat, gpsLon);
            // Destinations screen's Home/Work distance labels + nearest-
            // charging-station row - same reasoning, no-ops internally if
            // ui_destinationsScreen hasn't been created yet.
            ui_destinationsScreen_updateDistances(gpsLat, gpsLon);
        }
        if (gpsHeadingChanged) {
            if (ui_gpsHeadingBar) lv_bar_set_value(ui_gpsHeadingBar, (int)gpsHeading, LV_ANIM_ON);
            if (ui_gpsHeadingValLabel) {
                lv_label_set_text_fmt(ui_gpsHeadingValLabel, "%.0f", gpsHeading);
                setWarnColor(ui_gpsHeadingValLabel, false);
            }
            if (ui_navHeadingLabel) lv_label_set_text_fmt(ui_navHeadingLabel, ICON_NAVIGATION " %.0f\xC2\xB0", gpsHeading);
        }
        if (gpsAltChanged) {
            if (ui_gpsAltBar) lv_bar_set_value(ui_gpsAltBar, (int)gpsAlt, LV_ANIM_ON);
            if (ui_gpsAltValLabel) {
                lv_label_set_text_fmt(ui_gpsAltValLabel, "%.0f", gpsAlt);
                setWarnColor(ui_gpsAltValLabel, false);
            }
        }
        if (clockChanged) {
            refreshClockDisplay();
        }
        xSemaphoreGive(uiMutex);
    }
}

// --- Settings persistence --------------------------------------------------
bool lowVoltageShutdownEnabled = true;

void getWarningsSet() {
    preferences.begin("warn", true);
    warningSet.lowSoc       = preferences.getInt("lowSoc", DEF_WARN_LOW_SOC);
    warningSet.motorTemp    = preferences.getInt("motorT", DEF_WARN_MOTOR_TEMP);
    warningSet.heatsinkTemp = preferences.getInt("hsT", DEF_WARN_HEATSINK_TEMP);
    warningSet.packVLow     = preferences.getFloat("packVLo", DEF_WARN_PACK_V_LOW);
    // Same "warn" namespace, separate key - see zombie_updaters.h's comment
    // on lowVoltageShutdownEnabled for why this exists and why it's read
    // here (at boot, alongside the other warning settings) but written by
    // its own setLowVoltageShutdownEnabled() below instead of
    // updateWarningsSet() (applies+persists immediately, not Save-gated).
    lowVoltageShutdownEnabled = preferences.getBool("lo12vEn", true);
    preferences.end();
}

void updateWarningsSet() {
    preferences.begin("warn", false);
    preferences.putInt("lowSoc", warningSet.lowSoc);
    preferences.putInt("motorT", warningSet.motorTemp);
    preferences.putInt("hsT", warningSet.heatsinkTemp);
    preferences.putFloat("packVLo", warningSet.packVLow);
    preferences.end();
}

void setLowVoltageShutdownEnabled(bool enabled) {
    lowVoltageShutdownEnabled = enabled;
    preferences.begin("warn", false);
    preferences.putBool("lo12vEn", enabled);
    preferences.end();
}

void setDefaultWarnSet() {
    warningSet.lowSoc       = DEF_WARN_LOW_SOC;
    warningSet.motorTemp    = DEF_WARN_MOTOR_TEMP;
    warningSet.heatsinkTemp = DEF_WARN_HEATSINK_TEMP;
    warningSet.packVLow     = DEF_WARN_PACK_V_LOW;
    updateWarningsSet();
}

display_pref_t displayPref = DISPLAY_PREF_AUTO;

// Manual UTC display offset in whole hours (2026-09-22) - e.g. -8 for PST,
// -7 for PDT. Purely a DISPLAY-time shift applied to the clock/ETA labels
// below; gpsData.hour/minute/second themselves stay true UTC always, and
// resolveDisplayPref()'s sunrise/sunset math further down this file reads
// gpsData directly (not through this offset) since that calculation is
// astronomically defined in UTC and would give wrong sunrise/sunset times
// if shifted. No automatic DST handling - the user flips this by 1 twice a
// year, same as any dash clock with no RTC/timezone database.
int utcOffsetHours = -8;

void refreshClockDisplay(void) {
    uint8_t hour = 0, minute = 0;
    bool hasFix = false;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        hour = gpsData.hour;
        minute = gpsData.minute;
        hasFix = gpsData.hasFix;
        xSemaphoreGive(dataMutex);
    }
    // +24/%24 keeps the result in [0,24) regardless of sign, so a negative
    // offset near midnight UTC wraps to the previous/next day's hour
    // correctly (minutes are never affected - every real-world offset this
    // project supports is whole-hour).
    int localHour = ((int)hour + utcOffsetHours % 24 + 24) % 24;
    char tzCaption[24];
    snprintf(tzCaption, sizeof(tzCaption), "UTC%+d", utcOffsetHours);

    if (ui_splashClockLabel) {
        if (hasFix) lv_label_set_text_fmt(ui_splashClockLabel, "%02d:%02d", localHour, minute);
        else lv_label_set_text(ui_splashClockLabel, "--:--");
    }
    if (ui_splashClockCaptionLabel) {
        if (hasFix) lv_label_set_text(ui_splashClockCaptionLabel, tzCaption);
        else lv_label_set_text_fmt(ui_splashClockCaptionLabel, "%s - waiting for GPS fix", tzCaption);
    }
    if (ui_navClockLabel) {
        if (hasFix) lv_label_set_text_fmt(ui_navClockLabel, "%02d:%02d", localHour, minute);
        else lv_label_set_text(ui_navClockLabel, "--:--");
    }
}

// --- Day/night Auto-mode resolution (GPS-based, 2026-09-14) ----------------
// Cooper (1969) solar declination approximation - deliberately the simple
// formula, not the precise Julian-Day astronomical method. This only needs
// to pick between two backlight PALETTES, not navigate a ship - the ~1-
// degree/few-minute accuracy Cooper's approximation gives is far more than
// enough, and it's simple enough to verify by hand (checked against a known
// reference: solar noon in New York, ~74W, lands at ~11:56am local with
// this formula - confirms the longitude sign convention below is right,
// rather than trusting it from memory).
//
// Longitude convention: standard signed decimal degrees, EAST positive -
// matches TinyGPSPlus's gps.location.lng() (gpsData.longitude).

static int dayOfYear(int year, int month, int day) {
    static const int cumDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    int doy = cumDays[month - 1] + day;
    if (leap && month > 2) doy++;
    return doy;
}

// True if it's night at (latDeg, lonDeg) at the given UTC date/time.
static bool computeIsNight(double latDeg, double lonDeg, int year, int month, int day,
                            int utcHour, int utcMinute, int utcSecond) {
    int N = dayOfYear(year, month, day);
    const double deg2rad = M_PI / 180.0;

    // Solar declination: delta = 23.45 * sin(360/365 * (284+N)) degrees.
    double declRad = 23.45 * deg2rad * sin((2.0 * M_PI / 365.0) * (284 + N));
    double latRad = latDeg * deg2rad;

    // Hour angle at sunrise/sunset - plain geometric horizon (no atmospheric
    // refraction correction; a few minutes of precision doesn't matter for a
    // palette switch): cos(H0) = -tan(lat) * tan(decl).
    double cosH0 = -tan(latRad) * tan(declRad);
    if (cosH0 <= -1.0) return false;  // midnight sun - always day at this latitude/date
    if (cosH0 >= 1.0)  return true;   // polar night - always night at this latitude/date
    double H0Deg = acos(cosH0) / deg2rad;

    // Local mean solar time (hours) - working in this frame avoids UTC
    // day-wraparound edge cases entirely.
    double utcHours = utcHour + utcMinute / 60.0 + utcSecond / 3600.0;
    double localSolarHours = fmod(utcHours + lonDeg / 15.0, 24.0);
    if (localSolarHours < 0) localSolarHours += 24.0;

    double sunriseHours = 12.0 - H0Deg / 15.0;
    double sunsetHours  = 12.0 + H0Deg / 15.0;

    return (localSolarHours < sunriseHours) || (localSolarHours > sunsetHours);
}

// The one place Auto mode's resolution happens. Uses GPS position + UTC
// date/time (gps_driver.h) once a real fix has been seen; gpsData.year
// defaults to 0 (well below any real year) until TinyGPSPlus parses a valid
// $GPRMC/$GPGGA sentence, so ">= 2020" is a safe, simple validity check on
// what could otherwise be an all-zero, never-populated struct. Falls back
// to the old hardcoded "Auto = Day" behavior before that - same safe
// default as when this was a TODO stub with no GPS driver at all.
static ui_theme_mode_t resolveDisplayPref(display_pref_t pref) {
    switch (pref) {
        case DISPLAY_PREF_NIGHT: return UI_THEME_NIGHT;
        case DISPLAY_PREF_DAY:   return UI_THEME_DAY;
        case DISPLAY_PREF_AUTO:
        default: {
            double lat = 0, lon = 0;
            int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
                lat = gpsData.latitude;   lon = gpsData.longitude;
                year = gpsData.year;      month = gpsData.month;  day = gpsData.day;
                hour = gpsData.hour;      minute = gpsData.minute; second = gpsData.second;
                xSemaphoreGive(dataMutex);
            }
            if (year < 2020) return UI_THEME_DAY;  // no GPS fix yet
            return computeIsNight(lat, lon, year, month, day, hour, minute, second) ? UI_THEME_NIGHT : UI_THEME_DAY;
        }
    }
}

// Runs every PERIOD_AUTO_THEME_MS. A no-op unless displayPref is AUTO, and
// only actually touches LVGL (via ui_theme_set(), which unconditionally
// repaints every existing screen's static chrome - see ui_theme.cpp) when
// the resolved palette has genuinely changed, i.e. once per sunrise/sunset
// crossing - not every tick.
static void checkAutoTheme() {
    if (displayPref != DISPLAY_PREF_AUTO) return;
    ui_theme_mode_t resolved = resolveDisplayPref(DISPLAY_PREF_AUTO);
    if (resolved == ui_theme_get()) return;
    if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
        ui_theme_set(resolved);
        xSemaphoreGive(uiMutex);
    }
}

void applyDisplayMode() {
    ui_theme_set(resolveDisplayPref(displayPref));
}

void getDisplayMode() {
    preferences.begin("disp", true);
    int pref = preferences.getInt("pref", DISPLAY_PREF_AUTO);
    uint8_t dayBright = preferences.getUChar("dayBright", 220);
    uint8_t nightBright = preferences.getUChar("nightBright", 60);
    utcOffsetHours = preferences.getInt("utcOff", -8);
    preferences.end();

    displayPref = (display_pref_t)pref;
    ui_theme_load_brightness(dayBright, nightBright);
    applyDisplayMode();
}

void updateDisplayMode() {
    preferences.begin("disp", false);
    preferences.putInt("pref", (int)displayPref);
    preferences.putUChar("dayBright", ui_theme_get_day_brightness());
    preferences.putUChar("nightBright", ui_theme_get_night_brightness());
    preferences.putInt("utcOff", utcOffsetHours);
    preferences.end();
}
