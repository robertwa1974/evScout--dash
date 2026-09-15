#!/usr/bin/env python3
"""
Standing label-width regression check (2026-09-14, see
waveshare-dash-build.md's "bold-font fallout" milestone - this replaces
re-deriving overflow risk by hand every time the font or a layout changes).

Not a generic C-source parser - this project's screens use a small number
of well-known layout conventions (see CLAUDE.md's "Label width check"
rule), so this script encodes those conventions as a manifest below:
each entry is (context, font size, available width in px, list of every
string that widget can actually show - including worst-case dynamic
values, not just what's hardcoded at creation time). Update the manifest
when you add a screen or change a layout; this won't catch a NEW clash on
its own the way a real static analyzer would, but it makes checking a
KNOWN one trivial and repeatable instead of ad-hoc.

Usage: python check_label_widths.py
Exit code 0 if everything fits, 1 if anything overflows.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from convert_font import build_size, RANGES  # noqa: E402

_font_cache = {}


def _gid(cp):
    g = 1
    for lo, hi in RANGES:
        if lo <= cp <= hi:
            return g + (cp - lo)
        g += hi - lo + 1
    return None


def text_width(size, s):
    if size not in _font_cache:
        _font_cache[size] = build_size(size)
    glyphs = _font_cache[size]["glyphs"]
    w = 0.0
    for ch in s:
        i = _gid(ord(ch))
        g = glyphs[i] if i else glyphs[0]
        w += g["adv_w"] / 16
    return w


# (context, font_size_px, budget_px, [candidate strings]) -----------------
# Icon-prefixed pill strings get an extra ICON_ALLOWANCE_PX subtracted from
# the budget below rather than measured exactly - the icon glyph comes from
# each font's regular-weight FALLBACK font (see ui.h), which this script
# doesn't load metrics for. 30px is a conservative overestimate for a 24px
# icon glyph + its following space.
ICON_ALLOWANCE_PX = 30

MANIFEST = [
    # --- Drive screen: createPanel titles, 16px, 388px panel (~364px budget) ---
    ("Drive panel titles", 16, 364, [
        "POWER", "PACK CURRENT", "GEAR", "MOTOR MODE", "REGEN LIMIT",
    ]),
    # Drive screen enum values, 24px, centered in a 388px panel (~360px budget)
    ("Drive gear enum", 24, 360, ["LOW", "HIGH", "AUTO", "HI-FOR/LO-REV"]),
    ("Drive motor-mode enum", 24, 360, ["MG1+MG2", "MG1", "MG2", "BLEND"]),

    # --- Status screen: createPanel titles, 16px, 364px budget ---
    ("Status panel titles", 16, 364, [
        "SOC", "PACK VOLTAGE", "AUX 12V", "MOTOR TEMP", "INV TEMP", "MAX BATT TEMP",
    ]),

    # --- Charging screen: createPanel titles, 16px, 364px budget ---
    ("Charging panel titles", 16, 364, [
        "SOC", "CHARGE STATUS", "SETPOINT", "CHARGER TEMP", "AC VOLTS", "EVSE LIMIT",
    ]),
    # Charging status pill, 24px, 320px pill (icon shown only while charging)
    ("Charging status pill", 24, 320 - ICON_ALLOWANCE_PX, [
        "CHARGING - AC", "CHARGING - DCFC",
    ]),
    ("Charging status pill (no icon)", 24, 320, ["NOT CHARGING"]),
    # Secondary caption labels, 16px, generous panel-width budget (388px panel)
    ("Charging plug caption", 16, 360, [
        "Plug: Connected", "Plug: Not detected", "Plug: --",
    ]),
    ("Charging cable caption", 16, 200, ["Cable: 100A", "Cable: --"]),

    # --- Battery screen: createBarRow, 16px, title-before-bar = 160px ---
    ("Battery bar-row titles", 16, 160, [
        "MAX CELL V", "MIN CELL V", "CELL DELTA V", "MAX CELL TEMP",
    ]),
    # Battery status pill, 24px, 240px pill, no icon
    ("Battery status pill", 24, 240, ["CHARGING", "DISCHARGING", "IDLE"]),

    # --- GPS screen: createPanel titles, 16px, 364px budget ---
    ("GPS panel titles", 16, 364, [
        "GPS STATUS", "SPEED (GPS)", "LATITUDE", "LONGITUDE", "HEADING", "ALTITUDE",
    ]),
    # GPS status pill, 24px, 200px pill (icon shown only with a fix)
    ("GPS status pill", 24, 200 - ICON_ALLOWANCE_PX, ["GPS FIX"]),
    ("GPS status pill (no icon)", 24, 200, ["NO FIX"]),
    ("GPS sats caption", 16, 180, ["Sats: 24", "Sats: --"]),
    # GPS coordinate values, 24px, 388px panel (~360px budget)
    ("GPS coordinate values", 24, 360, ["-122.4194\xb0 W", "-179.9999\xb0 W"]),

    # --- Dyno LIVE screen: state pill, 24px, 340px pill ---
    ("Dyno state pill", 24, 340, [
        "READY - tap to arm", "ARMED - go!", "RUNNING", "DONE",
    ]),

    # --- Splash/Clock: 800px-wide screen, generously centered ---
    ("Splash caption", 16, 700, ["UTC - waiting for GPS fix", "UTC"]),
]


def main():
    failures = 0
    for context, size, budget, strings in MANIFEST:
        for s in strings:
            w = text_width(size, s)
            status = "OK" if w <= budget else "OVERFLOW"
            if status == "OVERFLOW":
                failures += 1
            print(f"[{status:8}] {context:32} {size:2}px  {w:6.1f}px / {budget}px  {s!r}")

    print()
    if failures:
        print(f"{failures} label(s) overflow their budget.")
        sys.exit(1)
    else:
        print("All labels fit.")
        sys.exit(0)


if __name__ == "__main__":
    main()
