#include "led-matrix.h"
#include "neopixel.h"

static neo_t neo_strip;

void led_matrix_check_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    // I know that the current will be higher than the port can handle for a mac if we run higher than ~1/2 brightness
    // to be super generous we can use 1/8 of the max brightness
    if(red > LED_MAX_BRIGHT)
        panic("led: red=%u exceeds max brightness %u\n", red, LED_MAX_BRIGHT);
    if(green > LED_MAX_BRIGHT)
        panic("led: green=%u exceeds max brightness %u\n", green, LED_MAX_BRIGHT);
    if(blue > LED_MAX_BRIGHT)
        panic("led: blue=%u exceeds max brightness %u\n", blue, LED_MAX_BRIGHT);
}

led_matrix_t led_matrix_init(uint8_t pin, unsigned npixels) {
    // make sure the number of pixels is correct
    if(npixels != LED_MATRIX_NPIX)
        panic("led: expected %u pixels, got %u\n", LED_MATRIX_NPIX, npixels);

    neo_strip = neopix_init(pin, npixels);

    led_matrix_t matrix;
    memset(&matrix, 0, sizeof matrix);
    matrix.pin = pin;
    matrix.npixels = npixels;
    return matrix;
}

// map the proper x,y to strip
unsigned led_matrix_index(unsigned x, unsigned y) {
    if(x >= LED_MATRIX_WIDTH || y >= LED_MATRIX_HEIGHT)
        panic("led: out of bounds pixel %u,%u\n", x, y);
    unsigned strip_x = x;

    if(y % 2 == 1)
        strip_x = LED_MATRIX_WIDTH - 1 - x;

    return y * LED_MATRIX_WIDTH + strip_x;
}

void led_matrix_clear(led_matrix_t *matrix) {
    memset(matrix->r, 0, LED_MATRIX_NPIX);
    memset(matrix->g, 0, LED_MATRIX_NPIX);
    memset(matrix->b, 0, LED_MATRIX_NPIX);
}

void led_matrix_set_pixel(led_matrix_t *matrix, unsigned x, unsigned y,
                          uint8_t red, uint8_t green, uint8_t blue) {
    led_matrix_check_rgb(red, green, blue);
    unsigned index = led_matrix_index(x, y);
    matrix->r[index] = red;
    matrix->g[index] = green;
    matrix->b[index] = blue;
}

void led_matrix_fill(led_matrix_t *matrix, uint8_t red, uint8_t green,
                     uint8_t blue) {
    led_matrix_check_rgb(red, green, blue);
    for(unsigned index = 0; index < LED_MATRIX_NPIX; index++) {
        matrix->r[index] = red;
        matrix->g[index] = green;
        matrix->b[index] = blue;
    }
}

void led_matrix_show(led_matrix_t *matrix) {
    for(unsigned index = 0; index < LED_MATRIX_NPIX; index++) {
        uint8_t red = matrix->r[index];
        uint8_t green = matrix->g[index];
        uint8_t blue = matrix->b[index];
        if(red > LED_MAX_BRIGHT || green > LED_MAX_BRIGHT || blue > LED_MAX_BRIGHT)
            panic("led: framebuffer exceeds max brightness before show\n");
        neopix_write(&neo_strip, index, red, green, blue);
    }
    neopix_flush(&neo_strip);
}
