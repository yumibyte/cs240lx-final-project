// Load signature ZIPs off the SD card into the BLE image store.
//
// Uses the FAT32 + SD driver stack pulled in from the cs240lx-final-project
// submodule (libs/sd, libs/fat32). The capture board writes signatures as
// SIG000.ZIP, SIG001.ZIP, ... to the root of the (FAT32) SD card; here we
// read them back so the Pi can push them to the phone over BLE. The phone
// unzips the payload to recover the BMP.

#include "rpi.h"
#include "ble_xfer.h"
#include "sd_images.h"

#include "fat32.h"
#include "mbr.h"
#include "pi-sd.h"

#include <stdbool.h>

// Accept SIG000.ZIP .. SIG999.ZIP (exactly 10 chars: SIGddd.ZIP).
// Skips macOS short-name aliases (contain '~').
static bool is_signature_zip(const char *name) {
    if (strlen(name) != 10)
        return false;
    if (name[0] != 'S' || name[1] != 'I' || name[2] != 'G')
        return false;
    if (name[3] < '0' || name[3] > '9')
        return false;
    if (name[4] < '0' || name[4] > '9')
        return false;
    if (name[5] < '0' || name[5] > '9')
        return false;
    return name[6] == '.'
        && name[7] == 'Z' && name[8] == 'I' && name[9] == 'P';
}

int ble_xfer_load_sd_images(void) {
    printk("sd: initializing SD card...\n");
    pi_sd_init();

    mbr_t *mbr = mbr_read();
    mbr_partition_ent_t part;
    memcpy(&part, mbr->part_tab1, sizeof(part));
    if (!mbr_part_is_fat32(part.part_type)) {
        printk("sd: partition 0 is not FAT32 (type=0x%x); no images loaded\n",
            part.part_type);
        return 0;
    }

    fat32_fs_t fs = fat32_mk(&part);
    pi_dirent_t root = fat32_get_root(&fs);
    pi_directory_t dir = fat32_readdir(&fs, &root);
    printk("sd: root has %d entries\n", dir.ndirents);

    int loaded = 0;
    for (unsigned i = 0; i < dir.ndirents; i++) {
        pi_dirent_t *d = &dir.dirents[i];
        if (d->is_dir_p)
            continue;
        if (!is_signature_zip(d->name))
            continue;

        pi_file_t *f = fat32_read(&fs, &root, d->name);
        if (!f || f->n_data == 0) {
            printk("sd: skip %s (empty or read failed)\n", d->name);
            continue;
        }

        int id = ble_xfer_add_image((const u8 *)f->data, f->n_data);
        if (id < 0) {
            printk("sd: image store full at %s; stopping\n", d->name);
            break;
        }
        printk("sd: loaded %s -> image id=%d (%d bytes)\n",
            d->name, id, f->n_data);
        loaded++;
    }

    printk("sd: loaded %d image(s) from SD card\n", loaded);
    return loaded;
}
