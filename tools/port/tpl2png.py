#!/usr/bin/env python3
"""Decode a GameCube TPL texture file to PNG(s). Standard library only.

Formats: I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, C4, C8, C14X2 (with an
IA8/RGB565/RGB5A3 palette) and CMPR. Each image of the file is written as
OUT_<n>.png (or OUT.png for a single image). Used on the user's own disc
data (e.g. the iOS icon); its output is never committed.

usage: tpl2png.py FILE.tpl OUT_PREFIX     (prints each image's size and format)
"""
import struct
import sys
import zlib

FORMATS = {0: "I4", 1: "I8", 2: "IA4", 3: "IA8", 4: "RGB565", 5: "RGB5A3", 6: "RGBA8",
           8: "C4", 9: "C8", 10: "C14X2", 14: "CMPR"}
# format -> (block width, block height, bits per pixel)
BLOCKS = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16),
          6: (4, 4, 32), 8: (8, 8, 4), 9: (8, 4, 8), 10: (4, 4, 16), 14: (8, 8, 4)}


def rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, 255)


def rgb5a3(v):
    if v & 0x8000:
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255)
    a, r, g, b = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17, a << 5 | a << 2 | a >> 1)


def ia8(v):
    i = v & 0xFF
    return (i, i, i, v >> 8)


def palette(data, off, count, fmt):
    conv = {0: ia8, 1: rgb565, 2: rgb5a3}[fmt]
    return [conv(struct.unpack_from(">H", data, off + 2 * i)[0]) for i in range(count)]


def cmpr_block(data, off):
    c0, c1 = struct.unpack_from(">HH", data, off)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1:
        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0[:3], p1[:3])) + (255,)
        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0[:3], p1[:3])) + (255,)
    else:
        p2 = tuple((a + b) // 2 for a, b in zip(p0[:3], p1[:3])) + (255,)
        p3 = (0, 0, 0, 0)
    pal = (p0, p1, p2, p3)
    bits = struct.unpack_from(">I", data, off + 4)[0]
    return [pal[(bits >> (30 - 2 * i)) & 3] for i in range(16)]


def decode(data, w, h, fmt, off, pal=None):
    bw, bh, bpp = BLOCKS[fmt]
    px = [(0, 0, 0, 0)] * (w * h)
    bx_n, by_n = (w + bw - 1) // bw, (h + bh - 1) // bh
    p = off

    def put(x, y, c):
        if x < w and y < h:
            px[y * w + x] = c

    for by in range(by_n):
        for bx in range(bx_n):
            x0, y0 = bx * bw, by * bh
            if fmt == 14:  # CMPR: 2x2 DXT1 sub-blocks
                for sb in range(4):
                    cols = cmpr_block(data, p)
                    p += 8
                    sx, sy = x0 + (sb & 1) * 4, y0 + (sb >> 1) * 4
                    for i, c in enumerate(cols):
                        put(sx + i % 4, sy + i // 4, c)
                continue
            if fmt == 6:  # RGBA8: AR plane then GB plane
                for i in range(16):
                    a, r = data[p + 2 * i], data[p + 2 * i + 1]
                    g, b = data[p + 32 + 2 * i], data[p + 33 + 2 * i]
                    put(x0 + i % 4, y0 + i // 4, (r, g, b, a))
                p += 64
                continue
            for i in range(bw * bh):
                x, y = x0 + i % bw, y0 + i // bw
                if bpp == 4:
                    v = data[p + i // 2] >> (4 if i % 2 == 0 else 0) & 15
                    c = (v * 17,) * 3 + (255,) if fmt == 0 else pal[v]
                elif bpp == 8:
                    v = data[p + i]
                    if fmt == 1:
                        c = (v, v, v, 255)
                    elif fmt == 2:
                        c = ((v & 15) * 17,) * 3 + ((v >> 4) * 17,)
                    else:
                        c = pal[v]
                else:
                    v = struct.unpack_from(">H", data, p + 2 * i)[0]
                    c = {3: ia8, 4: rgb565, 5: rgb5a3}.get(fmt, lambda v: pal[v & 0x3FFF])(v)
                put(x, y, c)
            p += bw * bh * bpp // 8
    return px


def write_png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(c for p in px[y * w:(y + 1) * w] for c in p) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    data = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    magic, count, table = struct.unpack_from(">III", data, 0)
    if magic != 0x0020AF30:
        sys.exit(f"{sys.argv[1]}: not a TPL")
    for n in range(count):
        img_off, pal_off = struct.unpack_from(">II", data, table + 8 * n)
        h, w, fmt, data_off = struct.unpack_from(">HHII", data, img_off)
        pal = None
        if pal_off and fmt in (8, 9, 10):
            pcount, _, _, pfmt, pdata = struct.unpack_from(">HBBII", data, pal_off)
            pal = palette(data, pdata, pcount, pfmt)
        if fmt not in BLOCKS:
            print(f"{n}: {w}x{h} format {fmt}: not supported")
            continue
        path = f"{out}.png" if count == 1 else f"{out}_{n}.png"
        write_png(path, w, h, decode(data, w, h, fmt, data_off, pal))
        print(f"{n}: {w}x{h} {FORMATS[fmt]} -> {path}")


if __name__ == "__main__":
    main()
