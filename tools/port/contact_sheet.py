#!/usr/bin/env python3
"""Tile frame captures (port/src/aurora/shot_aurora.c PNGs) into contact sheets.

Each capture is scaled down by --scale (default 4: 160x120) and placed in a
grid, --cols per row, --rows per sheet. The order is the sorted file names;
the index of each sheet (row, column -> name) is printed and written next to
the sheet as <sheet>.txt. Standard library only (zlib).

usage: contact_sheet.py SHOT_DIR OUT_PREFIX [--scale N] [--cols N] [--rows N]
"""
import argparse
import os
import struct
import sys
import zlib


def read_png(path):
    """8-bit RGB/RGBA PNG, any filter -> (w, h, rows of RGB bytes)."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype in (2, 6), path
            bpp = 3 if ctype == 2 else 4
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        prev = line
        rows.append(bytes(line) if bpp == 3 else bytes(v for i, v in enumerate(line) if i % 4 != 3))
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(t, body):
        return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def thumb(rows, w, h, s):
    """Box-filter downscale by s."""
    tw, th = w // s, h // s
    out = []
    for ty in range(th):
        line = bytearray(tw * 3)
        for tx in range(tw):
            acc = [0, 0, 0]
            for dy in range(s):
                r = rows[ty * s + dy]
                for dx in range(s):
                    i = (tx * s + dx) * 3
                    acc[0] += r[i]
                    acc[1] += r[i + 1]
                    acc[2] += r[i + 2]
            for c in range(3):
                line[tx * 3 + c] = acc[c] // (s * s)
        out.append(line)
    return tw, th, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shots")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--cols", type=int, default=8)
    ap.add_argument("--rows", type=int, default=6)
    a = ap.parse_args()
    names = sorted(n for n in os.listdir(a.shots) if n.endswith(".png"))
    if not names:
        print(f"contact_sheet: no captures in {a.shots}")
        return 1
    per = a.cols * a.rows
    gap = 2
    for sheet in range(0, len(names), per):
        group = names[sheet:sheet + per]
        thumbs = []
        for n in group:
            w, h, rows = read_png(os.path.join(a.shots, n))
            thumbs.append(thumb(rows, w, h, a.scale))
        tw, th = thumbs[0][0], thumbs[0][1]
        cols = min(a.cols, len(group))
        nrows = (len(group) + a.cols - 1) // a.cols
        W, H = cols * (tw + gap) + gap, nrows * (th + gap) + gap
        canvas = [bytearray(b"\x40\x40\x40" * W) for _ in range(H)]
        index = []
        for k, (n, (w2, h2, rows)) in enumerate(zip(group, thumbs)):
            r, c = divmod(k, a.cols)
            x0, y0 = gap + c * (tw + gap), gap + r * (th + gap)
            for y in range(h2):
                canvas[y0 + y][x0 * 3:(x0 + w2) * 3] = rows[y]
            index.append(f"row {r + 1} col {c + 1}: {n[:-4]}")
        path = f"{a.out}_{sheet // per + 1:02d}.png"
        write_png(path, W, H, canvas)
        open(path[:-4] + ".txt", "w").write("\n".join(index) + "\n")
        print(f"{path}: {len(group)} captures ({group[0][:-4]} .. {group[-1][:-4]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
