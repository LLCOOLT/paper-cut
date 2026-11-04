#!/usr/bin/env python3
"""Luigi's new animations: the disc files, the Blender scene and the round
trip (docs/LUIGI_ANIMS.md).

  luigi_anims.py extract DISC.iso [--out data/luigi/disc]
      the model files these tools read, from the user's own disc
  luigi_anims.py check [--disc data/luigi/disc]
      each model decoded and written again, byte for byte
  luigi_anims.py portcheck DUMP
      the c_luigi_pc a game run made ($TTYD_LUIGI_DUMP=DUMP) against
      luigi_pack.merge of the disc and the committed pack, byte for byte
  luigi_anims.py friends-portcheck DIR
      the friends' models a game run made ($TTYD_FRIENDS_DUMP=DIR) against
      luigi_pack.merge_friends, byte for byte

In Blender (live, or `blender --python tools/port/luigi_anims.py -- ...`):

  luigi_anims.py blend [--disc DIR] [--save FILE.blend]
      the authoring scene from the disc and the committed data alone:
      c_luigi_pc (Luigi with the hammer) as an editable object tree, his
      animations and every motion of luigi_motions.py as Actions, Mario's
      field set (a_mario) beside him as a baked reference
  luigi_anims.py pack
      the motions' Actions, through the writer, into the committed pack
  luigi_anims.py roundtrip
      Blender's pose against the game's at every half frame, and the
      model file written back from the Actions, against the disc's bytes
  luigi_anims.py friends-blend [--disc DIR] [--save FILE.blend]
      Luigi's five friends (friend_motions.py) as editable models, side by
      side, with every motion as an Action
  luigi_anims.py friends-pack
      their motions' Actions, through the writer, into the committed
      port/assets/luigi/friend_anims.bin

Nothing from the disc goes into the repository: everything these write is
under data/ (ignored by git).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

DISC_FILES = ["a/c_luigi", "a/c_luigi-", "a/a_mario", "a/a_mario-", "a/a_mario_r", "a/a_mario_r-",
              "a/e_mario", "a/b_mario", "a/ag2tg.bin"]
FRIEND_MODELS = ["c_lp_gesso", "c_lp_bom", "c_lp_met", "c_lp_pansy", "c_lp_moa"]
DISC_FILES += ["a/%s%s" % (m, s) for m in FRIEND_MODELS for s in ("", "-")]
MODELS = ["c_luigi", "a_mario", "a_mario_r", "e_mario", "b_mario"]
DATA = os.path.join(REPO, "data", "luigi")
PACK = os.path.join(REPO, "port", "assets", "luigi", "luigi_anims.bin")
FRIENDS_PACK = os.path.join(REPO, "port", "assets", "luigi", "friend_anims.bin")
# new models of our own (paper_model.py), each a module with NAME, parts(),
# build(anims) and MOTIONS; shipped whole in the friends' container
NEW_MODELS = ["statue_model"]


def cmd_extract(a):
    import gcdisc
    d = gcdisc.Disc(a.disc)
    for p in DISC_FILES:
        dst = os.path.join(a.out, p.split("/", 1)[1])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(d.read(p))
    print(f"{len(DISC_FILES)} files from {d.game_id} in {a.out}")


def cmd_check(a):
    import ttyd_anim
    return ttyd_anim.main(["check"] + [os.path.join(a.disc, m) for m in MODELS])


def cmd_blend(a):
    """the authoring scene, from the disc and the committed data alone: Luigi's
    c_luigi_pc (with the hammer) as an editable object tree, every motion of
    luigi_motions.py as an Action, Mario's field set baked beside him"""
    import bpy
    import luigi_motions
    import luigi_pack
    import ttyd_blender
    disc = a.disc
    gen = os.path.join(DATA, "gen")
    rd = lambda n: open(os.path.join(disc, n), "rb").read()
    res = luigi_pack.merge(rd("c_luigi"), rd("c_luigi-"), rd("a_mario-"), open(PACK, "rb").read())
    if res is None:
        sys.exit("c_luigi isn't the model the pack was made for")
    os.makedirs(gen, exist_ok=True)
    for name, blob in zip((luigi_pack.MODEL_NAME, luigi_pack.MODEL_NAME + "-"), res):
        with open(os.path.join(gen, name), "wb") as f:
            f.write(blob)
    luigi = ttyd_blender.build_model(os.path.join(gen, luigi_pack.MODEL_NAME),
                                     os.path.join(gen, luigi_pack.MODEL_NAME + "-"),
                                     os.path.join(DATA, "tex", luigi_pack.MODEL_NAME))
    luigi_motions.build_all(luigi_motions.MOTIONS)
    mario = ttyd_blender.build_baked(os.path.join(disc, "a_mario"), os.path.join(disc, "a_mario-"),
                                     os.path.join(DATA, "tex", "a_mario"), os.path.join(DATA, "pc2"),
                                     location=(-20, 0, 0))
    ttyd_blender.set_baked_anim(mario, "M_S_1")
    ttyd_blender.set_anim(luigi, "S_1")
    if a.save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(a.save))


def cmd_pack(a):
    """(in Blender) the motions' Actions, read back through the writer, into the committed pack"""
    import luigi_motions
    rep = luigi_motions.export_pack(luigi_motions.MOTIONS, PACK, os.path.join(a.disc, "c_luigi"))
    for name, r in rep.items():
        print("%-8s keys %3d  max error %s" % (name, r["keys"], r["max_error"]))


def cmd_portcheck(a):
    """the model the port made (its $TTYD_LUIGI_DUMP) against luigi_pack.merge, byte for byte"""
    import luigi_pack
    rd = lambda n: open(os.path.join(a.disc, n), "rb").read()
    res = luigi_pack.merge(rd("c_luigi"), rd("c_luigi-"), rd("a_mario-"), open(PACK, "rb").read())
    same = res is not None and open(a.dump, "rb").read() == res[0]
    print("the port's c_luigi_pc %s the Python merge" % ("matches" if same else "DIFFERS from"))
    return 0 if same else 1


def cmd_friends_blend(a):
    """the friends' authoring scene: each model from the disc, side by side, its
    motions (friend_motions.py) as Actions"""
    import bpy
    import friend_motions
    import ttyd_blender
    for i, m in enumerate(FRIEND_MODELS):
        model, tpl = friend_base(m, a.disc, a.work)
        root = ttyd_blender.build_model(model, tpl, os.path.join(a.work, "tex", m), location=(60 * i, 0, 0))
        if m in friend_motions.FRIENDS:
            friend_motions.build(m, root)
    for modname in NEW_MODELS:
        new_model(modname, a.work)
    if a.save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(a.save))


def cmd_friends_pack(a):
    """(in Blender) every friend's motions, read back through the writer, into the committed pack"""
    import friend_motions
    import luigi_pack
    import ttyd_anim
    packs = []
    for m in FRIEND_MODELS:
        if m not in friend_motions.FRIENDS:
            continue
        built = friend_motions.anims(m)
        base = ttyd_anim.PoseFile.parse(open(os.path.join(a.disc, m), "rb").read())
        recs, patches, tx = friend_extras_for(m, a.disc)
        packs.append((m, luigi_pack.pack_bytes([x for x, _r in built], base, recs, patches)))
        if tx is not None:
            packs.append((m + "-", tx))
        for x, r in built:
            print("%-10s %-10s keys %3d  max error %s" % (m, x.name, len(x.keys),
                                                     {k: round(v, 4) for k, v in r["max_error"].items()}))
    for modname in NEW_MODELS:
        packs += new_model_files(modname, a.work)
    luigi_pack.write_friends(FRIENDS_PACK, packs)


def new_model(modname, work):
    """(in Blender) a new model's editable object tree, built (once) from its parts
    alone, its motions as Actions; returns (module, root)"""
    import importlib
    import bpy
    import luigi_motions
    import ttyd_blender
    mod = importlib.import_module(modname)
    root = bpy.data.objects.get(mod.NAME)
    if root is None:
        os.makedirs(work, exist_ok=True)
        model, tex = mod.build()
        path = os.path.join(work, mod.NAME)
        open(path, "wb").write(model)
        open(path + "-", "wb").write(tex)
        root = ttyd_blender.build_model(path, path + "-", os.path.join(work, "tex", mod.NAME),
                                        location=(60 * (len(FRIEND_MODELS) + 1 + NEW_MODELS.index(modname)), 0, 0))
    luigi_motions.build_all(mod.MOTIONS, None, root)
    return mod, root


def new_model_files(modname, work):
    """(in Blender) [(name, file)]: a new model with its motions, and its textures"""
    import luigi_motions
    mod, root = new_model(modname, work)
    built = luigi_motions.anims(mod.MOTIONS, root=root)
    for x, r in built:
        print("%-10s %-10s keys %3d  max error %s" % (mod.NAME, x.name, len(x.keys),
                                                 {k: round(v, 4) for k, v in r["max_error"].items()}))
    model, tex = mod.build([x for x, _r in built])
    out_dir = os.environ.get("TTYD_NEW_MODEL_OUT")  # (a copy of each, to look at)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        open(os.path.join(out_dir, mod.NAME), "wb").write(model)
        open(os.path.join(out_dir, mod.NAME + "-"), "wb").write(tex)
    return [(mod.NAME, model), (mod.NAME + "-", tex)]


def friend_extras_for(m, disc):
    """(records, patches, texture recipe or None) a friend's model gets besides
    its animations (friend_extras.py)"""
    import friend_extras
    import luigi_pack
    import ttyd_anim
    if m not in friend_extras.EXTRAS:
        return None, (), None
    records, textures = friend_extras.EXTRAS[m]
    base = ttyd_anim.PoseFile.parse(open(os.path.join(disc, m), "rb").read())
    tpl = open(os.path.join(disc, m + "-"), "rb").read()
    recs, patches = records(base)
    return recs, patches, luigi_pack.make_tx(tpl, textures(tpl))


def friend_base(m, disc, work):
    """(model path, TPL path) of a friend as the port makes it, without our
    animations: the disc's, or with its extras (parts, made textures) in `work`"""
    import luigi_pack
    import ttyd_anim
    recs, patches, tx = friend_extras_for(m, disc)
    if tx is None:
        return os.path.join(disc, m), os.path.join(disc, m + "-")
    data = open(os.path.join(disc, m), "rb").read()
    base = ttyd_anim.PoseFile.parse(data)
    model = luigi_pack.merge_model(data, luigi_pack.read_pack(luigi_pack.pack_bytes([], base, recs, patches)))
    tpl = luigi_pack.apply_tx(open(os.path.join(disc, m + "-"), "rb").read(), tx)
    os.makedirs(work, exist_ok=True)
    out = os.path.join(work, m)
    open(out, "wb").write(model)
    open(out + "-", "wb").write(tpl)
    return out, out + "-"


def cmd_friends_portcheck(a):
    """the friends' models the port made ($TTYD_FRIENDS_DUMP) against merge_friends, byte for byte"""
    import luigi_pack
    data = open(FRIENDS_PACK, "rb").read()
    res = luigi_pack.merge_friends(a.disc, data)
    whole = {n for n, blob in luigi_pack.read_friends(data) if blob[:4] not in (luigi_pack.MAGIC, luigi_pack.TX_MAGIC)}
    bad = 0
    for m, data in res.items():
        if m in whole:
            continue  # a whole file of our own: added as it is, nothing made
        path = os.path.join(a.dump, m)
        same = data is not None and os.path.exists(path) and open(path, "rb").read() == data
        bad += not same
        print("%-10s %s" % (m, "matches" if same else "DIFFERS (or missing)"))
    return 1 if bad else 0


def cmd_roundtrip(a):
    import bpy
    import ttyd_blender
    root = bpy.data.objects.get("c_luigi_pc") or bpy.data.objects["c_luigi"]
    res = ttyd_blender.roundtrip_check(root)
    for name, r in res.items():
        print("%-6s frames %4d  max matrix error %.2g  visibility mismatches %d  bytes %s" % (
            name, r["frames"], r["max_matrix_error"], r["hidden_mismatch"], "equal" if r["bytes_equal"] else "DIFFER"))
    out = os.path.join(DATA, "roundtrip_c_luigi")
    data = ttyd_blender.file_roundtrip(root, out)
    same = data == open(root["ttyd_model"], "rb").read()
    print("whole file from the Actions: %s" % ("byte for byte" if same else "DIFFERS"))
    return 0 if same and all(r["bytes_equal"] and not r["hidden_mismatch"] for r in res.values()) else 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract")
    p.add_argument("disc")
    p.add_argument("--out", default=os.path.join(DATA, "disc"))
    for name in ("check", "blend", "roundtrip", "pack", "portcheck", "friends-blend", "friends-pack",
                 "friends-portcheck"):
        p = sub.add_parser(name)
        p.add_argument("--disc", default=os.path.join(DATA, "disc"))
        if name in ("blend", "friends-blend"):
            p.add_argument("--save")
        if name in ("friends-blend", "friends-pack"):
            p.add_argument("--work", default=os.path.join(DATA, "gen"),
                           help="where new models' first build goes (their editable scene's source)")
        if name == "portcheck":
            p.add_argument("dump", help="the file $TTYD_LUIGI_DUMP named in a game run")
        if name == "friends-portcheck":
            p.add_argument("dump", help="the folder $TTYD_FRIENDS_DUMP named in a game run")
    a = ap.parse_args(argv)
    return {"extract": cmd_extract, "check": cmd_check, "blend": cmd_blend, "roundtrip": cmd_roundtrip,
            "pack": cmd_pack, "portcheck": cmd_portcheck, "friends-blend": cmd_friends_blend,
            "friends-pack": cmd_friends_pack, "friends-portcheck": cmd_friends_portcheck}[a.cmd](a)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    sys.exit(main(argv) or 0)
