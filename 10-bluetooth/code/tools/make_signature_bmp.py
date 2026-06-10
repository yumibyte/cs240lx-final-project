#!/usr/bin/env python3
"""Create a small black-on-white signature BMP for BLE transfer tests.

Writes signature.bmp (128x64, 24-bit BMP).
"""

import struct
import math

W, H = 128, 64
OUT = "signature.bmp"


def write_bmp(path: str, pixels: list) -> None:
    """pixels[y][x] = (r, g, b)"""
    row = (W * 3 + 3) & ~3
    px_size = row * H
    fs = 14 + 40 + px_size

    out = bytearray()
    out += struct.pack("<2sIHHI", b"BM", fs, 0, 0, 14 + 40)
    out += struct.pack(
        "<IIIHHIIIIII",
        40, W, H, 1, 24, 0, px_size, 0, 0, 0, 0,
    )

    for y in range(H - 1, -1, -1):  # BMP rows are bottom-up
        row_bytes = bytearray()
        for x in range(W):
            r, g, b = pixels[y][x]
            row_bytes += bytes([b, g, r])  # BMP is BGR
        while len(row_bytes) < row:
            row_bytes.append(0)
        out += row_bytes

    with open(path, "wb") as f:
        f.write(out)


def stroke(pixels, points, thickness=2):
    """Draw thick polyline through (x,y) float points."""
    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for s in range(steps + 1):
            t = s / max(steps, 1)
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            for dy in range(-thickness, thickness + 1):
                for dx in range(-thickness, thickness + 1):
                    if dx * dx + dy * dy > thickness * thickness + 1:
                        continue
                    px, py = int(round(x + dx)), int(round(y + dy))
                    if 0 <= px < W and 0 <= py < H:
                        pixels[py][px] = (0, 0, 0)


def make_signature_pixels():
    # white canvas
    pixels = [[(255, 255, 255) for _ in range(W)] for _ in range(H)]

    # Cursive-style "A" loop + flowing underline (generic signature look)
    stroke(
        pixels,
        [
            (12, 38),
            (18, 22),
            (26, 18),
            (34, 24),
            (30, 32),
            (22, 36),
            (28, 40),
            (40, 28),
            (52, 20),
            (64, 22),
            (76, 30),
            (88, 28),
            (98, 22),
            (108, 26),
            (114, 34),
        ],
        thickness=2,
    )
    stroke(
        pixels,
        [
            (14, 44),
            (30, 48),
            (50, 46),
            (72, 50),
            (96, 47),
            (112, 49),
        ],
        thickness=2,
    )
    # small flourish dot
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            pixels[48 + dy][62 + dx] = (0, 0, 0)

    return pixels


def main():
    pixels = make_signature_pixels()
    write_bmp(OUT, pixels)
    print(f"Wrote {OUT} ({W}x{H}, 24-bit BMP)")


if __name__ == "__main__":
    main()
