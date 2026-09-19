#pragma once

// Shared 2x3 telemetry-card grid pattern (styling/UX pass, 2026-09-18).
// Consolidates createPanel()/createValueAndUnit()/createBar(), previously
// hand-duplicated near-verbatim in ui_driveScreen.c, ui_statusScreen.c,
// ui_gpsScreen.c, ui_chargingScreen.c (each had its own file-local static
// copy - an earlier, deliberate convention from the 2026-09-14 screen
// split, now revisited since the styling pass needs the SAME behavior
// change - font weight, no-data state, bar gradient rule - applied
// identically at 4+ call sites; hand-editing four copies in lockstep is
// exactly the failure mode a shared header avoids). ui_statusScreen.c was
// the most complete of the four (had the zoneDir gradient param the
// others lacked) and is the reference this consolidation is based on.
//
// ui_batteryScreen.c (Phase 3 of the styling pass) also builds onto this,
// replacing its previously-bespoke arc+pill+bar-row layout.
//
// C linkage - every current caller is a plain .c screen file.

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#define GRID_PANEL_W 388
// 124, not the original 149 - shrunk (styling/UX pass Phase 5, 2026-09-18)
// to make room for the persistent 72px bottom nav dock (ui_dock.h) without
// overlapping the grid's 3rd row: 8+124+8+124+8+124+8=404, +72 dock =
// 476/480, 4px slack. The original 149 used 479/480 with the dock, leaving
// no room at all. Every internal offset in ui_card_createValueAndUnit()/
// ui_card_createBar() below is tuned against THIS height - if it changes
// again, re-check both for overflow, not just this one define.
#define GRID_PANEL_H 124
#define GRID_GAP     8

#ifdef __cplusplus
extern "C" {
#endif

// Builds one 388x149 card: panel + title label. Title label uses the new
// SemiBold-16 weight (styling pass item 1: bold reserved for numeric
// values, labels get a visibly lighter weight) - a real behavior change
// from the pre-consolidation createPanel(), which used ExtraBold like
// everything else.
lv_obj_t *ui_card_createPanel(lv_obj_t *parent, int16_t x, int16_t y,
                               const char *title, lv_obj_t **outTitleLabel);

// Builds a centered value label (ExtraBold-32, stays bold - this is the
// numeric-hero role) + a centered unit label below it (SemiBold-16, same
// "labels get lighter" rule as the panel title).
void ui_card_createValueAndUnit(lv_obj_t *panel, lv_obj_t **outVal,
                                 lv_obj_t **outUnit, const char *unitText);

// zoneDir: 0 = flat track (no real warningSet threshold behind this
// field), +1 = danger-at-high-end gradient, -1 = danger-at-low-end
// gradient. Now a required param at every call site (previously only
// ui_statusScreen.c's variant had it, ui_gpsScreen.c's lacked it entirely
// and always built a flat track) - forces every migrated call site to
// make an explicit, auditable choice per styling pass item 5's "one
// consistent bar rule," rather than silently defaulting to flat.
lv_obj_t *ui_card_createBar(lv_obj_t *panel, bool symmetrical,
                             int32_t rangeMin, int32_t rangeMax, int zoneDir);

// Re-applies zoneDir's track gradient (or the flat ui_theme_bg() track if
// zoneDir==0) - exposed so each screen's own refresh_theme() loop can call
// it directly on day/night toggle, same usage pattern ui_statusScreen.c
// already had, just no longer file-local.
void ui_card_setZoneGradient(lv_obj_t *bar, int zoneDir);

// No-data state (styling pass items 3-4): while noData is true, the bar's
// track AND indicator both go to ui_theme_dim(), overriding zoneDir's
// gradient entirely - a value that has never arrived from the VCU/BMS
// must never look like "reading zero," let alone "in the danger zone."
// Pass the field's normal zoneDir so the bar reverts to its correct
// gradient/flat treatment the moment noData goes false - callers don't
// need to remember it separately.
void ui_card_setBarNoData(lv_obj_t *bar, bool noData, int zoneDir);

// Companion for a value/unit label pair - grays both to ui_theme_dim()
// while noData is true (val keeps its normal ExtraBold weight, this only
// changes color), restores ui_theme_text_primary()/text_secondary() when
// false. This is the "no real data yet" analog of zombie_updaters.cpp's
// setWarnColor() - a different concept (see ui_theme.h's ui_theme_dim()
// comment for why they're deliberately different colors), so a separate
// function rather than a third setWarnColor() argument.
void ui_card_setValueNoData(lv_obj_t *val, lv_obj_t *unit, bool noData);

#ifdef __cplusplus
}
#endif
