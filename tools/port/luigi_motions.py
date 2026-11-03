"""Luigi's new motions, posed key by key (runs inside Blender; docs/LUIGI_ANIMS.md).

Each motion is a list of key poses at game frames (60 a second), named as
the Mario animation it stands in for. A pose gives, per group of Luigi's
model, offsets from the model's rest pose:

  tx ty tz     translation (game units; x is Luigi's back, -x his front)
  rz rx ry     rotation in degrees (rz: counter-clockwise seen from the front)
  sx sy sz     scale, as a multiple of the rest scale
  vis          shown (1) or hidden (0)
  tex          the group's texture: an index into its texture slot's binds
  hw           (Hammer_pc only) the hammer's angle in the world, degrees:
               0 its head up, 90 its head toward Luigi's front

A channel a motion keys anywhere and a pose leaves out is at rest in that
pose: every key is a whole pose. `ease` per key: "auto" (smooth), "linear",
"vector" (a sharp corner: an impact), "step" (holds, then jumps).

Luigi's rig (c_luigi has no names: these are its parts):
  locator2   the whole body (root mover)     locator3  hips: body, head, arms
  pPlane17   body (overalls), pivot at hips  pPlane14  head, pivot at the neck
  pPlane16   front arm (shoulder pivot)      pPlane3   front glove (wrist)
  pPlane15   back arm                        pPlane2   back glove
  locator4   front leg (pPlane19 front shoe) locator5  back leg (pPlane18 back shoe)
  pPlane4    cap (pPlane5 its L, pPlane9 its shade)
  group1     eyes (pPlane8 white, pPlane7 pupil)   pPlane12 open mouth (hidden at rest)
  pPlane10   mustache   pPlane6 nose   pPlane13 ear   pPlane11 hair
  Hammer_pc  the hammer, a child of the front glove, its handle's end in the fist
"""
import math

import bpy

import ttyd_blender as TB

PREFIX = "c_luigi_pc"
DEG = math.pi / 180.0

CHANNELS = {
    "tx": ("location", 0, 1.0), "ty": ("location", 1, 1.0), "tz": ("location", 2, 1.0),
    "rx": ("rotation_euler", 0, DEG), "ry": ("rotation_euler", 1, DEG), "rz": ("rotation_euler", 2, DEG),
    "sx": ("scale", 0, None), "sy": ("scale", 1, None), "sz": ("scale", 2, None),
}

# the parts whose rotation turns the front glove (and so the hammer), root first
GLOVE_CHAIN = ["locator2", "locator3", "pPlane17", "pPlane16", "pPlane3"]


def _obj(name):
    return bpy.data.objects["%s:%s" % (PREFIX, name)]


def _rest(ob, path, index):
    n = list(ob["ttyd_node"])
    f = {"location": 0, "rotation_euler": 6, "scale": 3}[path] + index
    return TB.to_blender(f, n[f], n[12:15])


def _tex_slot(ob):
    """the root property and binds of an object's texture slot"""
    if ob.type != "MESH" or not ob.data.materials:
        return None
    name = ob.data.materials[0].name
    return "tex" + name.split(":tex")[1]


def build(name, motion, root=None):
    """the motion as this model's Action `<prefix>:<name>` (replacing one of that name)"""
    root = root or bpy.data.objects[PREFIX]
    keys = motion.get("keys", [])
    # a motion of tracks: {group: {channel: [(frame, value[, ease]), ...]}}, each
    # channel keyed on its own (head nods, blinks, mouth flaps to Mario's times)
    tracks = motion.get("tracks", {})
    act_name = "%s:%s" % (PREFIX, name)
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

    # every (group, channel) the motion keys
    used = {}
    for _f, pose, *_ in keys:
        for g, chans in pose.items():
            for c in chans:
                used.setdefault(g, set()).add(c)
    for g, chans in list(used.items()) + [(g, set(t)) for g, t in tracks.items()]:
        ob = _obj(g)
        for c in sorted(chans):
            if c == "hw":
                continue  # turned into the hammer's rz below
            pts = []
            if g in tracks and c in tracks[g]:
                pts = [(p[0], p[1], p[2] if len(p) > 2 else "auto") for p in tracks[g][c]]
            else:
                for f, pose, *opt in keys:
                    ease = (opt[0] if opt else {}).get("ease", "auto")
                    v = pose.get(g, {}).get(c)
                    pts.append((f, v, ease))
            if c in CHANNELS:
                path, i, k = CHANNELS[c]
                rest = _rest(ob, path, i)
                # (scale is stored in sixteenths: a squash of 0.9 would play as 0.875)
                vals = [(f, _snap16(rest * (1.0 if v is None else v), rest) if k is None
                         else rest + (0.0 if v is None else v) * k, e) for f, v, e in pts]
                _keys(bag(ob), path, i, vals)
            elif c == "vis":
                base = list(root["ttyd_base_vis"])[int(ob["ttyd_vis_index"])]
                _keys(bag(ob), '["vis"]', 0, [(f, float(base if v is None else v), "step") for f, v, _e in pts])
            elif c == "tex":
                slot = _tex_slot(ob)
                _keys(bag(root), '["%s"]' % slot, 0, [(f, float(0 if v is None else v), "step") for f, v, _e in pts])
            else:
                raise ValueError("%s: no channel %r" % (name, c))
    # the hammer's world angle: its rz is that less the glove chain's turn at each key
    if any("hw" in pose.get("Hammer_pc", {}) for _f, pose, *_ in keys):
        pts = []
        for f, pose, *opt in keys:
            ease = (opt[0] if opt else {}).get("ease", "auto")
            hw = pose.get("Hammer_pc", {}).get("hw", 0.0)
            chain = sum(pose.get(g, {}).get("rz", 0.0) for g in GLOVE_CHAIN)
            pts.append((f, (hw - chain) * DEG, ease))
        _keys(bag(_obj("Hammer_pc")), "rotation_euler", 2, pts)
    act.use_frame_range = True
    act.frame_start = 0
    act.frame_end = motion["end"]
    act["ttyd_loop"] = 1 if motion.get("loop") else 0
    act["ttyd_anim"] = name
    act["ttyd_model"] = PREFIX
    return act


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


def build_all(motions, names=None):
    out = []
    for name, m in motions.items():
        if names and name not in names:
            continue
        out.append(build(name, m))
    return out


def anims(motions, names=None, root=None):
    """the motions as game animations (ttyd_anim.Anim), read back from their Actions;
    returns [(anim, report)]"""
    import ttyd_anim as T
    root = root or bpy.data.objects[PREFIX]
    pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    out = []
    for name in motions:
        if names and name not in names:
            continue
        act = bpy.data.actions["%s:%s" % (PREFIX, name)]
        a, rep = TB.read_action(root, act, pose)
        a.bbox = anim_bbox(pose, a)
        out.append((a, rep))
    return out


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


def export_pack(motions, pack_path, disc_luigi, names=None):
    """the motions, read back from their Actions, into the pack the port builds in
    (port/assets/luigi/luigi_anims.bin); returns the per-motion writer reports"""
    import ttyd_anim as T
    import luigi_pack
    built = anims(motions, names)
    luigi = T.PoseFile.parse(open(disc_luigi, "rb").read())
    luigi_pack.write_pack(pack_path, [a for a, _r in built], luigi)
    return {a.name: {"keys": len(a.keys), "added": r["added_keys"],
                     "max_error": {k: round(v, 4) for k, v in r["max_error"].items()}} for a, r in built}


# ------------------------------------------------------------- motions ----

MOTIONS = {}

# The field hammer swing (mot_hammer.c: M_H_1 wooden, M_H_4 Super, M_H_7
# Ultra): Mario's timing, 10 frames, held at the end. Luigi winds up further
# behind, rises on his toes at the top, and lands it with a deeper crouch,
# his front foot stepping in.
MOTIONS["M_H_1"] = {"end": 10, "keys": [
    (0, {"locator3": {"rz": -10}, "pPlane14": {"rz": -6},
         "pPlane16": {"rz": 110}, "pPlane15": {"rz": 100}, "pPlane3": {"rz": 10},
         "locator4": {"tx": -0.3, "ty": 0.4, "rz": 8},
         "Hammer_pc": {"hw": -70, "vis": 1, "tex": 0}}),
    (1, {"locator3": {"rz": -12, "sy": 1.02}, "pPlane14": {"rz": -8},
         "pPlane16": {"rz": 150}, "pPlane15": {"rz": 140}, "pPlane3": {"rz": 5},
         "locator4": {"tx": -0.3, "ty": 0.5, "rz": 8},
         "Hammer_pc": {"hw": -30, "vis": 1, "tex": 0}}),
    (2, {"locator2": {"ty": 0.3}, "locator3": {"rz": -6, "sy": 1.05}, "pPlane14": {"rz": -4},
         "pPlane16": {"rz": 185}, "pPlane15": {"rz": 175},
         "locator4": {"tx": -0.2, "ty": 0.3},
         "Hammer_pc": {"hw": 5, "vis": 1, "tex": 0}}),
    (3, {"locator2": {"ty": 0.6}, "locator3": {"rz": 4, "sy": 1.07}, "pPlane14": {"rz": 2},
         "pPlane16": {"rz": 215}, "pPlane15": {"rz": 205},
         "Hammer_pc": {"hw": 40, "vis": 1, "tex": 2}}),
    (4, {"locator2": {"ty": 0.2}, "locator3": {"rz": 16}, "pPlane14": {"rz": 6},
         "pPlane16": {"rz": 255}, "pPlane15": {"rz": 245}, "pPlane12": {"vis": 1},
         "locator4": {"tx": -0.6},
         "Hammer_pc": {"hw": 85, "vis": 1, "tex": 2}}, {"ease": "linear"}),
    (5, {"locator3": {"ty": -0.6, "rz": 26, "sx": 1.07, "sy": 0.86}, "pPlane14": {"rz": 12},
         "pPlane16": {"rz": 302}, "pPlane15": {"rz": 294}, "pPlane12": {"vis": 1},
         "locator4": {"tx": -1.2},
         "Hammer_pc": {"hw": 103, "vis": 1, "tex": 3}}, {"ease": "vector"}),
    (6, {"locator3": {"ty": -0.4, "rz": 23, "sx": 1.03, "sy": 0.93}, "pPlane14": {"rz": 10},
         "pPlane16": {"rz": 298}, "pPlane15": {"rz": 290}, "pPlane12": {"vis": 1},
         "locator4": {"tx": -1.2},
         "Hammer_pc": {"hw": 100, "vis": 1, "tex": 3}}),
    (8, {"locator3": {"ty": -0.5, "rz": 24, "sx": 1.05, "sy": 0.9}, "pPlane14": {"rz": 11},
         "pPlane16": {"rz": 300}, "pPlane15": {"rz": 292}, "pPlane12": {"vis": 1},
         "locator4": {"tx": -1.2},
         "Hammer_pc": {"hw": 101, "vis": 1, "tex": 3}}),
    (10, {"locator3": {"ty": -0.5, "rz": 24, "sx": 1.05, "sy": 0.91}, "pPlane14": {"rz": 11},
          "pPlane16": {"rz": 300}, "pPlane15": {"rz": 292}, "pPlane12": {"vis": 1},
          "locator4": {"tx": -1.2},
          "Hammer_pc": {"hw": 101, "vis": 1, "tex": 3}}),
]}

# Hitting a wall (mot_hammer.c, kind 7: M_H_2 / M_H_5 / M_H_8): the swing's
# first four frames, then the hammer stops up in front and he holds there,
# jolted back by the knock.
MOTIONS["M_H_2"] = {"end": 4, "keys": MOTIONS["M_H_1"]["keys"][:4] + [
    (4, {"locator2": {"ty": 0.3}, "locator3": {"rz": -3, "sy": 1.03}, "pPlane14": {"rz": -5},
         "pPlane16": {"rz": 232}, "pPlane15": {"rz": 222}, "pPlane3": {"rz": -6},
         "group1": {"sy": 0.85},
         "Hammer_pc": {"hw": 42, "vis": 1, "tex": 2}}, {"ease": "vector"}),
]}

# The spin hammer (mot_hammer.c, hammer2): the spinning paper form shows the
# pose (and draws the hammer itself). M_H_3: a crouch to wind up; M_H_3A:
# braced while it charges; M_H_3B: let go, whirled round flat out, dizzy.
MOTIONS["M_H_3"] = {"end": 2, "keys": [
    (0, {"locator3": {"ty": -0.5, "rz": 5, "sy": 0.94}, "pPlane14": {"rz": 3},
         "pPlane16": {"rz": -22}, "pPlane15": {"rz": -38},
         "locator4": {"tx": -0.5}, "locator5": {"tx": 0.4}}),
    (2, {"locator3": {"ty": -0.5, "rz": 5, "sy": 0.94}, "pPlane14": {"rz": 3},
         "pPlane16": {"rz": -22}, "pPlane15": {"rz": -38},
         "locator4": {"tx": -0.5}, "locator5": {"tx": 0.4}}),
]}
MOTIONS["M_H_3A"] = {"end": 2, "keys": [
    (0, {"locator3": {"rz": -2, "sy": 1.02}, "pPlane14": {"rz": -3},
         "pPlane16": {"rz": -16}, "pPlane15": {"rz": -30},
         "locator4": {"tx": -0.6}, "locator5": {"tx": 0.5}}),
    (2, {"locator3": {"rz": -2, "sy": 1.02}, "pPlane14": {"rz": -3},
         "pPlane16": {"rz": -16}, "pPlane15": {"rz": -30},
         "locator4": {"tx": -0.6}, "locator5": {"tx": 0.5}}),
]}
_DIZZY = {"locator2": {"rz": 90, "tx": 12.5, "ty": 10.5}, "locator3": {"rz": 6},
          "pPlane14": {"rz": 10}, "group1": {"sy": 0.18}, "pPlane12": {"vis": 1},
          "pPlane16": {"rz": -60}, "pPlane15": {"rz": 70},
          "locator4": {"tx": -1.0, "rz": -15}, "locator5": {"tx": 0.8, "rz": 15}}
MOTIONS["M_H_3B"] = {"end": 2, "keys": [(0, _DIZZY), (2, _DIZZY)]}


def rear(motion):
    """the back-facing version: the paper turn mirrors him, so the cap's L is
    mirrored back (as c_luigi's own R animations do)"""
    keys = []
    for f, pose, *opt in motion["keys"]:
        p = {g: dict(c) for g, c in pose.items()}
        p.setdefault("pPlane5", {})["sx"] = -1.0
        keys.append((f, p, *opt))
    return dict(motion, keys=keys)


def hammer_level(motion, add):
    """the same motion with the Super (add 4) or Ultra (add 8) Hammer's drawings"""
    keys = []
    for f, pose, *opt in motion["keys"]:
        p = {g: dict(c) for g, c in pose.items()}
        if "Hammer_pc" in p and "tex" in p["Hammer_pc"]:
            p["Hammer_pc"]["tex"] += add
        keys.append((f, p, *opt))
    return dict(motion, keys=keys)


for _a, _b in (("M_H_1", "M_H_1R"), ("M_H_2", "M_H_2R")):
    MOTIONS[_b] = rear(MOTIONS[_a])
# Mario's back-facing wall hit holds two frames longer (6, not 4)
MOTIONS["M_H_2R"] = dict(MOTIONS["M_H_2R"], end=6,
                         keys=MOTIONS["M_H_2R"]["keys"] + [(6,) + tuple(MOTIONS["M_H_2R"]["keys"][-1][1:])])
for _lv, _add in ((1, 4), (2, 8)):
    for _n in ("1", "2", "3", "3A", "3B", "1R", "2R"):
        _src = "M_H_" + _n
        _num = int(_n[0]) + 3 * _lv
        MOTIONS["M_H_%d%s" % (_num, _n[1:])] = hammer_level(MOTIONS[_src], _add)

# ------------------------------------------------- batch 2: spin jump, damage

# The spin jump (mot_hip.c: Super Boots): the game spins and drops the pose;
# Luigi tucks up tight, knees to his chest (M_A_1A, held), and lands from the
# tuck, unfolding a little at the end (M_A_1B).
_TUCK = {"locator2": {"ty": -1.2}, "locator3": {"ty": -1.6, "sy": 0.9375, "rz": 8}, "pPlane14": {"rz": 6},
         "locator4": {"tx": -2.6, "ty": 2.4, "rz": -40}, "locator5": {"tx": -1.8, "ty": 2.1, "rz": -30},
         "pPlane16": {"rz": -70}, "pPlane15": {"rz": -55}, "pPlane3": {"rz": -20}}
MOTIONS["M_A_1A"] = {"end": 2, "keys": [(0, _TUCK), (2, _TUCK)]}
MOTIONS["M_A_1B"] = {"end": 4, "keys": [
    (0, _TUCK), (2, _TUCK),
    (4, {"locator3": {"ty": -1.0, "sy": 0.9375, "rz": 5}, "pPlane14": {"rz": 3},
         "locator4": {"tx": -0.9, "ty": 1.6, "rz": -18}, "locator5": {"tx": -0.3, "ty": 1.4, "rz": -12},
         "pPlane16": {"rz": -40}, "pPlane15": {"rz": -30}, "pPlane3": {"rz": -10}}),
]}

# Hurt (mario.c M_D_1, 6 frames): hit from the front, knocked back with a hop,
# arms flung up, eyes screwed shut, mouth open.
_HURT = {"locator2": {"tx": 1.0, "ty": 1.2}, "locator3": {"rz": -24, "sy": 1.0625}, "pPlane14": {"rz": -14},
         "pPlane16": {"rz": 150}, "pPlane15": {"rz": 205}, "pPlane3": {"rz": 20},
         "locator4": {"tx": -1.6, "ty": 1.4, "rz": 20}, "locator5": {"tx": -0.6, "ty": 0.6, "rz": 10},
         "group1": {"sy": 0.18}, "pPlane12": {"vis": 1}}
MOTIONS["M_D_1"] = {"end": 6, "keys": [
    (0, {"locator3": {"rz": -4}, "pPlane14": {"rz": -4}, "group1": {"sy": 0.6}}),
    (2, {"locator2": {"tx": 0.5, "ty": 0.8}, "locator3": {"rz": -16, "sy": 1.0625}, "pPlane14": {"rz": -10},
         "pPlane16": {"rz": 100}, "pPlane15": {"rz": 140},
         "locator4": {"tx": -1.0, "ty": 1.0, "rz": 12}, "group1": {"sy": 0.18}, "pPlane12": {"vis": 1}}),
    (4, _HURT),
    (6, _HURT),
]}

# Knocked flat (mot_damage.c M_D_2, 8 frames): pitches forward and lands face
# down, a small bounce at the end. The pose is turned about his feet, so the
# body is lifted to rest on the ground.
_FLAT = {"locator2": {"rz": 90, "tx": 11.5, "ty": 4.2}, "locator3": {"rz": 4}, "pPlane14": {"rz": -45},
         "pPlane16": {"rz": -150}, "pPlane15": {"rz": -165},
         "locator4": {"tx": -0.6, "rz": -20}, "locator5": {"tx": 0.6, "rz": -10},
         "group1": {"sy": 0.18}}
MOTIONS["M_D_2"] = {"end": 8, "keys": [
    (0, {"locator3": {"rz": -6}, "pPlane14": {"rz": -6}, "group1": {"sy": 0.6}, "pPlane12": {"vis": 1},
         "pPlane16": {"rz": 30}, "pPlane15": {"rz": 40}}),
    (2, {"locator2": {"rz": 30, "tx": 2.0, "ty": 1.6}, "locator3": {"rz": 8}, "pPlane14": {"rz": 6},
         "pPlane16": {"rz": -60}, "pPlane15": {"rz": -70}, "locator4": {"rz": -15},
         "group1": {"sy": 0.18}, "pPlane12": {"vis": 1}}),
    (4, _FLAT, {"ease": "vector"}),
    (6, {"locator2": {"rz": 86, "tx": 11.3, "ty": 4.9}, "locator3": {"rz": 3}, "pPlane14": {"rz": -40},
         "pPlane16": {"rz": -145}, "pPlane15": {"rz": -160},
         "locator4": {"tx": -0.6, "rz": -26}, "locator5": {"tx": 0.6, "rz": -16},
         "group1": {"sy": 0.18}}),
    (8, _FLAT),
]}

# Getting up (mot_damage.c M_U_3, 12 frames): pushes up off the ground, a
# kneel, then upright with a little shake of the head.
MOTIONS["M_U_3"] = {"end": 12, "keys": [
    (0, _FLAT),
    (3, {"locator2": {"rz": 62, "tx": 7.0, "ty": 3.0}, "locator3": {"rz": 6}, "pPlane14": {"rz": -30},
         "pPlane16": {"rz": -120}, "pPlane15": {"rz": -130},
         "locator4": {"tx": -1.2, "ty": 1.0, "rz": -60}, "locator5": {"tx": 0.4, "rz": -40},
         "group1": {"sy": 0.4}}),
    (6, {"locator2": {"rz": 28, "tx": 2.6, "ty": 0.8}, "locator3": {"ty": -0.8, "rz": 4, "sy": 0.9375},
         "pPlane14": {"rz": -12}, "pPlane16": {"rz": -40}, "pPlane15": {"rz": -50},
         "locator4": {"tx": -1.6, "ty": 0.6, "rz": -30}, "group1": {"sy": 0.8}}),
    (9, {"locator2": {"rz": 6, "tx": 0.4}, "locator3": {"ty": -0.2, "rz": -2, "sy": 1.0625}, "pPlane14": {"rz": 8},
         "pPlane16": {"rz": -10}, "pPlane15": {"rz": -12}}),
    (12, {}),
]}

# Hurt and worried (mot_damage.c M_D_6, an 8-frame loop): hunched back,
# hands up by his face, bobbing, mouth open.
MOTIONS["M_D_6"] = {"end": 8, "loop": True, "keys": [
    (0, {"locator3": {"rz": -8, "sy": 1.0}, "pPlane14": {"rz": -6}, "pPlane16": {"rz": 145},
         "pPlane15": {"rz": 165}, "pPlane3": {"rz": 40}, "pPlane12": {"vis": 1}, "group1": {"sy": 0.8}}),
    (4, {"locator3": {"rz": -10, "sy": 0.9375, "ty": -0.3}, "pPlane14": {"rz": -9}, "pPlane16": {"rz": 150},
         "pPlane15": {"rz": 170}, "pPlane3": {"rz": 46}, "pPlane12": {"vis": 1}, "group1": {"sy": 0.8}}),
    (8, {"locator3": {"rz": -8, "sy": 1.0}, "pPlane14": {"rz": -6}, "pPlane16": {"rz": 145},
         "pPlane15": {"rz": 165}, "pPlane3": {"rz": 40}, "pPlane12": {"vis": 1}, "group1": {"sy": 0.8}}),
]}

# ------------------------------------------------ batch 3: item pose, Yoshi

# Holding up an item (evt_mobj.c M_I_1, 4 frames, held): his fist
# thrust high, a stretch on the way up, looking up with a grin.
MOTIONS["M_I_1"] = {"end": 4, "keys": [
    (0, {"locator3": {"rz": -3}, "pPlane14": {"rz": -5}, "pPlane16": {"rz": 128}, "pPlane3": {"rz": 30},
         "pPlane15": {"rz": 10}, "pPlane12": {"vis": 1}}),
    (2, {"locator2": {"ty": 0.3}, "locator3": {"rz": -5, "sy": 1.0625}, "pPlane14": {"rz": -8},
         "pPlane16": {"rz": 142}, "pPlane3": {"rz": 24}, "pPlane15": {"rz": 14}, "pPlane12": {"vis": 1}}),
    (4, {"locator3": {"rz": -4}, "pPlane14": {"rz": -7}, "pPlane16": {"rz": 138}, "pPlane3": {"rz": 26},
         "pPlane15": {"rz": 12}, "pPlane12": {"vis": 1}}),
]}

# Something catches his eye (M_I_Y, 8 frames, held): a little start, up on
# his toes with his eyes wide, then settling.
MOTIONS["M_I_Y"] = {"end": 8, "keys": [
    (0, {}),
    (3, {"locator2": {"ty": 0.5}, "locator3": {"rz": -4, "sy": 1.0625}, "pPlane14": {"rz": -5},
         "pPlane16": {"rz": 14}, "pPlane15": {"rz": 18}, "group1": {"sy": 1.25}}),
    (8, {"locator3": {"rz": -2}, "pPlane14": {"rz": -3}, "pPlane16": {"rz": 6}, "pPlane15": {"rz": 8},
         "group1": {"sy": 1.125}}),
]}

# Riding Yoshi (mario.c M_Y_1, M_Y_2; M_Y_3 his flutter hop): sat low and
# leaning in, hands forward on Yoshi's neck, knees up; lowered so his taller
# frame sits where Mario does.
_RIDE = {"locator2": {"ty": -2.2}, "locator3": {"ty": -1.2, "rz": 12, "sy": 0.9375}, "pPlane14": {"rz": 4},
         "pPlane16": {"rz": -62}, "pPlane15": {"rz": -72}, "pPlane3": {"rz": -15},
         "locator4": {"tx": -2.2, "ty": 2.0, "rz": -30}, "locator5": {"tx": -1.4, "ty": 1.8, "rz": -22}}
MOTIONS["M_Y_1"] = {"end": 0, "keys": [(0, _RIDE)]}
MOTIONS["M_Y_2"] = {"end": 0, "loop": True, "keys": [(0, _RIDE)]}
MOTIONS["M_Y_3"] = {"end": 0, "keys": [(0, dict(_TUCK, locator2={"ty": -1.6}))]}

# ------------------------------------------- batch 4: event poses (e_mario)
# The poses the story scripts give Mario (evt_mario_set_pose). Timing is
# Mario's to the frame: where his drawing tilts his face, Luigi's head turns
# as far (his face's angle from rest; + is a nod, his head toward his front);
# where his drawings swap for talking or a blink, Luigi's mouth opens or his
# eyes shut on the same frames.


def _blinks(*pairs):
    """eyes shut at each pair's first frame, open at its second (group1's sy)"""
    out = [(0, 1.0, "step")]
    for a, b in pairs:
        out += [(a, 0.18, "step"), (b, 1.0, "step")]
    return out


def _talk(times, end):
    """the mouth open and shut in turn at each of `times` (pPlane12 shown)"""
    out = [(0, 0, "step")]
    for i, t in enumerate(times):
        out.append((t, 1 if i % 2 == 0 else 0, "step"))
    if out[-1][0] != end:
        out.append((end, out[-1][1], "step"))
    return out


# looks up a little (M_N_1, 12 frames)
MOTIONS["M_N_1"] = {"end": 12, "tracks": {
    "pPlane14": {"rz": [(0, 0.0), (12, -5.6)]},
    "locator3": {"rz": [(0, 0.0), (12, -1.5)]}}}

# nods twice (M_N_2, 18 frames: the story's most used)
MOTIONS["M_N_2"] = {"end": 18, "tracks": {
    "pPlane14": {"rz": [(0, 0.0), (2, 15.8), (4, 15.8), (7, -1.2), (9, 0.0), (11, 15.8), (13, 15.8),
                        (16, -1.2), (18, 0.0)]},
    "locator3": {"rz": [(0, 0.0), (2, 3.0), (4, 3.0), (7, -0.5), (9, 0.0), (11, 3.0), (13, 3.0), (16, -0.5),
                        (18, 0.0)]}}}

# a thoughtful look up, then two blinks (M_N_3, 32 frames)
MOTIONS["M_N_3"] = {"end": 32, "tracks": {
    "pPlane14": {"rz": [(0, -2.1), (2, -2.8), (3, -3.1), (6, -4.1), (32, -4.1)]},
    "group1": {"sy": _blinks((20, 22), (26, 28)) + [(32, 1.0, "step")]}}}

# talking (M_N_B, 34 frames; M_N_C, 70 frames)
MOTIONS["M_N_B"] = {"end": 34, "tracks": {
    "pPlane12": {"vis": _talk([4, 6, 10, 12, 16, 18, 22, 24, 28, 30, 34], 34)},
    "pPlane14": {"rz": [(0, 0.0), (4, 2.0), (10, -1.0), (16, 2.0), (22, -1.0), (28, 2.0), (34, 0.0)]}}}
MOTIONS["M_N_C"] = {"end": 70, "tracks": {
    "pPlane12": {"vis": _talk(list(range(2, 71, 2)), 70)},
    "pPlane14": {"rz": [(0, 0.0), (8, 2.0), (18, -1.5), (28, 2.0), (38, -1.5), (48, 2.0), (58, -1.5),
                        (70, 0.0)]}}}

# listening (M_I_N, a 120-frame loop): head a touch back, two blinks
MOTIONS["M_I_N"] = {"end": 120, "loop": True, "tracks": {
    "pPlane14": {"rz": [(0, -2.8), (120, -2.8)]},
    "group1": {"sy": _blinks((20, 22), (26, 28)) + [(120, 1.0, "step")]}}}

# hangs his head (M_I_Z, 12 frames)
MOTIONS["M_I_Z"] = {"end": 12, "tracks": {
    "pPlane14": {"rz": [(0, 0.0), (12, 15.8)]},
    "locator3": {"rz": [(0, 0.0), (12, 4.0)], "sy": [(0, 1.0), (12, 0.9375)]},
    "pPlane16": {"rz": [(0, 0.0), (12, 6.0)]}, "pPlane15": {"rz": [(0, 0.0), (12, 6.0)]}}}

# a hand to his mouth (M_N_10, 6 frames)
MOTIONS["M_N_10"] = {"end": 6, "tracks": {
    "pPlane14": {"rz": [(0, 0.0), (2, 3.8), (3, 5.7), (4, 7.5), (6, 7.5)]},
    "pPlane16": {"rz": [(0, 0.0), (2, 196.0), (4, 214.0), (6, 214.0)]},
    "pPlane3": {"rz": [(0, 0.0), (4, 30.0), (6, 30.0)]}}}

# fists clenched, determined (M_I_G, 6 frames)
MOTIONS["M_I_G"] = {"end": 6, "tracks": {
    "pPlane14": {"rz": [(0, 0.0), (2, -3.3), (6, -3.3)]},
    "locator3": {"ty": [(0, 0.0), (2, -0.4), (6, -0.3)], "rz": [(0, 0.0), (2, 4.0), (6, 3.0)]},
    "pPlane16": {"rz": [(0, 0.0), (2, -38.0), (6, -34.0)]},
    "pPlane15": {"rz": [(0, 0.0), (2, -48.0), (6, -44.0)]}}}

# thinking, hand to his chin (M_I_5, 50 frames)
MOTIONS["M_I_5"] = {"end": 50, "tracks": {
    "pPlane14": {"rz": [(0, -14.1), (4, -16.1), (6, -15.2), (8, -14.2), (12, -16.1), (14, -15.2), (16, -14.2),
                        (20, -16.1), (22, -15.2), (24, -14.2), (28, -16.1), (30, -15.2), (32, -14.2),
                        (36, -16.1), (38, -15.5), (40, -14.2), (42, -13.3), (50, -13.3)]},
    "pPlane16": {"rz": [(0, 206.0), (50, 206.0)]}, "pPlane3": {"rz": [(0, 40.0), (50, 40.0)]},
    "pPlane15": {"rz": [(0, -20.0), (50, -20.0)]}}}

# marching on the spot (M_N_7, an 18-frame loop): a step every 4.5 frames
MOTIONS["M_N_7"] = {"end": 18, "loop": True, "tracks": {
    "locator2": {"ty": [(0, 0.44), (0.75, 0.5), (2.25, 0.75), (3.75, 0.81), (5.25, 0.56), (6, 0.5),
                        (8.25, 0.69), (9.75, 0.81), (11.25, 0.56), (12.75, 0.5), (14.25, 0.75), (15.75, 0.81),
                        (17.25, 0.56), (18, 0.44)]},
    "pPlane14": {"rz": [(0, 11.8), (18, 11.8)]},
    "locator4": {"ty": [(0, 0.0), (2.25, 1.6), (4.5, 0.0), (9, 0.0), (11.25, 1.6), (13.5, 0.0), (18, 0.0)]},
    "locator5": {"ty": [(0, 0.0), (4.5, 0.0), (6.75, 1.6), (9, 0.0), (13.5, 0.0), (15.75, 1.6), (18, 0.0)]},
    "pPlane16": {"rz": [(0, -30.0), (4.5, 30.0), (9, -30.0), (13.5, 30.0), (18, -30.0)]},
    "pPlane15": {"rz": [(0, 30.0), (4.5, -30.0), (9, 30.0), (13.5, -30.0), (18, 30.0)]}}}

# stamping, impatient (M_I_O, a 24-frame loop): quick steps every 1.5 frames
_ST = [i * 1.5 for i in range(17)]
MOTIONS["M_I_O"] = {"end": 24, "loop": True, "tracks": {
    "pPlane14": {"rz": [(0, 3.9), (3, 5.9), (6, 4.0), (9, 5.9), (12, 4.0), (15, 5.9), (18, 4.0), (21, 5.9),
                        (24, 3.9)]},
    "locator2": {"ty": [(0, 0.44), (24, 0.44)]},
    "locator4": {"ty": [(t, 1.2 if i % 2 else 0.0, "linear") for i, t in enumerate(_ST)]},
    "locator5": {"ty": [(t, 0.0 if i % 2 else 1.2, "linear") for i, t in enumerate(_ST)]},
    "pPlane16": {"rz": [(0, 60.0), (3, 110.0), (6, 60.0), (9, 110.0), (12, 60.0), (15, 110.0), (18, 60.0),
                        (21, 110.0), (24, 60.0)]},
    "pPlane15": {"rz": [(0, 110.0), (3, 60.0), (6, 110.0), (9, 60.0), (12, 110.0), (15, 60.0), (18, 110.0),
                        (21, 60.0), (24, 110.0)]}}}

# jumps in surprise (M_N_5B, 32 frames): squash, up, land, settle
MOTIONS["M_N_5B"] = {"end": 32, "keys": [
    (0, {"locator2": {"ty": -0.7}, "locator3": {"sy": 0.875}, "group1": {"sy": 1.25}}),
    (2, {"locator3": {"sy": 1.0625}, "pPlane16": {"rz": 60}, "pPlane15": {"rz": 70},
         "group1": {"sy": 1.375}, "pPlane12": {"vis": 1}}),
    (8, {"locator2": {"ty": 2.3}, "locator3": {"sy": 1.125, "rz": -6}, "pPlane14": {"rz": -8},
         "pPlane16": {"rz": 150}, "pPlane15": {"rz": 165}, "locator4": {"ty": 0.8, "rz": 15},
         "locator5": {"ty": 0.6, "rz": 10}, "group1": {"sy": 1.375}, "pPlane12": {"vis": 1}}),
    (13, {"locator2": {"ty": 0.6}, "locator3": {"sy": 1.0625, "rz": -3}, "pPlane14": {"rz": -4},
          "pPlane16": {"rz": 120}, "pPlane15": {"rz": 135}, "group1": {"sy": 1.25}, "pPlane12": {"vis": 1}}),
    (15, {"locator2": {"ty": -0.3}, "locator3": {"sy": 0.875}, "pPlane16": {"rz": 40}, "pPlane15": {"rz": 50},
          "group1": {"sy": 1.25}, "pPlane12": {"vis": 1}}, {"ease": "vector"}),
    (16, {"locator3": {"sy": 0.9375}, "pPlane16": {"rz": 25}, "pPlane15": {"rz": 30}, "group1": {"sy": 1.25},
          "pPlane12": {"vis": 1}}),
    (32, {"group1": {"sy": 1.25}}),  # (wide eyes held: scale steps in sixteenths)
]}

# standing, breathing (M_N_5A, a 140-frame loop): a slow sway of the body
MOTIONS["M_N_5A"] = {"end": 140, "loop": True, "tracks": {
    "locator3": {"rz": [(0, 0.0), (26, 4.0), (40, 5.0), (50, 6.0), (60, 5.0), (74, 3.0), (110, 1.0),
                        (140, 0.0)]},
    "pPlane14": {"rz": [(0, 0.0), (50, -3.0), (110, -1.0), (140, 0.0)]}}}

# fists together, ready (M_C_3, 16 frames)
_READY = {"locator2": {"ty": 0.3}, "locator3": {"sy": 1.0625}, "pPlane14": {"rz": -6}, "pPlane16": {"rz": -66},
          "pPlane15": {"rz": -78}, "pPlane3": {"rz": -22}}
MOTIONS["M_C_3"] = {"end": 16, "keys": [
    (0, {"pPlane14": {"rz": -5}, "pPlane16": {"rz": -62}, "pPlane15": {"rz": -74}, "pPlane3": {"rz": -20}}),
    (10, _READY), (16, _READY)]}

# braced, fists low (M_N_9, 2 frames, held)
_BRACE = {"locator3": {"ty": -0.4, "sy": 0.9375, "rz": 4}, "pPlane14": {"rz": 2},
          "pPlane16": {"rz": -34}, "pPlane15": {"rz": -46}, "locator4": {"tx": -0.6}, "locator5": {"tx": 0.6}}
MOTIONS["M_N_9"] = {"end": 2, "keys": [(0, _BRACE), (2, _BRACE)]}

# mid-stride, held (M_N_H, 2 frames)
_STRIDE = {"locator2": {"ty": 0.4}, "locator3": {"rz": 8}, "pPlane14": {"rz": 4},
           "locator4": {"tx": -1.6, "ty": 0.8, "rz": 15}, "locator5": {"tx": 1.2, "ty": 0.4, "rz": -10},
           "pPlane16": {"rz": 40}, "pPlane15": {"rz": -40}}
MOTIONS["M_N_H"] = {"end": 2, "keys": [(0, _STRIDE), (2, _STRIDE)]}

# sitting on the ground (M_S_5, a 180-frame loop, breathing; M_N_11, 18 frames)
_SIT = {"locator2": {"ty": -4.2}, "locator3": {"ty": -0.6, "rz": -4}, "pPlane14": {"rz": -2},
        "pPlane16": {"rz": -30}, "pPlane15": {"rz": -40},
        "locator4": {"tx": -3.0, "ty": 3.6, "rz": -60}, "locator5": {"tx": -2.2, "ty": 3.4, "rz": -50}}
MOTIONS["M_S_5"] = {"end": 180, "loop": True, "keys": [
    (0, _SIT), (90, dict(_SIT, locator3={"ty": -0.5, "rz": -4, "sy": 1.0625})), (180, _SIT)]}
MOTIONS["M_N_11"] = {"end": 18, "keys": [
    (0, _SIT), (6, dict(_SIT, pPlane14={"rz": 8})), (12, dict(_SIT, pPlane14={"rz": -3})), (18, _SIT)]}

# dazed on the ground, in tears (M_D_7, an 8-frame loop); then coming round (M_D_7B)
_DAZED = dict(_SIT, pPlane14={"rz": 6}, group1={"sy": 0.18}, pPlane12={"vis": 1})
MOTIONS["M_D_7"] = {"end": 8, "loop": True, "keys": [
    (0, _DAZED), (4, dict(_DAZED, pPlane14={"rz": -2}, locator3={"ty": -0.7, "rz": -2})), (8, _DAZED)]}
MOTIONS["M_D_7B"] = {"end": 8, "keys": [
    (0, _DAZED), (4, dict(_SIT, pPlane14={"rz": -6}, group1={"sy": 1.25})),
    (8, dict(_SIT, pPlane14={"rz": -4}, group1={"sy": 1.125}))]}

# asleep on his back (M_I_6)
_ASLEEP = {"locator2": {"rz": -90, "tx": -11.0, "ty": 4.4}, "pPlane14": {"rz": 30}, "group1": {"sy": 0.18},
           "pPlane16": {"rz": 20}, "pPlane15": {"rz": 30}, "locator4": {"rz": 20}, "locator5": {"rz": 10}}
MOTIONS["M_I_6"] = {"end": 0, "keys": [(0, _ASLEEP)]}

# keels over (M_D_8, 16 frames): the knocked-flat fall at half speed
MOTIONS["M_D_8"] = {"end": 16, "keys": [(2 * f, pose, *opt) for f, pose, *opt in MOTIONS["M_D_2"]["keys"]]}

# eating, hands to his mouth (M_L_2, M_L_3, 130-frame loops)
_EAT = {"pPlane14": {"rz": 4}, "pPlane16": {"rz": 210}, "pPlane3": {"rz": 30}, "pPlane15": {"rz": 196},
        "pPlane2": {"rz": 20}, "group1": {"sy": 0.6}}
MOTIONS["M_L_2"] = {"end": 130, "loop": True, "keys": [
    (0, _EAT), (32, dict(_EAT, pPlane14={"rz": 7}, locator3={"sy": 0.9375})), (65, _EAT),
    (97, dict(_EAT, pPlane14={"rz": 7}, locator3={"sy": 0.9375})), (130, _EAT)]}
MOTIONS["M_L_3"] = MOTIONS["M_L_2"]

# hopping for joy (M_F_1, a 16-frame loop): two hops, arms up in turn
MOTIONS["M_F_1"] = {"end": 16, "loop": True, "keys": [
    (0, {"locator3": {"sy": 0.9375}, "pPlane16": {"rz": 150}, "pPlane15": {"rz": 60}, "pPlane12": {"vis": 1}}),
    (4, {"locator2": {"ty": 1.6}, "locator3": {"sy": 1.0625}, "pPlane16": {"rz": 170}, "pPlane15": {"rz": 90},
         "locator4": {"ty": 0.6}, "pPlane12": {"vis": 1}}),
    (8, {"locator3": {"sy": 0.9375}, "pPlane16": {"rz": 60}, "pPlane15": {"rz": 150}, "pPlane12": {"vis": 1}}),
    (12, {"locator2": {"ty": 1.6}, "locator3": {"sy": 1.0625}, "pPlane16": {"rz": 90}, "pPlane15": {"rz": 170},
          "locator5": {"ty": 0.6}, "pPlane12": {"vis": 1}}),
    (16, {"locator3": {"sy": 0.9375}, "pPlane16": {"rz": 150}, "pPlane15": {"rz": 60}, "pPlane12": {"vis": 1}}),
]}

# a wind-up and throw (M_W_4B, 8 frames)
MOTIONS["M_W_4B"] = {"end": 8, "keys": [
    (0, {"locator3": {"rz": -8}, "pPlane16": {"rz": 120}, "locator4": {"ty": 0.6, "tx": -0.4}}),
    (3, {"locator3": {"rz": -12}, "pPlane16": {"rz": 150}, "locator4": {"ty": 0.8, "tx": -0.6}}),
    (5, {"locator3": {"rz": 12}, "pPlane16": {"rz": 280}, "locator4": {"tx": -1.4}}, {"ease": "vector"}),
    (8, {"locator3": {"rz": 8}, "pPlane16": {"rz": 290}, "locator4": {"tx": -1.4}}),
]}

# ------------------------------------------------ batch 5: battle (b_mario)
# Battle animations of Luigi's are named "<Mario's name>@b" (luigi_pc.c
# looks for those in battle: b_mario reuses field names for other motions).
# Where Mario's battle pose is his field or event one again (same length,
# same purpose, drawn a little differently), Luigi's is the same motion.

for _n in ("M_N_2", "M_I_Y", "M_I_O", "M_N_C", "M_U_3", "M_A_1A", "M_A_1B", "M_D_7", "M_D_6"):
    MOTIONS[_n + "@b"] = MOTIONS[_n]
MOTIONS["M_N_5@b"] = MOTIONS["M_N_5A"]

# guarding, ready for the next move (M_C_3 in battle, a 180-frame loop):
# fists up in front, breathing
_GUARD = {"locator3": {"ty": -0.3, "rz": 6, "sy": 0.9375}, "pPlane14": {"rz": -4},
          "pPlane16": {"rz": -95}, "pPlane3": {"rz": -30}, "pPlane15": {"rz": -70}, "pPlane2": {"rz": -20},
          "locator4": {"tx": -0.8}, "locator5": {"tx": 0.7}}
MOTIONS["M_C_3@b"] = {"end": 180, "loop": True, "keys": [
    (0, _GUARD), (90, dict(_GUARD, locator3={"ty": -0.1, "rz": 6, "sy": 1.0}, pPlane16={"rz": -100})),
    (180, _GUARD)]}

# in peril, worn out (M_I_S, a 96-frame loop, field and battle): hunched,
# head low, swaying, breathing hard with his mouth open
_TIRED = {"locator3": {"ty": -0.5, "rz": 10, "sy": 0.9375}, "pPlane14": {"rz": 12}, "pPlane16": {"rz": 8},
          "pPlane15": {"rz": 10}, "group1": {"sy": 0.6}, "pPlane12": {"vis": 1}}
MOTIONS["M_I_S"] = {"end": 96, "loop": True, "keys": [
    (0, _TIRED), (24, dict(_TIRED, locator3={"ty": -0.3, "rz": 6, "sy": 1.0}, pPlane14={"rz": 8})),
    (48, dict(_TIRED, locator3={"ty": -0.5, "rz": 12, "sy": 0.9375})),
    (72, dict(_TIRED, locator3={"ty": -0.3, "rz": 7, "sy": 1.0}, pPlane14={"rz": 9})), (96, _TIRED)]}
MOTIONS["M_I_S@b"] = MOTIONS["M_I_S"]

# victory fist (M_V_2, 6 frames, field and battle) and a bow to the crowd
# with a wave (M_V_1, battle, 60 frames)
_FIST = {"locator2": {"ty": 0.3}, "locator3": {"rz": -4, "sy": 1.0625}, "pPlane14": {"rz": -6},
         "pPlane16": {"rz": 168}, "pPlane3": {"rz": 10}, "pPlane12": {"vis": 1}}
MOTIONS["M_V_2"] = {"end": 6, "keys": [(0, {"pPlane16": {"rz": 40}}), (3, _FIST, {"ease": "vector"}),
                                     (6, dict(_FIST, pPlane16={"rz": 162}))]}
MOTIONS["M_V_1@b"] = {"end": 60, "keys": [
    (0, {}),
    (12, {"locator3": {"rz": 22, "ty": -0.4}, "pPlane14": {"rz": 12}, "pPlane16": {"rz": -20},
          "pPlane15": {"rz": -25}, "group1": {"sy": 0.18}}),
    (28, {"locator3": {"rz": 22, "ty": -0.4}, "pPlane14": {"rz": 12}, "pPlane16": {"rz": -20},
          "pPlane15": {"rz": -25}, "group1": {"sy": 0.18}}),
    (38, {"pPlane14": {"rz": -4}, "pPlane16": {"rz": 150}, "pPlane12": {"vis": 1}}),
    (44, {"pPlane14": {"rz": -4}, "pPlane16": {"rz": 170}, "pPlane3": {"rz": 20}, "pPlane12": {"vis": 1}}),
    (50, {"pPlane14": {"rz": -4}, "pPlane16": {"rz": 150}, "pPlane12": {"vis": 1}}),
    (60, {}),
]}

# a cheer, fist up for a moment (M_N_8, battle, 60 frames)
MOTIONS["M_N_8@b"] = {"end": 60, "keys": [
    (0, {}), (10, _FIST, {"ease": "vector"}), (45, dict(_FIST, pPlane16={"rz": 162})), (60, {})]}

# bouncing off an enemy after a jump (M_J_1D, battle, 6 frames)
MOTIONS["M_J_1D@b"] = {"end": 6, "keys": [
    (0, {"locator3": {"sy": 0.875}, "locator4": {"ty": 0.8}, "locator5": {"ty": 0.6},
         "pPlane16": {"rz": 30}, "pPlane15": {"rz": 40}}),
    (3, {"locator3": {"sy": 1.0625}, "pPlane16": {"rz": 120}, "pPlane15": {"rz": 140}, "pPlane12": {"vis": 1}}),
    (6, {"locator3": {"sy": 1.0}, "pPlane16": {"rz": 100}, "pPlane15": {"rz": 120}, "pPlane12": {"vis": 1}}),
]}

# jump attacks in the air (battle): fist up on the way (M_A_3A held, M_A_3B),
# a tucked somersault (M_A_4), upside down (M_A_5), knocked flying backwards (M_A_6)
MOTIONS["M_A_3A@b"] = {"end": 0, "keys": [(0, {"locator3": {"rz": -4}, "pPlane16": {"rz": 40}})]}
MOTIONS["M_A_3B@b"] = {"end": 4, "keys": [
    (0, {"pPlane16": {"rz": 60}, "locator3": {"sy": 1.0}}),
    (2, {"locator3": {"sy": 1.0625, "rz": -6}, "pPlane16": {"rz": 172}, "pPlane15": {"rz": 30},
         "locator4": {"ty": 0.8, "rz": 15}, "pPlane12": {"vis": 1}}),
    (4, {"locator3": {"sy": 1.0625, "rz": -6}, "pPlane16": {"rz": 168}, "pPlane15": {"rz": 30},
         "locator4": {"ty": 0.8, "rz": 15}, "pPlane12": {"vis": 1}}),
]}
MOTIONS["M_A_4@b"] = {"end": 4, "loop": True, "tracks": {
    "locator3": {"rz": [(0, 0.0, "linear"), (1, -90.0, "linear"), (2, -180.0, "linear"), (3, -270.0, "linear"),
                        (4, -360.0, "linear")],
                 "ty": [(0, 4.0), (4, 4.0)], "sy": [(0, 0.9375), (4, 0.9375)]},
    "locator4": {"tx": [(0, -1.6), (4, -1.6)], "ty": [(0, 6.0), (4, 6.0)], "rz": [(0, -40.0), (4, -40.0)]},
    "locator5": {"tx": [(0, -0.8), (4, -0.8)], "ty": [(0, 5.6), (4, 5.6)], "rz": [(0, -30.0), (4, -30.0)]},
    "pPlane16": {"rz": [(0, -70.0), (4, -70.0)]}, "pPlane15": {"rz": [(0, -55.0), (4, -55.0)]}}}
_UPSIDE = dict(_TUCK, locator2={"rz": 180, "ty": 20.0})
MOTIONS["M_A_5@b"] = {"end": 2, "keys": [(0, _UPSIDE), (2, _UPSIDE)]}
_FLYING = {"locator2": {"rz": -90, "tx": -11.5, "ty": 6.0}, "pPlane14": {"rz": 40}, "pPlane16": {"rz": 40},
           "pPlane15": {"rz": 55}, "group1": {"sy": 0.18}, "pPlane12": {"vis": 1},
           "locator4": {"rz": 15}, "locator5": {"rz": 25, "tx": -0.6}}
MOTIONS["M_A_6@b"] = {"end": 0, "keys": [(0, _FLYING)]}

# stunned, reeling back (M_Z_10) and laid out face down (M_Z_11), battle statuses
MOTIONS["M_Z_10@b"] = {"end": 0, "keys": [(0, {
    "locator3": {"rz": -16}, "pPlane14": {"rz": -12}, "group1": {"sy": 0.18}, "pPlane12": {"vis": 1},
    "pPlane16": {"rz": 60}, "pPlane15": {"rz": 80}, "locator4": {"tx": -0.8, "ty": 0.4}})]}
MOTIONS["M_Z_11@b"] = {"end": 0, "keys": [(0, _FLAT)]}

# using an item, holding it out in front (M_C_4, battle, 16 frames)
_ITEM = {"pPlane14": {"rz": 4}, "pPlane16": {"rz": -70}, "pPlane3": {"rz": -20}, "pPlane15": {"rz": -80},
         "pPlane2": {"rz": -20}}
MOTIONS["M_C_4@b"] = {"end": 16, "keys": [
    (0, _ITEM), (8, dict(_ITEM, locator2={"ty": 0.4}, locator3={"sy": 1.0625}, pPlane16={"rz": -80},
                         pPlane15={"rz": -90})), (16, _ITEM)]}

# ---------------------------------------------- batch 6: battle hammer moves
# Mario's hammer is a part of its own, so its angle at each of his keys is
# the hammer's angle in the world: Luigi's swings take those angles and
# frames (and which drawing shows), and his arms follow them, the hammer
# carrying on the line of his arms from the fist, his body leaning into the
# swing.


def _hammer_swing(end, pts, loop=False, body=None, add=0):
    """pts: [(frame, hammer angle or None for hidden, drawing)]; body: extra
    keyed poses {frame: {group: {channel: value}}} merged into the generated ones"""
    keys = []
    prev = None
    for f, hw, tex in pts:
        if hw is not None and prev is not None:
            while hw - prev > 180:  # the shorter way round from the last key
                hw -= 360
            while prev - hw > 180:
                hw += 360
        if hw is not None:
            prev = hw
        h = prev if prev is not None else 0.0
        lean = max(-12.0, min(18.0, (((h + 180) % 360) - 180) / 7.0))
        arm = h + 180 - lean
        pose = {"locator3": {"rz": lean}, "pPlane14": {"rz": lean * 0.5},
                "pPlane16": {"rz": arm}, "pPlane15": {"rz": arm - 12},
                "pPlane12": {"vis": 1 if abs(lean) > 10 else 0},
                "Hammer_pc": {"hw": h, "vis": 0 if hw is None else 1, "tex": tex + add}}
        for g, ch in ((body or {}).get(f, {})).items():
            pose.setdefault(g, {}).update(ch)
        keys.append((f, pose))
    return {"end": end, "loop": loop, "keys": keys}


_H1A = [(0, 89, 3), (2, 90, 3), (4, 92, 3), (6, 94, 3), (8, 96, 3), (10, 97, 3), (11, 90, 3), (12, 76, 3),
        (13, 62, 2), (14, 47, 2), (15, 33, 2), (16, 17, 2), (17, 0, 2), (18, -20, 2), (19, -37, 2), (20, -56, 0),
        (21, -85, 0), (22, -108, 0), (24, -104, 0)]
_H1B = [(0, -108, 0), (1, -106, 0), (3, -99, 0), (4, -101, 0), (6, -108, 0)]
_H1C = [(0, -108, 0), (1, -112, 0), (2, -79, 0), (3, -22, 2), (4, 42, 2), (5, 82, 3), (6, 101, 3), (7, 103, 3),
        (8, 97, 3), (10, 97, 3)]
_H13 = [(0, -108, 0), (1, -105, 0), (2, -67, 0), (3, 0, 2), (4, 15, 2), (5, 29, 3), (6, 52, 3), (7, 82, 3),
        (8, 105, 3), (9, 106, 3), (10, 105, 3)]
_H17A = [(0, 96, 3), (1, 96, 3), (2, 47, 0), (3, 47, 0), (4, 52, 0), (5, 56, 0), (6, 60, 0)]
_H17B = [(0, 60, 0), (3, 62, 0), (6, 60, 0)]
# (round behind him: the angles keep going past 180 at frame 6)
_H17C = [(0, 60, 0), (1, 52, 0), (3, 16, 0), (4, -45, 3), (5, -90, 3), (6, -252, 2), (7, -268, 2), (8, -311, 0),
         (9, -362, 0), (10, -397, 0), (12, -397, 0)]
_H11 = [(0, -108, 0), (1, -112, 0), (2, -81, 0), (3, -24, 1), (4, None, 1), (5, None, 1), (6, None, 1),
        (8, None, 1), (10, None, 1)]
# the big overhead slam throws his weight forward and up off his feet (13-15)
_SLAM_BODY = {6: {"locator2": {"ty": 0.6}}, 7: {"locator2": {"ty": 1.0}, "locator3": {"rz": 30}},
              8: {"locator2": {"ty": 0.8}, "locator3": {"rz": 40}},
              9: {"locator3": {"rz": 42}}, 10: {"locator3": {"rz": 40}}}
for _lv, _add in ((0, 0), (1, 4), (2, 8)):
    _a, _b, _c = (("M_H_1A", "M_H_1B", "M_H_1C"), ("M_H_5A", "M_H_5B", "M_H_5C"),
                  ("M_H_9A", "M_H_9B", "M_H_9C"))[_lv]
    # wound up behind his head (A), held there (B), brought down (C)
    MOTIONS[_a + "@b"] = _hammer_swing(24, _H1A, add=_add)
    MOTIONS[_b + "@b"] = _hammer_swing(6, _H1B, loop=True, add=_add)
    MOTIONS[_c + "@b"] = _hammer_swing(10, _H1C, add=_add, body={5: {"locator2": {"ty": -0.3}}})
    MOTIONS["M_H_%d@b" % (13 + _lv)] = _hammer_swing(10, _H13, add=_add, body=_SLAM_BODY)
    # up over his head (A), held (B), and swung round behind him (C)
    _n = 17 + _lv
    MOTIONS["M_H_%dA@b" % _n] = _hammer_swing(6, _H17A, add=_add)
    MOTIONS["M_H_%dB@b" % _n] = _hammer_swing(6, _H17B, loop=True, add=_add)
    MOTIONS["M_H_%dC@b" % _n] = _hammer_swing(12, _H17C, add=_add)
# a swing that lets go of the hammer (M_H_11: it flies on its own after frame 4)
MOTIONS["M_H_11@b"] = _hammer_swing(10, _H11, add=8)
# a tumble and back on his feet, hammer away (M_H_16)
MOTIONS["M_H_16@b"] = {"end": 10, "keys": [
    (0, dict(_FLAT, group1={"sy": 0.6})),
    (2, {"locator2": {"rz": 60, "tx": 7.0, "ty": 3.0}, "locator3": {"rz": 6}, "pPlane14": {"rz": -30},
         "pPlane16": {"rz": -120}, "pPlane15": {"rz": -130}, "locator4": {"tx": -1.2, "ty": 1.0, "rz": -60}}),
    (4, {"locator2": {"rz": 28, "tx": 2.6, "ty": 0.8}, "locator3": {"ty": -0.8, "sy": 0.9375},
         "pPlane14": {"rz": -12}, "pPlane16": {"rz": -40}, "pPlane15": {"rz": -50}}),
    (6, {"locator3": {"sy": 1.0625}, "pPlane14": {"rz": -4}}),
    (10, {}),
]}
# the spin hammer's poses in battle at the wooden and Ultra levels (Super's
# are the field's M_H_6A/6B, which battle uses as they are)
MOTIONS["M_H_2A@b"] = MOTIONS["M_H_3A"]
MOTIONS["M_H_2B@b"] = MOTIONS["M_H_3B"]
MOTIONS["M_H_10A@b"] = MOTIONS["M_H_9A"]
MOTIONS["M_H_10B@b"] = MOTIONS["M_H_9B"]
MOTIONS["M_H_10@b"] = MOTIONS["M_H_3A"]

# ------------------------------------------------ batch 7: the last of battle

# a throw (M_C_1, 8 frames, field and battle; M_C_1R facing away): fist back
# and up, over the top, and down in front, the back foot kicking up
MOTIONS["M_C_1"] = {"end": 8, "keys": [
    (0, {"locator3": {"rz": -10}, "pPlane14": {"rz": -6}, "pPlane16": {"rz": 150}, "pPlane15": {"rz": -20},
         "locator4": {"tx": -0.6, "ty": 0.4}}),
    (2, {"locator3": {"rz": -4, "sy": 1.0625}, "pPlane14": {"rz": -4}, "pPlane16": {"rz": 190},
         "pPlane15": {"rz": -10}, "locator4": {"tx": -0.8, "ty": 0.3}}),
    (3, {"locator3": {"rz": 8}, "pPlane14": {"rz": 2}, "pPlane16": {"rz": 235}, "pPlane12": {"vis": 1},
         "locator4": {"tx": -1.0}}, {"ease": "linear"}),
    (5, {"locator3": {"rz": 18, "sy": 0.9375}, "pPlane14": {"rz": 8}, "pPlane16": {"rz": 290}, "pPlane15": {"rz": 30},
         "pPlane12": {"vis": 1}, "locator4": {"tx": -1.4}, "locator5": {"tx": 1.0, "ty": 1.2, "rz": -25}},
     {"ease": "vector"}),
    (8, {"locator3": {"rz": 16, "sy": 0.9375}, "pPlane14": {"rz": 7}, "pPlane16": {"rz": 286}, "pPlane15": {"rz": 26},
         "pPlane12": {"vis": 1}, "locator4": {"tx": -1.4}, "locator5": {"tx": 1.0, "ty": 1.0, "rz": -20}}),
]}
MOTIONS["M_C_1@b"] = MOTIONS["M_C_1"]
MOTIONS["M_C_1R"] = rear(MOTIONS["M_C_1"])

# getting ready (M_C_2A, 14 frames): hands come up to his chest; holding it
# (M_C_2B, a 6-frame loop); then a fist to the crowd, a lean and a kick of the
# heel before settling (M_C_2C, 36 frames)
_CHEST = {"locator3": {"rz": 3}, "pPlane14": {"rz": -2}, "pPlane16": {"rz": -60}, "pPlane3": {"rz": -20},
          "pPlane15": {"rz": -72}}
MOTIONS["M_C_2A@b"] = {"end": 14, "keys": [(0, {}), (6, dict(_CHEST, pPlane16={"rz": -40})), (14, _CHEST)]}
MOTIONS["M_C_2B@b"] = {"end": 6, "loop": True, "keys": [
    (0, _CHEST), (3, dict(_CHEST, locator3={"rz": 3, "sy": 0.9375})), (6, _CHEST)]}
_CROWD = {"locator3": {"rz": -10, "sy": 1.0625}, "pPlane14": {"rz": -8}, "pPlane16": {"rz": 140},
          "pPlane3": {"rz": 26}, "pPlane15": {"rz": -30}, "pPlane12": {"vis": 1},
          "locator5": {"tx": 1.0, "ty": 1.2, "rz": -25}}
MOTIONS["M_C_2C@b"] = {"end": 36, "keys": [
    (0, _CROWD, {"ease": "vector"}),
    (7, dict(_CROWD, locator3={"rz": -4, "sy": 1.0625}, pPlane16={"rz": 146})),
    (14, dict(_CROWD, locator3={"rz": 2, "sy": 1.0}, pPlane16={"rz": 136})),
    (22, dict(_CROWD, locator3={"rz": 4, "sy": 1.0}, pPlane16={"rz": 142})),
    (29, dict(_CROWD, locator3={"rz": 2, "sy": 1.0}, pPlane16={"rz": 137})),
    (36, {}),
]}

# bending low to spring (M_P_1, 8 frames, field and battle), and springing
# up, fist high (M_P_2)
MOTIONS["M_P_1"] = {"end": 8, "keys": [
    (0, {"pPlane16": {"rz": -20}, "pPlane15": {"rz": -30}}),
    (4, {"locator3": {"ty": -0.6, "rz": 10, "sy": 0.9375}, "pPlane14": {"rz": 5}, "pPlane16": {"rz": -40},
         "pPlane15": {"rz": -50}, "locator4": {"tx": -0.6}}),
    (8, {"locator3": {"ty": -1.0, "rz": 16, "sy": 0.875}, "pPlane14": {"rz": 8}, "pPlane16": {"rz": -50},
         "pPlane15": {"rz": -60}, "locator4": {"tx": -0.8}, "locator5": {"tx": 0.6}}),
]}
MOTIONS["M_P_2"] = {"end": 0, "keys": [(0, {
    "locator2": {"ty": 0.6}, "locator3": {"rz": -6, "sy": 1.0625}, "pPlane14": {"rz": -8},
    "pPlane16": {"rz": 175}, "pPlane3": {"rz": 10}, "pPlane15": {"rz": 30}, "pPlane12": {"vis": 1},
    "locator4": {"ty": 1.0, "rz": 20}, "locator5": {"ty": 0.4, "rz": -10}})]}
MOTIONS["M_P_1@b"] = MOTIONS["M_P_1"]
MOTIONS["M_P_2@b"] = MOTIONS["M_P_2"]

# the rest of battle's hammer names: a throw that lets go at frame 4 (M_H_3,
# M_H_7 at the Super level), the hammer held straight up (M_H_4, M_H_8), a
# plain stand with it put away (M_H_2)
_H3 = [(0, -108, 0), (1, -112, 0), (2, -81, 0), (3, -24, 2), (4, None, 2), (5, None, 3), (8, None, 3), (10, None, 3)]
MOTIONS["M_H_3@b"] = _hammer_swing(10, _H3)
MOTIONS["M_H_7@b"] = _hammer_swing(10, _H3, add=4)
MOTIONS["M_H_4@b"] = _hammer_swing(0, [(0, 0, 3)])
MOTIONS["M_H_8@b"] = _hammer_swing(0, [(0, 0, 7)])
MOTIONS["M_H_2@b"] = MOTIONS["M_H_3A"]

# ------------------------------------------ batch 8: the rest of the field set


def _walk_tracks(end, step, arms=None, lift=1.4, bob=0.4):
    """legs stepping in turn every `step` frames over a loop of `end`, a bob, arms held"""
    n = int(round(end / step))
    t4 = [(i * step, lift if i % 2 == 1 else 0.0) for i in range(n + 1)]
    # (each foot lifts on its own beats, down on the other's)
    t5 =[(i * step, 0.0 if i % 2 == 1 or i in (0, n) else lift) for i in range(n + 1)]
    out = {"locator4": {"ty": t4}, "locator5": {"ty": t5},
           "locator2": {"ty": [(i * step, bob if i % 2 == 1 else 0.0) for i in range(n + 1)]}}
    for g, v in (arms or {}).items():
        out[g] = {"rz": [(0, v), (end, v)]}
    return out


# shading his eyes to look into the distance (M_Z_9, 2 frames, held)
MOTIONS["M_Z_9"] = {"end": 2, "keys": [(0, {}), (2, {
    "locator3": {"rz": 4}, "pPlane14": {"rz": -6}, "pPlane16": {"rz": 204}, "pPlane3": {"rz": 40},
    "locator4": {"tx": -0.8, "ty": 0.3}})]}

# low on HP (M_S_3, a 120-frame loop): a tired sway, lighter than M_I_S
_LOW = {"locator3": {"ty": -0.3, "rz": 6}, "pPlane14": {"rz": 7}, "pPlane16": {"rz": 4}, "pPlane15": {"rz": 6},
        "group1": {"sy": 0.6}}
MOTIONS["M_S_3"] = {"end": 120, "loop": True, "keys": [
    (0, _LOW), (30, dict(_LOW, locator3={"ty": -0.15, "rz": 3, "sy": 1.0})), (60, dict(_LOW, locator3={"ty": -0.3, "rz": 8})),
    (90, dict(_LOW, locator3={"ty": -0.15, "rz": 4, "sy": 1.0})), (120, _LOW)]}

# pushing (M_O_1 shoulder to it, M_O_2 both hands flat, 32-frame loops)
MOTIONS["M_O_1"] = {"end": 32, "loop": True,
                    "tracks": dict(_walk_tracks(32, 8, {"pPlane16": -60.0, "pPlane15": -70.0}, lift=1.0, bob=0.2),
                                   locator3={"rz": [(0, 16.0), (32, 16.0)]}, pPlane14={"rz": [(0, 6.0), (32, 6.0)]})}
MOTIONS["M_O_2"] = {"end": 32, "loop": True,
                    "tracks": dict(_walk_tracks(32, 8, {"pPlane16": -88.0, "pPlane15": -96.0}, lift=1.0, bob=0.2),
                                   locator3={"rz": [(0, 20.0), (32, 20.0)]}, pPlane14={"rz": [(0, 4.0), (32, 4.0)]})}
MOTIONS["M_O_2R"] = {"end": 32, "loop": True,
                     "tracks": dict(MOTIONS["M_O_2"]["tracks"], pPlane5={"sx": [(0, -1.0), (32, -1.0)]})}

# a hand to his head (M_B_1, 6 frames); hands clasped (M_B_2, M_B_3, held)
MOTIONS["M_B_1"] = {"end": 6, "keys": [(0, {}), (3, {"pPlane16": {"rz": 206}, "pPlane3": {"rz": 30},
                                                   "pPlane14": {"rz": 4}}),
                                     (6, {"pPlane16": {"rz": 196}, "pPlane3": {"rz": 30}, "pPlane14": {"rz": 3}})]}
_CLASP = {"pPlane16": {"rz": -42}, "pPlane15": {"rz": -54}, "pPlane14": {"rz": 3}}
MOTIONS["M_B_2"] = {"end": 2, "keys": [(0, _CLASP), (2, _CLASP)]}
MOTIONS["M_B_3"] = {"end": 2, "keys": [(0, dict(_CLASP, pPlane14={"rz": -4})), (2, dict(_CLASP, pPlane14={"rz": -4}))]}

# thinking it over, hand to his chin (M_X_1, a 64-frame loop)
MOTIONS["M_X_1"] = {"end": 64, "loop": True, "tracks": {
    "pPlane16": {"rz": [(0, 206.0), (64, 206.0)]}, "pPlane3": {"rz": [(0, 40.0), (64, 40.0)]},
    "pPlane15": {"rz": [(0, -20.0), (64, -20.0)]},
    "pPlane14": {"rz": [(0, -8.0), (16, -11.0), (32, -8.0), (48, -11.0), (64, -8.0)]},
    "group1": {"sy": [(0, 1.0, "step"), (40, 0.18, "step"), (42, 1.0, "step"), (64, 1.0, "step")]}}}

# a fist by his face (M_I_U, held)
MOTIONS["M_I_U"] = {"end": 0, "keys": [(0, {"pPlane16": {"rz": 200}, "pPlane3": {"rz": 30}, "pPlane14": {"rz": -3}})]}

# lifting Bobbery over his head (M_U_2, 10 frames; M_U_2R) and walking with
# him up there (M_W_7, a 40-frame loop)
# (arms overhead: Luigi's reach is short of his cap, so the raised arms
# stretch, lanky, the front one across his face as Mario's does, the
# gloves kept their size)
_UP = {"pPlane16": {"rz": 194, "sy": 1.875, "ty": 5.0}, "pPlane15": {"rz": 186, "sy": 1.875, "ty": 5.0},
       "pPlane3": {"rz": 10, "sy": 0.5625}, "pPlane2": {"rz": 10, "sy": 0.5625}}
_UP_TRACKS = lambda end: {
    "pPlane16": {"rz": [(0, 194.0), (end, 194.0)], "sy": [(0, 1.875), (end, 1.875)], "ty": [(0, 5.0), (end, 5.0)]},
    "pPlane15": {"rz": [(0, 186.0), (end, 186.0)], "sy": [(0, 1.875), (end, 1.875)], "ty": [(0, 5.0), (end, 5.0)]},
    "pPlane3": {"sy": [(0, 0.5625), (end, 0.5625)]}, "pPlane2": {"sy": [(0, 0.5625), (end, 0.5625)]}}
MOTIONS["M_U_2"] = {"end": 10, "keys": [
    (0, {"locator3": {"sy": 0.9375, "ty": -0.4}, "pPlane16": {"rz": -40}, "pPlane15": {"rz": -50}}),
    (4, {"locator3": {"sy": 1.0625}, "pPlane16": {"rz": 120}, "pPlane15": {"rz": 130}}),
    (7, dict(_UP, locator2={"ty": 0.2}, locator3={"sy": 1.0625})), (10, _UP)]}
MOTIONS["M_U_2R"] = rear(MOTIONS["M_U_2"])
MOTIONS["M_W_7"] = {"end": 40, "loop": True, "tracks": dict(_walk_tracks(40, 10), **_UP_TRACKS(40))}

# a fist to the crowd (M_C_2, 36 frames: as in battle)
MOTIONS["M_C_2"] = MOTIONS["M_C_2C@b"]

# riding along with Flurrie (party_cloud.c M_A_2A 12 frames, M_A_2B a
# 4-frame loop, M_A_2C 8 frames): hands clasped, a little lift as she carries him
MOTIONS["M_A_2A"] = {"end": 12, "keys": [(0, {}), (12, dict(_CLASP, locator3={"sy": 1.0625}))]}
MOTIONS["M_A_2B"] = {"end": 4, "loop": True, "keys": [
    (0, dict(_CLASP, locator3={"sy": 1.0625})), (2, dict(_CLASP, locator3={"sy": 1.0})),
    (4, dict(_CLASP, locator3={"sy": 1.0625}))]}
MOTIONS["M_A_2C"] = {"end": 8, "keys": [(0, dict(_CLASP, locator3={"sy": 1.0625})), (8, {})]}

# arms up (mot_jabara.c): a jump with a fist up (M_J_5A), a jump with both
# arms up (M_J_5B, 14 frames), standing (M_S_4) and walking (M_W_6, a 32-frame loop)
MOTIONS["M_J_5A"] = {"end": 2, "keys": [(0, MOTIONS["M_P_2"]["keys"][0][1]), (2, MOTIONS["M_P_2"]["keys"][0][1])]}
MOTIONS["M_J_5B"] = {"end": 14, "keys": [
    (0, dict(_UP, locator3={"sy": 0.9375})),
    (5, dict(_UP, locator2={"ty": 1.8}, locator3={"sy": 1.0625}, locator4={"ty": 0.8, "rz": 15},
             pPlane12={"vis": 1})),
    (10, dict(_UP, locator2={"ty": 0.6}, pPlane12={"vis": 1})),
    (14, _UP)]}
MOTIONS["M_S_4"] = {"end": 2, "keys": [(0, _UP), (2, _UP)]}
MOTIONS["M_W_6"] = {"end": 32, "loop": True, "tracks": dict(_walk_tracks(32, 8), **_UP_TRACKS(32))}

# e_mario's poses no script uses, so that none of Mario's falls back: a
# march (M_W_4A, a 40-frame loop) and its end (M_W_4C), standing (M_I_7,
# M_I_M), pointing ahead (M_L_1), eating (M_L_4), mid-stride (M_F_2),
# walking with something held in front (M_W_5, a 40-frame loop)
MOTIONS["M_W_4A"] = {"end": 40, "loop": True,
                     "tracks": dict(_walk_tracks(40, 10), pPlane16={"rz": [(0, -30.0), (10, 30.0), (20, -30.0),
                                                                           (30, 30.0), (40, -30.0)]},
                                    pPlane15={"rz": [(0, 30.0), (10, -30.0), (20, 30.0), (30, -30.0), (40, 30.0)]})}
MOTIONS["M_W_4C"] = {"end": 6, "keys": [(0, _STRIDE), (6, {})]}
MOTIONS["M_I_7"] = {"end": 0, "keys": [(0, {})]}
MOTIONS["M_I_M"] = {"end": 0, "keys": [(0, {"pPlane14": {"rz": -3}})]}
MOTIONS["M_L_1"] = {"end": 0, "keys": [(0, {"locator3": {"rz": 4}, "pPlane16": {"rz": -95}, "pPlane3": {"rz": -10},
                                            "pPlane14": {"rz": -2}})]}
MOTIONS["M_L_4"] = {"end": 0, "keys": [(0, _EAT)]}
MOTIONS["M_F_2"] = {"end": 2, "keys": [(0, _STRIDE), (2, _STRIDE)]}
MOTIONS["M_W_5"] = {"end": 40, "loop": True,
                    "tracks": dict(_walk_tracks(40, 10), pPlane16={"rz": [(0, -70.0), (40, -70.0)]},
                                   pPlane15={"rz": [(0, -80.0), (40, -80.0)]})}
