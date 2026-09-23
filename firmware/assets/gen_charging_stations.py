#!/usr/bin/env python3
"""
Pulls EV charging station locations from OpenChargeMap
(https://openchargemap.org, free/community-maintained, global coverage)
for a bounding box around San Diego County - this vehicle's operating
region, same regional-scoping decision already made for the statewide NAV
map tiles (see CLAUDE.md's "Map tile format" section) - and writes a flat
binary file for firmware/src/charging_stations.h to load onto the SD card.

This is a ONE-TIME (or occasionally-rerun) offline data-prep step run on a
dev machine, NOT something the vehicle's own firmware ever calls live -
this project has no internet connectivity on the dash itself. Re-run this
script and re-copy its output whenever the station list needs refreshing
(OpenChargeMap's data changes as stations are added/removed/updated).

Requires a free OpenChargeMap API key - register at
https://openchargemap.org/site/develop/api, then either pass it with
--api-key or set the OCM_API_KEY environment variable. The key is only
ever used from this dev-machine script; it never ships in firmware.

File format (see charging_stations.h - keep both in sync if either
changes):
  Header:  4s  magic ("CHG1")
           I   count (uint32, little-endian)
  Then `count` records, each 80 bytes, little-endian, no padding:
           d   lat
           d   lon
           40s name       (UTF-8, NUL-padded/truncated)
           20s connector  (UTF-8, NUL-padded/truncated)
           f   powerKw

Usage:
  python gen_charging_stations.py --api-key YOUR_KEY
  python gen_charging_stations.py   # if OCM_API_KEY is set in the environment
Writes firmware/assets/charging_stations/stations.bin (path printed at the
end) - copy that file to the SD card at /charging/stations.bin (see
charging_stations.h's CHARGING_STATIONS_PATH).
"""
import argparse
import json
import os
import struct
import sys
import urllib.request
import urllib.parse
from pathlib import Path

HERE = Path(__file__).parent
OUT_DIR = HERE / "charging_stations"
OUT_PATH = OUT_DIR / "stations.bin"

# San Diego County's approximate extent - Mexico border to the Orange
# County line, coast to the Imperial County line. Approximate on purpose;
# a station just outside this box is a rare, low-stakes miss for a
# "nearest charging station" convenience feature, not a correctness bug
# worth chasing an exact county polygon for.
BBOX_LAT_MIN, BBOX_LAT_MAX = 32.53, 33.51
BBOX_LON_MIN, BBOX_LON_MAX = -117.60, -116.08

OCM_API_URL = "https://api.openchargemap.io/v3/poi/"

CHG_NAME_MAXLEN = 40
CHG_CONNECTOR_MAXLEN = 20
CHG_FILE_MAGIC = b"CHG1"
RECORD_FORMAT = "<dd%ds%dsf" % (CHG_NAME_MAXLEN, CHG_CONNECTOR_MAXLEN)
HEADER_FORMAT = "<4sI"

assert struct.calcsize(RECORD_FORMAT) == 80, \
    "record size drifted from charging_stations.h's ChargingStation struct - keep both in sync"


def fetch_stations(api_key: str) -> list:
    # OpenChargeMap API v3 /poi/ params - see
    # https://openchargemap.org/site/develop/api/reference/poi for the
    # current reference if this ever needs re-verifying against a live API
    # change. boundingbox is "(lat1,lon1),(lat2,lon2)" (two opposite
    # corners); compact=true trims response verbosity we don't need.
    params = {
        "output": "json",
        "countrycode": "US",
        "boundingbox": f"({BBOX_LAT_MAX},{BBOX_LON_MIN}),({BBOX_LAT_MIN},{BBOX_LON_MAX})",
        "maxresults": 5000,
        "compact": "true",
        "verbose": "false",
        "key": api_key,
    }
    url = OCM_API_URL + "?" + urllib.parse.urlencode(params)
    print(f"Fetching from OpenChargeMap: {url.split('key=')[0]}key=<redacted>")
    # A plain urllib.request.urlopen(url) sends Python's default
    # "Python-urllib/x.y" User-Agent, which OpenChargeMap's edge/WAF
    # rejects with a bare 403 (no body) before the request even reaches
    # their API logic - not an auth/key problem. A real browser-like User-
    # Agent, plus the key ALSO sent as the X-API-Key header (belt and
    # braces - some OCM edge configs check the header, not just the query
    # param), fixes this.
    req = urllib.request.Request(url, headers={
        "User-Agent": "Mozilla/5.0 (compatible; uaDASH-charging-station-fetch/1.0)",
        "X-API-Key": api_key,
    })
    with urllib.request.urlopen(req, timeout=60) as resp:
        data = json.load(resp)
    print(f"Received {len(data)} raw POI records")
    return data


def best_connection(connections: list):
    """Picks the highest-power connection at a station (what a driver
    actually cares about first), falling back to the first entry with any
    name at all if none report a power figure."""
    if not connections:
        return None, 0.0
    with_power = [c for c in connections if c.get("PowerKW")]
    chosen = max(with_power, key=lambda c: c["PowerKW"]) if with_power else connections[0]
    conn_type = chosen.get("ConnectionType") or {}
    name = conn_type.get("Title") or "Unknown"
    power = float(chosen.get("PowerKW") or 0.0)
    return name, power


def encode_station(poi: dict) -> bytes:
    addr = poi.get("AddressInfo") or {}
    lat = addr.get("Latitude")
    lon = addr.get("Longitude")
    if lat is None or lon is None:
        return None

    name = (addr.get("Title") or "Unknown Station")[:CHG_NAME_MAXLEN - 1]
    conn_type, power_kw = best_connection(poi.get("Connections") or [])
    conn_type = (conn_type or "Unknown")[:CHG_CONNECTOR_MAXLEN - 1]

    return struct.pack(
        RECORD_FORMAT,
        float(lat), float(lon),
        name.encode("utf-8"),
        conn_type.encode("utf-8"),
        power_kw,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--api-key", default=os.environ.get("OCM_API_KEY"),
                         help="OpenChargeMap API key (or set OCM_API_KEY)")
    args = parser.parse_args()

    if not args.api_key:
        print("ERROR: no API key - pass --api-key or set OCM_API_KEY", file=sys.stderr)
        print("Register for a free key at https://openchargemap.org/site/develop/api", file=sys.stderr)
        sys.exit(1)

    pois = fetch_stations(args.api_key)

    records = []
    skipped = 0
    for poi in pois:
        rec = encode_station(poi)
        if rec is None:
            skipped += 1
            continue
        records.append(rec)

    if skipped:
        print(f"Skipped {skipped} POIs with no usable coordinates")
    if not records:
        print("ERROR: no valid station records to write - aborting", file=sys.stderr)
        sys.exit(1)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with open(OUT_PATH, "wb") as f:
        f.write(struct.pack(HEADER_FORMAT, CHG_FILE_MAGIC, len(records)))
        for rec in records:
            f.write(rec)

    print(f"Wrote {len(records)} stations ({OUT_PATH.stat().st_size} bytes) to {OUT_PATH}")
    print("Copy this file to the SD card at /charging/stations.bin")


if __name__ == "__main__":
    main()
