// ============================================================================
// ui_splashScreen.c - Splash/Clock (logo + digital clock, deliberately simple)
// ============================================================================
// Hand-written (2026-09-14), see ui_splashScreen.h for the full design
// rationale. No SquareLine project (same situation as every other
// hand-written screen in this repo).
//
// Layout (800x480): img_scout_logo (380x142, native resolution - no
// upscale, it's a raster script logo not vector art) centered top, a
// 48px-hero digital UTC clock below it, a small "UTC" caption below that.
// No panels, no bars, no arcs - CLAUDE.md is explicit that splash/clock
// stays "deliberately simple... no data density."
//
// Navigation: this screen sits ahead of Settings in the chain (Settings
// previously had an unused physical-LEFT-swipe direction - now wired here,
// completing the loop): Splash/Clock <-> Settings <-> Speed(home) <-> ...
// <-> GPS <-> Dyno LIVE -> Dyno RESULTS. Only handles physical-RIGHT swipe
// (forward, into Settings) - it's the bookend, same as Settings itself
// previously only handled one direction. This board reports gesture
// direction inverted from the physical swipe (see CLAUDE.md's "Touch
// gesture direction") - the code checks LV_DIR_LEFT for the physical-RIGHT
// swipe. Intentional; don't "fix" it without re-verifying on hardware first.
//
// IS the eager boot screen (corrected 2026-09-14, same day it was first
// built - "no logo evident, just boots to speedo" feedback from Rob made
// clear a screen literally named "splash" should be the first thing
// shown, not a regular mid-chain screen). ui_init() (ui.c) creates+loads
// this screen instead of ui_speedScreen now; a one-shot lv_timer here
// auto-advances to Speed after SPLASH_AUTO_ADVANCE_MS. Because screens in
// this codebase are never actually destroyed once created (see
// _ui_screen_delete's known-inverted-condition note in ui_helpers.h),
// ui_splashScreen_screen_init() - and therefore this timer - only ever
// runs ONCE for the process lifetime: if the user later swipes back here
// manually (from Settings), _ui_screen_change() sees *target != NULL and
// skips re-init, so the auto-advance does not re-trigger on every revisit.
// The timer callback also checks lv_scr_act() == ui_splashScreen before
// acting, in case the user manually swipes away during the delay - without
// that guard, a quick swipe past this screen could get yanked back to
// Speed a couple of seconds later regardless of where they'd navigated to.
// ============================================================================

#include "ui.h"
#include "img_scout_logo.h"

#define SPLASH_AUTO_ADVANCE_MS 2500

lv_obj_t * ui_splashScreen = NULL;
lv_obj_t * ui_splashLogo = NULL;
lv_obj_t * ui_splashClockLabel = NULL;
lv_obj_t * ui_splashClockCaptionLabel = NULL;

static void splashAutoAdvanceCb(lv_timer_t * timer)
{
    LV_UNUSED(timer);  // one-shot (repeat_count=1) - LVGL deletes it after this call returns
    if (lv_scr_act() != ui_splashScreen) return;  // user already swiped away manually - don't yank them back
    _ui_screen_change(&ui_speedScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_speedScreen_screen_init);
    _ui_screen_delete(&ui_splashScreen);
}

void ui_event_splashScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        _ui_screen_change(&ui_settingsScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_settingsScreen_screen_init);
        _ui_screen_delete(&ui_splashScreen);
    }
}

void ui_splashScreen_screen_init(void)
{
    ui_splashScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_splashScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_splashScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_splashScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_splashLogo = lv_img_create(ui_splashScreen);
    lv_img_set_src(ui_splashLogo, &img_scout_logo);
    lv_obj_align(ui_splashLogo, LV_ALIGN_TOP_MID, 0, 70);

    ui_splashClockLabel = lv_label_create(ui_splashScreen);
    lv_obj_align(ui_splashClockLabel, LV_ALIGN_TOP_MID, 0, 250);
    lv_label_set_text(ui_splashClockLabel, "--:--");
    lv_obj_set_style_text_color(ui_splashClockLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_splashClockLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_splashClockCaptionLabel = lv_label_create(ui_splashScreen);
    lv_obj_align(ui_splashClockCaptionLabel, LV_ALIGN_TOP_MID, 0, 322);
    lv_label_set_text(ui_splashClockCaptionLabel, "UTC - waiting for GPS fix");
    lv_obj_set_style_text_color(ui_splashClockCaptionLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_splashClockCaptionLabel, &font_montserrat_extrabold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_splashScreen, ui_event_splashScreen, LV_EVENT_ALL, NULL);

    lv_timer_t *t = lv_timer_create(splashAutoAdvanceCb, SPLASH_AUTO_ADVANCE_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void ui_splashScreen_refresh_theme(void)
{
    if (ui_splashScreen == NULL) return;

    lv_obj_set_style_bg_color(ui_splashScreen, ui_theme_bg(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // Logo image itself is unaffected by day/night (it's a fixed asset,
    // not a themed widget) - only the clock text colors need refreshing.
    if (ui_splashClockLabel) lv_obj_set_style_text_color(ui_splashClockLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_splashClockCaptionLabel) lv_obj_set_style_text_color(ui_splashClockCaptionLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_splashScreen_screen_destroy(void)
{
    if (ui_splashScreen) lv_obj_del(ui_splashScreen);

    ui_splashScreen = NULL;
    ui_splashLogo = NULL;
    ui_splashClockLabel = NULL;
    ui_splashClockCaptionLabel = NULL;
}
