#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "led-graphics.h"
#include "sig-save.h"
#include "ble.h"
#include "ble_xfer.h"

#define TSC2007_ADDR 0x48
#define CMD_Z1 0xE4
#define CMD_Z2 0xF4
#define CMD_X 0xC4
#define CMD_Y 0xD4
#define CMD_POWERDOWN 0x00

enum {
  pix_pin = 21,
  npixels = 64,
  poll_ms = 6,
  tree_show_ms = 8000,
  scroll_step_ms = 75,
  go_trees_split = 3,
  go_trees_blink_char = 8,
  sig_grace_ms = 5000,
  sig_countdown_secs = 5,
  sig_sent_show_ms = 2000,
  ble_blink_ms = 500,
  ble_name_corner_w = 14,
  ble_name_corner_h = 7,
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

enum {
  phase_tree = 0,
  phase_transition = 1,
  phase_go_trees = 2,
  phase_cs240lx = 3,
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

static void matrix_begin_wipe(led_gfx_t *gfx, int *cycle_phase_out,
                              int *pending_phase_out, int next_phase) {
  led_gfx_start_wipe(gfx);
  *pending_phase_out = next_phase;
  *cycle_phase_out = phase_transition;
}

static void matrix_begin_red_dot_sweep(led_gfx_t *gfx, int *cycle_phase_out,
                                       int *pending_phase_out, int next_phase) {
  led_gfx_start_red_dot_sweep(gfx);
  *pending_phase_out = next_phase;
  *cycle_phase_out = phase_transition;
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
  ssd1306_display_draw_string_centered(20, "waiting for your", COLOR_WHITE);
  ssd1306_display_draw_string_centered(36, "signature...", COLOR_WHITE);
  oled_flush();
}

static void oled_show_countdown(unsigned sec_left) {
  ssd1306_display_clear();
  if(sec_left >= 5)
    ssd1306_display_draw_string_centered(28, "Sending in 5", COLOR_WHITE);
  else if(sec_left == 4)
    ssd1306_display_draw_string_centered(28, "4...", COLOR_WHITE);
  else if(sec_left == 3)
    ssd1306_display_draw_string_centered(28, "3...", COLOR_WHITE);
  else if(sec_left == 2)
    ssd1306_display_draw_string_centered(28, "2...", COLOR_WHITE);
  else
    ssd1306_display_draw_string_centered(28, "1...", COLOR_WHITE);
  oled_flush();
}

static void oled_show_sent(void) {
  ssd1306_display_clear();
  ssd1306_display_draw_string_centered(28, "Sent!", COLOR_WHITE);
  oled_flush();
}

static uint8_t oled_drawing_snapshot[SSD1306_DISPLAY_BUFFER_SIZE];
static int oled_drawing_saved = 0;

static void oled_drawing_snapshot_save(void) {
  ssd1306_display_snapshot_save(oled_drawing_snapshot);
  oled_drawing_saved = 1;
}

static void oled_drawing_snapshot_restore(void) {
  if(!oled_drawing_saved)
    return;
  ssd1306_display_snapshot_restore(oled_drawing_snapshot);
  oled_flush();
}

static void oled_drawing_snapshot_clear(void) {
  oled_drawing_saved = 0;
}

void notmain(void) {
  output("cap board boot\n");

  caches_enable();

  sig_init();

  i2c_init();
  ssd1306_display_init();
  oled_show_waiting();

  gpio_set_output(pix_pin);

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

  led_gfx_t gfx = led_gfx_init(pix_pin, npixels);

  matrix_show_tree(&gfx);
  led_gfx_show(&gfx);
  output("matrix cycle: tree -> Go Trees! -> CS240LX\n");

  int cycle_phase = phase_tree;
  int pending_phase = phase_tree;
  uint32_t phase_start_usec = timer_get_usec();

  int has_strokes = 0;
  int countdown_sec = -1;
  int showing_sent = 0;
  uint32_t last_touch_usec = 0;
  uint32_t sent_until_usec = 0;
  unsigned point_count = 0;
  int ble_was_connected = 0;
  int ble_oled_dirty = 1;
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
      oled_drawing_snapshot_clear();
      oled_show_waiting();
      countdown_sec = -1;
    }

    if(cycle_phase == phase_transition) {
      if(led_gfx_tick_transition(&gfx)) {
        cycle_phase = pending_phase;
        if(cycle_phase == phase_tree)
          phase_start_usec = timer_get_usec();
      }
    } else if(cycle_phase == phase_tree) {
      led_gfx_tick_tree_sparkle(&gfx);
      uint32_t elapsed = timer_get_usec() - phase_start_usec;
      if(elapsed >= (uint32_t)tree_show_ms * 1000) {
        matrix_show_go_trees(&gfx);
        matrix_begin_red_dot_sweep(&gfx, &cycle_phase, &pending_phase,
                                   phase_go_trees);
        output("matrix scroll: Go Trees!\n");
      }
    } else if(cycle_phase == phase_go_trees) {
      led_gfx_tick_scroll_anim(&gfx);
      if(led_gfx_tick_scroll_pass(&gfx)) {
        matrix_show_cs240lx(&gfx);
        matrix_begin_wipe(&gfx, &cycle_phase, &pending_phase, phase_cs240lx);
        output("matrix scroll: CS240LX\n");
      }
    } else {
      if(led_gfx_tick_scroll_pass(&gfx)) {
        matrix_show_tree(&gfx);
        matrix_begin_wipe(&gfx, &cycle_phase, &pending_phase, phase_tree);
        output("matrix: tree\n");
      }
    }

    uint16_t touch_x = 0;
    uint16_t touch_y = 0;
    if(!showing_sent && tsc2007_read_touch(&touch_x, &touch_y)) {
      int was_countdown = countdown_sec >= 0;
      countdown_sec = -1;

      if(was_countdown) {
        oled_drawing_snapshot_restore();
      } else if(!has_strokes) {
        oled_drawing_snapshot_clear();
        ssd1306_display_clear();
        oled_flush();
      }

      uint32_t pixel_x = 0;
      uint32_t pixel_y = 0;
      map_touch_to_pixel(touch_x, touch_y, SSD1306_DISPLAY_WIDTH,
                         SSD1306_DISPLAY_HEIGHT, &pixel_x, &pixel_y);
      oled_draw_touch_point(pixel_x, pixel_y);
      sig_draw_point(touch_x, touch_y);
      last_touch_usec = timer_get_usec();
      has_strokes = 1;
      point_count++;
      if (point_count % 5 == 0) {
        oled_flush();
      }
      output("touch x=%u y=%u -> oled %u,%u\n", touch_x, touch_y, pixel_x,
             pixel_y);
    } else if(has_strokes && !showing_sent) {
      uint32_t idle_usec = timer_get_usec() - last_touch_usec;
      uint32_t grace_usec = (uint32_t)sig_grace_ms * 1000;
      uint32_t countdown_usec = (uint32_t)sig_countdown_secs * 1000000;
      uint32_t save_after_usec = grace_usec + countdown_usec;

      if(idle_usec >= save_after_usec) {
        output("sig: saving...\n");
        sig_save();
        ble_register_saved_zip();
        sig_clear();
        oled_show_sent();
        showing_sent = 1;
        sent_until_usec = timer_get_usec()
                          + (uint32_t)sig_sent_show_ms * 1000;
        has_strokes = 0;
        countdown_sec = -1;
        point_count = 0;
        oled_drawing_snapshot_clear();
        output("sig: saved, ready for next\n");
      } else if(idle_usec >= grace_usec) {
        uint32_t countdown_elapsed = idle_usec - grace_usec;
        unsigned sec_left = sig_countdown_secs
                            - countdown_elapsed / 1000000;
        if(sec_left < 1)
          sec_left = 1;
        if((int)sec_left != countdown_sec) {
          if(countdown_sec < 0)
            oled_drawing_snapshot_save();
          countdown_sec = (int)sec_left;
          oled_show_countdown(sec_left);
        }
      } else if(countdown_sec >= 0) {
        countdown_sec = -1;
      }
    }
    delay_ms(poll_ms);
  }
}
