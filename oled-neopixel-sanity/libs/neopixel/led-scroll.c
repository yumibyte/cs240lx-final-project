#include "led-scroll.h"

extern const unsigned char standard_ascii_font[];

// y = 0 where that's the bottom of the display
static void glyph_column(unsigned char character, unsigned column,
                         uint8_t *rows_out) {
    // clear the rows
    for(unsigned row = 0; row < LED_MATRIX_HEIGHT; row++)
        rows_out[row] = 0;

    if(column >= LED_SCROLL_GLYPH_WIDTH)
        return;

    uint8_t line = standard_ascii_font[(unsigned)character * LED_SCROLL_GLYPH_WIDTH
                                       + column];

    for(unsigned row = 0; row < LED_SCROLL_GLYPH_HEIGHT; row++) {
        // font corresponds to bit 0 = top , y is 0
        unsigned matrix_row = (LED_SCROLL_GLYPH_HEIGHT - 1) - row;
        rows_out[matrix_row] = (line >> row) & 1;
    }
}

// columns until the message has fully scrolled off the left edge
static unsigned scroll_end_col(const led_scroll_t *scroll) {
    if(scroll->msg_len == 0)
        return LED_MATRIX_WIDTH;
    return scroll->msg_len * LED_SCROLL_COLS_PER_CHAR;
}

// start the scroll
void led_scroll_start(led_scroll_t *scroll, const char *msg, uint8_t red,
                      uint8_t green, uint8_t blue) {
    led_matrix_check_rgb(red, green, blue);
    memset(scroll, 0, sizeof *scroll);

    unsigned length = 0;
    if(msg) {
        while(msg[length] && length + 1 < LED_SCROLL_MSG_MAX) {
            scroll->msg[length] = msg[length];
            length++;
        }
    }
    scroll->msg[length] = 0;
    scroll->msg_len = length;
    scroll->step_ms = LED_SCROLL_STEP_MS_DEFAULT;
    scroll->color = (led_rgb_t){ .r = red, .g = green, .b = blue };
    scroll->color_split = 0;
    scroll->blink_char = LED_SCROLL_NO_BLINK;
    scroll->blink_on = 1;
    scroll->blink_last_usec = timer_get_usec();
}

void led_scroll_start_split(led_scroll_t *scroll, const char *msg,
                            const led_rgb_t *first, const led_rgb_t *second,
                            unsigned split_at_char) {
    led_matrix_check_rgb(first->r, first->g, first->b);
    led_matrix_check_rgb(second->r, second->g, second->b);
    led_scroll_start(scroll, msg, first->r, first->g, first->b);
    scroll->color_alt = *second;
    scroll->color_split = split_at_char;
}

void led_scroll_set_step_ms(led_scroll_t *scroll, unsigned step_ms) {
    scroll->step_ms = step_ms;
}

void led_scroll_set_blink_char(led_scroll_t *scroll, unsigned char_index) {
    scroll->blink_char = char_index;
    scroll->blink_on = 1;
    scroll->blink_last_usec = timer_get_usec();
}

void led_scroll_blink_tick(led_scroll_t *scroll) {
    if(scroll->blink_char == LED_SCROLL_NO_BLINK
            || scroll->blink_char >= scroll->msg_len)
        return;

    uint32_t now = timer_get_usec();
    if(now - scroll->blink_last_usec < (uint32_t)LED_SCROLL_BLINK_MS * 1000)
        return;

    scroll->blink_last_usec = now;
    scroll->blink_on = !scroll->blink_on;
}

void led_scroll_render(led_matrix_t *matrix, led_scroll_t *scroll) {
    led_matrix_clear(matrix);

    for(unsigned x = 0; x < LED_MATRIX_WIDTH; x++) {
        unsigned source_col = scroll->scroll_col + x;
        unsigned char_index = source_col / LED_SCROLL_COLS_PER_CHAR;
        unsigned char character = ' ';
        unsigned column_in_glyph = 0;


        if(scroll->msg_len > 0) {
            // get the character index and column in the glyph
            column_in_glyph = source_col % LED_SCROLL_COLS_PER_CHAR;

            // if the character index is within the message length, get the character
            if(char_index < scroll->msg_len)
                character = (unsigned char)scroll->msg[char_index];
            // if the character index is not within the message length, set 
            // the column in the glyph to the width of the glyph
            else
                column_in_glyph = LED_SCROLL_GLYPH_WIDTH;
        }

        uint8_t rows[LED_MATRIX_HEIGHT];
        glyph_column(character, column_in_glyph, rows);

        const led_rgb_t *color = &scroll->color;
        if(scroll->color_split > 0 && char_index >= scroll->color_split)
            color = &scroll->color_alt;

        if(char_index == scroll->blink_char && !scroll->blink_on)
            continue;

        for(unsigned y = 0; y < LED_MATRIX_HEIGHT; y++) {
            if(rows[y])
                led_matrix_set_pixel(matrix, x, y, color->r, color->g, color->b);
        }
    }
}

int led_scroll_step(led_scroll_t *scroll) {
    unsigned end_col = scroll_end_col(scroll);
    scroll->scroll_col++;
    if(scroll->scroll_col >= end_col) {
        scroll->scroll_col = 0;
        return 1;
    }
    return 0;
}
