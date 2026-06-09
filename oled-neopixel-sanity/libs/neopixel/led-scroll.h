#ifndef __LED_SCROLL_H__
#define __LED_SCROLL_H__

#include "led-matrix.h"

enum {
    LED_SCROLL_MSG_MAX = 64,
    LED_SCROLL_GLYPH_WIDTH = 5,
    LED_SCROLL_GLYPH_HEIGHT = 7,
    LED_SCROLL_GLYPH_SPACING = 1,
    LED_SCROLL_COLS_PER_CHAR = LED_SCROLL_GLYPH_WIDTH + LED_SCROLL_GLYPH_SPACING,
    LED_SCROLL_STEP_MS_DEFAULT = 75,
    LED_SCROLL_NO_BLINK = (unsigned)-1,
    LED_SCROLL_BLINK_MS = 44,
};

typedef struct {
    char msg[LED_SCROLL_MSG_MAX];
    unsigned msg_len;
    unsigned scroll_col;
    unsigned step_ms;
    led_rgb_t color;
    led_rgb_t color_alt;
    unsigned color_split;
    unsigned blink_char;
    int blink_on;
    uint32_t blink_last_usec;
} led_scroll_t;

void led_scroll_start(led_scroll_t *scroll, const char *msg, uint8_t red,
                      uint8_t green, uint8_t blue);

void led_scroll_start_split(led_scroll_t *scroll, const char *msg,
                            const led_rgb_t *first, const led_rgb_t *second,
                            unsigned split_at_char);

void led_scroll_set_step_ms(led_scroll_t *scroll, unsigned step_ms);

void led_scroll_set_blink_char(led_scroll_t *scroll, unsigned char_index);

void led_scroll_blink_tick(led_scroll_t *scroll);

void led_scroll_render(led_matrix_t *matrix, led_scroll_t *scroll);

// returns 1 when the message completes one full scroll pass
int led_scroll_step(led_scroll_t *scroll);

#endif
