// BLE peripheral scaffolding for the CYW43438 / BCM43430A1.
//
// This sits on top of the HCI command/event + ACL transport in bt.c and
// builds toward a BLE peripheral that an iPhone (CoreBluetooth) app can
// connect to. The layering, bottom to top:
//
//   pl011.c        H4 UART transport
//   bt.c           HCI commands/events, ACL, firmware upload
//   ble.c          LE advertising, LE connection, L2CAP/ATT/GATT  <-- here
//
// Spec references (Bluetooth Core Spec v5.1, included under ../docs):
//   - LE HCI commands:        Vol 4, Part E, section 7.8
//   - LE Meta events:         Vol 4, Part E, section 7.7.65
//   - L2CAP (LE):             Vol 3, Part A
//   - ATT:                    Vol 3, Part F
//   - GATT:                   Vol 3, Part G

#ifndef BLE_H
#define BLE_H

#include "bt.h"

#include <stdbool.h>

// ATT runs on a fixed L2CAP channel id; SMP on another. (Vol 3, Part A.)
#define L2CAP_CID_ATT   0x0004
#define L2CAP_CID_SMP   0x0006

// Default ATT MTU before any MTU exchange. (Vol 3, Part F, 3.2.8.)
#define ATT_MTU_DEFAULT 23

// The largest MTU we offer. iOS will negotiate down to its own limit.
// Sized so a response fits comfortably in our ACL reassembly buffer.
#define ATT_MTU_OURS    247

// ATT protocol opcodes. (Vol 3, Part F, 3.4.8.)
enum att_opcode {
    ATT_ERROR_RSP         = 0x01,
    ATT_EXCHANGE_MTU_REQ  = 0x02,
    ATT_EXCHANGE_MTU_RSP  = 0x03,
    ATT_FIND_INFO_REQ     = 0x04,
    ATT_FIND_INFO_RSP     = 0x05,
    ATT_FIND_BY_TYPE_REQ  = 0x06,
    ATT_FIND_BY_TYPE_RSP  = 0x07,
    ATT_READ_BY_TYPE_REQ  = 0x08,
    ATT_READ_BY_TYPE_RSP  = 0x09,
    ATT_READ_REQ          = 0x0A,
    ATT_READ_RSP          = 0x0B,
    ATT_READ_BLOB_REQ     = 0x0C,
    ATT_READ_BLOB_RSP     = 0x0D,
    ATT_READ_BY_GROUP_REQ = 0x10,
    ATT_READ_BY_GROUP_RSP = 0x11,
    ATT_WRITE_REQ         = 0x12,
    ATT_WRITE_RSP         = 0x13,
    ATT_WRITE_CMD         = 0x52,
    ATT_HANDLE_VALUE_NTF  = 0x1B,
    ATT_HANDLE_VALUE_IND  = 0x1D,
    ATT_HANDLE_VALUE_CFM  = 0x1E,
};

// ATT error codes for Error Response. (Vol 3, Part F, 3.4.1.1.)
enum att_error {
    ATT_ERR_INVALID_HANDLE        = 0x01,
    ATT_ERR_READ_NOT_PERMITTED    = 0x02,
    ATT_ERR_WRITE_NOT_PERMITTED   = 0x03,
    ATT_ERR_INVALID_PDU           = 0x04,
    ATT_ERR_REQUEST_NOT_SUPPORTED = 0x06,
    ATT_ERR_INVALID_OFFSET        = 0x07,
    ATT_ERR_ATTR_NOT_FOUND        = 0x0A,
    ATT_ERR_ATTR_NOT_LONG         = 0x0B,
    ATT_ERR_UNLIKELY              = 0x0E,
    ATT_ERR_UNSUPPORTED_GROUP     = 0x10,
};

// GATT well-known attribute-type UUIDs. (Vol 3, Part G, 3.x.)
#define GATT_PRIMARY_SERVICE   0x2800
#define GATT_SECONDARY_SERVICE 0x2801
#define GATT_INCLUDE           0x2802
#define GATT_CHARACTERISTIC    0x2803
#define GATT_CHAR_USER_DESC    0x2901
#define GATT_CCC_DESCRIPTOR    0x2902 // Client Characteristic Configuration

// Characteristic property bits in a characteristic declaration. (Vol 3, Part G, 3.3.1.1.)
#define CHAR_PROP_READ      0x02
#define CHAR_PROP_WRITE_NR  0x04
#define CHAR_PROP_WRITE     0x08
#define CHAR_PROP_NOTIFY    0x10
#define CHAR_PROP_INDICATE  0x20

// Simplified ATT attribute access permissions (no encryption support yet).
#define ATT_PERM_READ   0x01
#define ATT_PERM_WRITE  0x02

// One row of the ATT database. The GATT layer (step 7) builds the array.
// An attribute's TYPE is either a 16-bit UUID (`type`, when `type128` is NULL)
// or a 128-bit UUID (`type128`, little-endian). The VALUE bytes depend on the
// type (e.g. service UUID, characteristic declaration, or raw user data).
struct att_attr {
    u16 handle;          // unique, nonzero, assigned in increasing order
    u16 type;            // 16-bit type UUID (used when type128 == NULL)
    const u8 *type128;   // 128-bit type UUID (LE order), or NULL
    u8 *value;           // value bytes (may be modified by a write)
    u16 value_len;       // current value length
    u16 max_len;         // capacity for writable values
    u8 perms;            // ATT_PERM_* bits
};

// The GATT layer provides the attribute database (step 7). ble.c ships a tiny
// default (Generic Access service) so the ATT engine is testable on its own.
extern struct att_attr ble_att_db[];
extern u16 ble_att_db_len;

// Called when the peer writes a characteristic value. handle is the value
// attribute's handle. Lets the app react (e.g. assemble an incoming image).
typedef void (*ble_write_cb_t)(u16 handle, const u8 *data, u16 len);

// LE advertising address types. (Vol 4, Part E, 7.8.5.)
enum ble_addr_type {
    BLE_ADDR_PUBLIC = 0x00,
    BLE_ADDR_RANDOM = 0x01,
};

// LE advertising PDU types for Set Advertising Parameters. (Vol 4, Part E, 7.8.5.)
enum ble_adv_type {
    BLE_ADV_IND         = 0x00, // connectable, undirected
    BLE_ADV_DIRECT_IND  = 0x01, // connectable, directed
    BLE_ADV_SCAN_IND    = 0x02, // scannable, undirected
    BLE_ADV_NONCONN_IND = 0x03, // non-connectable, undirected
};

// AD types used inside advertising/scan-response data. (Core Spec Supplement, Part A.)
enum ble_ad_type {
    AD_TYPE_FLAGS                   = 0x01,
    AD_TYPE_INCOMPLETE_16_UUIDS     = 0x02,
    AD_TYPE_COMPLETE_16_UUIDS       = 0x03,
    AD_TYPE_INCOMPLETE_128_UUIDS    = 0x06,
    AD_TYPE_COMPLETE_128_UUIDS      = 0x07,
    AD_TYPE_SHORTENED_NAME          = 0x08,
    AD_TYPE_COMPLETE_NAME           = 0x09,
};

// State for a single LE connection (we only support one for now).
struct ble_conn {
    u16 handle;     // connection handle from LE Connection Complete
    u16 att_mtu;    // negotiated ATT MTU (starts at ATT_MTU_DEFAULT)
    bool connected;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Bring up the controller for BLE: bt_init(), bt_upload_firmware(), then
// enable LE events and read LE buffer sizes. Must be called once.
void ble_init(void);

// ---------------------------------------------------------------------------
// Advertising (Vol 4, Part E, 7.8.5 - 7.8.9)
// ---------------------------------------------------------------------------

// Configure connectable undirected advertising parameters.
void ble_set_advertising_params(void);

// Set the advertising payload: flags + complete local name.
void ble_set_advertising_data(const char *name);

// Set scan response payload (optional; e.g. service UUID). Pass NULL/0 to skip.
void ble_set_scan_response_data(const u8 *data, u8 len);

// Start or stop advertising.
void ble_advertising_enable(bool enable);

// Convenience: params + name + enable, the common "just advertise" path.
void ble_start_advertising(const char *name);

// Advertise with the Nordic UART Service UUID in the scan response so
// nRF Connect / LightBlue show a UART tab. No custom iPhone app required.
void ble_start_nus_advertising(const char *name);

// ---------------------------------------------------------------------------
// Connection (Vol 4, Part E, 7.7.65.1)
// ---------------------------------------------------------------------------

// Block until an LE Connection Complete arrives, filling in *conn.
// Other events received while waiting are drained.
void ble_wait_for_connection(struct ble_conn *conn);

// Pump one received packet and dispatch it (events + ATT over ACL).
// Returns true if the link is still connected afterward.
bool ble_poll(struct ble_conn *conn);

// ---------------------------------------------------------------------------
// L2CAP / ATT plumbing (Vol 3, Part A + Part F)
// ---------------------------------------------------------------------------

// Send an ATT PDU to the peer over L2CAP CID 0x0004, fragmenting across HCI
// ACL packets as needed and respecting controller buffer credits.
void ble_att_send(struct ble_conn *conn, const u8 *att_pdu, u16 att_len);

// Push an unsolicited Handle Value Notification for a value attribute.
// Truncated to the negotiated ATT MTU. (Vol 3, Part F, 3.4.7.1.)
void ble_notify(struct ble_conn *conn, u16 handle, const u8 *value, u16 len);

// Register a callback invoked on every ATT write to a database attribute.
void ble_set_write_callback(ble_write_cb_t cb);

// ---------------------------------------------------------------------------
// Nordic UART Service (NUS)  -- testable with free scanner apps, no TestFlight
// ---------------------------------------------------------------------------
//
// Service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// RX (write to Pi)  6E400002-...  handle BLE_NUS_RX_HANDLE
// TX (notify from Pi) 6E400003-... handle BLE_NUS_TX_HANDLE
// CCC on TX  handle BLE_NUS_CCC_HANDLE

#define BLE_NUS_RX_HANDLE   6
#define BLE_NUS_TX_HANDLE   8
#define BLE_NUS_CCC_HANDLE  9

// True after the phone enables notifications on the TX characteristic.
bool ble_nus_tx_enabled(void);

// Push bytes to the phone over NUS TX (no-op if notifications disabled).
void ble_nus_send(struct ble_conn *conn, const u8 *data, u16 len);

#endif // BLE_H
