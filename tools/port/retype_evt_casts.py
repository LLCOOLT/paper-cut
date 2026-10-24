#!/usr/bin/env python3
"""Inside every EvtWord array initializer, rewrite `(s32)` casts (pointers to
functions, strings, data) to `(EvtWord)`. Identical for MWCC (EvtWord is s32);
on 64-bit PC the cast no longer truncates a pointer."""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEAD = re.compile(r"\bEvtWord\s*\(?\s*\w+\s*\)?\s*\[[^\]]*\]\s*=\s*\{")

def main():
    changed = casts = 0
    for d in ["src", "rels"]:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            for f in fns:
                if not f.endswith(".c"):
                    continue
                p = os.path.join(dp, f)
                t = open(p, encoding="utf-8", errors="surrogateescape").read()
                out, pos = [], 0
                for m in HEAD.finditer(t):
                    end = t.index("};", m.end())
                    body = t[m.end():end]
                    casts += body.count("(s32)")
                    out.append(t[pos:m.end()] + body.replace("(s32)", "(EvtWord)"))
                    pos = end
                n = "".join(out) + t[pos:]
                if n != t:
                    open(p, "w", encoding="utf-8", errors="surrogateescape").write(n)
                    changed += 1
    print(f"rewrote {casts} casts in {changed} files")

if __name__ == "__main__":
    main()
