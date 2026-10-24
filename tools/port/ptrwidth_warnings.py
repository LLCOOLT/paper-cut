#!/usr/bin/env python3
"""List int<->pointer width warnings for a 64-bit host compile of the game.

Compiles every game C file with clang -m64 -fsyntax-only and only these
warnings enabled: -Wint-to-pointer-cast, -Wpointer-to-int-cast,
-Wshorten-64-to-32 (optional, --shorten), -Wint-conversion.

usage: tools/port/ptrwidth_warnings.py [-v] [--filter SUBSTR] [--shorten] [--top N] [--version V]
"""
import argparse, collections, concurrent.futures as cf, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import census

WARN_RE = re.compile(r"^(.*?):(\d+):\d+: warning: .*\[(-W[\w-]+)\]")

_code_cache = {}

def code_lines(path):
    """Set of line numbers inside function bodies (heuristic brace scan)."""
    if path in _code_cache:
        return _code_cache[path]
    lines, depth, in_fn, pending = set(), 0, False, False
    try:
        src = open(os.path.join(census.ROOT, path), errors="replace").read().splitlines()
    except OSError:
        src = []
    for i, l in enumerate(src, 1):
        t = re.sub(r'"(\\.|[^"\\])*"|//.*', '', l)
        if depth == 0 and not in_fn:
            h = t.strip()
            if re.match(r"^[A-Za-z_].*\)\s*\{?\s*$", h) and "=" not in h.split("(")[0] \
                    and not h.startswith(("#", "typedef", "struct", "union", "enum")):
                pending = True
            elif h and not h.startswith(("{", "//", "/*", "*")) and "=" in h:
                pending = False
        for c in t:
            if c == "{":
                if depth == 0 and pending:
                    in_fn, pending = True, False
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    if in_fn:
                        lines.add(i)
                    in_fn = False
        if in_fn:
            lines.add(i)
    _code_cache[path] = lines
    return lines

def run(path, cc, flags):
    cmd = [cc, "-fsyntax-only", "-m64", "-std=gnu99", "-ferror-limit=0", "-Wno-everything",
           *flags, *census.LENIENT, *census.DEFINES, "-DTARGET_PC",
           "-include", "port/include/port_prefix.h", *census.include_flags(path), path]
    r = subprocess.run(cmd, cwd=census.ROOT, capture_output=True, text=True)
    out = []
    for l in r.stderr.splitlines():
        m = WARN_RE.match(l)
        if m and (m.group(3) != "-Wincompatible-pointer-types" or "EvtWord" in l):
            out.append((m.group(1), int(m.group(2)), m.group(3), l))
    return path, out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="store_true", help="print every warning line")
    ap.add_argument("--filter")
    ap.add_argument("--shorten", action="store_true", help="also -Wshorten-64-to-32")
    ap.add_argument("--code", action="store_true",
                    help="only warnings inside function bodies (skip data initializers)")
    ap.add_argument("--evtptr", action="store_true",
                    help="also -Wincompatible-pointer-types lines that mention EvtWord "
                         "(s32* views of script words: wrong stride on 64-bit)")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--cc", default=os.environ.get("CC", "clang"))
    census.portversion.add_argument(ap)
    a = ap.parse_args()
    census.set_version(a.version)
    flags = ["-Wint-to-pointer-cast", "-Wpointer-to-int-cast", "-Wint-conversion"]
    if a.shorten:
        flags.append("-Wshorten-64-to-32")
    if a.evtptr:
        flags.append("-Wincompatible-pointer-types")
    seen = set()
    by_kind, by_file = collections.Counter(), collections.Counter()
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        for path, ws in ex.map(lambda p: run(p, a.cc, flags), census.game_files(a.filter)):
            for f, ln, kind, line in ws:
                if a.code and ln not in code_lines(os.path.relpath(
                        os.path.join(census.ROOT, f), census.ROOT)):
                    continue
                if (f, ln, kind) in seen:  # headers repeat per TU
                    continue
                seen.add((f, ln, kind))
                by_kind[kind] += 1
                by_file[os.path.normpath(f)] += 1
                if a.v:
                    print(line)
    for k, n in by_kind.most_common():
        print(f"{n:7d} {k}")
    print(f"{sum(by_kind.values()):7d} total")
    for f, n in by_file.most_common(a.top):
        print(f"  {n:6d} {f}")

if __name__ == "__main__":
    main()
