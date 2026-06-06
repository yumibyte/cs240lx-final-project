// cap board sanity: SSD1306 (lab 15) + WS2812B (lab 11) + TSC2007 touch
#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "tsc2007.h"
#include "WS2812B.h"
#include "neopixel.h"

enum {
  pix_pin = 21,
  matrix_width = 8,
  matrix_height = 8,
  npixels = matrix_width * matrix_height,
};

static int oled_ready = 0;

static void oled_draw_line(const char *text, int y) {
  for (int i = 0; text[i] != '\0'; i++) {
    ssd1306_display_draw_character_size((uint16_t)(2 + 6 * i), (uint16_t)y,
                                        (unsigned char)text[i], COLOR_WHITE, 1,
                                        1);
  }
}

static void oled_show(const char *line1, const char *line2) {
  if (!oled_ready) {
    return;
  }
  ssd1306_display_clear();
  oled_draw_line(line1, 10);
  oled_draw_line(line2, 28);
  ssd1306_display_show();
}

static void oled_show_touch(const touch_reading_t *sample, int touch_ready) {
  char line1[12];
  char line2[12];

  if (!touch_ready) {
    oled_show("no TSC2007", "check i2c");
    return;
  }
  if (!sample->touched) {
    oled_show("no touch", "press panel");
    return;
  }

  snprintk(line1, sizeof line1, "x=%u", sample->x);
  snprintk(line2, sizeof line2, "y=%u", sample->y);
  oled_show(line1, line2);
}

static void neopixel_show_one(neo_t *strip, unsigned index, uint8_t red,
                              uint8_t green, uint8_t blue) {
  for (unsigned pixel = 0; pixel < strip->npixel; pixel++) {
    neopix_write(strip, pixel, 0, 0, 0);
  }
  if (index < strip->npixel) {
    neopix_write(strip, index, red, green, blue);
  }
  neopix_flush(strip);
}

static void neopixel_all_off(neo_t *strip) {
  neopixel_show_one(strip, npixels, 0, 0, 0);
}

static void neopixel_matrix_sanity(neo_t *strip) {
  output("matrix: 8x8 one-at-a-time red\n");
  oled_show("8x8 matrix", "red scan");
  for (unsigned pixel = 0; pixel < npixels; pixel++) {
    neopixel_show_one(strip, pixel, 32, 0, 0);
    delay_ms(100);
  }

  output("matrix: 8x8 one-at-a-time blue\n");
  oled_show("8x8 matrix", "blue scan");
  for (unsigned pixel = 0; pixel < npixels; pixel++) {
    neopixel_show_one(strip, pixel, 0, 0, 32);
    delay_ms(100);
  }

  neopixel_all_off(strip);
  output("matrix: done (all off)\n");
}

static void touch_sanity_window(int touch_ready, unsigned duration_ms) {
  unsigned rounds = duration_ms / 50;
  if (rounds == 0) {
    rounds = 1;
  }

  output("touch window: press the panel\n");
  for (unsigned round = 0; round < rounds; round++) {
    touch_reading_t sample = tsc2007_read();
    i2c_bus_reconcile_for_libpi();
    oled_show_touch(&sample, touch_ready);
    if (touch_ready && sample.touched) {
      output("touch x=%u y=%u\n", sample.x, sample.y);
    }
    delay_ms(50);
  }
}

static void sanity_cycle(neo_t *strip, int touch_ready) {
  neopixel_matrix_sanity(strip);
  oled_show("neo ok", "touch test");
  touch_sanity_window(touch_ready, 5000);
}

void notmain(void) {
  output("sanity-check boot\n");
  delay_ms(100);
  i2c_init_clk_div(1500);
  delay_ms(100);

  // First try OLED
  if (i2c_probe_addr(0x3C)) {
    ssd1306_display_init();
    oled_ready = 1;
    output("oled init ok\n");
    oled_show("cap sanity", "oled ok");
  } else {
    output("oled missing: no ack at 0x3c\n");
  }
  delay_ms(400);

  // Then try resistive touch
  int touch_ready = tsc2007_init();
  i2c_bus_reconcile_for_libpi();
  if (touch_ready) {
    oled_show("TSC2007 ok", "press panel");
    output("TSC2007 found on i2c\n");
  } else {
    oled_show("no TSC2007", "0x48-0x4b?");
    output("TSC2007 missing: no ack on i2c (vin/sda/scl?)\n");
  }
  delay_ms(500);

  caches_enable();
  gpio_set_output(pix_pin);
  // Then try NeoPixels
  neo_t strip = neopix_init(pix_pin, npixels);

  output("sanity loop: runs forever\n");
  while (1) {
    output("--- sanity cycle ---\n");
    oled_show("sanity loop", "running...");
    delay_ms(300);
    sanity_cycle(&strip, touch_ready);
    neopixel_all_off(&strip);
    delay_ms(500);
  }
}
