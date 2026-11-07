#!/usr/bin/env python3
"""The animation packs of Play as Luigi, Peach and Bowser
(port/assets/luigi/*_anims.bin) and the models the port makes from them
(docs/LUIGI_ANIMS.md, docs/PLAY_AS.md).

A pack holds only our own work: new animations (in the game's format) and a
hammer for one of the disc's models (a plane, its index records and its
node). At startup the port (port/src/common/luigi_model_pc.c) reads the
user's disc and makes two files from each pack, `a/<ours>` and `a/<ours>-`
(c_luigi_pc from c_luigi, c_peach_pc from c_peach, c_peach_r_pc from
c_peach_r, c_koopa_pc from c_koopa):

- the disc's model with the hammer added: its records appended to each
  table, its group put before the root (which stays the last group), then a
  few fields of groups set (the hand's child: the hammer, so it follows the
  hand); then the model's animations, an animation of the pack replacing one
  of the same name, the rest after them.
- the model's textures, then the hammer drawings copied from a_mario's.

`merge()` here is the same, byte for byte, for the Blender scene and as the
check on the C code (`luigi_pack.py merge` writes the two files).

Pack layout (big-endian):
  0x00 "LUPK", u32 version, u32 base size, u32 base CRC-32 (the model it fits)
  0x10 u32 offset, u32 count per model table (ttyd_anim.TABLES order, 24):
       records appended to that table (groups: put before the root)
  0xD0 u32 count, u32 offset: fields to set, 12 bytes each: the record (its
       index in the merged table; groups', or in the top byte the table
       plus 1: node values, say), byte offset in the record, value
  0xD8 u32 count, u32 offset: a_mario- texture indices to copy
  0xE0 u32 count, u32 offset: animations, 0x48 each: name[60], offset, size
"""
import os
import struct
import sys
import zlib

import ttyd_anim as T

MAGIC = b"LUPK"
VERSION = 1
HEADER = 0xE8
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# Mario's hammer drawings in a_mario-: wooden, Super, Ultra; four angles each
HAMMER_TEXTURES = list(range(19, 31))


class Char:
    """A model a pack fits: the disc's, ours, and where the hammer goes. The
    hammer is a child of `parent` (the hand that holds it), its handle's end
    at `grip` at rest (the fist's middle), at depth `z` (just behind the
    hand); at `scale` 1 the plane is 12 x 17, as the drawings' 96 x 136."""

    def __init__(self, key, base, size, model_name, textures, parent, grip, z, scale=1.0, extra=None):
        self.key = key
        self.base = base              # the disc's model (a/<base>, a/<base>-)
        self.size = size              # its size (the pack's header has its CRC too)
        self.model_name = model_name  # ours (a/<model_name>, a/<model_name>-)
        self.textures = textures      # the base TPL's count: the copied drawings follow
        self.parent = parent
        self.hammer_size = (12.0 * scale, 17.0 * scale)
        self.pivot = (0.0, 1.6 * scale, 0.0)  # the handle's end, where the hands hold it
        self.rest = (grip[0], grip[1] - 1.6 * scale, z)
        self.pack = os.path.join(REPO, "port", "assets", "luigi", "%s_anims.bin" % key)
        self.extra = extra  # (base, records, fields) -> more of each: a model made over the disc's


# Luigi: the front glove (pPlane3); the fist's middle is the glove drawing's
# centre, (2.24, 6.69) at rest
LUIGI = Char("luigi", "c_luigi", 49096, "c_luigi_pc", 27, "pPlane3", (2.24, 6.69), -0.05)
# Peach: her front glove (pPlane25: forearm and hand drawn as one, the elbow
# its pivot), the hand's middle at rest, behind the glove (z 1.4) and in
# front of the sleeve (1.2)
PEACH = Char("peach", "c_peach", 154192, "c_peach_pc", 102, "pPlane25", (-3.0, 8.0), 1.35)
# her back-facing model: her left glove (pPlane20), behind her body
PEACH_R = Char("peach_r", "c_peach_r", 42260, "c_peach_r_pc", 31, "pPlane20", (-2.0, 6.7), -0.75)
# Bowser: his front hand (L_hand), the fist's middle; a hammer his size (he
# is drawn at 0.7 in Mario's place: at 1.6 it is a little over Mario's)
BOWSER = Char("bowser", "c_koopa", 110736, "c_koopa_pc", 41, "L_hand", (-12.6, 13.0), -0.05, 1.6)


def _bowser_back(base, recs, patches):
    import bowser_back
    bowser_back.records(base, recs, patches)


# his back view, made over c_koopa (bowser_back.py): the same hand
BOWSER_R = Char("bowser_r", "c_koopa", 110736, "c_koopa_r_pc", 41, "L_hand", (-12.6, 13.0), -0.05, 1.6,
                extra=_bowser_back)
CHARS = {c.key: c for c in (LUIGI, PEACH, PEACH_R, BOWSER, BOWSER_R)}
TABLE_INDEX = {name: i for i, (name, _s, _c) in enumerate(T.TABLES)}


def field(table, index, off, value):
    """a field to set (the pack's 0xD0 list): record `index` of `table`, as int bits"""
    t = 0 if table == "groups" else TABLE_INDEX[table] + 1
    if isinstance(value, float):
        value = struct.unpack(">i", struct.pack(">f", value))[0]
    return ((t << 24) | index, off, value)

# Luigi's, by their old names
LUIGI_SIZE = LUIGI.size
MODEL_NAME = LUIGI.model_name
HAMMER_SIZE = LUIGI.hammer_size
HAMMER_PIVOT = LUIGI.pivot
HAMMER_PARENT = LUIGI.parent
HAMMER_REST = LUIGI.rest
LUIGI_TEXTURES = LUIGI.textures


def _u32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def _i32(v):
    return struct.pack(">i", v)


def hammer_records(base, ch=LUIGI):
    """{table: [records]} to append for the hammer, and the group fields to set"""
    t = base.tables
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
    w, h = ch.hammer_size
    px, py, pz = ch.pivot
    x0, x1, y0, y1 = -w / 2, w / 2, 0.0, h
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
        struct.pack_into(">Ii", b, 4, ch.textures + j, 0)
        out["texBinds"].append(bytes(b))
    dr = bytearray(0x6C)
    struct.pack_into(">i4xi4x8i", dr, 0, 1, 0, slot_i, -1, -1, -1, -1, -1, -1, -1)
    dr[0x4:0x8] = _i32(1)  # as the disc's draws have it
    dr[0x30:0x38] = bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
    struct.pack_into(">2i11i", dr, 0x38, call_i, 1, pid_i, pid_i, pid_i, pid_i, 0, 0, 0, 0, 0, 0, 0)
    out["draws"].append(bytes(dr))
    out["vis"].append(b"\x00")  # hidden unless an animation shows it
    node = [ch.rest[0], ch.rest[1], ch.rest[2], 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
            px, py, pz, px, py, pz, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    out["node"] = [struct.pack(">f", v) for v in node]
    groups = base.groups()
    parent = [i for i, gr in enumerate(groups) if gr.name.lstrip("|") == ch.parent][0]
    g = bytearray(0x58)
    g[0:64] = T._pad_name("Hammer_pc", 64)
    struct.pack_into(">6i", g, 0x40, groups[parent].child, -1, shape_i, vis_i, node_i, 0)
    out["groups"].append(bytes(g))
    hammer = len(t["groups"]) - 1  # the hammer takes the root's old index
    patches = [(parent, 0x44, hammer)]  # the hand's child
    return out, patches


def write_pack(path, anims, base, ch=LUIGI):
    """the pack: the hammer's records and `anims` (ttyd_anim.Anim, our own) for ch's model `base`"""
    recs, patches = hammer_records(base, ch)
    if ch.extra is not None:
        ch.extra(base, recs, patches)
    body = bytearray()
    head = bytearray(HEADER)
    head[0:4] = MAGIC
    struct.pack_into(">III", head, 4, VERSION, ch.size, zlib.crc32(base.build()) & 0xFFFFFFFF)

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
    struct.pack_into(">II", head, 0xD8, len(HAMMER_TEXTURES), put(b"".join(_u32(x) for x in HAMMER_TEXTURES)))
    blobs = [a.build() for a in anims]
    toff = put(bytes(0x48 * len(anims)))
    for j, (a, blob) in enumerate(zip(anims, blobs)):
        e = bytearray(0x48)
        e[0:60] = T._pad_name(a.name, 60)
        struct.pack_into(">II", e, 60, put(blob), len(blob))
        body[toff - HEADER + 0x48 * j:toff - HEADER + 0x48 * (j + 1)] = e
    struct.pack_into(">II", head, 0xE0, len(anims), toff)
    data = bytes(head) + bytes(body)
    with open(path, "wb") as f:
        f.write(data)
    return data


def read_pack(data):
    if data[:4] != MAGIC:
        raise ValueError("not an animation pack")
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


def merge(base_data, base_tpl, mario_tpl, pack_data, ch=LUIGI):
    """(model, textures) as the port makes them; None if the disc's model isn't the one the pack fits"""
    pk = read_pack(pack_data)
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
        name = "groups" if gi >> 24 == 0 else T.TABLES[(gi >> 24) - 1][0]
        g = bytearray(p.tables[name][gi & 0xFFFFFF])
        struct.pack_into(">i", g, off, value)
        p.tables[name][gi & 0xFFFFFF] = bytes(g)
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
    p.set_names(ch.model_name, ch.model_name)
    model = p.build()
    tex = merge_tpl(base_tpl, mario_tpl, pk["textures"])
    return _pad32(model), _pad32(tex)


def merge_files(disc, ch, pack=None):
    """merge() of the files in the folder `disc` and ch's committed pack (or `pack`)"""
    rd = lambda n: open(os.path.join(disc, n), "rb").read()
    return merge(rd(ch.base), rd(ch.base + "-"), rd("a_mario-"), open(pack or ch.pack, "rb").read(), ch)


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
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("merge", help="write our model and its textures as the port makes them")
    m.add_argument("--disc", required=True)
    m.add_argument("--pack", help="(default: the committed pack)")
    m.add_argument("--out", required=True)
    e = sub.add_parser("empty", help="a pack with the hammer and no animations")
    e.add_argument("--disc", required=True)
    e.add_argument("--out", required=True)
    for p in (m, e):
        p.add_argument("--char", default="luigi", choices=sorted(CHARS))
    a = ap.parse_args(argv)
    ch = CHARS[a.char]
    if a.cmd == "merge":
        res = merge_files(a.disc, ch, a.pack)
        if res is None:
            print("%s isn't the model the pack was made for" % ch.base)
            return 1
        os.makedirs(a.out, exist_ok=True)
        for name, blob in zip((ch.model_name, ch.model_name + "-"), res):
            with open(os.path.join(a.out, name), "wb") as f:
                f.write(blob)
        print("%s: %d bytes, %s-: %d bytes" % (ch.model_name, len(res[0]), ch.model_name, len(res[1])))
    else:
        base = T.PoseFile.parse(open(os.path.join(a.disc, ch.base), "rb").read())
        write_pack(a.out, [], base, ch)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
