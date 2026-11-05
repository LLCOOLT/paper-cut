"""Blender side of the model tools (runs inside Blender; ttyd_anim.py does the
file format). See docs/LUIGI_ANIMS.md.

Two ways to bring a model in:

- build_model(..., editable=True): a group tree of objects (an empty or a
  plane each) whose location / rotation / scale are the game's node values,
  and one Action per animation (a slot per object) keyed so Blender's curves
  are the game's: Bezier keys with free handles at a third of each segment
  are the game's cubic Hermite exactly. Needs plain transform nodes whose
  rotation and scale pivots are one fixed point (Luigi's model is).
  read_action() turns such an Action back into a ttyd_anim.Anim.
- build_baked(...): any model (Mario's joints, skinning and vertex animation)
  as one mesh whose vertices follow the game's evaluation frame by frame
  (a Mesh Cache modifier on a .pc2 file per animation), for reference only.

Units are the game's (Y up in the game; the model root turns it to Blender's
Z up), at 60 frames a second, frame 0 the animation's start.
"""
import json
import math
import os
import struct

import bpy
from mathutils import Matrix

import ttyd_anim as T

FPS = 60
DEG = math.pi / 180.0

# node field -> (Blender property, index, to Blender, slope factor)
LOC = {0: 0, 1: 1, 2: 2}
ROT = {6: 0, 7: 1, 8: 2}
SCL = {3: 0, 4: 1, 5: 2}


def field_channel(f):
    if f in LOC:
        return "location", LOC[f], 1.0
    if f in ROT:
        return "rotation_euler", ROT[f], 2.0 * DEG
    if f in SCL:
        return "scale", SCL[f], 1.0
    return None


def to_blender(f, v, pivot):
    path, i, k = field_channel(f)
    return v * k + (pivot[i] if path == "location" else 0.0)


def from_blender(f, v, pivot):
    path, i, k = field_channel(f)
    return (v - (pivot[i] if path == "location" else 0.0)) / k


# --------------------------------------------------------------- textures ----

def load_textures(tpl_path, cache_dir, prefix):
    """bpy images for every texture of a TPL file (decoded once into cache_dir)"""
    os.makedirs(cache_dir, exist_ok=True)
    data = open(tpl_path, "rb").read()
    n = struct.unpack_from(">I", data, 4)[0]
    images = []
    pending = [i for i in range(n) if not os.path.exists(os.path.join(cache_dir, "%03d.png" % i))]
    if pending:
        decoded = T.tpl_images(data)
        for i in pending:
            T.write_png(os.path.join(cache_dir, "%03d.png" % i), decoded[i][2])
    for i in range(n):
        path = os.path.join(cache_dir, "%03d.png" % i)
        name = "%s:%03d" % (prefix, i)
        img = bpy.data.images.get(name)
        if img is None:
            img = bpy.data.images.load(path)
            img.name = name
        images.append(img)
    return images


def slot_binds(pose):
    """for each texture slot (texData entry) the texture binds it shows in any animation"""
    geo = T.Geometry(pose)
    ev = T.Evaluator(pose)
    out = [set() for _ in geo.texdata]
    for slot, (bind, _wrap) in enumerate(geo.texdata):
        out[slot].add(bind + ev.base_tex[slot][0])
    for a in pose.anims:
        for k in range(len(a.keys)):
            tex = ev.at_key(a, k)[3]
            for slot, (bind, _wrap) in enumerate(geo.texdata):
                out[slot].add(bind + tex[slot][0])
    return [sorted(s) for s in out]


def make_material(name, root, slot, binds, base_bind, geo, images):
    """a material for texture slot `slot`: an unlit, alpha-tested image times
    the vertex colour, its image picked by the model root's ["tex<slot>"] (the
    slot's animated bind offset, as the game's texture animation sets it)"""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (900, 0)
    vc = nt.nodes.new("ShaderNodeVertexColor")
    vc.layer_name = "Col"
    vc.location = (0, -300)
    uv = nt.nodes.new("ShaderNodeUVMap")
    uv.location = (-600, 0)
    color = alpha = None
    for j, b in enumerate(binds):
        tex_id = geo.texbinds[b][0] if 0 <= b < len(geo.texbinds) else 0
        im = nt.nodes.new("ShaderNodeTexImage")
        im.image = images[tex_id] if tex_id < len(images) else None
        im.interpolation = "Linear"
        im.extension = "EXTEND"
        im.location = (-400, -260 * j)
        nt.links.new(uv.outputs["UV"], im.inputs["Vector"])
        if color is None:
            color, alpha = im.outputs["Color"], im.outputs["Alpha"]
            first = im
            continue
        # picks this image while root["tex<slot>"] == b - base
        pick = nt.nodes.new("ShaderNodeValue")
        pick.location = (-200, -260 * j + 120)
        fc = pick.outputs[0].driver_add("default_value")
        fc.driver.type = "SCRIPTED"
        v = fc.driver.variables.new()
        v.name = "t"
        v.targets[0].id_type = "OBJECT"
        v.targets[0].id = root
        v.targets[0].data_path = '["tex%d"]' % slot
        fc.driver.expression = "1.0 if t == %d else 0.0" % (b - base_bind)
        mc = nt.nodes.new("ShaderNodeMix")
        mc.data_type = "RGBA"
        mc.location = (0, -260 * j)
        nt.links.new(pick.outputs[0], mc.inputs["Factor"])
        nt.links.new(color, mc.inputs[6])
        nt.links.new(im.outputs["Color"], mc.inputs[7])
        ma = nt.nodes.new("ShaderNodeMix")
        ma.data_type = "FLOAT"
        ma.location = (0, -260 * j - 120)
        nt.links.new(pick.outputs[0], ma.inputs["Factor"])
        nt.links.new(alpha, ma.inputs[2])
        nt.links.new(im.outputs["Alpha"], ma.inputs[3])
        color, alpha = mc.outputs[2], ma.outputs[0]
    if color is None:
        return mat
    nt.nodes.active = first
    mul = nt.nodes.new("ShaderNodeMix")
    mul.data_type = "RGBA"
    mul.blend_type = "MULTIPLY"
    mul.location = (300, 0)
    mul.inputs["Factor"].default_value = 1.0
    nt.links.new(color, mul.inputs[6])
    nt.links.new(vc.outputs["Color"], mul.inputs[7])
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.location = (500, 0)
    nt.links.new(mul.outputs[2], emit.inputs["Color"])
    # the game alpha-tests the paper at half
    cut = nt.nodes.new("ShaderNodeMath")
    cut.operation = "GREATER_THAN"
    cut.inputs[1].default_value = 0.5
    cut.location = (500, -200)
    nt.links.new(alpha, cut.inputs[0])
    clear = nt.nodes.new("ShaderNodeBsdfTransparent")
    clear.location = (500, 150)
    mix = nt.nodes.new("ShaderNodeMixShader")
    mix.location = (700, 0)
    nt.links.new(cut.outputs[0], mix.inputs[0])
    nt.links.new(clear.outputs[0], mix.inputs[1])
    nt.links.new(emit.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    try:
        mat.surface_render_method = "DITHERED"
    except AttributeError:
        pass
    mat.use_backface_culling = False
    return mat


def _shape_mesh(name, geo, si, offset, materials, positions=None):
    """a mesh of shape si's fans; vertices moved by -offset (the object's pivot)"""
    sh = geo.shapes[si]
    first, count = sh["pos"]
    pos = positions if positions is not None else geo.pos
    verts = [tuple(pos[first + j][c] - offset[c] for c in range(3)) for j in range(count)]
    faces, fuv, fcol, fmat, mats = [], [], [], [], []
    for di, fan in geo.shape_polys(si):
        m = materials(di)
        if m not in mats:
            mats.append(m)
        faces.append([c[0] - first for c in fan])
        fuv.append([c[2] for c in fan])
        fcol.append([c[1] for c in fan])
        fmat.append(mats.index(m))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    for m in mats:
        me.materials.append(m)
    uvl = me.uv_layers.new(name="UVMap")
    col = me.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
    li = 0
    for fi, poly in enumerate(me.polygons):
        poly.material_index = fmat[fi]
        for j, loop in enumerate(poly.loop_indices):
            uv = fuv[fi][j]
            if uv is not None:
                uvl.data[loop].uv = (uv[0], 1.0 - uv[1])
            r, g, b, a = fcol[fi][j]
            col.data[loop].color = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)
    me.update()
    return me


def _materials_for(pose, prefix, root, images):
    geo = T.Geometry(pose)
    binds = slot_binds(pose)
    ev = T.Evaluator(pose)
    cache = {}

    def get(di):
        dr = geo.draws[di]
        slot = dr["tex_data"][0] if dr["tex_count"] else -1
        if slot not in cache:
            if slot < 0 or slot >= len(geo.texdata):
                cache[slot] = None
            else:
                base = geo.texdata[slot][0] + ev.base_tex[slot][0]
                cache[slot] = make_material("%s:tex%d" % (prefix, slot), root, slot, binds[slot], base, geo, images)
                root["tex%d" % slot] = 0
        return cache[slot]
    return geo, get


# ---------------------------------------------------------- editable model ----

def editable_problems(pose):
    """why a model can't be the editable object tree (empty when it can)"""
    probs = []
    node = pose.node_values()
    for g in pose.groups():
        n = node[g.node:g.node + T.NODE_SIZE]
        if g.ssc:
            probs.append("%s is a joint (segment scale compensation)" % g.name)
        if any(n[9:12]):
            probs.append("%s has a joint orient" % g.name)
        if n[12:15] != n[15:18]:
            probs.append("%s: rotation and scale pivots differ" % g.name)
        if any(n[18:24]):
            probs.append("%s: pivot translations" % g.name)
    for a in pose.anims:
        for k in a.keys:
            for i in k.node:
                if field_channel(i % T.NODE_SIZE) is None:
                    probs.append("%s animates %s" % (a.name, T.NODE_FIELDS[i % T.NODE_SIZE]))
                    break
    return sorted(set(probs))


def build_model(pose_path, tpl_path, cache_dir, prefix=None, collection=None, location=(0, 0, 0),
                actions=True):
    """the model as an editable object tree, its animations as Actions;
    returns the root object"""
    data = open(pose_path, "rb").read()
    pose = T.PoseFile.parse(data)
    prefix = prefix or pose.file_name
    probs = editable_problems(pose)
    if probs:
        raise ValueError("%s can't be edited as objects: %s" % (prefix, "; ".join(probs[:5])))
    coll = bpy.data.collections.new(collection or prefix)
    bpy.context.scene.collection.children.link(coll)
    root = bpy.data.objects.new(prefix, None)
    root.empty_display_type = "ARROWS"
    root.empty_display_size = 4
    root.rotation_euler = (math.pi / 2, 0, 0)
    root.location = location
    root["ttyd_model"] = os.path.abspath(pose_path)
    root["ttyd_prefix"] = prefix
    coll.objects.link(root)
    images = load_textures(tpl_path, cache_dir, prefix)
    geo, mat_for = _materials_for(pose, prefix, root, images)
    groups = pose.groups()
    node = pose.node_values()
    vis = pose.vis_values()
    objs = [None] * len(groups)

    def make(gi, parent_obj, parent_pivot):
        g = groups[gi]
        n = node[g.node:g.node + T.NODE_SIZE]
        pivot = n[12:15]
        name = "%s:%s" % (prefix, g.name.lstrip("|"))
        if g.shape >= 0:
            me = _shape_mesh(name, geo, g.shape, pivot, mat_for)
            ob = bpy.data.objects.new(name, me)
        else:
            ob = bpy.data.objects.new(name, None)
            ob.empty_display_type = "PLAIN_AXES"
            ob.empty_display_size = 0.6
        coll.objects.link(ob)
        ob.parent = parent_obj
        ob.matrix_parent_inverse = Matrix.Translation([-c for c in parent_pivot])
        ob.rotation_mode = "XYZ"
        for f in range(9):
            path, i, _k = field_channel(f)
            getattr(ob, path)[i] = to_blender(f, n[f], pivot)
        ob["ttyd_group"] = gi
        ob["ttyd_node"] = list(n)
        ob["ttyd_vis_index"] = g.vis
        ob["vis"] = vis[g.vis]
        objs[gi] = ob
        return ob, pivot

    def walk(gi, parent_obj, parent_pivot):
        while gi != -1:
            ob, pivot = make(gi, parent_obj, parent_pivot)
            walk(groups[gi].child, ob, pivot)
            gi = groups[gi].brother
    walk(len(groups) - 1, root, (0.0, 0.0, 0.0))
    _vis_drivers(objs, groups)
    root["ttyd_base_vis"] = vis
    if actions:
        for a in pose.anims:
            if a.keys:  # (an entry with no data of its own, in a test file, has none)
                make_action(root, pose, a)
        first = next((a for a in pose.anims if a.keys), None)
        if first is not None:
            set_anim(root, first.name)
    return root


def _vis_drivers(objs, groups):
    """a group the game hides hides its subtree: hide = not (own and every ancestor's ["vis"])"""
    for ob in objs:
        chain, p = [], ob
        while p is not None and "ttyd_group" in p.keys():
            chain.append(p)
            p = p.parent
        for prop in ("hide_viewport", "hide_render"):
            ob.driver_remove(prop)
            fc = ob.driver_add(prop)
            fc.driver.type = "SCRIPTED"
            names = []
            for j, o in enumerate(chain):
                v = fc.driver.variables.new()
                v.name = "v%d" % j
                v.targets[0].id_type = "OBJECT"
                v.targets[0].id = o
                v.targets[0].data_path = '["vis"]'
                names.append(v.name)
            fc.driver.expression = "not (%s)" % " and ".join(names)


def model_objects(root):
    """{group index: object} of a model root"""
    out = {}
    stack = list(root.children)
    while stack:
        o = stack.pop()
        if "ttyd_group" in o.keys():
            out[int(o["ttyd_group"])] = o
        stack.extend(o.children)
    return out


def _fcurve(bag, path, index, keys, group=None):
    """keys: [(frame, value, left slope, right slope, interpolation)] -> an F-curve
    with free handles a third of the way to each neighbour"""
    fc = bag.fcurves.new(data_path=path, index=index)
    fc.keyframe_points.add(len(keys))
    for j, (t, v, sl, sr, interp) in enumerate(keys):
        kp = fc.keyframe_points[j]
        kp.co = (t, v)
        kp.interpolation = interp
        kp.handle_left_type = kp.handle_right_type = "FREE"
        dl = (t - keys[j - 1][0]) / 3.0 if j > 0 else 1.0
        dr = (keys[j + 1][0] - t) / 3.0 if j + 1 < len(keys) else 1.0
        kp.handle_left = (t - dl, v - sl * dl)
        kp.handle_right = (t + dr, v + sr * dr)
    fc.extrapolation = "CONSTANT"
    return fc


def make_action(root, pose, anim):
    """the Action for one animation: a slot per object of the model (and one
    for the root's texture slots), the game's curves as F-curves"""
    prefix = root["ttyd_prefix"]
    name = "%s:%s" % (prefix, anim.name)
    old = bpy.data.actions.get(name)
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    strip = act.layers.new("Layer").strips.new(type="KEYFRAME")
    objs = model_objects(root)
    groups = pose.groups()
    base = pose.node_values()
    times = [k.time for k in anim.keys]
    raw = {"node": {}, "key0": {}, "tex": {}, "vis": {}, "pos": {}, "nrm": {}}

    # node values, per float
    per_float = {}
    for ki, key in enumerate(anim.keys):
        for i, nk in key.node.items():
            per_float.setdefault(i, {})[ki] = nk
            ch = nk.chunks()
            if nk.raw is not None or len(ch) > 1:
                # kept so an untouched delta writes back as the disc has it (the
                # writer rounds angles, the game's converter truncated them)
                eff = nk.effective() or (0.0, 0.0)
                raw["node"]["%d@%d" % (i, ki)] = {"n": nk.n, "sa": eff[0], "sb": eff[1], "step": nk.step,
                                                  "chunks": [list(c) for c in ch]}
    bags = {}

    def bag_for(ob):
        if ob.name not in bags:
            slot = act.slots.new("OBJECT", ob.name)
            bags[ob.name] = (slot, strip.channelbag(slot, ensure=True))
        return bags[ob.name][1]

    for gi, g in enumerate(groups):
        ob = objs[gi]
        pivot = base[g.node + 12:g.node + 15]
        for f in range(9):
            i = g.node + f
            entries = per_float.get(i)
            if not entries:
                continue
            path, index, k = field_channel(f)
            q = 0  # sixteenths from the base value
            vals = []
            for ki, key in enumerate(anim.keys):
                if ki in entries:
                    q += entries[ki].n
                vals.append(base[i] + q / 16.0)
            if 0 in entries:
                e = entries[0]
                raw["key0"][str(i)] = {"sa": e.sa, "sb": e.sb, "step": e.step}
            # Blender keys: both ends of every segment that has an entry
            pts = {}  # key index -> [left slope, right slope, interp]
            for ki in range(1, len(anim.keys)):
                if ki not in entries:
                    continue
                e = entries[ki]
                pts.setdefault(ki - 1, [0.0, 0.0, "BEZIER"])
                pts.setdefault(ki, [0.0, 0.0, "BEZIER"])
                if e.step:
                    pts[ki - 1][2] = "CONSTANT"
                else:
                    sa, sb = e.effective()
                    pts[ki - 1][1] = sa
                    pts[ki][0] = sb
            if not pts:
                pts[0] = [0.0, 0.0, "BEZIER"]
            keys = [(times[ki], to_blender(f, vals[ki], pivot), p[0] * k, p[1] * k, p[2])
                    for ki, p in sorted(pts.items())]
            _fcurve(bag_for(ob), path, index, keys)
    # visibility, per object: its value at every key where it changes
    ev = T.Evaluator(pose)
    vis_track = [ev.at_key(anim, ki)[1] for ki in range(len(anim.keys))]
    tex_track = [ev.at_key(anim, ki)[3] for ki in range(len(anim.keys))]
    base_vis = pose.vis_values()
    for gi, g in enumerate(groups):
        vals = [v[g.vis] for v in vis_track]
        if all(v == base_vis[g.vis] for v in vals):
            continue
        keys, last = [], None
        for ki, v in enumerate(vals):
            if v != last:
                keys.append((times[ki], float(v), 0.0, 0.0, "CONSTANT"))
                last = v
        _fcurve(bag_for(objs[gi]), '["vis"]', 0, keys)
    base_tex = ev.base_tex
    for slot in range(len(base_tex)):
        for c, prop in ((0, "tex%d"), (1, "texS%d"), (2, "texT%d")):
            vals = [t[slot][c] for t in tex_track]
            if all(v == base_tex[slot][c] for v in vals):
                continue
            keys, last = [], None
            for ki, v in enumerate(vals):
                vv = (v - base_tex[slot][0]) if c == 0 else v
                if vv != last:
                    keys.append((times[ki], float(vv), 0.0, 0.0, "CONSTANT"))
                    last = vv
            if (prop % slot) not in root.keys():
                root[prop % slot] = 0 if c == 0 else 0.0
            _fcurve(bag_for(root), '["%s"]' % (prop % slot), 0, keys)
    for ki, key in enumerate(anim.keys):
        raw["tex"][str(ki)] = [list(t) for t in key.tex]
        if key.raw_vis is not None:
            raw["vis"][str(ki)] = [list(t) for t in key.raw_vis]
        for which in ("pos", "nrm"):
            d = getattr(key, which)
            if d:
                raw[which][str(ki)] = {
                    "delta": {str(i): list(v) for i, v in d.items()},
                    "raw": None if getattr(key, "raw_" + which) is None else
                    [[i, [list(c) for c in ch]] for i, ch in getattr(key, "raw_" + which)]}
    act.use_frame_range = True
    act.frame_start, act.frame_end = anim.start, max(anim.end, anim.start + 1)
    act["ttyd_loop"] = int(anim.loop_info[0][0])
    act["ttyd_loop_info"] = json.dumps(anim.loop_info)
    act["ttyd_end"] = anim.end
    act["ttyd_range_end"] = act.frame_end
    act["ttyd_keys"] = times
    act["ttyd_bbox"] = list(anim.bbox)
    act["ttyd_value_keys"] = json.dumps(anim.value_keys)
    act["ttyd_raw"] = json.dumps(raw)
    act["ttyd_table_tail"] = getattr(anim, "table_tail", b"").hex()
    act["ttyd_empty_ptrs"] = json.dumps(getattr(anim, "empty_ptrs", {}))
    act["ttyd_vis_pad"] = getattr(anim, "vis_pad", b"").hex()
    act["ttyd_header_size"] = getattr(anim, "header_size", T.ANIM_HEADER_SIZE)
    act["ttyd_model"] = prefix
    act["ttyd_anim"] = anim.name
    return act


def set_anim(root, anim_name):
    """shows one animation: every object back to its base values, then the
    animation's Action and slot on each object that has one"""
    prefix = root["ttyd_prefix"]
    act = bpy.data.actions.get("%s:%s" % (prefix, anim_name))
    objs = model_objects(root)
    for gi, ob in objs.items():
        n = list(ob["ttyd_node"])
        pivot = n[12:15]
        for f in range(9):
            path, i, _k = field_channel(f)
            getattr(ob, path)[i] = to_blender(f, n[f], pivot)
        ob["vis"] = list(root["ttyd_base_vis"])[int(ob["ttyd_vis_index"])]
        _assign(ob, act)
    for k in list(root.keys()):
        if k.startswith("tex"):
            root[k] = 0 if k[3:].isdigit() else 0.0
    _assign(root, act)
    if act is not None:
        sc = bpy.context.scene
        sc.render.fps = FPS
        sc.frame_start = int(act.frame_start)
        sc.frame_end = int(act.frame_end)
        root["ttyd_current"] = anim_name
    return act


def _assign(ob, act):
    slot = None
    if act is not None:
        for s in act.slots:
            if s.name_display == ob.name:
                slot = s
    if slot is None:
        if ob.animation_data is not None:
            ob.animation_data.action = None
        return
    ad = ob.animation_data_create()
    ad.action = act
    ad.action_slot = slot


# ------------------------------------------------------- Action -> Anim ----

def _channel_fc(act, ob, path, index):
    for s in act.slots:
        if s.name_display != ob.name:
            continue
        for layer in act.layers:
            for strip in layer.strips:
                bag = strip.channelbag(s)
                if bag is None:
                    continue
                fc = bag.fcurves.find(path, index=index)
                if fc is not None:
                    return fc
    return None


def _interp_left(fc, t):
    """the interpolation of the F-curve key at or before t"""
    best = None
    for kp in fc.keyframe_points:
        if kp.co[0] <= t + 1e-6:
            best = kp
    return best.interpolation if best is not None else "BEZIER"


def _slope(fc, t, side, h=1e-3):
    """the F-curve's slope just to the right (side=+1) or left (-1) of t"""
    for j, kp in enumerate(fc.keyframe_points):
        if abs(kp.co[0] - t) < 1e-6:
            if side > 0:
                if kp.interpolation == "CONSTANT":
                    return 0.0
                if kp.interpolation == "LINEAR" and j + 1 < len(fc.keyframe_points):
                    nx = fc.keyframe_points[j + 1].co
                    return (nx[1] - kp.co[1]) / (nx[0] - kp.co[0])
                dx = kp.handle_right[0] - kp.co[0]
                if kp.interpolation == "BEZIER" and dx > 1e-9:
                    return (kp.handle_right[1] - kp.co[1]) / dx
            else:
                if j > 0:
                    pv = fc.keyframe_points[j - 1]
                    if pv.interpolation == "CONSTANT":
                        return 0.0
                    if pv.interpolation == "LINEAR":
                        return (kp.co[1] - pv.co[1]) / (kp.co[0] - pv.co[0])
                dx = kp.co[0] - kp.handle_left[0]
                if dx > 1e-9:
                    return (kp.co[1] - kp.handle_left[1]) / dx
            break
    return (fc.evaluate(t + side * h) - fc.evaluate(t)) / (side * h) if side > 0 else \
        (fc.evaluate(t) - fc.evaluate(t - h)) / h


def read_action(root, act, pose=None, tolerance=None, max_passes=4):
    """an Action of this model back to a ttyd_anim.Anim. Key times: the
    original's (when it came from a file) and every F-curve key; segments the
    game's Hermite can't follow within `tolerance` get a key in the middle.
    Returns (anim, report)."""
    if pose is None:
        pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    tol = tolerance or {"location": 0.03, "rotation_euler": 0.25 * DEG, "scale": 0.004}
    objs = model_objects(root)
    groups = pose.groups()
    base = pose.node_values()
    raw = json.loads(act.get("ttyd_raw", "{}") or "{}")
    for k in ("node", "key0", "tex", "vis", "pos", "nrm"):
        raw.setdefault(k, {})
    orig_times = list(act.get("ttyd_keys", []))
    times = set(float(t) for t in orig_times)
    fcs = []  # (group, field, fcurve)
    for gi, g in enumerate(groups):
        ob = objs[gi]
        for f in range(9):
            path, index, _k = field_channel(f)
            fc = _channel_fc(act, ob, path, index)
            if fc is not None:
                fcs.append((gi, f, fc))
                times.update(float(kp.co[0]) for kp in fc.keyframe_points)
        fc = _channel_fc(act, ob, '["vis"]', 0)
        if fc is not None:
            times.update(float(kp.co[0]) for kp in fc.keyframe_points)
    for kname in list(root.keys()):
        if kname.startswith("tex"):
            fc = _channel_fc(act, root, '["%s"]' % kname, 0)
            if fc is not None:
                times.update(float(kp.co[0]) for kp in fc.keyframe_points)
    start = float(act.frame_start)
    # the loop end: the frame range's end, unless it is still the one written
    # for a zero-length animation (Blender's range can't be empty)
    end = float(act.frame_end)
    if "ttyd_end" in act.keys() and abs(act.frame_end - act.get("ttyd_range_end", -1)) < 1e-6:
        end = float(act["ttyd_end"])
    times.add(start)
    times = sorted(t for t in times if start - 1e-6 <= t <= end + 1e-6)
    report = {"added_keys": [], "max_error": {}}
    for _pass in range(max_passes):
        anim, worst = _build_anim(act, root, pose, groups, base, objs, fcs, times, raw, orig_times, end, tol)
        report["max_error"] = worst["max"]
        if not worst["split"]:
            break
        new = sorted(set(worst["split"]) - set(times))
        if not new:
            break
        report["added_keys"].extend(new)
        times = sorted(set(times) | set(new))
    return anim, report


def _hermite(v0, v1, m0, m1, t):
    t2, t3 = t * t, t * t * t
    return (2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * m1


def _build_anim(act, root, pose, groups, base, objs, fcs, times, raw, orig_times, end, tol):
    anim = T.Anim(act.get("ttyd_anim", act.name.split(":")[-1]))
    anim.exact = False  # angles rounded to the nearest degree (see NodeKey.chunks)
    li = act.get("ttyd_loop_info")
    if li:
        anim.loop_info = [tuple(x) for x in json.loads(li)]
        anim.loop_info[0] = (int(act.get("ttyd_loop", 0)), float(act.frame_start), end)
    else:
        anim.loop_info = [(int(act.get("ttyd_loop", 0)), float(act.frame_start), end)]
    vk = act.get("ttyd_value_keys")
    anim.value_keys = [tuple(x) for x in json.loads(vk)] if vk else [(0.0, 0)]
    anim.keys = [T.Key(t) for t in times]
    same_times = [float(t) for t in orig_times] == list(times)
    worst = {"max": {}, "split": []}
    for gi, f, fc in fcs:
        g = groups[gi]
        i = g.node + f
        pivot = base[g.node + 12:g.node + 15]
        path, _index, k = field_channel(f)
        q = [round((from_blender(f, fc.evaluate(t), pivot) - base[i]) * 16) for t in times]
        prev = 0
        for ki, t in enumerate(times):
            n = q[ki] - prev
            prev = q[ki]
            if ki == 0:
                if n:
                    k0 = raw["key0"].get(str(i), {})
                    anim.keys[0].node[i] = T.NodeKey(n, k0.get("sa", 0.0), k0.get("sb", 0.0), k0.get("step", False))
                continue
            t0 = times[ki - 1]
            dt = t - t0
            step = _interp_left(fc, t0) == "CONSTANT"
            if n == 0 and step:
                continue
            sa = _slope(fc, t0, +1) / k
            sb = _slope(fc, t, -1) / k
            if step:
                nk = T.NodeKey(n, 0.0, 0.0, True)
            else:
                if n == 0 and abs(sa) < 1e-4 and abs(sb) < 1e-4:
                    continue
                nk = T.NodeKey(n, sa, sb, False)
            # the disc's chunks, when nothing about this delta changed
            r = raw["node"].get("%d@%d" % (i, ki)) if same_times else None
            if r is not None and r["n"] == n and r["step"] == nk.step and \
                    abs(r["sa"] - nk.sa) < 1e-3 * max(1, abs(r["sa"])) and \
                    abs(r["sb"] - nk.sb) < 1e-3 * max(1, abs(r["sb"])):
                nk.raw = [tuple(c) for c in r["chunks"]]
            elif r is None and same_times and not nk.step:
                # an untouched delta from the disc: the converter's slope
                # interval holds the F-curve's (summed) slope; encode exactly
                pass
            anim.keys[ki].node[i] = nk
            # how far the game's curve is from Blender's
            if step:
                continue
            ch = nk.chunks(exact=False)
            eff = (sum(T._tan(c[1]) for c in ch if -89 < c[2] < 89), sum(T._tan(c[2]) for c in ch if -89 < c[2] < 89))
            v0 = (q[ki - 1]) / 16.0
            v1 = q[ki] / 16.0
            err = 0.0
            for s in range(1, 8):
                u = s / 8.0
                game = _hermite(v0, v1, eff[0] * dt, eff[1] * dt, u)
                want = from_blender(f, fc.evaluate(t0 + u * dt), pivot) - base[i]
                err = max(err, abs(game - want) * k)
            key = path
            worst["max"][key] = max(worst["max"].get(key, 0.0), err)
            if err > tol[path] and dt >= 2:
                worst["split"].append(float(round(t0 + dt / 2)))
    # visibility
    base_vis = pose.vis_values()
    for gi, g in enumerate(groups):
        fc = _channel_fc(act, objs[gi], '["vis"]', 0)
        if fc is None:
            continue
        prev = base_vis[g.vis]
        for ki, t in enumerate(times):
            v = int(round(fc.evaluate(t)))
            if v != prev:
                anim.keys[ki].vis[g.vis] = anim.keys[ki].vis.get(g.vis, 0) + (v - prev)
                prev = v
    # texture slots
    ev = T.Evaluator(pose)
    nslots = len(ev.base_tex)
    tracks = {}
    for slot in range(nslots):
        for c, prop in ((0, "tex%d"), (1, "texS%d"), (2, "texT%d")):
            fc = _channel_fc(act, root, '["%s"]' % (prop % slot), 0)
            if fc is not None:
                tracks[(slot, c)] = fc
    prev = {s: [0, ev.base_tex[s][1], ev.base_tex[s][2]] for s in range(nslots)}
    for ki, t in enumerate(times):
        changes = []
        for slot in range(nslots):
            cur = list(prev[slot])
            for c in range(3):
                fc = tracks.get((slot, c))
                if fc is not None:
                    v = fc.evaluate(t)
                    cur[c] = int(round(v)) if c == 0 else v
            if cur != prev[slot]:
                changes.append((slot, (cur[0] - prev[slot][0]) & 0xFF, cur[1] - prev[slot][1], cur[2] - prev[slot][2]))
                prev[slot] = cur
        r = raw["tex"].get(str(ki)) if same_times else None
        if r is not None and _tex_same(r, changes):
            anim.keys[ki].tex = [tuple(x) for x in r]
        else:
            last = 0
            for slot, add, ds, dt_ in changes:
                anim.keys[ki].tex.append((slot - last, add, ds, dt_, 0))
                last = slot
    if same_times:
        for ki in range(len(times)):
            r = raw["vis"].get(str(ki))
            if r is not None:
                d = {}
                idx = 0
                for s, v in r:
                    idx += s
                    d[idx] = d.get(idx, 0) + v
                if {a: b for a, b in d.items() if b} == {a: b for a, b in anim.keys[ki].vis.items() if b}:
                    anim.keys[ki].raw_vis = [tuple(x) for x in r]
            for which in ("pos", "nrm"):
                r = raw[which].get(str(ki))
                if r is not None:
                    setattr(anim.keys[ki], which, {int(a): tuple(b) for a, b in r["delta"].items()})
                    if r["raw"] is not None:
                        setattr(anim.keys[ki], "raw_" + which,
                                [(i, [tuple(c) for c in ch]) for i, ch in r["raw"]])
    anim.bbox = tuple(act.get("ttyd_bbox", (0.0,) * 6))
    tail = act.get("ttyd_table_tail")
    if tail:
        anim.table_tail = bytes.fromhex(tail)
    ep = act.get("ttyd_empty_ptrs")
    if ep:
        anim.empty_ptrs = {int(a): b for a, b in json.loads(ep).items()}
    vp = act.get("ttyd_vis_pad")
    if vp:
        anim.vis_pad = bytes.fromhex(vp)
    if act.get("ttyd_header_size") == T.ANIM_HEADER_OLD:
        anim.header_size = T.ANIM_HEADER_OLD
    return anim, worst


def _tex_same(raw, changes):
    slot, out = 0, []
    agg = {}
    for s, add, ts, tt, *_ in raw:
        slot += s
        a = agg.setdefault(slot, [0, 0.0, 0.0])
        a[0] = (a[0] + add) & 0xFF
        a[1] += ts
        a[2] += tt
    want = {s: [add, ds, dt] for s, add, ds, dt in changes}
    if set(agg) != set(want):
        return False
    return all(agg[s][0] == want[s][0] and abs(agg[s][1] - want[s][1]) < 1e-6 and abs(agg[s][2] - want[s][2]) < 1e-6
               for s in agg)


# ------------------------------------------------------------- reviews ----

def contact_sheet(frames, out_path, cols=6, label=True):
    """the scene camera's render at each frame, side by side in one PNG"""
    import numpy as np
    sc = bpy.context.scene
    tmp = out_path + ".frame.png"
    tiles = []
    for f in frames:
        sc.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp, check_existing=False)
        w, h = img.size
        px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
        bpy.data.images.remove(img)
        if label:
            _stamp(px, "%g" % f)
        tiles.append(px)
    os.remove(tmp)
    h, w = tiles[0].shape[:2]
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.ones((rows * h, cols * w, 4), dtype=np.float32) * 0.15
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        y0 = (rows - 1 - r) * h  # Blender's pixels start at the bottom
        sheet[y0:y0 + h, c * w:(c + 1) * w] = t
    out = bpy.data.images.new("sheet", cols * w, rows * h, alpha=True)
    out.pixels[:] = sheet.ravel()
    out.filepath_raw = out_path
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)
    return out_path


def tile_images(paths, out_path, cols=6, crop=None, labels=None):
    """PNGs (game captures) side by side in one; crop: (x0, y0, x1, y1) from the top left"""
    import numpy as np
    tiles = []
    for i, p in enumerate(paths):
        img = bpy.data.images.load(p, check_existing=False)
        w, h = img.size
        px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
        bpy.data.images.remove(img)
        if crop:
            x0, y0, x1, y1 = crop
            px = px[h - y1:h - y0, x0:x1]
        px = px.copy()
        if labels:
            _stamp(px, labels[i])
        tiles.append(px)
    h, w = tiles[0].shape[:2]
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.zeros((rows * h, cols * w, 4), dtype=np.float32)
    sheet[..., 3] = 1
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        y0 = (rows - 1 - r) * h
        sheet[y0:y0 + h, c * w:(c + 1) * w] = t
    out = bpy.data.images.new("tiles", cols * w, rows * h, alpha=True)
    out.pixels[:] = sheet.ravel()
    out.filepath_raw = out_path
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)
    return out_path


_DIGITS = {  # 3x5 pixel digits for frame labels
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
    "8": "111101111101111", "9": "111101111001111", ".": "000000000000010", "-": "000000111000000",
}


def _stamp(px, text, scale=4):
    h = px.shape[0]
    x = 6
    for ch in text:
        bits = _DIGITS.get(ch)
        if bits is None:
            continue
        for j, b in enumerate(bits):
            if b == "1":
                r, c = divmod(j, 3)
                y = h - 6 - (r + 1) * scale
                px[y:y + scale, x + c * scale:x + (c + 1) * scale] = (1.0, 1.0, 0.0, 1.0)
        x += 4 * scale


# -------------------------------------------------------------- checks ----

def roundtrip_check(root, names=None, step=0.5, tol=1e-3):
    """For each animation of an editable model: Blender's pose against the
    game's (ttyd_anim.Evaluator) at every `step` frames, every group's matrix
    and visibility; then the Action read back and written, against the
    file's bytes. Returns {anim: {"frames", "max_matrix_error", "hidden_mismatch",
    "bytes_equal", "added_keys"}}."""
    pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    ev = T.Evaluator(pose)
    objs = model_objects(root)
    groups = pose.groups()
    base = pose.node_values()
    sc = bpy.context.scene
    r0_inv = root.matrix_world.inverted()
    out = {}
    for anim in pose.anims:
        if names and anim.name not in names:
            continue
        act = set_anim(root, anim.name)
        worst, hid, frames = 0.0, 0, 0
        f = anim.start
        while f <= anim.end + 1e-9:
            sc.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
            node, vis, _pos, _tex = ev.at_frame(anim, f)
            mats = ev.world_matrices(node, vis)
            for gi, g in enumerate(groups):
                ob = objs[gi]
                hidden = mats[gi] is None
                if hidden != bool(ob.hide_viewport):
                    hid += 1
                if hidden:
                    continue
                p = base[g.node + 12:g.node + 15]
                G = r0_inv @ ob.matrix_world @ Matrix.Translation([-c for c in p])
                m = mats[gi]
                for r in range(3):
                    for c in range(4):
                        worst = max(worst, abs(G[r][c] - m[r][c]))
            frames += 1
            f += step
        new, rep = read_action(root, act, pose)
        blob = new.build()
        orig = anim.build()
        out[anim.name] = {"frames": frames, "max_matrix_error": worst, "hidden_mismatch": hid,
                          "bytes_equal": blob == orig, "added_keys": rep["added_keys"]}
    return out


def fresh_check(root, names=None, step=0.5):
    """The writer as a new animation meets it: each Action copied without
    what was kept from the disc (key times, chunks), read back and written,
    then the game's pose of the result against the original's. Returns
    {anim: (max node-matrix error, visibility mismatches, keys before, after)}."""
    pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    ev = T.Evaluator(pose)
    prefix = root["ttyd_prefix"]
    out = {}
    for anim in pose.anims:
        if names and anim.name not in names:
            continue
        act = bpy.data.actions["%s:%s" % (prefix, anim.name)].copy()
        try:
            for k in ("ttyd_raw", "ttyd_keys", "ttyd_bbox"):
                if k in act.keys():
                    del act[k]
            new, rep = read_action(root, act, pose)
        finally:
            bpy.data.actions.remove(act)
        new = T.Anim.parse(new.name, new.build())  # through the bytes
        worst, vis_bad = 0.0, 0
        f = anim.start
        while f <= anim.end + 1e-9:
            a = ev.at_frame(anim, f)
            b = ev.at_frame(new, f)
            ma, mb = ev.world_matrices(a[0], a[1]), ev.world_matrices(b[0], b[1])
            for x, y in zip(ma, mb):
                if (x is None) != (y is None):
                    vis_bad += 1
                elif x is not None:
                    worst = max(worst, max(abs(x[r][c] - y[r][c]) for r in range(3) for c in range(4)))
            if a[3] != b[3]:
                vis_bad += 1
            f += step
        out[anim.name] = (worst, vis_bad, len(anim.keys), len(new.keys))
    return out


def file_roundtrip(root, out_path):
    """the whole model file written again from Blender's Actions"""
    pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    prefix = root["ttyd_prefix"]
    anims = []
    for a in pose.anims:
        if getattr(a, "model_offset", None) is not None:
            anims.append(a)  # no animation data of its own (a test file)
            continue
        act = bpy.data.actions["%s:%s" % (prefix, a.name)]
        new, _rep = read_action(root, act, pose)
        new.table_tail = getattr(a, "table_tail", None)
        new.disc_offset = getattr(a, "disc_offset", None)  # shared AnimData stays shared
        anims.append(new)
    pose.anims = anims
    data = pose.build()
    with open(out_path, "wb") as f:
        f.write(data)
    return data


# ---------------------------------------------------------- baked model ----

def write_pc2(path, frames):
    """frames: [[(x, y, z), ...], ...] -> a .pc2 point cache"""
    nv = len(frames[0])
    with open(path, "wb") as f:
        f.write(struct.pack("<12siiffi", b"POINTCACHE2\0", 1, nv, 0.0, 1.0, len(frames)))
        for fr in frames:
            for p in fr:
                f.write(struct.pack("<3f", *p))


def bake_frames(pose, anim, frames=None):
    """every vertex of the model in model space at each frame (hidden groups
    collapsed onto their parent's origin)"""
    ev = T.Evaluator(pose)
    geo = T.Geometry(pose)
    groups = pose.groups()
    owner = {}
    for gi, g in enumerate(groups):
        if g.shape >= 0:
            owner[g.shape] = gi
    if frames is None:
        frames = range(int(anim.start), int(math.ceil(anim.end)) + 1)
    out = []
    for fr in frames:
        node, vis, pos, _tex = ev.at_frame(anim, anim.local_frame(fr))
        mats = ev.world_matrices(node, vis)
        pts = [(0.0, 0.0, 0.0)] * len(pos)
        for si, sh in enumerate(geo.shapes):
            gi = owner.get(si)
            m = mats[gi] if gi is not None else None
            first, count = sh["pos"]
            for vi in range(first, first + count):
                if m is None:
                    pts[vi] = (0.0, -1000.0, 0.0)  # hidden: out of sight below the floor
                    continue
                x, y, z = pos[vi]
                pts[vi] = tuple(m[r][0] * x + m[r][1] * y + m[r][2] * z + m[r][3] for r in range(3))
        out.append(pts)
    return out


def build_baked(pose_path, tpl_path, cache_dir, pc2_dir, prefix=None, collection=None, location=(0, 0, 0)):
    """any model as one mesh following the game frame by frame; set_baked_anim picks the animation"""
    pose = T.PoseFile.parse(open(pose_path, "rb").read())
    prefix = prefix or pose.file_name
    coll = bpy.data.collections.new(collection or prefix)
    bpy.context.scene.collection.children.link(coll)
    root = bpy.data.objects.new(prefix, None)
    root.empty_display_type = "ARROWS"
    root.empty_display_size = 4
    root.rotation_euler = (math.pi / 2, 0, 0)
    root.location = location
    root["ttyd_model"] = os.path.abspath(pose_path)
    root["ttyd_prefix"] = prefix
    root["ttyd_pc2_dir"] = os.path.abspath(pc2_dir)
    coll.objects.link(root)
    images = load_textures(tpl_path, cache_dir, prefix)
    geo, mat_for = _materials_for(pose, prefix, root, images)
    verts = list(geo.pos)
    faces, fuv, fcol, fmat, mats = [], [], [], [], []
    for si in range(len(geo.shapes)):
        for di, fan in geo.shape_polys(si):
            m = mat_for(di)
            if m not in mats:
                mats.append(m)
            faces.append([c[0] for c in fan])
            fuv.append([c[2] for c in fan])
            fcol.append([c[1] for c in fan])
            fmat.append(mats.index(m))
    me = bpy.data.meshes.new(prefix + ":mesh")
    me.from_pydata(verts, [], faces)
    for m in mats:
        me.materials.append(m)
    uvl = me.uv_layers.new(name="UVMap")
    col = me.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
    for fi, poly in enumerate(me.polygons):
        poly.material_index = fmat[fi]
        for j, loop in enumerate(poly.loop_indices):
            uv = fuv[fi][j]
            if uv is not None:
                uvl.data[loop].uv = (uv[0], 1.0 - uv[1])
            r, g, b, a = fcol[fi][j]
            col.data[loop].color = (r / 255.0, g / 255.0, b / 255.0, a / 255.0)
    me.update()
    ob = bpy.data.objects.new(prefix + ":baked", me)
    coll.objects.link(ob)
    ob.parent = root
    mod = ob.modifiers.new("game", "MESH_CACHE")
    mod.cache_format = "PC2"
    mod.forward_axis = "POS_Y"
    mod.up_axis = "POS_Z"
    mod.frame_start = 0
    mod.frame_scale = 1.0
    os.makedirs(pc2_dir, exist_ok=True)
    for a in pose.anims:
        make_tex_action(root, pose, a)
    return root


def make_tex_action(root, pose, anim):
    """a baked model's Action: only the root's texture slots"""
    prefix = root["ttyd_prefix"]
    name = "%s:%s" % (prefix, anim.name)
    old = bpy.data.actions.get(name)
    if old is not None:
        bpy.data.actions.remove(old)
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    strip = act.layers.new("Layer").strips.new(type="KEYFRAME")
    slot = act.slots.new("OBJECT", root.name)
    bag = strip.channelbag(slot, ensure=True)
    ev = T.Evaluator(pose)
    times = [k.time for k in anim.keys]
    tex_track = [ev.at_key(anim, ki)[3] for ki in range(len(anim.keys))]
    for s in range(len(ev.base_tex)):
        vals = [t[s][0] - ev.base_tex[s][0] for t in tex_track]
        if all(v == 0 for v in vals):
            continue
        keys, last = [], None
        for ki, v in enumerate(vals):
            if v != last:
                keys.append((times[ki], float(v), 0.0, 0.0, "CONSTANT"))
                last = v
        _fcurve(bag, '["tex%d"]' % s, 0, keys)
    act.use_frame_range = True
    act.frame_start, act.frame_end = anim.start, max(anim.end, anim.start + 1)
    act["ttyd_anim"] = anim.name
    return act


def set_baked_anim(root, anim_name, location=None):
    """points the baked mesh at one animation's point cache (made on first use)"""
    pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    anim = pose.anim(anim_name)
    path = os.path.join(root["ttyd_pc2_dir"], "%s_%s.pc2" % (root["ttyd_prefix"], anim_name))
    if not os.path.exists(path):
        write_pc2(path, bake_frames(pose, anim))
    ob = [c for c in root.children if c.name.endswith(":baked")][0]
    mod = ob.modifiers["game"]
    mod.filepath = path
    mod.frame_start = anim.start
    for k in list(root.keys()):
        if k.startswith("tex") and k[3:].isdigit():
            root[k] = 0
    act = bpy.data.actions.get("%s:%s" % (root["ttyd_prefix"], anim_name))
    _assign(root, act)
    root["ttyd_current"] = anim_name
    return path
