#!/usr/bin/env python3
"""PC-port progress census.

Reports (1) remaining Metrowerks inline-asm stopgaps and (2) which C files
compile on the host compiler with the PC prefix header.

usage: tools/port/census.py [asm|compile] [-v] [--filter SUBSTR] [--version V]
"""
import argparse, collections, concurrent.futures as cf, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portversion

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME_DIRS = ["src", "rels"]
# MWCC accepts these; real fixes are tracked separately.
LENIENT = ["-Wno-error=implicit-function-declaration",
           "-Wno-error=incompatible-function-pointer-types",
           "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types"]
ARCH = ["-m64", "-fPIC"] if os.environ.get("PORT_ARCH") == "64" else ["-m32"]
# Mirrors cflags_base in configure.py (-char unsigned) plus the version flags.
COMMON_DEFINES = ["-funsigned-char", "-DSDK_REVISION=1", "-DNDEBUG=1", "-DNONMATCHING"]
VERSION = portversion.default()
DEFINES = COMMON_DEFINES + portversion.defines(VERSION)

def set_version(version):
    """Select the game version (flags and source list) for this run."""
    global VERSION
    VERSION = version
    DEFINES[:] = COMMON_DEFINES + portversion.defines(version)
ASM_RE = re.compile(r"^\s*(?:static\s+)?asm\b|\basm\s*\{", re.M)

def game_files(filt=None):
    """The game/REL sources the selected version builds (as CMake does)."""
    return portversion.sources(VERSION, filt)

def include_flags(path):
    sdk = "extern/aurora/include" if os.environ.get("PORT_SDK") == "aurora" else "libs/dolsdk2004/include"
    flags = ["-Iport/include", "-Iinclude", f"-I{sdk}",
             "-Ilibs/musyx/include"]
    if os.environ.get("PORT_SDK") == "aurora":
        flags.append("-DTTYD_BACKEND_AURORA=1")  # as CMakeLists.txt does
    parts = path.replace("\\", "/").split("/")
    if parts[0] == "rels":
        flags.append(f"-Irels/{parts[1]}/include")
    return flags

def compile_one(path, cc):
    cmd = [cc, "-fsyntax-only", *ARCH, "-w", "-std=gnu99", "-ferror-limit=0", *LENIENT,
           *DEFINES, "-include", "port/include/port_prefix.h", *include_flags(path), path]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if ": error:" in l or ": fatal error:" in l]
    return path, r.returncode == 0, errs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["asm", "compile"])
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--filter")
    ap.add_argument("--cc", default=os.environ.get("CC", "clang"))
    portversion.add_argument(ap)
    a = ap.parse_args()
    set_version(a.version)
    files = game_files(a.filter)
    if a.mode == "asm":
        total, per = 0, []
        for f in files:
            n = len(ASM_RE.findall(open(os.path.join(ROOT, f), errors="replace").read()))
            if n: per.append((n, f)); total += n
        for n, f in sorted(per, reverse=True):
            print(f"{n:5d}  {f}")
        print(f"total asm stopgaps: {total} in {len(per)} files")
        return
    ok, kinds, failing = 0, collections.Counter(), []
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        for path, good, errs in ex.map(lambda f: compile_one(f, a.cc), files):
            if good: ok += 1
            else: failing.append((len(errs), path))
            for e in errs:
                kinds[re.sub(r"'[^']*'", "'X'", re.split(r": (?:fatal )?error:", e, maxsplit=1)[1].strip())] += 1
    if a.v:
        for n, p in sorted(failing, reverse=True): print(f"{n:5d}  {p}")
    print("top error kinds:")
    for k, n in kinds.most_common(25): print(f"{n:7d}  {k}")
    print(f"host-compiling: {ok}/{len(files)} files")

if __name__ == "__main__":
    main()
