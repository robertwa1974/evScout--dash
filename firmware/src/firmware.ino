
#include "display_driver.h"
#include "zombie_updaters.h"
#include "mutex.h"
#include "imu_driver.h"
#include "gps_driver.h"
#include "sd_driver.h"
#include "lv_fs_sd.h"

// PlatformIO defines UNIT_TEST when building an on-device test (e.g.
// test_nav_tile, which needs sd_driver.cpp/nav_reader.cpp linked in via
// test_build_src=true) - guarded out here so this file's own setup()/loop()
// don't collide with the test's own setup()/loop() at link time.
#ifndef UNIT_TEST

static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf1[LCD_WIDTH * LCD_HEIGHT / 10];
static lv_color_t disp_draw_buf2[LCD_WIDTH * LCD_HEIGHT / 10];
static lv_disp_drv_t disp_drv;

void setup(void) {

#if defined(DEBUG) || defined(CAN_TRACE)
  Serial.begin(115200);
#endif
#ifdef DEBUG
  Serial.println("Setup starting");
#endif

  // gps_init() claims Serial/UART0 at the GPS module's baud rate in normal
  // builds - it deliberately no-ops here under DEBUG/CAN_TRACE (see
  // gps_driver.h) rather than fight the 115200 debug baud set just above,
  // since this board physically can't route UART0 to both the USB debug
  // port and the external GPS header at the same time anyway (DIP switch).
  gps_init();

  mutex_init();
  lcd_panel_start();  // brings up I2C_NUM_0 (GPIO8/9) - imu_init() needs this done first
  imu_init();          // MPU6050 wake + WHO_AM_I check - false is fine, just means not wired yet
  imu_start_sampling();
  bool sdOk = sd_init();  // mount microSD (SPI bus GPIO11/12/13, CS via CH422G) -
                           // false is fine, just means no card inserted yet;
                           // check sd_available() before any future file I/O
#if defined(DEBUG) || defined(CAN_TRACE)
  Serial.println(sdOk ? "SD card mounted" : "SD card not mounted (no card, or mount failed)");
#endif
  ui_theme_init();   // default (day) palette state
  getDisplayMode();  // load saved night/day pref from NVS, apply via ui_theme_set() -
                      // must happen before ui_init() below so the very first
                      // screen is built with the right colors, no boot flash
  lv_init();
  lv_fs_sd_register();  // "S:" drive - see lv_fs_sd.h. LVGL subsystem call, needs lv_init() done first;
                         // sd_init() above doesn't need to have succeeded (the driver's ready_cb checks
                         // sd_available() dynamically on every file open).

  // disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * LCD_WIDTH * LCD_HEIGHT / 10, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  // disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * LCD_WIDTH * LCD_HEIGHT / 10, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, LCD_WIDTH * LCD_HEIGHT / 10);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = disp_flush_callback;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = &touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Crate task get data and updater
  xTaskCreatePinnedToCore(TaskCANReceiver, "TaskCANReceiver", 4 * 1024, NULL, 1, NULL, 0);

  ui_init();

#ifdef DEBUG
  Serial.println("Setup done");
#endif
}

void loop(void) {
  if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
    lv_timer_handler();
    xSemaphoreGive(uiMutex);
  }

  gps_poll();  // cheap, non-blocking Serial drain - no-ops under DEBUG/CAN_TRACE

  // Services any GPS NAV tile load queued by ui_navScreen_addPoint() -
  // MUST run from here (a normal task), never from slowUpdate()'s Ticker
  // callback: a real nav_tile_load() blocks on SD I/O long enough to
  // starve the watchdog if run from that context (confirmed on real
  // hardware 2026-09-15 - see ui_navScreen.h's ui_navScreen_addPoint()
  // comment). Cheap no-op when nothing's pending. uiMutex-guarded like
  // every other LVGL touch (this function rebuilds tile LVGL objects) -
  // safe to hold it across the blocking SD read too, since a normal-
  // priority task waiting on this mutex properly yields (unlike the
  // Ticker/esp_timer context this was moved out of).
  if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
    ui_navScreen_processPendingTileLoad();
    xSemaphoreGive(uiMutex);
  }

  // Services a queued route computation - same reasoning as
  // ui_navScreen_processPendingTileLoad() above: Router::route() blocks
  // on SD I/O (ROUTE.bin header/index/page-cache reads), so it must run
  // from here, not from slowUpdate()'s Ticker callback. See
  // ui_navScreen.h's ui_navScreen_processPendingRoute() comment.
  if (xSemaphoreTake(uiMutex, portMAX_DELAY) == pdTRUE) {
    ui_navScreen_processPendingRoute();
    xSemaphoreGive(uiMutex);
  }

  vTaskDelay(pdMS_TO_TICKS(5));
}

#endif  // UNIT_TEST