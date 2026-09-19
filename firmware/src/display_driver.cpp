#include "display_driver.h"


// Dislay driver config

LCD_Panel::LCD_Panel(void) {
  {
    auto cfg = _bus_instance.config();
    cfg.panel = &_panel_instance;

    // Configure sync and clock pins.
    cfg.pin_henable = LCD_PIN_HENABLE;  //GPIO_NUM_40;
    cfg.pin_vsync = LCD_PIN_VSYNC;      //GPIO_NUM_41;
    cfg.pin_hsync = LCD_PIN_HSYNC;      //GPIO_NUM_39;
    cfg.pin_pclk = LCD_PIN_PCLK;        //GPIO_NUM_42;
    cfg.freq_write = LCD_FREQ;          //16000000;  // 14000000 (try 15, 14 or 16 Mhz for different esp32 chip clone)

    // Configure data pins.
    cfg.pin_d0 = LCD_PIN_DATA_B0;  //GPIO_NUM_8;   // B0
    cfg.pin_d1 = LCD_PIN_DATA_B1;  //GPIO_NUM_3;   // B1
    cfg.pin_d2 = LCD_PIN_DATA_B2;  //GPIO_NUM_46;  // B2
    cfg.pin_d3 = LCD_PIN_DATA_B3;  //GPIO_NUM_9;   // B3
    cfg.pin_d4 = LCD_PIN_DATA_B4;  //GPIO_NUM_1;   // B4

    cfg.pin_d5 = LCD_PIN_DATA_G0;   //GPIO_NUM_5;   // G0
    cfg.pin_d6 = LCD_PIN_DATA_G1;   //GPIO_NUM_6;   // G1
    cfg.pin_d7 = LCD_PIN_DATA_G2;   //GPIO_NUM_7;   // G2
    cfg.pin_d8 = LCD_PIN_DATA_G3;   //GPIO_NUM_15;  // G3
    cfg.pin_d9 = LCD_PIN_DATA_G4;   //GPIO_NUM_16;  // G4
    cfg.pin_d10 = LCD_PIN_DATA_G5;  //GPIO_NUM_4;  // G5

    cfg.pin_d11 = LCD_PIN_DATA_R0;  //GPIO_NUM_45;  // R0
    cfg.pin_d12 = LCD_PIN_DATA_R1;  //GPIO_NUM_48;  // R1
    cfg.pin_d13 = LCD_PIN_DATA_R2;  //GPIO_NUM_47;  // R2
    cfg.pin_d14 = LCD_PIN_DATA_R3;  //GPIO_NUM_21;  // R3
    cfg.pin_d15 = LCD_PIN_DATA_R4;  //GPIO_NUM_14;  // R4

    // Configure timing parameters for horizontal and vertical sync.
    cfg.hsync_polarity = LCD_HSYNC_POLARITY;        //0;
    cfg.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;  //8;
    cfg.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;  //4;
    cfg.hsync_back_porch = LCD_HSYNC_BACK_PORCH;    //8;

    cfg.vsync_polarity = LCD_VSYNC_POLARITY;        //0;
    cfg.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;  //8;
    cfg.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;  //4;
    cfg.vsync_back_porch = LCD_VSYNC_BACK_PORCH;    //8;

    // Configure polarity for clock and data transmission.
    cfg.pclk_active_neg = LCD_PCLK_ACTIVE_NEG;  //1;
    cfg.de_idle_high = LCD_DE_IDLE_HIGH;        //1;  // 0
    cfg.pclk_idle_high = LCD_PCLK_IDLE_HIGH;    //1;  // 0

    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);
  }
  {
    auto cfg = _panel_instance.config();
    cfg.memory_width = LCD_WIDTH;   //800;
    cfg.memory_height = LCD_HEIGHT;  //480;
    cfg.panel_width = LCD_WIDTH;    //800;
    cfg.panel_height = LCD_HEIGHT;   //480;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    _panel_instance.config(cfg);
  }
  {
    auto cfg = _touch_instance.config();
    cfg.x_min = 0;
    cfg.x_max = TOUCH_XMAX;  //799;
    cfg.y_min = 0;
    cfg.y_max = TOUCH_YMAX;       //479;
    cfg.pin_int = TOUCH_PIN_INT;  //GPIO_NUM_NC;
    cfg.pin_rst = TOUCH_PIN_RST;  //GPIO_NUM_38;
    cfg.bus_shared = true;
    cfg.offset_rotation = TOUCH_ROTATION;  //0;
    cfg.i2c_port = 0;
    cfg.pin_sda = TOUCH_PIN_SDA;  //GPIO_NUM_19;
    cfg.pin_scl = TOUCH_PIN_SCL;  //GPIO_NUM_20;
    cfg.freq = TOUCH_FREQ;        //400000;

    _touch_instance.config(cfg);
    _panel_instance.setTouch(&_touch_instance);
  }

  setPanel(&_panel_instance);
}

LCD_Panel display;
int brightnessVal = 205;

#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
// CH422G I/O expander driven directly through LovyanGFX's (legacy-driver) I2C
// primitives, bypassing ESP32_IO_Expander's C++ wrapper. That wrapper claims
// ESP-IDF's new i2c_master ("driver_ng") API, which the kernel refuses to run
// alongside the legacy driver/i2c.h API that LovyanGFX's Touch_GT911 already
// uses on this same bus (GPIO8 SDA / GPIO9 SCL) -> boot abort:
//   "CONFLICT! driver_ng is not allowed to be used with this old driver"
// The CH422G exposes each function as its own I2C address, not addr+register.
// Full bit map confirmed against Waveshare's own official example
// (github.com/waveshareteam/ESP32-S3-Touch-LCD-7, examples/Arduino/
// examples/03_SD_Test/waveshare_sd_card.h) rather than guessed: b1=TP_RST,
// b2=LCD_BL, b3=LCD_RST, b4=SD_CS, b5=USB_SEL (HIGH routes GPIO19/20 to
// CAN_TX/CAN_RX per that same demo's comment - already correct at 0xFF,
// which is why CAN has worked on this board all along without this bit
// ever being touched).
#define CH422G_ADDR_WR_SET  0x24  // mode/config: bit0 = IO_OE (all 12 pins output)
#define CH422G_ADDR_WR_IO   0x38  // push-pull outputs 0-7: TP_RST b1, LCD_BL b2, LCD_RST b3, SD_CS b4, USB_SEL b5

static bool ch422gWriteReg(uint8_t addr, uint8_t value) {
  if (!lgfx::i2c::beginTransaction(I2C_NUM_0, addr, 400000U, false).has_value()) return false;
  bool ok = lgfx::i2c::writeBytes(I2C_NUM_0, &value, 1).has_value();
  lgfx::i2c::endTransaction(I2C_NUM_0);
  return ok;
}

// SD_CS (CH422G bit4) - see sd_driver.h for why this is a permanent assert,
// not a per-transaction toggle. The CH422G's IO register is write-only (no
// readback), so this writes the FULL known byte (0xFF, the value
// lcd_panel_start() already set, with only bit4 cleared) rather than a
// read-modify-write - preserves TP_RST/LCD_BL/LCD_RST/USB_SEL exactly as
// lcd_panel_start() left them. Must be called AFTER lcd_panel_start() (that
// function brings up I2C_NUM_0 and sets the expander to output mode).
void ch422g_assert_sd_cs(void) {
  ch422gWriteReg(CH422G_ADDR_WR_IO, 0xEF);  // 0xFF with bit4 (SD_CS) cleared
}
#endif

void lcd_panel_start() {

  ledcAttach(LCD_PIN_BACKLIGHT, 1000, 8);
  ledcWrite(LCD_PIN_BACKLIGHT, brightnessVal);
#if defined(WAVESHARE_S3_LCD7) || defined(WAVESHARE_S3_LCD5)
  pinMode(4, OUTPUT);
  /* Bring up the CH422G expander directly: TP_RST, LCD_RST and LCD_BL all high */
  lgfx::i2c::init(I2C_NUM_0, 8 /*SDA*/, 9 /*SCL*/);
  ch422gWriteReg(CH422G_ADDR_WR_SET, 0x01);  // all 12 pins -> output mode
  ch422gWriteReg(CH422G_ADDR_WR_IO, 0xFF);   // pins 0-7 HIGH
  delay(100);
#endif

  display.init();
  display.fillScreen(TFT_BLACK);
}

// LVGL callbacks
void disp_flush_callback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  display.startWrite();
  display.setAddrWindow(area->x1, area->y1, w, h);
  display.pushPixelsDMA((uint16_t *)px_map, w * h, true);
  display.endWrite();

  lv_disp_flush_ready(disp);
}

void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  int32_t x, y;
  data->state = LV_INDEV_STATE_REL;
  if (display.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  }
}

void setBrightness(int val) {
// #ifdef JC8048W550C
  if (val <= 55) {
    brightnessVal = 55;
  } else {
    if (val >= 255) {
      brightnessVal = 255;
    } else {
      brightnessVal = val;
    }
  }
  ledcWrite(LCD_PIN_BACKLIGHT, brightnessVal);
// #endif
}

// See display_driver.h for the rationale (styling pass item 11). Dummy
// static var, not a real object - this animates a raw PWM duty value, not
// any widget's style property, so there's nothing else for lv_anim_t's
// var/exec_cb pairing to key off; a fixed address just gives lv_anim_del()
// something stable to identify "the backlight ramp" by if one is ever
// re-triggered mid-fade (e.g. splash's exit gate firing while a boot-in
// ramp is still running - the new lv_anim_start() call below cancels any
// existing animation on this same var+exec_cb pair first, LVGL's own
// built-in behavior, see lv_anim_start()'s doc comment).
static uint8_t backlightRampAnimVar;

static void backlightRampAnimExec(void *var, int32_t v) {
  (void)var;
  brightnessVal = v;
  ledcWrite(LCD_PIN_BACKLIGHT, v);
}

void backlight_rampTo(int targetDuty, uint32_t ms) {
  if (targetDuty < 0) targetDuty = 0;
  if (targetDuty > 255) targetDuty = 255;

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &backlightRampAnimVar);
  lv_anim_set_exec_cb(&a, backlightRampAnimExec);
  lv_anim_set_values(&a, brightnessVal, targetDuty);
  lv_anim_set_time(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}