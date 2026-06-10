#pragma once
#include "rpi.h"

// miso is 9, mosi is 10, sclk is 11, ceo is 8, channel 0
void pot_init(void);

// 10-bit reading, 0 = low wiper, 1023 = high
uint16_t pot_read(void);
