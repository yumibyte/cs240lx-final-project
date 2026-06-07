#pragma once
#include "rpi.h"
#include "fat32.h"
#include "mbr.h"
#include "pi-sd.h"

// OLED:    128 x 64 pixels
// TSC2007: outputs 0-4095 for both X and Y
//
// 1024x512: captures all detail the touch hardware can provide.
// 1024*3 = 3072 bytes/row — 4-byte aligned, no BMP row padding needed.
#define SIG_W 1024
#define SIG_H 512

// --- internal state (do not use directly) ---
static uint8_t     *sig_pixels = 0;   // heap-allocated in sig_init()
static int         sig_counter = 0;
static fat32_fs_t  sig_fs;
static pi_dirent_t sig_root;

// Fill the pixel buffer white. Call after each save.
void sig_clear(void) {
    memset(sig_pixels, 0xFF, SIG_H * SIG_W * 3);
}

// Initialize SD card, FAT32, and scan for existing SIG files.
// Call once at the start of notmain() before anything else.
void sig_init(void) {
    kmalloc_init();
    sig_pixels = kmalloc(SIG_H * SIG_W * 3);   // 1.5MB from heap, not BSS
    pi_sd_init();

    mbr_t *mbr = mbr_read();
    mbr_partition_ent_t partition;
    memcpy(&partition, mbr->part_tab1, sizeof(partition));
    assert(mbr_part_is_fat32(partition.part_type));

    sig_fs   = fat32_mk(&partition);
    sig_root = fat32_get_root(&sig_fs);

    // find highest existing SIG file so we don't overwrite on reboot
    pi_directory_t dir = fat32_readdir(&sig_fs, &sig_root);
    int max = -1;
    for (int i = 0; i < dir.ndirents; i++) {
        char *n = dir.dirents[i].name;
        if (n[0]=='S' && n[1]=='I' && n[2]=='G'
         && n[3]>='0' && n[3]<='9'
         && n[4]>='0' && n[4]<='9'
         && n[5]>='0' && n[5]<='9') {
            int num = (n[3]-'0')*100 + (n[4]-'0')*10 + (n[5]-'0');
            if (num > max) max = num;
        }
    }
    sig_counter = (max >= 0) ? max + 1 : 0;
    printk("sig_init: SD ready, starting from SIG%d%d%d.BMP\n",
        sig_counter/100, (sig_counter/10)%10, sig_counter%10);

    // clear pixel buffer to white for first signature
    sig_clear();
}

// Record one touch point into the pixel buffer.
// tx, ty are raw TSC2007 coordinates (0-4095). Only call when Z > threshold.
// Draws a 3x3 dot so strokes are visible at 1024x512.
void sig_draw_point(uint16_t tx, uint16_t ty) {
    uint32_t px = (uint32_t)tx * SIG_W / 4096;
    uint32_t py = (uint32_t)ty * SIG_H / 4096;
    for (int dy = 0; dy <= 2; dy++) {
        for (int dx = 0; dx <= 2; dx++) {
            uint32_t x = px + dx, y = py + dy;
            if (x >= SIG_W || y >= SIG_H) continue;
            uint8_t *p = sig_pixels + (y * SIG_W + x) * 3;
            p[0] = p[1] = p[2] = 0;
        }
    }
}

// Render sig_pixels as a 24-bit BMP and write it to the SD card.
// Filename cycles: SIG000.BMP, SIG001.BMP, ...
// Call when button is pressed or timeout fires, then call sig_clear().
void sig_save(void) {
    char filename[12] = "SIG000.BMP";
    filename[3] = '0' + (sig_counter / 100) % 10;
    filename[4] = '0' + (sig_counter / 10)  % 10;
    filename[5] = '0' +  sig_counter        % 10;

    uint32_t row_bytes  = SIG_W * 3;
    uint32_t pixel_size = row_bytes * SIG_H;
    uint32_t file_size  = 54 + pixel_size;

    uint8_t *bmp = kmalloc(file_size);
    memset(bmp, 0, 54);

    // BMP file header (14 bytes) — Microsoft BMP standard
    bmp[0] = 'B'; bmp[1] = 'M';
    *(uint32_t *)(bmp +  2) = file_size;
    *(uint32_t *)(bmp + 10) = 54;           // pixel data offset

    // DIB / BITMAPINFOHEADER (40 bytes at offset 14)
    *(uint32_t *)(bmp + 14) = 40;           // DIB header size
    *(int32_t  *)(bmp + 18) = SIG_W;
    *(int32_t  *)(bmp + 22) = -SIG_H;      // negative = top-down rows
    *(uint16_t *)(bmp + 26) = 1;            // color planes
    *(uint16_t *)(bmp + 28) = 24;           // bits per pixel (RGB)
    *(uint32_t *)(bmp + 30) = 0;            // no compression
    *(uint32_t *)(bmp + 34) = pixel_size;
    *(int32_t  *)(bmp + 38) = 2835;         // ~72 DPI horizontal
    *(int32_t  *)(bmp + 42) = 2835;         // ~72 DPI vertical

    memcpy(bmp + 54, sig_pixels, pixel_size);

    fat32_delete(&sig_fs, &sig_root, filename);
    assert(fat32_create(&sig_fs, &sig_root, filename, 0));
    pi_file_t f = {
        .data    = (char *)bmp,
        .n_data  = file_size,
        .n_alloc = file_size,
    };
    assert(fat32_write(&sig_fs, &sig_root, filename, &f));
    printk("sig_save: wrote %s (%d bytes)\n", filename, file_size);

    sig_counter++;
}