"""Bowser's back view for Play as (docs/PLAY_AS.md): the disc has none, so
our c_koopa_r_pc is made over his own c_koopa at startup, from his own
parts: the same drawings, placed and layered as he looks from behind and a
little to his side. Only our numbers are here (which part goes where, which
are copied, mirrored or hidden); no geometry or pixels of the disc.

What changes (luigi_pack.py writes it into the pack as records and fields):
- REST: a part's rest values moved: offsets to its translation and Z
  rotation (degrees), its scale multiplied. They are the model's own values
  in place, so his disc animations (walk, run, ...) move the parts as they
  always do, from where they now stand.
- HIDE: parts from the front only (his eyes, brows, mouth inside, and his
  snout and cheek: skin he shows only from the front). A hidden part hides
  its children too (animdrv.c dispProc).
- COPIES: a part drawn twice (the shell's other half, the far horn, more
  spikes): a new group with the part's own shape and drawings, a child of
  `parent`, at the part's rest values with offsets as REST's (sx -1: mirrored
  about the part's pivot).

Depth (tz) is what layers the paper parts, through the depth buffer: the
field camera looks down on him, so a higher part also stands nearer the
camera (char_sheet.py --pitch draws it so). From behind: his shell and tail
nearest, his head beyond it with its lower edge behind the shell's top, his
hair over the back of his head and a horn each side of it.

Issue #12: his snout (lip) and cheek (hoho_waku) were moved and kept, so
skin showed beside and over his shell walking away from the camera, and his
head sat off to one side with its flat underside showing. They are hidden
now, his head stands centred over the shell, and the far horn is a mirrored
copy of the near one.

`python bowser_back.py repack --disc DIR` writes these numbers into the
committed bowser_r pack again, its animations kept byte for byte (no
Blender: the pack's records and fields are made here, and the animations
are deltas from the rest values, so they play on as before).
"""
import math
import os
import struct
import sys

import ttyd_anim as T

NODE = 24  # values per group: t xyz, s xyz, r xyz (halved), r2 xyz, pivots
F_T, F_S, F_R = 0, 3, 6

REST = {
    # the shell, over his back: forward onto his middle, nearest of all
    "koura1": {"tx": -4.0, "tz": 2.6},
    # its rim along the bottom
    "koura2": {"tx": -3.0, "ty": -9.0, "rz": 45.0, "tz": 2.9},
    # the tail before the shell, toward us
    "teil1": {"tz": 3.4},
    # his head from behind: centred over the shell, its flat underside behind
    # the shell's top (the head's world z -0.1, the shell's 2.0-2.6)
    "head": {"tx": 6.0, "ty": -2.5},
    # his hair over the back of his head (world z 5.3), his horns beside it (4.8)
    "hear": {"tz": 6.5, "sx": 1.15, "sy": 1.15},
    "R_tuno": {"tz": 5.0},
    # the near arm at his side, before the shell's edge; the near leg before his body
    "L_udewa": {"tx": -7.0, "tz": -0.6},
    "L_foot": {"tz": 1.2},
}
HIDE = ["sirome", "miken1", "ikari", "mabuta", "kuti", "sita_kuti", "ago", "ago_pat", "yoko_ago",
        "lip", "hoho_waku"]
COPIES = [
    # (name, the part copied, parent, offsets)
    ("koura1_r", "koura1", "body", {"tx": -10.0, "ty": -0.5, "sx": -1.0, "sy": 1.06, "tz": 2.5}),
    ("toge1_1_r", "toge1_1", "koura1_r", {}),
    ("toge1_3_r", "toge1_3", "koura1_r", {}),
    # the spikes down his spine, over the seam where the halves meet
    ("toge_s1", "toge1_2", "koura1", {"tx": -2.4, "ty": -3.5, "rz": -8.0, "tz": 0.3}),
    ("toge_s2", "toge1_2", "koura1", {"tx": -2.0, "ty": -12.0, "rz": -4.0, "tz": 0.3}),
    # the shell's middle, low, behind both halves: no gap where they meet
    ("koura1_m", "koura1", "body", {"tx": -7.5, "ty": -3.5, "tz": 2.2}),
    # his far horn: the near one mirrored, on the hair's other side
    ("R_tuno_r", "R_tuno", "head", {"sx": -1.0, "tx": 20.0, "tz": 5.0}),
]

# parts whose own drawing isn't shown (their shape off; children still drawn): of his
# own, or copies: the shell's two halves and their rim, drawn instead as one dome and one
# band (CARDS, STRIPS)
NOSHAPE = ["koura2", "koura1", "koura1_r", "koura1_m"]

# the drawings of our own, made from the disc by bowser_back.recipe, after the hammer's
# in c_koopa_r_pc- in this order
ART = ["art/models/c_koopa_r_pc/shell.png", "art/models/c_koopa_r_pc/rim.png"]
RECIPE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                      "port", "assets", "luigi", "art", "recipes", "bowser_back.recipe")

# flat cards: (name, parent, drawing, (x0, y0, x1, y1), z, clip) in the model as he stands
# (KPA_S_1's first frame); clip None, or (a strip's name, depth, columns): the card's bottom cut
# along that band's lower edge, `depth` inside it, in that many columns (so nothing of the card
# shows below the band)
CARDS = [
    # his shell whole, one smooth dome over his back; cut along the rim
    ("back_shell", "koura1", 0, (-19.0, 6.0, 13.0, 35.0), 1.9, ("back_rim", 0.6, 32)),
]

# bent cards: (name, parent, drawing, (cx, cy), rx, ry, degrees, thickness, z middle, z ends,
# segments): a band along the bottom of the ellipse about (cx, cy), x = cx + rx sin t,
# y = cy - ry cos t for t from -degrees to degrees, its depth from z middle to z ends (so
# its ends can go behind the shell), the drawing along it
STRIPS = [
    # the shell's rim, one band curved round the bottom of the dome
    ("back_rim", "koura1", 1, (-3.0, 23.0), 16.0, 14.5, 72.0, 4.5, 2.9, 1.2, 18),
]


def _copies():
    return COPIES


def _f2i(v):
    return struct.unpack(">i", struct.pack(">f", v))[0]


def _apply(vals, off):
    v = list(vals)
    for k, d in off.items():
        if k in ("tx", "ty", "tz"):
            v[F_T + "xyz".index(k[1])] += d
        elif k in ("sx", "sy", "sz"):
            v[F_S + "xyz".index(k[1])] *= d
        elif k in ("rx", "ry", "rz"):
            v[F_R + "xyz".index(k[1])] += d / 2.0  # (stored halved)
        else:
            raise ValueError("no rest value %r" % k)
    return v


def records(base, recs, patches):
    """our back view's records added to `recs` ({table: [records]}, after the
    hammer's) and its fields to `patches` (luigi_pack.field's tuples)"""
    import luigi_pack as LP
    t = base.tables
    groups = base.groups()
    by = {g.name.lstrip("|"): i for i, g in enumerate(groups)}
    added = {}  # a copy's name -> (group index, its record)
    node = base.node_values()
    for name, off in REST.items():
        g = groups[by[name]]
        new = _apply(node[g.node:g.node + NODE], off)
        for k in range(NODE):
            if new[k] != node[g.node + k]:
                patches.append(LP.field("node", g.node + k, 0, new[k]))
    # one hidden visibility for the parts from the front
    hidden = len(t["vis"]) + len(recs["vis"])
    recs["vis"].append(b"\x00")
    for name in HIDE:
        patches.append(LP.field("groups", by[name], 0x4C, hidden))
    for name in NOSHAPE:
        if name in by:
            patches.append(LP.field("groups", by[name], 0x48, -1))
    # the copies: new groups before the root (after the hammer's), each its
    # parent's first child
    n_base = len(t["groups"])
    child = {}  # parent -> its first child as the fields leave it
    for gi, off, value in patches:
        if gi >> 24 == 0 and off == 0x44:
            child[gi] = value
    for name, src, parent, off in _copies():
        s = groups[by[src]]
        p = by[parent] if parent in by else added[parent][0]
        gi = n_base - 1 + len(recs["groups"])  # (the root moves after the added groups)
        node_at = len(t["node"]) + len(recs["node"])
        recs["node"] += [struct.pack(">f", v) for v in _apply(node[s.node:s.node + NODE], off)]
        g = bytearray(0x58)
        g[0:64] = T._pad_name(name, 64)
        first = child.get(p, groups[p].child if p < n_base else -1)
        struct.pack_into(">6i", g, 0x40, first, -1, -1 if name in NOSHAPE else s.shape, s.vis, node_at, s.ssc)
        recs["groups"].append(bytes(g))
        added[name] = (gi, len(recs["groups"]) - 1)
        child[p] = gi
        if parent in added:  # (a copy's child: set in its record)
            r = bytearray(recs["groups"][added[parent][1]])
            struct.pack_into(">i", r, 0x44, gi)
            recs["groups"][added[parent][1]] = bytes(r)
        else:
            patches.append(LP.field("groups", p, 0x44, gi))
    if CARDS or STRIPS:
        _new_cards(base, recs, patches, child)


def _new_cards(base, recs, patches, child):
    """CARDS and STRIPS, each a new group: a child of its part, its vertices in that
    part's space as he stands"""
    import luigi_pack as LP
    t = base.tables
    groups = base.groups()
    by = {g.name.lstrip("|"): i for i, g in enumerate(groups)}
    first_art = LP.CHARS["bowser_r"].textures + len(LP.CHARS["bowser_r"].hammer_textures)
    ev = T.Evaluator(base)
    a = base.anim("KPA_S_1")
    nv, vis, _pos, _tex = ev.at_frame(a, a.start)
    nv = list(nv)
    for name, off in REST.items():  # (the parts as REST moves them)
        g = groups[by[name]]
        moved = _apply(nv[g.node:g.node + NODE], off)
        nv[g.node:g.node + NODE] = moved
    mats = ev.world_matrices(nv, vis)
    shown = len(t["vis"]) + len(recs["vis"])
    recs["vis"].append(b"\x01")
    n_base = len(t["groups"])
    meshes = []
    for name, parent, art, (x0, y0, x1, y1), z, clip in CARDS:
        if clip is None:
            meshes.append((name, parent, art, [(x0, y0, z), (x1, y0, z), (x0, y1, z), (x1, y1, z)],
                           [(0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0)], 1))
            continue
        strip, depth, n = clip
        verts, uvs = [], []
        for k in range(n + 1):  # columns, each its bottom on the cut and its top on the card's top
            x = x0 + (x1 - x0) * k / n
            y = min(y1, max(y0, _band_edge([s for s in STRIPS if s[0] == strip][0], depth, x)))
            verts += [(x, y, z), (x, y1, z)]
            uvs += [(k / n, 1.0 - (y - y0) / (y1 - y0)), (k / n, 0.0)]
        meshes.append((name, parent, art, verts, uvs, n))
    for name, parent, art, (cx, cy), rx, ry, deg, th, zm, ze, n in STRIPS:
        verts, uvs = [], []
        for k in range(n + 1):
            u = -1.0 + 2.0 * k / n
            ang = math.radians(deg) * u
            x, y, z = cx + rx * math.sin(ang), cy - ry * math.cos(ang), zm + (ze - zm) * u * u
            tx, ty = rx * math.cos(ang), ry * math.sin(ang)  # the tangent; the band's across is its normal
            ln = (tx * tx + ty * ty) ** 0.5
            nx, ny = -ty / ln, tx / ln
            verts += [(x - nx * th / 2, y - ny * th / 2, z), (x + nx * th / 2, y + ny * th / 2, z)]
            uvs += [(k / n, 1.0), (k / n, 0.0)]
        meshes.append((name, parent, art, verts, uvs, n))
    for name, parent, art, verts, uvs, nq in meshes:
        p = by[parent]
        inv = _inverse(mats[p])
        local = [_mulv(inv, v) for v in verts]
        gi = n_base - 1 + len(recs["groups"])  # (the root moves after the added groups)
        recs["groups"].append(_mesh(t, recs, name, local, uvs, nq, first_art + art, shown,
                                    child.get(p, groups[p].child)))
        child[p] = gi
        patches.append(LP.field("groups", p, 0x44, gi))


def _band_edge(strip, depth, x):
    """the height at `x` of a STRIPS band's lower edge moved `depth` into the band (beyond its
    ends: its lower edge's curve carried on round the ellipse)"""
    _n, _p, _a, (cx, cy), rx, ry, _deg, th, _zm, _ze, _s = strip
    d = th / 2 - depth

    def at(t):  # the point of the offset curve at angle t (the band's normal points up, inward)
        tx, ty = rx * math.cos(t), ry * math.sin(t)
        ln = (tx * tx + ty * ty) ** 0.5
        return cx + rx * math.sin(t) + ty / ln * d, cy - ry * math.cos(t) - tx / ln * d
    lo, hi = -math.pi / 2, math.pi / 2
    if x <= at(lo)[0] or x >= at(hi)[0]:
        return float("inf")
    for _ in range(60):  # (x rises with t)
        mid = (lo + hi) / 2
        if at(mid)[0] < x:
            lo = mid
        else:
            hi = mid
    return at((lo + hi) / 2)[1]


def _inverse(m):
    """the inverse of an affine 4x4 (row lists)"""
    a = [r[:3] for r in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    inv = [[0.0] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            r = [x for x in range(3) if x != j]
            c = [x for x in range(3) if x != i]
            inv[i][j] = (-1) ** (i + j) * (a[r[0]][c[0]] * a[r[1]][c[1]] - a[r[0]][c[1]] * a[r[1]][c[0]]) / det
    t = [-sum(inv[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [inv[0] + [t[0]], inv[1] + [t[1]], inv[2] + [t[2]], [0.0, 0.0, 0.0, 1.0]]


def _mulv(m, v):
    return [sum(m[r][c] * v[c] for c in range(3)) + m[r][3] for r in range(3)]


def _mesh(t, recs, name, verts, uvs, nq, tex_index, vis_index, brother):
    """a card's records appended to `recs`: nq quads, quad q of vertices 2q, 2q+1,
    2q+2, 2q+3 (in its parent's space; uvs on the drawing tex_index); returns its
    group record"""
    n = lambda k: len(t[k]) + len(recs[k])  # noqa: E731
    shape_i, call_i, pos_i, nrm_i, clr_i = n("shapes"), n("drawCalls"), n("pos"), n("nrm"), n("clr")
    tex_i, slot_i, bind_i, draw_i, node_i, pid_i = n("texVtx"), n("texData"), n("texBinds"), n("draws"), \
        n("node"), n("posIdx")
    for k in ("nrmIdx", "clrIdx", "texIdx0"):
        if n(k) != pid_i:
            raise ValueError("the index tables aren't in step")
    nv = len(verts)
    sh = bytearray(0xA8)
    sh[0:64] = T._pad_name(name + "Shape", 64)
    struct.pack_into(">26i", sh, 0x40, *([pos_i, nv, 0, 0, 0, 0, tex_i, nv] + [tex_i + nv, 0] * 7
                                         + [draw_i, 1, 0, 3]))
    recs["shapes"].append(bytes(sh))
    for x, y, z in verts:
        recs["pos"].append(struct.pack(">3f", x, y, z))
        recs["nrm"].append(struct.pack(">3f", 0.0, 0.0, 1.0))
        recs["clr"].append(b"\xff\xff\xff\xff")
    for uv in uvs:
        recs["texVtx"].append(struct.pack(">2f", *uv))
    for q in range(nq):  # each quad a fan: bottom left, bottom right, top right, top left
        recs["drawCalls"].append(_i32(4 * q) + _i32(4))
        for k in (2 * q, 2 * q + 1, 2 * q + 3, 2 * q + 2):
            recs["posIdx"].append(_u32(k))
            recs["texIdx0"].append(_u32(k))
            recs["nrmIdx"].append(_u32(nrm_i + k))
            recs["clrIdx"].append(_u32(clr_i + k))
    recs["texAnim"].append(struct.pack(">B3x5f", 0, 0.0, 0.0, 1.0, 1.0, 0.0))
    recs["texData"].append(_i32(bind_i) + _i32(0))
    b = bytearray(0x40)
    struct.pack_into(">Ii", b, 4, tex_index, 0)
    recs["texBinds"].append(bytes(b))
    dr = bytearray(0x6C)
    struct.pack_into(">i4xi4x8i", dr, 0, 1, 0, slot_i, -1, -1, -1, -1, -1, -1, -1)
    dr[0x4:0x8] = _i32(1)
    dr[0x30:0x38] = bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
    struct.pack_into(">2i11i", dr, 0x38, call_i, nq, pid_i, pid_i, pid_i, pid_i, 0, 0, 0, 0, 0, 0, 0)
    recs["draws"].append(bytes(dr))
    recs["node"] += [struct.pack(">f", v) for v in [0.0, 0.0, 0.0, 1.0, 1.0, 1.0] + [0.0] * 18]
    g = bytearray(0x58)
    g[0:64] = T._pad_name(name, 64)
    struct.pack_into(">6i", g, 0x40, brother, -1, shape_i, vis_index, node_i, 0)
    return bytes(g)


def _u32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def _i32(v):
    return struct.pack(">i", v)


def art_images(disc):
    """ART made from the disc by the recipe: [(w, h, rgba rows)] (disc: the extracted
    disc, or its files/a folder)"""
    if not ART:
        return []
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(here)), "port", "assets", "luigi", "art",
                                    "tools"))
    import recipes as R
    if os.path.basename(os.path.normpath(disc)) == "a":
        disc = os.path.dirname(os.path.normpath(disc))
    run = R.Runner(R.Disc(disc))
    text = open(RECIPE, encoding="utf-8").read()
    out = []
    for path in ART:
        im = run.make(text, path)
        out.append((im.w, im.h, [[tuple(im.px[y * im.w + x]) for x in range(im.w)] for y in range(im.h)]))
    return out


def repack(disc, pack=None):
    """the committed bowser_r pack (or `pack`) written again over the disc's
    c_koopa in `disc`, with this file's numbers and its animations as they are"""
    import luigi_pack as LP
    ch = LP.CHARS["bowser_r"]
    path = pack or ch.pack
    pk = LP.read_pack(open(path, "rb").read())

    class Blob:
        def __init__(self, name, data):
            self.name, self.data = name, data

        def build(self, exact=None):
            return self.data
    base = T.PoseFile.parse(open(os.path.join(disc, ch.base), "rb").read())
    old = open(path, "rb").read()
    new = LP.write_pack(path, [Blob(n, b) for n, b in pk["anims"]], base, ch)
    print("%s: %s" % (path, "unchanged" if new == old else "rewritten (%d animations kept)" % len(pk["anims"])))


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("repack", help="these numbers into the bowser_r pack, its animations kept")
    r.add_argument("--disc", required=True, help="a folder with the disc's a/c_koopa")
    r.add_argument("--pack", help="(default: the committed pack)")
    a = ap.parse_args(argv)
    repack(a.disc, a.pack)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
