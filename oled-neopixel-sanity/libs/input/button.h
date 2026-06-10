#pragma once
#include "rpi.h"

typedef struct {
    unsigned pin;
    int stable;
    int last_read;
    uint32_t last_change_usec;
} button_t;

void button_init(button_t *button, unsigned pin);

// returns 1 once per press (active low, 20 ms debounce)
int button_pressed(button_t *button);
