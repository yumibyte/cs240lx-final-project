#pragma once

#include "rpi.h"

enum {
  TSC2007_I2C_ADDR_DEFAULT = 0x48,
  TSC2007_PRESSURE_MIN = 120,
};

typedef struct {
  int touched;
  unsigned x;
  unsigned y;
} touch_reading_t;

// check for tsc
int i2c_probe_addr(unsigned addr);

// this is necessary because libpi i2c uses a different bus than the tsc2007
void i2c_bus_reconcile_for_libpi(void);

// setup tsc
int tsc2007_init(void);

touch_reading_t tsc2007_read(void);
