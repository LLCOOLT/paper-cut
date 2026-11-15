#!/usr/bin/env python3
"""How any model's group tree becomes Blender objects that move exactly as the
game moves it (ttyd_blender.build_model; docs/LUIGI_ANIMS.md "Joints and
vertex morphs"). Pure Python: the tools, the tests and Blender share it.

The game puts a group in place with one of two matrices (ttyd_anim):

  a transform node   T(t + rt + rc) R T(st + sc - rc) S T(-sc)
  a joint (ssc)      T(t) S(1 / parent's scale) R2 R S

t, R (from r1, stored halved) and S are what animations key; rc, sc, rt, st
(the pivots and their translations) and R2 (a joint's orient) never change in
any of the discs' animations. A Blender object's matrix is its parent's, its
parent-inverse matrix, then T(loc) R(rot) S(scale). So each group is one
object (named as the group) and, where the constant parts sit between the
keyed ones, a helper object:

  plain        one object: loc = t + rt + rc, rotation, scale; its mesh and
               its children's parent-inverse moved by -sc
  "~s" helper  a transform node whose rotation and scale pivots differ (or with
               pivot translations between them): the object keys loc and
               rotation, a child "<group>~s" (parent-inverse T(st + sc - rc))
               keys the scale and holds the mesh and the children
  "~j" helper  a joint with an orient, or under another joint: "<group>~j" keys
               the location and undoes the parent joint's scale (drivers), the
               object under it (parent-inverse R2) keys rotation and scale

So every channel of every group is one F-curve, as the game's are, and a
model with joints, orients and pivots edits like Luigi's plain parts.

Vertex morphs (the discs' vertex position keys) are the meshes' shape keys:
each state of a mesh's vertices an animation reaches at a key is a shape key
("<animation>@<key>", the same state shared), its weight 1 at that key and 0
at the keys either side, linear in between, which is the game's own linear
blend of the vertex positions between keys.

  anim_rig.py check ISO|FOLDER [NAME...]   every group's matrix through the
      objects against the game's (ttyd_anim.Evaluator), every frame of every
      animation, and what can't be edited; no Blender needed
  anim_rig.py selftest
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402

EPS = 1e-6
# Blender channel -> node fields
CHANNEL_FIELDS = {"location": (0, 1, 2), "rotation_euler": (6, 7, 8), "scale": (3, 4, 5)}
FIELD_CHANNEL = {f: (path, i) for path, fs in CHANNEL_FIELDS.items() for i, f in enumerate(fs)}
KEYED_FIELDS = 9  # t, s, r1: the rest (r2, pivots, their translations) never change


def _nonzero(v):
    return any(abs(x) > EPS for x in v)


class Part:
    """one group as Blender objects: `helper` None, "s" or "j" (see the module's
    doc); offsets in game units; owner[channel] "main" or "helper"."""

    def __init__(self, gi, name, parent):
        self.gi = gi
        self.name = name
        self.parent = parent
        self.joint = False
        self.helper = None
        self.loc_offset = (0.0, 0.0, 0.0)  # added to t for Blender's location
        self.end_offset = (0.0, 0.0, 0.0)  # the mesh and the children: moved by minus this
        self.scale_gap = (0.0, 0.0, 0.0)   # "~s": T(st + sc - rc) between rotation and scale
        self.orient = (0.0, 0.0, 0.0)      # "~j": R2 in degrees
        self.parent_joint = False          # "~j": undoes the parent joint's scale
        self.owner = {"location": "main", "rotation_euler": "main", "scale": "main"}
        self.end = "main"                  # which object holds the mesh and the children

    @property
    def helper_name(self):
        return None if self.helper is None else "%s~%s" % (self.name, self.helper)


def parents(groups):
    out = {}
    for gi, g in enumerate(groups):
        c = g.child
        while c != -1:
            out[c] = gi
            c = groups[c].brother
    return out


def layout(pose):
    """[Part] per group"""
    groups = pose.groups()
    node = pose.node_values()
    par = parents(groups)
    out = []
    for gi, g in enumerate(groups):
        n = node[g.node:g.node + T.NODE_SIZE]
        p = Part(gi, g.name.lstrip("|"), par.get(gi, -1))
        if g.ssc:
            p.joint = True
            p.orient = tuple(n[9:12])
            p.parent_joint = p.parent != -1 and bool(groups[p.parent].ssc)
            if _nonzero(p.orient) or p.parent_joint:
                p.helper = "j"
                p.owner["location"] = "helper"
        else:
            rc, sc, rt, st = n[12:15], n[15:18], n[18:21], n[21:24]
            p.loc_offset = tuple(rt[c] + rc[c] for c in range(3))
            p.end_offset = tuple(sc)
            gap = tuple(st[c] + sc[c] - rc[c] for c in range(3))
            if _nonzero(gap):
                p.helper = "s"
                p.scale_gap = gap
                p.owner["scale"] = "helper"
                p.end = "helper"
        out.append(p)
    return out


def to_blender(part, f, v):
    """node field f's value v as its Blender channel's value"""
    path, i = FIELD_CHANNEL[f]
    if path == "location":
        return v + part.loc_offset[i]
    if path == "rotation_euler":
        return v * 2.0 * math.pi / 180.0
    return v


def from_blender(part, f, v):
    path, i = FIELD_CHANNEL[f]
    if path == "location":
        return v - part.loc_offset[i]
    if path == "rotation_euler":
        return v * 180.0 / (2.0 * math.pi)
    return v


def problems(pose):
    """why the model can't be edited as objects (empty: it can): what animations
    key that no object has a channel for, and shapes two groups share at
    different offsets"""
    probs = []
    parts = layout(pose)
    groups = pose.groups()
    owner = {}
    for gi, g in enumerate(groups):
        if g.shape >= 0:
            owner.setdefault(g.shape, []).append(gi)
    for s, gis in owner.items():
        offs = {parts[gi].end_offset for gi in gis}
        if len(offs) > 1:
            probs.append("shape %d is drawn by %s at different pivots" % (s, ", ".join(groups[gi].name for gi in gis)))
    for a in pose.anims:
        for k in a.keys:
            bad = sorted({T.NODE_FIELDS[i % T.NODE_SIZE] for i in k.node if i % T.NODE_SIZE >= KEYED_FIELDS})
            if bad:
                probs.append("%s animates %s (pivots or joint orients)" % (a.name, ", ".join(bad)))
                break
    return sorted(set(probs))


# ------------------------------------------------------------ matrices ----

def _inv_scale(s):
    return T._scale(*(1.0 / v if v else 1.0 for v in s))


def _mat3(m):
    return [row[:] for row in m]


def object_chain(part, values, parent_scale):
    """the group's objects' basis matrices, in order (first: under the parent's
    end object, whose parent-inverse is T(-its end offset)); values: the 24
    node values; parent_scale: the parent joint's (sx, sy, sz)"""
    t, s, r1 = values[0:3], values[3:6], values[6:9]
    loc = [to_blender(part, f, values[f]) for f in (0, 1, 2)]
    rot = T.rot_matrix(2 * r1[0], 2 * r1[1], 2 * r1[2])
    if part.joint:
        if part.helper == "j":
            ps = parent_scale if part.parent_joint else (1.0, 1.0, 1.0)
            h = T._mul(T._trans(*t), _inv_scale(ps))
            o = T._mul(T.rot_matrix(*part.orient), T._mul(rot, T._scale(*s)))
            return [h, o]
        return [T._mul(T._mul(T._trans(*loc), rot), T._scale(*s))]
    if part.helper == "s":
        o = T._mul(T._trans(*loc), rot)
        h = T._mul(T._trans(*part.scale_gap), T._scale(*s))
        return [o, h]
    return [T._mul(T._mul(T._trans(*loc), rot), T._scale(*s))]


def group_matrices(pose, parts, node, vis):
    """each group's matrix through the objects' chain (None: hidden), as
    ttyd_anim.Evaluator.world_matrices gives it"""
    groups = pose.groups()
    out = [None] * len(groups)

    def walk(parent, gi, mat):
        while gi != -1:
            g = groups[gi]
            if vis[g.vis]:
                p = parts[gi]
                ps = tuple(node[groups[parent].node + 3:groups[parent].node + 6]) if parent != -1 else (1.0,) * 3
                m = mat
                for b in object_chain(p, node[g.node:g.node + T.NODE_SIZE], ps):
                    m = T._mul(m, b)
                end = T._mul(m, T._trans(*(-c for c in p.end_offset)))
                out[gi] = end
                walk(gi, g.child, end)
            gi = g.brother
    walk(-1, len(groups) - 1, T._ident())
    return out


def check_pose(pose, step=1.0):
    """the worst difference of any group's matrix (objects' chain vs the game's)
    over every animation, and the frames checked"""
    parts = layout(pose)
    ev = T.Evaluator(pose)
    worst, frames = 0.0, 0
    anims = [a for a in pose.anims if a.keys] or [None]
    for a in anims:
        if a is None:
            samples = [(pose.node_values(), pose.vis_values())]
        else:
            samples = []
            f = a.start
            while f <= a.end + 1e-9:
                node, vis, _p, _t = ev.at_frame(a, f)
                samples.append((node, vis))
                f += step
        for node, vis in samples:
            game = ev.world_matrices(node, vis)
            ours = group_matrices(pose, parts, node, vis)
            for x, y in zip(game, ours):
                if (x is None) != (y is None):
                    return float("inf"), frames
                if x is not None:
                    worst = max(worst, max(abs(x[r][c] - y[r][c]) for r in range(3) for c in range(4)))
            frames += 1
    return worst, frames


# --------------------------------------------------------------- morphs ----

def shape_owner(pose):
    """{shape index: group index} (the first group that draws it)"""
    out = {}
    for gi, g in enumerate(pose.groups()):
        if g.shape >= 0:
            out.setdefault(g.shape, gi)
    return out


def vertex_owner(pose):
    """{vertex (pos index): (shape, index in the shape)} of every drawn shape"""
    geo = T.Geometry(pose)
    out = {}
    for s in shape_owner(pose):
        first, count = geo.shapes[s]["pos"]
        for j in range(count):
            out.setdefault(first + j, (s, j))
    return out


def morph_states(pose, anim):
    """per key, {shape: tuple of the shape's vertices' offsets from rest, in
    sixteenths ((dx, dy, dz) per vertex)} for each shape a key has moved so
    far; and the vertices outside every drawn shape that move ({key: {v: d}})"""
    geo = T.Geometry(pose)
    vo = vertex_owner(pose)
    acc = {}
    states, stray = [], {}
    for ki, key in enumerate(anim.keys):
        for v, d in key.pos.items():
            if v not in vo:
                stray.setdefault(ki, {})[v] = d
                continue
            cur = acc.get(v, (0, 0, 0))
            acc[v] = (cur[0] + d[0], cur[1] + d[1], cur[2] + d[2])
        per = {}
        for v, d in acc.items():
            s, j = vo[v]
            per.setdefault(s, {})[j] = d
        st = {}
        for s, offs in per.items():
            count = geo.shapes[s]["pos"][1]
            st[s] = tuple(offs.get(j, (0, 0, 0)) for j in range(count))
        states.append(st)
    return states, stray


def is_rest(state):
    return all(d == (0, 0, 0) for d in state)


def weight_keys(anim, states, shape, times=None):
    """the shape's morph as shape key weights: ({state: name}, {name: [(time, weight)]})
    with each key's state at weight 1 and the others 0 (linear in between); times:
    the keys' times to use (default the animation's)"""
    times = times or [k.time for k in anim.keys]
    seq = [st.get(shape) for st in states]
    names, uses = {}, {}
    for ki, s in enumerate(seq):
        if s is None or is_rest(s):
            continue
        if s not in names:
            names[s] = "%s@%d" % (anim.name, ki)
        uses.setdefault(names[s], set()).add(ki)
    curves = {}
    n = len(times)
    for name, kis in uses.items():
        pts = set()
        for ki in kis:
            pts.update(j for j in (ki - 1, ki, ki + 1) if 0 <= j < n)
        curves[name] = [(times[j], 1.0 if j in kis else 0.0) for j in sorted(pts)]
    return names, curves


# --------------------------------------------------------------- tests ----

def make_joint_model():
    """a small jointed model with an orient, a joint under a joint, and a part
    whose pivots differ (with translations): nothing from the disc"""
    import struct
    import character_tool as C
    p = C.make_test_model(4)
    t = p.tables
    vals = [struct.unpack(">f", r)[0] for r in t["node"]]
    # group 0: a joint with an orient; group 1: a joint (parent 0 below); group 2: pivots differ
    vals[0 * 24 + 9:0 * 24 + 12] = [0.0, 30.0, 45.0]
    vals[1 * 24 + 3:1 * 24 + 6] = [1.0, 1.0, 1.0]
    vals[2 * 24 + 12:2 * 24 + 24] = [1.0, 2.0, 0.0, 0.5, 1.0, 0.0, 0.25, -0.5, 0.0, 0.1, 0.2, 0.0]
    t["node"] = [struct.pack(">f", v) for v in vals]
    groups = [bytearray(r) for r in t["groups"]]
    # tree: root(4) -> 0 -> 1, 0's brother 2, 2's brother 3
    struct.pack_into(">6i", groups[0], 0x40, 2, 1, 0, 0, 0, 1)
    struct.pack_into(">6i", groups[1], 0x40, -1, -1, 1, 1, 24, 1)
    struct.pack_into(">6i", groups[2], 0x40, 3, -1, 2, 2, 48, 0)
    struct.pack_into(">6i", groups[3], 0x40, -1, -1, 3, 3, 72, 0)
    t["groups"] = [bytes(g) for g in groups]
    a = T.Anim("A_1")
    a.loop_info = [(1, 0.0, 30.0)]
    for time, deltas, pos in ((0.0, {}, {}), (10.0, {0 * 24 + 8: 160, 0 * 24 + 3: 8, 1 * 24 + 6: -100, 2 * 24 + 7: 300,
                                                       2 * 24 + 4: 24, 0 * 24 + 1: 40, 1 * 24 + 0: 16},
                                              {4: (16, 8, 0), 5: (-8, 0, 0)}),
                              (20.0, {0 * 24 + 8: -160, 1 * 24 + 6: 100, 2 * 24 + 7: -300, 1 * 24 + 4: 8},
                               {4: (-16, -8, 0), 6: (300, 0, 0)}),
                              (30.0, {}, {})):
        k = T.Key(time)
        for i, n in deltas.items():
            k.node[i] = T.NodeKey(n, 0.5, -0.25)
        k.pos = dict(pos)
        a.keys.append(k)
    p.anims = [a]
    return T.PoseFile.parse(p.build())


def selftest():
    p = make_joint_model()
    parts = layout(p)
    assert parts[0].helper == "j" and parts[1].helper == "j" and parts[2].helper == "s" and parts[3].helper is None, \
        [x.helper for x in parts]
    assert not problems(p), problems(p)
    worst, frames = check_pose(p, 0.5)
    assert worst < 1e-9 and frames == 61, (worst, frames)
    # an animation keying a joint's orient can't be edited as objects
    a = p.anims[0]
    a.keys[1].node[0 * 24 + 10] = T.NodeKey(16)
    assert problems(p), "an animated orient"
    del a.keys[1].node[0 * 24 + 10]
    states, stray = morph_states(p, a)
    assert not stray and set(states[1]) == {1} and states[3] == {1: ((0, 0, 0), (-8, 0, 0), (300, 0, 0), (0, 0, 0))}, states
    names, curves = weight_keys(a, states, 1)
    assert sorted(curves) == ["A_1@1", "A_1@2"] and curves["A_1@2"] == [(10.0, 0.0), (20.0, 1.0), (30.0, 1.0)], curves
    assert curves["A_1@1"] == [(0.0, 0.0), (10.0, 1.0), (20.0, 0.0)], curves
    print("anim_rig: ok (joints, orients, pivots: %d frames exact; morph states)" % frames)
    return 0


def cmd_check(src_path, names):
    import character_tool as C
    src = C.open_source(src_path)
    names = names or src.models()
    bad = 0
    for n in names:
        try:
            pose = T.PoseFile.parse(src.read("a/" + n))
        except Exception as e:  # noqa: BLE001 (a file the tools can't read: reported, the rest go on)
            print("%-24s not read: %s" % (n, e))
            continue
        probs = problems(pose)
        worst, frames = check_pose(pose)
        helpers = sum(1 for p in layout(pose) if p.helper)
        ok = not probs and worst < 1e-3
        bad += not ok
        print("%-24s %s: %d frames, worst matrix difference %.2g, %d helpers%s" % (
            n, "ok" if ok else "PROBLEMS", frames, worst, helpers, "" if not probs else "; " + "; ".join(probs[:3])))
    return 1 if bad else 0


def main(argv):
    if argv[:1] == ["selftest"]:
        return selftest()
    if argv[:1] == ["check"] and len(argv) >= 2:
        return cmd_check(argv[1], argv[2:])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
