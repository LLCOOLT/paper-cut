#!/usr/bin/env python3
"""Every PC hazard-class diagnostic in every translation unit, all versions at once.

The audit build (-DTTYD_PTRWIDTH_WARNINGS=ON) compiles one version, so code in
the other version's #ifdef blocks is never checked (two US-only pointer
truncations reached the game that way). This takes the compile commands of
one build directory per version, reruns each C file with -fsyntax-only and
the hazard-class warnings (in parallel), and reports them grouped by class,
then by function or message, with the versions each one appears in.

usage: diag_census.py BUILD_DIR [BUILD_DIR ...] [-j N] [--class NAME] [--list]
       e.g. diag_census.py build-pc-jp build-pc-us
Exit status 1 if any class marked as fatal (an error in the build) has hits.
"""
import argparse
import collections
import concurrent.futures
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# warning flag -> (class name, fatal: must stay at zero)
CLASSES = {
    "implicit-function-declaration": ("undeclared call (implicit int)", True),
    "shorten-64-to-32": ("64-bit value narrowed to 32", False),
    "pointer-to-int-cast": ("pointer cast to a narrower int", False),
    "int-to-pointer-cast": ("int cast to a pointer", False),
    "int-conversion": ("int/pointer converted implicitly", False),
    "incompatible-pointer-types": ("incompatible pointer types", False),
    "incompatible-function-pointer-types": ("incompatible function pointer types", False),
    "cast-function-type-strict": ("call through a recast function pointer", False),
    "format": ("printf/scanf format", False),
    "varargs": ("varargs", False),
}
FLAGS = ["-W" + f for f in CLASSES]


def commands(build):
    out = subprocess.run(["ninja", "-C", build, "-t", "commands"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if not re.search(r" -c \S+\.c$", line) or "clang" not in line.split(" ", 1)[0]:
            continue
        args = shlex.split(line, posix=False)
        args[0] = "clang"
        drop = set()
        for i, a in enumerate(args):
            if a in ("-o", "-MF", "-MT"):
                drop.update((i, i + 1))
            elif a in ("-MD", "-c", "-w") or a.startswith("-Wno-") or a.startswith("-Werror"):
                drop.add(i)
        args = [a[1:-1] if a.startswith('"') and a.endswith('"') else a
                for i, a in enumerate(args) if i not in drop]
        yield args + ["-fsyntax-only", "-Wno-everything"] + FLAGS


def run(build, args):
    r = subprocess.run(args, capture_output=True, text=True, cwd=build, errors="replace")
    return r.stderr.splitlines()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("builds", nargs="+")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--class", dest="only", default="", help="flag name, e.g. implicit-function-declaration")
    ap.add_argument("--list", action="store_true", help="every site, not just counts per key")
    a = ap.parse_args()
    # (flag, key) -> {(file, line): set(builds)}
    hits = collections.defaultdict(lambda: collections.defaultdict(set))
    diag = re.compile(r"^(.*?):(\d+):\d+: (?:warning|error): (.*) \[-W([\w-]+)")
    with concurrent.futures.ThreadPoolExecutor(a.j) as pool:
        jobs = [(b, pool.submit(run, b, args)) for b in a.builds for args in commands(b)]
        for build, job in jobs:
            for line in job.result():
                m = diag.match(line)
                if not m or m.group(4) not in CLASSES or (a.only and m.group(4) != a.only):
                    continue
                path = os.path.relpath(os.path.abspath(os.path.join(build, m.group(1))), ROOT).replace("\\", "/")
                if path.startswith(".."):
                    continue  # system and SDK headers
                fn = re.search(r"'(\w+)'", m.group(3))
                key = fn.group(1) if m.group(4) == "implicit-function-declaration" and fn else re.sub(r"'[^']*'", "'…'", m.group(3))
                hits[(m.group(4), key)][(path, int(m.group(2)))].add(os.path.basename(os.path.normpath(build)))
    fatal = 0
    by_class = collections.defaultdict(list)
    for (flag, key), sites in hits.items():
        by_class[flag].append((key, sites))
    for flag in CLASSES:
        if flag not in by_class:
            continue
        name, is_fatal = CLASSES[flag]
        total = sum(len(s) for _, s in by_class[flag])
        fatal += total if is_fatal else 0
        print(f"\n== {name} (-W{flag}): {total} sites{'  [must be 0]' if is_fatal else ''}")
        for key, sites in sorted(by_class[flag], key=lambda ks: -len(ks[1])):
            builds = sorted(set().union(*sites.values()))
            print(f"  {len(sites):5d}  {key}  [{', '.join(builds)}]")
            if a.list or is_fatal:
                for (path, line), bs in sorted(sites.items()):
                    print(f"           {path}:{line}" + ("" if len(bs) == len(a.builds) else f"  [{', '.join(sorted(bs))}]"))
    print(f"\ndiag_census: {sum(len(s) for s in hits.values())} sites in {len(hits)} groups"
          f" over {len(a.builds)} builds; {fatal} in fatal classes")
    return 1 if fatal else 0


if __name__ == "__main__":
    sys.exit(main())
