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
  0xD0 u32 count, u32 offset: group fields to set, 12 bytes each: group
       (its index in the merged table), byte offset in the record, value
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
    body = bytearray()
    head = bytearray(HEADER)
    head[0:4] = MAGIC
    struct.pack_into(">III", head, 4, VERSION, LUIGI_SIZE, zlib.crc32(luigi.build()) & 0xFFFFFFFF)

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
    if len(luigi_data) != pk["size"] or zlib.crc32(luigi_data) & 0xFFFFFFFF != pk["crc"]:
        return None
    p = T.PoseFile.parse(luigi_data)
    for name, _s, _c in T.TABLES:
        add = pk["tables"][name]
        if name == "groups":
            p.tables[name] = p.tables[name][:-1] + add + p.tables[name][-1:]
        else:
            p.tables[name] = p.tables[name] + add
    for gi, off, value in pk["patches"]:
        g = bytearray(p.tables["groups"][gi])
        struct.pack_into(">i", g, off, value)
        p.tables["groups"][gi] = bytes(g)
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
    p.set_names(MODEL_NAME, MODEL_NAME)
    model = p.build()
    tex = merge_tpl(luigi_tpl, mario_tpl, pk["textures"])
    return _pad32(model), _pad32(tex)


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
