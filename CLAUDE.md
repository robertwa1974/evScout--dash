# CLAUDE.md

Project configuration and standing conventions for the Scout 80 Waveshare
dash display firmware. Read this before making UI/screen changes.

## Project overview

Fork of `Light-r4y/uaDASH` (rusEFI CAN dashboard), adapted to talk to a
ZombieVerter EV VCU (CANopen SDO + broadcast decode) instead of rusEFI,
running on a Waveshare ESP32-S3-Touch-LCD-7 (800x480), built with
PlatformIO. Full architecture and build-issue history: see
`scout80-dash-architecture.md` and `waveshare-dash-build.md` in the repo
root — read both before touching CAN/SDO code or the build config.

Twelve screens: Splash/Clock (logo + UTC digital clock), Settings, Speed
(home), Drive, Status, Battery, Charging, GPS (telemetry-only, see below),
GPS NAV (breadcrumb trail, see below), 0-60/virtual dyno (LIVE + RESULTS).
GPS navigation as originally envisioned (full offline tile map) is still
not built — GPS NAV's breadcrumb trail is the buildable-now stand-in for
it, same relationship the telemetry-only GPS screen already has to that
original plan. (Originally planned as seven with a single central-
telemetry-summary screen and a single BMS-detail screen; those two were
split into four — Speed/Drive/Status/Battery — on 2026-09-14, and Charging
+ a telemetry-only GPS screen were added the same day, inserted into the
chain between Battery and the dyno screens; Splash/Clock was built once a
real Scout wordmark asset existed to put on it; GPS NAV was added last,
between GPS and the dyno screens, once a mock GPS source made it possible
to build and verify without a physical GPS module. See "Screen layout
conventions" below and `waveshare-dash-build.md`'s milestone entries for
why.)

Current topology: `Splash/Clock <-> Settings <-> Speed(home) <-> Drive <->
Status <-> Battery <-> Charging <-> GPS <-> GPS NAV <-> Dyno LIVE -> Dyno
RESULTS`.

## GPS data source (mock vs. real)

`gps_driver.h`'s `GpsData gpsData` global is the single interface every
GPS-consuming screen (GPS, GPS NAV, Splash/Clock's UTC caption, Settings'
day/night Auto resolution) reads from — none of them know or care whether
it's populated by a real module or a canned route. Two interchangeable
implementations of the same `gps_init()`/`gps_poll()` pair, selected at
**build time**, never at runtime, and never by branching in screen code:
- `gps_driver.cpp` — the real driver (TinyGPSPlus-based NMEA parsing over
  the shared UART0/USB port, see that file's header for the DIP-switch/
  UART-sharing constraint). Used by `[env:waveshare-s3-lcd7]` and
  `[env:cantrace]`.
- `gps_mock_source.cpp` — replays a canned driving route into the same
  `gpsData` global, no hardware needed. Used by `[env:mock-gps]` only
  (`platformio.ini`'s `build_src_filter` swaps which of the two `.cpp`
  files gets compiled — never both, they define the same symbols).
`pio run -e mock-gps -t upload` to see GPS/GPS-NAV screens animate
without a physical module on the bench; flash back to
`[env:waveshare-s3-lcd7]` before real use — mock-gps is a dev aid, never
the shipping build.

## microSD card

`sd_driver.h`'s `sd_init()`/`sd_available()` mount the card at boot
(called from `firmware.ino`'s `setup()`, after `lcd_panel_start()`) — no
tile-reading or file-format code yet, that's blocked on real map tile data
existing to test against (see GPS NAV above). Pin facts here were
confirmed against Waveshare's own official example
(`github.com/waveshareteam/ESP32-S3-Touch-LCD-7`), not guessed:
- SPI bus: `SD_MOSI=GPIO11`, `SD_CLK=GPIO12`, `SD_MISO=GPIO13` — a
  dedicated bus, not shared with the display, CAN, or the I2C touch/
  CH422G/IMU bus.
- `SD_CS` is **not a real GPIO** — it's EXIO4 on the same CH422G I/O
  expander this project already drives directly via `lgfx::i2c`
  (`display_driver.cpp::ch422g_assert_sd_cs()`), for the same
  i2c_master/legacy-driver-conflict reason the CH422G wrapper library is
  avoided everywhere else in this project.
- **SD_CS is asserted once and left low permanently** — never toggled per
  SPI transaction (an I2C-bus IO expander is far too slow for that
  anyway; Waveshare's own demo does the identical thing). This means
  **no other SPI peripheral may ever share GPIO11/12/13** — there is no
  way to deselect the card once `sd_init()` runs.
- A card-not-present mount failure looks like `sdCommand(): Card Failed!
  cmd: 0x00` → `f_mount failed: (3)` in `cantrace`'s serial log — that's
  the expected, correct behavior with no card inserted, not a bug.
- Known cosmetic wart: the ESP32 Arduino SD library still calls
  `pinMode`/`digitalWrite` on pin `-1` (logged as pin 255) internally
  despite `-1` being the documented "externally managed CS" signal —
  logs a few harmless `E`-level warnings at boot. Waveshare's own demo has
  the same pattern; left alone since it's non-fatal.

## Typography

- Do NOT use uaDASH's original custom fonts (`ui_font_FontIndicator`,
  `ui_font_FontIndicatorLabel`, `ui_font_FontLabel`, `ui_font_FontRPM`,
  `ui_font_FontSpeed`) — these are the stylized/italic racing-dashboard
  fonts from the original rusEFI theme and don't match this project's
  visual direction. If you see italic text anywhere, it's one of these —
  replace it.
- Once every screen is migrated off the old fonts, delete
  `ui_font_FontIndicator.c/.h`, `ui_font_FontIndicatorLabel.c`,
  `ui_font_FontLabel.c`, `ui_font_FontRPM.c`, `ui_font_FontSpeed.c`
  entirely — `FontSpeed.c` alone is 849KB of flash for a font no longer
  in use. (Done — see `waveshare-dash-build.md`'s milestone-3 entry.)
- **Use the hand-baked ExtraBold fonts, not LVGL's bundled regular-weight
  ones** (2026-09-14 — "much bolder, chunkier... for readability"):
  `font_montserrat_extrabold_16` for labels, `_24`/`_32` for standard
  values, `_48` for hero numerics (dyno timer, primary speed readout).
  These are declared in `ui.h` (included everywhere already) — LVGL's own
  `lv_font_montserrat_N` still exist and are linked in too, but only as
  each ExtraBold font's `.fallback` (for the `LV_SYMBOL_*` icons from the
  icons pass below) — don't reference `lv_font_montserrat_N` directly in
  new screen code, it'll render in the wrong (regular) weight.
  - Regenerating at a different weight or size: edit `WEIGHT_NAME`/`SIZES`
    in `firmware/assets/convert_font.py` and re-run it (needs Pillow —
    `pip install Pillow` — and `firmware/assets/fonts/
    Montserrat-Variable.ttf`, Google's OFL-licensed variable font, already
    checked into that folder). Verify the output with `firmware/assets/
    preview_font.py` before trusting it — see that script's docstring for
    why (a wrong glyph offset silently misaligns every character on every
    screen, not just the one you're looking at).
- **Icon set** (2026-09-14): a small custom icon font, `font_icons_20`
  (`firmware/assets/convert_icons.py`, baked from Google's Material Icons,
  Apache 2.0), covering exactly 5 icons — `ICON_BOLT` (electrical
  quantities: power/voltage/current), `ICON_THERMOSTAT` (temperature),
  `ICON_LOCATION_ON` (GPS lat/long), `ICON_NAVIGATION` (heading),
  `ICON_SPEED` (GPS speed). Use inline in any 16px label, e.g.
  `createPanel(..., ICON_BOLT " POWER", ...)` — no separate icon widget or
  layout change needed, because `font_montserrat_extrabold_16`'s
  `.fallback` chains through `font_icons_20` before reaching
  `lv_font_montserrat_16` (confirmed LVGL's fallback walk in `lv_font.c`
  supports multi-hop chains before relying on this). **Only 16px chains
  through the icon font** (`convert_font.py`'s `ICON_FONT_SIZES` set) since
  that's the only size used for panel titles — don't assume `_24`/`_32`/
  `_48` can render these icon codepoints.
  - Adding a new icon: add it to the `ICONS` dict in `convert_icons.py`
    (name → Material Icons codepoint, look up in
    `firmware/assets/fonts/MaterialIcons-Regular.codepoints`), re-run it,
    verify with `firmware/assets/preview_icons.py` before wiring it into
    any screen.
  - Deliberately one shared bolt icon for every electrical quantity rather
    than a distinct icon per unit — near-identical bolt variants are hard
    to tell apart at 20px, and the panel's text label already says which
    quantity it is. Don't add unit-specific electrical icon variants
    without a real legibility reason to.

## Widget selection for telemetry display

- **Never use `lv_slider` for displaying data.** Sliders imply
  user-draggable input, which is wrong for read-only telemetry (SOC,
  speed, temps, pack voltage/current, etc.).
- **Ring/circular readouts** (SOC, dyno efficiency %) → `lv_arc`, with
  `lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE)` set so it isn't
  draggable — `lv_arc` is interactive by default, same trap as sliders.
  **Size and center the value label to sit fully inside the arc without
  overlapping its stroke** — check this against the actual rendered
  arc radius, not just the layout editor's preview.
- **Speedometer/tachometer-style gauges** with a needle or tick scale →
  `lv_meter`.
- **Thin linear strips** (temperature bars, threshold indicators) →
  `lv_bar`, not `lv_slider`.
- If asked for "a gauge" with no other detail, default to `lv_meter` for
  primary readouts and `lv_arc` for secondary/ring readouts.

## Screen layout conventions

- **Speed (home screen)**: full-screen `lv_meter` speedometer (needle +
  tick scale), digital readout centered inside the meter face, displayed
  in **mph** (converted from the underlying kph telemetry field at display
  time only — never change the field's unit, the dyno screens depend on
  it staying kph). Plus a P/N/F/R shifter-position pill below the meter.
  This is the screen `ui_init()` creates at boot and the one every other
  screen's back-navigation eventually returns to.
- **Drive**: 2x3 grid (six equal panels) of in-motion telemetry: Power,
  Pack Current (both `lv_bar`, signed/`SYMMETRICAL`),
  Gear Selection and Motor Mode (plain enum-text panels, no bar — these
  are discrete states, not continuous quantities, so the "never
  `lv_slider`" rule's rationale doesn't imply a bar either), Regen Limit
  (`lv_bar`), and one spare slot left deliberately empty for a future
  field.
- **Status**: 2x3 grid of static/at-rest telemetry: SOC (`lv_arc`, scaled
  down to fit a grid cell — smaller than Battery's ring but the same
  arc-containment rule applies), Pack Voltage, Aux/12V Voltage, Motor
  Temp, Inverter Temp, Max Battery Temp (all `lv_bar`).
- **Battery**: SOC ring (`lv_arc`, NOT the splash/logo image) at
  top-left, status pill (Charging/Discharging/Idle, derived from sign of
  `packCurrent`), labeled threshold-colored bars for genuinely cell-level
  data: Max Cell Voltage, Min Cell Voltage, Cell Delta V (computed, not a
  separate SDO param), Max Cell Temp. Pattern borrowed from
  holoduke/JKBMS's esp32-bms-lvgl. Pack-aggregate voltage/current
  (formerly shown here) moved to Status/Drive — this screen is cell-level
  detail only now. Current CAN data has no full per-cell array (only
  pack-level min/max/delta), so a true per-cell bar grid (JKBMS's actual
  headline feature) still isn't built — leave a comment marking where
  that would go if a full per-cell BMS data source is added later.
- **Charging**: 2x3 grid, inserted between Battery and Dyno LIVE. SOC
  (small `lv_arc`, same treatment as Status's SOC cell), Charge Status
  (pill — "CHARGING - AC"/"CHARGING - DCFC"/"NOT CHARGING" from
  `opmode==4` + `chgType`, plus a secondary "Plug: Connected/Not
  detected" label from `PlugDet`), Charge Setpoint (`lv_bar` — this VCU
  charges to a target pack **voltage** (`Voltspnt`), not a target SOC%;
  confirmed with Rob rather than guessed, since `CCS_SOCLim`/
  `BMS_ChargeLim`/`BMS_MaxCharge` were all plausible-looking candidates
  that would've been wrong), Charger Temperature (`lv_bar`, signed), AC
  Supply Voltage (`lv_bar`), and Cable/EVSE Current Limit (`PilotLim` as
  the primary `lv_bar`+value — what the EVSE is currently offering — with
  `CableLim` — the cable's rated capacity — as a secondary text label in
  the same panel). There's no raw "PPVal" telemetry on this VCU; PilotLim/
  CableLim are the closest real equivalents (CP- and PP-derived current
  limits respectively) — don't invent a param that isn't in
  `zombieverter_params.h`.
- **GPS (telemetry-only, current)**: 2x3 grid, same geometry as Drive/
  Status/Charging: GPS Status (pill + secondary satellite-count label),
  Speed (GPS-derived, `lv_bar`), Latitude/Longitude (plain formatted
  values, no bar — a coordinate isn't a progress-style quantity, same
  reasoning as Drive's Gear/Motor Mode text cells), Heading (`lv_bar`),
  Altitude (`lv_bar`). This is deliberately NOT the full offline-map
  screen from the original plan below — no map tile data or SD card
  wiring exists in this repo yet. Sourced from `gps_driver.h`'s
  `gpsData`, bound in `slowUpdate()` (GPS fixes update at most ~1Hz).
- **GPS NAV** (breadcrumb trail, added 2026-09-14): full-width trail
  canvas (800x410) plus a thin telemetry strip (fix status, speed,
  heading) at the bottom edge — the "thin telemetry strip overlay" from
  the original architecture note, kept even without real map tiles under
  it yet. Plots the accumulated recent GPS fix history as a line, current
  position always recentered to the canvas middle (local flat-earth
  approximation relative to the latest fix, not the trail's start point —
  see `ui_navScreen.c`'s header comment for the projection formula and
  its accuracy tradeoff). Fixed scale (`METERS_PER_PIXEL` in
  `ui_navScreen.c`), not yet tuned against a real driving route. This is
  NOT the full offline tile map from the original plan, but as of
  2026-09-15 all the pieces exist: the SD card mounts (see `sd_driver.h`),
  a NAV vector tile parser exists, it's rendered onto this screen (see
  "Map tile format" below), and real Tile-Generator output now exists for
  California (chosen as the vehicle's driving region), verified on real
  hardware — see that section for how it was generated and its one real
  operational gotcha (it lives at the same SD path the synthetic test
  fixture uses).

## Map tile format (NAV vector tiles, not PNG raster)

Maperitive/PNG raster tiles were explicitly dropped as the map-data plan
(2026-09-14) in favor of `jgauchia/Tile-Generator`'s NAV vector format,
**pinned to tag `v.0.9.0`** (commit `7f8e9819133369cd270c51f427a562cf2a8279fc`)
— the format is pre-1.0 and its own author calls it actively evolving, so
never re-derive it against a newer commit without re-reading both
`docs/bin_tile_format.md` AND `src/tile_processor.hpp`'s actual encoder
again (they disagreed with each other at this exact tag until both were
traced through — see `nav_tile_format.h`'s header comment for the full
story). Key facts, so nobody re-derives them from scratch:
- **One file per zoom level** (`Z{zoom}.nav`), not a z/x/y-per-tile file
  tree — a flat array index inside that one file gives O(1) tile lookup
  by direct arithmetic, no R-tree, no per-tile file open.
- **Text features use a different payload layout than geometry features**
  — their "coordCount" varint is a 4-byte-word padding count, not a
  vertex count. Confirmed by reading the encoder's text path completely
  separately from its geometry path, since they share almost no code.
- `nav_tile_format.h` (raw on-disk structs/constants) and
  `nav_tile_reader.{h,cpp}` (the decode-to-plain-structs interface,
  `nav_tile_load(zoom, tileX, tileY, NavTileData*)`) are the ONLY files
  that should ever need to change if the upstream format changes — no
  other code should parse NPK2/NAV1 bytes directly.
- `firmware/assets/gen_nav_fixture.py` hand-builds and self-verifies a
  tiny 3-feature synthetic test fixture (not real map data), and
  `test/test_nav_tile/` is an on-device test against it (needs the SD
  card + that fixture copied to its `/maps/Z16.nav` to actually run).
- **Real Tile-Generator output (2026-09-15)**: generated for California
  (the vehicle's driving region) from `jgauchia/Tile-Generator` pinned to
  `v.0.9.0`, zoom 16 only (matching `NAV_TILE_ZOOM` in `ui_navScreen.c` —
  no other zoom is loaded by anything yet), against the Geofabrik
  `california-latest.osm.pbf` extract (~1.3GB PBF in, ~1.3GB `Z16.nav`
  out, 2,440,227 of 7,267,043 possible zoom-16 tiles populated). Verified
  on real hardware: `nav_tile_load()` finds and decodes San Francisco's
  tile with a nonzero feature count off the actual card.
  - Tile-Generator's own build assumes a Debian/Ubuntu box (apt, WSL, or
    similar) — this dev machine has neither WSL nor Docker and lacks
    Administrator rights to install WSL2, so it was instead built native
    on Windows via MSYS2 (mingw-w64 toolchain, installed via `winget`;
    `libosmium`/`protozero` aren't packaged there so they're vendored as
    plain header clones, not pinned to any tag since they're header-only).
    This needed two small Windows-portability additions, NEITHER of which
    changes Tile-Generator's actual tile-generation logic or output
    format: (1) mingw-w64 has no `<sys/mman.h>` — the exact `mmap`/`munmap`
    subset `src/mapped_store.hpp` uses is shimmed over
    `CreateFileMappingA`/`MapViewOfFile`; (2) mingw-w64's `ftruncate()`
    wraps the 32-bit `_chsize()`, which silently fails past ~2GB — never
    hit by the small Andorra smoke-test extract, but California's much
    larger dataset needs more scratch space than that, so
    `mapped_store.hpp` redirects `ftruncate` to `_chsize_s` (real 64-bit)
    on Windows only. Neither patch lives in this repo (they're local-only
    changes to a separate clone outside it) — if Tile-Generator is ever
    rebuilt from scratch on Windows, both will be needed again.
  - **Operational gotcha, not a bug**: the real `Z16.nav` and the
    synthetic test fixture both want the SD card's `/maps/Z16.nav` path.
    Writing real map data there means `test_nav_tile` will fail against
    it (wrong tile numbers, wrong feature content) until the small
    fixture is copied back for testing — same "swap before/after" pattern
    already established for `[env:mock-gps]` vs. the shipping build.
  - Only zoom 16 was generated — no LOD/zoom-out yet, matching
    `ui_navScreen.c`'s renderer, which also only loads one zoom.
    Regenerating at additional zooms (or other states/regions) reuses the
    same local Tile-Generator build; only the `--zoom`/input-PBF choice
    changes.
- **Rendering pass (2026-09-15)**: `ui_navScreen.c` now draws whatever
  tile currently covers the vehicle's position — reuses the same
  flat-earth-meters projection the breadcrumb trail already uses (new
  `nav_latlon_to_tile()`/`nav_tile_local_to_latlon()` in
  `nav_tile_reader.{h,cpp}` convert between GPS lat/lon and this format's
  absolute tile numbers / 0-4096 local-tile-space, standard Web Mercator
  slippy-map math). SD reads (`nav_tile_load()`) only happen on a tile
  crossing, not every GPS fix; repositioning the already-built LVGL
  objects happens every fix, same split as the trail's own
  addPoint()/redrawTrail(). Deliberately simple for a first pass: only a
  polygon's outer ring is drawn (no holes), no LOD/priority filtering by
  `minZoom`. Verified on real hardware via `[env:mock-gps]` — required
  regenerating `gen_nav_fixture.py`'s tile to actually sit under the mock
  route's canned coordinates (an arbitrary tile number doesn't overlap a
  real GPS position by chance) and swapping its text-label color from
  black to white (black was legible in isolation but invisible against
  this theme's near-black panel background — see `ui_theme.cpp`'s
  `panelBg` — a real bug the fixture had been masking since nothing had
  ever rendered it on-screen before).
- **Splash/clock** (built 2026-09-14, `ui_splashScreen.{h,c}`):
  deliberately simple — the real Scout wordmark (`img_scout_logo.c/.h`,
  converted from `firmware/assets/SCOUT.png` by `firmware/assets/
  convert_logo.py` — see that script's header for the LVGL pixel-format
  and alpha-keying details) plus a 48px digital UTC clock sourced from
  `gpsData`, no data density beyond that. The splash/logo graphic belongs
  ONLY on this screen — it must not appear as a placeholder on other
  screens (e.g. Battery) in place of the widget that screen actually
  needs. Sits at the far end of the nav chain from the dyno screens,
  reachable via Settings' previously-unused swipe direction — see
  `waveshare-dash-build.md`'s milestone entry for the exact topology.
- **GPS navigation**: IceNav-v3's map-rendering surface as the base, with
  a thin telemetry strip overlay (speed, heading) at the edge.
- **Label width check (applies to every screen):** every text label must
  be verified to fit fully within the 800px display width at its actual
  rendered position — check against the real screen, not just the
  SquareLine/layout editor preview. Truncated labels ("PACK VOLTAG...")
  are a real bug, not a cosmetic nit; fix by shortening the label,
  reducing font size, or widening the container, not by ignoring it.

## 0-60 / virtual dyno screen

Two distinct modes, built as **two separate LVGL screens** navigated via
`lv_scr_load_anim()` (e.g. `LV_SCR_LOAD_ANIM_MOVE_LEFT`) — not a single
screen with hidden/shown content.

**LIVE screen**: minimal chrome. One dominant, large elapsed-time
numeral. A secondary live speed/acceleration readout. A state badge
(READY / ARMED / RUNNING / DONE). Update labels only at CAN cadence —
**never refresh a chart on this screen**, it's label updates only.

**RESULTS screen** (built once, after the run completes, not live):
- Single `lv_chart` with `LV_CHART_TYPE_SCATTER` (non-uniform X spacing),
  two series: road-load power and electrical input power, both in kW.
- **Default to a single shared Y-axis** (both curves are the same unit,
  kW) rather than dual axes — the vertical gap between curves directly
  reads as drivetrain loss. Shade the gap area if feasible. Show a
  computed efficiency-% readout separately.
- Cap points to ~50-100 per series — this hardware can't afford more on
  a chart redraw.
- Peak power for each curve as headline text callouts (numbers, not just
  curve shape) — that's what people actually look at first.
- LVGL has **no native legend widget** — build one manually: small flex
  row of colored squares + labels, aligned to the chart with
  `lv_obj_align_to()`.
- LVGL 8.4 axis ticks/labels: `lv_chart_set_axis_tick(...)`, not
  `lv_scale` (that's 9.x only). Custom tick text via
  `LV_EVENT_DRAW_PART_BEGIN` + `LV_CHART_DRAW_PART_TICK_LABEL`.

## Settings screen

- Use `lv_menu` with `lv_menu_section` groups, three sections: **(1)
  Warning Thresholds** (low SOC, high motor temp, low pack voltage),
  **(2) Display** (day/night, brightness), **(3) Calibration** (dyno
  drivetrain-efficiency workflow entry point).
- **Numeric threshold adjustment**: `lv_spinbox` driven by large +/-
  buttons (use `LV_EVENT_LONG_PRESSED_REPEAT` for accelerating hold on
  press-and-hold). Not a slider (imprecise on a bumpy road), not a
  keypad popup (too much sustained attention while driving).
- **Touch targets**: minimum ~80px (≈15mm) for standard controls, ~130px
  (≈25mm) for any safety-critical confirmation, with real spacing
  between adjacent targets — this is a vehicle context, not a phone.
- **Explicit Save button, not auto-save** — matches uaDASH's existing
  convention (press Save to persist after reboot/power cycle). Enable/
  highlight the Save button only when edits are pending (dirty-state
  indicator). Show an `lv_msgbox` "Saved" confirmation on success.
  Persist to NVS/flash only on save, not on every field edit.
- **Day/night mode**: Auto mode (time-based or light-sensor-based) with
  a manual override switch, not just a bare toggle. Apply the palette
  live/immediately when changed so the user sees a preview, don't wait
  for Save to show the visual change. Keep uaDASH's existing swipe-up/
  down brightness gesture working alongside this.

## Performance notes (ESP32-S3, this board specifically)

- Enable `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM`, pin the LVGL task to
  core 1, build with `-O2`, run at 240MHz, use double buffering.
- Board benchmarks around 26fps for the LVGL benchmark demo single-core
  at PCLK 21MHz per Waveshare's own wiki — budget accordingly, don't
  assume smartphone-class chart/animation performance.
- Never do a full `lv_chart_refresh()` on a live/high-frequency update
  path — that invalidates the whole chart. Only the RESULTS screen chart
  should be drawn, once, after a run completes.

## Version note

uaDASH declares `lvgl@8.4.0` in its build; Waveshare's own wiki for this
board references a bundled LVGL 8.3. Confirm actual version before
relying on any 8.4-specific API surface — `lv_chart_set_range` becomes
`lv_chart_set_axis_range` and `lv_scale` replaces some chart-axis tick
handling in LVGL 9.x, so don't mix API generations.

## Touch gesture direction (this board)

Confirmed on hardware 2026-09-14: **a physical rightward swipe is reported
by LVGL as `LV_DIR_LEFT`, and a physical leftward swipe as `LV_DIR_RIGHT`**
— inverted from what the names suggest. Confirmed narrowly: raw tap
*position* is correct (a button tap at its actual on-screen location
activates that exact button - verified via the settings screen's +/-
steppers), so this is **not** a touch-rotation/mirroring problem worth
"fixing" at the driver level (`TOUCH_ROTATION` in `display_driver.h`) —
doing that would risk breaking the tap accuracy that already works, to fix
something that's purely a gesture-direction *label* mismatch.

Every screen's gesture handler intentionally checks `LV_DIR_LEFT` for what
is physically a rightward swipe (confirmed working navigation prior to the
2026-09-14 Speed/Drive/Status/Battery split: physical RIGHT swipe advances
settings → main → BMS → dyno LIVE; physical LEFT goes back — the split
preserved this same convention across the current topology: splash/clock →
settings → Speed (home) → Drive → Status → Battery → Charging → GPS →
GPS NAV → dyno LIVE → dyno RESULTS). Each
screen's header comment states the *physical* direction, not
the `LV_DIR_*` constant name, to avoid re-confusing this — read the code's
`LV_DIR_LEFT`/`LV_DIR_RIGHT` checks with this inversion in mind, don't
"correct" them to match the constant names without re-verifying on
hardware first (the two are deliberately reversed here).

## Verification habit

After any screen change, check the result against a real photo of the
running hardware, not just "it compiled." First-pass hardware testing
already caught two real bugs the layout editor didn't surface: an SOC
label overlapping its arc, and a screen silently falling back to the
splash graphic with truncated labels instead of its intended widgets.
Compiling clean does not mean the screen renders correctly.
