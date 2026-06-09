#ifndef __LED_TREE_H__
#define __LED_TREE_H__

#include "led-matrix.h"

typedef struct {
    uint8_t leaf_rows[LED_MATRIX_HEIGHT];
    uint8_t trunk_rows[LED_MATRIX_HEIGHT];
    led_rgb_t leaf;
    led_rgb_t trunk;
} led_tree_t;

void led_tree_set(led_tree_t *tree, const uint8_t rows[LED_MATRIX_HEIGHT],
                  uint8_t red, uint8_t green, uint8_t blue);

void led_tree_use_default(led_tree_t *tree);

void led_tree_render(led_matrix_t *matrix, const led_tree_t *tree);

void led_tree_render_sparkle(led_matrix_t *matrix, const led_tree_t *tree,
                             uint32_t seed);

#endif
