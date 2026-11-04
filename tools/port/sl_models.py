"""Super Luigi's new models (docs/LUIGI_ANIMS.md, "Super Luigi's models"):
Hizza, the bride's veil, Big Green 01, Eclair, Crepe, the Chestnut King and
Cranberry. Each is a module as statue_model.py is (NAME, parts(), build(anims),
MOTIONS, from the art in port/assets/super_luigi/art/<character>/), made the
way "New models" describes, and shipped in a container of their own,
port/assets/super_luigi/sl_models.bin (luigi_pack's LUFR, whole files only),
which the port adds as a/<name> at startup (port/src/aurora/sl_models_aurora.c).

  blender --background --python tools/port/sl_models.py -- pack [--save F.blend]
      every model's motions as Actions (ttyd_blender.build_model on the model
      built from its parts), read back through the writer, into the container
  blender --background --python tools/port/sl_models.py -- repack VARIANT_MODULE...
      only those variants' entries, the other models as they are
  blender --background --python tools/port/sl_models.py -- sheet NAME OUT_DIR [MOTION...]
      a contact sheet per motion (the scene camera, front on), to review
  blender --background --python tools/port/sl_models.py -- blend NAME OUT.blend
      the authoring scene for one model
  python tools/port/sl_models.py cards NAME
      which texture image each card of a model shows (recipes replace them by number)
  python tools/port/sl_models.py list
      what the container holds
"""
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

MODELS = ["sl_crepe:CREPE", "sl_eclair:ECLAIR", "sl_marron:MARRON", "sl_hizza:FRONT", "sl_hizza:TAIL", "sl_hizza:WHOLE", "sl_kart:KART", "sl_kart:WRECK", "sl_chestnut:KING", "sl_cranberry:CRANBERRY", "sl_fuselle:FUSELLE", "sl_icewall:WALL", "sl_icewall:WALL_LARGE", "sl_ch2_props:BANNER", "sl_ch2_props:CAGE", "sl_ch3_props:CROWN", "sl_ch3_props:MIC", "sl_ch3_props:LIGHTS", "sl_ch3_props:SCOREBOARD", "sl_ch3_props:TROPHY", "sl_ch3_props:WHEEL", "sl_ch3_props:TARP", "sl_ch3_props:SHUTTER"] + ["sl_ch4_props:" + n for n in ("FIERY_HAT", "CHANDELIER", "SET_ROAD", "SET_BALLROOM", "SET_VOLCANO", "LEAF", "GRASS_BANNER", "TIARA", "BARON_HAT")] + ["sl_ch5_props:" + n for n in (
    "SOCKET_MOON", "SOCKET_SUN", "SOCKET_TEAR", "MURAL", "GEAR_GATE", "SIGNPOST", "EDGE_25", "EDGE_50", "EDGE_100",
    "EDGE_200", "EDGE_25_V", "EDGE_50_V", "EDGE_100_V", "EDGE_200_V")] + ["sl_ch6:NIGHTMARE", "sl_ch6:GOO_PIT", "sl_ch6:GOO_MOAT", "sl_ch6:TORCH"] + [
    "sl_kart:KART_" + c.upper() for c in ("red", "blue", "yellow", "purple", "orange", "white", "black")]  # module[:object with NAME, build, MOTIONS]
# variants of a model the port makes from the disc (sl_attach.py): NAME, BASE, cards(), MOTIONS
VARIANTS = ["sl_veil", "sl_kart_luigi", "sl_crown_luigi", "sl_grass", "sl_bulkhead", "sl_petunia"]
DISC = os.path.join(HERE, "..", "..", "data", "G8ME01", "files", "a")
if not os.path.isdir(DISC):  # a worktree without the junction: the main checkout beside it
    DISC = os.path.join(HERE, "..", "..", "..", "ttyd-port", "data", "G8ME01", "files", "a")
PACK = os.path.join(HERE, "..", "..", "port", "assets", "super_luigi", "sl_models.bin")
WORK = os.path.join(HERE, "..", "..", "build", "sl_models")


def _camera(height=64.0, width=None, res=(512, 512), center=(0.0, 0.0)):
    """an orthographic camera on the model's front (the game's +z), Blender's -y
    under the model root's turn; transparent film, the game's 60 frames a second"""
    import bpy
    sc = bpy.context.scene
    cam = bpy.data.objects.get("sl_camera")
    if cam is None:
        cam = bpy.data.objects.new("sl_camera", bpy.data.cameras.new("sl_camera"))
        sc.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(height, width or height)
    cam.location = (center[0], -200.0, center[1] + height / 2.0)
    cam.rotation_euler = (1.5707963, 0.0, 0.0)
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.film_transparent = True
    for eng in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            sc.render.engine = eng
            break
        except TypeError:
            continue
    sc.render.fps = 60
    sc.view_settings.view_transform = "Standard"
    return cam


def load(modname):
    """a model's module, or an object in it ("module:NAME")"""
    m, _s, attr = modname.partition(":")
    mod = importlib.import_module(m)
    return getattr(mod, attr) if attr else mod


def model(modname, work=WORK):
    """(in Blender) the model's editable object tree, built from its parts, its
    motions as Actions; returns (module, root)"""
    import bpy
    import luigi_motions
    import ttyd_blender
    mod = load(modname)
    root = bpy.data.objects.get(mod.NAME)
    if root is None:
        os.makedirs(work, exist_ok=True)
        mdl, tex = mod.build()
        path = os.path.join(work, mod.NAME)
        open(path, "wb").write(mdl)
        open(path + "-", "wb").write(tex)
        root = ttyd_blender.build_model(path, path + "-", os.path.join(work, "tex", mod.NAME),
                                        location=(120 * MODELS.index(modname), 0, 0))
        _all_binds(root, path)
    luigi_motions.build_all(mod.MOTIONS, None, root)
    return mod, root


def _all_binds(root, path):
    """every drawing of each texture slot in its material (build_model finds a
    slot's drawings from the model's animations; the model built from its
    parts has none yet), so the "tex" channel shows in Blender"""
    import ttyd_anim as T
    import ttyd_blender as TB
    pose = T.PoseFile.parse(open(path, "rb").read())
    geo = T.Geometry(pose)
    images = TB.load_textures(path + "-", os.path.join(os.path.dirname(path), "tex", root["ttyd_prefix"]),
                              root["ttyd_prefix"])
    starts = [b for b, _w in geo.texdata] + [len(geo.texbinds)]
    for slot, (b, _w) in enumerate(geo.texdata):
        end = min(s for s in starts if s > b) if any(s > b for s in starts) else len(geo.texbinds)
        if end - b > 1:
            TB.make_material("%s:tex%d" % (root["ttyd_prefix"], slot), root, slot, list(range(b, end)), b, geo,
                             images)


def files(modname, work=WORK):
    """(in Blender) [(name, bytes)]: the model with its motions, and its textures"""
    import luigi_motions
    mod, root = model(modname, work)
    built = luigi_motions.anims(mod.MOTIONS, root=root)
    worst = max([v for _x, r in built for v in r["max_error"].values()] or [0.0])
    print("%-16s %2d poses, %3d keys, largest writer error %.4f" % (mod.NAME, len(built),
                                                                  sum(len(x.keys) for x, _r in built), worst))
    mdl, tex = mod.build([x for x, _r in built])
    os.makedirs(work, exist_ok=True)
    for n, b in ((mod.NAME, mdl), (mod.NAME + "-", tex)):
        open(os.path.join(work, "out_" + n), "wb").write(b)  # (a copy, to look at)
    return [(mod.NAME, mdl), (mod.NAME + "-", tex)]


def luigi_base(disc=DISC):
    """c_luigi_pc and its textures, as the port makes them at startup"""
    import luigi_pack
    rd = lambda n: open(os.path.join(disc, n), "rb").read()
    lp = open(os.path.join(HERE, "..", "..", "port", "assets", "luigi", "luigi_anims.bin"), "rb").read()
    return luigi_pack.merge(rd("c_luigi"), rd("c_luigi-"), rd("a_mario-"), lp)


def variant_base(mod, disc=DISC):
    """(model, textures) a variant goes on: c_luigi_pc as the port makes it,
    else the disc's own file (the port reads it from the disc too)"""
    if mod.BASE == "c_luigi_pc":
        return luigi_base(disc)
    import ttyd_anim
    ttyd_anim.STRICT = False  # a disc model whose animations don't rewrite byte for byte: only its tables matter here
    rd = lambda n: open(os.path.join(disc, n), "rb").read()
    return rd(mod.BASE), rd(mod.BASE + "-")


def variant_model(mod, base, btex, work=WORK):
    """(in Blender) the variant (without motions of its own) as an editable
    object tree, its motions as Actions; returns the root"""
    import bpy
    import luigi_motions
    import sl_attach
    import ttyd_blender
    root = bpy.data.objects.get(mod.NAME)
    if root is None:
        pk, tpl = sl_attach.variant(base, btex, mod.cards(), swaps=getattr(mod, "SWAPS", None))
        model, tex = sl_attach.merge(base, btex, pk, tpl, mod.NAME)
        os.makedirs(work, exist_ok=True)
        path = os.path.join(work, mod.NAME)
        open(path, "wb").write(model)
        open(path + "-", "wb").write(tex)
        ttyd_blender.ALLOW_PIVOT_TRANSLATIONS = True  # only the variant's own motions are read back
        root = ttyd_blender.build_model(path, path + "-", os.path.join(work, "tex", mod.NAME), actions=False,
                                        location=(-120, 0, 0))
        _all_binds(root, path)
    luigi_motions.build_all(mod.MOTIONS, None, root)
    return root


def variant_anims(mod, base, btex, work=WORK):
    """(in Blender) the variant's motions, read back through the writer"""
    import luigi_motions
    root = variant_model(mod, base, btex, work)
    built = luigi_motions.anims(mod.MOTIONS, root=root)
    worst = max([v for _x, r in built for v in r["max_error"].values()] or [0.0])
    print("%-16s %2d poses, %3d keys, largest writer error %.4f" % (mod.NAME, len(built),
                                                                  sum(len(x.keys) for x, _r in built), worst))
    return [x for x, _r in built]


def variant_files(modname, disc=DISC, work=WORK, anims=()):
    """[(entry name, bytes)]: a variant's pack and drawings; the merged files in work/out_*"""
    import sl_attach
    mod = importlib.import_module(modname)
    base, btex = variant_base(mod, disc)
    if not anims and getattr(mod, "MOTIONS", None) and "bpy" in sys.modules:
        anims = variant_anims(mod, base, btex, work)
    pk, tpl = sl_attach.variant(base, btex, mod.cards(), anims, getattr(mod, "SWAPS", None))
    model, tex = sl_attach.merge(base, btex, pk, tpl, mod.NAME)
    os.makedirs(work, exist_ok=True)
    open(os.path.join(work, "out_" + mod.NAME), "wb").write(model)
    open(os.path.join(work, "out_" + mod.NAME + "-"), "wb").write(tex)
    return [("%s@%s" % (mod.NAME, mod.BASE), pk), ("%s-@%s-" % (mod.NAME, mod.BASE), tpl)]


def pack(save=None):
    import bpy
    import luigi_pack
    entries = []
    for m in MODELS:
        entries += files(m)
    for m in VARIANTS:
        entries += variant_files(m)
    luigi_pack.write_friends(PACK, entries)
    print("wrote %s: %s" % (os.path.normpath(PACK), ", ".join(n for n, _b in entries)))
    if save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(save))


def repack(mods):
    """(in Blender) only these variants' entries in the container, the rest as they are"""
    import luigi_pack
    entries = luigi_pack.read_friends(open(PACK, "rb").read())
    for m in mods:
        name = load(m).NAME
        at = [i for i, (n, _b) in enumerate(entries) if n == name or n.startswith(name + "@")]
        entries = [(n, b) for n, b in entries if n not in (name, name + "-") and
                   not n.startswith(name + "@") and not n.startswith(name + "-@")]
        entries[at[0] if at else len(entries):at[0] if at else len(entries)] = files(m) if ":" in m else variant_files(m)
    luigi_pack.write_friends(PACK, entries)
    print("wrote %s: %d entries" % (os.path.normpath(PACK), len(entries)))


def repack_friend(modname):
    """(in Blender) a whole model of the friends' pack (c_lp_statue) from its module"""
    import luigi_pack
    path = os.path.join(HERE, "..", "..", "port", "assets", "luigi", "friend_anims.bin")
    mod = load(modname)
    if modname not in MODELS:
        MODELS.append(modname)
    new = dict(files(modname))
    entries = [(n, new.get(n, b)) for n, b in luigi_pack.read_friends(open(path, "rb").read())]
    luigi_pack.write_friends(path, entries)
    print("wrote %s: %s" % (os.path.normpath(path), mod.NAME))


def sheet(modname, out_dir, motions=None, n=12):
    import ttyd_blender as TB
    if modname in VARIANTS:
        mod = importlib.import_module(modname)
        base, btex = variant_base(mod)
        root = variant_model(mod, base, btex)
    else:
        mod, root = model(modname)
    cam = dict(getattr(mod, "CAMERA", {}))
    cx, cy = cam.get("center", (0.0, 0.0))
    cam["center"] = (cx + root.location.x, cy)
    _camera(**cam)
    os.makedirs(out_dir, exist_ok=True)
    for mo in motions or list(mod.MOTIONS):
        TB.set_anim(root, mo)
        end = mod.MOTIONS[mo]["end"]
        frames = sorted(set(round(end * i / (n - 1.0)) for i in range(n))) if end else [0]
        TB.contact_sheet(frames, os.path.join(out_dir, "%s_%s.png" % (mod.NAME, mo)), cols=6)
        print("sheet", mo)


def cards(name):
    """["<image> WxH <shape> [<bind>]"]: which texture image of a model each card shows: the numbers a
    recipe's art/models/<model>/<n>.png replaces (art/RECIPES.md). The model comes from our container,
    the friends' pack (c_lp_statue) or else the disc; a variant's images come after its base's."""
    import struct
    import luigi_pack
    import ttyd_anim as T
    T.STRICT = False
    entries = dict(luigi_pack.read_friends(open(PACK, "rb").read()))
    entries.update(luigi_pack.read_friends(open(os.path.join(HERE, "..", "..", "port", "assets", "luigi",
                                                             "friend_anims.bin"), "rb").read()))
    variant = [k for k in entries if k.startswith(name + "@")]
    if variant:
        mod = importlib.import_module(next(m for m in VARIANTS if importlib.import_module(m).NAME == name))
        m, t = variant_base(mod)
        import sl_attach
        m, t = sl_attach.merge(m, t, entries[variant[0]], entries[variant[0].replace("@", "-@") + "-"], name)
    elif name in entries:
        m, t = entries[name], entries[name + "-"]
    else:
        rd = lambda n: open(os.path.join(DISC, n), "rb").read()
        m, t = rd(name), rd(name + "-")
    geo = T.Geometry(T.PoseFile.parse(m))
    n, tab = struct.unpack_from(">II", t, 4)
    size = lambda k: struct.unpack_from(">HH", t, struct.unpack_from(">I", t, tab + 8 * k)[0])
    starts = sorted(b for b, _w in geo.texdata) + [len(geo.texbinds)]
    out = {k: [] for k in range(n)}
    for sh in geo.shapes:
        for di in range(sh["draw_start"], sh["draw_start"] + sh["draw_count"]):
            for slot in geo.draws[di]["tex_data"]:
                if slot < 0:
                    continue
                b = geo.texdata[slot][0]
                for j in range(b, min(s for s in starts if s > b)):
                    out[geo.texbinds[j][0]].append("%s[%d]" % (sh["name"].replace("Shape", ""), j - b))
    return ["%3d %3dx%-3d %s" % (k, size(k)[1], size(k)[0], " ".join(sorted(set(v)))) for k, v in out.items()]


def main(argv):
    if argv and argv[0] == "list":
        import luigi_pack
        for n, b in luigi_pack.read_friends(open(PACK, "rb").read()):
            print("%-24s %7d bytes" % (n, len(b)))
        return 0
    if argv and argv[0] == "repack_friend":
        repack_friend(argv[1])
        return 0
    if argv and argv[0] == "repack":
        repack(argv[1:])
        return 0
    if argv and argv[0] == "cards":
        for row in cards(argv[1]):
            print(row)
        return 0
    if argv and argv[0] == "pack":
        pack(argv[argv.index("--save") + 1] if "--save" in argv else None)
        return 0
    if argv and argv[0] == "sheet":
        sheet(argv[1], argv[2], argv[3:] or None)
        return 0
    if argv and argv[0] == "blend":
        import bpy
        model(argv[1])
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(argv[2]))
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    av = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    rc = main(av)
    if "bpy" not in sys.modules:
        sys.exit(rc)
