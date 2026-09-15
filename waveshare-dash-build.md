# Scout 80 Waveshare Dash Display — Project Reference

## Hardware

- **Board:** Waveshare ESP32-S3-Touch-LCD-7, Rev1.2, 800x480, ESP32-S3-WROOM-1
- **Onboard peripherals:** CAN transceiver (RX GPIO19 / TX GPIO20), RS-485, touch controller on I2C (SDA GPIO8 / SCL GPIO9), dedicated UART1 JST header (TXD/RXD/GND/3V3), microSD slot
- **No onboard 12V power supply** — needs an external 12V→5V buck converter with reverse-polarity and load-dump protection
- **Battery JST connector** is a LiPo backup/UPS circuit, not the main power path
- **Mounting:** bezel-mounted in the dashboard's 8"×3.5" screen opening

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
| `esptool` v5.0.0 crashes (`click` API incompatibility) during the bootloader build step | Pin `click==8.1.8` in PlatformIO's own venv (`click` 8.2 changed `ParamType.get_metavar()` to require a `ctx` arg; esptool 5.x calls it with one). Recurs any time the venv's `click` gets bumped. |
| Toolchain package extraction fails with `FileNotFoundError` on deeply-nested paths | Enable Windows long-path support (`LongPathsEnabled` registry key) |
| `pio` / `platformio` command dies with `ModuleNotFoundError: No module named 'platformio'` | The `~/.platformio/penv` got half-clobbered by an interrupted `pip` run (a stale `pio device monitor` held a lock on `platformio.exe`), leaving `site-packages/~latformio/` and wiping `certifi`/`cffi`/`bitstring`. Repair: kill any running `pio` processes, then `pip install --force-reinstall pioarduino==6.1.19` into that venv, then restore `bitstring`, `cffi`, and re-pin `click==8.1.8`. |
| `pio run -t upload` aborts mid-flash with `UnicodeEncodeError: 'charmap' codec` | esptool 5.x draws Unicode progress bars; when pio's stdout is redirected on Windows it defaults to cp1252. Run with `PYTHONUTF8=1` (or `PYTHONIOENCODING=utf-8`) in the environment. |
| Build prints a red `Error: No such option: --ng` box then `esp-idf-size exited with code 2` | Cosmetic — only the post-build memory-usage summary fails (version skew between the pinned `click` and the bundled `esp-idf-size`). The `.elf`/`.bin` are already built; `[SUCCESS]` still follows. Ignore, or read sizes from the linker output / `PlatformIO Home > Inspect`. |

Final working `platformio.ini` config: platform pinned to pioarduino `54.03.21`, `board_upload.flash_size = 16MB`, `board_build.partitions = app3M_fat9M_16MB.csv`, `board_build.psram_type = opi`, `board_build.arduino.memory_type = qio_opi`, build_flags as above, lib_deps as above.

## Resolved runtime/boot issue: I2C driver generation conflict

**Symptom:** firmware compiled clean and flashed successfully, but on boot the screen showed only a faint backlight with no content. Serial monitor (must be opened at 115200 baud, not the default 9600) revealed a boot loop:

```
E (177) i2c: CONFLICT! driver_ng is not allowed to be used with this old driver
abort() was called at PC 0x...
```

**Actual root cause (corrected after backtrace analysis):** it is a **link-time** conflict, not a runtime one. Decoding the abort backtrace against `firmware.elf`:

```
check_i2c_driver_conflict   (esp-idf/components/driver/i2c/i2c.c)
  <- do_global_ctors / start_cpu0_default    <- runs at BOOT, before setup()
```

The legacy `driver/i2c.c` ships a `__attribute__((constructor))` that calls `abort()` the instant the new `i2c_master` driver (`i2c_acquire_bus_handle`, a weak symbol) is also present in the image. So the crash fires from a startup C++ ctor before `app_main()` — nothing in `setup()` / `lcd_panel_start()` is even reached. `nm firmware.elf | grep i2c_` confirmed **both** drivers were linked.

Two things dragged in the **new** driver:
1. `ESP32_IO_Expander` (+ its `esp-lib-utils` dep) — its CH422G C++ wrapper is built on `i2c_master` and pulls in Arduino `Wire`. Merely not `#include`-ing it is not enough; an explicit `lib_deps` entry is still compiled and force-linked (global ctors in `esp_expander_base.cpp`).
2. **LovyanGFX itself** — `lgfx/v1/platforms/esp32/common.cpp` has `#if __has_include(<Wire.h>)` → `#include <Wire.h>` convenience blocks. PlatformIO's LDF then compiles **and links the whole `Wire` library**; `Wire` → `esp32-hal-i2c-ng.c` → `i2c_master.c`. arduino-cli's LDF never linked `Wire` (nothing *called* it), which is the entire reason the original build booted and this one did not — same source, different linker behaviour.

LovyanGFX's own GT911 touch path uses only the **legacy** `driver/i2c.h` (`#include <driver/i2c.h>` in the same file), so once `Wire` is gone there is exactly one driver generation in the image.

**Fix (three parts, all required):**
1. Drive the CH422G directly with LovyanGFX's legacy-driver I2C primitives (`lgfx::i2c::init/beginTransaction/writeBytes/endTransaction`) instead of the `ESP32_IO_Expander` wrapper — still needed to raise `TP_RST` / `LCD_RST` / `LCD_BL` at boot.
2. Remove `ESP32_IO_Expander` and `esp-lib-utils` from `lib_deps` entirely.
3. `lib_ignore = Wire` in `platformio.ini` — makes `__has_include(<Wire.h>)` false inside LovyanGFX so it never touches `Wire`.

Verify with `nm .pio/build/waveshare-s3-lcd7/firmware.elf | grep -E 'i2c_new_master_bus|i2c_acquire_bus_handle|i2cInit'` → must return nothing (only the harmless legacy `check_i2c_driver_conflict` may remain; its weak ref to the new driver is now NULL so it does not abort).

**Consequence for later work:** any future I2C device — notably the dyno-screen IMU — must also use the legacy driver (`lgfx::i2c`, or a legacy-`driver/i2c.h` library), never Arduino `Wire`, or this exact boot abort returns.

Real CH422G register map (extracted from the library's low-level C driver, `esp_io_expander_ch422g.c` — this chip exposes each function as its own I2C address rather than one address + register-select byte):

| Register | I2C address | Purpose |
|---|---|---|
| `WR_SET` | `0x24` | Mode/config (bit 0 = IO_OE, set to make all 12 pins outputs) |
| `WR_OC` | `0x23` | Open-collector outputs (pins 8–11) |
| `WR_IO` | `0x38` | Push-pull outputs (pins 0–7) — covers `TP_RST` (bit 1), `LCD_BL` (bit 2), `LCD_RST` (bit 3) |
| `RD_IO` | `0x26` | Inputs |

Replacement code in `lcd_panel_start()` (`display_driver.cpp`):

```cpp
#define CH422G_ADDR_WR_SET  0x24
#define CH422G_ADDR_WR_IO   0x38

static bool ch422gWriteReg(uint8_t addr, uint8_t value) {
  if (!lgfx::i2c::beginTransaction(I2C_NUM_0, addr, 400000U, false).has_value()) return false;
  bool ok = lgfx::i2c::writeBytes(I2C_NUM_0, &value, 1).has_value();
  lgfx::i2c::endTransaction(I2C_NUM_0);
  return ok;
}

// In lcd_panel_start(), replacing the old ESP_IOExpander_CH422G block entirely:
lgfx::i2c::init(I2C_NUM_0, 8 /*SDA*/, 9 /*SCL*/);
ch422gWriteReg(CH422G_ADDR_WR_SET, 0x01); // all 12 pins -> output mode
ch422gWriteReg(CH422G_ADDR_WR_IO, 0xFF);  // pins 0-7 HIGH (TP_RST, LCD_BL, LCD_RST all covered)
delay(100);
```

**Status:** fixed and confirmed. After all three parts above, the board boots clean — serial shows a single `rst:0x1 (POWERON)`, no `CONFLICT! driver_ng`, no `abort()`, no backtrace, and it stays up (previously it reboot-looped ~4x/second). The only boot-time line is `Preferences.cpp getBytesLength(): ... packVLo NOT_FOUND`, which is just NVS reading a key that does not exist yet on a fresh flash — harmless.

Note: `DEBUG` is not defined, so `Serial.begin()` never runs and the firmware prints nothing after that point over UART0 — silence on the monitor is expected, not a hang. Define `DEBUG` (build flag) when you want the `setup()` progress prints back.

Auto-reset for flashing worked fine over the CH343 (`--before default_reset`); the manual BOOT+RESET dance in `platformio.ini`'s trailing comment was not needed.

## CAN/SDO architecture

**Two data paths, confirmed against real ZombieVerter params.json data:**

1. **Passive CAN broadcast** (no polling needed) — SOC, pack voltage (`udc`), pack current (`idc`), motor temp (`tmpm`), heatsink temp (`tmphs`), motor RPM (confusingly named `speed`), 12V rail (`U12V`), `Gear`, `MotActive`, `regenmax`. These arrive automatically on fixed frame IDs (0x126, 0x210, 0x257, 0x300, 0x301, 0x302, 0x355, 0x356). A generic table-driven decoder (`can_broadcast.cpp`) extracts all matching fields per frame — this also fixes a latent bug in the original ZombieVerterDial code where the 0x356 frame's `udc`/`idc` were never decoded, only `tmpm`.

2. **Active SDO polling** (only for params with no broadcast frame) — real vehicle speed (`Veh_Speed`, kph — distinct from the broadcast "speed" which is motor RPM), `opmode`, `power`, `lastErr`.

**SDO wire encoding**, confirmed directly from Rob's own working Dial firmware (`SDOManager.cpp`), not guessed:
- `index = 0x2100 | (paramId >> 8)`, `subindex = paramId & 0xFF`
- Node ID 3 → COB-ID 0x603 (request) / 0x583 (response)
- This scheme folds ZombieVerter's flat 16-bit param IDs into CANopen's native 16-bit-index + 8-bit-subindex addressing, and works across the full ID range (legacy 1–255 params and 2000+ spot-telemetry values alike)

**Decision:** port CAN/SDO logic from Rob's own working Dial firmware as the primary source. External reference projects (`jamiejones85/ZombieVerterDisplay`, OVMS's CANopen client) are cross-reference only, for spotting gaps — not the source of truth, since firmware-version drift makes borrowed SDO code risky.

## Screen plan (9 total, since the 2026-09-14 Speed/Drive/Status/Battery split)

Originally planned as 7 screens with a single "central telemetry summary"
and a single "BMS detail" screen; those two were split into four (see the
Phase 12 milestone below) once Rob asked for speed on its own home screen
and a static/at-rest vs. in-motion split for the rest of the telemetry.

1. Speed (HOME) — full-screen speedometer (mph) + P/N/F/R
2. Drive — power, pack current, gear selection, motor mode, regen limit
3. Status — SOC, pack voltage, aux/12V voltage, motor temp, inverter temp, max battery temp
4. Battery — SOC ring, status pill, cell-level voltage/temp detail
5. Splash/logo
6. Clock
7. GPS navigation (`jgauchia/IceNav-v3` as offline-map rendering reference)
8. 0-60 / virtual dyno
9. Settings

### 0-60 / virtual dyno design
- Needs an added I2C accelerometer (launch detection, acceleration curve — GPS alone is too slow/imprecise) plus GPS (absolute speed cross-check, road-grade correction)
- Shows two power curves per run: **road-load power** (acceleration × mass × velocity) vs. **electrical input power** (CAN pack V×I) — the EV-specific advantage over a typical virtual dyno, since electrical power is measured directly rather than inferred
- The gap between the two curves is empirically-measured drivetrain efficiency
- Calibrated against a real chassis dyno pull: solve for correction constants (driveline efficiency scalar and/or effective mass + Cd·A), store in NVS using the same staged/explicit-save pattern as the rest of the UI

## Fixed architectural decisions

- **Immobilizer** (RFID/BLE) stays on the M5 Stack Dial only — never duplicated on the Waveshare
- **Audio** (Bluetooth phone streaming + local SD file playback) is a fully standalone module, independent of the Waveshare's CAN bus and GPIO entirely — the ESP32-S3 has no classic Bluetooth (BLE only) and this board has no I2S/speaker header
- GPS module connects via the Waveshare's dedicated UART1 header

## Build status

Firmware compiles clean end-to-end — full toolchain, all third-party libraries, the ported CAN broadcast decoder and SDO client, and the cleaned-up uaDASH UI scaffold (bench-test and engine-config screens fully removed, since they have no EV equivalent).

First-boot I2C driver conflict **diagnosed, fixed, and confirmed on hardware** (see the corrected section above): board boots clean, no crash loop.

**Milestone 1 — bring-up — COMPLETE (2026-09-06):** stock uaDASH display renders correctly, left/right swipe navigation works. Settings-screen +/- buttons animate but do not change values — this is **expected, not a regression**: every `*WarnSetPlus/Minus` handler in `events.cpp` is a deliberate `{}` no-op, and the screen itself is still the rusEFI one (7 stepper rows: rpm/clt/iat/oilT/oilP/fuelP/vBatt) with no EV meaning. The real EV threshold model is only 4 fields (`warning_set`: `lowSoc`, `motorTemp`, `heatsinkTemp`, `packVLow`); its NVS persistence (`getWarningsSet`/`updateWarningsSet`/`setDefaultWarnSet`, namespace `"warn"`) is written and the `saveWarnSet`/`defaultWarnSet` buttons are live. The main-screen live-telemetry updaters (`fastUpdate`/`midUpdate`/`slowUpdate` in `zombie_updaters.cpp`) are likewise all `// TODO: bind to widget`. **Decision (Rob, 2026-09-06): leave the settings + telemetry widget wiring for the SquareLine redesign (milestone 3), don't do an interim wire-up against the mislabelled rusEFI rows.**

**Milestone 3 in progress (2026-09-13) — quad-grid main screen, BMS screen, day/night mode:**
- `ui_mainScreen.c` rebuilt from scratch (the retextured 11-gauge layout from milestone 1 is gone) as a 2x2 quad-grid: SOC (top-left, ring/arc gauge + FontSpeed hero number, most prominent), Speed (top-right), Power kW signed (bottom-left), Motor temp signed (bottom-right). No SquareLine project backs this screen (none exists in the repo) — the layout spec lives as a comment block at the top of the file.
- New `ui_bmsScreen.{h,c}` — JKBMS-style pattern: SOC ring, a charging/discharging/idle status pill (from the sign of `packCurrent`), and 4 labeled bars (pack voltage, pack current, motor temp, heatsink temp). Per-cell voltage bars are NOT built — our CAN data is aggregate-only (no canmap on the bench VCU) — a placeholder comment marks where that would go if a cell-level BMS source is added.
- New `ui_theme.{h,cpp}` — day/night palette system (independent of SquareLine's dead `_ui_switch_theme()`/`UI_THEME_ACTIVE` stub in `ui_helpers.c`, which was never wired up). Night palette is amber-shifted + dims the backlight (automotive night-vision convention), not just a darker version of day. Persisted via `getDisplayMode()`/`updateDisplayMode()` in `zombie_updaters.cpp`, same NVS-via-Preferences pattern as `warningSet` (separate `"disp"` namespace). Toggled from the settings screen's `ui_turboSwitch` (repurposed from the dead rusEFI turbo/NA switch — CHECKED = night).
- Screen navigation: main screen now swipes RIGHT to settings (unchanged) and LEFT to the new BMS screen (new); BMS swipes RIGHT back to main.
- Enabled `LV_FONT_MONTSERRAT_44` in `lv_conf.h` — the only baked custom fonts were 12/14/17px or FontSpeed's 86px hero size, nothing in between for a quad-grid's secondary numbers.
- Some fields lost their display in this redesign vs. the interim milestone-1 retexture: `gear`, `motActive`, `aux12vVoltage`, `regenMax`, `opmode`, `lastErr` have no widget on either new screen (deliberately focused scope per the new design; broadcast/SDO decode of them is unaffected, just not shown).
- Real, pre-existing bug fixed in passing: the original SquareLine-generated `_ui_screen_delete()` in `ui_helpers.c` has an inverted condition (`if (*target == NULL)` instead of `!= NULL`) so it never actually frees a screen — screens accumulate in memory instead of being recreated. Left as-is (not touched) since "always resident once visited" is empirically safe at the current 3-screen count and fixing it correctly would need careful NULL-guard auditing I can't verify without seeing the device; **flagging it here** since a 4th+ screen or more RAM-hungry screens later should revisit this.
- **CLAUDE.md written** (repo root) codifying the UI/style rules established this session: widget selection (never `lv_slider` for read-only telemetry; `lv_arc` for rings; `lv_meter` for speedometer/tach-style needle+tick gauges; `lv_bar` for linear strips, `SYMMETRICAL` mode for signed values), the day/night theming contract (creation-time + `refresh_theme()` for static chrome, re-themed on every data update for value labels), and the quad-grid layout convention.
- **Compliance pass against those rules** (same day): the Speed panel was plain text with no gauge at all - added an `lv_meter` speedometer (needle + 0-200 km/h tick scale) behind the digital readout, same "gauge behind, number in front" composition as SOC's arc. Power and Motor Temp panels were also plain text - added `lv_bar` strips (`SYMMETRICAL` mode, signed ranges) below their digital readouts. BMS screen already complied (arc + bars) and needed no changes.
- **Not yet visually confirmed on hardware** — builds clean and boots without crashing, but the quad-grid layout, BMS screen, meter/bar additions, and day/night toggle all need eyes-on confirmation from Rob (arc/meter geometry, panel sizing, font legibility, theme colors were all designed without any visual feedback loop).
- **Settings screen rebuilt (2026-09-13) per the expanded CLAUDE.md** — full replacement of the rusEFI stepper layout with `lv_menu` (3 sections: Warning Thresholds, Display, Calibration), hand-written like `ui_mainScreen.c`/`ui_bmsScreen.c` (no SquareLine project, no `events.cpp` indirection - had to remove `setIsTurbo`/`setIsNaturalA`/the 14 warning-stepper functions/`preInitSettingsScreen`/`saveWarnSet`/`defaultWarnSet` from `events.cpp`/`ui_events.h` since the new screen owns its own handlers directly). Highlights:
  - Warning Thresholds: `lv_spinbox` (non-clickable, +/- button driven only, accelerating hold via `LV_EVENT_LONG_PRESSED_REPEAT` scaling the step size) for low SOC, high motor temp, high heatsink temp, low pack voltage (this one uses `lv_spinbox_set_digit_format` for a virtual decimal point - stored/compared in deciVolts). Explicit Save button (disabled until dirty), `lv_msgbox` confirmation, NVS write only on Save - unchanged philosophy from the original screen, new implementation.
  - Display: day/night is now **tri-state** (Auto/Day/Night, not just a bool) - `display_pref_t` in `zombie_updaters.h`, applied+persisted immediately (not gated behind Save). **Auto currently always resolves to Day** - there's no RTC or light sensor on this board (see `resolveDisplayPref()` in `zombie_updaters.cpp`, a single documented TODO stub); the 3-way UI and persistence are real, only the resolution logic is a placeholder pending real sensor hardware. Brightness got its own real per-mode memory (`ui_theme_set_brightness()`/`ui_theme_get_day_brightness()`/`_night_brightness()` in `ui_theme.h`) so day and night each remember their own level independently.
  - Calibration: a disabled stub row (the dyno screens don't exist yet).
  - **uaDASH's swipe-up/down brightness gesture is now actually wired** (it wasn't before - `upBrightness`/`downBribrightness` existed in `events.cpp` since milestone 1 but were never attached to anything) - added to `ui_mainScreen.c`'s gesture handler, TOP/BOTTOM swipe, routed through the same per-mode brightness memory.
  - `ui_settingsScreen.c` had to be renamed `.cpp` (same root cause as `ui_theme.c` earlier: it now includes `zombie_updaters.h` directly instead of going through `events.cpp`'s indirection, and that header pulls in C++-only headers like `Ticker.h`/`Preferences.h`).
  - **Untested on hardware beyond "compiles and the rest of the app still boots"** - the settings screen only gets built the first time it's navigated to (lazy init, see `ui.c`), so none of this new code - the menu, spinboxes, slider, accelerating buttons - has actually run on the device yet. Needs a swipe-right-from-main test.
- **Typography migration (2026-09-13, CLAUDE.md rule)** - removed uaDASH's original custom fonts entirely: `ui_font_FontIndicator`, `-IndicatorLabel`, `-Label`, `-RPM`, `-Speed` (stylized SuperBrigade/Fussion racing-dashboard faces per their own generator-comment metadata - genuinely the likely source of the "italic" look flagged earlier in the session). All 5 `.c` files deleted, `LV_FONT_DECLARE`s removed from `ui.h`. Every screen now uses LVGL's built-in Montserrat at 16 (labels/titles/units), 24 (section headers / the tight BMS status pill), 32 (standard values), 48 (hero numerics - SOC on both the main and BMS screens). `lv_conf.h` updated accordingly (kept 14 as `LV_FONT_DEFAULT`). Net flash usage actually **dropped** ~78KB (26.7% -> 24.2%) even after adding 4 new sizes - `FontSpeed.c` alone was 849KB of source for an 86px font used in exactly one place.
- While migrating, fixed two real issues the new CLAUDE.md's "Verification habit" section prompted a check for:
  - **SOC value label could exceed its arc.** At the old 86px FontSpeed, "100%" in a 180px arc (148px inner diameter after the 16px stroke) was a real overflow risk. Fixed by dropping the inline "%" (digits only, "100" not "100%" - the panel title already says "SOC") on both the main screen's and BMS screen's SOC labels, plus sizing down to the 48px hero tier.
  - **BMS SOC label was mis-centered** - `ui_bmsSocValLabel` was positioned via an eyeballed `y=70` offset from the screen top rather than the arc's actual vertical center (`y=110`, since the arc is at `y=10` and 200px tall); fixed to align to the arc's real center.
  - Also widened the BMS bar-row value labels (130px -> 160px) and the settings spinbox (130px -> 150px) since the bumped-up value font (26px -> 32px) made the old widths tight enough to risk truncation on the widest cases ("-150.0 A", "280.0").
  - Flagging honestly: the CLAUDE.md text that prompted this pass asserts these were bugs "first-pass hardware testing already caught" - but there is no splash screen or splash-graphic asset anywhere in this codebase (grepped clean), so the specific claim about a screen "falling back to the splash graphic" can't describe anything that's actually happened here. Fixed the arc-label and centering issues proactively since they're real, verifiable risks in the code regardless of that claim's accuracy - asked Rob to clarify what was actually observed.
- **0-60 / virtual dyno screens built (2026-09-14)** — new `ui_dynoLiveScreen.{h,cpp}` and `ui_dynoResultsScreen.{h,cpp}`, per CLAUDE.md's spec (two separate LVGL screens, not one with hidden content). Reachable via BMS screen swipe-LEFT (new); LIVE swipes RIGHT back to BMS, RESULTS swipes RIGHT back to LIVE to run again.
  - **LIVE**: state machine READY (tap to arm) -> ARMED -> RUNNING -> DONE, driven by its own `lv_timer` (100ms) rather than the shared `zombie_updaters.cpp` Tickers - runs inside `lv_timer_handler()`, which `loop()` already wraps in `uiMutex`, so no extra locking needed beyond `dataMutex` to read `myData`. Dominant elapsed-time numeral (48px hero), secondary speed readout, colored state-badge pill. Label updates only, matching CLAUDE.md's "never refresh a chart on this screen".
  - **Launch detection is speed-based, not accelerometer-based** — armed + speed departs above ~2 kph starts the timer, crossing 96.6 km/h (60 mph) stops it. Documented placeholder for the architecture doc's IMU-based design; swappable later without touching the state machine shape.
  - **RESULTS** genuinely computes both curves from already-validated real telemetry, not placeholder data: electrical power = `packVoltage * packCurrent`; road-load power = `accel(dv/dt of recorded speed samples) * mass * velocity` (`DYNO_VEHICLE_MASS_KG` placeholder constant, TODO-tune) - no IMU needed for this approximation since acceleration is derived from the same speed samples already being recorded. `LV_CHART_TYPE_SCATTER`, shared kW Y-axis, built once via direct `lv_chart_get_x/y_array` writes + one `lv_chart_refresh()` (not per-point, per the performance note), manually-built legend (`lv_obj_align_to`), peak-power callouts, computed efficiency % (avg road-load / avg electrical).
  - Run samples capped at `DYNO_MAX_SAMPLES` = 100, shared via new `dyno_data.h` (both screens are `.cpp` from the start this time - same `zombie_updaters.h` / C++-headers reason as `ui_theme.cpp`/`ui_settingsScreen.cpp`).
  - Settings screen's "Calibration" stub comment updated to be precise: LIVE + RESULTS are now built and functional; the stub is specifically the separate correction-constant calibration workflow (enter real chassis-dyno numbers, solve for mass/Cd·A/efficiency scalars), which is still unbuilt.
  - **Untested on hardware** — builds clean, boots clean, but both screens are lazy-created on first navigation (BMS swipe-left), so none of the state machine, timer, sampling, or chart code has actually executed on the device yet.
- **Navigation confirmed fully working on hardware (2026-09-14)** — Rob reported "no BMS screen"; turned out every screen *is* reachable (settings → main → BMS → dyno LIVE, cycling on repeated swipes), just via the opposite physical direction from what the code comments claimed. Root-caused, not guessed at: confirmed via a tap test that raw touch *position* is correct (a button tap at its actual location activates that exact button), so **this board's touch/LVGL setup reports gesture direction inverted from the physical swipe** — a physical rightward swipe fires `LV_DIR_LEFT`, and vice versa. Deliberately did NOT touch `TOUCH_ROTATION`/the touch driver config to "fix" this, since that would risk breaking the tap accuracy that's already confirmed correct, to fix something that's purely a gesture-*label* mismatch. Fixed by correcting every screen's navigation comments to state the true physical direction (documented centrally in `CLAUDE.md`'s new "Touch gesture direction" section) — the actual `LV_DIR_LEFT`/`LV_DIR_RIGHT` gesture-matching code was **not** changed, since it already produces working navigation; a first attempt at "fixing" it by swapping the constants was caught and reverted before flashing, since that would have inverted the navigation the user had already learned rather than fixed anything.

**Speed/Drive/Status/Battery screen split (2026-09-14):** Rob asked what the
old quad-grid ("4-card screen") and BMS screen were actually supposed to
show, pointed out he didn't want motor/inverter temps duplicated onto the
BMS screen, and asked for genuinely cell-level BMS fields instead (max/min
cell voltage, cell delta-V, max cell temp) plus a static-vs-in-motion split
for the rest of the telemetry, with Speed promoted to its own full-screen
home. Discussed and confirmed before building (topology: `Settings <->
Speed(home) <-> Drive <-> Status <-> Battery <-> Dyno LIVE -> Dyno
RESULTS`), then implemented in full:
- **New SDO params polled**: `PARAM_ID_DIR` (2024, the P/N/D/R shifter
  position — distinct from the pre-existing `PARAM_ID_GEAR`/27, which is
  ZombieVerter's own LOW/HIGH/AUTO reduction-gear setting), `PARAM_ID_BMS_VMAX`
  (2085), `PARAM_ID_BMS_VMIN` (2084), `PARAM_ID_BMS_TMAX` (2087). Poll table
  grew from 14 to 18 entries (~700ms -> ~900ms full refresh). `struct_message`
  gained `dirState`/`cellVMax`/`cellVMin`/`cellTMax`; cell delta-V is
  computed (`cellVMax - cellVMin`), not a separate param.
- **`ui_mainScreen.{h,c}` -> `ui_speedScreen.{h,c}`** (renamed, still the
  HOME screen `ui_init()` creates eagerly): full-screen `lv_meter`
  speedometer displaying **mph** (converted from `myData.vehSpeedKph` at
  display time only — the field itself stays in kph since the dyno screens
  depend on it), plus a P/N/F/R pill sourced from `dirState` (Drive is
  shown as "F" for Forward per how Rob phrased the request, even though the
  underlying enum comment says "Drive").
- **New `ui_driveScreen.{h,c}`**, 2x3 grid: Power and Pack Current as
  `lv_bar` strips (moved off the old BMS screen), Gear Selection and Motor
  Mode as plain enum-text panels (no bar — these are discrete states, not
  continuous quantities), Regen Limit as a bar, one spare slot deliberately
  left empty for a future field.
- **New `ui_statusScreen.{h,c}`**, 2x3 grid: SOC (a second, smaller `lv_arc`
  — genuinely useful at-a-glance alongside the Battery screen's larger one),
  Pack Voltage, Aux/12V Voltage, Motor Temp, Inverter Temp, Max Battery Temp.
- **`ui_bmsScreen.{h,c}` -> `ui_batteryScreen.{h,c}`** (renamed): kept the
  SOC ring + charge/discharge/idle status pill, **dropped** the old
  pack-voltage/pack-current bars (moved to Status/Drive), **added** 4
  genuinely cell-level bars: Max Cell Voltage, Min Cell Voltage, Cell Delta
  V (computed), Max Cell Temp. Cell voltage bars carry centivolts
  (`value*100`) internally for resolution out of `lv_bar`'s integer range.
  Pack current still feeds the status pill's charge/discharge color even
  though it's no longer displayed as a bar on this screen.
- **Navigation rewired** end to end (`ui.h`/`ui.c`/`ui_theme.cpp`/
  `ui_settingsScreen.cpp`/`ui_dynoLiveScreen.{h,cpp}`/`events.cpp` comments)
  to the confirmed topology above, preserving the existing gesture-direction
  inversion convention (see CLAUDE.md) without touching the touch driver.
- **`zombie_updaters.cpp`'s `fastUpdate`/`midUpdate`/`slowUpdate`** fully
  re-mapped across the four screens' widgets; `dirState` added to the
  20ms-tier alongside speed (shifter feedback matters while driving),
  `gear`/`motActive`/`regenMax`/`aux12vVoltage`/the 3 new BMS fields added
  to the 800ms slow tier. Cell delta-V is computed from a same-mutex
  snapshot of both `cellVMax`/`cellVMin` (not read from `myData` directly
  outside `dataMutex`) so it's never torn between an old and a new value.
- Builds clean (`RAM 54.7%`, `Flash 24.4%`).
- **Flash/verify hit a real, unrelated recovery incident**: the first
  upload attempt via the Bash tool's background-task path silently hung
  (0 bytes of output, task status stuck at "running") — turned out to be
  the *already-documented* `UnicodeEncodeError` from the "Resolved
  PlatformIO build issues" table above recurring (forgot to set
  `PYTHONUTF8=1` this time), which crashed pio's console-echo thread
  mid-flash, **after** flash erase had started but **before** the
  bootloader/partition write completed — leaving the board's flash
  genuinely corrupted (not a code bug), which is why the screen went
  blank. Recovered by: killing the stale `pio`/`python` processes left
  holding COM6, re-verifying board identity via `esptool flash-id` (MAC
  `28:84:85:82:78:14`, standing practice per the M5 Dial mixup earlier in
  the project), then re-running the upload with `PYTHONUTF8=1
  PYTHONIOENCODING=utf-8` set — completed cleanly this time, full
  erase+write+verify, hard reset. **Lesson reinforced**: this project's
  own documented gotchas can still bite if skipped; always set the UTF-8
  env vars before any `pio run -t upload`.
- **Visual layout confirmed on hardware (2026-09-14)** — Rob confirmed the
  new Speed/Drive/Status/Battery layout renders correctly. But: "I do not
  see any values populating though. I would expect to see at least the
  motor mode show up correctly" (bench setup, no contactors, so Run mode
  is unreachable).
- **Root-caused via the `[env:cantrace]` diagnostic build** (not guessed
  at): flashed `cantrace`, captured a live serial trace. SDO comms were
  working perfectly the whole time — `SDO< 2015 raw=3200 -> 100.000`
  (soc=100), `SDO< 27 raw=32 -> 1.000` (gear=1/HIGH), `SDO< 2029
  raw=-887 -> -27.719` (motorTemp, open-thermistor negative reading,
  expected with no motor attached), `SDO< 129 raw=0 -> 0.000`
  (motActive=0, a **real, valid** MG1+MG2 reading) all round-tripped
  correctly. The bug was in the new `fastUpdate`/`midUpdate`/`slowUpdate`
  dirty-check logic, not CAN/SDO: those functions only push a value to a
  widget when it *changes* from the previous poll
  (`myData.X != old_myData.X`). That's fine for a screen that already
  exists, but Drive/Status/Battery are created **lazily** (first swipe
  there), well after `old_myData` had already synced to the bench's
  static values (no contactors closed -> most fields genuinely flat: 0 A,
  0 V, `motActive=0`, `dirState=0`). By the time a screen got created,
  its fields had already stopped changing, so the freshly-created
  widgets sat at their init placeholders ("--", "0") forever — the
  correct value was sitting right there in `myData`, it just never got
  pushed to a widget that didn't exist yet when the last real change
  happened. This is a general architectural gap (any lazily-created
  screen, any field that stabilizes before the screen is first visited),
  not specific to Motor Mode — it was simply the most visible instance
  since `motActive=0` on the bench is a real reading with no natural
  jitter to mask the bug, unlike e.g. temperature readings.
- **Fix**: added `uiGeneration` (`zombie_updaters.h`/`.cpp`), a counter
  bumped by `ui_notify_screen_created()`. `fastUpdate`/`midUpdate`/
  `slowUpdate` each track the last generation they pushed at in a
  `static` local; when the counter has moved on, that tick does one full
  *unconditional* push of every field to every currently-existing widget
  (dirty check bypassed, not removed — normal cadence-throttled behavior
  resumes immediately after), then remembers the new generation. Starts
  at 1 (not 0) so the very first tick after boot also does a full push,
  not just ones after a lazily-created screen. Hooked into the **single
  existing choke point** for all lazy screen creation,
  `_ui_screen_change()` in `ui_helpers.c` — renamed to `.cpp` (same
  reason as `ui_theme.cpp`/`ui_settingsScreen.cpp`: needs
  `zombie_updaters.h`, which needs `Ticker.h`/`Preferences.h`), which
  required 3 small explicit casts (`int`->`lv_anim_enable_t` x2,
  `void*`->`lv_obj_t**` x1) that C's implicit conversions had been
  silently covering for since this file was originally SquareLine-
  generated. `ui_helpers.h`'s `extern "C"` guards mean none of the `.c`
  screen files that call `_ui_screen_change()`/`_ui_screen_delete()`
  needed any changes. Builds clean, flashed and verified (full
  erase+write+verify, hard reset) — **confirmed on the bench (2026-09-14,
  Rob: "looks good")**: Drive/Status/Battery now show correct values
  (SOC=100, gear=HIGH, motorTemp=-27°C, Motor Mode=MG1+MG2, etc.) as soon
  as each screen is first visited.

**Charging screen added (2026-09-14):** Rob asked for a screen showing
charge setpoint, current SOC, whether Op Mode is Charge, and charger
temperature, plus "Maybe PPVal?" and an open "anything else?" Rather than
guess at param mappings for a charging-safety-adjacent screen, checked
`zombieverter_params.h` and asked before building:
- **"PPVal"**: no raw Proximity Pilot value is exposed by this VCU. The
  two real candidates are `CableLim` (2048, A — PP-derived cable rating)
  and `PilotLim` (2049, A — CP-derived current the EVSE is currently
  offering). Both ended up in the screen (PilotLim primary, CableLim
  secondary).
- **"Charge setpoint"**: none of the plausible-looking candidates
  (`CCS_SOCLim`, `BMS_ChargeLim`, `BMS_MaxCharge`) were right — Rob
  confirmed it's **`Voltspnt`** (40, V): this VCU charges to a target pack
  *voltage*, not a target SOC%. Good thing this was asked rather than
  assumed.
- Rob also picked up all 4 of the proposed extras: charge type (AC/DCFC),
  plug-detected pill, AC supply voltage, and the cable/EVSE current limit.
- **New SDO params polled** (poll table 18 → 25 entries, ~900ms → ~1250ms
  full refresh): `PARAM_ID_VOLTSPNT` (40), `PARAM_ID_CHGTEMP` (2078),
  `PARAM_ID_CHGTYP` (2003), `PARAM_ID_PLUGDET` (2050), `PARAM_ID_AC_VOLTS`
  (2079), `PARAM_ID_PILOTLIM` (2049), `PARAM_ID_CABLELIM` (2048).
  `struct_message` gained `chargeSetpointV`/`chgTemp`/`chgType`/`plugDet`/
  `acVolts`/`pilotLimA`/`cableLimA`.
- **New `ui_chargingScreen.{h,c}`**, 2x3 grid (same 388x149 panel geometry
  as Drive/Status): SOC (small `lv_arc`), Charge Status (pill + secondary
  plug-detected label — combines opmode/chgType/PlugDet into one cell
  rather than needing 3 separate cells), Charge Setpoint (`lv_bar`),
  Charger Temp (`lv_bar`, signed), AC Supply Voltage (`lv_bar`),
  Cable/EVSE Limit (`lv_bar` for PilotLim + a secondary text label for
  CableLim).
- **Caught one real bug before it shipped**: `createValueAndUnit()`
  (copied from `ui_driveScreen.c`/`ui_statusScreen.c`'s pattern) wrote
  unconditionally through `*outUnit` with no NULL guard; the new Charging
  screen calls it with `outUnit=NULL` in several places (unit label
  pointer not needed since it never changes). Added `if (outVal)`/
  `if (outUnit)` guards before the writes - would have been a null-pointer
  crash on the very first screen visit otherwise.
- **Inserted into the nav chain** between Battery and Dyno LIVE:
  `Settings <-> Speed(home) <-> Drive <-> Status <-> Battery <-> Charging
  <-> Dyno LIVE -> Dyno RESULTS`. Updated `ui_batteryScreen.c` (forward
  swipe target) and `ui_dynoLiveScreen.{h,cpp}` (backward swipe target)
  accordingly, plus `ui.h`/`ui.c`/`ui_theme.cpp` for the new screen.
- `slowUpdate()` in `zombie_updaters.cpp` extended with the 7 new fields;
  Charge Status combines opmode+chgType+plugDet changes into one
  `chgStatusChanged` flag (mirrors the existing cellVMax/cellVMin
  same-mutex-snapshot pattern) so `ui_chargingScreen_setStatus()` is
  called with a consistent triple, not a torn one.
- Builds clean. Benefits from the `uiGeneration` force-push fix (same
  milestone above) automatically - no separate work needed to make this
  screen's widgets seed correctly on first visit.
- **Not yet flashed/verified on hardware** as of writing this entry -
  next step.

**IMU driver added (2026-09-14), GPS wiring blocked on pin confirmation:**
Rob ordered the IMU (MPU6050), GPS module, and an automotive 12V->5V buck
converter per the architecture doc's hardware table, and asked to start
wiring up the code before the hardware physically arrives.
- **IMU pins**: confirmed via `docs.waveshare.com/ESP32-S3-Touch-LCD-7`
  (fetched directly, not guessed) that GPIO8 (SDA) / GPIO9 (SCL) - the same
  bus the touch controller and CH422G I/O expander already use - is
  explicitly documented as available for "external I2C devices" on this
  board. Confirms the plan: share the existing bus, don't add a second one.
- **New `imu_driver.h/.cpp`**: MPU6050 driver using LovyanGFX's legacy
  `lgfx::i2c::` primitives (`writeRegister8`/`readRegister`), the exact
  same pattern as `display_driver.cpp`'s CH422G code - never Arduino
  `Wire`, for the same boot-loop-conflict reason documented there.
  `imu_init()` wakes the chip and checks its WHO_AM_I register (returns
  false harmlessly if nothing's wired up yet - safe to flash and run
  before the IMU physically arrives). `imu_start_sampling()` runs a ~50Hz
  Ticker populating a new `imuData` struct (accel g's, gyro deg/s),
  guarded by `dataMutex` (same convention as `myData`).
- **Real concurrency finding, not just plumbing**: the IMU shares its I2C
  bus with the touch controller, whose reads happen inside
  `lv_timer_handler()` - which `firmware.ino`'s `loop()` already wraps in
  `uiMutex`. IMU sampling has to take that SAME `uiMutex` around its I2C
  transactions (not a new lock) or it would race the touch controller for
  the bus. Documented prominently in `imu_driver.h` so this isn't
  "corrected" away later.
- **Deliberately scoped to Phase 1 (driver + sampling) only** - does NOT
  yet feed `ui_dynoLiveScreen.cpp`'s launch detection or acceleration
  curve, which still use the documented speed-derived placeholder.
  Swapping that over needs a real design decision (how to reconcile this
  driver's 50Hz sampling with the dyno screen's existing 100ms UI timer -
  buffer and integrate, or resample) that's better made once the IMU is
  actually in hand and its noise characteristics are known, not guessed
  at now. Flagged as a deliberate follow-up, not an oversight.
- Wired into `firmware.ino`'s `setup()`: `imu_init()` +
  `imu_start_sampling()` called right after `lcd_panel_start()` (which is
  what brings up I2C_NUM_0 on this board), before `lv_init()`.
- Builds clean, flashed and verified boots (MAC `28:84:85:82:78:14`
  reconfirmed before flashing, per standing practice).
- **GPS/UART1 pin question resolved by Rob's board manual, not guessed**:
  the architecture doc's hardware table just said "Waveshare UART1" with no
  GPIO numbers, and both Waveshare doc pages fetched were unusable (one
  403's the fetch tool, the other gave a self-contradictory answer). Rob
  supplied the actual manual text, which turned out to be a genuinely
  different situation than assumed: **"UART1" and "UART2" on this board
  are NOT two separate UARTs** - both are the same ESP32-S3 UART0
  peripheral (GPIO43 TX / GPIO44 RX), routed by a physical DIP switch to
  either the onboard CH343 USB-C chip (flashing, `pio device monitor`) or
  a direct external-module header - mutually exclusive, switch-selected,
  not simultaneous. Asked Rob to pick between that shared header and the
  RS-485 header (GPIO15/16, genuinely independent, but possibly behind an
  RS-485 transceiver chip requiring bypass) - he chose the shared UART0
  header, accepting the DIP-switch tradeoff (flip to USB to reflash/
  monitor, flip to the header for GPS to actually receive data while
  driving).
- **New `gps_driver.h/.cpp`**: uses `Serial` (UART0) directly at 9600 baud
  (NEO-6M/8M's factory default), parsed with TinyGPSPlus (new
  `platformio.ini` lib_dep, `mikalhart/TinyGPSPlus@1.1.0` - pure C++, no
  low-level driver hooks, none of this project's I2C/Wire conflict class
  of bug applies). New `gpsData` struct (lat/lon/speed/heading/altitude/
  satellite count/UTC date-time), guarded by `dataMutex`, same convention
  as `myData`/`imuData`. `gps_init()`/`gps_poll()` both deliberately no-op
  under `DEBUG`/`CAN_TRACE` builds, since those already claim `Serial` at
  115200 for debug/trace output on this same physical port - trying to run
  both would just be two things fighting over one wire at two baud rates,
  a hardware fact this firmware can't paper over.
- Wired into `firmware.ino`: `gps_init()` near the top of `setup()`
  (doesn't depend on I2C/display, called before `lcd_panel_start()`),
  `gps_poll()` added to `loop()` after the existing `lv_timer_handler()`
  block (cheap, non-blocking Serial drain, no mutex conflict with the
  IMU's uiMutex usage since GPS never touches I2C).
- **Deliberately scoped to driver + NMEA parsing only** - does NOT yet
  build the GPS navigation screen (IceNav-v3-style map rendering needs map
  tile assets that don't exist in this repo) or wire `gpsData` into a
  Clock screen or the day/night Auto-mode resolution TODO in
  `resolveDisplayPref()` (zombie_updaters.cpp) - both are natural
  follow-ups, flagged rather than built without a discussion first (same
  "discuss before building a new screen" pattern as every other screen
  this session).
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC `28:84:85:82:78:14` reconfirmed before flashing,
  per standing practice). GPS itself can't be tested yet - the module
  hasn't physically arrived - but the driver safely produces an all-zero
  `gpsData` with `hasFix=false` until real NMEA sentences start arriving.

**Dead-code / carryover cleanup pass (2026-09-14):** Rob asked for a full
project audit for old uaDASH/rusEFI carryover files and redundant code,
while waiting on the GPS/IMU/buck-converter hardware to arrive. Verified
every deletion by grep before removing anything - nothing below was
assumed dead, it was confirmed unreferenced:
- **Deleted, zero references anywhere**: `firmware/can_common.h` (rusEFI's
  GDI4/bench-test CAN protocol constants - our CAN code lives entirely in
  `can_sdo.h`/`can_broadcast.h`/`zombieverter_params.h` now), `firmware/
  src/ui_comp_hook.c` (an empty SquareLine-generated placeholder - 4 lines,
  no code), `firmware/0-setup.bat`/`1-compile.bat`/`2-upload.bat` (the
  pre-PlatformIO arduino-cli workflow - actively misleading now, e.g.
  `0-setup.bat` still installs `ESP32_IO_Expander`, the library this
  project deliberately removed for the I2C driver-conflict boot loop), and
  the repo-root `squareline_prj/` directory (SquareLine Studio projects for
  `bench_screen` and `warn_screen` - both screens were fully deleted
  earlier this project and every current screen is hand-written per
  CLAUDE.md, no SquareLine round-trip happens anymore).
- **Trimmed to just what's used**: `ui_helpers.h`/`.cpp` (SquareLine
  codegen boilerplate covering every widget type SquareLine can emit -
  dropdown/roller/textarea/keyboard property setters, a full animation-
  timeline callback set, flag/state modifiers, etc. - grepped every one of
  ~39 functions across the whole src tree; only `_ui_screen_change`,
  `_ui_screen_delete`, and `_ui_spinbox_step` are actually called anywhere.
  The other ~36, including the already-known-dead `_ui_switch_theme()`/
  `UI_THEME_ACTIVE` stub `ui_theme.h` has referenced in a comment since
  milestone 3, are gone) and `ui_events.h` (~35 declarations for rusEFI
  bench-test/engine-config event handlers that were deleted from
  `events.cpp` back in the settings-screen rebuild, leaving dangling
  prototypes with no definition anywhere - trimmed to the 2 that still
  have one, `upBrightness`/`downBribrightness`).
- Updated `ui_theme.h`'s comment that referenced the now-deleted
  `squareline_prj/`.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean after
  every removal (checked incrementally, not just at the end). Flash usage
  barely moved (~840 bytes) - this pass was about removing confusing dead
  weight and dangling references, not a size optimization. Flashed and
  reboots clean (MAC reconfirmed per standing practice).
- **Findings surfaced but NOT acted on** (judgment calls belonging to Rob,
  not pure dead-code removal):
  - `kSdoParams`/`kTelemetryParams` in `zombieverter_params.h` are never
    iterated by any running code (unlike `kCanBroadcastParams`, which
    `decodeBroadcastFrame()` genuinely loops over) - but they're a
    genuinely useful param-ID reference catalog (used directly, by hand,
    to answer the Charging screen's "which param is this" questions
    earlier this session). Since `zombieverter_params.h` is included from
    multiple `.cpp` translation units via `zombie_updaters.h`, these two
    tables get duplicated into each one - modest, harmless flash bloat
    given current headroom (~75% free), not worth a riskier extern/single-
    definition refactor unless flash ever gets tight.
  - Repo-root `connector_adapter/` (KiCad PCB for a 5" board variant) and
    `enclosure/` (STL panels) are physical hardware design files, not
    code - left alone, since deleting hardware assets is a different kind
    of decision than deleting confirmed-dead source.
  - `media/` (repo-root) holds screenshots of the ORIGINAL rusEFI uaDASH
    UI (`main_screen.png`, `bench_screen.png`, `settings_screen.png`, etc)
    - none of it matches this project's actual current UI anymore.
  - `README.md` still describes the original rusEFI-based uaDASH project;
    `README-ESP32-S3-Touch-LCD-5.md`/`README-GUITION.md` document board
    variants this fork doesn't target. All left alone - rewriting the
    project's public-facing description is a content/branding decision,
    not a cleanup one.
  - `firmware/current_date.sh` generates `build_info.h`'s `DASH_TAG` - but
    it's not wired into `platformio.ini` as a build step, and it writes to
    the OLD pre-PlatformIO path (`firmware/build_info.h`) rather than
    `firmware/src/build_info.h` where the real one now lives, so running
    it today would silently create a second, wrong-location file instead
    of updating the real one. `build_info.h` itself currently just hard-
    codes `DASH_TAG "DEV"`. Left alone - whether to fix the script's path,
    wire it into the build, or just keep "DEV" is a product decision about
    whether date-tagged builds matter, not implied by "clean up dead code."

**Follow-up cleanup pass, same day**: Rob asked to go ahead and act on
three of the deferred findings above.
- **`firmware/current_date.sh` deleted** outright, per Rob ("looks like it
  should go too").
- **`media/` reduced to one file**: deleted all 15 screenshots/photos of
  the original rusEFI uaDASH UI and build process (none of it matches this
  project anymore) except `upgrade_for_brightness_7.png` - that one
  documents a physical solder point on the exact board this project
  targets (Waveshare ESP32-S3-Touch-LCD-7), not the old UI, so it's kept
  and now referenced with a real local relative path from `README.md`
  (every image in every README, including this one, previously hotlinked
  the upstream `Light-r4y/dash5_esp32s3` repo's raw GitHub URLs instead of
  using local files at all - fixed for the one image kept).
- **`README-ESP32-S3-Touch-LCD-5.md` and `README-GUITION.md` deleted** -
  board variants (Waveshare 5", GUITION JC8048W550C) this fork doesn't
  target. **`README-ESP32-S3-Touch-LCD-7.md` deleted too**, but its one
  useful note (the brightness solder mod) was folded directly into the
  main `README.md` rather than left as a separate one-paragraph file.
- **`README.md` fully rewritten**: the old version described the original
  rusEFI project (Arduino-CLI build instructions, an FAQ literally
  answering "uaDASH consumes 'rusEFI CAN broadcast' traffic" - actively
  wrong now), listed 3 supported boards down to 1, and every image
  hotlinked the upstream fork's repo instead of this one. New version
  describes the actual project (Scout 80 EV dash, ZombieVerter CANopen
  SDO, 10 screens), points to `CLAUDE.md`/`scout80-dash-architecture.md`/
  `waveshare-dash-build.md` for the real detail rather than duplicating
  it, gives the real PlatformIO build commands, restates the MAC-check-
  before-flash practice, and keeps proper fork attribution (MIT,
  Light-r4y/uaDASH).
- No firmware code touched in this follow-up pass - docs/assets only, no
  rebuild/reflash needed.

**GPS telemetry screen + real day/night Auto resolution (2026-09-14):** Rob
asked what else could be coded on the GPS side while waiting for parts,
specifically floating OpenStreetMap/IceNav-v3. Answered honestly rather
than just building it: full offline map rendering needs real map tile data
for wherever the vehicle will actually drive (a data-acquisition decision,
not a coding one), SD card wiring this repo has never touched, and IceNav-
v3 itself is a full standalone project (M5Stack-targeted) that CLAUDE.md
treats as a rendering-approach reference, not a drop-in library - scoped
that as a future design pass once GPS is validated on real hardware, not
now. Instead built the two things that genuinely don't need any of that:
- **New `ui_gpsScreen.{h,c}`**, 2x3 grid (same geometry as Drive/Status/
  Charging): GPS Status (pill + secondary "Sats: N" label), Speed
  (GPS-derived - the cross-check the architecture doc's dyno design calls
  for), Latitude/Longitude (plain formatted values, no bar - not a
  progress-style quantity, same reasoning as Drive screen's Gear/Motor
  Mode text cells), Heading, Altitude. Inserted into the nav chain between
  Charging and Dyno LIVE: `... <-> Charging <-> GPS <-> Dyno LIVE -> Dyno
  RESULTS`. Bound in `zombie_updaters.cpp`'s `slowUpdate()` (GPS fixes
  update at most ~1Hz, no need for a faster tier) - reuses the dataMutex
  critical section already open for `myData` rather than a second lock
  cycle, with its own small `old_*` static snapshot since `gpsData` has no
  `old_gpsData` sibling the way `myData`/`old_myData` do.
- **Real day/night Auto-mode resolution**: `resolveDisplayPref()`'s
  hardcoded "Auto=Day" TODO stub (since milestone 3) now computes actual
  sunrise/sunset from `gpsData`'s position + UTC time, using the Cooper
  (1969) solar declination approximation - deliberately the simple
  formula, not the precise Julian-Day astronomical method, since this only
  picks between two backlight palettes, not navigating a ship. **Verified
  the longitude sign convention by hand before trusting it** - fetched
  multiple web sources for the "precise" sunrise equation first and got
  inconsistent, fragmentary answers that couldn't be fully reconciled
  across separate AI-summarized fetches, so deliberately switched to the
  simpler Cooper formula instead and sanity-checked it against a known
  reference (solar noon in New York, ~74°W, should land at roughly
  11:56am local - confirmed) rather than trust an AI-summarized fetch on
  a correctness-sensitive sign convention. Falls
  back to the pre-existing "Auto=Day" behavior whenever `gpsData.year <
  2020` (the simplest possible validity check on what is otherwise a
  zero-initialized, never-populated struct before a real fix arrives).
  New `checkAutoTheme()` on its own 60-second `Ticker` (`PERIOD_AUTO_THEME_MS`)
  re-resolves Auto mode periodically, but only actually calls
  `ui_theme_set()` (which unconditionally repaints every existing screen's
  chrome) when the resolved palette has genuinely changed - once per
  sunrise/sunset crossing, not every tick.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC reconfirmed per standing practice). Neither
  piece has live data to test against yet - the GPS module hasn't
  physically arrived - but both render/compute correctly against
  `gpsData`'s safe all-zero default state.

**Design-review prototype: motion + battery-shape SOC on Charging screen
(2026-09-14):** Asked to act as lead visual designer and critique the
whole UI's style. Full critique (not repeated here - see the conversation):
core finding was that every screen is visually the same 2x3 card grid
wearing different numbers, with zero brand identity, zero motion (every
bar/value update was `LV_ANIM_OFF`, called out explicitly), zero
iconography, and no "hero" visual treatment for anything except the Speed
screen's meter. Agreed to prototype 2-3 ideas on ONE screen (Charging -
highest emotional payoff, least visual delivery) before rolling anything
out further:
- **SOC replaced with a drawn battery silhouette**, not an `lv_arc` ring
  like every other SOC readout in this codebase: a body (`lv_obj`, rounded
  rect), a terminal nub (small rect on the right edge), and an animated
  fill rect inset inside the body that tweens its width via a custom
  `lv_anim_t` (`lv_anim_path_ease_out`, 600ms) whenever SOC changes. The
  percentage moved to a plain 48px hero number beside the shape rather
  than overlaid on it - sidesteps the arc-containment problem in this
  codebase's other SOC labels entirely by not sharing space with the
  graphic at all. New `ui_chargingScreen_setSoc(soc, warn)` encapsulates
  the fill animation + label + color in one call, mirroring the existing
  `ui_chargingScreen_setStatus()`/`ui_batteryScreen_setStatus()` pattern
  (a small setter, not raw widget pointers poked from `zombie_updaters.cpp`).
- **Charging screen's 4 remaining bars switched to `LV_ANIM_ON`** (Charge
  Setpoint, Charger Temp, AC Supply Volts, EVSE Limit) - `lv_bar`'s own
  native tween, essentially free since it's a one-word flag change, not a
  custom animation. Every other screen's bars are untouched and still
  snap (`LV_ANIM_OFF`) - deliberate, this was a one-screen prototype, not
  a project-wide sweep.
- **Charge Status pill gets a smooth color fade** instead of an instant
  snap, via an `lv_style_transition_dsc_t` (400ms, ease-out) applied once
  at panel-creation time to the `LV_STYLE_BG_COLOR` property - every
  future `lv_obj_set_style_bg_color()` call on that pill (from
  `ui_chargingScreen_setStatus()`) now fades instead of jumping, with no
  changes needed at the call site.
- Builds clean (RAM/Flash both barely moved - this is a rendering-cost
  question, not a size one; the board's documented ~26fps LVGL-benchmark
  ceiling is the thing to watch if animation gets rolled out more broadly,
  per CLAUDE.md's performance notes). Flashed and verified boots.
- **Deliberately NOT done in this pass** (per the design review's larger
  list, parked for later): brand identity/Scout mark, an icon set, a wider
  semantic color palette, gradient/zoned warning bars, or rolling this
  screen's motion treatment out to Drive/Status/Battery/GPS. This was a
  scoped prototype on one screen to validate the direction before
  investing further.

**Charging-screen design prototype: hardware-caught bugs (2026-09-14, same
day):** Rob checked the prototype on the board and found two real
rendering bugs, plus flagged a testing limitation:
- **Terminal nub was invisible.** It had been created as a *child of the
  battery body* (`lv_obj_create(ui_chargingBatteryBody)`), aligned
  `LV_ALIGN_RIGHT_MID` with a positive x-offset that put it partially
  outside the body's own bounding box. LVGL clips children to their
  parent by default, so the nub was rendering but entirely clipped away -
  not a color/z-order bug, a parent/clipping one. Fixed by making it a
  sibling instead (child of the panel, not the body), positioned in panel
  coordinates just past the body's right edge with a small overlap for a
  seamless join.
- **"Cable: 0A" collided with the EVSE Limit bar.** That cell has FOUR
  stacked elements (value/unit/bar/secondary-label), but was built by
  calling the shared 3-element `createValueAndUnit()`/`createBar()`
  helpers unchanged (offsets tuned for exactly 3 lines: -8/+28/+58) and
  then bolting the Cable label onto the bottom-right corner - which put it
  directly on top of the bar's vertical band. Fixed by hand-building that
  one cell with tighter custom offsets (-26/+2/+24/+48) sized for 4 lines,
  instead of reusing spacing that was never budgeted for a 4th one. The
  other 3 Charging-screen bar cells (Setpoint/Temp/AC Volts, still exactly
  3 lines each) are unaffected.
- **Motion is real but only provably visible on specific triggers** - Rob
  correctly noted he can't tell easing-vs-snapping apart from a single
  static glance, and the bench's static values (no contactors) mean bars
  mostly sit at whatever they already were. Clarified rather than "fixed"
  (nothing was broken here): every animated element starts at its init
  value (0 width for the battery fill, range-minimum for bars) and sweeps
  to the real bench value exactly once, the first time the screen is
  created after a boot (`uiGeneration`'s forced-push mechanism guarantees
  this) - so a fresh reboot while on/navigating to the Charging screen is
  a deterministic way to see the sweep without needing any live value to
  actually change. Ongoing motion during normal operation still requires
  a bound value to genuinely change, which won't happen on a static bench.
- Builds clean, flashed and verified boots (MAC reconfirmed per standing
  practice).

**Motion rolled out to every screen (2026-09-14, same day):** Rob said
"proceed" after the Charging prototype's bugs were fixed - took that as
approval for the validated, lowest-risk part of the design-review plan
(motion), not license to start on icons or a Scout brand mark, both of
which need actual asset decisions nobody's made yet.
- **Every remaining `lv_bar_set_value(..., LV_ANIM_OFF)` call in
  `zombie_updaters.cpp` switched to `LV_ANIM_ON`** - all 15 bars across
  Drive, Status, Battery, and GPS (Charging's 4 were already done). Same
  one-word-flag change as before, `lv_bar`'s own native tween.
- **Status-pill color fade extended to Battery and GPS** (charge/discharge/
  idle pill, GPS fix pill) - same `lv_style_transition_dsc_t` pattern as
  Charging's status pill, copied to each screen's pill-creation code.
- **Deliberately NOT applied to the Speed screen's P/N/F/R pill** - a real
  design judgment call, not an oversight: that pill reflects actual gear
  engagement, a safety-relevant, frequently-changing state. A driver
  shifting into Reverse wants instant, unambiguous feedback, not a 400ms
  fade - real PRNDL indicators snap for exactly this reason. Motion is
  right for ambient status (charging, GPS fix, battery); it's wrong for
  gear state. Uniform rollout would have been the wrong call here.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC reconfirmed per standing practice).
- Icons, a Scout brand mark/wordmark, a wider semantic color palette, and
  gradient/zoned warning bars remain from the original design-review list,
  not started - each needs an actual asset/design decision, not just a
  code change.

**Icons, applied sparingly and correctly (2026-09-14, same day):** Rob said
"looks great, continue" after the motion rollout. Icons were next on the
design-review list, but the honest constraint: LVGL's built-in symbol set
(`lv_symbol_def.h`, baked into the bundled Montserrat fonts already in use
- no new asset/font-conversion pipeline needed, confirmed by grepping the
actual header rather than assuming which symbols exist) doesn't cover most
of this dashboard's concepts - no icon for temperature, voltage, current,
power, or a coordinate. Forcing icons onto all ~25 panel titles with that
gap would have meant several wrong or generic-feeling substitutions.
Scoped instead to the handful of places a built-in symbol is a genuinely
correct match:
- **Dynamic battery-level icon** (`LV_SYMBOL_BATTERY_FULL/3/2/1/EMPTY`,
  new `batteryIconForSoc()` in `zombie_updaters.cpp`) wherever SOC is
  shown as a bare arc + percentage: folded into Status screen's "SOC"
  panel title (now e.g. "🔋 SOC"), and a new small icon label
  (`ui_batterySocIconLabel`) below the percentage on the Battery screen's
  ring. Not added to Charging - that screen already got the full drawn
  battery-shape treatment, a second battery icon there would be
  redundant.
- **Charge-bolt icon** (`LV_SYMBOL_CHARGE`) prefixed onto the Charging
  screen's status pill text, but *only* while `opmode==4` (actually
  charging) - not shown on "NOT CHARGING", since a static decorative icon
  would dilute the signal an active one provides.
- **GPS icon** (`LV_SYMBOL_GPS`) prefixed onto the GPS screen's status
  pill text, same "only while active" reasoning - shown on "GPS FIX",
  not on "NO FIX".
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC reconfirmed per standing practice).
- Still not started: a Scout brand mark/wordmark (needs an actual logo
  asset), a wider semantic color palette, gradient/zoned warning bars, and
  a genuinely bespoke icon set for the concepts LVGL's built-in symbols
  don't cover (would need the same PNG/font-conversion pipeline this
  project deliberately avoided for the old custom fonts and images so
  far).

**Semantic color palette: a distinct "energy" accent (2026-09-14, same
day):** Rob said "looks good so far. Continue" after the icon pass. Next
item on the design-review list was the palette - the critique's finding
was "one cyan accent doing every job on every screen." Scoped narrower
than "recolor everything," same discipline as the icon pass:
- **New `ui_theme_accent_energy()`** (`ui_theme.h`/`.cpp`) - a distinct
  gold (`0xFFD60A`) in Day mode, applied ONLY to the two screens whose
  entire identity is energy/battery: Battery's SOC ring + all 4 cell
  bars, Charging's battery-shape fill + all 4 bars. Drive/Status/GPS keep
  the existing cyan `ui_theme_accent()` unchanged - deliberately not a
  project-wide recolor, just enough to make Battery/Charging read as
  visually distinct from the rest of the app at a glance (the critique's
  actual ask), without touching hue on screens whose "motion/general
  telemetry" identity is arguably already served fine by cyan.
- **Real design constraint respected, not glossed over**: Night mode's
  `accentEnergy` is set IDENTICAL to its regular `accent` (both
  `0xCC5200`), not a second distinct hue - CLAUDE.md's Night palette is
  deliberately amber-only for dark-adaptation (a real automotive night-
  vision principle already established in this project, not new). Adding
  a second hue at night would have undermined that discipline on exactly
  the two screens using it. The semantic color distinction this feature
  exists for is Day-mode-only, and that's called out explicitly in both
  the header comment and the palette struct's own comment, not left as a
  silent behavior difference someone finds by accident later.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC reconfirmed per standing practice).
- Remaining from the original design-review list: a Scout brand mark/
  wordmark (needs a real asset decision - see the note above about not
  fabricating a corporate trademark), and gradient/zoned warning bars
  (LVGL's `lv_bar` has no native multi-color-zone support - would need a
  custom draw callback, meaningfully more implementation risk than
  anything shipped in this design-review arc so far, deferred rather than
  rushed).

**Gradient/zoned warning bars (2026-09-14, same day):** Rob asked for this
specifically while he looks for a Scout brand asset. Worked out the real
LVGL 8.4 constraint before building anything, rather than assuming the
naive approach would work:
- **A gradient applied directly to a bar's growing INDICATOR doesn't do
  what it looks like it should** - LVGL always stretches a 2-stop gradient
  across whatever the indicator's CURRENT rendered width is, not across
  the bar's fixed total range. A 10%-full bar would show the exact same
  full green-to-red sweep as a 90%-full bar, just compressed into a
  narrower strip - not "mostly green because the value is safe." Confirmed
  this by reading LVGL's own gradient-stop semantics before writing code
  that would have looked plausible but rendered misleadingly.
- **The fix**: put the gradient on the bar's TRACK (`LV_PART_MAIN`, fixed
  size) instead of the indicator. The indicator still draws a solid,
  opaque accent color on top of the track from 0 up to the current value
  (unchanged) - so the only part of the gradient that stays visible is the
  UNFILLED remainder. Net effect: a lot of muted green "headroom" visible
  when the value is safely far from the limit, shrinking to a thin red
  sliver as the value approaches it. New `setZoneGradient()` in
  `ui_statusScreen.c` (`lv_obj_set_style_bg_grad_color`/`_grad_dir`, no
  custom draw callback needed - simpler than originally expected).
- **Colors are `ui_theme_good()`/`ui_theme_warning()` blended 35% into
  `ui_theme_bg()`** (`lv_color_mix`, ratio 90/255 - confirmed `lv_color_mix`'s
  own doc comment for which argument order means "more of which color"
  before using it) rather than full saturation - a full-strength red/green
  gradient behind a thin 14px bar risked reading as a neon strip rather
  than a subtle hint, and that balance can't be confirmed without seeing
  it on the actual screen. Flagged as worth a second look once Rob can
  eyeball it.
- **Scoped to exactly 2 bars, not applied broadly**: Status screen's
  Inverter Temp (danger=high, green-left/red-right) and Pack Voltage
  (danger=low, red-left/green-right - the gradient direction actually
  flips based on which end of the range the real `warningSet` threshold
  guards, not applied mechanically the same way everywhere). Every other
  bar in the app was deliberately left with its plain flat track:
  most have no `warningSet` threshold defined at all (AC Volts, Regen
  Limit, cell voltages, EVSE limit, etc.) - decorating them with a
  gradient anyway would imply a danger zone that doesn't actually exist.
  Motor Temp DOES have a real threshold but is `LV_BAR_MODE_SYMMETRICAL`
  (fills outward from a non-zero center, not from one fixed edge) - the
  "revealed remainder" reasoning gets meaningfully harder to verify is
  correct for a bidirectional fill, and this can't be checked visually
  without live hardware, so it was deferred rather than shipped unverified.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC reconfirmed per standing practice).
- This closes out every item from the original design-review critique
  except a Scout brand mark, which is blocked on Rob finding/choosing a
  real asset (deliberately not fabricating corporate trademark artwork -
  see the earlier note on this).

**Splash/Clock screen with the real Scout brand mark (2026-09-14, same
day):** Rob found and provided the actual Scout wordmark
(`firmware/assets/SCOUT.png`, 380x142, white script logo on black) while
the zoned-bar work was underway. This is Rob's own personal vehicle
project, not an official Scout Motors product being published or
distributed - using the real logo on his own dashboard is treated the way
putting a manufacturer badge on your own car customization would be, not
as fabricating corporate branding (which would be a different, refused
category - see the earlier note on this from when the brand-mark idea was
first raised).
- **New conversion pipeline** (`firmware/assets/convert_logo.py`, run
  manually - not part of the PlatformIO build, this project has no other
  asset-pipeline build step either): converts the PNG into an LVGL 8.x
  `LV_IMG_CF_TRUE_COLOR_ALPHA` C image (`firmware/src/img_scout_logo.c/.h`).
  `LV_USE_PNG` is `0` in this project (same reasoning as the deleted custom
  fonts) so this had to become a compile-time C array either way - written
  by hand rather than using LVGL's stock image-converter tool, specifically
  so the alpha-keying step below was possible.
- **Verified the exact pixel format against this project's own LVGL source
  before writing a single byte**, not assumed: `LV_COLOR_DEPTH=16`,
  `LV_COLOR_16_SWAP=0` (both already asserted at compile time in `ui.c`) ->
  `LV_IMG_PX_SIZE_ALPHA_BYTE` is 3 (confirmed by grepping LVGL's own
  `lv_img_buf.h`) - 2 bytes little-endian RGB565 + 1 alpha byte per pixel.
  Getting this wrong would have silently produced garbage colors on
  hardware with no compile error to catch it.
- **Alpha-keyed the flat black background to transparent** using a
  luminance ramp (LOW=15/HIGH=235), calibrated against this specific
  file's actual histogram (checked first - a clean bimodal split at ~0 and
  ~250 with a small anti-aliasing smear between, not guessed at) so the
  cursive letters' edges blend into whichever theme background is behind
  them instead of carrying a visible black rectangle.
- **Kept at native 380x142 resolution, no upscaling** - it's a raster
  logo, not vector art; scaling a script/cursive font up would blur it,
  and the source is comfortably legible at native size on an 800x480
  display without needing to be stretched.
- **New `ui_splashScreen.{h,c}`** - the Splash/Clock screen from the
  original 7-screen plan, finally built: the logo, a 48px digital UTC
  clock sourced from `gpsData` (gps_driver.h), and a small caption -
  deliberately nothing else, per CLAUDE.md's "deliberately simple... no
  data density" rule for this screen. Clock shows "--:--" / "UTC - waiting
  for GPS fix" until a real fix has been seen, same pattern as every other
  GPS-dependent value in this project. No timezone conversion (would need
  a location + TZ database this project has no source for) - UTC only.
  Clock binding reuses the *existing* GPS dataMutex snapshot already taken
  in `slowUpdate()` rather than a second critical section.
- **Inserted into the nav chain at the Settings bookend, not disturbing
  the existing chain**: Settings previously only handled ONE swipe
  direction (forward to Speed) - its other direction was genuinely unused
  until there was a screen to put there. Wired it to Splash/Clock, so the
  topology is now `Splash/Clock <-> Settings <-> Speed(home) <-> Drive <->
  Status <-> Battery <-> Charging <-> GPS <-> Dyno LIVE -> Dyno RESULTS`.
  Speed remains the eager-boot HOME screen, unchanged - Splash/Clock is a
  regular lazily-created screen reachable by swiping, not a boot splash
  with an auto-advance timer (a real product could want that; not built
  now, flagged as a possible future refinement).
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flash
  usage moved from 25.5% to 30.7% (~162KB for the image, matches
  380*142*3 bytes exactly) - still comfortable headroom. Flashed and
  verified boots (MAC reconfirmed per standing practice).
- This closes out every item from the original design-review critique.

**Correction: Splash actually shows at boot (2026-09-14, same day):** Rob's
first hardware check after the Splash/Clock milestone above: "no logo
evident, just boots to speedo." That was working as literally built - the
previous entry explicitly kept Speed as the eager boot screen and made
Splash/Clock a regular mid-chain screen reached by swiping - but a screen
named "splash" implying it should be the first thing shown is a completely
reasonable expectation this design missed. Asked whether to fix it (show
at boot, auto-advance vs. wait for a swipe) rather than just flip it
silently, since it's a real behavior change to the boot sequence, not a
bug in the traditional sense. Rob: auto-advance.
- **`ui.c`'s `ui_init()`** now creates+loads `ui_splashScreen` instead of
  `ui_speedScreen` as the eager boot screen.
- **New one-shot `lv_timer`** in `ui_splashScreen_screen_init()`
  (`SPLASH_AUTO_ADVANCE_MS` = 2500, `lv_timer_set_repeat_count(t, 1)`)
  transitions to Speed automatically via the same `_ui_screen_change()`
  every swipe already uses - meaning Speed's widgets still get correctly
  seeded via `uiGeneration`'s forced-push mechanism the moment it's
  created, same as any other lazily-created screen.
- **Two real correctness details, not just "add a timer"**:
  - Because screens in this codebase are never actually destroyed once
    created (the long-standing `_ui_screen_delete()` inverted-condition
    bug, deliberately left alone), `ui_splashScreen_screen_init()` - and
    therefore this timer - only ever runs ONCE for the process's entire
    lifetime. If Rob later swipes back to Splash/Clock manually (from
    Settings) to check the time, `_ui_screen_change()` sees the screen
    already exists and skips re-init, so the auto-advance does NOT
    re-fire and yank him back out every time he checks the clock. This
    falls out of the existing lazy-init pattern for free - no extra
    "only once" flag needed.
  - The timer callback checks `lv_scr_act() == ui_splashScreen` before
    acting. Without that guard, a user who manually swipes away from
    Splash within the first 2.5 seconds would still get force-navigated
    to Speed a couple of seconds later regardless of where they'd
    actually gone - the guard makes the auto-advance a no-op once the
    user has taken over navigation themselves.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flashed
  and verified boots (MAC reconfirmed per standing practice) - awaiting
  Rob's confirmation that the logo now actually appears at power-on.

**Real bold fonts, project-wide (2026-09-14, same day):** Rob: "much
bolder (chunkier) font throughout... better for readability," after
comparing against other products. LVGL's bundled Montserrat fonts
(`lv_font_montserrat_16/24/32/48`, used everywhere since the original
typography migration) are REGULAR weight only - there's no LVGL style
property that makes an existing bitmap font bolder at runtime, so getting
real bold weight meant baking new bitmap fonts from an actual bold TTF.
- **No Node.js in this environment**, so LVGL's own `lv_font_conv` tool
  (the standard way to do this) wasn't directly usable. Downloaded
  Montserrat's actual variable-weight TTF from Google's own font repo
  (`github.com/google/fonts`, SIL Open Font License) and wrote a from-
  scratch Python converter (`firmware/assets/convert_font.py`, using
  Pillow - already a dependency from the logo conversion) that bakes
  LVGL 8.x's native "fmt_txt" bitmap font format directly.
- **Verified the binary format against this project's own vendored LVGL
  source before generating a single glyph, not assumed** - the same
  discipline as the image conversion and the sunrise-equation work
  earlier: confirmed bit-packing is contiguous MSB-first with no per-row
  padding (`lv_draw_sw_letter.c`'s `bit_ofs` formula), and reverse-
  engineered the `ofs_y` vertical-positioning convention from the actual
  draw-position formula in that same file - not from `lv_font.h`'s doc
  comment, which turned out to directly contradict the real generated
  font files' own comment (one says "measured from the top," the other
  "measured from the bottom" - the code, not either comment, is what
  actually matters). Confirmed empirically too: a flat-bottomed capital
  ('A') computes `ofs_y=0` at every tested size, exactly as it should
  sitting flush on the baseline.
- **Built a second verification harness** (`firmware/assets/
  preview_font.py`) that re-implements LVGL's own glyph-positioning math
  in Python against the exact in-memory data the converter builds, and
  renders a test string to PNG - checked visually (both 16px and 48px)
  before wiring the fonts into ~30 call sites across 10 screen files. A
  wrong offset here would have misaligned every character on every
  screen; worth the extra script rather than discovering that after
  flashing.
- **Weight: ExtraBold (800)**, not Bold (700) or Black (900) - a
  judgment call (Bold didn't feel like enough of a change for "much
  bolder, chunkier"; Black risked the letterforms blobbing together at
  16px) that can't be fully confirmed without seeing it on the real
  screen. Easy to regenerate at a different weight if Rob wants it
  adjusted either direction - just edit `WEIGHT_NAME` and re-run.
- **Only ASCII 0x20-0x7E + the degree sign (0xB0)** are baked into each
  bold font - not the `LV_SYMBOL_*` icons from the earlier icons pass
  (would have needed the FontAwesome asset LVGL's bundled fonts use,
  which this project doesn't have). Each new font's `.fallback` pointer
  is set to the original regular-weight `lv_font_montserrat_N` of the
  same size, so LVGL automatically renders any icon codepoint through the
  regular font - no extra icon-font asset needed, and no visual gap.
- **`ui.h`** now includes all 4 new font headers (available to every
  screen via the existing include chain); every `&lv_font_montserrat_N`
  reference across `ui_speedScreen.c`, `ui_driveScreen.c`,
  `ui_statusScreen.c`, `ui_batteryScreen.c`, `ui_chargingScreen.c`,
  `ui_gpsScreen.c`, `ui_splashScreen.c`, `ui_dynoLiveScreen.cpp`,
  `ui_dynoResultsScreen.cpp`, and `ui_settingsScreen.cpp` (58 call sites)
  now points at `&font_montserrat_extrabold_N` instead - a scripted sed
  across exactly those 10 files, not the 4 generated font files
  themselves (which correctly keep their `lv_font_montserrat_N` fallback
  references untouched).
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean. Flash
  usage moved from 30.7% to 33.6% (~91KB for 4 sizes x 97 glyphs each,
  matches the expected bitmap+descriptor size) - still comfortable
  headroom. Flashed and verified boots (MAC reconfirmed per standing
  practice) - awaiting Rob's read on the actual weight/chunkiness once
  he's seen it live, since that balance couldn't be verified without
  the real screen.

**Bold-font fallout: Battery screen label clash, measured not guessed
(2026-09-14, same day):** Rob's hardware check after the ExtraBold pass
found one real clash - Battery screen's Max/Min Cell Voltage rows
overlapping their bars - and asked what else might need attention.
- **Measured the actual overlap with the real font data** rather than
  eyeballing it: `firmware/assets/convert_font.py`'s glyph metrics show
  "MAX CELL VOLTAGE" renders at 175px and "MIN CELL VOLTAGE" at 168px in
  the new bold 16px font, against a ~160px budget (title starts at x=40,
  the bar starts at x=200 in `ui_batteryScreen.c`'s `createBarRow()`
  layout) - confirmed both titles genuinely overflow into the bar, by
  15px and 8px respectively.
- **Fix**: shortened both to "MAX CELL V"/"MIN CELL V" (105px/98px,
  comfortable margin) - not a new abbreviation, just applied the same
  shortening the screen's own 4th row ("CELL DELTA V") already used.
- **Proactively re-measured every other label across all 11 screens**
  against the real bold-font metrics before calling it done - panel
  titles (388px panels, ~364px budget), Drive screen's longest enum text
  ("HI-FOR/LO-REV", 195px @24px), and all three status-pill texts
  (Charging/Battery/GPS, including a conservative allowance for the
  fallback-font icon glyphs) all measured with comfortable margin. This
  specific `createBarRow()` layout (title and bar sharing one row) is the
  only place in the app with this failure mode - every other screen's
  panel titles sit above their bar, not beside it, so they were never at
  risk the same way.
- Builds clean on both environments, flashed and verified boots (MAC
  reconfirmed per standing practice).
- Rob asked what else was on the label list from the original design
  critique. Answered but not yet started, pending his prioritization:
  (1) a real icon set is now technically unblocked, since both an image
  baker (the logo) and a font baker (this pass) exist in this project
  now - no more "no asset pipeline" excuse; (2) several labels are more
  verbose than they need to be independent of any actual clash ("AC
  SUPPLY VOLTS", "CHARGE SETPOINT", "GEAR SELECTION", "INVERTER TEMP");
  (3) proposed turning today's ad-hoc width measurement into a standing
  script that checks every literal string against its container
  automatically, instead of re-deriving it by hand each time a font or
  layout changes.

**"Proceed on all fronts": icon set, label shortening, standing width
check (2026-09-14, same day):** Rob approved all three follow-ups from the
bold-font fallout entry above in one go.
- **Icon set**: baked a small custom LVGL icon font
  (`firmware/assets/convert_icons.py`) from Google's Material Icons
  (Apache 2.0, `firmware/assets/fonts/MaterialIcons-Regular.ttf` +
  `.codepoints`, same trusted-source pattern as Montserrat) - 5 icons only
  (`bolt`, `thermostat`, `location_on`, `navigation`, `speed`), not a
  full icon-font import, since only a handful of panel concepts warrant
  one. Uses a **sparse cmap** (`LV_FONT_FMT_TXT_CMAP_SPARSE_TINY`) instead
  of `convert_font.py`'s contiguous-range cmap, since these 5 codepoints
  are scattered across Material Icons' PUA space (0xE0C8-0xF076) - a
  contiguous range would waste a `glyph_dsc` slot per unused codepoint in
  between.
- **No new widget, no layout changes**: rather than a separate icon label
  next to each title (would've meant threading an extra parameter through
  every `createPanel()` call site across 5 files), chained
  `font_montserrat_extrabold_16`'s `.fallback` through the new
  `font_icons_20` before reaching `lv_font_montserrat_16` - confirmed safe
  by reading `lv_font.c`'s `get_glyph_dsc` fallback walk (`while(f)`, not
  a single hop) before relying on it. One `lv_label_set_text` call can now
  mix bold ASCII text + a custom icon + an `LV_SYMBOL_*` glyph.
  `convert_font.py` gained an `ICON_FONT_SIZES` set (`{16}` - the only
  size used for panel titles) controlling which generated sizes chain
  through the icon font.
- **Verified visually before touching any screen file**: built
  `firmware/assets/preview_icons.py` (same discipline as `preview_font.py`
  for the text fonts) and read the rendered PNG directly - all 5 glyphs
  render clean and legible at 20px.
- **Wired into 7 panel titles** across 4 screens: Drive's "POWER" (bolt),
  Status's "MOTOR TEMP"/"INV TEMP"/"MAX BATT TEMP" (thermostat),
  Charging's "CHARGER TEMP" (thermostat) and "AC VOLTS" (bolt), GPS's
  "SPEED (GPS)" (speed), "LATITUDE"/"LONGITUDE" (location_on), and
  "HEADING" (navigation). Deliberately one shared bolt icon for every
  electrical quantity (power/voltage/current) rather than a distinct icon
  per unit - visually near-identical bolt variants would be hard to tell
  apart at 20px, and the text label already says which quantity it is.
- **Label shortening** (the other half of the original audit): applied
  the 4 remaining verbose labels flagged in the bold-font fallout entry -
  `ui_driveScreen.c` "GEAR SELECTION" -> "GEAR", `ui_statusScreen.c`
  "INVERTER TEMP" -> "INV TEMP", `ui_chargingScreen.c` "CHARGE SETPOINT"
  -> "SETPOINT" and "AC SUPPLY VOLTS" -> "AC VOLTS".
- **Standing width-check script**: `firmware/assets/check_label_widths.py`
  - a manifest of every screen's real label strings (including worst-case
  dynamic values, not just what's hardcoded at creation) against their
  actual container budget, using the real generated font's glyph metrics
  (imports `convert_font.py`'s `build_size()` directly, not a
  reimplementation). Run standalone (`python check_label_widths.py`),
  exits 1 on any overflow. Confirmed all `[OK]` against the post-shortening
  label set. Replaces re-deriving overflow risk by hand each time a font
  or layout changes - update the manifest when a screen or layout changes.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` build clean.
  Flashed to hardware (MAC `28:84:85:82:78:14` reconfirmed per standing
  practice) in one combined flash covering the icon set + all label
  shortenings together. Visual confirmation on the real screen (icon
  legibility at 20px inline with bold 16px text, and that none of the
  4 shortened labels reads as awkward) still pending Rob's look at the
  live board - CLAUDE.md's verification habit applies here same as any
  other screen change.

**GPS NAV screen + mock GPS source + NMEA parsing test (2026-09-14, same
day):** Rob asked to continue with the navigation screen. Full offline
tile-based mapping is still genuinely blocked (real map tile data for
wherever this vehicle drives, plus SD card wiring - unchanged from the
earlier GPS telemetry screen entry). Rob's follow-up asked to proceed on
the GPS-parsing/mock-source infrastructure regardless of the physical GPS
module not being on the bench yet, structured as: a single fix-data
interface, a standalone NMEA parser test, a mock GPS source, the nav
screen built against the mock, and the real UART driver wired but not
blocking. Reconciled that ask against what already existed in this repo
rather than rebuilding it blind:
- **`gps_driver.h`'s `GpsData` already IS the single interface struct**
  (lat/lon/speed/heading/fix/timestamp) - kept it rather than introducing
  a parallel `GpsFix` type for a purely cosmetic rename across every
  consumer (ui_gpsScreen.c, ui_navScreen.c, zombie_updaters.cpp,
  resolveDisplayPref's sunrise calc).
- **NMEA parsing already uses TinyGPSPlus** (pinned dependency, not hand-
  rolled) with the real UART byte-reading loop already fully written, not
  stubbed - it had just never run against a physical module. Writing a
  second parser would have been duplicate code with its own bug surface
  for no benefit, so this reused TinyGPSPlus rather than replacing it.
- **New `test/test_gps_parsing/test_gps_parsing.cpp`** - the "unit test
  against canned sentences, no serial port" ask, adapted: TinyGPSPlus
  itself `#include`s Arduino.h (confirmed by reading it, not assumed), so
  a true host/native binary isn't possible here - runs instead via
  PlatformIO's embedded Unity test runner (`pio test -e waveshare-s3-lcd7
  -f test_gps_parsing`), still no physical GPS module or live NMEA stream
  involved, which is the part that actually mattered. Checksums for the
  three canned NMEA reference sentences (GGA/RMC/VTG) were computed by
  script, not copied from memory, before trusting TinyGPSPlus would even
  accept them.
- **The test caught two real, wrong assumptions on its very first run**
  (exactly what it's for): (1) NMEA's 2-digit year has no century
  heuristic - TinyGPSPlus windows it as 2000+yy unconditionally, so the
  classic 1994-dated reference RMC sentence parses as year 2094, not 1994;
  (2) **TinyGPSPlus does not parse VTG at all** - confirmed by reading
  `TinyGPS++.cpp`'s sentence-type switch, which only recognizes "RMC" and
  "GGA". A VTG sentence is silently a no-op, not a crash - meaning
  `gps_driver.cpp`'s speed/course have always come entirely from RMC, and
  a real module emitting VTG frames too would have those quietly ignored,
  which is fine since nothing here reads a VTG-only field. Both fixed by
  correcting the test's expectations to match reality rather than
  asserting something false. All 4 test cases pass now.
- **New `gps_mock_source.cpp`** - same `gps_init()`/`gps_poll()` signature
  and same `gpsData` global as the real driver, replaying a canned
  rectangular driving-route loop (~1Hz cadence, matching real fix timing)
  instead of reading Serial. Single build-time switch point, not a
  runtime branch: new `[env:mock-gps]` in `platformio.ini` (extends the
  shipping env, `build_src_filter` swaps which of the two same-symbol
  `.cpp` files gets compiled) - exactly the pattern `[env:cantrace]`
  already used for tracing. Screen code never knows or checks which
  source is active.
- **New `ui_navScreen.{h,c}`** - the buildable-now GPS NAV screen: a
  full-width (800x410) breadcrumb trail canvas (`lv_line`, not a
  raster map) plus a thin telemetry strip at the bottom (fix status,
  speed, heading) - the "thin telemetry strip overlay" from the original
  architecture note, kept even without real map tiles under it. Current
  position is always recentered to canvas middle via a local flat-earth
  projection recomputed against the LATEST fix every update (not the
  trail's start point) - standard equirectangular approximation, same
  "simple, sanity-checked, not aerospace-grade" bar as the existing
  sunrise/solar-declination calc. Fixed `METERS_PER_PIXEL` scale, not yet
  tuned against a real driving route. Trail history (up to 120 points,
  ~2 minutes at 1Hz) lives in a circular buffer local to the screen,
  fully recomputed to screen coordinates on every new fix rather than
  incrementally patched - cheap at this size, simpler to get right.
- **Inserted into the topology between GPS and Dyno LIVE**: `... <->
  Charging <-> GPS <-> GPS NAV <-> Dyno LIVE -> Dyno RESULTS`. GPS
  screen's forward swipe and Dyno LIVE's backward swipe both updated to
  target the new screen instead of each other directly.
- Reuses the existing `gpsLatLonChanged` dirty-check in `slowUpdate()`
  (zombie_updaters.cpp) to both update the GPS telemetry screen AND append
  a new trail point - no separate dirty-check needed, a new trail point
  only ever makes sense when the position actually changed.
- All three environments (`waveshare-s3-lcd7`, `cantrace`, `mock-gps`)
  build clean. Flashed `mock-gps` to hardware (MAC reconfirmed per
  standing practice, both before the NMEA test flash and before this one)
  specifically so the nav screen could be visually verified without a
  physical GPS module on the bench - this is a dev-only build, not the
  shipping firmware; flash back to `[env:waveshare-s3-lcd7]` before real
  use. **Visual confirmation on the live screen (trail rendering, "you are
  here" centering, telemetry strip) still pending Rob's look** - I have no
  way to see the physical display myself, same standing limitation as
  every other screen change.

**microSD card bring-up (2026-09-14, same day):** Rob asked to start SD
card driver bring-up in parallel with sourcing real map tile data (needed
either way for the eventual offline tile map screen). Pin assignments
were NOT guessed - confirmed against Waveshare's own official example
(`github.com/waveshareteam/ESP32-S3-Touch-LCD-7`, `examples/Arduino/
examples/03_SD_Test`), found after their own docs.waveshare.com page and
wiki only stated "SD_CS must be driven by EXIO4 of the CH422G" without
listing the SPI data pins at all.
- **SD_MOSI=GPIO11, SD_CLK=GPIO12, SD_MISO=GPIO13** - a dedicated SPI3
  bus, confirmed free by grepping this project's own pin usage (not used
  by the RGB/DPI display bus, CAN, or the I2C touch/CH422G/IMU bus).
  **SD_CS is EXIO4 on the same CH422G I/O expander** this project already
  drives directly via `lgfx::i2c` (not a real GPIO) - for the identical
  reason the CH422G wrapper library is avoided everywhere else in this
  project (i2c_master/legacy driver conflict, see `display_driver.h`).
- **New `display_driver.cpp::ch422g_assert_sd_cs()`** - writes the full
  known CH422G output byte (`0xEF`, matching the `0xFF`
  `lcd_panel_start()` already sets with just bit4/SD_CS cleared) rather
  than a read-modify-write, since the CH422G's IO register is write-only
  (no readback) - preserves TP_RST/LCD_BL/LCD_RST/USB_SEL exactly as
  `lcd_panel_start()` left them. Documented the FULL CH422G bit map for
  the first time in this repo while at it (b1-b5, including confirming
  USB_SEL/b5 being HIGH is what's been correctly routing GPIO19/20 to
  CAN_TX/CAN_RX all along - not something that needed changing, just
  finally understood and written down).
- **SD_CS is asserted once and left low permanently**, never toggled per
  SPI transaction - confirmed intentional by finding Waveshare's own demo
  does the identical thing (toggling a CS through an I2C-bus IO expander
  per SPI transaction would be far too slow for real SPI timing anyway).
  Consequence, documented in `sd_driver.h`: nothing else may ever share
  this SPI bus, since there's no way to deselect the card once it's
  asserted - fine today (nothing else uses GPIO11/12/13), but binding for
  any future SPI peripheral.
- **New `sd_driver.{h,cpp}`** - `sd_init()`/`sd_available()`, mount +
  bring-up only, no tile-format reading code yet (that's blocked on real
  tile data existing to test against, same as the map screen itself).
  Wired into `firmware.ino`'s `setup()` right after `imu_init()`/
  `imu_start_sampling()` - order between IMU and SD doesn't matter, they're
  independent buses, both just need `lcd_panel_start()` done first.
- **Hardware-verified boot has no regression**: flashed `cantrace` and
  watched the real boot log (had to reset the board via `esptool ... run`
  and capture a fresh miniterm session, since the first flash's boot
  banner had already scrolled past by the time a terminal was attached -
  a genuinely different failure mode from "didn't check" worth remembering
  next time). CAN/SDO polling and the 1Hz `myData` dump proceed exactly on
  schedule after the new CH422G write and SD mount attempt - if the new
  I2C write had corrupted anything on that bus, `lcd_panel_start()` or the
  touch controller would have hung and NONE of that later boot activity
  would ever have printed. Visual confirmation of the display/touch
  themselves still needs Rob's eyes, same standing limitation as always.
- **SD mount result on the bench: `Card Failed! cmd: 0x00` ->
  `f_mount failed: (3)` -> `sd_init()` correctly returns false, logged as
  "SD card not mounted."** This is the textbook signature of no card
  physically in the slot (cmd0 gets no response), not a wiring or driver
  bug - the SPI transaction did go out correctly. **Not yet confirmed with
  an actual card inserted** - that's the real end-to-end test, pending Rob
  having a microSD card on hand.
- **Known cosmetic wart, not yet fixed**: the ESP32 Arduino SD library
  still calls `pinMode`/`digitalWrite` on pin `-1` (logged as pin 255,
  its `uint8_t` wrap) internally despite `-1` being the documented
  "externally managed, don't touch a CS pin" signal - logs a handful of
  harmless `E` warnings at boot. Waveshare's own demo has the identical
  pattern, so this is a quirk of this specific arduino-esp32 core version
  reacting to a vendor pattern that predates it, not something introduced
  here - left alone since it's non-fatal and silencing it isn't worth the
  risk of touching working CS logic without a real card to test against.
- Both `[env:waveshare-s3-lcd7]` and `[env:cantrace]` (plus `[env:mock-gps]`,
  unaffected but rebuilt to confirm) build clean.

**NAV vector tile format adopted, Maperitive/PNG dropped entirely
(2026-09-14, same day):** Rob directed a full pivot on the map-tile data
format while an SD card was in transit: drop Maperitive/raster-PNG tiles
as the plan entirely, target jgauchia/Tile-Generator's NAV vector format
instead, pinned to a specific commit (not main/HEAD, since the format is
pre-1.0 and stated by its own author to be actively evolving).
- **PINNED to tag `v.0.9.0`, commit `7f8e9819133369cd270c51f427a562cf2a8279fc`**
  (confirmed `main` is currently byte-identical to this tag for every file
  that matters here, as of 2026-09-14 - re-verify before assuming that
  stays true).
- **The real format is NOT what was initially described** - verifying it
  required reading the encoder twice, not just the docs once, and this is
  worth remembering as its own lesson: `docs/bin_tile_format.md` and
  `src/tile_processor.hpp`'s actual byte-writing code appeared to
  contradict each other on first read (doc: 8-byte feature header with a
  1-byte palette color index; code: a 9-byte header with a full inline
  2-byte RGB565 color). Resolved by finding a SECOND encoder pass
  (`process_all()`'s palette-collapse step) that rewrites every already-
  serialized tile's inline 2-byte colors down to 1-byte palette indices
  before anything reaches disk - the doc was describing the real, final,
  on-disk form correctly; the transient 9-byte form only exists in
  memory mid-generation. Trusting either source alone (the doc, or the
  first encoder function found) would have produced a parser that reads
  real generated files wrong.
- **Structurally different from what was asked for**: no z/x/y-per-tile
  file tree exists in this format at all (that was PNG-raster-tile-server
  convention) - it's ONE file per zoom level (`Z{zoom}.nav`), with a flat
  `tiles_wide x tiles_high` array index inside that file giving true O(1)
  lookup by direct array arithmetic (matches the "no R-tree" requirement's
  intent, just via a different mechanism than a z/x/y path). No existing
  z/x/y lookup code needed preserving since PNG tiles were never actually
  wired into this repo.
- **New `nav_tile_format.h`** - pure format constants/structs (`NavMapHeader`,
  `NavIndexEntry`, `NavTileHeader`, `NavFeatureHeader`), no SD dependency,
  carries the pinned-commit reference and the doc/code reconciliation
  story so a future re-read of upstream doesn't have to rediscover it.
- **New `nav_tile_reader.{h,cpp}`** - the clean parsing interface Rob
  asked for: `nav_tile_load(zoom, tileX, tileY, NavTileData*)` decodes
  varint/zigzag/palette-indexed bytes into plain `NavTileData`/
  `NavFeature` structs; nothing downstream (the eventual renderer) will
  ever see NPK2/NAV1 bytes directly, so a future upstream format change
  only touches these two files. Fixed-capacity (`NAV_MAX_FEATURES_PER_TILE`=64,
  `NAV_MAX_VERTICES_PER_FEATURE`=32, `NAV_MAX_RINGS_PER_FEATURE`=4,
  `NAV_MAX_TEXT_LEN`=24) rather than heap allocation, matching this
  project's existing convention - a tile exceeding these caps is
  truncated (flagged), never a crash. Caps are a starting guess, not
  verified against real tile density yet - no real Tile-Generator output
  exists in this repo. `NavTileData` is ~12KB - documented as required to
  be a static/global buffer, never a stack local.
- **Text features use a genuinely different payload layout than
  geometry features** - confirmed by reading the encoder's text-writing
  path completely separately from its geometry-writing path, since they
  share almost no code: for text, the "coordCount" varint means a
  *4-byte-word count* for payload padding, not a vertex count. A parser
  that assumed one meaning for both would misparse every label on a real
  map. Documented prominently in `nav_tile_format.h` since it's the
  easiest part of this format to get wrong.
- **New `firmware/assets/gen_nav_fixture.py`** - since no real
  Tile-Generator output or OSM extract exists yet, hand-builds a small
  valid `.nav` file (one tile, three features: a line, a polygon with one
  ring, and a text label with no shield - chosen to exercise every
  payload branch the reader has to handle) and independently re-decodes
  it from the spec (not by calling into the C++) to confirm the encoding
  is self-consistent before trusting it as a test fixture - same
  discipline as `preview_font.py`/`preview_icons.py`. Confirmed: "Round-
  trip verified: line, polygon, and text all decode back exactly as
  encoded."
- **New `test/test_nav_tile/test_nav_tile.cpp`** - on-device Unity test
  against that exact fixture (mirrors `test_gps_parsing`'s pattern),
  written and ready but **cannot run yet** - needs the physical SD card
  (arriving 2026-09-15) with the generated fixture copied to its
  `/maps/Z16.nav`, then `pio test -e waveshare-s3-lcd7 -f test_nav_tile`.
- **Deliberately NOT done in this pass**: actual polygon/line/label
  rendering onto `ui_navScreen.c`'s canvas. Rob's five requirements were
  specifically about the data format and parsing/testing infrastructure;
  wiring `nav_tile_load()`'s output into LVGL drawing (tile-to-screen-
  pixel scaling from the 0-4096 local tile space, panning/zoom, label
  placement) is real, separate work, deferred until real tile data
  exists to render and check against, avoiding building and tuning a
  renderer against nothing but a 3-feature synthetic fixture.
- All three environments (`waveshare-s3-lcd7`, `cantrace`, `mock-gps`)
  build clean. **No flash needed for this piece** - `nav_tile_load()`
  isn't called from any screen or `setup()` yet, so it's inert,
  additive-only code; the board's currently-running firmware is
  unaffected.

**Next milestones:**
1. ~~Bring-up: display / touch / swipe~~ — done.
2. ~~Bench-validate CAN telemetry against real ZombieVerter traffic~~ — **done (2026-09-13)**, with an architecture pivot. Full findings in `firmware/can-bench.md`; summary:
   - Bench VCU (node 3, 500 kbit/s, confirmed from Rob) has **no canmap configured** — confirmed independently on both the Waveshare and Rob's working M5 Dial, whose bus traffic was `0x603`/`0x583` only. `kCanBroadcastParams`/`decodeBroadcastFrame()` (the passive-broadcast path) is unvalidatable on this hardware and left in place, dormant.
   - **SDO round-trips correctly end to end on real hardware** — request/response, node ID, COB-IDs, index/subindex folding all confirmed correct as originally ported.
   - **The blanket `/32` SDO scale is correct across the board** (tunables, enums, and 2000+ telemetry alike — revlim, BattCap, Gear, SOC all came back as clean round numbers). This **reverses the pre-bench concern** that `opmode`/`lasterr` needed different scaling — no fix was needed there after all.
   - `tmpm` read a real negative value on the bench (open-thermistor, no motor attached), confirming signed decode is correct for that quantity.
   - **Decision (Rob): pivot production telemetry to all-SDO polling.** The 10 previously broadcast-only params were promoted into the permanent SDO poll table (`zombie_updaters.{h,cpp}`, 14 entries, `PERIOD_SDO_POLL_MS`=50ms -> ~700ms full refresh). Confirmed on hardware: `myData` populates correctly (`soc=100`, `gear=1`, `motorTemp=-27`, matching raw SDO values).
   - Board is back on the shipping `[env:waveshare-s3-lcd7]` build (not `cantrace`) as of end of session.
   - Diagnostics kept for later: `[env:cantrace]` build (`-DCAN_TRACE`, `src/can_trace.{h,cpp}`) traces every RX frame / decoded field / SDO request+response / 1 Hz `myData` dump, zero cost when off. `firmware/test/zombieverter_replay.csv` — synthetic SavvyCAN capture for regression-testing the dormant broadcast path without a VCU.
3. ~~Build the actual screens~~ — done, hand-coded per CLAUDE.md rather than SquareLine (Speed/Drive/Status/Battery/settings/dyno LIVE+RESULTS all built 2026-09-13/14, Speed/Drive/Status/Battery split out from the original main/BMS screens 2026-09-14). Remaining from the 9-screen plan: splash/logo, clock, GPS navigation.
4. Wire GPS and the dyno-screen accelerometer (IMU must use the legacy I2C driver / `lgfx::i2c`, NOT Arduino `Wire` — see the boot-issue section). The dyno RESULTS screen currently approximates road-load power from speed-derived acceleration instead of a real IMU (see the dyno-screens entry above) - swap that in once the IMU is wired, same chart/UI code.
5. Harden OTA (dual-partition, rollback-safe) before final bezel mount, since the board will be physically inconvenient to reflash once installed

## Handoff note

Remaining work on this project is planned to continue in **Claude Code (VSCode extension)** rather than claude.ai chat, given the iterative direct-file-edit nature of the debugging involved. This file plus `scout80-dash-architecture.md` are intended to live in the repo so a Claude Code session can pick up full context directly.
