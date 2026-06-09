PROGS = main.c

WS2812_DIR = ../../11-ws2812b/1-code

COMMON_SRC = libs/display/ssd1306-display-driver.c
COMMON_SRC += libs/display/standard-ascii-font.c
COMMON_SRC += libs/i2c/i2c.c
COMMON_SRC += libs/neopixel/led-matrix.c
COMMON_SRC += libs/neopixel/led-scroll.c
COMMON_SRC += libs/neopixel/led-tree.c
COMMON_SRC += libs/neopixel/led-graphics.c

LIB_INC = -Ilibs/i2c -Ilibs/display -Ilibs/sd -Ilibs/fat32 -Ilibs/neopixel
INC += -I$(WS2812_DIR) $(LIB_INC)
OPT_LEVEL = -O2
RUN = 1

include $(CS240LX_2026_PATH)/libpi/mk/Makefile.robust-v3

install-sd: main.bin
	rm -f $(SD)/kernel.img
	cp main.bin $(SD)/kernel.img && sync
