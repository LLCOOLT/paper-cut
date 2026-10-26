#!/usr/bin/env python3
"""Windows: a module-definition file exporting every game symbol from the
executable, so code mods (DLLs) can call the game's functions through its
import library (ttyd-<jp|us>.lib, which the linker writes beside the .exe)
and find its variables (GetProcAddress, ttyd_mod.h's api->find).

Run by CMakeLists.txt before linking the Aurora executable:
  gen_exports.py --nm llvm-nm --out exports.def LIST...
where each LIST is a file naming objects or archives, one per line (the
game's objects, its RELs' archives). Exports: the global symbols they
define, functions as code and the rest as DATA; the def is rewritten only
when it changes (no relink otherwise).
"""
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("lists", nargs="+")
    a = ap.parse_args()
    paths = []
    for lst in a.lists:
        for line in open(lst, encoding="utf-8"):
            line = line.strip()
            if line:
                paths.append(line)
    code, data = set(), set()
    # llvm-nm takes many files at once; batches keep command lines short
    for i in range(0, len(paths), 200):
        r = subprocess.run([a.nm, "--defined-only", "--extern-only", "--format=posix", *paths[i:i + 200]],
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            return 1
        for ln in r.stdout.splitlines():
            parts = ln.split()
            if len(parts) < 2 or parts[0].endswith(":") or not parts[0][0].isalpha() and parts[0][0] != "_":
                continue
            name, kind = parts[0], parts[1]
            if name.startswith(("__imp_", "??", ".")) or "@" in name or "$" in name:
                continue
            if kind == "T":
                code.add(name)
            elif kind in ("D", "B", "R", "C"):
                data.add(name)
    data -= code
    text = "EXPORTS\n" + "".join(f"    {n}\n" for n in sorted(code)) + "".join(f"    {n} DATA\n" for n in sorted(data))
    old = open(a.out, encoding="utf-8").read() if os.path.exists(a.out) else None
    if old != text:
        with open(a.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
