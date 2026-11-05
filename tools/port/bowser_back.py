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
- HIDE: parts from the front only (his eyes, brows, cheek, mouth inside).
- COPIES: a part drawn twice (the shell's other half, the far horn, more
  spikes): a new group with the part's own shape and drawings, a child of
  `parent`, at the part's rest values with offsets as REST's (sx -1: mirrored
  about the part's pivot).

Depth (tz) is what layers the paper parts: from behind, his shell and tail
are nearest, then his body, his head behind his hair.
"""
import struct

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
    # his head from behind: behind his hair, a little back over the shell
    "head": {"tx": 2.0, "ty": -1.0},
    "hear": {"tz": 2.6, "sx": 1.15, "sy": 1.15},
    "locator3": {"tz": -1.6},
    "hoho_waku": {"tz": 1.6},
    "lip": {"tx": 5.0, "ty": 1.5, "tz": -0.8},
    # the near arm at his side, before the shell's edge; the near leg before his body
    "L_udewa": {"tx": -7.0, "tz": -0.6},
    "L_foot": {"tz": 1.2},
}
HIDE = ["sirome", "miken1", "ikari", "mabuta", "kuti", "sita_kuti", "ago", "ago_pat", "yoko_ago"]
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
]


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
    # the copies: new groups before the root (after the hammer's), each its
    # parent's first child
    n_base = len(t["groups"])
    child = {}  # parent -> its first child as the fields leave it
    for gi, off, value in patches:
        if gi >> 24 == 0 and off == 0x44:
            child[gi] = value
    for name, src, parent, off in COPIES:
        s = groups[by[src]]
        p = by[parent] if parent in by else added[parent][0]
        gi = n_base - 1 + len(recs["groups"])  # (the root moves after the added groups)
        node_at = len(t["node"]) + len(recs["node"])
        recs["node"] += [struct.pack(">f", v) for v in _apply(node[s.node:s.node + NODE], off)]
        g = bytearray(0x58)
        g[0:64] = T._pad_name(name, 64)
        first = child.get(p, groups[p].child if p < n_base else -1)
        struct.pack_into(">6i", g, 0x40, first, -1, s.shape, s.vis, node_at, s.ssc)
        recs["groups"].append(bytes(g))
        added[name] = (gi, len(recs["groups"]) - 1)
        child[p] = gi
        if parent in added:  # (a copy's child: set in its record)
            r = bytearray(recs["groups"][added[parent][1]])
            struct.pack_into(">i", r, 0x44, gi)
            recs["groups"][added[parent][1]] = bytes(r)
        else:
            patches.append(LP.field("groups", p, 0x44, gi))
