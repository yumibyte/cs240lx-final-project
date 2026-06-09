#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "led-graphics.h"

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
  tree_show_ms = 5000,
  scroll_step_ms = 600,
  go_trees_split = 3,
};

static const led_rgb_t go_red = {
    .r = LED_MAX_BRIGHT,
    .g = 0,
    .b = 0,
};

static const led_rgb_t trees_forest_green = {
    .r = 8,
    .g = LED_MAX_BRIGHT,
    .b = 0,
};

enum {
  phase_tree = 0,
  phase_go_trees = 1,
  phase_cs240lx = 2,
};

static void matrix_show_go_trees(led_gfx_t *gfx) {
  led_gfx_set_scroll_text_split_timed(gfx, "Go Trees!", &go_red,
                                      &trees_forest_green, go_trees_split,
                                      scroll_step_ms);
  led_gfx_show(gfx);
}

static void matrix_show_cs240lx(led_gfx_t *gfx) {
  led_gfx_set_scroll_text_timed(gfx, "CS240LX", 0, 0, LED_MAX_BRIGHT,
                                scroll_step_ms);
  led_gfx_show(gfx);
}

static void matrix_show_tree(led_gfx_t *gfx) {
  led_gfx_set_tree_default(gfx);
  led_gfx_show(gfx);
}

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

void notmain(void) {
  output("cap board boot\n");

  i2c_init();
  ssd1306_display_init();
  ssd1306_display_clear();
  ssd1306_display_show();

  caches_enable();
  gpio_set_output(pix_pin);

  led_gfx_t gfx = led_gfx_init(pix_pin, npixels);

  matrix_show_tree(&gfx);
  output("matrix cycle: tree -> Go Trees! -> CS240LX\n");

  int cycle_phase = phase_tree;
  uint32_t phase_start_usec = timer_get_usec();

  unsigned point_count = 0;
  while (1) {
    if(cycle_phase == phase_tree) {
      uint32_t elapsed = timer_get_usec() - phase_start_usec;
      if(elapsed >= (uint32_t)tree_show_ms * 1000) {
        matrix_show_go_trees(&gfx);
        cycle_phase = phase_go_trees;
        output("matrix scroll: Go Trees!\n");
      }
    } else if(cycle_phase == phase_go_trees) {
      if(led_gfx_tick_scroll_pass(&gfx)) {
        matrix_show_cs240lx(&gfx);
        cycle_phase = phase_cs240lx;
        output("matrix scroll: CS240LX\n");
      }
    } else {
      if(led_gfx_tick_scroll_pass(&gfx)) {
        matrix_show_tree(&gfx);
        cycle_phase = phase_tree;
        phase_start_usec = timer_get_usec();
        output("matrix: tree\n");
      }
    }

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
