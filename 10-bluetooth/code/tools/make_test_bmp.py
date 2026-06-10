#!/usr/bin/env python3
"""Create a small 32x32 red BMP for BLE transfer testing."""
import struct
import sys

w, h = 32, 32
row = (w * 3 + 3) & ~3  # BMP rows are padded to 4 bytes
px = row * h
hdr, dib = 14, 40
fs = hdr + dib + px

# BMP file header (14 bytes) + DIB header (40 bytes) + BGR pixels
with open("test.bmp", "wb") as f:
    f.write(struct.pack("<2sIHHI", b"BM", fs, 0, 0, hdr + dib))
    f.write(struct.pack("<IIIHHIIIIII", dib, w, h, 1, 24, 0, px, 0, 0, 0, 0))
    f.write(b"\xff\x00\x00" * (w * h))  # red pixels (BGR)

print(f"Wrote test.bmp ({fs} bytes)")
