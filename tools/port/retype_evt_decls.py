#!/usr/bin/env python3
"""Retype declarations of event scripts from s32 to EvtWord.

Every array defined with EVT_BEGIN(name) is an EvtWord array; this rewrites
`s32 name[...]` / `s32 (name)[...]` / `s32* name` declarations of those names
(forward declarations, externs, header prototypes) to EvtWord. EvtWord is
typedef'd to s32 for the GameCube, so the matching build is unaffected.
"""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = ["src", "rels", "include"]

def files():
    for d in DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for f in fns:
                if f.endswith((".c", ".h")):
                    yield os.path.join(dp, f)

def main():
    names = set()
    texts = {}
    for p in files():
        t = open(p, encoding="utf-8", errors="surrogateescape").read()
        texts[p] = t
        names |= set(re.findall(r"EVT_BEGIN\(\s*(\w+)\s*\)", t))
    pat = re.compile(r"\bs32(\s*\(?\s*)(\w+)(\s*\)?\s*\[)")
    changed = 0
    for p, t in texts.items():
        def repl(m):
            return "EvtWord" + m.group(1) + m.group(2) + m.group(3) if m.group(2) in names else m.group(0)
        n = pat.sub(repl, t)
        if n != t:
            open(p, "w", encoding="utf-8", errors="surrogateescape").write(n)
            changed += 1
    print(f"{len(names)} scripts; rewrote declarations in {changed} files")

if __name__ == "__main__":
    main()
