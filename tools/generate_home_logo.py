#!/usr/bin/env python3
"""Generate a compact 2-bit LCD bitmap from the canonical RFScout PNG logo.

Uses only the Python standard library so it can run in the ESP-IDF CI container.
The output palette is black, RFScout orange, and white.
"""

import argparse
import struct
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PALETTE = ((0, 0, 0), (255, 128, 0), (255, 255, 255))
NEUTRAL_CHROMA_THRESHOLD = 16
NEUTRAL_WHITE_THRESHOLD = 192


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def read_png(path):
    data = Path(path).read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")

    pos = len(PNG_SIGNATURE)
    width = height = color_type = bit_depth = interlace = None
    compressed = bytearray()
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError("logo PNG must be non-interlaced 8-bit RGB or RGBA")

    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    rows = []
    offset = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        source = raw[offset : offset + stride]
        offset += stride
        row = bytearray(stride)
        for x, value in enumerate(source):
            left = row[x - channels] if x >= channels else 0
            up = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = value + left
            elif filter_type == 2:
                decoded = value + up
            elif filter_type == 3:
                decoded = value + ((left + up) // 2)
            elif filter_type == 4:
                decoded = value + paeth(left, up, upper_left)
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}")
            row[x] = decoded & 0xFF
        rows.append(row)
        previous = row
    return width, height, channels, rows


def rgb_at(rows, channels, x, y):
    row = rows[y]
    base = x * channels
    rgb = tuple(row[base : base + 3])
    if channels != 4:
        return rgb

    alpha = row[base + 3]
    return tuple(channel * alpha // 255 for channel in rgb)


def content_bounds(width, height, channels, rows):
    xs, ys = [], []
    for y in range(height):
        for x in range(width):
            r, g, b = rgb_at(rows, channels, x, y)
            if max(r, g, b) > 12:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise ValueError("logo contains no visible pixels")
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def nearest_palette(rgb):
    r, g, b = rgb

    # The canonical RGB asset is already antialiased onto black. Keep neutral
    # gray edge pixels on the black/white axis so Euclidean distance cannot
    # misclassify a gray edge as saturated RFScout orange.
    if max(rgb) - min(rgb) <= NEUTRAL_CHROMA_THRESHOLD:
        return 0 if r + g + b < (3 * NEUTRAL_WHITE_THRESHOLD) else 2

    return min(
        range(len(PALETTE)),
        key=lambda i: (r - PALETTE[i][0]) ** 2 + (g - PALETTE[i][1]) ** 2 + (b - PALETTE[i][2]) ** 2,
    )


def generate(source, output, out_width, out_height):
    width, height, channels, rows = read_png(source)
    x0, y0, x1, y1 = content_bounds(width, height, channels, rows)
    crop_width, crop_height = x1 - x0, y1 - y0
    packed = bytearray()

    for y in range(out_height):
        values = []
        source_y = y0 + min(crop_height - 1, (y * crop_height) // out_height)
        for x in range(out_width):
            source_x = x0 + min(crop_width - 1, (x * crop_width) // out_width)
            values.append(nearest_palette(rgb_at(rows, channels, source_x, source_y)))
        for x in range(0, out_width, 4):
            byte = 0
            for shift in range(4):
                if x + shift < out_width:
                    byte |= values[x + shift] << (shift * 2)
            packed.append(byte)

    lines = []
    for offset in range(0, len(packed), 24):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in packed[offset : offset + 24]) + ",")

    text = f'''/** Auto-generated from assets/logo.png by tools/generate_home_logo.py. */
#pragma once
#include <stdint.h>
#define HOME_LOGO_WIDTH {out_width}
#define HOME_LOGO_HEIGHT {out_height}
#define HOME_LOGO_BYTES_PER_ROW ((HOME_LOGO_WIDTH + 3) / 4)
static const uint8_t home_logo_bitmap[HOME_LOGO_BYTES_PER_ROW * HOME_LOGO_HEIGHT] = {{
{chr(10).join(lines)}
}};
'''
    Path(output).write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("output")
    parser.add_argument("--width", type=int, default=216)
    parser.add_argument("--height", type=int, default=122)
    args = parser.parse_args()
    generate(args.source, args.output, args.width, args.height)


if __name__ == "__main__":
    main()
