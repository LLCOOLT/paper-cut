#!/usr/bin/env python3
"""Luigi's higher jump (port/src/common/luigi_jump_pc.c): every field map of a disc checked for a
place only the higher jump reaches.

Reads each map's collision (m/<map>/d, as mapdump.py does) and the blocks the maps' scripts place
(rels/*/src/<map>.c: evt_mobj_* calls with plain numbers), from your disc at run time; writes only a
report of positions.

The jumps are simulated frame by frame as the game steps them (marioJumpData's curve, marioJump,
marioFall's landing rays; the constants of luigi_jump_pc.c are read from it, so the two can't
differ), with the air speed's cap (baseDashSpeed 2.0 times marioGetMoveRate's 1.19) for both. That
gives each jump's reach: for a floor dh above the one jumped from, how far away it can be caught.

The walkable floors (under 50 degrees, solid to Mario, not water, lava or spikes) are joined into
areas where they share an edge or meet within a step (11). Each area's rim (its outer edges) is
sampled every few units; for every two areas, Mario's jump and Luigi's are tried from each rim point
of one to the rim points of the other, a tall wall between them (taller than the jump with Mario's
body over it) stopping it. An area is newly reachable from another when it is in what Luigi can get
to from there (jump after jump) and not in what Mario can. Blocks: their tops are areas too, and one
is newly hit from below when Luigi's head at the top of his jump reaches its bottom and Mario's
doesn't.

With --parasol, the same for Play as Peach's parasol float (port/src/common/peach_parasol_pc.c) in
Luigi's place: Mario's jump to its top, then sinking under the parasol at most FLOAT_SINK a frame and
drifting at most FLOAT_DRIFT of her walk for FLOAT_FRAMES, letting go of A at any of those frames (or not
opening it), then falling as Mario does; and that file's kNoFloat (the take-offs she doesn't open it
from) in place of kMarioJumpHere.

usage: luigi_jump_sweep.py [--iso ISO | --version G8ME01] [--only PREFIX,...] [-j N] [--json OUT]
                           [--arc]    (print the two jumps frame by frame and their reach)
                           [--parasol]
"""
import argparse
import collections
import concurrent.futures
import glob
import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gcdisc import Disc  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# --- the jumps --------------------------------------------------------------------------------

AIR_SPEED = 2.0 * 1.19  # mario.c: the air move is capped at baseDashSpeed, times marioGetMoveRate's most
RAY_OUT = 0.375 * 20.0  # marioSearchGround: four rays 0.375 of his width out from his centre
CATCH = 5.0             # ... from 5 units over his feet plus the frame's fall
HEAD = 37.0             # wPlayerCollisionBox.y
PRELUDE = 2             # mot_jump's two frames before the rise (he moves, but doesn't rise)
BLOCK = 25.0            # a block's size (the mobj models' boxes, times 10)
STEP = 11.0             # mario_hit.c's step up


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def zones(parasol=False):
    """map -> [(x0, z0, x1, z1, y0, y1)]: luigi_jump_pc.c's kMarioJumpHere, where Luigi takes off with Mario's jump
    (parasol: peach_parasol_pc.c's kNoFloat, where Peach doesn't float)"""
    src = open(os.path.join(ROOT, "port", "src", "common", "peach_parasol_pc.c" if parasol else "luigi_jump_pc.c"),
               encoding="utf-8").read()
    out = collections.defaultdict(list)
    num = r"\s*(-?[0-9.]+)f?\s*"
    for m in re.finditer(r'\{"(\w+)",%s,%s,%s,%s,%s,%s\}' % ((num,) * 6), src):
        out[m.group(1)].append(tuple(float(m.group(i)) for i in range(2, 8)))
    return out


def consts():
    src = open(os.path.join(ROOT, "port", "src", "common", "luigi_jump_pc.c"), encoding="utf-8").read()
    out = {}
    for name in ("RISE", "APEX_SPEED", "APEX_GRAVITY", "FALL_START"):
        out[name] = float(re.search(r"#define %s ([0-9.]+)f" % name, src).group(1))
    return out


def arc(luigi, c):
    """[(y after the frame, the frame's speed)] from take-off (y 0), the game's steps in f32."""
    k = c["RISE"] if luigi else 1.0
    v, a, j, s = (f32(x * k) for x in (8.0, -0.84, 0.08, -0.02))
    y = 0.0
    out = [(0.0, 0.0)] * PRELUDE
    while True:
        prev = v
        j = f32(j + s)
        a = f32(a + j)
        v = f32(v + a)
        if luigi and 0.0 < v < c["APEX_SPEED"]:
            v = f32(prev + f32(f32(v - prev) * c["APEX_GRAVITY"]))
        if v <= 0.0:
            v, a, j, s = f32(-0.1), f32(-0.2), f32(0.024), f32(-0.004)
            if luigi:
                a = f32(a * c["FALL_START"])
            j = f32(j + s)
            a = f32(a + j)
            v = f32(v + a)
            y = f32(y + v)
            out.append((y, v))
            break
        y = f32(y + v)
        out.append((y, v))
    while y > -400.0:
        j = f32(j + s)
        if j > 0.0:
            j = 0.0
        a = f32(a + j)
        v = f32(v + a)
        if v <= -18.0:
            v, a, j, s = -18.0, 0.0, 0.0, 0.0
        y = f32(y + v)
        out.append((y, v))
    return out


def parasol_consts():
    src = open(os.path.join(ROOT, "port", "src", "common", "peach_parasol_pc.c"), encoding="utf-8").read()
    return {name: float(re.search(r"#define %s ([0-9.]+)f?" % name, src).group(1))
            for name in ("FLOAT_FRAMES", "FLOAT_SINK", "FLOAT_DRIFT")}


def parasol_arc(c, hold):
    """[(y, v, sideways)] of Peach's float, A let go `hold` frames after the top (mario_motion.c marioFall
    with portParasolFall: the fall's curve seeded again each frame from the speed, so letting go falls as
    from the top); sideways: the frame's most, AIR_SPEED or the float's drift (baseWalkSpeed 1)"""
    p = parasol_consts()
    drift = p["FLOAT_DRIFT"] * 1.19  # times marioGetMoveRate's most, as AIR_SPEED
    out = []
    for y, v in arc(False, c):
        out.append((y, v, AIR_SPEED))
        if len(out) > PRELUDE + 1 and v < 0.0:
            break
    y, v = out[-1][0], out[-1][1]
    for _ in range(int(min(hold, p["FLOAT_FRAMES"]))):
        if y <= -400.0:
            break
        v = max(f32(v - 0.18), -p["FLOAT_SINK"])  # (the seeded curve's first step, -0.2 + 0.024 - 0.004)
        y = f32(y + v)
        out.append((y, v, drift))
    a, j, s = f32(-0.2), f32(0.024), f32(-0.004)
    while y > -400.0:
        j = f32(j + s)
        if j > 0.0:
            j = 0.0
        a = f32(a + j)
        v = f32(v + a)
        if v <= -18.0:
            v, a, j, s = -18.0, 0.0, 0.0, 0.0
        y = f32(y + v)
        out.append((y, v, AIR_SPEED))
    return out


def second(c):
    """the jump compared with Mario's: Luigi's, or (c["parasol"]) the best of Peach's floats"""
    if not c.get("parasol"):
        return Reach(arc(True, c))
    n = int(parasol_consts()["FLOAT_FRAMES"])
    return Best([Reach(arc(False, c))] + [Reach(parasol_arc(c, h)) for h in range(0, n + 1, 5)])


class Best:
    """the farthest of several jumps (Peach lets go of A whenever she likes)"""

    def __init__(self, reaches):
        self.rs = reaches
        self.peak = max(r.peak for r in reaches)
        self.head = max(r.head for r in reaches)
        self.top = max(r.top for r in reaches)

    def dist(self, dh):
        ds = [d for d in (r.dist(dh) for r in self.rs) if d is not None]
        return max(ds) if ds else None


class Reach:
    """How far a floor dh over the take-off can be caught (None: not at all), and the head's top."""

    def __init__(self, frames):
        self.xs = None  # sideways by each frame, where it isn't AIR_SPEED's (the float)
        if any(len(f) > 2 for f in frames):
            self.xs = [0.0]
            for f in frames[1:]:
                self.xs.append(self.xs[-1] + f[2])
        frames = [f[:2] for f in frames]
        self.peak = max(y for y, _ in frames)
        self.head = self.peak + HEAD
        self.catch = []  # (frame, the highest floor caught that frame)
        for t in range(1, len(frames)):
            y0, _ = frames[t - 1]
            _, v = frames[t]
            if v < 0.0:
                self.catch.append((t + 1, y0 + CATCH + abs(v)))
        self.top = max(h for _, h in self.catch)

    def dist(self, dh):
        best = None
        for t, h in self.catch:
            if h >= dh:
                best = t
        return None if best is None else (AIR_SPEED * best if self.xs is None or best >= len(self.xs) else self.xs[best]) + RAY_OUT


# --- the map ----------------------------------------------------------------------------------

def u32(b, a):
    return struct.unpack_from(">I", b, a)[0]


def s32(b, a):
    return struct.unpack_from(">i", b, a)[0]


def f3(b, a):
    return struct.unpack_from(">fff", b, a)


def cstr(b, a):
    return b[a:b.index(b"\0", a)].decode("ascii", "replace")


def compose(p, scale, rot, trans):
    """hitdrv.c's _hitEnt: parent * T * Rz * Ry * Rx * S"""
    sx, sy, sz = scale
    rx, ry, rz = (math.radians(r) for r in rot)
    cx, snx = math.cos(rx), math.sin(rx)
    cy, sny = math.cos(ry), math.sin(ry)
    cz, snz = math.cos(rz), math.sin(rz)
    r = [[cz * cy, cz * sny * snx - snz * cx, cz * sny * cx + snz * snx],
         [snz * cy, snz * sny * snx + cz * cx, snz * sny * cx - cz * snx],
         [-sny, cy * snx, cy * cx]]
    l = [r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, trans[0], r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, trans[1],
         r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, trans[2]]
    out = []
    for i in range(3):
        for k in range(4):
            out.append(sum(p[i * 4 + m] * l[m * 4 + k] for m in range(3)) + (p[i * 4 + 3] if k == 3 else 0))
    return out


def apply(m, v):
    x, y, z = v
    return (m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])


def collision(raw):
    """[(v0, v1, v2, attr, joint name)] of the map's hit tree, in game units"""
    _, data_size, rel_count, chunk_count, extern_count = struct.unpack_from(">5i", raw, 0)
    data = raw[0x20:0x20 + data_size]
    at = 0x20 + data_size + rel_count * 4
    chunks = [struct.unpack_from(">II", raw, at + i * 8) for i in range(chunk_count)]
    strings_at = at + chunk_count * 8 + extern_count * 8
    names = {cstr(raw, strings_at + s): o for o, s in chunks}
    info = names["information"]
    root = u32(data, info + 4)
    hit_name = cstr(data, u32(data, info + 0xC)) if u32(data, info + 0xC) else ""
    tris = []

    def mesh(mo, mtx, attr, name):
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
            return apply(mtx, (x * sc, y * sc, z * sc))

        if packed:
            mask = u32(data, mo + 8)
            stride = 2 * bin(mask & 0xFFF).count("1")
            for j in range(n):
                lst = u32(data, mo + 0x10 + j * 8)
                cnt = struct.unpack_from(">H", data, lst + 1)[0]
                idx = [struct.unpack_from(">H", data, lst + 3 + k * stride)[0] for k in range(cnt)]
                for k in range(cnt - 2):
                    o = (idx[k], idx[k + 2], idx[k + 1]) if k % 2 == 0 else (idx[k], idx[k + 1], idx[k + 2])
                    tris.append((pos(o[0]), pos(o[1]), pos(o[2]), attr, name))
            return
        for j in range(n):
            dl = u32(data, mo + 0x10 + j * 4)
            vc = s32(data, dl)
            vs = [pos(struct.unpack_from(">h", data, dl + 4 + k * 0x18)[0]) for k in range(vc)]
            for k in range(vc - 2):
                tris.append((vs[k], vs[k + 1], vs[k + 2], attr, name))

    def walk(o, mtx, in_hit):
        while o:
            name = cstr(data, u32(data, o))
            m = compose(mtx, f3(data, o + 0x18), f3(data, o + 0x24), f3(data, o + 0x30))
            hit = in_hit or (hit_name and name == hit_name)
            if hit:
                draw = u32(data, o + 0x58)
                attr = u32(data, draw + 8) if draw else 0
                for p in range(s32(data, o + 0x5C)):
                    mo = u32(data, o + 0x60 + p * 8 + 4)
                    if mo:
                        mesh(mo, m, attr, name)
            walk(u32(data, o + 0xC), m, hit)
            o = u32(data, o + 0x10)

    walk(root, [10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 10, 0], False)  # the game's maps are drawn at 10x
    return tris


def normal(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t[0], t[1], t[2]
    e0 = (cx - ax, cy - ay, cz - az)  # hitdrv's: (v2 - v0) x (v0 - v1)
    e1 = (ax - bx, ay - by, az - bz)
    n = (e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0])
    ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    return None if ln < 1e-9 else (n[0] / ln, n[1] / ln, n[2] / ln)


PASS = 0x4
PARTY_PASS = 0x800000
HAZARD = 0x200 | 0x400 | 0x800  # deep water, damaging liquid, spikes


def solid(attr):
    return not (attr & PASS) or (attr & PARTY_PASS)


# --- the blocks the scripts place ---------------------------------------------------------------

BLOCKS = ("save_blk", "powerupblk", "badgeblk", "lv_blk", "recovery_blk", "brick", "float_blk", "koopa_brick",
          "itembox", "switch_float_blk", "kururing_floor")
NUM = r"\s*(?:FLOAT\()?(-?[0-9.]+)f?\)?\s*"


def beros():
    """map -> [(hit name, next map, next bero)]: the doors, pipes and edges Mario comes in by (bero tables)"""
    out = collections.defaultdict(list)
    pat = re.compile(r'(NULL|"(\w*)"), // hitName.*?(NULL|"(\w*)"), // nextMapName\s*(NULL|"(\w*)"), // nextBeroName', re.S)
    for path in glob.glob(os.path.join(ROOT, "rels", "*", "src", "**", "*.c"), recursive=True):
        stem = os.path.splitext(os.path.basename(path))[0]
        if not re.fullmatch(r"[a-z]{3}_\d\d", stem):
            continue
        for m in pat.finditer(open(path, encoding="utf-8", errors="replace").read()):
            if m.group(2):
                out[stem].append((m.group(2), m.group(4) or "", m.group(6) or ""))
    return out


def mobjs():
    out = collections.defaultdict(list)
    pat = re.compile(r"CALL\(evt_mobj_(%s),\s*STRING\(\"([^\"]*)\"\),%s,%s,%s[,)]" % ("|".join(BLOCKS), NUM, NUM, NUM))
    for path in glob.glob(os.path.join(ROOT, "rels", "*", "src", "**", "*.c"), recursive=True):
        stem = os.path.splitext(os.path.basename(path))[0]
        if not re.fullmatch(r"[a-z]{3}_\d\d", stem):
            continue
        for m in pat.finditer(open(path, encoding="utf-8", errors="replace").read()):
            out[stem].append((m.group(1), m.group(2), float(m.group(3)), float(m.group(4)), float(m.group(5))))
    return out


ITEM = re.compile(r"CALL\(evt_item_entry,\s*STRING\(\"([^\"]*)\"\),\s*(\w+),%s,%s,%s," % (NUM, NUM, NUM))


def items():
    """map -> [(name, item, x, y, z)]: the items the scripts put down at fixed places"""
    out = collections.defaultdict(list)
    for path in glob.glob(os.path.join(ROOT, "rels", "*", "src", "**", "*.c"), recursive=True):
        stem = os.path.splitext(os.path.basename(path))[0]
        if re.fullmatch(r"[a-z]{3}_\d\d", stem):
            for m in ITEM.finditer(open(path, encoding="utf-8", errors="replace").read()):
                out[stem].append((m.group(1), m.group(2), float(m.group(3)), float(m.group(4)), float(m.group(5))))
    return out


# --- one map ------------------------------------------------------------------------------------

RIM_STEP = 4.0
CELL = 100.0


def sweep_map(args):
    iso, name, blocks, beros, things, c, zl, want = args
    raw = Disc(iso).read(f"m/{name}/d")
    tris = collision(raw)
    mario, luigi = Reach(arc(False, c)), second(c)
    floors, walls = [], []
    for t in tris:
        n = normal(t)
        if n is None or not solid(t[3]):
            continue
        if n[1] >= math.cos(math.radians(50)):
            if not (t[3] & HAZARD):
                floors.append(t)
        elif abs(n[1]) < 0.5:
            walls.append(t)
    # the blocks' tops: two triangles each
    for kind, bname, x, y, z in blocks:
        h = BLOCK / 2
        top = y + (BLOCK if kind not in ("kururing_floor",) else 0.0)
        a, b, cc, d = (x - h, top, z - h), (x + h, top, z - h), (x + h, top, z + h), (x - h, top, z + h)
        floors.append((a, b, cc, 0, f"mobj {kind} {bname}"))
        floors.append((a, cc, d, 0, f"mobj {kind} {bname}"))
    if not floors:
        return {"map": name, "areas": 0, "starts": 0, "new": [], "blocks": []}

    # areas: floors sharing an edge
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
    for tl in edges.values():
        for i in tl[1:]:
            parent[find(i)] = find(tl[0])
    # the rims: edges of one floor only, sampled
    rim = []  # (x, y, z, floor index)
    for (a, b), tl in edges.items():
        if len(tl) != 1:
            continue
        ln = math.hypot(b[0] - a[0], b[2] - a[2])
        steps = max(1, int(ln / RIM_STEP))
        for s in range(steps + 1):
            f = s / steps
            rim.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, tl[0]))
    grid = collections.defaultdict(list)
    for i, p in enumerate(rim):
        grid[(int(p[0] // CELL), int(p[2] // CELL))].append(i)
    # floors that meet within a step (separate meshes side by side, a stair's treads) are one area
    for i, p in enumerate(rim):
        gx, gz = int(p[0] // CELL), int(p[2] // CELL)
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for k in grid.get((gx + dx, gz + dz), ()):
                    q = rim[k]
                    if k > i and abs(q[1] - p[1]) <= STEP and (q[0] - p[0]) ** 2 + (q[2] - p[2]) ** 2 <= 4.0 ** 2:
                        parent[find(q[3])] = find(p[3])
    area_of = [find(i) for i in range(len(floors))]
    areas = sorted(set(area_of))
    aid = {a: n for n, a in enumerate(areas)}
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
        for gx in range(int(min(xs) // CELL), int(max(xs) // CELL) + 1):
            for gz in range(int(min(zs) // CELL), int(max(zs) // CELL) + 1):
                wgrid[(gx, gz)].append(wi)

    def crosses(p, q, w):
        # the segment p-q (top-down) crosses one of the wall's edges or ends inside it
        def side(a, b, c):
            return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        P, Q = (p[0], p[2]), (q[0], q[2])
        vs = [(v[0], v[2]) for v in w[:3]]
        for e in range(3):
            A, B = vs[e], vs[(e + 1) % 3]
            d1, d2, d3, d4 = side(A, B, P), side(A, B, Q), side(P, Q, A), side(P, Q, B)
            if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
                return True
        return False

    def blocked(p, q, reach):
        lo = max(p[1], q[1]) + CATCH
        hi = p[1] + reach.peak + HEAD
        cells = set()
        for f in (0.0, 0.5, 1.0):
            x, z = p[0] + (q[0] - p[0]) * f, p[2] + (q[2] - p[2]) * f
            cells.add((int(x // CELL), int(z // CELL)))
        for cell in cells:
            for wi in wgrid.get(cell, ()):
                w = walls[wi]
                ys = [v[1] for v in w[:3]]
                if min(ys) <= lo and max(ys) >= hi and crosses(p, q, w):
                    return True
        return False

    # jumps between areas: to each rim point of an area, from the nearest point of every floor of another
    # area in reach (the take-off can be anywhere on a floor, not only at its edge)
    mreach, lreach = {}, {}  # (from, to) -> an example (p, q)
    dmax = luigi.dist(-1e9)
    TC = 50.0
    tgrid = collections.defaultdict(list)
    for i, t in enumerate(floors):
        xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
        for gx in range(int(min(xs) // TC), int(max(xs) // TC) + 1):
            for gz in range(int(min(zs) // TC), int(max(zs) // TC) + 1):
                tgrid[(gx, gz)].append(i)
    cmax = int(dmax // TC) + 1

    def nearest(t, qx, qz):
        # the point of triangle t (top-down) nearest (qx, qz), with the floor's height there
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

    def in_zone(p):
        return any(z[0] <= p[0] <= z[2] and z[1] <= p[2] <= z[3] and z[4] <= p[1] <= z[5] for z in zl)

    samples = {}

    def outside(ti, q):
        # the triangle's points (every 3 units) outside the zones, the one nearest q
        if ti not in samples:
            t = floors[ti]
            pts = []
            (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t[0], t[1], t[2]
            n = max(1, int(max(math.hypot(bx - ax, bz - az), math.hypot(cx - ax, cz - az),
                               math.hypot(cx - bx, cz - bz)) / 3.0))
            for i in range(n + 1):
                for j in range(n + 1 - i):
                    u, v = i / n, j / n
                    w = 1 - u - v
                    pt = (ax * w + bx * u + cx * v, ay * w + by * u + cy * v, az * w + bz * u + cz * v)
                    if not in_zone(pt):
                        pts.append(pt)
            samples[ti] = pts
        best = None
        for pt in samples[ti]:
            dd = (pt[0] - q[0]) ** 2 + (pt[2] - q[2]) ** 2
            if best is None or dd < best[0]:
                best = (dd, pt)
        return best[1] if best else None

    zpts = collections.defaultdict(list)
    for q in rim:
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
                    if a == b or ((a, b) in lreach and (a, b) in mreach):
                        continue
                    t = floors[ti]
                    if max(v[1] for v in t[:3]) < q[1] - luigi.top - 1 and min(v[1] for v in t[:3]) > q[1] + 500:
                        continue
                    p = nearest(t, q[0], q[2])
                    dh = q[1] - p[1]
                    if dh > luigi.top:
                        continue
                    d = math.hypot(q[0] - p[0], q[2] - p[2])
                    if d > dmax:
                        continue
                    if (a, b) not in lreach or b in want:
                        lp = p
                        if zl and in_zone(p):
                            lp = outside(ti, q)  # Luigi's take-off nearest q outside the zones
                        if lp is not None:
                            ldh = q[1] - lp[1]
                            ld = luigi.dist(ldh) if ldh <= luigi.top else None
                            ldd = math.hypot(q[0] - lp[0], q[2] - lp[2])
                            if ld is not None and ldd <= ld and not blocked(lp, q, luigi):
                                lreach.setdefault((a, b), (lp, q))
                                if b in want:
                                    zpts[(b, a)].append((q, ld, lp[1]))
                    if (a, b) not in mreach:
                        md = mario.dist(dh)
                        if md is not None and d <= md and not blocked(p, q, mario):
                            mreach[(a, b)] = (p, q)

    def closure(starts, edges_):
        seen, todo = set(starts), list(starts)
        while todo:
            x = todo.pop()
            for y in edges_.get(x, ()):
                if y not in seen:
                    seen.add(y)
                    todo.append(y)
        return seen

    # where Mario comes in: the areas at each door, pipe or edge of the map (its bero table's hit
    # objects); a pipe to another of this map's is a way between its two areas, for both
    jbox = {}
    for t in tris:
        b = jbox.setdefault(t[4], [1e9, 1e9, 1e9, -1e9, -1e9, -1e9])
        for v in t[:3]:
            for k in range(3):
                b[k] = min(b[k], v[k])
                b[k + 3] = max(b[k + 3], v[k])

    def areas_at(hit):
        b = jbox.get(hit)
        if b is None:
            return set()
        out = set()
        for i, t in enumerate(floors):
            if t[4] == hit:
                out.add(aid[area_of[i]])
                continue
            xs, ys, zs = [v[0] for v in t[:3]], [v[1] for v in t[:3]], [v[2] for v in t[:3]]
            if (min(xs) <= b[3] + 40 and max(xs) >= b[0] - 40 and min(zs) <= b[5] + 40 and max(zs) >= b[2] - 40
                    and min(ys) <= b[1] + 20 and max(ys) >= b[1] - 40):
                out.add(aid[area_of[i]])
        return out

    madj, ladj = collections.defaultdict(set), collections.defaultdict(set)
    for (a, b) in mreach:
        madj[a].add(b)
    for (a, b) in lreach:
        ladj[a].add(b)
    starts = set()
    for hit, nmap, nbero in beros:
        here = areas_at(hit)
        if nmap == name and nbero:
            for a in here:
                for b in areas_at(nbero):
                    madj[a].add(b)
                    ladj[a].add(b)
        else:
            starts |= here
    if not starts:
        starts = {max(range(len(areas)), key=lambda a: size[a])}
    rm, rl = closure(starts, madj), closure(starts, ladj)
    abox = {}
    for i, t in enumerate(floors):
        bx = abox.setdefault(aid[area_of[i]], [1e9, 1e9, 1e9, -1e9, -1e9, -1e9])
        for v in t[:3]:
            for k in range(3):
                bx[k] = min(bx[k], v[k])
                bx[k + 3] = max(bx[k + 3], v[k])

    def contents(b):
        # what is on a new area: doors and pipes, blocks, items (by its box, up to a jump over it)
        bx, out = abox[b], []
        def inside(x, y, z, r=10.0):
            return bx[0] - r <= x <= bx[3] + r and bx[2] - r <= z <= bx[5] + r and bx[1] - 5 <= y <= bx[4] + 80
        for hit, nmap, nbero in beros:
            j = jbox.get(hit)
            if j and inside((j[0] + j[3]) / 2, j[1], (j[2] + j[5]) / 2, 30.0):
                out.append(f"bero {hit} -> {nmap or '?'}")
        for kind, bname, x, y, z in blocks:
            if inside(x, y, z) and not label.get(b, "").endswith(bname):
                out.append(f"{kind} {bname}")
        for iname, item, x, y, z in things:
            if inside(x, y, z):
                out.append(f"item {item}")
        return out

    found = []
    for b in sorted(rl - rm):
        # the jump that opens it: one of Luigi's from what Mario reaches (or else from another new area)
        via = [(a, e) for (a, x), e in lreach.items() if x == b and a in rm]
        first = bool(via)
        if not via:
            via = [(a, e) for (a, x), e in lreach.items() if x == b and a in rl]
        p, q = via[0][1] if via else (None, None)
        found.append({
            "area": b, "label": label.get(b, ""), "size": round(size[b]), "gateway": first,
            "mario_entries": sum(1 for (x, y) in mreach if y == b),
            "from": [round(v) for v in p[:3]] if p else None,
            "to": [round(v) for v in q[:3]] if q else None,
            "dh": round(q[1] - p[1], 1) if p else None,
            "dist": round(math.hypot(q[0] - p[0], q[2] - p[2]), 1) if p else None,
            "has": contents(b),
            "back": bool(closure({b}, ladj) & rm),  # Luigi can get back to where Mario goes from it
        })
    # blocks newly hit from below
    bfound = []
    for kind, bname, x, y, z in blocks:
        if kind in ("itembox", "kururing_floor"):
            continue
        best = None
        for t in floors:
            if t[4].startswith("mobj"):
                continue
            xs, zs = [v[0] for v in t[:3]], [v[2] for v in t[:3]]
            r = BLOCK / 2 + RAY_OUT
            if min(xs) - r > x or max(xs) + r < x or min(zs) - r > z or max(zs) + r < z:
                continue
            fy = max(v[1] for v in t[:3])
            if fy < y:
                best = fy if best is None else max(best, fy)
        if best is None:
            continue
        gap = y - best
        if mario.head < gap <= luigi.head:
            bfound.append({"kind": kind, "name": bname, "pos": [x, y, z], "floor": round(best, 1), "gap": round(gap, 1)})
    suggest = []
    for (b, a), pts in sorted(zpts.items()):
        if a not in rl:
            continue
        # every take-off from which the jump reaches one of those rim points: within its reach of it
        x0 = min(q_[0] - r for q_, r, _ in pts)
        x1 = max(q_[0] + r for q_, r, _ in pts)
        z0 = min(q_[2] - r for q_, r, _ in pts)
        z1 = max(q_[2] + r for q_, r, _ in pts)
        ys = [y for _, _, y in pts]
        suggest.append({"area": b, "from_area": a, "from_mario": a in rm,
                        "box": [math.floor(x0 - 6), math.floor(z0 - 6), math.ceil(x1 + 6), math.ceil(z1 + 6),
                                math.floor(min(ys) - 5), math.ceil(max(ys) + 5)]})
    return {"map": name, "areas": len(areas), "starts": len(starts), "new": found, "blocks": bfound,
            "zones": suggest}


def known_maps():
    table = os.path.join(ROOT, "src", "data", "mapdata.c")
    return set(re.findall(r'^MapData \w+ = \{\s*"(\w+)"', open(table, encoding="utf-8").read(), re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="")
    ap.add_argument("--version", default="G8ME01")
    ap.add_argument("--only", default="")
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--json", default="")
    ap.add_argument("--arc", action="store_true")
    ap.add_argument("--parasol", action="store_true", help="Peach's parasol float in place of Luigi's jump")
    ap.add_argument("--zones-for", default="", help="MAP:AREA,...: the take-off boxes that reach those areas")
    a = ap.parse_args()
    c = consts()
    c["parasol"] = a.parasol
    m, l = arc(False, c), (parasol_arc(c, parasol_consts()["FLOAT_FRAMES"]) if a.parasol else arc(True, c))
    rm, rl = Reach(m), second(c)
    land = lambda fr: next(i for i in range(PRELUDE + 1, len(fr)) if fr[i][0] <= 0.0)  # noqa: E731
    print(f"Mario: peak {rm.peak:.2f}, catches floors up to {rm.top:.2f}, head {rm.head:.1f}, back down on frame "
          f"{land(m)}, {rm.dist(0):.1f} across at the same height")
    print(f"Luigi: peak {rl.peak:.2f} (+{100 * (rl.peak / rm.peak - 1):.1f} %), catches up to {rl.top:.2f}, head "
          f"{rl.head:.1f}, back down on frame {land(l)} (+{land(l) - land(m)}), {rl.dist(0):.1f} across")
    if a.arc:
        for i in range(max(land(m), land(l)) + 1):
            print(f"  {i:3d}  Mario {m[i][0]:7.2f}  Luigi {l[i][0]:7.2f}")
        for dh in (-100, -50, -25, 0, 10, 20, 30, 35, 40, 42, 44, 46):
            print(f"  dh {dh:5}: Mario {rm.dist(dh)}  Luigi {rl.dist(dh)}")
        return
    iso = a.iso or (glob.glob(os.path.join(ROOT, "orig", a.version, "*.iso")) + [""])[0]
    if not iso or not os.path.exists(iso):
        sys.exit("luigi_jump_sweep: no disc image (--iso, or orig/<version>/*.iso)")
    disc = Disc(iso)
    known = known_maps()
    names = sorted(f.split("/")[1] for f in disc.files if re.fullmatch(r"m/\w+/d", f) and f.split("/")[1] in known)
    if a.only:
        names = [n for n in names if any(n.startswith(p) for p in a.only.split(","))]
    blocks, doors, stuff, zt = mobjs(), beros(), items(), zones(a.parasol)
    want = collections.defaultdict(set)
    for w in filter(None, a.zones_for.split(",")):
        mp, ar = w.split(":")
        want[mp].add(int(ar))
    results = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.j) as ex:
        for r in ex.map(sweep_map, [(iso, n, blocks.get(n, []), doors.get(n, []), stuff.get(n, []), c, zt.get(n, []), want.get(n, set())) for n in names]):
            results.append(r)
            for f in r["new"]:
                print(f"{r['map']}: area {f['area']} ({f['label']}, {f['size']} sq) "
                      f"{'opened' if f['gateway'] else 'beyond'}: from {f['from']} to {f['to']}"
                      f" (up {f['dh']}, across {f['dist']}); Mario's jumps into it {f['mario_entries']}"
                      f"{'; on it: ' + ', '.join(f['has']) if f['has'] else ''}{'' if f['back'] else '; NO WAY BACK'}", flush=True)
            for z in r["zones"]:
                print(f"{r['map']}: take-offs into area {z['area']} from area {z['from_area']}"
                      f"{' (Mario reaches it)' if z['from_mario'] else ''}: box x z x z y y {z['box']}", flush=True)
            for b in r["blocks"]:
                print(f"{r['map']}: block {b['kind']} {b['name']} at {b['pos']} newly hit from the floor at "
                      f"{b['floor']} (its bottom {b['gap']} up)", flush=True)
    spots = sum(len(r["new"]) + len(r["blocks"]) for r in results)
    print(f"{len(results)} maps, {sum(r['areas'] for r in results)} areas: {spots} spots only the higher jump reaches")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"mario_peak": rm.peak, "luigi_peak": rl.peak, "maps": results}, f, indent=1)


if __name__ == "__main__":
    main()
