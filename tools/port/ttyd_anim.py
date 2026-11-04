#!/usr/bin/env python3
"""The game's model and animation files (a/<name>, src/driver/animdrv.c), in
Python: read, decode, evaluate and write them, byte for byte.

A model file (AnimationPoseData) is a header, the model's tables (shapes,
vertex arrays, draws, textures, the group tree and its node values) and a
table of animations (AnimData). An animation is a list of keyframes, each a
list of deltas from the previous one:

- node values (the groups' transforms, 24 floats a group): an index step, a
  value in 1/16 and two tangent angles (whole degrees, the slope as tan(angle)
  in value units per frame); between keys the value follows the cubic Hermite
  through the next key's deltas (animPoseMain). A delta beyond -128..127 is
  split into chunks at the same index, each with its own tangents (the whole
  slope shared out by the chunk's share of the delta, truncated toward zero);
  an index step over 255 takes 255-steps with an empty delta. An end tangent
  of 89..90 (or -89) makes the chunk a step: it jumps at the key.
- vertex positions and normals (1/16, x y z), linear between keys;
- group visibility (s8, added), applied at the key;
- texture slots: a texture-bind index added and a texture offset.

Pure Python, no dependencies. `python ttyd_anim.py check FILE...` decodes and
rewrites each file and checks the bytes match.
"""
import math
import struct
import sys

# ---------------------------------------------------------------- binary ----

NODE_FIELDS = ("tx ty tz sx sy sz r1x r1y r1z r2x r2y r2z rcx rcy rcz scx scy scz "
               "rtx rty rtz stx sty stz").split()
NODE_SIZE = 24  # floats a node
# parse() checks every file writes back byte for byte; False reads the few that
# don't (other models' rarer encodings) for reference only
STRICT = True

# model tables in file order: (name, record size, header count field index)
# count fields: header words from 0xE8 (see AnimationPoseData)
_C_SHAPE, _C_DRAWCALL, _C_POS, _C_POSIDX, _C_NRM, _C_NRMIDX, _C_CLR, _C_CLRIDX = range(8)
_C_TEXIDX = 8  # 8 of them
_C_TEXVTX, _C_TEXANIM, _C_TEXDATA, _C_TEXBIND, _C_DRAW, _C_VIS, _C_NODE, _C_GROUP, _C_ANIM = range(16, 25)

TABLES = [
    ("shapes", 0xA8, _C_SHAPE), ("drawCalls", 8, _C_DRAWCALL), ("pos", 12, _C_POS), ("posIdx", 4, _C_POSIDX),
    ("nrm", 12, _C_NRM), ("nrmIdx", 4, _C_NRMIDX), ("clr", 4, _C_CLR), ("clrIdx", 4, _C_CLRIDX),
] + [("texIdx%d" % i, 4, _C_TEXIDX + i) for i in range(8)] + [
    ("texVtx", 8, _C_TEXVTX), ("texAnim", 0x18, _C_TEXANIM), ("texData", 8, _C_TEXDATA),
    ("texBinds", 0x40, _C_TEXBIND), ("draws", 0x6C, _C_DRAW), ("vis", 1, _C_VIS), ("node", 4, _C_NODE),
    ("groups", 0x58, _C_GROUP),
]
HEADER_SIZE = 0x1B0
ANIM_HEADER_SIZE = 0x5C


def _align4(n):
    return (n + 3) & ~3


def _cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1")


def _pad_name(s, n):
    b = s.encode("latin-1")
    if len(b) >= n:
        raise ValueError(f"name {s!r} is longer than {n - 1} bytes")
    return b + b"\0" * (n - len(b))


class Group:
    """A node of the group tree (AnimPoseGroup)."""

    def __init__(self, raw):
        self.raw = bytearray(raw)

    name = property(lambda s: _cstr(s.raw[0:64]))
    brother = property(lambda s: struct.unpack_from(">i", s.raw, 0x40)[0])
    child = property(lambda s: struct.unpack_from(">i", s.raw, 0x44)[0])
    shape = property(lambda s: struct.unpack_from(">i", s.raw, 0x48)[0])
    vis = property(lambda s: struct.unpack_from(">i", s.raw, 0x4C)[0])
    node = property(lambda s: struct.unpack_from(">i", s.raw, 0x50)[0])
    ssc = property(lambda s: struct.unpack_from(">i", s.raw, 0x54)[0])

    def set(self, **kw):
        offs = {"brother": 0x40, "child": 0x44, "shape": 0x48, "vis": 0x4C, "node": 0x50, "ssc": 0x54}
        for k, v in kw.items():
            if k == "name":
                self.raw[0:64] = _pad_name(v, 64)
            else:
                struct.pack_into(">i", self.raw, offs[k], v)


class PoseFile:
    """A model file. Tables are lists of raw records (bytes), so anything the
    format has that nothing reads survives as it is; `anims` holds the decoded
    animations (Anim), in table order."""

    def __init__(self):
        self.header = bytearray(HEADER_SIZE)
        self.tables = {}
        self.anims = []

    # header fields
    def _hs(self, off, n):
        return _cstr(self.header[off:off + n])

    file_name = property(lambda s: s._hs(0x4, 64))
    texture_name = property(lambda s: s._hs(0x44, 64))
    build_time = property(lambda s: s._hs(0x84, 64))

    def set_names(self, file_name=None, texture_name=None):
        if file_name is not None:
            self.header[0x4:0x44] = _pad_name(file_name, 64)
        if texture_name is not None:
            self.header[0x44:0x84] = _pad_name(texture_name, 64)

    @property
    def bbox(self):
        return struct.unpack_from(">6f", self.header, 0xD0)

    # convenience views
    def groups(self):
        return [Group(r) for r in self.tables["groups"]]

    def node_values(self):
        return [struct.unpack(">f", r)[0] for r in self.tables["node"]]

    def vis_values(self):
        return [struct.unpack(">b", r)[0] for r in self.tables["vis"]]

    def anim(self, name):
        for a in self.anims:
            if a.name == name:
                return a
        raise KeyError(name)

    @classmethod
    def parse(cls, data):
        p = cls()
        p.header = bytearray(data[:HEADER_SIZE])
        counts = struct.unpack_from(">25I", data, 0xE8)
        ptrs = struct.unpack_from(">25I", data, 0x14C)
        for i, (name, size, ci) in enumerate(TABLES):
            off, n = ptrs[i], counts[ci]
            p.tables[name] = [bytes(data[off + j * size:off + (j + 1) * size]) for j in range(n)]
        aoff, na = ptrs[24], counts[_C_ANIM]
        for j in range(na):
            rec = data[aoff + j * 0x40:aoff + (j + 1) * 0x40]
            name = _cstr(rec[:0x3C])
            doff = struct.unpack_from(">I", rec, 0x3C)[0]
            size = struct.unpack_from(">I", data, doff)[0]
            a = Anim.parse(name, data[doff:doff + size])
            a.table_tail = bytes(rec[len(name):0x3C])  # the name field past the name (zeros on the disc)
            p.anims.append(a)
        p._check_layout(data)
        return p

    def _check_layout(self, data):
        """The writer lays the file out as the disc's files are: every table
        packed in order, 4-aligned, the animations in table order after it."""
        rebuilt = self.build()
        data = bytes(data)
        if len(data) > len(rebuilt) and not data[len(rebuilt):].strip(bytes(1)):
            data = data[:len(rebuilt)]  # read in 32-byte units: zeros after the end
        if rebuilt != data and STRICT:
            raise ValueError("model %s: rewriting it does not give the same bytes" % self.file_name)

    def build(self):
        out = bytearray(HEADER_SIZE)
        out[:] = self.header
        counts = list(struct.unpack_from(">25I", self.header, 0xE8))
        ptrs = [0] * 25
        for i, (name, size, ci) in enumerate(TABLES):
            recs = self.tables[name]
            out.extend(b"\0" * (_align4(len(out)) - len(out)))
            ptrs[i] = len(out)
            counts[ci] = len(recs)
            for r in recs:
                if len(r) != size:
                    raise ValueError(f"{name}: a {len(r)}-byte record, not {size}")
                out.extend(r)
        out.extend(b"\0" * (_align4(len(out)) - len(out)))
        ptrs[24] = len(out)
        counts[_C_ANIM] = len(self.anims)
        table_at = len(out)
        out.extend(b"\0" * (0x40 * len(self.anims)))
        out.extend(b"\0" * (_align4(len(out)) - len(out)))
        struct.pack_into(">I", out, 0, len(out))  # size: the model's part, up to the first AnimData
        for j, a in enumerate(self.anims):
            blob = a.build(getattr(a, "exact", True))
            out.extend(b"\0" * (_align4(len(out)) - len(out)))
            rec = bytearray(0x40)
            nb = a.name.encode("latin-1")
            if len(nb) >= 0x3C:
                raise ValueError(f"animation name {a.name!r} is too long")
            rec[:len(nb)] = nb
            tail = getattr(a, "table_tail", None)
            if tail is not None and len(tail) == 0x3C - len(nb):
                rec[len(nb):0x3C] = tail
            struct.pack_into(">I", rec, 0x3C, len(out))
            out[table_at + j * 0x40:table_at + (j + 1) * 0x40] = rec
            out.extend(blob)
        struct.pack_into(">25I", out, 0xE8, *counts)
        struct.pack_into(">25I", out, 0x14C, *ptrs)
        return bytes(out)


# ------------------------------------------------------------ animations ----

STEP_TAN = 90  # the end tangent a stepped chunk carries


def _trunc_deg(x):
    """degrees, truncated toward zero, as the game's converter wrote them (with
    a little slack so tan/atan round trips don't drop a degree)"""
    d = math.degrees(math.atan(x))
    return int(d + (1e-4 if d >= 0 else -1e-4))


def _tan(a):
    return math.tan(math.radians(a))


def split_value(n):
    """a 1/16 delta as the converter split it: 127 / -128 chunks, then the rest"""
    out = []
    while n > 127:
        out.append(127)
        n -= 127
    while n < -128:
        out.append(-128)
        n += 128
    if n or not out:
        out.append(n)
    return out


def split_vec(v):
    out = []
    v = list(v)
    while any(c > 127 or c < -128 for c in v):
        ch = [127 if c > 127 else -128 if c < -128 else 0 for c in v]
        out.append(tuple(ch))
        v = [a - b for a, b in zip(v, ch)]
    if any(v) or not out:
        out.append(tuple(v))
    return out


class NodeKey:
    """One node value's change at a key: n (the delta, in 1/16), the slopes at
    the segment's start and end (value units a frame, as the game sums them),
    whether it steps, and the disc's chunks when they aren't what encoding
    these values gives (kept so an untouched animation writes back as it was)."""
    __slots__ = ("n", "sa", "sb", "step", "raw")

    def __init__(self, n, sa=0.0, sb=0.0, step=False, raw=None):
        self.n, self.sa, self.sb, self.step, self.raw = n, sa, sb, step, raw

    def chunks(self, exact=True):
        """(value, start tangent, end tangent) chunks. exact: as the game's
        converter would (for an untouched animation); otherwise the angles are
        rounded so the summed slope comes out as close as the bytes allow."""
        if self.raw is not None:
            return list(self.raw)
        vals = split_value(self.n)
        if self.step:
            return [(v, 0, STEP_TAN) for v in vals]
        if exact:
            if not self.n:
                return [(0, _trunc_deg(self.sa), _trunc_deg(self.sb))]
            return [(v, _trunc_deg(self.sa * v / self.n), _trunc_deg(self.sb * v / self.n)) for v in vals]
        return list(zip(vals, _fit_angles(self.sa, vals, self.n), _fit_angles(self.sb, vals, self.n)))

    def effective(self):
        """(start slope, end slope) the game uses for these chunks, None for a step"""
        ch = self.chunks()
        if self.step:
            return None
        return sum(_tan(c[1]) for c in ch), sum(_tan(c[2]) for c in ch)


def _fit_angles(slope, vals, n):
    """whole-degree angles (-88..88) for the chunks whose tangents sum closest to slope"""
    if n == 0:  # no change in value, only a curve (one chunk)
        return [max(-88, min(88, int(round(math.degrees(math.atan(slope))))))]
    angles = []
    for v in vals:
        d = math.degrees(math.atan(slope * v / n))
        angles.append(max(-88, min(88, int(round(d)))))
    # nudge the last chunk to take up the rounding of the others
    rest = slope - sum(_tan(a) for a in angles[:-1])
    angles[-1] = max(-88, min(88, int(round(math.degrees(math.atan(rest))))))
    return angles


def _decode_node_run(run):
    """a list of (value, a, b) chunks at one index -> NodeKey"""
    n = sum(c[0] for c in run)
    if all(c[2] == STEP_TAN and c[1] == 0 for c in run):
        k = NodeKey(n, 0.0, 0.0, True)
    else:
        sa = sum(_tan(c[1]) for c in run if abs(c[1]) < 89)
        sb = sum(_tan(c[2]) for c in run if abs(c[2]) < 89)
        k = NodeKey(n, sa, sb, False)
        # the converter's slope is in an interval only; pick its middle and
        # check that this gives the chunks back, else keep them as they are
        if len(run) > 1 and n:
            mid = _slope_interval(run, n)
            if mid is not None:
                k.sa, k.sb = mid
    if k.chunks() != [tuple(c) for c in run]:
        k.raw = [tuple(c) for c in run]
        if not k.step and len(run) > 1:
            k.sa = sum(_tan(c[1]) for c in run if abs(c[1]) < 89)
            k.sb = sum(_tan(c[2]) for c in run if abs(c[2]) < 89)
    return k


def _slope_interval(run, n):
    res = []
    for t in (1, 2):
        lo, hi = -1e30, 1e30
        for c in run:
            a = c[t]
            if abs(a) >= 89:
                return None
            f = c[0] / n
            if f == 0:
                continue
            if a > 0:
                r = (a, a + 1)
            elif a < 0:
                r = (a - 1, a)
            else:
                r = (-1, 1)
            x0, x1 = _tan(r[0]) / f, _tan(r[1]) / f
            lo, hi = max(lo, min(x0, x1)), min(hi, max(x0, x1))
        if lo > hi:
            return None
        res.append((lo + hi) / 2)
    return tuple(res)


class Key:
    """One keyframe: its time and what changes at it."""

    def __init__(self, time):
        self.time = time
        self.node = {}  # float index -> NodeKey
        self.pos = {}   # vertex index -> (x, y, z) in 1/16
        self.nrm = {}
        self.vis = {}   # visibility index -> added
        self.tex = []   # (slot step, bind added, s offset, t offset[, pad]), in order
        self.raw_pos = None  # disc chunk lists kept when they differ from canonical
        self.raw_nrm = None
        self.raw_vis = None


class Anim:
    def __init__(self, name):
        self.name = name
        self.loop_info = [(0, 0.0, 0.0)]  # (loop, start, end) records; the game reads the first
        self.bbox = (0.0,) * 6
        self.value_keys = [(0.0, 0)]
        self.keys = []

    @property
    def loop(self):
        return bool(self.loop_info[0][0])

    @property
    def start(self):
        return self.loop_info[0][1]

    @property
    def end(self):
        return self.loop_info[0][2]

    @classmethod
    def parse(cls, name, d):
        a = cls(name)
        h = struct.unpack_from(">9I8I6f", d, 0)
        (_size, n_loop, n_key, n_pos, n_nrm, n_tex, n_vis, n_node, n_val) = h[:9]
        p_loop, p_key, p_pos, p_nrm, p_tex, p_vis, p_node, p_val = h[9:17]
        a.bbox = h[17:23]
        a.empty_ptrs = {}  # a few disc files hold a stray pointer for an empty section: kept as it is
        for i, (cnt, ptr) in enumerate(zip(h[1:9], h[9:17])):
            if cnt == 0:
                a.empty_ptrs[i] = ptr
        a.loop_info = [struct.unpack_from(">iff", d, p_loop + 12 * i) for i in range(n_loop)]
        a.value_keys = [struct.unpack_from(">fi", d, p_val + 8 * i) for i in range(n_val)]
        for k in range(n_key):
            kf = struct.unpack_from(">f10i", d, p_key + 0x2C * k)
            key = Key(kf[0])
            # vertex positions / normals
            for which, base, start, cnt in (("pos", p_pos, kf[1], kf[2]), ("nrm", p_nrm, kf[3], kf[4])):
                runs, idx = [], 0
                for j in range(start, start + cnt):
                    s, x, y, z = struct.unpack_from(">Bbbb", d, base + 4 * j)
                    if s == 0 and runs:
                        runs[-1][1].append((x, y, z))
                    else:
                        idx += s
                        runs.append((idx, [(x, y, z)]))
                tgt = getattr(key, which)
                raw = []
                for idx, ch in runs:
                    tot = tuple(sum(c[i] for c in ch) for i in range(3))
                    tgt[idx] = tot
                    raw.append((idx, ch))
                if _encode_vec_runs(tgt) != _vec_raw_bytes(raw):
                    setattr(key, "raw_" + which, raw)
            # texture slots
            for j in range(kf[5], kf[5] + kf[6]):
                s, add, pad, ts, tt = struct.unpack_from(">BBHff", d, p_tex + 12 * j)
                key.tex.append((s, add, ts, tt, pad))  # slot step from the last one; pad: as stored
            # visibility
            raw, idx = [], 0
            for j in range(kf[7], kf[7] + kf[8]):
                s, v = struct.unpack_from(">Bb", d, p_vis + 2 * j)
                idx += s
                raw.append((s, v))
                key.vis[idx] = key.vis.get(idx, 0) + v
            if _encode_vis(key.vis) != raw:
                key.raw_vis = raw
            # node values
            runs, idx = [], 0
            for j in range(kf[9], kf[9] + kf[10]):
                s, v, ta, tb = struct.unpack_from(">Bbbb", d, p_node + 4 * j)
                if s == 0 and runs:
                    runs[-1][1].append((v, ta, tb))
                else:
                    idx += s
                    runs.append((idx, [(v, ta, tb)]))
            for idx, run in runs:
                if run == [(0, 0, 0)]:
                    continue  # a 255-step filler (or an empty delta: the encoder puts those back)
                key.node[idx] = _decode_node_run(run)
            a.keys.append(key)
        if a.build(True) != bytes(d):
            # ours (tools/port/ttyd_blender.py) round the angles; the disc's truncate
            if a.build(False) != bytes(d) and STRICT:
                raise ValueError(f"animation {name}: rewriting it does not give the same bytes")
            a.exact = False
        return a

    def build(self, exact=None):
        if exact is None:
            exact = getattr(self, "exact", True)
        pos, nrm, tex, vis, node, keyrecs = bytearray(), bytearray(), bytearray(), bytearray(), bytearray(), []
        for key in self.keys:
            rec = [key.time]
            for which, buf in (("pos", pos), ("nrm", nrm)):
                raw = getattr(key, "raw_" + which)
                b = _vec_raw_bytes(raw) if raw is not None else _encode_vec_runs(getattr(key, which))
                rec += [len(buf) // 4, len(b) // 4]
                buf.extend(b)
            rec += [len(tex) // 12, len(key.tex)]
            for s, add, ts, tt, *pad in key.tex:
                tex.extend(struct.pack(">BBHff", s, add & 0xFF, pad[0] if pad else 0, ts, tt))
            v = key.raw_vis if key.raw_vis is not None else _encode_vis(key.vis)
            rec += [len(vis) // 2, len(v)]
            for s, x in v:
                vis.extend(struct.pack(">Bb", s, x))
            b = _encode_nodes(key.node, exact)
            rec += [len(node) // 4, len(b) // 4]
            node.extend(b)
            keyrecs.append(rec)
        out = bytearray(ANIM_HEADER_SIZE)
        offs = []
        offs.append(len(out))
        for l in self.loop_info:
            out.extend(struct.pack(">iff", *l))
        offs.append(len(out))
        for r in keyrecs:
            out.extend(struct.pack(">f10i", *r))
        for buf in (pos, nrm, tex, vis, node):
            out.extend(b"\0" * (_align4(len(out)) - len(out)))
            offs.append(len(out))
            out.extend(buf)
        out.extend(b"\0" * (_align4(len(out)) - len(out)))
        offs.append(len(out))
        for t, v in self.value_keys:
            out.extend(struct.pack(">fi", t, v))
        counts = [len(self.loop_info), len(self.keys), len(pos) // 4, len(nrm) // 4, len(tex) // 12,
                  len(vis) // 2, len(node) // 4, len(self.value_keys)]
        for i, ptr in getattr(self, "empty_ptrs", {}).items():
            if counts[i] == 0:
                offs[i] = ptr
        struct.pack_into(">9I8I6f", out, 0, len(out), *counts, *offs, *self.bbox)
        return bytes(out)

    # ------------------------------------------------------- evaluation ----

    def frame_state(self, frame):
        """(keyframe index, next index, blend) the game uses at local frame `frame`
        (animPoseMain's search, given the frame already wrapped)."""
        n = len(self.keys)
        i = 0
        while i < n and self.keys[i].time <= frame:
            i += 1
        k = i - 1
        nk = k + 1
        if not self.loop and nk >= n:
            nk = k
        start = self.keys[k].time
        if self.loop and nk >= n:
            nk = 0
            length = self.end - self.keys[k].time
        else:
            length = self.keys[nk].time - start
        t = 0.0 if length == 0 else (frame - start) / length
        return k, nk, t, length

    def local_frame(self, raw):
        """the game's frame for `raw` frames since the animation started"""
        start, end = self.start, self.end
        length = end - start
        if length == 0:
            return start
        if self.loop:
            tm = raw - start
            if tm < 0:
                return start
            return raw - int(tm / length) * length
        if raw < start:
            return start
        if raw > end:
            return end
        return raw


def _vec_raw_bytes(raw):
    out, last = bytearray(), 0
    for idx, ch in raw:
        for j, c in enumerate(ch):
            step = idx - last if j == 0 else 0
            while step > 255:
                out.extend(struct.pack(">Bbbb", 255, 0, 0, 0))
                step -= 255
                last += 255
            out.extend(struct.pack(">Bbbb", step, *c))
            last = idx
    return bytes(out)


def _encode_vec_runs(d):
    return _vec_raw_bytes([(i, split_vec(d[i])) for i in sorted(d) if any(d[i])])


def _encode_vis(d):
    out, last = [], 0
    for i in sorted(d):
        v = d[i]
        if not v:
            continue
        step = i - last
        while step > 255:
            out.append((255, 0))
            step -= 255
            last += 255
        first = True
        for c in split_value(v):
            out.append((step if first else 0, c))
            first = False
        last = i
    return out


def _encode_nodes(d, exact=True):
    out, last = bytearray(), 0
    for i in sorted(d):
        k = d[i]
        ch = k.chunks(exact)
        if not ch or (len(ch) == 1 and ch[0] == (0, 0, 0)):
            continue
        step = i - last
        while step > 255:
            out.extend(struct.pack(">Bbbb", 255, 0, 0, 0))
            step -= 255
            last += 255
        for j, c in enumerate(ch):
            out.extend(struct.pack(">Bbbb", step if j == 0 else 0, *c))
        last = i
    return bytes(out)


# ------------------------------------------------------ pose evaluation ----

def _f32(x):
    return struct.unpack(">f", struct.pack(">f", x))[0]


class Evaluator:
    """Evaluates an animation of a model as animPoseMain does: node values,
    vertex positions, visibility and texture slots at a frame."""

    def __init__(self, pose):
        self.pose = pose
        self.base_node = pose.node_values()
        self.base_vis = pose.vis_values()
        self.base_pos = [struct.unpack(">3f", r) for r in pose.tables["pos"]]
        self.base_tex = [struct.unpack_from(">B3x5f", r) for r in pose.tables["texAnim"]]

    def at_key(self, anim, k):
        """values after applying keys 0..k (the game's reset loop)"""
        node = list(self.base_node)
        vis = list(self.base_vis)
        pos = [list(p) for p in self.base_pos]
        tex = [[t[0], t[1], t[2]] for t in self.base_tex]
        for key in anim.keys[:k + 1]:
            for i, nk in key.node.items():
                node[i] += nk.n / 16.0
            for i, v in key.pos.items():
                for c in range(3):
                    pos[i][c] += v[c] / 16.0
            for i, v in key.vis.items():
                vis[i] = ((vis[i] + v + 128) & 0xFF) - 128
            slot = 0
            for s, add, ts, tt, *_ in key.tex:
                slot += s
                tex[slot][0] = (tex[slot][0] + add) & 0xFF
                tex[slot][1] += ts
                tex[slot][2] += tt
        return node, vis, pos, tex

    def at_frame(self, anim, frame):
        """everything at local frame `frame` (already wrapped by local_frame)"""
        k, nk, t, _ = anim.frame_state(frame)
        node, vis, pos, tex = self.at_key(anim, k)
        if k != nk and k <= nk:
            nxt = anim.keys[nk]
            for i, v in nxt.pos.items():
                for c in range(3):
                    pos[i][c] += v[c] / 16.0 * t
            dt = nxt.time - anim.keys[k].time
            t2, t3 = t * t, t * t * t
            h01 = -2 * t3 + 3 * t2
            h10 = t + (t3 - 2 * t2)
            h11 = t3 - t2
            for i, nkey in nxt.node.items():
                for v, a, b in nkey.chunks():
                    if -89 < b < 89:
                        # a start angle of 89+ reads past the game's 178-entry
                        # table (a few of Mario's chunks): taken as flat here
                        ta = _tan(a) if -89 < a < 89 else 0.0
                        node[i] += h10 * (dt * ta) + h01 * (v / 16.0) + h11 * (dt * _tan(b))
        return node, vis, pos, tex

    def world_matrices(self, node, vis, root=None):
        """each drawn group's model matrix (4x4 row lists) for node values `node`;
        None for groups the visibility hides"""
        groups = self.pose.groups()
        out = [None] * len(groups)
        stack = [_ident()]

        def walk(parent, gi):
            while gi != -1:
                g = groups[gi]
                if vis[g.vis]:
                    nv = node[g.node:g.node + NODE_SIZE]
                    if g.ssc:
                        if parent == -1 or not groups[parent].ssc:
                            s = (1.0, 1.0, 1.0)
                        else:
                            pn = groups[parent].node
                            s = tuple(node[pn + 3:pn + 6])
                        m = joint_matrix(nv, s)
                    else:
                        m = transform_matrix(nv)
                    stack.append(_mul(stack[-1], m))
                    out[gi] = stack[-1]
                    walk(gi, g.child)
                    stack.pop()
                gi_next = g.brother
                parent, gi = parent, gi_next
        walk(-1, len(groups) - 1 if root is None else root)
        return out


def _ident():
    return [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def _trans(x, y, z):
    m = _ident()
    m[0][3], m[1][3], m[2][3] = x, y, z
    return m


def _scale(x, y, z):
    m = _ident()
    m[0][0], m[1][1], m[2][2] = x, y, z
    return m


def _rot(axis, deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    m = _ident()
    if axis == "x":
        m[1][1], m[1][2], m[2][1], m[2][2] = c, -s, s, c
    elif axis == "y":
        m[0][0], m[0][2], m[2][0], m[2][2] = c, s, -s, c
    else:
        m[0][0], m[0][1], m[1][0], m[1][1] = c, -s, s, c
    return m


def rot_matrix(rx, ry, rz):
    """Rz * Ry * Rx (degrees), PSMTXRotRad order: Blender's XYZ Euler"""
    return _mul(_mul(_rot("z", rz), _rot("y", ry)), _rot("x", rx))


def transform_matrix(n):
    """pushGXModelMtx_TransformNode__: T * (Trt Trc R Trc^-1) * (Tst Tsc S Tsc^-1); rot1 is stored halved"""
    t, s, r1 = n[0:3], n[3:6], n[6:9]
    rc, sc, rt, st = n[12:15], n[15:18], n[18:21], n[21:24]
    S = _scale(*s)
    S = _mul(_mul(_mul(_trans(*st), _trans(*sc)), S), _trans(-sc[0], -sc[1], -sc[2]))
    R = rot_matrix(2 * r1[0], 2 * r1[1], 2 * r1[2])
    R = _mul(_mul(_mul(_trans(*rt), _trans(*rc)), R), _trans(-rc[0], -rc[1], -rc[2]))
    return _mul(_mul(_trans(*t), R), S)


def joint_matrix(n, parent_scale):
    """pushGXModelMtx_JointNode__: T * inverse parent scale * R2 * R1 * S"""
    t, s, r1, r2 = n[0:3], n[3:6], n[6:9], n[9:12]
    px, py, pz = parent_scale
    inv = _scale(1 / px, 1 / py, 1 / pz) if px and py and pz else _ident()
    R = _mul(rot_matrix(r2[0], r2[1], r2[2]), rot_matrix(2 * r1[0], 2 * r1[1], 2 * r1[2]))
    return _mul(_mul(_mul(_trans(*t), inv), R), _scale(*s))


# ------------------------------------------------------------ geometry ----

class Geometry:
    """A model's shapes as polygons: per shape the vertex positions it uses
    (indices into the model's position array), and per draw its triangle fans
    with each corner's position, colour and texture coordinate."""

    def __init__(self, pose):
        t = pose.tables
        self.pos = [struct.unpack(">3f", r) for r in t["pos"]]
        self.nrm = [struct.unpack(">3f", r) for r in t["nrm"]]
        self.clr = [tuple(r) for r in t["clr"]]
        self.texvtx = [struct.unpack(">2f", r) for r in t["texVtx"]]
        idx = lambda name: [struct.unpack(">I", r)[0] & 0xFFFF for r in t[name]]
        self.posidx, self.clridx = idx("posIdx"), idx("clrIdx")
        self.texidx = [idx("texIdx%d" % i) for i in range(8)]
        self.drawcalls = [struct.unpack(">2i", r) for r in t["drawCalls"]]
        self.texdata = [struct.unpack(">2i", r) for r in t["texData"]]
        self.texbinds = [struct.unpack_from(">4xIi", r) for r in t["texBinds"]]
        self.shapes = []
        for r in t["shapes"]:
            name = _cstr(r[:64])
            v = struct.unpack_from(">26i", r, 0x40)
            self.shapes.append({
                "name": name, "pos": (v[0], v[1]), "nrm": (v[2], v[3]), "clr": (v[4], v[5]),
                "tex": [(v[6 + 2 * i], v[7 + 2 * i]) for i in range(8)],
                "draw_start": v[22], "draw_count": v[23], "disp_mode": v[24], "cull": v[25],
            })
        self.draws = []
        for r in t["draws"]:
            v = struct.unpack_from(">i4xi4x8i8B2i11i", r, 0)
            self.draws.append({
                "tex_count": v[0], "draw_mode": v[1], "tex_data": list(v[2:10]), "tex_array": list(v[10:18]),
                "first_call": v[18], "call_count": v[19], "first_pos": v[20], "first_nrm": v[21],
                "first_clr": v[22], "first_tex": list(v[23:31]),
            })

    def shape_polys(self, si):
        """[(draw index, [(pos index (global), colour, uv), ...]), ...] triangle fans of shape si"""
        sh = self.shapes[si]
        out = []
        for di in range(sh["draw_start"], sh["draw_start"] + sh["draw_count"]):
            dr = self.draws[di]
            tarr = dr["tex_array"][0] if dr["tex_count"] else None
            for ci in range(dr["first_call"], dr["first_call"] + dr["call_count"]):
                first, count = self.drawcalls[ci]
                fan = []
                for k in range(count):
                    pi = self.posidx[first + dr["first_pos"] + k] + sh["pos"][0]
                    c = self.clr[self.clridx[first + dr["first_clr"] + k] + sh["clr"][0]]
                    uv = None
                    if tarr is not None:
                        ti = self.texidx[tarr][first + dr["first_tex"][tarr] + k] + sh["tex"][tarr][0]
                        uv = self.texvtx[ti]
                    fan.append((pi, c, uv))
                out.append((di, fan))
        return out


# ------------------------------------------------------------- textures ----

def _rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def decode_cmpr(data, w, h):
    """GX CMPR (DXT1 in 8x8 blocks of four 4x4 sub-blocks, big-endian) to RGBA rows"""
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    off = 0
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for sub in range(4):
                sx, sy = bx + (sub & 1) * 4, by + (sub >> 1) * 4
                c0, c1 = struct.unpack_from(">HH", data, off)
                bits = struct.unpack_from(">I", data, off + 4)[0]
                off += 8
                p0, p1 = _rgb565(c0), _rgb565(c1)
                if c0 > c1:
                    pal = [p0 + (255,), p1 + (255,),
                           tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,),
                           tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)]
                else:
                    pal = [p0 + (255,), p1 + (255,), tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
                for j in range(16):
                    x, y = sx + (j & 3), sy + (j >> 2)
                    sel = (bits >> (30 - 2 * j)) & 3
                    if x < w and y < h:
                        px[y][x] = pal[sel]
    return px


def tpl_images(data):
    """[(w, h, rgba rows, wrap s, wrap t)] of a TPL texture file"""
    magic, n, tab = struct.unpack_from(">III", data, 0)
    if magic != 0x0020AF30:
        raise ValueError("not a TPL file")
    out = []
    for i in range(n):
        th, ph = struct.unpack_from(">II", data, tab + 8 * i)
        h, w, fmt, off, ws, wt = struct.unpack_from(">HHIIII", data, th)
        if fmt != 14:
            raise ValueError(f"texture {i}: format {fmt} isn't decoded here (only CMPR)")
        out.append((w, h, decode_cmpr(data[off:], w, h), ws, wt))
    return out


def write_png(path, rows):
    import zlib
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(c for p in r for c in p) for r in rows)

    def chunk(t, b):
        c = struct.pack(">I", len(b)) + t + b
        return c + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ------------------------------------------------------------------- cli ----

def main(argv):
    if len(argv) >= 2 and argv[0] == "check":
        bad = 0
        for path in argv[1:]:
            data = open(path, "rb").read()
            try:
                p = PoseFile.parse(data)
            except ValueError as e:
                print(f"{path}: {e}")
                bad += 1
                continue
            raw = sum(1 for a in p.anims for k in a.keys for n in k.node.values() if n.raw is not None)
            tot = sum(len(k.node) for a in p.anims for k in a.keys)
            print(f"{path}: {len(p.anims)} animations, byte for byte; node deltas {tot}, "
                  f"{raw} kept as on the disc")
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
