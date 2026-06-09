// BLE Nordic UART Service demo.
//
// 1. Flash this to the Pi.
// 2. On your phone, install nRF Connect (free, no TestFlight).
// 3. Scan, connect to "cs240lx-pi", open the UART tab (NUS UUID).
// 4. Enable notifications on TX, type in the UART box -- bytes arrive on the Pi.
// 5. Pi echoes each chunk back over TX notifications.

#include "rpi.h"
#include "ble.h"

static struct ble_conn g_conn;

static void on_nus_write(u16 handle, const u8 *data, u16 len) {
    if (handle != BLE_NUS_RX_HANDLE)
        return;

    printk("NUS RX (%d bytes): ", len);
    for (u16 i = 0; i < len; i++)
        printk("%c", data[i]);
    printk("\n");

    // Echo back so you can see round-trip in the nRF Connect UART view.
    ble_nus_send(&g_conn, data, len);
}

void notmain(void) {
    ble_init();
    ble_set_write_callback(on_nus_write);

    printk("Advertising as 'cs240lx-pi' (Nordic UART Service)...\n");
    printk("Connect with nRF Connect or LightBlue, open UART, enable TX notify.\n");

    ble_start_nus_advertising("cs240lx-pi");

    g_conn.connected = false;
    ble_wait_for_connection(&g_conn);

    while (ble_poll(&g_conn))
        ;

    printk("Disconnected.\n");
}
