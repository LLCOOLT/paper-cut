#!/usr/bin/env python3
"""Add the missing declarations behind implicit function calls on PC.

MWCC accepts calls to undeclared functions (implicit 'int f()'), and on the
GameCube that is harmless: pointers and ints are both 32 bits in r3. On the
64-bit PC port an implicit declaration truncates returned pointers and
EvtWords and passes ints where the callee reads 64-bit registers. For each
file with -Wimplicit-function-declaration warnings (audit build), this finds
the header that declares each function and includes it under TARGET_PC, so
the GameCube build (where a new prototype could change argument promotion)
is unchanged.

usage: implicit_fix.py AUDIT_LOG [--apply]
"""
import collections
import glob
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WARN = re.compile(r"^(.*?):(\d+):\d+: warning: (?:call to undeclared function|implicit declaration of function) '(\w+)'")


def rel(p):
    return re.sub(r"^.*?ttyd-port[/\\]", "", p).replace("\\", "/")


def header_index():
    idx = collections.defaultdict(list)
    heads = glob.glob(os.path.join(ROOT, "include", "**", "*.h"), recursive=True)
    heads += glob.glob(os.path.join(ROOT, "rels", "*", "include", "**", "*.h"), recursive=True)
    decl = re.compile(r"^(?:extern\s+)?[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{]*\)\s*;", re.M)
    for h in heads:
        r = os.path.relpath(h, ROOT).replace("\\", "/")
        for m in decl.finditer(open(h, encoding="utf-8", errors="replace").read()):
            idx[m.group(1)].append(r)
    return idx


def include_name(header, src):
    """How src includes header: relative to include/ or the REL's include/."""
    if header.startswith("include/"):
        return header[len("include/"):]
    m = re.match(r"rels/(\w+)/include/(.*)$", header)
    if m and src.startswith(f"rels/{m.group(1)}/"):
        return m.group(2)
    return None


def definition_index():
    """Prototypes of non-static functions from their definitions in src/ and rels/."""
    idx = {}
    defn = re.compile(r"^((?!static\b)[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{)]*\))\s*\{?\s*$", re.M)
    for root in ("src", "rels"):
        for p in glob.glob(os.path.join(ROOT, root, "**", "*.c"), recursive=True):
            for m in defn.finditer(open(p, encoding="utf-8", errors="replace").read()):
                if m.group(2) not in idx and not m.group(1).startswith(("return", "else", "if ", "USER_FUNC")):
                    idx[m.group(2)] = re.sub(r"\s+", " ", m.group(1)).strip() + ";"
    return idx


BLOCK_HEAD = "#ifdef TARGET_PC /* declarations the GameCube code calls implicitly */\n"
INNER_BLOCK = re.compile(re.escape(BLOCK_HEAD) + r"((?:(?!#if)[^\n]*\n)*?)#endif\n")


def insert_block(path, lines):
    """Merge lines with the file's existing blocks into one block, placed after
    the leading #includes at preprocessor depth 0 (never inside a version
    #ifdef) and before the first line of code."""
    text = open(path, encoding="utf-8").read()
    old = []
    while True:  # innermost first: earlier runs could nest blocks
        m = INNER_BLOCK.search(text)
        if not m:
            break
        old += m.group(1).splitlines()
        text = text[:m.start()] + text[m.end():]
    merged = []
    for line in old + lines:
        if line.strip() and line not in merged:
            merged.append(line)
    depth, braces, pos, region_include = 0, 0, 0, False
    for m in re.finditer(r"^.*\n", text, re.M):
        line = m.group(0).strip()
        if re.match(r"#\s*if", line):
            depth += 1
        elif re.match(r"#\s*endif", line):
            depth -= 1
            if depth == 0 and braces == 0 and region_include:
                pos = m.end()  # after a conditional group of #includes
            if depth == 0:
                region_include = False
        elif line.startswith("#include"):
            region_include = depth > 0
            if depth == 0 and braces == 0:
                pos = m.end()
        elif depth == 0 and braces == 0 and (line.endswith("{") or line.startswith(("{", "EVT_BEGIN", "USER_FUNC("))) \
                and not line.startswith(("typedef", "struct", "union", "enum")):
            break  # first function or script body: declarations above it may need the block's types
        if not line.startswith("#"):
            code = re.sub(r'"(?:\\.|[^"\\])*"|//.*$', "", line)
            braces += code.count("{") - code.count("}")
    text = text[:pos] + BLOCK_HEAD + "".join(f"{l}\n" for l in merged) + "#endif\n" + text[pos:]
    write(path, text)


def write(path, text):
    """Atomic replace with retries (Windows sometimes fails a write with EINVAL
    while another process has the file open)."""
    for _ in range(20):
        try:
            with open(path + ".tmp", "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            os.replace(path + ".tmp", path)
            return
        except OSError:
            time.sleep(0.2)
    raise OSError(f"could not write {path}")


def main():
    if sys.argv[1] == "--normalize":  # re-place and merge the blocks already in the tree
        for root in ("src", "rels"):
            for p in glob.glob(os.path.join(ROOT, root, "**", "*.c"), recursive=True):
                if BLOCK_HEAD in open(p, encoding="utf-8").read():
                    insert_block(p, [])
        return
    log = sys.argv[1]
    apply = "--apply" in sys.argv
    wanted = collections.defaultdict(set)
    for line in open(log, encoding="utf-8", errors="replace"):
        m = WARN.match(line.strip())
        if m:
            wanted[rel(m.group(1))].add(m.group(3))
    idx = header_index()
    defs = definition_index()
    missing = collections.Counter()
    for src, fns in sorted(wanted.items()):
        if not src.endswith(".c") or not os.path.exists(os.path.join(ROOT, src)):
            continue
        incs, protos = [], []
        for fn in sorted(fns):
            cands = [include_name(h, src) for h in idx.get(fn, [])]
            cands = [c for c in cands if c]
            if cands:
                if cands[0] not in incs:
                    incs.append(cands[0])
            elif fn in defs:
                protos.append(defs[fn])
            else:
                missing[fn] += 1
        if not incs and not protos:
            continue
        print(f"{src}: {', '.join(incs + protos)}")
        if apply:
            insert_block(os.path.join(ROOT, src), [f'#include "{i}"' for i in incs] + protos)
    if missing:
        print("not declared in any header:", ", ".join(f"{f}({n})" for f, n in missing.most_common()))


if __name__ == "__main__":
    main()
