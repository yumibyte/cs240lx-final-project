#!/usr/bin/env python3
"""Read cap-board signature BMPs (1024x512, 24-bit RGB) from SD or ZIP."""

import argparse
import struct
import sys
import zipfile
from pathlib import Path

SIG_W = 1024
SIG_H = 512
HEADER_SIZE = 54


def load_bmp_bytes(path: Path) -> bytes:
    if path.suffix.upper() == ".ZIP":
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.upper().endswith(".BMP")]
            if not names:
                raise ValueError(f"no BMP entry in {path}")
            if len(names) > 1:
                names.sort()
                print(f"note: using {names[0]} ({len(names)} bmp entries)", file=sys.stderr)
            return zf.read(names[0])
    return path.read_bytes()


def _header_layout(data: bytes) -> tuple[int, int, int]:
    if data[0:2] == b"BM":
        return 2, 10, 14
    # some SD copies drop the 2-byte BM magic; dib fields still line up from offset 12
    if len(data) >= 28:
        dib_size, width, height = struct.unpack_from("<Iii", data, 12)
        if dib_size == 40 and width == SIG_W and abs(height) == SIG_H:
            print("note: no BM magic, using cap-board header layout", file=sys.stderr)
            return 0, 8, 12
    raise ValueError("not a BMP (missing BM magic)")


def parse_sig_bmp(data: bytes) -> tuple[int, int, bytes, int]:
    if len(data) < HEADER_SIZE:
        raise ValueError("file too small for BMP header")

    file_ofs, pixel_ofs_ofs, dib_ofs = _header_layout(data)
    file_size = struct.unpack_from("<I", data, file_ofs)[0]
    pixel_offset = struct.unpack_from("<I", data, pixel_ofs_ofs)[0]
    dib_size, width, height = struct.unpack_from("<Iii", data, dib_ofs)
    planes, bpp = struct.unpack_from("<HH", data, dib_ofs + 12)
    compression = struct.unpack_from("<I", data, dib_ofs + 16)[0]

    if dib_size != 40:
        raise ValueError(f"expected BITMAPINFOHEADER (40), got {dib_size}")
    if planes != 1 or bpp != 24:
        raise ValueError(f"expected 24-bit BMP, got planes={planes} bpp={bpp}")
    if compression != 0:
        raise ValueError(f"expected uncompressed BMP, compression={compression}")
    if pixel_offset != HEADER_SIZE:
        raise ValueError(f"expected pixel offset {HEADER_SIZE}, got {pixel_offset}")
    if file_size != len(data):
        print(f"warning: header file size {file_size} != actual {len(data)}", file=sys.stderr)

    top_down = height < 0
    height = abs(height)
    if width != SIG_W or height != SIG_H:
        print(f"warning: expected {SIG_W}x{SIG_H}, got {width}x{height}", file=sys.stderr)

    row_bytes = width * 3
    expected_pixels = row_bytes * height
    pixels = data[pixel_offset : pixel_offset + expected_pixels]
    if len(pixels) != expected_pixels:
        raise ValueError(f"short pixel data: got {len(pixels)}, need {expected_pixels}")

    return width, height, pixels, dib_ofs + 8


def bgr_rows_to_rgb(pixels: bytes, width: int, height: int, top_down: bool) -> bytes:
    row_bytes = width * 3
    rows = [pixels[row * row_bytes : (row + 1) * row_bytes] for row in range(height)]
    if not top_down:
        rows.reverse()

    out = bytearray(width * height * 3)
    pos = 0
    for row in rows:
        for col in range(width):
            b = row[col * 3]
            g = row[col * 3 + 1]
            r = row[col * 3 + 2]
            out[pos] = r
            out[pos + 1] = g
            out[pos + 2] = b
            pos += 3
    return bytes(out)


def ink_stats(rgb: bytes) -> tuple[int, int]:
    dark = 0
    for i in range(0, len(rgb), 3):
        if rgb[i] < 250 or rgb[i + 1] < 250 or rgb[i + 2] < 250:
            dark += 1
    return dark, len(rgb) // 3


def write_ppm(path: Path, width: int, height: int, rgb: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + rgb)


def main() -> int:
    parser = argparse.ArgumentParser(description="Read cap-board signature BMP/ZIP")
    parser.add_argument("inputs", nargs="+", type=Path, help="SIG###.BMP or .ZIP")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output PPM path (default: <input_stem>.ppm beside input)",
    )
    parser.add_argument("--no-write", action="store_true", help="print stats only")
    args = parser.parse_args()

    for input_path in args.inputs:
        if not input_path.exists():
            print(f"error: {input_path} not found", file=sys.stderr)
            return 1

        raw = load_bmp_bytes(input_path)
        width, height, pixels, height_ofs = parse_sig_bmp(raw)
        top_down = struct.unpack_from("<i", raw, height_ofs)[0] < 0
        rgb = bgr_rows_to_rgb(pixels, width, height, top_down)
        dark, total = ink_stats(rgb)

        stem = input_path.stem
        if input_path.suffix.upper() == ".ZIP":
            stem = Path(stem).stem if stem.upper().endswith(".BMP") else stem

        out = args.output
        if out is None:
            out = input_path.with_name(f"{stem}.ppm")
        elif len(args.inputs) > 1:
            out = out / f"{stem}.ppm"

        print(f"{input_path.name}: {width}x{height}  ink_pixels={dark} ({100.0 * dark / total:.2f}%)")
        if not args.no_write:
            write_ppm(out, width, height, rgb)
            print(f"  wrote {out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
