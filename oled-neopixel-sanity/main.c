#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "led-graphics.h"
#include "sig-save.h"
#include "ble.h"
#include "ble_xfer.h"
#include "button.h"
#include "pot-mcp3008.h"

#define TSC2007_ADDR 0x48
#define CMD_Z1 0xE4
#define CMD_Z2 0xF4
#define CMD_X 0xC4
#define CMD_Y 0xD4
#define CMD_POWERDOWN 0x00

enum {
  pix_pin = 21,
  done_pin = 17,
  mode_pin = 27,
  npixels = 64,
  poll_ms = 6,
  scroll_step_ms = 75,
  scroll_step_ms_min = 25,
  scroll_step_ms_max = 200,
  go_trees_split = 3,
  go_trees_blink_char = 8,
  sig_sent_show_ms = 2000,
  ble_blink_ms = 500,
  ble_name_corner_w = 14,
  ble_name_corner_h = 7,
  pot_poll_ms = 50,
};

enum {
  led_mode_tree = 0,
  led_mode_go_trees = 1,
  led_mode_cs240lx = 2,
  led_mode_count = 3,
};

static struct ble_conn g_ble_conn;
static int ble_blink_on = 1;
static uint32_t ble_blink_last_usec = 0;

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

static void matrix_show_go_trees(led_gfx_t *gfx) {
  led_gfx_set_scroll_text_split_timed(gfx, "Go Trees!", &go_red,
                                      &trees_forest_green, go_trees_split,
                                      scroll_step_ms);
  led_scroll_set_blink_char(&gfx->scroll, go_trees_blink_char);
}

static void matrix_show_cs240lx(led_gfx_t *gfx) {
  led_gfx_set_scroll_text_timed(gfx, "CS240LX", 0, 0, LED_MAX_BRIGHT,
                                scroll_step_ms);
}

static void matrix_show_tree(led_gfx_t *gfx) {
  led_gfx_set_tree_default(gfx);
}

static void apply_led_mode(led_gfx_t *gfx, int mode, unsigned scroll_ms) {
  switch(mode) {
  case led_mode_tree:
    matrix_show_tree(gfx);
    break;
  case led_mode_go_trees:
    matrix_show_go_trees(gfx);
    led_scroll_set_step_ms(&gfx->scroll, scroll_ms);
    break;
  case led_mode_cs240lx:
    matrix_show_cs240lx(gfx);
    led_scroll_set_step_ms(&gfx->scroll, scroll_ms);
    break;
  default:
    break;
  }
  led_gfx_show(gfx);
}

static unsigned pot_to_scroll_ms(uint16_t raw) {
  unsigned span = scroll_step_ms_max - scroll_step_ms_min;
  unsigned ms = scroll_step_ms_max - (raw * span / 1023);
  if(ms < scroll_step_ms_min)
    ms = scroll_step_ms_min;
  return ms;
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

static void oled_ble_clear_corner(void) {
  for(unsigned row = 0; row < ble_name_corner_h; row++) {
    for(unsigned col = 0; col < ble_name_corner_w; col++)
      ssd1306_display_draw_pixel(col, row, COLOR_BLACK);
  }
}

static void oled_ble_status_overlay(int show_disconnected) {
  oled_ble_clear_corner();
  if(show_disconnected && ble_blink_on)
    ssd1306_display_draw_string(0, 0, "BT", COLOR_WHITE);
}

static void oled_flush(void) {
  oled_ble_status_overlay(!g_ble_conn.connected);
  ssd1306_display_show();
}

static void on_nus_write(u16 handle, const u8 *data, u16 len) {
  if(handle != BLE_NUS_RX_HANDLE)
    return;
  ble_xfer_handle(&g_ble_conn, data, len);
}

static void ble_register_saved_zip(void) {
  const uint8_t *zip = 0;
  uint32_t zip_len = 0;
  sig_last_saved_zip(&zip, &zip_len);
  if(!zip || zip_len == 0)
    return;
  int image_id = ble_xfer_add_image(zip, zip_len);
  if(image_id < 0)
    output("ble: image store full, zip not queued\n");
  else
    output("ble: queued SIG zip id=%d (%d bytes)\n", image_id, zip_len);
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

static void oled_show_waiting(void) {
  ssd1306_display_clear();
  ssd1306_display_draw_string_centered(20, "sign here", COLOR_WHITE);
  ssd1306_display_draw_string_centered(36, "press Done", COLOR_WHITE);
  oled_flush();
}

static void oled_show_sent(void) {
  ssd1306_display_clear();
  ssd1306_display_draw_string_centered(28, "Sent!", COLOR_WHITE);
  oled_flush();
}

static void sig_save_and_send(int *has_strokes_out, unsigned *point_count_out,
                              int *showing_sent_out, uint32_t *sent_until_out) {
  output("sig: saving...\n");
  sig_save();
  ble_register_saved_zip();
  sig_clear();
  oled_show_sent();
  *showing_sent_out = 1;
  *sent_until_out = timer_get_usec() + (uint32_t)sig_sent_show_ms * 1000;
  *has_strokes_out = 0;
  *point_count_out = 0;
  output("sig: saved, ready for next\n");
}

void notmain(void) {
  output("cap board boot\n");

  caches_enable();

  sig_init();

  i2c_init();
  ssd1306_display_init();
  oled_show_waiting();

  gpio_set_output(pix_pin);

  button_t done_button;
  button_t mode_button;
  button_init(&done_button, done_pin);
  button_init(&mode_button, mode_pin);
  pot_init();

  led_gfx_t gfx = led_gfx_init(pix_pin, npixels);
  int led_mode = led_mode_tree;
  unsigned scroll_ms = scroll_step_ms;
  apply_led_mode(&gfx, led_mode, scroll_ms);

  output("ble: init...\n");
  ble_init();
  ble_xfer_init();
  ble_set_write_callback(on_nus_write);
  ble_start_nus_advertising("cs240lx-pi");
  g_ble_conn.connected = false;
  ble_blink_last_usec = timer_get_usec();
  for(int i = 0; i < 32; i++)
    ble_poll(&g_ble_conn);
  output("ble: advertising as cs240lx-pi\n");

  output("led: mode button cycles tree / Go Trees! / CS240LX\n");

  int has_strokes = 0;
  int showing_sent = 0;
  uint32_t sent_until_usec = 0;
  unsigned point_count = 0;
  int ble_was_connected = 0;
  int ble_oled_dirty = 1;
  uint32_t pot_last_usec = 0;

  while (1) {
    ble_poll(&g_ble_conn);
    ble_xfer_poll(&g_ble_conn);

    if(g_ble_conn.connected != ble_was_connected) {
      ble_was_connected = g_ble_conn.connected;
      ble_oled_dirty = 1;
      if(!g_ble_conn.connected) {
        ble_start_nus_advertising("cs240lx-pi");
        output("ble: disconnected, advertising again\n");
      } else {
        output("ble: connected\n");
      }
    }

    if(!g_ble_conn.connected) {
      uint32_t now_usec = timer_get_usec();
      if(now_usec - ble_blink_last_usec >= (uint32_t)ble_blink_ms * 1000) {
        ble_blink_last_usec = now_usec;
        ble_blink_on = !ble_blink_on;
        ble_oled_dirty = 1;
      }
    }

    if(ble_oled_dirty) {
      oled_ble_status_overlay(!g_ble_conn.connected);
      ssd1306_display_show();
      ble_oled_dirty = 0;
    }

    if(showing_sent && timer_get_usec() >= sent_until_usec) {
      showing_sent = 0;
      oled_show_waiting();
    }

    if(button_pressed(&mode_button)) {
      led_mode = (led_mode + 1) % led_mode_count;
      apply_led_mode(&gfx, led_mode, scroll_ms);
      output("led: mode %d\n", led_mode);
    }

    if(button_pressed(&done_button) && has_strokes && !showing_sent) {
      sig_save_and_send(&has_strokes, &point_count, &showing_sent,
                        &sent_until_usec);
    }

    uint32_t now_usec = timer_get_usec();
    if(now_usec - pot_last_usec >= (uint32_t)pot_poll_ms * 1000) {
      pot_last_usec = now_usec;
      unsigned new_scroll_ms = pot_to_scroll_ms(pot_read());
      if(new_scroll_ms != scroll_ms) {
        scroll_ms = new_scroll_ms;
        if(led_mode != led_mode_tree)
          led_scroll_set_step_ms(&gfx.scroll, scroll_ms);
      }
    }

    if(led_mode == led_mode_tree) {
      led_gfx_tick_tree_sparkle(&gfx);
    } else {
      led_gfx_tick_scroll_anim(&gfx);
      led_gfx_tick_scroll_pass(&gfx);
    }

    uint16_t touch_x = 0;
    uint16_t touch_y = 0;
    if(!showing_sent && tsc2007_read_touch(&touch_x, &touch_y)) {
      if(!has_strokes) {
        ssd1306_display_clear();
        oled_flush();
      }

      uint32_t pixel_x = 0;
      uint32_t pixel_y = 0;
      map_touch_to_pixel(touch_x, touch_y, SSD1306_DISPLAY_WIDTH,
                         SSD1306_DISPLAY_HEIGHT, &pixel_x, &pixel_y);
      oled_draw_touch_point(pixel_x, pixel_y);
      sig_draw_point(touch_x, touch_y);
      has_strokes = 1;
      point_count++;
      if (point_count % 5 == 0) {
        oled_flush();
      }
      output("touch x=%u y=%u -> oled %u,%u\n", touch_x, touch_y, pixel_x,
             pixel_y);
    }

    delay_ms(poll_ms);
  }
}
