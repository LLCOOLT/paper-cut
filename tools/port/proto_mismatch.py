#!/usr/bin/env python3
"""Functions declared with a different return type than they are defined with.

The decomp's files often declare the functions they call themselves; on the
GameCube a mismatch can be harmless (a void function "returning" whatever its
last call left in r3, an s32 holding a pointer), on 64-bit PC it is a wrong
value: a void function's "result" is garbage, an int truncates a pointer.
Reports each function whose definition and some declaration disagree in the
return type's kind: void / pointer / integer / float.

  proto_mismatch.py [--all]   (default: only those that give the caller a wrong
                              value: declared with a value but defined void,
                              pointer vs integer, float vs integer)
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = ["src", "rels", "libs/musyx/src", "include"]
TYPE = r"(?:(?:static|extern|inline|const|volatile|unsigned|signed|struct|enum)\s+)*[A-Za-z_]\w*(?:\s*\*+\s*|\s+)"
DEF_RE = re.compile(r"^(" + TYPE + r")([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{", re.M)
DECL_RE = re.compile(r"^\s*(" + TYPE + r")([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*;", re.M)
SKIP = {"if", "while", "for", "switch", "return", "sizeof", "USER_FUNC", "EVT_BEGIN", "else", "case", "goto"}
FLOATS = {"f32", "f64", "float", "double"}


def kind(ret):
    r = re.sub(r"\b(static|extern|inline|const|volatile|struct|enum)\b", "", ret).strip()
    if "*" in r or re.search(r"Ptr$", r):  # TEXDescriptorPtr & co.: pointer typedefs
        return "pointer"
    base = r.split()[-1] if r.split() else r
    if base == "void":
        return "void"
    if base in FLOATS:
        return "float"
    if base in ("EvtWord", "AddrWord", "intptr_t", "uintptr_t", "size_t", "ssize_t"):
        return "word"
    return "int"


def strip(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return pc_only(text)


def pc_only(text):
    """The text as the PC build sees it: the GameCube-only side of
    #ifdef/#ifndef TARGET_PC blocks blanked (lines kept, for line numbers).
    Other conditions are kept whole."""
    out = []
    stack = []  # per open #if: (tracks TARGET_PC, PC side active now)
    for line in text.split("\n"):
        t = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", t)
        if m:
            d, rest = m.group(1), m.group(2)
            if d in ("ifdef", "ifndef", "if"):
                pc = "TARGET_PC" in rest and not re.search(r"&&|\|\|", rest)
                neg = d == "ifndef" or re.search(r"!\s*defined\s*\(?\s*TARGET_PC", rest)
                stack.append((pc, not neg if pc else True))
            elif d == "else" and stack:
                pc, active = stack[-1]
                stack[-1] = (pc, not active if pc else True)
            elif d == "elif" and stack:
                pc, active = stack[-1]
                stack[-1] = (pc, False if pc else True)
            elif d == "endif" and stack:
                stack.pop()
            out.append("")
            continue
        out.append(line if all(a for _, a in stack) else "")
    return "\n".join(out)


def uses_value(text, name):
    """A call of name whose result is used (assigned, tested, passed on)."""
    for m in re.finditer(r"(^[^\n]*)\b" + re.escape(name) + r"\s*\(", text, re.M):
        before = m.group(1).strip()
        if before == "" or before.endswith((";", "{", "}")):
            continue
        if re.match(r"^(extern\s+)?[\w\s\*]+$", before) and not re.search(r"(=|return|if|while|\(|&&|\|\||\?|,)", before):
            continue  # a declaration
        return True
    return False


def main():
    everything = "--all" in sys.argv
    defs = defaultdict(list)
    decls = defaultdict(list)
    for d in DIRS:
        for dp, _, fs in os.walk(os.path.join(ROOT, d)):
            for f in fs:
                if not f.endswith((".c", ".h")):
                    continue
                p = os.path.join(dp, f)
                t = strip(open(p, encoding="utf-8", errors="replace").read())
                rel = os.path.relpath(p, ROOT)
                for m in DEF_RE.finditer(t):
                    name = m.group(2)
                    if name in SKIP or "static" in m.group(1):
                        continue
                    defs[name].append((kind(m.group(1)), m.group(1).strip(), rel, t.count("\n", 0, m.start()) + 1))
                for m in DECL_RE.finditer(t):
                    name = m.group(2)
                    first = m.group(1).split()[0] if m.group(1).split() else ""
                    if name in SKIP or "typedef" in m.group(0) or first in SKIP:
                        continue
                    decls[name].append((kind(m.group(1)), m.group(1).strip(), rel, t.count("\n", 0, m.start()) + 1))
    texts = {}
    found = 0
    for name in sorted(defs):
        dk = {k for k, *_ in defs[name]}
        if len(dk) != 1:
            continue  # version-conditional definitions
        dkind = next(iter(dk))
        for k, ret, rel, line in decls.get(name, []):
            if k == dkind:
                continue
            # harmful: the caller uses a value the definition does not return
            # (void), or reads it from the wrong register or at the wrong width
            serious = (dkind == "void" and k != "void") or {k, dkind} in ({"pointer", "int"}, {"word", "int"}) or \
                ("float" in (k, dkind) and k != "void" and dkind != "void")
            if not serious and not everything:
                continue
            if not everything:
                if rel not in texts:
                    texts[rel] = strip(open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace").read())
                if not uses_value(texts[rel], name):
                    continue  # the caller ignores the result
            d = defs[name][0]
            print(f"{rel}:{line}: {name} declared '{ret}' ({k}); defined '{d[1]}' ({dkind}) at {d[2]}:{d[3]}")
            found += 1
    print(f"proto_mismatch: {found} mismatched declarations")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
