# Scout 80 Waveshare Dash Display — Project Reference

## Hardware

- **Board:** Waveshare ESP32-S3-Touch-LCD-7, Rev1.2, 800x480, ESP32-S3-WROOM-1
- **Onboard peripherals:** CAN transceiver (RX GPIO19 / TX GPIO20), RS-485, touch controller on I2C (SDA GPIO8 / SCL GPIO9), dedicated UART1 JST header (TXD/RXD/GND/3V3), microSD slot
- **No onboard 12V power supply** — needs an external 12V→5V buck converter with reverse-polarity and load-dump protection
- **Battery JST connector** is a LiPo backup/UPS circuit, not the main power path
- **Mounting:** front-insert into the dash with a bezel flange and rear clamp — see Enclosure below (dash opening is being enlarged from 8"×3.5" to 203.2 × 118 mm — existing width kept, height opened up)
- **GPS:** GY-NEO6MV2 (u-blox NEO-6M), ceramic 25 mm patch antenna on u.FL lead, 9600 baud TTL on UART0 (GPIO43/44). That's the same UART as the USB flash/debug port, selected by the board's UART DIP switch: **GPS** for driving, **USB** to flash. See `gps_driver.h`. Board measured 36.80 × 25.75 mm
- **IMU:** GY-521 (MPU-6050), I2C. Board measured 20.55 × 15.80 mm

## Enclosure

- **Base case:** `VaAndCob/ESP32-S3-Mastermind` → `waveshare7_3D/waveshare_7_case.stl` (MIT). Single back shell, 197 × 115 × 19 mm, 2 mm floor, 1.5 mm walls; Waveshare board screws through four Ø4 holes at (−61.6, 25.07), (64.6, 25.07), (−61.6, 90.73), (64.6, 90.73) in the STL frame
- **Design file:** `scout_dash_enclosure.scad` (parametric OpenSCAD, imports the original STL) → three parts:
  - **Case + bezel flange** — 8 mm flange all round, 3 mm thick, flush with the front face; bezel outer 213 × 131 mm. Prints floor-down like the original; flange needs build-plate-only supports (its underside is the clamp face against the dash, so it must stay flat — no 45° taper). Optional Ø10 wiring hole through the floor at (15, 90)
  - **Rear carrier / retainer** — H-frame. The four stock board screws are replaced with **M3 × 6 male-female hex standoffs** (male thread = stock screw thread length): they hold the board in the case, and their hex bodies stick out of the case back as locating pins. The carrier has four Ø6.7 × 6.5 mm sockets with 1 mm chamfered lead-ins that drop over the standoffs by feel, then four **captive M3 × 8** screws (Ø3.0 hole — they self-thread into the print and stay put) go into the standoffs. Board + case is a self-contained unit that slides in from the front. Feet reach past the cut-out and stop 0.3 mm short of the panel's rear face, so tightening the screws clamps the dash between bezel and feet. GPS and IMU boards sit in pockets facing the case floor (protected); wire channels run to the floor hole; a coax channel exits the top edge. Left rail arches 4 mm over the side port window; rails kept at 12 mm wide because 13 mm clips a floor port opening. Back face is flat — prints back-down
  - **GPS antenna pod** — 25 × 25 patch faces up; screw ears or VHB. Separate so the antenna can sit where it sees sky (behind a steel dash, reception will be poor); needs a u.FL extension
- **Key parameters:** `panel_t` (default 1.5 mm), `cut_clear` (1.5 mm top/bottom), `gps_pcb` 36.80 × 25.75 and `imu_pcb` 20.55 × 15.80 (measured), `standoff_len`, `socket_clear`, `screw_hole_d`
- **Dash cut-out:** keep the existing 8" (203.2 mm) width — it's already wider than the case (197), so the cut can't be narrowed to 200; open the height from 88.9 to 118 mm (14.55 mm top and bottom, assuming centred), R12 corners via 24 mm (15/16") hole saw. Leaves ~3.1 mm side play per side; bezel still overlaps the cut edge by ~4.9 mm at the sides and ~6.5 mm top/bottom. 1:1 printable template: `scout_dash_cutout_template.pdf` (US Letter landscape, print at 100%)
- **Material:** PETG or ASA for in-cab heat, not PLA
- **IMU orientation:** board lies parallel to the case back, so its Z axis points fore-aft in the vehicle — map axes in firmware accordingly

## Repo & toolchain

- Forked **`Light-r4y/uaDASH`** (open-source rusEFI CAN dashboard) as the firmware base — same LVGL 8.4.0 + LovyanGFX 1.2.0 + ESP32_IO_Expander stack, already proven on this exact board
- Cloned to `C:\Users\rober\Documents\GitHub\Waveshare\uaDASH`; actual firmware lives in its `firmware\` subfolder
- Converted from the original Arduino CLI / Arduino IDE workflow to **PlatformIO** (added `src/` and `include/` folders inside `firmware\`)
- Board-select macro for this display: `WAVESHARE_S3_LCD7`

## Resolved PlatformIO build issues

These were all genuinely tricky — worth keeping as a reference since any future rebuild or fresh clone will hit the same walls:

| Problem | Fix |
|---|---|
| PlatformIO's default `espressif32` platform is stuck on Arduino core 2.x; project needs core 3.x (`ledcAttach`, `ESP_IOExpander_CH422G`) | Switch to the **pioarduino** fork of platform-espressif32 |
| pioarduino's `stable` tag (core 3.3.11) breaks LovyanGFX's `Bus_RGB.cpp` (renamed GPIO HAL function) | Pin to tag **`54.03.21`** specifically (core 3.2.1) — matches the version that worked via `arduino-cli` originally |
| PlatformIO's own `ESP32_IO_Expander` registry entry lacks CH422G chip support even at "the same" version number | Pull directly from `https://github.com/esp-arduino-libs/ESP32_IO_Expander.git#v1.1.0` (the real official repo) |
| `ESP32_IO_Expander` needs `esp-lib-utils` but PlatformIO doesn't auto-resolve it from a git-URL dependency | Add explicitly: `https://github.com/esp-arduino-libs/esp-lib-utils.git#v0.2.3` |
| LVGL can't find `lv_conf.h` when it's in `src/` | Move it to `include/` (library dependencies can't see `src/`, only `include/`); add `-DLV_CONF_INCLUDE_SIMPLE -Iinclude` to build_flags |
| `esptool` v5.0.0 crashes (`click` API incompatibility) during the bootloader build step | `pip install "click<8.2"` into PlatformIO's own Python venv |
| Toolchain package extraction fails with `FileNotFoundError` on deeply-nested paths | Enable Windows long-path support (`LongPathsEnabled` registry key) |

Final working `platformio.ini` config: platform pinned to pioarduino `54.03.21`, `board_upload.flash_size = 16MB`, `board_build.partitions = app3M_fat9M_16MB.csv`, `board_build.psram_type = opi`, `board_build.arduino.memory_type = qio_opi`, build_flags as above, lib_deps as above.

## CAN/SDO architecture

**All telemetry is SDO-polled** (decided 2026-09-13 after the bench session in `firmware/can-bench.md`: the spare VCU had no canmap, so nothing was broadcast). Every param the screens use is in the poll table in `zombie_updaters.{h,cpp}`. Each entry takes a 50 ms slot, shared alternately with on-demand reads and writes from `sdo_client.h`.

**canmap broadcasts are decoded for the trace only** (v2.2.1). Rob's VCU does send canmap frames (0x355/0x356/0x126/0x210/0x257, 10 Hz). `can_broadcast.cpp` used to write them into `myData`, but with the gain inverted, so it overwrote good SDO values every 100 ms (e.g. udc 366 V shown as 3.2 V). Every one of those params is already SDO-polled at full resolution, so the broadcast path no longer touches `myData`. Found via the CAN Monitor's shunt-vs-VCU cross-check.

**Road speed is not from the VCU.** With Vehicle = Classic, `Veh_Speed` is never set, so the dash computes speed from motor rpm (`speed`) per LS600h range. See `speed_cal.h` and CLAUDE.md's Speed screen entry.

**SDO wire encoding**, confirmed directly from Rob's own working Dial firmware (`SDOManager.cpp`), not guessed:
- `index = 0x2100 | (paramId >> 8)`, `subindex = paramId & 0xFF`
- Node ID 3 → COB-ID 0x603 (request) / 0x583 (response)
- This scheme folds ZombieVerter's flat 16-bit param IDs into CANopen's native 16-bit-index + 8-bit-subindex addressing, and works across the full ID range (legacy 1–255 params and 2000+ spot-telemetry values alike)

**Decision:** port CAN/SDO logic from Rob's own working Dial firmware as the primary source. External reference projects (`jamiejones85/ZombieVerterDisplay`, OVMS's CANopen client) are cross-reference only, for spotting gaps — not the source of truth, since firmware-version drift makes borrowed SDO code risky.

## Screens (12, all built)

The original 7-screen plan grew into: Splash/Clock, Settings, Speed (home), Drive, Status, Battery, Charging, GPS, GPS NAV, Destinations, Dyno LIVE and Dyno RESULTS. All are hand-written LVGL; none came from SquareLine Studio. Layout rules and the reasons behind each screen are in CLAUDE.md; operating instructions are in `USER_MANUAL.md`.

### 0-60 / virtual dyno design
- Needs an added I2C accelerometer (launch detection, acceleration curve — GPS alone is too slow/imprecise) plus GPS (absolute speed cross-check, road-grade correction)
- Shows two power curves per run: **road-load power** (acceleration × mass × velocity) vs. **electrical input power** (CAN pack V×I) — the EV-specific advantage over a typical virtual dyno, since electrical power is measured directly rather than inferred
- The gap between the two curves is empirically-measured drivetrain efficiency
- Calibrated against a real chassis dyno pull: solve for correction constants (driveline efficiency scalar and/or effective mass + Cd·A), store in NVS using the same staged/explicit-save pattern as the rest of the UI

## Fixed architectural decisions

- **Immobilizer** (RFID/BLE) stays on the M5 Stack Dial only — never duplicated on the Waveshare
- **Audio** (Bluetooth phone streaming + local SD file playback) is a fully standalone module, independent of the Waveshare's CAN bus and GPIO entirely — the ESP32-S3 has no classic Bluetooth (BLE only) and this board has no I2S/speaker header
- GPS module shares UART0 with the USB port via the board's UART DIP switch (see Hardware above)

## Build status (v2.2.1, 2026-10-24)

The firmware is in use on real hardware with the real VCU. Releases are tagged in git (`v2.0`, `v2.1`, `v2.2`, `v2.2.1`); the git log is the detailed history.

**Done:**
- Boots and runs on the Waveshare-7. Display, touch, swipe and dock navigation, and NVS settings persistence all verified on hardware.
- CAN/SDO validated against a real ZombieVerter (`firmware/can-bench.md`). Telemetry is all-SDO; canmap broadcasts are trace-only (see CAN/SDO above).
- All 12 screens built by hand in LVGL. SquareLine Studio was dropped.
- GPS (real NEO-6M plus the `mock-gps` build), MPU-6050 IMU launch detection, and the 0-60 LIVE/RESULTS dyno.
- microSD with offline NAV vector map tiles for California, routing, and a San Diego charging-station dataset.
- Cluster home screen with odometer/trip, road speed from motor rpm with GPS calibration, and Wh/mi efficiency plus range.
- Settings: warning thresholds, display, speed calibration, VCU Parameters (SDO read/write plus save to flash, interlocked), Diagnostics, CAN Monitor (decode, baseline, SavvyCAN capture) and Data Logging.
- Local WiFi hotspot with a config page, live telemetry, log/capture downloads and **firmware update over WiFi** (`/update`, v2.1). The update writes the inactive OTA slot and is verified by ESP-IDF before reboot.

**Still open:**
- **OTA rollback.** This Arduino core has no bootloader rollback. A valid build that crashes on boot still needs a USB flash to recover, so keep USB access until that's solved or the build is trusted.
- **Dyno calibration workflow** (fit correction constants from a real chassis-dyno pull). The Settings › Calibration entry is still a disabled "Not built yet" button.
- **In-truck verification** of data-logging arm, efficiency/range, IVT-S decoding and the odometer. All are bench-tested but need the VCU in Run and real driving.
- **Map:** only zoom 16, no LOD, polygon holes not drawn.
- **Battery screen:** no per-cell bar grid; the CAN data has no per-cell array.
- **Enclosure v2** (case + bezel flange, rear carrier, GPS/IMU pockets, antenna pod, 1:1 cut-out template) is printed and fitted, but **not yet installed** in the dash.
