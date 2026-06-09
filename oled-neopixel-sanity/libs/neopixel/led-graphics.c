#include "led-graphics.h"

led_gfx_t led_gfx_init(uint8_t pin, unsigned npixels) {
    led_gfx_t gfx;

    memset(&gfx, 0, sizeof gfx);
    gfx.matrix = led_matrix_init(pin, npixels);
    gfx.mode = LED_GFX_NONE;
    gfx.scroll_last_usec = timer_get_usec();
    return gfx;
}

void led_gfx_set_scroll_text(led_gfx_t *gfx, const char *msg, uint8_t red,
                             uint8_t green, uint8_t blue) {
    led_gfx_set_scroll_text_timed(gfx, msg, red, green, blue,
                                  LED_SCROLL_STEP_MS_DEFAULT);
}

// scroll text with custom step time
void led_gfx_set_scroll_text_timed(led_gfx_t *gfx, const char *msg, uint8_t red,
                                   uint8_t green, uint8_t blue,
                                   unsigned step_ms) {
    led_scroll_start(&gfx->scroll, msg, red, green, blue);
    led_scroll_set_step_ms(&gfx->scroll, step_ms);
    gfx->mode = LED_GFX_SCROLL_TEXT;
    gfx->scroll_last_usec = timer_get_usec();
}

void led_gfx_set_scroll_text_split_timed(led_gfx_t *gfx, const char *msg,
                                         const led_rgb_t *first,
                                         const led_rgb_t *second,
                                         unsigned split_at_char,
                                         unsigned step_ms) {
    led_scroll_start_split(&gfx->scroll, msg, first, second, split_at_char);
    led_scroll_set_step_ms(&gfx->scroll, step_ms);
    gfx->mode = LED_GFX_SCROLL_TEXT;
    gfx->scroll_last_usec = timer_get_usec();
}

// set tree graphics with custom color
void led_gfx_set_tree(led_gfx_t *gfx, const uint8_t rows[LED_MATRIX_HEIGHT],
                      uint8_t red, uint8_t green, uint8_t blue) {
    led_tree_set(&gfx->tree, rows, red, green, blue);
    gfx->mode = LED_GFX_TREE;
}

void led_gfx_set_tree_default(led_gfx_t *gfx) {
    led_tree_use_default(&gfx->tree);
    gfx->mode = LED_GFX_TREE;
}

// show the current graphics mode
void led_gfx_show(led_gfx_t *gfx) {
    switch(gfx->mode) {
    case LED_GFX_SCROLL_TEXT:
        led_scroll_render(&gfx->matrix, &gfx->scroll);
        break;
    case LED_GFX_TREE:
        led_tree_render(&gfx->matrix, &gfx->tree);
        break;
    case LED_GFX_NONE:
        led_matrix_clear(&gfx->matrix);
        break;
    default:
        panic("led: unknown graphics mode %d\n", gfx->mode);
    }
    led_matrix_show(&gfx->matrix);
}

// scroll text step handler
int led_gfx_tick_scroll_pass(led_gfx_t *gfx) {
    if(gfx->mode != LED_GFX_SCROLL_TEXT)
        return 0;

    uint32_t now = timer_get_usec();
    uint32_t elapsed = now - gfx->scroll_last_usec;
    unsigned step_us = gfx->scroll.step_ms * 1000;

    if(elapsed < step_us)
        return 0;

    gfx->scroll_last_usec = now;
    int wrapped = led_scroll_step(&gfx->scroll);
    led_gfx_show(gfx);
    return wrapped;
}

void led_gfx_tick_scroll(led_gfx_t *gfx) {
    (void)led_gfx_tick_scroll_pass(gfx);
}
