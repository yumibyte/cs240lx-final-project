#!/usr/bin/env python3
"""Send a BMP file to the Pi over BLE Nordic UART Service (CS24 protocol).

Usage:
  pip install bleak
  python3 ble_send_bmp.py path/to/image.bmp

The Pi must be running 7-ble-xfer.c and advertising as 'cs240lx-pi'.
Disconnect your phone from the Pi first -- only one BLE central at a time.
"""

import argparse
import asyncio
import struct
import sys

from bleak import BleakClient, BleakScanner

NAME = "cs240lx-pi"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

MAGIC = b"CS24"
XFER_HDR_LEN = 15  # magic(4)+cmd(1)+seq(2)+total(4)+offset(4)
CMD_CHUNK = 0x01
CMD_COMMIT = 0x02
RSP_ACK = 0x80
RSP_NACK = 0x81
RSP_DONE = 0x82

ERR_NAMES = {
    1: "BAD_MAGIC",
    2: "BAD_CMD",
    3: "TOO_LARGE",
    4: "NO_TRANSFER",
    5: "INCOMPLETE",
    6: "STORAGE_FULL",
    7: "SHORT",
}

CHUNK_PAYLOAD = 200  # bytes per frame (fits in ATT MTU with 15-byte header)


def make_frame(cmd: int, seq: int, total: int, offset: int, payload: bytes = b"") -> bytes:
    hdr = MAGIC + bytes([cmd]) + struct.pack("<HII", seq, total, offset)
    assert len(hdr) == XFER_HDR_LEN
    return hdr + payload


def parse_frame(data: bytes):
    if len(data) < XFER_HDR_LEN or data[:4] != MAGIC:
        return None
    cmd = data[4]
    seq, total, offset = struct.unpack("<HII", data[5:XFER_HDR_LEN])
    return cmd, seq, total, offset


def on_notify(_handle, data: bytearray):
    parsed = parse_frame(bytes(data))
    if not parsed:
        print(f"  notify (raw {len(data)} bytes): {data[:20]!r}...")
        return
    cmd, seq, total, offset = parsed
    if cmd == RSP_ACK:
        print(f"  ACK  seq={seq} progress={total} bytes")
    elif cmd == RSP_DONE:
        print(f"  DONE seq={seq} len={total} image_id={offset}")
    elif cmd == RSP_NACK:
        err = ERR_NAMES.get(offset, str(offset))
        print(f"  NACK seq={seq} err={err}")
    else:
        print(f"  rsp cmd=0x{cmd:02x} seq={seq} total={total} offset={offset}")


async def find_device(timeout=10.0):
    print(f"Scanning for '{NAME}'...")
    devices = await BleakScanner.discover(timeout=timeout)
    for d in devices:
        if d.name and NAME in d.name:
            print(f"Found {d.name} [{d.address}]")
            return d.address
    # fallback: partial name match
    for d in devices:
        if d.name and "cs240lx" in d.name.lower():
            print(f"Found {d.name} [{d.address}]")
            return d.address
    return None


async def send_bmp(path: str):
    data = open(path, "rb").read()
    if len(data) == 0:
        print(f"Error: {path} is empty. Run: python3 make_test_bmp.py")
        sys.exit(1)
    if len(data) > 32 * 1024:
        print(f"Error: file too large ({len(data)} bytes, max 32KB)")
        sys.exit(1)
    if len(data) >= 2 and data[:2] != b"BM":
        print("Warning: file does not start with BM (not a BMP?)")

    addr = await find_device()
    if not addr:
        print(f"Error: could not find '{NAME}'. Is the Pi running 7-ble-xfer?")
        sys.exit(1)

    done_event = asyncio.Event()
    done_info = {}

    def notify_handler(_handle, raw: bytearray):
        on_notify(_handle, raw)
        parsed = parse_frame(bytes(raw))
        if parsed and parsed[0] == RSP_DONE:
            done_info["len"] = parsed[2]
            done_info["id"] = parsed[3]
            done_event.set()

    async with BleakClient(addr) as client:
        print(f"Connected to {client.address}")
        await client.start_notify(NUS_TX, notify_handler)
        await asyncio.sleep(0.3)  # let ATT MTU exchange finish

        # ATT write value max = MTU - 3 (opcode + handle). Default MTU is 23
        # so only 20 bytes fit unless the Pi/Mac negotiate higher.
        mtu = client.mtu_size or 23
        payload_max = mtu - 3 - XFER_HDR_LEN
        if payload_max < 1:
            payload_max = 1
        chunk_size = min(CHUNK_PAYLOAD, payload_max)
        print(f"MTU={mtu}, using {chunk_size}-byte payloads ({XFER_HDR_LEN}-byte header)")

        total = len(data)
        seq = 0
        offset = 0
        print(f"Sending {total} bytes...")

        while offset < total:
            chunk = data[offset : offset + chunk_size]
            frame = make_frame(CMD_CHUNK, seq, total, offset, chunk)
            await client.write_gatt_char(NUS_RX, frame, response=False)
            offset += len(chunk)
            seq += 1
            await asyncio.sleep(0.02)

        print("Sending COMMIT...")
        await client.write_gatt_char(
            NUS_RX, make_frame(CMD_COMMIT, seq, 0, 0), response=False
        )

        try:
            await asyncio.wait_for(done_event.wait(), timeout=10.0)
            print(
                f"Success! Pi stored image id={done_info.get('id')} "
                f"len={done_info.get('len')}"
            )
        except asyncio.TimeoutError:
            print("Timed out waiting for DONE (check Pi serial terminal)")

        await client.stop_notify(NUS_TX)


def main():
    parser = argparse.ArgumentParser(description="Send BMP to Pi over BLE")
    parser.add_argument("bmp", help="path to .bmp file")
    args = parser.parse_args()
    asyncio.run(send_bmp(args.bmp))


if __name__ == "__main__":
    main()
