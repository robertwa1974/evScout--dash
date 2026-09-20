#pragma once

#include "ui.h"
#include "config.h"
#include "twai_lib.h"
#include "can_sdo.h"
#include "can_broadcast.h"
#include "zombieverter_params.h"
#include "mutex.h"
#include <Ticker.h>
#include <Preferences.h>
#include "build_info.h"

#define PERIOD_FAST_MS      20
#define PERIOD_MID_MS       200
#define PERIOD_SLOW_MS      800
#define PERIOD_SDO_POLL_MS  50   // 25-entry pollTable -> ~1250ms full refresh
#define PERIOD_AUTO_THEME_MS 60000  // how often Auto mode re-checks GPS-based day/night (a sunrise/sunset crossing doesn't need finer granularity than this)

// ============================================================================
// ALL telemetry is polled via SDO (decision 2026-09-13). The original design
// split telemetry into passive CAN-broadcast (decodeBroadcastFrame(), fed by
// the VCU's canmap) plus a small SDO poll table for the handful of params
// with no broadcast frame. On bench validation the spare ZombieVerter turned
// out to have NO canmap configured at all - confirmed on both this firmware
// and Rob's own working M5 Dial, whose bus traffic was 0x603/0x583 only, no
// broadcast IDs whatsoever. Since CANopen SDO reaches the object dictionary
// directly regardless of canmap, all 10 formerly-broadcast-only params were
// promoted into the poll table below alongside the original 4.
//
// kCanBroadcastParams / decodeBroadcastFrame() are left in place, dormant -
// if canmap ever gets configured on a VCU, broadcast decode "just works"
// again in parallel with these SDO polls with no code changes needed.
//
// Confirmed on real hardware (2026-09-13, see firmware/can-bench.md):
//   - SDO's blanket /32 fixed-point scale (OI_FIXED_POINT_SCALE) is correct
//     across the board - tunables (revlim raw 192000 -> 6000rpm, BattCap raw
//     1152 -> 36kWh), enums (Gear raw 32 -> enum 1/HIGH), AND 2000+ telemetry
//     (SOC raw 3200 -> 100%) all came back as clean round numbers post-/32.
//     No special-casing needed for opmode/lasterr after all.
//   - tmpm read a real negative value (-27.7 degC, open-thermistor reading
//     with no motor attached) confirming it must be treated as signed.
// ============================================================================
#define PARAM_ID_VEH_SPEED   2017  // kph - real vehicle speed, not motor rpm
#define PARAM_ID_OPMODE      2002  // 0=Off,1=Run,2=Precharge,3=PchFail,4=Charge
#define PARAM_ID_POWER       2011  // kW
#define PARAM_ID_LASTERR     2004  // fault code enum

// Promoted from kCanBroadcastParams (see comment above)
#define PARAM_ID_SOC         2015  // %
#define PARAM_ID_UDC         2006  // pack voltage, V
#define PARAM_ID_IDC         2012  // pack current, A (signed - regen/charge)
#define PARAM_ID_TMPM        2029  // motor temp, degC (signed - confirmed negative on bench)
#define PARAM_ID_TMPHS       2028  // heatsink temp, degC (signed)
#define PARAM_ID_U12V        2070  // 12V aux rail, V
#define PARAM_ID_MOTOR_SPEED 2016  // motor rpm - NOT vehicle speed (signed - reverse)
#define PARAM_ID_GEAR        27    // 0=LOW,1=HIGH,2=AUTO,3=HIGHFWDLOWREV
#define PARAM_ID_MOT_ACTIVE  129   // 0=Mg1and2,1=Mg1,2=Mg2,3=BlendingMG2and1
#define PARAM_ID_REGENMAX    61    // %

// Added 2026-09-14 for the Speed/Drive/Status/Battery screen split.
#define PARAM_ID_DIR         2024  // -1=Reverse,0=Neutral,1=Drive,2=Park - the P/N/D/R
                                    // shifter-position indicator, distinct from PARAM_ID_GEAR
                                    // (which is ZombieVerter's own LOW/HIGH/AUTO reduction-gear setting)
#define PARAM_ID_BMS_VMAX    2085  // V - highest individual cell voltage, as relayed by the BMS to the VCU
#define PARAM_ID_BMS_VMIN    2084  // V - lowest individual cell voltage, ditto
#define PARAM_ID_BMS_TMAX    2087  // degC - highest individual cell temperature, ditto
// Cell delta-V is computed (cellVMax - cellVMin), not a separate SDO param.

// Added 2026-09-14 for the Charging screen. "Charge setpoint" is Voltspnt
// (40) per Rob, not any of the CCS_*/BMS_Charge* candidates considered -
// this VCU charges to a target pack voltage, not a target SOC%.
#define PARAM_ID_VOLTSPNT    40    // V - charge target/setpoint voltage (a tunable, not 2000+ telemetry, but SDO reads work identically across the whole ID range - see can-bench.md)
#define PARAM_ID_CHGTEMP     2078  // degC - charger temperature
#define PARAM_ID_CHGTYP      2003  // 0=Off,1=AC,2=DCFC
#define PARAM_ID_PLUGDET     2050  // 0=Off,1=On,2=na - charge plug/cable connected
#define PARAM_ID_AC_VOLTS    2079  // V - AC supply voltage actually present at the charger
#define PARAM_ID_PILOTLIM    2049  // A - control-pilot (CP) current limit: what the EVSE is currently offering
#define PARAM_ID_CABLELIM    2048  // A - proximity-pilot (PP) current limit: the cable's rated capacity (the closest thing to a raw "PPVal" this VCU exposes - there's no raw PP resistance/voltage telemetry, only these two derived current limits)

// Default warning thresholds - EV equivalents of rusEFI's oil/coolant/rpm limits
#define DEF_WARN_LOW_SOC        15      // percent
#define DEF_WARN_MOTOR_TEMP     140     // deg C (tmpm)
#define DEF_WARN_HEATSINK_TEMP  85      // deg C (tmphs)
#define DEF_WARN_PACK_V_LOW     280.0   // volts - TODO tune to your pack

// --- Live telemetry ---------------------------------------------------------
// All fields are filled by the SDO poll cycle (see the pollTable comment in
// zombie_updaters.cpp). Marked [broadcast-capable] where decodeBroadcastFrame()
// would also feed the same field if canmap is ever configured on the VCU.
typedef struct struct_message {
    // [broadcast-capable]
    int   motorRpm;        // "speed" param - motor rpm, not vehicle speed
    float packVoltage;      // udc
    float packCurrent;      // idc
    int   motorTemp;        // tmpm
    int   heatsinkTemp;     // tmphs
    float aux12vVoltage;    // U12V
    int   soc;
    int   gear;
    int   motActive;
    float regenMax;

    // [SDO poll only - no broadcast equivalent]
    int   vehSpeedKph;      // Veh_Speed - actual vehicle speed
    int   opmode;
    float powerKw;
    int   lastErr;
    int   dirState;         // dir - P/N/D/R shifter position, NOT the same as gear (see PARAM_ID_DIR)
    float cellVMax;         // BMS_Vmax - highest individual cell voltage
    float cellVMin;         // BMS_Vmin - lowest individual cell voltage
    int   cellTMax;         // BMS_Tmax - highest individual cell temperature
    float chargeSetpointV;  // Voltspnt - charge target/setpoint voltage
    int   chgTemp;          // ChgTemp - charger temperature, degC (signed - same rationale as motorTemp/heatsinkTemp)
    int   chgType;          // chgtyp - 0=Off,1=AC,2=DCFC
    int   plugDet;          // PlugDet - 0=Off,1=On,2=na (charge plug/cable connected)
    float acVolts;          // AC_Volts - AC supply voltage at the charger
    float pilotLimA;        // PilotLim - CP-derived current limit (what the EVSE is offering)
    float cableLimA;        // CableLim - PP-derived current limit (the cable's rated capacity)

    // [custom, not in ZombieVerter's own object dictionary]
    int   dcdcState;        // your existing 5-state machine, from the 0x377 frame decode
} struct_message;

typedef struct warning_set {
    int   lowSoc;
    int   motorTemp;
    int   heatsinkTemp;
    float packVLow;
} warning_set;

extern struct_message myData;
extern struct_message old_myData;
extern warning_set warningSet;

// Low-12V shutdown on/off (Settings screen, 2026-09-19) - defaults true.
// Added because a bench ZombieVerter with no U12V sensor wired legitimately
// reports ~0V on that channel (a valid SDO response, not an abort/no-data
// condition - see ui_shutdown.h/.cpp), which is indistinguishable at the
// protocol level from a real dying 12V battery. There is no way to tell
// these apart from CAN data alone, so this is a manual override: turn it
// off on hardware with no working U12V sensor, leave it on for any vehicle
// that has one wired. Applies and persists immediately when changed (same
// "no separate Save step" pattern as displayPref below), not gated behind
// the Warning Thresholds section's Save button.
extern bool lowVoltageShutdownEnabled;
void setLowVoltageShutdownEnabled(bool enabled);

// Bumped every time a screen is (re)created (see each ui_*Screen_screen_init()
// - call ui_notify_screen_created() at the end of any screen that binds
// myData to widgets). fastUpdate/midUpdate/slowUpdate each remember the last
// generation they pushed at; when this counter has moved on, they do one
// full unconditional push of every field to every currently-existing widget
// before returning to the normal dirty-check-only behavior.
//
// Why this exists (found 2026-09-14 on the bench): the dirty-check pattern
// only pushes a value when it *changes* from the previous poll. That's fine
// for a screen that already exists, but Drive/Status/Battery are created
// lazily, well after old_myData has already synced to whatever's on the
// bus. On a bench with no contactors closed, most fields are genuinely
// static (0 A, 0 V, motActive=0, dirState=0) - so a freshly-created screen's
// widgets sat at their init placeholders ("--", "0") forever, since the
// field they're bound to had already stopped changing before the screen was
// created. Confirmed via the cantrace build: SDO was reading motActive=0
// (a real, valid MG1+MG2 reading) correctly the whole time - it just never
// reached the Drive screen's freshly-created label.
extern volatile uint32_t uiGeneration;
void ui_notify_screen_created(void);

// --- Tasks -------------------------------------------------------------------
void TaskCANReceiver(void *pvParameters);

// --- Widget updaters -----------------------------------------------------
void fastUpdate();
void midUpdate();
void slowUpdate();

// --- Settings persistence -----------------------------------------------
void getWarningsSet();
void updateWarningsSet();
void setDefaultWarnSet();

// Day/night display mode - same NVS-via-Preferences pattern as the warning
// thresholds above, separate namespace ("disp") since it's a display
// preference, not a telemetry warning threshold. Unlike warningSet there's
// no separate "save" step for any of this - the settings screen applies and
// persists immediately on every change (day/night, Auto, brightness), same
// as CLAUDE.md specifies ("don't wait for Save to show the visual change").
//
// Three-state PREFERENCE (what the user picked) vs the two-state PALETTE
// that's actually rendered (ui_theme_mode_t, UI_THEME_DAY/UI_THEME_NIGHT):
// DISPLAY_PREF_AUTO has to resolve to one of the two real palettes somehow.
// There is currently no RTC and no light sensor on this board (see
// scout80-dash-architecture.md's hardware list) - resolveDisplayPref() is
// the one place that decision gets made, and today it's a hardcoded "Auto
// behaves as Day" TODO stub pending real hardware. Wire a light sensor or a
// time source in there when one exists; nothing else needs to change.
typedef enum {
    DISPLAY_PREF_AUTO,
    DISPLAY_PREF_DAY,
    DISPLAY_PREF_NIGHT,
} display_pref_t;

extern display_pref_t displayPref;

// Loads displayPref + both per-mode brightness levels from NVS, resolves
// Auto if needed, and applies via ui_theme_set()/ui_theme_load_brightness().
// Called once at boot (firmware.ino setup(), before ui_init() creates any
// screen) so the very first screen already renders correctly, no boot flash.
void getDisplayMode();

// Re-resolves displayPref (a no-op unless it's AUTO) and re-applies via
// ui_theme_set() - call this after changing displayPref, and it's also the
// hook a future periodic "check the light sensor" task would call.
void applyDisplayMode();

// Persists the current displayPref + both per-mode brightness levels to NVS.
// Call right after applyDisplayMode()/ui_theme_set_brightness() - see the
// settings screen's day/night buttons and brightness slider for the pattern.
void updateDisplayMode();
