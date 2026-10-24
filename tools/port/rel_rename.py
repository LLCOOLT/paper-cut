#!/usr/bin/env python3
"""Prepare one REL's objects for linking into the executable (TTYD_RELS_STATIC).

On the GameCube each REL is linked on its own, so different RELs (and the
DOL) may define the same global names. Linked into one executable they would
clash, so every global symbol a REL defines is renamed to rel_<area>__<name>
in all of that REL's objects (definitions and the REL's own references), and
the renamed objects are archived for a whole-archive link. References to
symbols the REL does not define (the DOL's) are left alone.

usage: rel_rename.py --rel NAME --out LIB [--nm llvm-nm] [--objcopy llvm-objcopy]
                     [--ar llvm-ar] OBJ...
"""
import argparse
import os
import subprocess
import sys


def defined_globals(nm, obj):
    out = subprocess.run([nm, "--defined-only", "--extern-only", "--format=posix", obj],
                         capture_output=True, text=True, check=True).stdout
    names = set()
    for line in out.splitlines():
        parts = line.split()
        # posix format: name type [value [size]]; skip absolute/feature symbols
        if len(parts) >= 2 and parts[1] not in ("a", "A") and not parts[0].startswith("@"):
            names.add(parts[0])
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rel", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--nm", default="llvm-nm")
    ap.add_argument("--objcopy", default="llvm-objcopy")
    ap.add_argument("--ar", default="llvm-ar")
    ap.add_argument("objs", nargs="+")
    a = ap.parse_args()

    names = set()
    for obj in a.objs:
        names |= defined_globals(a.nm, obj)
    prefix = f"rel_{a.rel}__"
    work = a.out + ".d"
    os.makedirs(work, exist_ok=True)
    mapfile = os.path.join(work, "rename.txt")
    with open(mapfile, "w", newline="\n") as f:
        for n in sorted(names):
            f.write(f"{n} {prefix}{n}\n")

    renamed = []
    for i, obj in enumerate(a.objs):
        dst = os.path.join(work, f"{i:04d}_{os.path.basename(obj)}")
        subprocess.run([a.objcopy, f"--redefine-syms={mapfile}", obj, dst], check=True)
        renamed.append(dst)
    if os.path.exists(a.out):
        os.remove(a.out)
    subprocess.run([a.ar, "rcs", a.out, *renamed], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
