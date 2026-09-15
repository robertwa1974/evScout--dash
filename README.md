# Scout 80 EV Dashboard

A CAN-bus dashboard for a Scout 80 EV conversion, running on a Waveshare
ESP32-S3-Touch-LCD-7 (800x480). Talks to a **ZombieVerter** EV VCU over
CANopen SDO — this is not the original rusEFI ICE dashboard project it was
forked from.

Ten screens: Speed (home, full-screen speedometer + P/N/F/R), Drive,
Status, Battery, Charging, splash/logo, clock, GPS navigation, 0-60/
virtual dyno, and settings.

For the full picture — architecture, hardware list, build-issue history,
and every design decision along the way — read, in this order:

1. [`CLAUDE.md`](CLAUDE.md) — UI/style conventions (widget selection,
   screen layout, typography, touch-gesture quirks)
2. [`scout80-dash-architecture.md`](scout80-dash-architecture.md) —
   system overview and hardware list
3. [`waveshare-dash-build.md`](waveshare-dash-build.md) — the living build
   log: every resolved toolchain/boot issue, milestone, and bug found on
   real hardware

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

A `[env:cantrace]` build is also available for CAN/SDO bench diagnostics
(verbose serial trace of every frame and SDO transaction) — see
`waveshare-dash-build.md` for details.

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
