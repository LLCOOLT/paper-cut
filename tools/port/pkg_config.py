#!/usr/bin/env python3
"""A small pkg-config over the ARM64 sysroot (tools/port/arm64_sysroot.py),
for cross-compiling on a host that has none (Windows): what CMake's
FindPkgConfig asks of it (SDL3's driver checks) and no more.

  pkg_config.py --sysroot DIR [--version | --exists | --modversion |
                --cflags[-only-I|-only-other] | --libs[-only-L|-only-l|-only-other] |
                --variable=NAME] [--print-errors] [--short-errors] [--static] MODULE[ OP VERSION]...

The toolchain (cmake/toolchains/linux-arm64.cmake) writes a wrapper beside
the build that calls this with the sysroot. Paths in -I and -L get the
sysroot put in front, as PKG_CONFIG_SYSROOT_DIR does.
"""
import os
import re
import sys

DIRS = ("usr/lib/aarch64-linux-gnu/pkgconfig", "usr/lib/pkgconfig", "usr/share/pkgconfig")


def find_pc(sysroot, name):
    for d in DIRS:
        p = os.path.join(sysroot, *d.split("/"), name + ".pc")
        if os.path.isfile(p):
            return p
    return None


def parse(path, sysroot):
    variables = {"pc_sysrootdir": sysroot.replace("\\", "/")}
    fields = {}

    def expand(s):
        for _ in range(16):
            n = re.sub(r"\$\{(\w+)\}", lambda m: variables.get(m.group(1), ""), s)
            if n == s:
                break
            s = n
        return s

    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"^([\w.]+)\s*=\s*(.*)$", line)
        if m:
            variables[m.group(1)] = expand(m.group(2))
            continue
        m = re.match(r"^([\w.]+)\s*:\s*(.*)$", line)
        if m:
            fields[m.group(1).lower()] = expand(m.group(2))
    return variables, fields


def vkey(v):
    return [int(x) if x.isdigit() else x for x in re.findall(r"\d+|[a-zA-Z]+", v)]


def satisfies(have, op, want):
    a, b = vkey(have), vkey(want)
    try:
        return {"=": a == b, "==": a == b, "!=": a != b, ">=": a >= b, "<=": a <= b, ">": a > b, "<": a < b}[op]
    except TypeError:
        return True


def specs(args):
    """The modules asked for: (name, op, version) from 'a >= 1', 'a>=1', 'a, b'."""
    text = " ".join(args).replace(",", " ")
    return [(m.group(1), m.group(2), m.group(3)) for m in
            re.finditer(r"([A-Za-z0-9_.+-]+)(?:\s*(<=|>=|==|!=|=|<|>)\s*([A-Za-z0-9_.~+-]+))?", text)]


def main(argv):
    sysroot, mode, var, static, errors = None, "exists", None, False, False
    outputs = []
    mods = []
    it = iter(argv)
    for a in it:
        if a == "--sysroot":
            sysroot = next(it)
        elif a.startswith("--sysroot="):
            sysroot = a.split("=", 1)[1]
        elif a == "--version":
            print("0.29.2")
            return 0
        elif a.startswith("--variable="):
            mode, var = "variable", a.split("=", 1)[1]
        elif a == "--static":
            static = True
        elif a == "--print-errors":
            errors = True
        elif a in ("--short-errors", "--silence-errors"):
            pass
        elif a.startswith(("--cflags", "--libs")):
            outputs.append(a[2:])
            mode = "flags"
        elif a.startswith("--"):
            mode = a[2:]
        else:
            mods.append(a)
    sysroot = os.path.abspath(sysroot or os.environ.get("PKG_CONFIG_SYSROOT_DIR", "/"))
    wanted = specs(mods)
    seen, order = set(), []

    def load(name, op=None, ver=None):
        p = find_pc(sysroot, name)
        if not p:
            if errors:
                print(f"Package {name} was not found in the pkg-config search path.", file=sys.stderr)
            return None
        v, f = parse(p, sysroot)
        if op and not satisfies(f.get("version", "0"), op, ver):
            if errors:
                print(f"Requested '{name} {op} {ver}' but version of {name} is {f.get('version')}", file=sys.stderr)
            return None
        return v, f

    def walk(name):
        if name in seen:
            return True
        seen.add(name)
        got = load(name)
        if not got:
            return False
        order.append((name, got))
        reqs = got[1].get("requires", "") + ("," + got[1].get("requires.private", "") if static else "")
        for n, _, _ in specs([reqs]):
            if not walk(n):
                return False
        return True

    top = []
    for name, op, ver in wanted:
        got = load(name, op, ver)
        if not got:
            return 1
        top.append((name, got))
    if mode == "exists":
        return 0 if all(walk(n) for n, _ in top) else 1
    if mode == "modversion":
        for _, (v, f) in top:
            print(f.get("version", ""))
        return 0
    if mode == "variable":
        for _, (v, f) in top:
            val = v.get(var, "")
            if var in ("prefix", "includedir", "libdir", "pkgdatadir", "datadir") and val.startswith("/"):
                val = sysroot.replace("\\", "/") + val
            print(val)
        return 0
    for n, _ in top:
        if not walk(n):
            return 1
    cflags, libs = [], []
    for _, (v, f) in order:
        cflags += f.get("cflags", "").split()
        libs += f.get("libs", "").split() + (f.get("libs.private", "").split() if static else [])
    root = sysroot.replace("\\", "/")

    def rooted(flag, prefix):
        path = flag[len(prefix):]
        return prefix + root + path if path.startswith("/") else flag

    cflags = [rooted(c, "-I") if c.startswith("-I") else c for c in cflags]
    libs = [rooted(l, "-L") if l.startswith("-L") else l for l in libs]
    table = {
        "cflags": cflags,
        "cflags-only-I": [c for c in cflags if c.startswith("-I")],
        "cflags-only-other": [c for c in cflags if not c.startswith("-I")],
        "libs": libs,
        "libs-only-L": [l for l in libs if l.startswith("-L")],
        "libs-only-l": [l for l in libs if l.startswith("-l")],
        "libs-only-other": [l for l in libs if not l.startswith(("-l", "-L"))],
    }
    pick = []
    for o in outputs:
        if o not in table:
            print(f"pkg_config: --{o} is not supported", file=sys.stderr)
            return 1
        pick += table[o]
    # without the system's own directories, as pkg-config leaves them out
    pick = [p for p in pick if p not in ("-I" + root + "/usr/include", "-L" + root + "/usr/lib")]
    seen_flags, out = set(), []
    for p in pick:
        if p not in seen_flags:
            seen_flags.add(p)
            out.append(p)
    print(" ".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
