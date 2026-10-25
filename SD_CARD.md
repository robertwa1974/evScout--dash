# SD Card Guide

*Last updated: 2026-10-24 (firmware v2.2.1)*

What goes on the dash's microSD card, how to set up a new card, and how to regenerate each dataset. The dash works without a card, but you lose the map, routing, charging stations, the splash animation, data logging and CAN capture.

## Card layout

```
/maps/Z16_r{row}_c{col}.nav     map tiles (739 files for California)
/ROUTE/CAR/ROUTE.bin            routing graph
/charging/stations.bin          charging stations
/splash_frames/frame_00.bin     splash animation, frame_00 .. frame_21
/logs/                          written by the dash (Data Logging)
/can/                           written by the dash (CAN Monitor capture)
```

| Path | Size | Source | If it's missing |
| --- | --- | --- | --- |
| `/maps/` | 1.3 GB | [Maps](#maps-and-routing): `nav_generator` + `split_nav_file.py` | GPS NAV shows the breadcrumb trail on a blank background |
| `/ROUTE/CAR/ROUTE.bin` | 214 MB | [Maps](#maps-and-routing): `route_generator` | Destinations can't compute a route |
| `/charging/stations.bin` | 120 KB | [Charging stations](#charging-stations): `gen_charging_stations.py` | Destinations shows "No charging data loaded" |
| `/splash_frames/` | 2.5 MB | [Splash animation](#splash-animation): `convert_splash_frames.py` | No truck animation, and the splash **waits up to 45 s** before moving on (it normally leaves after one full turn) |

Paths and file names must match exactly. The dash looks files up by name, and there's no index file.

## Setting up a card

1. **Use a card of 32 GB or less, formatted FAT32.** That's what the dash has been tested with. Larger cards come formatted exFAT, and Windows won't format them as FAT32. The data needs about 1.6 GB, so any 4 GB+ card has room for years of logs.
2. **Copy the four datasets.** On this PC they're already built:

   | Copy from | To the card |
   | --- | --- |
   | `Documents\GitHub\tilegen-build\split_output\*.nav` | `\maps\` |
   | `Documents\GitHub\tilegen-build\ROUTE_california\ROUTE\CAR\ROUTE.bin` | `\ROUTE\CAR\ROUTE.bin` |
   | `firmware\assets\charging_stations\stations.bin` (in this repo) | `\charging\stations.bin` |
   | `firmware\assets\splash_frames_out\frame_*.bin` (in this repo) | `\splash_frames\` |

   From PowerShell, with the card on `E:` (adjust the drive letter):

   ```powershell
   $tg   = "$env:USERPROFILE\Documents\GitHub\tilegen-build"
   $repo = "$env:USERPROFILE\Documents\GitHub\Waveshare\uaDASH\firmware\assets"
   robocopy "$tg\split_output"                 E:\maps          *.nav
   robocopy "$tg\ROUTE_california\ROUTE\CAR"   E:\ROUTE\CAR     ROUTE.bin
   robocopy "$repo\charging_stations"          E:\charging      stations.bin
   robocopy "$repo\splash_frames_out"          E:\splash_frames frame_*.bin
   ```

3. **Insert the card with the dash powered off.** It's mounted once at boot, so a card inserted while running isn't seen until the next restart.

**Common mistakes:**
- **Copying `Z16.nav` instead of the split files.** The single 1.3 GB `NAVMAP_california\Z16.nav` is Tile-Generator's raw output and isn't used. The dash reads only the split `Z16_r…_c….nav` files, directly inside `/maps/` with no subfolder.
- **Copying all three routing profiles.** Only `CAR` is used. `BIKE` and `WALK` are about 530 MB that the dash never reads.
- **Leaving the test fixture on the card is fine.** `firmware/assets/fixtures/Z16_r387_c298.nav` is only for the `test_nav_tile` on-device test, and its name doesn't clash with any real California file.

## Maps and routing

Map tiles and the routing graph both come from [`jgauchia/Tile-Generator`](https://github.com/jgauchia/Tile-Generator), **pinned to tag `v.0.9.0`**, run on an OpenStreetMap extract. Don't use a newer tag without re-checking the format: it's pre-1.0, and the dash's reader matches v.0.9.0 exactly. See CLAUDE.md, "Map tile format".

The build on this PC is in `Documents\GitHub\tilegen-build\`:

| Folder | Contents |
| --- | --- |
| `Tile-Generator\build\` | `nav_generator.exe`, `route_generator.exe` |
| `california-latest.osm.pbf` | Geofabrik extract used for the current data |
| `NAVMAP_california\Z16.nav` | Raw tile output (input to the split step) |
| `split_output\` | Split tiles, ready for `/maps/` |
| `ROUTE_california\ROUTE\` | Routing graphs, `CAR` used |

### Regenerating (new region or fresher OSM data)

1. **Download an extract** from [Geofabrik](https://download.geofabrik.de/), e.g. `california-latest.osm.pbf`.
2. **Generate the tiles at zoom 16 only.** That's the only zoom the dash loads (`NAV_TILE_ZOOM`):

   ```
   nav_generator.exe california-latest.osm.pbf NAVMAP_california Tile-Generator\features.json --zoom 16-16
   ```

3. **Split into regional files.** Use the copy of the script in this repo:

   ```
   python firmware\assets\split_nav_file.py NAVMAP_california\Z16.nav split_output
   ```

   It produces `Z16_r{tileY/64}_c{tileX/64}.nav` files using absolute tile numbers. Keep the default `--region-tiles 64`, which must match `NAV_REGION_TILES` in `nav_tile_format.h`. One large file makes SD seeks too slow.
4. **Build the routing graph.** One run writes all three profiles; copy only `CAR`:

   ```
   route_generator.exe california-latest.osm.pbf ROUTE_california
   ```

   California took about 19 minutes for all three profiles.
5. **Copy to the card** as in [Setting up a card](#setting-up-a-card). Delete the old `/maps/` files first, so tiles from a previous region don't linger.

### Rebuilding the tools on Windows

Tile-Generator expects Linux. On this PC it was built natively with MSYS2 (mingw-w64, installed via `winget`). `libosmium` and `protozero` are plain header-only clones next to it. Two small portability patches are needed. Neither changes the tile logic or output, and copies are kept in this repo at `firmware/assets/tilegen-windows/`:

- **`win_compat/sys/mman.h`**: a shim for the `mmap`/`munmap` calls `src/mapped_store.hpp` uses, which mingw-w64 lacks. Put it on the include path.
- **`mapped_store-chsize.patch`**: redirects `ftruncate` to `_chsize_s`. mingw's `ftruncate` silently fails past 2 GB, which a state-sized extract needs. Apply it with `git apply` inside the Tile-Generator clone.

The CMake configure used:

```
CMAKE_CXX_FLAGS=-I<tilegen-build>/win_compat -I<tilegen-build>/libosmium/include -I<tilegen-build>/protozero/include
```

## Charging stations

`/charging/stations.bin` holds stations from [OpenChargeMap](https://openchargemap.org) for a bounding box around San Diego County. The current file was built on 2026-09-22 and holds 1,536 stations; it's committed at `firmware/assets/charging_stations/stations.bin`. It's an offline snapshot: the dash never fetches station data itself.

To refresh it:

1. Get a free API key at <https://openchargemap.org/site/develop/api>.
2. Run:

   ```
   python firmware\assets\gen_charging_stations.py --api-key YOUR_KEY
   ```

   You can set `OCM_API_KEY` instead of passing `--api-key`. The key stays on the PC and never goes into firmware.
3. Copy `firmware\assets\charging_stations\stations.bin` to `\charging\stations.bin` on the card.

For a different area, edit `BBOX_LAT_MIN/MAX` and `BBOX_LON_MIN/MAX` at the top of the script. The file format is described in that script and in `charging_stations.h`; keep the two in sync if either changes.

## Splash animation

The splash screen plays 22 frames of the truck on a turntable, loaded from `/splash_frames/` into PSRAM at boot. Ready-to-copy frames are committed at `firmware/assets/splash_frames_out/`. Each is 240×240 RGB565 in LVGL's raw `.bin` image format, 115,204 bytes.

To replace them with new renders:

1. Put the source PNGs in one folder. They're sorted by file name, so name them in turntable order. The originals are in `Desktop\scout\`.
2. Convert:

   ```
   pip install Pillow
   python firmware\assets\convert_splash_frames.py "C:\Users\rober\Desktop\scout"
   ```

   Output goes to `firmware/assets/splash_frames_out/`. `--size` sets the frame size (default 240). `--bg-threshold` sets how dark a background pixel must be to be flattened to pure black; without that the render's dark-grey studio floor shows as a box.
3. Check them with `python firmware\assets\preview_splash_frames.py`.
4. Copy `frame_*.bin` to `\splash_frames\` on the card.

The firmware expects **exactly 22 frames** (`UI_SPLASH_TRUCK_FRAME_COUNT`) at the size set by `UI_SPLASH_TRUCK_FRAME_SIZE`, both in `ui_splash_truck.h`. A different count or size needs those changed and a rebuild. The splash logo (`img_scout_logo.c`) and the fonts are compiled into the firmware and don't come from the card.

## Files the dash writes

| Path | Written by | How to get it off |
| --- | --- | --- |
| `/logs/LOG0001.csv` … | Settings › Data Logging | WiFi hotspot `/logs` page, or read the card on a PC |
| `/can/CAN0001.csv` … | Settings › CAN Monitor › START CAPTURE | Same; open in SavvyCAN |

File numbers come from counters stored on the board, not on the card. A new card continues the numbering rather than restarting at 0001.
