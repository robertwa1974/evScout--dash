# Milestone 2 — CAN bench validation

## Result (2026-09-13): telemetry validated, architecture pivoted to all-SDO

Bench session with the spare ZombieVerter (node 3, 500 kbit/s) plus Rob's
working M5 Dial on the same bus. Findings:

1. **This VCU has no canmap configured — confirmed independently on both the
   Dial and the Waveshare.** In 20s+ traces the *only* CAN IDs ever seen were
   `0x603`/`0x583` (SDO). None of `0x126/0x210/0x257/0x300/0x301/0x302/0x355/0x356`
   ever appeared. `kCanBroadcastParams` / `decodeBroadcastFrame()` cannot be
   validated on this hardware — there's no canmap traffic to decode.
2. **SDO round-trips correctly end to end.** Every `0x603` request got a
   matching `0x583` response with correct index/subindex and `cmd=0x43`.
   Node ID, COB-IDs, and the index/subindex folding in `can_sdo.cpp` are
   confirmed correct as-is.
3. **The blanket `/32` SDO scale (`OI_FIXED_POINT_SCALE`) is correct across
   the board** — tunables, enums, and 2000+ telemetry all came back as clean
   round numbers after dividing by 32:

   | param | raw (wire) | ÷32 | reads as |
   |---|---|---|---|
   | revlim (15) | 192000 | 6000.000 | plausible motor rev limit (rpm) |
   | BattCap (38) | 1152 | 36.000 | plausible pack capacity (kWh) |
   | Gear (27) | 32 | 1.000 | valid enum (HIGH) — raw=32 alone is out of range |
   | SOC (2015) | 3200 | 100.000 | plausible % |
   | tmpm (2029) | −887 | −27.719 | negative — confirms **signed** |

   This **reverses an earlier concern**: `can_sdo.h`'s `sdoToFloat` (÷32 for
   everything, including `opmode`/`lasterr`) was flagged pre-bench as likely
   wrong for enums. Real data shows OpenInverter stores tunables, enums, and
   telemetry all in the same ×32 fixed-point representation — no special
   case needed. `opmode`/`lasterr`/`power`/`Veh_Speed` stayed at 0 (VCU idle,
   `opmode=0`/Off) so they're not independently confirmed, but nothing points
   to different scaling either.
4. **`tmpm` reading a real negative value** (open-thermistor with no motor
   attached, plausible) directly confirms the signed decode is needed for
   that physical quantity — supports the `isSigned=true` guess already in
   `kCanBroadcastParams` for the dormant broadcast path too, even though that
   path itself is unvalidated.

**Decision (Rob, 2026-09-13): pivot production telemetry to all-SDO
polling.** The 10 params that used to be broadcast-only (`SOC`, `udc`, `idc`,
`tmpm`, `tmphs`, `U12V`, `speed`, `Gear`, `MotActive`, `regenmax`) were
promoted into the permanent SDO poll table in `zombie_updaters.{h,cpp}`
(`PARAM_ID_SOC` etc., `PERIOD_SDO_POLL_MS` = 50ms, 14 entries -> ~700ms full
refresh). Confirmed on hardware: `myData` populates correctly end-to-end
(`soc=100`, `gear=1`, `motorTemp=-27`, matching the raw SDO values above).

`kCanBroadcastParams` / `decodeBroadcastFrame()` are left in place, dormant —
if canmap ever gets configured on a VCU, broadcast decode runs in parallel
with the SDO polls with no code changes needed, and this document's original
validation procedure (below) still applies to it.

---

## Original validation plan (for if/when canmap exists on a bench VCU)

Goal: confirm `kCanBroadcastParams`' `gain` and `isSigned` values against
real canmap traffic — moot until a VCU has canmap configured (see Result
above for why the spare VCU can't be used for this).

### What you'd need

- A ZombieVerter VCU **with canmap configured** for the 10 params above.
- A SavvyCAN-capable USB-CAN tool on the same bus, for ground-truth capture
  and later replay.
- The VCU's web UI (`esp32-web-interface`) as the human-readable reference.

### Wiring

All nodes on one twisted pair, **120 Ω at each physical end only**, common
ground between all of them. The Waveshare has no onboard 120 Ω — add one at
its connector if it ends up at a physical end of the bus.

### Build, flash, monitor (trace build)

```
pio run -e cantrace -t upload
pio device monitor -e cantrace          # 115200; PYTHONUTF8=1 on Windows
```

`[env:cantrace]` is the normal firmware **plus** `-DCAN_TRACE` — nothing else
changes. The shipping `[env:waveshare-s3-lcd7]` build stays silent.

Trace output:

```
RX 0x356 [8] A0 0F 2C 02 26 00 00 00
FLD udc        id=2006 raw=4000     -> 360.000 V
SDO> 0x603 [8] 40 07 21 E1 00 00 00 00   (param 2017)
SDO< 2017 cmd=0x43 raw=1920     -> 60.000
==== myData @ 12345 ms ====
  [bcast] motorRpm=2000  packV=360.00  packA=50.04  motT=38  hsT=45  aux12V=13.50
  [bcast] soc=82  gear=2  motActive=1  regenMax=75.00
  [sdo]   vehSpeedKph=60  opmode=1  powerKw=15.00  lastErr=0  dcdcState=0
```

If the raw `RX` firehose is too much, comment out the `canTraceFrame("RX", …)`
call in `zombie_updaters.cpp` `TaskCANReceiver` — `FLD`/`SDO` lines are the
ones that matter for decode validation.

### Validation procedure

1. Let the VCU idle ~30s with canmap active. Compare each `FLD` value against
   the web UI.
2. Off by a constant ratio → fix `gain` in `kCanBroadcastParams`.
3. Huge positive where a small negative is expected (regen current, reverse
   rpm, sub-zero temp) → flip `isSigned` for that row.
4. Drive/throttle so `idc` swings negative and `speed` goes to reverse;
   confirm the signed fields track correctly.
5. Save the SavvyCAN capture as `test/zombieverter_replay.csv` (overwrite the
   synthetic one) — note the VCU firmware `version` (param 2000) alongside it.

### Fill-in table

| field | source | decoded (trace) | reference (web UI / SavvyCAN raw) | verdict |
|-------|--------|-----------------|----------------------------------|---------|
| tmphs | 0x126 | | | not testable — no canmap on the spare VCU |
| U12V  | 0x210 | | | not testable — no canmap on the spare VCU |
| speed | 0x257 | | | not testable — no canmap on the spare VCU |
| Gear  | 0x300 | | | not testable — no canmap on the spare VCU |
| MotActive | 0x301 | | | not testable — no canmap on the spare VCU |
| regenmax | 0x302 | | | not testable — no canmap on the spare VCU |
| SOC   | 0x355 | | | not testable — no canmap on the spare VCU |
| udc   | 0x356 | | | not testable — no canmap on the spare VCU |
| idc   | 0x356 | | | not testable — no canmap on the spare VCU |
| tmpm  | 0x356 | | | not testable — no canmap on the spare VCU |
| Veh_Speed | SDO 2017 | raw=0 | 0 (idle) | ✅ SDO path confirmed working, value not independently checkable at 0 |
| opmode | SDO 2002 | raw=0 | 0 = Off (idle) | ✅ plausible, scale unconfirmed at 0 |
| power | SDO 2011 | raw=0 | 0 (idle) | ✅ plausible, scale unconfirmed at 0 |
| lasterr | SDO 2004 | raw=0 | 0 = NONE (idle) | ✅ plausible, scale unconfirmed at 0 |

### Replay (no VCU needed)

`test/zombieverter_replay.csv` is a **synthetic** SavvyCAN capture exercising
the dormant broadcast path: the 8 broadcast IDs plus scripted `0x583` SDO
responses, over 3s, cycling cruise → regen/reverse → accel. Payloads are
back-computed from the current decoder gains, so a clean replay prints round
numbers. It regression-tests `decodeBroadcastFrame()` without needing a VCU
at all — useful if canmap ever gets configured and that code path is
revisited. It does **not** need replacing by a real capture for the SDO path,
since that's now validated directly on hardware (see Result above).

SavvyCAN → **Connection ▸ Add ▸ Mock/loopback** (or a real adapter looped to
the Waveshare), then **File ▸ Load Frames** the CSV, then **Playback ▸** set
the file, enable *loop*. Native `Time Stamp,ID,Extended,Dir,Bus,LEN,D1..D8`
format.
