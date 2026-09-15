# Scout 80 EV Dashboard — Architecture & Build Reference

## System overview

| Node | Role | Connection |
|---|---|---|
| ZombieVerter STM32 VCU | CAN bus hub | — |
| Wemos ESP32 (onboard VCU) | WiFi tuning UI (`wjcloudy/esp32-web-interface`) | UART to STM32 |
| Waveshare ESP32-S3-Touch-LCD-7 (Rev1.2, 800x480) | Main dash display, bezel-mounted in 8"x3.5" opening | CAN to VCU |
| M5 Stack Dial | Bench/backup display, **RFID/BLE immobilizer (stays here only)** | CAN to VCU |
| GPS module (NMEA) | Nav + dyno speed cross-check | Waveshare's "UART2" header (GPIO43/44) - NOT a separate UART from the USB flash/debug port; see waveshare-dash-build.md's 2026-09-14 GPS entry for the DIP-switch tradeoff this implies |
| IMU (new — e.g. MPU6050) | Launch detection, acceleration curve, grade correction | Waveshare I2C header |
| Standalone audio module | BT receiver + SD MP3 + amp | Fully independent — no CAN/GPIO link |
| External 12V→5V buck converter | Power for Waveshare (no onboard PSU) | Needs reverse-polarity + load-dump protection |

## Waveshare software stack

- **Fork base:** `Light-r4y/uaDASH` — LVGL 8.4.0 + LovyanGFX 1.2.0 + ESP32_IO_Expander 1.1.0, UI built in SquareLine Studio
- Strip uaDASH's rusEFI CAN parser; port in your **own** CANopen SDO polling logic from `Zombieverter-Dial-Display` (primary source — not Jamie's ZombieVerterDisplay or OVMS's CANopen client, which are cross-reference only, for spotting gaps in your own implementation)
- Screen navigation: swipe left/right (uaDASH pattern); settings staged in RAM, written to flash only on explicit save

## Screens (7 total)

1. Central telemetry summary
2. BMS detail
3. Splash / logo
4. Clock
5. GPS navigation — `jgauchia/IceNav-v3` as offline OSM rendering reference
6. 0-60 / virtual dyno
7. Settings

## 0-60 / virtual dyno — design summary

- **Dual power estimate per run:** road-load power (acceleration × mass × velocity, force-balance model) vs. electrical input power (CAN bus V×I) — the gap between them is empirically measured drivetrain efficiency, not a guess
- Requires the new IMU (50–100Hz) for launch detection and the acceleration curve; GPS cross-checks absolute speed and can correct for road grade
- **Calibration workflow:** log a virtual run alongside a real chassis dyno pull (same day/conditions); solve for correction constants (driveline efficiency scalar, and/or effective mass + Cd·A via least-squares); store in NVS using the same staged/explicit-save pattern as the rest of the UI
- **Modes:** Live (auto-launch-detect timer, interpolated 60mph crossing) / Dyno (post-run power-vs-speed graph, both curves, peak numbers) / Calibration (enter dyno numbers, compute + save constants)
- Raw run data logs to SD in a capped/rotating file pattern (same approach as trip logging)

**Deferred, low-cost later additions** (ride on the same IMU+GPS infra, no action needed now): lap/autocross timer via GPS geofencing + launch detection; wheelspin/traction flag via IMU accel dip-then-recover, fed into the fault banner.

## Reference projects — what to borrow from what

| Project | Use for |
|---|---|
| `Light-r4y/uaDASH` | Driver stack + screen-nav shell (fork base) |
| Your own `Zombieverter-Dial-Display` | SDO polling logic (primary source) |
| `jamiejones85/ZombieVerterDisplay`, OVMS CANopen client | Cross-reference only — check your SDO code against these for gaps (segmented transfers, EMCY, abort codes) |
| `holoduke/JKBMS` (`esp32-bms-lvgl`) | BMS screen UI patterns — SOC ring, per-cell bars, status pills |
| `jgauchia/IceNav-v3` | GPS offline map rendering |
| `nickn17/evDash` | BMS detail layout, capped/rotating SD logging pattern |
| ESP-IDF dual-partition OTA + rollback | Safe reflash given bezel-mounted inaccessibility |
| WiCAN, OpenDTU-OnBattery | MQTT/HA discovery pattern — later, for V2H dashboard unification |

## Fixed decisions (don't relitigate)

- **Immobilizer** stays on the M5 Dial only — never duplicated on the Waveshare
- **Audio** (BT streaming + local SD playback) is a fully standalone module — no CAN/GPIO integration with the Waveshare or VCU, because the ESP32-S3 lacks classic Bluetooth and the board has no I2S header
- **Power**: external 12V→5V buck converter required, with automotive-grade input protection

## Build order

1. Clone `uaDASH`, build stock against the Waveshare-7, confirm display/touch driver bring-up before changing anything
2. Strip the rusEFI CAN parser; keep the LVGL/LovyanGFX/IO_Expander shell and swipe navigation
3. Port your own CANopen SDO polling code in place of the removed parser; validate against live VCU data on a placeholder screen
4. Build screens in SquareLine Studio, starting with the central telemetry summary (reuses the most SDO plumbing)
5. Wire GPS on UART1; integrate IceNav-v3 map rendering
6. Add the I2C IMU; build the dyno screen logic and calibration workflow
7. Build the settings screen (staged config + explicit save)
8. Harden OTA (dual-partition, rollback-safe, AP-hosted `/update` page) before final bezel mount
