# Scout 80 Dash User Manual

*Last updated: 2026-10-24 (firmware v2.2.1)*

This is the everyday operating guide for the Scout 80's dashboard: the twelve screens, how to move between them, the 0-60 dyno mode, the on-board Settings tools, and the WiFi hotspot pages (config, live telemetry, log downloads and firmware updates).

## Getting started

The dash boots to a splash screen with the Scout logo and a clock, then moves on to the Speed screen automatically.

**The bottom dock.** Every screen except the splash has a row of 5 icons pinned to the bottom: **Telemetry, BMS (battery), GPS, Dyno, Settings**. Tap any icon to jump straight to that group. The highlighted icon shows which group you're currently in.

**Swiping between screens.** Several screens are grouped together and reached by swiping left/right within a group, not through the dock:
- Telemetry group: Speed → Drive → Incline → Status → Charging
- GPS group: GPS → GPS NAV → Destinations
- BMS group: Battery only
- Dyno group: 0-60 LIVE → RESULTS (RESULTS opens automatically when a run finishes)

The dock always jumps to that group's *home* screen (Speed, Battery, GPS, Dyno LIVE, or Settings). Swipe from there to reach the other screens in that group. The splash/clock screen is reached by swiping from Settings.

**Brightness.** Swipe up or down on most screens to adjust screen brightness directly, without opening Settings.

**Warnings that appear over any screen.**
- A **fault banner** across the top shows an active VCU fault (e.g. "PRECHARGE FAULT"), an over-temperature reading, or lost CAN data.
- If the 12V supply drops below 11.5V, a full-screen **LOW 12V SUPPLY** warning appears and the display then fades out. It cancels itself if the voltage recovers above 12V. Tap it to dismiss it and turn the feature off (re-enable it in Settings).

## Telemetry screens

| Screen | What it shows |
| --- | --- |
| **Speed** (home) | Two-dial cluster. Left: speedometer in mph, with the odometer above and the trip meter below. Right: P R N D position, SOC and inverter-temperature gauges, and **Power**, **Range**, **Pack A** (pack current) and **12V** cards. This is the default screen. |
| **Drive** | Power, pack current, gear selection, motor mode, regen limit and brake regen. The last four are **tap-to-edit** (see below). |
| **Incline** | Pitch and roll in degrees, each drawn as a Scout outline that tilts with the truck. Needs the IMU and its level calibration (see below). |
| **Status** | State-of-charge, pack voltage, 12V aux voltage, and motor/inverter/battery temperatures: the vehicle's vitals at a glance. |
| **Battery** | Cell-level detail: state-of-charge ring, charging/discharging/idle status, max/min cell voltage, cell voltage spread, and max cell temperature. |
| **Charging** | Only relevant while plugged in: charge status (AC or DC fast charging), charge voltage target, charger temperature, AC supply voltage, and what current the charger/cable are offering. |

Bars and gauges are color-coded: green/normal, amber/caution, red only for a genuine fault. A gray or dimmed value means the dash hasn't received real data for that field yet (e.g. right after power-on), not that the value is actually zero.

### Speed, odometer, trip and range

**Speed** is worked out by the dash from motor rpm, because the VCU doesn't report road speed on this vehicle. Until you've done a GPS calibration (Settings → Calibration), the unit label reads **"mph - estimated"**. Estimated speed usually reads a few percent high. Calibrate each range (LOW and HIGH) once and the label changes to plain "mph".

**Odometer and trip.** The VCU has no distance counter, so the dash keeps its own. It's saved every 0.25 mi and a few seconds after you stop, so switching off loses nothing. **Long-press the trip readout to reset it.** The odometer can be corrected from the WiFi config page.

**Efficiency and range.** The TRIP caption shows the trip's energy use in Wh/mi. The **RANGE** card estimates miles to empty: current SOC × the VCU's battery capacity (`BattCap`) ÷ your average Wh/mi over roughly the last 10 miles. It shows a dash until enough distance has been driven to give a meaningful number. Charging time and time parked with the VCU switched on don't count, so they won't skew the figure.

### Incline (pitch and roll)

The left panel shows **pitch** (side view: NOSE UP / NOSE DOWN), the right panel **roll** (seen from behind: LEANING LEFT / LEANING RIGHT). The number turns amber at 75% of the High Pitch / High Roll warning thresholds and red at them; set those in Settings › Warning Thresholds. They're a reminder you choose, not a safe-angle or rollover limit: the IMU reads to roughly ±1–2°, and only you know what your truck and load will tolerate.

- Before first use, level the IMU in **Settings › Calibration › IMU level**, parked on level ground. Until then the screen just asks you to. If the spot wasn't level, every reading is off by that slope.
- While you're moving, the dash subtracts the truck's own acceleration (from road speed) and cornering (speed × turn rate), so speeding up or turning doesn't read as a hill. **UNCORRECTED** at the top of a panel means the truck seems to be moving (turning or accelerating) but there's no road speed to correct with (no CAN and no GPS fix), so the reading can be off until it settles. It stays hidden while parked.
- Expect the most accurate readings at trail speeds. Hard braking or sharp turns at speed can still throw it briefly.

### Editing on the Drive screen

The **Gear**, **Motor Mode**, **Regen Limit** and **Brake Regen** cards have a pencil icon. Tap one to open a full-screen editor. Gear and Motor Mode offer large option buttons. The regen values use big -/+ buttons, and you can hold one to repeat. Changes are sent to the VCU straight away but are **not saved to the VCU's flash**: they last until the VCU restarts, the same as the M5 Dial.

When an edit isn't allowed right now, the buttons are disabled and the reason is shown:
- Regen Limit and Brake Regen can be changed **while driving**, as long as CAN data is live.
- Gear and Motor Mode need the vehicle **stopped** (no wheel or motor movement).

## GPS & navigation

**GPS.** Fix status and satellite count, speed (mph), latitude/longitude, heading, and altitude. "NO FIX" is normal indoors or under heavy cover. A real GPS module needs a clear view of the sky and can take anywhere from 30 seconds to a few minutes to lock on outdoors.

**GPS NAV.** A map of the area around you (offline map data on the SD card) with your recent driving trail. Once a route is set, a banner near the top shows turn-by-turn distance, or "OFF ROUTE"; it reads "SELECT DESTINATION" when no route is set. The bottom strip shows fix status, speed, heading, ETA, and distance remaining.

**Destinations.** Swipe on from GPS NAV to reach this screen. It lists:
- **Home** and **Work**: two fixed saved locations
- **Nearest charging stations**: the 5 closest stations to your current position, each showing name, connector type, power rating, and distance, in a scrollable list

Tap any entry to route there. The dash computes a route and switches back to GPS NAV to show turn-by-turn progress. There's no address search; only these preset destinations. Home and Work can be changed from the WiFi config page. The charging-station list is a file on the SD card and is updated by whoever maintains the dash.

## 0-60 dyno mode

Reached via the Dyno dock icon. Two screens:

**LIVE.** Tap anywhere on the screen to arm it. The status pill turns amber and reads "ARMED - go!". The run starts automatically once you actually accelerate, detected two ways (whichever happens first):
- Road speed crossing the launch threshold, or
- A hard acceleration detected by the onboard motion sensor (useful for a quick bench test without driving)

While running you'll see a large elapsed-time counter and current speed in mph. The run ends and the RESULTS screen opens automatically once you reach the target speed (60 mph by default).

**RESULTS.** A chart of two power curves plotted against speed:
- **Road-load power** is the mechanical power actually accelerating the truck, estimated from how fast the recorded speed is climbing.
- **Electrical power** is the real power drawn from the battery.

The gap between the two curves is drivetrain loss: electrical power in is always somewhat more than road-load power out. Peak values for both, plus an overall efficiency percentage (road-load ÷ electrical), are called out above the chart.

Tap the screen after a run completes to reset back to READY for another pull.

## Settings

Reached via the Settings dock icon. The first page is a list of sections; tap one to open it, and use the back arrow at the top to return. The firmware version is shown on the Settings screen.

### Warning Thresholds

Low SOC (%), high motor temperature, high heatsink (inverter) temperature, low pack voltage, and high pitch / high roll (°, for the Incline screen). Adjust each with the large +/- buttons (press and hold to speed up). Changes only take effect after pressing **Save**; the Save button highlights when you have unsaved changes. **Defaults** restores and saves the factory values straight away.

There's also a **Low 12V Shutdown** toggle here, which applies immediately. Turn it off if your vehicle doesn't have a working 12V sensor wired up, since a missing sensor otherwise looks identical to a real dying battery.

### Display

All of these apply and save immediately, with no Save button:
- **Day / Night**: Auto, Day, or Night. **Auto** switches at sunrise and sunset, worked out from the GPS position and time. It stays on Day until the GPS has had a fix.
- **Brightness** slider.
- **UTC Offset (hr)**: shifts the clock to local time, e.g. -8 for Pacific Standard Time and -7 during daylight saving. Daylight saving isn't automatic; change this by 1 twice a year.
- **WiFi Config** toggle. See the WiFi section below.

### Calibration

**Road speed.** This is where the speedometer's accuracy comes from:
- **Axle ratio**, **Tire diameter (in)** and **Transfer case ratio** (1.00 for this truck's LS600h drivetrain). These apply immediately. Changing any of them resets the GPS calibration, because the old calibration no longer matches.
- A live line showing which range the VCU says is engaged (LOW or HIGH), the motor rpm and the resulting speed. Underneath, each range shows whether it's GPS-calibrated or still estimated.
- **CALIBRATE FROM GPS.** To use it:
  1. Get a GPS fix and pick a straight, level road.
  2. Tap the button and drive at a **steady speed above 25 mph** for about **20 seconds**.
  3. The status text walks you through it. It calibrates whichever range is engaged, so repeat in the other range to calibrate both.
  4. It gives up after 2 minutes if it can't get a steady stretch, and refuses a result that looks badly wrong; the message suggests what to check.
  5. Tap **CANCEL** to stop early.

**Dyno drivetrain calibration**: a future workflow for correcting the dyno against a real chassis-dyno pull. Its button reads "Not built yet".

### VCU Parameters

Read and change ZombieVerter settings without a laptop. The page lists a curated set of numeric settings, grouped into Throttle, Regen, Limits, Charging, BMS and Other, each with the VCU's current value. Tap a row to edit it, set the new value with the +/- buttons, and press **WRITE**.

- Writes are only allowed with the vehicle **parked**: stopped, and the VCU not in Run. Otherwise the buttons are disabled and the reason is shown.
- A value the VCU rejects shows as "Value out of range"; the VCU enforces its own limits.
- Written values last until the VCU restarts. To keep them, press **SAVE TO VCU FLASH** and confirm.
- Settings that change how the drivetrain is wired up (inverter type, CAN routing, pin functions) aren't on this page on purpose. Use the VCU's own web interface for those.

### Diagnostics

A read-only grid of about 40 raw values from the VCU, using the VCU's own names (opmode, status, lasterr, udc, idc, pot readings, digital inputs and so on). It's useful on the bench or when chasing a fault. While this page is open, the main screens refresh at half speed.

### CAN Monitor

A bus-level view for checking the CAN wiring and spotting new devices:
- **Bus health line**: driver state, frames per second, estimated bus load, error counters, missed frames.
- **ID table**: one row per CAN ID with the device name (where known), rate and latest data. **RAW/DECODED** switches between bytes and decoded values for the VCU, the IVT-S current shunt and SimpBMS. Bytes that just changed are highlighted, and IDs that have gone quiet are grayed out.
- **Shunt cross-check**: compares the shunt's current and voltage with what the VCU reports. A mismatch points at the shunt, the VCU's shunt settings, or the decoding.
- **SAVE BASELINE**: records the IDs and rates of a known-good bus. Afterwards rows are flagged **NEW**, **MISSING** or **RATE** (rate well off its baseline). Hold **FORGET** to erase the baseline.
- **START/STOP CAPTURE**: records raw frames to the SD card (`/can/CANnnnn.csv`, SavvyCAN's CSV format). The **SDO** toggle leaves out the dash's own VCU polling so other devices stand out. A capture keeps running after you leave the page. Download captures from the WiFi `/logs` page.

### Data Logging

An on/off switch and a 1 / 2 / 5 Hz rate, both applied and saved immediately. When on, logging is **armed**: a new file starts once CAN data is live and the VCU leaves Off, so bench power-ups with the VCU off leave nothing behind. Each session gets its own file (`/logs/LOG0001.csv`, `LOG0002.csv`, …) containing telemetry plus GPS position and time. Left on, every drive gets its own file without touching the screen. The status line shows the current file and row count, or why it isn't recording (e.g. "No SD card").

## WiFi hotspot

The dash can run its own small WiFi network, which your phone joins directly. It's for editing settings, watching live data, downloading logs and updating firmware.

**Turning it on.** Settings → Display → **WiFi Config** toggle. Once on, the status line below it shows the network name and password. Turn it off when you're done to stop it broadcasting.

**Connecting.**
1. On your phone, join the WiFi network **Scout80-Config** (password: **Scout80Dash!**)
2. Open a browser and go to **192.168.4.1**

**Pages:**

| Page | What it's for |
| --- | --- |
| **Config** (`/`) | Edit the values below, then press **Save**. Values are stored permanently and take effect immediately, with no reflash. |
| **Live** (`/live`) | Live telemetry, refreshed once a second. Works while driving. |
| **Logs** (`/logs`) | Download or delete data logs and CAN captures, individually or all at once. The file currently being recorded can't be deleted. |
| **Update** (`/update`) | Install new firmware. See below. |

**Config page fields:**

| Field | Notes |
| --- | --- |
| Vehicle mass (lbs) | Include the driver's weight. This is the only physics constant the dyno's power estimate needs. |
| Target speed (mph) | The speed a 0-60 run ends at: 60 mph by default. |
| Launch speed (mph) | Minimum road speed that counts as "launched". |
| Launch IMU threshold (g) | How hard an acceleration needs to be to trigger the motion-sensor launch detection. |
| Home / Work latitude & longitude | The two fixed destinations shown on the Destinations screen. |
| Axle ratio, tire diameter, transfer case ratio | Same settings as Settings → Calibration. Changing one resets the GPS speed calibration. |
| Odometer (mi) | Only changes if you edit it. |

**Updating firmware.**
1. Download the release's **`firmware.bin`**. Don't use the merged image: the dash rejects it.
2. Open `/update`, choose the file, and press Install.
3. The dash checks the file, installs it and restarts. Settings, the odometer and the SD card are kept.

If a new build won't start, recovery is a USB flash.

**Safety note:** the config page, log downloads/deletes and firmware updates are all locked while the vehicle is moving. You can look, but not change anything, until you've stopped. Only the Live page works while moving.

This is a local-only connection: the dash never connects to the internet through it. It's your phone talking directly to the dash, the same as connecting to any other local device's WiFi.

## Troubleshooting & tips

| Symptom | What's going on |
| --- | --- |
| GPS says "NO FIX" | Normal indoors or under cover. Needs open sky; can take a few minutes outdoors on a cold start. |
| Speed label says "mph - estimated" | That range hasn't been GPS-calibrated yet. Run Settings → Calibration → CALIBRATE FROM GPS in each range. |
| Speedometer is off after changing tires | Update the tire diameter (Settings → Calibration or the WiFi config page), then recalibrate from GPS. |
| RANGE shows a dash | Not enough recent driving yet to estimate Wh/mi. It fills in after the first mile or so. |
| A value looks grayed-out / dashed | The dash hasn't received real data for that field yet, not an actual zero reading. |
| Charging screen shows nothing while parked | Only populates while actually plugged in and charging. |
| VCU Parameters WRITE is grayed out | Writes need the vehicle parked: stopped and the VCU not in Run. The reason is shown on the page. |
| Data Logging is on but no file appears | Logging waits for live CAN data and the VCU to leave Off. Also check the SD card is inserted. |
| Dyno won't arm/respond to a tap | Make sure you're tapping the screen itself, not mid-swipe. |
| Firmware update says "Not an app firmware file" | You picked the merged image. Use `firmware.bin` from the release. |
| "No charging data loaded" on Destinations | The charging-station file is missing from the SD card or needs regenerating. It's prepared ahead of time from an online database, not fetched live, and currently only covers San Diego County. See [SD_CARD.md](SD_CARD.md). |
| GPS NAV shows no map, or routes never compute | The map tiles or routing file are missing from the SD card. See [SD_CARD.md](SD_CARD.md) for what goes where. |
| Splash screen sits for a long time without the truck animation | The splash frames are missing from the SD card, or there's no card. The dash moves on after 45 seconds. See [SD_CARD.md](SD_CARD.md). |
| Screen too dim/bright | Swipe up or down on most screens, or use the brightness slider in Settings. |
| Low-12V warning seems wrong | If your vehicle has no working 12V sensor, turn off Low 12V Shutdown in Settings. Otherwise the dash can't tell a missing sensor apart from a real dying battery. |

For anything not covered here, the dash's swipe gestures and dock icons are the two ways to get anywhere. If a screen seems stuck, try the dock icon for its group first.
