# Scout 80 Dash User Manual

*Last updated: 2026-09-22*

This is the everyday operating guide for the Scout 80's dashboard: the thirteen screens, how to move between them, the 0-60 dyno mode, and the on-board Settings and WiFi configuration tools.

## Getting started

The dash boots to a splash screen with the Scout logo and a UTC clock, then moves on to the Speed screen automatically.

**The bottom dock.** Every screen except the splash has a row of 5 icons pinned to the bottom: **Telemetry, BMS (battery), GPS, Dyno, Settings**. Tap any icon to jump straight to that group. The highlighted icon shows which group you're currently in.

**Swiping between screens.** Several screens are grouped together and reached by swiping left/right within a group, not through the dock:
- Telemetry group: Speed → Drive → Status → Charging
- GPS group: GPS → GPS NAV → Destinations
- BMS group: Battery only
- Dyno group: 0-60 LIVE → RESULTS (RESULTS opens automatically when a run finishes)

The dock always jumps to that group's *home* screen (Speed, Battery, GPS, Dyno LIVE, or Settings) - swipe from there to reach the other screens in that group.

**Brightness.** Swipe up or down on most screens to adjust screen brightness directly, without opening Settings.

## Telemetry screens

| Screen | What it shows |
| --- | --- |
| **Speed** (home) | Full-screen speedometer in mph, plus the gear position (P/N/F/R) below it. This is the default screen and where every other screen's back-navigation returns to. |
| **Drive** | Power, pack current, gear selection, motor mode, and regen limit - the things worth watching while actually driving. |
| **Status** | State-of-charge, pack voltage, 12V aux voltage, and motor/inverter/battery temperatures - the vehicle's vitals at a glance. |
| **Battery** | Cell-level detail: state-of-charge ring, charging/discharging/idle status, and max/min cell voltage, cell voltage spread, and max cell temperature. |
| **Charging** | Only relevant while plugged in: charge status (AC or DC fast charging), charge voltage target, charger temperature, AC supply voltage, and what current the charger/cable are offering. |

Bars and gauges are color-coded - green/normal, amber/caution, red only for a genuine fault. A gray or dimmed value means the dash hasn't received real data for that field yet (e.g. right after power-on), not that the value is actually zero.

## GPS & navigation

**GPS.** Fix status and satellite count, speed (mph), latitude/longitude, heading, and altitude. "NO FIX" is normal indoors or under heavy cover - a real GPS module needs a clear view of the sky and can take anywhere from 30 seconds to a few minutes to lock on outdoors.

**GPS NAV.** A live map of your recent driving trail. Once a route is set, a status banner near the top shows turn-by-turn distance, or "OFF ROUTE" / "NO ROUTE" when idle. The bottom strip shows fix status, speed, heading, ETA, and distance remaining.

**Destinations.** Swipe right from GPS NAV (or left from the Dyno screens) to reach this screen. It lists:
- **Home** and **Work** - two fixed saved locations
- **Nearest charging stations** - the 5 closest stations to your current position, each showing name, connector type, power rating, and distance, in a scrollable list

Tap any entry to route there - the dash computes a route and switches back to GPS NAV to show turn-by-turn progress. There's no address search; only these preset destinations. Home, Work, and the charging-station data can all be updated from the WiFi Configuration page - see that section below.

## 0-60 dyno mode

Reached via the Dyno dock icon. Two screens:

**LIVE.** Tap anywhere on the screen to arm it - the status pill turns amber and reads "ARMED - go!". The run starts automatically once you actually accelerate, detected two ways (whichever happens first):
- Real wheel speed crossing the launch threshold, or
- A hard acceleration detected by the onboard motion sensor (useful for a quick bench test without driving)

While running you'll see a large elapsed-time counter and current speed in mph. The run ends and the RESULTS screen opens automatically once you reach the target speed (60 mph by default).

**RESULTS.** A chart of two power curves plotted against speed: **road-load power** (the mechanical power actually accelerating the truck, estimated from how fast the recorded speed is climbing) and **electrical power** (the real power drawn from the battery). The gap between the two curves is drivetrain loss - electrical power in is always somewhat more than road-load power out. Peak values for both, plus an overall efficiency percentage (road-load ÷ electrical), are called out above the chart.

Tap the screen after a run completes to reset back to READY for another pull.

## Settings

Reached via the Settings dock icon. Three groups:

**Warning thresholds** - low SOC%, high motor temperature, and low pack voltage cutoffs, each adjusted with large +/- buttons (press and hold to speed up). Changes only take effect after pressing **Save**; the Save button highlights when you have unsaved changes. There's also a **Low 12V Shutdown** toggle here, which applies immediately - turn it off if your vehicle doesn't have a working 12V sensor wired up, since a missing sensor otherwise looks identical to a real dying battery.

**Display** - Day/Night mode (Auto, Day, or Night), a brightness slider, a UTC time offset (e.g. -8 for Pacific Standard Time, -7 during daylight saving - this isn't automatic, flip it by 1 twice a year), and the WiFi Configuration toggle (see next section). All of these apply and save immediately, no Save button needed.

**Calibration** - a placeholder for a future dyno drivetrain-calibration workflow; not yet built.

## WiFi configuration

A local configuration page lets you edit a handful of values from your phone without needing a laptop or a firmware update.

**Turning it on.** Settings → Display → **WiFi Config** toggle. Once on, the status line below it shows the network name and password.

**Connecting.**
1. On your phone, join the WiFi network **Scout80-Config** (password: **Scout80Dash!**)
2. Open a browser and go to **192.168.4.1**

**What you can edit:**

| Field | Notes |
| --- | --- |
| Vehicle mass (lbs) | Include the driver's weight - this is the only physics constant the dyno's power estimate needs. |
| Dyno target speed (mph) | The speed a 0-60 run ends at - 60 mph by default. |
| Dyno launch threshold (mph) | Minimum speed that counts as "launched" for the CAN-speed detection path. |
| Launch sensor sensitivity (g) | How hard a shake/acceleration needs to be to trigger the motion-sensor launch detection. |
| Home / Work coordinates | The two fixed destinations shown on the Destinations screen. |

Press **Save** to apply - values are stored permanently and take effect immediately, no reflash needed.

**Safety note:** the page refuses to load or save changes while the vehicle is moving - you can view it, but not edit, until you've stopped.

This is a local-only connection - the dash never connects to the internet through this. It's just your phone talking directly to the dash, the same as connecting to any other local device's WiFi. Turn the toggle back off when you're done to stop it broadcasting.

## Troubleshooting & tips

| Symptom | What's going on |
| --- | --- |
| GPS says "NO FIX" | Normal indoors or under cover. Needs open sky; can take a few minutes outdoors on a cold start. |
| A value looks grayed-out / dashed | The dash hasn't received real data for that field yet, not an actual zero reading. |
| Charging screen shows nothing while parked | Only populates while actually plugged in and charging. |
| Dyno won't arm/respond to a tap | Make sure you're tapping the screen itself, not mid-swipe. |
| "No charging data loaded" on Destinations | The charging-station list needs to be refreshed occasionally (it's pulled from an online database ahead of time, not live) - ask whoever maintains the dash to regenerate and reload it. Currently only covers the San Diego County area. |
| Screen too dim/bright | Swipe up or down on most screens, or use the brightness slider in Settings. |
| Low-12V warning seems wrong | If your vehicle has no working 12V sensor, turn off Low 12V Shutdown in Settings - otherwise the dash can't tell a missing sensor apart from a real dying battery. |

For anything not covered here, the dash's physical swipe gestures and dock icons are the two ways to get anywhere - if a screen seems stuck, try the dock icon for its group first.
