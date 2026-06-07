PROGS = main.c

WS2812_DIR = ../../11-ws2812b/1-code

COMMON_SRC = ssd1306-display-driver.c
COMMON_SRC += standard-ascii-font.c
COMMON_SRC += i2c.c

INC += -I$(WS2812_DIR) -I.
OPT_LEVEL = -O2
RUN = 1

include $(CS240LX_2026_PATH)/libpi/mk/Makefile.robust-v3

install-sd: main.bin
	rm -f $(SD)/kernel.img
	cp main.bin $(SD)/kernel.img && sync
