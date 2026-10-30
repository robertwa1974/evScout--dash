# Scout 80 EV Dashboard

A CAN-bus dashboard for a Scout 80 EV conversion, running on a Waveshare
ESP32-S3-Touch-LCD-7 (800x480). Talks to a **ZombieVerter** EV VCU over
CANopen SDO — this is not the original rusEFI ICE dashboard project it was
forked from.

## What it does

- **Home cluster** — 400px speedometer (mph) with odometer and trip meter,
  P R N D strip, SOC and inverter-temperature needle gauges, power /
  range / pack A / 12V cards. Road speed is computed from motor rpm
  (GPS-calibratable); trip, recent and lifetime Wh/mi drive the range
  estimate.
- **Telemetry screens** — Drive (with tap-to-edit gear, motor mode, regen
  limit and brake regen, mirroring the Zombieverter Dial), Incline (pitch
  and roll from the IMU, corrected for acceleration and cornering), Status,
  Battery (cell-level), Charging, GPS.
- **Navigation** — GPS NAV with offline vector map tiles and routing to
  Home / Work / the nearest charging station (offline datasets on the SD
  card).
- **0-60 / virtual dyno** — live timer plus a road-load vs. electrical
  power results chart.
- **Settings** — warning thresholds, display (day/night, brightness, UTC
  offset, WiFi), road-speed **Calibration**, **VCU Parameters** (read/write ZombieVerter tunables over
  SDO, save to VCU flash), **Diagnostics** (~40 raw VCU values), **Data
  Logging** (CSV to SD) and a **CAN Monitor** (per-ID bus summary, light
  decoding of VCU / IVT-S / SimpBMS frames, known-good baseline with
  NEW / MISSING / RATE flags, raw capture in SavvyCAN CSV format).
- **Local WiFi hotspot** (`Scout80-Config`, off by default) — config page,
  live telemetry, log/capture downloads and firmware update at
  `http://192.168.4.1`. No
  internet access; see `CLAUDE.md` → Networking.

Hardware: Waveshare ESP32-S3-Touch-LCD-7 only (the upstream multi-board
support was removed), microSD card, ZombieVerter VCU on CAN at 500 kbit,
optional GPS module and MPU6050 IMU.

For day-to-day use of the dash, see [`USER_MANUAL.md`](USER_MANUAL.md). For
what goes on the microSD card (maps, routing, charging stations, splash
frames) and how to regenerate it, see [`SD_CARD.md`](SD_CARD.md).

For the full picture — architecture, hardware list, build-issue history,
and every design decision along the way — read, in this order:

1. [`CLAUDE.md`](CLAUDE.md) — UI/style conventions (widget selection,
   screen layout, typography, touch-gesture quirks)
2. [`scout80-dash-architecture.md`](scout80-dash-architecture.md) —
   system overview and hardware list
3. [`waveshare-dash-build.md`](waveshare-dash-build.md) — hardware,
   resolved toolchain issues, CAN/SDO design and current build status
   (detailed history is in the git log and release tags)

## Fork lineage

Forked from [`Light-r4y/uaDASH`](https://github.com/Light-r4y/uaDASH) (MIT
licensed), which was itself built for rusEFI-based ICE vehicles talking
rusEFI's own CAN broadcast protocol. This fork replaced that CAN layer
entirely, and the UI has since been rebuilt screen-by-screen for EV
telemetry — none of it is SquareLine-round-tripped anymore; every screen
is hand-written (see `CLAUDE.md`).

## Build

PlatformIO, not Arduino CLI/IDE:

```
cd firmware
pio run -e waveshare-s3-lcd7
pio run -e waveshare-s3-lcd7 -t upload --upload-port <your COM port>
```

Other environments:

- `[env:cantrace]` — CAN/SDO bench diagnostics: verbose serial trace of
  every frame and SDO transaction, plus a once-a-second internal/PSRAM
  heap line. Keeps UART0 on the USB port at 115200, so crash backtraces
  are readable (the shipping build hands UART0 to the GPS module).
- `[env:mock-gps]` — replays a canned GPS route for bench work on the
  GPS/NAV screens. Note that it also feeds the odometer.

**1-click web installer:** <https://robertwa1974.github.io/evScout--dash/>
(Chrome/Edge over USB, UART DIP switch on **USB**). It writes the
bootloader, partition table, `boot_app0` and app as separate parts, so NVS
(settings, speed calibration, odometer) survives unless you tick "Erase
device". Published by `.github/workflows/web-flasher.yml` on every `v*`
tag, which also creates the GitHub Release. Set `DASH_TAG` in
`firmware/src/build_info.h` to the tag first; the workflow fails if they
differ.

**Updating over WiFi** (v2.1+, once that firmware has been flashed over USB
once): join the dash's `Scout80-Config` hotspot, open
`http://192.168.4.1/update`, choose a release's `firmware.bin` (not the
`-full.bin` merged image - it's rejected), Install. Settings and the
odometer are kept.

Flashing over USB needs the board's UART DIP switch on **USB** (not GPS). CI
(`.github/workflows/binary.yaml`) builds the shipping firmware on every
push and uploads it as an artifact.

**Before flashing**, always confirm the target board's identity:

```
esptool --chip esp32s3 --port <COM port> flash-id
```

and check the MAC address against the board you intend to flash — this
project runs on more than one ESP32-S3 board during development, and
flashing the wrong one is easy to do by accident.

## Brightness solder mod (Waveshare ESP32-S3-Touch-LCD-7)

Inherited from the upstream fork; worth re-verifying against your board
revision before relying on it. A wire needs to be soldered for the
swipe-up/down brightness gesture to work:

![Brightness mod point](media/upgrade_for_brightness_7.png)

## License

MIT — see [`LICENSE`](LICENSE). Original copyright Light-r4y (2025).
