#!/usr/bin/env python3
"""Link census for the PC build: which symbols the game objects need that
nothing defines (the SDK shim work list), and which are defined twice
(clashes from linking all RELs statically).

usage: tools/port/linkcensus.py [build-pc] [--undef] [--dups]
"""
import collections, os, subprocess, sys

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    build = args[0] if args else "build-pc"
    objs = []
    for dp, _, fns in os.walk(build):
        if "/rel_" in dp:
            continue  # per-file REL objects; the pre-linked build-pc/rels/*.o count
        objs += [os.path.join(dp, f) for f in fns if f.endswith((".o", ".obj"))]
    defined, undef = collections.defaultdict(list), collections.defaultdict(list)
    for o in objs:
        out = subprocess.run(["nm", "-P", o], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 2: continue
            name, kind = parts[0], parts[1]
            if kind == "U": undef[name].append(o)
            elif kind in "TDBRVW": defined[name].append(o)  # global, non-common
    missing = sorted(set(undef) - set(defined))
    dups = {k: v for k, v in defined.items() if len(v) > 1}
    if "--undef" in sys.argv:
        for s in missing: print(f"{len(undef[s]):4d} {s}")
    if "--dups" in sys.argv:
        for s, v in sorted(dups.items()):
            print(s, " ".join(os.path.basename(x) for x in v))
    print(f"objects: {len(objs)}  undefined: {len(missing)}  duplicate definitions: {len(dups)}")

if __name__ == "__main__":
    main()
