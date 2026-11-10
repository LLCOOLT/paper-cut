#!/usr/bin/env python3
"""A map's collision, read from your disc: the joint tree (names, bounding
boxes) and a top-down text picture of the floor heights and walls, for
routing input scripts without pictures (tests/port/full_run/).

Reads m/<map>/d from the disc image at run time; prints only coordinates and
names, and writes nothing.

usage: mapdump.py MAP [--iso ISO | --version G8ME01] [--grid N] [--tree] [--quiet]
                      [--flip] [--both] [--nowalls] [--band-lo Y] [--band-hi Y]
  --grid N    cell size in game units (default 50)
  --tree      every joint, not only the collision ones
  --quiet     the floor picture only
  --flip / --both  ceilings instead of floors / both
  --band-lo/--band-hi  the heights between which a wall is drawn (#)
"""
import argparse
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gcdisc import Disc  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def u32(b, a):
    return struct.unpack_from(">I", b, a)[0]


def s32(b, a):
    return struct.unpack_from(">i", b, a)[0]


def f3(b, a):
    return struct.unpack_from(">fff", b, a)


def cstr(b, a):
    return b[a:b.index(b"\0", a)].decode("ascii", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--iso", default="", help="the disc image (default orig/<version>/*.iso)")
    ap.add_argument("--version", default="G8ME01", help="G8MJ01, G8ME01 or G8MP01, for the default disc")
    ap.add_argument("--grid", type=float, default=50.0)
    ap.add_argument("--tree", action="store_true")
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--both", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--nowalls", action="store_true")
    ap.add_argument("--band-lo", type=float, default=-30)
    ap.add_argument("--band-hi", type=float, default=40)
    a = ap.parse_args()
    iso = a.iso or (glob.glob(os.path.join(ROOT, "orig", a.version, "*.iso")) + [""])[0]
    if not iso or not os.path.exists(iso):
        sys.exit(f"mapdump: no disc image (--iso, or orig/{a.version}/*.iso)")
    raw = Disc(iso).read(f"m/{a.map}/d")
    file_size, data_size, rel_count, chunk_count, extern_count = struct.unpack_from(">5i", raw, 0)
    data = raw[0x20:0x20 + data_size]
    at = 0x20 + data_size + rel_count * 4
    chunks = [struct.unpack_from(">II", raw, at + i * 8) for i in range(chunk_count)]
    strings_at = at + chunk_count * 8 + extern_count * 8
    names = {cstr(raw, strings_at + s): o for o, s in chunks}
    info = names["information"]
    root = u32(data, info + 4)
    hit_name = cstr(data, u32(data, info + 0xC)) if u32(data, info + 0xC) else ""
    print(f"map {a.map}: chunks {sorted(names)}; hit root name '{hit_name}'")

    def joint(o):
        return dict(name=cstr(data, u32(data, o)), type=cstr(data, u32(data, o + 4)), child=u32(data, o + 0xC),
                    next=u32(data, o + 0x10), scale=f3(data, o + 0x18), rot=f3(data, o + 0x24),
                    trans=f3(data, o + 0x30), bmin=f3(data, o + 0x3C), bmax=f3(data, o + 0x48),
                    draw=u32(data, o + 0x58), parts=s32(data, o + 0x5C), off=o)

    tris = []

    def mesh_tris(mo, mtx):
        packed = data[mo + 3]
        n = s32(data, mo + 4)
        vcd = u32(data, mo + 0xC)
        pos_arr = u32(data, vcd)
        frac = s32(data, vcd + 0x44)
        if not pos_arr:
            return
        sc = 1.0 / (1 << frac)

        def pos(i):
            x, y, z = struct.unpack_from(">hhh", data, pos_arr + 4 + i * 6)
            return x * sc, y * sc, z * sc

        if packed:
            mask = u32(data, mo + 8)
            stride = 2 * bin(mask & 0xFFF).count("1")
            for j in range(n):
                lst = u32(data, mo + 0x10 + j * 8)
                cnt = struct.unpack_from(">H", data, lst + 1)[0]
                idx = [struct.unpack_from(">H", data, lst + 3 + k * stride)[0] for k in range(cnt)]
                for k in range(cnt - 2):
                    o = (idx[k], idx[k + 2], idx[k + 1]) if k % 2 == 0 else (idx[k], idx[k + 1], idx[k + 2])
                    tris.append(tuple(apply(mtx, pos(v)) for v in o))
            return
        for j in range(n):
            dl = u32(data, mo + 0x10 + j * 4)
            vc = s32(data, dl)
            vs = [pos(struct.unpack_from(">h", data, dl + 4 + k * 0x18)[0]) for k in range(vc)]
            for k in range(vc - 2):
                tris.append(tuple(apply(mtx, v) for v in (vs[k], vs[k + 1], vs[k + 2])))

    def apply(m, v):
        x, y, z = v
        return (m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7],
                m[8] * x + m[9] * y + m[10] * z + m[11])

    import math

    def compose(p, j):
        sx, sy, sz = j["scale"]
        rx, ry, rz = (math.radians(r) for r in j["rot"])
        tx, ty, tz = j["trans"]
        cx, sxr = math.cos(rx), math.sin(rx)
        cy, syr = math.cos(ry), math.sin(ry)
        cz, szr = math.cos(rz), math.sin(rz)
        # R = Rz * Ry * Rx (approximate the exporter's order)
        r = [[cz * cy, cz * syr * sxr - szr * cx, cz * syr * cx + szr * sxr],
             [szr * cy, szr * syr * sxr + cz * cx, szr * syr * cx - cz * sxr],
             [-syr, cy * sxr, cy * cx]]
        l = [r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, tx, r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, ty,
             r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, tz]
        out = []
        for i in range(3):
            row = []
            for k in range(4):
                v = sum(p[i * 4 + m] * l[m * 4 + k] for m in range(3)) + (p[i * 4 + 3] if k == 3 else 0)
                row.append(v)
            out += row
        return out

    ident = [10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 10, 0]  # the game draws maps at 10x

    def walk(o, depth, mtx, in_hit):
        while o:
            j = joint(o)
            m = compose(mtx, j)
            hit = in_hit or (hit_name and j["name"] == hit_name)
            if a.tree or (hit and not a.quiet):
                bmin = ",".join(f"{v:.0f}" for v in j["bmin"])
                bmax = ",".join(f"{v:.0f}" for v in j["bmax"])
                attr = s32(data, j["draw"] + 8) if j["draw"] else 0
                print(f"{'  ' * depth}{'*' if hit else ' '}{j['name']} [{bmin} .. {bmax}] attr {attr:#x}"
                      f" parts {j['parts']} S{tuple(round(v,2) for v in j['scale'])} R{tuple(round(v) for v in j['rot'])} T{tuple(round(v) for v in j['trans'])}")
            if hit:
                for p in range(j["parts"]):
                    mo = u32(data, o + 0x60 + p * 8 + 4)
                    if mo:
                        mesh_tris(mo, m)
            walk(j["child"], depth + 1, m, hit)
            o = j["next"]

    walk(root, 0, ident, False)
    if not tris:
        return
    # top-down: for each cell, the floor heights of upward-facing triangles there
    xs = [v[0] for t in tris for v in t]
    zs = [v[2] for t in tris for v in t]
    g = a.grid
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    W, H = int((x1 - x0) / g) + 1, int((z1 - z0) / g) + 1
    cells = [[None] * W for _ in range(H)]
    walls = set()
    for t in tris:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
        # the game's normal: edge0 = v2 - v0, edge1 = v0 - v1, n = edge0 x edge1
        e0 = (cx - ax, cy - ay, cz - az)
        e1 = (ax - bx, ay - by, az - bz)
        n = (e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0])
        ln = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
        if ln < 1e-9:
            continue
        if abs(n[1]) / ln < 0.5:
            # a wall: mark the cells along its bottom edge segment within the walking band
            lo, hi = min(ay, by, cy), max(ay, by, cy)
            if hi > a.band_lo and lo < a.band_hi:
                pts = [(ax, az), (bx, bz), (cx, cz)]
                for (p0, p1) in ((pts[0], pts[1]), (pts[1], pts[2]), (pts[2], pts[0])):
                    steps = int(max(abs(p1[0] - p0[0]), abs(p1[1] - p0[1])) / (g / 3)) + 1
                    for s in range(steps + 1):
                        wx = p0[0] + (p1[0] - p0[0]) * s / steps
                        wz = p0[1] + (p1[1] - p0[1]) * s / steps
                        walls.add((int((wz - z0) / g), int((wx - x0) / g)))
            continue
        if not a.both and (n[1] < 0) != a.flip:
            continue  # a ceiling
        for r in range(H):
            pz = z0 + (r + 0.5) * g
            for c in range(W):
                px = x0 + (c + 0.5) * g
                d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
                if abs(d) < 1e-9:
                    continue
                l1 = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / d
                l2 = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / d
                l3 = 1 - l1 - l2
                if l1 >= 0 and l2 >= 0 and l3 >= 0:
                    y = l1 * ay + l2 * by + l3 * cy
                    if cells[r][c] is None or y > cells[r][c]:
                        cells[r][c] = y
    print(f"\nfloor tops, cell {g:.0f}: x {x0:.0f}..{x1:.0f} (columns), z {z0:.0f}..{z1:.0f} (rows, top = -z);"
          " each cell = top floor height / 25 as a char (0-9 a-z for 0..875, 'A'.. below 0 in steps of 10 (A -2..-10, B -10..-20 ...), '.' none)")
    hdr = "".join(str(int((x0 + c * g) // 100) % 10) if c % 4 == 0 else " " for c in range(W))
    print("      " + hdr)
    for r in range(H):
        row = ""
        for c in range(W):
            y = cells[r][c]
            if (r, c) in walls and not a.nowalls:
                row += "#"
            elif y is None:
                row += "."
            elif y < -2:
                row += "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[min(int(-y // 10), 25)]
            else:
                k = int(y // 25)
                row += "0123456789abcdefghijklmnopqrstuvwxyz"[min(k, 35)]
        print(f"{z0 + r * g:6.0f} {row}")


if __name__ == "__main__":
    main()
