#!/usr/bin/env python3
"""The Paper Mario 64 partners' per-part poses (the Lab pm64_integration, $TTYD_PM64_POSES): for each PM64 sprite
row a partner plays, keyframes of our own on the joints of its TTYD model (c_kurio .. c_resaresa), so the model
moves part by part as the sprite does, frame for frame, locked to the sprite row's clock.

The poses (v1) are port/assets/luigi/pm64_<name>_poses.json, written by hand. This tool checks them and turns them into C tables (port/src/common/pm64_<name>_poses.inc): a recipe
(anim_recipe.h) the game bakes onto the model at startup, one animation per row ("PF06": field row 6, "PB12":
battle row 12), on the model's still pose (Z_1, or "face"'s frame), and the rows' table (frames, loop, events,
the owner's sprite list).

  pm64_poses.py check [NAME ...] [--disc DIR] [--no-disc]
        every poses file (or NAME's): the format, every joint name against the model's rig on the disc, every row
        against the rows pm64_partner_pc.c plays, keyframes in range; then baked as the game bakes it and the model
        checked (ttyd_anim.check_model); the .inc up to date. Exit 1 on any error. Prints each partner's coverage.
  pm64_poses.py write [NAME ...] [--disc DIR]
        the .inc files (a stub for a partner with no poses file yet, so the game always builds)
  pm64_poses.py rows NAME
        every row the partner's sprite plays (field and battle), with the names that reach it
  pm64_poses.py rig NAME [--disc DIR]
        the model's joints in rig order (what "joints" must be), with each one's parent
  pm64_poses.py sheet NAME OUT.png [--rows F6,B12,...] [--at 0,4,8] [--disc DIR]
        the baked model's poses drawn front-on (char_sheet.py), a line a row, at those frames (default 8 even ones)
  pm64_poses.py diff RUNDIR [--iou 0.85] [--centre 1.0] [--extent 1.5]
        the gallery's exactness check ($TTYD_PM64_GALLERY with $TTYD_PM64_POSES): for every shot
        gal_<p>_<F|B><row>_f<frame>.png in RUNDIR (searched below it), the model's and the sprite's silhouettes
        (keyed off the gallery's magenta sheet, anchored on its two cyan marks) compared: mask IoU, centroid and
        extent deltas in model units; a CSV per partner (pm64poses_<name>.csv in RUNDIR) and a row summary; a row
        is "not exact" when any frame is under --iou or over --centre / --extent.

Rows (the anims' keys):
  A_IDLE .. A_MOVE        the field row of that class (pm64_partner_pc.c kAnims[p][0])
  B_IDLE .. B_VICTORY     the battle row of that class (kBattleRows[p])
  F<n> / B<n>             field / battle sprite row n (also field:<n>, battle:<n>); every move row, a ride
Two keys reaching the same row is an error: the sprite plays one row, so the model has one pose for it.

Keys: "f" 0..frames (whole frames: the sprite's clock is whole frames), "r" degrees added to the joint's own
rotation angles (TTYD's rx, ry, rz as its animations store them), "t" model units added to its place, "s"
multiplying its scale; any of r/t/s per key (a channel's keys are the keys that give it). "ease" leaves the key:
step (held to the next key: frame-exact sprite frames), linear, in (quadratic), out, inout. A loop wraps from its
last key to its first at "frames". "root" moves the whole model (the rig's top joint: turning about the feet).
"face": a model animation (and "@frame") whose pose the row starts from instead of the still pose (T_1@7: the
mouth open); its r/t/s are then on top of that.
Optional: "exact": false (the partner's own mark: not yet frame-exact), and at the top "sprite_rows": [rows] the
owner approved to stay sprites in play (the gallery still draws them posed).
"""
import argparse
import csv
import glob
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

PARTNER_PC = os.path.join(REPO, "port", "src", "common", "pm64_partner_pc.c")
MOVES = os.path.join(REPO, "port", "src", "common", "pm64_moves")

# PM64 partner number (port/include/port/pm64.h), the model, the moves file
PARTNERS = {
    "goombario": (1, "c_kurio"),
    "kooper": (2, "c_kameki"),
    "bombette": (3, "c_pinky"),
    "parakarry": (4, "c_paretta"),
    "watt": (5, "c_akarin"),
    "sushie": (6, "c_opuku"),
    "lakilester": (7, "c_pokopi"),
    "bow": (8, "c_resaresa"),
}
A_NAMES = ["A_IDLE", "A_WALK", "A_RUN", "A_JUMP", "A_FALL", "A_TALK", "A_HURT", "A_GLAD", "A_MOVE"]
B_NAMES = ["B_IDLE", "B_WALK", "B_RUN", "B_JUMP", "B_FALL", "B_TALK", "B_HURT", "B_GLAD", "B_MOVE", "B_LOW", "B_KO",
           "B_STILL", "B_SLEEP", "B_DIZZY", "B_GUARD", "B_APPEAL", "B_ITEM", "B_BURNHURT", "B_BURNT", "B_VICTORY"]
EASES = ("step", "linear", "in", "out", "inout")
# rows pm64_partner_pc.c plays by hand (battleAnim, wattSparks), not from its tables: (partner, battle, row, why)
EXTRA_ROWS = [
    (2, 1, 14, "battleAnim K_KOOPER_SHELL: into the shell"),
    (2, 1, 15, "battleAnim K_KOOPER_SHELL: out of the shell"),
    (2, 1, 16, "battleAnim K_KOOPER_SHELL"),
    (2, 1, 25, "battleAnim: flipped and hurt"),
    (3, 1, 14, "battleAnim: charging while the command runs"),
    (1, 1, 6, "battleAnim K_ASCEND: the bonk"),
    (5, 1, 16, "wattSparks: the sparks overlay while charged"),
]
CHANNELS = {"t": ("tx", "ty", "tz"), "r": ("rx", "ry", "rz"), "s": ("sx", "sy", "sz")}
KNOWN_ANIM = {"pm64", "frames", "loop", "keys", "face", "from_model", "events", "exact", "approved", "tex", "show",
              "hide", "vis", "tint", "parts", "sprite_parts", "recolor", "notes", "note", "_help"}
KNOWN_TOP = {"version", "partner", "model", "joints", "anims", "sprite_rows", "approved_rows", "texture_edits", "_help",
             "notes", "note"}
EXTRA_TEX, EXTRA_SHOW, EXTRA_HIDE, EXTRA_TINT = 0, 1, 2, 3
RULE_RECT, RULE_IN_ELLIPSE, RULE_OUT_ELLIPSE, RULE_LUMA, RULE_NEAR = 1, 2, 4, 8, 16
ACT_RGB, ACT_CLEAR, ACT_BASE, ACT_GRAY, ACT_PALETTE = 0, 1, 2, 3, 4
PART_FLIP, PART_OFF = 1, 2


def poses_path(name):
    return os.path.join(REPO, "port", "assets", "luigi", "pm64_%s_poses.json" % name)


def inc_path(name):
    return os.path.join(REPO, "port", "src", "common", "pm64_%s_poses.inc" % name)


def anim_name(battle, row):
    return "P%s%02d" % ("B" if battle else "F", row)


# ------------------------------------------------------------ the C side's rows ----

def _strip_c(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    return re.sub(r"//[^\n]*", " ", s)


def _c_ints(body):
    return [int(x) for x in re.findall(r"-?\d+", body)]


def c_rows():
    """{p: {"A": [9 field rows], "Afb": [9 battle-stage rows], "B": [20 battle rows], "played": {(battle, row):
    [why, ...]}}} read off pm64_partner_pc.c and pm64_moves/*.inc"""
    raw = open(PARTNER_PC, encoding="utf-8").read()
    src = _strip_c(raw)
    out = {p: {"played": {}} for p, _m in PARTNERS.values()}

    def played(p, battle, row, why):
        if row is None or row < 0:
            return
        out[p]["played"].setdefault((battle, row), []).append(why)
    m = re.search(r"kAnims\[PM64_PARTNER_COUNT\]\[2\]\[A_COUNT\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("pm64_poses: kAnims isn't in %s" % PARTNER_PC)
    groups = re.findall(r"\{\{([^{}]*)\},\s*\{([^{}]*)\}\}", m.group(1))
    # (the first is {{0}, {0}}: partner 0)
    for p, (f, b) in enumerate(groups):
        if p == 0:
            continue
        out[p]["A"] = _c_ints(f)
        out[p]["Afb"] = _c_ints(b)
        for i, r in enumerate(out[p]["A"]):
            played(p, 0, r, A_NAMES[i])
        for i, r in enumerate(out[p]["Afb"]):
            played(p, 1, r, A_NAMES[i] + " (battle stage)")
    m = re.search(r"kBattleRows\[PM64_PARTNER_COUNT\]\[B_COUNT\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("pm64_poses: kBattleRows isn't in %s" % PARTNER_PC)
    rows = re.findall(r"\{([^{}]*)\}", m.group(1))
    for p, body in enumerate(rows):
        if p == 0:
            continue
        out[p]["B"] = _c_ints(body)
        for i, r in enumerate(out[p]["B"]):
            played(p, 1, r, B_NAMES[i])
    m = re.search(r"kBattlePose\[\]\s*=\s*\{(.*?)\};", src, re.S)
    pnum = {"PM64_PARTNER_" + n.upper(): p for n, (p, _m) in PARTNERS.items()}
    for pp, pose, anim in re.findall(r"\{(PM64_PARTNER_\w+),\s*\"([^\"]*)\",\s*(-?\w+)\}", m.group(1) if m else ""):
        if re.fullmatch(r"-?\d+", anim):
            played(pnum[pp], 1, int(anim), "kBattlePose " + pose)
    for p, battle, row, why in EXTRA_ROWS:
        played(p, battle, row, why)
    # the port's move scripts' own rows (pm64SetAnim(unit, anim, overlay))
    call = re.compile(r"CALL\((pm64SetAnim|[a-z]+Anim),\s*[^,()]+,\s*([\w-]+)(?:,\s*([\w-]+))?\)")
    for name, (p, _m) in PARTNERS.items():
        path = os.path.join(MOVES, name + ".inc")
        text = open(path, encoding="utf-8").read() if os.path.isfile(path) else ""
        if name == "bow":
            text += raw
        defines = {k: int(v) for k, v in re.findall(r"#define\s+(\w+)\s+(-?\d+)\b", text)}
        where = os.path.basename(path) if name != "bow" else "Bow's events"
        for fn, a, o in call.findall(_strip_c(text)):
            if fn != "pm64SetAnim" and not fn.startswith(name):
                continue
            for tok, what in ((a, fn), (o, fn + " overlay")):
                if not tok:
                    continue
                v = int(tok) if re.fullmatch(r"-?\d+", tok) else defines.get(tok)
                played(p, 1, v, "%s (%s)" % (what, where))
    return out


def row_of(key, rows):
    """an anims key -> (battle, row), or raises ValueError"""
    if key in A_NAMES:
        return 0, rows["A"][A_NAMES.index(key)]
    if key in B_NAMES:
        return 1, rows["B"][B_NAMES.index(key)]
    m = re.fullmatch(r"(?:(F|B)_?|(field|battle)[:_ ]?)(\d+)", key)
    if m:
        battle = (m.group(1) or m.group(2)[0].upper()) == "B"
        return (1 if battle else 0), int(m.group(3))
    raise ValueError("%r isn't a row (A_IDLE..A_MOVE, B_IDLE..B_VICTORY, F<n>, B<n>)" % key)


# --------------------------------------------------------------------- the disc ----

def default_disc():
    for root in (REPO, os.path.join(os.path.dirname(REPO), "ttyd-port")):
        for d in (os.path.join(root, "data", "G8ME01", "files"), os.path.join(root, "orig", "G8ME01", "files")):
            if os.path.isfile(os.path.join(d, "a", "c_kurio")):
                return d
        isos = glob.glob(os.path.join(root, "orig", "G8ME01", "*.iso"))
        if isos:
            return isos[0]
    return None


class Disc:
    def __init__(self, path):
        import anim_recipe
        self.src = anim_recipe._source(path)
        self.cache = {}

    def read(self, name):
        if name not in self.cache:
            self.cache[name] = self.src.read("a/" + name)
        return self.cache[name]

    def pose(self, model):
        import ttyd_anim as T
        return T.PoseFile.parse(self.read(model))


def rig(pose):
    """[(name without its '|', parent index or -1)] in rig order, and the top joints"""
    gs = pose.groups()
    parent = [-1] * len(gs)
    for i, g in enumerate(gs):
        c = g.child
        while 0 <= c < len(gs):
            parent[c] = i
            c = gs[c].brother
    names = [g.name.lstrip("|") for g in gs]
    tops = [names[i] for i in range(len(gs)) if parent[i] < 0]
    return [(names[i], parent[i]) for i in range(len(gs))], tops, [g.name for g in gs]


def rig_info(pose):
    """{"tops": [...], "vis": {joint: visibility index}, "slot": {joint: texture slot (texAnim index) or None}}"""
    import ttyd_anim as T
    _r, tops, _raw = rig(pose)
    geo = T.Geometry(pose)
    vis, slot, shape, uv, area = {}, {}, {}, {}, {}
    for g in pose.groups():
        n = g.name.lstrip("|")
        vis[n] = g.vis
        slot[n] = None
        shape[n] = g.shape
        uv[n] = None
        area[n] = 0.0
        if 0 <= g.shape < len(geo.shapes):
            for _d, fan in geo.shape_polys(g.shape):
                ps = [geo.pos[c[0]] for c in fan]
                for k in range(1, len(ps) - 1):  # (its triangles' area, the model's rest, by x and y)
                    (x0, y0, _a), (x1, y1, _b), (x2, y2, _c) = ps[0], ps[k], ps[k + 1]
                    area[n] += abs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) / 2.0
            uvs = [c[2] for _d, fan in geo.shape_polys(g.shape) for c in fan if c[2] is not None]
            if uvs:
                uv[n] = [round(min(u for u, _v in uvs), 5), round(min(v for _u, v in uvs), 5),
                         round(max(u for u, _v in uvs), 5), round(max(v for _u, v in uvs), 5)]
            sh = geo.shapes[g.shape]
            for di in range(sh["draw_start"], sh["draw_start"] + sh["draw_count"]):
                dr = geo.draws[di]
                if dr["tex_count"] > 0:
                    slot[n] = dr["tex_data"][dr["tex_count"] - 1]  # (the slot animdrv animates: the last)
                    break
    body = max(area, key=lambda n: area[n]) if area else None
    return {"tops": tops, "vis": vis, "slot": slot, "shape": shape, "uv": uv, "area": area, "body": body,
            "nslots": len(pose.tables["texAnim"]), "pose": pose}


# ----------------------------------------------------------- keys -> recipe ----

def ease_u(e, u):
    if e == "step":
        return 0.0
    if e == "in":
        return u * u
    if e == "out":
        return 1.0 - (1.0 - u) * (1.0 - u)
    if e == "inout":
        return 2 * u * u if u < 0.5 else 1.0 - 2 * (1.0 - u) * (1.0 - u)
    return u


def track_value(pts, t, frames, loop):
    """pts [(f, value, ease)] sorted; the value at t (a loop wraps from the last key to the first at frames)"""
    if loop and len(pts) > 1:
        ext = [(pts[-1][0] - frames, pts[-1][1], pts[-1][2])] + pts + [(pts[0][0] + frames, pts[0][1], pts[0][2])]
    else:
        ext = pts
    if t <= ext[0][0]:
        return ext[0][1]
    if t >= ext[-1][0]:
        return ext[-1][1]
    j = 0
    while ext[j + 1][0] <= t:
        j += 1
    f0, v0, e0 = ext[j]
    f1, v1, _e1 = ext[j + 1]
    u = ease_u(e0, (t - f0) / (f1 - f0)) if f1 > f0 else 0.0
    return v0 + (v1 - v0) * u


def track_ease_at(pts, t, frames, loop):
    """the ease of the segment leaving t"""
    if loop and len(pts) > 1:
        ext = [(pts[-1][0] - frames, pts[-1][1], pts[-1][2])] + pts
    else:
        ext = pts
    e = ext[0][2]
    for f, _v, ee in ext:
        if f <= t:
            e = ee
    return e


def track_points(pts, frames, loop):
    """recipe points [frame, value, "linear"|"step"], equal to the keys at every whole frame"""
    if all(abs(v - pts[0][1]) < 1e-9 for _f, v, _e in pts):
        return [[0.0, pts[0][1], "linear"]]
    times = {0.0, float(frames)} | {float(f) for f, _v, _e in pts if 0 <= f <= frames}
    ext = pts
    if loop and len(pts) > 1:
        ext = [(pts[-1][0] - frames, pts[-1][1], pts[-1][2])] + pts + [(pts[0][0] + frames, pts[0][1], pts[0][2])]
    for j in range(len(ext) - 1):
        f0, _v, e0 = ext[j]
        f1 = ext[j + 1][0]
        if e0 in ("in", "out", "inout"):
            for t in range(int(math.floor(f0)) + 1, int(math.ceil(f1))):
                if 0 <= t <= frames:
                    times.add(float(t))
    out = []
    for t in sorted(times):
        e = track_ease_at(pts, t, frames, loop)
        out.append([t, round(track_value(pts, t, frames, loop), 5), "step" if e == "step" else "linear"])
    return out


def recipe_of(doc, rows, info):
    """(the recipe for anim_recipe: {"model", "anims"}, [(battle, row, anim, frames, loop, exact, sprite, events,
    extras, tint)]); info: rig_info"""
    tops = info["tops"]
    anims = {}
    table = []
    sprite = set()
    for k in doc.get("sprite_rows") or []:
        sprite.add(row_of(k, rows))
    approved_all = "*" in (doc.get("approved_rows") or [])
    approved = {row_of(k, rows) for k in doc.get("approved_rows") or [] if k != "*"}
    for key, a in doc["anims"].items():
        battle, row = row_of(key, rows)
        frames = int(a["frames"])
        loop = bool(a.get("loop", True))
        face = a.get("face")
        src, rng = "Z_1", [0.0, 0.0]
        if face:
            fa, _sep, ff = str(face).partition("@")
            src, rng = fa, [float(ff or 0), float(ff or 0)]
        add = {}
        for joint, keys in (a.get("keys") or {}).items():
            groups = tops if joint == "root" else [joint]
            keys = sorted(keys, key=lambda k: float(k["f"]))
            for ch, names in CHANNELS.items():
                if not any(ch in k for k in keys):
                    continue
                # every key of the joint is a key of each channel: one it leaves out holds the key before's value
                # (the rest's before the first that gives it), with that key's ease
                pts, held = [], [1.0, 1.0, 1.0] if ch == "s" else [0.0, 0.0, 0.0]
                for k in keys:
                    if ch in k:
                        held = [float(x) for x in k[ch]]
                    pts.append((float(k["f"]), list(held), k.get("ease", "linear")))
                for c in range(3):
                    cp = [(f, v[c], e) for f, v, e in pts]
                    if all(abs(v - (1.0 if ch == "s" else 0.0)) < 1e-9 for _f, v, _e in cp):
                        continue
                    for g in groups:
                        add.setdefault(g, {})[names[c]] = track_points(cp, frames, loop)
        spec = {"from": src, "range": rng, "end": float(frames), "loop": loop}
        if add:
            spec["add"] = add
        name = anim_name(battle, row)
        anims[name] = spec
        if a.get("from_model"):
            spec["from_model"] = a["from_model"]
        events = sorted((int(e["f"]), str(e.get("what", ""))) for e in a.get("events") or [])
        exact = bool(a.get("exact", True))
        ok = approved_all or (battle, row) in approved or bool(a.get("approved", False))
        # in play: the pose, unless the owner listed the row a sprite, or it is not exact and not approved yet
        keep = (battle, row) in sprite or (not exact and not ok)
        t = a.get("tint")
        tint = [int(x) for x in t][:3] + [255] if isinstance(t, list) and t and not isinstance(t[0], dict) else [0] * 4
        table.append((battle, row, name, frames, loop, exact, keep, events, extras_of(a, info), tint,
                      parts_of(a, info), rules_of(doc, a, info)))
    table.sort()
    return {"model": doc["model"], "anims": anims}, table


def slot_of(name, info):
    """a "tex" target: a texture slot number, or a joint (its shape's slot)"""
    if re.fullmatch(r"\d+", str(name)):
        return int(name)
    sl = info["slot"].get(str(name).lstrip("|")) if info else None
    if sl is None:
        raise ValueError("tex %r: not a slot number, nor a joint with a texture" % name)
    return sl


def tex_keys(a, info):
    """{slot: [(f, frame)]}: the row's "tex" ({"<slot or joint>": [{"f", "frame"}]}) and its keys' own
    ("tex": {"<slot or joint>": n}); frame: the slot's texture added to the model's own (0 its rest)"""
    out = {}
    for name, ks in (a.get("tex") or {}).items():
        for k in ks:
            out.setdefault(slot_of(name, info), {})[int(k["f"])] = int(k["frame"])
    for _joint, keys in (a.get("keys") or {}).items():
        for k in keys:
            for name, v in (k.get("tex") or {}).items():
                out.setdefault(slot_of(name, info), {})[int(k["f"])] = int(v)
    return {slot: sorted(d.items()) for slot, d in out.items()}


def extras_of(a, info):
    """[(f, kind, target, value)]: texture frames (step), groups shown / hidden all the row"""
    ex = []
    vis = info["vis"]
    for slot, ks in sorted(tex_keys(a, info).items()):
        ex += [(f, EXTRA_TEX, slot, v) for f, v in ks]
    # shown / hidden: "show" and "hide" (all the row), "vis" ({"<joint>": [{"f", "on"}]}, step); one track a joint
    on = {}
    for field, v in (("show", 1), ("hide", 0)):
        for j in a.get(field) or []:
            on.setdefault(vis[j.lstrip("|")], {})[0] = v
    for j, ks in (a.get("vis") or {}).items():
        for k in ks:
            on.setdefault(vis[j.lstrip("|")], {})[int(k["f"])] = 1 if k.get("on", True) else 0
    for _name, _plane, hides, _keys in sprite_tracks(a):
        for j in hides:
            on[vis[j.lstrip("|")]] = {0: 0}  # (a joint a sprite part replaces: hidden all the row)
    for target in sorted(on):
        ex += [(f, EXTRA_SHOW, target, v) for f, v in sorted(on[target].items())]
    t = a.get("tint")
    if isinstance(t, list) and t and isinstance(t[0], dict):
        # a keyed tint: [{"f", "rgb": [r, g, b]}, ...] (step; "rgb": null: none)
        for k in sorted(t, key=lambda k: k["f"]):
            c = k.get("rgb")
            ex.append((int(k["f"]), EXTRA_TINT, 0, -1 if not c else (int(c[0]) << 16) | (int(c[1]) << 8) | int(c[2])))
    return ex


def _bits(v):
    return sum(1 << int(m) for m in set(v or []))


def sprite_tracks(a):
    """the row's sprite-pixel tracks, one format: [(name, plane joint or None, hides, keys)]. "sprite_parts"
    ({"<name>": {"plane"?, "hides"?, "keys": [...]}}) and the older "parts" ({"<joint>": [keys]}: a plane each, its
    raster -1 meaning the component's frame, as Bow wrote it)"""
    out = []
    for name, tr in (a.get("sprite_parts") or {}).items():
        out.append((name, tr.get("plane"), list(tr.get("hides") or []), list(tr.get("keys") or [])))
    for joint, keys in (a.get("parts") or {}).items():
        ks = [dict(k, raster="frame") if k.get("raster", "frame") == -1 else k for k in keys]
        out.append(("parts:" + joint, joint, [], ks))
    return out


RULE_KEYS = {"rect", "inside_ellipse", "outside_ellipse", "luma_min", "luma_max", "near", "tol", "to", "to_gray",
             "to_palette", "note"}


def rules_of(doc, a, info):
    """the row's recolour rules for the C table, in order: the file's "texture_edits" (every row), then the row's
    "recolor"; each {"<joint>" or "*": [rule, ...]}. A rule: conditions (all must hold) "rect" [x, y, w, h],
    "inside_ellipse" / "outside_ellipse" [cx, cy, rx, ry] (texture pixels from the top left), "luma_min" /
    "luma_max", "near" [[r, g, b], ...] with "tol"; then one of "to": [r, g, b] | "clear" | "base", "to_gray":
    [[luma in, grey out], ...], "to_palette": {"component", "from", "to"}. The first rule a texel matches wins."""
    out = []
    for pre, src in ((1, doc.get("texture_edits") or {}), (0, a.get("recolor") or {})):
        for joint, rules in src.items():
            shape = -1 if joint == "*" else info["shape"][joint.lstrip("|")]
            for r in rules:
                when, box, ell = 0, [0.0] * 4, [0.0] * 4
                if "rect" in r:
                    when |= RULE_RECT
                    box = [float(x) for x in r["rect"]]
                for fld, bit in (("inside_ellipse", RULE_IN_ELLIPSE), ("outside_ellipse", RULE_OUT_ELLIPSE)):
                    if fld in r:
                        when |= bit
                        ell = [float(x) for x in r[fld]]
                lo, hi = int(r.get("luma_min", 0)), int(r.get("luma_max", 255))
                if "luma_min" in r or "luma_max" in r:
                    when |= RULE_LUMA
                near = [[int(c) for c in x] for x in (r.get("near") or [])][:4]
                if near:
                    when |= RULE_NEAR
                act, rgb, gray, comp, pf, pt = ACT_RGB, [0, 0, 0], [], 0, -1, -2
                to = r.get("to")
                if "to_gray" in r:
                    act, gray = ACT_GRAY, [[int(x), int(y)] for x, y in r["to_gray"]][:8]
                elif "to_palette" in r:
                    tp = r["to_palette"] or {}
                    act, comp = ACT_PALETTE, int(tp.get("component", 0))
                    pf = -1 if tp.get("from", 0) == "own" else int(tp.get("from", 0))
                    pt = -2 if tp.get("to", "frame") == "frame" else int(tp.get("to"))
                elif to == "clear":
                    act = ACT_CLEAR
                elif to == "base":
                    act = ACT_BASE
                else:
                    rgb = [int(x) for x in to]
                out.append((shape, when, act, box, ell, lo, hi, near, int(r.get("tol", 24)), rgb, gray, comp, pf, pt,
                            pre))
    return out


def check_rules(where, src, info, known, rep):
    if not isinstance(src, dict):
        rep.err("%s must be {\"<joint>\" or \"*\": [rules]}" % where)
        return
    for joint, rules in src.items():
        rw = "%s[%s]" % (where, joint)
        j = joint.lstrip("|")
        if joint != "*" and j not in known:
            rep.err("%s: no joint %r in the rig" % (rw, joint))
            continue
        if joint != "*" and info is not None and info["shape"].get(j, -1) < 0:
            rep.err("%s: %s has no plane (no texture of its own)" % (rw, joint))
        if not isinstance(rules, list):
            rep.err("%s: must be a list of rules" % rw)
            continue
        for i, r in enumerate(rules):
            kw = "%s[%d]" % (rw, i)
            if not isinstance(r, dict):
                rep.err("%s: must be an object" % kw)
                continue
            for x in r:
                if x not in RULE_KEYS:
                    rep.note("%s: unknown field %r (ignored)" % (kw, x))
            acts = [x for x in ("to", "to_gray", "to_palette") if x in r]
            if len(acts) != 1:
                rep.err("%s: one of \"to\", \"to_gray\", \"to_palette\"" % kw)
            to = r.get("to")
            if "to" in r and not (to in ("clear", "base") or (isinstance(to, list) and len(to) == 3 and
                                                              all(isinstance(v, int) and 0 <= v <= 255 for v in to))):
                rep.err("%s: to must be [r, g, b], \"clear\" or \"base\"" % kw)
            if "to_gray" in r and not (isinstance(r["to_gray"], list) and 0 < len(r["to_gray"]) <= 8 and
                                       all(isinstance(q, list) and len(q) == 2 for q in r["to_gray"])):
                rep.err("%s: to_gray must be 1..8 [luma in, grey out] pairs, in order" % kw)
            for fld in ("rect", "inside_ellipse", "outside_ellipse"):
                if fld in r and not (isinstance(r[fld], list) and len(r[fld]) == 4 and
                                     all(isinstance(v, (int, float)) for v in r[fld])):
                    rep.err("%s: %s must be 4 numbers (texture pixels)" % (kw, fld))
            if "near" in r and not (isinstance(r["near"], list) and 0 < len(r["near"]) <= 4):
                rep.err("%s: near must be 1..4 [r, g, b] colours" % kw)


_IDENT = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


def _affine_inverse(m):
    """the inverse of a 3x4 affine (rows of 4)"""
    a = [r[:3] for r in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        return [r[:] for r in _IDENT]
    inv = [[(a[(j + 1) % 3][(i + 1) % 3] * a[(j + 2) % 3][(i + 2) % 3] -
             a[(j + 1) % 3][(i + 2) % 3] * a[(j + 2) % 3][(i + 1) % 3]) / det for j in range(3)] for i in range(3)]
    t = [m[0][3], m[1][3], m[2][3]]
    return [[inv[i][0], inv[i][1], inv[i][2], -sum(inv[i][k] * t[k] for k in range(3))] for i in range(3)]


def follow_rest(info, a, joint):
    """(group index, the model space to that joint's space at the row's rest): a quad that follows the joint"""
    import ttyd_anim as T
    pose = info["pose"]
    names = [g.name.lstrip("|") for g in pose.groups()]
    j = info["tops"][0] if joint == "root" else joint.lstrip("|")
    gi = names.index(j)
    src, fr = "Z_1", 0.0
    if a.get("face") and not a.get("from_model"):
        fa, _sep, ff = str(a["face"]).partition("@")
        src, fr = fa, float(ff or 0)
    key = (gi, src, fr)
    cache = info.setdefault("_rest", {})
    if key not in cache:
        ev = T.Evaluator(pose)
        node, _vis, _pos, _tex = ev.at_frame(pose.anim(src), fr)
        mats = ev.world_matrices(node, [1] * 4096)
        m = mats[gi]
        cache[key] = _affine_inverse([list(m[r][:4]) for r in range(3)]) if m is not None else _IDENT
    return gi, cache[key]


def parts_of(a, info):
    """the C table's part keys: [(f, track, shape, raster, component, flags, palette, mask, edge, notNear, rect[4],
    uv[4], at[2], px[2], z, rz, joint, rest[3][4])], a track's keys together in f order"""
    out = []
    for t, (name, plane, _hides, keys) in enumerate(sprite_tracks(a)):
        j = plane.lstrip("|") if plane else None
        fol = (a.get("sprite_parts") or {}).get(name, {}).get("follow") if not name.startswith("parts:") else None
        jo, rest = follow_rest(info, a, fol) if fol and not j else (-1, _IDENT)
        fm = {"full": 0, "translate": 1, "translate_scale": 2}.get(
            (a.get("sprite_parts") or {}).get(name, {}).get("follow_mode", "full"), 0) if not name.startswith("parts:") else 0
        for k in sorted(keys, key=lambda k: k["f"]):
            r = k.get("raster", "frame")
            pal = k.get("palette", "frame" if r == "frame" else "own")
            off = bool(k.get("off")) or r == -1 or r is None
            out.append((int(k["f"]), t, info["shape"][j] if j else -1, -1 if r in ("frame", -1, None) else int(r),
                        int(k.get("component", 0)),
                        (PART_FLIP if k.get("flip") else 0) | (PART_OFF if off else 0),
                        -2 if pal == "frame" else -1 if pal == "own" else int(pal),
                        _bits(k.get("idx", k.get("mask"))), _bits(k.get("edge")),
                        _bits(k.get("edge_not_near", k.get("not_near"))),
                        [int(x) for x in (k.get("rect") or [0, 0, 0, 0])],
                        info["uv"][j] if j else [0.0, 0.0, 1.0, 1.0],
                        [float(x) for x in (k.get("at") or [0.0, 0.0])],
                        [float(x) for x in (k.get("px") or [0.5, 0.5])],
                        float(k.get("z", 0.0)), float(k.get("rz", 0.0)), jo, rest, fm))
    return out


# ----------------------------------------------------------------- checking ----

class Report:
    def __init__(self, name):
        self.name = name
        self.errors = []
        self.notes = []

    def err(self, msg):
        self.errors.append(msg)

    def note(self, msg):
        self.notes.append(msg)


def _vec(v):
    return isinstance(v, list) and len(v) == 3 and all(isinstance(x, (int, float)) and not isinstance(x, bool)
                                                         for x in v)


def check_doc(name, doc, rows, disc, rep):
    p, model = PARTNERS[name]
    if doc.get("version") != 1:
        rep.err("version must be 1")
    if doc.get("partner") != name:
        rep.err("partner must be %r" % name)
    if doc.get("model") != model:
        rep.err("model must be %r" % model)
    for k in doc:
        if k not in KNOWN_TOP:
            rep.note("unknown top-level field %r (ignored)" % k)
    joints = [str(j).lstrip("|") for j in doc.get("joints") or []]
    rigj = tops = info = None
    pose = None
    if disc is not None:
        pose = disc.pose(model)
        info = rig_info(pose)
        r, tops, _raw = rig(pose)
        rigj = [n for n, _p in r]
        if joints != rigj:
            missing = [j for j in rigj if j not in joints]
            extra = [j for j in joints if j not in rigj]
            rep.err("joints isn't the rig in order (pm64_poses.py rig %s): %s%s%s" % (
                name, "missing %s; " % missing if missing else "", "not in the rig %s; " % extra if extra else "",
                "order differs" if not missing and not extra else ""))
        model_anims = {a.name: a for a in pose.anims}
    else:
        model_anims = None
    known = set(rigj or joints)
    anims = doc.get("anims")
    if not isinstance(anims, dict) or not anims:
        rep.err("anims must be a non-empty object")
        return None
    seen = {}
    for key, a in anims.items():
        where = "anims[%s]" % key
        try:
            br = row_of(key, rows)
        except ValueError as e:
            rep.err("%s: %s" % (where, e))
            continue
        if br in seen:
            rep.err("%s: the same sprite row (%s %d) as anims[%s]: one pose a row" % (
                where, "battle" if br[0] else "field", br[1], seen[br]))
        seen[br] = key
        if br not in rows["played"]:
            rep.note("%s: %s row %d isn't one pm64_partner_pc.c is seen playing (kept; check the row)" % (
                where, "battle" if br[0] else "field", br[1]))
        if not isinstance(a, dict):
            rep.err("%s: must be an object" % where)
            continue
        for k in a:
            if k not in KNOWN_ANIM:
                rep.note("%s: unknown field %r (ignored)" % (where, k))
        if not a.get("pm64"):
            rep.note("%s: no \"pm64\" (what PM64 plays, and where in the decomp)" % where)
        frames = a.get("frames")
        if not isinstance(frames, int) or isinstance(frames, bool) or frames <= 0 or frames > 2000:
            rep.err("%s: frames must be a whole number 1..2000" % where)
            continue
        if "loop" in a and not isinstance(a["loop"], bool):
            rep.err("%s: loop must be true or false" % where)
        for b in ("exact", "approved"):
            if b in a and not isinstance(a[b], bool):
                rep.err("%s: %s must be true or false" % (where, b))
        tint = a.get("tint")
        if isinstance(tint, list) and tint and isinstance(tint[0], dict):
            for i, k in enumerate(tint):
                c = k.get("rgb") if isinstance(k, dict) else None
                if not isinstance(k, dict) or not isinstance(k.get("f"), int) or not 0 <= k["f"] <= frames or not (
                        c is None or (isinstance(c, list) and len(c) == 3 and
                                      all(isinstance(x, int) and 0 <= x <= 255 for x in c))):
                    rep.err("%s: tint[%d] must be {\"f\": 0..%d, \"rgb\": [r, g, b] or null}" % (where, i, frames))
        elif tint is not None and not (isinstance(tint, list) and len(tint) in (3, 4) and
                                       all(isinstance(x, int) and 0 <= x <= 255 for x in tint)):
            rep.err("%s: tint must be [r, g, b], or keyed [{\"f\", \"rgb\"}]" % where)
        if a.get("recolor") is not None:
            check_rules("%s.recolor" % where, a["recolor"], info, known, rep)
        for fld in ("parts", "sprite_parts"):
            if not isinstance(a.get(fld) or {}, dict):
                rep.err("%s: %s must be an object" % (where, fld))
                a = dict(a)
                a[fld] = {}
        try:
            tracks = sprite_tracks(a)
        except (AttributeError, TypeError):
            rep.err("%s: sprite_parts must be {\"<name>\": {\"plane\"?, \"hides\"?, \"keys\": [...]}}" % where)
            tracks = []
        nplanes = nquads = 0
        for tname, plane, hides, pks in tracks:
            pw = "%s.sprite_parts[%s]" % (where, tname)
            fol = (a.get("sprite_parts") or {}).get(tname, {}).get("follow") if not tname.startswith("parts:") else None
            if fol is not None and fol != "root" and str(fol).lstrip("|") not in known:
                rep.err("%s: follow %r: no joint of the rig (or \"root\")" % (pw, fol))
            fmode = (a.get("sprite_parts") or {}).get(tname, {}).get("follow_mode") if not tname.startswith("parts:") else None
            if fmode is not None and fmode not in ("full", "translate", "translate_scale"):
                rep.err("%s: follow_mode must be \"full\", \"translate\" or \"translate_scale\"" % pw)
            if info is not None and info["body"] is not None:
                big = info["area"][info["body"]]
                for j in list(hides) + ([plane] if plane else []):
                    jj = str(j).lstrip("|")
                    if info["area"].get(jj, 0.0) >= 0.5 * big:
                        rep.note("%s: WARNING %s is a body plane (%s): the owner's rule is that the body stays the "
                                 "TTYD model; sprite pixels are for features only" % (
                                     pw, j, "hidden" if j in hides else "drawn as the sprite"))
            for j in hides:
                if str(j).lstrip("|") not in known:
                    rep.err("%s: hides %r: no such joint" % (pw, j))
            if plane is not None:
                nplanes += 1
                j = str(plane).lstrip("|")
                if j not in known:
                    rep.err("%s: plane %r: no joint of the rig" % (pw, plane))
                elif info is not None and (info["shape"].get(j, -1) < 0 or info["uv"].get(j) is None or
                                           info["slot"].get(j) is None):
                    rep.err("%s: plane %s has no textured plane of its own" % (pw, plane))
            else:
                nquads += 1
            if not isinstance(pks, list) or not pks:
                rep.err("%s: keys must be a non-empty list" % pw)
                continue
            last = None
            for i, k in enumerate(pks):
                kw = "%s[%d]" % (pw, i)
                if not isinstance(k, dict) or not isinstance(k.get("f"), int) or isinstance(k.get("f"), bool):
                    rep.err("%s: must be an object with a whole \"f\"" % kw)
                    continue
                if not 0 <= k["f"] <= frames:
                    rep.err("%s: f %d outside 0..%d" % (kw, k["f"], frames))
                if last is not None and k["f"] <= last:
                    rep.err("%s: f %d not after the key before (%d)" % (kw, k["f"], last))
                last = k["f"]
                r = k.get("raster", "frame")
                if not (r in ("frame", None) or (isinstance(r, int) and -1 <= r < 400)):
                    rep.err("%s: raster must be \"frame\", -1 (nothing) or a raster number" % kw)
                cmp_ = k.get("component", 0)
                if not isinstance(cmp_, int) or not 0 <= cmp_ < 16:
                    rep.err("%s: component must be 0..15 (the sprite's component)" % kw)
                pal = k.get("palette", "frame")
                if not (pal in ("frame", "own") or (isinstance(pal, int) and 0 <= pal < 64)):
                    rep.err("%s: palette must be \"frame\", \"own\" or a palette number" % kw)
                rc = k.get("rect")
                if rc is not None and not (isinstance(rc, list) and len(rc) == 4 and
                                           all(isinstance(x, int) and 0 <= x <= 512 for x in rc)):
                    rep.err("%s: rect must be [x, y, w, h] in the raster's pixels (from its top left)" % kw)
                for fld in ("idx", "mask", "edge", "edge_not_near", "not_near"):
                    v = k.get(fld)
                    if v is not None and not (isinstance(v, list) and all(isinstance(x, int) and 0 <= x < 16
                                                                          for x in v)):
                        rep.err("%s: %s must be a list of palette indices 0..15" % (kw, fld))
                for fld, n in (("at", 2), ("px", 2)):
                    v = k.get(fld)
                    if v is not None and not (isinstance(v, list) and len(v) == n and
                                              all(isinstance(x, (int, float)) for x in v)):
                        rep.err("%s: %s must be %d numbers" % (kw, fld, n))
                if plane is None and r not in (-1, None) and not k.get("off") and k.get("at") is None:
                    rep.err("%s: a quad needs \"at\" (its rect's top left in model units) and \"px\"" % kw)
                for fld in ("z", "rz"):
                    if fld in k and not isinstance(k[fld], (int, float)):
                        rep.err("%s: %s must be a number" % (kw, fld))
                for x in k:
                    if x not in ("f", "raster", "component", "palette", "rect", "idx", "mask", "edge",
                                 "edge_not_near", "not_near", "at", "px", "z", "rz", "flip", "off", "note"):
                        rep.note("%s: unknown field %r (ignored)" % (kw, x))
        if nplanes > 12 or nquads > 12:
            rep.err("%s: more than 12 plane parts or 12 quads in a row (the game keeps 12 of each a frame)" % where)
        for fld in ("show", "hide"):
            for j in a.get(fld) or []:
                if str(j).lstrip("|") not in known:
                    rep.err("%s: %s %r: no such joint" % (where, fld, j))
        vs = a.get("vis") or {}
        if not isinstance(vs, dict):
            rep.err("%s: vis must be {\"<joint>\": [{\"f\": n, \"on\": bool}]}" % where)
            vs = {}
        for j, ks in vs.items():
            if str(j).lstrip("|") not in known:
                rep.err("%s: vis %r: no such joint" % (where, j))
            if not isinstance(ks, list) or not all(isinstance(k, dict) and isinstance(k.get("f"), int) and
                                                   0 <= k["f"] <= frames for k in ks):
                rep.err("%s: vis[%s] must be a list of {\"f\": 0..%d, \"on\": bool}" % (where, j, frames))
        texs = a.get("tex") or {}
        if not isinstance(texs, dict):
            rep.err("%s: tex must be {\"<slot or joint>\": [{\"f\": n, \"frame\": n}]}" % where)
            texs = {}
        for tname, tks in texs.items():
            if not isinstance(tks, list) or not all(isinstance(k, dict) and isinstance(k.get("f"), int) and
                                                   isinstance(k.get("frame"), int) for k in tks):
                rep.err("%s: tex[%s] must be a list of {\"f\": frame, \"frame\": n}" % (where, tname))
                continue
            for k in tks:
                if k["f"] < 0 or k["f"] > frames:
                    rep.err("%s: tex[%s] f %d outside 0..%d" % (where, tname, k["f"], frames))
        if info is not None:
            try:
                for slot, ks in tex_keys(a, info).items():
                    if slot >= info["nslots"]:
                        rep.err("%s: tex slot %d: the model has %d" % (where, slot, info["nslots"]))
                    if any(not -8 <= v <= 32 for _f, v in ks):
                        rep.note("%s: tex slot %d: a texture step past -8..32 (check it)" % (where, slot))
            except (ValueError, TypeError, KeyError) as e:
                rep.err("%s: %s" % (where, e))
        if a.get("from_model") and disc is not None:
            try:
                fm = disc.pose(a["from_model"])
                if a.get("face") and str(a["face"]).partition("@")[0] not in {x.name for x in fm.anims}:
                    rep.err("%s: from_model %s has no %s" % (where, a["from_model"], a["face"]))
                if [g.name.lstrip("|") for g in fm.groups()] != rigj:
                    rep.err("%s: from_model %s's rig isn't this model's" % (where, a["from_model"]))
            except KeyError:
                rep.err("%s: from_model %r isn't on the disc" % (where, a["from_model"]))
        face = a.get("face")
        if a.get("from_model"):
            face = None  # (checked against that model above)
        if face is not None:
            fa, _sep, ff = str(face).partition("@")
            if model_anims is not None and fa not in model_anims:
                rep.err("%s: face %r: the model has no animation %r (it has %s)" % (
                    where, face, fa, ", ".join(sorted(model_anims))))
            elif ff and not re.fullmatch(r"\d+(\.\d+)?", ff):
                rep.err("%s: face %r: the frame after @ must be a number" % (where, face))
            elif ff and model_anims is not None and float(ff) > model_anims[fa].end:
                rep.err("%s: face %r: %s is %g frames long" % (where, face, fa, model_anims[fa].end))
        keys = a.get("keys") or {}
        if not isinstance(keys, dict):
            rep.err("%s: keys must be an object" % where)
            continue
        if "root" in keys and tops and any(t in keys for t in tops):
            rep.err("%s: both root and %s keyed (root is the top joint %s)" % (where, tops, tops))
        for joint, ks in keys.items():
            jw = "%s.keys[%s]" % (where, joint)
            if joint != "root" and joint.lstrip("|") not in known:
                rep.err("%s: no joint %r in the rig" % (jw, joint))
            if not isinstance(ks, list) or not ks:
                rep.err("%s: must be a non-empty list of keys" % jw)
                continue
            last = None
            for i, k in enumerate(ks):
                kw = "%s[%d]" % (jw, i)
                if not isinstance(k, dict):
                    rep.err("%s: must be an object" % kw)
                    continue
                f = k.get("f")
                if not isinstance(f, (int, float)) or isinstance(f, bool):
                    rep.err("%s: f must be a number" % kw)
                    continue
                if f < 0 or f > frames:
                    rep.err("%s: f %g outside 0..%d" % (kw, f, frames))
                if f != int(f):
                    rep.note("%s: f %g isn't a whole frame (the sprite's clock is whole frames)" % (kw, f))
                if last is not None and f <= last:
                    rep.err("%s: f %g not after the key before (%g)" % (kw, f, last))
                last = f
                if not any(c in k for c in ("r", "t", "s", "tex")):
                    rep.note("%s: no r, t, s or tex" % kw)
                for c in "rts":
                    if c in k and not _vec(k[c]):
                        rep.err("%s: %s must be [x, y, z] numbers" % (kw, c))
                if "s" in k and _vec(k["s"]) and any(abs(x) < 1e-3 for x in k["s"]):
                    rep.note("%s: s has a 0 (the part vanishes)" % kw)
                if "r" in k and _vec(k["r"]) and any(abs(x) > 1440 for x in k["r"]):
                    rep.err("%s: r over 1440 degrees" % kw)
                if k.get("ease", "linear") not in EASES:
                    rep.err("%s: ease %r isn't one of %s" % (kw, k.get("ease"), ", ".join(EASES)))
                if "tex" in k and not (isinstance(k["tex"], dict) and
                                       all(isinstance(v, int) for v in k["tex"].values())):
                    rep.err("%s: tex must be {\"<slot or joint>\": n}" % kw)
                for x in k:
                    if x not in ("f", "r", "t", "s", "ease", "note", "tex"):
                        rep.note("%s: unknown field %r (ignored)" % (kw, x))
        for i, e in enumerate(a.get("events") or []):
            ew = "%s.events[%d]" % (where, i)
            if not isinstance(e, dict) or not isinstance(e.get("f"), (int, float)) or isinstance(e.get("f"), bool):
                rep.err("%s: must be {\"f\": frame, \"what\": text}" % ew)
            elif e["f"] < 0 or e["f"] > frames:
                rep.err("%s: f %g outside 0..%d" % (ew, e["f"], frames))
            elif not e.get("what"):
                rep.note("%s: no \"what\"" % ew)
    for fld in ("sprite_rows", "approved_rows"):
        for k in doc.get(fld) or []:
            if fld == "approved_rows" and k == "*":
                continue
            try:
                br = row_of(k, rows)
                if br not in seen:
                    rep.note("%s %r: no pose for that row" % (fld, k))
            except ValueError as e:
                rep.err("%s: %s" % (fld, e))
    held = [k for k, a in anims.items() if isinstance(a, dict) and a.get("exact") is False and not (
        a.get("approved") or "*" in (doc.get("approved_rows") or []) or k in (doc.get("approved_rows") or []))]
    if held:
        rep.note("not exact and not approved (sprites in play until approved): %s" % ", ".join(held))
    if doc.get("texture_edits") is not None:
        check_rules("texture_edits", doc["texture_edits"], info, known, rep)
    missing = sorted(set(rows["played"]) - set(seen))
    rep.note("coverage: %d of the %d rows the sprite plays have poses%s" % (
        len(set(rows["played"]) & set(seen)), len(rows["played"]),
        ("; missing (sprites): " + ", ".join("%s%d" % ("B" if b else "F", r) for b, r in missing)) if missing else ""))
    return info


def bake(name, doc, disc, rows, info):
    """the model as the game makes it: the motions recipe, then the poses' (ttyd_anim.check_model); None or why"""
    import anim_recipe
    import ttyd_anim as T
    p, model = PARTNERS[name]
    motions = json.load(open(os.path.join(REPO, "port", "assets", "luigi", "pm64_%s_recipe.json" % name),
                             encoding="utf-8"))
    recipe, _table = recipe_of(doc, rows, info)
    anim_recipe.LOADER = disc.read
    for an, spec in recipe["anims"].items():
        n = sum(len(ch) for ch in (spec.get("add") or {}).values())
        if n > 256:
            return "%s: %d moving channels (joint x r/t/s axis); the game's maker takes 256" % (an, n)
    try:
        one = anim_recipe.make_pick(disc.read(model), motions, model + "_pc")
        two = anim_recipe.make_pick(one, recipe, model + "_pc")
    except (ValueError, KeyError) as e:
        return "baking: %s" % e
    ok, why = T.check_model(two, lambda n: n in recipe["anims"] or n in motions["anims"])
    return None if ok else "the baked model fails its check: %s" % why


# ------------------------------------------------------------------ C tables ----

def _cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ") + '"'


def inc_text(name, doc, rows, info):
    import anim_recipe
    cap = name.capitalize()
    src = "port/assets/luigi/pm64_%s_poses.json" % name
    head = ["/* Made by tools/port/pm64_poses.py write from %s: don't edit. Our numbers only: the poses are" % (
        src if doc else "(no poses file yet)"),
        " * baked onto the disc's model at startup (pm64_models_pc.c). */", ""]
    if doc is None:
        return "\n".join(head + [
            "static const Pm64PoseRow kPm64%sPoseRows[] = {{0, 0, \"\", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0}, 0, 0}};"
            % cap,
            "static const Pm64PoseEvent kPm64%sPoseEvents[] = {{0, \"\"}};" % cap,
            "static const Pm64PoseExtra kPm64%sPoseExtras[] = {{0, 0, 0, 0}};" % cap,
            "static const Pm64PosePart kPm64%sPoseParts[] = {{0}};" % cap,
            "static const Pm64PoseRule kPm64%sPoseRules[] = {{0}};" % cap,
            "static const Pm64Poses kPm64%sPoses = {NULL, kPm64%sPoseRows, 0, kPm64%sPoseEvents, kPm64%sPoseExtras, "
            "kPm64%sPoseParts, kPm64%sPoseRules};" % (cap, cap, cap, cap, cap, cap),
            ""])
    recipe, table = recipe_of(doc, rows, info)
    text = anim_recipe.cinc_text(recipe, "kPm64%sPosesRecipe" % cap, src)
    body = text.split("\n", 3)[3]  # (anim_recipe's own header off)
    out = head + [body.rstrip("\n"), ""]
    ev, ex, pa, ru = [], [], [], []
    out.append("static const Pm64PoseRow kPm64%sPoseRows[] = {" % cap)
    for battle, row, an, frames, loop, exact, sprite, events, extras, tint, parts, rules in table:
        out.append('    {%d, %d, "%s", %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, {%d, %d, %d, %d}, %d, %d},' % (
            battle, row, an, frames, int(loop), int(exact), int(sprite), len(ev), len(events), len(ex), len(extras),
            len(pa), len(parts), tint[0], tint[1], tint[2], tint[3], len(ru), len(rules)))
        ev += events
        ex += extras
        pa += parts
        ru += rules
    out.append("};")
    out.append("static const Pm64PoseEvent kPm64%sPoseEvents[] = {" % cap)
    out += ["    {%d, %s}," % (f, _cstr(w)) for f, w in ev] or ['    {0, ""},']
    out.append("};")
    out.append("static const Pm64PoseExtra kPm64%sPoseExtras[] = {" % cap)
    out += ["    {%d, %d, %d, %d}," % e for e in ex] or ["    {0, 0, 0, 0},"]
    out.append("};")
    out.append("static const Pm64PosePart kPm64%sPoseParts[] = {" % cap)

    def fl(x):
        return repr(float(x)) + "f"
    for f, tr, sh, ra, co, flg, pal, mask, edge, nn, rect, uv, at, px, z, rz, jo, rest, fm in pa:
        out.append("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, {%d, %d, %d, %d}, {%s}, {%s}, {%s}, %s, %s, %d, {%s}, %d}," % (
            f, tr, sh, ra, co, flg, pal, mask, edge, nn, rect[0], rect[1], rect[2], rect[3],
            ", ".join(fl(x) for x in uv), ", ".join(fl(x) for x in at), ", ".join(fl(x) for x in px), fl(z), fl(rz),
            jo, ", ".join("{%s}" % ", ".join(fl(x) for x in r3) for r3 in rest), fm))
    if not pa:
        out.append("    {0},")
    out.append("};")
    out.append("static const Pm64PoseRule kPm64%sPoseRules[] = {" % cap)
    for shape, when, act, box, ell, lo, hi, near, tol, rgb, gray, comp, pf, pt, pre in ru:
        nc = (near + [[0, 0, 0]] * 4)[:4]
        gr = (gray + [[0, 0]] * 8)[:8]
        out.append("    {%d, %d, %d, {%s}, {%s}, %d, %d, %d, %d, {%s}, {%d, %d, %d}, %d, {%s}, %d, %d, %d, %d}," % (
            shape, when, act, ", ".join(fl(x) for x in box), ", ".join(fl(x) for x in ell), lo, hi, len(near),
            min(tol, 255), ", ".join("{%d, %d, %d}" % tuple(c) for c in nc), rgb[0], rgb[1], rgb[2], len(gray),
            ", ".join("{%d, %d}" % tuple(g) for g in gr), comp, pf, pt, pre))
    if not ru:
        out.append("    {0},")
    out.append("};")
    out.append("static const Pm64Poses kPm64%sPoses = {&kPm64%sPosesRecipe, kPm64%sPoseRows, %d, kPm64%sPoseEvents, "
               "kPm64%sPoseExtras, kPm64%sPoseParts, kPm64%sPoseRules};" % (cap, cap, cap, len(table), cap, cap, cap, cap))
    return "\n".join(out) + "\n"


JSON_OVERRIDE = {}


def load(name):
    path = JSON_OVERRIDE.get(name) or poses_path(name)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def info_of(name, disc):
    if disc is None:
        return None
    return rig_info(disc.pose(PARTNERS[name][1]))


# ---------------------------------------------------------------- commands ----

def cmd_check(a):
    names = a.names or list(PARTNERS)
    if a.json:
        if len(a.names) != 1:
            raise SystemExit("--json takes one NAME")
        JSON_OVERRIDE[a.names[0]] = a.json
    rowsall = c_rows()
    dpath = None if a.no_disc else (a.disc or default_disc())
    disc = Disc(dpath) if dpath else None
    if disc is None and not a.no_disc:
        print("pm64_poses: no disc found (--disc DIR: the extracted files, or the .iso): joints not checked")
    bad = 0
    for name in names:
        if name not in PARTNERS:
            print("%s: not a partner (%s)" % (name, ", ".join(PARTNERS)))
            bad = 1
            continue
        rows = rowsall[PARTNERS[name][0]]
        rep = Report(name)
        try:
            doc = load(name)
        except ValueError as e:
            print("%s: %s isn't JSON: %s" % (name, poses_path(name), e))
            bad = 1
            continue
        if doc is None:
            if a.names:
                print("%s: no %s yet" % (name, poses_path(name)))
                bad = 1
            want = inc_text(name, None, rows, None)
        else:
            info = check_doc(name, doc, rows, disc, rep)
            if not rep.errors and disc is not None:
                why = bake(name, doc, disc, rows, info)
                if why:
                    rep.err(why)
            want = inc_text(name, doc, rows, info) if not rep.errors and info is not None else None
        if want is not None and disc is None:
            rep.note("no disc: the .inc not compared")
        elif want is not None and not a.no_inc and not a.json:
            old = open(inc_path(name), encoding="utf-8").read() if os.path.isfile(inc_path(name)) else None
            if old != want:
                rep.err("%s out of date: run pm64_poses.py write %s" % (inc_path(name), name))
        if doc is None and not rep.errors:
            continue
        print("%s: %s" % (name, "ok" if not rep.errors else "%d error(s)" % len(rep.errors)))
        for e in rep.errors:
            print("  error: " + e)
        if not a.quiet:
            for n in rep.notes:
                print("  note: " + n)
        bad |= bool(rep.errors)
    return 1 if bad else 0


def cmd_write(a):
    names = a.names or list(PARTNERS)
    rowsall = c_rows()
    dpath = a.disc or default_disc()
    disc = Disc(dpath) if dpath else None
    bad = 0
    for name in names:
        rows = rowsall[PARTNERS[name][0]]
        doc = load(name)
        info = None
        if doc is not None:
            rep = Report(name)
            info = check_doc(name, doc, rows, disc, rep)
            if rep.errors:
                print("%s: not written, %d error(s) (pm64_poses.py check %s)" % (name, len(rep.errors), name))
                for e in rep.errors:
                    print("  error: " + e)
                bad = 1
                continue
            if info is None:
                raise SystemExit("pm64_poses write: needs the disc (--disc) for the rig's top joint")
        text = inc_text(name, doc, rows, info)
        old = open(inc_path(name), encoding="utf-8").read() if os.path.isfile(inc_path(name)) else None
        if old != text:
            open(inc_path(name), "w", encoding="utf-8", newline="\n").write(text)
            print("wrote " + inc_path(name))
    return bad


def cmd_rows(a):
    p = PARTNERS[a.name][0]
    rows = c_rows()[p]
    for (battle, row), why in sorted(rows["played"].items()):
        print("%s%-3d %s" % ("B" if battle else "F", row, "; ".join(sorted(set(why)))))
    return 0


def cmd_rig(a):
    disc = Disc(a.disc or default_disc())
    r, tops, raw = rig(disc.pose(PARTNERS[a.name][1]))
    for i, (n, par) in enumerate(r):
        print("%2d %-12s parent %s%s" % (i, raw[i], r[par][0] if par >= 0 else "-", "  (root)" if n in tops else ""))
    print(json.dumps([n for n, _p in r]))
    return 0


SPRITE_OF = {"goombario": 0, "kooper": 1, "bombette": 2, "parakarry": 3, "bow": 4, "watt": 5, "sushie": 6,
             "lakilester": 7}  # (the ROM's NPC sprite numbers of the field sprites; battle: 8 on: kWho)


def _step_key(keys, frame, loop):
    """the key in force at frame: [(f, ...)] sorted; before the first, a loop's last"""
    k = None
    for q in keys:
        if q[0] <= frame:
            k = q
    if k is None and loop and keys:
        k = keys[-1]
    return k


def preview_tris(pose, images, info, doc, a, anim, frame, loop, spr, state, scale, ox, oy):
    """the posed model at frame as the game draws it (pm64_models_pc.c lookStep): its triangles for char_sheet's
    rasteriser, with the row's texture frames, shown/hidden joints, plane parts and quads (spr, state: the sprite
    and its components at that tick)"""
    import char_sheet as CS
    import ttyd_anim as T
    ev = T.Evaluator(pose)
    geo = T.Geometry(pose)
    node, vis, pos, tex = ev.at_frame(anim, frame)
    vis, tex = list(vis), [list(t) for t in tex]
    ex = extras_of(a, info)
    tracks = {}
    for f, kind, target, v in ex:
        tracks.setdefault((kind, target), []).append((f, v))
    for (kind, target), ks in tracks.items():
        k = _step_key(sorted(ks), frame, loop)
        if k is None:
            continue
        if kind == EXTRA_TEX and target < len(tex):
            tex[target][0] = (ev.base_tex[target][0] + k[1]) & 0xFF
        elif kind == EXTRA_SHOW and target < len(vis):
            vis[target] = k[1]
    # the sprite parts in force
    planes, quads = {}, []
    by = {}
    for q in parts_of(a, info):
        by.setdefault(q[1], []).append(q)
    for _t, ks in by.items():
        k = _step_key(ks, frame, loop)
        if k is None or (k[5] & PART_OFF) or spr is None:
            continue
        f, tr, sh, ra, co, flg, pal, mask, edge, nn, rect, uv, at, px, z, rz = k[:16]
        im = None
        if ra < 0:
            if co >= len(state):
                continue
            ra = state[co][0]
            if pal == -2:
                pal = state[co][1]
        if ra < 0:
            continue
        pal = -1 if pal < 0 else pal
        bits = lambda m: [i for i in range(16) if (m >> i) & 1]  # noqa: E731
        im = spr.cut(ra, pal, rect if rect[2] > 0 else None, bits(mask), bits(edge), bits(nn))
        if sh >= 0:
            planes[sh] = (im, uv, bool(flg & PART_FLIP))
        else:
            fol = None
            if k[16] >= 0:
                wm = ev.world_matrices(node, [1] * 4096)[k[16]]
                if wm is not None:
                    rest = k[17]
                    fol = [[sum(wm[r][q] * (rest[q][c] if q < 3 else (1.0 if c == 3 else 0.0)) for q in range(4))
                            for c in range(4)] for r in range(3)]
                    if k[18]:
                        # (place only, or place and size: the model's axes, moved with the quad's middle)
                        w0_, h0_ = im[0] * px[0], im[1] * px[1]
                        c0 = (at[0] + 0.5 * w0_, at[1] - 0.5 * h0_, z)
                        cw = CS._mulv(fol, *c0)
                        sc = [math.sqrt(sum(fol[r][ax] ** 2 for r in range(3))) for ax in range(3)] if k[18] == 2 \
                            else [1.0, 1.0, 1.0]
                        fol = [[sc[0] if r == 0 and c == 0 else sc[1] if r == 1 and c == 1 else
                                sc[2] if r == 2 and c == 2 else 0.0 for c in range(3)] +
                               [cw[r] - sc[r] * c0[r]] for r in range(3)]
            w0, h0 = im[0] * px[0], im[1] * px[1]
            cx, cy = at[0] + 0.5 * w0, at[1] - 0.5 * h0
            c, s = math.cos(math.radians(rz)), math.sin(math.radians(rz))
            corners = []
            for (dx, dy), (u, v) in zip(((-0.5, 0.5), (0.5, 0.5), (0.5, -0.5), (-0.5, -0.5)),
                                        ((0, 0), (1, 0), (1, 1), (0, 1))):
                x, y = dx * w0, dy * h0
                if flg & PART_FLIP:
                    u = 1 - u
                qx, qy, qz = cx + c * x - s * y, cy + s * x + c * y, z
                if fol is not None:
                    qx, qy, qz = CS._mulv(fol, qx, qy, qz)
                corners.append((ox + qx * scale, oy - qy * scale, qz, (min(u, 0.9999), min(v, 0.9999)),
                                (255, 255, 255, 255)))
            quads.append((z, corners, im))
    mats = ev.world_matrices(node, vis)
    rules = rules_of(doc, a, info)
    tris = []
    for gi, g in enumerate(pose.groups()):
        if g.shape < 0 or mats[gi] is None:
            continue
        m = mats[gi]
        for di, fan in geo.shape_polys(g.shape):
            dr = geo.draws[di]
            im, ts, tt, sub = None, 0.0, 0.0, None
            if g.shape in planes:
                im, sub, flip = planes[g.shape]
            elif dr["tex_count"]:
                slot = dr["tex_data"][0]
                if 0 <= slot < len(geo.texdata):
                    b = geo.texdata[slot][0] + tex[slot][0]
                    if 0 <= b < len(geo.texbinds):
                        ti = geo.texbinds[b][0]
                        im = images[ti] if ti < len(images) else None
                        if im is not None and rules:
                            im = _recolored(im, rules, g.shape, spr, state)
                    ts, tt = tex[slot][1], tex[slot][2]
            pts = []
            for pi, c, uv in fan:
                x, y, z = CS._mulv(m, *pos[pi])
                if uv is not None and sub is not None:
                    u = (uv[0] - sub[0]) / ((sub[2] - sub[0]) or 1.0)
                    v = (uv[1] - sub[1]) / ((sub[3] - sub[1]) or 1.0)
                    uv = (min(max(1 - u if flip else u, 0.0), 0.9999), min(max(v, 0.0), 0.9999))
                elif uv is not None:
                    uv = (uv[0] + ts, uv[1] + tt)
                pts.append((ox + x * scale, oy - y * scale, z, uv, c))
            for k in range(1, len(pts) - 1):
                tri = (pts[0], pts[k], pts[k + 1])
                tris.append((sum(p[2] for p in tri) / 3.0, gi, tri, im))
    for z, cs, im in quads:
        for tri in ((cs[0], cs[1], cs[2]), (cs[0], cs[2], cs[3])):
            tris.append((z, 1000, tri, im))
    tris.sort(key=lambda t: (t[0], t[1]))
    return tris


_RECOLORED = {}


def _luma(c):
    return int(0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2] + 0.5)


def _recolored(im, rules, shape, spr, state):
    """a model texture through the row's rules, as pm64_models_pc.c recolored() does"""
    mine = [r for r in rules if r[0] < 0 or r[0] == shape]
    if not mine:
        return im
    pals = None
    for r in mine:
        if r[2] == ACT_PALETTE and spr is not None and r[11] < len(state) and state[r[11]][0] >= 0:
            own = spr.raster(state[r[11]][0])[3]
            fp = r[12] if r[12] >= 0 else own
            tp = r[13] if r[13] >= 0 else (state[r[11]][1] if state[r[11]][1] >= 0 else own)
            pals = (spr.colours(fp), spr.colours(tp))
            break
    key = (id(im), shape, tuple(map(repr, mine)), repr(pals))
    if key in _RECOLORED:
        return _RECOLORED[key]
    w, h, rows = im
    hist = {}
    for row in rows:
        for c in row:
            if len(c) < 4 or c[3] >= 128:
                q = (c[0] >> 3, c[1] >> 3, c[2] >> 3)
                hist[q] = hist.get(q, 0) + 1
    bq = max(hist, key=hist.get) if hist else (0, 0, 0)
    base = tuple(v * 255 // 31 for v in bq)
    out = []
    for y, row in enumerate(rows):
        orow = []
        for x, c in enumerate(row):
            if len(c) > 3 and c[3] == 0:
                orow.append(c)
                continue
            fx, fy = x + 0.5, y + 0.5
            for pas in (1, 0):
                if len(c) > 3 and c[3] == 0:
                    break
                c = _apply_rules([r for r in mine if r[14] == pas], c, fx, fy, base, pals)
            orow.append(c)
        out.append(orow)
    _RECOLORED[key] = (w, h, out)
    return _RECOLORED[key]


def _apply_rules(mine, c, fx, fy, base, pals):
    """the first of these rules a texel matches, applied (pm64_models_pc.c recolored's pass)"""
    nc = c
    if True:
        if True:
            for (sh, when, act, box, ell, lo, hi, near, tol, rgb, gray, comp, pf, pt, _pre) in mine:
                if when & RULE_RECT and not (box[0] <= fx < box[0] + box[2] and box[1] <= fy < box[1] + box[3]):
                    continue
                if when & (RULE_IN_ELLIPSE | RULE_OUT_ELLIPSE):
                    dx = (fx - ell[0]) / (ell[2] or 1.0)
                    dy = (fy - ell[1]) / (ell[3] or 1.0)
                    inside = dx * dx + dy * dy <= 1.0
                    if (when & RULE_IN_ELLIPSE and not inside) or (when & RULE_OUT_ELLIPSE and inside):
                        continue
                if when & RULE_LUMA and not lo <= _luma(c) <= hi:
                    continue
                if when & RULE_NEAR and not any(sum((c[i] - n[i]) ** 2 for i in range(3)) <= tol * tol for n in near):
                    continue
                if act == ACT_RGB:
                    nc = tuple(rgb) + (255,)
                elif act == ACT_CLEAR:
                    nc = (0, 0, 0, 0)
                elif act == ACT_BASE:
                    nc = base + (255,)
                elif act == ACT_GRAY:
                    l = _luma(c)
                    g = gray[0][1]
                    for q in range(len(gray)):
                        if l <= gray[q][0]:
                            g = gray[0][1] if q == 0 else gray[q - 1][1] + (gray[q][1] - gray[q - 1][1]) * (
                                l - gray[q - 1][0]) // ((gray[q][0] - gray[q - 1][0]) or 1)
                            break
                        g = gray[q][1]
                    g = max(0, min(255, g))
                    nc = (g, g, g, 255)
                elif act == ACT_PALETTE and pals is not None:
                    fr, to = pals
                    best = min((q for q in range(16) if fr[q][3]),
                               key=lambda q: sum((c[i] - fr[q][i]) ** 2 for i in range(3)), default=None)
                    if best is not None:
                        nc = tuple(to[best][:3]) + (255,)
                break
    return nc


def sprite_tris(spr, state, upx, scale, ox, oy):
    """the sprite's components at that tick (as pm64_partner_pc.c drawParts lays them: each raster -w/2..w/2 across,
    0..h up about its point, turned about z, the later ones in front), upx model units a sprite pixel"""
    tris = []
    for i, (ra, pal, x, y, _z, r, s) in enumerate(state):
        if ra < 0:
            continue
        im = spr.rgba(ra, pal)
        w, h = im[0], im[1]
        c, sn = math.cos(math.radians(r[2])), math.sin(math.radians(r[2]))
        pts = []
        for (qx, qy), uv in zip(((-(w // 2), h), (w - w // 2, h), (w - w // 2, 0), (-(w // 2), 0)),
                                ((0, 0), (0.9999, 0), (0.9999, 0.9999), (0, 0.9999))):
            qx, qy = qx * s[0], qy * s[1]
            wx, wy = x + c * qx - sn * qy, y + sn * qx + c * qy
            pts.append((ox + wx * upx * scale, oy - wy * upx * scale, i, uv, (255, 255, 255, 255)))
        tris += [(i, i, (pts[0], pts[1], pts[2]), im), (i, i, (pts[0], pts[2], pts[3]), im)]
    return tris


def cmd_sheet(a):
    import anim_recipe
    import char_sheet as CS
    import pm64_sprite
    import ttyd_anim as T
    disc = Disc(a.disc or default_disc())
    name = a.name
    model = PARTNERS[name][1]
    doc = load(name)
    if doc is None:
        raise SystemExit("no %s" % poses_path(name))
    rows = c_rows()[PARTNERS[name][0]]
    info = rig_info(disc.pose(model))
    recipe, table = recipe_of(doc, rows, info)
    anim_recipe.LOADER = disc.read
    want = {row_of(r.strip(), rows) for r in a.rows.split(",")} if a.rows else None
    keys = {row_of(k, rows): k for k in doc["anims"]}
    picked = [t for t in table if want is None or (t[0], t[1]) in want]
    if not picked:
        raise SystemExit("no rows to draw")
    sub = {"model": recipe["model"], "anims": {t[2]: recipe["anims"][t[2]] for t in picked}}
    pose = T.PoseFile.parse(anim_recipe.make_pick(disc.read(model), sub, model + "_pc"))
    images = [(w, h, rr) for w, h, rr, _s, _t in T.tpl_images(disc.read(model + "-"))]
    sprites = {}
    if not a.no_sprite:
        try:
            for b in (0, 1):
                sprites[b] = pm64_sprite.Sprite(SPRITE_OF[name] + 8 * b, a.rom)
        except SystemExit as e:
            print("%s: the sprites aren't drawn" % e)
            sprites = {}
    scale = a.scale
    lo = pose.bbox
    half = max(abs(lo[0]), abs(lo[3]), 12.0) + 6.0
    cw = int(half * 2 * scale) + 4
    ch = int((max(lo[4], 20.0) + 14.0) * scale) + 4
    cells = []
    for battle, row, an, frames, loop, *_rest in picked:
        at = ([int(x) for x in a.at.split(",")] if a.at and a.at != "all" else list(range(frames + (0 if loop else 1)))
              if a.at == "all" else sorted({round(frames * i / 8.0) for i in range(8)}))
        spr = sprites.get(battle)
        states = pm64_sprite.play(spr, row, max(at) + 1) if spr is not None and row < len(spr.anims) else None
        cells.append((battle, row, an, loop, at, spr, states, doc["anims"][keys[(battle, row)]]))
    cols = max(len(c[4]) for c in cells) * (2 if sprites else 1)
    W, H = cw * cols, ch * len(cells)
    img = [[[250, 250, 250, 255] if ((x // cw) % 2 == 0) else [226, 228, 236, 255] for x in range(W)] for y in range(H)]
    for r, (battle, row, an, loop, at, spr, states, arow) in enumerate(cells):
        upx0, follow = 0.5, None
        for q in parts_of(arow, info):
            if q[2] < 0:
                upx0 = q[13][0]
                follow = (q[16], q[17]) if q[16] >= 0 else None
                break
        for i, f in enumerate(at):
            st = states[f] if states else []
            upx = upx0
            if follow is not None:
                # (the quads follow a joint: the sprite drawn at that joint's size this frame, as they are)
                ev = T.Evaluator(pose)
                node, _v, _p, _t = ev.at_frame(pose.anim(an), float(f))
                wm = ev.world_matrices(node, [1] * 4096)[follow[0]]
                if wm is not None:
                    rest = follow[1]
                    d0 = [sum(wm[r][q] * rest[q][0] for q in range(3)) for r in range(3)]
                    upx *= math.sqrt(sum(v * v for v in d0))
            ox, oy = (i * (2 if sprites else 1)) * cw + cw / 2.0, r * ch + ch - 6 * scale
            for _z, _g, tri, im in preview_tris(pose, images, info, doc, arow, pose.anim(an), float(f), loop, spr, st,
                                                 scale, ox, oy):
                CS._raster(img, W, H, tri, im)
            if sprites and states:
                for _z, _g, tri, im in sprite_tris(spr, st, upx, scale, ox + cw, oy):
                    CS._raster(img, W, H, tri, im)
        print("%s%d (%s): frames %s" % ("B" if battle else "F", row, an, ",".join(str(f) for f in at)))
    T.write_png(a.out, [[tuple(p) for p in rr] for rr in img])
    print("model | sprite pairs, a line a row: %s" % a.out)
    return 0


# ------------------------------------------------------- the gallery's check ----

KEY = (255, 0, 255)  # the gallery's sheet (pm64_partner_pc.c galleryBackdrop)
MARK = (0, 255, 255)  # its anchor marks, MARK_BELOW model units under each one's feet
MARK_BELOW = 4.0
APART = 68.0  # model units from the model's anchor to the sprite's


def _read_png(path):
    import zlib
    import struct
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype not in (2, 6):
                raise ValueError("only 8-bit RGB/RGBA PNGs")
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _y in range(h):
        ft = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def _near(px, col, tol):
    return abs(px[0] - col[0]) + abs(px[1] - col[1]) + abs(px[2] - col[2]) <= tol


def _blobs(w, h, bpp, rows, col, tol):
    """centres of the pixels near col, split in two by x (left, right)"""
    pts = []
    for y in range(h):
        r = rows[y]
        for x in range(w):
            o = x * bpp
            if _near(r[o:o + 3], col, tol):
                pts.append((x, y))
    if len(pts) < 2:
        return None
    xs = sorted(p[0] for p in pts)
    cut = (xs[0] + xs[-1]) / 2.0
    left = [p for p in pts if p[0] < cut]
    right = [p for p in pts if p[0] >= cut]
    if not left or not right:
        return None
    c = lambda q: (sum(p[0] for p in q) / len(q), sum(p[1] for p in q) / len(q))  # noqa: E731
    return c(left), c(right)


def _mask_stats(pts, ax, ay, k):
    """pts: pixels (x, y); anchored at (ax, ay), k px a unit: (set of unit-grid cells, centroid, extents)"""
    if not pts:
        return set(), (0.0, 0.0), (0.0, 0.0, 0.0, 0.0)
    us = [((x + 0.5 - ax) / k, (ay - y - 0.5) / k) for x, y in pts]
    cells = {(int(math.floor(u * 2)), int(math.floor(v * 2))) for u, v in us}  # (half-unit cells)
    cx = sum(u for u, _v in us) / len(us)
    cy = sum(v for _u, v in us) / len(us)
    return cells, (cx, cy), (min(u for u, _ in us), max(u for u, _ in us), min(v for _, v in us),
                              max(v for _, v in us))


def diff_shot(path, tol_key=90, tol_mark=60):
    w, h, bpp, rows = _read_png(path)
    marks = _blobs(w, h, bpp, rows, MARK, tol_mark)
    if marks is None:
        return None, "no anchor marks"
    (lx, ly), (rx, ry) = marks
    k = math.hypot(rx - lx, ry - ly) / APART
    if k <= 0:
        return None, "anchor marks on top of each other"
    # the feet: MARK_BELOW units over each mark
    la = (lx, ly - MARK_BELOW * k)
    ra = (rx, ry - MARK_BELOW * k)
    mid = (lx + rx) / 2.0
    left, right = [], []
    # (only the band the sheet covers: from the marks' rows up, half the gap either side of each anchor)
    y0 = max(0, int(min(ly, ry) - 60 * k))
    y1 = min(h, int(max(ly, ry)))
    x0 = max(0, int(lx - APART * 0.5 * k))
    x1 = min(w, int(rx + APART * 0.5 * k) + 1)
    for y in range(y0, y1):
        r = rows[y]
        for x in range(x0, x1):
            o = x * bpp
            px = r[o:o + 3]
            if _near(px, KEY, tol_key) or _near(px, MARK, tol_mark):
                continue
            (left if x < mid else right).append((x, y))
    mc, mcen, mext = _mask_stats(left, la[0], la[1], k)
    sc, scen, sext = _mask_stats(right, ra[0], ra[1], k)
    union = len(mc | sc)
    iou = len(mc & sc) / union if union else 1.0
    return {
        "iou": round(iou, 4),
        "dcx": round(mcen[0] - scen[0], 3), "dcy": round(mcen[1] - scen[1], 3),
        "dleft": round(mext[0] - sext[0], 3), "dright": round(mext[1] - sext[1], 3),
        "dbottom": round(mext[2] - sext[2], 3), "dtop": round(mext[3] - sext[3], 3),
        "model_px": len(left), "sprite_px": len(right), "px_per_unit": round(k, 3),
    }, None


def cmd_diff(a):
    shots = {}
    for path in glob.glob(os.path.join(a.rundir, "**", "gal_*.png"), recursive=True):
        m = re.match(r"gal_(\d+)_([FB]\d+)_f(\d+)\.png$", os.path.basename(path))
        if m:
            shots.setdefault(int(m.group(1)), []).append((m.group(2), int(m.group(3)), path))
    if not shots:
        print("pm64_poses diff: no gal_<p>_<F|B><row>_f<frame>.png under %s" % a.rundir)
        return 1
    byp = {p: n for n, (p, _m) in PARTNERS.items()}
    for p in sorted(shots):
        name = byp.get(p, str(p))
        doc = load(name) if name in PARTNERS else None
        exact_mark = {}
        if doc:
            rows = c_rows()[p]
            for key, an in doc["anims"].items():
                try:
                    b, r = row_of(key, rows)
                    exact_mark["%s%d" % ("B" if b else "F", r)] = an.get("exact", True)
                except ValueError:
                    pass
        out = os.path.join(a.rundir, "pm64poses_%s.csv" % name)
        summary = {}
        with open(out, "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f)
            wr.writerow(["row", "frame", "iou", "dcx", "dcy", "dleft", "dright", "dbottom", "dtop", "model_px",
                         "sprite_px", "px_per_unit", "flag", "file"])
            for row, frame, path in sorted(shots[p], key=lambda s: (s[0][0], int(s[0][1:]), s[1])):
                res, why = diff_shot(path)
                if res is None:
                    wr.writerow([row, frame] + [""] * 10 + ["unreadable: " + why, os.path.basename(path)])
                    summary.setdefault(row, []).append(None)
                    continue
                flag = []
                if res["iou"] < a.iou:
                    flag.append("iou")
                if math.hypot(res["dcx"], res["dcy"]) > a.centre:
                    flag.append("centre")
                if max(abs(res[k]) for k in ("dleft", "dright", "dbottom", "dtop")) > a.extent:
                    flag.append("extent")
                wr.writerow([row, frame] + [res[k] for k in ("iou", "dcx", "dcy", "dleft", "dright", "dbottom",
                                                             "dtop", "model_px", "sprite_px", "px_per_unit")]
                            + ["+".join(flag), os.path.basename(path)])
                summary.setdefault(row, []).append((res["iou"], bool(flag)))
        print("%s: %s" % (name, out))
        for row in sorted(summary, key=lambda r: (r[0], int(r[1:]))):
            got = [s for s in summary[row] if s is not None]
            bad = sum(1 for s in got if s[1])
            worst = min((s[0] for s in got), default=0.0)
            mark = exact_mark.get(row)
            print("  %-4s %3d frames  worst IoU %.3f  %s%s" % (
                row, len(summary[row]), worst, "EXACT" if got and bad == 0 else "not exact (%d frames)" % bad
                if got else "unreadable", "" if mark is None else "  (marked %s)" % ("exact" if mark else "not exact")))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("check")
    c.add_argument("names", nargs="*")
    c.add_argument("--disc")
    c.add_argument("--no-disc", action="store_true", help="skip the rig and bake checks")
    c.add_argument("--no-inc", action="store_true", help="don't require the .inc to be up to date")
    c.add_argument("--quiet", action="store_true", help="errors only")
    c.add_argument("--json", help="check this file as NAME's poses (a draft elsewhere)")
    w = sub.add_parser("write")
    w.add_argument("names", nargs="*")
    w.add_argument("--disc")
    r = sub.add_parser("rows")
    r.add_argument("name", choices=list(PARTNERS))
    g = sub.add_parser("rig")
    g.add_argument("name", choices=list(PARTNERS))
    g.add_argument("--disc")
    s = sub.add_parser("sheet")
    s.add_argument("name", choices=list(PARTNERS))
    s.add_argument("out")
    s.add_argument("--rows")
    s.add_argument("--at")
    s.add_argument("--scale", type=float, default=4.0)
    s.add_argument("--disc")
    s.add_argument("--rom", help="the PM64 ROM (else $TTYD_PM64_ROM, else a *.z64 beside the checkout)")
    s.add_argument("--no-sprite", action="store_true", help="the model alone")
    d = sub.add_parser("diff")
    d.add_argument("rundir")
    d.add_argument("--iou", type=float, default=0.85)
    d.add_argument("--centre", type=float, default=1.0)
    d.add_argument("--extent", type=float, default=1.5)
    a = ap.parse_args(argv)
    if a.cmd is None:
        print(__doc__)
        return 2
    return {"check": cmd_check, "write": cmd_write, "rows": cmd_rows, "rig": cmd_rig, "sheet": cmd_sheet,
            "diff": cmd_diff}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
