#include "led-tree.h"

#define LED_ROW8(c0, c1, c2, c3, c4, c5, c6, c7)                              \
    ((uint8_t)(((c0) ? 0x80 : 0) | ((c1) ? 0x40 : 0) | ((c2) ? 0x20 : 0) |   \
               ((c3) ? 0x10 : 0) | ((c4) ? 0x08 : 0) | ((c5) ? 0x04 : 0) |   \
               ((c6) ? 0x02 : 0) | ((c7) ? 0x01 : 0)))

static const uint8_t led_tree_leaf_rows[LED_MATRIX_HEIGHT] = {
    0x00,
    0x00,
    LED_ROW8(0, 1, 1, 1, 1, 1, 0, 0),
    LED_ROW8(0, 0, 1, 1, 1, 0, 0, 0),
    LED_ROW8(0, 1, 1, 1, 1, 1, 0, 0),
    LED_ROW8(0, 0, 1, 1, 1, 0, 0, 0),
    LED_ROW8(0, 0, 0, 1, 0, 0, 0, 0),
    0x00,
};

// row 0 is the trunk
static const uint8_t led_tree_trunk_rows[LED_MATRIX_HEIGHT] = {
    LED_ROW8(0, 0, 0, 1, 0, 0, 0, 0),
    LED_ROW8(0, 0, 0, 1, 0, 0, 0, 0),
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static void tree_render_rows(led_matrix_t *matrix, const uint8_t rows[LED_MATRIX_HEIGHT],
                             const led_rgb_t *color) {
    for(unsigned y = 0; y < LED_MATRIX_HEIGHT; y++) {
        uint8_t row_bits = rows[y];

        for(unsigned x = 0; x < LED_MATRIX_WIDTH; x++) {
            unsigned shift = LED_MATRIX_WIDTH - 1 - x;
            if((row_bits >> shift) & 1)
                led_matrix_set_pixel(matrix, x, y, color->r, color->g, color->b);
        }
    }
}

void led_tree_set(led_tree_t *tree, const uint8_t rows[LED_MATRIX_HEIGHT],
                  uint8_t red, uint8_t green, uint8_t blue) {
    led_matrix_check_rgb(red, green, blue);
    memset(tree->trunk_rows, 0, LED_MATRIX_HEIGHT);

    for(unsigned row = 0; row < LED_MATRIX_HEIGHT; row++)
        tree->leaf_rows[row] = rows[row];

    tree->leaf = (led_rgb_t){ .r = red, .g = green, .b = blue };
    tree->trunk = tree->leaf;
}

void led_tree_use_default(led_tree_t *tree) {
    for(unsigned row = 0; row < LED_MATRIX_HEIGHT; row++) {
        tree->leaf_rows[row] = led_tree_leaf_rows[row];
        tree->trunk_rows[row] = led_tree_trunk_rows[row];
    }
    tree->leaf = (led_rgb_t){ .r = 0, .g = LED_MAX_BRIGHT, .b = 0 };
    tree->trunk = (led_rgb_t){ .r = 14, .g = 8, .b = 0 };
}

void led_tree_render(led_matrix_t *matrix, const led_tree_t *tree) {
    led_matrix_clear(matrix);
    tree_render_rows(matrix, tree->leaf_rows, &tree->leaf);
    tree_render_rows(matrix, tree->trunk_rows, &tree->trunk);
}
