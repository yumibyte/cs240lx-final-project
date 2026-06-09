PROGS = main.c

WS2812_DIR = ../../11-ws2812b/1-code
BT_DIR = ../10-bluetooth/code

COMMON_SRC = libs/display/ssd1306-display-driver.c
COMMON_SRC += libs/display/standard-ascii-font.c
COMMON_SRC += libs/i2c/i2c.c
COMMON_SRC += libs/neopixel/led-matrix.c
COMMON_SRC += libs/neopixel/led-scroll.c
COMMON_SRC += libs/neopixel/led-tree.c
COMMON_SRC += libs/neopixel/led-graphics.c
COMMON_SRC += libs/sd/emmc.c
COMMON_SRC += libs/sd/pi-sd.c
COMMON_SRC += libs/sd/mbr.c
COMMON_SRC += libs/sd/mbr-helpers.c
COMMON_SRC += libs/fat32/fat32.c
COMMON_SRC += libs/fat32/fat32-helpers.c
COMMON_SRC += libs/fat32/fat32-lfn-helpers.c
COMMON_SRC += libs/fat32/unicode-utf8.c
COMMON_SRC += libs/fat32/sig-zip.c
COMMON_SRC += libs/fat32/miniz/miniz.c
COMMON_SRC += libs/fat32/miniz/miniz_tdef.c
COMMON_SRC += $(BT_DIR)/pl011.c
COMMON_SRC += $(BT_DIR)/bt.c
COMMON_SRC += $(BT_DIR)/ble.c
COMMON_SRC += $(BT_DIR)/ble_xfer.c
COMMON_SRC += $(BT_DIR)/BCM43430A1.c

O := $(CS240LX_2026_PATH)/libpi
STAFF_OBJS += $(O)/staff-objs/staff-kmalloc.o

LIB_INC = -Ilibs/i2c -Ilibs/display -Ilibs/sd -Ilibs/fat32 -Ilibs/neopixel
INC += -I$(WS2812_DIR) -I$(BT_DIR) $(LIB_INC)
OPT_LEVEL = -O2
CFLAGS_EXTRA += -Wno-strict-aliasing
CFLAGS_EXTRA += -Ilibs/fat32/miniz -include libs/fat32/miniz-port.h
RUN = 1

include $(CS240LX_2026_PATH)/libpi/mk/Makefile.robust-v3

install-sd: main.bin
	rm -f $(SD)/kernel.img
	cp main.bin $(SD)/kernel.img && sync
