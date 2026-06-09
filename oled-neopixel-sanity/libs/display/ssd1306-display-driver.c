#include "rpi.h"
#include "ssd1306-display-driver.h"
#include "i2c.h"

static uint8_t i2c_buffer[SSD1306_I2C_BUFFER_SIZE];
static uint8_t *display_buffer = i2c_buffer + 1;

// 1. Done! Signal 
// 2. fill in between lines 
// 3. 
// Helper function to send a byte over I2C
void ssd1306_display_send_command(uint8_t cmd) {
  uint8_t cmd_buf[2] = {0x00, cmd};
  i2c_write(SSD1306_DISPLAY_ADDRESS, cmd_buf, 2);
}

// Initialize the display.
// Requirement: I2C should have been initialized beforehand.
void ssd1306_display_init(void) {
  /* Display initialization flow [SSD1306 datasheet pg 64] */

  // 0. Turn the display off to be safe [SSD1306 pg 28]
  ssd1306_display_send_command(0xAE);
  // 1. Set multiplex ratio
  ssd1306_display_send_command(0xA8);  // want to set multiplex ratio
  ssd1306_display_send_command(0x3F);  // having all 64 rows of the display active

  // 2. Set display offset [SSD1306 pg 37]
  ssd1306_display_send_command(0xD3); // want to set display offset
  ssd1306_display_send_command(0x00);

  // 3. Set display start line [SSD1306 pg 36]
  ssd1306_display_send_command(0x40); // offset = 0

  // 4. Set segment re-map [SSD1306 pg 36]
  ssd1306_display_send_command(0xA1); // column address 0 mapped to SEG127 (horizontal flip)

  // 5. Set COM output scan direction
  ssd1306_display_send_command(0xC8); // scan from COM[N-1] to COM0 (vertical flip)

  // 6. Set COM pins hardware configuration [SSD1306 pg 40]
  ssd1306_display_send_command(0xDA); // 
  ssd1306_display_send_command(0x12); // to set the configuration.

  // 7. Set contrast control [SSD1306 pg 36]
  ssd1306_display_send_command(0x81); // 
  ssd1306_display_send_command(0xFF); // contrast step

  // 8. Display output according to GDDRAM contents [SSD1306 pg 37]
  ssd1306_display_send_command(0xA4);

  // 9. Set normal or inverse display [SSD1306 pg 37]
  ssd1306_display_send_command(0xA6);

  // 10. Set display clock divide ratio/oscillator frequency [SSD1306 pg 40]
  ssd1306_display_send_command(0xD5);
  ssd1306_display_send_command(0x80); /// not sure about this 

  // 11. Enable charge pump regulator [SSD1306 pg 62]
  ssd1306_display_send_command(0x8D);
  ssd1306_display_send_command(0x14);

  // 12. Specify HORIZONTAL addressing mode [SSD1306 pg 35]
  ssd1306_display_send_command(0x20); // set addr mode
  ssd1306_display_send_command(0x00); // horizontal addressing mode.
  ssd1306_display_send_command(0x21); // set col addresses
  ssd1306_display_send_command(0x00); // addr of col[start]
  ssd1306_display_send_command(0x7f); // addr of col[end]
  ssd1306_display_send_command(0x22); // set page addr 
  ssd1306_display_send_command(0xB0); // addr of page[start]
  ssd1306_display_send_command(0xB7); // addr of page[end]

  // trace("here\n");

  // 13. Display on [SSD1306 pg 62]
  ssd1306_display_send_command(0xAF);
  // 14. Clear the screen to black and call display_show()
  ssd1306_display_clear();
  ssd1306_display_show();

}

// Send display buffer to screen via I2C
// Must be called to actually update the display!
void ssd1306_display_show(void) {
  i2c_buffer[0] = 0x40; // control byte to indicate data
  i2c_write(SSD1306_DISPLAY_ADDRESS, i2c_buffer, sizeof(i2c_buffer));
}

void ssd1306_display_snapshot_save(uint8_t buf[SSD1306_DISPLAY_BUFFER_SIZE]) {
  memcpy(buf, display_buffer, SSD1306_DISPLAY_BUFFER_SIZE);
}

void ssd1306_display_snapshot_restore(
    const uint8_t buf[SSD1306_DISPLAY_BUFFER_SIZE]) {
  memcpy(display_buffer, buf, SSD1306_DISPLAY_BUFFER_SIZE);
}

// Clears the screen to black; no change until display_show() is called
void ssd1306_display_clear(void) {
  i2c_buffer[0] = 0x40; // control byte to indicate data
  memset(display_buffer, 0, SSD1306_DISPLAY_BUFFER_SIZE);
}

// Fills the display completely with white
void ssd1306_display_fill_white(void) {
  i2c_buffer[0] = 0x40; // control byte to indicate data
  memset(display_buffer, 0xFF, SSD1306_DISPLAY_BUFFER_SIZE);
}

void ssd1306_display_draw_pixel(uint16_t x, uint16_t y, color_t color) {
  // 0xA1+0xC8 in init rotate the hardware 180 degrees, so compensate here
  // so that draw_pixel(x,y) reliably lights up visual position (x,y).
  x = (SSD1306_DISPLAY_WIDTH  - 1) - x;
  y = (SSD1306_DISPLAY_HEIGHT - 1) - y;

  switch (color) {
  case COLOR_WHITE:
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] |= (1 << (y & 7));
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x + 1] |= (1 << (y & 7));
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] |= (1 << (y & 7));
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] |= (1 << (y & 7));
    break;
  case COLOR_BLACK:
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] &= ~(1 << (y & 7));
    break;
  case COLOR_INVERT:
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] ^= (1 << (y & 7));
    break;
  }
}

void ssd1306_display_draw_horizontal_line(int16_t x_start, int16_t x_end,
                                          int16_t y, color_t color) {

  // https://github.com/adafruit/Adafruit_SSD1306/blob/master/Adafruit_SSD1306.cpp#L706

  if (y < 0 || y >= SSD1306_DISPLAY_HEIGHT) {
    return;
  }
  for (int i = x_start; i <= x_end; i++) {
    ssd1306_display_draw_pixel(i, y, color);
  }
}

void ssd1306_display_draw_vertical_line(int16_t y_start, int16_t y_end,
                                        int16_t x, color_t color) {

  // https://github.com/adafruit/Adafruit_SSD1306/blob/master/Adafruit_SSD1306.cpp#L806

  if (x < 0 || x >= SSD1306_DISPLAY_WIDTH) {
    return;
  }
  for (int i = y_start; i <= y_end; i++) {
    ssd1306_display_draw_pixel(x, i, color);
  }
}

void ssd1306_display_draw_fill_rect(int16_t x, int16_t y, uint16_t w,
                                    uint16_t h, color_t color) {

  // https://github.com/adafruit/Adafruit-GFX-Library/blob/master/Adafruit_GFX.cpp#L300
  for (int16_t i = x; i < x + w; i++) {
    ssd1306_display_draw_vertical_line(y, y + h, i, color);
  }
}

void ssd1306_display_draw_character_size(uint16_t x, uint16_t y,
                                         unsigned char c, color_t color,
                                         uint8_t size_x, uint8_t size_y) {

  // https://github.com/adafruit/Adafruit-GFX-Library/blob/master/Adafruit_GFX.cpp#L1249

  if ((x >= SSD1306_DISPLAY_WIDTH) ||  // Clip right
      (y >= SSD1306_DISPLAY_HEIGHT) || // Clip bottom
      ((x + 6 * size_x - 1) < 0) ||    // Clip left
      ((y + 8 * size_y - 1) < 0)) {    // Clip top
    return;
  }

  for (int8_t i = 0; i < 5; i++) { // Char bitmap = 5 columns
      uint8_t line = standard_ascii_font[c * 5 + i];
      for (int8_t j = 0; j < 7; j++, line >>= 1) {
        if (line & 1) {
          if (size_x == 1 && size_y == 1)
            ssd1306_display_draw_pixel(x + i, y + j, color);
          else
            ssd1306_display_draw_fill_rect(x + i * size_x, y + j * size_y, size_x, size_y,
                          color);
        } 
        // else if (bg != color) {
        //   if (size_x == 1 && size_y == 1)
        //     ssd1306_display_draw_pixel(x + i, y + j, bg);
        //   else
        //     ssd1306_display_draw_fill_rect(x + i * size_x, y + j * size_y, size_x, size_y, bg);
        // }
      }
    }
    // if (bg != color) { // If opaque, draw vertical line for last column
    //   if (size_x == 1 && size_y == 1)
    //     ssd1306_display_draw_vertical_line(x + 5, y, 8, bg);
    //   else
    //     ssd1306_display_draw_fill_rect(x + 5 * size_x, y, size_x, 8 * size_y, bg);
    // }

}

static unsigned display_string_len(const char *msg) {
  unsigned len = 0;
  while(msg[len])
    len++;
  return len;
}

void ssd1306_display_draw_string(uint16_t x, uint16_t y, const char *msg,
                                 color_t color) {
  for(unsigned c = 0; msg[c]; c++, x += 6) {
    for(int8_t col = 0; col < 5; col++) {
      uint8_t line = standard_ascii_font[(unsigned char)msg[c] * 5 + col];
      for(int8_t row = 0; row < 7; row++, line >>= 1) {
        if(line & 1) {
          uint16_t rx = (SSD1306_DISPLAY_WIDTH - 1) - (x + col);
          uint16_t ry = (SSD1306_DISPLAY_HEIGHT - 1) - (y + row);
          ssd1306_display_draw_pixel(rx, ry, color);
        }
      }
    }
  }
}

void ssd1306_display_draw_string_centered(uint16_t y, const char *msg,
                                          color_t color) {
  unsigned len = display_string_len(msg);
  uint16_t x = 0;
  if(len * 6 < SSD1306_DISPLAY_WIDTH)
    x = (uint16_t)((SSD1306_DISPLAY_WIDTH - len * 6) / 2);
  ssd1306_display_draw_string(x, y, msg, color);
}

void ssd1306_display_show_message_lines(const char *line1, const char *line2,
                                        color_t color) {
  ssd1306_display_clear();
  if(line1)
    ssd1306_display_draw_string_centered(20, line1, color);
  if(line2)
    ssd1306_display_draw_string_centered(36, line2, color);
  ssd1306_display_show();
}
