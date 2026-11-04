#!/usr/bin/env python3
"""The art recipe runner (port/src/common/art_recipe.c, built as art_recipe_test) against the
authoring tool (port/assets/super_luigi/art/tools/recipes.py): every recipe (the game's, the
examples and tests/port/art_recipes/) on each disc, the two images compared texel for texel,
RGBA. Exits 1 on any difference or on an image only one of them makes.

usage: art_recipe_check.py [--disc VERSION=DIR=EXE ...]
  default: for G8MJ01, G8ME01 and G8MP01, the extracted disc data/<ID> (of this checkout or the main
  one beside it) and build-pc-<jp|us|eu>-aurora/art_recipe_test.exe, where both are there.
Outputs go to a temporary folder (they're made from the disc: never into the repo).
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
ART = os.path.join(ROOT, "port", "assets", "super_luigi", "art")
BUILDS = {"G8MJ01": "jp", "G8ME01": "us", "G8MP01": "eu"}


def recipes():
    return sorted(glob.glob(os.path.join(ART, "recipes", "*.recipe")) +
                  glob.glob(os.path.join(ART, "recipes", "examples", "*.recipe")) +
                  glob.glob(os.path.join(HERE, "art_recipes", "*.recipe")))


def pixels(path):
    im = Image.open(path).convert("RGBA")
    return im.size, im.tobytes()


def outputs(folder):
    out = {}
    for root, _, files in os.walk(folder):
        for f in files:
            if f.endswith(".png") and f != "preview.png":
                p = os.path.join(root, f)
                out[os.path.relpath(p, folder).replace(os.sep, "/")] = p
    return out


def check(gid, disc, exe, files, tmp):
    c_out, py_out = os.path.join(tmp, gid, "c"), os.path.join(tmp, gid, "py")
    rc = subprocess.run([exe, disc, c_out] + files, capture_output=True, text=True)
    rp = subprocess.run([sys.executable, "-I", os.path.join(ART, "tools", "recipes.py"), "--disc", disc,
                         "--out", py_out, "--zoom", "0"] + files, capture_output=True, text=True)
    c, py = outputs(c_out), outputs(py_out)
    bad = 0
    for name in sorted(set(c) | set(py)):
        if name not in c or name not in py:
            print(f"  {gid} {name}: only the {'C runner' if name in c else 'Python tool'} made it")
            bad += 1
            continue
        (sa, pa), (sb, pb) = pixels(c[name]), pixels(py[name])
        if sa != sb:
            print(f"  {gid} {name}: {sa} (C) against {sb} (Python)")
            bad += 1
        elif pa != pb:
            n = sum(pa[i:i + 4] != pb[i:i + 4] for i in range(0, len(pa), 4))
            print(f"  {gid} {name}: {n} texels differ")
            bad += 1
    fails_c = [ln for ln in rc.stdout.splitlines() if ": line " in ln or "isn't" in ln]
    fails_py = [ln for ln in rp.stdout.splitlines() if ": line " in ln]
    print(f"{gid}: {len(c)} images by C, {len(py)} by Python, {bad} differences")
    for ln in fails_c:
        print("  C: " + ln)
    for ln in fails_py:
        print("  Python: " + ln)
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc", action="append", default=[], help="VERSION=DIR=EXE")
    a = ap.parse_args()
    runs = []
    for spec in a.disc:
        gid, disc, exe = spec.split("=", 2)
        runs.append((gid, os.path.abspath(disc), os.path.abspath(exe)))
    if not runs:
        for gid, short in BUILDS.items():
            exe = os.path.join(ROOT, f"build-pc-{short}-aurora", "art_recipe_test.exe")
            if not os.path.exists(exe):
                exe = exe[:-4]
            for base in (ROOT, os.path.join(os.path.dirname(ROOT), "ttyd-port")):
                disc = os.path.join(base, "data", gid)
                if os.path.isdir(disc) and os.path.exists(exe):
                    runs.append((gid, disc, exe))
                    break
    if not runs:
        sys.exit("no disc and runner to check (see --help)")
    files = recipes()
    with tempfile.TemporaryDirectory(prefix="art-recipes-") as tmp:
        bad = sum(check(gid, disc, exe, files, tmp) for gid, disc, exe in runs)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
