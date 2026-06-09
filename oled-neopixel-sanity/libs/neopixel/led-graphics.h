#ifndef __LED_GRAPHICS_H__
#define __LED_GRAPHICS_H__

#include "led-matrix.h"
#include "led-scroll.h"
#include "led-tree.h"

typedef enum {
    LED_GFX_NONE = 0,
    LED_GFX_SCROLL_TEXT,
    LED_GFX_TREE,
} led_gfx_mode_t;

typedef enum {
    LED_GFX_TRANS_NONE = 0,
    LED_GFX_TRANS_WIPE,
    LED_GFX_TRANS_RED_DOT_SWEEP,
} led_gfx_trans_t;

typedef struct {
    led_matrix_t matrix;
    led_gfx_mode_t mode;
    led_scroll_t scroll;
    led_tree_t tree;
    uint32_t scroll_last_usec;
    led_gfx_trans_t trans;
    unsigned trans_step;
    uint32_t trans_last_usec;
} led_gfx_t;

led_gfx_t led_gfx_init(uint8_t pin, unsigned npixels);

void led_gfx_set_scroll_text(led_gfx_t *gfx, const char *msg, uint8_t red,
                             uint8_t green, uint8_t blue);

void led_gfx_set_scroll_text_timed(led_gfx_t *gfx, const char *msg, uint8_t red,
                                   uint8_t green, uint8_t blue,
                                   unsigned step_ms);

void led_gfx_set_scroll_text_split_timed(led_gfx_t *gfx, const char *msg,
                                         const led_rgb_t *first,
                                         const led_rgb_t *second,
                                         unsigned split_at_char,
                                         unsigned step_ms);

void led_gfx_set_tree(led_gfx_t *gfx, const uint8_t rows[LED_MATRIX_HEIGHT],
                      uint8_t red, uint8_t green, uint8_t blue);

void led_gfx_set_tree_default(led_gfx_t *gfx);

void led_gfx_show(led_gfx_t *gfx);

void led_gfx_start_wipe(led_gfx_t *gfx);

void led_gfx_start_red_dot_sweep(led_gfx_t *gfx);

int led_gfx_tick_transition(led_gfx_t *gfx);

void led_gfx_tick_tree_sparkle(led_gfx_t *gfx);

void led_gfx_tick_scroll_anim(led_gfx_t *gfx);

void led_gfx_tick_scroll(led_gfx_t *gfx);

// returns 1 when scroll completes one full pass
int led_gfx_tick_scroll_pass(led_gfx_t *gfx);

#endif
