#!/usr/bin/env python3
"""List host-compile errors not caused by inline asm / nofralloc.
usage: tools/port/nonasm_errors.py [--filter SUBSTR]"""
import os, re, sys, concurrent.futures as cf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import census
ASM_HINTS = re.compile(r"asm|nofralloc|fralloc|frfree|blr|entry|opword", re.I)

def asm_lines(path):
    """Line numbers inside asm functions/blocks (brace-tracked)."""
    src = open(os.path.join(census.ROOT, path), errors="replace").read().splitlines()
    out, depth, inasm = set(), 0, False
    for i, l in enumerate(src, 1):
        if not inasm and census.ASM_RE.search(l + "\n"):
            inasm, depth, seen = True, 0, False
        if inasm:
            out.add(i)
            depth += l.count("{") - l.count("}")
            if "{" in l: seen = True
            if seen and depth <= 0: inasm = False
    return out

def main():
    filt = sys.argv[2] if len(sys.argv) > 2 and sys.argv[1] == "--filter" else None
    files = census.game_files(filt)
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        for path, ok, errs in ex.map(lambda f: census.compile_one(f, "clang"), files):
            if ok: continue
            al = asm_lines(path)
            for e in errs:
                m = re.match(r"(.*?):(\d+):\d+: error: (.*)", e)
                if not m: print(e); continue
                f, ln, msg = m.group(1), int(m.group(2)), m.group(3)
                if f == path and ln in al: continue
                if ASM_HINTS.search(msg) and "static" not in msg: continue
                print(e)
main()
