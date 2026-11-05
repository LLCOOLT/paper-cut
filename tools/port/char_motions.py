"""Motions posed key by key for any character's model (runs inside Blender;
docs/LUIGI_ANIMS.md, docs/PLAY_AS.md). The engine of luigi_motions.py, made
per model: peach_motions.py and bowser_motions.py hold the motions, a Rig
names the model and the parts that turn its hammer.

A motion is {"end": frames, "loop": bool, "keys": [(frame, pose[, {"ease": e}])]}
or {"end", "loop", "tracks": {group: {channel: [(frame, value[, ease])]}}}.
A pose gives, per group (by its name in the disc's model), offsets from the
model's rest pose:

  tx ty tz     translation (game units; -x is the character's front)
  rz rx ry     rotation in degrees (rz: counter-clockwise seen from the front)
  sx sy sz     scale, as a multiple of the rest scale (snapped to sixteenths)
  vis          shown (1) or hidden (0)
  tex          the group's texture: an index into its texture slot's binds
  hw           (Hammer_pc only) the hammer's angle in the world, degrees:
               0 its head up, 90 its head toward the character's front

A channel a motion keys anywhere and a pose leaves out is at rest in that
pose. `ease` per key: "auto", "linear", "vector" (an impact), "step".
"""
import math

DEG = math.pi / 180.0

CHANNELS = {
    "tx": ("location", 0, 1.0), "ty": ("location", 1, 1.0), "tz": ("location", 2, 1.0),
    "rx": ("rotation_euler", 0, DEG), "ry": ("rotation_euler", 1, DEG), "rz": ("rotation_euler", 2, DEG),
    "sx": ("scale", 0, None), "sy": ("scale", 1, None), "sz": ("scale", 2, None),
}


class Rig:
    """a model's Blender prefix (our model's name), its pack (luigi_pack.CHARS key)
    and the parts whose rotation turns the hand holding the hammer, root first"""

    def __init__(self, prefix, char, chain):
        self.prefix = prefix
        self.char = char
        self.chain = chain


def _obj(rig, name):
    import bpy
    return bpy.data.objects["%s:%s" % (rig.prefix, name)]


def _rest(ob, path, index):
    import ttyd_blender as TB
    n = list(ob["ttyd_node"])
    f = {"location": 0, "rotation_euler": 6, "scale": 3}[path] + index
    return TB.to_blender(f, n[f], n[12:15])


def _tex_slot(ob):
    if ob.type != "MESH" or not ob.data.materials:
        return None
    return "tex" + ob.data.materials[0].name.split(":tex")[1]


def _snap16(v, rest):
    return rest + round((v - rest) * 16) / 16.0


def _keys(bag, path, index, pts):
    fc = bag.fcurves.find(path, index=index) or bag.fcurves.new(data_path=path, index=index)
    fc.keyframe_points.clear()
    fc.keyframe_points.add(len(pts))
    for j, (f, v, ease) in enumerate(pts):
        kp = fc.keyframe_points[j]
        kp.co = (f, v)
        if ease == "step":
            kp.interpolation = "CONSTANT"
        elif ease == "linear":
            kp.interpolation = "LINEAR"
        else:
            kp.interpolation = "BEZIER"
            kp.handle_left_type = kp.handle_right_type = "VECTOR" if ease == "vector" else "AUTO_CLAMPED"
    fc.extrapolation = "CONSTANT"
    fc.update()


def build(rig, name, motion):
    """the motion as the model's Action `<prefix>:<name>` (replacing one of that name)"""
    import bpy
    root = bpy.data.objects[rig.prefix]
    keys = motion.get("keys", [])
    tracks = motion.get("tracks", {})
    act_name = "%s:%s" % (rig.prefix, name)
    old = bpy.data.actions.get(act_name)
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(act_name)
    act.use_fake_user = True
    strip = act.layers.new("Layer").strips.new(type="KEYFRAME")
    bags = {}

    def bag(ob):
        if ob.name not in bags:
            bags[ob.name] = strip.channelbag(act.slots.new("OBJECT", ob.name), ensure=True)
        return bags[ob.name]

    used = {}
    for _f, pose, *_ in keys:
        for g, chans in pose.items():
            for c in chans:
                used.setdefault(g, set()).add(c)
    for g, chans in list(used.items()) + [(g, set(t)) for g, t in tracks.items()]:
        ob = _obj(rig, g)
        for c in sorted(chans):
            if c == "hw":
                continue
            if g in tracks and c in tracks[g]:
                pts = [(p[0], p[1], p[2] if len(p) > 2 else "auto") for p in tracks[g][c]]
            else:
                pts = [(f, pose.get(g, {}).get(c), (opt[0] if opt else {}).get("ease", "auto"))
                       for f, pose, *opt in keys]
            if c in CHANNELS:
                path, i, k = CHANNELS[c]
                rest = _rest(ob, path, i)
                vals = [(f, _snap16(rest * (1.0 if v is None else v), rest) if k is None
                         else rest + (0.0 if v is None else v) * k, e) for f, v, e in pts]
                _keys(bag(ob), path, i, vals)
            elif c == "vis":
                base = list(root["ttyd_base_vis"])[int(ob["ttyd_vis_index"])]
                _keys(bag(ob), '["vis"]', 0, [(f, float(base if v is None else v), "step") for f, v, _e in pts])
            elif c == "tex":
                _keys(bag(root), '["%s"]' % _tex_slot(ob), 0,
                      [(f, float(0 if v is None else v), "step") for f, v, _e in pts])
            else:
                raise ValueError("%s: no channel %r" % (name, c))
    # the hammer's world angle: its rz is that less the hand chain's turn at each key
    if any("hw" in pose.get("Hammer_pc", {}) for _f, pose, *_ in keys):
        pts = []
        for f, pose, *opt in keys:
            hw = pose.get("Hammer_pc", {}).get("hw", 0.0)
            chain = sum(pose.get(g, {}).get("rz", 0.0) for g in rig.chain)
            pts.append((f, (hw - chain) * DEG, (opt[0] if opt else {}).get("ease", "auto")))
        _keys(bag(_obj(rig, "Hammer_pc")), "rotation_euler", 2, pts)
    act.use_frame_range = True
    act.frame_start = 0
    act.frame_end = motion["end"]
    act["ttyd_loop"] = 1 if motion.get("loop") else 0
    act["ttyd_anim"] = name
    act["ttyd_model"] = rig.prefix
    return act


def anim_bbox(pose, anim, margin=0.5):
    """the animation's bounds over every frame (what the paper forms capture), with a margin"""
    import ttyd_anim as T
    ev = T.Evaluator(pose)
    geo = T.Geometry(pose)
    groups = pose.groups()
    lo, hi = [1e9] * 3, [-1e9] * 3
    f = anim.start
    while f <= anim.end + 1e-9:
        node, vis, pos, _tex = ev.at_frame(anim, f)
        mats = ev.world_matrices(node, vis)
        for gi, g in enumerate(groups):
            if g.shape < 0 or mats[gi] is None:
                continue
            m = mats[gi]
            sh = geo.shapes[g.shape]
            for vi in range(sh["pos"][0], sh["pos"][0] + sh["pos"][1]):
                x, y, z = pos[vi]
                w = [m[r][0] * x + m[r][1] * y + m[r][2] * z + m[r][3] for r in range(3)]
                for c in range(3):
                    lo[c], hi[c] = min(lo[c], w[c]), max(hi[c], w[c])
        f += 1.0
    return tuple(v - margin for v in lo) + tuple(v + margin for v in hi)


def export_pack(rig, motions, disc_dir):
    """every motion built, read back through the writer, into the rig's committed pack;
    returns the per-motion writer reports"""
    import os
    import bpy
    import luigi_pack
    import ttyd_anim as T
    import ttyd_blender as TB
    ch = luigi_pack.CHARS[rig.char]
    root = bpy.data.objects[rig.prefix]
    pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    out = []
    rep = {}
    for name, m in motions.items():
        act = build(rig, name, m)
        a, r = TB.read_action(root, act, pose)
        a.bbox = anim_bbox(pose, a)
        out.append(a)
        rep[name] = {"keys": len(a.keys), "added": r["added_keys"],
                     "max_error": {k: round(v, 4) for k, v in r["max_error"].items()}}
    base = T.PoseFile.parse(open(os.path.join(disc_dir, ch.base), "rb").read())
    luigi_pack.write_pack(ch.pack, out, base, ch)
    return rep


# ------------------------------------------------------------ helpers ----

def levels(motions, names, add_tex=(4, 8), rename=None):
    """the Super and Ultra Hammer versions of hammer motions (drawings +4, +8):
    rename(name, level) gives each one's name"""
    for lv, add in zip((1, 2), add_tex):
        for n in names:
            src = motions[n]
            keys = []
            for f, pose, *opt in src["keys"]:
                p = {g: dict(c) for g, c in pose.items()}
                if "tex" in p.get("Hammer_pc", {}):
                    p["Hammer_pc"]["tex"] += add
                keys.append((f, p, *opt))
            motions[rename(n, lv)] = dict(src, keys=keys)


def field_hammer_name(n, lv):
    """M_H_1 -> M_H_4 / M_H_7, M_H_2R -> M_H_5R / M_H_8R"""
    num = int(n[4]) + 3 * lv
    return "M_H_%d%s" % (num, n[5:])


def battle_hammer_name(n, lv):
    """M_H_1A@b -> M_H_5A@b / M_H_9A@b"""
    return "M_H_%d%s" % (1 + 4 * lv, n[5:])


def merged(*poses):
    """poses merged left to right (later groups' channels win)"""
    out = {}
    for p in poses:
        for g, ch in p.items():
            out.setdefault(g, {}).update(ch)
    return out


def held(pose, end):
    """a pose held from 0 to `end`, as tracks (to merge with keyed tracks)"""
    out = {}
    for g, ch in pose.items():
        for c, v in ch.items():
            if c == "hw":
                raise ValueError("tracks can't turn the hammer (hw)")
            e = "step" if c in ("tex", "vis") else "auto"
            out.setdefault(g, {})[c] = [(0, v, e), (end, v, e)]
    return out


def tracks(*ts):
    """track sets merged left to right (a later set's channel replaces one)"""
    out = {}
    for t in ts:
        for g, ch in t.items():
            out.setdefault(g, {}).update(ch)
    return out


def steps(groups, pairs, base, end):
    """a step track on each of `groups`' channel: `base` at 0, then (frame,
    value) in turn, the last held to `end`; groups: [(group, channel)]"""
    pts = [(0, base, "step")] + [(f, v, "step") for f, v in pairs]
    if pts[-1][0] != end:
        pts.append((end, pts[-1][1], "step"))
    return {g: {c: list(pts)} for g, c in groups}


def blink_pairs(*pairs, shut, open_):
    """(frame, drawing) steps shutting the eyes at each pair's first frame and
    opening them at its second"""
    out = []
    for a, b in pairs:
        out += [(a, shut), (b, open_)]
    return out


def talk_pairs(times, open_, shut):
    """(frame, drawing) steps opening and shutting the mouth in turn at `times`"""
    return [(t, open_ if i % 2 == 0 else shut) for i, t in enumerate(times)]


def slowed(motion, k):
    """the motion at 1/k speed (its keys' frames times k)"""
    return dict(motion, end=motion["end"] * k,
                keys=[(f * k, pose, *opt) for f, pose, *opt in motion["keys"]])


def hammer_swing_pts(pts):
    """Mario's battle hammer keys [(frame, angle or None: thrown, drawing)]
    with each angle taken the shorter way round from the one before; returns
    [(frame, angle, shown, drawing)]"""
    out = []
    prev = None
    for f, hw, tex in pts:
        if hw is not None and prev is not None:
            while hw - prev > 180:
                hw -= 360
            while prev - hw > 180:
                hw += 360
        if hw is not None:
            prev = hw
        out.append((f, prev if prev is not None else 0.0, hw is not None, tex))
    return out


# Mario's battle hammer: the hammer's angle in the world at each of his keys,
# and his drawing (luigi_motions.py batch 6 has where they come from)
H1A = [(0, 89, 3), (2, 90, 3), (4, 92, 3), (6, 94, 3), (8, 96, 3), (10, 97, 3), (11, 90, 3), (12, 76, 3),
       (13, 62, 2), (14, 47, 2), (15, 33, 2), (16, 17, 2), (17, 0, 2), (18, -20, 2), (19, -37, 2), (20, -56, 0),
       (21, -85, 0), (22, -108, 0), (24, -104, 0)]
H1B = [(0, -108, 0), (1, -106, 0), (3, -99, 0), (4, -101, 0), (6, -108, 0)]
H1C = [(0, -108, 0), (1, -112, 0), (2, -79, 0), (3, -22, 2), (4, 42, 2), (5, 82, 3), (6, 101, 3), (7, 103, 3),
       (8, 97, 3), (10, 97, 3)]
H13 = [(0, -108, 0), (1, -105, 0), (2, -67, 0), (3, 0, 2), (4, 15, 2), (5, 29, 3), (6, 52, 3), (7, 82, 3),
       (8, 105, 3), (9, 106, 3), (10, 105, 3)]
H17A = [(0, 96, 3), (1, 96, 3), (2, 47, 0), (3, 47, 0), (4, 52, 0), (5, 56, 0), (6, 60, 0)]
H17B = [(0, 60, 0), (3, 62, 0), (6, 60, 0)]
H17C = [(0, 60, 0), (1, 52, 0), (3, 16, 0), (4, -45, 3), (5, -90, 3), (6, -252, 2), (7, -268, 2), (8, -311, 0),
        (9, -362, 0), (10, -397, 0), (12, -397, 0)]
H11 = [(0, -108, 0), (1, -112, 0), (2, -81, 0), (3, -24, 1), (4, None, 1), (5, None, 1), (6, None, 1),
       (8, None, 1), (10, None, 1)]
H3 = [(0, -108, 0), (1, -112, 0), (2, -81, 0), (3, -24, 2), (4, None, 2), (5, None, 3), (8, None, 3),
      (10, None, 3)]


def battle_hammer_set(motions, swing):
    """every battle hammer move of Mario's, from swing(end, pts, loop=False,
    add=0, slam=False) -> motion (pts as H1A's; add: drawings on, 4 Super, 8
    Ultra; slam: the big overhead blow, the body thrown into it): the plain
    attack (1, 5, 9 A-C), the slam (13-15), the swing round behind
    (17-19 A-C), the throws (3, 7, 11) and the hammer held up (4, 8)"""
    for lv, add in ((0, 0), (1, 4), (2, 8)):
        a, b, c = (("M_H_1A", "M_H_1B", "M_H_1C"), ("M_H_5A", "M_H_5B", "M_H_5C"),
                   ("M_H_9A", "M_H_9B", "M_H_9C"))[lv]
        motions[a + "@b"] = swing(24, H1A, add=add)
        motions[b + "@b"] = swing(6, H1B, loop=True, add=add)
        motions[c + "@b"] = swing(10, H1C, add=add, slam=False)
        motions["M_H_%d@b" % (13 + lv)] = swing(10, H13, add=add, slam=True)
        n = 17 + lv
        motions["M_H_%dA@b" % n] = swing(6, H17A, add=add)
        motions["M_H_%dB@b" % n] = swing(6, H17B, loop=True, add=add)
        motions["M_H_%dC@b" % n] = swing(12, H17C, add=add)
    motions["M_H_11@b"] = swing(10, H11, add=8)
    motions["M_H_3@b"] = swing(10, H3)
    motions["M_H_7@b"] = swing(10, H3, add=4)
    motions["M_H_4@b"] = swing(0, [(0, 0, 3)])
    motions["M_H_8@b"] = swing(0, [(0, 0, 7)])
