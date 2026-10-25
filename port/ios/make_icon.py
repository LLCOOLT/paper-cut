#!/usr/bin/env python3
"""Write the iOS app's icons (port/ios/ios.cmake, at configure time).

With --art (the user's own extracted disc, data/<version>/files/mariost.tpl):
the title screen's key art (image 5, 608x480), cropped square around Mario
and scaled to each size. The repository never contains it; it exists only in
the build. Without it: a plain generated picture (a star on a gradient).
Standard library only.

usage: make_icon.py OUTDIR [--art MARIOST_TPL]
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


# Title key art: the square around Mario and his hammer (source pixels).
ART_IMAGE, ART_X, ART_Y, ART_SIDE = 5, 150, 140, 300


def art_pixels(tpl_path):
    """The title key art as a list of RGBA tuples (608x480), or None."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "port"))
    import tpl2png
    data = open(tpl_path, "rb").read()
    magic, count, table = struct.unpack_from(">III", data, 0)
    if magic != 0x0020AF30 or count <= ART_IMAGE:
        return None
    img, _ = struct.unpack_from(">II", data, table + 8 * ART_IMAGE)
    h, w, fmt, off = struct.unpack_from(">HHII", data, img)
    if (w, h) != (608, 480):
        return None
    return tpl2png.decode(data, w, h, fmt, off)


def render_art(px, size):
    """Box-filter the crop down to size x size."""
    step = ART_SIDE / size
    rows = []
    for y in range(size):
        row = bytearray(b"\x00")
        y0, y1 = ART_Y + y * step, ART_Y + (y + 1) * step
        for x in range(size):
            x0, x1 = ART_X + x * step, ART_X + (x + 1) * step
            acc = [0, 0, 0]
            n = 0
            for yy in range(int(y0), max(int(y0) + 1, math.ceil(y1))):
                for xx in range(int(x0), max(int(x0) + 1, math.ceil(x1))):
                    c = px[yy * 608 + xx]
                    acc[0] += c[0]
                    acc[1] += c[1]
                    acc[2] += c[2]
                    n += 1
            row += bytes(a // n for a in acc)
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
    args = sys.argv[1:]
    art = None
    if len(args) == 3 and args[1] == "--art":
        art = args[2]
        args = args[:1]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    out = args[0]
    os.makedirs(out, exist_ok=True)
    px = art_pixels(art) if art and os.path.exists(art) else None
    kind = "art" if px else "placeholder"
    stamp = os.path.join(out, "source.txt")
    if os.path.exists(stamp) and open(stamp).read() == kind and all(
            os.path.exists(os.path.join(out, n)) for n in ICONS):
        return 0
    for name, size in ICONS.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(png(size, render_art(px, size) if px else render(size)))
    with open(stamp, "w") as f:
        f.write(kind)
    print(f"make_icon: {kind} icons in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
