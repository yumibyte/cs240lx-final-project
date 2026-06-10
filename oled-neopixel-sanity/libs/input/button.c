#include "button.h"

enum {
    debounce_us = 20000,
};

void button_init(button_t *button, unsigned pin) {
    button->pin = pin;
    gpio_set_input(pin);
    gpio_set_pullup(pin);
    button->stable = gpio_read(pin);
    button->last_read = button->stable;
    button->last_change_usec = timer_get_usec();
}

// one shot on falling edge after debounce
int button_pressed(button_t *button) {
    int cur = gpio_read(button->pin);
    uint32_t now = timer_get_usec();

    if(cur != button->last_read) {
        button->last_read = cur;
        button->last_change_usec = now;
    }
    if(now - button->last_change_usec < debounce_us)
        return 0;
    if(cur == button->stable)
        return 0;

    int was = button->stable;
    button->stable = cur;
    return was == 1 && cur == 0;
}
