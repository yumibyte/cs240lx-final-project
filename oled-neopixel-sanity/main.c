#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "WS2812B.h"
#include "neopixel.h"

#define TSC2007_ADDR 0x48
#define CMD_Z1 0xE4
#define CMD_Z2 0xF4
#define CMD_X 0xC4
#define CMD_Y 0xD4
#define CMD_POWERDOWN 0x00

enum {
  pix_pin = 21,
  npixels = 64,
  poll_ms = 50,
  blue_eighth = 255 / 8,
};

static void map_touch_to_pixel(uint16_t touch_x, uint16_t touch_y, uint32_t width,
                               uint32_t height, uint32_t *pixel_x,
                               uint32_t *pixel_y) {
  *pixel_x = (uint32_t)(4095 - touch_y) * width / 4096;
  *pixel_y = (uint32_t)(4095 - touch_x) * height / 4096;
}

static uint16_t tsc2007_cmd(uint8_t cmd) {
  i2c_write(TSC2007_ADDR, &cmd, 1);
  delay_us(500);
  uint8_t raw[2];
  i2c_read(TSC2007_ADDR, raw, 2);
  return ((uint16_t)raw[0] << 4) | (raw[1] >> 4);
}

static int tsc2007_read_touch(uint16_t *touch_x, uint16_t *touch_y) {
  tsc2007_cmd(CMD_Z1);
  tsc2007_cmd(CMD_Z2);

  uint16_t x1 = tsc2007_cmd(CMD_X);
  uint16_t y1 = tsc2007_cmd(CMD_Y);
  uint16_t x2 = tsc2007_cmd(CMD_X);
  uint16_t y2 = tsc2007_cmd(CMD_Y);

  tsc2007_cmd(CMD_POWERDOWN);

  int32_t delta_x = (int32_t)x1 - (int32_t)x2;
  int32_t delta_y = (int32_t)y1 - (int32_t)y2;
  if (delta_x < 0) {
    delta_x = -delta_x;
  }
  if (delta_y < 0) {
    delta_y = -delta_y;
  }
  if (delta_x > 100 || delta_y > 100) {
    return 0;
  }

  *touch_x = x1;
  *touch_y = y1;
  return (*touch_x != 4095) && (*touch_y != 4095);
}

static void oled_draw_touch_point(uint32_t x, uint32_t y) {
  ssd1306_display_draw_pixel(x, y, COLOR_WHITE);
  if (x > 0) {
    ssd1306_display_draw_pixel(x - 1, y, COLOR_WHITE);
  }
  if (x + 1 < SSD1306_DISPLAY_WIDTH) {
    ssd1306_display_draw_pixel(x + 1, y, COLOR_WHITE);
  }
  if (y > 0) {
    ssd1306_display_draw_pixel(x, y - 1, COLOR_WHITE);
  }
  if (y + 1 < SSD1306_DISPLAY_HEIGHT) {
    ssd1306_display_draw_pixel(x, y + 1, COLOR_WHITE);
  }
}

static void neopixel_set_all(neo_t *strip, uint8_t red, uint8_t green,
                             uint8_t blue) {
  for (unsigned pixel = 0; pixel < strip->npixel; pixel++) {
    neopix_write(strip, pixel, red, green, blue);
  }
  neopix_flush(strip);
}

void notmain(void) {
  output("cap board boot\n");

  i2c_init();
  ssd1306_display_init();
  ssd1306_display_clear();
  ssd1306_display_show();

  caches_enable();
  gpio_set_output(pix_pin);
  neo_t strip = neopix_init(pix_pin, npixels);
  neopixel_set_all(&strip, 0, 0, (uint8_t)blue_eighth);
  output("matrix blue 1/8, draw on oled\n");

  unsigned point_count = 0;
  while (1) {
    uint16_t touch_x = 0;
    uint16_t touch_y = 0;
    if (tsc2007_read_touch(&touch_x, &touch_y)) {
      uint32_t pixel_x = 0;
      uint32_t pixel_y = 0;
      map_touch_to_pixel(touch_x, touch_y, SSD1306_DISPLAY_WIDTH,
                         SSD1306_DISPLAY_HEIGHT, &pixel_x, &pixel_y);
      oled_draw_touch_point(pixel_x, pixel_y);
      point_count++;
      if (point_count % 5 == 0) {
        ssd1306_display_show();
      }
      output("touch x=%u y=%u -> oled %u,%u\n", touch_x, touch_y, pixel_x,
             pixel_y);
    }
    delay_ms(poll_ms);
  }
}
