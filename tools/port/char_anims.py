#!/usr/bin/env python3
"""Peach's and Bowser's new animations for Play as (docs/PLAY_AS.md): the
Blender scene and the packs, as luigi_anims.py does Luigi's.

In Blender (`blender -b --python tools/port/char_anims.py -- pack --char peach`):

  char_anims.py pack --char peach|bowser [--disc DIR] [--save FILE.blend]
      each of the character's models (Peach: c_peach and her back-facing
      c_peach_r; Bowser: c_koopa and his back view made over it,
      bowser_back.py) with the hammer added, as an editable object tree; every
      motion of peach_motions.py / bowser_motions.py built on it as an
      Action, read back through the writer into the committed pack
      (port/assets/luigi/<model>_anims.bin)

DIR holds the disc's files (c_peach, c_peach-, c_peach_r, c_peach_r-,
c_koopa, c_koopa-, a_mario-; default data/pb/disc). Nothing from the disc
goes into the repository: what these write from it is under data/.
Review without Blender: char_sheet.py --char peach --disc DIR OUT.png.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
DATA = os.path.join(REPO, "data", "pb")


def cmd_pack(a):
    import bpy
    import importlib
    import char_motions
    import luigi_pack
    import ttyd_anim as T
    import ttyd_blender
    mod = importlib.import_module("%s_motions" % a.char)
    gen = os.path.join(DATA, "gen")
    os.makedirs(gen, exist_ok=True)
    for rig, motions in mod.MODELS:
        if a.only and rig.char not in a.only.split(","):
            continue
        ch = luigi_pack.CHARS[rig.char]
        # the model with the hammer alone (an empty pack), which the motions are built on
        empty = os.path.join(gen, ch.key + "_empty.bin")
        base = T.PoseFile.parse(open(os.path.join(a.disc, ch.base), "rb").read())
        luigi_pack.write_pack(empty, [], base, ch)
        res = luigi_pack.merge_files(a.disc, ch, empty)
        if res is None:
            sys.exit("%s isn't the model the packs are made for" % ch.base)
        for name, blob in zip((ch.model_name, ch.model_name + "-"), res):
            with open(os.path.join(gen, name), "wb") as f:
                f.write(blob)
        ttyd_blender.build_model(os.path.join(gen, ch.model_name), os.path.join(gen, ch.model_name + "-"),
                                 os.path.join(DATA, "tex", ch.model_name), actions=False)
        rep = char_motions.export_pack(rig, motions, a.disc)
        for name, r in rep.items():
            print("%-10s keys %3d  added %s  max error %s" % (name, r["keys"], r["added"], r["max_error"]))
        print("%s: %d animations -> %s" % (ch.model_name, len(rep), ch.pack))
    if a.save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(a.save))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--char", required=True, choices=("peach", "bowser"))
    p.add_argument("--disc", default=os.path.join(DATA, "disc"))
    p.add_argument("--only", help="only these models (luigi_pack keys: peach, peach_r, bowser, bowser_r)")
    p.add_argument("--save")
    a = ap.parse_args(argv)
    return {"pack": cmd_pack}[a.cmd](a)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    sys.exit(main(argv) or 0)
