// BLE peripheral on the CYW43438 / BCM43430A1.
//
// Built on top of bt.c (HCI commands/events + ACL). See ble.h for the layer
// map and spec references. This file implements, bottom to top:
//
//   step 4  HCI ACL for BLE   - PB flags, fragmentation/reassembly, credits
//   step 5  L2CAP             - basic B-frame header, CID routing
//   step 6  ATT               - request/response engine over an attribute table
//
// GATT (step 7) is just the *contents* of that attribute table; ble.c ships a
// tiny default Generic Access database so this layer is testable on its own.

#include "ble.h"
#include "hci-consts.h"
#include "rpi.h"

#include <stdbool.h>

// ---------------------------------------------------------------------------
// Module state: controller buffer sizing + ACL reassembly.
// ---------------------------------------------------------------------------

static struct {
    u16 acl_max;        // max HCI ACL data bytes the controller accepts per pkt
    u16 acl_credits;    // controller LE ACL buffers currently free (flow ctrl)

    // Reassembly of one inbound L2CAP PDU across HCI ACL fragments.
    u8  rx[1024];
    u16 rx_len;         // bytes accumulated so far
    u16 rx_target;      // full PDU size we expect (l2cap_len + 4)
    bool rx_active;     // mid-reassembly

    ble_write_cb_t write_cb;
    bool nus_tx_enabled; // set when phone writes 0x0001 to the CCC descriptor
} ble;

// HCI ACL packet-boundary (PB) flags in the upper nibble of the handle field.
// (Vol 4, Part E, 5.4.2.) Host->controller LE uses START_NO_FLUSH then CONT.
#define ACL_PB_START_NO_FLUSH  0x0
#define ACL_PB_CONT            0x1

// ---------------------------------------------------------------------------
// Small helpers around bt_send_command() that wait for the matching reply.
// Mirrors the command/event pattern used in bt.c and the 4/5 test programs.
// ---------------------------------------------------------------------------

// Send a command and block until its Command Complete, asserting success.
static struct hci_event_pkt *le_cmd(struct hci_command_pkt *cmd) {
    u16 opcode = cmd->opcode;
    bt_send_command(cmd);

    // Wait for the Command Complete that echoes our opcode.
    // params: [num_cmds][opcode_lo][opcode_hi][status][...]
    while (1) {
        struct hci_event_pkt *evt = bt_receive_event();
        if (evt->event_code == EVENT_COMMAND_COMPLETE && evt->params_len >= 4) {
            u16 echoed = evt->params[1] | (evt->params[2] << 8);
            if (echoed == opcode) {
                if (evt->params[3] != 0)
                    panic("LE cmd %x failed, status=%x\n", opcode, evt->params[3]);
                return evt;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ble_init(void) {
    bt_init();
    bt_upload_firmware();

    // Enable the LE Meta event globally. (Vol 4, Part E, 7.3.1 Set Event Mask.)
    // Byte 7, bit 61 = LE Meta event. Set the default events plus LE Meta.
    {
        struct hci_command_pkt cmd = { 0 };
        cmd.opcode = CMD_SET_EVENT_MASK;
        cmd.params_len = 8;
        u8 mask[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x20 };
        for (int i = 0; i < 8; i++)
            cmd.params[i] = mask[i];
        le_cmd(&cmd);
    }

    // Enable all LE events we care about. (Vol 4, Part E, 7.8.1 LE Set Event Mask.)
    {
        struct hci_command_pkt cmd = { 0 };
        cmd.opcode = CMD_LE_SET_EVENT_MASK;
        cmd.params_len = 8;
        cmd.params[0] = 0x1f; // connection complete, adv report, conn update, etc.
        le_cmd(&cmd);
    }

    // Read LE buffer size so we know max ACL payload + how many we can queue.
    // (Vol 4, Part E, 7.8.2.) If the controller reports 0, it shares the BR/EDR
    // buffer; fall back to conservative defaults.
    {
        struct hci_command_pkt cmd = { 0 };
        cmd.opcode = CMD_LE_READ_BUFFER_SIZE;
        cmd.params_len = 0;
        struct hci_event_pkt *evt = le_cmd(&cmd);
        u16 le_data_len = evt->params[4] | (evt->params[5] << 8);
        u8 le_num_pkts = evt->params[6];

        ble.acl_max = le_data_len ? le_data_len : 27;
        ble.acl_credits = le_num_pkts ? le_num_pkts : 4;
        printk("LE buffer size: data_len=%d num_pkts=%d\n",
            ble.acl_max, ble.acl_credits);
    }
}

// ---------------------------------------------------------------------------
// Advertising
// ---------------------------------------------------------------------------

void ble_set_advertising_params(void) {
    // (Vol 4, Part E, 7.8.5 LE Set Advertising Parameters.)
    struct hci_command_pkt cmd = { 0 };
    cmd.opcode = CMD_LE_SET_ADVERTISING_PARAMETERS;
    cmd.params_len = 15;

    // Advertising interval min/max: 0x00A0 = 100ms (units of 0.625ms).
    cmd.params[0] = 0xa0;
    cmd.params[1] = 0x00;
    cmd.params[2] = 0xa0;
    cmd.params[3] = 0x00;

    cmd.params[4] = BLE_ADV_IND;     // connectable undirected
    cmd.params[5] = BLE_ADDR_PUBLIC; // own address type
    cmd.params[6] = BLE_ADDR_PUBLIC; // peer address type (unused for undirected)
    // params[7..12] peer address = 0
    cmd.params[13] = 0x07;           // advertise on all 3 channels (37/38/39)
    cmd.params[14] = 0x00;           // filter policy: allow any

    le_cmd(&cmd);
}

void ble_set_advertising_data(const char *name) {
    // (Vol 4, Part E, 7.8.7 LE Set Advertising Data.)
    // Layout: [adv_data_len(1)][ adv_data(31) ]
    // adv_data is a sequence of AD structures: [len][type][value...].
    struct hci_command_pkt cmd = { 0 };
    cmd.opcode = CMD_LE_SET_ADVERTISING_DATA;

    u8 buf[31] = { 0 };
    u8 n = 0;

    // AD: Flags = LE General Discoverable + BR/EDR not supported.
    buf[n++] = 0x02;             // length
    buf[n++] = AD_TYPE_FLAGS;
    buf[n++] = 0x06;

    // AD: Complete local name.
    unsigned name_len = 0;
    while (name[name_len])
        name_len++;
    if (name_len > 31 - n - 2)
        name_len = 31 - n - 2;
    buf[n++] = (u8)(name_len + 1); // length = type + chars
    buf[n++] = AD_TYPE_COMPLETE_NAME;
    for (unsigned i = 0; i < name_len; i++)
        buf[n++] = name[i];

    cmd.params_len = 32; // 1 length byte + 31 data bytes (fixed by spec)
    cmd.params[0] = n;   // significant adv-data length
    for (unsigned i = 0; i < 31; i++)
        cmd.params[1 + i] = buf[i];

    le_cmd(&cmd);
}

void ble_set_scan_response_data(const u8 *data, u8 len) {
    // (Vol 4, Part E, 7.8.8 LE Set Scan Response Data.)
    if (!data || len == 0)
        return;
    if (len > 31)
        len = 31;

    struct hci_command_pkt cmd = { 0 };
    cmd.opcode = CMD_LE_SET_SCAN_RESPONSE_DATA;
    cmd.params_len = 32;
    cmd.params[0] = len;
    for (u8 i = 0; i < len; i++)
        cmd.params[1 + i] = data[i];

    le_cmd(&cmd);
}

void ble_advertising_enable(bool enable) {
    // (Vol 4, Part E, 7.8.9 LE Set Advertising Enable.)
    struct hci_command_pkt cmd = { 0 };
    cmd.opcode = CMD_LE_SET_ADVERTISE_ENABLE;
    cmd.params_len = 1;
    cmd.params[0] = enable ? 0x01 : 0x00;
    le_cmd(&cmd);
}

void ble_start_advertising(const char *name) {
    ble_set_advertising_params();
    ble_set_advertising_data(name);
    ble_advertising_enable(true);
}

// Nordic UART Service UUID for scan-response advertising.
// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (little-endian byte order).
static const u8 nus_service_uuid128[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
};

void ble_start_nus_advertising(const char *name) {
    // AD structure: [len=17][type=complete 128-bit UUID][uuid*16]
    u8 scan_rsp[31] = { 0 };
    scan_rsp[0] = 17;
    scan_rsp[1] = AD_TYPE_COMPLETE_128_UUIDS;
    memcpy(scan_rsp + 2, nus_service_uuid128, 16);

    ble_set_advertising_params();
    ble_set_advertising_data(name);
    ble_set_scan_response_data(scan_rsp, 18);
    ble_advertising_enable(true);
}

// ---------------------------------------------------------------------------
// Event handling shared by the connection wait, the send path, and the poll.
// ---------------------------------------------------------------------------

// Parse an LE Connection Complete subevent into *conn.
// (Vol 4, Part E, 7.7.65.1.) params layout (after event header):
//   [subevent][status][handle_lo][handle_hi][role][peer_addr_type][addr*6]...
static bool parse_le_connection_complete(struct hci_event_pkt *evt,
                                         struct ble_conn *conn) {
    if (evt->event_code != EVENT_LE_META_EVENT || evt->params_len < 1)
        return false;

    u8 subevent = evt->params[0];
    if (subevent != LE_EVENT_CONNECTION_COMPLETE &&
        subevent != LE_EVENT_ENHANCED_CONNECTION_COMPLETE)
        return false;

    u8 status = evt->params[1];
    if (status != 0) {
        printk("LE connection failed, status=%x\n", status);
        return false;
    }

    conn->handle = evt->params[2] | (evt->params[3] << 8);
    conn->att_mtu = ATT_MTU_DEFAULT;
    conn->connected = true;
    ble.nus_tx_enabled = false;
    return true;
}

// Interpret an HCI event during the data phase: disconnects + ACL credits.
static void ble_handle_event(struct ble_conn *conn, struct hci_event_pkt *evt) {
    switch (evt->event_code) {
    case EVENT_DISCONNECTION_COMPLETE:
        conn->connected = false;
        ble.nus_tx_enabled = false;
        printk("BLE disconnected\n");
        break;

    case EVENT_NUMBER_OF_COMPLETED_PACKETS: {
        // (Vol 4, Part E, 7.7.19.) [num_handles][ handle(2) count(2) ]*
        u8 num_handles = evt->params[0];
        for (u8 i = 0; i < num_handles; i++) {
            unsigned base = 1 + i * 4;
            u16 count = evt->params[base + 2] | (evt->params[base + 3] << 8);
            ble.acl_credits += count;
        }
        break;
    }

    default:
        break;
    }
}

void ble_wait_for_connection(struct ble_conn *conn) {
    conn->connected = false;
    while (!conn->connected) {
        struct hci_event_pkt *evt = bt_receive_event();
        parse_le_connection_complete(evt, conn);
    }
    printk("BLE connected! handle=%x\n", conn->handle);
}

// ===========================================================================
// step 4: HCI ACL for BLE  (fragmentation + flow control)
// ===========================================================================

// Block until the controller frees an ACL buffer (or the link drops),
// processing events while we wait.
static void wait_for_acl_credit(struct ble_conn *conn) {
    while (ble.acl_credits == 0 && conn->connected) {
        struct hci_event_pkt *evt = bt_receive_event();
        ble_handle_event(conn, evt);
    }
}

// ===========================================================================
// step 5: L2CAP  (basic B-frame: [len(2)][cid(2)][payload])
// ===========================================================================

// Frame a payload on `cid` and send it, fragmenting across HCI ACL packets and
// respecting controller buffer credits.
static void l2cap_send(struct ble_conn *conn, u16 cid,
                       const u8 *payload, u16 len) {
    u8 frame[4 + ATT_MTU_OURS];
    if (len > sizeof(frame) - 4)
        len = sizeof(frame) - 4;

    frame[0] = len & 0xff;
    frame[1] = (len >> 8) & 0xff;
    frame[2] = cid & 0xff;
    frame[3] = (cid >> 8) & 0xff;
    memcpy(frame + 4, payload, len);

    u16 total = len + 4;
    u16 frag_max = ble.acl_max ? ble.acl_max : 27;
    u16 off = 0;
    bool first = true;

    while (off < total) {
        u16 chunk = total - off;
        if (chunk > frag_max)
            chunk = frag_max;

        wait_for_acl_credit(conn);
        if (!conn->connected)
            return;

        struct hci_acl_data_pkt acl = { 0 };
        u8 pb = first ? ACL_PB_START_NO_FLUSH : ACL_PB_CONT;
        acl.handle = (conn->handle & 0x0fff) | (pb << 12);
        acl.data_len = chunk;
        memcpy(acl.data, frame + off, chunk);

        bt_send_acl_data(&acl);
        if (ble.acl_credits)
            ble.acl_credits--;

        off += chunk;
        first = false;
    }
}

void ble_att_send(struct ble_conn *conn, const u8 *att_pdu, u16 att_len) {
    l2cap_send(conn, L2CAP_CID_ATT, att_pdu, att_len);
}

// ===========================================================================
// step 6: ATT  (request/response engine over the ble_att_db table)
// ===========================================================================

static struct att_attr *att_find(u16 handle) {
    for (u16 i = 0; i < ble_att_db_len; i++)
        if (ble_att_db[i].handle == handle)
            return &ble_att_db[i];
    return NULL;
}

// True if attribute `a`'s TYPE equals the requested UUID (2- or 16-byte).
static bool attr_type_matches(const struct att_attr *a,
                              const u8 *uuid, u8 uuid_len) {
    if (uuid_len == 2) {
        if (a->type128)
            return false;
        return a->type == (uuid[0] | (uuid[1] << 8));
    }
    if (uuid_len == 16) {
        if (a->type128)
            return memcmp(a->type128, uuid, 16) == 0;
        return false;
    }
    return false;
}

// End of the group beginning at db index `idx`: the handle just before the next
// service declaration, or the last handle in the database. (Vol 3, Part G, 2.5.)
static u16 group_end_handle(u16 idx) {
    for (u16 j = idx + 1; j < ble_att_db_len; j++) {
        struct att_attr *a = &ble_att_db[j];
        if (!a->type128 &&
            (a->type == GATT_PRIMARY_SERVICE || a->type == GATT_SECONDARY_SERVICE))
            return ble_att_db[j - 1].handle;
    }
    return ble_att_db[ble_att_db_len - 1].handle;
}

static void att_send_error(struct ble_conn *conn, u8 req_op, u16 handle, u8 err) {
    u8 pdu[5] = { ATT_ERROR_RSP, req_op, handle & 0xff, handle >> 8, err };
    ble_att_send(conn, pdu, 5);
}

// 0x02 Exchange MTU Request -> 0x03 Response. (Vol 3, Part F, 3.4.2.)
static void att_exchange_mtu(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len < 3) {
        att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 client_mtu = pdu[1] | (pdu[2] << 8);
    conn->att_mtu = client_mtu < ATT_MTU_OURS ? client_mtu : ATT_MTU_OURS;
    if (conn->att_mtu < ATT_MTU_DEFAULT)
        conn->att_mtu = ATT_MTU_DEFAULT;

    u8 rsp[3] = { ATT_EXCHANGE_MTU_RSP, ATT_MTU_OURS & 0xff, ATT_MTU_OURS >> 8 };
    ble_att_send(conn, rsp, 3);
}

// 0x10 Read By Group Type Request -> 0x11 Response. Used to discover services.
// (Vol 3, Part F, 3.4.4.9.)
static void att_read_by_group(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len != 7 && len != 21) {
        att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 start = pdu[1] | (pdu[2] << 8);
    u16 end   = pdu[3] | (pdu[4] << 8);
    const u8 *grp = &pdu[5];
    u8 grp_len = len - 5;

    // Only the (primary|secondary) service group types are valid here.
    u16 grp16 = (grp_len == 2) ? (grp[0] | (grp[1] << 8)) : 0;
    if (grp_len != 2 ||
        (grp16 != GATT_PRIMARY_SERVICE && grp16 != GATT_SECONDARY_SERVICE)) {
        att_send_error(conn, pdu[0], start, ATT_ERR_UNSUPPORTED_GROUP);
        return;
    }

    u8 rsp[ATT_MTU_OURS];
    u16 rlen = 0;
    u16 entry_len = 0;
    rsp[rlen++] = ATT_READ_BY_GROUP_RSP;
    rsp[rlen++] = 0; // per-entry length, filled below

    for (u16 i = 0; i < ble_att_db_len; i++) {
        struct att_attr *a = &ble_att_db[i];
        if (a->handle < start || a->handle > end)
            continue;
        if (a->type128 || a->type != grp16)
            continue;

        u16 this_len = 4 + a->value_len; // handle + end + value
        if (entry_len == 0)
            entry_len = this_len;
        if (this_len != entry_len)
            break; // spec: one length per response
        if (rlen + entry_len > conn->att_mtu)
            break;

        u16 eg = group_end_handle(i);
        rsp[rlen++] = a->handle & 0xff;
        rsp[rlen++] = a->handle >> 8;
        rsp[rlen++] = eg & 0xff;
        rsp[rlen++] = eg >> 8;
        memcpy(rsp + rlen, a->value, a->value_len);
        rlen += a->value_len;
    }

    if (entry_len == 0) {
        att_send_error(conn, pdu[0], start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    rsp[1] = (u8)entry_len;
    ble_att_send(conn, rsp, rlen);
}

// 0x08 Read By Type Request -> 0x09 Response. Used to discover characteristics
// (type 0x2803) or read a value by UUID. (Vol 3, Part F, 3.4.4.1.)
static void att_read_by_type(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len != 7 && len != 21) {
        att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 start = pdu[1] | (pdu[2] << 8);
    u16 end   = pdu[3] | (pdu[4] << 8);
    const u8 *type = &pdu[5];
    u8 type_len = len - 5;

    u8 rsp[ATT_MTU_OURS];
    u16 rlen = 0;
    u16 entry_len = 0;
    rsp[rlen++] = ATT_READ_BY_TYPE_RSP;
    rsp[rlen++] = 0; // per-entry length, filled below

    for (u16 i = 0; i < ble_att_db_len; i++) {
        struct att_attr *a = &ble_att_db[i];
        if (a->handle < start || a->handle > end)
            continue;
        if (!attr_type_matches(a, type, type_len))
            continue;
        if (!(a->perms & ATT_PERM_READ))
            continue;

        u16 this_len = 2 + a->value_len; // handle + value
        if (entry_len == 0)
            entry_len = this_len;
        if (this_len != entry_len)
            break;
        if (rlen + entry_len > conn->att_mtu)
            break;

        rsp[rlen++] = a->handle & 0xff;
        rsp[rlen++] = a->handle >> 8;
        memcpy(rsp + rlen, a->value, a->value_len);
        rlen += a->value_len;
    }

    if (entry_len == 0) {
        att_send_error(conn, pdu[0], start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    rsp[1] = (u8)entry_len;
    ble_att_send(conn, rsp, rlen);
}

// 0x04 Find Information Request -> 0x05 Response. Used to discover descriptors.
// (Vol 3, Part F, 3.4.3.1.)
static void att_find_info(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len != 5) {
        att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 start = pdu[1] | (pdu[2] << 8);
    u16 end   = pdu[3] | (pdu[4] << 8);

    u8 rsp[ATT_MTU_OURS];
    u16 rlen = 0;
    u8 fmt = 0; // 0x01 = 16-bit UUIDs, 0x02 = 128-bit UUIDs
    rsp[rlen++] = ATT_FIND_INFO_RSP;
    rsp[rlen++] = 0; // format, filled below

    for (u16 i = 0; i < ble_att_db_len; i++) {
        struct att_attr *a = &ble_att_db[i];
        if (a->handle < start || a->handle > end)
            continue;

        u8 this_fmt = a->type128 ? 0x02 : 0x01;
        if (fmt == 0)
            fmt = this_fmt;
        if (this_fmt != fmt)
            break; // one format per response

        u16 entry = 2 + (fmt == 0x01 ? 2 : 16);
        if (rlen + entry > conn->att_mtu)
            break;

        rsp[rlen++] = a->handle & 0xff;
        rsp[rlen++] = a->handle >> 8;
        if (fmt == 0x01) {
            rsp[rlen++] = a->type & 0xff;
            rsp[rlen++] = a->type >> 8;
        } else {
            memcpy(rsp + rlen, a->type128, 16);
            rlen += 16;
        }
    }

    if (fmt == 0) {
        att_send_error(conn, pdu[0], start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    rsp[1] = fmt;
    ble_att_send(conn, rsp, rlen);
}

// 0x0A Read Request -> 0x0B Response. (Vol 3, Part F, 3.4.4.3.)
static void att_read(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len != 3) {
        att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 handle = pdu[1] | (pdu[2] << 8);
    struct att_attr *a = att_find(handle);
    if (!a) {
        att_send_error(conn, pdu[0], handle, ATT_ERR_INVALID_HANDLE);
        return;
    }
    if (!(a->perms & ATT_PERM_READ)) {
        att_send_error(conn, pdu[0], handle, ATT_ERR_READ_NOT_PERMITTED);
        return;
    }

    u8 rsp[ATT_MTU_OURS];
    rsp[0] = ATT_READ_RSP;
    u16 n = a->value_len;
    if (n > conn->att_mtu - 1)
        n = conn->att_mtu - 1; // remainder fetched via Read Blob
    memcpy(rsp + 1, a->value, n);
    ble_att_send(conn, rsp, n + 1);
}

// 0x0C Read Blob Request -> 0x0D Response. Long reads from an offset.
// (Vol 3, Part F, 3.4.4.5.)
static void att_read_blob(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len != 5) {
        att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 handle = pdu[1] | (pdu[2] << 8);
    u16 offset = pdu[3] | (pdu[4] << 8);
    struct att_attr *a = att_find(handle);
    if (!a) {
        att_send_error(conn, pdu[0], handle, ATT_ERR_INVALID_HANDLE);
        return;
    }
    if (!(a->perms & ATT_PERM_READ)) {
        att_send_error(conn, pdu[0], handle, ATT_ERR_READ_NOT_PERMITTED);
        return;
    }
    if (offset > a->value_len) {
        att_send_error(conn, pdu[0], handle, ATT_ERR_INVALID_OFFSET);
        return;
    }

    u8 rsp[ATT_MTU_OURS];
    rsp[0] = ATT_READ_BLOB_RSP;
    u16 n = a->value_len - offset;
    if (n > conn->att_mtu - 1)
        n = conn->att_mtu - 1;
    memcpy(rsp + 1, a->value + offset, n);
    ble_att_send(conn, rsp, n + 1);
}

// 0x12 Write Request / 0x52 Write Command. (Vol 3, Part F, 3.4.5.)
static void att_write(struct ble_conn *conn, const u8 *pdu, u16 len, bool need_rsp) {
    if (len < 3) {
        if (need_rsp)
            att_send_error(conn, pdu[0], 0, ATT_ERR_INVALID_PDU);
        return;
    }
    u16 handle = pdu[1] | (pdu[2] << 8);
    const u8 *val = &pdu[3];
    u16 vlen = len - 3;

    struct att_attr *a = att_find(handle);
    if (!a) {
        if (need_rsp)
            att_send_error(conn, pdu[0], handle, ATT_ERR_INVALID_HANDLE);
        return;
    }
    if (!(a->perms & ATT_PERM_WRITE)) {
        if (need_rsp)
            att_send_error(conn, pdu[0], handle, ATT_ERR_WRITE_NOT_PERMITTED);
        return;
    }

    // Store into the attribute value if it has room (e.g. CCC descriptor).
    if (a->value && a->max_len) {
        u16 n = vlen < a->max_len ? vlen : a->max_len;
        memcpy(a->value, val, n);
        a->value_len = n;
    }

    // Track NUS TX notifications (CCC = Client Characteristic Configuration).
    if (handle == BLE_NUS_CCC_HANDLE && vlen >= 2)
        ble.nus_tx_enabled = (val[0] & 0x01) != 0;

    // Hand the raw write to the app (e.g. image chunk assembly).
    if (ble.write_cb)
        ble.write_cb(handle, val, vlen);

    if (need_rsp) {
        u8 rsp = ATT_WRITE_RSP;
        ble_att_send(conn, &rsp, 1);
    }
}

// Dispatch a received ATT PDU (the L2CAP payload on CID 0x0004).
static void ble_handle_att(struct ble_conn *conn, const u8 *pdu, u16 len) {
    if (len < 1)
        return;
    u8 op = pdu[0];
    switch (op) {
    case ATT_EXCHANGE_MTU_REQ:  att_exchange_mtu(conn, pdu, len);   break;
    case ATT_FIND_INFO_REQ:     att_find_info(conn, pdu, len);      break;
    case ATT_READ_BY_TYPE_REQ:  att_read_by_type(conn, pdu, len);   break;
    case ATT_READ_BY_GROUP_REQ: att_read_by_group(conn, pdu, len);  break;
    case ATT_READ_REQ:          att_read(conn, pdu, len);           break;
    case ATT_READ_BLOB_REQ:     att_read_blob(conn, pdu, len);      break;
    case ATT_WRITE_REQ:         att_write(conn, pdu, len, true);    break;
    case ATT_WRITE_CMD:         att_write(conn, pdu, len, false);   break;
    default:
        // Confirmations carry no response; everything else we reject cleanly.
        if (op != ATT_HANDLE_VALUE_CFM)
            att_send_error(conn, op, 0, ATT_ERR_REQUEST_NOT_SUPPORTED);
        break;
    }
}

void ble_notify(struct ble_conn *conn, u16 handle, const u8 *value, u16 len) {
    u8 pdu[ATT_MTU_OURS];
    if (len > conn->att_mtu - 3)
        len = conn->att_mtu - 3;
    pdu[0] = ATT_HANDLE_VALUE_NTF;
    pdu[1] = handle & 0xff;
    pdu[2] = handle >> 8;
    memcpy(pdu + 3, value, len);
    ble_att_send(conn, pdu, len + 3);
}

void ble_set_write_callback(ble_write_cb_t cb) {
    ble.write_cb = cb;
}

bool ble_nus_tx_enabled(void) {
    return ble.nus_tx_enabled;
}

void ble_nus_send(struct ble_conn *conn, const u8 *data, u16 len) {
    if (!ble.nus_tx_enabled)
        return;
    ble_notify(conn, BLE_NUS_TX_HANDLE, data, len);
}

// ===========================================================================
// step 4/5 receive path: HCI ACL fragments -> reassembled L2CAP PDU -> CID
// ===========================================================================

static void ble_handle_acl(struct ble_conn *conn, struct hci_acl_data_pkt *acl) {
    u8 pb = (acl->handle >> 12) & 0x3;
    bool is_start = (pb != ACL_PB_CONT); // continuation == 0b01; else a new PDU

    if (is_start) {
        if (acl->data_len < 4)
            return; // too short to hold the L2CAP header
        u16 l2cap_len = acl->data[0] | (acl->data[1] << 8);
        ble.rx_target = l2cap_len + 4;
        ble.rx_len = 0;
        ble.rx_active = true;
    }
    if (!ble.rx_active)
        return;

    if (ble.rx_len + acl->data_len > sizeof(ble.rx)) {
        ble.rx_active = false; // overflow; drop this PDU
        return;
    }
    memcpy(ble.rx + ble.rx_len, acl->data, acl->data_len);
    ble.rx_len += acl->data_len;

    if (ble.rx_len < ble.rx_target)
        return; // need more fragments

    u16 cid = ble.rx[2] | (ble.rx[3] << 8);
    const u8 *payload = ble.rx + 4;
    u16 plen = ble.rx_target - 4;
    ble.rx_active = false;

    if (cid == L2CAP_CID_ATT) {
        ble_handle_att(conn, payload, plen);
    } else if (cid == L2CAP_CID_SMP) {
        // Pairing/encryption. Our characteristics don't require it, so iOS
        // won't normally initiate this; ignore for now.
        printk("BLE: ignoring SMP packet\n");
    } else {
        printk("BLE: ignoring L2CAP CID %x\n", cid);
    }
}

bool ble_poll(struct ble_conn *conn) {
    struct hci_event_pkt *evt = bt_receive_event_async();
    if (evt)
        ble_handle_event(conn, evt);

    struct hci_acl_data_pkt *acl = bt_receive_acl_async();
    if (acl)
        ble_handle_acl(conn, acl);

    return conn->connected;
}

// ===========================================================================
// GATT attribute database (step 7).
//
// Two services:
//   0x1800 Generic Access  - device name (required for scanners)
//   NUS (128-bit UUID)     - UART bridge testable with nRF Connect / LightBlue
//
// Handle map:
//   1  GAP primary service
//   2  Device Name declaration
//   3  Device Name value
//   4  NUS primary service
//   5  NUS RX declaration   (write / write-without-response)
//   6  NUS RX value         <-- phone writes here
//   7  NUS TX declaration   (notify)
//   8  NUS TX value         <-- Pi notifies here
//   9  CCC descriptor       <-- phone enables notifications here
// ===========================================================================

static u8 gap_service_uuid[] = { 0x00, 0x18 };
static u8 gap_devname_decl[] = {
    CHAR_PROP_READ, 0x03, 0x00, 0x00, 0x2A,
};
static u8 gap_devname_value[] = "cs240lx-pi";

// 128-bit UUIDs, little-endian (Bluetooth base UUID byte order).
static u8 nus_svc_uuid[] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
};
static u8 nus_rx_uuid[] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e,
};
static u8 nus_tx_uuid[] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e,
};

// Characteristic declarations: [props][value_handle_lo][value_handle_hi][uuid*16]
static u8 nus_rx_decl[] = {
    CHAR_PROP_WRITE | CHAR_PROP_WRITE_NR,
    BLE_NUS_RX_HANDLE & 0xff, BLE_NUS_RX_HANDLE >> 8,
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e,
};
static u8 nus_tx_decl[] = {
    CHAR_PROP_NOTIFY,
    BLE_NUS_TX_HANDLE & 0xff, BLE_NUS_TX_HANDLE >> 8,
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e,
};

static u8 nus_rx_value[244]; // writable RX buffer (one ATT MTU-ish chunk)
static u8 nus_tx_value[1];   // notify-only; value not meaningfully read
static u8 nus_ccc_value[2];  // 0x0001 = notifications enabled

struct att_attr ble_att_db[] = {
    // Generic Access
    { 1, GATT_PRIMARY_SERVICE, NULL, gap_service_uuid,
      sizeof(gap_service_uuid), sizeof(gap_service_uuid), ATT_PERM_READ },
    { 2, GATT_CHARACTERISTIC,  NULL, gap_devname_decl,
      sizeof(gap_devname_decl), sizeof(gap_devname_decl), ATT_PERM_READ },
    { 3, 0x2A00,               NULL, gap_devname_value,
      sizeof(gap_devname_value) - 1, sizeof(gap_devname_value) - 1, ATT_PERM_READ },

    // Nordic UART Service
    { 4, GATT_PRIMARY_SERVICE, NULL, nus_svc_uuid,
      sizeof(nus_svc_uuid), sizeof(nus_svc_uuid), ATT_PERM_READ },
    { 5, GATT_CHARACTERISTIC,  NULL, nus_rx_decl,
      sizeof(nus_rx_decl), sizeof(nus_rx_decl), ATT_PERM_READ },
    { 6, 0, nus_rx_uuid, nus_rx_value,
      0, sizeof(nus_rx_value), ATT_PERM_WRITE },
    { 7, GATT_CHARACTERISTIC,  NULL, nus_tx_decl,
      sizeof(nus_tx_decl), sizeof(nus_tx_decl), ATT_PERM_READ },
    { 8, 0, nus_tx_uuid, nus_tx_value,
      sizeof(nus_tx_value), sizeof(nus_tx_value), ATT_PERM_READ },
    { 9, GATT_CCC_DESCRIPTOR,  NULL, nus_ccc_value,
      sizeof(nus_ccc_value), sizeof(nus_ccc_value), ATT_PERM_READ | ATT_PERM_WRITE },
};
u16 ble_att_db_len = sizeof(ble_att_db) / sizeof(ble_att_db[0]);
