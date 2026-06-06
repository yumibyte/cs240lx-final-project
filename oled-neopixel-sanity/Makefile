PROGS = sanity-check.c

WS2812_DIR = ../../11-ws2812b/1-code
OLED_DIR = ../../15-oled-display/ssd1306-display/code

COMMON_SRC = $(OLED_DIR)/ssd1306-display-driver.c
COMMON_SRC += $(OLED_DIR)/standard-ascii-font.c
COMMON_SRC += tsc2007.c

INC += -I$(WS2812_DIR) -I$(OLED_DIR) -I.
OPT_LEVEL = -O2
RUN = 1

include $(CS240LX_2026_PATH)/libpi/mk/Makefile.robust-v3

install-sd: sanity-check.bin
	cp sanity-check.bin $(SD)/kernel.img && sync