#!/usr/bin/env python3
"""Run every decodable file of an extracted disc through the PC loaders.

Walks <data>/files and feeds disc_sweep (tests/port/disc_sweep.c) one
"kind path" line per file: a/* anim models (a/*- are their TPLs), m/*/d maps, m/*/c camera roads,
m/*/t and *.tpl textures, *.bfn fonts. All failures are reported in one run.

usage: disc_sweep.py SWEEP_EXE DATA_DIR      (DATA_DIR: data/G8MJ01 or its files/)
Exits 77 (ctest: skipped) when the disc has not been extracted.
"""
import os
import subprocess
import sys

# Files that may fail: data the retail game never loads.
EXPECTED = {
    "a/OFF_hei_01b": "pre-release model format (0x68-byte shapes); hei_01 loads OFF_hei_01c",
}


def files_of(root):
    out = []
    for d, _, names in os.walk(root):
        rel = os.path.relpath(d, root).replace("\\", "/")
        for n in sorted(names):
            p = os.path.join(d, n)
            parts = rel.split("/")
            if parts[0] == "a" and len(parts) == 1 and "." not in n:
                out.append(("tpl" if n.endswith("-") else "anim", p))  # a/<name>- is the model's TPL
            elif parts[0] == "m" and len(parts) == 2 and n in ("d", "c", "t"):
                out.append(({"d": "map", "c": "camroad", "t": "tpl"}[n], p))
            elif n.endswith(".tpl"):
                out.append(("tpl", p))
            elif n.endswith(".bfn"):
                out.append(("font", p))
    return sorted(out)


def main():
    exe, data = sys.argv[1], sys.argv[2]
    root = os.path.join(data, "files") if os.path.isdir(os.path.join(data, "files")) else data
    if not os.path.isdir(root):
        print(f"disc_sweep: no extracted disc at {data}: skipped")
        return 77
    work = "".join(f"{k} {p}\n" for k, p in files_of(root))
    r = subprocess.run([os.path.abspath(exe)], input=work, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode not in (0, 1):
        print(r.stdout, end="")
        last = [l for l in r.stderr.splitlines() if l.startswith("@ ")][-1:]
        print(f"disc_sweep: crashed (exit {r.returncode:#x}) on {last[0][2:] if last else '?'}")
        return r.returncode
    unexpected = 0
    for line in r.stdout.splitlines():
        if line.startswith("FAIL "):
            path = os.path.relpath(line.split()[2].rstrip(":"), root).replace("\\", "/")
            if path in EXPECTED:
                line = f"expected {line[5:]}  [{EXPECTED[path]}]"
            else:
                unexpected += 1
        print(line)
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
