#include "pot-mcp3008.h"
#include "spi.h"

enum {
    pot_spi_div = 16,
    pot_mcp3008_ch = 0,
};

static spi_t pot_spi;

void pot_init(void) {
    pot_spi = spi_n_init(SPI_CE0, pot_spi_div);
}

// MCP3008 single-ended read: start bit + (8+ch)<<4 command, 10 bits in rx[1:2]
uint16_t pot_read(void) {
    uint8_t tx[3] = { 1, (uint8_t)((8 + pot_mcp3008_ch) << 4), 0 };
    uint8_t rx[3] = { 0, 0, 0 };
    spi_n_transfer(pot_spi, rx, tx, 3);
    return (uint16_t)(((rx[1] & 3) << 8) | rx[2]);
}
