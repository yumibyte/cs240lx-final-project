#!/usr/bin/env python3
"""Request and receive a BMP from the Pi over BLE.

Usage:
  pip install bleak
  python3 ble_recv_bmp.py [output.bmp] [image_id]

The Pi must be running 7-ble-xfer.c with a stored image (test BMP is image 0).
Disconnect your phone from the Pi first.
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
XFER_HDR_LEN = 15

CMD_GET = 0x04
CMD_LIST = 0x05

RSP_NACK = 0x81
RSP_PUSH_CHUNK = 0x83
RSP_PUSH_DONE = 0x84
RSP_LIST = 0x85

ERR_NAMES = {
    1: "BAD_MAGIC", 2: "BAD_CMD", 3: "TOO_LARGE", 4: "NO_TRANSFER",
    5: "INCOMPLETE", 6: "STORAGE_FULL", 7: "SHORT", 8: "NO_IMAGE",
    9: "NOTIFY_OFF",
}


def make_frame(cmd: int, seq: int, total: int, offset: int) -> bytes:
    return MAGIC + bytes([cmd]) + struct.pack("<HII", seq, total, offset)


def parse_frame(data: bytes):
    if len(data) < XFER_HDR_LEN or data[:4] != MAGIC:
        return None
    cmd = data[4]
    seq, total, offset = struct.unpack("<HII", data[5:XFER_HDR_LEN])
    payload = data[XFER_HDR_LEN:]
    return cmd, seq, total, offset, payload


async def find_device(timeout=10.0):
    print(f"Scanning for '{NAME}'...")
    devices = await BleakScanner.discover(timeout=timeout)
    for d in devices:
        if d.name and NAME in d.name:
            print(f"Found {d.name} [{d.address}]")
            return d.address
    return None


async def recv_bmp(out_path: str, image_id: int):
    addr = await find_device()
    if not addr:
        print(f"Error: could not find '{NAME}'")
        sys.exit(1)

    buf = bytearray()
    expected_total = 0
    done_event = asyncio.Event()
    error = None

    def on_notify(_handle, raw: bytearray):
        nonlocal expected_total, error
        parsed = parse_frame(bytes(raw))
        if not parsed:
            print(f"  (unparsed notify {len(raw)} bytes)")
            return
        cmd, seq, total, offset, payload = parsed
        if cmd == RSP_PUSH_CHUNK:
            if offset == 0:
                buf.clear()
                expected_total = total
                print(f"  receiving {total} bytes...")
            end = offset + len(payload)
            if end > len(buf):
                buf.extend(b"\x00" * (end - len(buf)))
            buf[offset:end] = payload
            print(f"  chunk off={offset} +{len(payload)} ({min(end, expected_total)}/{expected_total})")
        elif cmd == RSP_PUSH_DONE:
            print(f"  PUSH_DONE id={offset} len={total}")
            done_event.set()
        elif cmd == RSP_LIST:
            print(f"  LIST: {total} image(s) stored on Pi")
        elif cmd == RSP_NACK:
            err = ERR_NAMES.get(offset, str(offset))
            error = f"NACK: {err}"
            print(f"  {error}")
            done_event.set()
        else:
            print(f"  rsp 0x{cmd:02x} seq={seq} total={total} offset={offset}")

    async with BleakClient(addr) as client:
        print(f"Connected to {client.address}")
        await client.start_notify(NUS_TX, on_notify)
        await asyncio.sleep(0.3)

        print("Requesting image list...")
        await client.write_gatt_char(NUS_RX, make_frame(CMD_LIST, 0, 0, 0), response=False)
        await asyncio.sleep(0.5)

        print(f"Requesting image {image_id}...")
        await client.write_gatt_char(
            NUS_RX, make_frame(CMD_GET, 1, 0, image_id), response=False
        )

        try:
            await asyncio.wait_for(done_event.wait(), timeout=60.0)
        except asyncio.TimeoutError:
            print("Timed out waiting for image")
            sys.exit(1)

        if error:
            sys.exit(1)

        if len(buf) < expected_total:
            print(f"Warning: got {len(buf)} bytes, expected {expected_total}")

        with open(out_path, "wb") as f:
            f.write(buf[:expected_total] if expected_total else buf)
        print(f"Saved {out_path} ({len(buf)} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Receive BMP from Pi over BLE")
    parser.add_argument("output", nargs="?", default="received.bmp")
    parser.add_argument("image_id", nargs="?", type=int, default=0)
    args = parser.parse_args()
    asyncio.run(recv_bmp(args.output, args.image_id))


if __name__ == "__main__":
    main()
