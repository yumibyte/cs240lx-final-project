#pragma once
#include "rpi.h"
#include "fat32.h"
#include "mbr.h"
#include "pi-sd.h"
#include "sig-zip.h"

// OLED:    128 x 64 pixels
// TSC2007: outputs 0-4095 for both X and Y
//
// 1024x512: captures all detail the touch hardware can provide.
// 1024*3 = 3072 bytes/row — 4-byte aligned, no BMP row padding needed.
#define SIG_W 1024
#define SIG_H 512
// Max pixel gap to draw a connecting line (new touch farther = separate stroke).
#define SIG_CONNECT_DIST 48

// --- internal state (do not use directly) ---
static uint8_t     *sig_pixels = 0;   // heap-allocated in sig_init()
static int         sig_counter = 0;
static fat32_fs_t  sig_fs;
static pi_dirent_t sig_root;
static uint8_t    *sig_last_zip = 0;
static uint32_t    sig_last_zip_size = 0;
static int         sig_last_valid = 0;
static uint32_t    sig_last_x = 0;
static uint32_t    sig_last_y = 0;

// BMP is rotated 180° vs the OLED preview (map_touch_to_pixel in main.c).
static void sig_touch_to_pixel(uint16_t tx, uint16_t ty, uint32_t *px, uint32_t *py) {
    *px = (uint32_t)ty * SIG_W / 4096;
    *py = (uint32_t)tx * SIG_H / 4096;
}

static void sig_plot(uint32_t x, uint32_t y) {
    for (int dy = 0; dy <= 2; dy++) {
        for (int dx = 0; dx <= 2; dx++) {
            uint32_t px = x + dx, py = y + dy;
            if (px >= SIG_W || py >= SIG_H) continue;
            uint8_t *p = sig_pixels + (py * SIG_W + px) * 3;
            p[0] = p[1] = p[2] = 0;
        }
    }
}

static void sig_pixel(uint32_t x, uint32_t y) {
    if (x >= SIG_W || y >= SIG_H) return;
    uint8_t *p = sig_pixels + (y * SIG_W + x) * 3;
    p[0] = p[1] = p[2] = 0;
}

static int sig_close_enough(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    int32_t dx = (int32_t)x1 - (int32_t)x0;
    int32_t dy = (int32_t)y1 - (int32_t)y0;
    uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
    uint32_t thresh = (uint32_t)SIG_CONNECT_DIST * SIG_CONNECT_DIST;
    return d2 <= thresh;
}

static void sig_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    int32_t dx = (int32_t)(x1 > x0 ? x1 - x0 : x0 - x1);
    int32_t dy = (int32_t)(y1 > y0 ? y1 - y0 : y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        sig_pixel((uint32_t)x0, (uint32_t)y0);
        if (x0 == x1 && y0 == y1)
            break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

// Fill the pixel buffer white. Call after each save.
void sig_clear(void) {
    memset(sig_pixels, 0xFF, SIG_H * SIG_W * 3);
    sig_last_valid = 0;
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
        int is_sig = n[0]=='S' && n[1]=='I' && n[2]=='G'
                  && n[3]>='0' && n[3]<='9'
                  && n[4]>='0' && n[4]<='9'
                  && n[5]>='0' && n[5]<='9';
        int is_zip = n[6]=='.' && n[7]=='Z' && n[8]=='I' && n[9]=='P';
        int is_bmp = n[6]=='.' && n[7]=='B' && n[8]=='M' && n[9]=='P';
        if (is_sig && (is_zip || is_bmp)) {
            int num = (n[3]-'0')*100 + (n[4]-'0')*10 + (n[5]-'0');
            if (num > max) max = num;
        }
    }
    sig_counter = (max >= 0) ? max + 1 : 0;
    printk("sig_init: SD ready, starting from SIG%d%d%d.ZIP\n",
        sig_counter/100, (sig_counter/10)%10, sig_counter%10);

    // clear pixel buffer to white for first signature
    sig_clear();
}

// Record one touch point into the pixel buffer.
// tx, ty are raw TSC2007 coordinates (0-4095). Only call when Z > threshold.
// Connects to the previous point when it is nearby (same stroke).
void sig_draw_point(uint16_t tx, uint16_t ty) {
    uint32_t px, py;
    sig_touch_to_pixel(tx, ty, &px, &py);
    if (sig_last_valid && sig_close_enough(sig_last_x, sig_last_y, px, py))
        sig_line(sig_last_x, sig_last_y, px, py);
    sig_plot(px, py);
    sig_last_x = px;
    sig_last_y = py;
    sig_last_valid = 1;
}

// Render sig_pixels as a 24-bit BMP, zip it, and write SIG###.ZIP to the SD card.
// The archive contains SIG###.BMP (deflate-compressed).
// Call when the 'done' button is pressed
void sig_save(void) {
    char zip_name[12] = "SIG000.ZIP";
    char bmp_name[12] = "SIG000.BMP";
    zip_name[3] = bmp_name[3] = '0' + (sig_counter / 100) % 10;
    zip_name[4] = bmp_name[4] = '0' + (sig_counter / 10)  % 10;
    zip_name[5] = bmp_name[5] = '0' +  sig_counter        % 10;

    uint32_t row_bytes  = SIG_W * 3;
    uint32_t pixel_size = row_bytes * SIG_H;
    uint32_t bmp_size   = 54 + pixel_size;

    uint8_t *bmp = kmalloc(bmp_size);
    memset(bmp, 0, 54);

    // BMP file header (14 bytes) — Microsoft BMP standard
    bmp[0] = 'B'; bmp[1] = 'M';
    *(uint32_t *)(bmp +  2) = bmp_size;
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

    uint8_t *zip = 0;
    uint32_t zip_size = 0;
    assert(sig_zip_pack(bmp_name, bmp, bmp_size, &zip, &zip_size) == 0);

    fat32_delete(&sig_fs, &sig_root, zip_name);
    assert(fat32_create(&sig_fs, &sig_root, zip_name, 0));
    pi_file_t f = {
        .data    = (char *)zip,
        .n_data  = zip_size,
        .n_alloc = zip_size,
    };
    assert(fat32_write(&sig_fs, &sig_root, zip_name, &f));
    printk("sig_save: wrote %s (%d bytes, bmp %d)\n", zip_name, zip_size, bmp_size);

    sig_last_zip = zip;
    sig_last_zip_size = zip_size;
    sig_counter++;
}

// last ZIP from sig_save(); valid until the next sig_save()
void sig_last_saved_zip(const uint8_t **zip, uint32_t *zip_len) {
    if(zip)
        *zip = sig_last_zip;
    if(zip_len)
        *zip_len = sig_last_zip_size;
}