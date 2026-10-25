# Scout 80 EV Dashboard — Architecture & Build Reference

## System overview

| Node | Role | Connection |
|---|---|---|
| ZombieVerter STM32 VCU | CAN bus hub | — |
| Wemos ESP32 (onboard VCU) | WiFi tuning UI (`wjcloudy/esp32-web-interface`) | UART to STM32 |
| Waveshare ESP32-S3-Touch-LCD-7 (Rev1.2, 800x480) | Main dash display, bezel-mounted in 8"x3.5" opening | CAN to VCU |
| M5 Stack Dial | Bench/backup display, **RFID/BLE immobilizer (stays here only)** | CAN to VCU |
| GPS module (NMEA) | Nav + dyno speed cross-check | Waveshare's "UART2" header (GPIO43/44) - NOT a separate UART from the USB flash/debug port; see waveshare-dash-build.md's 2026-09-14 GPS entry for the DIP-switch tradeoff this implies |
| IMU (GY-521 / MPU-6050) | Launch detection, acceleration curve, grade correction | Waveshare I2C header |
| Standalone audio module | BT receiver + SD MP3 + amp | Fully independent — no CAN/GPIO link |
| External 12V→5V buck converter | Power for Waveshare (no onboard PSU) | Needs reverse-polarity + load-dump protection |

## Waveshare software stack

- **Fork base:** `Light-r4y/uaDASH` — LVGL 8.4.0 + LovyanGFX 1.2.0 + ESP32_IO_Expander 1.1.0. uaDASH's SquareLine Studio UI has been replaced; every screen is now hand-written LVGL
- uaDASH's rusEFI CAN parser is gone. In its place is CANopen SDO polling ported from Rob's own `Zombieverter-Dial-Display` (primary source — not Jamie's ZombieVerterDisplay or OVMS's CANopen client, which are cross-reference only, for spotting gaps). All telemetry is SDO-polled; canmap broadcasts are decoded for the trace only
- Navigation: a 5-icon bottom dock for cross-group jumps, plus left/right swipes within a group; warning thresholds staged in RAM and written to flash only on explicit save

## Screens (12, all built)

The original 7-screen plan (telemetry summary, BMS detail, splash, clock, GPS nav, dyno, settings) became:

| Group | Screens |
| --- | --- |
| Telemetry | Speed (home cluster), Drive, Status, Charging |
| BMS | Battery (cell-level) |
| GPS | GPS (telemetry), GPS NAV (offline vector map + routing, IceNav-v3 ports), Destinations |
| Dyno | 0-60 LIVE, RESULTS |
| Settings | Settings (thresholds, display, calibration, VCU parameters, diagnostics, CAN monitor, data logging) |
| — | Splash / clock (boot screen, reached from Settings) |

Per-screen layout rules are in CLAUDE.md; operating instructions are in `USER_MANUAL.md`.

## 0-60 / virtual dyno — design summary

- **Dual power estimate per run:** road-load power (acceleration × mass × velocity, force-balance model) vs. electrical input power (CAN bus V×I) — the gap between them is empirically measured drivetrain efficiency, not a guess
- Requires the new IMU (50–100Hz) for launch detection and the acceleration curve; GPS cross-checks absolute speed and can correct for road grade
- **Calibration workflow:** log a virtual run alongside a real chassis dyno pull (same day/conditions); solve for correction constants (driveline efficiency scalar, and/or effective mass + Cd·A via least-squares); store in NVS using the same staged/explicit-save pattern as the rest of the UI
- **Modes:** Live (auto-launch-detect timer, interpolated 60mph crossing) / Dyno (post-run power-vs-speed graph, both curves, peak numbers) / Calibration (enter dyno numbers, compute + save constants). Live and Dyno are built as the LIVE and RESULTS screens; Calibration is not built yet
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

## Build order — status (v2.2.1, 2026-10-24)

1. ✅ Clone `uaDASH`, build stock against the Waveshare-7, confirm display/touch bring-up
2. ✅ Strip the rusEFI CAN parser; keep the LVGL/LovyanGFX/IO_Expander shell
3. ✅ Port CANopen SDO polling; validated against a real VCU (`firmware/can-bench.md`)
4. ✅ Build the screens. Done by hand in LVGL, not SquareLine Studio
5. ✅ Wire GPS (on UART0, shared with USB via the DIP switch, not UART1); port IceNav-v3 map rendering and routing
6. ◐ Add the I2C IMU and dyno screens — done; the dyno **calibration** workflow is not built
7. ✅ Settings screen (staged thresholds + explicit save), since grown to VCU parameters, diagnostics, CAN monitor and data logging
8. ◐ OTA — AP-hosted `/update` page writing the inactive OTA slot is done (v2.1); **rollback is not**: this Arduino core has no bootloader rollback, so a build that crashes on boot still needs a USB flash

Beyond the original plan: road speed from motor rpm with GPS calibration, odometer/trip, Wh/mi efficiency and range estimate. Remaining work and open hardware tests are tracked in `waveshare-dash-build.md` → Build status.
