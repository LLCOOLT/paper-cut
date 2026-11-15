#!/usr/bin/env python3
"""New animations for any model, made from its own disc animations by a
recipe (docs/LUIGI_ANIMS.md "Joints and vertex morphs"; Play as's picks,
docs/PLAY_AS.md). Pure Python on ttyd_anim; no Blender.

A recipe is our own work, names and numbers only: which of the model's disc
animations each new one is made from, its new length and loop, and offsets of
our own on top. The animations it makes come from the player's disc (they
carry its joints' keys and its vertex morphs), so they are made where the disc
is, never committed.

  {"model": "c_pkr",
   "anims": {
     "M_W_1": {"from": "PKR_W_1", "end": 40, "loop": true},
     "M_J_1A": {"from": "PKR_J_1A", "end": 6, "loop": false, "range": [0, 6],
                "add": {"|locator6": {"ty": [[0, 0], [6, 4, "linear"]]}}}}}

  from    the disc animation (every channel of it: node values, the joints'
          rotations, vertex morphs, visibility, texture drawings)
  range   the part of it played, frames (default: its loop range)
  end     the new length; loop: whether it loops. A loop plays the part
          `turns` times (default: as many as fit, at least once), the keys'
          times scaled to fit; a one-shot plays it once, or `turns` times
          when it says so.
  from_model  (optional) another model whose `from` animation it is: the same rig (Sushie from c_puku), its
          keys put on this model's rest
  add     tracks of our own by group: tx ty tz (game units) and rx ry rz
          (degrees) added, sx sy sz multiplied; [frame, value, ease]: "auto"
          (smooth), "linear" or "step"

Retarget by kind: every animation of Mario's (anim_parity.py's 196) from the
model's disc animation of its kind (luigi_pc.c plainMove: stand, walk, run,
jump up and down, talk, hurt, attack), fitted to Mario's length and loop,
where the recipe doesn't give one.

  anim_recipe.py make SRC RECIPE.json --out FILE [--retarget] [--export DIR]
      the recipe's model from SRC (a disc image or a folder of a/ files) with
      the new animations added (an animation of the same name replaced),
      written to FILE and checked (ttyd_anim.check_model, strict); with
      --retarget, Mario's every animation too; --export DIR: also as a
      character_tool folder, to edit on in Blender
  anim_recipe.py pick SRC RECIPE.json --name NAME --out FILE
      the model as Play as makes it at startup (anim_recipe_pc.c): the
      recipe's animations added, named NAME; byte for byte the game's
  anim_recipe.py cinc RECIPE.json --symbol SYM --out FILE.inc [--check]
      the recipe as C tables for the game's maker (port/include/port/anim_recipe.h)
  anim_recipe.py kinds SRC MODEL
      the model's plain move of each kind (what a retarget uses)
  anim_recipe.py selftest
"""
import argparse
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402

# a channel of our offsets: (node field, how: "add" in node units per unit, or "mul")
ADD_CHANNELS = {"tx": (0, "add", 1.0), "ty": (1, "add", 1.0), "tz": (2, "add", 1.0),
                "sx": (3, "mul", 1.0), "sy": (4, "mul", 1.0), "sz": (5, "mul", 1.0),
                "rx": (6, "add", 0.5), "ry": (7, "add", 0.5), "rz": (8, "add", 0.5)}  # (r1: half degrees)

# where a recipe's "from_model" is read from: a function (model name) -> its bytes, set by the tool using it
LOADER = None

SLOTS = ("idle", "stand", "walk", "run", "talk", "up", "down", "hurt", "attack")


# ----------------------------------------------------------------- kinds ----

def mario_slot(name):
    """luigi_pc.c plainMove: the plain move of Mario's animation's kind"""
    if name in ("M_Z_1", "M_Z_1R"):
        return "idle"
    if name.startswith("M_W_"):
        return "walk"
    if name.startswith("M_R_"):
        return "run"
    if name.startswith(("M_J_1A", "M_J_5A", "M_A_3A")):
        return "up"
    if name.startswith(("M_J_", "M_A_", "M_Y_")):
        return "down"
    if name.startswith(("M_D_", "M_U_3", "M_Z_")):
        return "hurt"
    if name.startswith(("M_I_2", "M_I_3", "M_N_B")):
        return "talk"
    if name.startswith("M_H_"):
        return "attack"
    return "stand"


def _kind(name):
    """a disc animation's kind letter(s) and number: PKR_W_1 -> ("W", "1"), PKR_J_1A -> ("J", "1A")"""
    parts = name.split("_")
    if len(parts) < 3:
        return None, None
    return parts[-2], parts[-1]


def kinds(pose):
    """{slot: the model's disc animation for it} (the first of each kind, by name; a slot it has nothing
    for falls back to its stand)"""
    anims = [a for a in pose.anims if a.keys]
    by = {}
    for a in anims:
        k, n = _kind(a.name)
        if k is not None:
            by.setdefault(k, []).append((n, a))
    for k in by:
        by[k].sort(key=lambda x: x[0])

    def first(letter, loop=None, suffix=None):
        for n, a in by.get(letter, []):
            if suffix is not None and not n.endswith(suffix):
                continue
            if loop is None or a.loop == loop:
                return a.name
        return None
    out = {}
    out["stand"] = first("S", True) or first("S") or (anims[0].name if anims else None)
    out["idle"] = first("Z") or out["stand"]
    out["walk"] = first("W", True) or first("W") or out["stand"]
    out["run"] = first("R", True) or first("R") or out["walk"]
    out["talk"] = first("T", True) or first("T") or out["stand"]
    out["up"] = first("J", None, "A") or first("J") or out["stand"]
    out["down"] = first("J", None, "B") or first("J") or out["stand"]
    out["hurt"] = first("D") or out["stand"]
    out["attack"] = first("A", False) or first("A") or out["stand"]
    return out


# --------------------------------------------------------- source curves ----

class Curves:
    """an animation's every channel as functions of time, as the game plays it"""

    def __init__(self, pose, anim):
        self.pose = pose
        self.anim = anim
        self.ev = T.Evaluator(pose)
        self.times = [k.time for k in anim.keys]
        self.at = [self.ev.at_key(anim, k) for k in range(len(anim.keys))]
        self.floats = sorted({i for k in anim.keys for i in k.node})
        # per segment (into key k+1) and float: (start slope, change, end slope) or None for a step
        self.seg = []
        for k in range(1, len(anim.keys)):
            d = {}
            for i, nk in anim.keys[k].node.items():
                sa = sb = 0.0
                step = True
                for v, a, b in nk.chunks():
                    if -89 < b < 89:
                        step = False
                        sa += T._tan(a) if -89 < a < 89 else 0.0
                        sb += T._tan(b)
                d[i] = None if step else (sa, nk.n / 16.0, sb)
            self.seg.append(d)

    def segment(self, t):
        """(key k at or before t, u in the segment to k+1 or None when held)"""
        k = 0
        while k + 1 < len(self.times) and self.times[k + 1] <= t:
            k += 1
        if k + 1 >= len(self.times) or self.times[k + 1] == self.times[k]:
            return k, None
        return k, (t - self.times[k]) / (self.times[k + 1] - self.times[k])

    def node(self, i, t, side=+1):
        """(value, slope per frame) of node float i at t; side: the slope after (+1) or before (-1) t"""
        k, u = self.segment(t)
        if side < 0 and k > 0 and t == self.times[k] and self.times[k - 1] < t:
            k, u = k - 1, 1.0  # (the slope coming into a key: the segment before it)
        base = self.at[k][0][i]
        if u is None:
            return base, 0.0
        s = self.seg[k].get(i)
        if s is None:
            return base, 0.0  # (no change here, or a step at the next key)
        dt = self.times[k + 1] - self.times[k]
        sa, dv, sb = s
        u2, u3 = u * u, u * u * u
        v = base + (u + u3 - 2 * u2) * dt * sa + (-2 * u3 + 3 * u2) * dv + (u3 - u2) * dt * sb
        slope = (1 - 4 * u + 3 * u2) * sa + (6 * u - 6 * u2) * dv / dt + (3 * u2 - 2 * u) * sb
        return v, slope

    def steps_into(self, i, t):
        """float i steps (jumps) at the key at t"""
        for k in range(1, len(self.times)):
            if self.times[k] == t:
                return self.seg[k - 1].get(i, 0) is None and i in self.anim.keys[k].node
        return False

    def morph(self, t):
        """every vertex's offset from rest at t, in sixteenths (linear between keys)"""
        k, u = self.segment(t)
        p0 = self.at[k][2]
        if u is None or u == 0.0:
            return p0
        p1 = self.at[k + 1][2]
        return [tuple(a[c] + u * (b[c] - a[c]) for c in range(3)) for a, b in zip(p0, p1)]

    def vis_tex(self, t):
        k, _u = self.segment(t)
        return self.at[k][1], self.at[k][3]


# ------------------------------------------------------------- overlays ----

def _track(pts):
    """[(frame, value, ease)] -> f(t) -> (value, slope): Hermite with auto slopes (Catmull-Rom),
    linear or step"""
    pts = [(float(p[0]), float(p[1]), p[2] if len(p) > 2 else "auto") for p in pts]
    pts.sort(key=lambda p: p[0])
    slopes = []
    for j, (t, v, e) in enumerate(pts):
        if 0 < j < len(pts) - 1 and e == "auto":
            slopes.append((pts[j + 1][1] - pts[j - 1][1]) / (pts[j + 1][0] - pts[j - 1][0]))
        else:
            slopes.append(0.0)

    def f(t, side=+1):
        """(value, slope) at t; the slope after t (side +1) or before it (-1)"""
        if t < pts[0][0] or (side < 0 and t == pts[0][0]):
            return pts[0][1], 0.0
        if t > pts[-1][0] or (side > 0 and t == pts[-1][0]):
            return pts[-1][1], 0.0
        # the segment [j, j + 1] holding t: t in [t_j, t_j+1) going on, (t_j, t_j+1] coming in
        j = 0
        while j + 2 < len(pts) and (pts[j + 1][0] < t or (side > 0 and pts[j + 1][0] == t)):
            j += 1
        t0, v0, e0 = pts[j]
        t1, v1, _e1 = pts[j + 1]
        dt = t1 - t0
        u = (t - t0) / dt
        if e0 == "step":
            return v0, 0.0
        if e0 == "linear":
            return v0 + u * (v1 - v0), (v1 - v0) / dt
        m0, m1 = slopes[j] * dt, slopes[j + 1] * dt
        u2, u3 = u * u, u * u * u
        v = (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * m1
        dv = ((6 * u2 - 6 * u) * v0 + (3 * u2 - 4 * u + 1) * m0 + (-6 * u2 + 6 * u) * v1 + (3 * u2 - 2 * u) * m1) / dt
        return v, dv
    f.times = [p[0] for p in pts]
    f.step_at = {pts[j + 1][0] for j in range(len(pts) - 1) if pts[j][2] == "step"}
    return f


# ----------------------------------------------------------------- make ----

def make(pose, name, spec):
    """the animation `name` a recipe entry makes (ttyd_anim.Anim)"""
    if spec.get("from_model"):
        # another model's animation of the same rig (Sushie from Cheep Cheep's c_puku): its keys on this model's
        # rest, as the game's maker does (anim_recipe_pc.c sLoader); LOADER(name) -> that model's bytes
        if LOADER is None:
            raise ValueError("%s: from_model %s, but no LOADER (anim_recipe.LOADER = disc reader)" % (
                name, spec["from_model"]))
        src = T.PoseFile.parse(LOADER(spec["from_model"])).anim(spec["from"])
    else:
        src = pose.anim(spec["from"])
    if not src.keys:
        raise ValueError("%s: %s has no keys" % (name, spec["from"]))
    cur = Curves(pose, src)
    a, b = (float(x) for x in spec.get("range", (src.start, src.end)))
    if b < a:
        raise ValueError("%s: its range ends before it starts" % name)
    end = float(spec.get("end", b - a))
    loop = bool(spec.get("loop", src.loop))
    span = b - a
    # (a one-shot plays the part once, or `turns` times when the recipe says: Mario's 60-frame talk is
    # four of a 14-frame loop's turns)
    if span > 0 and end > 0 and (loop or "turns" in spec):
        turns = int(spec.get("turns", max(1, int(round(end / span)))))
    else:
        turns = 1
    scale = (end / (span * turns)) if span > 0 else 0.0
    groups = {g.name.lstrip("|"): g for g in pose.groups()}
    add = {}
    for gname, chans in (spec.get("add") or {}).items():
        g = groups.get(gname.lstrip("|"))
        if g is None:
            raise ValueError("%s: the model has no group %s" % (name, gname))
        for c, pts in chans.items():
            if c not in ADD_CHANNELS:
                raise ValueError("%s: no channel %r (%s)" % (name, c, ", ".join(ADD_CHANNELS)))
            f, how, k = ADD_CHANNELS[c]
            add[(g.node + f, how)] = (_track(pts), k)
    # the new keys: each pass's source keys (and its ends), then our tracks' keys
    taus = {}

    def put(tau, sigma):
        tau = round(tau, 4)
        taus.setdefault(tau, sigma)
    for p in range(turns):
        put(p * span * scale, a)
        for t in cur.times:
            if a < t < b:
                put((p * span + t - a) * scale, t)
    put(turns * span * scale, b)
    for (_i, _how), (trk, _k) in add.items():
        for t in trk.times:
            if 0 <= t <= end:
                # the source time under it
                if span > 0 and scale > 0:
                    p = min(int(t / (span * scale)), turns - 1)
                    put(t, a + (t - p * span * scale) / scale)
                else:
                    put(t, a)
    order = sorted(taus)
    sig = [taus[t] for t in order]
    # a pass's end is the next pass's start: there, the next pass's (the loop joined at the turn)
    for j in range(1, len(order) - 1):
        if span > 0 and abs(order[j] / (span * scale) - round(order[j] / (span * scale))) < 1e-9 and scale > 0:
            sig[j] = a
    base = pose.node_values()
    floats = sorted(set(cur.floats) | {i for (i, _h) in add})
    out = T.Anim(name)
    out.loop_info = [(1 if loop else 0, 0.0, end)]
    out.value_keys = [(0.0, 0)]
    out.exact = False
    out.keys = [T.Key(t) for t in order]
    inv = (1.0 / scale) if scale > 0 else 0.0
    for i in floats:
        vals, right, left, steps = [], [], [], []
        for j, (tau, s) in enumerate(zip(order, sig)):
            v, sr = cur.node(i, s, +1)
            _v, sl = cur.node(i, s, -1)
            if j > 0 and sig[j] <= sig[j - 1]:
                _v, sl = cur.node(i, b, -1)  # (joined at a turn: coming in as the part ends)
            sr, sl = sr * inv, sl * inv
            for (fi, how), (trk, k) in add.items():
                if fi != i:
                    continue
                ov, osr = trk(tau, +1)
                _ov, osl = trk(tau, -1)
                if how == "add":
                    v, sr, sl = v + ov * k, sr + osr * k, sl + osl * k
                else:
                    v, sr, sl = v * ov, sr * ov + v * osr, sl * ov + v * osl
            vals.append(v)
            right.append(sr)
            left.append(sl)
            st = cur.steps_into(i, s) if j > 0 and sig[j] > sig[j - 1] else False
            st = st or any(fi == i and tau in trk.step_at for (fi, _h), (trk, _k) in add.items())
            steps.append(st)
        q_prev = 0
        for j in range(len(order)):
            q = int(round((vals[j] - base[i]) * 16))
            n = q - q_prev
            q_prev = q
            if j == 0:
                if n:
                    out.keys[0].node[i] = T.NodeKey(n)
                continue
            if steps[j]:
                if n:
                    out.keys[j].node[i] = T.NodeKey(n, 0.0, 0.0, True)
                continue
            if n == 0 and abs(right[j - 1]) < 1e-6 and abs(left[j]) < 1e-6:
                continue
            out.keys[j].node[i] = T.NodeKey(n, right[j - 1], left[j], False)
    # vertex morphs, visibility and texture slots: the source's at each key's time
    prev_pos = [(0, 0, 0)] * len(pose.tables["pos"])
    rest = [struct.unpack(">3f", r) for r in pose.tables["pos"]]
    ev0 = T.Evaluator(pose)
    prev_vis, prev_tex = list(ev0.base_vis), [list(x) for x in ev0.base_tex]
    for j, s in enumerate(sig):
        cur_pos = cur.morph(s)
        q = [tuple(int(round((cur_pos[v][c] - rest[v][c]) * 16)) for c in range(3)) for v in range(len(rest))]
        for v in range(len(rest)):
            d = tuple(q[v][c] - prev_pos[v][c] for c in range(3))
            if any(d):
                out.keys[j].pos[v] = d
        prev_pos = q
        vis, tex = cur.vis_tex(s)
        for v in range(len(vis)):
            if vis[v] != prev_vis[v]:
                out.keys[j].vis[v] = ((vis[v] - prev_vis[v] + 128) & 0xFF) - 128
        last = 0
        for slot in range(len(tex)):
            t0, t1 = prev_tex[slot], tex[slot]
            if t0[0] != t1[0] or t0[1] != t1[1] or t0[2] != t1[2]:
                out.keys[j].tex.append((slot - last, (t1[0] - t0[0]) & 0xFF, t1[1] - t0[1], t1[2] - t0[2], 0))
                last = slot
        prev_vis, prev_tex = list(vis), [list(x) for x in tex]
    import char_motions
    out.bbox = char_motions.anim_bbox(pose, out)
    return T.Anim.parse(name, out.build())  # (through the bytes: what the game reads)


def retarget_specs(pose, recipe=None):
    """{name: spec}: the recipe's, then every animation of Mario's from the model's of its kind"""
    import anim_parity as AP
    files, _only = AP.disc_set()
    rows = AP.mario_set(files, AP.a_mario_group())
    sib = set(AP.same_in_battle(AP._c_text(AP.LUIGI_PC)))
    slots = kinds(pose)
    out = {}
    for ctx, mname, (loop, end), _f in rows:
        name = mname if ctx == "field" or mname in sib else mname + "@b"
        if name in out:
            continue
        src = slots[mario_slot(mname[:-1] if AP.is_back(mname) else mname)]
        if src is None:
            continue
        out[name] = {"from": src, "end": end, "loop": loop}
    for name, spec in ((recipe or {}).get("anims") or {}).items():
        out[name] = spec
    return out


def apply(pose, specs):
    """the model with the new animations (an animation of the same name replaced)"""
    new = {name: make(pose, name, spec) for name, spec in specs.items()}
    anims = []
    for a in pose.anims:
        if a.name in new:
            b = new.pop(a.name)
            anims.append(b)
        else:
            anims.append(a)
    anims += [new[n] for n in specs if n in new]
    p = T.PoseFile.parse(pose.build())
    p.anims = anims
    for a in p.anims:
        if a.name in specs:
            a.table_tail = None
            a.disc_offset = None
    return p


def make_pick(model, recipe, name):
    """the model Play as makes at startup from the disc's (port/src/common/anim_recipe_pc.c, luigi_model_pc.c):
    the recipe's animations added, named `name` (its textures still the disc's: the Wardrobe's colours of the
    character are the pick's too), 32-aligned; byte for byte what the game makes ($TTYD_PLAY_AS_DUMP)"""
    pose = T.PoseFile.parse(model)
    specs = dict(recipe["anims"])
    p = apply(pose, specs)
    p.set_names(file_name=name)
    data = p.build()
    ok, why = T.check_model(data, lambda n: n in specs)
    if not ok:
        raise ValueError("the model made fails its check: %s" % why)
    return data + b"\0" * (-len(data) % 32)


# ------------------------------------------------- the recipe for the game ----

EASES = {"auto": 0, "linear": 1, "step": 2}
CHANNEL_ORDER = list(ADD_CHANNELS)


def _cnum(x):
    """a double as C reads it back exactly (repr is the shortest that round-trips)"""
    r = repr(float(x))
    return r if ("." in r or "e" in r or "inf" in r or "nan" in r) else r + ".0"


def cinc_text(recipe, symbol, source):
    """the recipe as C tables (port/include/port/anim_recipe.h) for the game's maker"""
    pts, trks, anims = [], [], []
    for name, spec in recipe["anims"].items():
        first = len(trks)
        seen = set()
        for gname, chans in (spec.get("add") or {}).items():
            for c, p in chans.items():
                if c not in ADD_CHANNELS:
                    raise ValueError("%s: no channel %r" % (name, c))
                if (gname.lstrip("|"), c) in seen:
                    raise ValueError("%s: %s %s twice" % (name, gname, c))
                seen.add((gname.lstrip("|"), c))
                rows = [(float(q[0]), float(q[1]), q[2] if len(q) > 2 else "auto") for q in p]
                rows.sort(key=lambda q: q[0])  # (as _track: stable, by frame)
                for q in rows:
                    if q[2] not in EASES:
                        raise ValueError("%s: no ease %r" % (name, q[2]))
                if not rows:
                    raise ValueError("%s: %s %s has no points" % (name, gname, c))
                trks.append((gname, CHANNEL_ORDER.index(c), len(pts), len(rows)))
                pts += rows
        rng = spec.get("range")
        anims.append((name, spec["from"], rng is not None, rng[0] if rng else 0.0, rng[1] if rng else 0.0,
                       "end" in spec, spec.get("end", 0.0), "loop" in spec, bool(spec.get("loop", False)),
                       int(spec["turns"]) if "turns" in spec else 0, first, len(trks) - first,
                       spec.get("from_model")))
    s = symbol
    out = ["/* Made by tools/port/anim_recipe.py cinc from %s: don't edit. Our numbers only (docs/LUIGI_ANIMS.md," % source,
           " * \"New animations from a recipe\"): the animations are made from the player's disc at startup. */", ""]
    out.append("static const PortRecipePoint %sPoints[] = {" % s)
    out += ["    {%s, %s, %d}," % (_cnum(t), _cnum(v), EASES[e]) for t, v, e in pts] or ["    {0.0, 0.0, 0},"]
    out.append("};")
    out.append("static const PortRecipeTrack %sTracks[] = {" % s)
    out += ['    {"%s", %d, %d, %d},' % t for t in trks] or ['    {"", 0, 0, 0},']
    out.append("};")
    out.append("static const PortRecipeAnim %sAnims[] = {" % s)
    for a in anims:
        fm = (', "%s"' % a[12]) if a[12] else ""  # (from_model: only when given, the tables before it unchanged)
        out.append('    {"%s", "%s", %d, %s, %s, %d, %s, %d, %d, %d, %d, %d%s},' % (
            a[0], a[1], a[2], _cnum(a[3]), _cnum(a[4]), a[5], _cnum(a[6]), a[7], a[8], a[9], a[10], a[11], fm))
    out.append("};")
    out.append('static const PortRecipe %s = {"%s", %sAnims, %d, %sTracks, %sPoints};' % (
        s, recipe["model"], s, len(anims), s, s))
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- tests ----

def selftest():
    import anim_rig
    p = anim_rig.make_joint_model()
    src = p.anims[0]
    # the same length, once: the source played back as it was
    same = make(p, "SAME", {"from": "A_1", "end": src.end, "loop": True})
    ev = T.Evaluator(p)
    worst = vworst = 0.0
    f = 0.0
    while f <= src.end:
        x, y = ev.at_frame(src, f), ev.at_frame(same, f)
        worst = max(worst, max(abs(u - v) for u, v in zip(x[0], y[0])))
        vworst = max(vworst, max(abs(u[c] - v[c]) for u, v in zip(x[2], y[2]) for c in range(3)))
        f += 0.25
    assert worst < 0.07 and vworst < 1e-6, ("same", worst, vworst)
    # twice as long, two turns; a hop of our own on group 3 (ty), a step on group 2's sx
    long = make(p, "LONG", {"from": "A_1", "end": 60, "loop": True,
                            "add": {"pPlane3": {"ty": [[0, 0], [15, 2.0], [30, 0]]},
                                    "pPlane2": {"sx": [[0, 1, "step"], [20, 2, "step"], [60, 2]]}}})
    assert long.loop and long.end == 60 and long.keys[-1].time == 60, (long.loop_info, long.keys[-1].time)
    ok, why = T.check_model(apply(p, {"LONG": {"from": "A_1", "end": 60, "loop": True}}).build(), lambda n: True)
    assert ok, why
    ev2 = T.Evaluator(p)
    for f in (0.0, 5.0, 12.5, 25.0):
        x, y = ev2.at_frame(src, f), ev2.at_frame(long, f + 30)  # (the second turn: as the first, at its speed)
        d = max(abs(u - v) for k, (u, v) in enumerate(zip(x[0], y[0])) if k != 2 * 24 + 3)
        assert d < 0.1, ("long", f, d)
        assert max(abs(u[c] - v[c]) for u, v in zip(x[2], y[2]) for c in range(3)) < 0.04, ("long morph", f)
    # our hop: at frame 15 group 3 is 2 higher than the source's
    x, y = ev2.at_frame(src, 15.0), ev2.at_frame(long, 15.0)
    assert abs(y[0][3 * 24 + 1] - x[0][3 * 24 + 1] - 2.0) < 0.07, (y[0][3 * 24 + 1], x[0][3 * 24 + 1])
    # the step: group 2's x scale doubled from frame 20
    x, y = ev2.at_frame(src, 20.0), ev2.at_frame(long, 20.0)
    assert abs(y[0][2 * 24 + 3] - 2 * x[0][2 * 24 + 3]) < 0.07, (y[0][2 * 24 + 3], x[0][2 * 24 + 3])
    # slowed down: one turn over 60 frames, the source's frame f at 2f
    slow = make(p, "SLOW", {"from": "A_1", "end": 60, "loop": True, "turns": 1})
    for f in (2.5, 10.0, 17.5, 25.0):
        x, y = ev2.at_frame(src, f), ev2.at_frame(slow, 2 * f)
        assert max(abs(u - v) for u, v in zip(x[0], y[0])) < 0.07, ("slow", f)
        assert max(abs(u[c] - v[c]) for u, v in zip(x[2], y[2]) for c in range(3)) < 0.04, ("slow morph", f)
    # a one-shot from part of it
    part = make(p, "PART", {"from": "A_1", "range": [10, 20], "end": 5, "loop": False})
    x, y = ev2.at_frame(src, 20.0), ev2.at_frame(part, 5.0)
    assert max(abs(u - v) for u, v in zip(x[0], y[0])) < 0.07
    assert [k.time for k in part.keys] == [0.0, 5.0], [k.time for k in part.keys]
    # kinds
    assert mario_slot("M_W_1") == "walk" and mario_slot("M_J_1A") == "up" and mario_slot("M_D_2") == "hurt"
    print("anim_recipe: ok (retimed, turned, joined, offsets of our own, morphs)")
    return 0


# ------------------------------------------------------------------ cli ----

def _source(path):
    import character_tool as C
    return C.open_source(path)


def cmd_make(a):
    recipe = json.load(open(a.recipe, encoding="utf-8"))
    src = _source(a.src)
    pose = T.PoseFile.parse(src.read("a/" + recipe["model"]))
    specs = retarget_specs(pose, recipe) if a.retarget else dict(recipe["anims"])
    p = apply(pose, specs)
    data = p.build()
    ok, why = T.check_model(data, lambda n: n in specs)
    if not ok:
        print("the model made fails its check: %s" % why)
        return 1
    T.PoseFile.parse(data)  # (reads back)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(data + b"\0" * (-len(data) % 32))
    if a.export:
        # the character_tool folder of it, to go on in Blender (`character_tool.py blend` / `roundtrip`)
        import character_tool as C
        C.export_model(data + b"\0" * (-len(data) % 32), C._texture_of(src, data), a.export, src.game_id,
                       os.path.abspath(a.src))
    print("%s: %d animations made (%s), %d in all -> %s" % (recipe["model"], len(specs),
                                                          ", ".join(list(specs)[:8]) + ("..." if len(specs) > 8 else ""),
                                                          len(p.anims), a.out))
    return 0


def cmd_pick(a):
    recipe = json.load(open(a.recipe, encoding="utf-8"))
    src = _source(a.src)
    data = make_pick(src.read("a/" + recipe["model"]), recipe, a.name)
    with open(a.out, "wb") as f:
        f.write(data)
    print("%s: %d animations made, as %s -> %s" % (recipe["model"], len(recipe["anims"]), a.name, a.out))
    return 0


def cmd_cinc(a):
    recipe = json.load(open(a.recipe, encoding="utf-8"))
    rel = os.path.relpath(os.path.abspath(a.recipe), os.path.dirname(os.path.dirname(HERE))).replace(os.sep, "/")
    text = cinc_text(recipe, a.symbol, rel)
    try:
        old = open(a.out, encoding="utf-8").read()
    except OSError:
        old = None
    if a.check:
        print("%s %s" % (a.out, "up to date" if old == text else "out of date: run anim_recipe.py cinc"))
        return 0 if old == text else 1
    if old != text:
        open(a.out, "w", encoding="utf-8", newline="\n").write(text)
    print("%s: %d animations" % (a.out, len(recipe["anims"])))
    return 0


def cmd_kinds(a):
    pose = T.PoseFile.parse(_source(a.src).read("a/" + a.model))
    for slot, name in kinds(pose).items():
        print("%-7s %s" % (slot, name))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("src")
    m.add_argument("recipe")
    m.add_argument("--out", required=True)
    m.add_argument("--retarget", action="store_true")
    m.add_argument("--export", metavar="DIR", help="also as a character_tool folder (for Blender)")
    pk = sub.add_parser("pick")
    pk.add_argument("src")
    pk.add_argument("recipe")
    pk.add_argument("--name", required=True)
    pk.add_argument("--out", required=True)
    ci = sub.add_parser("cinc")
    ci.add_argument("recipe")
    ci.add_argument("--symbol", required=True)
    ci.add_argument("--out", required=True)
    ci.add_argument("--check", action="store_true")
    k = sub.add_parser("kinds")
    k.add_argument("src")
    k.add_argument("model")
    sub.add_parser("selftest")
    a = ap.parse_args(argv)
    return {"make": cmd_make, "pick": cmd_pick, "cinc": cmd_cinc, "kinds": cmd_kinds,
            "selftest": lambda _a: selftest()}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
