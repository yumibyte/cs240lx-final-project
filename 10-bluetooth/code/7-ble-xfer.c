// BLE image transfer demo -- Pi sends signature BMPs to a connected phone.
//
// On boot the Pi reads every *.BMP file off the SD card (FAT32, partition 0)
// and registers them as images. The phone connects, enables TX notifications,
// and pulls each image with a GET request -- no Mac-then-phone upload needed.
//
// Put signatures on the SD card as SIG000.BMP, SIG001.BMP, ... (the capture
// board does this via sig_save()). If the card has no BMPs, the Pi falls back
// to a built-in test square so there's always something to fetch.

#include "rpi.h"
#include "ble.h"
#include "ble_xfer.h"
#include "sd_images.h"

static struct ble_conn g_conn;

static void on_nus_write(u16 handle, const u8 *data, u16 len) {
    if (handle != BLE_NUS_RX_HANDLE)
        return;
    ble_xfer_handle(&g_conn, data, len);
}

void notmain(void) {
    ble_init();
    ble_xfer_init();

    // Pull signatures off the SD card; fall back to the test square if none.
    int n = ble_xfer_load_sd_images();
    if (n <= 0) {
        printk("xfer: no SD images, using built-in test BMP\n");
        ble_xfer_add_test_bmp();
    }

    ble_set_write_callback(on_nus_write);

    printk("BLE xfer ready. Advertising as 'cs240lx-pi'...\n");
    printk("Central: enable TX notify, then send LIST/GET to fetch images.\n");

    ble_start_nus_advertising("cs240lx-pi");

    g_conn.connected = false;
    ble_wait_for_connection(&g_conn);

    while (ble_poll(&g_conn)) {
        ble_xfer_poll(&g_conn);
    }

    printk("Disconnected.\n");
}
