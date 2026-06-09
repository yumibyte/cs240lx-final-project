#ifndef __LED_MATRIX_H__
#define __LED_MATRIX_H__

#include "rpi.h"

enum {
    LED_MATRIX_WIDTH = 8,
    LED_MATRIX_HEIGHT = 8,
    LED_MATRIX_NPIX = 64,
    // never should be more than this
    LED_MAX_BRIGHT = 255 / 8,
};

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_t;

typedef struct {
    uint8_t pin;
    uint32_t npixels;
    uint8_t r[LED_MATRIX_NPIX];
    uint8_t g[LED_MATRIX_NPIX];
    uint8_t b[LED_MATRIX_NPIX];
} led_matrix_t;

led_matrix_t led_matrix_init(uint8_t pin, unsigned npixels);

void led_matrix_check_rgb(uint8_t red, uint8_t green, uint8_t blue);

void led_matrix_clear(led_matrix_t *matrix);

void led_matrix_set_pixel(led_matrix_t *matrix, unsigned x, unsigned y,
                          uint8_t red, uint8_t green, uint8_t blue);

void led_matrix_fill(led_matrix_t *matrix, uint8_t red, uint8_t green,
                     uint8_t blue);

unsigned led_matrix_index(unsigned x, unsigned y);

void led_matrix_show(led_matrix_t *matrix);

#endif
