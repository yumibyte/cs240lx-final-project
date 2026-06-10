# PROGS = 1-test-pl011.c
# PROGS = 2-hci-reset.c
# PROGS = 3-firmware-upload.c
# PROGS = 4-read-bdaddr.c
# PROGS = 5-connect.c
# PROGS = 5-accept-connection.c
# PROGS = 6-ble-advertise.c
PROGS = 7-ble-xfer.c

COMMON_SRC += bt.c
COMMON_SRC += ble.c
COMMON_SRC += ble_xfer.c
COMMON_SRC += BCM43430A1.c

# Replace with your own pl011.c!
COMMON_SRC += pl011.c
# STAFF_OBJS += staff-pl011.o

# SD card + FAT32 stack (from cs240lx-final-project submodule) so the Pi can
# load signature BMPs straight off the SD card and serve them over BLE.
SD_DIR  = libs/sd
FAT_DIR = libs/fat32
COMMON_SRC += $(SD_DIR)/emmc.c
COMMON_SRC += $(SD_DIR)/pi-sd.c
COMMON_SRC += $(SD_DIR)/mbr.c
COMMON_SRC += $(SD_DIR)/mbr-helpers.c
COMMON_SRC += $(FAT_DIR)/fat32.c
COMMON_SRC += $(FAT_DIR)/fat32-helpers.c
COMMON_SRC += $(FAT_DIR)/fat32-lfn-helpers.c
COMMON_SRC += $(FAT_DIR)/unicode-utf8.c
COMMON_SRC += sd_images.c

INC += -I$(SD_DIR) -I$(FAT_DIR)

# if you need to override the install
BOOTLOADER = my-install

OPT_LEVEL = -Og

# automatically run the code
RUN = 1

include $(CS240LX_2026_PATH)/libpi/mk/Makefile.robust-v3