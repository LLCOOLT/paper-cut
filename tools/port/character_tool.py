#!/usr/bin/env python3
"""Characters out of the game and back in as a mod (docs/CHARACTERS.md).

A character is a model file `a/<name>` (its shapes, group tree and
animations; src/driver/animdrv.c) and its textures, the TPL `a/<texture>-`.
This tool turns them into an editable folder and builds that folder into a
mod that replaces the character (docs/MODDING.md, overlay/).

  character_tool.py list SRC [--filter TEXT]
      the models on SRC: a disc image (.iso/.gcm), an extracted disc
      (Dolphin's or extract_disc.py's folder, or any folder with the a/ files)
  character_tool.py export SRC NAME... [--out DIR]
      each model as DIR/<name>/ (default exports/): its files as on the disc
      (disc/), the textures as PNG (textures/), the rest pose, groups and
      geometry (model.json, geometry.json), each animation (anims/*.json) and
      manifest.json. SRC may also be a folder the game's "Export character"
      (Mods tab) wrote: it is completed in place.
  character_tool.py build DIR...
      the edited folder(s) back into game files (DIR/build/a/...), checked by
      decoding them again; what changed is listed
  character_tool.py pack DIR... --out MODDIR [--id ID] [--name NAME]
          [--as MODEL] [--games G8ME01,...] [--zip]
      build, then a mod: MODDIR/mod.json and overlay/a/<files>. --as puts
      the (one) model in place of another (e.g. your Luigi as a_mario);
      --zip writes MODDIR.ttydmod instead of a folder
  character_tool.py check SRC [NAME...]
      export and build again without changes: the files must come back byte
      for byte (every model on SRC when no name is given)
  character_tool.py selftest
      the same round trips on a small model made here (no disc needed)

In Blender (`blender --python tools/port/character_tool.py -- CMD ...`, or
`--background` to run without the window):

  character_tool.py blend DIR [--save FILE.blend]
      the folder as a Blender scene: the model as objects, each animation an
      Action (ttyd_blender.build_model); a model Blender can't edit as objects
      (Mario's jointed rig) comes in baked, for reference
  character_tool.py import DIR
      the scene back into the folder: every Action "<model>:<anim>" (new ones
      too) to anims/*.json, moved vertices and UVs to geometry.json

Nothing in the folders comes with the repository: they are made from the
user's own disc.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ttyd_anim as T  # noqa: E402
import ttyd_tpl  # noqa: E402

FORMAT = 1
TOOL = "paper-cut character_tool"

# Well-known sets (src/mario, src/party.c, src/data/npc_data.c): `export SRC
# mario` exports every file of the set.
SETS = {
    "mario": ["a_mario", "a_mario_r", "a_mario_l", "a_mario_lr", "a_mario_w", "a_mario_wr", "a_mario_wl",
              "a_mario_wlr", "e_mario", "e_mario_w", "e_mario_l", "e_mario_wl", "b_mario", "b_mario_l",
              "b_mario_w", "b_mario_wl"],
    "luigi": ["c_luigi"],
}


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def file_sha1(path):
    with open(path, "rb") as f:
        return sha1(f.read())


def _pad32(b):
    return b + b"\0" * (-len(b) % 32)


def _write(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def _write_json(path, obj):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=1, ensure_ascii=False)
        f.write("\n")


def _read_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def safe_name(name):
    """an animation's name as a file name"""
    s = re.sub(r"[^A-Za-z0-9_@.+-]", "_", name)
    return s or "_"


# --------------------------------------------------------------- sources ----

class Source:
    """Where the disc's files come from: read("a/name") -> bytes, models()"""

    game_id = None

    def read(self, path):
        raise NotImplementedError

    def names(self):
        raise NotImplementedError

    def models(self):
        """the model files of a/ (the textures end in '-'; .bin files are tables)"""
        return sorted(n for n in self.names() if not n.endswith("-") and "." not in n)


class DiscSource(Source):
    def __init__(self, path):
        import gcdisc
        self.disc = gcdisc.Disc(path)
        self.game_id = self.disc.game_id

    def read(self, path):
        return self.disc.read(path)

    def names(self):
        return [p[2:] for p in self.disc.files if p.startswith("a/") and "/" not in p[2:]]


class FolderSource(Source):
    def __init__(self, adir, game_id=None):
        self.adir = adir
        self.game_id = game_id

    def read(self, path):
        name = path[2:] if path.startswith("a/") else path
        p = os.path.join(self.adir, name)
        if not os.path.isfile(p):
            raise KeyError(f"{name} isn't in {self.adir}")
        with open(p, "rb") as f:
            return f.read()

    def names(self):
        return [n for n in os.listdir(self.adir) if os.path.isfile(os.path.join(self.adir, n))]


def open_source(path):
    if os.path.isfile(path):
        return DiscSource(path)
    if not os.path.isdir(path):
        sys.exit(f"{path}: no such disc image or folder")
    game_id = None
    for boot in (os.path.join(path, "sys", "boot.bin"), os.path.join(path, "..", "sys", "boot.bin")):
        if os.path.isfile(boot):
            with open(boot, "rb") as f:
                game_id = f.read(6).decode("ascii", "replace")
            break
    for sub in ("files/a", "a", "root/a"):
        d = os.path.join(path, sub)
        if os.path.isdir(d) and os.listdir(d):
            return FolderSource(os.path.normpath(d), game_id)
    return FolderSource(path, game_id)  # the a/ files themselves


def is_export(path):
    m = os.path.join(path, "manifest.json")
    return os.path.isfile(m) and os.path.isdir(os.path.join(path, "disc"))


# ------------------------------------------------------- model <-> JSON ----

def node_labels(pose):
    """float index -> "group.field" (a group named twice: "group@index.field")"""
    groups = pose.groups()
    count = {}
    for g in groups:
        count[g.name] = count.get(g.name, 0) + 1
    labels = {}
    for gi, g in enumerate(groups):
        gname = g.name if count[g.name] == 1 else "%s@%d" % (g.name, gi)
        for f in range(T.NODE_SIZE):
            labels.setdefault(g.node + f, "%s.%s" % (gname, T.NODE_FIELDS[f]))
    return labels


def _label_index(pose):
    labels = node_labels(pose)
    out = {v: k for k, v in labels.items()}

    def index(label):
        if label in out:
            return out[label]
        if label.startswith("#") and label[1:].isdigit():
            return int(label[1:])
        raise ValueError("no node value %r in this model (names are group.field, as in model.json)" % label)
    return labels, index


def anim_to_json(anim, pose):
    """an animation as JSON: node values as values (the model's rest value
    plus every change so far), with their slopes; the rest as stored"""
    labels = node_labels(pose)
    base = pose.node_values()
    q = {}
    keys = []
    for key in anim.keys:
        nodes = []
        for i in sorted(key.node):
            nk = key.node[i]
            q[i] = q.get(i, 0) + nk.n
            e = {"node": labels.get(i, "#%d" % i), "value": (base[i] if i < len(base) else 0.0) + q[i] / 16.0}
            if nk.step:
                e["step"] = True
            else:
                e["slope"] = [nk.sa, nk.sb]
            if nk.raw is not None:
                e["raw"] = [list(c) for c in nk.raw]
            nodes.append(e)
        k = {"time": key.time}
        if nodes:
            k["nodes"] = nodes
        if key.vis:
            k["vis"] = [[i, v] for i, v in sorted(key.vis.items())]
        if key.raw_vis is not None:
            k["raw_vis"] = [list(x) for x in key.raw_vis]
        if key.tex:
            k["tex"] = [list(t) for t in key.tex]
        for which in ("pos", "nrm"):
            d = getattr(key, which)
            if d:
                k[which] = [[i] + list(v) for i, v in sorted(d.items())]
            r = getattr(key, "raw_" + which)
            if r is not None:
                k["raw_" + which] = [[i, [list(c) for c in ch]] for i, ch in r]
        keys.append(k)
    out = {
        "name": anim.name,
        "loop_info": [list(x) for x in anim.loop_info],
        "bbox": list(anim.bbox),
        "value_keys": [list(x) for x in anim.value_keys],
        "exact": bool(getattr(anim, "exact", True)),
        "keys": keys,
    }
    tail = getattr(anim, "table_tail", None)
    if tail is not None and any(tail):
        out["table_tail"] = tail.hex()
    ep = getattr(anim, "empty_ptrs", None)
    if ep and any(ep.values()):
        out["empty_ptrs"] = {str(k): v for k, v in ep.items()}
    return out


def json_to_anim(js, pose):
    """JSON (anim_to_json's form, edited) back to a ttyd_anim.Anim"""
    _labels, index = _label_index(pose)
    base = pose.node_values()
    a = T.Anim(js["name"])
    a.loop_info = [(int(x[0]), float(x[1]), float(x[2])) for x in js.get("loop_info", [[0, 0.0, 0.0]])]
    a.bbox = tuple(float(v) for v in js.get("bbox", (0.0,) * 6))
    if len(a.bbox) != 6:
        raise ValueError("%s: bbox needs six numbers" % a.name)
    a.value_keys = [(float(x[0]), int(x[1])) for x in js.get("value_keys", [[0.0, 0]])]
    a.exact = bool(js.get("exact", False))
    if "table_tail" in js:
        a.table_tail = bytes.fromhex(js["table_tail"])
    if "empty_ptrs" in js:
        a.empty_ptrs = {int(k): int(v) for k, v in js["empty_ptrs"].items()}
    q = {}
    last_time = None
    for kj in js.get("keys", []):
        t = float(kj["time"])
        if last_time is not None and t < last_time:
            raise ValueError("%s: key at %g comes after one at %g (keys go in time order)" % (a.name, t, last_time))
        last_time = t
        key = T.Key(t)
        for e in kj.get("nodes", []):
            i = index(e["node"])
            b = base[i] if i < len(base) else 0.0
            qi = int(round((float(e["value"]) - b) * 16))
            n = qi - q.get(i, 0)
            q[i] = qi
            step = bool(e.get("step", False))
            sa, sb = (0.0, 0.0) if step else (float(e["slope"][0]), float(e["slope"][1])) if "slope" in e else (0.0, 0.0)
            nk = T.NodeKey(n, sa, sb, step)
            raw = e.get("raw")
            if raw is not None:
                raw = [tuple(int(v) for v in c) for c in raw]
                was = T._decode_node_run(raw)
                if was.n == n and was.step == step and (step or (abs(was.sa - sa) < 1e-9 and abs(was.sb - sb) < 1e-9)):
                    nk.raw = raw
            if i in key.node:
                raise ValueError("%s: %s twice in the key at %g" % (a.name, e["node"], t))
            key.node[i] = nk
        key.vis = {int(i): int(v) for i, v in kj.get("vis", [])}
        rv = kj.get("raw_vis")
        if rv is not None:
            rv = [tuple(int(v) for v in x) for x in rv]
            d, idx = {}, 0
            for s, v in rv:
                idx += s
                d[idx] = d.get(idx, 0) + v
            if {k: v for k, v in d.items() if v} == {k: v for k, v in key.vis.items() if v}:
                key.raw_vis = rv
        key.tex = [(int(x[0]), int(x[1]) & 0xFF, float(x[2]), float(x[3]), int(x[4]) if len(x) > 4 else 0)
                   for x in kj.get("tex", [])]
        for which in ("pos", "nrm"):
            d = {int(x[0]): (int(x[1]), int(x[2]), int(x[3])) for x in kj.get(which, [])}
            setattr(key, which, d)
            r = kj.get("raw_" + which)
            if r is not None:
                r = [(int(i), [tuple(int(v) for v in c) for c in ch]) for i, ch in r]
                sums = {i: tuple(sum(c[j] for c in ch) for j in range(3)) for i, ch in r}
                if sums == {i: v for i, v in d.items()}:
                    setattr(key, "raw_" + which, r)
        a.keys.append(key)
    if not a.keys:
        raise ValueError("%s: an animation needs at least one key" % a.name)
    return a


def model_to_json(pose, anim_files):
    groups = pose.groups()
    node = pose.node_values()
    parent = {}
    for gi, g in enumerate(groups):
        c = g.child
        while c != -1:
            parent[c] = gi
            c = groups[c].brother
    gl = []
    for gi, g in enumerate(groups):
        gl.append({
            "index": gi,
            "name": g.name,
            "parent": parent.get(gi, -1),
            "shape": g.shape,
            "vis_index": g.vis,
            "joint": bool(g.ssc),
            "node": {T.NODE_FIELDS[f]: node[g.node + f] for f in range(T.NODE_SIZE)},
        })
    return {
        "_help": "Rest pose and visibility of the model (docs/CHARACTERS.md). Edit a group's node values "
                 "(tx ty tz: position; sx sy sz: scale; r1x r1y r1z: rotation in half degrees; the rest are "
                 "pivots and joint values) or vis (1 shown, 0 hidden, per vis_index). 'animations' is the "
                 "animation table in order: remove an entry to drop one, add one (and its file) for a new one.",
        "file_name": pose.file_name,
        "texture_name": pose.texture_name,
        "vis": pose.vis_values(),
        "groups": gl,
        "animations": anim_files,
    }


def geometry_to_json(pose):
    t = pose.tables
    return {
        "_help": "Vertex positions (model space, before animation), texture coordinates and vertex colours "
                 "(RGBA 0-255) by index, as the shapes use them. Move them; the count can't change here.",
        "positions": [list(struct.unpack(">3f", r)) for r in t["pos"]],
        "uvs": [list(struct.unpack(">2f", r)) for r in t["texVtx"]],
        "colors": [list(r) for r in t["clr"]],
    }


def apply_model_json(pose, mj):
    groups = pose.groups()
    if len(mj["groups"]) != len(groups):
        raise ValueError("model.json has %d groups, the model %d: groups can't be added or removed here"
                         % (len(mj["groups"]), len(groups)))
    for gj, g in zip(mj["groups"], groups):
        for f, field in enumerate(T.NODE_FIELDS):
            if field in gj.get("node", {}):
                pose.tables["node"][g.node + f] = struct.pack(">f", float(gj["node"][field]))
    vis = mj.get("vis")
    if vis is not None:
        if len(vis) != len(pose.tables["vis"]):
            raise ValueError("model.json's vis has %d values, the model %d" % (len(vis), len(pose.tables["vis"])))
        pose.tables["vis"] = [struct.pack(">b", int(v)) for v in vis]


def apply_geometry_json(pose, gj):
    t = pose.tables
    for key, table, fmt in (("positions", "pos", ">3f"), ("uvs", "texVtx", ">2f")):
        vals = gj.get(key)
        if vals is None:
            continue
        if len(vals) != len(t[table]):
            raise ValueError("geometry.json: %d %s, the model has %d" % (len(vals), key, len(t[table])))
        t[table] = [struct.pack(fmt, *(float(x) for x in v)) for v in vals]
    cols = gj.get("colors")
    if cols is not None:
        if len(cols) != len(t["clr"]):
            raise ValueError("geometry.json: %d colors, the model has %d" % (len(cols), len(t["clr"])))
        t["clr"] = [bytes(max(0, min(255, int(c))) for c in v) for v in cols]


# ---------------------------------------------------------------- export ----

def blender_problems(pose):
    """ttyd_blender.editable_problems without Blender"""
    probs = []
    node = pose.node_values()
    for g in pose.groups():
        n = node[g.node:g.node + T.NODE_SIZE]
        if g.ssc:
            probs.append("%s is a joint" % g.name)
        if any(n[9:12]):
            probs.append("%s has a joint orient" % g.name)
        if n[12:15] != n[15:18]:
            probs.append("%s: rotation and scale pivots differ" % g.name)
        if any(n[18:24]):
            probs.append("%s: pivot translations" % g.name)
    for a in pose.anims:
        for k in a.keys:
            if any(i % T.NODE_SIZE >= 9 for i in k.node):
                probs.append("%s animates pivots or joint values" % a.name)
                break
            if k.pos or k.nrm:
                probs.append("%s moves vertices" % a.name)
                break
    return sorted(set(probs))


def export_model(model_data, tex_name_data, out, game_id=None, source=None, quiet=False):
    """the editable folder for one model; tex_name_data: (texture file name, bytes) or None"""
    pose = T.PoseFile.parse(model_data)
    name = pose.file_name
    _write(os.path.join(out, "disc", name), model_data)
    textures = []
    if tex_name_data is not None:
        tname, tdata = tex_name_data
        _write(os.path.join(out, "disc", tname), tdata)
        tpl = ttyd_tpl.TplFile.parse(tdata)
        for i, im in enumerate(tpl.images):
            png = os.path.join(out, "textures", "%03d.png" % i)
            entry = {"file": "textures/%03d.png" % i, "width": im.width, "height": im.height,
                     "format": im.format_name, "mipmaps": im.levels()}
            try:
                ttyd_tpl.write_png(png, im.pixels())
                entry["sha1"] = file_sha1(png)
            except ValueError as e:
                entry["error"] = str(e)
            textures.append(entry)
    anim_files, anim_info, used = [], [], set()
    for a in pose.anims:
        fn = safe_name(a.name)
        while fn.lower() in used:
            fn += "_"
        used.add(fn.lower())
        path = os.path.join(out, "anims", fn + ".json")
        _write_json(path, anim_to_json(a, pose))
        anim_files.append({"name": a.name, "file": "anims/%s.json" % fn})
        anim_info.append({"name": a.name, "file": "anims/%s.json" % fn, "sha1": file_sha1(path),
                          "loop": a.loop, "start": a.start, "end": a.end, "keys": len(a.keys)})
    _write_json(os.path.join(out, "model.json"), model_to_json(pose, anim_files))
    _write_json(os.path.join(out, "geometry.json"), geometry_to_json(pose))
    probs = blender_problems(pose)
    old = {}
    mpath = os.path.join(out, "manifest.json")
    if os.path.isfile(mpath):
        old = _read_json(mpath)
    manifest = {
        "format": FORMAT,
        "tool": TOOL,
        "game": game_id or old.get("game"),
        "source": source or old.get("source"),
        "model": name,
        "texture": pose.texture_name,
        "files": {
            "model": {"path": "disc/" + name, "size": len(model_data), "sha1": sha1(model_data)},
        },
        "textures": textures,
        "animations": anim_info,
        "model_json_sha1": file_sha1(os.path.join(out, "model.json")),
        "geometry_json_sha1": file_sha1(os.path.join(out, "geometry.json")),
        "blender": {"editable": not probs, "why_not": probs[:20]},
        "help": "docs/CHARACTERS.md in the Paper Cut repository; tools/port/character_tool.py builds it into a mod",
    }
    if tex_name_data is not None:
        manifest["files"]["texture"] = {"path": "disc/" + tex_name_data[0], "size": len(tex_name_data[1]),
                                        "sha1": sha1(tex_name_data[1])}
    for k in ("exported_by", "modded"):
        if k in old:
            manifest[k] = old[k]
    _write_json(mpath, manifest)
    if not quiet:
        print("%s: %d groups, %d animations, %d textures -> %s%s" % (
            name, len(pose.groups()), len(pose.anims), len(textures), out,
            "" if not probs else " (not editable as objects in Blender: %s)" % probs[0]))
    return manifest


def _texture_of(src, model_data):
    pose_tex = T._cstr(model_data[0x44:0x84])
    tname = pose_tex + "-"
    try:
        return tname, src.read("a/" + tname)
    except KeyError:
        return None


def cmd_list(a):
    src = open_source(a.src)
    n = 0
    for m in src.models():
        if a.filter and a.filter.lower() not in m.lower():
            continue
        print(m)
        n += 1
    print("%d models%s" % (n, " (%s)" % src.game_id if src.game_id else ""), file=sys.stderr)


def cmd_export(a):
    if is_export(a.src):
        # a folder the game wrote (Mods tab, "Export character"): completed in place
        man = _read_json(os.path.join(a.src, "manifest.json"))
        name = man["model"]
        model = open(os.path.join(a.src, "disc", name), "rb").read()
        tex = None
        tname = (man.get("texture") or T._cstr(model[0x44:0x84])) + "-"
        tpath = os.path.join(a.src, "disc", tname)
        if os.path.isfile(tpath):
            tex = (tname, open(tpath, "rb").read())
        export_model(model, tex, a.out or a.src, man.get("game"), man.get("source"))
        return 0
    src = open_source(a.src)
    names = []
    for n in a.names:
        names += SETS.get(n.lower(), [n])
    if not names:
        sys.exit("name the models to export (`list` shows them)")
    have = set(src.models())
    out = a.out or "exports"
    for n in names:
        if n not in have:
            if n in sum(SETS.values(), []):
                continue  # a set's member this version doesn't have
            sys.exit("%s isn't a model on %s" % (n, a.src))
        data = src.read("a/" + n)
        export_model(data, _texture_of(src, data), os.path.join(out, n), src.game_id, os.path.abspath(a.src))
    return 0


# ----------------------------------------------------------------- build ----

def _same_pixels(a, b):
    if len(a) != len(b) or any(len(x) != len(y) for x, y in zip(a, b)):
        return False
    for ra, rb in zip(a, b):
        for p, q in zip(ra, rb):
            if p != q and not (p[3] == 0 and q[3] == 0):
                return False
    return True


def build_folder(d, as_name=None, texture_as=None, quiet=False):
    """the folder's game files: {"a/<model>": bytes, "a/<texture>-": bytes}, report"""
    man = _read_json(os.path.join(d, "manifest.json"))
    name = man["model"]
    orig = open(os.path.join(d, "disc", name), "rb").read()
    orig_pose = T.PoseFile.parse(orig)
    pose = T.PoseFile.parse(orig)
    report = []
    mj = _read_json(os.path.join(d, "model.json"))
    if file_sha1(os.path.join(d, "model.json")) != man.get("model_json_sha1"):
        apply_model_json(pose, mj)
        report.append("model.json (rest pose, visibility)")
    gpath = os.path.join(d, "geometry.json")
    if os.path.isfile(gpath) and file_sha1(gpath) != man.get("geometry_json_sha1"):
        apply_geometry_json(pose, _read_json(gpath))
        report.append("geometry.json")
    by_name = {x.name: x for x in orig_pose.anims}
    exported = {x["name"]: x for x in man.get("animations", [])}
    anims, seen = [], set()
    for entry in mj["animations"]:
        if isinstance(entry, str):
            entry = {"name": entry, "file": "anims/%s.json" % safe_name(entry)}
        aname, path = entry["name"], os.path.join(d, entry["file"])
        if aname in seen:
            raise ValueError("animation %s is listed twice in model.json" % aname)
        seen.add(aname)
        ex = exported.get(aname)
        if ex is not None and ex.get("file") == entry["file"] and aname in by_name and \
                os.path.isfile(path) and file_sha1(path) == ex.get("sha1"):
            anims.append(by_name[aname])  # untouched: as on the disc
            continue
        if not os.path.isfile(path):
            raise ValueError("animation %s: %s is missing" % (aname, entry["file"]))
        js = _read_json(path)
        if js.get("name", aname) != aname:
            js["name"] = aname  # model.json's name wins
        anim = json_to_anim(js, orig_pose)
        anim.name = aname
        if len(aname.encode("latin-1")) >= 0x3C:
            raise ValueError("animation name %r is too long (59 characters at most)" % aname)
        anims.append(anim)
        report.append("animation %s%s" % (aname, "" if aname in by_name else " (new)"))
    dropped = [x.name for x in orig_pose.anims if x.name not in seen]
    if dropped:
        report.append("animations left out: %s" % ", ".join(dropped))
    pose.anims = anims
    tname = man.get("texture") or pose.texture_name
    if as_name:
        pose.set_names(as_name, texture_as or as_name)
        report.append("as %s (textures %s-)" % (as_name, texture_as or as_name))
    model = pose.build()
    # check: it reads back, and nothing was lost on the way
    again = T.PoseFile.parse(model)
    if len(again.anims) != len(anims):
        raise ValueError("%s: the model written doesn't read back" % name)
    stripped = orig.rstrip(b"\0")
    if model.rstrip(b"\0") == stripped and not as_name:
        model = orig
    else:
        model = _pad32(model)
    out = {"a/" + (as_name or name): model}
    tfile = os.path.join(d, "disc", tname + "-")
    if os.path.isfile(tfile):
        tdata = open(tfile, "rb").read()
        tpl = ttyd_tpl.TplFile.parse(tdata)
        info = {t["file"]: t for t in man.get("textures", [])}
        for i, im in enumerate(tpl.images):
            rel = "textures/%03d.png" % i
            png = os.path.join(d, rel)
            if not os.path.isfile(png):
                continue
            if info.get(rel, {}).get("sha1") == file_sha1(png):
                continue
            rows = ttyd_tpl.read_png(png)
            if _same_pixels(rows, im.pixels()):
                continue
            im.replace(rows)
            report.append("texture %d (%dx%d %s)" % (i, len(rows[0]), len(rows), im.format_name))
        extra = sorted(f for f in os.listdir(os.path.join(d, "textures"))
                       if f.endswith(".png") and re.match(r"\d{3}\.png$", f) and int(f[:3]) >= len(tpl.images)) \
            if os.path.isdir(os.path.join(d, "textures")) else []
        if extra:
            report.append("ignored (the model's animations can't name them): %s" % ", ".join(extra))
        out["a/" + (texture_as or as_name or tname) + "-"] = tpl.build() if not as_name else _pad32(tpl.build())
    if not quiet:
        print("%s: %s" % (name, "; ".join(report) if report else "unchanged"))
    return out, report, man


def cmd_build(a):
    for d in a.dirs:
        files, _report, _man = build_folder(d)
        for path, data in files.items():
            _write(os.path.join(d, "build", path), data)
        print("  -> %s" % os.path.join(d, "build", "a"))
    return 0


def cmd_pack(a):
    if a.as_name and len(a.dirs) != 1:
        sys.exit("--as takes one folder")
    files, games, names = {}, set(), []
    for d in a.dirs:
        built, _report, man = build_folder(d, a.as_name, a.texture_as)
        names.append(man["model"])
        if man.get("game"):
            games.add(man["game"])
        for path, data in built.items():
            if path in files and files[path] != data:
                sys.exit("%s is made twice, differently (two folders share a texture file?)" % path)
            files[path] = data
    model_names = [a.as_name] if a.as_name else names
    replaced = getattr(a, "target", None)
    if replaced:
        # the model it replaces (an export of it): the animations the game asks it for
        want = [x["name"] for x in _read_json(os.path.join(replaced, "manifest.json")).get("animations", [])]
        table = [x.name for x in T.PoseFile.parse(files["a/" + model_names[0]]).anims]
        missing = [n for n in want if n not in set(table)]
        if missing:
            print("%d of %s's %d animations aren't in it (where the game asks for one, it plays the first, %s): %s"
                  % (len(missing), model_names[0], len(want), table[0] if table else "-", " ".join(missing)))
        else:
            print("it has every animation of %s" % model_names[0])
    mod = {
        "id": a.id or "character." + "-".join(model_names),
        "name": a.name or "Character: " + ", ".join(model_names),
        "version": a.version,
        "author": a.author or "",
        "description": a.description or "Replaces %s (models and textures; made with character_tool.py)."
        % ", ".join("a/" + n for n in model_names),
    }
    if a.games:
        mod["games"] = [g.strip() for g in a.games.split(",") if g.strip()]
    out = a.out.rstrip("/\\")
    if a.zip:
        zpath = out if out.endswith((".ttydmod", ".zip")) else out + ".ttydmod"
        top = os.path.splitext(os.path.basename(zpath))[0]
        os.makedirs(os.path.dirname(os.path.abspath(zpath)), exist_ok=True)
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(top + "/mod.json", json.dumps(mod, indent=2) + "\n")
            for path, data in sorted(files.items()):
                z.writestr(top + "/overlay/" + path, data)
        print("mod %s: %s" % (mod["id"], zpath))
    else:
        if os.path.isdir(os.path.join(out, "overlay")):
            shutil.rmtree(os.path.join(out, "overlay"))
        _write_json(os.path.join(out, "mod.json"), mod)
        for path, data in files.items():
            _write(os.path.join(out, "overlay", path), data)
        print("mod %s: %s (%d files)" % (mod["id"], out, len(files)))
    if games and not a.games:
        print("made from %s; it is used on every version (--games to limit it)" % ", ".join(sorted(games)))
    return 0


# ----------------------------------------------------------------- check ----

def roundtrip(model_data, tex, tmp):
    """export and build without changes: (model same, texture same)"""
    d = os.path.join(tmp, "m")
    if os.path.isdir(d):
        shutil.rmtree(d)
    export_model(model_data, tex, d, quiet=True)
    files, _r, man = build_folder(d, quiet=True)
    msame = files["a/" + man["model"]] == model_data
    tsame = tex is None or files.get("a/" + tex[0]) == tex[1]
    # and through the JSON, as an edited folder is: every animation re-read
    pose = T.PoseFile.parse(model_data)
    for a in pose.anims:
        b = json_to_anim(json.loads(json.dumps(anim_to_json(a, pose))), pose)
        if b.build() != a.build():
            return msame, tsame, a.name
    return msame, tsame, None


def cmd_check(a):
    src = open_source(a.src)
    names = a.names or src.models()
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for n in names:
            data = src.read("a/" + n)
            try:
                T.PoseFile.parse(data)
            except Exception as e:  # noqa: BLE001 (a file the tools can't read: reported, the rest go on)
                print("%-24s not read: %s" % (n, e))
                continue
            tex = _texture_of(src, data)
            try:
                m, t, anim = roundtrip(data, tex, tmp)
            except Exception as e:  # noqa: BLE001
                print("%-24s FAILED: %s" % (n, e))
                bad += 1
                continue
            ok = m and t and anim is None
            bad += not ok
            if not ok or a.verbose:
                print("%-24s model %s, textures %s%s" % (n, "same" if m else "DIFFERS", "same" if t else "DIFFER",
                                                        "" if anim is None else ", animation %s through JSON DIFFERS" % anim))
    print("%d models, %d failed" % (len(names), bad))
    return 1 if bad else 0


# --------------------------------------------------------------- Blender ----

def cmd_blend(a):
    import bpy
    import ttyd_blender as B
    d = a.dir
    man = _read_json(os.path.join(d, "manifest.json"))
    files, _r, _m = build_folder(d, quiet=True)
    gen = os.path.join(d, "build", "a")
    for path, data in files.items():
        _write(os.path.join(d, "build", path), data)
    name = man["model"]
    model_path = os.path.join(gen, name)
    tex_path = os.path.join(gen, man["texture"] + "-")
    pose = T.PoseFile.parse(open(model_path, "rb").read())
    # the textures as the folder has them (Blender's image editor saves them there)
    if B.editable_problems(pose):
        print("%s can't be edited as objects (%s); baked for reference: edit its animations in anims/*.json"
              % (name, B.editable_problems(pose)[0]))
        root = B.build_baked(model_path, tex_path, os.path.join(d, "textures"), os.path.join(d, "build", "pc2"))
    else:
        root = B.build_model(model_path, tex_path, os.path.join(d, "textures"))
        root["ttyd_export_dir"] = os.path.abspath(d)
    if a.save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(a.save))
    return 0


def _mesh_edits(root, pose, geo_json):
    """moved vertices and UVs of the scene's planes into geometry.json's lists"""
    import ttyd_blender as B
    geo = T.Geometry(pose)
    node = pose.node_values()
    groups = pose.groups()
    objs = B.model_objects(root)
    moved = 0
    for gi, ob in objs.items():
        g = groups[gi]
        if g.shape < 0 or ob.type != "MESH":
            continue
        pivot = node[g.node + 12:g.node + 15]
        first, count = geo.shapes[g.shape]["pos"]
        me = ob.data
        if len(me.vertices) != count:
            print("%s: %d vertices, the shape has %d: its vertices are left as they were" % (ob.name, len(me.vertices), count))
            continue
        for j, v in enumerate(me.vertices):
            p = [v.co[c] + pivot[c] for c in range(3)]
            if any(abs(p[c] - geo_json["positions"][first + j][c]) > 1e-5 for c in range(3)):
                geo_json["positions"][first + j] = p
                moved += 1
        uvl = me.uv_layers.get("UVMap")
        sh = geo.shapes[g.shape]
        if uvl is None or len(me.polygons) != len(geo.shape_polys(g.shape)):
            continue
        # corners' texture indices, in shape_polys' order
        k = 0
        for di in range(sh["draw_start"], sh["draw_start"] + sh["draw_count"]):
            dr = geo.draws[di]
            tarr = dr["tex_array"][0] if dr["tex_count"] else None
            for ci in range(dr["first_call"], dr["first_call"] + dr["call_count"]):
                first_c, cnt = geo.drawcalls[ci]
                poly = me.polygons[k]
                k += 1
                if tarr is None or len(poly.loop_indices) != cnt:
                    continue
                for c, loop in enumerate(poly.loop_indices):
                    ti = geo.texidx[tarr][first_c + dr["first_tex"][tarr] + c] + sh["tex"][tarr][0]
                    u, v = uvl.data[loop].uv
                    new = [u, 1.0 - v]
                    if abs(new[0] - geo_json["uvs"][ti][0]) > 1e-6 or abs(new[1] - geo_json["uvs"][ti][1]) > 1e-6:
                        geo_json["uvs"][ti] = new
                        moved += 1
    return moved


def cmd_import(a):
    import bpy
    import ttyd_blender as B
    d = a.dir
    man = _read_json(os.path.join(d, "manifest.json"))
    name = man["model"]
    orig_pose = T.PoseFile.parse(open(os.path.join(d, "disc", name), "rb").read())
    root = None
    for ob in bpy.data.objects:
        if ob.get("ttyd_prefix") == name and "ttyd_model" in ob.keys():
            root = ob
    if root is None:
        sys.exit("no %s in this scene (character_tool.py blend %s makes it)" % (name, d))
    built_pose = T.PoseFile.parse(open(root["ttyd_model"], "rb").read())
    if B.editable_problems(built_pose):
        sys.exit("%s is in the scene baked, for reference only (%s): edit its anims/*.json"
                 % (name, B.editable_problems(built_pose)[0]))
    mj = _read_json(os.path.join(d, "model.json"))
    listed = {}
    for e in mj["animations"]:
        if isinstance(e, str):
            e = {"name": e, "file": "anims/%s.json" % safe_name(e)}
        listed[e["name"]] = e
    order = [e if isinstance(e, dict) else {"name": e, "file": "anims/%s.json" % safe_name(e)}
             for e in mj["animations"]]
    written = 0
    for act in bpy.data.actions:
        if not act.name.startswith(name + ":"):
            continue
        aname = act.name.split(":", 1)[1]
        anim, rep = B.read_action(root, act, built_pose)
        anim.name = aname
        if act.get("ttyd_anim") != aname:
            anim.table_tail = None  # a copy of another Action: not the disc's table entry
        js = anim_to_json(anim, orig_pose)
        e = listed.get(aname)
        if e is None:
            e = {"name": aname, "file": "anims/%s.json" % safe_name(aname)}
            order.append(e)
            listed[aname] = e
        path = os.path.join(d, e["file"])
        old = _read_json(path) if os.path.isfile(path) else None
        old_anim = json_to_anim(old, orig_pose) if old is not None else None
        if old_anim is not None and old_anim.build() == anim.build():
            continue  # the same animation: the file stays as it is
        _write_json(path, js)
        written += 1
        added = rep.get("added_keys") or []
        print("%s: written%s" % (aname, " (%d keys added where the game's curve can't follow)" % len(added)
                                  if added else ""))
    mj["animations"] = order
    _write_json(os.path.join(d, "model.json"), mj)
    gpath = os.path.join(d, "geometry.json")
    gj = _read_json(gpath)
    moved = _mesh_edits(root, built_pose, gj)
    if moved:
        _write_json(gpath, gj)
    print("%d animations written, %d vertex values changed; `character_tool.py pack %s --out ...` makes the mod"
          % (written, moved, d))
    return 0


# -------------------------------------------------------------- selftest ----

def make_test_model(n_groups=26, anims=()):
    """a model of n_groups planes (four vertices each) in a row under a root,
    for the tests: real tables, nothing from the disc"""
    p = T.PoseFile()
    p.set_names("t_test", "t_test")
    for name, _s, _c in T.TABLES:
        p.tables[name] = []
    t = p.tables
    for gi in range(n_groups):
        shape = bytearray(0xA8)
        shape[0:64] = T._pad_name("shape%d" % gi, 64)
        pos0, tex0 = len(t["pos"]), len(t["texVtx"])
        struct.pack_into(">26i", shape, 0x40, pos0, 4, 0, 0, gi * 4, 4, tex0, 4, *([tex0 + 4, 0] * 7),
                         gi, 1, 0, 3)
        t["shapes"].append(bytes(shape))
        t["drawCalls"].append(struct.pack(">2i", 4 * gi, 4))
        for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)):
            t["pos"].append(struct.pack(">3f", x + gi, y, 0.0))
            t["texVtx"].append(struct.pack(">2f", x, 1 - y))
            t["clr"].append(b"\xff\xff\xff\xff")
        for k in range(4):
            t["posIdx"].append(struct.pack(">I", k))
            t["texIdx0"].append(struct.pack(">I", k))
            t["clrIdx"].append(struct.pack(">I", k))
        dr = bytearray(0x6C)
        struct.pack_into(">i4xi4x8i", dr, 0, 1, 0, 0, -1, -1, -1, -1, -1, -1, -1)
        struct.pack_into(">2i11i", dr, 0x38, gi, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        t["draws"].append(bytes(dr))
        t["vis"].append(b"\x01")
        node = [0.5 * gi, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0] + [0.0] * 12
        t["node"] += [struct.pack(">f", v) for v in node]
    t["texAnim"].append(struct.pack(">B3x5f", 0, 0.0, 0.0, 1.0, 1.0, 0.0))
    t["texData"] = [struct.pack(">2i", 0, 0) for _ in range(20)]
    for j in range(30):
        b = bytearray(0x40)
        struct.pack_into(">Ii", b, 4, j % 2, 0)
        t["texBinds"].append(bytes(b))
    t["vis"].append(b"\x01")
    t["node"] += [struct.pack(">f", v) for v in [0.0] * 3 + [1.0] * 3 + [0.0] * 18]
    for gi in range(n_groups):
        g = bytearray(0x58)
        g[0:64] = T._pad_name("pPlane%d" % gi, 64)
        struct.pack_into(">6i", g, 0x40, gi + 1 if gi + 1 < n_groups else -1, -1, gi, gi, gi * 24, 0)
        t["groups"].append(bytes(g))
    root = bytearray(0x58)
    root[0:64] = T._pad_name("root", 64)
    struct.pack_into(">6i", root, 0x40, -1, 0, -1, n_groups, n_groups * 24, 0)
    t["groups"].append(bytes(root))
    p.anims = list(anims)
    return p


def _test_tpl():
    rows = [[((x * 16) & 255, (y * 16) & 255, 128, 255 if x < 12 else 0) for x in range(16)] for y in range(16)]
    n = 2
    data = bytearray(0xC + 8 * n + 0x24 * n)
    struct.pack_into(">III", data, 0, ttyd_tpl.MAGIC, n, 0xC)
    for i, fmt in enumerate((14, 5)):
        hoff = 0xC + 8 * n + 0x24 * i
        struct.pack_into(">II", data, 0xC + 8 * i, hoff, 0)
        data.extend(b"\0" * (-len(data) % 32))
        struct.pack_into(">HHII", data, hoff, 16, 16, fmt, len(data))
        data.extend(ttyd_tpl.encode(rows, fmt))
    return bytes(data)


def selftest():
    ttyd_tpl.selftest()
    import luigi_pack
    pack = luigi_pack.read_pack(open(os.path.join(os.path.dirname(os.path.dirname(HERE)), "port", "assets", "luigi",
                                                  "luigi_anims.bin"), "rb").read())
    # Luigi's 156 animations of our own (port/assets/luigi): real keys, steps, chunked deltas, textures, visibility
    anims = [T.Anim.parse(name, blob) for name, blob in pack["anims"]]
    pose = make_test_model(26, anims)
    model = pose.build()
    pose = T.PoseFile.parse(model)  # the layout check: it writes back the same
    tex = ("t_test-", _test_tpl())
    with tempfile.TemporaryDirectory() as tmp:
        m, t, bad = roundtrip(model, tex, tmp)
        assert m and t and bad is None, ("round trip", m, t, bad)
        d = os.path.join(tmp, "edit")
        export_model(model, tex, d, game_id="G8ME01", quiet=True)
        # an edit of each kind
        mj = _read_json(os.path.join(d, "model.json"))
        mj["groups"][3]["node"]["sx"] = 1.5
        mj["vis"][5] = 0
        first = mj["animations"][0]
        mj["animations"] = mj["animations"][1:] + [{"name": "NEW_1", "file": "anims/NEW_1.json"}]
        _write_json(os.path.join(d, "model.json"), mj)
        aj = _read_json(os.path.join(d, first["file"]))
        aj["name"] = "NEW_1"
        aj["loop_info"][0][2] = aj["loop_info"][0][2] + 10
        moved = None
        for k in aj["keys"]:
            for e in k.get("nodes", []):
                if not e.get("step"):
                    e["value"] += 2.0
                    moved = (e["node"], k["time"], e["value"])
                    break
            if moved:
                break
        _write_json(os.path.join(d, "anims", "NEW_1.json"), aj)
        gj = _read_json(os.path.join(d, "geometry.json"))
        gj["positions"][0] = [-3.0, -2.0, 0.5]
        _write_json(os.path.join(d, "geometry.json"), gj)
        rows = ttyd_tpl.read_png(os.path.join(d, "textures", "000.png"))
        rows[0][0] = (255, 0, 0, 255)
        big = [[rows[y // 2][x // 2] for x in range(32)] for y in range(32)]
        ttyd_tpl.write_png(os.path.join(d, "textures", "001.png"), big)
        ttyd_tpl.write_png(os.path.join(d, "textures", "000.png"), rows)
        files, report, _man = build_folder(d, quiet=True)
        p2 = T.PoseFile.parse(files["a/t_test"])
        assert [a.name for a in p2.anims] == [a.name for a in pose.anims[1:]] + ["NEW_1"], "animation table"
        assert p2.groups()[3].node is not None and p2.node_values()[3 * 24 + 3] == 1.5, "rest pose"
        assert p2.vis_values()[5] == 0, "visibility"
        assert struct.unpack(">3f", p2.tables["pos"][0]) == (-3.0, -2.0, 0.5), "geometry"
        new = p2.anims[-1]
        assert new.end == pose.anims[0].end + 10, "loop range"
        for a, b in zip(p2.anims[:-1], pose.anims[1:]):
            assert a.build() == b.build(), "untouched animation %s" % a.name
        # the moved value: the evaluator at that key
        idx = _label_index(pose)[1](moved[0])
        ev = T.Evaluator(p2)
        ki = [k.time for k in new.keys].index(moved[1])
        got = ev.at_key(new, ki)[0][idx]
        ev0 = T.Evaluator(pose)
        was = ev0.at_key(pose.anims[0], ki)[0][idx]
        assert abs(got - was - 2.0) < 1e-4, ("moved value", got, was)
        tpl = ttyd_tpl.TplFile.parse(files["a/t_test-"])
        assert tpl.images[0].pixels()[0][0][:3] != (0, 0, 0)
        assert (tpl.images[1].width, tpl.images[1].height) == (32, 32), "a bigger texture"
        # the mod
        class Args:
            pass
        args = Args()
        args.dirs, args.out, args.id, args.name, args.as_name, args.texture_as = [d], os.path.join(tmp, "mod"), None, None, None, None
        args.games, args.zip, args.version, args.author, args.description = None, False, "1.0", None, None
        args.target = None
        cmd_pack(args)
        assert os.path.isfile(os.path.join(tmp, "mod", "overlay", "a", "t_test"))
        assert _read_json(os.path.join(tmp, "mod", "mod.json"))["id"] == "character.t_test"
        args.as_name, args.zip, args.out, args.target = "a_mario", True, os.path.join(tmp, "mario"), d
        cmd_pack(args)
        with zipfile.ZipFile(os.path.join(tmp, "mario.ttydmod")) as z:
            names = set(z.namelist())
        assert {"mario/mod.json", "mario/overlay/a/a_mario", "mario/overlay/a/a_mario-"} <= names, names
        # a folder as the game's "Export character" writes it: completed in place
        g = os.path.join(tmp, "ingame", "t_test")
        _write(os.path.join(g, "disc", "t_test"), model)
        _write(os.path.join(g, "disc", "t_test-"), tex[1])
        _write_json(os.path.join(g, "manifest.json"), {"format": FORMAT, "model": "t_test", "texture": "t_test",
                                                       "game": "G8MJ01", "exported_by": "Paper Cut"})
        args = Args()
        args.src, args.out, args.names = g, None, []
        cmd_export(args)
        assert is_export(g) and _read_json(os.path.join(g, "manifest.json"))["game"] == "G8MJ01"
        files, report, _man = build_folder(g, quiet=True)
        assert files["a/t_test"] == model and not report
        # a folder of a/ files as the source
        src = open_source(os.path.join(g, "disc"))
        assert src.models() == ["t_test"]
    print("character_tool: ok (%d animations through JSON byte for byte; edits of each kind; mods)" % len(anims))
    return 0


# ------------------------------------------------------------------- cli ----

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("src")
    p.add_argument("--filter")
    p = sub.add_parser("export")
    p.add_argument("src")
    p.add_argument("names", nargs="*")
    p.add_argument("--out")
    p = sub.add_parser("build")
    p.add_argument("dirs", nargs="+")
    p = sub.add_parser("pack")
    p.add_argument("dirs", nargs="+")
    p.add_argument("--out", required=True)
    p.add_argument("--id")
    p.add_argument("--name")
    p.add_argument("--version", default="1.0")
    p.add_argument("--author")
    p.add_argument("--description")
    p.add_argument("--as", dest="as_name", metavar="MODEL")
    p.add_argument("--texture-as", dest="texture_as", metavar="NAME")
    p.add_argument("--target", metavar="DIR", help="with --as: an export of the model replaced, to list "
                   "the animations the replacement lacks")
    p.add_argument("--games")
    p.add_argument("--zip", action="store_true")
    p = sub.add_parser("check")
    p.add_argument("src")
    p.add_argument("names", nargs="*")
    p.add_argument("-v", "--verbose", action="store_true")
    sub.add_parser("selftest")
    p = sub.add_parser("blend")
    p.add_argument("dir")
    p.add_argument("--save")
    p = sub.add_parser("import")
    p.add_argument("dir")
    a = ap.parse_args(argv)
    try:
        return {"list": cmd_list, "export": cmd_export, "build": cmd_build, "pack": cmd_pack, "check": cmd_check,
                "selftest": lambda _a: selftest(), "blend": cmd_blend, "import": cmd_import}[a.cmd](a)
    except ValueError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    sys.exit(main(argv) or 0)
