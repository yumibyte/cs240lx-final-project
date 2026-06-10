#ifndef SD_IMAGES_H
#define SD_IMAGES_H

// Mount the SD card (partition 0, FAT32), read every *.BMP file in the root
// directory, and register each one as a BLE xfer image. Signatures written by
// the final-project capture board land here as SIG000.BMP, SIG001.BMP, ...
//
// Returns the number of images loaded (0 if no SD/FAT32/BMPs were found).
int ble_xfer_load_sd_images(void);

#endif // SD_IMAGES_H
