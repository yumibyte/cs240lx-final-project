#include "led-graphics.h"

enum {
    red_dot_sweep_gradient = 8,
    red_dot_sweep_steps = LED_MATRIX_WIDTH + red_dot_sweep_gradient,
    red_dot_sweep_step_ms = 80,
};

static void gfx_render(led_gfx_t *gfx) {
    switch(gfx->mode) {
    case LED_GFX_SCROLL_TEXT:
        led_scroll_blink_tick(&gfx->scroll);
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
}

static void gfx_mask_right_columns(led_gfx_t *gfx, unsigned max_x_inclusive) {
    for(unsigned y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for(unsigned x = max_x_inclusive + 1; x < LED_MATRIX_WIDTH; x++) {
            unsigned index = led_matrix_index(x, y);
            gfx->matrix.r[index] = 0;
            gfx->matrix.g[index] = 0;
            gfx->matrix.b[index] = 0;
        }
    }
}

static void gfx_show_partial(led_gfx_t *gfx, unsigned max_x_inclusive) {
    gfx_render(gfx);
    gfx_mask_right_columns(gfx, max_x_inclusive);
    led_matrix_show(&gfx->matrix);
}

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
    gfx_render(gfx);
    led_matrix_show(&gfx->matrix);
}

void led_gfx_start_wipe(led_gfx_t *gfx) {
    gfx->trans = LED_GFX_TRANS_WIPE;
    gfx->trans_step = 0;
    gfx->trans_last_usec = timer_get_usec();
    gfx_show_partial(gfx, 0);
}

static void gfx_render_red_dot_sweep(led_gfx_t *gfx) {
    unsigned front = gfx->trans_step;

    led_matrix_clear(&gfx->matrix);
    for(unsigned y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for(unsigned x = 0; x < LED_MATRIX_WIDTH; x++) {
            if((x + y) % 2 != 0)
                continue;

            int pixel_pos = (int)(LED_MATRIX_WIDTH - 1 - x);
            int dist = (int)front - pixel_pos;
            if(dist < 0 || dist >= red_dot_sweep_gradient)
                continue;

            unsigned level = (LED_MAX_BRIGHT * (red_dot_sweep_gradient - dist))
                             / red_dot_sweep_gradient;
            if(level == 0)
                continue;

            led_matrix_set_pixel(&gfx->matrix, x, y, (uint8_t)level, 0, 0);
        }
    }
    led_matrix_show(&gfx->matrix);
}

void led_gfx_start_red_dot_sweep(led_gfx_t *gfx) {
    gfx->trans = LED_GFX_TRANS_RED_DOT_SWEEP;
    gfx->trans_step = 0;
    gfx->trans_last_usec = timer_get_usec();
    gfx_render_red_dot_sweep(gfx);
}

static int gfx_tick_wipe(led_gfx_t *gfx) {
    gfx->trans_step++;
    if(gfx->trans_step >= LED_MATRIX_WIDTH) {
        gfx->trans = LED_GFX_TRANS_NONE;
        led_gfx_show(gfx);
        return 1;
    }

    gfx_show_partial(gfx, gfx->trans_step);
    return 0;
}

static int gfx_tick_red_dot_sweep(led_gfx_t *gfx) {
    uint32_t now = timer_get_usec();
    if(now - gfx->trans_last_usec < (uint32_t)red_dot_sweep_step_ms * 1000)
        return 0;

    gfx->trans_last_usec = now;
    gfx->trans_step++;
    if(gfx->trans_step >= red_dot_sweep_steps) {
        gfx->trans = LED_GFX_TRANS_NONE;
        gfx->scroll_last_usec = timer_get_usec();
        led_gfx_show(gfx);
        return 1;
    }

    gfx_render_red_dot_sweep(gfx);
    return 0;
}

int led_gfx_tick_transition(led_gfx_t *gfx) {
    switch(gfx->trans) {
    case LED_GFX_TRANS_WIPE:
        return gfx_tick_wipe(gfx);
    case LED_GFX_TRANS_RED_DOT_SWEEP:
        return gfx_tick_red_dot_sweep(gfx);
    default:
        return 0;
    }
}

void led_gfx_tick_tree_sparkle(led_gfx_t *gfx) {
    if(gfx->mode != LED_GFX_TREE)
        return;

    led_tree_render_sparkle(&gfx->matrix, &gfx->tree, timer_get_usec());
    led_matrix_show(&gfx->matrix);
}

void led_gfx_tick_scroll_anim(led_gfx_t *gfx) {
    if(gfx->mode != LED_GFX_SCROLL_TEXT)
        return;
    if(gfx->scroll.blink_char == LED_SCROLL_NO_BLINK)
        return;

    int old_on = gfx->scroll.blink_on;
    led_scroll_blink_tick(&gfx->scroll);
    if(old_on != gfx->scroll.blink_on)
        led_gfx_show(gfx);
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
    if(!wrapped)
        led_gfx_show(gfx);
    return wrapped;
}

void led_gfx_tick_scroll(led_gfx_t *gfx) {
    (void)led_gfx_tick_scroll_pass(gfx);
}
