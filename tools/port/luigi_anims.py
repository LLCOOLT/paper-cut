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
MODELS = ["c_luigi", "a_mario", "a_mario_r", "e_mario", "b_mario"]
DATA = os.path.join(REPO, "data", "luigi")
PACK = os.path.join(REPO, "port", "assets", "luigi", "luigi_anims.bin")


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
    for name in ("check", "blend", "roundtrip", "pack", "portcheck"):
        p = sub.add_parser(name)
        p.add_argument("--disc", default=os.path.join(DATA, "disc"))
        if name == "blend":
            p.add_argument("--save")
        if name == "portcheck":
            p.add_argument("dump", help="the file $TTYD_LUIGI_DUMP named in a game run")
    a = ap.parse_args(argv)
    return {"extract": cmd_extract, "check": cmd_check, "blend": cmd_blend, "roundtrip": cmd_roundtrip,
            "pack": cmd_pack, "portcheck": cmd_portcheck}[a.cmd](a)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    sys.exit(main(argv) or 0)
