#!/usr/bin/env python3
"""Write the iOS app's placeholder icons (port/ios/ios.cmake, at configure time).

A plain generated picture (a star on a gradient) so the bundle has an icon
without shipping any of the game's artwork. Standard library only.

usage: make_icon.py OUTDIR
"""
import math
import os
import struct
import sys
import zlib

# File name -> pixel size (CFBundleIconFiles in Info.plist.in).
ICONS = {
    "AppIcon60x60@2x.png": 120,
    "AppIcon60x60@3x.png": 180,
    "AppIcon76x76@2x~ipad.png": 152,
    "AppIcon83.5x83.5@2x~ipad.png": 167,
}


def star(cx, cy, outer, inner, points=5):
    verts = []
    for i in range(points * 2):
        r = outer if i % 2 == 0 else inner
        a = -math.pi / 2 + i * math.pi / points
        verts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return verts


def inside(poly, x, y):
    hit = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit


def render(size):
    poly = star(size / 2, size * 0.53, size * 0.38, size * 0.16)
    top = (58, 40, 120)
    bottom = (24, 90, 170)
    fill = (255, 214, 64)
    rows = []
    for y in range(size):
        t = y / max(size - 1, 1)
        bg = tuple(round(top[k] + (bottom[k] - top[k]) * t) for k in range(3))
        row = bytearray(b"\x00")  # PNG filter: none
        for x in range(size):
            cover = sum(inside(poly, x + dx, y + dy) for dx in (0.25, 0.75) for dy in (0.25, 0.75)) / 4
            row += bytes(round(bg[k] + (fill[k] - bg[k]) * cover) for k in range(3))
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)  # 8-bit RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, size in ICONS.items():
        path = os.path.join(out, name)
        if not os.path.exists(path):
            with open(path, "wb") as f:
                f.write(png(size, render(size)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
