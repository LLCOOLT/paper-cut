#!/usr/bin/env python3
"""The Paper Mario 64 partners' movement extras (port/src/common/pm64_field_pc.c, "As in Paper Mario"): every field
map of a disc checked for the places each extra reaches that its TTYD counterpart (the host) doesn't, and whether
Mario can get back from there.

  parakarry   Yoshi's flutter flying three times as long (portPm64FlutterFrames), against Yoshi's own
  lakilester  Mario carried over spikes and lava unhurt (portPm64MarioAttr; he's on Flurrie): those floors
              walkable, onto them from at most CARRY_DROP above, against Flurrie (Mario's own jump)
  sushie      deep water a floor (portPm64MarioAttr), and the jump off her back higher (portPm64SwimJump, the
              jump's first speed times 12/8), against Vivian (Mario's own jump)

The map's collision, blocks, doors and items, the areas (walkable floors joined where they share an edge or meet
within a step), their rims and the jumps between them are luigi_jump_sweep.py's, with the two reaches here: the
host's (Mario's own jump, and Yoshi's flutter for the two on Yoshi) and the extra's. Floors only the extra can
stand on (the hazards, the water) are areas of their own, walked onto from the floors beside them by the extra
alone. An area newly reached is reported with what is on it (doors, pipes, blocks, items) and NO WAY BACK when
nothing the extra can do gets Mario from it back to where the host goes, and it has no door or pipe out: there
Mario would be stuck (a softlock) unless a script takes him off.

The flutter, as party_yoshi.c steps it (US): A on the ground starts it at once (useMotionId 20); each frame the
bob's angle goes 7 degrees on from 1, the height bobbing by its sine (0.5 of it rising, 0.37 falling) until 360,
then level; held A keeps it up until the air timer passes portYoshiFlutterFrames (68; Parakarry three times
that); the stick's sideways push (at most 72 a frame) adds 0.01 of itself to a velocity capped at 16, moved at
0.1 of it a frame (at most 1.6); a floor within 11 of Yoshi lands him from the tenth frame on. Then the fall
(L_fall): -0.4 a frame more each frame, to -15, still steered. Only sideways to the camera: taken here as
along x (the maps' cameras look along z), a take-off within a flutter's width of the landing's z.

usage: pm64_field_sweep.py {parakarry|lakilester|sushie} [--iso ISO | --version G8ME01] [--only PREFIX,...]
                           [-j N] [--json OUT] [--arc]
"""
import argparse
import collections
import concurrent.futures
import glob
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gcdisc import Disc  # noqa: E402
import luigi_jump_sweep as L  # noqa: E402

ROOT = L.ROOT
DEEP = 0x200
LIQUID = 0x400
SPIKES = 0x800
DOCKS = 0x4000 | 0x8000 | 0x10000 | 0x20000
FLUTTER = 68
SWIM_DROP = 80.0  # pm64_field_pc.c's SWIM_DROP: Sushie swims only in water at most this far below Mario's last floor
CARRY_DROP = 40.0  # pm64_field_pc.c's CARRY_DROP: Lakilester carries Mario onto hazards at most this far below
STICK = 72.0


# --- the reaches ----------------------------------------------------------------------------------

class Glide:
    """A move given frame by frame as (y, sideways so far, the highest floor caught that frame or None)."""

    def __init__(self, frames):
        self.frames = frames
        self.peak = max(y for y, _, _ in frames)
        self.head = self.peak + L.HEAD
        self.top = max(c for _, _, c in frames if c is not None)

    def dist(self, dh):
        best = None
        for _, x, c in self.frames:
            if c is not None and c >= dh:
                best = x
        return None if best is None else best + L.RAY_OUT


def flutter(frames):
    """Yoshi's flutter held for `frames` of air, then the fall"""
    out = [(0.0, 0.0, None)]
    angle, bob, vel, x, air = 1.0, 0.0, 0.0, 0.0, 0
    while air <= frames:
        if angle < 360.0:
            s = math.sin(math.pi * angle / 180.0)
            bob += s * (0.5 if angle <= 180.0 else 0.37)
            angle = min(360.0, angle + 7.0)
        vel = min(16.0, vel + STICK * 0.01)
        x += min(0.1 * vel, 2.0)
        air += 1
        out.append((bob, x, bob + 11.0 if air >= 10 else None))
    y, g, a = bob, -0.4, -0.4
    while y > -600.0:
        g = max(-15.0, g + a)
        y += g
        x += min(0.1 * vel, 2.0)
        out.append((y, x, y + max(1.0, abs(g))))
    return Glide(out)


def jump_arc(v0):
    """Mario's jump (marioJumpData's curve, as luigi_jump_sweep.arc) with its first speed v0 (8: his own)"""
    v, a, j, s = (L.f32(x) for x in (v0, -0.84, 0.08, -0.02))
    y = 0.0
    out = [(0.0, 0.0)] * L.PRELUDE
    while True:
        j = L.f32(j + s)
        a = L.f32(a + j)
        v = L.f32(v + a)
        if v <= 0.0:
            v, a, j, s = L.f32(-0.1), L.f32(-0.2), L.f32(0.024), L.f32(-0.004)
            j = L.f32(j + s)
            a = L.f32(a + j)
            v = L.f32(v + a)
            y = L.f32(y + v)
            out.append((y, v))
            break
        y = L.f32(y + v)
        out.append((y, v))
    while y > -400.0:
        j = L.f32(j + s)
        if j > 0.0:
            j = 0.0
        a = L.f32(a + j)
        v = L.f32(v + a)
        if v <= -18.0:
            v, a, j, s = -18.0, 0.0, 0.0, 0.0
        y = L.f32(y + v)
        out.append((y, v))
    return L.Reach(out)


def reaches(mode):
    """the host's moves, the extra's from a plain floor, and the extra's from its own kind of floor: each a list of
    (reach, sideways only), the flutter going only sideways to the camera (its push is the stick's x alone)"""
    mario = (jump_arc(8.0), False)
    if mode == "parakarry":
        # (his long flutter, not through the airspace pm64_field_pc.c's kParakarryShort lists: Yoshi's there)
        return ([mario, (flutter(FLUTTER), True)],
                [mario, (flutter(FLUTTER), True), (flutter(FLUTTER * 3), True, None, "zoned")], [])
    if mode == "lakilester":
        return [mario], [mario], [mario]  # (on Flurrie: Mario's own jump, on the hazards too)
    return [mario], [mario], [(jump_arc(8.0 * 12.0 / 8.0), False)]


def special(mode, attr):
    """a floor only the extra stands on"""
    if mode == "lakilester":
        return bool(attr & (LIQUID | SPIKES))
    if mode == "sushie":
        return bool(attr & DEEP) and not attr & (LIQUID | SPIKES)
    return False


# --- the scripts: doors and pipes, and where hazards put Mario back ------------------------------------

def beros():
    """map -> [(hit name, next map, next bero)], each bero table entry read on its own (a pipe to another of the same
    map's has no next map: the map itself)"""
    out = collections.defaultdict(list)
    for path in glob.glob(os.path.join(ROOT, "rels", "*", "src", "**", "*.c"), recursive=True):
        stem = os.path.splitext(os.path.basename(path))[0]
        if not re.fullmatch(r"[a-z]{3}_\d\d", stem):
            continue
        src = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r'"(\w+)", // hitName(.*?)// nextBeroName', src, re.S):
            body = m.group(2)
            nm = re.search(r'"(\w*)", // nextMapName', body)
            nb = re.search(r'(NULL|"(\w*)"),\s*$', body)
            out[stem].append((m.group(1), nm.group(1) if nm else stem, (nb.group(2) or "") if nb else ""))
    return out


def zones(mode):
    """pm64_field_pc.c's places where the extra is left out: parakarry, map -> [(x0, z0, x1, z1, y0, y1)], the
    airspace his long flutter is cut short in (kParakarryShort); none for the others (Lakilester's and Sushie's are
    the drop limits, CARRY_DROP and SWIM_DROP)"""
    src = open(os.path.join(ROOT, "port", "src", "common", "pm64_field_pc.c"), encoding="utf-8").read()
    table = {"parakarry": "kParakarryShort"}.get(mode)  # (lakilester: none now, CARRY_DROP instead)
    out = collections.defaultdict(list)
    table = None if table is None else table + "[] = {"
    if table is None or table not in src:
        return out
    body = src[src.index(table):]
    body = body[:body.index("};")]
    if mode == "lakilester":
        for m in re.finditer(r'\{"(\w+)",\s*"(\w+)"\}', body):
            out[m.group(1)].append(m.group(2))
        return out
    num = r"\s*(-?[0-9.]+)f?\s*"
    for m in re.finditer(r'\{"(\w+)",%s,%s,%s,%s,%s,%s\}' % ((num,) * 6), body):
        out[m.group(1)].append(tuple(float(m.group(i)) for i in range(2, 8)))
    return out


def returns():
    """map -> {hit name: (x, y, z)}: hitDmgRetPoints, where a hazard floor (deep water, spikes) puts Mario back
    (y -1000: nowhere set, his own last floor)"""
    out = collections.defaultdict(dict)
    pat = re.compile(r'\{"(\w+)", \{%s,%s,%s\}\}' % (L.NUM, L.NUM, L.NUM))
    for path in glob.glob(os.path.join(ROOT, "rels", "*", "src", "**", "*.c"), recursive=True):
        stem = os.path.splitext(os.path.basename(path))[0]
        if not re.fullmatch(r"[a-z]{3}_\d\d", stem):
            continue
        for m in pat.finditer(open(path, encoding="utf-8", errors="replace").read()):
            out[stem][m.group(1)] = (float(m.group(2)), float(m.group(3)), float(m.group(4)))
    return out


# --- one map ------------------------------------------------------------------------------------

def sweep_map(args):
    iso, name, mode, blocks, beros, things, rets, zl = args
    raw = Disc(iso).read(f"m/{name}/d")
    tris = L.collision(raw)
    host, extra, extra_sp = reaches(mode)
    if mode == "sushie" and name.startswith("dou_"):  # (Pirate's Grotto: the boat's; Boat Mode rooms swim)
        return {"map": name, "areas": 0, "new": [], "skipped": "the boat's water"}
    floors, walls, kind = [], [], []
    for t in tris:
        n = L.normal(t)
        if n is None or not L.solid(t[3]):
            continue
        if n[1] >= math.cos(math.radians(50)):
            if not (t[3] & L.HAZARD):
                floors.append(t)
                kind.append(0)
            elif special(mode, t[3]) and not (mode == "lakilester" and t[4] in zl):
                floors.append(t)
                kind.append(1)
        elif abs(n[1]) < 0.5:
            walls.append(t)
    if mode != "parakarry" and not any(kind):
        return {"map": name, "areas": 0, "new": []}  # (none of the floors the extra adds: the host's reach)
    for bkind, bname, x, y, z in blocks:
        h = L.BLOCK / 2
        top = y + (L.BLOCK if bkind != "kururing_floor" else 0.0)
        a, b, cc, d = (x - h, top, z - h), (x + h, top, z - h), (x + h, top, z + h), (x - h, top, z + h)
        floors += [(a, b, cc, 0, f"mobj {bkind} {bname}"), (a, cc, d, 0, f"mobj {bkind} {bname}")]
        kind += [0, 0]

    parent = list(range(len(floors)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def key(v):
        return (round(v[0], 1), round(v[1], 1), round(v[2], 1))

    edges = collections.defaultdict(list)
    for i, t in enumerate(floors):
        for e in ((0, 1), (1, 2), (2, 0)):
            a, b = key(t[e[0]]), key(t[e[1]])
            edges[(min(a, b), max(a, b))].append(i)
    walk = set()  # (floor, floor) of the two kinds side by side: walked across by the extra alone
    for tl in edges.values():
        for i in tl[1:]:
            if kind[i] == kind[tl[0]]:
                parent[find(i)] = find(tl[0])
            else:
                walk.add((i, tl[0]))
    rim = []
    for (a, b), tl in edges.items():
        if len(set(kind[i] for i in tl)) == 1 and len(tl) != 1:
            continue
        for ti in tl:
            if sum(1 for k in tl if kind[k] == kind[ti]) != 1:
                continue
            ln = math.hypot(b[0] - a[0], b[2] - a[2])
            steps = max(1, int(ln / L.RIM_STEP))
            for s in range(steps + 1):
                f = s / steps
                rim.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, ti))
    grid = collections.defaultdict(list)
    for i, p in enumerate(rim):
        grid[(int(p[0] // L.CELL), int(p[2] // L.CELL))].append(i)
    for i, p in enumerate(rim):
        gx, gz = int(p[0] // L.CELL), int(p[2] // L.CELL)
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for k in grid.get((gx + dx, gz + dz), ()):
                    q = rim[k]
                    if k > i and abs(q[1] - p[1]) <= L.STEP and (q[0] - p[0]) ** 2 + (q[2] - p[2]) ** 2 <= 16.0:
                        if kind[q[3]] == kind[p[3]]:
                            parent[find(q[3])] = find(p[3])
                        else:
                            walk.add((q[3], p[3]))
    area_of = [find(i) for i in range(len(floors))]
    areas = sorted(set(area_of))
    aid = {a: n for n, a in enumerate(areas)}
    akind = {aid[area_of[i]]: kind[i] for i in range(len(floors))}
    size = collections.Counter()
    for i, t in enumerate(floors):
        (ax, _, az), (bx, _, bz), (cx, _, cz) = t[0], t[1], t[2]
        size[aid[area_of[i]]] += abs((bx - ax) * (cz - az) - (cx - ax) * (bz - az)) / 2
    label = {}
    for i, t in enumerate(floors):
        a = aid[area_of[i]]
        if t[4].startswith("mobj") or a not in label:
            label[a] = t[4]

    wgrid = collections.defaultdict(list)
    for wi, w in enumerate(walls):
        xs, zs = [v[0] for v in w[:3]], [v[2] for v in w[:3]]
        for gx in range(int(min(xs) // L.CELL), int(max(xs) // L.CELL) + 1):
            for gz in range(int(min(zs) // L.CELL), int(max(zs) // L.CELL) + 1):
                wgrid[(gx, gz)].append(wi)

    def crosses(p, q, w):
        def side(a, b, c):
            return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        P, Q = (p[0], p[2]), (q[0], q[2])
        vs = [(v[0], v[2]) for v in w[:3]]
        for e in range(3):
            A, B = vs[e], vs[(e + 1) % 3]
            if ((side(A, B, P) > 0) != (side(A, B, Q) > 0)) and ((side(P, Q, A) > 0) != (side(P, Q, B) > 0)):
                return True
        return False

    def blocked(p, q, reach):
        lo = max(p[1], q[1]) + L.CATCH
        hi = p[1] + reach.peak + L.HEAD
        cells = set()
        for f in (0.0, 0.25, 0.5, 0.75, 1.0):
            cells.add((int((p[0] + (q[0] - p[0]) * f) // L.CELL), int((p[2] + (q[2] - p[2]) * f) // L.CELL)))
        for cell in cells:
            for wi in wgrid.get(cell, ()):
                w = walls[wi]
                ys = [v[1] for v in w[:3]]
                if min(ys) <= lo and max(ys) >= hi and crosses(p, q, w):
                    return True
        return False

    def reach_for(side, a, b):
        """the reach of a jump from area a to area b: the host's (None to or from the extra's floors), or the
        extra's (from its own floors, its own reach)"""
        if side == 0:
            return [] if akind[a] or akind[b] else host
        if akind[a]:
            return extra_sp
        if akind[b] and mode == "lakilester":
            return [(r, x, -CARRY_DROP) for r, x in extra]  # (onto them from higher he's hurt: no carry)
        if akind[b] and mode == "sushie":
            return [(r, x, -SWIM_DROP) for r, x in extra]  # (in from higher he falls in: no swim)
        return extra

    every = [m[0] for m in host + extra + extra_sp]
    big = max((r.dist(-1e9) or 0.0) for r in every)
    tops = max(r.top for r in every)
    TC = 50.0
    tgrid = collections.defaultdict(list)
    for i, t in enumerate(floors):
        xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
        for gx in range(int(min(xs) // TC), int(max(xs) // TC) + 1):
            for gz in range(int(min(zs) // TC), int(max(zs) // TC) + 1):
                tgrid[(gx, gz)].append(i)
    cmax = int(big // TC) + 1

    def nearest(t, qx, qz):
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t[0], t[1], t[2]
        d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(d) > 1e-9:
            l1 = ((bz - cz) * (qx - cx) + (cx - bx) * (qz - cz)) / d
            l2 = ((cz - az) * (qx - cx) + (ax - cx) * (qz - cz)) / d
            l3 = 1 - l1 - l2
            if l1 >= 0 and l2 >= 0 and l3 >= 0:
                return (qx, l1 * ay + l2 * by + l3 * cy, qz)
        best = None
        for (p0, p1) in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            ex, ez = p1[0] - p0[0], p1[2] - p0[2]
            ln = ex * ex + ez * ez
            f = 0.0 if ln < 1e-9 else max(0.0, min(1.0, ((qx - p0[0]) * ex + (qz - p0[2]) * ez) / ln))
            x, z = p0[0] + ex * f, p0[2] + ez * f
            dd = (x - qx) ** 2 + (z - qz) ** 2
            if best is None or dd < best[0]:
                best = (dd, (x, p0[1] + (p1[1] - p0[1]) * f, z))
        return best[1]

    # the extra's floors are landed on anywhere, not only at their rims (a sea round a ship: its rim is far)
    inner = []
    for i, t in enumerate(floors):
        if not kind[i]:
            continue
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t[0], t[1], t[2]
        n = max(1, int(max(math.hypot(bx - ax, bz - az), math.hypot(cx - ax, cz - az),
                           math.hypot(cx - bx, cz - bz)) / 24.0))
        for u in range(n + 1):
            for v in range(n + 1 - u):
                w = 1 - (u + v) / n
                inner.append((ax * w + bx * u / n + cx * v / n, ay * w + by * u / n + cy * v / n,
                              az * w + bz * u / n + cz * v / n, i))
    def sideways(t, qx, qz):
        """the point of triangle t on the line z = qz (within a flutter's width of it) nearest qx: a take-off
        for a move along x alone, or None"""
        best = None
        for zz in (qz, qz - L.RAY_OUT, qz + L.RAY_OUT):
            xs = []
            for (p0, p1) in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                if (p0[2] - zz) * (p1[2] - zz) <= 0 and p0[2] != p1[2]:
                    f = (zz - p0[2]) / (p1[2] - p0[2])
                    xs.append((p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * f))
            if len(xs) < 2:
                continue
            (x0, y0), (x1, y1) = min(xs), max(xs)
            x = max(x0, min(x1, qx))
            y = y0 if x1 == x0 else y0 + (y1 - y0) * (x - x0) / (x1 - x0)
            if best is None or abs(x - qx) < abs(best[0] - qx):
                best = (x, y, qz)
        return best

    # a floor under deep water (a bottle on a grotto's bed) isn't landed on: the water's surface is met first
    wet = collections.defaultdict(list)
    for t in tris:
        if t[3] & DEEP and L.solid(t[3]):
            xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
            for gx in range(int(min(xs) // L.CELL), int(max(xs) // L.CELL) + 1):
                for gz in range(int(min(zs) // L.CELL), int(max(zs) // L.CELL) + 1):
                    wet[(gx, gz)].append(t)

    def under_water(q):
        for t in wet.get((int(q[0] // L.CELL), int(q[2] // L.CELL)), ()):
            xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
            if min(xs) <= q[0] <= max(xs) and min(zs) <= q[2] <= max(zs) and nearest(t, q[0], q[2])[1] > q[1] + 1:
                return True
        return False

    def through_zone(p, q, r):
        """the flight from p to q passes through one of the boxes the long flutter is cut short in (the game cuts it
        as Mario enters one: a flight through it ends as Yoshi's would)"""
        lo, hi = min(p[1], q[1]), max(p[1], q[1]) + r.peak
        for z in zl:
            if hi < z[4] or lo > z[5]:
                continue
            for i in range(41):
                x, zz = p[0] + (q[0] - p[0]) * i / 40.0, p[2] + (q[2] - p[2]) * i / 40.0
                if z[0] <= x <= z[2] and z[1] <= zz <= z[3]:
                    return True
        return False

    hreach, xreach = {}, {}
    for q in rim + inner:
        if not kind[q[3]] and under_water(q):
            continue
        b = aid[area_of[q[3]]]
        gx, gz = int(q[0] // TC), int(q[2] // TC)
        seen = set()
        for dx in range(-cmax, cmax + 1):
            for dz in range(-cmax, cmax + 1):
                for ti in tgrid.get((gx + dx, gz + dz), ()):
                    if ti in seen:
                        continue
                    seen.add(ti)
                    a = aid[area_of[ti]]
                    if a == b or ((a, b) in hreach and (a, b) in xreach):
                        continue
                    t = floors[ti]
                    if max(v[1] for v in t[:3]) < q[1] - tops - 1:
                        continue
                    p_any = nearest(t, q[0], q[2])
                    p_side = sideways(t, q[0], q[2])
                    for side, got in ((0, hreach), (1, xreach)):
                        if (a, b) in got:
                            continue
                        for move in reach_for(side, a, b):
                            r, xonly = move[0], move[1]
                            p = p_side if xonly else p_any
                            if p is None:
                                continue
                            dh = q[1] - p[1]
                            if dh > r.top or (len(move) > 2 and move[2] is not None and dh < move[2]):
                                continue
                            if len(move) > 3 and through_zone(p, q, r):
                                continue
                            rd = r.dist(dh)
                            if rd is not None and math.hypot(q[0] - p[0], q[2] - p[2]) <= rd and not blocked(p, q, r):
                                got[(a, b)] = (p, q)
                                break

    def closure(starts, adj):
        seen, todo = set(starts), list(starts)
        while todo:
            x = todo.pop()
            for y in adj.get(x, ()):
                if y not in seen:
                    seen.add(y)
                    todo.append(y)
        return seen

    jbox = {}
    for t in tris:
        bx = jbox.setdefault(t[4], [1e9, 1e9, 1e9, -1e9, -1e9, -1e9])
        for v in t[:3]:
            for k in range(3):
                bx[k] = min(bx[k], v[k])
                bx[k + 3] = max(bx[k + 3], v[k])

    def areas_at(hit):
        bx = jbox.get(hit)
        if bx is None:
            return set()
        out = set()
        for i, t in enumerate(floors):
            if kind[i]:
                continue
            if t[4] == hit:
                out.add(aid[area_of[i]])
                continue
            xs, ys, zs = [v[0] for v in t[:3]], [v[1] for v in t[:3]], [v[2] for v in t[:3]]
            if (min(xs) <= bx[3] + 40 and max(xs) >= bx[0] - 40 and min(zs) <= bx[5] + 40 and max(zs) >= bx[2] - 40
                    and min(ys) <= bx[1] + 20 and max(ys) >= bx[1] - 40):
                out.add(aid[area_of[i]])
        return out

    hadj, xadj = collections.defaultdict(set), collections.defaultdict(set)
    for (a, b) in hreach:
        hadj[a].add(b)
    for (a, b) in xreach:
        xadj[a].add(b)
    for i, k in walk:
        a, b = aid[area_of[i]], aid[area_of[k]]
        xadj[a].add(b)
        xadj[b].add(a)
    starts, exits = set(), collections.defaultdict(list)
    for hit, nmap, nbero in beros:
        here = areas_at(hit)
        if nmap == name and nbero:
            for a in here:
                for b in areas_at(nbero):
                    hadj[a].add(b)
                    xadj[a].add(b)
        else:
            starts |= here
            for a in here:
                exits[a].append(f"{hit} -> {nmap or '?'}")
    if not starts:
        starts = {max((a for a in range(len(areas)) if not akind[a]), key=lambda a: size[a])}
    # a hazard (deep water, spikes) with a return point is a way out: stepped or dropped into from beside or
    # above, Mario is put back there (an area on the floor under the point)
    def area_under(x, y, z):
        best = None
        for i, t in enumerate(floors):
            if kind[i]:
                continue
            xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
            if min(xs) - 1 <= x <= max(xs) + 1 and min(zs) - 1 <= z <= max(zs) + 1:
                fy = nearest(t, x, z)[1]
                if fy <= y + 20 and (best is None or fy > best[0]):
                    best = (fy, aid[area_of[i]])
        return None if best is None else best[1]
    # Deep water is a way out (mot_damage.c): Mario falling in is put back at the return point of the last floor he
    # stood on that has one (hitDmgRetPoints, mario.c's damageHit; y -1000: below the map, so the bottomless reset,
    # the way he came in), or with none at a fixed spot ((0, 0, 0); muj_00, gor_00 and gor_04 their own). Taken
    # here: the take-off floor's own point; one without, on a map that has points, the last one's (somewhere he
    # has been: the way in); on a map with none, the fixed spot. Spikes and lava aren't (they bounce him back).
    fixed = {"muj_00": (400.0, 35.0, -100.0), "gor_00": (0.0, 0.0, -200.0), "gor_04": (146.0, 0.0, 146.0)}
    for t in tris:
        if not (t[3] & DEEP) or not L.solid(t[3]):
            continue
        hx0, hx1 = min(v[0] for v in t[:3]) - 40, max(v[0] for v in t[:3]) + 40
        hz0, hz1 = min(v[2] for v in t[:3]) - 40, max(v[2] for v in t[:3]) + 40
        hy = max(v[1] for v in t[:3])
        # (Sushie swims there unless dropped in from higher than her jump gets back up: pm64_field_pc.c)
        lo = hy + SWIM_DROP if mode == "sushie" and special(mode, t[3]) else hy - 20
        for q in rim:
            if not (hx0 <= q[0] <= hx1 and hz0 <= q[2] <= hz1 and lo <= q[1] <= hy + 400):
                continue
            a = aid[area_of[q[3]]]
            if akind[a]:
                continue
            ret = rets.get(floors[q[3]][4]) or (None if rets else fixed.get(name, (0.0, 0.0, 0.0)))
            back = starts if ret is None or ret[1] < -999.0 else {area_under(*ret)} - {None}
            hadj[a] |= back
            xadj[a] |= back
    rh, rx = closure(starts, hadj), closure(starts, xadj)
    abox = {}
    for i, t in enumerate(floors):
        bx = abox.setdefault(aid[area_of[i]], [1e9, 1e9, 1e9, -1e9, -1e9, -1e9])
        for v in t[:3]:
            for k in range(3):
                bx[k] = min(bx[k], v[k])
                bx[k + 3] = max(bx[k + 3], v[k])

    def contents(b):
        bx, out = abox[b], []

        def inside(x, y, z, r=10.0):
            return bx[0] - r <= x <= bx[3] + r and bx[2] - r <= z <= bx[5] + r and bx[1] - 5 <= y <= bx[4] + 80
        for hit, nmap, nbero in beros:
            j = jbox.get(hit)
            if j and inside((j[0] + j[3]) / 2, j[1], (j[2] + j[5]) / 2, 30.0):
                out.append(f"bero {hit} -> {nmap or '?'}")
        for bkind, bname, x, y, z in blocks:
            if inside(x, y, z) and not label.get(b, "").endswith(bname):
                out.append(f"{bkind} {bname}")
        for iname, item, x, y, z in things:
            if inside(x, y, z):
                out.append(f"item {item}")
        return out

    found = []
    for b in sorted(rx - rh):
        if akind[b] and closure({b}, xadj) & rh:
            continue  # (the water or the hazard itself, with a way out: where it leads is what counts)
        via = [(a, e) for (a, x), e in xreach.items() if x == b and a in rh]
        first = bool(via)
        if not via:
            via = [(a, e) for (a, x), e in xreach.items() if x == b and a in rx]
        p, q = via[0][1] if via else (None, None)
        back = bool(closure({b}, xadj) & rh)
        found.append({
            "area": b, "label": label.get(b, ""), "size": round(size[b]), "gateway": first,
            "from": [round(v) for v in p[:3]] if p else None,
            "to": [round(v) for v in q[:3]] if q else None,
            "dh": round(q[1] - p[1], 1) if p else None,
            "dist": round(math.hypot(q[0] - p[0], q[2] - p[2]), 1) if p else None,
            "has": contents(b), "back": back,
            "exit": any(x in exits for x in closure({b}, xadj)),
        })
    out = {"map": name, "areas": len(areas), "new": found}
    if os.environ.get("PM64_SWEEP_DEBUG"):
        out["debug"] = {
            "areas": {a: (label.get(a, ""), round(size[a]), akind[a], [round(v) for v in abox[a]]) for a in abox},
            "starts": sorted(starts), "host": sorted(rh), "extra": sorted(rx),
            "host_adj": {a: sorted(v) for a, v in hadj.items()}, "extra_adj": {a: sorted(v) for a, v in xadj.items()},
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=("parakarry", "lakilester", "sushie"))
    ap.add_argument("--iso", default="")
    ap.add_argument("--version", default="G8ME01")
    ap.add_argument("--only", default="")
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--json", default="")
    ap.add_argument("--arc", action="store_true")
    a = ap.parse_args()
    host, extra, extra_sp = reaches(a.mode)
    for nm, rs in (("host", host), ("extra", extra), ("extra, from its own floors", extra_sp)):
        for move in rs:
            r, xonly = move[0], move[1]
            print(f"{a.mode} {nm}{' (sideways only)' if xonly else ''}{' (zoned)' if len(move) > 3 else ''}: peak {r.peak:.1f}, catches floors up to "
                  f"{r.top:.1f}, {r.dist(0):.1f} across at the same height, {r.dist(-100):.1f} to 100 below")
    if a.arc:
        return
    iso = a.iso or (glob.glob(os.path.join(ROOT, "orig", a.version, "*.iso")) + [""])[0]
    if not iso or not os.path.exists(iso):
        sys.exit("pm64_field_sweep: no disc image (--iso, or orig/<version>/*.iso)")
    disc = Disc(iso)
    known = L.known_maps()
    names = sorted(f.split("/")[1] for f in disc.files if re.fullmatch(r"m/\w+/d", f) and f.split("/")[1] in known)
    if a.only:
        names = [n for n in names if any(n.startswith(p) for p in a.only.split(","))]
    blocks, doors, stuff, rets, zt = L.mobjs(), beros(), L.items(), returns(), zones(a.mode)
    results = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.j) as ex:
        for r in ex.map(sweep_map, [(iso, n, a.mode, blocks.get(n, []), doors.get(n, []), stuff.get(n, []),
                                     rets.get(n, {}), zt.get(n, [])) for n in names]):
            results.append(r)
            for f in r["new"]:
                stuck = not f["back"] and not f["exit"]
                print(f"{r['map']}: area {f['area']} ({f['label']}, {f['size']} sq) "
                      f"{'opened' if f['gateway'] else 'beyond'}: from {f['from']} to {f['to']}"
                      f" (up {f['dh']}, across {f['dist']})"
                      f"{'; on it: ' + ', '.join(f['has']) if f['has'] else ''}"
                      f"{'' if f['back'] else '; no way back' + (' but a door out' if f['exit'] else '')}"
                      f"{'; STUCK' if stuck else ''}", flush=True)
    n = sum(len(r["new"]) for r in results)
    stuck = sum(1 for r in results for f in r["new"] if not f["back"] and not f["exit"])
    print(f"{a.mode}: {len(results)} maps: {n} areas only the extra reaches, {stuck} with no way back or out")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"mode": a.mode, "maps": results}, f, indent=1)


if __name__ == "__main__":
    main()
