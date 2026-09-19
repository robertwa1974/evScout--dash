// ============================================================================
// ui_splashScreen.cpp - Splash/Clock (logo wipe + digital clock)
// ============================================================================
// Hand-written (2026-09-14, rebuilt 2026-09-18 - styling pass item 11), see
// ui_splashScreen.h for the full design rationale. No SquareLine project
// (same situation as every other hand-written screen in this repo).
// Renamed .c -> .cpp with this rebuild - needs display_driver.h
// (backlight_rampTo()), which pulls in LovyanGFX's C++ class definitions
// and can't be included from a plain .c translation unit (same reason
// ui_navScreen.c and ui_dynoLiveScreen.c were renamed .cpp earlier).
//
// Layout (800x480): img_scout_logo (380x142, native resolution - no
// upscale, it's a raster script logo not vector art) centered top, a
// 48px-hero digital UTC clock below it, a small "UTC" caption below that.
// No panels, no bars, no arcs - CLAUDE.md is explicit that splash/clock
// stays "deliberately simple... no data density."
//
// Boot sequence (styling pass item 11):
//   1. Background is hardcoded pure black (0x000000), NOT ui_theme_bg() -
//      the one screen in this codebase that deliberately doesn't theme its
//      background. Don't "fix" this to use the theme getter in a future
//      consistency pass - it's intentional, so the logo wipe reads as a
//      real boot sequence regardless of which theme the user last left the
//      dash in, not a themed page loading.
//   2. Logo wipe: a black rectangle exactly covering the logo, right edge
//      pinned to the logo's right edge, animates its own width from full
//      to 0 over SPLASH_WIPE_MS - as the rectangle's LEFT edge retreats
//      rightward (right edge fixed), the logo is revealed left-to-right
//      underneath it. Positioned as a SIBLING of the logo image (not a
//      child of it) - a child positioned/resized in ways that touch a
//      parent's edge has bitten this codebase before (see
//      ui_chargingScreen.c's battery-nub comment for the exact clipping
//      bug that taught that lesson), so this avoids the same risk instead
//      of re-verifying it doesn't apply here.
//   3. Backlight ramps from whatever duty it was left at up to full (255)
//      over the same ~900ms window (backlight_rampTo(), display_driver.h)
//      - so the screen visibly brightens as the logo reveals, not before.
//   4. Exit gate: waits for the wipe to finish, THEN exits on whichever
//      comes first - the first real CAN frame (ui_can_freshness_
//      hasEverReceived(), so the dash doesn't sit on a splash screen once
//      the VCU is actually talking) or a SPLASH_EXIT_TIMEOUT_MS timeout
//      (so a dash with no CAN connected yet - bench testing, wiring in
//      progress - doesn't hang here forever). Polled via a short-period
//      lv_timer rather than computed as a single deadline, since the
//      CAN-arrival condition can't be predicted in advance.
//   5. Exit transition is LV_SCR_LOAD_ANIM_FADE_ON, not a slide - the only
//      screen transition in this codebase that isn't MOVE_LEFT/MOVE_RIGHT.
//      Intentional: this is a boot sequence ending, not lateral navigation
//      between sibling screens, so a fade reads correctly where a slide
//      wouldn't. Don't "correct" it to match every other transition.
//   6. Tap-to-skip: a tap anywhere on this screen fires the same exit path
//      immediately, bypassing the wipe/CAN-wait/timeout gate entirely -
//      registered as a separate LV_EVENT_CLICKED handler alongside the
//      existing LV_EVENT_ALL gesture handler, same "two handlers on one
//      object, different event codes" pattern already used on
//      ui_dynoLiveScreen.cpp's screen-tap-to-arm handler.
//   No navigation dock on this screen - splash sits outside the dock
//   system entirely (confirmed with Rob, styling pass dock spec).
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
// this screen instead of ui_speedScreen now. Because screens in this
// codebase are never actually destroyed once created (see
// _ui_screen_delete's known-inverted-condition note in ui_helpers.h),
// ui_splashScreen_screen_init() - and therefore its wipe animation and
// exit-poll timer - only ever runs ONCE for the process lifetime: if the
// user later swipes back here manually (from Settings), _ui_screen_change()
// sees *target != NULL and skips re-init, so none of this re-triggers on
// every revisit. The exit-poll timer also gets explicitly stopped the
// moment the user manually swipes away (see ui_event_splashScreen below) -
// without that, it would keep polling harmlessly in the background for up
// to SPLASH_EXIT_TIMEOUT_MS after the user has already left.
// ============================================================================

#include "ui.h"
#include "img_scout_logo.h"
#include "display_driver.h"
#include "ui_can_freshness.h"

#define SPLASH_WIPE_MS          900
#define SPLASH_EXIT_POLL_MS      50
#define SPLASH_EXIT_TIMEOUT_MS 3000
#define SPLASH_BACKLIGHT_TARGET 255

lv_obj_t * ui_splashScreen = NULL;
lv_obj_t * ui_splashLogo = NULL;
lv_obj_t * ui_splashClockLabel = NULL;
lv_obj_t * ui_splashClockCaptionLabel = NULL;
static lv_obj_t * ui_splashWipeRect = NULL;

// Wipe-rect geometry, captured once at creation (see this file's header
// comment) - referenced by splashWipeAnimExec() below, which can't close
// over locals since it's a plain C-style lv_anim exec callback.
static lv_coord_t splashWipeBaseX = 0;
static lv_coord_t splashWipeBaseW = 0;

static bool     splashWipeDone = false;
static bool     splashExiting = false;
static uint32_t splashExitStartTick = 0;
static lv_timer_t * splashExitTimer = NULL;

static void splashWipeAnimExec(void * obj, int32_t v) {
    lv_obj_t * rect = (lv_obj_t *)obj;
    lv_obj_set_width(rect, v);
    lv_obj_set_x(rect, splashWipeBaseX + splashWipeBaseW - v);
}

static void splashWipeReadyCb(lv_anim_t * a) {
    LV_UNUSED(a);
    splashWipeDone = true;
}

// Single exit path, shared by the CAN/timeout gate and the tap-to-skip
// handler - idempotent (splashExiting guard) since both can race to call
// this around the same moment.
static void splashDoExit(void) {
    if (splashExiting) return;
    splashExiting = true;
    if (splashExitTimer) {
        lv_timer_del(splashExitTimer);
        splashExitTimer = NULL;
    }
    if (lv_scr_act() != ui_splashScreen) return;  // user already swiped away manually - don't yank them back
    _ui_screen_change(&ui_speedScreen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, &ui_speedScreen_screen_init);
    _ui_screen_delete(&ui_splashScreen);
}

static void splashExitPollCb(lv_timer_t * timer) {
    LV_UNUSED(timer);
    if (!splashWipeDone) return;  // let the wipe finish before either exit condition can fire
    bool haveData = ui_can_freshness_hasEverReceived();
    bool timedOut = (lv_tick_elaps(splashExitStartTick) >= SPLASH_EXIT_TIMEOUT_MS);
    if (haveData || timedOut) splashDoExit();
}

static void splashTapCb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    splashDoExit();
}

void ui_event_splashScreen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_get_act());
        if (splashExitTimer) {
            lv_timer_del(splashExitTimer);
            splashExitTimer = NULL;
        }
        _ui_screen_change(&ui_settingsScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_settingsScreen_screen_init);
        _ui_screen_delete(&ui_splashScreen);
    }
}

void ui_splashScreen_screen_init(void)
{
    ui_splashScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_splashScreen, LV_OBJ_FLAG_SCROLLABLE);
    // Hardcoded black, not ui_theme_bg() - see this file's header comment.
    lv_obj_set_style_bg_color(ui_splashScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_splashScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_splashLogo = lv_img_create(ui_splashScreen);
    lv_img_set_src(ui_splashLogo, &img_scout_logo);
    lv_obj_align(ui_splashLogo, LV_ALIGN_TOP_MID, 0, 70);
    // lv_img_set_src() only MARKS the object's size dirty (lv_obj_mark_
    // layout_as_dirty) - it doesn't recompute it synchronously, so reading
    // lv_obj_get_width()/get_x() right after align() would see stale (0)
    // geometry without this forced layout pass. Found on real hardware:
    // the wipe rect was silently created at 0x0, so there was nothing to
    // reveal - the logo just appeared instantly with no visible wipe at
    // all, not a subtly-wrong one.
    lv_obj_update_layout(ui_splashLogo);

    // Wipe rect: a sibling of the logo (not its child, see header comment),
    // sized/positioned to exactly cover it, captured right after alignment
    // so this doesn't need to know the logo's fixed pixel geometry itself.
    splashWipeBaseX = lv_obj_get_x(ui_splashLogo);
    splashWipeBaseW = lv_obj_get_width(ui_splashLogo);
    lv_coord_t logoY = lv_obj_get_y(ui_splashLogo);
    lv_coord_t logoH = lv_obj_get_height(ui_splashLogo);

    ui_splashWipeRect = lv_obj_create(ui_splashScreen);
    lv_obj_clear_flag(ui_splashWipeRect, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ui_splashWipeRect, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_splashWipeRect, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_splashWipeRect, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_splashWipeRect, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(ui_splashWipeRect, splashWipeBaseW, logoH);
    lv_obj_set_pos(ui_splashWipeRect, splashWipeBaseX, logoY);

    ui_splashClockLabel = lv_label_create(ui_splashScreen);
    lv_obj_align(ui_splashClockLabel, LV_ALIGN_TOP_MID, 0, 250);
    lv_label_set_text(ui_splashClockLabel, "--:--");
    lv_obj_set_style_text_color(ui_splashClockLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_splashClockLabel, &font_montserrat_extrabold_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_splashClockCaptionLabel = lv_label_create(ui_splashScreen);
    lv_obj_align(ui_splashClockCaptionLabel, LV_ALIGN_TOP_MID, 0, 322);
    lv_label_set_text(ui_splashClockCaptionLabel, "UTC - waiting for GPS fix");
    lv_obj_set_style_text_color(ui_splashClockCaptionLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_splashClockCaptionLabel, &font_montserrat_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_splashScreen, ui_event_splashScreen, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_splashScreen, splashTapCb, LV_EVENT_CLICKED, NULL);

    // Boot sequence: wipe + backlight ramp start together, exit gate waits
    // for the wipe (splashWipeReadyCb) then polls CAN-freshness/timeout.
    splashWipeDone = false;
    splashExiting = false;
    splashExitStartTick = lv_tick_get();

    lv_anim_t wipeAnim;
    lv_anim_init(&wipeAnim);
    lv_anim_set_var(&wipeAnim, ui_splashWipeRect);
    lv_anim_set_exec_cb(&wipeAnim, splashWipeAnimExec);
    lv_anim_set_values(&wipeAnim, splashWipeBaseW, 0);
    lv_anim_set_time(&wipeAnim, SPLASH_WIPE_MS);
    lv_anim_set_path_cb(&wipeAnim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&wipeAnim, splashWipeReadyCb);
    lv_anim_start(&wipeAnim);

    backlight_rampTo(SPLASH_BACKLIGHT_TARGET, SPLASH_WIPE_MS);

    splashExitTimer = lv_timer_create(splashExitPollCb, SPLASH_EXIT_POLL_MS, NULL);
}

void ui_splashScreen_refresh_theme(void)
{
    if (ui_splashScreen == NULL) return;

    // Background is deliberately NOT re-themed here - see this file's
    // header comment for why it stays hardcoded black in both day and
    // night mode.
    if (ui_splashClockLabel) lv_obj_set_style_text_color(ui_splashClockLabel, ui_theme_text_primary(), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_splashClockCaptionLabel) lv_obj_set_style_text_color(ui_splashClockCaptionLabel, ui_theme_text_secondary(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_splashScreen_screen_destroy(void)
{
    if (ui_splashScreen) lv_obj_del(ui_splashScreen);

    ui_splashScreen = NULL;
    ui_splashLogo = NULL;
    ui_splashWipeRect = NULL;
    ui_splashClockLabel = NULL;
    ui_splashClockCaptionLabel = NULL;
    if (splashExitTimer) {
        lv_timer_del(splashExitTimer);
        splashExitTimer = NULL;
    }
    splashWipeDone = false;
    splashExiting = false;
}
