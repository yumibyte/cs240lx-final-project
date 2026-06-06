#include "tsc2007.h"

#include "libc/bit-support.h"

typedef struct {
  uint32_t control;
  uint32_t status;
  uint32_t dlen;
  uint32_t dev_addr;
  uint32_t fifo;
  uint32_t clock_div;
  uint32_t clock_delay;
  uint32_t clock_stretch_timeout;
} rpi_i2c_t;

static volatile rpi_i2c_t *const i2c_hw = (void *)0x20804000;
static unsigned tsc2007_addr = TSC2007_I2C_ADDR_DEFAULT;
static int tsc2007_ready = 0;

enum {
  MEASURE_TEMP0 = 0x0,
  MEASURE_Z1 = 0xb,
  MEASURE_Z2 = 0xc,
  MEASURE_X = 0xd,
  MEASURE_Y = 0x9,
  PWR_ADON_IRQOFF = 0x2,
  ADC_12BIT = 0x1,
  PWR_POWERDOWN_IRQON = 0x0,
};

static int i2c_has_error(void) { return (i2c_hw->status & (1 << 8)) != 0; }

static int i2c_wait_idle(void) {
  unsigned spins = 0;
  while (i2c_hw->status & 1) {
    if (++spins > 1000000) {
      return -1;
    }
  }
  return 0;
}

static int tsc_i2c_write(unsigned addr, const uint8_t *data, unsigned nbytes) {
  if (i2c_wait_idle() < 0) {
    return -1;
  }
  if ((i2c_hw->status & (1 << 6)) == 0) {
    return -1;
  }

  i2c_hw->status = bit_set(i2c_hw->status, 1);
  i2c_hw->dev_addr = addr;
  i2c_hw->dlen = nbytes;
  i2c_hw->control = bit_set(i2c_hw->control, 5);
  i2c_hw->control = bit_set(i2c_hw->control, 4);
  i2c_hw->control = bit_clr(i2c_hw->control, 0);
  i2c_hw->control = bit_set(i2c_hw->control, 7);

  while (!(i2c_hw->status & 1)) {
    ;
  }

  for (unsigned sent = 0; sent < nbytes; sent++) {
    unsigned spins = 0;
    while ((i2c_hw->status & (1 << 4)) == 0) {
      if (++spins > 1000000) {
        return -1;
      }
    }
    i2c_hw->fifo = data[sent];
  }

  {
    unsigned spins = 0;
    while (!(i2c_hw->status & (1 << 1))) {
      if (++spins > 1000000) {
        return -1;
      }
    }
  }
  if ((i2c_hw->status & 1) != 0 || i2c_has_error()) {
    return -1;
  }
  return 0;
}

static int tsc_i2c_read(unsigned addr, uint8_t *data, unsigned nbytes) {
  if (i2c_wait_idle() < 0) {
    return -1;
  }
  if ((i2c_hw->status & (1 << 6)) == 0) {
    return -1;
  }

  i2c_hw->status = bit_set(i2c_hw->status, 1);
  i2c_hw->dev_addr = addr;
  i2c_hw->dlen = nbytes;
  i2c_hw->control = bit_set(i2c_hw->control, 5);
  i2c_hw->control = bit_set(i2c_hw->control, 4);
  i2c_hw->control = bit_set(i2c_hw->control, 0);
  i2c_hw->control = bit_set(i2c_hw->control, 7);

  while (!(i2c_hw->status & 1)) {
    ;
  }

  for (unsigned got = 0; got < nbytes; got++) {
    unsigned spins = 0;
    while ((i2c_hw->status & (1 << 5)) == 0) {
      if (++spins > 1000000) {
        return -1;
      }
    }
    data[got] = (uint8_t)i2c_hw->fifo;
  }

  {
    unsigned spins = 0;
    while (!(i2c_hw->status & (1 << 1))) {
      if (++spins > 1000000) {
        return -1;
      }
    }
  }
  if ((i2c_hw->status & 1) != 0 || i2c_has_error()) {
    return -1;
  }
  return 0;
}

static uint8_t tsc2007_make_cmd(unsigned func, unsigned pwr, unsigned adc) {
  return (uint8_t)((func << 4) | (pwr << 2) | (adc << 1));
}

void i2c_bus_reconcile_for_libpi(void) {
  unsigned spins;

  i2c_wait_idle();

  // flush fifos so libpi xfer_start sees status.txe == 1
  i2c_hw->control = bit_set(i2c_hw->control, 4);
  i2c_hw->control = bit_set(i2c_hw->control, 5);

  i2c_hw->status = bit_set(i2c_hw->status, 8);
  i2c_hw->status = bit_set(i2c_hw->status, 1);

  spins = 0;
  while ((i2c_hw->status & (1 << 6)) == 0) {
    if (++spins > 1000000) {
      return;
    }
  }
}

int i2c_probe_addr(unsigned addr) {
  uint8_t cmd[2] = {0x00, 0xAE};
  int ok;
  if (addr == 0x3C) {
    ok = tsc_i2c_write(addr, cmd, 2);
  } else {
    cmd[0] = tsc2007_make_cmd(MEASURE_TEMP0, PWR_POWERDOWN_IRQON, ADC_12BIT);
    ok = tsc_i2c_write(addr, cmd, 1);
  }
  i2c_bus_reconcile_for_libpi();
  return ok == 0;
}

static int tsc2007_probe_addr(unsigned addr) {
  uint8_t cmd = tsc2007_make_cmd(MEASURE_TEMP0, PWR_POWERDOWN_IRQON, ADC_12BIT);
  return tsc_i2c_write(addr, &cmd, 1);
}

static uint16_t tsc2007_read_channel(unsigned addr, uint8_t cmd) {
  uint8_t raw[2] = {0, 0};
  if (tsc_i2c_write(addr, &cmd, 1) < 0) {
    return 0;
  }
  delay_us(500);
  if (tsc_i2c_read(addr, raw, 2) < 0) {
    return 0;
  }
  return (uint16_t)(((raw[0] << 4) | (raw[1] >> 4)) & 0x0FFF);
}

int tsc2007_init(void) {
  tsc2007_ready = 0;
  for (unsigned addr = 0x48; addr <= 0x4b; addr++) {
    if (tsc2007_probe_addr(addr) == 0) {
      i2c_bus_reconcile_for_libpi();
      tsc2007_addr = addr;
      tsc2007_ready = 1;
      uint8_t setup =
          tsc2007_make_cmd(MEASURE_TEMP0, PWR_POWERDOWN_IRQON, ADC_12BIT);
      tsc_i2c_write(tsc2007_addr, &setup, 1);
      delay_us(500);
      return 1;
    }
  }
  return 0;
}

touch_reading_t tsc2007_read(void) {
  touch_reading_t sample = {0, 0, 0};
  if (!tsc2007_ready) {
    return sample;
  }

  uint8_t cmd_z1 = tsc2007_make_cmd(MEASURE_Z1, PWR_ADON_IRQOFF, ADC_12BIT);
  uint8_t cmd_z2 = tsc2007_make_cmd(MEASURE_Z2, PWR_ADON_IRQOFF, ADC_12BIT);
  uint8_t cmd_x = tsc2007_make_cmd(MEASURE_X, PWR_ADON_IRQOFF, ADC_12BIT);
  uint8_t cmd_y = tsc2007_make_cmd(MEASURE_Y, PWR_ADON_IRQOFF, ADC_12BIT);

  uint16_t z1 = tsc2007_read_channel(tsc2007_addr, cmd_z1);
  uint16_t z2 = tsc2007_read_channel(tsc2007_addr, cmd_z2);
  int pressure = (int)z1 + 4096 - (int)z2;

  if (pressure < TSC2007_PRESSURE_MIN) {
    return sample;
  }

  sample.touched = 1;
  sample.x = tsc2007_read_channel(tsc2007_addr, cmd_x);
  sample.y = tsc2007_read_channel(tsc2007_addr, cmd_y);
  return sample;
}
