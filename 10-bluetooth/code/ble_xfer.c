// Binary image transfer over BLE NUS. See ble_xfer.h for the frame format.

#include "ble_xfer.h"
#include "rpi.h"

#include <stdbool.h>

static const u8 XFER_MAGIC[4] = { 'C', 'S', '2', '4' };

static u8 active_buf[XFER_MAX_IMAGE_BYTES];

static struct {
    u8 *buf;        // points at active_buf while a transfer is in progress
    u32 total_len;
    u32 high_water; // highest byte offset written (offset + payload len)

    struct {
        u8 *data;   // kmalloc'd; no free in this heap -- max XFER_MAX_IMAGES total
        u32 len;
        bool valid;
    } images[XFER_MAX_IMAGES];

    u8 image_count;

    // Incremental Pi->phone push (started by GET, driven from ble_xfer_poll).
    struct {
        bool active;
        u16 reply_seq;
        u8 image_id;
        u32 offset;
        u32 total;
        u16 push_seq;
        u16 chunk;
        u32 next_report;
        const u8 *data;
    } push;
} xfer;

static u16 read16(const u8 *p) {
    return p[0] | (p[1] << 8);
}

static u32 read32(const u8 *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static void write16(u8 *p, u16 v) {
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void write32(u8 *p, u32 v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

static void xfer_nack(struct ble_conn *conn, u16 seq, enum xfer_err err);

static void xfer_free_active(void) {
    xfer.buf = NULL;
    xfer.total_len = 0;
    xfer.high_water = 0;
}

static void xfer_send_frame(struct ble_conn *conn, u8 cmd, u16 seq,
                            u32 total, u32 offset,
                            const u8 *payload, u16 plen) {
    u8 frame[XFER_HDR_LEN + 200];
    if (plen > 200)
        plen = 200;

    memcpy(frame, XFER_MAGIC, 4);
    frame[4] = cmd;
    write16(frame + 5, seq);
    write32(frame + 7, total);
    write32(frame + 11, offset);
    if (plen)
        memcpy(frame + XFER_HDR_LEN, payload, plen);
    ble_nus_send(conn, frame, XFER_HDR_LEN + plen);
}

static void xfer_send(struct ble_conn *conn, u8 rsp, u16 seq,
                      u32 total, u32 offset) {
    xfer_send_frame(conn, rsp, seq, total, offset, NULL, 0);
}

// Max payload per PUSH_CHUNK notify (fits in negotiated ATT MTU).
static u16 xfer_push_payload_max(struct ble_conn *conn) {
    u16 mtu = conn->att_mtu;
    if (mtu < ATT_MTU_DEFAULT)
        mtu = ATT_MTU_DEFAULT;
    if (mtu <= XFER_HDR_LEN + 3)
        return 1;
    return mtu - 3 - XFER_HDR_LEN;
}

static void xfer_push_begin(struct ble_conn *conn, u16 seq, u8 image_id) {
    if (!ble_nus_tx_enabled()) {
        xfer_nack(conn, seq, XFER_ERR_NOTIFY_OFF);
        return;
    }
    if (image_id >= XFER_MAX_IMAGES || !xfer.images[image_id].valid) {
        xfer_nack(conn, seq, XFER_ERR_NO_IMAGE);
        return;
    }
    if (xfer.push.active) {
        xfer_nack(conn, seq, XFER_ERR_NO_TRANSFER);
        return;
    }

    xfer.push.active = true;
    xfer.push.reply_seq = seq;
    xfer.push.image_id = image_id;
    xfer.push.offset = 0;
    xfer.push.push_seq = 0;
    xfer.push.total = xfer.images[image_id].len;
    xfer.push.data = xfer.images[image_id].data;
    xfer.push.chunk = xfer_push_payload_max(conn);
    xfer.push.next_report = 0;

    printk("xfer: pushing image id=%d, %d bytes, chunk=%d\n",
        image_id, xfer.push.total, xfer.push.chunk);
}

// Drive an in-progress push a few chunks at a time so ble_poll() keeps running
// in the main loop (HCI events + disconnect detection stay live).
void ble_xfer_poll(struct ble_conn *conn) {
    if (!xfer.push.active)
        return;
    if (!conn->connected) {
        printk("xfer: push aborted at %d/%d (disconnected)\n",
            xfer.push.offset, xfer.push.total);
        xfer.push.active = false;
        return;
    }
    if (!ble_nus_tx_enabled()) {
        xfer.push.active = false;
        return;
    }

    for (int i = 0; i < 4 && xfer.push.offset < xfer.push.total; i++) {
        u32 remaining = xfer.push.total - xfer.push.offset;
        u16 n = (remaining > xfer.push.chunk)
            ? xfer.push.chunk : (u16)remaining;

        xfer_send_frame(conn, XFER_RSP_PUSH_CHUNK, xfer.push.push_seq,
            xfer.push.total, xfer.push.offset,
            xfer.push.data + xfer.push.offset, n);

        xfer.push.offset += n;
        xfer.push.push_seq++;

        if (xfer.push.offset >= xfer.push.next_report) {
            printk("xfer: push %d/%d bytes\n",
                xfer.push.offset, xfer.push.total);
            xfer.push.next_report = xfer.push.offset + 65536;
        }
    }

    if (xfer.push.offset >= xfer.push.total) {
        xfer_send(conn, XFER_RSP_PUSH_DONE, xfer.push.reply_seq,
            xfer.push.total, xfer.push.image_id);
        printk("xfer: push done id=%d\n", xfer.push.image_id);
        xfer.push.active = false;
    }
}

static void xfer_nack(struct ble_conn *conn, u16 seq, enum xfer_err err) {
    printk("xfer: NACK seq=%d err=%d\n", seq, err);
    xfer_send(conn, XFER_RSP_NACK, seq, 0, err);
}

static int xfer_find_free_slot(void) {
    for (int i = 0; i < XFER_MAX_IMAGES; i++)
        if (!xfer.images[i].valid)
            return i;
    return -1;
}

void ble_xfer_init(void) {
    xfer_free_active();
    xfer.push.active = false;
    for (int i = 0; i < XFER_MAX_IMAGES; i++) {
        xfer.images[i].data = NULL;
        xfer.images[i].len = 0;
        xfer.images[i].valid = false;
    }
    xfer.image_count = 0;
}

int ble_xfer_add_image(const u8 *data, u32 len) {
    int slot = xfer_find_free_slot();
    if (slot < 0)
        return -1;

    u8 *buf = kmalloc(len);
    if (!buf)
        return -1;
    memcpy(buf, data, len);

    xfer.images[slot].data = buf;
    xfer.images[slot].len = len;
    xfer.images[slot].valid = true;
    xfer.image_count++;
    return slot;
}

// Build the same 32x32 red BMP as tools/make_test_bmp.py.
void ble_xfer_add_test_bmp(void) {
    int slot = xfer_find_free_slot();
    if (slot < 0) {
        printk("xfer: no slot for test bmp\n");
        return;
    }

    u32 w = 32, h = 32;
    u32 row = (w * 3 + 3) & ~3u;
    u32 px = row * h;
    u32 fs = 14 + 40 + px;

    u8 *buf = kmalloc(fs);
    if (!buf) {
        printk("xfer: kmalloc failed for test bmp\n");
        return;
    }

    u32 off = 0;
    buf[off++] = 'B'; buf[off++] = 'M';
    write32(buf + off, fs); off += 4;
    write16(buf + off, 0); off += 2;
    write16(buf + off, 0); off += 2;
    write32(buf + off, 54); off += 4;

    write32(buf + off, 40); off += 4;
    write32(buf + off, w); off += 4;
    write32(buf + off, h); off += 4;
    write16(buf + off, 1); off += 2;
    write16(buf + off, 24); off += 2;
    write32(buf + off, 0); off += 4;
    write32(buf + off, px); off += 4;
    write32(buf + off, 0); off += 4;
    write32(buf + off, 0); off += 4;
    write32(buf + off, 0); off += 4;
    write32(buf + off, 0); off += 4;

    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            buf[off++] = 0xff; // B
            buf[off++] = 0x00; // G
            buf[off++] = 0x00; // R
        }
        for (u32 p = w * 3; p < row; p++)
            buf[off++] = 0;
    }

    xfer.images[slot].data = buf;
    xfer.images[slot].len = fs;
    xfer.images[slot].valid = true;
    xfer.image_count++;
    printk("xfer: test BMP stored as image id=%d (%d bytes)\n", slot, fs);
}

void ble_xfer_handle(struct ble_conn *conn, const u8 *data, u16 len) {
    if (len < XFER_HDR_LEN) {
        xfer_nack(conn, 0, XFER_ERR_SHORT);
        return;
    }
    if (memcmp(data, XFER_MAGIC, 4) != 0) {
        xfer_nack(conn, read16(data + 5), XFER_ERR_BAD_MAGIC);
        return;
    }

    u8 cmd = data[4];
    u16 seq = read16(data + 5);
    u32 total = read32(data + 7);
    u32 offset = read32(data + 11);
    const u8 *payload = data + XFER_HDR_LEN;
    u16 plen = len - XFER_HDR_LEN;

    switch (cmd) {
    case XFER_CMD_RESET:
        xfer_free_active();
        printk("xfer: reset\n");
        xfer_send(conn, XFER_RSP_ACK, seq, 0, 0);
        return;

    case XFER_CMD_CHUNK: {
        if (total == 0 || total > XFER_MAX_IMAGE_BYTES) {
            xfer_nack(conn, seq, XFER_ERR_TOO_LARGE);
            return;
        }
        if (offset + plen > total) {
            xfer_nack(conn, seq, XFER_ERR_TOO_LARGE);
            return;
        }

        // offset==0 starts a new in-progress image
        if (offset == 0) {
            xfer_free_active();
            xfer.buf = active_buf;
            xfer.total_len = total;
            xfer.high_water = 0;
            printk("xfer: start image, total=%d bytes\n", total);
        }

        if (!xfer.buf) {
            xfer_nack(conn, seq, XFER_ERR_NO_TRANSFER);
            return;
        }
        if (total != xfer.total_len) {
            xfer_nack(conn, seq, XFER_ERR_NO_TRANSFER);
            return;
        }

        memcpy(xfer.buf + offset, payload, plen);
        u32 end = offset + plen;
        if (end > xfer.high_water)
            xfer.high_water = end;

        printk("xfer: chunk seq=%d off=%d +%d (%d/%d)\n",
            seq, offset, plen, xfer.high_water, xfer.total_len);
        xfer_send(conn, XFER_RSP_ACK, seq, xfer.high_water, offset);
        return;
    }

    case XFER_CMD_COMMIT: {
        if (!xfer.buf) {
            xfer_nack(conn, seq, XFER_ERR_NO_TRANSFER);
            return;
        }
        if (xfer.high_water < xfer.total_len) {
            xfer_nack(conn, seq, XFER_ERR_INCOMPLETE);
            return;
        }

        int slot = xfer_find_free_slot();
        if (slot < 0) {
            xfer_nack(conn, seq, XFER_ERR_STORAGE_FULL);
            return;
        }

        // Basic BMP sanity check (optional -- warn only).
        if (xfer.total_len >= 2 &&
            (xfer.buf[0] != 'B' || xfer.buf[1] != 'M'))
            printk("xfer: warning: not a BMP header\n");

        u8 *stored = kmalloc(xfer.total_len);
        if (!stored) {
            xfer_nack(conn, seq, XFER_ERR_STORAGE_FULL);
            return;
        }
        memcpy(stored, active_buf, xfer.total_len);

        xfer.images[slot].data = stored;
        xfer.images[slot].len = xfer.total_len;
        xfer.images[slot].valid = true;
        xfer_free_active();
        xfer.image_count++;

        printk("xfer: DONE image id=%d, len=%d (total stored=%d)\n",
            slot, xfer.images[slot].len, xfer.image_count);
        xfer_send(conn, XFER_RSP_DONE, seq, xfer.images[slot].len, slot);
        return;
    }

    case XFER_CMD_GET: {
        u8 image_id = (u8)offset;
        xfer_push_begin(conn, seq, image_id);
        return;
    }

    case XFER_CMD_LIST: {
        u8 n = ble_xfer_image_count();
        printk("xfer: LIST -> %d images\n", n);
        xfer_send(conn, XFER_RSP_LIST, seq, n, 0);
        return;
    }

    default:
        xfer_nack(conn, seq, XFER_ERR_BAD_CMD);
        return;
    }
}

u8 ble_xfer_image_count(void) {
    u8 n = 0;
    for (int i = 0; i < XFER_MAX_IMAGES; i++)
        if (xfer.images[i].valid)
            n++;
    return n;
}

const u8 *ble_xfer_image_data(u8 id, u32 *out_len) {
    if (id >= XFER_MAX_IMAGES || !xfer.images[id].valid)
        return NULL;
    if (out_len)
        *out_len = xfer.images[id].len;
    return xfer.images[id].data;
}
