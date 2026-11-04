#!/usr/bin/env python3
"""Luigi's animation pack (port/assets/luigi/luigi_anims.bin) and the model
the port makes from it (docs/LUIGI_ANIMS.md).

The pack holds only our own work: the new animations (in the game's format)
and a hammer for Luigi's model (a plane, its index records and its node). At
startup the port (port/src/common/luigi_model_pc.c) reads the user's disc and
makes two files from it, `a/c_luigi_pc` and `a/c_luigi_pc-`:

- c_luigi's model with the hammer added: its records appended to each table,
  its group put before the root (which stays the last group), then a few
  fields of groups set (the front glove's child: the hammer, so it follows
  the hand); then c_luigi's animations, an animation of the pack replacing one
  of the same name, the rest after them.
- c_luigi's textures, then the hammer drawings copied from a_mario's.

`merge()` here is the same, byte for byte, for the Blender scene and as the
check on the C code (`luigi_pack.py merge` writes the two files).

Pack layout (big-endian):
  0x00 "LUPK", u32 version, u32 base size, u32 base CRC-32 (c_luigi it fits)
  0x10 u32 offset, u32 count per model table (ttyd_anim.TABLES order, 24):
       records appended to that table (groups: put before the root)
  0xD0 u32 count, u32 offset: fields to set, 12 bytes each: record (its
       index in the merged table; a group's, or with the table's
       ttyd_anim.TABLES index + 1 in the top byte another table's, as
       table_patch makes: a variant's texture swap), byte offset in the
       record, value
  0xD8 u32 count, u32 offset: a_mario- texture indices to copy
  0xE0 u32 count, u32 offset: animations, 0x48 each: name[60], offset, size
"""
import struct
import sys
import zlib

import ttyd_anim as T

MAGIC = b"LUPK"
VERSION = 1
LUIGI_SIZE = 49096
LUIGI_CRC = None  # checked against the disc file at merge time (any version: they are the same)
MODEL_NAME = "c_luigi_pc"

# Mario's hammer drawings in a_mario-: wooden, Super, Ultra; four angles each
HAMMER_TEXTURES = list(range(19, 31))
HAMMER_SIZE = (12.0, 17.0)    # the plane, as the drawings' 96x136
HAMMER_PIVOT = (0.0, 1.6, 0.0)  # the handle's end, where the hands hold it
# the hammer is a child of the front glove (pPlane3), its handle's end in the
# fist's middle (the glove drawing's centre, (2.24, 6.69) at rest), just behind it
HAMMER_PARENT = "pPlane3"
HAMMER_REST = (2.24, 6.69 - 1.6, -0.05)
HEADER = 0xE8


def _u32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def _i32(v):
    return struct.pack(">i", v)


def hammer_records(luigi):
    """{table: [records]} to append for the hammer, {"groups": [record]} and the root's new brother"""
    t = luigi.tables
    shape_i = len(t["shapes"])
    call_i = len(t["drawCalls"])
    pos_i = len(t["pos"])
    nrm_i = len(t["nrm"])
    clr_i = len(t["clr"])
    tex_i = len(t["texVtx"])
    slot_i = len(t["texData"])
    bind_i = len(t["texBinds"])
    draw_i = len(t["draws"])
    vis_i = len(t["vis"])
    node_i = len(t["node"])
    pid_i = len(t["posIdx"])
    w, h = HAMMER_SIZE
    px, py, pz = HAMMER_PIVOT
    x0, x1, y0, y1 = -w / 2, w / 2, py - 1.6, py - 1.6 + h
    out = {name: [] for name, _s, _c in T.TABLES}
    # shape: four vertices from pos_i, one draw
    sh = bytearray(0xA8)
    sh[0:64] = T._pad_name("HammerShape_pc", 64)
    vals = [pos_i, 4, 0, 0, 0, 0, tex_i, 4] + [tex_i + 4, 0] * 7 + [draw_i, 1, 0, 3]
    struct.pack_into(">26i", sh, 0x40, *vals)
    out["shapes"].append(bytes(sh))
    out["drawCalls"].append(_i32(0) + _i32(4))
    # corners: bottom left, bottom right, top left, top right; the fan goes 0 1 3 2
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        out["pos"].append(struct.pack(">3f", x, y, pz))
        out["nrm"].append(struct.pack(">3f", 0.0, 0.0, 1.0))
        out["clr"].append(b"\xff\xff\xff\xff")
    for uv in ((0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0)):
        out["texVtx"].append(struct.pack(">2f", *uv))
    for k in (0, 1, 3, 2):
        out["posIdx"].append(_u32(k))
        out["texIdx0"].append(_u32(k))
    for k in range(4):
        out["nrmIdx"].append(_u32(nrm_i + k))
        out["clrIdx"].append(_u32(clr_i + k))
    # its texture slot, starting at the first copied drawing
    out["texAnim"].append(struct.pack(">B3x5f", 0, 0.0, 0.0, 1.0, 1.0, 0.0))
    out["texData"].append(_i32(bind_i) + _i32(0))
    for j in range(len(HAMMER_TEXTURES)):
        b = bytearray(0x40)
        struct.pack_into(">Ii", b, 4, LUIGI_TEXTURES + j, 0)
        out["texBinds"].append(bytes(b))
    dr = bytearray(0x6C)
    struct.pack_into(">i4xi4x8i", dr, 0, 1, 0, slot_i, -1, -1, -1, -1, -1, -1, -1)
    dr[0x4:0x8] = _i32(1)  # as c_luigi's draws have it
    dr[0x30:0x38] = bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
    struct.pack_into(">2i11i", dr, 0x38, call_i, 1, pid_i, pid_i, pid_i, pid_i, 0, 0, 0, 0, 0, 0, 0)
    out["draws"].append(bytes(dr))
    out["vis"].append(b"\x00")  # hidden unless an animation shows it
    node = [HAMMER_REST[0], HAMMER_REST[1], HAMMER_REST[2], 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            px, py, pz, px, py, pz, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    out["node"] = [struct.pack(">f", v) for v in node]
    groups = luigi.groups()
    parent = [i for i, gr in enumerate(groups) if gr.name == HAMMER_PARENT][0]
    g = bytearray(0x58)
    g[0:64] = T._pad_name("Hammer_pc", 64)
    struct.pack_into(">6i", g, 0x40, groups[parent].child, -1, shape_i, vis_i, node_i, 0)
    out["groups"].append(bytes(g))
    hammer = len(t["groups"]) - 1  # the hammer takes the root's old index
    patches = [(parent, 0x44, hammer)]  # the glove's child
    return out, patches


LUIGI_TEXTURES = 27  # c_luigi-'s count; the copied drawings follow


def write_pack(path, anims, luigi):
    """the pack: the hammer's records and `anims` (ttyd_anim.Anim, our own)"""
    recs, patches = hammer_records(luigi)
    data = pack_bytes(anims, luigi, recs, patches, HAMMER_TEXTURES)
    with open(path, "wb") as f:
        f.write(data)
    return data


def table_patch(table, index, off, value):
    """a pack's field to set in a record of another table than the groups'"""
    t = [name for name, _s, _c in T.TABLES].index(table)
    return ((t + 1) << 24 | index, off, value)


def pack_bytes(anims, base, recs=None, patches=(), textures=()):
    """a pack for the model `base` (ttyd_anim.PoseFile, as on the disc): records
    appended to its tables, group fields to set, textures to copy, and `anims`;
    a pack of animations alone when the rest is left out (Luigi's friends')"""
    recs = recs or {name: [] for name, _s, _c in T.TABLES}
    data = base.build()
    body = bytearray()
    head = bytearray(HEADER)
    head[0:4] = MAGIC
    struct.pack_into(">III", head, 4, VERSION, len(data), zlib.crc32(data) & 0xFFFFFFFF)

    def put(blob):
        body.extend(bytes(-len(body) % 4))
        off = HEADER + len(body)
        body.extend(blob)
        return off
    for i, (name, _size, _c) in enumerate(T.TABLES):
        r = recs[name]
        struct.pack_into(">II", head, 0x10 + 8 * i, put(b"".join(r)) if r else 0, len(r))
    struct.pack_into(">II", head, 0xD0, len(patches),
                     put(b"".join(struct.pack(">IIi", *p) for p in patches)))
    struct.pack_into(">II", head, 0xD8, len(textures), put(b"".join(_u32(x) for x in textures)))
    blobs = [a.build() for a in anims]
    toff = put(bytes(0x48 * len(anims)))
    for j, (a, blob) in enumerate(zip(anims, blobs)):
        e = bytearray(0x48)
        e[0:60] = T._pad_name(a.name, 60)
        struct.pack_into(">II", e, 60, put(blob), len(blob))
        body[toff - HEADER + 0x48 * j:toff - HEADER + 0x48 * (j + 1)] = e
    struct.pack_into(">II", head, 0xE0, len(anims), toff)
    return bytes(head) + bytes(body)


def read_pack(data):
    if data[:4] != MAGIC:
        raise ValueError("not a Luigi pack")
    ver, size, crc = struct.unpack_from(">III", data, 4)
    tables = {}
    for i, (name, rsize, _c) in enumerate(T.TABLES):
        off, n = struct.unpack_from(">II", data, 0x10 + 8 * i)
        tables[name] = [data[off + j * rsize:off + (j + 1) * rsize] for j in range(n)]
    npatch, poff = struct.unpack_from(">II", data, 0xD0)
    patches = [struct.unpack_from(">IIi", data, poff + 12 * j) for j in range(npatch)]
    ntex, toff = struct.unpack_from(">II", data, 0xD8)
    textures = [struct.unpack_from(">I", data, toff + 4 * j)[0] for j in range(ntex)]
    na, aoff = struct.unpack_from(">II", data, 0xE0)
    anims = []
    for j in range(na):
        e = data[aoff + 0x48 * j:aoff + 0x48 * (j + 1)]
        name = T._cstr(e[:60])
        off, sz = struct.unpack_from(">II", e, 60)
        anims.append((name, data[off:off + sz]))
    return {"version": ver, "size": size, "crc": crc, "tables": tables, "patches": patches,
            "textures": textures, "anims": anims}


def merge(luigi_data, luigi_tpl, mario_tpl, pack_data):
    """(model, textures) as the port makes them; None if c_luigi isn't the one the pack fits"""
    pk = read_pack(pack_data)
    model = merge_model(luigi_data, pk, MODEL_NAME)
    if model is None:
        return None
    tex = merge_tpl(luigi_tpl, mario_tpl, pk["textures"])
    return model, _pad32(tex)


def merge_model(base_data, pk, model_name=None):
    """the model file `base_data` with the pack `pk` (read_pack) merged in, named
    `model_name` (else as it was); None if it isn't the model the pack was made for"""
    if len(base_data) != pk["size"] or zlib.crc32(base_data) & 0xFFFFFFFF != pk["crc"]:
        return None
    p = T.PoseFile.parse(base_data)
    for name, _s, _c in T.TABLES:
        add = pk["tables"][name]
        if name == "groups":
            p.tables[name] = p.tables[name][:-1] + add + p.tables[name][-1:]
        else:
            p.tables[name] = p.tables[name] + add
    for gi, off, value in pk["patches"]:
        table = T.TABLES[(gi >> 24) - 1][0] if gi >> 24 else "groups"
        gi &= 0xFFFFFF
        g = bytearray(p.tables[table][gi])
        struct.pack_into(">i", g, off, value)
        p.tables[table][gi] = bytes(g)
    by_name = {a.name: i for i, a in enumerate(p.anims)}
    for name, blob in pk["anims"]:
        a = T.Anim.parse(name, blob)  # (checks the blob; written back as it is)
        a.build = lambda exact=None, b=bytes(blob): b
        if name in by_name:
            a.table_tail = None
            p.anims[by_name[name]] = a
        else:
            p.anims.append(a)
    for a in p.anims:
        a.table_tail = None  # names end in zeros
    if model_name is not None:
        p.set_names(model_name, model_name)
    return _pad32(p.build())


# Luigi's friends (docs/LUIGI_ANIMS.md, "Luigi's friends"): their animations, one
# pack each (animations alone), in one file, port/assets/luigi/friend_anims.bin:
#   0x00 "LUFR", u32 version, u32 count; then per model 0x28 bytes: name[32]
#   (the disc's a/<name>), u32 offset, u32 size of its pack (LUPK, as above)
# The port merges each into the disc's own file, under its own name. An entry
# that isn't a pack is a whole file of our own (a new model "c_lp_statue" or
# its textures "c_lp_statue-"), which the port adds as a/<name> as it is.
FRIENDS_MAGIC = b"LUFR"


def write_friends(path, packs):
    """packs: [(model name, pack bytes)]"""
    head = bytearray(12 + 0x28 * len(packs))
    head[0:4] = FRIENDS_MAGIC
    struct.pack_into(">II", head, 4, VERSION, len(packs))
    body = bytearray()
    for j, (name, blob) in enumerate(packs):
        at = len(head) + len(body)
        e = 12 + 0x28 * j
        head[e:e + 32] = T._pad_name(name, 32)
        struct.pack_into(">II", head, e + 32, at, len(blob))
        body.extend(blob)
        body.extend(bytes(-len(body) % 4))
    data = bytes(head) + bytes(body)
    with open(path, "wb") as f:
        f.write(data)
    return data


def read_friends(data):
    """[(model name, pack bytes)]"""
    if data[:4] != FRIENDS_MAGIC:
        raise ValueError("not a friends pack")
    _ver, n = struct.unpack_from(">II", data, 4)
    out = []
    for j in range(n):
        e = 12 + 0x28 * j
        off, size = struct.unpack_from(">II", data, e + 32)
        out.append((T._cstr(data[e:e + 32]), data[off:off + size]))
    return out


def merge_friends(disc_dir, friends_data):
    """{model name: merged model bytes (None if the disc's isn't the one it fits)},
    as the port makes them"""
    out = {}
    for name, blob in read_friends(friends_data):
        if blob[:4] == TX_MAGIC:
            out[name] = apply_tx(open("%s/%s" % (disc_dir, name), "rb").read(), blob)
            continue
        if blob[:4] != MAGIC:
            out[name] = blob  # a whole file of our own (a new model, its textures): as it is
            continue
        out[name] = merge_model(open("%s/%s" % (disc_dir, name), "rb").read(), read_pack(blob))
    return out


# ------------------------------------------------- textures made at startup ----
# A friends' container entry can also be a texture recipe (LUTX) for a disc
# TPL: the port reads the disc's TPL, keeps its textures and appends textures
# the recipe makes, so a drawing made from the disc's (burnt Blooey, soot
# black) is never in the repository. Layout (big-endian):
#   "LUTX", u32 version, u32 base size, u32 base CRC-32, u32 op count, then
#   per op: u32 kind, u32 payload size, payload (4-aligned):
#     kind 1, soot: u32 source texture index, u32 spot count, spots (i32 x, y,
#             radius in texels): the source darkened to soot, ember spots on it
#     kind 2, a texture of our own: u16 height, u16 width, u32 format (14),
#             the image data (CMPR)
# Appended textures follow the disc's, in op order. CMPR is decoded and
# encoded with integers only, the same in luigi_model_aurora.c.
TX_MAGIC = b"LUTX"
SOOT = 1
OWN = 2
EMBER = ((255, 140, 40), (255, 214, 120))  # the glow's rim, its core


def _expand565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def cmpr_decode(data, w, h):
    """RGBA rows (alpha 0 or 255), as the GameCube does"""
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    off = 0
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for sub in range(4):
                sx, sy = bx + (sub & 1) * 4, by + (sub >> 1) * 4
                c0, c1, bits = struct.unpack_from(">HHI", data, off)
                off += 8
                p0, p1 = _expand565(c0), _expand565(c1)
                if c0 > c1:
                    pal = [p0 + (255,), p1 + (255,), tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,),
                           tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)]
                else:
                    pal = [p0 + (255,), p1 + (255,), tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,),
                           (0, 0, 0, 0)]
                for j in range(16):
                    x, y = sx + (j & 3), sy + (j >> 2)
                    if x < w and y < h:
                        px[y][x] = pal[(bits >> (30 - 2 * j)) & 3]
    return px


def _to565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def _cmpr_block(px):
    opaque = [p for p in px if p[3] >= 128]
    if not opaque:
        return struct.pack(">HHI", 0, 0, 0xFFFFFFFF)
    alpha = len(opaque) < 16
    lum = [299 * p[0] + 587 * p[1] + 114 * p[2] for p in opaque]
    lo = opaque[lum.index(min(lum))]
    hi = opaque[lum.index(max(lum))]
    c0, c1 = _to565(hi), _to565(lo)
    if alpha:
        if c0 > c1:
            c0, c1 = c1, c0
        p0, p1 = _expand565(c0), _expand565(c1)
        pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1))]
    else:
        if c0 == c1:
            return struct.pack(">HHI", c0, c1, 0)
        if c0 < c1:
            c0, c1 = c1, c0
        p0, p1 = _expand565(c0), _expand565(c1)
        pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    bits = 0
    for j, p in enumerate(px):
        if alpha and p[3] < 128:
            sel = 3
        else:
            best, sel = None, 0
            for k, q in enumerate(pal):
                d = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2
                if best is None or d < best:
                    best, sel = d, k
        bits |= sel << (30 - 2 * j)
    return struct.pack(">HHI", c0, c1, bits)


def cmpr_encode(rows):
    """RGBA rows (sizes multiples of 8) to CMPR"""
    h, w = len(rows), len(rows[0])
    out = bytearray()
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for sub in range(4):
                sx, sy = bx + (sub & 1) * 4, by + (sub >> 1) * 4
                out += _cmpr_block([rows[sy + (j >> 2)][sx + (j & 3)] if sx + (j & 3) < w and sy + (j >> 2) < h
                                    else (0, 0, 0, 0) for j in range(16)])
    return bytes(out)


def soot(rows, spots):
    """a drawing burnt black: each texel's brightness kept as a dark soot grey
    (warm), ember spots (x, y, radius) glowing orange to a yellow core"""
    out = []
    for y, row in enumerate(rows):
        o = []
        for x, (r, g, b, a) in enumerate(row):
            if a < 128:
                o.append((r, g, b, a))
                continue
            s = 16 + (299 * r + 587 * g + 114 * b) // 1000 * 28 // 100
            c = [s + 8, s + 3, s]
            for sx, sy, rad in spots:
                d2 = (x - sx) * (x - sx) + (y - sy) * (y - sy)
                if d2 < rad * rad:
                    t = 255 - d2 * 255 // (rad * rad)
                    tgt = EMBER[1] if t > 190 else EMBER[0]
                    c = [c[i] + (tgt[i] - c[i]) * t // 255 for i in range(3)]
            o.append((c[0], c[1], c[2], 255))
        out.append(o)
    return out


def make_tx(base_tpl, ops):
    """a recipe for base_tpl (the disc's bytes): ops [(SOOT, source, spots) |
    (OWN, rows)]"""
    body = bytearray()
    for op in ops:
        if op[0] == SOOT:
            p = struct.pack(">II", op[1], len(op[2])) + b"".join(struct.pack(">iii", *s) for s in op[2])
        else:
            rows = op[1]
            p = struct.pack(">HHI", len(rows), len(rows[0]), 14) + cmpr_encode(rows)
        body += struct.pack(">II", op[0], len(p)) + p
        body += bytes(-len(body) % 4)
    head = TX_MAGIC + struct.pack(">IIII", VERSION, len(base_tpl), zlib.crc32(base_tpl) & 0xFFFFFFFF, len(ops))
    return bytes(head) + bytes(body)


def apply_tx(base_tpl, tx):
    """the TPL the port makes: base_tpl's textures and the recipe's; None if it
    isn't the TPL the recipe was made for"""
    _v, size, crc, n = struct.unpack_from(">IIII", tx, 4)
    if len(base_tpl) != size or zlib.crc32(base_tpl) & 0xFFFFFFFF != crc:
        return None
    ents = _tpl_entries(base_tpl)
    at = 20
    for _ in range(n):
        kind, ln = struct.unpack_from(">II", tx, at)
        p = tx[at + 8:at + 8 + ln]
        at += 8 + ln + (-ln % 4)
        hdr = bytearray(0x24)
        if kind == SOOT:
            src, ns = struct.unpack_from(">II", p, 0)
            spots = [struct.unpack_from(">iii", p, 8 + 12 * k) for k in range(ns)]
            shdr, sdata = ents[src]
            h, w = struct.unpack_from(">HH", shdr, 0)
            hdr[:] = shdr
            ents.append((hdr, cmpr_encode(soot(cmpr_decode(sdata, w, h), spots))))
        else:
            h, w, fmt = struct.unpack_from(">HHI", p, 0)
            struct.pack_into(">HHIIIIII", hdr, 0, h, w, fmt, 0, 0, 0, 1, 1)
            ents.append((hdr, p[8:]))
    return _pad32(layout_tpl(ents))


def plane_records(base, name, parent_name, rect, z, pivot, bind, visible=False, pre=None):
    """({table: records}, patches): a textured plane `name` added to the model
    `base` (ttyd_anim.PoseFile, as on the disc) as the last child of the group
    `parent_name`, its corners rect (x0, y0, x1, y1), its texture slot showing
    texture `bind` of the TPL; hidden unless an animation shows it. As Luigi's
    hammer (hammer_records), for any model; `pre`: records appended before these"""
    pre = pre or {}
    t = {n: base.tables[n] + pre.get(n, []) for n, _s, _c in T.TABLES}
    shape_i, call_i = len(t["shapes"]), len(t["drawCalls"])
    pos_i, nrm_i, tex_i = len(t["pos"]), len(t["nrm"]), len(t["texVtx"])
    slot_i, bind_i, draw_i = len(t["texData"]), len(t["texBinds"]), len(t["draws"])
    vis_i, node_i, pid_i = len(t["vis"]), len(t["node"]), len(t["posIdx"])
    # each index and vertex table on its own: a disc model's needn't number its
    # positions (c_heavy's untextured planes have no texture indices)
    nid_i, cid_i, tid_i, clr_i = len(t["nrmIdx"]), len(t["clrIdx"]), len(t["texIdx0"]), len(t["clr"])
    out = {n: [] for n, _s, _c in T.TABLES}
    sh = bytearray(0xA8)
    sh[0:64] = T._pad_name(name + "Shape", 64)
    struct.pack_into(">26i", sh, 0x40, *([pos_i, 4, 0, 0, 0, 0, tex_i, 4] + [tex_i + 4, 0] * 7 + [draw_i, 1, 0, 3]))
    out["shapes"].append(bytes(sh))
    out["drawCalls"].append(_i32(0) + _i32(4))
    x0, y0, x1, y1 = rect
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        out["pos"].append(struct.pack(">3f", x, y, z))
        out["nrm"].append(struct.pack(">3f", 0.0, 0.0, 1.0))
        out["clr"].append(b"\xff\xff\xff\xff")
    for uv in ((0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0)):
        out["texVtx"].append(struct.pack(">2f", *uv))
    for k in (0, 1, 3, 2):
        out["posIdx"].append(_u32(k))
        out["texIdx0"].append(_u32(k))
    for k in range(4):
        out["nrmIdx"].append(_u32(nrm_i + k))
        out["clrIdx"].append(_u32(clr_i + k))
    out["texAnim"].append(struct.pack(">B3x5f", 0, 0.0, 0.0, 1.0, 1.0, 0.0))
    out["texData"].append(_i32(bind_i) + _i32(0))
    b = bytearray(0x40)
    struct.pack_into(">Ii", b, 4, bind, 0)
    out["texBinds"].append(bytes(b))
    dr = bytearray(0x6C)
    struct.pack_into(">i4xi4x8i", dr, 0, 1, 0, slot_i, -1, -1, -1, -1, -1, -1, -1)
    dr[0x4:0x8] = _i32(1)
    dr[0x30:0x38] = bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
    struct.pack_into(">2i11i", dr, 0x38, call_i, 1, pid_i, nid_i, cid_i, tid_i, 0, 0, 0, 0, 0, 0, 0)
    out["draws"].append(bytes(dr))
    out["vis"].append(b"\x01" if visible else b"\x00")
    px, py = pivot
    node = [0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, px, py, z, px, py, z,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    out["node"] = [struct.pack(">f", v) for v in node]
    groups = base.groups()  # (groups added before aren't parents here)
    parent = [i for i, gr in enumerate(groups) if gr.name.lstrip("|") == parent_name][0]
    g = bytearray(0x58)
    g[0:64] = T._pad_name(name, 64)
    struct.pack_into(">6i", g, 0x40, groups[parent].child, -1, shape_i, vis_i, node_i, 0)
    out["groups"].append(bytes(g))
    new = len(t["groups"]) - 1  # it takes the root's old index (the root stays last)
    return out, [(parent, 0x44, new)]


def bind_records(texture):
    """one more texture bind (to `texture` of the TPL), appended to the binds:
    with a slot's binds the table's last, the tex channel's next value"""
    out = {nm: [] for nm, _s, _c in T.TABLES}
    b = bytearray(0x40)
    struct.pack_into(">Ii", b, 4, texture, 0)
    out["texBinds"].append(bytes(b))
    return out


def add_records(*recs):
    """record dicts appended in order"""
    out = {n: [] for n, _s, _c in T.TABLES}
    for r in recs:
        for n in out:
            out[n] += r.get(n, [])
    return out


def _pad32(b):
    return b + b"\0" * (-len(b) % 32)


def _tpl_entries(data):
    magic, n, tab = struct.unpack_from(">III", data, 0)
    out = []
    for i in range(n):
        th, ph = struct.unpack_from(">II", data, tab + 8 * i)
        if ph:
            raise ValueError("palettes aren't merged")
        hdr = bytearray(data[th:th + 0x24])
        h, w, fmt, off = struct.unpack_from(">HHII", hdr, 0)
        size = _tex_size(w, h, fmt)
        out.append((hdr, data[off:off + size]))
    return out


def _tex_size(w, h, fmt):
    bw, bh, bpp = {14: (8, 8, 4), 0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16),
                   5: (4, 4, 16), 6: (4, 4, 32)}[fmt]
    return ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * bw * bh * bpp // 8


def merge_tpl(a, b, pick):
    """TPL a's textures, then those of b at the indices `pick`; laid out as the disc's TPLs are"""
    ents = _tpl_entries(a)
    eb = _tpl_entries(b)
    ents += [eb[i] for i in pick]
    return layout_tpl(ents)


def layout_tpl(ents):
    """a TPL of [(0x24-byte header, image data)], laid out as the disc's TPLs are"""
    n = len(ents)
    out = bytearray(0xC + 8 * n + 0x24 * n)
    struct.pack_into(">III", out, 0, 0x0020AF30, n, 0xC)
    data_at = (len(out) + 31) & ~31
    out.extend(b"\0" * (data_at - len(out)))
    for i, (hdr, img) in enumerate(ents):
        hoff = 0xC + 8 * n + 0x24 * i
        struct.pack_into(">II", out, 0xC + 8 * i, hoff, 0)
        hdr = bytearray(hdr)
        out.extend(b"\0" * (-len(out) % 32))
        struct.pack_into(">I", hdr, 8, len(out))
        out[hoff:hoff + 0x24] = hdr
        out.extend(img)
    return bytes(out)


def main(argv):
    import argparse
    import os
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("merge", help="write c_luigi_pc and c_luigi_pc- as the port makes them")
    m.add_argument("--disc", required=True)
    m.add_argument("--pack", required=True)
    m.add_argument("--out", required=True)
    e = sub.add_parser("empty", help="a pack with the hammer and no animations")
    e.add_argument("--disc", required=True)
    e.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "merge":
        rd = lambda n: open(os.path.join(a.disc, n), "rb").read()
        res = merge(rd("c_luigi"), rd("c_luigi-"), rd("a_mario-"), open(a.pack, "rb").read())
        if res is None:
            print("c_luigi isn't the model the pack was made for")
            return 1
        os.makedirs(a.out, exist_ok=True)
        for name, blob in zip((MODEL_NAME, MODEL_NAME + "-"), res):
            with open(os.path.join(a.out, name), "wb") as f:
                f.write(blob)
        print("%s: %d bytes, %s-: %d bytes" % (MODEL_NAME, len(res[0]), MODEL_NAME, len(res[1])))
    else:
        luigi = T.PoseFile.parse(open(os.path.join(a.disc, "c_luigi"), "rb").read())
        write_pack(a.out, [], luigi)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
