#!/usr/bin/env python3
"""Classify calls through recast function pointers (64-bit PC hazards).

The decomp sometimes calls a function through a pointer of a different type
(`((f32(*)(f32, f32, s32, s32, s32))intplGetValue)(...)`) to reproduce MWCC's
register use. On PowerPC that is harmless; on PC it is not always:
  - int/float parameters in different positions: wrong registers on Windows
    x64 (arguments are assigned by position there);
  - a pointer passed as a 32-bit int (or returned as one): truncated;
  - a struct passed by value where the callee takes a pointer (or the
    reverse): wrong on every PC ABI.
Integer parameters that only differ in width/signedness (s16 vs s32, BOOL
vs u8, enums) are safe.

Input: the warnings of an audit build (-DTTYD_PTRWIDTH_WARNINGS=ON includes
-Wcast-function-type-mismatch), e.g.
    ninja -C build-pc-audit -k 0 > audit.log; fncast_audit.py audit.log [-v]
"""
import collections
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def rel_to_root(path):
    """A compiler's path, relative to this checkout's root when it is under it."""
    path = path.replace("\\", "/")
    root = ROOT.replace("\\", "/").rstrip("/") + "/"
    return path[len(root):] if path.lower().startswith(root.lower()) else path

WARN = re.compile(r"^(.*?):(\d+):(\d+): warning: cast from '([^']*)'(?: \(aka '([^']*)'\))? to '([^']*)'(?: \(aka '([^']*)'\))?"
                  r".*\[-Wcast-function-type-(?:mismatch|strict)\]")
INTS = {"s8", "u8", "s16", "u16", "s32", "u32", "int", "unsigned int", "short", "unsigned short", "char",
        "signed char", "unsigned char", "BOOL", "long", "unsigned long", "_Bool"}
FLOATS = {"f32", "f64", "float", "double"}


def split_fn(t):
    """'f32 (*)(s32, s32)' -> ('f32', ['s32', 's32'])"""
    m = re.match(r"^(.*?)\s*\(\*\)\((.*)\)$", t.strip())
    if not m:
        return None
    ret, params = m.group(1).strip(), m.group(2).strip()
    out, depth, cur = [], 0, ""
    for ch in params:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip() and cur.strip() != "void":
        out.append(cur.strip())
    return ret, out


def kind(t, enums):
    t = re.sub(r"\b(const|volatile|struct|enum)\b", "", t).strip()
    if t.endswith("*") or "(*)" in t or t.endswith("]"):
        return "ptr"
    if t in FLOATS:
        return "float"
    if t in INTS or t in enums or t.endswith("Id") or t.endswith("Type") or t.endswith("Flags"):
        return "int"
    if t == "void":
        return "void"
    return "struct:" + t


def classify(src, dst):
    a, b = split_fn(src), split_fn(dst)
    if not a or not b:
        return "unparsed", []
    problems = []
    enums = set()
    ka = [kind(p, enums) for p in a[1]]
    kb = [kind(p, enums) for p in b[1]]
    for i, (x, y) in enumerate(zip(ka, kb)):
        if x == y:
            continue
        if {x, y} == {"int", "float"}:
            problems.append(f"arg{i}: int/float swap (Win64)")
        elif "ptr" in (x, y) and ("int" in (x, y)):
            problems.append(f"arg{i}: pointer as 32-bit int")
        elif x.startswith("struct:") or y.startswith("struct:"):
            problems.append(f"arg{i}: struct by value vs {y if x.startswith('struct:') else x}")
        else:
            problems.append(f"arg{i}: {x} vs {y}")
    if len(ka) < len(kb):
        problems.append(f"call passes {len(kb) - len(ka)} extra arg(s)")
    elif len(ka) > len(kb):
        problems.append(f"callee reads {len(ka) - len(kb)} missing arg(s)")
    ra, rb = kind(a[0], enums), kind(b[0], enums)
    if ra != rb and not (ra == "void" or rb == "void"):
        if "ptr" in (ra, rb):
            problems.append("return: pointer as 32-bit int")
        elif {ra, rb} == {"int", "float"}:
            problems.append("return: int/float")
    return ("unsafe" if problems else "safe"), problems


def main():
    verbose = "-v" in sys.argv
    path = [a for a in sys.argv[1:] if not a.startswith("-")][0]
    groups = collections.defaultdict(list)
    for line in open(path, encoding="utf-8", errors="replace"):
        m = WARN.match(line.strip())
        if not m:
            continue
        f = rel_to_root(m.group(1))
        src = m.group(5) or m.group(4)
        dst = m.group(7) or m.group(6)
        groups[(src, dst)].append(f"{f}:{m.group(2)}")
    unsafe = 0
    for (src, dst), sites in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        verdict, why = classify(src, dst)
        sites = sorted(set(sites))
        if verdict != "safe":
            unsafe += len(sites)
        if verdict != "safe" or verbose:
            print(f"{verdict:8s} {len(sites):4d}  {src}  ==>  {dst}")
            for w in why:
                print(f"           - {w}")
            if verbose:
                for s in sites[:8]:
                    print(f"           {s}")
    print(f"unsafe call sites: {unsafe}")


if __name__ == "__main__":
    main()
