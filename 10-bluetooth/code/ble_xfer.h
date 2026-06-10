// Binary image transfer protocol over BLE Nordic UART Service.
//
// Bidirectional over NUS:
//   Client -> Pi on RX (write):  CHUNK/COMMIT/RESET/GET/LIST
//   Pi -> Client on TX (notify): ACK/NACK/DONE/PUSH_CHUNK/PUSH_DONE
//
// Frame layout (header = 15 bytes, then optional payload):
//   magic[4]   = 'C' 'S' '2' '4'
//   cmd[1]     = xfer_cmd or xfer_rsp
//   seq[2]     = sequence number (little-endian)
//   total[4]   = total image size or progress/meta
//   offset[4]  = byte offset in image, or image id for GET
//   payload[]  = chunk data (CHUNK / PUSH_CHUNK only)

#ifndef BLE_XFER_H
#define BLE_XFER_H

#include "ble.h"

#define XFER_HDR_LEN        15   // magic(4)+cmd(1)+seq(2)+total(4)+offset(4)
#define XFER_MAX_IMAGE_BYTES (32 * 1024)  // max single image size
#define XFER_MAX_IMAGES     8             // completed images kept in RAM

// Client -> Pi commands (write to NUS RX)
enum xfer_cmd {
    XFER_CMD_CHUNK  = 0x01, // client->pi: payload chunk at offset
    XFER_CMD_COMMIT = 0x02, // client->pi: finalize incoming transfer
    XFER_CMD_RESET  = 0x03, // client->pi: abort incoming transfer
    XFER_CMD_GET    = 0x04, // client->pi: request image; offset field = image id
    XFER_CMD_LIST   = 0x05, // client->pi: how many images stored?
};

// Pi -> client responses (notify on NUS TX)
enum xfer_rsp {
    XFER_RSP_ACK        = 0x80, // incoming chunk accepted
    XFER_RSP_NACK       = 0x81, // error; offset field = error code
    XFER_RSP_DONE       = 0x82, // incoming image stored; offset = image id
    XFER_RSP_PUSH_CHUNK = 0x83, // outgoing chunk; offset+payload
    XFER_RSP_PUSH_DONE  = 0x84, // outgoing transfer complete; offset = image id
    XFER_RSP_LIST       = 0x85, // image count in total field
};

enum xfer_err {
    XFER_ERR_BAD_MAGIC    = 1,
    XFER_ERR_BAD_CMD      = 2,
    XFER_ERR_TOO_LARGE    = 3,
    XFER_ERR_NO_TRANSFER  = 4,
    XFER_ERR_INCOMPLETE   = 5,
    XFER_ERR_STORAGE_FULL = 6,
    XFER_ERR_SHORT        = 7,
    XFER_ERR_NO_IMAGE     = 8,
    XFER_ERR_NOTIFY_OFF   = 9, // client has not enabled TX notifications
};

void ble_xfer_init(void);

// Create a 32x32 red test BMP and store it as image 0.
void ble_xfer_add_test_bmp(void);

// Register an already-in-memory image (e.g. read from the SD card) into the
// image store. Copies `len` bytes. Returns the new image id, or -1 if the
// store is full or out of memory.
int ble_xfer_add_image(const u8 *data, u32 len);

// Feed raw bytes from a NUS RX write.
void ble_xfer_handle(struct ble_conn *conn, const u8 *data, u16 len);

// Drive an in-progress PUSH_CHUNK stream one step at a time. Call from the
// main loop after ble_poll() so HCI events keep flowing during large images.
void ble_xfer_poll(struct ble_conn *conn);

u8 ble_xfer_image_count(void);
const u8 *ble_xfer_image_data(u8 id, u32 *out_len);

#endif // BLE_XFER_H
